//! BSP local APIC in xAPIC MMIO mode.
#![allow(missing_docs)]
#![allow(clippy::all, clippy::pedantic, clippy::nursery)]

use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use super::paging::{ActiveAddressSpace, MappingPermissions, PhysicalFrame, VirtualPage};

/// Fixed virtual address reserved for the local APIC page.
pub const LOCAL_APIC_VIRTUAL_BASE: u64 = 0x0000_3000_0000_0000;
/// IA32_APIC_BASE MSR.
pub const IA32_APIC_BASE: u32 = 0x1b;
/// xAPIC software-enable bit.
pub const APIC_SOFTWARE_ENABLE: u32 = 1 << 8;
/// Periodic timer bit in the timer LVT.
pub const TIMER_PERIODIC: u32 = 1 << 17;
/// Timer mask bit.
pub const LVT_MASKED: u32 = 1 << 16;
/// Local APIC timer vector.
pub const TIMER_VECTOR: u8 = 0x40;
/// Local APIC spurious vector.
pub const SPURIOUS_VECTOR: u8 = 0xff;

const ID: u32 = 0x020;
const VERSION: u32 = 0x030;
const EOI: u32 = 0x0b0;
const SPURIOUS: u32 = 0x0f0;
const ERROR_STATUS: u32 = 0x280;
const LVT_TIMER: u32 = 0x320;
const LVT_THERMAL: u32 = 0x330;
const LVT_PERFORMANCE: u32 = 0x340;
const LVT_LINT0: u32 = 0x350;
const LVT_LINT1: u32 = 0x360;
const LVT_ERROR: u32 = 0x370;
const TIMER_INITIAL: u32 = 0x380;
const TIMER_CURRENT: u32 = 0x390;
const TIMER_DIVIDE: u32 = 0x3e0;

static APIC_READY: AtomicBool = AtomicBool::new(false);
static EOI_COUNT: AtomicU64 = AtomicU64::new(0);
static SPURIOUS_EOI_COUNT: AtomicU64 = AtomicU64::new(0);

/// APIC initialization errors.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ApicError {
    /// CPUID does not advertise a local APIC.
    ApicUnsupported,
    /// x2APIC is active and this milestone will not transition modes.
    X2ApicActive,
    /// The MSR base is zero, misaligned, or too wide.
    InvalidApicBase,
    /// The page could not be mapped with the required MMIO permissions.
    ApicMappingFailed,
    /// A register offset is invalid.
    InvalidRegisterOffset,
    /// The APIC version is not plausible.
    InvalidVersion,
    /// Software enable or vector readback failed.
    SoftwareEnableFailed,
}

/// Decode and validate an IA32_APIC_BASE value.
pub fn decode_base(msr: u64, physical_width: u8) -> Result<(u64, bool), ApicError> {
    let x2apic = msr & (1 << 10) != 0;
    if x2apic {
        return Err(ApicError::X2ApicActive);
    }
    if physical_width < 36 || physical_width > 52 {
        return Err(ApicError::InvalidApicBase);
    }
    let base = msr & (((1u64 << physical_width) - 1) & !0xfff);
    if base == 0 || !base.is_multiple_of(4096) || base & !((1u64 << physical_width) - 1) != 0 {
        return Err(ApicError::InvalidApicBase);
    }
    Ok((base, msr & (1 << 11) != 0))
}

/// The xAPIC register block.
#[derive(Clone, Copy)]
pub struct LocalApic {
    physical_base: u64,
    virtual_base: u64,
    #[allow(dead_code)]
    lvt_count: u8,
}

