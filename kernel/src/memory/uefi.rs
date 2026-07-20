//! Safe decoding of raw UEFI memory-map descriptors.
//!
//! All decoding operates on `&[u8]` using explicit little-endian reads.

use super::MemoryMapError;
use super::region::{MemoryRegion, MemoryRegionSource, UefiMemoryType};

/// Size of a UEFI page in bytes.
pub const UEFI_PAGE_SIZE: u64 = 4096;

/// Minimum size of a UEFI memory descriptor covering the fields the kernel reads.
///
/// UEFI descriptors are at least 40 bytes. Firmware may report a larger stride.
pub const MIN_DESCRIPTOR_SIZE: u64 = 40;

/// A decoded UEFI memory descriptor.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UefiDescriptor {
    /// UEFI memory type.
    pub memory_type: UefiMemoryType,
    /// Physical start address.
    pub physical_start: u64,
    /// Virtual start address (ignored for classification).
    pub virtual_start: u64,
    /// Number of 4 KiB pages.
    pub page_count: u64,
    /// UEFI attribute mask.
    pub attributes: u64,
}

impl UefiDescriptor {
    /// Byte length of the described range, or an error on overflow.
    ///
    /// # Errors
    ///
    /// Returns `PageCountOverflow` if the page count times the page size
    /// overflows `u64`.
    pub fn byte_len(&self) -> Result<u64, MemoryMapError> {
        self.page_count
            .checked_mul(UEFI_PAGE_SIZE)
            .ok_or(MemoryMapError::PageCountOverflow)
    }

    /// Exclusive end address of the described range, or an error on overflow.
    ///
    /// # Errors
    ///
    /// Returns `RegionOverflow` if the start plus byte length overflows `u64`.
    pub fn end(&self) -> Result<u64, MemoryMapError> {
        let len = self.byte_len()?;
        self.physical_start
            .checked_add(len)
            .ok_or(MemoryMapError::RegionOverflow)
    }
}

/// Decode a single UEFI descriptor from a byte slice starting at `offset`.
///
/// Only the first 40 bytes are interpreted; any trailing descriptor stride is
/// skipped by the caller.
///
/// # Errors
///
/// Returns `DescriptorTooSmall` if the slice is too short, or
/// `AddressOverflow` if the offset calculation overflows.
pub fn decode_descriptor(bytes: &[u8], offset: usize) -> Result<UefiDescriptor, MemoryMapError> {
    let min_size =
        usize::try_from(MIN_DESCRIPTOR_SIZE).map_err(|_| MemoryMapError::DescriptorTooSmall)?;
    let required = offset
        .checked_add(min_size)
        .ok_or(MemoryMapError::AddressOverflow)?;
    if bytes.len() < required {
        return Err(MemoryMapError::DescriptorTooSmall);
    }
    let raw_type = read_u32(bytes, offset);
    let physical_start = read_u64(bytes, offset + 8);
    let virtual_start = read_u64(bytes, offset + 16);
    let page_count = read_u64(bytes, offset + 24);
    let attributes = read_u64(bytes, offset + 32);

    let memory_type =
        UefiMemoryType::from_raw(raw_type).ok_or(MemoryMapError::UnknownFirmwareType)?;

    Ok(UefiDescriptor {
        memory_type,
        physical_start,
        virtual_start,
        page_count,
        attributes,
    })
}

/// Read a little-endian `u32` from a byte slice.
fn read_u32(bytes: &[u8], offset: usize) -> u32 {
    let mut buf = [0u8; 4];
    buf.copy_from_slice(&bytes[offset..offset + 4]);
    u32::from_le_bytes(buf)
}

/// Read a little-endian `u64` from a byte slice.
fn read_u64(bytes: &[u8], offset: usize) -> u64 {
    let mut buf = [0u8; 8];
    buf.copy_from_slice(&bytes[offset..offset + 8]);
    u64::from_le_bytes(buf)
}

/// Convert a decoded UEFI descriptor into a checked `FinnOS` memory region.
///
/// # Errors
///
/// Returns a typed overflow error when the page count or exclusive end cannot
/// be represented in `u64`.
pub fn descriptor_to_region(descriptor: &UefiDescriptor) -> Result<MemoryRegion, MemoryMapError> {
    let kind = descriptor.memory_type.classify();
    let byte_len = descriptor.byte_len()?;
    let _ = descriptor.end()?;
    Ok(MemoryRegion {
        start: descriptor.physical_start,
        byte_len,
        kind,
        source: MemoryRegionSource::Uefi(descriptor.memory_type),
        attributes: descriptor.attributes,
    })
}
