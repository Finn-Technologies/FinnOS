//! Virtual layout and lifecycle for guarded cooperative task stacks on ARM64.

#![allow(clippy::manual_let_else)]
#![allow(clippy::missing_const_for_fn)]
#![allow(clippy::missing_errors_doc)]
#![allow(clippy::must_use_candidate)]
#![allow(unsafe_code)]

#[cfg(target_os = "none")]
use crate::arch::aarch64::paging::{ActiveAddressSpace, MemoryType, Permissions};
use crate::arch::aarch64::paging::{PAGE_SIZE, PagingError};
#[cfg(target_os = "none")]
use crate::memory::EarlyPhysicalPageAllocator;
use crate::memory::PageAllocationError;
#[cfg(target_os = "none")]
use crate::memory::PageRange;

/// Base of the virtual region reserved for non-bootstrap task stacks.
pub const TASK_STACK_REGION_BASE: u64 = 0x0000_2800_0000_0000;
/// Usable bytes in every non-bootstrap task stack (64 KiB).
pub const TASK_STACK_SIZE: usize = 64 * 1024;
/// Number of mapped pages in every non-bootstrap task stack.
pub const TASK_STACK_PAGE_COUNT: usize = 16;
/// Virtual distance between consecutive task-stack slots (128 KiB).
pub const TASK_STACK_SLOT_STRIDE: u64 = 128 * 1024;

/// A validated virtual layout for one guarded task stack.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaskStackLayout {
    /// First unmapped guard page.
    pub lower_guard: u64,
    /// First mapped writable stack page.
    pub stack_start: u64,
    /// Exclusive end of the mapped stack.
    pub stack_end: u64,
    /// First address of the unmapped upper guard page.
    pub upper_guard: u64,
    /// Exclusive end of this virtual slot.
    pub slot_end: u64,
}

/// Errors from task-stack layout arithmetic.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskStackLayoutError {
    /// Bootstrap has no separately mapped task-stack slot.
    BootstrapHasNoMappedStack,
    /// The slot is outside the task table.
    InvalidSlot,
    /// Arithmetic overflow.
    AddressOverflow,
}

impl TaskStackLayout {
    /// Computes the layout for a given slot.
    pub const fn for_slot(slot: usize) -> Result<Self, TaskStackLayoutError> {
        if slot == 0 {
            return Err(TaskStackLayoutError::BootstrapHasNoMappedStack);
        }
        if slot >= crate::task::MAX_TASKS {
            return Err(TaskStackLayoutError::InvalidSlot);
        }
        let offset = match (slot as u64).checked_mul(TASK_STACK_SLOT_STRIDE) {
            Some(v) => v,
            None => return Err(TaskStackLayoutError::AddressOverflow),
        };
        let lower_guard = match TASK_STACK_REGION_BASE.checked_add(offset) {
            Some(v) => v,
            None => return Err(TaskStackLayoutError::AddressOverflow),
        };
        let stack_start = match lower_guard.checked_add(PAGE_SIZE) {
            Some(v) => v,
            None => return Err(TaskStackLayoutError::AddressOverflow),
        };
        let stack_end = match stack_start.checked_add((TASK_STACK_PAGE_COUNT as u64) * PAGE_SIZE) {
            Some(v) => v,
            None => return Err(TaskStackLayoutError::AddressOverflow),
        };
        let upper_guard = stack_end;
        let slot_end = match lower_guard.checked_add(TASK_STACK_SLOT_STRIDE) {
            Some(v) => v,
            None => return Err(TaskStackLayoutError::AddressOverflow),
        };

        Ok(Self {
            lower_guard,
            stack_start,
            stack_end,
            upper_guard,
            slot_end,
        })
    }
}

/// Fixed ownership metadata for one mapped task stack.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskStackMapping {
    slot: u8,
    virtual_start: u64,
    virtual_end: u64,
    physical_pages: [u64; TASK_STACK_PAGE_COUNT],
    owned_count: usize,
    mapped_count: usize,
}

impl TaskStackMapping {
    /// Construct empty mapping metadata for `slot`.
    pub fn empty(slot: usize) -> Result<Self, TaskStackLayoutError> {
        let layout = TaskStackLayout::for_slot(slot)?;
        Ok(Self {
            slot: u8::try_from(slot).map_err(|_| TaskStackLayoutError::InvalidSlot)?,
            virtual_start: layout.stack_start,
            virtual_end: layout.stack_end,
            physical_pages: [0; TASK_STACK_PAGE_COUNT],
            owned_count: 0,
            mapped_count: 0,
        })
    }

    /// Task slot owning this mapping.
    #[must_use]
    pub const fn slot(&self) -> usize {
        self.slot as usize
    }

    /// First virtual address of mapped stack.
    #[must_use]
    pub const fn virtual_start(&self) -> u64 {
        self.virtual_start
    }

    /// Exclusive end of mapped stack.
    #[must_use]
    pub const fn virtual_end(&self) -> u64 {
        self.virtual_end
    }

    /// Whether this mapping contains an address.
    #[must_use]
    pub const fn contains(&self, address: u64) -> bool {
        self.virtual_start <= address && address < self.virtual_end
    }

    /// Number of physical frames owned.
    #[must_use]
    pub const fn owned_count(&self) -> usize {
        self.owned_count
    }

    /// Whether no physical frames are owned.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.owned_count == 0
    }
}

