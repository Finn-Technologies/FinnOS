//! FinnOS x86-64 exception dispatch and handlers.

use core::sync::atomic::{AtomicU8, Ordering};

use super::idt;
use super::qemu;
use super::serial;
use super::tss::TSS;

/// Exception vector for the breakpoint instruction (`int3`).
pub const VECTOR_BREAKPOINT: u8 = 3;
/// Exception vector for invalid opcode (`ud2`).
pub const VECTOR_INVALID_OPCODE: u8 = 6;
/// Exception vector for double fault.
pub const VECTOR_DOUBLE_FAULT: u8 = 8;
/// Exception vector for general-protection fault.
pub const VECTOR_GENERAL_PROTECTION: u8 = 13;
/// Exception vector for page fault.
pub const VECTOR_PAGE_FAULT: u8 = 14;

/// Vectors that carry a hardware error code.
///
/// These are the x86-64 architecturally-defined error-code vectors in the 0–31 range. Some
/// vectors depend on processor feature support (e.g., `21` and `29`), but their frame format
/// must still be encoded correctly when they occur.
pub const ERROR_CODE_VECTORS: &[u8] = &[8, 10, 11, 12, 13, 14, 17, 21, 29, 30];

/// Decoded x86-64 page-fault error-code bits.
///
/// See Intel SDM Vol. 3A, "Page-Fault Exception (#PF)".
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PageFaultErrorCode(u64);

impl PageFaultErrorCode {
    /// Create a decoder from the raw error code.
    #[must_use]
    pub const fn new(code: u64) -> Self {
        Self(code)
    }

    /// Return the raw error code.
    #[must_use]
    pub const fn raw(self) -> u64 {
        self.0
    }

    /// Page was present (protection violation) vs. not present.
    #[must_use]
    pub const fn present(self) -> bool {
        self.0 & 0x1 != 0
    }

    /// Access was a write.
    #[must_use]
    pub const fn write(self) -> bool {
        self.0 & 0x2 != 0
    }

    /// Access originated in user mode.
    #[must_use]
    pub const fn user(self) -> bool {
        self.0 & 0x4 != 0
    }

    /// A reserved bit was set in a page-table entry.
    #[must_use]
    pub const fn reserved_violation(self) -> bool {
        self.0 & 0x8 != 0
    }

    /// Fault occurred during instruction fetch.
    #[must_use]
    pub const fn instruction_fetch(self) -> bool {
        self.0 & 0x10 != 0
    }

    /// Protection-key violation.
    #[must_use]
    pub const fn protection_key(self) -> bool {
        self.0 & 0x20 != 0
    }

    /// Shadow-stack access.
    #[must_use]
    pub const fn shadow_stack(self) -> bool {
        self.0 & 0x40 != 0
    }

    /// SGX-related page fault.
    #[must_use]
    pub const fn sgx(self) -> bool {
        self.0 & 0x8000 != 0
    }
}

/// Atomic test state for the controlled exception test feature.
static TEST_STATE: AtomicU8 = AtomicU8::new(TestState::Idle as u8);

/// Permanently resident Task State Segment used by the early exception foundation.
static mut EXCEPTION_TSS: TSS = TSS::new();

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
#[allow(dead_code)] // Variants are read only in `cfg(test)` builds.
enum TestState {
    /// No exception test is in progress.
    Idle = 0,
    /// A breakpoint exception is expected next.
    BreakpointExpected = 1,
    /// The breakpoint exception has been handled.
    BreakpointHandled = 2,
    /// An invalid-opcode exception is expected next.
    InvalidOpcodeExpected = 3,
}

