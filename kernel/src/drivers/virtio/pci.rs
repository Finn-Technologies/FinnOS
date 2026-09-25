#![allow(clippy::missing_errors_doc)]

//! Modern `VirtIO` PCI capability discovery and register-offset policy.

/// Capability-chain iteration limit from the PCI Express capability list.
pub const MAX_PCI_CAPABILITIES: u8 = 48;

/// A parsed modern `VirtIO` PCI capability header.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtioPciCapability {
    /// PCI configuration-space offset of the capability.
    pub config_offset: u8,
    /// Total capability length in bytes.
    pub capability_length: u8,
    /// `VirtIO` configuration-space type.
    pub kind: VirtioPciCapabilityKind,
    /// BAR index selected by this capability.
    pub bar: u8,
    /// Region offset for common, ISR, or device capabilities.
    pub region_offset: u32,
    /// Region length for common, ISR, or device capabilities.
    pub region_length: u32,
    /// Notify capability's notification-region offset.
    pub notify_offset: u32,
    /// Notify capability's notification-region length.
    pub notify_length: u32,
    /// Multiplier applied to the common queue-notify offset.
    pub notify_offset_multiplier: u32,
}

/// Modern `VirtIO` PCI capability kinds.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VirtioPciCapabilityKind {
    /// Common configuration structure.
    Common,
    /// Notification structure.
    Notify,
    /// ISR structure.
    Isr,
    /// Device-specific configuration structure.
    Device,
    /// A vendor-defined or unknown capability.
    Unknown(u8),
}

/// PCI capability identifier for the MSI capability.
pub const PCI_CAPABILITY_MSI: u8 = 0x05;
/// PCI capability identifier for the MSI-X capability.
pub const PCI_CAPABILITY_MSI_X: u8 = 0x11;

/// One parsed MSI capability.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PciMsiCapability {
    /// PCI configuration-space offset of the capability.
    pub config_offset: u8,
    /// Total capability length in bytes.
    pub length: u8,
    /// Number of interrupt vectors the device can raise.
    pub vector_count: u8,
    /// Whether the capability carries a 64-bit message address.
    pub is_64bit: bool,
    /// Whether the device supports per-vector masking.
    pub maskable: bool,
}

impl PciMsiCapability {
    /// Message-data register offset relative to the capability.
    ///
    /// The 64-bit form carries an extra high address dword, so the message
    /// data register moves down by one word.
    #[must_use]
    pub const fn message_data_offset(&self) -> u8 {
        if self.is_64bit { 0x0c } else { 0x08 }
    }
}

/// One parsed MSI-X capability.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PciMsiXCapability {
    /// PCI configuration-space offset of the capability.
    pub config_offset: u8,
    /// BAR index containing the MSI-X table.
    pub table_bar: u8,
    /// Byte offset of the table within that BAR.
    pub table_offset: u32,
    /// Number of table entries the device exposes.
    pub table_size: u16,
}

/// One MSI-X table entry as it appears in device memory.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct MsiXEntry {
    /// Message address delivered when this vector fires.
    pub message_address: u64,
    /// Message data delivered with the interrupt.
    pub message_data: u32,
    /// Vector control bits (enable and mask).
    pub vector_control: u32,
}

/// MSI-X table entry control bits.
pub mod msix_control {
    /// Bit 0 enables the vector for delivery.
    pub const ENABLE: u32 = 1 << 0;
    /// Bit 1 masks the vector.
    pub const MASK: u32 = 1 << 1;
}

impl MsiXEntry {
    /// Build a masked, disabled entry for one message.
    ///
    /// The entry starts masked so a vector can never fire with a partially
    /// programmed address or data value.
    #[must_use]
    pub const fn masked(message_address: u64, message_data: u32) -> Self {
        Self {
            message_address,
            message_data,
            vector_control: msix_control::MASK,
        }
    }

    /// Return the entry state that delivers one message.
    #[must_use]
    pub const fn enabled(message_address: u64, message_data: u32) -> Self {
        Self {
            message_address,
            message_data,
            vector_control: msix_control::ENABLE,
        }
    }