/// Failures from live task-stack allocation or reclamation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskStackError {
    /// Layout error.
    Layout(TaskStackLayoutError),
    /// Physical allocation failure.
    Physical(PageAllocationError),
    /// Paging failure.
    Paging(PagingError),
    /// Inconsistent mapping state.
    CorruptMapping,
    /// Stack already mapped.
    AlreadyMapped,
}

impl From<TaskStackLayoutError> for TaskStackError {
    fn from(error: TaskStackLayoutError) -> Self {
        Self::Layout(error)
    }
}

impl From<PageAllocationError> for TaskStackError {
    fn from(error: PageAllocationError) -> Self {
        Self::Physical(error)
    }
}

impl From<PagingError> for TaskStackError {
    fn from(error: PagingError) -> Self {
        Self::Paging(error)
    }
}

/// Allocate and map a task stack.
#[cfg(target_os = "none")]
pub fn map_task_stack(
    mapping: &mut TaskStackMapping,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(), TaskStackError> {
    if mapping.owned_count != 0 || mapping.mapped_count != 0 {
        return Err(TaskStackError::AlreadyMapped);
    }

    // Allocate physical pages
    for i in 0..TASK_STACK_PAGE_COUNT {
        match allocator.allocate_page() {
            Ok(res) => {
                mapping.physical_pages[i] = res.start_address();
                mapping.owned_count += 1;
            }
            Err(e) => {
                reclaim_task_stack(mapping, address_space, allocator).ok();
                return Err(TaskStackError::Physical(e));
            }
        }
    }

    // Map each page
    for i in 0..TASK_STACK_PAGE_COUNT {
        let va = mapping.virtual_start + (i as u64) * PAGE_SIZE;
        let pa = mapping.physical_pages[i];
        if let Err(e) = address_space.map_page(
            va,
            pa,
            Permissions::ReadWriteNoExecute,
            MemoryType::NormalWriteBack,
        ) {
            reclaim_task_stack(mapping, address_space, allocator).ok();
            return Err(TaskStackError::Paging(e));
        }
        mapping.mapped_count += 1;

        // Zero page content
        unsafe {
            core::ptr::write_bytes(va as *mut u8, 0, PAGE_SIZE as usize);
        }
    }

    Ok(())
}

/// Unmap and deallocate a task stack.
#[cfg(target_os = "none")]
pub fn reclaim_task_stack(
    mapping: &mut TaskStackMapping,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(), TaskStackError> {
    let mut err = None;

    // Unmap any mapped pages
    while mapping.mapped_count > 0 {
        let idx = mapping.mapped_count - 1;
        let va = mapping.virtual_start + (idx as u64) * PAGE_SIZE;
        if let Err(e) = address_space.unmap_page(va) {
            if err.is_none() {
                err = Some(TaskStackError::Paging(e));
            }
        }
        mapping.mapped_count -= 1;
    }

    // Deallocate physical pages
    while mapping.owned_count > 0 {
        let idx = mapping.owned_count - 1;
        let pa = mapping.physical_pages[idx];
        if let Ok(range) = PageRange::new(pa, 1) {
            if let Err(e) = allocator.deallocate(range) {
                if err.is_none() {
                    err = Some(TaskStackError::Physical(e));
                }
            }
        }
        mapping.physical_pages[idx] = 0;
        mapping.owned_count -= 1;
    }

    if let Some(e) = err { Err(e) } else { Ok(()) }
}

/// Restore / clean up partially initialized stack.
#[cfg(target_os = "none")]
pub fn restore_task_stack(
    mapping: &mut TaskStackMapping,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(), TaskStackError> {
    reclaim_task_stack(mapping, address_space, allocator)
}

/// Validate that a mapped stack exists in the address space.
#[cfg(target_os = "none")]
pub fn validate_task_stack(
    mapping: &TaskStackMapping,
    address_space: &ActiveAddressSpace,
) -> Result<(), TaskStackError> {
    if mapping.mapped_count != TASK_STACK_PAGE_COUNT {
        return Err(TaskStackError::CorruptMapping);
    }
    for i in 0..TASK_STACK_PAGE_COUNT {
        let va = mapping.virtual_start + (i as u64) * PAGE_SIZE;
        let trans = address_space.translate(va)?;
        if trans.physical_address != mapping.physical_pages[i] {
            return Err(TaskStackError::CorruptMapping);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_calculations_match_slot_expectations() {
        assert_eq!(
            TaskStackLayout::for_slot(0),
            Err(TaskStackLayoutError::BootstrapHasNoMappedStack)
        );

        let s1 = TaskStackLayout::for_slot(1).unwrap();
        assert_eq!(
            s1.lower_guard,
            TASK_STACK_REGION_BASE + TASK_STACK_SLOT_STRIDE
        );
        assert_eq!(s1.stack_start, s1.lower_guard + PAGE_SIZE);
        assert_eq!(
            s1.stack_end,
            s1.stack_start + (TASK_STACK_PAGE_COUNT as u64) * PAGE_SIZE
        );
        assert_eq!(s1.stack_end - s1.stack_start, TASK_STACK_SIZE as u64);
        assert_eq!(s1.upper_guard, s1.stack_end);
        assert_eq!(s1.slot_end, s1.lower_guard + TASK_STACK_SLOT_STRIDE);
    }
}
