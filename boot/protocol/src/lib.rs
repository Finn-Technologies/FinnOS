#![no_std]
#![forbid(unsafe_code)]
#![deny(missing_docs)]

//! The architecture-neutral, versioned `FinnOS` firmware-to-kernel handoff.

/// Magic identifying a `FinnOS` boot-information structure (`FINNOSBI`).
pub const BOOT_INFO_MAGIC: u64 = 0x4649_4E4E_4F53_4249;
/// Version of the C-compatible boot protocol.
pub const BOOT_PROTOCOL_VERSION: u32 = 3;
/// Size and alignment of the loader-owned page containing `BootInfo`.
pub const BOOT_INFO_PAGE_SIZE: u64 = 4096;
/// A framebuffer is present and directly writable.
pub const BOOT_FLAG_FRAMEBUFFER_PRESENT: u64 = 1 << 0;
/// A raw UEFI memory map is present.
pub const BOOT_FLAG_MEMORY_MAP_PRESENT: u64 = 1 << 1;
/// An ACPI RSDP address is present.
pub const BOOT_FLAG_RSDP_PRESENT: u64 = 1 << 2;
/// All presence flags understood by protocol version three.
pub const BOOT_KNOWN_FLAGS: u64 =
    BOOT_FLAG_FRAMEBUFFER_PRESENT | BOOT_FLAG_MEMORY_MAP_PRESENT | BOOT_FLAG_RSDP_PRESENT;

/// Pixel encodings supported by the initial diagnostic renderer.
///
/// This is intentionally an integer newtype rather than a Rust enum: every
/// possible firmware-provided bit pattern is valid to copy before validation.
#[repr(transparent)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PixelFormat(u32);

#[allow(non_upper_case_globals)]
impl PixelFormat {
    /// RGB byte order.
    pub const Rgb: Self = Self(0);
    /// BGR byte order.
    pub const Bgr: Self = Self(1);
    /// UEFI bitmask format, currently rejected by the kernel renderer.
    pub const Bitmask: Self = Self(2);
    /// GOP blit-only mode, not directly writable.
    pub const BltOnly: Self = Self(3);
    /// Unknown or unsupported format.
    pub const Unknown: Self = Self(0xffff_ffff);

    /// Construct a pixel format from its wire representation.
    #[must_use]
    pub const fn from_raw(raw: u32) -> Self {
        Self(raw)
    }

    /// Return the wire representation.
    #[must_use]
    pub const fn raw(self) -> u32 {
        self.0
    }
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
            pixel_format: PixelFormat::Rgb,
        }
    }
}

/// Version-three firmware-to-kernel handoff.
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
    /// Create an explicitly empty version-three structure.
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
                pixel_format: PixelFormat::Rgb,
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
    /// The handoff pointer is null.
    NullPointer,
    /// The handoff pointer is not aligned for `BootInfo`.
    MisalignedPointer,
    /// Magic does not identify `FinnOS`.
    BadMagic,
    /// Protocol version is unsupported.
    UnsupportedVersion,
    /// Structure size does not match version three.
    UnexpectedStructureSize,
    /// Presence flags include bits unknown to this protocol version.
    UnknownFlags,
    /// Required memory-map metadata is absent.
    MissingMemoryMap,
    /// Memory-map metadata is inconsistent.
    InvalidMemoryMap,
    /// Framebuffer metadata is inconsistent or unsupported.
    InvalidFramebuffer,
    /// Kernel image metadata is invalid or wraps the address space.
    InvalidKernelRange,
    /// `BootInfo` storage metadata is invalid or wraps the address space.
    InvalidBootInfoStorage,
    /// The copied structure is not fully contained in its declared storage.
    BootInfoOutsideDeclaredStorage,
    /// RSDP presence and address fields are inconsistent.
    InvalidRsdp,
}

const fn valid_range(start: u64, byte_len: u64) -> bool {
    start != 0 && byte_len != 0 && start.checked_add(byte_len).is_some()
}