/// Normalized exception frame built by the assembly entry stubs.
///
/// The assembly stub pushes the general-purpose registers in the order shown below, then the
/// vector, then a synthetic or hardware error code, followed by the CPU-pushed `rip`, `cs`, and
/// `rflags`. This layout must exactly match the assembly push order.
///
/// Three hardware frame variants exist on x86-64:
///
/// 1. Ring-0 exception without an IST or privilege transition: the CPU pushes `rip`, `cs`, and
///    `rflags`. Error-code exceptions also push an error code. This is the current FinnOS case.
///
/// 2. Exception using an IST stack (including double fault): the CPU switches to the IST stack
///    and pushes the same frame as case 1, but on the new stack. The `ExceptionFrame` describes
///    the normalized common prefix; the previous stack state is not modeled here.
///
/// 3. Ring-3 to ring-0 transition: the hardware frame additionally includes the previous `rsp`
///    and `ss`. The current `ExceptionFrame` does not include those optional fields and must not
///    access them unless the frame type or entry metadata proves they exist.
#[repr(C)]
#[allow(missing_docs)]
pub struct ExceptionFrame {
    /// General-purpose registers, pushed in order from `rax` up to `r15`.
    pub rax: u64,
    pub rbx: u64,
    pub rcx: u64,
    pub rdx: u64,
    pub rsi: u64,
    pub rdi: u64,
    pub rbp: u64,
    pub r8: u64,
    pub r9: u64,
    pub r10: u64,
    pub r11: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    /// Exception vector number.
    pub vector: u64,
    /// Hardware error code, or a synthetic zero for exceptions without one.
    pub error_code: u64,
    /// Instruction pointer pushed by the CPU.
    pub rip: u64,
    /// Code segment pushed by the CPU.
    pub cs: u64,
    /// RFLAGS pushed by the CPU.
    pub rflags: u64,
}

/// Assert that the frame layout matches the assembly push order.
#[allow(dead_code)]
const fn _assert_frame_layout() {
    use core::mem::offset_of;
    let _ = offset_of!(ExceptionFrame, rax);
    let _ = offset_of!(ExceptionFrame, vector);
    let _ = offset_of!(ExceptionFrame, error_code);
    let _ = offset_of!(ExceptionFrame, rip);
}

/// Assembly entry stubs and their external declarations.
///
/// This module isolates the `global_asm!` block and the `unsafe extern "C"` declarations so
/// that the unavoidable `unsafe_code` lint can be scoped to this module only.
mod asm_stubs {
    #![allow(unsafe_code)]