impl LocalApic {
    /// Map and initialize the BSP local APIC.
    #[allow(unsafe_code)]
    pub fn initialize(space: &mut ActiveAddressSpace, width: u8) -> Result<Self, ApicError> {
        if core::arch::x86_64::__cpuid(1).edx & (1 << 9) == 0 {
            return Err(ApicError::ApicUnsupported);
        }
        let raw = rdmsr(IA32_APIC_BASE);
        let (physical, enabled) = decode_base(raw, width)?;
        if !enabled {
            wrmsr(IA32_APIC_BASE, raw | (1 << 11));
        }
        let readback = rdmsr(IA32_APIC_BASE);
        if readback & (1 << 11) == 0 {
            return Err(ApicError::SoftwareEnableFailed);
        }
        let frame = PhysicalFrame::new(physical, width).map_err(|_| ApicError::InvalidApicBase)?;
        let page =
            VirtualPage::new(LOCAL_APIC_VIRTUAL_BASE).map_err(|_| ApicError::InvalidApicBase)?;
        space
            .map_page(page, frame, MappingPermissions::kernel_mmio_rw_nx())
            .map_err(|_| ApicError::ApicMappingFailed)?;
        let candidate = Self {
            physical_base: physical,
            virtual_base: LOCAL_APIC_VIRTUAL_BASE,
            lvt_count: 0,
        };
        let version = candidate.read(VERSION)?;
        let lvt_count = ((version >> 16) as u8).saturating_add(1);
        if version & 0xff == 0 {
            return Err(ApicError::InvalidVersion);
        }
        let apic = Self {
            lvt_count,
            ..candidate
        };
        apic.write(LVT_TIMER, LVT_MASKED)?;
        apic.write(LVT_LINT0, LVT_MASKED)?;
        apic.write(LVT_LINT1, LVT_MASKED)?;
        if lvt_count > 4 {
            apic.write(LVT_THERMAL, LVT_MASKED)?;
        }
        if lvt_count > 5 {
            apic.write(LVT_PERFORMANCE, LVT_MASKED)?;
        }
        if lvt_count > 6 {
            apic.write(LVT_ERROR, LVT_MASKED)?;
        }
        apic.write(SPURIOUS, u32::from(SPURIOUS_VECTOR) | APIC_SOFTWARE_ENABLE)?;
        let spurious = apic.read(SPURIOUS)?;
        if spurious & 0xff != u32::from(SPURIOUS_VECTOR) || spurious & APIC_SOFTWARE_ENABLE == 0 {
            return Err(ApicError::SoftwareEnableFailed);
        }
        apic.write(ERROR_STATUS, 0)?;
        apic.write(ERROR_STATUS, 0)?;
        APIC_READY.store(true, Ordering::Release);
        Ok(apic)
    }

    /// Physical APIC base reported by IA32_APIC_BASE.
    #[must_use]
    pub const fn physical_base(self) -> u64 {
        self.physical_base
    }
    /// Virtual APIC base used by FinnOS.
    #[must_use]
    pub const fn virtual_base(self) -> u64 {
        self.virtual_base
    }
    /// Read an aligned 32-bit APIC register.
    #[allow(unsafe_code)]
    pub fn read(self, offset: u32) -> Result<u32, ApicError> {
        let address = self
            .virtual_base
            .checked_add(u64::from(offset))
            .ok_or(ApicError::InvalidRegisterOffset)?;
        if offset >= 4096 || !offset.is_multiple_of(16) {
            return Err(ApicError::InvalidRegisterOffset);
        }
        // SAFETY: The page is installed as supervisor RW NX MMIO before this object is used.
        Ok(unsafe { core::ptr::read_volatile(address as *const u32) })
    }
    /// Write an aligned 32-bit APIC register.
    #[allow(unsafe_code)]
    pub fn write(self, offset: u32, value: u32) -> Result<(), ApicError> {
        let address = self
            .virtual_base
            .checked_add(u64::from(offset))
            .ok_or(ApicError::InvalidRegisterOffset)?;
        if offset >= 4096 || !offset.is_multiple_of(16) {
            return Err(ApicError::InvalidRegisterOffset);
        }
        // SAFETY: The page is installed as supervisor RW NX MMIO before this object is used.
        unsafe {
            core::ptr::write_volatile(address as *mut u32, value);
        }
        Ok(())
    }
    /// APIC identifier.
    pub fn id(self) -> Result<u32, ApicError> {
        self.read(ID)
    }
    /// APIC version register.
    pub fn version(self) -> Result<u32, ApicError> {
        self.read(VERSION)
    }
    /// Configure the periodic timer registers, leaving it masked until the initial count write.
    pub fn program_timer(self, initial: u32) -> Result<(), ApicError> {
        if initial == 0 {
            return Err(ApicError::SoftwareEnableFailed);
        }
        self.write(TIMER_DIVIDE, 0x3)?;
        self.write(LVT_TIMER, u32::from(TIMER_VECTOR) | TIMER_PERIODIC)?;
        self.write(TIMER_INITIAL, initial)
    }
    /// Return the current timer count.
    pub fn timer_current(self) -> Result<u32, ApicError> {
        self.read(TIMER_CURRENT)
    }
    /// Return whether a vector is in an APIC in-service register.
    pub fn is_in_service(self, vector: u8) -> Result<bool, ApicError> {
        let register = 0x100 + (u32::from(vector) / 32) * 16;
        Ok(self.read(register)? & (1 << (vector % 32)) != 0)
    }
    /// Send one local APIC EOI.
    pub fn eoi(self) -> Result<(), ApicError> {
        self.write(EOI, 0)
    }
    /// Return the programmed LVT timer value.
    pub fn timer_lvt(self) -> Result<u32, ApicError> {
        self.read(LVT_TIMER)
    }
    /// Return the configured initial count.
    pub fn timer_initial(self) -> Result<u32, ApicError> {
        self.read(TIMER_INITIAL)
    }
}

