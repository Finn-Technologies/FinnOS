//! Generic PCI bus abstraction and scanning.

use crate::drivers::virtio::pci::{VirtioPciCapabilities, VirtioPciCapabilityKind};

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

/// A decoded memory base-address register.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PciMemoryBar {
    /// BAR number (`0..=4`) selected by the capability.
    pub index: u8,
    /// Decoded physical base address with the type bits removed.
    pub address: u64,
    /// Whether the BAR is a 64-bit memory BAR.
    pub is_64_bit: bool,
    /// Whether the firmware marked the BAR prefetchable.
    pub prefetchable: bool,
}

/// Errors returned while decoding a PCI memory BAR.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PciBarError {
    /// The requested BAR index is outside the six standard BAR slots.
    InvalidIndex(u8),
    /// The BAR is an I/O BAR rather than a memory BAR.
    NotMemory(u8),
    /// The BAR type field is reserved.
    ReservedType(u8),
    /// The BAR has not been assigned an address.
    Unassigned(u8),
    /// A 64-bit BAR has no valid upper dword.
    InvalidUpperDword(u8),
}

/// One validated `VirtIO` PCI memory window.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PciVirtioRegion {
    /// BAR containing this region.
    pub bar_index: u8,
    /// Physical base address of the containing BAR.
    pub bar_address: u64,
    /// Region offset within the BAR.
    pub offset: u32,
    /// Physical base address of the region.
    pub physical_base: u64,
    /// Region length in bytes.
    pub byte_len: u32,
}

/// Common, notification, and device windows for a modern `VirtIO` device.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PciVirtioRegions {
    /// Common configuration structure.
    pub common: PciVirtioRegion,
    /// Queue-notification structure.
    pub notify: PciVirtioRegion,
    /// Device-specific configuration structure.
    pub device: PciVirtioRegion,
    /// Interrupt-status structure, absent on devices that expose none.
    ///
    /// A device without this window cannot signal queue completion through
    /// the `ISR` structure, so interrupt-driven completion must stay disabled
    /// rather than being silently approximated by polling.
    pub isr: Option<PciVirtioRegion>,
    /// Multiplier applied to queue-notification offsets.
    pub notify_offset_multiplier: u32,
}

/// Errors returned while resolving modern `VirtIO` memory regions.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PciVirtioRegionError {
    /// The capability chain itself is malformed or incomplete.
    Capabilities(crate::drivers::virtio::pci::PciCapabilityError),
    /// A capability-selected BAR cannot be decoded.
    Bar(PciBarError),
    /// A capability region has no length.
    EmptyRegion(VirtioPciCapabilityKind),
    /// A capability region overflows its BAR address.
    RegionOverflow(VirtioPciCapabilityKind),
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
        if bar_index > 5 {
            return 0;
        }
        self.read_u32(0x10 + bar_index * 4)
    }

    /// Decode a memory BAR without probing or assigning PCI resources.
    ///
    /// # Errors
    ///
    /// Returns [`PciBarError`] for invalid indices, I/O or reserved BARs,
    /// unassigned addresses, truncated 64-bit addresses, and malformed upper
    /// dwords.
    pub fn read_memory_bar(&self, bar_index: u8) -> Result<PciMemoryBar, PciBarError> {
        if bar_index > 5 {
            return Err(PciBarError::InvalidIndex(bar_index));
        }
        let low = self.read_u32(0x10 + bar_index * 4);
        let high = if (low >> 1) & 0x3 == 2 && bar_index < 5 {
            self.read_u32(0x10 + (bar_index + 1) * 4)
        } else {
            0
        };
        decode_memory_bar(bar_index, low, high)
    }

    /// Enable Bus Mastering and Memory/IO decoding in the PCI Command register.
    pub fn enable_bus_mastering(&self) {
        let mut cmd = self.read_u16(0x04);
        cmd |= (1 << 0) | (1 << 1) | (1 << 2);
        self.write_u16(0x04, cmd);
    }

    /// Discover the modern `VirtIO` capability chain for this device.
    ///
    /// # Errors
    ///
    /// Returns the bounded parser error when the capability list is malformed,
    /// cyclic, incomplete, or does not expose a usable modern `VirtIO` interface.
    pub fn virtio_capabilities(
        &self,
    ) -> Result<VirtioPciCapabilities, crate::drivers::virtio::pci::PciCapabilityError> {
        VirtioPciCapabilities::discover(self.read_u8(0x34), |offset| self.read_u8(offset))
    }

    /// Return whether this device exposes a complete modern `VirtIO` interface.
    #[must_use]
    pub fn has_modern_virtio(&self) -> bool {
        self.virtio_capabilities().is_ok()
    }

    /// Return the BAR index used by a modern `VirtIO` capability kind.
    #[must_use]
    pub fn virtio_capability_bar(&self, kind: VirtioPciCapabilityKind) -> Option<u8> {
        self.virtio_capabilities()
            .ok()?
            .find(kind)
            .map(|cap| cap.bar)
    }

    /// Return this device's MSI and MSI-X capabilities, if present.
    ///
    /// # Errors
    ///
    /// Returns [`crate::drivers::virtio::pci::PciCapabilityError`] when the
    /// capability chain is malformed or cyclic.
    pub fn msi_capabilities(
        &self,
    ) -> Result<
        (
            Option<crate::drivers::virtio::pci::PciMsiCapability>,
            Option<crate::drivers::virtio::pci::PciMsiXCapability>,
        ),
        crate::drivers::virtio::pci::PciCapabilityError,
    > {
        crate::drivers::virtio::pci::discover_msi_capabilities(self.read_u8(0x34), |offset| {
            self.read_u8(offset)
        })
    }

    /// Resolve the modern `VirtIO` common, notify, and device memory regions.
    ///
    /// # Errors
    ///
    /// Returns [`PciVirtioRegionError`] when capability parsing, BAR
    /// decoding, or region arithmetic fails.
    pub fn virtio_regions(&self) -> Result<PciVirtioRegions, PciVirtioRegionError> {
        let capabilities = self
            .virtio_capabilities()
            .map_err(PciVirtioRegionError::Capabilities)?;
        let resolve = |kind: VirtioPciCapabilityKind| {
            let capability = capabilities
                .find(kind)
                .ok_or(PciVirtioRegionError::Capabilities(
                    crate::drivers::virtio::pci::PciCapabilityError::MissingCapability(kind),
                ))?;
            let (offset, byte_len) = match kind {
                VirtioPciCapabilityKind::Notify => {
                    (capability.notify_offset, capability.notify_length)
                }
                _ => (capability.region_offset, capability.region_length),
            };
            if byte_len == 0 {
                return Err(PciVirtioRegionError::EmptyRegion(kind));
            }
            let bar = self
                .read_memory_bar(capability.bar)
                .map_err(PciVirtioRegionError::Bar)?;
            let physical_base = bar
                .address
                .checked_add(u64::from(offset))
                .ok_or(PciVirtioRegionError::RegionOverflow(kind))?;
            let end = physical_base
                .checked_add(u64::from(byte_len))
                .ok_or(PciVirtioRegionError::RegionOverflow(kind))?;
            if end <= physical_base {
                return Err(PciVirtioRegionError::RegionOverflow(kind));
            }
            Ok(PciVirtioRegion {
                bar_index: bar.index,
                bar_address: bar.address,
                offset,
                physical_base,
                byte_len,
            })
        };
        let isr = capabilities
            .find(VirtioPciCapabilityKind::Isr)
            .map(|_| resolve(VirtioPciCapabilityKind::Isr))
            .transpose()?;
        Ok(PciVirtioRegions {
            common: resolve(VirtioPciCapabilityKind::Common)?,
            notify: resolve(VirtioPciCapabilityKind::Notify)?,
            device: resolve(VirtioPciCapabilityKind::Device)?,
            isr,
            notify_offset_multiplier: capabilities
                .find(VirtioPciCapabilityKind::Notify)
                .map_or(0, |capability| capability.notify_offset_multiplier),
        })
    }

    /// Return the interrupt-status window, if the device exposes one.
    ///
    /// Interrupt-driven completion requires this window; its absence is a
    /// device capability fact, not an error to be papered over with polling.
    ///
    /// # Errors
    ///
    /// Returns [`PciVirtioRegionError`] when the capability chain is
    /// malformed or a capability-selected BAR cannot be decoded.
    pub fn virtio_isr_region(&self) -> Result<Option<PciVirtioRegion>, PciVirtioRegionError> {
        self.virtio_regions().map(|regions| regions.isr)
    }
}

