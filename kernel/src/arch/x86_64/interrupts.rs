//! x86-64 external interrupt entry and fixed vector policy.

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64};
use core::sync::atomic::{AtomicU8, Ordering};

use super::{idt, serial, timer};
use crate::task::TaskId;

#[allow(unsafe_code)]
#[unsafe(no_mangle)]
/// Test-only pre-call stack-alignment diagnostic.
pub static INTERRUPT_CALL_ALIGNMENT: AtomicU8 = AtomicU8::new(u8::MAX);

/// First legacy PIC vector.
pub const PIC_VECTOR_START: u8 = 0x20;
/// Last legacy PIC vector.
pub const PIC_VECTOR_END: u8 = 0x2f;
/// BSP local APIC timer vector.
pub const TIMER_VECTOR: u8 = 0x40;
/// BSP local APIC spurious vector.
pub const SPURIOUS_VECTOR: u8 = 0xff;
/// Ring-0 interrupt gate with IST zero.
pub const EXTERNAL_GATE_IST: u8 = 0;
/// Dedicated ring-0 diagnostic software interrupt vector.
pub const PREEMPTION_TEST_VECTOR: u8 = 0x41;

const MAX_PUBLISHED_STACKS: usize = crate::task::MAX_TASKS;

struct PublishedTaskStack {
    active: AtomicBool,
    generation: AtomicU32,
    start: AtomicU64,
    end: AtomicU64,
}
impl PublishedTaskStack {
    const EMPTY: Self = Self {
        active: AtomicBool::new(false),
        generation: AtomicU32::new(0),
        start: AtomicU64::new(0),
        end: AtomicU64::new(0),
    };
}
static PUBLISHED_STACKS: [PublishedTaskStack; MAX_PUBLISHED_STACKS] =
    [const { PublishedTaskStack::EMPTY }; MAX_PUBLISHED_STACKS];

/// Errors from stack-derived task attribution.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AttributionError {
    /// The address is not canonical.
    NoncanonicalRsp,
    /// No active published stack contains the address.
    NoMatch,
    /// More than one active stack contains the address.
    MultipleMatch,
    /// A publication changed while it was read.
    UnstablePublication,
    /// The mirrored generation was invalid.
    InvalidGeneration,
    /// The frame does not fit entirely in the selected stack.
    FrameOutsideStack,
}

/// A copy of all general-purpose registers saved by an interrupt entry.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SavedGeneralRegisters {
    /// RAX.
    pub rax: u64,
    /// RBX.
    pub rbx: u64,
    /// RCX.
    pub rcx: u64,
    /// RDX.
    pub rdx: u64,
    /// RSI.
    pub rsi: u64,
    /// RDI.
    pub rdi: u64,
    /// RBP.
    pub rbp: u64,
    /// R8.
    pub r8: u64,
    /// R9.
    pub r9: u64,
    /// R10.
    pub r10: u64,
    /// R11.
    pub r11: u64,
    /// R12.
    pub r12: u64,
    /// R13.
    pub r13: u64,
    /// R14.
    pub r14: u64,
    /// R15.
    pub r15: u64,
}

