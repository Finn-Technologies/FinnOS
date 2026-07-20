//! Bounded BSP ownership of the QEMU virt `GICv2` interrupt controller.
//!
//! Fixed MMIO addresses are an arm64-qemu platform contract. Discovery, SMP,
//! timers, and external-device routing remain separate milestones.

#![allow(unsafe_code)]

#[cfg(target_os = "none")]
use core::sync::atomic::{AtomicBool, AtomicU8, AtomicU64, Ordering};

#[cfg(target_os = "none")]
use crate::interrupt::InterruptContextGuard;

/// QEMU virt `GICv2` distributor base.
pub const DISTRIBUTOR_BASE: u64 = 0x0800_0000;
/// QEMU virt `GICv2` CPU-interface base.
pub const CPU_INTERFACE_BASE: u64 = 0x0801_0000;
/// Size of each owned `GICv2` MMIO window.
pub const INTERFACE_SIZE: u64 = 0x1_0000;
/// Self-targeted software interrupt used by the isolated test.
pub const TEST_SGI_ID: u32 = 1;
/// `GICv2` spurious interrupt identifier.
pub const SPURIOUS_INTERRUPT_ID: u32 = 1023;
/// State value consumed by the bounded assembly wait loop.
#[cfg(target_os = "none")]
pub const TEST_STATE_OBSERVED: u8 = 2;

#[cfg(any(target_os = "none", test))]
const INTERRUPT_ID_MASK: u32 = 0x3ff;
#[cfg(any(target_os = "none", test))]
const SPECIAL_INTERRUPT_ID_START: u32 = 1020;
#[cfg(target_os = "none")]
const GICD_CTLR: u64 = 0x000;
#[cfg(target_os = "none")]
const GICD_TYPER: u64 = 0x004;
#[cfg(target_os = "none")]
const GICD_IGROUPR: u64 = 0x080;
#[cfg(target_os = "none")]
const GICD_ISENABLER: u64 = 0x100;
#[cfg(target_os = "none")]
const GICD_ICENABLER: u64 = 0x180;
#[cfg(target_os = "none")]
const GICD_ICPENDR: u64 = 0x280;
#[cfg(target_os = "none")]
const GICD_ICACTIVER: u64 = 0x380;
#[cfg(target_os = "none")]
const GICD_IPRIORITYR: u64 = 0x400;
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
const GICD_SGIR: u64 = 0xf00;
#[cfg(target_os = "none")]
const GICD_CPENDSGIR: u64 = 0xf10;
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
const GICD_SPENDSGIR: u64 = 0xf20;
#[cfg(target_os = "none")]
const GICC_CTLR: u64 = 0x000;
#[cfg(target_os = "none")]
const GICC_PMR: u64 = 0x004;
#[cfg(target_os = "none")]
const GICC_BPR: u64 = 0x008;
#[cfg(target_os = "none")]
const GICC_IAR: u64 = 0x00c;
#[cfg(target_os = "none")]
const GICC_EOIR: u64 = 0x010;
#[cfg(target_os = "none")]
const GICC_IIDR: u64 = 0x0fc;
#[cfg(target_os = "none")]
const TEST_STATE_IDLE: u8 = 0;
#[cfg(target_os = "none")]
const TEST_STATE_ARMED: u8 = 1;

#[cfg(target_os = "none")]
static READY: AtomicBool = AtomicBool::new(false);
#[cfg(target_os = "none")]
static TEST_STATE: AtomicU8 = AtomicU8::new(TEST_STATE_IDLE);
#[cfg(target_os = "none")]
static DELIVERIES: AtomicU64 = AtomicU64::new(0);
#[cfg(target_os = "none")]
static EOIS: AtomicU64 = AtomicU64::new(0);
#[cfg(target_os = "none")]
static SPURIOUS: AtomicU64 = AtomicU64::new(0);
#[cfg(target_os = "none")]
static FRAME_SENTINEL: AtomicU64 = AtomicU64::new(0);
#[cfg(target_os = "none")]
static IRQ_SPSR: AtomicU64 = AtomicU64::new(0);
#[cfg(target_os = "none")]
static LAST_IAR: AtomicU64 = AtomicU64::new(0);

