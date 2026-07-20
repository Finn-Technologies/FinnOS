//! Minimal EL1 synchronous-exception foundation for the ARM64 R4.1 slice.
//!
//! The raw frame and vector mechanisms remain architecture-specific. This
//! module deliberately does not introduce a shared trap trait or depend on
//! allocation, locks, the timer, or tasks. R4.4 adds bounded GIC IRQ dispatch
//! after the owned MMU and controller are initialized.

#![allow(unsafe_code)]

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
use core::sync::atomic::AtomicU64;
#[cfg(target_os = "none")]
use core::sync::atomic::{AtomicU8, Ordering};

#[cfg(target_os = "none")]
use super::gic;
#[cfg(all(target_os = "none", feature = "qemu-test-exit"))]
use super::qemu;
#[cfg(target_os = "none")]
use super::serial;

#[cfg(any(target_os = "none", test))]
const FRAME_SIZE: usize = 816;
#[cfg(target_os = "none")]
const VECTOR_ALIGNMENT: usize = 2048;
#[cfg(any(target_os = "none", test))]
const CPACR_EL1_FPEN_MASK: u64 = 0b11 << 20;
#[cfg(any(target_os = "none", test))]
const SOURCE_CURRENT_SP0_SYNC: u64 = 0;
#[cfg(any(target_os = "none", test))]
const SOURCE_CURRENT_SPX_SYNC: u64 = 4;
#[cfg(target_os = "none")]
const SOURCE_CURRENT_SPX_IRQ: u64 = 5;
#[cfg(any(target_os = "none", test))]
const EC_BRK64: u8 = 0x3c;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const EC_INSTRUCTION_ABORT_CURRENT_EL: u8 = 0x21;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const EC_DATA_ABORT_CURRENT_EL: u8 = 0x25;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const ISS_WRITE_NOT_READ: u64 = 1 << 6;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const ISS_STAGE_ONE_PAGE_TABLE_WALK: u64 = 1 << 7;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const ISS_FAR_NOT_VALID: u64 = 1 << 10;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const ESR_INSTRUCTION_LENGTH: u64 = 1 << 25;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const FSC_MASK: u64 = 0x3f;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const FSC_CLASS_MASK: u64 = !0x3;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const FSC_TRANSLATION_FAULT: u64 = 0x4;
#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const FSC_PERMISSION_FAULT: u64 = 0xc;
#[cfg(any(target_os = "none", test))]
const CONTROLLED_BRK_IMMEDIATE: u16 = 0xf100;
#[cfg(any(target_os = "none", test))]
const TEST_IDLE: u8 = 0;
#[cfg(any(target_os = "none", test))]
const TEST_ARMED: u8 = 1;
#[cfg(target_os = "none")]
const TEST_OBSERVED: u8 = 2;

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_IDLE: u8 = 0;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_NULL_ARMED: u8 = 1;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_NULL_OBSERVED: u8 = 2;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_GUARD_ARMED: u8 = 3;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_GUARD_OBSERVED: u8 = 4;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_TEXT_WRITE_ARMED: u8 = 5;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_TEXT_WRITE_OBSERVED: u8 = 6;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_DATA_EXEC_ARMED: u8 = 7;
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
const PAGE_FAULT_DATA_EXEC_OBSERVED: u8 = 8;

#[cfg(target_os = "none")]
static TEST_STATE: AtomicU8 = AtomicU8::new(TEST_IDLE);

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
static PAGE_FAULT_STATE: AtomicU8 = AtomicU8::new(PAGE_FAULT_IDLE);
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
static PAGE_FAULT_LOW_GUARD: AtomicU64 = AtomicU64::new(0);
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
static PAGE_FAULT_TEXT_TARGET: AtomicU64 = AtomicU64::new(0);
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
static PAGE_FAULT_DATA_TARGET: AtomicU64 = AtomicU64::new(0);

#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ExpectedPageFault {
    NullRead,
    GuardRead,
    TextWrite,
    DataExecute,
}

