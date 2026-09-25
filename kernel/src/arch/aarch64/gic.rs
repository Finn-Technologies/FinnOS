//! Bounded BSP ownership of the QEMU virt `GICv2` interrupt controller.
//!
//! Fixed MMIO addresses are an arm64-qemu platform contract. Discovery, SMP,
//! timers, and external-device routing remain separate milestones.

#![allow(unsafe_code)]

#[cfg(target_os = "none")]
use core::sync::atomic::{AtomicBool, AtomicU8, AtomicU32, AtomicU64, Ordering};

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
/// Non-secure EL1 physical timer PPI identifier.
pub const TIMER_PPI_ID: u32 = 30;
/// `GICv2` spurious interrupt identifier.
pub const SPURIOUS_INTERRUPT_ID: u32 = 1023;
/// First shared-peripheral interrupt identifier available to a driver.
pub const SPI_ID_START: u32 = 32;
/// Highest supported shared-peripheral interrupt identifier.
pub const SPI_ID_END: u32 = 1019;
/// Priority granted to a routed driver SPI.
pub const SPI_PRIORITY: u8 = 0x80;
/// QEMU virt `GICv2m` frame base, used for MSI/MSI-X message delivery.
pub const V2M_FRAME_BASE: u64 = 0x0802_0000;
/// `GICD_MSI_TYPER`: reports the frame's SPI window.
pub const V2M_MSI_TYPER: u64 = 0x008;
/// `GICD_MSPIR`: writing an interrupt identifier raises it.
pub const V2M_MSI_SETSPI_NS: u64 = 0x040;
/// State value consumed by the bounded assembly wait loop.
#[cfg(target_os = "none")]
pub const TEST_STATE_OBSERVED: u8 = 2;

#[cfg(any(target_os = "none", test))]
const INTERRUPT_ID_MASK: u32 = 0x3ff;
#[cfg(any(target_os = "none", test))]
const SPECIAL_INTERRUPT_ID_START: u32 = 1020;

/// Return whether an identifier is a routable shared peripheral interrupt.
///
/// PPIs and SGIs belong to the timer and self-IPI paths, and identifiers at or
/// above the special range are not device interrupts, so a driver may not
/// claim them.
#[must_use]
pub const fn is_routable_spi(id: u32) -> bool {
    id >= SPI_ID_START && id <= SPI_ID_END
}

/// Decode the `GICD_MSI_TYPER` word describing a `GICv2m` frame.
#[must_use]
pub const fn decode_v2m_typer(typer: u32) -> (u32, u32) {
    // QEMU encodes `val = (base_spi + 32) << 16 | num_spi`, so the identifier
    // count occupies the low half and the already-offset base identifier the
    // high half. No further offset is applied here.
    let num_spi = typer & 0xffff;
    let base_spi = typer >> 16;
    (base_spi, num_spi)
}

/// Return the distributor interrupt identifier the v2m frame raises first.
///
/// `GICD_MSI_TYPER` reports the frame's *MSI window*, whose identifiers are
/// offset from the frame's own first SPI by 32. QEMU subtracts that offset from
/// the value written to the doorbell and then pulses the frame's SPI line `n`,
/// which the machine wires to the GIC input `frame_base + n`. The identifier
/// that actually reaches the distributor is therefore the frame base, not the
/// first identifier in the reported window.
#[must_use]
pub const fn v2m_first_distributor_id(base_spi: u32) -> u32 {
    base_spi.saturating_sub(32)
}

/// Return the value a guest writes to the doorbell for one frame SPI line.
///
/// The frame subtracts its window offset from the value and pulses the
/// corresponding line, so the value is the window base plus the line index.
#[must_use]
pub const fn v2m_doorbell_value(window_base: u32, frame_line: u32) -> u32 {
    window_base.saturating_add(frame_line)
}

