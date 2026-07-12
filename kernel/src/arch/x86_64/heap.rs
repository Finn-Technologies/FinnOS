//! x86-64 mapping and backing storage for the bounded early kernel heap.
#![allow(unsafe_code)]

use crate::memory::{EarlyPhysicalPageAllocator, PAGE_SIZE, PageAllocationError, PageRange};

use super::paging::{
    ActiveAddressSpace, MappingPermissions, PagingError, PhysicalFrame, VirtualPage,
};

/// Base of the reserved virtual heap region, including the lower guard page.
pub const KERNEL_HEAP_REGION_BASE: u64 = 0x0000_2000_0000_0000;
/// Heap size excluding its two guard pages.
pub const KERNEL_HEAP_SIZE: usize = 1024 * 1024;
/// Number of 4 KiB heap backing pages.
pub const KERNEL_HEAP_PAGE_COUNT: usize = KERNEL_HEAP_SIZE / 4096;
/// First mapped heap address.
pub const KERNEL_HEAP_START: u64 = KERNEL_HEAP_REGION_BASE + PAGE_SIZE;
/// Exclusive end of the mapped heap.
pub const KERNEL_HEAP_END: u64 = KERNEL_HEAP_START + KERNEL_HEAP_SIZE as u64;
/// Lower unmapped heap guard page.
pub const KERNEL_HEAP_GUARD_LOW: u64 = KERNEL_HEAP_REGION_BASE;
/// Upper unmapped heap guard page.
pub const KERNEL_HEAP_GUARD_HIGH: u64 = KERNEL_HEAP_END;

/// Errors returned while constructing the heap's virtual mapping.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HeapMappingError {
    /// A physical page allocation failed.
    PhysicalAllocation(PageAllocationError),
    /// A page-table operation failed.
    Paging(PagingError),
    /// The fixed virtual layout is invalid.
    InvalidHeapRange,
    /// A heap guard page was present.
    GuardPageMapped,
    /// A heap page had an unexpected translation or permission.
    MappingValidationFailed,
    /// Mapping rollback could not restore the allocator and page tables.
    RollbackFailed,
}

impl From<PageAllocationError> for HeapMappingError {
    fn from(error: PageAllocationError) -> Self {
        Self::PhysicalAllocation(error)
    }
}

impl From<PagingError> for HeapMappingError {
    fn from(error: PagingError) -> Self {
        Self::Paging(error)
    }
}

/// Ownership and diagnostics for the heap's 256 physical backing pages.
pub struct KernelHeapMapping {
    physical_pages: [u64; KERNEL_HEAP_PAGE_COUNT],
    mapped_count: usize,
}

impl KernelHeapMapping {
    /// Construct an empty mapping owner.
    #[must_use]
    pub const fn empty() -> Self {
        Self {
            physical_pages: [0; KERNEL_HEAP_PAGE_COUNT],
            mapped_count: 0,
        }
    }

    /// Initialize, map, validate, and zero the fixed heap.
    ///
    /// # Errors
    ///
    /// Returns an allocation, paging, validation, guard, or rollback error.
    pub fn initialize(
        &mut self,
        address_space: &mut ActiveAddressSpace,
        allocator: &mut EarlyPhysicalPageAllocator,
    ) -> Result<(), HeapMappingError> {
        if !KERNEL_HEAP_START.is_multiple_of(PAGE_SIZE)
            || !KERNEL_HEAP_END.is_multiple_of(PAGE_SIZE)
            || KERNEL_HEAP_START >= KERNEL_HEAP_END
            || !super::paging::is_canonical(KERNEL_HEAP_GUARD_HIGH)
        {
            return Err(HeapMappingError::InvalidHeapRange);
        }
        if address_space.translate(KERNEL_HEAP_GUARD_LOW)?.is_some()
            || address_space.translate(KERNEL_HEAP_GUARD_HIGH)?.is_some()
        {
            return Err(HeapMappingError::GuardPageMapped);
        }
        for index in 0..KERNEL_HEAP_PAGE_COUNT {
            let virtual_address = KERNEL_HEAP_START
                .checked_add(
                    (index as u64)
                        .checked_mul(PAGE_SIZE)
                        .ok_or(HeapMappingError::InvalidHeapRange)?,
                )
                .ok_or(HeapMappingError::InvalidHeapRange)?;
            let virtual_page =
                VirtualPage::new(virtual_address).map_err(HeapMappingError::Paging)?;
            let page = match allocator.allocate_page() {
                Ok(page) => page,
                Err(error) => {
                    return match self.rollback(address_space, allocator) {
                        Ok(()) => Err(error.into()),
                        Err(rollback_error) => Err(rollback_error),
                    };
                }
            };
            self.physical_pages[index] = page.start_address();
            let frame = match PhysicalFrame::new(page.start_address(), address_space.width()) {
                Ok(frame) => frame,
                Err(error) => {
                    let cleanup = self.release_unmapped_page(index, allocator).is_ok();
                    let rollback = self.rollback(address_space, allocator).is_ok();
                    return if cleanup && rollback {
                        Err(error.into())
                    } else {
                        Err(HeapMappingError::RollbackFailed)
                    };
                }
            };
            let outcome = match address_space.map_page(
                virtual_page,
                frame,
                MappingPermissions::kernel_rw_nx(),
            ) {
                Ok(outcome) => outcome,
                Err(error) => {
                    let cleanup = self.release_unmapped_page(index, allocator).is_ok();
                    let rollback = self.rollback(address_space, allocator).is_ok();
                    return if cleanup && rollback {
                        Err(error.into())
                    } else {
                        Err(HeapMappingError::RollbackFailed)
                    };
                }
            };
            if outcome != super::paging::MapOutcome::Created {
                let cleanup = self.release_unmapped_page(index, allocator).is_ok();
                let rollback = self.rollback(address_space, allocator).is_ok();
                return if cleanup && rollback {
                    Err(HeapMappingError::MappingValidationFailed)
                } else {
                    Err(HeapMappingError::RollbackFailed)
                };
            }
            self.mapped_count = match self.mapped_count.checked_add(1) {
                Some(count) => count,
                None => {
                    let rollback = self.rollback(address_space, allocator);
                    return match rollback {
                        Ok(()) => Err(HeapMappingError::MappingValidationFailed),
                        Err(error) => Err(error),
                    };
                }
            };
        }
        if self.validate(address_space).is_err() {
            self.rollback(address_space, allocator)?;
            return Err(HeapMappingError::MappingValidationFailed);
        }
        // SAFETY: Every byte in this complete virtual range was just validated as mapped,
        // writable, supervisor-only, and NX. The range is exclusively reserved for this heap.
        unsafe {
            core::ptr::write_bytes(KERNEL_HEAP_START as *mut u8, 0, KERNEL_HEAP_SIZE);
        }
        Ok(())
    }