/// Controller initialization failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InitializationError {
    /// Controller setup was attempted with IRQ exceptions unmasked.
    IrqNotMasked,
    /// The singleton controller was initialized more than once.
    AlreadyInitialized,
    /// `GICD_TYPER` described an impossible interrupt-line count.
    InvalidTyper,
    /// An initialization register failed exact readback.
    RegisterReadback,
}

/// Verified controller identity and capacity.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ControllerInfo {
    /// Raw `GICD_TYPER`.
    pub typer: u32,
    /// Raw `GICC_IIDR`.
    pub iidr: u32,
    /// Implemented interrupt identifier slots.
    pub interrupt_lines: u32,
}

/// Result of one current-EL IRQ dispatch.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IrqDisposition {
    /// The armed self-SGI was acknowledged and deactivated.
    Handled,
    /// A special/spurious ID was reported; no EOI was issued.
    Spurious(u32),
    /// A normal unsupported interrupt was acknowledged and deactivated.
    Unexpected {
        /// Exact token returned by the primary interrupt-acknowledge register.
        raw_iar: u32,
        /// Decoded ten-bit interrupt identifier.
        interrupt_id: u32,
    },
    /// Interrupt-context accounting could not enter safely.
    ContextFault,
    /// Dispatch occurred before controller publication.
    ControllerNotReady,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[cfg(any(target_os = "none", test))]
enum AcknowledgeClass {
    ExpectedSgi,
    Special(u32),
    Unexpected(u32),
}

#[cfg(any(target_os = "none", test))]
const fn classify_acknowledge(raw_iar: u32, armed: bool) -> AcknowledgeClass {
    let id = raw_iar & INTERRUPT_ID_MASK;
    if id >= SPECIAL_INTERRUPT_ID_START {
        AcknowledgeClass::Special(id)
    } else if id == TEST_SGI_ID && raw_iar & (0x7 << 10) == 0 && armed {
        AcknowledgeClass::ExpectedSgi
    } else {
        AcknowledgeClass::Unexpected(id)
    }
}

#[cfg(any(target_os = "none", test))]
const fn register_offset_valid(offset: u64) -> bool {
    offset.is_multiple_of(4) && offset <= INTERFACE_SIZE - 4
}