    /// Return whether the vector currently delivers interrupts.
    #[must_use]
    pub const fn is_enabled(&self) -> bool {
        self.vector_control & msix_control::ENABLE != 0
            && self.vector_control & msix_control::MASK == 0
    }
}

/// Return the byte offset of one MSI-X table entry, or `None` when the index
/// is outside the device's advertised table.
#[must_use]
pub fn msix_entry_offset(table_offset: u32, table_size: u16, index: u16) -> Option<u64> {
    if index >= table_size {
        return None;
    }
    Some(u64::from(table_offset) + u64::from(index) * 16)
}

/// Walk the PCI capability chain and return the first MSI and MSI-X headers.
///
/// # Errors
///
/// Returns [`PciCapabilityError`] for a malformed or cyclic chain. A device
/// with neither capability yields two `None` results rather than an error:
/// lacking MSI is a device fact the caller must respect, not a parse failure.
pub fn discover_msi_capabilities<F>(
    first: u8,
    mut read: F,
) -> Result<(Option<PciMsiCapability>, Option<PciMsiXCapability>), PciCapabilityError>
where
    F: FnMut(u8) -> u8,
{
    let mut msi = None;
    let mut msi_x = None;
    let mut offset = first;
    let mut visited = [0u8; MAX_PCI_CAPABILITIES as usize];
    let mut steps = 0usize;
    while offset != 0 {
        if steps >= MAX_PCI_CAPABILITIES as usize {
            return Err(PciCapabilityError::TooManyCapabilities);
        }
        if offset < 0x40 || !offset.is_multiple_of(4) {
            return Err(PciCapabilityError::InvalidPointer(offset));
        }
        if visited[..steps].contains(&offset) {
            return Err(PciCapabilityError::CyclicPointer(offset));
        }
        visited[steps] = offset;
        steps += 1;

        let id = read(offset);
        let next = read(offset + 1);
        if id == 0xff || next == 0xff {
            return Err(PciCapabilityError::InvalidPointer(offset));
        }
        match id {
            PCI_CAPABILITY_MSI if msi.is_none() => {
                // Standard MSI is a four-byte header (ID, next, length) plus
                // the message-control dword and the address/data registers.
                let length = read(offset + 2);
                if length < 0x10 {
                    return Err(PciCapabilityError::InvalidLength(offset));
                }
                let control = read(offset + 4);
                let multiple = control & 0x7;
                msi = Some(PciMsiCapability {
                    config_offset: offset,
                    length,
                    // Bit 0 is the enable flag; the remaining low bits are a
                    // multiple-message-capable count, so the vector count is
                    // one more than the encoded value.
                    vector_count: if multiple == 0 { 1 } else { multiple + 1 },
                    is_64bit: control & 0x80 != 0,
                    maskable: control & 0x8 != 0,
                });
            }
            PCI_CAPABILITY_MSI_X if msi_x.is_none() => {
                // MSI-X has a two-byte header only: the length byte is not
                // part of this capability. QEMU encodes MSIX_CAP_LENGTH = 12
                // and the standard layout places message control at +2, the
                // table size in its low 11 bits, and the table offset with
                // its BAR select at +4.
                let control = u16::from(read(offset + 2)) | (u16::from(read(offset + 3)) << 8);
                let raw_table = u32::from(read(offset + 4))
                    | (u32::from(read(offset + 5)) << 8)
                    | (u32::from(read(offset + 6)) << 16)
                    | (u32::from(read(offset + 7)) << 24);
                // The table-size field stores nentries - 1.
                let table_size = (control & 0x07ff) + 1;
                msi_x = Some(PciMsiXCapability {
                    config_offset: offset,
                    table_bar: (raw_table & 0x7) as u8,
                    table_offset: raw_table & 0xffff_fff8,
                    table_size,
                });
            }
            _ => {}
        }
        offset = next;
    }
    Ok((msi, msi_x))
}

/// Errors returned while walking a PCI capability list.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PciCapabilityError {
    /// The capability pointer is invalid.
    InvalidPointer(u8),
    /// A capability has a truncated header.
    InvalidLength(u8),
    /// The chain exceeded its fixed bound.
    TooManyCapabilities,
    /// The chain contains a cycle or a repeated pointer.
    CyclicPointer(u8),
    /// A required `VirtIO` capability is missing.
    MissingCapability(VirtioPciCapabilityKind),
}

