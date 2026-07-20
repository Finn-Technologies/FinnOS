//! Memory-map parsing, classification, and normalization.
//!
//! This module contains unavoidable unsafe code for converting a validated
//! physical address range into a byte slice. The rest of the parser operates on
//! `&[u8]` and is safe.
#![allow(unsafe_code)]
#![allow(clippy::missing_errors_doc)]

use finn_boot_protocol::{BootInfo, MemoryMapInfo, PhysicalRange};

use super::MemoryMapError;
use super::region::{MAX_MEMORY_REGIONS, MemoryRegion, MemoryRegionKind, MemoryRegionSource};
use super::uefi::{MIN_DESCRIPTOR_SIZE, decode_descriptor, descriptor_to_region};

/// A fixed-capacity table of classified memory regions.
#[derive(Clone, Debug)]
pub struct RegionTable {
    regions: [MemoryRegion; MAX_MEMORY_REGIONS],
    count: usize,
}

impl RegionTable {
    /// Create an empty region table.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            regions: [MemoryRegion {
                start: 0,
                byte_len: 0,
                kind: MemoryRegionKind::Reserved,
                source: MemoryRegionSource::FinnOS,
                attributes: 0,
            }; MAX_MEMORY_REGIONS],
            count: 0,
        }
    }

    /// Number of regions in the table.
    #[must_use]
    pub const fn len(&self) -> usize {
        self.count
    }

    /// True if the table contains no regions.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Access the stored regions as a slice.
    #[must_use]
    pub fn as_slice(&self) -> &[MemoryRegion] {
        &self.regions[..self.count]
    }

    /// Append a region, returning an error if capacity is exceeded.
    pub const fn push(&mut self, region: MemoryRegion) -> Result<(), MemoryMapError> {
        if self.count >= MAX_MEMORY_REGIONS {
            return Err(MemoryMapError::OutputCapacityExceeded);
        }
        self.regions[self.count] = region;
        self.count += 1;
        Ok(())
    }

    /// Insert a region at `index`, shifting later regions right.
    pub fn insert_at(&mut self, index: usize, region: MemoryRegion) -> Result<(), MemoryMapError> {
        if self.count >= MAX_MEMORY_REGIONS || index > self.count {
            return Err(MemoryMapError::OutputCapacityExceeded);
        }
        for i in (index..self.count).rev() {
            self.regions[i + 1] = self.regions[i];
        }
        self.regions[index] = region;
        self.count += 1;
        Ok(())
    }

    /// Remove a region at `index`, shifting later regions left.
    pub fn remove_at(&mut self, index: usize) {
        if index >= self.count {
            return;
        }
        for i in index..self.count - 1 {
            self.regions[i] = self.regions[i + 1];
        }
        self.count -= 1;
    }
}

impl Default for RegionTable {
    fn default() -> Self {
        Self::new()
    }
}

/// Validate memory-map metadata and return the raw byte slice for this parse.
///
/// # Safety
///
/// The caller must ensure `info.memory_map.address` and `info.memory_map.byte_len`
/// describe a valid, readable physical region. This function only performs
/// arithmetic validation.
unsafe fn memory_map_bytes(info: &MemoryMapInfo) -> Result<&[u8], MemoryMapError> {
    if info.descriptor_size == 0 {
        return Err(MemoryMapError::ZeroDescriptorSize);
    }
    if info.descriptor_size < MIN_DESCRIPTOR_SIZE {
        return Err(MemoryMapError::DescriptorTooSmall);
    }
    if info.byte_len == 0 {
        return Err(MemoryMapError::MissingMemoryMap);
    }
    if !info.byte_len.is_multiple_of(info.descriptor_size) {
        return Err(MemoryMapError::MisalignedMapLength);
    }
    let descriptor_count = info
        .byte_len
        .checked_div(info.descriptor_size)
        .ok_or(MemoryMapError::AddressOverflow)?;
    if descriptor_count > u64::from(u32::MAX) {
        return Err(MemoryMapError::AddressOverflow);
    }
    let start = info.address;
    let end = info
        .address
        .checked_add(info.byte_len)
        .ok_or(MemoryMapError::AddressOverflow)?;
    if start == 0 {
        return Err(MemoryMapError::NullMemoryMapAddress);
    }
    if end <= start {
        return Err(MemoryMapError::AddressOverflow);
    }

    // SAFETY: The caller guarantees the physical range is readable. The kernel
    // entry validates BootInfo before reaching this point.
    let byte_len = usize::try_from(info.byte_len).map_err(|_| MemoryMapError::AddressOverflow)?;
    // SAFETY: Required by this function's contract; arithmetic and length
    // conversion were checked above.
    let slice = unsafe { core::slice::from_raw_parts(start as *const u8, byte_len) };
    Ok(slice)
}

