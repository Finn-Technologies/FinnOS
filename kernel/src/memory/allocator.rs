//! A fixed-capacity early physical page allocator.
//!
//! The allocator owns no memory and performs no mapping. It returns physical
//! addresses from classified `Usable` regions only.
#![allow(clippy::doc_markdown)]
#![allow(clippy::missing_errors_doc)]
#![allow(clippy::must_use_candidate)]
#![allow(clippy::option_if_let_else)]
#![allow(clippy::unused_self)]

use super::map::RegionTable;
use super::region::MemoryRegionKind;

/// The size of every physical page managed by FinnOS.
pub const PAGE_SIZE: u64 = 4096;
/// Maximum number of immutable managed extents.
pub const MAX_MANAGED_EXTENTS: usize = 256;
/// Maximum number of mutable free extents.
pub const MAX_FREE_EXTENTS: usize = 256;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Extent {
    start: u64,
    page_count: u64,
}

impl Extent {
    fn end(self) -> Result<u64, PageAllocationError> {
        let bytes = self
            .page_count
            .checked_mul(PAGE_SIZE)
            .ok_or(PageAllocationError::PageCountOverflow)?;
        self.start
            .checked_add(bytes)
            .ok_or(PageAllocationError::AddressOverflow)
    }
}

/// One validated physical page.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PhysicalPage {
    start: u64,
}

impl PhysicalPage {
    /// Construct a page from a 4 KiB-aligned physical address.
    pub const fn new(start: u64) -> Result<Self, PageAllocationError> {
        if !start.is_multiple_of(PAGE_SIZE) {
            return Err(PageAllocationError::AddressNotPageAligned);
        }
        Ok(Self { start })
    }

    /// Return the page's physical start address.
    pub const fn start_address(self) -> u64 {
        self.start
    }
}

/// A validated contiguous range of physical pages.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PageRange {
    start: u64,
    page_count: u64,
}

impl PageRange {
    /// Construct a non-empty, page-aligned physical page range.
    pub const fn new(start: u64, page_count: u64) -> Result<Self, PageAllocationError> {
        if !start.is_multiple_of(PAGE_SIZE) {
            return Err(PageAllocationError::AddressNotPageAligned);
        }
        if page_count == 0 {
            return Err(PageAllocationError::ZeroPageCount);
        }
        if page_count.checked_mul(PAGE_SIZE).is_none() {
            return Err(PageAllocationError::PageCountOverflow);
        }
        if start.checked_add(page_count * PAGE_SIZE).is_none() {
            return Err(PageAllocationError::AddressOverflow);
        }
        Ok(Self { start, page_count })
    }

    /// Return the first physical address in the range.
    pub const fn start_address(self) -> u64 {
        self.start
    }
    /// Return the number of pages in the range.
    pub const fn page_count(self) -> u64 {
        self.page_count
    }
    /// Return the range length in bytes.
    pub fn byte_len(self) -> Result<u64, PageAllocationError> {
        self.page_count
            .checked_mul(PAGE_SIZE)
            .ok_or(PageAllocationError::PageCountOverflow)
    }
    /// Return the exclusive physical end address.
    pub fn end_exclusive(self) -> Result<u64, PageAllocationError> {
        self.byte_len().and_then(|len| {
            self.start
                .checked_add(len)
                .ok_or(PageAllocationError::AddressOverflow)
        })
    }
    /// Return whether this range contains a physical address.
    pub fn contains(self, address: u64) -> bool {
        match self.end_exclusive() {
            Ok(end) => self.start <= address && address < end,
            Err(_) => false,
        }
    }
    /// Return whether two ranges touch without overlapping.
    pub fn is_adjacent(self, other: Self) -> bool {
        match (self.end_exclusive(), other.end_exclusive()) {
            (Ok(a), Ok(b)) => a == other.start || b == self.start,
            _ => false,
        }
    }
}

/// Errors returned by the early physical page allocator.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PageAllocationError {
    /// No classified usable memory was supplied.
    NoUsableMemory,
    /// A range requested zero pages.
    ZeroPageCount,
    /// An address was not 4 KiB aligned.
    AddressNotPageAligned,
    /// Physical address arithmetic overflowed.
    AddressOverflow,
    /// Page-count arithmetic overflowed.
    PageCountOverflow,
    /// A classified region was malformed.
    InvalidManagedRegion,
    /// Classified regions overlapped.
    OverlappingManagedRegions,
    /// The immutable extent table is full.
    ManagedExtentCapacityExceeded,
    /// The mutable free extent table is full.
    FreeExtentCapacityExceeded,
    /// No contiguous free range satisfies the request.
    OutOfMemory,
    /// A returned range is not inside one managed extent.
    RangeOutsideManagedMemory,
    /// A range was returned twice.
    DoubleFree,
    /// A returned range overlaps free memory.
    FreeRangeOverlap,
    /// A page counter would overflow or underflow.
    CounterOverflow,
    /// Internal extent invariants are inconsistent.
    CorruptAllocatorState,
}

