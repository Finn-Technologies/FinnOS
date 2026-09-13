//! Generic PCI bus abstraction and scanning.

/// PCI device location coordinates (bus, device, function).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PciAddress {
    /// Bus number (0..255).
    pub bus: u8,
    /// Device number on bus (0..31).
    pub device: u8,
    /// Function number on device (0..7).
    pub function: u8,
}

/// Standard PCI configuration header information.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PciDeviceInfo {
    /// Address coordinates.
    pub address: PciAddress,
    /// Vendor identification.
    pub vendor_id: u16,
    /// Device identification.
    pub device_id: u16,
    /// Command register.
    pub command: u16,
    /// Status register.
    pub status: u16,
    /// Base class code.
    pub class_code: u8,
    /// Subclass code.
    pub subclass: u8,
    /// Programming interface.
    pub prog_if: u8,
    /// Header layout type.
    pub header_type: u8,
    /// Subsystem vendor ID.
    pub subsystem_vendor_id: u16,
    /// Subsystem device ID.
    pub subsystem_id: u16,
}

#[cfg(target_arch = "x86_64")]
use crate::arch::x86_64::pci as arch_pci;

#[cfg(target_arch = "aarch64")]
use crate::arch::aarch64::pci as arch_pci;

#[cfg(not(any(target_arch = "x86_64", target_arch = "aarch64")))]
mod arch_pci {
    pub fn read_config_u32(_bus: u8, _dev: u8, _func: u8, _reg: u8) -> u32 {
        0xffff_ffff
    }
    pub fn write_config_u32(_bus: u8, _dev: u8, _func: u8, _reg: u8, _val: u32) {}
    pub fn read_config_u16(_bus: u8, _dev: u8, _func: u8, _reg: u8) -> u16 {
        0xffff
    }
    pub fn write_config_u16(_bus: u8, _dev: u8, _func: u8, _reg: u8, _val: u16) {}
    pub fn read_config_u8(_bus: u8, _dev: u8, _func: u8, _reg: u8) -> u8 {
        0xff
    }
    pub fn write_config_u8(_bus: u8, _dev: u8, _func: u8, _reg: u8, _val: u8) {}
}

impl PciDeviceInfo {
    /// Read a 32-bit register from this device's configuration space.
    #[must_use]
    pub fn read_u32(&self, reg: u8) -> u32 {
        arch_pci::read_config_u32(
            self.address.bus,
            self.address.device,
            self.address.function,
            reg,
        )
    }

    /// Write a 32-bit register to this device's configuration space.
    pub fn write_u32(&self, reg: u8, val: u32) {
        arch_pci::write_config_u32(
            self.address.bus,
            self.address.device,
            self.address.function,
            reg,
            val,
        );
    }

    /// Read a 16-bit register from this device's configuration space.
    #[must_use]
    pub fn read_u16(&self, reg: u8) -> u16 {
        arch_pci::read_config_u16(
            self.address.bus,
            self.address.device,
            self.address.function,
            reg,
        )
    }

    /// Write a 16-bit register to this device's configuration space.
    pub fn write_u16(&self, reg: u8, val: u16) {
        arch_pci::write_config_u16(
            self.address.bus,
            self.address.device,
            self.address.function,
            reg,
            val,
        );
    }

    /// Read an 8-bit register from this device's configuration space.
    #[must_use]
    pub fn read_u8(&self, reg: u8) -> u8 {
        arch_pci::read_config_u8(
            self.address.bus,
            self.address.device,
            self.address.function,
            reg,
        )
    }

    /// Read a Base Address Register (BAR 0..5).
    #[must_use]
    pub fn read_bar(&self, bar_index: u8) -> u32 {
        if bar_index >= 6 {
            return 0;
        }
        self.read_u32(0x10 + bar_index * 4)
    }