/// Return the distributor identifier one frame SPI line reaches.
#[must_use]
pub fn v2m_doorbell_to_distributor_id(
    window_base: u32,
    num_spi: u32,
    frame_line: u32,
) -> Option<u32> {
    // A line outside the frame's own table never reaches the distributor, even
    // if the resulting identifier would fall inside the global SPI range.
    if frame_line >= num_spi {
        return None;
    }
    let id = v2m_first_distributor_id(window_base).checked_add(frame_line)?;
    if is_routable_spi(id) { Some(id) } else { None }
}

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

/// `GICD_CTLR`/`GICC_CTLR` group 0 enable.
#[cfg(target_os = "none")]
const GICC_CTLR_GROUP0: u32 = 1 << 0;
/// `GICD_CTLR`/`GICC_CTLR` group 1 enable, required for routed device SPIs.
#[cfg(target_os = "none")]
const GICC_CTLR_GROUP1: u32 = 1 << 1;
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

#[cfg(target_os = "none")]
static LAST_V2M_TYPER: AtomicU32 = AtomicU32::new(0);

/// Maximum number of simultaneously routed device interrupts.
pub const MAX_DEVICE_HANDLERS: usize = 8;

/// One registered device-interrupt handler.
///
/// The handler runs in interrupt context, so it must be reentrant, must not
/// allocate, and must not take a lock that an interrupted context could hold.
///
/// Equality is deliberately not derived: a function pointer's address is not
/// a stable identity across codegen units, and handler ownership is decided by
/// the interrupt identifier alone.
#[derive(Clone, Copy, Debug)]
pub struct DeviceHandler {
    /// Routed shared-peripheral identifier this handler serves.
    pub interrupt_id: u32,
    /// Handler entry point.
    pub handler: extern "C" fn(),
}

// The table is exercised by host tests and by the bare-metal registration
// path; a non-test, non-kernel build simply has no GIC user yet.
#[allow(dead_code)]
struct DeviceHandlerTable {
    slots: [Option<DeviceHandler>; MAX_DEVICE_HANDLERS],
}

// The mutation helpers are used by the bare-metal registration path and by
// host tests; a plain library build has no GIC user yet.
#[cfg_attr(not(target_os = "none"), allow(dead_code))]
impl DeviceHandlerTable {
    /// Create an empty handler table.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            slots: [None; MAX_DEVICE_HANDLERS],
        }
    }

    /// Return the handler registered for an identifier, if any.
    #[must_use]
    pub fn find(&self, interrupt_id: u32) -> Option<DeviceHandler> {
        self.slots
            .iter()
            .flatten()
            .find(|entry| entry.interrupt_id == interrupt_id)
            .copied()
    }

    /// Register a handler, returning `false` for a duplicate or a full table.
    #[must_use]
    pub fn insert(&mut self, handler: DeviceHandler) -> bool {
        if self.find(handler.interrupt_id).is_some() {
            return false;
        }
        for slot in &mut self.slots {
            if slot.is_none() {
                *slot = Some(handler);
                return true;
            }
        }
        false
    }

    /// Remove the handler registered for an identifier, if any.
    pub fn remove(&mut self, interrupt_id: u32) {
        for slot in &mut self.slots {
            if let Some(entry) = slot
                && entry.interrupt_id == interrupt_id
            {
                *slot = None;
                return;
            }
        }
    }
}

#[cfg(target_os = "none")]
struct DeviceHandlerCell(core::cell::UnsafeCell<DeviceHandlerTable>);

// SAFETY: the BSP is the only CPU that touches the GIC, matching the existing
// single-owner contract used by the scheduler runtime.
#[cfg(target_os = "none")]
unsafe impl Sync for DeviceHandlerCell {}

