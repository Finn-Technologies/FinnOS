//! Owned display-buffer policy for bounded VirtIO-GPU presentation.
//!
//! This module owns geometry and physical-range bookkeeping, but does not
//! map memory or submit device commands. Architecture adapters may use the
//! resulting guest-physical address only after they have made the pages
//! reachable under their own paging and cache-coherency rules.

use crate::memory::{EarlyPhysicalPageAllocator, PAGE_SIZE, PageAllocationError, PageRange};

/// Bytes per pixel used by the current 2D presentation format (BGRX/BGRA).
pub const GPU_DISPLAY_BYTES_PER_PIXEL: u64 = 4;
/// Maximum pages one bounded display buffer may own (4 MiB with 4 KiB pages).
pub const GPU_DISPLAY_MAX_PAGES: u64 = 1_024;
/// Conservative upper bound for four-level 4 KiB translation-table pages.
///
/// A contiguous range can straddle an extra level-1 table, in addition to
/// one level-0 table per 512 pages and the three ancestor tables. This is a
/// capacity bound, not a promise that a particular virtual address is aligned.
pub const GPU_DISPLAY_MAX_TABLE_PAGES_BOUND: u64 = 6;

/// Errors returned while validating or owning a GPU display buffer.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GpuDisplayBufferError {
    /// Width was zero.
    ZeroWidth,
    /// Height was zero.
    ZeroHeight,
    /// Stride was zero.
    ZeroStride,
    /// Stride was smaller than the visible width.
    StrideTooSmall,
    /// Geometry arithmetic overflowed.
    GeometryOverflow,
    /// The required page count exceeds the bounded display-buffer policy.
    CapacityExceeded {
        /// Requested number of 4 KiB pages.
        requested_pages: u64,
        /// Maximum number of pages allowed by the policy.
        max_pages: u64,
    },
    /// The supplied range is smaller than the visible frame.
    BackingTooSmall {
        /// Bytes required by the visible frame.
        required_bytes: u64,
        /// Bytes available in the supplied page range.
        available_bytes: u64,
    },
    /// The backing range is not representable by the VirtIO-GPU u32 length.
    BackingLengthOverflow {
        /// Backing length in bytes.
        byte_len: u64,
    },
    /// The backing range starts at the null physical address.
    NullBackingAddress,
    /// The early allocator rejected the requested contiguous range.
    Allocation(PageAllocationError),
    /// The early allocator rejected the release request.
    Release(PageAllocationError),
    /// The buffer was already released.
    AlreadyReleased,
}

/// A kernel-owned physical backing range for one 2D display surface.
///
/// The object is intentionally not `Copy` or `Clone`: while it is live, it
/// represents one allocator-owned `PageRange`. Call [`Self::release`] after
/// the GPU and all DMA activity have stopped. A failed release leaves the
/// range owned by this object and can be retried.
#[derive(Debug, Eq, PartialEq)]
pub struct GpuDisplayBuffer {
    range: Option<PageRange>,
    width: u32,
    height: u32,
    stride: u32,
    visible_byte_len: u64,
    backing_byte_len: u32,
}

impl GpuDisplayBuffer {
    /// Validate geometry and take ownership of an existing physical range.
    ///
    /// The range may be larger than the visible frame, but both its page
    /// count and its byte length remain bounded by the display policy.
    ///
    /// # Errors
    ///
    /// Returns [`GpuDisplayBufferError`] for a null backing address,
    /// malformed geometry, an undersized range, arithmetic overflow, or a
    /// range outside the bounded display-buffer policy.
    pub fn from_page_range(
        width: u32,
        height: u32,
        stride: u32,
        range: PageRange,
    ) -> Result<Self, GpuDisplayBufferError> {
        let geometry = Self::checked_geometry(width, height, stride)?;
        let required_pages = Self::required_pages_for_bytes(geometry.visible_byte_len)?;
        if range.start_address() == 0 {
            return Err(GpuDisplayBufferError::NullBackingAddress);
        }
        if range.page_count() > GPU_DISPLAY_MAX_PAGES || required_pages > GPU_DISPLAY_MAX_PAGES {
            return Err(GpuDisplayBufferError::CapacityExceeded {
                requested_pages: range.page_count().max(required_pages),
                max_pages: GPU_DISPLAY_MAX_PAGES,
            });
        }
        let available_bytes = range
            .byte_len()
            .map_err(|_| GpuDisplayBufferError::GeometryOverflow)?;
        if available_bytes < geometry.visible_byte_len {
            return Err(GpuDisplayBufferError::BackingTooSmall {
                required_bytes: geometry.visible_byte_len,
                available_bytes,
            });
        }
        let backing_byte_len = u32::try_from(available_bytes).map_err(|_| {
            GpuDisplayBufferError::BackingLengthOverflow {
                byte_len: available_bytes,
            }
        })?;
        Ok(Self {
            range: Some(range),
            width,
            height,
            stride,
            visible_byte_len: geometry.visible_byte_len,
            backing_byte_len,
        })
    }