/// Initialize the fixed `GICv2` distributor and BSP CPU interface.
///
/// Production IRQ delivery remains masked in DAIF after return.
///
/// # Errors
///
/// Returns an error if IRQ delivery is already unmasked, initialization was
/// already published, the controller capacity is invalid, or interface
/// readback does not match the programmed policy.
///
/// # Safety
///
/// Both complete GIC windows must be mapped Device RW/NX and the caller must be
/// the only executing CPU.
#[cfg(target_os = "none")]
pub unsafe fn initialize() -> Result<ControllerInfo, InitializationError> {
    if !irq_is_masked() {
        return Err(InitializationError::IrqNotMasked);
    }
    if READY.load(Ordering::Acquire) {
        return Err(InitializationError::AlreadyInitialized);
    }
    // SAFETY: the caller owns both mapped windows and all offsets are aligned.
    unsafe {
        write_cpu(GICC_CTLR, 0);
        write_distributor(GICD_CTLR, 0);
        barrier();
    }
    // SAFETY: GICD_TYPER is a read-only aligned register.
    let typer = unsafe { read_distributor(GICD_TYPER) };
    let groups = (typer & 0x1f) + 1;
    if groups == 0 || groups > 32 {
        return Err(InitializationError::InvalidTyper);
    }

    // Keep every PPI and SPI disabled/inactive. SGIs remain architecturally
    // enabled; clear their banked pending bits through CPENDSGIR.
    // SAFETY: computed offsets remain aligned and inside GICD.
    unsafe {
        write_distributor(GICD_ICENABLER, 0xffff_0000);
        write_distributor(GICD_ICPENDR, 0xffff_0000);
        write_distributor(GICD_ICACTIVER, 0xffff_0000);
        for group in 1..groups {
            let delta = u64::from(group) * 4;
            write_distributor(GICD_ICENABLER + delta, u32::MAX);
            write_distributor(GICD_ICPENDR + delta, u32::MAX);
            write_distributor(GICD_ICACTIVER + delta, u32::MAX);
        }
        for register in 0..4u64 {
            write_distributor(GICD_CPENDSGIR + register * 4, u32::MAX);
        }
        write_distributor(
            GICD_IGROUPR,
            read_distributor(GICD_IGROUPR) & !(1 << TEST_SGI_ID),
        );
        write_distributor(GICD_ISENABLER, 1 << TEST_SGI_ID);
        let priorities = read_distributor(GICD_IPRIORITYR) & !(0xff << 8);
        write_distributor(GICD_IPRIORITYR, priorities | (0x80 << 8));
        write_cpu(GICC_PMR, 0xff);
        write_cpu(GICC_BPR, 0);
        write_cpu(GICC_CTLR, 1);
        write_distributor(GICD_CTLR, 1);
        barrier();
    }
    // SAFETY: all values are read from initialized aligned registers.
    let readback_ok = unsafe {
        read_distributor(GICD_CTLR) & 1 != 0
            && read_cpu(GICC_CTLR) & 1 != 0
            && read_cpu(GICC_PMR) & 0xff == 0xff
            && read_distributor(GICD_IGROUPR) & (1 << TEST_SGI_ID) == 0
            && (read_distributor(GICD_IPRIORITYR) >> 8) & 0xff == 0x80
    };
    if !readback_ok {
        // SAFETY: setup still owns both mapped interfaces and leaves them
        // disabled before reporting a failed publication.
        unsafe {
            write_cpu(GICC_CTLR, 0);
            write_distributor(GICD_CTLR, 0);
            barrier();
        }
        return Err(InitializationError::RegisterReadback);
    }
    // SAFETY: GICC_IIDR is read-only and aligned.
    let iidr = unsafe { read_cpu(GICC_IIDR) };
    READY.store(true, Ordering::Release);
    Ok(ControllerInfo {
        typer,
        iidr,
        interrupt_lines: groups * 32,
    })
}

/// Acknowledge and complete one IRQ without logging or allocation.
#[cfg(target_os = "none")]
pub fn handle_irq(frame_sentinel: u64, spsr: u64) -> IrqDisposition {
    let Ok(_guard) = InterruptContextGuard::enter() else {
        return IrqDisposition::ContextFault;
    };
    if !READY.load(Ordering::Acquire) {
        return IrqDisposition::ControllerNotReady;
    }
    // SAFETY: READY publishes the initialized, mapped CPU interface.
    let raw_iar = unsafe { read_cpu(GICC_IAR) };
    let armed = TEST_STATE.load(Ordering::Acquire) == TEST_STATE_ARMED;
    match classify_acknowledge(raw_iar, armed) {
        AcknowledgeClass::Special(id) => {
            SPURIOUS.fetch_add(1, Ordering::Relaxed);
            IrqDisposition::Spurious(id)
        }
        AcknowledgeClass::ExpectedSgi => {
            FRAME_SENTINEL.store(frame_sentinel, Ordering::Relaxed);
            IRQ_SPSR.store(spsr, Ordering::Relaxed);
            LAST_IAR.store(u64::from(raw_iar), Ordering::Relaxed);
            // SAFETY: EOIR receives the exact token returned by IAR.
            unsafe {
                write_cpu(GICC_EOIR, raw_iar);
                barrier();
            }
            EOIS.fetch_add(1, Ordering::Relaxed);
            DELIVERIES.fetch_add(1, Ordering::Relaxed);
            if TEST_STATE
                .compare_exchange(
                    TEST_STATE_ARMED,
                    TEST_STATE_OBSERVED,
                    Ordering::Release,
                    Ordering::Acquire,
                )
                .is_ok()
            {
                IrqDisposition::Handled
            } else {
                IrqDisposition::Unexpected {
                    raw_iar,
                    interrupt_id: TEST_SGI_ID,
                }
            }
        }
        AcknowledgeClass::Unexpected(id) => {
            // Normal IDs own an active interrupt, so deactivate before the
            // dispatcher takes its fail-closed fatal path.
            // SAFETY: EOIR receives the exact unmodified IAR token once.
            unsafe {
                write_cpu(GICC_EOIR, raw_iar);
                barrier();
            }
            IrqDisposition::Unexpected {
                raw_iar,
                interrupt_id: id,
            }
        }
    }
}