#[cfg(target_os = "none")]
static DEVICE_HANDLERS: DeviceHandlerCell =
    DeviceHandlerCell(core::cell::UnsafeCell::new(DeviceHandlerTable::new()));

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
    /// A requested interrupt identifier is not a routable shared peripheral.
    InvalidInterruptId(u32),
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
    /// A routed driver interrupt was acknowledged, deactivated, and handed to
    /// its registered handler.
    Device {
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
    TimerTick,
    Device(u32),
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
    } else if id == TIMER_PPI_ID {
        AcknowledgeClass::TimerTick
    } else if is_routable_spi(id) {
        // A routed device SPI is owned by a registered handler, never by the
        // fail-closed fatal path.
        AcknowledgeClass::Device(id)
    } else {
        AcknowledgeClass::Unexpected(id)
    }
}

/// Return whether an acknowledged identifier is a routed device SPI.
#[cfg(test)]
#[must_use]
pub const fn classifies_as_device_spi(id: u32) -> bool {
    matches!(classify_acknowledge(id, false), AcknowledgeClass::Device(_))
}

#[cfg(test)]
mod routing_tests {
    use super::{
        DeviceHandler, DeviceHandlerTable, MAX_DEVICE_HANDLERS, SPI_ID_END, SPI_ID_START,
        TIMER_PPI_ID, classifies_as_device_spi, decode_v2m_typer, is_routable_spi, v2m_can_raise,
        v2m_doorbell_to_distributor_id, v2m_doorbell_value, v2m_first_distributor_id,
    };

    #[test]
    fn only_shared_peripherals_are_routable() {
        assert!(is_routable_spi(SPI_ID_START));
        assert!(is_routable_spi(SPI_ID_END));
        assert!(is_routable_spi(96));
        // PPIs and SGIs belong to the timer and self-IPI paths.
        assert!(!is_routable_spi(0));
        assert!(!is_routable_spi(1));
        assert!(!is_routable_spi(TIMER_PPI_ID));
        assert!(!is_routable_spi(31));
        // Special and spurious identifiers are not device interrupts.
        assert!(!is_routable_spi(1020));
        assert!(!is_routable_spi(1023));
        assert!(!is_routable_spi(u32::MAX));
    }

    #[test]
    fn routed_spi_acknowledges_to_a_device_handler_not_the_fatal_path() {
        // A routed SPI must never fall through to the fail-closed unexpected
        // branch, which would panic the kernel on the first device interrupt.
        assert!(classifies_as_device_spi(SPI_ID_START));
        assert!(classifies_as_device_spi(48));
        assert!(classifies_as_device_spi(80));
        assert!(classifies_as_device_spi(SPI_ID_END));
        // The timer PPI, the test SGI, and special identifiers keep their own
        // classifications and are not stolen by a device handler.
        assert!(!classifies_as_device_spi(TIMER_PPI_ID));
        assert!(!classifies_as_device_spi(1));
        assert!(!classifies_as_device_spi(1023));
    }

    extern "C" fn first_handler() {}
    extern "C" fn second_handler() {}

    #[test]
    fn handler_table_enforces_single_ownership() {
        let mut table = DeviceHandlerTable::new();
        assert!(table.find(SPI_ID_START).is_none());
        assert!(table.insert(DeviceHandler {
            interrupt_id: SPI_ID_START,
            handler: first_handler,
        }));
        // The same identifier can never be claimed by a second owner.
        assert!(!table.insert(DeviceHandler {
            interrupt_id: SPI_ID_START,
            handler: second_handler,
        }));
        assert!(table.find(SPI_ID_START).is_some());
        // A different identifier is a different owner.
        assert!(table.insert(DeviceHandler {
            interrupt_id: SPI_ID_START + 1,
            handler: second_handler,
        }));
        table.remove(SPI_ID_START);
        assert!(table.find(SPI_ID_START).is_none());
        // Removing an absent handler is harmless.
        table.remove(SPI_ID_START);
    }