/// Parsed modern `VirtIO` PCI capabilities.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtioPciCapabilities {
    capabilities: [VirtioPciCapability; MAX_PCI_CAPABILITIES as usize],
    count: usize,
}

impl VirtioPciCapabilities {
    /// Walk a PCI capability list using a bounded configuration-space reader.
    ///
    /// # Errors
    ///
    /// Returns [`PciCapabilityError`] for malformed, cyclic, or incomplete
    /// capability chains. A missing optional ISR capability is permitted.
    #[allow(clippy::too_many_lines)]
    pub fn discover<F>(first: u8, mut read: F) -> Result<Self, PciCapabilityError>
    where
        F: FnMut(u8) -> u8,
    {
        if first == 0xff {
            return Ok(Self {
                capabilities: [VirtioPciCapability {
                    config_offset: 0,
                    capability_length: 0,
                    kind: VirtioPciCapabilityKind::Unknown(0),
                    bar: 0,
                    region_offset: 0,
                    region_length: 0,
                    notify_offset: 0,
                    notify_length: 0,
                    notify_offset_multiplier: 0,
                }; MAX_PCI_CAPABILITIES as usize],
                count: 0,
            });
        }

        let mut parsed = Self {
            capabilities: [VirtioPciCapability {
                config_offset: 0,
                capability_length: 0,
                kind: VirtioPciCapabilityKind::Unknown(0),
                bar: 0,
                region_offset: 0,
                region_length: 0,
                notify_offset: 0,
                notify_length: 0,
                notify_offset_multiplier: 0,
            }; MAX_PCI_CAPABILITIES as usize],
            count: 0,
        };
        let mut offset = first;
        let mut visited = [0u8; MAX_PCI_CAPABILITIES as usize];
        let mut steps = 0usize;
        while offset != 0 {
            if steps >= MAX_PCI_CAPABILITIES as usize {
                return Err(PciCapabilityError::TooManyCapabilities);
            }
            if offset < 0x40 || !offset.is_multiple_of(4) {
                return Err(PciCapabilityError::InvalidPointer(offset));
            }
            if visited[..steps].contains(&offset) {
                return Err(PciCapabilityError::CyclicPointer(offset));
            }
            visited[steps] = offset;
            steps += 1;

            let id = read(offset);
            let next = read(offset + 1);
            if id == 0xff || next == 0xff {
                return Err(PciCapabilityError::InvalidPointer(offset));
            }
            if id != 0x09 {
                // A non-PCI-express capability is still a valid chain link;
                // it is intentionally not represented in this VirtIO set.
                offset = next;
                continue;
            }
            let length = read(offset + 2);
            if length < 4 {
                return Err(PciCapabilityError::InvalidLength(offset));
            }
            let kind_id = read(offset + 3);
            let kind = match kind_id {
                1 => VirtioPciCapabilityKind::Common,
                2 => VirtioPciCapabilityKind::Notify,
                3 => VirtioPciCapabilityKind::Isr,
                4 => VirtioPciCapabilityKind::Device,
                other => VirtioPciCapabilityKind::Unknown(other),
            };
            let (
                bar,
                region_offset,
                region_length,
                notify_offset_multiplier,
                notify_offset,
                notify_length,
            ) = match (kind_id, length) {
                (1 | 3 | 4, length) if length >= 0x10 => (
                    read(offset + 4),
                    u32::from_le_bytes([
                        read(offset + 8),
                        read(offset + 9),
                        read(offset + 10),
                        read(offset + 11),
                    ]),
                    u32::from_le_bytes([
                        read(offset + 12),
                        read(offset + 13),
                        read(offset + 14),
                        read(offset + 15),
                    ]),
                    0,
                    0,
                    0,
                ),
                (2, length) if length >= 0x14 => (
                    read(offset + 4),
                    0,
                    0,
                    u32::from_le_bytes([
                        read(offset + 16),
                        read(offset + 17),
                        read(offset + 18),
                        read(offset + 19),
                    ]),
                    u32::from_le_bytes([
                        read(offset + 8),
                        read(offset + 9),
                        read(offset + 10),
                        read(offset + 11),
                    ]),
                    u32::from_le_bytes([
                        read(offset + 12),
                        read(offset + 13),
                        read(offset + 14),
                        read(offset + 15),
                    ]),
                ),
                _ => (0, 0, 0, 0, 0, 0),
            };
            let capability = VirtioPciCapability {
                config_offset: offset,
                capability_length: length,
                kind,
                bar,
                region_offset,
                region_length,
                notify_offset,
                notify_length,
                notify_offset_multiplier,
            };
            parsed.capabilities[parsed.count] = capability;
            parsed.count += 1;
            offset = next;
        }

        for required in [
            VirtioPciCapabilityKind::Common,
            VirtioPciCapabilityKind::Notify,
            VirtioPciCapabilityKind::Device,
        ] {
            if !parsed.capabilities[..parsed.count]
                .iter()
                .any(|cap| cap.kind == required)
            {
                return Err(PciCapabilityError::MissingCapability(required));
            }
        }
        Ok(parsed)
    }

