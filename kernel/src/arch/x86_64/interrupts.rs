//! x86-64 external interrupt entry and fixed vector policy.

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicUsize};
use core::sync::atomic::{AtomicU8, Ordering};

use super::{idt, serial, timer};
use crate::interrupt::InterruptContextGuard;
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
    const fn empty() -> Self {
        Self {
            active: AtomicBool::new(false),
            generation: AtomicU32::new(0),
            start: AtomicU64::new(0),
            end: AtomicU64::new(0),
        }
    }
}
static PUBLISHED_STACKS: [PublishedTaskStack; MAX_PUBLISHED_STACKS] =
    [const { PublishedTaskStack::empty() }; MAX_PUBLISHED_STACKS];

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
    /// The interrupt frame pointer or its complete-frame arithmetic is invalid.
    InvalidFrame,
}

/// A stable copy of one published task-stack interval.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PublishedStackInfo {
    /// Generation-tagged task identity.
    pub task_id: TaskId,
    /// Inclusive lower publication boundary.
    pub start: u64,
    /// Exclusive upper publication boundary.
    pub end: u64,
}

/// Errors validating an interrupt frame before dispatch.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FrameValidationError {
    /// The frame pointer was null.
    Null,
    /// The frame pointer was not canonical.
    NoncanonicalFrame,
    /// The frame pointer did not satisfy the ABI alignment contract.
    MisalignedFrame,
    /// Checked frame arithmetic overflowed.
    AddressOverflow,
    /// A required field was invalid.
    InvalidFields,
    /// The derived interrupted RSP was not canonical.
    NoncanonicalRsp,
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
    /// Address of the CPU-saved RSP slot.
    pub saved_rsp_field_address: u64,
    /// CPU-saved SS selector.
    pub saved_ss: u64,
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
    saved_rsp_field_address: 0,
    saved_ss: 0,
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
static CAPTURE_ACTIVE: AtomicBool = AtomicBool::new(false);
static CAPTURED: AtomicBool = AtomicBool::new(false);
static CAPTURE_VECTOR: AtomicU8 = AtomicU8::new(u8::MAX);
static CAPTURE_TASK_SLOT: AtomicUsize = AtomicUsize::new(usize::MAX);
static CAPTURE_TASK_GENERATION: AtomicU32 = AtomicU32::new(0);
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
/// Set while the dedicated real-timer preservation helper is active.
pub static PREEMPTION_TIMER_PHASE: AtomicBool = AtomicBool::new(false);
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
/// Set by the timer dispatcher after observing the active preservation helper.
pub static PREEMPTION_TIMER_OBSERVED: AtomicBool = AtomicBool::new(false);

/// Publishes one task stack with release ordering.
///
/// # Errors
///
/// Returns [`AttributionError::InvalidGeneration`] when the task identity or
/// stack bounds cannot be represented by the publication table.
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

/// Returns whether a generation-tagged task currently has an active publication.
#[must_use]
pub fn task_stack_published(id: TaskId) -> bool {
    let Some(slot) = PUBLISHED_STACKS.get(id.slot()) else {
        return false;
    };
    slot.active.load(Ordering::Acquire)
        && slot.generation.load(Ordering::Acquire) == id.generation()
}
fn find_published_stack(interrupted_rsp: u64) -> Result<PublishedStackInfo, AttributionError> {
    if !super::paging::is_canonical(interrupted_rsp) {
        return Err(AttributionError::NoncanonicalRsp);
    }
    let mut found = None;
    for (slot_index, slot) in PUBLISHED_STACKS.iter().enumerate() {
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
            let slot_index =
                u8::try_from(slot_index).map_err(|_| AttributionError::InvalidGeneration)?;
            let id = TaskId::new(slot_index, generation)
                .map_err(|_| AttributionError::InvalidGeneration)?;
            if found
                .replace(PublishedStackInfo {
                    task_id: id,
                    start,
                    end,
                })
                .is_some()
            {
                return Err(AttributionError::MultipleMatch);
            }
        }
    }
    found.ok_or(AttributionError::NoMatch)
}