    #[test]
    fn handler_table_refuses_to_exceed_its_bound() {
        let mut table = DeviceHandlerTable::new();
        for offset in 0..MAX_DEVICE_HANDLERS {
            let interrupt_id = SPI_ID_START + u32::try_from(offset).unwrap();
            assert!(table.insert(DeviceHandler {
                interrupt_id,
                handler: first_handler,
            }));
        }
        assert!(!table.insert(DeviceHandler {
            interrupt_id: SPI_ID_START + u32::try_from(MAX_DEVICE_HANDLERS).unwrap(),
            handler: second_handler,
        }));
    }

    #[test]
    fn v2m_typer_reports_the_frame_spi_window() {
        // QEMU's default base_spi is 0, and it stores base_spi + 32, so a
        // default frame with 64 SPIs reports identifiers 32..=95.
        let (base, count) = decode_v2m_typer((32 << 16) | 64);
        assert_eq!(base, 32);
        assert_eq!(count, 64);
        assert!(v2m_can_raise(base, count, 32));
        assert!(v2m_can_raise(base, count, 95));
        assert!(!v2m_can_raise(base, count, 31));
        assert!(!v2m_can_raise(base, count, 96));
        // A frame reporting no SPIs can raise nothing.
        assert!(!v2m_can_raise(0, 0, 32));
        // A higher base field shifts the window: 33 SPIs from identifier 64.
        let (base, count) = decode_v2m_typer((64 << 16) | 0b10_0001);
        assert_eq!(base, 64);
        assert_eq!(count, 33);
        assert!(v2m_can_raise(base, count, 64));
        assert!(!v2m_can_raise(base, count, 63));
    }

    #[test]
    fn frame_base_is_not_the_reported_window_base() {
        // QEMU wires the frame's SPI line n to the distributor input
        // frame_base + n, while TYPER reports base_spi + 32. Routing the
        // window base would enable a line the frame never pulses, so the
        // distributor identifier is the frame base.
        let (window_base, _count) = decode_v2m_typer((48 + 32) << 16 | 64);
        assert_eq!(window_base, 80);
        assert_eq!(v2m_first_distributor_id(window_base), 48);
        // A frame with no window offset collapses onto the same identifier.
        assert_eq!(v2m_first_distributor_id(0), 0);
        // The derived identifier must still be a routable SPI.
        assert!(is_routable_spi(v2m_first_distributor_id(window_base)));
    }