/// Diagnostic copy of the most recently dispatched interrupt.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct InterruptedTaskSnapshot {
    /// Attributed generation-tagged task identity.
    pub task_id: TaskId,
    /// Address of the copied live frame.
    pub frame_pointer: u64,
    /// Address selected for return.
    pub returned_frame_pointer: u64,
    /// Derived interrupted RSP.
    pub interrupted_rsp: u64,
    /// Interrupted RIP.
    pub rip: u64,
    /// Interrupted CS.
    pub cs: u64,
    /// Interrupted RFLAGS.
    pub rflags: u64,
    /// External vector.
    pub vector: u8,
    /// Monotonic snapshot sequence.
    pub sequence: u64,
    /// Complete saved GPRs.
    pub registers: SavedGeneralRegisters,
}
#[allow(unsafe_code)]
struct SnapshotCell(UnsafeCell<InterruptedTaskSnapshot>);
// SAFETY: the sequence counter makes the bounded copy race-safe on the BSP.
#[allow(unsafe_code)]
unsafe impl Sync for SnapshotCell {}
static SNAPSHOT: SnapshotCell = SnapshotCell(UnsafeCell::new(InterruptedTaskSnapshot {
    task_id: TaskId {
        slot: 0,
        generation: 1,
    },
    frame_pointer: 0,
    returned_frame_pointer: 0,
    interrupted_rsp: 0,
    rip: 0,
    cs: 0,
    rflags: 0,
    vector: 0,
    sequence: 0,
    registers: SavedGeneralRegisters {
        rax: 0,
        rbx: 0,
        rcx: 0,
        rdx: 0,
        rsi: 0,
        rdi: 0,
        rbp: 0,
        r8: 0,
        r9: 0,
        r10: 0,
        r11: 0,
        r12: 0,
        r13: 0,
        r14: 0,
        r15: 0,
    },
}));
static SNAPSHOT_SEQUENCE: AtomicU64 = AtomicU64::new(0);
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
/// Set while the dedicated real-timer preservation helper is active.
pub static PREEMPTION_TIMER_PHASE: AtomicBool = AtomicBool::new(false);
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
/// Set by the timer dispatcher after observing the active preservation helper.
pub static PREEMPTION_TIMER_OBSERVED: AtomicBool = AtomicBool::new(false);

/// Publishes one task stack with release ordering.
pub fn publish_task_stack(id: TaskId, start: u64, end: u64) -> Result<(), AttributionError> {
    if id.generation() == 0 || id.slot() >= MAX_PUBLISHED_STACKS || start >= end {
        return Err(AttributionError::InvalidGeneration);
    }
    let slot = &PUBLISHED_STACKS[id.slot()];
    slot.active.store(false, Ordering::Release);
    slot.start.store(start, Ordering::Relaxed);
    slot.end.store(end, Ordering::Relaxed);
    slot.generation.store(id.generation(), Ordering::Relaxed);
    slot.active.store(true, Ordering::Release);
    Ok(())
}
/// Removes a task stack publication before slot reuse or reclamation.
pub fn unpublish_task_stack(slot_index: usize) {
    if let Some(slot) = PUBLISHED_STACKS.get(slot_index) {
        slot.active.store(false, Ordering::Release);
    }
}
/// Attributes an interrupted stack pointer using only atomic publications.
pub fn attribute_interrupted_rsp(interrupted_rsp: u64) -> Result<TaskId, AttributionError> {
    if !super::paging::is_canonical(interrupted_rsp) {
        return Err(AttributionError::NoncanonicalRsp);
    }
    let mut found = None;
    for slot_index in 0..MAX_PUBLISHED_STACKS {
        let slot = &PUBLISHED_STACKS[slot_index];
        let active = slot.active.load(Ordering::Acquire);
        if !active {
            continue;
        }
        let start = slot.start.load(Ordering::Relaxed);
        let end = slot.end.load(Ordering::Relaxed);
        let generation = slot.generation.load(Ordering::Relaxed);
        let stable = slot.active.load(Ordering::Acquire)
            && start == slot.start.load(Ordering::Relaxed)
            && end == slot.end.load(Ordering::Relaxed)
            && generation == slot.generation.load(Ordering::Relaxed);
        if !stable {
            return Err(AttributionError::UnstablePublication);
        }
        if start < interrupted_rsp && interrupted_rsp <= end {
            let id = TaskId::new(slot_index as u8, generation)
                .map_err(|_| AttributionError::InvalidGeneration)?;
            if found.replace(id).is_some() {
                return Err(AttributionError::MultipleMatch);
            }
        }
    }
    found.ok_or(AttributionError::NoMatch)
}