    /// Allocate an owned display buffer from the early physical allocator.
    ///
    /// # Errors
    ///
    /// Returns [`GpuDisplayBufferError`] when geometry is invalid, the
    /// required range exceeds policy capacity, or the allocator cannot
    /// provide the contiguous range.
    pub fn allocate(
        allocator: &mut EarlyPhysicalPageAllocator,
        width: u32,
        height: u32,
        stride: u32,
    ) -> Result<Self, GpuDisplayBufferError> {
        let geometry = Self::checked_geometry(width, height, stride)?;
        let required_pages = Self::required_pages_for_bytes(geometry.visible_byte_len)?;
        if required_pages > GPU_DISPLAY_MAX_PAGES {
            return Err(GpuDisplayBufferError::CapacityExceeded {
                requested_pages: required_pages,
                max_pages: GPU_DISPLAY_MAX_PAGES,
            });
        }
        let range = allocator
            .allocate_contiguous(required_pages)
            .map_err(GpuDisplayBufferError::Allocation)?;
        match Self::from_page_range(width, height, stride, range) {
            Ok(buffer) => Ok(buffer),
            Err(error) => {
                // The range came from this allocator and is not published to
                // the caller until the policy accepts it.
                let _ = allocator.deallocate(range);
                Err(error)
            }
        }
    }

    /// Validate display geometry without allocating memory.
    ///
    /// # Errors
    ///
    /// Returns [`GpuDisplayBufferError`] for zero dimensions, an invalid
    /// stride, or arithmetic overflow.
    pub fn checked_geometry(
        width: u32,
        height: u32,
        stride: u32,
    ) -> Result<DisplayGeometry, GpuDisplayBufferError> {
        if width == 0 {
            return Err(GpuDisplayBufferError::ZeroWidth);
        }
        if height == 0 {
            return Err(GpuDisplayBufferError::ZeroHeight);
        }
        if stride == 0 {
            return Err(GpuDisplayBufferError::ZeroStride);
        }
        if stride < width {
            return Err(GpuDisplayBufferError::StrideTooSmall);
        }
        let row_bytes = u64::from(stride)
            .checked_mul(GPU_DISPLAY_BYTES_PER_PIXEL)
            .ok_or(GpuDisplayBufferError::GeometryOverflow)?;
        let visible_byte_len = row_bytes
            .checked_mul(u64::from(height))
            .ok_or(GpuDisplayBufferError::GeometryOverflow)?;
        Ok(DisplayGeometry {
            width,
            height,
            stride,
            visible_byte_len,
        })
    }

    /// Return the number of pages required for a visible byte length.
    ///
    /// # Errors
    ///
    /// Returns [`GpuDisplayBufferError::GeometryOverflow`] for a zero or
    /// unrepresentable byte length.
    pub fn required_pages_for_bytes(visible_byte_len: u64) -> Result<u64, GpuDisplayBufferError> {
        if visible_byte_len == 0 {
            return Err(GpuDisplayBufferError::GeometryOverflow);
        }
        visible_byte_len
            .checked_add(PAGE_SIZE - 1)
            .map(|value| value / PAGE_SIZE)
            .ok_or(GpuDisplayBufferError::GeometryOverflow)
    }

    /// Return the number of pages required for a visible frame.
    ///
    /// # Errors
    ///
    /// Returns [`GpuDisplayBufferError`] for invalid geometry, arithmetic
    /// overflow, or a frame larger than the bounded display policy.
    pub fn required_pages(
        width: u32,
        height: u32,
        stride: u32,
    ) -> Result<u64, GpuDisplayBufferError> {
        let geometry = Self::checked_geometry(width, height, stride)?;
        let required_pages = Self::required_pages_for_bytes(geometry.visible_byte_len)?;
        if required_pages > GPU_DISPLAY_MAX_PAGES {
            return Err(GpuDisplayBufferError::CapacityExceeded {
                requested_pages: required_pages,
                max_pages: GPU_DISPLAY_MAX_PAGES,
            });
        }
        Ok(required_pages)
    }