    core::arch::global_asm!(
        r#"
        .macro SAVE_REGS
            push r15
            push r14
            push r13
            push r12
            push r11
            push r10
            push r9
            push r8
            push rbp
            push rdi
            push rsi
            push rdx
            push rcx
            push rbx
            push rax
        .endm

        .macro RESTORE_REGS
            pop rax
            pop rbx
            pop rcx
            pop rdx
            pop rsi
            pop rdi
            pop rbp
            pop r8
            pop r9
            pop r10
            pop r11
            pop r12
            pop r13
            pop r14
            pop r15
        .endm

        // Stack layout on entry to exception_no_error (top of stack is lowest address):
        //   [vector]          <- pushed by the per-vector stub
        //   [synthetic 0]      <- pushed by the per-vector stub to keep frame layout uniform
        //   [rip] [cs] [rflags] <- pushed by the CPU
        // After RESTORE_REGS, RSP points at [vector]. Remove both vector and synthetic zero so
        // iretq sees the CPU-pushed frame starting at [rip].
        .align 16
        .globl exception_no_error
        exception_no_error:
            SAVE_REGS
            mov rdi, rsp
            call rust_exception_dispatch
            RESTORE_REGS
            add rsp, 16
            iretq

        // Stack layout on entry to exception_error (top of stack is lowest address):
        //   [vector]          <- pushed by the per-vector stub
        //   [error_code]      <- pushed by the CPU for error-code vectors
        //   [rip] [cs] [rflags] <- pushed by the CPU
        // After RESTORE_REGS, RSP points at [vector]. Remove both vector and error_code so
        // iretq sees the CPU-pushed frame starting at [rip].
        .align 16
        .globl exception_error
        exception_error:
            SAVE_REGS
            mov rdi, rsp
            call rust_exception_dispatch
            RESTORE_REGS
            add rsp, 16
            iretq

        .align 16
        .globl vector_0
        vector_0:
            push 0
            push 0
            jmp exception_no_error
        .align 16
        .globl vector_1
        vector_1:
            push 0
            push 1
            jmp exception_no_error
        .align 16
        .globl vector_2
        vector_2:
            push 0
            push 2
            jmp exception_no_error
        .align 16
        .globl vector_3
        vector_3:
            push 0
            push 3
            jmp exception_no_error
        .align 16
        .globl vector_4
        vector_4:
            push 0
            push 4
            jmp exception_no_error
        .align 16
        .globl vector_5
        vector_5:
            push 0
            push 5
            jmp exception_no_error
        .align 16
        .globl vector_6
        vector_6:
            push 0
            push 6
            jmp exception_no_error
        .align 16
        .globl vector_7
        vector_7:
            push 0
            push 7
            jmp exception_no_error
        .align 16
        .globl vector_8
        vector_8:
            push 8
            jmp exception_error
        .align 16
        .globl vector_9
        vector_9:
            push 0
            push 9
            jmp exception_no_error
        .align 16
        .globl vector_10
        vector_10:
            push 10
            jmp exception_error
        .align 16
        .globl vector_11
        vector_11:
            push 11
            jmp exception_error
        .align 16
        .globl vector_12
        vector_12:
            push 12
            jmp exception_error
        .align 16
        .globl vector_13
        vector_13:
            push 13
            jmp exception_error
        .align 16
        .globl vector_14
        vector_14:
            push 14
            jmp exception_error
        .align 16
        .globl vector_15
        vector_15:
            push 0
            push 15
            jmp exception_no_error
        .align 16
        .globl vector_16
        vector_16:
            push 0
            push 16
            jmp exception_no_error
        .align 16
        .globl vector_17
        vector_17:
            push 17
            jmp exception_error
        .align 16
        .globl vector_18
        vector_18:
            push 0
            push 18
            jmp exception_no_error
        .align 16
        .globl vector_19
        vector_19:
            push 0
            push 19
            jmp exception_no_error
        .align 16
        .globl vector_20
        vector_20:
            push 0
            push 20
            jmp exception_no_error
        .align 16
        .globl vector_21
        vector_21:
            push 21
            jmp exception_error
        .align 16
        .globl vector_22
        vector_22:
            push 0
            push 22
            jmp exception_no_error
        .align 16
        .globl vector_23
        vector_23:
            push 0
            push 23
            jmp exception_no_error
        .align 16
        .globl vector_24
        vector_24:
            push 0
            push 24
            jmp exception_no_error
        .align 16
        .globl vector_25
        vector_25:
            push 0
            push 25
            jmp exception_no_error
        .align 16
        .globl vector_26
        vector_26:
            push 0
            push 26
            jmp exception_no_error
        .align 16
        .globl vector_27
        vector_27:
            push 0
            push 27
            jmp exception_no_error
        .align 16
        .globl vector_28
        vector_28:
            push 0
            push 28
            jmp exception_no_error
        .align 16
        .globl vector_29
        vector_29:
            push 29
            jmp exception_error
        .align 16
        .globl vector_30
        vector_30:
            push 30
            jmp exception_error
        .align 16
        .globl vector_31
        vector_31:
            push 0
            push 31
            jmp exception_no_error
        "#
    );

    unsafe extern "C" {
        pub(super) fn vector_0();
        pub(super) fn vector_1();
        pub(super) fn vector_2();
        pub(super) fn vector_3();
        pub(super) fn vector_4();
        pub(super) fn vector_5();
        pub(super) fn vector_6();
        pub(super) fn vector_7();
        pub(super) fn vector_8();
        pub(super) fn vector_9();
        pub(super) fn vector_10();
        pub(super) fn vector_11();
        pub(super) fn vector_12();
        pub(super) fn vector_13();
        pub(super) fn vector_14();
        pub(super) fn vector_15();
        pub(super) fn vector_16();
        pub(super) fn vector_17();
        pub(super) fn vector_18();
        pub(super) fn vector_19();
        pub(super) fn vector_20();
        pub(super) fn vector_21();
        pub(super) fn vector_22();
        pub(super) fn vector_23();
        pub(super) fn vector_24();
        pub(super) fn vector_25();
        pub(super) fn vector_26();
        pub(super) fn vector_27();
        pub(super) fn vector_28();
        pub(super) fn vector_29();
        pub(super) fn vector_30();
        pub(super) fn vector_31();
    }
}