    /// Enable Bus Mastering and Memory/IO decoding in the PCI Command register.
    pub fn enable_bus_mastering(&self) {
        let mut cmd = self.read_u16(0x04);
        cmd |= (1 << 0) | (1 << 1) | (1 << 2);
        self.write_u16(0x04, cmd);
    }
}

/// Scan a PCI bus and invoke a callback for every discovered device.
pub fn scan_bus<F>(bus: u8, mut callback: F)
where
    F: FnMut(PciDeviceInfo),
{
    for dev in 0..32 {
        let vendor = arch_pci::read_config_u16(bus, dev, 0, 0x00);
        if vendor == 0xffff || vendor == 0 {
            continue;
        }
        let header_type = arch_pci::read_config_u8(bus, dev, 0, 0x0e);
        let max_functions = if header_type & 0x80 != 0 { 8 } else { 1 };
        for func in 0..max_functions {
            let vendor_id = arch_pci::read_config_u16(bus, dev, func, 0x00);
            if vendor_id == 0xffff || vendor_id == 0 {
                continue;
            }
            let device_id = arch_pci::read_config_u16(bus, dev, func, 0x02);
            let command = arch_pci::read_config_u16(bus, dev, func, 0x04);
            let status = arch_pci::read_config_u16(bus, dev, func, 0x06);
            let class_rev = arch_pci::read_config_u32(bus, dev, func, 0x08);
            let class_code = (class_rev >> 24) as u8;
            let subclass = ((class_rev >> 16) & 0xff) as u8;
            let prog_if = ((class_rev >> 8) & 0xff) as u8;
            let htype = arch_pci::read_config_u8(bus, dev, func, 0x0e);
            let sub_vendor = arch_pci::read_config_u16(bus, dev, func, 0x2c);
            let sub_id = arch_pci::read_config_u16(bus, dev, func, 0x2e);

            let info = PciDeviceInfo {
                address: PciAddress {
                    bus,
                    device: dev,
                    function: func,
                },
                vendor_id,
                device_id,
                command,
                status,
                class_code,
                subclass,
                prog_if,
                header_type: htype,
                subsystem_vendor_id: sub_vendor,
                subsystem_id: sub_id,
            };
            callback(info);
        }
    }
}

/// Returns `true` when the IDs identify a legacy `VirtIO` block device.
///
/// Matches vendor `0x1AF4` with device `0x1001` (see [`super::virtio`] for the
/// `VirtIO` policy foundation). This is a pure ID comparison for driver
/// matching; it touches no MMIO, IRQ, or DMA state.
#[must_use]
pub const fn is_virtio_block(vendor_id: u16, device_id: u16) -> bool {
    vendor_id == 0x1AF4 && device_id == 0x1001
}

/// Returns `true` when the IDs identify a `VirtIO-GPU` display device (`0x1AF4:0x1050` or `0x1AF4:0x103F`).
#[must_use]
pub const fn is_virtio_gpu(vendor_id: u16, device_id: u16) -> bool {
    vendor_id == 0x1AF4 && (device_id == 0x1050 || device_id == 0x103F)
}

#[cfg(test)]
mod tests {
    use super::{is_virtio_block, is_virtio_gpu};

    #[test]
    fn virtio_block_matcher_accepts_legacy_ids() {
        assert!(is_virtio_block(0x1AF4, 0x1001));
    }

    #[test]
    fn virtio_block_matcher_rejects_other_devices() {
        assert!(!is_virtio_block(0x1AF4, 0x1002));
        assert!(!is_virtio_block(0x8086, 0x1001));
        assert!(!is_virtio_block(0xffff, 0xffff));
    }

    #[test]
    fn virtio_gpu_matcher_accepts_standard_ids() {
        assert!(is_virtio_gpu(0x1AF4, 0x1050));
        assert!(is_virtio_gpu(0x1AF4, 0x103F));
        assert!(!is_virtio_gpu(0x1AF4, 0x1001));
        assert!(!is_virtio_gpu(0x8086, 0x1050));
    }
}