fn save_registers(frame: &KernelInterruptFrame) -> SavedGeneralRegisters {
    SavedGeneralRegisters {
        rax: frame.rax,
        rbx: frame.rbx,
        rcx: frame.rcx,
        rdx: frame.rdx,
        rsi: frame.rsi,
        rdi: frame.rdi,
        rbp: frame.rbp,
        r8: frame.r8,
        r9: frame.r9,
        r10: frame.r10,
        r11: frame.r11,
        r12: frame.r12,
        r13: frame.r13,
        r14: frame.r14,
        r15: frame.r15,
    }
}
#[allow(unsafe_code)]
fn record_snapshot(frame: &KernelInterruptFrame, returned: *mut KernelInterruptFrame, id: TaskId) {
    let sequence = SNAPSHOT_SEQUENCE
        .fetch_add(1, Ordering::AcqRel)
        .saturating_add(1);
    // SAFETY: this is the sole BSP writer; the sequence brackets the copy.
    unsafe {
        *SNAPSHOT.0.get() = InterruptedTaskSnapshot {
            task_id: id,
            frame_pointer: frame as *const _ as u64,
            returned_frame_pointer: returned as u64,
            interrupted_rsp: frame.interrupted_rsp(),
            rip: frame.rip,
            cs: frame.cs,
            rflags: frame.rflags,
            vector: frame.vector as u8,
            sequence,
            registers: save_registers(frame),
        };
    }
    SNAPSHOT_SEQUENCE.store(sequence.saturating_add(1), Ordering::Release);
}
/// Reads a stable diagnostic snapshot, retrying a bounded number of times.
#[must_use]
#[allow(unsafe_code)]
pub fn snapshot() -> Option<InterruptedTaskSnapshot> {
    for _ in 0..3 {
        let before = SNAPSHOT_SEQUENCE.load(Ordering::Acquire);
        if before == 0 || before % 2 != 0 {
            continue;
        }
        // SAFETY: sequence validation rejects a concurrent interrupt write.
        let copy = unsafe { *SNAPSHOT.0.get() };
        if before == SNAPSHOT_SEQUENCE.load(Ordering::Acquire) {
            return Some(copy);
        }
    }
    None
}
/// Starts the bounded real-timer preservation observation phase.
pub fn begin_timer_test() {
    PREEMPTION_TIMER_OBSERVED.store(false, Ordering::Release);
    PREEMPTION_TIMER_PHASE.store(true, Ordering::Release);
}
/// Ends the bounded real-timer preservation observation phase.
pub fn end_timer_test() {
    PREEMPTION_TIMER_PHASE.store(false, Ordering::Release);
}
/// Returns whether a real timer was observed during the phase.
#[must_use]
pub fn timer_test_observed() -> bool {
    PREEMPTION_TIMER_OBSERVED.load(Ordering::Acquire)
}

/// The common frame built by the external-interrupt stubs.
#[repr(C)]
pub struct KernelInterruptFrame {
    /// Saved registers in assembly order.
    pub rax: u64,
    /// Saved RBX.
    pub rbx: u64,
    /// Saved RCX.
    pub rcx: u64,
    /// Saved RDX.
    pub rdx: u64,
    /// Saved RSI.
    pub rsi: u64,
    /// Saved RDI.
    pub rdi: u64,
    /// Saved base pointer.
    pub rbp: u64,
    /// Saved extended registers.
    pub r8: u64,
    /// Saved R9.
    pub r9: u64,
    /// Saved R10.
    pub r10: u64,
    /// Saved R11.
    pub r11: u64,
    /// Saved R12.
    pub r12: u64,
    /// Saved R13.
    pub r13: u64,
    /// Saved R14.
    pub r14: u64,
    /// Saved R15.
    pub r15: u64,
    /// Vector pushed by the stub.
    pub vector: u64,
    /// Synthetic zero error code.
    pub error_code: u64,
    /// CPU-pushed return frame.
    pub rip: u64,
    /// Saved code selector.
    pub cs: u64,
    /// Saved flags.
    pub rflags: u64,
}