/// Mark and send an EOI from the timer ISR without acquiring a lock or allocating.
#[allow(unsafe_code)]
pub fn timer_eoi() {
    if APIC_READY.load(Ordering::Acquire) {
        // SAFETY: APIC mapping and software enable were validated before interrupts were enabled.
        unsafe {
            core::ptr::write_volatile((LOCAL_APIC_VIRTUAL_BASE + u64::from(EOI)) as *mut u32, 0);
        }
        EOI_COUNT.fetch_add(1, Ordering::Relaxed);
    }
}

/// Count EOIs issued by the timer path.
#[must_use]
pub fn eoi_count() -> u64 {
    EOI_COUNT.load(Ordering::Acquire)
}
/// Count accidental spurious-path EOIs (must remain zero).
#[must_use]
pub fn spurious_eoi_count() -> u64 {
    SPURIOUS_EOI_COUNT.load(Ordering::Acquire)
}
/// Return whether local APIC setup completed.
#[must_use]
pub fn is_ready() -> bool {
    APIC_READY.load(Ordering::Acquire)
}

/// Inspect the timer vector's in-service bit through the fixed APIC mapping.
#[allow(unsafe_code)]
#[must_use]
pub fn timer_in_service() -> bool {
    if !is_ready() {
        return false;
    }
    // SAFETY: The APIC page was mapped and validated before APIC_READY was published.
    unsafe {
        core::ptr::read_volatile((LOCAL_APIC_VIRTUAL_BASE + 0x100 + 0x20) as *const u32)
            & (1 << (TIMER_VECTOR % 32))
            != 0
    }
}

#[allow(unsafe_code)]
fn rdmsr(msr: u32) -> u64 {
    let low: u32;
    let high: u32;
    // SAFETY: IA32_APIC_BASE is a documented ring-0 MSR and this is called with IF clear.
    unsafe {
        core::arch::asm!("rdmsr", in("ecx") msr, out("eax") low, out("edx") high, options(nostack, preserves_flags));
    }
    (u64::from(high) << 32) | u64::from(low)
}
#[allow(unsafe_code)]
fn wrmsr(msr: u32, value: u64) {
    // SAFETY: Only the global-enable bit of the documented APIC base MSR is changed.
    unsafe {
        core::arch::asm!("wrmsr", in("ecx") msr, in("eax") value as u32, in("edx") (value >> 32) as u32, options(nostack, preserves_flags));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::arch::x86_64::paging;
    #[test]
    fn decode_rejects_x2apic() {
        assert_eq!(
            decode_base(1 << 10 | 0xfee00000, 36),
            Err(ApicError::X2ApicActive)
        );
    }
    #[test]
    fn decode_preserves_enable_state() {
        assert_eq!(
            decode_base(1 << 11 | 0xfee00000, 36),
            Ok((0xfee00000, true))
        );
    }
    #[test]
    fn virtual_base_is_canonical_and_separate() {
        assert!(paging::is_canonical(LOCAL_APIC_VIRTUAL_BASE));
        assert_ne!(LOCAL_APIC_VIRTUAL_BASE, 0x0000_2000_0000_0000);
    }
}
