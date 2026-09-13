//! ARM64 `PCIe` ECAM (Enhanced Configuration Access Mechanism) for QEMU virt machine.

#![allow(unsafe_code)]

/// Physical base of `PCIe` ECAM on QEMU virt.
///
/// QEMU places the ECAM at `0x40_1000_0000` when `highmem` is enabled (the
/// default for `virt,gic-version=2,secure=off`). The legacy low aperture at
/// `0x3f00_0000` is only used with `-machine highmem=off` and faults on the
/// default configuration (FAR `0x3f00_0000`, ESR `0x9600_0010`).
pub const PCIE_ECAM_BASE: u64 = 0x40_1000_0000;
/// Size of `PCIe` ECAM bus 0 aperture (1 MiB, covering 32 devices with 8 functions).
pub const PCIE_ECAM_SIZE: u64 = 0x0010_0000;

#[inline]
#[allow(clippy::missing_const_for_fn)]
fn ecam_offset(bus: u8, dev: u8, func: u8, reg: u8) -> u64 {
    (u64::from(bus) << 20)
        | (u64::from(dev & 0x1f) << 15)
        | (u64::from(func & 0x07) << 12)
        | (u64::from(reg) & 0xfc)
}

/// Read a 32-bit word from `PCIe` ECAM.
#[must_use]
pub fn read_config_u32(bus: u8, dev: u8, func: u8, reg: u8) -> u32 {
    let addr = PCIE_ECAM_BASE + ecam_offset(bus, dev, func, reg);
    // SAFETY: Read from mapped PCIe ECAM MMIO aperture.
    unsafe { core::ptr::read_volatile(addr as *const u32) }
}

/// Write a 32-bit word to `PCIe` ECAM.
pub fn write_config_u32(bus: u8, dev: u8, func: u8, reg: u8, value: u32) {
    let addr = PCIE_ECAM_BASE + ecam_offset(bus, dev, func, reg);
    // SAFETY: Write to mapped PCIe ECAM MMIO aperture.
    unsafe { core::ptr::write_volatile(addr as *mut u32, value) }
}

/// Read a 16-bit halfword from `PCIe` configuration space.
#[must_use]
pub fn read_config_u16(bus: u8, dev: u8, func: u8, reg: u8) -> u16 {
    let val = read_config_u32(bus, dev, func, reg & !3);
    let shift = (reg & 2) * 8;
    ((val >> shift) & 0xffff) as u16
}

/// Read an 8-bit byte from `PCIe` configuration space.
#[must_use]
pub fn read_config_u8(bus: u8, dev: u8, func: u8, reg: u8) -> u8 {
    let val = read_config_u32(bus, dev, func, reg & !3);
    let shift = (reg & 3) * 8;
    ((val >> shift) & 0xff) as u8
}

/// Write a 16-bit halfword to `PCIe` configuration space.
pub fn write_config_u16(bus: u8, dev: u8, func: u8, reg: u8, value: u16) {
    let aligned_reg = reg & !3;
    let old = read_config_u32(bus, dev, func, aligned_reg);
    let shift = (reg & 2) * 8;
    let mask = !(0xffffu32 << shift);
    let new = (old & mask) | (u32::from(value) << shift);
    write_config_u32(bus, dev, func, aligned_reg, new);
}

/// Write an 8-bit byte to `PCIe` configuration space.
pub fn write_config_u8(bus: u8, dev: u8, func: u8, reg: u8, value: u8) {
    let aligned_reg = reg & !3;
    let old = read_config_u32(bus, dev, func, aligned_reg);
    let shift = (reg & 3) * 8;
    let mask = !(0xffu32 << shift);
    let new = (old & mask) | (u32::from(value) << shift);
    write_config_u32(bus, dev, func, aligned_reg, new);
}