    /// Return all parsed capabilities in configuration-space order.
    #[must_use]
    pub fn all(&self) -> &[VirtioPciCapability] {
        &self.capabilities[..self.count]
    }

    /// Find one capability kind.
    #[must_use]
    pub fn find(&self, kind: VirtioPciCapabilityKind) -> Option<VirtioPciCapability> {
        self.capabilities[..self.count]
            .iter()
            .copied()
            .find(|capability| capability.kind == kind)
    }

    /// Number of parsed `VirtIO` capabilities.
    #[must_use]
    pub const fn len(&self) -> usize {
        self.count
    }

    /// Return whether no `VirtIO` capabilities were parsed.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.count == 0
    }
}

/// Common configuration register offsets from the `VirtIO` PCI common structure.
pub mod common {
    /// Selects the low or high 32-bit device feature word.
    pub const DEVICE_FEATURE_SELECT: u32 = 0x00;
    /// Selected device feature word.
    pub const DEVICE_FEATURE: u32 = 0x04;
    /// Selects the low or high 32-bit driver feature word.
    pub const DRIVER_FEATURE_SELECT: u32 = 0x08;
    /// Selected driver feature word.
    pub const DRIVER_FEATURE: u32 = 0x0c;
    /// Device status register.
    pub const STATUS: u32 = 0x14;
    /// Configuration generation register.
    pub const CONFIG_GENERATION: u32 = 0x15;
    /// Number of queues exposed by the device.
    pub const NUM_QUEUES: u32 = 0x12;
    /// Queue-select register.
    pub const QUEUE_SELECT: u32 = 0x16;
    /// Queue-size register.
    pub const QUEUE_SIZE: u32 = 0x18;
    /// Queue MSI-X vector register.
    pub const QUEUE_MSIX_VECTOR: u32 = 0x1a;
    /// Queue-notify offset register.
    pub const QUEUE_NOTIFY_OFF: u32 = 0x1e;
    /// Queue descriptor address register.
    pub const QUEUE_DESC: u32 = 0x20;
    /// Driver-visible queue address register.
    pub const QUEUE_DRIVER: u32 = 0x28;
    /// Device-visible queue address register.
    pub const QUEUE_DEVICE: u32 = 0x30;
    /// Queue-enable register.
    pub const QUEUE_ENABLE: u32 = 0x1c;
}

#[cfg(test)]
mod tests {
    use super::{
        MAX_PCI_CAPABILITIES, PciCapabilityError, VirtioPciCapabilities, VirtioPciCapabilityKind,
        discover_msi_capabilities,
    };