/// Decode a memory BAR from its raw low and optional upper dwords.
///
/// # Errors
///
/// Returns [`PciBarError`] for an invalid index, I/O or reserved type,
/// unassigned address, or malformed 64-bit upper dword.
pub fn decode_memory_bar(bar_index: u8, low: u32, high: u32) -> Result<PciMemoryBar, PciBarError> {
    if bar_index > 5 {
        return Err(PciBarError::InvalidIndex(bar_index));
    }
    if low & 1 != 0 {
        return Err(PciBarError::NotMemory(bar_index));
    }
    let bar_type = (low >> 1) & 0x3;
    let (address, is_64_bit) = match bar_type {
        0 => (u64::from(low & !0x0f), false),
        2 => {
            if bar_index >= 5 {
                return Err(PciBarError::InvalidUpperDword(bar_index));
            }
            if high == u32::MAX {
                return Err(PciBarError::InvalidUpperDword(bar_index));
            }
            (u64::from(high) << 32 | u64::from(low & !0x0f), true)
        }
        3 => return Err(PciBarError::ReservedType(3)),
        1 => return Err(PciBarError::ReservedType(1)),
        _ => return Err(PciBarError::ReservedType(0)),
    };
    if address == 0 || (low & !0x0f) == 0xffff_fff0 {
        return Err(PciBarError::Unassigned(bar_index));
    }
    Ok(PciMemoryBar {
        index: bar_index,
        address,
        is_64_bit,
        prefetchable: low & (1 << 3) != 0,
    })
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
    use super::{PciBarError, decode_memory_bar, is_virtio_block, is_virtio_gpu};

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

    #[test]
    fn memory_bar_decoder_handles_64_bit_and_rejects_invalid_input() {
        assert_eq!(
            decode_memory_bar(4, 0xc000_0004, 0x0000_000c)
                .unwrap()
                .address,
            0x0000_000c_c000_0000
        );
        assert_eq!(
            decode_memory_bar(4, 0xffff_fffd, 0),
            Err(PciBarError::NotMemory(4))
        );
        assert_eq!(
            decode_memory_bar(4, 0xc000_0004, u32::MAX),
            Err(PciBarError::InvalidUpperDword(4))
        );
        assert_eq!(decode_memory_bar(4, 0, 0), Err(PciBarError::Unassigned(4)));
    }
}