    /// Return the visible surface width in pixels.
    #[must_use]
    pub const fn width(&self) -> u32 {
        self.width
    }

    /// Return the visible surface height in pixels.
    #[must_use]
    pub const fn height(&self) -> u32 {
        self.height
    }

    /// Return the source stride in pixels.
    #[must_use]
    pub const fn stride(&self) -> u32 {
        self.stride
    }

    /// Return the guest-physical address of the owned backing range.
    pub fn backing_address(&self) -> Option<u64> {
        self.range.map(PageRange::start_address)
    }

    /// Return the number of owned physical pages.
    pub fn page_count(&self) -> u64 {
        self.range.map_or(0, PageRange::page_count)
    }

    /// Return the visible frame length, including row padding.
    #[must_use]
    pub const fn visible_byte_len(&self) -> u64 {
        self.visible_byte_len
    }

    /// Return the full page-rounded backing length used by a GPU attachment.
    #[must_use]
    pub const fn backing_byte_len(&self) -> u32 {
        self.backing_byte_len
    }

    /// Return whether the backing range has been released.
    #[must_use]
    pub const fn is_released(&self) -> bool {
        self.range.is_none()
    }

    /// Release the owned range after all device activity has stopped.
    ///
    /// # Errors
    ///
    /// Returns [`GpuDisplayBufferError::AlreadyReleased`] when no range is
    /// owned, or [`GpuDisplayBufferError::Release`] when the allocator
    /// refuses the deallocation. A failed release leaves ownership intact.
    pub fn release(
        &mut self,
        allocator: &mut EarlyPhysicalPageAllocator,
    ) -> Result<(), GpuDisplayBufferError> {
        let range = self.range.ok_or(GpuDisplayBufferError::AlreadyReleased)?;
        allocator
            .deallocate(range)
            .map_err(GpuDisplayBufferError::Release)?;
        self.range = None;
        Ok(())
    }

    /// Return a conservative four-level 4 KiB table-page upper bound.
    ///
    /// # Errors
    ///
    /// Returns [`GpuDisplayBufferError::CapacityExceeded`] for zero pages or
    /// a page count outside the bounded display policy, and
    /// [`GpuDisplayBufferError::GeometryOverflow`] for arithmetic overflow.
    pub fn translation_table_page_upper_bound(
        page_count: u64,
    ) -> Result<u64, GpuDisplayBufferError> {
        if page_count == 0 || page_count > GPU_DISPLAY_MAX_PAGES {
            return Err(GpuDisplayBufferError::CapacityExceeded {
                requested_pages: page_count,
                max_pages: GPU_DISPLAY_MAX_PAGES,
            });
        }
        let leaf_tables = page_count
            .checked_add(511)
            .map(|value| value / 512)
            .ok_or(GpuDisplayBufferError::GeometryOverflow)?;
        leaf_tables
            .checked_add(1)
            .and_then(|value| value.checked_add(3))
            .ok_or(GpuDisplayBufferError::GeometryOverflow)
    }
}

/// Checked geometry shared by allocation and presentation policy.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DisplayGeometry {
    /// Visible surface width in pixels.
    pub width: u32,
    /// Visible surface height in pixels.
    pub height: u32,
    /// Source stride in pixels.
    pub stride: u32,
    /// Visible frame length in bytes, including row padding.
    pub visible_byte_len: u64,
}

#[cfg(test)]
mod tests {
    use super::{DisplayGeometry, GPU_DISPLAY_MAX_PAGES, GpuDisplayBuffer, GpuDisplayBufferError};
    use crate::memory::{EarlyPhysicalPageAllocator, PAGE_SIZE, PageAllocationError, PageRange};
    use crate::memory::{
        MemoryRegion, MemoryRegionKind, MemoryRegionSource, RegionTable, UefiMemoryType,
    };

    fn table(regions: &[(u64, u64)]) -> RegionTable {
        let mut table = RegionTable::new();
        for &(start, byte_len) in regions {
            table
                .push(MemoryRegion {
                    start,
                    byte_len,
                    kind: MemoryRegionKind::Usable,
                    source: MemoryRegionSource::Uefi(UefiMemoryType::Conventional),
                    attributes: 0,
                })
                .unwrap();
        }
        table
    }