/// Parse and classify the UEFI memory map into `FinnOS` memory regions.
///
/// The returned table is sorted, non-overlapping, and has adjacent regions of
/// the same kind merged.
///
/// # Safety
///
/// `info.memory_map` must describe a valid readable buffer that remains alive
/// for this call. Callers must first validate the copied `BootInfo` handoff and
/// ensure the firmware-owned storage remains reserved.
pub unsafe fn parse_and_classify(
    info: &BootInfo,
) -> Result<(RegionTable, MemoryMapSummary), MemoryMapError> {
    // SAFETY: Forwarding the public function's raw-buffer contract.
    unsafe { parse_and_classify_impl(info, true) }
}

#[cfg(test)]
pub(super) unsafe fn parse_and_classify_without_range_containment_for_tests(
    info: &BootInfo,
) -> Result<(RegionTable, MemoryMapSummary), MemoryMapError> {
    // SAFETY: Forwarding the test helper's raw-buffer contract. Parser unit
    // tests use synthetic host addresses that are not physical-map entries;
    // containment itself is tested directly against constructed tables.
    unsafe { parse_and_classify_impl(info, false) }
}

unsafe fn parse_and_classify_impl(
    info: &BootInfo,
    validate_protected_ranges: bool,
) -> Result<(RegionTable, MemoryMapSummary), MemoryMapError> {
    if info.flags & finn_boot_protocol::BOOT_FLAG_MEMORY_MAP_PRESENT == 0 {
        return Err(MemoryMapError::MissingMemoryMap);
    }
    let map = &info.memory_map;

    // SAFETY: The kernel entry validates the physical range before calling this.
    // SAFETY: This function's caller provides the same raw-buffer guarantee.
    let bytes = unsafe { memory_map_bytes(map)? };

    let mut table = RegionTable::new();
    let descriptor_count = map
        .byte_len
        .checked_div(map.descriptor_size)
        .ok_or(MemoryMapError::AddressOverflow)?;

    for index in 0..descriptor_count {
        let offset = index
            .checked_mul(map.descriptor_size)
            .ok_or(MemoryMapError::AddressOverflow)?;
        let offset_usize = usize::try_from(offset).map_err(|_| MemoryMapError::AddressOverflow)?;
        let descriptor = decode_descriptor(bytes, offset_usize)?;
        if descriptor.page_count == 0 {
            continue;
        }
        let region = descriptor_to_region(&descriptor)?;
        table.push(region)?;
    }

    detect_overlapping_firmware_regions(&table)?;

    let exclusions = build_exclusions(info)?;
    if validate_protected_ranges {
        validate_exclusions(&table, &exclusions)?;
    }
    apply_exclusions(&mut table, &exclusions)?;

    normalize(&mut table)?;

    let summary = MemoryMapSummary::from_table(&table, descriptor_count);
    Ok((table, summary))
}