/// Collect the addresses of the early exception handler entry points.
///
/// This is a separate helper so host tests can build an IDT without linking the assembly stubs.
#[allow(unsafe_code)]
fn handler_addresses() -> idt::HandlerAddresses {
    let mut addresses = idt::HandlerAddresses::new();
    addresses.handlers[0] = asm_stubs::vector_0 as *const () as u64;
    addresses.handlers[1] = asm_stubs::vector_1 as *const () as u64;
    addresses.handlers[2] = asm_stubs::vector_2 as *const () as u64;
    addresses.handlers[3] = asm_stubs::vector_3 as *const () as u64;
    addresses.handlers[4] = asm_stubs::vector_4 as *const () as u64;
    addresses.handlers[5] = asm_stubs::vector_5 as *const () as u64;
    addresses.handlers[6] = asm_stubs::vector_6 as *const () as u64;
    addresses.handlers[7] = asm_stubs::vector_7 as *const () as u64;
    addresses.handlers[8] = asm_stubs::vector_8 as *const () as u64;
    addresses.handlers[9] = asm_stubs::vector_9 as *const () as u64;
    addresses.handlers[10] = asm_stubs::vector_10 as *const () as u64;
    addresses.handlers[11] = asm_stubs::vector_11 as *const () as u64;
    addresses.handlers[12] = asm_stubs::vector_12 as *const () as u64;
    addresses.handlers[13] = asm_stubs::vector_13 as *const () as u64;
    addresses.handlers[14] = asm_stubs::vector_14 as *const () as u64;
    addresses.handlers[15] = asm_stubs::vector_15 as *const () as u64;
    addresses.handlers[16] = asm_stubs::vector_16 as *const () as u64;
    addresses.handlers[17] = asm_stubs::vector_17 as *const () as u64;
    addresses.handlers[18] = asm_stubs::vector_18 as *const () as u64;
    addresses.handlers[19] = asm_stubs::vector_19 as *const () as u64;
    addresses.handlers[20] = asm_stubs::vector_20 as *const () as u64;
    addresses.handlers[21] = asm_stubs::vector_21 as *const () as u64;
    addresses.handlers[22] = asm_stubs::vector_22 as *const () as u64;
    addresses.handlers[23] = asm_stubs::vector_23 as *const () as u64;
    addresses.handlers[24] = asm_stubs::vector_24 as *const () as u64;
    addresses.handlers[25] = asm_stubs::vector_25 as *const () as u64;
    addresses.handlers[26] = asm_stubs::vector_26 as *const () as u64;
    addresses.handlers[27] = asm_stubs::vector_27 as *const () as u64;
    addresses.handlers[28] = asm_stubs::vector_28 as *const () as u64;
    addresses.handlers[29] = asm_stubs::vector_29 as *const () as u64;
    addresses.handlers[30] = asm_stubs::vector_30 as *const () as u64;
    addresses.handlers[31] = asm_stubs::vector_31 as *const () as u64;
    addresses
}

/// Initialize the IDT with the early exception handlers.
///
/// # Safety
///
/// Must be called once on the BSP after the GDT is loaded.
#[allow(unsafe_code)]
pub unsafe fn init() {
    let addresses = handler_addresses();
    let idt = idt::build_exception_idt(&addresses);
    // SAFETY: `idt::install` copies the built IDT into the static IDT storage.
    unsafe {
        idt::install(idt);
    }
}

/// Rust dispatch function called from the assembly entry stubs.
///
/// # Safety
///
/// `frame` must point to a valid `ExceptionFrame` built by the assembly stubs.
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
extern "C" fn rust_exception_dispatch(frame: *const ExceptionFrame) {
    // SAFETY: The assembly stubs always pass a valid, aligned frame pointer.
    let frame = unsafe { &*frame };
    match frame.vector as u8 {
        VECTOR_BREAKPOINT => handle_breakpoint(frame),
        VECTOR_INVALID_OPCODE => handle_invalid_opcode(frame),
        VECTOR_DOUBLE_FAULT => handle_double_fault(frame),
        VECTOR_GENERAL_PROTECTION => handle_general_protection(frame),
        VECTOR_PAGE_FAULT => handle_page_fault(frame),
        _ => handle_unhandled(frame),
    }
}