/// Backwards-compatible spelling for the external entry's fixed frame.
pub type InterruptFrame = KernelInterruptFrame;

impl KernelInterruptFrame {
    /// Size of the complete CPL0 frame built by the stubs.
    pub const SIZE: u64 = 20 * 8;
    /// Returns the interrupted CPL0 stack pointer. CPL0/IST0 does not push RSP/SS.
    #[must_use]
    pub fn interrupted_rsp(&self) -> u64 {
        (self as *const Self as u64)
            .checked_add(Self::SIZE)
            .unwrap_or(0)
    }
    /// Validates the invariant fields for an installed external vector.
    #[must_use]
    pub fn valid(&self) -> bool {
        let vector = u8::try_from(self.vector).ok();
        matches!(
            vector,
            Some(TIMER_VECTOR | SPURIOUS_VECTOR | PREEMPTION_TEST_VECTOR)
        ) && self.error_code == 0
            && self.cs == super::gdt::KERNEL_CODE_SELECTOR as u64
            && self.cs & 3 == 0
            && super::paging::is_canonical(self.rip)
            && super::paging::is_canonical(self.interrupted_rsp())
            && self.rflags & 2 != 0
    }
}

mod asm_stubs {
    #![allow(unsafe_code)]
    core::arch::global_asm!(
        r#"
        .macro SAVE_INTERRUPT_REGS
            push r15; push r14; push r13; push r12; push r11; push r10; push r9; push r8
            push rbp; push rdi; push rsi; push rdx; push rcx; push rbx; push rax
        .endm
        .macro RESTORE_INTERRUPT_REGS
            pop rax; pop rbx; pop rcx; pop rdx; pop rsi; pop rdi; pop rbp; pop r8
            pop r9; pop r10; pop r11; pop r12; pop r13; pop r14; pop r15
        .endm
        .align 16
        .globl external_interrupt_entry
        external_interrupt_entry:
            cld
            SAVE_INTERRUPT_REGS
            mov r12, rsp
            mov rdi, r12
            and rsp, -16
            mov rdx, rsp
            and edx, 15
            mov byte ptr [rip + INTERRUPT_CALL_ALIGNMENT], dl
            call rust_interrupt_dispatch
            test rax, rax
            jz fatal_interrupt_return
            mov rsp, rax
            RESTORE_INTERRUPT_REGS
            add rsp, 16
            iretq
        fatal_interrupt_return:
            cli
        1: hlt
            jmp 1b
        .align 16
        .globl vector_0x40
        vector_0x40:
            push 0
            push 0x40
            jmp external_interrupt_entry
        .align 16
        .globl vector_0xff
        vector_0xff:
            push 0
            push 0xff
            jmp external_interrupt_entry
        .align 16
        .globl vector_0x41
        vector_0x41:
            push 0
            push 0x41
            jmp external_interrupt_entry
    "#
    );

    unsafe extern "C" {
        pub fn vector_0x40();
        pub fn vector_0xff();
        pub fn vector_0x41();
    }
}

/// Return the timer entry address.
#[must_use]
pub fn timer_entry_address() -> u64 {
    asm_stubs::vector_0x40 as *const () as u64
}
/// Return the spurious entry address.
#[must_use]
pub fn spurious_entry_address() -> u64 {
    asm_stubs::vector_0xff as *const () as u64
}
/// Return the diagnostic software-interrupt entry address.
#[must_use]
pub fn preemption_test_entry_address() -> u64 {
    asm_stubs::vector_0x41 as *const () as u64
}
/// Return the Rust dispatcher address.
#[must_use]
pub fn dispatcher_address() -> u64 {
    rust_interrupt_dispatch as *const () as u64
}