    #[test]
    fn doorbell_values_map_back_to_frame_lines() {
        // QEMU computes `spi = value - (base_spi + 32)` and pulses the frame's
        // own line, so the value written for a frame line is the window base
        // plus that line index, while the distributor identifier reached is the
        // frame base plus the same index.
        let (window_base, _count) = decode_v2m_typer((48 + 32) << 16 | 64);
        let frame_base = v2m_first_distributor_id(window_base);
        for line in 0u32..8 {
            let value = v2m_doorbell_value(window_base, line);
            assert_eq!(value, window_base + line);
            // The value must land inside the frame's accepted window.
            assert!(v2m_can_raise(window_base, 64, value));
            // And it must resolve to the distributor identifier for that line.
            assert_eq!(
                v2m_doorbell_to_distributor_id(window_base, 64, line),
                Some(frame_base + line)
            );
        }
        // A line beyond the table is rejected rather than wrapping silently.
        // A line past the frame's own table is rejected even though the
        // resulting identifier would still be a globally routable SPI.
        assert_eq!(v2m_doorbell_to_distributor_id(window_base, 64, 64), None);
        assert_eq!(v2m_doorbell_to_distributor_id(window_base, 64, 200), None);
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
        // Driver-routed SPIs are placed in group 1, and the distributor and CPU
        // interface must both be enabled for group 1 before such an interrupt
        // can be signalled at all. Enabling group 0 alone leaves group 1
        // interrupts invisible to the CPU interface.
        write_cpu(GICC_CTLR, GICC_CTLR_GROUP0 | GICC_CTLR_GROUP1);
        write_distributor(GICD_CTLR, GICC_CTLR_GROUP0 | GICC_CTLR_GROUP1);
        barrier();
    }
    // SAFETY: all values are read from initialized aligned registers.
    let readback_ok = unsafe {
        read_distributor(GICD_CTLR) & (GICC_CTLR_GROUP0 | GICC_CTLR_GROUP1)
            == (GICC_CTLR_GROUP0 | GICC_CTLR_GROUP1)
            && read_cpu(GICC_CTLR) & (GICC_CTLR_GROUP0 | GICC_CTLR_GROUP1)
                == (GICC_CTLR_GROUP0 | GICC_CTLR_GROUP1)
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

/// Enable PPI 30 (Physical Timer) on the distributor.
///
/// # Errors
///
/// Returns `IrqNotMasked` if called before controller initialization.
#[cfg(target_os = "none")]
pub fn enable_timer_ppi() -> Result<(), InitializationError> {
    if !READY.load(Ordering::Acquire) {
        return Err(InitializationError::IrqNotMasked);
    }
    // SAFETY: caller is single-BSP and GIC distributor is mapped.
    unsafe {
        let group = read_distributor(GICD_IGROUPR);
        write_distributor(GICD_IGROUPR, group & !(1 << TIMER_PPI_ID));
        let offset = GICD_IPRIORITYR + (u64::from(TIMER_PPI_ID) / 4) * 4;
        let shift = (TIMER_PPI_ID % 4) * 8;
        let prio = read_distributor(offset) & !(0xff << shift);
        write_distributor(offset, prio | (0x80 << shift));
        write_distributor(GICD_ISENABLER, 1 << TIMER_PPI_ID);
        barrier();
    }
    Ok(())
}

/// Route one shared peripheral interrupt to the requesting CPU.
///
/// The interrupt is placed in group 1 (non-secure at EL1), given
/// [`SPI_PRIORITY`], and enabled. Only routable SPI identifiers are accepted,
/// so this can never retarget the timer PPI or the self-SGI test path.
///
/// # Errors
///
/// Returns [`InitializationError::IrqNotMasked`] before controller
/// initialization and [`InitializationError::InvalidInterruptId`] for a
/// non-SPI identifier.
#[cfg(target_os = "none")]
pub fn route_spi(interrupt_id: u32) -> Result<(), InitializationError> {
    route_spi_in_group(interrupt_id, 1)
}

/// Route one shared peripheral interrupt into an explicit interrupt group.
///
/// Group 1 is the normal choice for a non-secure EL1 driver SPI. Group 0 is
/// available so a caller can A/B delivery between the two groups on a system
/// where only one of them reaches the CPU interface.
///
/// # Errors
///
/// Returns [`InitializationError::IrqNotMasked`] before controller
/// initialization, and [`InitializationError::InvalidInterruptId`] for a
/// non-SPI identifier or an unsupported group.
#[cfg(target_os = "none")]
pub fn route_spi_in_group(interrupt_id: u32, group: u8) -> Result<(), InitializationError> {
    if !READY.load(Ordering::Acquire) {
        return Err(InitializationError::IrqNotMasked);
    }
    if !is_routable_spi(interrupt_id) {
        return Err(InitializationError::InvalidInterruptId(interrupt_id));
    }
    if group > 1 {
        return Err(InitializationError::InvalidInterruptId(interrupt_id));
    }
    // SAFETY: caller is single-BSP, the distributor is mapped and initialized,
    // and the identifier was proven to be a routable SPI.
    unsafe {
        let bit = 1u32 << (interrupt_id % 32);
        let group_offset = (u64::from(interrupt_id) / 32) * 4;
        let groups = read_distributor(GICD_IGROUPR + group_offset);
        let selected = if group == 0 {
            groups & !bit
        } else {
            groups | bit
        };
        write_distributor(GICD_IGROUPR + group_offset, selected);
        let priority_offset = GICD_IPRIORITYR + (u64::from(interrupt_id) / 4) * 4;
        let shift = (interrupt_id % 4) * 8;
        let priority = read_distributor(priority_offset) & !(0xff << shift);
        write_distributor(
            priority_offset,
            priority | (u32::from(SPI_PRIORITY) << shift),
        );
        write_distributor(
            GICD_ISENABLER + group_offset,
            read_distributor(GICD_ISENABLER + group_offset) | bit,
        );
        barrier();
    }
    Ok(())
}

/// Remove one shared peripheral interrupt from the requesting CPU.
///
/// # Errors
///
/// Returns [`InitializationError::IrqNotMasked`] before controller
/// initialization and [`InitializationError::InvalidInterruptId`] for a
/// non-SPI identifier.
#[cfg(target_os = "none")]
pub fn unroute_spi(interrupt_id: u32) -> Result<(), InitializationError> {
    if !READY.load(Ordering::Acquire) {
        return Err(InitializationError::IrqNotMasked);
    }
    if !is_routable_spi(interrupt_id) {
        return Err(InitializationError::InvalidInterruptId(interrupt_id));
    }
    // SAFETY: caller is single-BSP and the distributor is mapped.
    unsafe {
        let bit = 1u32 << (interrupt_id % 32);
        let group_offset = (u64::from(interrupt_id) / 32) * 4;
        write_distributor(GICD_ICENABLER + group_offset, bit);
        write_distributor(GICD_ICPENDR + group_offset, bit);
        write_distributor(GICD_ICACTIVER + group_offset, bit);
        barrier();
    }
    Ok(())
}

/// Read the distributor's enable, group, and priority state for one SPI.
///
/// # Errors
///
/// Returns [`InitializationError`] before controller initialization or for a
/// non-SPI identifier.
#[cfg(target_os = "none")]
pub fn spi_routing_state(interrupt_id: u32) -> Result<(bool, bool, u8), InitializationError> {
    if !READY.load(Ordering::Acquire) {
        return Err(InitializationError::IrqNotMasked);
    }
    if !is_routable_spi(interrupt_id) {
        return Err(InitializationError::InvalidInterruptId(interrupt_id));
    }
    // SAFETY: the distributor is mapped and the identifier is a proven SPI.
    unsafe {
        let bit = 1u32 << (interrupt_id % 32);
        let group_offset = (u64::from(interrupt_id) / 32) * 4;
        let enable_offset = GICD_ISENABLER + group_offset;
        let enabled = read_distributor(enable_offset) & bit != 0;
        let group_one = read_distributor(GICD_IGROUPR + group_offset) & bit != 0;
        let priority_offset = GICD_IPRIORITYR + (u64::from(interrupt_id) / 4) * 4;
        let shift = (interrupt_id % 4) * 8;
        let priority = ((read_distributor(priority_offset) >> shift) & 0xff) as u8;
        Ok((enabled, group_one, priority))
    }
}

/// Return the group-enable bits currently published by the distributor and CPU
/// interface.
///
/// A routed group-1 SPI is invisible to the CPU interface unless both
/// registers carry the group-1 enable, so this is worth observing directly.
#[cfg(target_os = "none")]
#[must_use]
pub fn ctlr_group_enables() -> (u32, u32) {
    if !READY.load(Ordering::Acquire) {
        return (0, 0);
    }
    // SAFETY: READY publishes the initialized, mapped controller windows.
    unsafe { (read_distributor(GICD_CTLR), read_cpu(GICC_CTLR)) }
}

/// Read the distributor's per-SPI pending and active bits.
///
/// # Errors
///
/// Returns [`InitializationError`] before controller initialization or for a
/// non-SPI identifier.
#[cfg(target_os = "none")]
pub fn spi_pending_state(interrupt_id: u32) -> Result<(bool, bool), InitializationError> {
    if !READY.load(Ordering::Acquire) {
        return Err(InitializationError::IrqNotMasked);
    }
    if !is_routable_spi(interrupt_id) {
        return Err(InitializationError::InvalidInterruptId(interrupt_id));
    }
    // SAFETY: the distributor is mapped and the identifier is a proven SPI.
    unsafe {
        let bit = 1u32 << (interrupt_id % 32);
        let group_offset = (u64::from(interrupt_id) / 32) * 4;
        let pending = read_distributor(GICD_ICPENDR + group_offset) & bit != 0;
        let active = read_distributor(GICD_ICACTIVER + group_offset) & bit != 0;
        Ok((pending, active))
    }
}

/// Read the `GICv2m` frame's SPI window, if the frame is mapped.
///
/// `GICD_MSI_TYPER` reports how many SPI identifiers the frame can raise and
/// the frame's base offset, so a driver learns which identifiers are
/// deliverable here instead of assuming a window.
///
/// # Errors
///
/// Returns [`InitializationError::IrqNotMasked`] before the controller is
/// initialized.
#[cfg(target_os = "none")]
pub fn v2m_spi_window() -> Result<(u32, u32), InitializationError> {
    if !READY.load(Ordering::Acquire) {
        return Err(InitializationError::IrqNotMasked);
    }
    // SAFETY: the caller has mapped the v2m frame as device memory.
    let typer = unsafe { core::ptr::read_volatile((V2M_FRAME_BASE + V2M_MSI_TYPER) as *const u32) };
    LAST_V2M_TYPER.store(typer, Ordering::Release);
    Ok(decode_v2m_typer(typer))
}

/// Return the raw `GICD_MSI_TYPER` value last read from the v2m frame.
#[cfg(target_os = "none")]
#[must_use]
pub fn v2m_typer_raw() -> u32 {
    LAST_V2M_TYPER.load(Ordering::Acquire)
}

/// Return whether a `GICv2m` MSI message can raise this interrupt identifier.
#[must_use]
pub const fn v2m_can_raise(base_spi: u32, num_spi: u32, interrupt_id: u32) -> bool {
    num_spi != 0 && interrupt_id >= base_spi && interrupt_id < base_spi + num_spi
}

/// Write an MSI doorbell to raise one shared peripheral interrupt.
///
/// The identifier is masked to the ten bits `GICD_MSPIR` decodes. A zero
/// identifier is rejected, because it is a software-generated interrupt and
/// would not reach a device SPI.
///
/// # Errors
///
/// Returns [`InitializationError::InvalidInterruptId`] for an identifier
/// outside the ten-bit range the doorbell decodes.
#[cfg(target_os = "none")]
pub fn raise_v2m_spi(interrupt_id: u32) -> Result<(), InitializationError> {
    if interrupt_id == 0 || interrupt_id > INTERRUPT_ID_MASK {
        return Err(InitializationError::InvalidInterruptId(interrupt_id));
    }
    // SAFETY: the caller has mapped the v2m frame as device memory, and the
    // doorbell offset is 4-byte aligned.
    unsafe {
        core::ptr::write_volatile(
            (V2M_FRAME_BASE + V2M_MSI_SETSPI_NS) as *mut u32,
            interrupt_id,
        );
        barrier();
    }
    Ok(())
}

/// Register a handler for one routed device interrupt.
///
/// The identifier must already be routed through [`route_spi`]. Registering
/// the same identifier twice, or exceeding the bounded table, is rejected so
/// an interrupt can never be dispatched to an ambiguous owner.
///
/// # Errors
///
/// Returns [`InitializationError::InvalidInterruptId`] for a non-SPI
/// identifier, and [`InitializationError::RegisterReadback`] for a duplicate
/// registration or a full table.
#[cfg(target_os = "none")]
pub fn register_device_handler(handler: DeviceHandler) -> Result<(), InitializationError> {
    if !is_routable_spi(handler.interrupt_id) {
        return Err(InitializationError::InvalidInterruptId(
            handler.interrupt_id,
        ));
    }
    // SAFETY: registration is a BSP-only bootstrap step performed before the
    // device is permitted to raise an interrupt, so this single-BSP mutation
    // cannot overlap a concurrent interrupt-context lookup.
    unsafe {
        let table = &mut *DEVICE_HANDLERS.0.get();
        if table.insert(handler) {
            return Ok(());
        }
    }
    Err(InitializationError::RegisterReadback)
}

/// Remove a registered device-interrupt handler.
#[cfg(target_os = "none")]
pub fn unregister_device_handler(interrupt_id: u32) {
    // SAFETY: see `register_device_handler`; unregistration is likewise a
    // BSP-only step that runs after the interrupt has been unrouted.
    unsafe {
        let table = &mut *DEVICE_HANDLERS.0.get();
        table.remove(interrupt_id);
    }
}

/// Return the handler registered for an interrupt identifier, if any.
#[cfg(target_os = "none")]
#[must_use]
pub fn device_handler_for(interrupt_id: u32) -> Option<DeviceHandler> {
    // SAFETY: see `register_device_handler`. This is a bounded read that copies
    // the handler out by value and never yields a reference into the table, so
    // no reference can outlive an interrupt entry.
    unsafe {
        let table = &*DEVICE_HANDLERS.0.get();
        table.find(interrupt_id)
    }
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
        AcknowledgeClass::TimerTick => {
            FRAME_SENTINEL.store(frame_sentinel, Ordering::Relaxed);
            IRQ_SPSR.store(spsr, Ordering::Relaxed);
            LAST_IAR.store(u64::from(raw_iar), Ordering::Relaxed);
            super::timer::handle_tick();
            // SAFETY: EOIR receives the exact token returned by IAR.
            unsafe {
                write_cpu(GICC_EOIR, raw_iar);
                barrier();
            }
            EOIS.fetch_add(1, Ordering::Relaxed);
            DELIVERIES.fetch_add(1, Ordering::Relaxed);
            IrqDisposition::Handled
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
            // handler runs, because a driver handler may re-arm its device and
            // must not be racing the acknowledgement.
            // SAFETY: EOIR receives the exact unmodified IAR token once.
            unsafe {
                write_cpu(GICC_EOIR, raw_iar);
                barrier();
            }
            EOIS.fetch_add(1, Ordering::Relaxed);
            DELIVERIES.fetch_add(1, Ordering::Relaxed);
            if let Some(device) = device_handler_for(id) {
                // A routed device interrupt is serviced, not fatal. The handler
                // runs in interrupt context and owns its own device policy.
                (device.handler)();
                return IrqDisposition::Device { interrupt_id: id };
            }
            IrqDisposition::Unexpected {
                raw_iar,
                interrupt_id: id,
            }
        }
        AcknowledgeClass::Device(id) => {
            // A routed device SPI owns an active interrupt, so deactivate it
            // before the handler runs so a re-armed device cannot race the
            // acknowledgement.
            // SAFETY: EOIR receives the exact unmodified IAR token once.
            unsafe {
                write_cpu(GICC_EOIR, raw_iar);
                barrier();
            }
            EOIS.fetch_add(1, Ordering::Relaxed);
            DELIVERIES.fetch_add(1, Ordering::Relaxed);
            if let Some(device) = device_handler_for(id) {
                // A routed device interrupt is serviced, not fatal. The
                // handler runs in interrupt context and owns device policy.
                (device.handler)();
                return IrqDisposition::Device { interrupt_id: id };
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
        // A PPI that is neither the timer nor the test SGI is still unowned and
        // must fail closed rather than reach a device handler.
        assert_eq!(
            classify_acknowledge(16, true),
            AcknowledgeClass::Unexpected(16)
        );
        assert_eq!(
            classify_acknowledge(TIMER_PPI_ID, false),
            AcknowledgeClass::TimerTick
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
