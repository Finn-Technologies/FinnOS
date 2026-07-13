//! Virtual layout calculations for guarded cooperative-task stacks.

use crate::arch::x86_64::paging::{
    ActiveAddressSpace, MappingPermissions, PagingError, PhysicalFrame, VirtualPage, is_canonical,
};
use crate::memory::{EarlyPhysicalPageAllocator, PAGE_SIZE, PageAllocationError, PageRange};

/// Base of the virtual region reserved for non-bootstrap task stacks.
pub const TASK_STACK_REGION_BASE: u64 = 0x0000_2800_0000_0000;
/// Usable bytes in every non-bootstrap task stack.
pub const TASK_STACK_SIZE: usize = 64 * 1024;
/// Number of mapped pages in every non-bootstrap task stack.
pub const TASK_STACK_PAGE_COUNT: usize = 16;
/// Virtual distance between consecutive task-stack slots.
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
    /// Checked virtual-address arithmetic overflowed.
    AddressOverflow,
    /// A calculated address is not canonical or page aligned.
    InvalidAddress,
}

/// Fixed, heap-free ownership metadata for one mapped task stack.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaskStackMapping {
    slot: u8,
    virtual_start: u64,
    virtual_end: u64,
    physical_pages: [u64; TASK_STACK_PAGE_COUNT],
    mapped_count: usize,
}

impl TaskStackMapping {
    /// Makes empty ownership metadata for `slot`.
    ///
    /// # Errors
    ///
    /// Returns a layout error when `slot` cannot own a mapped task stack.
    pub fn empty(slot: usize) -> Result<Self, TaskStackLayoutError> {
        let layout = TaskStackLayout::for_slot(slot)?;
        Ok(Self {
            slot: u8::try_from(slot).map_err(|_| TaskStackLayoutError::InvalidSlot)?,
            virtual_start: layout.stack_start,
            virtual_end: layout.stack_end,
            physical_pages: [0; TASK_STACK_PAGE_COUNT],
            mapped_count: 0,
        })
    }

    /// Returns the task-table slot that owns the mapping.
    #[must_use]
    pub const fn slot(self) -> usize {
        self.slot as usize
    }
    /// Returns the first mapped virtual stack address.
    #[must_use]
    pub const fn virtual_start(self) -> u64 {
        self.virtual_start
    }
    /// Returns the exclusive mapped virtual stack end.
    #[must_use]
    pub const fn virtual_end(self) -> u64 {
        self.virtual_end
    }
    /// Returns the number of currently mapped leaves.
    #[must_use]
    pub const fn mapped_count(self) -> usize {
        self.mapped_count
    }
    /// Returns whether no physical frame is owned.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.mapped_count == 0
    }
    /// Returns a recorded physical frame for a mapped page.
    #[must_use]
    pub fn physical_page(self, index: usize) -> Option<u64> {
        (index < self.mapped_count).then(|| self.physical_pages[index])
    }
}

/// Failures from live task-stack mapping or reclamation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskStackError {
    /// Layout arithmetic or slot validation failed.
    Layout(TaskStackLayoutError),
    /// The early physical allocator failed.
    Physical(PageAllocationError),
    /// The active page-table manager failed.
    Paging(PagingError),
    /// Metadata described a partial or contradictory mapping.
    CorruptMapping,
    /// Rollback could not restore all resources.
    RollbackFailed,
    /// An existing mapping was supplied for a new stack.
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