/// A deterministic, allocation-free first-fit allocator for physical pages.
pub struct EarlyPhysicalPageAllocator {
    managed: [Extent; MAX_MANAGED_EXTENTS],
    managed_count: usize,
    free: [Extent; MAX_FREE_EXTENTS],
    free_count: usize,
    total_pages: u64,
    free_pages: u64,
}

impl EarlyPhysicalPageAllocator {
    /// Build an allocator from the normalized classified memory map.
    pub fn from_memory_regions(table: &RegionTable) -> Result<Self, PageAllocationError> {
        let empty = Extent {
            start: 0,
            page_count: 0,
        };
        let mut allocator = Self {
            managed: [empty; MAX_MANAGED_EXTENTS],
            managed_count: 0,
            free: [empty; MAX_FREE_EXTENTS],
            free_count: 0,
            total_pages: 0,
            free_pages: 0,
        };
        let mut previous_end = 0;
        let mut has_previous = false;
        for region in table.as_slice() {
            let end = region
                .end()
                .ok_or(PageAllocationError::InvalidManagedRegion)?;
            if region.byte_len == 0 {
                return Err(PageAllocationError::InvalidManagedRegion);
            }
            if has_previous && region.start < previous_end {
                return Err(PageAllocationError::OverlappingManagedRegions);
            }
            previous_end = end;
            has_previous = true;
            if region.kind != MemoryRegionKind::Usable {
                continue;
            }
            let start = region
                .start
                .checked_add(PAGE_SIZE - 1)
                .ok_or(PageAllocationError::AddressOverflow)?
                & !(PAGE_SIZE - 1);
            let aligned_end = end & !(PAGE_SIZE - 1);
            if start >= aligned_end {
                continue;
            }
            let pages = (aligned_end - start) / PAGE_SIZE;
            let extent = Extent {
                start,
                page_count: pages,
            };
            if allocator.managed_count > 0
                && allocator.managed[allocator.managed_count - 1].end()? == start
            {
                let previous = &mut allocator.managed[allocator.managed_count - 1];
                previous.page_count = previous
                    .page_count
                    .checked_add(pages)
                    .ok_or(PageAllocationError::CounterOverflow)?;
                allocator.free[allocator.free_count - 1].page_count = allocator.free
                    [allocator.free_count - 1]
                    .page_count
                    .checked_add(pages)
                    .ok_or(PageAllocationError::CounterOverflow)?;
            } else {
                if allocator.managed_count == MAX_MANAGED_EXTENTS {
                    return Err(PageAllocationError::ManagedExtentCapacityExceeded);
                }
                if allocator.free_count == MAX_FREE_EXTENTS {
                    return Err(PageAllocationError::FreeExtentCapacityExceeded);
                }
                allocator.managed[allocator.managed_count] = extent;
                allocator.managed_count += 1;
                allocator.free[allocator.free_count] = extent;
                allocator.free_count += 1;
            }
            allocator.total_pages = allocator
                .total_pages
                .checked_add(pages)
                .ok_or(PageAllocationError::CounterOverflow)?;
            allocator.free_pages = allocator
                .free_pages
                .checked_add(pages)
                .ok_or(PageAllocationError::CounterOverflow)?;
        }
        if allocator.total_pages == 0 {
            return Err(PageAllocationError::NoUsableMemory);
        }
        allocator.check_invariants()?;
        Ok(allocator)
    }

    /// Allocate one physical page using first-fit allocation.
    pub fn allocate_page(&mut self) -> Result<PhysicalPage, PageAllocationError> {
        let range = self.allocate_contiguous(1)?;
        Ok(PhysicalPage { start: range.start })
    }

    /// Allocate contiguous pages from the lowest suitable free extent.
    pub fn allocate_contiguous(
        &mut self,
        page_count: u64,
    ) -> Result<PageRange, PageAllocationError> {
        let requested = PageRange::new(0, page_count).map_err(|error| {
            if error == PageAllocationError::AddressOverflow {
                PageAllocationError::PageCountOverflow
            } else {
                error
            }
        })?;
        let index = (0..self.free_count)
            .find(|&i| self.free[i].page_count >= requested.page_count)
            .ok_or(PageAllocationError::OutOfMemory)?;
        let extent = self.free[index];
        let result = PageRange::new(extent.start, page_count)?;
        self.free_pages = self
            .free_pages
            .checked_sub(page_count)
            .ok_or(PageAllocationError::CorruptAllocatorState)?;
        if extent.page_count == page_count {
            self.remove_free(index);
        } else {
            self.free[index].start = self.free[index]
                .start
                .checked_add(page_count * PAGE_SIZE)
                .ok_or(PageAllocationError::AddressOverflow)?;
            self.free[index].page_count -= page_count;
        }
        self.check_invariants()?;
        Ok(result)
    }