/// Build the list of FinnOS-protected ranges to exclude from firmware regions.
const fn build_exclusions(info: &BootInfo) -> Result<[MemoryRegion; 4], MemoryMapError> {
    let kernel_image = info.kernel_image;
    let boot_info_storage = info.boot_info_storage;
    let memory_map_storage = PhysicalRange {
        start: info.memory_map.address,
        byte_len: info.memory_map.byte_len,
    };

    if kernel_image.start == 0
        || kernel_image.byte_len == 0
        || kernel_image
            .start
            .checked_add(kernel_image.byte_len)
            .is_none()
    {
        return Err(MemoryMapError::InvalidKernelRange);
    }
    if boot_info_storage.start == 0
        || boot_info_storage.byte_len == 0
        || boot_info_storage
            .start
            .checked_add(boot_info_storage.byte_len)
            .is_none()
    {
        return Err(MemoryMapError::InvalidBootInfoRange);
    }
    if memory_map_storage.start == 0
        || memory_map_storage.byte_len == 0
        || memory_map_storage
            .start
            .checked_add(memory_map_storage.byte_len)
            .is_none()
    {
        return Err(MemoryMapError::InvalidMemoryMapStorageRange);
    }

    let mut exclusions = [
        MemoryRegion {
            start: kernel_image.start,
            byte_len: kernel_image.byte_len,
            kind: MemoryRegionKind::Kernel,
            source: MemoryRegionSource::FinnOS,
            attributes: 0,
        },
        MemoryRegion {
            start: boot_info_storage.start,
            byte_len: boot_info_storage.byte_len,
            kind: MemoryRegionKind::BootInfo,
            source: MemoryRegionSource::FinnOS,
            attributes: 0,
        },
        MemoryRegion {
            start: memory_map_storage.start,
            byte_len: memory_map_storage.byte_len,
            kind: MemoryRegionKind::MemoryMapStorage,
            source: MemoryRegionSource::FinnOS,
            attributes: 0,
        },
        MemoryRegion {
            start: 0,
            byte_len: 0,
            kind: MemoryRegionKind::Framebuffer,
            source: MemoryRegionSource::FinnOS,
            attributes: 0,
        },
    ];

    if info.flags & finn_boot_protocol::BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
        if info.framebuffer.byte_len == 0
            || info.framebuffer.address == 0
            || info
                .framebuffer
                .address
                .checked_add(info.framebuffer.byte_len)
                .is_none()
        {
            return Err(MemoryMapError::InvalidFramebufferRange);
        }
        exclusions[3].start = info.framebuffer.address;
        exclusions[3].byte_len = info.framebuffer.byte_len;
    }

    Ok(exclusions)
}

/// Validate protected-range separation and firmware-map containment before
/// applying any mutations to the region table.
fn validate_exclusions(
    table: &RegionTable,
    exclusions: &[MemoryRegion],
) -> Result<(), MemoryMapError> {
    for (index, range) in exclusions.iter().enumerate() {
        if range.byte_len == 0 {
            continue;
        }
        let range_end = range.end().ok_or(MemoryMapError::RegionOverflow)?;
        for other in exclusions.iter().skip(index + 1) {
            if other.byte_len == 0 {
                continue;
            }
            let other_end = other.end().ok_or(MemoryMapError::RegionOverflow)?;
            if range.start < other_end && other.start < range_end {
                return Err(MemoryMapError::OverlappingProtectedRanges);
            }
        }
        if range_is_covered_by_one_region(table, range.start, range_end)? {
            continue;
        }
        let framebuffer_is_separate = range.kind == MemoryRegionKind::Framebuffer
            && !range_overlaps_any_region(table, range.start, range_end)?;
        if !framebuffer_is_separate {
            return Err(MemoryMapError::ProtectedRangeOutsideFirmwareMap);
        }
    }
    Ok(())
}

/// Return true when one firmware descriptor fully covers `[start, end)`.
/// Requiring one owner prevents duplicate exclusions across adjacent entries.
fn range_is_covered_by_one_region(
    table: &RegionTable,
    start: u64,
    end: u64,
) -> Result<bool, MemoryMapError> {
    for region in table.as_slice() {
        let region_end = region.end().ok_or(MemoryMapError::RegionOverflow)?;
        if region.start <= start && end <= region_end {
            return Ok(true);
        }
    }
    Ok(false)
}

fn range_overlaps_any_region(
    table: &RegionTable,
    start: u64,
    end: u64,
) -> Result<bool, MemoryMapError> {
    for region in table.as_slice() {
        let region_end = region.end().ok_or(MemoryMapError::RegionOverflow)?;
        if region.start < end && start < region_end {
            return Ok(true);
        }
    }
    Ok(false)
}