/// Attempt to accept a controlled breakpoint exception.
///
/// Returns `true` if the breakpoint was expected and the state machine transitioned to
/// `BreakpointHandled`. Returns `false` if the breakpoint was unexpected.
#[cfg(feature = "qemu-test-exceptions")]
fn accept_breakpoint() -> bool {
    let previous = TEST_STATE.load(Ordering::SeqCst);
    if previous != TestState::BreakpointExpected as u8 {
        return false;
    }
    TEST_STATE.store(TestState::BreakpointHandled as u8, Ordering::SeqCst);
    true
}

fn handle_breakpoint(frame: &ExceptionFrame) {
    serial::log(format_args!("FINNOS:EXCEPTION:BREAKPOINT\n"));
    #[cfg(feature = "qemu-test-exceptions")]
    {
        if !accept_breakpoint() {
            fatal(frame, "UNEXPECTED_BREAKPOINT");
        }
    }
    // `frame` is only used in the test-exceptions fatal path above.
    let _ = frame;
    // Breakpoint is resumable; the assembly stub will perform iretq.
}

/// Return true if an invalid-opcode exception is currently expected by the test state machine.
#[cfg(feature = "qemu-test-exceptions")]
fn invalid_opcode_expected() -> bool {
    TEST_STATE.load(Ordering::SeqCst) == TestState::InvalidOpcodeExpected as u8
}

fn handle_invalid_opcode(frame: &ExceptionFrame) {
    serial::log(format_args!("FINNOS:EXCEPTION:INVALID_OPCODE\n"));
    #[cfg(feature = "qemu-test-exceptions")]
    {
        if invalid_opcode_expected() {
            serial::log(format_args!("FINNOS:TEST:INVALID_OPCODE:PASS\n"));
            qemu::exit(0x10);
        }
    }
    fatal(frame, "INVALID_OPCODE");
}

fn handle_double_fault(frame: &ExceptionFrame) {
    serial::log(format_args!("FINNOS:EXCEPTION:DOUBLE_FAULT\n"));
    fatal(frame, "DOUBLE_FAULT");
}

fn handle_general_protection(frame: &ExceptionFrame) {
    serial::log(format_args!("FINNOS:EXCEPTION:GENERAL_PROTECTION\n"));
    fatal(frame, "GENERAL_PROTECTION");
}

fn handle_page_fault(frame: &ExceptionFrame) {
    serial::log(format_args!("FINNOS:EXCEPTION:PAGE_FAULT\n"));
    // SAFETY: Reading CR2 is safe in ring 0 and returns the faulting address.
    let cr2: u64;
    unsafe {
        core::arch::asm!("mov {}, cr2", out(reg) cr2, options(nomem, nostack, preserves_flags));
    }
    let error = PageFaultErrorCode::new(frame.error_code);
    serial::log(format_args!(
        "CR2={cr2:#018x} ERROR={error_code:#018x} PRESENT={present} WRITE={write} USER={user} RESERVED={reserved} IFETCH={ifetch} PK={pk} SHADOW={shadow} SGX={sgx}\n",
        cr2 = cr2,
        error_code = frame.error_code,
        present = error.present(),
        write = error.write(),
        user = error.user(),
        reserved = error.reserved_violation(),
        ifetch = error.instruction_fetch(),
        pk = error.protection_key(),
        shadow = error.shadow_stack(),
        sgx = error.sgx(),
    ));
    fatal(frame, "PAGE_FAULT");
}

fn handle_unhandled(frame: &ExceptionFrame) {
    serial::log(format_args!("FINNOS:EXCEPTION:UNHANDLED\n"));
    fatal(frame, "UNHANDLED");
}

/// Print fatal exception diagnostics and halt or exit QEMU.
#[allow(unsafe_code)]
fn fatal(frame: &ExceptionFrame, category: &str) {
    serial::log(format_args!("FINNOS:EXCEPTION:FATAL\n"));
    serial::log(format_args!(
        "VECTOR={vector:#04x} ERROR={error:#018x} RIP={rip:#018x} CS={cs:#06x} RFLAGS={rflags:#018x}\n",
        vector = frame.vector,
        error = frame.error_code,
        rip = frame.rip,
        cs = frame.cs,
        rflags = frame.rflags,
    ));
    serial::log(format_args!("CATEGORY={category}\n"));
    // SAFETY: Fatal exceptions must not return; disabling interrupts prevents an errant
    // interrupt from resuming execution after the final halt.
    unsafe {
        core::arch::asm!("cli", options(nomem, nostack, preserves_flags));
    }
    qemu::exit(0x11);
}