    /// Return an allocated range to the free list, merging adjacent extents.
    pub fn deallocate(&mut self, range: PageRange) -> Result<(), PageAllocationError> {
        if !self.contains_managed(range) {
            return Err(PageAllocationError::RangeOutsideManagedMemory);
        }
        let mut index = 0;
        while index < self.free_count && self.free[index].start < range.start {
            index += 1;
        }
        if index > 0 && self.free[index - 1].end()? > range.start {
            return Err(PageAllocationError::DoubleFree);
        }
        if index < self.free_count && range.end_exclusive()? > self.free[index].start {
            return Err(PageAllocationError::FreeRangeOverlap);
        }
        let joins_previous = index > 0 && self.free[index - 1].end()? == range.start;
        let joins_next =
            index < self.free_count && range.end_exclusive()? == self.free[index].start;
        if !joins_previous && !joins_next && self.free_count == MAX_FREE_EXTENTS {
            return Err(PageAllocationError::FreeExtentCapacityExceeded);
        }
        if joins_previous {
            self.free[index - 1].page_count = self.free[index - 1]
                .page_count
                .checked_add(range.page_count)
                .ok_or(PageAllocationError::CounterOverflow)?;
            if joins_next {
                self.free[index - 1].page_count = self.free[index - 1]
                    .page_count
                    .checked_add(self.free[index].page_count)
                    .ok_or(PageAllocationError::CounterOverflow)?;
                self.remove_free(index);
            }
        } else if joins_next {
            self.free[index].start = range.start;
            self.free[index].page_count = self.free[index]
                .page_count
                .checked_add(range.page_count)
                .ok_or(PageAllocationError::CounterOverflow)?;
        } else {
            self.insert_free(
                index,
                Extent {
                    start: range.start,
                    page_count: range.page_count,
                },
            );
        }
        self.free_pages = self
            .free_pages
            .checked_add(range.page_count)
            .ok_or(PageAllocationError::CorruptAllocatorState)?;
        self.check_invariants()
    }

    /// Return the total number of managed pages.
    pub const fn total_pages(&self) -> u64 {
        self.total_pages
    }
    /// Return the number of currently free pages.
    pub const fn free_pages(&self) -> u64 {
        self.free_pages
    }
    /// Return the number of allocated pages.
    pub const fn allocated_pages(&self) -> u64 {
        self.total_pages - self.free_pages
    }
    /// Return the number of managed extents.
    pub const fn managed_extent_count(&self) -> usize {
        self.managed_count
    }
    /// Return the number of free extents.
    pub const fn free_extent_count(&self) -> usize {
        self.free_count
    }
    /// Validate all allocator invariants.
    pub fn check_invariants(&self) -> Result<(), PageAllocationError> {
        if self.free_pages > self.total_pages {
            return Err(PageAllocationError::CorruptAllocatorState);
        }
        for i in 0..self.managed_count {
            if self.managed[i].page_count == 0
                || (i > 0 && self.managed[i - 1].end()? >= self.managed[i].start)
            {
                return Err(PageAllocationError::CorruptAllocatorState);
            }
        }
        for i in 0..self.free_count {
            if self.free[i].page_count == 0
                || (i > 0 && self.free[i - 1].end()? >= self.free[i].start)
                || !self.contains_managed_extent(self.free[i])
            {
                return Err(PageAllocationError::CorruptAllocatorState);
            }
        }
        Ok(())
    }