/// Apply a single protected range to the region table, splitting regions as needed.
fn apply_exclusion(
    table: &mut RegionTable,
    exclusion: &MemoryRegion,
) -> Result<(), MemoryMapError> {
    if exclusion.byte_len == 0 {
        return Ok(());
    }
    let ex_end = exclusion.end().ok_or(MemoryMapError::RegionOverflow)?;
    let mut i = 0;
    while i < table.len() {
        let region = &table.regions[i];
        let Some(region_end) = region.end() else {
            i += 1;
            continue;
        };
        if region_end <= exclusion.start || region.start >= ex_end {
            i += 1;
            continue;
        }

        // Capture region fields before any mutation.
        let region_start = region.start;
        let region_byte_len = region.byte_len;
        let kind = region.kind;
        let source = region.source;
        let attributes = region.attributes;

        // Exclusion covers the entire region.
        if exclusion.start <= region_start && ex_end >= region_end {
            table.regions[i].kind = exclusion.kind;
            table.regions[i].source = MemoryRegionSource::FinnOS;
            i += 1;
            continue;
        }

        // Exclusion is in the middle: split into three regions (left, exclusion, right).
        if exclusion.start > region_start && ex_end < region_end {
            let left_len = exclusion.start - region_start;
            let right_len = region_end - ex_end;
            table.regions[i].byte_len = left_len;
            let right = MemoryRegion {
                start: ex_end,
                byte_len: right_len,
                kind,
                source,
                attributes,
            };
            table.insert_at(i + 1, right)?;
            table.insert_at(i + 1, *exclusion)?;
            i += 3;
            continue;
        }

        // Exclusion overlaps the start.
        if exclusion.start <= region_start && ex_end < region_end {
            let remaining = ex_end - region_start;
            table.regions[i].start = ex_end;
            table.regions[i].byte_len = region_byte_len - remaining;
            table.insert_at(i, *exclusion)?;
            i += 2;
            continue;
        }

        // Exclusion overlaps the end.
        if exclusion.start > region_start && ex_end >= region_end {
            let left_len = exclusion.start - region_start;
            table.regions[i].byte_len = left_len;
            table.insert_at(i + 1, *exclusion)?;
            i += 2;
            continue;
        }

        i += 1;
    }
    Ok(())
}

/// Apply all protected ranges using a documented priority order.
fn apply_exclusions(
    table: &mut RegionTable,
    exclusions: &[MemoryRegion],
) -> Result<(), MemoryMapError> {
    for exclusion in exclusions {
        apply_exclusion(table, exclusion)?;
    }
    // GOP apertures may be absent from the UEFI map. Preserve their ownership
    // explicitly after the separately validated non-overlap check.
    let framebuffer = &exclusions[3];
    if framebuffer.byte_len > 0 {
        let end = framebuffer.end().ok_or(MemoryMapError::RegionOverflow)?;
        if !range_overlaps_any_region(table, framebuffer.start, end)? {
            table.push(*framebuffer)?;
        }
    }
    Ok(())
}

/// Detect overlapping or conflicting firmware descriptors.
fn detect_overlapping_firmware_regions(table: &RegionTable) -> Result<(), MemoryMapError> {
    let slice = table.as_slice();
    for (i, a) in slice.iter().enumerate() {
        let a_end = a.end().ok_or(MemoryMapError::RegionOverflow)?;
        for b in slice.iter().skip(i + 1) {
            let b_end = b.end().ok_or(MemoryMapError::RegionOverflow)?;
            if a.start < b_end && b.start < a_end {
                return Err(MemoryMapError::OverlappingFirmwareRegions);
            }
        }
    }
    Ok(())
}