/// Initialize descriptor tables and exception handlers.
///
/// # Safety
///
/// Must be called once on the BSP with interrupts disabled.
#[allow(unsafe_code)]
pub unsafe fn init_exception_foundation(rsp0: u64) {
    // `EXCEPTION_TSS` is a static mutable accessed only during single-core init.
    let tss = &raw mut EXCEPTION_TSS;
    unsafe {
        super::tss::init(&mut *tss, rsp0);
    }
    serial::log(format_args!("FINNOS:KERNEL:TSS_INIT\n"));
    // SAFETY: `gdt::init` loads the GDT with a valid TSS descriptor.
    unsafe {
        super::gdt::init(&*tss);
        serial::log(format_args!("FINNOS:KERNEL:GDT_LOADED\n"));
        super::gdt::reload_segments();
        serial::log(format_args!("FINNOS:KERNEL:SEGMENTS_RELOADED\n"));
        super::gdt::load_task_register();
    }
    serial::log(format_args!("FINNOS:KERNEL:GDT_OK\n"));
    serial::log(format_args!("FINNOS:KERNEL:TSS_OK\n"));

    // SAFETY: `exceptions::init` installs handlers into the static IDT.
    unsafe {
        init();
        idt::load();
    }
    serial::log(format_args!("FINNOS:KERNEL:IDT_OK\n"));
    serial::log(format_args!("FINNOS:KERNEL:EXCEPTIONS_READY\n"));
}