/// Allocates, maps, zeros, and validates all leaves for one task stack.
///
/// Mapping is transactional: on failure this function attempts to restore the
/// mapping and allocator baselines before returning an error.
///
/// # Errors
///
/// Returns a structured error if allocation, mapping, zeroing validation, or
/// rollback fails.
pub fn map_task_stack(
    mapping: &mut TaskStackMapping,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(), TaskStackError> {
    if !mapping.is_empty() {
        return Err(TaskStackError::AlreadyMapped);
    }
    let layout = TaskStackLayout::for_slot(mapping.slot())?;
    let free_baseline = allocator.free_pages();
    let mapped_baseline = address_space.mapped_pages();
    for index in 0..TASK_STACK_PAGE_COUNT {
        let page = match allocator.allocate_page() {
            Ok(page) => page,
            Err(error) => {
                return rollback(
                    mapping,
                    address_space,
                    allocator,
                    free_baseline,
                    mapped_baseline,
                    TaskStackError::Physical(error),
                );
            }
        };
        let Some(virtual_address) = layout.stack_start.checked_add((index as u64) * PAGE_SIZE)
        else {
            let _ = allocator.deallocate(
                PageRange::new(page.start_address(), 1).map_err(TaskStackError::Physical)?,
            );
            return rollback(
                mapping,
                address_space,
                allocator,
                free_baseline,
                mapped_baseline,
                TaskStackError::Layout(TaskStackLayoutError::AddressOverflow),
            );
        };
        if let Err(error) = address_space.map_page(
            VirtualPage::new(virtual_address)?,
            PhysicalFrame::new(page.start_address(), address_space.width())?,
            MappingPermissions::kernel_rw_nx(),
        ) {
            let _ = allocator.deallocate(
                PageRange::new(page.start_address(), 1).map_err(TaskStackError::Physical)?,
            );
            return rollback(
                mapping,
                address_space,
                allocator,
                free_baseline,
                mapped_baseline,
                TaskStackError::Paging(error),
            );
        }
        mapping.physical_pages[index] = page.start_address();
        mapping.mapped_count += 1;
    }
    // SAFETY: Every byte in [stack_start, stack_end) has just been mapped writable,
    // supervisor-only, and exclusively belongs to this task-stack mapping.
    #[allow(unsafe_code)]
    // SAFETY: documented directly above; this is the sole raw write boundary.
    unsafe {
        core::ptr::write_bytes(layout.stack_start as *mut u8, 0, TASK_STACK_SIZE);
    }
    if validate_task_stack(*mapping, address_space).is_err() {
        return rollback(
            mapping,
            address_space,
            allocator,
            free_baseline,
            mapped_baseline,
            TaskStackError::CorruptMapping,
        );
    }
    Ok(())
}

/// Validates mappings, permissions, guards, and padding for a task stack.
///
/// # Errors
///
/// Returns an error when metadata and active page tables disagree.
pub fn validate_task_stack(
    mapping: TaskStackMapping,
    address_space: &ActiveAddressSpace,
) -> Result<(), TaskStackError> {
    if mapping.mapped_count != TASK_STACK_PAGE_COUNT {
        return Err(TaskStackError::CorruptMapping);
    }
    let layout = TaskStackLayout::for_slot(mapping.slot())?;
    for index in 0..TASK_STACK_PAGE_COUNT {
        let address = layout.stack_start + (index as u64) * PAGE_SIZE;
        let translation = address_space
            .translate(address)?
            .ok_or(TaskStackError::CorruptMapping)?;
        if translation.physical_address & !(PAGE_SIZE - 1) != mapping.physical_pages[index]
            || !translation.effective_writable
            || translation.effective_executable
            || translation.effective_user
            || translation.cache_disable
            || translation.write_through
        {
            return Err(TaskStackError::CorruptMapping);
        }
    }
    if address_space.translate(layout.lower_guard)?.is_some()
        || address_space.translate(layout.upper_guard)?.is_some()
        || address_space
            .translate(layout.upper_guard + PAGE_SIZE)?
            .is_some()
    {
        return Err(TaskStackError::CorruptMapping);
    }
    Ok(())
}

/// Unmaps and returns every physical leaf owned by an exited task stack.
///
/// # Errors
///
/// Returns an error before clearing metadata if validation, unmapping, or
/// physical-frame return fails.
pub fn reclaim_task_stack(
    mapping: &mut TaskStackMapping,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(), TaskStackError> {
    validate_task_stack(*mapping, address_space)?;
    let layout = TaskStackLayout::for_slot(mapping.slot())?;
    for index in 0..TASK_STACK_PAGE_COUNT {
        let address = layout.stack_start + (index as u64) * PAGE_SIZE;
        let returned = address_space.unmap_page(VirtualPage::new(address)?)?;
        if returned.address() != mapping.physical_pages[index] {
            return Err(TaskStackError::CorruptMapping);
        }
        allocator.deallocate(PageRange::new(returned.address(), 1)?)?;
    }
    if address_space.translate(layout.lower_guard)?.is_some()
        || address_space.translate(layout.upper_guard)?.is_some()
    {
        return Err(TaskStackError::CorruptMapping);
    }
    *mapping = TaskStackMapping::empty(mapping.slot())?;
    allocator.check_invariants()?;
    Ok(())
}

fn rollback(
    mapping: &mut TaskStackMapping,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
    free_baseline: u64,
    mapped_baseline: u64,
    original: TaskStackError,
) -> Result<(), TaskStackError> {
    let mut failed = false;
    for index in (0..mapping.mapped_count).rev() {
        let address = mapping.virtual_start + (index as u64) * PAGE_SIZE;
        match address_space.unmap_page(VirtualPage::new(address).map_err(TaskStackError::Paging)?) {
            Ok(frame) if frame.address() == mapping.physical_pages[index] => {
                if allocator
                    .deallocate(
                        PageRange::new(frame.address(), 1).map_err(TaskStackError::Physical)?,
                    )
                    .is_err()
                {
                    failed = true;
                }
            }
            _ => failed = true,
        }
    }
    *mapping = TaskStackMapping::empty(mapping.slot())?;
    if failed
        || allocator.free_pages() != free_baseline
        || address_space.mapped_pages() != mapped_baseline
        || allocator.check_invariants().is_err()
    {
        return Err(TaskStackError::RollbackFailed);
    }
    Err(original)
}

impl TaskStackLayout {
    /// Calculates a non-bootstrap task's distinct guarded stack slot.
    ///
    /// # Errors
    ///
    /// Returns an error for bootstrap, out-of-range slots, overflow, or an
    /// invalid calculated virtual address.
    pub fn for_slot(slot: usize) -> Result<Self, TaskStackLayoutError> {
        if slot == 0 {
            return Err(TaskStackLayoutError::BootstrapHasNoMappedStack);
        }
        if slot >= crate::task::MAX_TASKS {
            return Err(TaskStackLayoutError::InvalidSlot);
        }
        let offset = (slot as u64)
            .checked_mul(TASK_STACK_SLOT_STRIDE)
            .ok_or(TaskStackLayoutError::AddressOverflow)?;
        let lower_guard = TASK_STACK_REGION_BASE
            .checked_add(offset)
            .ok_or(TaskStackLayoutError::AddressOverflow)?;
        let stack_start = lower_guard
            .checked_add(PAGE_SIZE)
            .ok_or(TaskStackLayoutError::AddressOverflow)?;
        let stack_end = stack_start
            .checked_add(TASK_STACK_SIZE as u64)
            .ok_or(TaskStackLayoutError::AddressOverflow)?;
        let upper_guard = stack_end;
        let slot_end = lower_guard
            .checked_add(TASK_STACK_SLOT_STRIDE)
            .ok_or(TaskStackLayoutError::AddressOverflow)?;
        if ![lower_guard, stack_start, stack_end, upper_guard, slot_end]
            .iter()
            .all(|address| is_canonical(*address) && address.is_multiple_of(PAGE_SIZE))
            || upper_guard
                .checked_add(PAGE_SIZE)
                .ok_or(TaskStackLayoutError::AddressOverflow)?
                > slot_end
        {
            return Err(TaskStackLayoutError::InvalidAddress);
        }
        Ok(Self {
            lower_guard,
            stack_start,
            stack_end,
            upper_guard,
            slot_end,
        })
    }

    /// Returns whether an address is inside the usable mapped stack range.
    #[must_use]
    pub const fn contains(self, address: u64) -> bool {
        self.stack_start <= address && address < self.stack_end
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn slots_are_guarded_aligned_and_disjoint() {
        let first = TaskStackLayout::for_slot(1).unwrap();
        assert_eq!(first.lower_guard, 0x0000_2800_0002_0000);
        assert_eq!(first.stack_end - first.stack_start, 64 * 1024);
        for slot in 1..crate::task::MAX_TASKS {
            let layout = TaskStackLayout::for_slot(slot).unwrap();
            assert!(!layout.contains(layout.lower_guard));
            assert!(!layout.contains(layout.upper_guard));
            if slot > 1 {
                assert!(
                    TaskStackLayout::for_slot(slot - 1).unwrap().slot_end <= layout.lower_guard
                );
            }
        }
    }
    #[test]
    fn bootstrap_and_invalid_slots_are_rejected() {
        assert_eq!(
            TaskStackLayout::for_slot(0),
            Err(TaskStackLayoutError::BootstrapHasNoMappedStack)
        );
        assert_eq!(
            TaskStackLayout::for_slot(crate::task::MAX_TASKS),
            Err(TaskStackLayoutError::InvalidSlot)
        );
    }
}