    /// Validate heap translations and both unmapped guard pages.
    ///
    /// # Errors
    ///
    /// Returns an error if a guard is mapped or a heap page has unexpected translation or
    /// permissions.
    pub fn validate(&self, address_space: &ActiveAddressSpace) -> Result<(), HeapMappingError> {
        if address_space.translate(KERNEL_HEAP_GUARD_LOW)?.is_some()
            || address_space.translate(KERNEL_HEAP_GUARD_HIGH)?.is_some()
        {
            return Err(HeapMappingError::GuardPageMapped);
        }
        for index in 0..self.mapped_count {
            let virtual_address = KERNEL_HEAP_START
                .checked_add((index as u64) * PAGE_SIZE)
                .ok_or(HeapMappingError::InvalidHeapRange)?;
            let translation = address_space
                .translate(virtual_address)?
                .ok_or(HeapMappingError::MappingValidationFailed)?;
            if translation.physical_address & !(PAGE_SIZE - 1) != self.physical_pages[index]
                || !translation.effective_writable
                || translation.effective_executable
                || translation.effective_user
            {
                return Err(HeapMappingError::MappingValidationFailed);
            }
        }
        Ok(())
    }

    /// Return the number of successfully mapped backing pages.
    #[must_use]
    pub const fn mapped_count(&self) -> usize {
        self.mapped_count
    }

    fn release_unmapped_page(
        &mut self,
        index: usize,
        allocator: &mut EarlyPhysicalPageAllocator,
    ) -> Result<(), HeapMappingError> {
        let address = self.physical_pages[index];
        let range = PageRange::new(address, 1).map_err(HeapMappingError::PhysicalAllocation)?;
        allocator
            .deallocate(range)
            .map_err(HeapMappingError::PhysicalAllocation)?;
        self.physical_pages[index] = 0;
        Ok(())
    }

    fn rollback(
        &mut self,
        address_space: &mut ActiveAddressSpace,
        allocator: &mut EarlyPhysicalPageAllocator,
    ) -> Result<(), HeapMappingError> {
        let mut failed = false;
        for index in (0..self.mapped_count).rev() {
            let offset = match (index as u64).checked_mul(PAGE_SIZE) {
                Some(offset) => offset,
                None => {
                    failed = true;
                    continue;
                }
            };
            let virtual_address = match KERNEL_HEAP_START.checked_add(offset) {
                Some(address) => address,
                None => {
                    failed = true;
                    continue;
                }
            };
            let page = match VirtualPage::new(virtual_address) {
                Ok(page) => page,
                Err(_) => {
                    failed = true;
                    continue;
                }
            };
            match address_space.unmap_page(page) {
                Ok(frame) if frame.address() == self.physical_pages[index] => {
                    if self.release_unmapped_page(index, allocator).is_err() {
                        failed = true;
                    }
                }
                Ok(_) | Err(_) => failed = true,
            }
        }
        self.mapped_count = 0;
        if self.physical_pages.iter().any(|address| *address != 0) {
            failed = true;
        }
        if allocator.check_invariants().is_err() {
            failed = true;
        }
        if address_space
            .translate(KERNEL_HEAP_GUARD_LOW)
            .map_or(true, |translation| translation.is_some())
            || address_space
                .translate(KERNEL_HEAP_GUARD_HIGH)
                .map_or(true, |translation| translation.is_some())
        {
            failed = true;
        }
        if failed {
            Err(HeapMappingError::RollbackFailed)
        } else {
            Ok(())
        }
    }
}