    fn contains_managed(&self, range: PageRange) -> bool {
        (0..self.managed_count).any(|i| self.contains_managed_extent_in(self.managed[i], range))
    }
    fn contains_managed_extent(&self, extent: Extent) -> bool {
        (0..self.managed_count).any(|i| {
            self.managed[i] == extent
                || self.contains_managed_extent_in(
                    self.managed[i],
                    PageRange {
                        start: extent.start,
                        page_count: extent.page_count,
                    },
                )
        })
    }
    fn contains_managed_extent_in(&self, extent: Extent, range: PageRange) -> bool {
        match (extent.end(), range.end_exclusive()) {
            (Ok(end), Ok(range_end)) => extent.start <= range.start && range_end <= end,
            _ => false,
        }
    }
    fn remove_free(&mut self, index: usize) {
        for i in index..self.free_count - 1 {
            self.free[i] = self.free[i + 1];
        }
        self.free_count -= 1;
    }
    fn insert_free(&mut self, index: usize, extent: Extent) {
        for i in (index..self.free_count).rev() {
            self.free[i + 1] = self.free[i];
        }
        self.free[index] = extent;
        self.free_count += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::memory::{MemoryRegion, MemoryRegionKind, MemoryRegionSource, RegionTable};

    fn table(regions: &[(u64, u64, MemoryRegionKind)]) -> RegionTable {
        let mut table = RegionTable::new();
        for &(start, byte_len, kind) in regions {
            table
                .push(MemoryRegion {
                    start,
                    byte_len,
                    kind,
                    source: MemoryRegionSource::FinnOS,
                    attributes: 0,
                })
                .unwrap();
        }
        table
    }

    #[test]
    fn validates_page_and_range_inputs() {
        assert_eq!(
            PhysicalPage::new(1),
            Err(PageAllocationError::AddressNotPageAligned)
        );
        assert_eq!(
            PageRange::new(0x1000, 0),
            Err(PageAllocationError::ZeroPageCount)
        );
        assert_eq!(
            PageRange::new(0x1000, u64::MAX),
            Err(PageAllocationError::PageCountOverflow)
        );
        assert_eq!(
            PageRange::new(0x1001, 1),
            Err(PageAllocationError::AddressNotPageAligned)
        );
        let range = PageRange::new(u64::MAX - (2 * PAGE_SIZE) + 1, 1);
        assert!(range.is_ok());
    }

    #[test]
    fn ignores_non_usable_regions_and_aligns_inward() {
        let allocator = EarlyPhysicalPageAllocator::from_memory_regions(&table(&[
            (0x1003, 0x4000, MemoryRegionKind::Usable),
            (0x6000, 0x1000, MemoryRegionKind::Reserved),
        ]))
        .unwrap();
        assert_eq!(allocator.managed_extent_count(), 1);
        assert_eq!(allocator.total_pages(), 3);
        assert_eq!(allocator.free_pages(), 3);
    }

    #[test]
    fn first_fit_contiguous_allocates_and_reuses_hole() {
        let mut allocator = EarlyPhysicalPageAllocator::from_memory_regions(&table(&[
            (0x1000, 4 * PAGE_SIZE, MemoryRegionKind::Usable),
            (0x9000, 2 * PAGE_SIZE, MemoryRegionKind::Usable),
        ]))
        .unwrap();
        let first = allocator.allocate_contiguous(2).unwrap();
        let second = allocator.allocate_page().unwrap();
        assert_eq!(first.start_address(), 0x1000);
        assert_eq!(second.start_address(), 0x3000);
        allocator.deallocate(first).unwrap();
        assert_eq!(allocator.allocate_page().unwrap().start_address(), 0x1000);
    }

    #[test]
    fn deallocation_merges_and_rejects_double_free_or_unmanaged_ranges() {
        let mut allocator = EarlyPhysicalPageAllocator::from_memory_regions(&table(&[(
            0x1000,
            4 * PAGE_SIZE,
            MemoryRegionKind::Usable,
        )]))
        .unwrap();
        let a = allocator.allocate_page().unwrap();
        let b = allocator.allocate_page().unwrap();
        let c = allocator.allocate_page().unwrap();
        allocator
            .deallocate(PageRange::new(b.start_address(), 1).unwrap())
            .unwrap();
        allocator
            .deallocate(PageRange::new(a.start_address(), 1).unwrap())
            .unwrap();
        allocator
            .deallocate(PageRange::new(c.start_address(), 1).unwrap())
            .unwrap();
        assert_eq!(allocator.free_extent_count(), 1);
        assert_eq!(allocator.free_pages(), allocator.total_pages());
        assert!(matches!(
            allocator.deallocate(PageRange::new(a.start_address(), 1).unwrap()),
            Err(PageAllocationError::DoubleFree | PageAllocationError::FreeRangeOverlap)
        ));
        assert_eq!(
            allocator.deallocate(PageRange::new(0x9000, 1).unwrap()),
            Err(PageAllocationError::RangeOutsideManagedMemory)
        );
    }

    #[test]
    fn no_usable_memory_is_an_error() {
        assert!(matches!(
            EarlyPhysicalPageAllocator::from_memory_regions(&table(&[(
                0x1000,
                PAGE_SIZE,
                MemoryRegionKind::Reserved
            )])),
            Err(PageAllocationError::NoUsableMemory)
        ));
    }
}