    #[test]
    fn reference_geometry_is_bounded_and_maps_within_table_pool() {
        let geometry = GpuDisplayBuffer::checked_geometry(1280, 800, 1280).unwrap();
        assert_eq!(geometry.visible_byte_len, 4_096_000);
        let pages = GpuDisplayBuffer::required_pages(1280, 800, 1280).unwrap();
        assert_eq!(pages, 1_000);
        assert!(pages <= GPU_DISPLAY_MAX_PAGES);
        assert!(
            GpuDisplayBuffer::translation_table_page_upper_bound(pages).unwrap()
                <= super::GPU_DISPLAY_MAX_TABLE_PAGES_BOUND
        );
    }

    #[test]
    fn allocation_owns_and_releases_exact_page_range() {
        let mut allocator =
            EarlyPhysicalPageAllocator::from_memory_regions(&table(&[(0x1000, 1_024 * PAGE_SIZE)]))
                .unwrap();
        let mut buffer = GpuDisplayBuffer::allocate(&mut allocator, 1280, 800, 1280).unwrap();
        assert_eq!(buffer.backing_address(), Some(0x1000));
        assert_eq!(buffer.page_count(), 1_000);
        assert_eq!(
            buffer.backing_byte_len(),
            u32::try_from(1_000 * PAGE_SIZE).unwrap()
        );
        assert_eq!(allocator.free_pages(), 24);
        buffer.release(&mut allocator).unwrap();
        assert!(buffer.is_released());
        assert_eq!(buffer.backing_address(), None);
        assert_eq!(allocator.free_pages(), 1_024);
        assert_eq!(
            buffer.release(&mut allocator),
            Err(GpuDisplayBufferError::AlreadyReleased)
        );
    }

    #[test]
    fn invalid_geometry_and_capacity_fail_without_mutation() {
        assert_eq!(
            GpuDisplayBuffer::checked_geometry(0, 1, 1),
            Err(GpuDisplayBufferError::ZeroWidth)
        );
        assert_eq!(
            GpuDisplayBuffer::checked_geometry(1, 0, 1),
            Err(GpuDisplayBufferError::ZeroHeight)
        );
        assert_eq!(
            GpuDisplayBuffer::checked_geometry(1, 1, 0),
            Err(GpuDisplayBufferError::ZeroStride)
        );
        assert_eq!(
            GpuDisplayBuffer::checked_geometry(10, 1, 9),
            Err(GpuDisplayBufferError::StrideTooSmall)
        );
        assert_eq!(
            GpuDisplayBuffer::required_pages(u32::MAX, u32::MAX, u32::MAX),
            Err(GpuDisplayBufferError::GeometryOverflow)
        );
        assert!(matches!(
            GpuDisplayBuffer::required_pages(2_048, 2_048, 2_048),
            Err(GpuDisplayBufferError::CapacityExceeded { .. })
        ));

        let mut allocator =
            EarlyPhysicalPageAllocator::from_memory_regions(&table(&[(0x1000, 4 * PAGE_SIZE)]))
                .unwrap();
        let before = allocator.free_pages();
        assert!(matches!(
            GpuDisplayBuffer::allocate(&mut allocator, 1280, 800, 1280),
            Err(GpuDisplayBufferError::Allocation(
                PageAllocationError::OutOfMemory
            ))
        ));
        assert_eq!(allocator.free_pages(), before);
    }

    #[test]
    fn supplied_range_must_cover_visible_frame_and_policy_cap() {
        let too_small = PageRange::new(0x1000, 1).unwrap();
        assert_eq!(
            GpuDisplayBuffer::from_page_range(1280, 800, 1280, too_small),
            Err(GpuDisplayBufferError::BackingTooSmall {
                required_bytes: 4_096_000,
                available_bytes: PAGE_SIZE,
            })
        );
        let oversized = PageRange::new(0x1000, GPU_DISPLAY_MAX_PAGES + 1).unwrap();
        assert_eq!(
            GpuDisplayBuffer::from_page_range(1, 1, 1, oversized),
            Err(GpuDisplayBufferError::CapacityExceeded {
                requested_pages: GPU_DISPLAY_MAX_PAGES + 1,
                max_pages: GPU_DISPLAY_MAX_PAGES,
            })
        );
        assert_eq!(
            GpuDisplayBuffer::from_page_range(1, 1, 1, PageRange::new(0, 1).unwrap()),
            Err(GpuDisplayBufferError::NullBackingAddress)
        );
    }

    #[test]
    fn geometry_type_remains_small_and_constructible() {
        let geometry = DisplayGeometry {
            width: 800,
            height: 600,
            stride: 800,
            visible_byte_len: 1_920_000,
        };
        assert_eq!(geometry.width, 800);
        assert_eq!(geometry.height, 600);
    }
}