/// Run the controlled exception test sequence.
///
/// # Safety
///
/// Must be called after `init_exception_foundation` with the IDT loaded.
#[allow(unsafe_code)]
pub unsafe fn run_exception_tests() {
    serial::log(format_args!("FINNOS:TEST:EXCEPTIONS:BEGIN\n"));

    serial::log(format_args!("FINNOS:TEST:BREAKPOINT:BEGIN\n"));
    TEST_STATE.store(TestState::BreakpointExpected as u8, Ordering::SeqCst);
    // SAFETY: `int3` is a controlled breakpoint in the test feature. It causes an
    // exception that pushes a frame and runs a handler; no memory or stack behavior
    // is declared for the instruction itself.
    unsafe {
        core::arch::asm!("int3");
    }
    serial::log(format_args!("FINNOS:TEST:BREAKPOINT:PASS\n"));

    serial::log(format_args!("FINNOS:TEST:INVALID_OPCODE:BEGIN\n"));
    TEST_STATE.store(TestState::InvalidOpcodeExpected as u8, Ordering::SeqCst);
    // SAFETY: `ud2` is a controlled invalid-opcode in the test feature. It intentionally
    // does not return through the ordinary path; the CPU raises #UD and the handler exits.
    // No memory or stack behavior is declared for the instruction itself.
    unsafe {
        core::arch::asm!("ud2");
    }
    // `ud2` should not return; if it does, the test failed.
    fatal(
        &ExceptionFrame {
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
            vector: 0,
            error_code: 0,
            rip: 0,
            cs: 0,
            rflags: 0,
        },
        "INVALID_OPCODE_DID_NOT_FAULT",
    );
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::offset_of;

    #[test]
    fn error_code_vectors_include_double_fault() {
        assert!(ERROR_CODE_VECTORS.contains(&VECTOR_DOUBLE_FAULT));
        assert!(ERROR_CODE_VECTORS.contains(&VECTOR_GENERAL_PROTECTION));
        assert!(ERROR_CODE_VECTORS.contains(&VECTOR_PAGE_FAULT));
    }

    #[test]
    fn breakpoint_vector_has_no_error_code() {
        assert!(!ERROR_CODE_VECTORS.contains(&VECTOR_BREAKPOINT));
    }

    #[test]
    fn invalid_opcode_vector_has_no_error_code() {
        assert!(!ERROR_CODE_VECTORS.contains(&VECTOR_INVALID_OPCODE));
    }

    #[test]
    fn exception_frame_layout_matches_push_order() {
        // The assembly stub pushes rax first (lowest address) and r15 last (highest address among
        // the saved general-purpose registers), then vector, error_code, rip, cs, rflags.
        assert_eq!(offset_of!(ExceptionFrame, rax), 0);
        assert_eq!(offset_of!(ExceptionFrame, rbx), 8);
        assert_eq!(offset_of!(ExceptionFrame, rbp), 8 * 6);
        assert_eq!(offset_of!(ExceptionFrame, r15), 8 * 14);
        assert_eq!(offset_of!(ExceptionFrame, vector), 8 * 15);
        assert_eq!(offset_of!(ExceptionFrame, error_code), 8 * 16);
        assert_eq!(offset_of!(ExceptionFrame, rip), 8 * 17);
        assert_eq!(offset_of!(ExceptionFrame, cs), 8 * 18);
        assert_eq!(offset_of!(ExceptionFrame, rflags), 8 * 19);
        assert_eq!(core::mem::size_of::<ExceptionFrame>(), 8 * 20);
    }

    #[test]
    fn breakpoint_and_invalid_opcode_have_synthetic_zero_error_code() {
        assert!(!ERROR_CODE_VECTORS.contains(&VECTOR_BREAKPOINT));
        assert!(!ERROR_CODE_VECTORS.contains(&VECTOR_INVALID_OPCODE));
    }

    #[test]
    fn error_code_vector_set_is_exact() {
        let expected = [8u8, 10, 11, 12, 13, 14, 17, 21, 29, 30];
        assert_eq!(ERROR_CODE_VECTORS, &expected[..]);
    }

    #[test]
    fn page_fault_error_code_decodes_zero() {
        let err = PageFaultErrorCode::new(0);
        assert!(!err.present());
        assert!(!err.write());
        assert!(!err.user());
        assert!(!err.reserved_violation());
        assert!(!err.instruction_fetch());
        assert!(!err.protection_key());
        assert!(!err.shadow_stack());
        assert!(!err.sgx());
    }

    #[test]
    fn page_fault_error_code_decodes_write_protection_violation() {
        let err = PageFaultErrorCode::new(0b011);
        assert!(err.present());
        assert!(err.write());
        assert!(!err.user());
    }

    #[test]
    fn page_fault_error_code_decodes_user_instruction_fetch() {
        let err = PageFaultErrorCode::new(0b1_0111);
        assert!(err.present());
        assert!(!err.write());
        assert!(err.user());
        assert!(err.instruction_fetch());
    }

    #[test]
    fn page_fault_error_code_decodes_reserved_bit_violation() {
        let err = PageFaultErrorCode::new(0b1001);
        assert!(err.present());
        assert!(err.reserved_violation());
    }

    #[test]
    fn page_fault_error_code_decodes_combined_flags() {
        let err = PageFaultErrorCode::new(0b1101_0111);
        assert!(err.present());
        assert!(err.write());
        assert!(err.user());
        assert!(err.reserved_violation());
        assert!(err.instruction_fetch());
        assert!(!err.protection_key());
        assert!(!err.shadow_stack());
        assert!(!err.sgx());
    }

    #[cfg(feature = "qemu-test-exceptions")]
    #[test]
    fn breakpoint_transition_is_accepted_when_expected() {
        TEST_STATE.store(TestState::Idle as u8, Ordering::SeqCst);
        TEST_STATE.store(TestState::BreakpointExpected as u8, Ordering::SeqCst);
        assert!(accept_breakpoint());
        assert_eq!(
            TEST_STATE.load(Ordering::SeqCst),
            TestState::BreakpointHandled as u8
        );
    }

    #[cfg(feature = "qemu-test-exceptions")]
    #[test]
    fn breakpoint_transition_is_rejected_when_not_expected() {
        TEST_STATE.store(TestState::Idle as u8, Ordering::SeqCst);
        assert!(!accept_breakpoint());
        assert_eq!(TEST_STATE.load(Ordering::SeqCst), TestState::Idle as u8);
    }

    #[cfg(feature = "qemu-test-exceptions")]
    #[test]
    fn invalid_opcode_is_expected_only_in_expected_state() {
        TEST_STATE.store(TestState::Idle as u8, Ordering::SeqCst);
        assert!(!invalid_opcode_expected());
        TEST_STATE.store(TestState::InvalidOpcodeExpected as u8, Ordering::SeqCst);
        assert!(invalid_opcode_expected());
    }
}