    fn fixture() -> [u8; 0x100] {
        let mut data = [0u8; 0x100];
        data[0x40] = 0x09;
        data[0x41] = 0x50;
        data[0x42] = 0x10;
        data[0x43] = 1;
        data[0x50] = 0x09;
        data[0x51] = 0x68;
        data[0x52] = 0x14;
        data[0x53] = 2;
        data[0x54] = 4;
        data[0x58] = 0x00;
        data[0x59] = 0x30;
        data[0x5a] = 0x00;
        data[0x5b] = 0x00;
        data[0x5c] = 0x00;
        data[0x5d] = 0x10;
        data[0x5e] = 0x00;
        data[0x5f] = 0x00;
        data[0x60] = 0x04;
        data[0x61] = 0x00;
        data[0x62] = 0x00;
        data[0x63] = 0x00;
        data[0x68] = 0x09;
        data[0x69] = 0x78;
        data[0x6a] = 0x10;
        data[0x6b] = 3;
        data[0x6c] = 4;
        data[0x6d] = 0x00;
        data[0x6e] = 0x00;
        data[0x6f] = 0x00;
        data[0x70] = 0x00;
        data[0x71] = 0x10;
        data[0x72] = 0x00;
        data[0x73] = 0x00;
        // ISR region length: one 4 KiB page at 0x74..0x78.
        data[0x74] = 0x00;
        data[0x75] = 0x10;
        data[0x76] = 0x00;
        data[0x77] = 0x00;
        data[0x78] = 0x09;
        data[0x79] = 0x00;
        data[0x7a] = 0x10;
        data[0x7b] = 4;
        data[0x7c] = 4;
        data
    }

    #[test]
    fn modern_capability_chain_is_discovered() {
        let data = fixture();
        let caps = VirtioPciCapabilities::discover(0x40, |offset| data[offset as usize]).unwrap();
        assert_eq!(caps.len(), 4);
        assert_eq!(
            caps.find(VirtioPciCapabilityKind::Common)
                .unwrap()
                .config_offset,
            0x40
        );
        assert_eq!(
            caps.find(VirtioPciCapabilityKind::Notify)
                .unwrap()
                .notify_offset,
            0x3000
        );
        assert_eq!(
            caps.find(VirtioPciCapabilityKind::Notify)
                .unwrap()
                .notify_length,
            0x1000
        );
        assert_eq!(
            caps.find(VirtioPciCapabilityKind::Notify)
                .unwrap()
                .notify_offset_multiplier,
            4
        );
        assert_eq!(caps.find(VirtioPciCapabilityKind::Device).unwrap().bar, 4);
    }

    #[test]
    fn malformed_and_missing_chains_fail_closed() {
        let mut data = fixture();
        data[0x40] = 0xff;
        assert!(matches!(
            VirtioPciCapabilities::discover(0x40, |offset| data[offset as usize]),
            Err(PciCapabilityError::InvalidPointer(0x40))
        ));
        data = fixture();
        data[0x42] = 2;
        assert_eq!(
            VirtioPciCapabilities::discover(0x40, |offset| data[offset as usize]),
            Err(PciCapabilityError::InvalidLength(0x40))
        );
        data = fixture();
        data[0x7b] = 0xff;
        assert_eq!(
            VirtioPciCapabilities::discover(0x40, |offset| data[offset as usize]),
            Err(PciCapabilityError::MissingCapability(
                VirtioPciCapabilityKind::Device
            ))
        );
    }

    #[test]
    fn chain_iteration_is_bounded() {
        #[allow(clippy::cast_possible_truncation)]
        let mut data = [0u8; 0x100];
        for index in 0..MAX_PCI_CAPABILITIES {
            let offset = 0x40u16 + u16::from(index) * 4;
            data[offset as usize] = 0x09;
            data[offset as usize + 1] = if index + 1 == MAX_PCI_CAPABILITIES {
                0x40
            } else {
                u8::try_from(offset + 4).unwrap_or(0xff)
            };
            data[offset as usize + 2] = 4;
            data[offset as usize + 3] = 1;
        }
        assert!(matches!(
            VirtioPciCapabilities::discover(0x40, |offset| data[offset as usize]),
            Err(PciCapabilityError::TooManyCapabilities)
        ));
    }