#[cfg(any(test, all(target_os = "none", feature = "qemu-test-page-tables")))]
const fn page_fault_syndrome_matches(
    expected: ExpectedPageFault,
    source: u64,
    esr: u64,
    far: u64,
    elr: u64,
    expected_far: u64,
    expected_elr: u64,
) -> bool {
    let (expected_ec, expected_fsc, expected_write) = match expected {
        ExpectedPageFault::NullRead | ExpectedPageFault::GuardRead => {
            (EC_DATA_ABORT_CURRENT_EL, FSC_TRANSLATION_FAULT, false)
        }
        ExpectedPageFault::TextWrite => (EC_DATA_ABORT_CURRENT_EL, FSC_PERMISSION_FAULT, true),
        ExpectedPageFault::DataExecute => {
            (EC_INSTRUCTION_ABORT_CURRENT_EL, FSC_PERMISSION_FAULT, false)
        }
    };
    source == SOURCE_CURRENT_SPX_SYNC
        && far == expected_far
        && elr == expected_elr
        && exception_class(esr) == expected_ec
        && (esr & FSC_MASK) & FSC_CLASS_MASK == expected_fsc
        && (esr & ISS_WRITE_NOT_READ != 0) == expected_write
        && esr & ESR_INSTRUCTION_LENGTH != 0
        && esr & (ISS_FAR_NOT_VALID | ISS_STAGE_ONE_PAGE_TABLE_WALK) == 0
}

/// Raw state saved by the `AArch64` vector entry before Rust dispatch.
///
/// The layout is consumed directly by assembly and is therefore covered by
/// offset and size tests. General-purpose registers occupy the first 31 words.
#[repr(C)]
#[cfg(any(target_os = "none", test))]
pub struct ExceptionFrame {
    registers: [u64; 31],
    elr: u64,
    spsr: u64,
    esr: u64,
    far: u64,
    source: u64,
    vectors: [u128; 32],
    fpcr: u64,
    fpsr: u64,
}

#[cfg(target_os = "none")]
const _: () = assert!(core::mem::size_of::<ExceptionFrame>() == FRAME_SIZE);

/// Failures that prevent safe installation of the EL1 vector table.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InitializationError {
    /// Firmware entered the kernel at an exception level outside this slice.
    UnsupportedExceptionLevel(u8),
    /// The linked vector table does not satisfy the architectural alignment.
    MisalignedVectorBase,
    /// EL1 FP/SIMD access was not enabled before installing the vector table.
    FpSimdUnavailable,
}

/// Decode the architectural `CurrentEL` register into an exception-level number.
#[must_use]
pub const fn decode_current_el(raw: u64) -> u8 {
    ((raw >> 2) & 0x3) as u8
}

/// Return the exception class encoded in `ESR_EL1`.
#[must_use]
pub const fn exception_class(esr: u64) -> u8 {
    ((esr >> 26) & 0x3f) as u8
}

/// Return the immediate encoded by an `AArch64` breakpoint syndrome.
#[must_use]
pub const fn breakpoint_immediate(esr: u64) -> u16 {
    (esr & 0xffff) as u16
}

#[cfg(any(target_os = "none", test))]
const fn fp_simd_enabled(cpacr_el1: u64) -> bool {
    cpacr_el1 & CPACR_EL1_FPEN_MASK == CPACR_EL1_FPEN_MASK
}