/// Read an idle IAR for the isolated spurious test without issuing EOI.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
pub fn observe_spurious_for_test() -> u32 {
    // SAFETY: the CPU interface is initialized and IRQ remains masked.
    let id = unsafe { read_cpu(GICC_IAR) } & INTERRUPT_ID_MASK;
    if id >= SPECIAL_INTERRUPT_ID_START {
        SPURIOUS.fetch_add(1, Ordering::Relaxed);
    }
    id
}

/// Arm the isolated SGI test while IRQ delivery is masked.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
pub fn arm_test() -> bool {
    irq_is_masked()
        && READY.load(Ordering::Acquire)
        && TEST_STATE
            .compare_exchange(
                TEST_STATE_IDLE,
                TEST_STATE_ARMED,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
            .is_ok()
}

/// Generate SGI1 targeting only the requesting BSP.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
pub fn issue_test_sgi() {
    // TargetListFilter=2 means the requesting CPU. DSB orders the armed state
    // publication before the distributor observes this write.
    // SAFETY: the initialized distributor is mapped and exclusively owned.
    unsafe {
        barrier();
        write_distributor(GICD_SGIR, (0b10 << 24) | TEST_SGI_ID);
        barrier();
    }
}

/// Return whether BSP source zero has SGI1 pending without acknowledging it.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
#[must_use]
pub fn test_sgi_pending() -> bool {
    // SPENDSGIR0 holds four source bytes; SGI1 is byte one and source CPU zero
    // is its low bit. Reading it neither acknowledges nor deactivates the SGI.
    // SAFETY: the banked pending register is aligned in the mapped distributor.
    unsafe { read_distributor(GICD_SPENDSGIR) & (1 << 8) != 0 }
}

/// Address of the atomic byte consumed by the assembly wait loop.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
#[must_use]
pub fn test_state_address() -> *const u8 {
    core::ptr::addr_of!(TEST_STATE).cast::<u8>()
}

/// Return whether the isolated SGI was observed.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
pub fn test_observed() -> bool {
    TEST_STATE.load(Ordering::Acquire) == TEST_STATE_OBSERVED
}

/// Expected-SGI delivery count.
#[cfg(target_os = "none")]
pub fn deliveries() -> u64 {
    DELIVERIES.load(Ordering::Acquire)
}
/// Exact-IAR EOI count.
#[cfg(target_os = "none")]
pub fn eois() -> u64 {
    EOIS.load(Ordering::Acquire)
}
/// Special/spurious count.
#[cfg(target_os = "none")]
pub fn spurious_count() -> u64 {
    SPURIOUS.load(Ordering::Acquire)
}
/// Raw expected-SGI IAR token.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
pub fn last_iar() -> u64 {
    LAST_IAR.load(Ordering::Acquire)
}
/// x19 value saved in the IRQ frame.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
pub fn frame_sentinel() -> u64 {
    FRAME_SENTINEL.load(Ordering::Acquire)
}
/// SPSR value saved in the IRQ frame.
#[cfg(all(target_os = "none", feature = "qemu-test-arm64-gic"))]
pub fn irq_spsr() -> u64 {
    IRQ_SPSR.load(Ordering::Acquire)
}

/// Read the current DAIF value.
#[cfg(target_os = "none")]
#[must_use]
pub fn daif() -> u64 {
    let value: u64;
    // SAFETY: DAIF is readable at EL1 without side effects.
    unsafe {
        core::arch::asm!(
            "mrs {value}, daif",
            value = out(reg) value,
            options(nomem, nostack, preserves_flags)
        );
    }
    value
}

#[cfg(target_os = "none")]
fn irq_is_masked() -> bool {
    daif() & (1 << 7) != 0
}

#[cfg(target_os = "none")]
unsafe fn read_distributor(offset: u64) -> u32 {
    debug_assert!(register_offset_valid(offset));
    // SAFETY: caller proves the GICD mapping; the offset stays aligned/in-range.
    unsafe { core::ptr::read_volatile((DISTRIBUTOR_BASE + offset) as *const u32) }
}
#[cfg(target_os = "none")]
unsafe fn write_distributor(offset: u64, value: u32) {
    debug_assert!(register_offset_valid(offset));
    // SAFETY: caller proves the GICD mapping; the offset stays aligned/in-range.
    unsafe { core::ptr::write_volatile((DISTRIBUTOR_BASE + offset) as *mut u32, value) }
}
#[cfg(target_os = "none")]
unsafe fn read_cpu(offset: u64) -> u32 {
    debug_assert!(register_offset_valid(offset));
    // SAFETY: caller proves the GICC mapping; the offset stays aligned/in-range.
    unsafe { core::ptr::read_volatile((CPU_INTERFACE_BASE + offset) as *const u32) }
}
#[cfg(target_os = "none")]
unsafe fn write_cpu(offset: u64, value: u32) {
    debug_assert!(register_offset_valid(offset));
    // SAFETY: caller proves the GICC mapping; the offset stays aligned/in-range.
    unsafe { core::ptr::write_volatile((CPU_INTERFACE_BASE + offset) as *mut u32, value) }
}
#[cfg(target_os = "none")]
unsafe fn barrier() {
    // SAFETY: DSB completes Device accesses and ISB synchronizes control state.
    unsafe {
        core::arch::asm!("dsb sy", "isb", options(nomem, nostack, preserves_flags));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn register_offsets_are_aligned_and_bounded() {
        assert!(register_offset_valid(0));
        assert!(register_offset_valid(INTERFACE_SIZE - 4));
        assert!(!register_offset_valid(2));
        assert!(!register_offset_valid(INTERFACE_SIZE));
    }

    #[test]
    fn acknowledge_policy_is_fail_closed() {
        assert_eq!(
            classify_acknowledge(TEST_SGI_ID, true),
            AcknowledgeClass::ExpectedSgi
        );
        assert_eq!(
            classify_acknowledge(TEST_SGI_ID, false),
            AcknowledgeClass::Unexpected(TEST_SGI_ID)
        );
        assert_eq!(
            classify_acknowledge(33, true),
            AcknowledgeClass::Unexpected(33)
        );
    }

    #[test]
    fn special_ids_never_enter_normal_eoi_path() {
        for id in SPECIAL_INTERRUPT_ID_START..=SPURIOUS_INTERRUPT_ID {
            assert_eq!(
                classify_acknowledge(id, true),
                AcknowledgeClass::Special(id)
            );
        }
    }

    #[test]
    fn self_sgi_rejects_a_non_bsp_source_cpu() {
        assert_eq!(
            classify_acknowledge((0b101 << 10) | TEST_SGI_ID, true),
            AcknowledgeClass::Unexpected(TEST_SGI_ID)
        );
    }
}
