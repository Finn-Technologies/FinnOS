#![no_std]
#![forbid(unsafe_code)]
#![deny(missing_docs)]

//! The architecture-neutral, versioned `FinnOS` firmware-to-kernel handoff.

/// Magic identifying a `FinnOS` boot-information structure (`FINNOSBI`).
pub const BOOT_INFO_MAGIC: u64 = 0x4649_4E4E_4F53_4249;
/// Version of the C-compatible boot protocol.
pub const BOOT_PROTOCOL_VERSION: u32 = 2;
/// A framebuffer is present and directly writable.
pub const BOOT_FLAG_FRAMEBUFFER_PRESENT: u64 = 1 << 0;
/// A raw UEFI memory map is present.
pub const BOOT_FLAG_MEMORY_MAP_PRESENT: u64 = 1 << 1;
/// An ACPI RSDP address is present.
pub const BOOT_FLAG_RSDP_PRESENT: u64 = 1 << 2;

/// Pixel encodings supported by the initial diagnostic renderer.
#[repr(u32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PixelFormat {
    /// RGB byte order.
    Rgb = 0,
    /// BGR byte order.
    Bgr = 1,
    /// UEFI bitmask format, currently rejected by the kernel renderer.
    Bitmask = 2,
    /// GOP blit-only mode, not directly writable.
    BltOnly = 3,
    /// Unknown or unsupported format.
    Unknown = 0xffff_ffff,
}

/// A physical address range.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PhysicalRange {
    /// Start address.
    pub start: u64,
    /// Length in bytes.
    pub byte_len: u64,
}

/// Metadata for the raw UEFI memory-map buffer.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct MemoryMapInfo {
    /// Buffer address.
    pub address: u64,
    /// Buffer length.
    pub byte_len: u64,
    /// Bytes per UEFI descriptor.
    pub descriptor_size: u64,
    /// UEFI descriptor version.
    pub descriptor_version: u32,
    /// Reserved for alignment and future use.
    pub reserved: u32,
}

/// Information needed to write a GOP framebuffer.
#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FramebufferInfo {
    /// Framebuffer base address.
    pub address: u64,
    /// Framebuffer capacity in bytes.
    pub byte_len: u64,
    /// Horizontal resolution.
    pub width: u32,
    /// Vertical resolution.
    pub height: u32,
    /// Pixels per scanline.
    pub stride: u32,
    /// Pixel encoding.
    pub pixel_format: PixelFormat,
}

impl Default for FramebufferInfo {
    fn default() -> Self {
        Self {
            address: 0,
            byte_len: 0,
            width: 0,
            height: 0,
            stride: 0,
            pixel_format: PixelFormat::Unknown,
        }
    }
}

/// Version-one firmware-to-kernel handoff.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct BootInfo {
    /// Protocol magic.
    pub magic: u64,
    /// Protocol version.
    pub version: u32,
    /// Size of this structure.
    pub structure_size: u32,
    /// Presence flags.
    pub flags: u64,
    /// Raw UEFI memory-map metadata.
    pub memory_map: MemoryMapInfo,
    /// GOP framebuffer metadata.
    pub framebuffer: FramebufferInfo,
    /// Loaded kernel physical range.
    pub kernel_image: PhysicalRange,
    /// `BootInfo` structure physical range (retained by the loader).
    pub boot_info_storage: PhysicalRange,
    /// Physical ACPI RSDP address, if present.
    pub rsdp_address: u64,
}

impl BootInfo {
    /// Create an explicitly empty version-one structure.
    #[must_use]
    pub fn empty() -> Self {
        Self {
            magic: BOOT_INFO_MAGIC,
            version: BOOT_PROTOCOL_VERSION,
            structure_size: u32::try_from(core::mem::size_of::<Self>()).unwrap_or(u32::MAX),
            flags: 0,
            memory_map: MemoryMapInfo {
                address: 0,
                byte_len: 0,
                descriptor_size: 0,
                descriptor_version: 0,
                reserved: 0,
            },
            framebuffer: FramebufferInfo {
                address: 0,
                byte_len: 0,
                width: 0,
                height: 0,
                stride: 0,
                pixel_format: PixelFormat::Unknown,
            },
            kernel_image: PhysicalRange {
                start: 0,
                byte_len: 0,
            },
            boot_info_storage: PhysicalRange {
                start: 0,
                byte_len: 0,
            },
            rsdp_address: 0,
        }
    }
}