    #[test]
    fn isr_capability_is_discovered_but_not_required() {
        let data = fixture();
        let caps = VirtioPciCapabilities::discover(0x40, |offset| data[offset as usize]).unwrap();
        let isr = caps.find(VirtioPciCapabilityKind::Isr).unwrap();
        assert_eq!(isr.config_offset, 0x68);
        assert_eq!(isr.bar, 4);
        assert_eq!(isr.region_offset, 0x1000);
        assert_eq!(isr.region_length, 0x1000);

        // Discovery still succeeds without an ISR capability, because a device
        // that exposes none is a legitimate polled-only configuration rather
        // than a malformed one.
        let mut without_isr = fixture();
        without_isr[0x51] = 0x78; // link past the ISR capability
        without_isr[0x68] = 0x00;
        let caps =
            VirtioPciCapabilities::discover(0x40, |offset| without_isr[offset as usize]).unwrap();
        assert!(caps.find(VirtioPciCapabilityKind::Isr).is_none());
        assert!(caps.find(VirtioPciCapabilityKind::Common).is_some());
    }

    /// A realistic chain: MSI-X, then MSI, then the `VirtIO` capabilities.
    fn msi_fixture() -> [u8; 0x100] {
        let mut data = fixture();
        // Insert MSI-X at 0x40 and MSI at 0x50, relinking the VirtIO chain.
        // MSI-X has a two-byte header: message control at +2, and a table
        // dword at +4 carrying the offset with the BAR index in its low bits.
        data[0x40] = 0x11;
        data[0x41] = 0x50;
        // Message control holds nentries - 1 in its low 11 bits, so 0x02
        // encodes three vectors.
        data[0x42] = 0x02;
        data[0x43] = 0x00;
        // A table at 0x40 in BAR 4 encodes as 0x044.
        data[0x44] = 0x44;
        data[0x45] = 0x00;
        data[0x46] = 0x00;
        data[0x47] = 0x00;
        // MSI: id 0x05, next 0x68 (the old Common capability offset).
        data[0x50] = 0x05;
        data[0x51] = 0x68;
        data[0x52] = 0x10;
        data[0x54] = 0x81; // 64-bit capable, multiple-message capable = 1
        data
    }

    #[test]
    fn msi_and_msix_capabilities_are_parsed() {
        let data = msi_fixture();
        let (msi, msi_x) = discover_msi_capabilities(0x40, |offset| data[offset as usize]).unwrap();
        let msi_x = msi_x.expect("MSI-X capability");
        assert_eq!(msi_x.config_offset, 0x40);
        assert_eq!(msi_x.table_offset, 0x40);
        assert_eq!(msi_x.table_bar, 4);
        assert_eq!(msi_x.table_size, 3);
        let msi = msi.expect("MSI capability");
        assert_eq!(msi.config_offset, 0x50);
        assert!(msi.is_64bit);
        // Multiple-message capable = 1 means two vectors.
        assert_eq!(msi.vector_count, 2);
        // The 64-bit form moves the message-data dword down one word.
        assert_eq!(msi.message_data_offset(), 0x0c);
    }

    #[test]
    fn missing_msi_is_reported_not_an_error() {
        let data = fixture();
        let (msi, msi_x) = discover_msi_capabilities(0x40, |offset| data[offset as usize]).unwrap();
        assert!(msi.is_none());
        assert!(msi_x.is_none());
    }

    #[test]
    fn msix_entries_start_masked_and_are_bounded() {
        use super::{MsiXEntry, msix_control, msix_entry_offset};

        let masked = MsiXEntry::masked(0xfee0_0000, 64);
        // A newly built vector must never deliver before it is programmed.
        assert!(!masked.is_enabled());
        assert_eq!(
            masked.vector_control & msix_control::MASK,
            msix_control::MASK
        );
        let enabled = MsiXEntry::enabled(0xfee0_0000, 64);
        assert!(enabled.is_enabled());
        assert_eq!(enabled.message_address, 0xfee0_0000);
        assert_eq!(enabled.message_data, 64);

        // Entry offsets stay inside the advertised table.
        assert_eq!(msix_entry_offset(0x40, 2, 0), Some(0x40));
        assert_eq!(msix_entry_offset(0x40, 2, 1), Some(0x50));
        assert_eq!(msix_entry_offset(0x40, 2, 2), None);
        assert_eq!(msix_entry_offset(0x40, 0, 0), None);
    }
}