/// Sort, remove zero-length regions, and merge adjacent compatible regions.
fn normalize(table: &mut RegionTable) -> Result<(), MemoryMapError> {
    // Sort by start address using insertion sort (no allocation).
    let n = table.len();
    for i in 1..n {
        let mut j = i;
        while j > 0 && table.regions[j - 1].start > table.regions[j].start {
            table.regions.swap(j - 1, j);
            j -= 1;
        }
    }

    // Remove zero-length regions.
    let mut i = 0;
    while i < table.len() {
        if table.regions[i].byte_len == 0 {
            table.remove_at(i);
        } else {
            i += 1;
        }
    }

    // Merge adjacent regions with the same kind and compatible attributes.
    if table.len() < 2 {
        return Ok(());
    }
    let mut write = 0;
    for read in 1..table.len() {
        let prev_end = table.regions[write]
            .end()
            .ok_or(MemoryMapError::RegionOverflow)?;
        let curr = table.regions[read];
        let curr_end = curr.end().ok_or(MemoryMapError::RegionOverflow)?;
        if prev_end == curr.start
            && table.regions[write].kind == curr.kind
            && table.regions[write].attributes == curr.attributes
        {
            table.regions[write].byte_len = curr_end - table.regions[write].start;
        } else {
            write += 1;
            table.regions[write] = curr;
        }
    }
    table.count = write + 1;
    Ok(())
}

/// Summary statistics produced after classifying the memory map.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MemoryMapSummary {
    /// Number of UEFI descriptors parsed.
    pub descriptor_count: u64,
    /// Number of final `FinnOS` regions.
    pub region_count: usize,
    /// Total usable bytes.
    pub usable_bytes: u64,
    /// Total reserved bytes (including kernel, framebuffer, etc.).
    pub reserved_bytes: u64,
    /// Kernel image bytes.
    pub kernel_bytes: u64,
    /// `BootInfo` storage bytes.
    pub boot_info_bytes: u64,
    /// Raw memory-map storage bytes.
    pub memory_map_storage_bytes: u64,
    /// Framebuffer bytes.
    pub framebuffer_bytes: u64,
}

impl MemoryMapSummary {
    /// Compute summary statistics from a region table.
    #[must_use]
    pub fn from_table(table: &RegionTable, descriptor_count: u64) -> Self {
        let mut usable_bytes = 0u64;
        let mut reserved_bytes = 0u64;
        let mut kernel_bytes = 0u64;
        let mut boot_info_bytes = 0u64;
        let mut memory_map_storage_bytes = 0u64;
        let mut framebuffer_bytes = 0u64;
        for region in table.as_slice() {
            match region.kind {
                MemoryRegionKind::Usable => {
                    usable_bytes = usable_bytes.saturating_add(region.byte_len);
                }
                MemoryRegionKind::Kernel => {
                    kernel_bytes = kernel_bytes.saturating_add(region.byte_len);
                    reserved_bytes = reserved_bytes.saturating_add(region.byte_len);
                }
                MemoryRegionKind::BootInfo => {
                    boot_info_bytes = boot_info_bytes.saturating_add(region.byte_len);
                    reserved_bytes = reserved_bytes.saturating_add(region.byte_len);
                }
                MemoryRegionKind::MemoryMapStorage => {
                    memory_map_storage_bytes =
                        memory_map_storage_bytes.saturating_add(region.byte_len);
                    reserved_bytes = reserved_bytes.saturating_add(region.byte_len);
                }
                MemoryRegionKind::Framebuffer => {
                    framebuffer_bytes = framebuffer_bytes.saturating_add(region.byte_len);
                    reserved_bytes = reserved_bytes.saturating_add(region.byte_len);
                }
                _ => {
                    reserved_bytes = reserved_bytes.saturating_add(region.byte_len);
                }
            }
        }
        Self {
            descriptor_count,
            region_count: table.len(),
            usable_bytes,
            reserved_bytes,
            kernel_bytes,
            boot_info_bytes,
            memory_map_storage_bytes,
            framebuffer_bytes,
        }
    }
}

/// Validate that the region table is sorted, non-overlapping, and contains no
/// zero-length entries.
#[must_use]
pub fn validate_table(table: &RegionTable) -> bool {
    if table.is_empty() {
        return true;
    }
    let mut prev_end = 0u64;
    for region in table.as_slice() {
        if region.byte_len == 0 {
            return false;
        }
        let Some(end) = region.end() else {
            return false;
        };
        if region.start < prev_end {
            return false;
        }
        prev_end = end;
    }
    true
}