/// Attributes an interrupted stack pointer using only atomic publications.
///
/// # Errors
///
/// Returns an [`AttributionError`] when the pointer is non-canonical, no
/// publication matches, publications overlap or change while being read, or
/// the mirrored task identity is invalid.
pub fn attribute_interrupted_rsp(interrupted_rsp: u64) -> Result<TaskId, AttributionError> {
    Ok(find_published_stack(interrupted_rsp)?.task_id)
}

/// Validates that a complete frame lies inside the publication selected by its RSP.
pub fn validate_frame_stack(
    frame_pointer: u64,
    interrupted_rsp: u64,
) -> Result<PublishedStackInfo, AttributionError> {
    let publication = find_published_stack(interrupted_rsp)?;
    let frame_end = frame_pointer
        .checked_add(KernelInterruptFrame::SIZE)
        .ok_or(AttributionError::FrameOutsideStack)?;
    if frame_pointer < publication.start || frame_end > publication.end {
        return Err(AttributionError::FrameOutsideStack);
    }
    Ok(publication)
}

const fn save_registers(frame: &KernelInterruptFrame) -> SavedGeneralRegisters {
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
fn record_snapshot(
    frame: &KernelInterruptFrame,
    returned: *mut KernelInterruptFrame,
    id: TaskId,
) -> bool {
    let expected_vector = CAPTURE_VECTOR.load(Ordering::Acquire);
    let expected_slot = CAPTURE_TASK_SLOT.load(Ordering::Acquire);
    let expected_generation = CAPTURE_TASK_GENERATION.load(Ordering::Acquire);
    if !CAPTURE_ACTIVE.load(Ordering::Acquire)
        || expected_vector != u8::try_from(frame.vector).unwrap_or(u8::MAX)
        || expected_slot != id.slot()
        || expected_generation != id.generation()
    {
        return false;
    }
    if CAPTURE_ACTIVE
        .compare_exchange(true, false, Ordering::AcqRel, Ordering::Acquire)
        .is_err()
    {
        return false;
    }
    let sequence = SNAPSHOT_SEQUENCE
        .fetch_add(1, Ordering::AcqRel)
        .saturating_add(1);
    // SAFETY: this is the sole BSP writer; the sequence brackets the copy.
    unsafe {
        *SNAPSHOT.0.get() = InterruptedTaskSnapshot {
            task_id: id,
            frame_pointer: core::ptr::from_ref(frame) as u64,
            returned_frame_pointer: returned as u64,
            interrupted_rsp: frame.interrupted_rsp().unwrap_or(0),
            saved_rsp_field_address: (core::ptr::from_ref(frame) as u64)
                .checked_add(
                    KernelInterruptFrame::GPR_PREFIX_SIZE
                        + KernelInterruptFrame::SOFTWARE_PREFIX_SIZE
                        + KernelInterruptFrame::HARDWARE_RETURN_SIZE,
                )
                .unwrap_or(0),
            saved_ss: frame.saved_ss,
            rip: frame.rip,
            cs: frame.cs,
            rflags: frame.rflags,
            vector: u8::try_from(frame.vector).unwrap_or(u8::MAX),
            sequence,
            registers: save_registers(frame),
        };
    }
    SNAPSHOT_SEQUENCE.store(sequence.saturating_add(1), Ordering::Release);
    CAPTURED.store(true, Ordering::Release);
    true
}
/// Reads a stable diagnostic snapshot, retrying a bounded number of times.
#[must_use]
#[allow(unsafe_code)]
pub fn snapshot() -> Option<InterruptedTaskSnapshot> {
    if crate::interrupt::in_interrupt_context() {
        return None;
    }
    #[cfg(target_os = "none")]
    let interrupts_were_enabled = super::cpu::interrupts_enabled();
    #[cfg(target_os = "none")]
    if interrupts_were_enabled {
        super::cpu::disable_interrupts();
    }
    let result = snapshot_copy();
    #[cfg(target_os = "none")]
    if interrupts_were_enabled {
        super::cpu::enable_interrupts();
    }
    result
}

#[allow(unsafe_code)]
fn snapshot_copy() -> Option<InterruptedTaskSnapshot> {
    if !CAPTURED.load(Ordering::Acquire) {
        return None;
    }
    for _ in 0..3 {
        let before = SNAPSHOT_SEQUENCE.load(Ordering::Acquire);
        if before == 0 || !before.is_multiple_of(2) {
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
pub fn begin_capture(vector: u8, task_id: TaskId) {
    SNAPSHOT_SEQUENCE.store(0, Ordering::Release);
    CAPTURED.store(false, Ordering::Release);
    CAPTURE_VECTOR.store(vector, Ordering::Release);
    CAPTURE_TASK_SLOT.store(task_id.slot(), Ordering::Release);
    CAPTURE_TASK_GENERATION.store(task_id.generation(), Ordering::Release);
    CAPTURE_ACTIVE.store(true, Ordering::Release);
}

/// Ends a phase-specific capture without permitting later interrupts to overwrite it.
pub fn end_capture() {
    CAPTURE_ACTIVE.store(false, Ordering::Release);
}

/// Starts the bounded real-timer preservation observation phase.
pub fn begin_timer_test(task_id: TaskId) {
    PREEMPTION_TIMER_OBSERVED.store(false, Ordering::Release);
    PREEMPTION_TIMER_PHASE.store(true, Ordering::Release);
    begin_capture(TIMER_VECTOR, task_id);
}
/// Ends the bounded real-timer preservation observation phase.
pub fn end_timer_test() {
    PREEMPTION_TIMER_PHASE.store(false, Ordering::Release);
    end_capture();
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
    /// CPU-saved interrupted RSP for the current 64-bit interrupt contract.
    pub saved_rsp: u64,
    /// CPU-saved interrupted SS selector.
    pub saved_ss: u64,
    /// Hardware alignment slot following the iretq fields.
    pub hardware_alignment: u64,
}

/// Backwards-compatible spelling for the external entry's fixed frame.
pub type InterruptFrame = KernelInterruptFrame;

impl KernelInterruptFrame {
    /// Number of bytes occupied by the saved GPR prefix.
    pub const GPR_PREFIX_SIZE: u64 = 15 * 8;
    /// Number of bytes occupied by the software vector and synthetic error code.
    pub const SOFTWARE_PREFIX_SIZE: u64 = 2 * 8;
    /// Number of bytes occupied by the CPU's CPL0 return fields.
    pub const HARDWARE_RETURN_SIZE: u64 = 3 * 8;
    /// Size of the `iretq`-interpreted CPL0/IST0 frame through saved SS.
    pub const IRET_FRAME_SIZE: u64 =
        Self::GPR_PREFIX_SIZE + Self::SOFTWARE_PREFIX_SIZE + Self::HARDWARE_RETURN_SIZE + (2 * 8);
    /// Size of the complete hardware footprint including its alignment slot.
    pub const SIZE: u64 = Self::IRET_FRAME_SIZE + 8;
    /// Returns the interrupted CPL0 stack pointer from the hardware tail.
    #[must_use]
    pub fn interrupted_rsp(&self) -> Result<u64, FrameValidationError> {
        if !super::paging::is_canonical(self.saved_rsp) {
            return Err(FrameValidationError::NoncanonicalRsp);
        }
        Ok(self.saved_rsp)
    }
    /// Validates the complete frame and its derived old RSP.
    #[must_use]
    pub fn validate(&self, frame_pointer: u64) -> Result<u64, FrameValidationError> {
        if frame_pointer == 0 {
            return Err(FrameValidationError::Null);
        }
        if !super::paging::is_canonical(frame_pointer) {
            return Err(FrameValidationError::NoncanonicalFrame);
        }
        if frame_pointer % 8 != 0 {
            return Err(FrameValidationError::MisalignedFrame);
        }
        let frame_end = frame_pointer
            .checked_add(Self::SIZE)
            .ok_or(FrameValidationError::AddressOverflow)?;
        if !super::paging::is_canonical(frame_end.saturating_sub(1)) {
            return Err(FrameValidationError::NoncanonicalFrame);
        }
        let vector = u8::try_from(self.vector).ok();
        if !matches!(
            vector,
            Some(TIMER_VECTOR | SPURIOUS_VECTOR | PREEMPTION_TEST_VECTOR)
        ) || self.error_code != 0
            || self.cs != u64::from(super::gdt::KERNEL_CODE_SELECTOR)
            || self.cs.trailing_zeros() < 2
            || !super::paging::is_canonical(self.rip)
            || self.rflags & 2 == 0
            || self.saved_ss != u64::from(super::gdt::KERNEL_DATA_SELECTOR)
        {
            return Err(FrameValidationError::InvalidFields);
        }
        self.interrupted_rsp()
    }
    /// Validates a frame using its own address.
    #[must_use]
    pub fn valid(&self) -> bool {
        self.validate(core::ptr::from_ref(self) as u64).is_ok()
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
    let Ok(_interrupt_guard) = InterruptContextGuard::enter() else {
        return core::ptr::null_mut();
    };
    let frame_pointer = frame as u64;
    if frame.is_null() || !super::paging::is_canonical(frame_pointer) || frame_pointer % 8 != 0 {
        return core::ptr::null_mut();
    }
    // SAFETY: the entry stubs pass a canonical, aligned pointer to their
    // complete fixed frame; validation below precedes all field use.
    let frame_ref = unsafe { &*frame };
    let Ok(interrupted_rsp) = frame_ref.validate(frame_pointer) else {
        return core::ptr::null_mut();
    };
    let Ok(publication) = validate_frame_stack(frame_pointer, interrupted_rsp) else {
        return core::ptr::null_mut();
    };
    let task_id = publication.task_id;
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
    let captured = record_snapshot(frame_ref, frame, task_id);
    if captured
        && frame_ref.vector == u64::from(TIMER_VECTOR)
        && PREEMPTION_TIMER_PHASE.load(Ordering::Acquire)
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
            offset_of!(KernelInterruptFrame, saved_rsp),
            offset_of!(KernelInterruptFrame, saved_ss),
            offset_of!(KernelInterruptFrame, hardware_alignment),
        ];
        assert_eq!(offsets, core::array::from_fn(|field| field * 8));
        assert_eq!(size_of::<KernelInterruptFrame>(), 23 * 8);
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
            cs: u64::from(crate::arch::x86_64::gdt::KERNEL_CODE_SELECTOR),
            rflags: 2,
            saved_rsp: 0x1100,
            saved_ss: u64::from(crate::arch::x86_64::gdt::KERNEL_DATA_SELECTOR),
            hardware_alignment: 0,
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

    #[test]
    fn frame_tail_extracts_saved_rsp_and_validates_ss() {
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
            rip: 0x2000,
            cs: u64::from(crate::arch::x86_64::gdt::KERNEL_CODE_SELECTOR),
            rflags: 2,
            saved_rsp: 0x8000,
            saved_ss: u64::from(crate::arch::x86_64::gdt::KERNEL_DATA_SELECTOR),
            hardware_alignment: 0,
        };
        assert_eq!(frame.interrupted_rsp(), Ok(0x8000));
        assert!(frame.valid());
        frame.saved_rsp = 0x0001_0000_0000_0000;
        assert!(!frame.valid());
        frame.saved_rsp = 0x8000;
        frame.saved_ss = 0;
        assert!(!frame.valid());
    }

    #[test]
    fn complete_frame_bounds_use_the_hardware_footprint() {
        let id = TaskId::new(6, 3).unwrap();
        publish_task_stack(id, 0x1000, 0x2000).unwrap();
        assert_eq!(
            validate_frame_stack(0x1000, 0x1000 + KernelInterruptFrame::SIZE),
            Ok(PublishedStackInfo {
                task_id: id,
                start: 0x1000,
                end: 0x2000,
            })
        );
        assert_eq!(
            validate_frame_stack(0x1f00, 0x1f00),
            Err(AttributionError::FrameOutsideStack)
        );
        unpublish_task_stack(id.slot());
    }

    #[test]
    fn phase_capture_is_frozen_until_explicitly_started_again() {
        let id = TaskId::new(7, 4).unwrap();
        begin_capture(PREEMPTION_TEST_VECTOR, id);
        end_capture();
        assert!(snapshot().is_none());
    }
}
