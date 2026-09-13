//! x86-64 PCI configuration space access using legacy I/O ports 0xCF8 and 0xCFC.

#![allow(unsafe_code)]

const CONFIG_ADDRESS: u16 = 0x0cf8;
const CONFIG_DATA: u16 = 0x0cfc;

#[inline]
#[allow(clippy::missing_const_for_fn)]
fn pci_address(bus: u8, dev: u8, func: u8, reg: u8) -> u32 {
    0x8000_0000u32
        | (u32::from(bus) << 16)
        | (u32::from(dev & 0x1f) << 11)
        | (u32::from(func & 0x07) << 8)
        | (u32::from(reg) & 0xfc)
}

/// Read a 32-bit word from PCI configuration space.
#[must_use]
pub fn read_config_u32(bus: u8, dev: u8, func: u8, reg: u8) -> u32 {
    let address = pci_address(bus, dev, func, reg);
    let data: u32;
    // SAFETY: Accessing standard PCI configuration ports 0xCF8 and 0xCFC.
    unsafe {
        core::arch::asm!(
            "out dx, eax",
            in("dx") CONFIG_ADDRESS,
            in("eax") address,
            options(nomem, nostack, preserves_flags)
        );
        core::arch::asm!(
            "in eax, dx",
            in("dx") CONFIG_DATA,
            out("eax") data,
            options(nomem, nostack, preserves_flags)
        );
    }
    data
}

/// Write a 32-bit word to PCI configuration space.
pub fn write_config_u32(bus: u8, dev: u8, func: u8, reg: u8, value: u32) {
    let address = pci_address(bus, dev, func, reg);
    // SAFETY: Writing to standard PCI configuration ports.
    unsafe {
        core::arch::asm!(
            "out dx, eax",
            in("dx") CONFIG_ADDRESS,
            in("eax") address,
            options(nomem, nostack, preserves_flags)
        );
        core::arch::asm!(
            "out dx, eax",
            in("dx") CONFIG_DATA,
            in("eax") value,
            options(nomem, nostack, preserves_flags)
        );
    }
}

/// Read a 16-bit halfword from PCI configuration space.
#[must_use]
pub fn read_config_u16(bus: u8, dev: u8, func: u8, reg: u8) -> u16 {
    let val = read_config_u32(bus, dev, func, reg & !3);
    let shift = (reg & 2) * 8;
    ((val >> shift) & 0xffff) as u16
}

/// Read an 8-bit byte from PCI configuration space.
#[must_use]
pub fn read_config_u8(bus: u8, dev: u8, func: u8, reg: u8) -> u8 {
    let val = read_config_u32(bus, dev, func, reg & !3);
    let shift = (reg & 3) * 8;
    ((val >> shift) & 0xff) as u8
}

/// Write a 16-bit halfword to PCI configuration space.
pub fn write_config_u16(bus: u8, dev: u8, func: u8, reg: u8, value: u16) {
    let aligned_reg = reg & !3;
    let old = read_config_u32(bus, dev, func, aligned_reg);
    let shift = (reg & 2) * 8;
    let mask = !(0xffffu32 << shift);
    let new = (old & mask) | (u32::from(value) << shift);
    write_config_u32(bus, dev, func, aligned_reg, new);
}

/// Write an 8-bit byte to PCI configuration space.
pub fn write_config_u8(bus: u8, dev: u8, func: u8, reg: u8, value: u8) {
    let aligned_reg = reg & !3;
    let old = read_config_u32(bus, dev, func, aligned_reg);
    let shift = (reg & 3) * 8;
    let mask = !(0xffu32 << shift);
    let new = (old & mask) | (u32::from(value) << shift);
    write_config_u32(bus, dev, func, aligned_reg, new);
}