#[cfg(target_os = "none")]
core::arch::global_asm!(
    r#"
    .macro FINNOS_VECTOR_SLOT source
        sub sp, sp, #816
        stp x16, x17, [sp, #128]
        mov x16, #\source
        b finnos_arm64_exception_common
    .endm

    .section .text.arm64_vectors,"ax"
    .balign 2048
    .global finnos_arm64_vector_table
finnos_arm64_vector_table:
    FINNOS_VECTOR_SLOT 0
    .org finnos_arm64_vector_table + 0x080
    FINNOS_VECTOR_SLOT 1
    .org finnos_arm64_vector_table + 0x100
    FINNOS_VECTOR_SLOT 2
    .org finnos_arm64_vector_table + 0x180
    FINNOS_VECTOR_SLOT 3
    .org finnos_arm64_vector_table + 0x200
    FINNOS_VECTOR_SLOT 4
    .org finnos_arm64_vector_table + 0x280
    FINNOS_VECTOR_SLOT 5
    .org finnos_arm64_vector_table + 0x300
    FINNOS_VECTOR_SLOT 6
    .org finnos_arm64_vector_table + 0x380
    FINNOS_VECTOR_SLOT 7
    .org finnos_arm64_vector_table + 0x400
    FINNOS_VECTOR_SLOT 8
    .org finnos_arm64_vector_table + 0x480
    FINNOS_VECTOR_SLOT 9
    .org finnos_arm64_vector_table + 0x500
    FINNOS_VECTOR_SLOT 10
    .org finnos_arm64_vector_table + 0x580
    FINNOS_VECTOR_SLOT 11
    .org finnos_arm64_vector_table + 0x600
    FINNOS_VECTOR_SLOT 12
    .org finnos_arm64_vector_table + 0x680
    FINNOS_VECTOR_SLOT 13
    .org finnos_arm64_vector_table + 0x700
    FINNOS_VECTOR_SLOT 14
    .org finnos_arm64_vector_table + 0x780
    FINNOS_VECTOR_SLOT 15
    .org finnos_arm64_vector_table + 0x800

    .balign 16
finnos_arm64_exception_common:
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x17, elr_el1
    str x17, [sp, #248]
    mrs x17, spsr_el1
    str x17, [sp, #256]
    mrs x17, esr_el1
    str x17, [sp, #264]
    mrs x17, far_el1
    str x17, [sp, #272]
    str x16, [sp, #280]
    stp q0, q1, [sp, #288]
    stp q2, q3, [sp, #320]
    stp q4, q5, [sp, #352]
    stp q6, q7, [sp, #384]
    stp q8, q9, [sp, #416]
    stp q10, q11, [sp, #448]
    stp q12, q13, [sp, #480]
    stp q14, q15, [sp, #512]
    stp q16, q17, [sp, #544]
    stp q18, q19, [sp, #576]
    stp q20, q21, [sp, #608]
    stp q22, q23, [sp, #640]
    stp q24, q25, [sp, #672]
    stp q26, q27, [sp, #704]
    stp q28, q29, [sp, #736]
    stp q30, q31, [sp, #768]
    mrs x17, fpcr
    str x17, [sp, #800]
    mrs x17, fpsr
    str x17, [sp, #808]
    mov x0, sp
    bl finnos_arm64_exception_dispatch
    ldr x16, [sp, #248]
    msr elr_el1, x16
    ldr x16, [sp, #256]
    msr spsr_el1, x16
    ldp q0, q1, [sp, #288]
    ldp q2, q3, [sp, #320]
    ldp q4, q5, [sp, #352]
    ldp q6, q7, [sp, #384]
    ldp q8, q9, [sp, #416]
    ldp q10, q11, [sp, #448]
    ldp q12, q13, [sp, #480]
    ldp q14, q15, [sp, #512]
    ldp q16, q17, [sp, #544]
    ldp q18, q19, [sp, #576]
    ldp q20, q21, [sp, #608]
    ldp q22, q23, [sp, #640]
    ldp q24, q25, [sp, #672]
    ldp q26, q27, [sp, #704]
    ldp q28, q29, [sp, #736]
    ldp q30, q31, [sp, #768]
    ldr x16, [sp, #800]
    msr fpcr, x16
    ldr x16, [sp, #808]
    msr fpsr, x16
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldp x16, x17, [sp, #128]
    add sp, sp, #816
    eret
"#
);

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
core::arch::global_asm!(
    r#"
    .section .text.arm64_page_fault_tests,"ax"
    .balign 16

    .global finnos_arm64_test_null_read
    .global finnos_arm64_test_null_read_fault
    .global finnos_arm64_test_null_read_resume
finnos_arm64_test_null_read:
    mov x0, xzr
finnos_arm64_test_null_read_fault:
    ldr xzr, [x0]
finnos_arm64_test_null_read_resume:
    ret

    .balign 16
    .global finnos_arm64_test_guard_read
    .global finnos_arm64_test_guard_read_fault
    .global finnos_arm64_test_guard_read_resume
finnos_arm64_test_guard_read:
finnos_arm64_test_guard_read_fault:
    ldr xzr, [x0]
finnos_arm64_test_guard_read_resume:
    ret

    .balign 16
    .global finnos_arm64_test_text_write
    .global finnos_arm64_test_text_write_fault
    .global finnos_arm64_test_text_write_resume
finnos_arm64_test_text_write:
    ldr x1, [x0]
finnos_arm64_test_text_write_fault:
    str x1, [x0]
finnos_arm64_test_text_write_resume:
    ret

    .balign 16
    .global finnos_arm64_test_data_execute
    .global finnos_arm64_test_data_execute_resume
finnos_arm64_test_data_execute:
    stp x19, x30, [sp, #-16]!
    ldr w19, [x0]
    mov w1, #0x03c0
    movk w1, #0xd65f, lsl #16
    str w1, [x0]
    dc cvau, x0
    dsb ish
    ic ivau, x0
    dsb ish
    isb
    blr x0
finnos_arm64_test_data_execute_resume:
    str w19, [x0]
    dc cvau, x0
    dsb ish
    ic ivau, x0
    dsb ish
    isb
    ldp x19, x30, [sp], #16
    ret
"#
);

#[cfg(target_os = "none")]
unsafe extern "C" {
    static finnos_arm64_vector_table: u8;
}

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
unsafe extern "C" {
    fn finnos_arm64_test_null_read();
    static finnos_arm64_test_null_read_fault: u8;
    static finnos_arm64_test_null_read_resume: u8;
    fn finnos_arm64_test_guard_read(address: u64);
    static finnos_arm64_test_guard_read_fault: u8;
    static finnos_arm64_test_guard_read_resume: u8;
    fn finnos_arm64_test_text_write(address: u64);
    static finnos_arm64_test_text_write_fault: u8;
    static finnos_arm64_test_text_write_resume: u8;
    fn finnos_arm64_test_data_execute(address: u64);
    static finnos_arm64_test_data_execute_resume: u8;
}

/// Mask asynchronous exceptions and install the EL1 vector table.
///
/// This must run on the boot CPU before any handoff pointer is dereferenced.
///
/// # Errors
///
/// Returns an error unless firmware entered at EL1 or if the linked vector
/// table violates the required 2 KiB alignment.
#[cfg(target_os = "none")]
pub fn initialize() -> Result<(), InitializationError> {
    let current_el_raw: u64;
    // SAFETY: `CurrentEL` is readable at every supported privileged AArch64 EL.
    unsafe {
        core::arch::asm!(
            "mrs {raw}, CurrentEL",
            raw = out(reg) current_el_raw,
            options(nomem, nostack, preserves_flags)
        );
    }
    let level = decode_current_el(current_el_raw);
    if level != 1 {
        return Err(InitializationError::UnsupportedExceptionLevel(level));
    }
    let cpacr_el1: u64;
    // SAFETY: the supported handoff enters at EL1. Assembly entry has set
    // FPEN and executed ISB before Rust can use SIMD; this read-back proves
    // exception entry may save q0-q31 without recursively trapping at EL1.
    unsafe {
        core::arch::asm!(
            "mrs {value}, cpacr_el1",
            value = out(reg) cpacr_el1,
            options(nomem, nostack, preserves_flags)
        );
    }
    if !fp_simd_enabled(cpacr_el1) {
        return Err(InitializationError::FpSimdUnavailable);
    }
    let base = core::ptr::addr_of!(finnos_arm64_vector_table) as usize;
    if !base.is_multiple_of(VECTOR_ALIGNMENT) {
        return Err(InitializationError::MisalignedVectorBase);
    }
    // SAFETY: the table is linked resident and executable, all asynchronous
    // exceptions remain masked, and ISB makes the new VBAR visible before any
    // later potentially faulting handoff access.
    unsafe {
        core::arch::asm!(
            "msr daifset, #0xf",
            "msr vbar_el1, {base}",
            "isb",
            base = in(reg) base,
            options(nostack, preserves_flags)
        );
    }
    serial::line("FINNOS:KERNEL:ARM64_CURRENT_EL=1\n");
    serial::line("FINNOS:KERNEL:ARM64_EXCEPTION_VECTORS_READY\n");
    Ok(())
}

/// Execute and verify one controlled `AArch64` breakpoint exception.
///
/// This is available only in the isolated exception-test image.
#[cfg(all(target_os = "none", feature = "qemu-test-exceptions"))]
pub fn run_controlled_test() {
    serial::line("FINNOS:TEST:ARM64_EXCEPTIONS:BEGIN\n");
    if TEST_STATE
        .compare_exchange(TEST_IDLE, TEST_ARMED, Ordering::AcqRel, Ordering::Acquire)
        .is_err()
    {
        fatal("FINNOS:EXCEPTION:ARM64_TEST_STATE_ERROR\n");
    }
    serial::line("FINNOS:TEST:ARM64_EXCEPTIONS:BRK_BEGIN\n");
    // SAFETY: the vector table is installed and the dispatcher resumes only
    // this armed immediate after advancing ELR_EL1 by one instruction.
    unsafe {
        core::arch::asm!("brk #0xf100");
    }
    if TEST_STATE.swap(TEST_IDLE, Ordering::AcqRel) != TEST_OBSERVED {
        fatal("FINNOS:EXCEPTION:ARM64_TEST_NOT_OBSERVED\n");
    }
    serial::line("FINNOS:TEST:ARM64_EXCEPTIONS:BRK_PASS\n");
    serial::line("FINNOS:TEST:ARM64_EXCEPTIONS:PASS\n");
}

/// Verify the active EL1 address space with four exact, resumable faults.
///
/// `low_guard` must name an unmapped stack-guard page, `text_target` must name
/// an aligned readable/executable but read-only word, and `data_target` must
/// name an aligned writable but non-executable instruction-sized test cell.
/// The data cell is restored before this function returns.
#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
pub fn run_page_fault_test(low_guard: u64, text_target: u64, data_target: u64) {
    if low_guard == 0
        || !low_guard.is_multiple_of(4096)
        || text_target == 0
        || !text_target.is_multiple_of(8)
        || data_target == 0
        || !data_target.is_multiple_of(4)
        || PAGE_FAULT_STATE.load(Ordering::Acquire) != PAGE_FAULT_IDLE
    {
        fatal("FINNOS:EXCEPTION:ARM64_PAGE_FAULT_TEST_STATE_ERROR\n");
    }
    PAGE_FAULT_LOW_GUARD.store(low_guard, Ordering::Release);
    PAGE_FAULT_TEXT_TARGET.store(text_target, Ordering::Release);
    PAGE_FAULT_DATA_TARGET.store(data_target, Ordering::Release);

    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:BEGIN\n");

    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_BEGIN\n");
    arm_page_fault(PAGE_FAULT_NULL_ARMED);
    // SAFETY: the isolated test has installed its EL1 vector table and armed
    // exactly the null-read stub and its explicit resume symbol.
    unsafe { finnos_arm64_test_null_read() };
    observe_page_fault(PAGE_FAULT_NULL_OBSERVED);
    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_PASS\n");

    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:LOW_GUARD_READ_BEGIN\n");
    arm_page_fault(PAGE_FAULT_GUARD_ARMED);
    // SAFETY: the caller identifies the deliberately unmapped low guard and
    // the armed dispatcher resumes only the matching assembly stub.
    unsafe { finnos_arm64_test_guard_read(low_guard) };
    observe_page_fault(PAGE_FAULT_GUARD_OBSERVED);
    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:LOW_GUARD_READ_PASS\n");

    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:TEXT_WRITE_BEGIN\n");
    arm_page_fault(PAGE_FAULT_TEXT_WRITE_ARMED);
    // SAFETY: the stub reads and attempts to rewrite the same text word, so a
    // missing permission fault cannot silently alter the target bytes.
    unsafe { finnos_arm64_test_text_write(text_target) };
    observe_page_fault(PAGE_FAULT_TEXT_WRITE_OBSERVED);
    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:TEXT_WRITE_PASS\n");

    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:DATA_EXECUTE_BEGIN\n");
    arm_page_fault(PAGE_FAULT_DATA_EXEC_ARMED);
    // SAFETY: the stub temporarily writes one RET instruction into the caller's
    // dedicated RW/NX cell, attempts execution, and restores the old word at
    // its explicit resume symbol.
    unsafe { finnos_arm64_test_data_execute(data_target) };
    observe_page_fault(PAGE_FAULT_DATA_EXEC_OBSERVED);
    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:DATA_EXECUTE_PASS\n");

    serial::line("FINNOS:TEST:ARM64_PAGE_FAULTS:PASS\n");
}

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
fn arm_page_fault(armed: u8) {
    if PAGE_FAULT_STATE
        .compare_exchange(PAGE_FAULT_IDLE, armed, Ordering::AcqRel, Ordering::Acquire)
        .is_err()
    {
        fatal("FINNOS:EXCEPTION:ARM64_PAGE_FAULT_TEST_STATE_ERROR\n");
    }
}

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
fn observe_page_fault(observed: u8) {
    if PAGE_FAULT_STATE.swap(PAGE_FAULT_IDLE, Ordering::AcqRel) != observed {
        fatal("FINNOS:EXCEPTION:ARM64_PAGE_FAULT_NOT_OBSERVED\n");
    }
}

/// Trigger an unarmed breakpoint to verify the bounded fatal diagnostic path.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-exception-fatal"))]
pub fn run_fatal_test() -> ! {
    serial::line("FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN\n");
    // SAFETY: the installed vector table handles this deliberately unarmed
    // breakpoint as fatal, emits bounded diagnostics, and never returns.
    unsafe { core::arch::asm!("brk #0xf101") }
    loop {
        core::hint::spin_loop();
    }
}

#[cfg(any(target_os = "none", test))]
const fn controlled_resume_delta(state: u8, source: u64, esr: u64) -> Option<u64> {
    if state == TEST_ARMED
        && (source == SOURCE_CURRENT_SP0_SYNC || source == SOURCE_CURRENT_SPX_SYNC)
        && exception_class(esr) == EC_BRK64
        && breakpoint_immediate(esr) == CONTROLLED_BRK_IMMEDIATE
    {
        Some(4)
    } else {
        None
    }
}

#[cfg(all(target_os = "none", feature = "qemu-test-page-tables"))]
fn armed_page_fault_expectation(state: u8) -> Option<(ExpectedPageFault, u64, u64, u64, u8)> {
    let low_guard = PAGE_FAULT_LOW_GUARD.load(Ordering::Acquire);
    let text_target = PAGE_FAULT_TEXT_TARGET.load(Ordering::Acquire);
    let data_target = PAGE_FAULT_DATA_TARGET.load(Ordering::Acquire);
    match state {
        PAGE_FAULT_NULL_ARMED => Some((
            ExpectedPageFault::NullRead,
            0,
            core::ptr::addr_of!(finnos_arm64_test_null_read_fault) as u64,
            core::ptr::addr_of!(finnos_arm64_test_null_read_resume) as u64,
            PAGE_FAULT_NULL_OBSERVED,
        )),
        PAGE_FAULT_GUARD_ARMED => Some((
            ExpectedPageFault::GuardRead,
            low_guard,
            core::ptr::addr_of!(finnos_arm64_test_guard_read_fault) as u64,
            core::ptr::addr_of!(finnos_arm64_test_guard_read_resume) as u64,
            PAGE_FAULT_GUARD_OBSERVED,
        )),
        PAGE_FAULT_TEXT_WRITE_ARMED => Some((
            ExpectedPageFault::TextWrite,
            text_target,
            core::ptr::addr_of!(finnos_arm64_test_text_write_fault) as u64,
            core::ptr::addr_of!(finnos_arm64_test_text_write_resume) as u64,
            PAGE_FAULT_TEXT_WRITE_OBSERVED,
        )),
        PAGE_FAULT_DATA_EXEC_ARMED => Some((
            ExpectedPageFault::DataExecute,
            data_target,
            data_target,
            core::ptr::addr_of!(finnos_arm64_test_data_execute_resume) as u64,
            PAGE_FAULT_DATA_EXEC_OBSERVED,
        )),
        _ => None,
    }
}

#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
extern "C" fn finnos_arm64_exception_dispatch(frame: *mut ExceptionFrame) {
    // SAFETY: every vector slot allocates exactly `FRAME_SIZE` bytes and passes
    // its aligned SP. The frame remains exclusively owned until ERET.
    let frame = unsafe { &mut *frame };
    if frame.source == SOURCE_CURRENT_SPX_IRQ {
        match gic::handle_irq(frame.registers[19], frame.spsr) {
            gic::IrqDisposition::Handled | gic::IrqDisposition::Spurious(_) => return,
            gic::IrqDisposition::Unexpected {
                raw_iar,
                interrupt_id,
            } => {
                serial::hex_line(
                    "FINNOS:INTERRUPT:ARM64_GIC_ERROR:IAR=0x",
                    u64::from(raw_iar),
                );
                serial::hex_line(
                    "FINNOS:INTERRUPT:ARM64_GIC_ERROR:INTID=0x",
                    u64::from(interrupt_id),
                );
            }
            gic::IrqDisposition::ContextFault => {
                serial::line("FINNOS:INTERRUPT:ARM64_GIC_ERROR:CONTEXT\n");
            }
            gic::IrqDisposition::ControllerNotReady => {
                serial::line("FINNOS:INTERRUPT:ARM64_GIC_ERROR:NOT_READY\n");
            }
        }
        fatal("FINNOS:EXCEPTION:ARM64_FATAL\n");
    }
    #[cfg(feature = "qemu-test-page-tables")]
    {
        let state = PAGE_FAULT_STATE.load(Ordering::Acquire);
        if let Some((expected, far, elr, resume, observed)) = armed_page_fault_expectation(state)
            && page_fault_syndrome_matches(
                expected,
                frame.source,
                frame.esr,
                frame.far,
                frame.elr,
                far,
                elr,
            )
        {
            if resume == 0
                || PAGE_FAULT_STATE
                    .compare_exchange(state, observed, Ordering::AcqRel, Ordering::Acquire)
                    .is_err()
            {
                fatal("FINNOS:EXCEPTION:ARM64_PAGE_FAULT_TEST_STATE_ERROR\n");
            }
            frame.elr = resume;
            serial::line("FINNOS:EXCEPTION:ARM64_PAGE_FAULT\n");
            return;
        }
    }
    let state = TEST_STATE.load(Ordering::Acquire);
    if let Some(delta) = controlled_resume_delta(state, frame.source, frame.esr) {
        let Some(resume) = frame.elr.checked_add(delta) else {
            fatal("FINNOS:EXCEPTION:ARM64_ELR_OVERFLOW\n");
        };
        if TEST_STATE
            .compare_exchange(
                TEST_ARMED,
                TEST_OBSERVED,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
            .is_err()
        {
            fatal("FINNOS:EXCEPTION:ARM64_TEST_STATE_ERROR\n");
        }
        frame.elr = resume;
        serial::line("FINNOS:EXCEPTION:ARM64_BREAKPOINT\n");
        return;
    }
    serial::hex_line("FINNOS:EXCEPTION:ARM64_SOURCE=0x", frame.source);
    serial::hex_line("FINNOS:EXCEPTION:ARM64_ESR=0x", frame.esr);
    serial::hex_line("FINNOS:EXCEPTION:ARM64_ELR=0x", frame.elr);
    serial::hex_line("FINNOS:EXCEPTION:ARM64_FAR=0x", frame.far);
    serial::hex_line("FINNOS:EXCEPTION:ARM64_SPSR=0x", frame.spsr);
    serial::hex_line("FINNOS:EXCEPTION:ARM64_X0=0x", frame.registers[0]);
    fatal("FINNOS:EXCEPTION:ARM64_FATAL\n")
}

#[cfg(target_os = "none")]
fn fatal(marker: &str) -> ! {
    serial::line(marker);
    #[cfg(feature = "qemu-test-exit")]
    qemu::failure();
    #[cfg(not(feature = "qemu-test-exit"))]
    loop {
        // SAFETY: the fatal path has masked asynchronous exceptions and only
        // waits for an event; it does not access memory or return.
        unsafe { core::arch::asm!("wfe", options(nomem, nostack, preserves_flags)) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{offset_of, size_of};

    #[test]
    fn current_el_decoding_is_bounded() {
        assert_eq!(decode_current_el(0), 0);
        assert_eq!(decode_current_el(0b01 << 2), 1);
        assert_eq!(decode_current_el(0b10 << 2), 2);
        assert_eq!(decode_current_el(0b11 << 2), 3);
        assert_eq!(decode_current_el(u64::MAX), 3);
    }

    #[test]
    fn fp_simd_access_requires_both_fpen_bits() {
        assert!(!fp_simd_enabled(0));
        assert!(!fp_simd_enabled(1 << 20));
        assert!(!fp_simd_enabled(1 << 21));
        assert!(fp_simd_enabled(CPACR_EL1_FPEN_MASK));
        assert!(fp_simd_enabled(u64::MAX));
    }

    #[test]
    fn raw_frame_layout_matches_vector_assembly() {
        assert_eq!(offset_of!(ExceptionFrame, registers), 0);
        assert_eq!(offset_of!(ExceptionFrame, elr), 248);
        assert_eq!(offset_of!(ExceptionFrame, spsr), 256);
        assert_eq!(offset_of!(ExceptionFrame, esr), 264);
        assert_eq!(offset_of!(ExceptionFrame, far), 272);
        assert_eq!(offset_of!(ExceptionFrame, source), 280);
        assert_eq!(offset_of!(ExceptionFrame, vectors), 288);
        assert_eq!(offset_of!(ExceptionFrame, fpcr), 800);
        assert_eq!(offset_of!(ExceptionFrame, fpsr), 808);
        assert_eq!(size_of::<ExceptionFrame>(), FRAME_SIZE);
        assert_eq!(FRAME_SIZE % 16, 0);
    }

    #[test]
    fn controlled_breakpoint_policy_is_exact() {
        let esr = (u64::from(EC_BRK64) << 26) | u64::from(CONTROLLED_BRK_IMMEDIATE);
        assert_eq!(
            controlled_resume_delta(TEST_ARMED, SOURCE_CURRENT_SP0_SYNC, esr),
            Some(4)
        );
        assert_eq!(
            controlled_resume_delta(TEST_ARMED, SOURCE_CURRENT_SPX_SYNC, esr),
            Some(4)
        );
        assert_eq!(
            controlled_resume_delta(TEST_IDLE, SOURCE_CURRENT_SPX_SYNC, esr),
            None
        );
        assert_eq!(controlled_resume_delta(TEST_ARMED, 8, esr), None);
        assert_eq!(
            controlled_resume_delta(TEST_ARMED, SOURCE_CURRENT_SPX_SYNC, esr ^ 1),
            None
        );
        assert_eq!(
            controlled_resume_delta(TEST_ARMED, SOURCE_CURRENT_SPX_SYNC, esr ^ (1 << 26)),
            None
        );
    }

    #[test]
    fn page_fault_policy_matches_exact_syndrome_and_addresses() {
        let syndrome = |ec: u8, fsc: u64, write: bool| {
            (u64::from(ec) << 26)
                | ESR_INSTRUCTION_LENGTH
                | fsc
                | if write { ISS_WRITE_NOT_READ } else { 0 }
        };
        let far = 0x4000_u64;
        let elr = 0x8000_u64;
        for (expected, esr) in [
            (
                ExpectedPageFault::NullRead,
                syndrome(EC_DATA_ABORT_CURRENT_EL, 0x4, false),
            ),
            (
                ExpectedPageFault::GuardRead,
                syndrome(EC_DATA_ABORT_CURRENT_EL, 0x7, false),
            ),
            (
                ExpectedPageFault::TextWrite,
                syndrome(EC_DATA_ABORT_CURRENT_EL, 0xf, true),
            ),
            (
                ExpectedPageFault::DataExecute,
                syndrome(EC_INSTRUCTION_ABORT_CURRENT_EL, 0xd, false),
            ),
        ] {
            assert!(page_fault_syndrome_matches(
                expected,
                SOURCE_CURRENT_SPX_SYNC,
                esr,
                far,
                elr,
                far,
                elr,
            ));
            assert!(!page_fault_syndrome_matches(
                expected,
                SOURCE_CURRENT_SP0_SYNC,
                esr,
                far,
                elr,
                far,
                elr,
            ));
            for forbidden_bit in [ISS_FAR_NOT_VALID, ISS_STAGE_ONE_PAGE_TABLE_WALK] {
                assert!(!page_fault_syndrome_matches(
                    expected,
                    SOURCE_CURRENT_SPX_SYNC,
                    esr | forbidden_bit,
                    far,
                    elr,
                    far,
                    elr,
                ));
            }
            assert!(!page_fault_syndrome_matches(
                expected,
                SOURCE_CURRENT_SPX_SYNC,
                esr & !ESR_INSTRUCTION_LENGTH,
                far,
                elr,
                far,
                elr,
            ));
            assert!(!page_fault_syndrome_matches(
                expected,
                SOURCE_CURRENT_SPX_SYNC,
                esr,
                far + 1,
                elr,
                far,
                elr,
            ));
            assert!(!page_fault_syndrome_matches(
                expected,
                SOURCE_CURRENT_SPX_SYNC,
                esr,
                far,
                elr + 4,
                far,
                elr,
            ));
            assert!(!page_fault_syndrome_matches(
                expected,
                SOURCE_CURRENT_SPX_SYNC,
                esr ^ ISS_WRITE_NOT_READ,
                far,
                elr,
                far,
                elr,
            ));
        }
    }

    #[test]
    fn page_fault_policy_rejects_wrong_exception_and_fsc_classes() {
        let far = 0x1000_u64;
        let elr = 0x2000_u64;
        let data_translation =
            (u64::from(EC_DATA_ABORT_CURRENT_EL) << 26) | ESR_INSTRUCTION_LENGTH | 0x4;
        let data_permission =
            (u64::from(EC_DATA_ABORT_CURRENT_EL) << 26) | ESR_INSTRUCTION_LENGTH | 0xc;
        let instruction_permission =
            (u64::from(EC_INSTRUCTION_ABORT_CURRENT_EL) << 26) | ESR_INSTRUCTION_LENGTH | 0xc;
        assert!(!page_fault_syndrome_matches(
            ExpectedPageFault::TextWrite,
            SOURCE_CURRENT_SPX_SYNC,
            data_translation | ISS_WRITE_NOT_READ,
            far,
            elr,
            far,
            elr,
        ));
        assert!(!page_fault_syndrome_matches(
            ExpectedPageFault::GuardRead,
            SOURCE_CURRENT_SPX_SYNC,
            data_permission,
            far,
            elr,
            far,
            elr,
        ));
        assert!(!page_fault_syndrome_matches(
            ExpectedPageFault::DataExecute,
            SOURCE_CURRENT_SPX_SYNC,
            data_permission,
            far,
            elr,
            far,
            elr,
        ));
        assert!(page_fault_syndrome_matches(
            ExpectedPageFault::DataExecute,
            SOURCE_CURRENT_SPX_SYNC,
            instruction_permission,
            far,
            elr,
            far,
            elr,
        ));
    }
}