/// Structured validation failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BootInfoError {
    /// Magic does not identify `FinnOS`.
    BadMagic,
    /// Protocol version is unsupported.
    UnsupportedVersion,
    /// Structure size does not match version one.
    UnexpectedStructureSize,
    /// Required memory-map metadata is absent.
    MissingMemoryMap,
    /// Memory-map metadata is inconsistent.
    InvalidMemoryMap,
    /// Framebuffer metadata is inconsistent or unsupported.
    InvalidFramebuffer,
}

/// Validate a boot-information structure without dereferencing any addresses.
///
/// # Errors
///
/// Returns a structured error when the version-one invariants are not met.
pub fn validate(info: &BootInfo) -> Result<(), BootInfoError> {
    if info.magic != BOOT_INFO_MAGIC {
        return Err(BootInfoError::BadMagic);
    }
    if info.version != BOOT_PROTOCOL_VERSION {
        return Err(BootInfoError::UnsupportedVersion);
    }
    if info.structure_size != u32::try_from(core::mem::size_of::<BootInfo>()).unwrap_or(u32::MAX) {
        return Err(BootInfoError::UnexpectedStructureSize);
    }
    if info.flags & BOOT_FLAG_MEMORY_MAP_PRESENT != 0 {
        if info.memory_map.address == 0
            || info.memory_map.byte_len == 0
            || info.memory_map.descriptor_size == 0
            || info.memory_map.descriptor_size > info.memory_map.byte_len
            || info.boot_info_storage.start == 0
            || info.boot_info_storage.byte_len == 0
        {
            return Err(BootInfoError::InvalidMemoryMap);
        }
    } else if info.memory_map.address != 0 || info.memory_map.byte_len != 0 {
        return Err(BootInfoError::MissingMemoryMap);
    }
    if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
        let format_ok = matches!(
            info.framebuffer.pixel_format,
            PixelFormat::Rgb | PixelFormat::Bgr
        );
        let pixels = u64::from(info.framebuffer.stride)
            .checked_mul(u64::from(info.framebuffer.height))
            .ok_or(BootInfoError::InvalidFramebuffer)?;
        let bytes = pixels
            .checked_mul(4)
            .ok_or(BootInfoError::InvalidFramebuffer)?;
        if !format_ok
            || info.framebuffer.address == 0
            || info.framebuffer.width == 0
            || info.framebuffer.height == 0
            || info.framebuffer.stride < info.framebuffer.width
            || bytes > info.framebuffer.byte_len
        {
            return Err(BootInfoError::InvalidFramebuffer);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_are_valid_without_optional_resources() {
        assert!(validate(&BootInfo::empty()).is_ok());
    }

    #[test]
    fn rejects_bad_magic() {
        let mut info = BootInfo::empty();
        info.magic = 0;
        assert_eq!(validate(&info), Err(BootInfoError::BadMagic));
    }

    #[test]
    fn rejects_version_and_size() {
        let mut info = BootInfo::empty();
        info.version = 3;
        assert_eq!(validate(&info), Err(BootInfoError::UnsupportedVersion));
        info.version = BOOT_PROTOCOL_VERSION + 1;
        assert_eq!(validate(&info), Err(BootInfoError::UnsupportedVersion));
        info.version = BOOT_PROTOCOL_VERSION;
        info.structure_size = 0;
        assert_eq!(validate(&info), Err(BootInfoError::UnexpectedStructureSize));
    }

    #[test]
    fn rejects_invalid_map_and_framebuffer() {
        let mut info = BootInfo::empty();
        info.flags = BOOT_FLAG_MEMORY_MAP_PRESENT;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidMemoryMap));
        info = BootInfo::empty();
        info.flags = BOOT_FLAG_FRAMEBUFFER_PRESENT;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidFramebuffer));
    }
}