/// Return the recorded pre-call stack alignment (test diagnostics).
#[must_use]
pub fn call_site_alignment() -> u8 {
    INTERRUPT_CALL_ALIGNMENT.load(Ordering::Acquire)
}

/// Install timer and spurious gates while IF remains clear.
///
/// # Safety
///
/// Must be called during single-BSP initialization before enabling maskable interrupts.
#[allow(unsafe_code)]
pub unsafe fn install() {
    // SAFETY: The two symbols are executable ring-0 entry points and IDT storage is resident.
    unsafe {
        idt::set_handler(
            usize::from(TIMER_VECTOR),
            timer_entry_address(),
            EXTERNAL_GATE_IST,
            idt::IDT_INTERRUPT_GATE,
        );
        idt::set_handler(
            usize::from(SPURIOUS_VECTOR),
            spurious_entry_address(),
            EXTERNAL_GATE_IST,
            idt::IDT_INTERRUPT_GATE,
        );
        idt::set_handler(
            usize::from(PREEMPTION_TEST_VECTOR),
            preemption_test_entry_address(),
            EXTERNAL_GATE_IST,
            idt::IDT_INTERRUPT_GATE,
        );
    }
}

/// Validate the two external gates.
#[must_use]
pub fn validate() -> bool {
    [TIMER_VECTOR, SPURIOUS_VECTOR, PREEMPTION_TEST_VECTOR]
        .iter()
        .all(|&vector| {
            idt::gate_diagnostic(usize::from(vector)).is_some_and(
                |(offset, selector, ist, attr, reserved)| {
                    offset != 0
                        && super::paging::is_canonical(offset)
                        && selector == super::gdt::KERNEL_CODE_SELECTOR
                        && ist == 0
                        && attr == idt::IDT_INTERRUPT_GATE | 0x80
                        && reserved == 0
                },
            )
        })
}