/// Validate a boot-information structure without dereferencing any addresses.
///
/// # Errors
///
/// Returns a structured error when the version-three invariants are not met.
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
    if info.flags & !BOOT_KNOWN_FLAGS != 0 {
        return Err(BootInfoError::UnknownFlags);
    }
    if !valid_range(info.kernel_image.start, info.kernel_image.byte_len) {
        return Err(BootInfoError::InvalidKernelRange);
    }
    if !valid_range(
        info.boot_info_storage.start,
        info.boot_info_storage.byte_len,
    ) || !info
        .boot_info_storage
        .start
        .is_multiple_of(BOOT_INFO_PAGE_SIZE)
        || info.boot_info_storage.byte_len != BOOT_INFO_PAGE_SIZE
        || info.boot_info_storage.byte_len < u64::from(info.structure_size)
    {
        return Err(BootInfoError::InvalidBootInfoStorage);
    }
    if info.flags & BOOT_FLAG_MEMORY_MAP_PRESENT != 0 {
        if !valid_range(info.memory_map.address, info.memory_map.byte_len)
            || info.memory_map.descriptor_size < 40
            || info.memory_map.descriptor_size > info.memory_map.byte_len
            || !info
                .memory_map
                .byte_len
                .is_multiple_of(info.memory_map.descriptor_size)
            || info.memory_map.descriptor_version != 1
            || info.memory_map.reserved != 0
        {
            return Err(BootInfoError::InvalidMemoryMap);
        }
    } else if info.memory_map != MemoryMapInfo::default() {
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
            || !valid_range(info.framebuffer.address, info.framebuffer.byte_len)
            || bytes > info.framebuffer.byte_len
        {
            return Err(BootInfoError::InvalidFramebuffer);
        }
    } else if info.framebuffer != FramebufferInfo::default() {
        return Err(BootInfoError::InvalidFramebuffer);
    }
    if info.flags & BOOT_FLAG_RSDP_PRESENT != 0 {
        if info.rsdp_address == 0 {
            return Err(BootInfoError::InvalidRsdp);
        }
    } else if info.rsdp_address != 0 {
        return Err(BootInfoError::InvalidRsdp);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn valid_info() -> BootInfo {
        let mut info = BootInfo::empty();
        info.kernel_image = PhysicalRange {
            start: 0x10_0000,
            byte_len: 0x20_0000,
        };
        info.boot_info_storage = PhysicalRange {
            start: 0x40_0000,
            byte_len: 4096,
        };
        info
    }

    #[test]
    fn defaults_are_valid_without_optional_resources() {
        assert_eq!(BootInfo::empty().framebuffer.pixel_format.raw(), 0);
        assert_eq!(BootInfo::empty().framebuffer, FramebufferInfo::default());
        assert!(validate(&valid_info()).is_ok());
    }

    #[test]
    fn rejects_bad_magic() {
        let mut info = valid_info();
        info.magic = 0;
        assert_eq!(validate(&info), Err(BootInfoError::BadMagic));
    }

    #[test]
    fn rejects_version_and_size() {
        let mut info = valid_info();
        info.version = 2;
        assert_eq!(validate(&info), Err(BootInfoError::UnsupportedVersion));
        info.version = BOOT_PROTOCOL_VERSION + 1;
        assert_eq!(validate(&info), Err(BootInfoError::UnsupportedVersion));
        info.version = BOOT_PROTOCOL_VERSION;
        info.structure_size = 0;
        assert_eq!(validate(&info), Err(BootInfoError::UnexpectedStructureSize));
    }

    #[test]
    fn rejects_invalid_map_and_framebuffer() {
        let mut info = valid_info();
        info.flags = BOOT_FLAG_MEMORY_MAP_PRESENT;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidMemoryMap));
        info = valid_info();
        info.flags = BOOT_FLAG_FRAMEBUFFER_PRESENT;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidFramebuffer));
    }

    #[test]
    fn rejects_unknown_flags_and_invalid_required_ranges() {
        let mut info = valid_info();
        info.flags = 1 << 63;
        assert_eq!(validate(&info), Err(BootInfoError::UnknownFlags));

        info = valid_info();
        info.kernel_image.byte_len = u64::MAX;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidKernelRange));

        info = valid_info();
        info.boot_info_storage.byte_len = 1;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidBootInfoStorage));
        info = valid_info();
        info.boot_info_storage.start += 1;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidBootInfoStorage));
    }

    #[test]
    fn validates_complete_memory_map_metadata() {
        let mut info = valid_info();
        info.flags = BOOT_FLAG_MEMORY_MAP_PRESENT;
        info.memory_map = MemoryMapInfo {
            address: 0x80_0000,
            byte_len: 80,
            descriptor_size: 40,
            descriptor_version: 1,
            reserved: 0,
        };
        assert!(validate(&info).is_ok());

        info.memory_map.descriptor_size = 39;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidMemoryMap));
        info.memory_map.descriptor_size = 40;
        info.memory_map.byte_len = 81;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidMemoryMap));
        info.memory_map.byte_len = 80;
        info.memory_map.descriptor_version = 2;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidMemoryMap));
        info.memory_map.descriptor_version = 1;
        info.memory_map.reserved = 1;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidMemoryMap));
    }

    #[test]
    fn rejects_nonzero_absent_fields_and_bad_rsdp_consistency() {
        let mut info = valid_info();
        info.memory_map.descriptor_version = 1;
        assert_eq!(validate(&info), Err(BootInfoError::MissingMemoryMap));

        info = valid_info();
        info.framebuffer.pixel_format = PixelFormat::Bgr;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidFramebuffer));

        info = valid_info();
        info.rsdp_address = 0x1000;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidRsdp));
        info.flags = BOOT_FLAG_RSDP_PRESENT;
        assert!(validate(&info).is_ok());
        info.rsdp_address = 0;
        assert_eq!(validate(&info), Err(BootInfoError::InvalidRsdp));
    }

    #[test]
    fn arbitrary_pixel_format_bits_are_safe_and_rejected_when_present() {
        let format = PixelFormat::from_raw(0x1234_5678);
        assert_eq!(format.raw(), 0x1234_5678);

        let mut info = valid_info();
        info.flags = BOOT_FLAG_FRAMEBUFFER_PRESENT;
        info.framebuffer = FramebufferInfo {
            address: 0x90_0000,
            byte_len: 4,
            width: 1,
            height: 1,
            stride: 1,
            pixel_format: format,
        };
        assert_eq!(validate(&info), Err(BootInfoError::InvalidFramebuffer));
    }
}
