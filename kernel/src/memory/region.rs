//! Architecture-neutral memory-region types for `FinnOS` physical memory classification.

/// Maximum number of `FinnOS` memory regions the early parser can produce.
///
/// This limit keeps the parser allocation-free. A future dynamic allocator can
/// supersede this fixed table.
pub const MAX_MEMORY_REGIONS: usize = 256;

/// A classified physical memory region.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MemoryRegion {
    /// Physical start address.
    pub start: u64,
    /// Length in bytes.
    pub byte_len: u64,
    /// `FinnOS` classification.
    pub kind: MemoryRegionKind,
    /// Source of the region (UEFI type or `FinnOS` override).
    pub source: MemoryRegionSource,
    /// UEFI attribute mask, when applicable.
    pub attributes: u64,
}

impl MemoryRegion {
    /// Exclusive end address, or `None` if it would overflow.
    #[must_use]
    pub const fn end(&self) -> Option<u64> {
        self.start.checked_add(self.byte_len)
    }

    /// Return true if the region has zero length.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.byte_len == 0
    }

    /// Return true if this region overlaps the given range.
    #[must_use]
    pub const fn overlaps(&self, start: u64, end: u64) -> bool {
        let Some(self_end) = self.end() else {
            return false;
        };
        self.start < end && start < self_end
    }
}

/// How `FinnOS` intends to treat a physical memory region.
#[derive(Clone, Copy, Debug, Eq, PartialEq, PartialOrd, Ord)]
pub enum MemoryRegionKind {
    /// Usable conventional RAM after all exclusions.
    Usable,
    /// UEFI boot-services code or data (not yet reclaimed).
    BootServices,
    /// UEFI runtime-services code or data.
    RuntimeServices,
    /// Firmware-reserved memory.
    Firmware,
    /// ACPI reclaimable memory.
    AcpiReclaimable,
    /// ACPI non-volatile storage.
    AcpiNonVolatile,
    /// Memory-mapped I/O.
    MemoryMappedIo,
    /// Framebuffer backing memory.
    Framebuffer,
    /// Kernel image.
    Kernel,
    /// `BootInfo` structure storage.
    BootInfo,
    /// Raw UEFI memory-map storage.
    MemoryMapStorage,
    /// Reserved for safety or policy reasons.
    Reserved,
    /// Unusable memory reported by firmware.
    Unusable,
    /// Persistent memory (not yet supported as usable).
    Persistent,
    /// Unknown UEFI memory type.
    UnknownFirmwareType,
}

/// The source of a memory region.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemoryRegionSource {
    /// Derived directly from a UEFI descriptor of the given type.
    Uefi(UefiMemoryType),
    /// Derived from a FinnOS-protected range.
    FinnOS,
}

/// UEFI memory types as defined by the UEFI specification.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum UefiMemoryType {
    /// Conventional RAM.
    Conventional = 7,
    /// UEFI reserved.
    Reserved = 0,
    /// Loader code.
    LoaderCode = 1,
    /// Loader data.
    LoaderData = 2,
    /// Boot-services code.
    BootServicesCode = 3,
    /// Boot-services data.
    BootServicesData = 4,
    /// Runtime-services code.
    RuntimeServicesCode = 5,
    /// Runtime-services data.
    RuntimeServicesData = 6,
    /// Unusable memory.
    Unusable = 8,
    /// ACPI reclaimable.
    AcpiReclaimable = 9,
    /// ACPI non-volatile storage.
    AcpiNonVolatile = 10,
    /// Memory-mapped I/O.
    MemoryMappedIo = 11,
    /// Memory-mapped I/O port space.
    MemoryMappedIoPortSpace = 12,
    /// PAL code.
    PalCode = 13,
    /// Persistent memory.
    PersistentMemory = 14,
    /// Unknown or unsupported UEFI memory type.
    Unknown = 0xffff_ffff,
}

impl UefiMemoryType {
    /// Decode a raw UEFI memory type value.
    #[must_use]
    pub const fn from_raw(value: u32) -> Option<Self> {
        match value {
            0 => Some(Self::Reserved),
            1 => Some(Self::LoaderCode),
            2 => Some(Self::LoaderData),
            3 => Some(Self::BootServicesCode),
            4 => Some(Self::BootServicesData),
            5 => Some(Self::RuntimeServicesCode),
            6 => Some(Self::RuntimeServicesData),
            7 => Some(Self::Conventional),
            8 => Some(Self::Unusable),
            9 => Some(Self::AcpiReclaimable),
            10 => Some(Self::AcpiNonVolatile),
            11 => Some(Self::MemoryMappedIo),
            12 => Some(Self::MemoryMappedIoPortSpace),
            13 => Some(Self::PalCode),
            14 => Some(Self::PersistentMemory),
            _ => Some(Self::Unknown),
        }
    }

    /// Classify a UEFI memory type into an initial `FinnOS` classification.
    #[must_use]
    pub const fn classify(self) -> MemoryRegionKind {
        match self {
            Self::Conventional => MemoryRegionKind::Usable,
            Self::LoaderCode
            | Self::LoaderData
            | Self::BootServicesCode
            | Self::BootServicesData => MemoryRegionKind::BootServices,
            Self::RuntimeServicesCode | Self::RuntimeServicesData => {
                MemoryRegionKind::RuntimeServices
            }
            Self::Reserved => MemoryRegionKind::Reserved,
            Self::Unusable => MemoryRegionKind::Unusable,
            Self::AcpiReclaimable => MemoryRegionKind::AcpiReclaimable,
            Self::AcpiNonVolatile => MemoryRegionKind::AcpiNonVolatile,
            Self::MemoryMappedIo | Self::MemoryMappedIoPortSpace => {
                MemoryRegionKind::MemoryMappedIo
            }
            Self::PalCode => MemoryRegionKind::Firmware,
            Self::PersistentMemory => MemoryRegionKind::Persistent,
            Self::Unknown => MemoryRegionKind::UnknownFirmwareType,
        }
    }
}