/// Dispatcher reached by the real assembly entries.
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
extern "C" fn rust_interrupt_dispatch(
    frame: *mut KernelInterruptFrame,
) -> *mut KernelInterruptFrame {
    // SAFETY: The entry stubs pass a pointer to their fixed, register-aligned frame.
    let frame_ref = unsafe { &*frame };
    if !frame_ref.valid() {
        return core::ptr::null_mut();
    }
    let task_id = match attribute_interrupted_rsp(frame_ref.interrupted_rsp()) {
        Ok(id) => id,
        Err(_) => return core::ptr::null_mut(),
    };
    match u8::try_from(frame_ref.vector).unwrap_or(u8::MAX) {
        TIMER_VECTOR => timer::handle_tick(),
        SPURIOUS_VECTOR => timer::handle_spurious(),
        PREEMPTION_TEST_VECTOR => {}
        vector => {
            serial::log(format_args!(
                "FINNOS:INTERRUPT:UNEXPECTED\nFINNOS:INTERRUPT:VECTOR={vector:#x}\n"
            ));
            unsafe {
                core::arch::asm!("cli", options(nomem, nostack, preserves_flags));
            }
            super::qemu::exit(0x11);
        }
    }
    record_snapshot(frame_ref, frame, task_id);
    if frame_ref.vector == u64::from(TIMER_VECTOR) && PREEMPTION_TIMER_PHASE.load(Ordering::Acquire)
    {
        PREEMPTION_TIMER_OBSERVED.store(true, Ordering::Release);
    }
    frame
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{offset_of, size_of};
    #[test]
    fn policy_does_not_collide() {
        const { assert!(PIC_VECTOR_END < TIMER_VECTOR) };
        assert!((0..=0x1f).all(|v| v != TIMER_VECTOR));
        assert_ne!(TIMER_VECTOR, SPURIOUS_VECTOR);
    }
    #[test]
    fn frame_layout_is_stable() {
        let offsets = [
            offset_of!(KernelInterruptFrame, rax),
            offset_of!(KernelInterruptFrame, rbx),
            offset_of!(KernelInterruptFrame, rcx),
            offset_of!(KernelInterruptFrame, rdx),
            offset_of!(KernelInterruptFrame, rsi),
            offset_of!(KernelInterruptFrame, rdi),
            offset_of!(KernelInterruptFrame, rbp),
            offset_of!(KernelInterruptFrame, r8),
            offset_of!(KernelInterruptFrame, r9),
            offset_of!(KernelInterruptFrame, r10),
            offset_of!(KernelInterruptFrame, r11),
            offset_of!(KernelInterruptFrame, r12),
            offset_of!(KernelInterruptFrame, r13),
            offset_of!(KernelInterruptFrame, r14),
            offset_of!(KernelInterruptFrame, r15),
            offset_of!(KernelInterruptFrame, vector),
            offset_of!(KernelInterruptFrame, error_code),
            offset_of!(KernelInterruptFrame, rip),
            offset_of!(KernelInterruptFrame, cs),
            offset_of!(KernelInterruptFrame, rflags),
        ];
        assert_eq!(offsets, core::array::from_fn(|field| field * 8));
        assert_eq!(size_of::<KernelInterruptFrame>(), 20 * 8);
        assert_eq!(core::mem::align_of::<KernelInterruptFrame>(), 8);
    }

    #[test]
    fn attribution_uses_published_stack_boundaries() {
        let id = TaskId::new(2, 7).unwrap();
        publish_task_stack(id, 0x1000, 0x2000).unwrap();
        assert_eq!(attribute_interrupted_rsp(0x1001), Ok(id));
        assert_eq!(attribute_interrupted_rsp(0x2000), Ok(id));
        assert_eq!(
            attribute_interrupted_rsp(0x1000),
            Err(AttributionError::NoMatch)
        );
        assert_eq!(
            attribute_interrupted_rsp(0x0fff),
            Err(AttributionError::NoMatch)
        );
        assert_eq!(
            attribute_interrupted_rsp(0xffff_8000_0000_0000),
            Err(AttributionError::NoMatch)
        );
        unpublish_task_stack(id.slot());
    }

    #[test]
    fn attribution_rejects_overlap_and_noncanonical_addresses() {
        let first = TaskId::new(4, 8).unwrap();
        let second = TaskId::new(5, 9).unwrap();
        publish_task_stack(first, 0x3000, 0x4000).unwrap();
        publish_task_stack(second, 0x3800, 0x4800).unwrap();
        assert_eq!(
            attribute_interrupted_rsp(0x3900),
            Err(AttributionError::MultipleMatch)
        );
        assert_eq!(
            attribute_interrupted_rsp(0x0001_0000_0000_0000),
            Err(AttributionError::NoncanonicalRsp)
        );
        unpublish_task_stack(first.slot());
        unpublish_task_stack(second.slot());
    }

    #[test]
    fn frame_validation_rejects_contract_violations() {
        let mut frame = KernelInterruptFrame {
            rax: 0,
            rbx: 0,
            rcx: 0,
            rdx: 0,
            rsi: 0,
            rdi: 0,
            rbp: 0,
            r8: 0,
            r9: 0,
            r10: 0,
            r11: 0,
            r12: 0,
            r13: 0,
            r14: 0,
            r15: 0,
            vector: u64::from(TIMER_VECTOR),
            error_code: 0,
            rip: 0x1000,
            cs: u64::from(super::gdt::KERNEL_CODE_SELECTOR),
            rflags: 2,
        };
        assert!(frame.valid());
        frame.cs |= 3;
        assert!(!frame.valid());
        frame.cs &= !3;
        frame.rflags = 0;
        assert!(!frame.valid());
        frame.rflags = 2;
        frame.error_code = 1;
        assert!(!frame.valid());
        frame.error_code = 0;
        frame.vector = 0x20;
        assert!(!frame.valid());
    }
}
