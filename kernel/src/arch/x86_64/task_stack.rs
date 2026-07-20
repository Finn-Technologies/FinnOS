//! Virtual layout calculations for guarded cooperative-task stacks.
#![allow(unsafe_code)]

use core::cell::UnsafeCell;

use crate::arch::x86_64::paging::{
    ActiveAddressSpace, MapOutcome, MappingPermissions, PagingError, PhysicalFrame, VirtualPage,
    is_canonical,
};
use crate::memory::{EarlyPhysicalPageAllocator, PAGE_SIZE, PageAllocationError, PageRange};

struct AllocatorTransactionCell(UnsafeCell<EarlyPhysicalPageAllocator>);
// SAFETY: task-stack mutation is BSP-only and scheduler entry is non-reentrant.
unsafe impl Sync for AllocatorTransactionCell {}
static ALLOCATOR_TRANSACTION: AllocatorTransactionCell = AllocatorTransactionCell(UnsafeCell::new(
    EarlyPhysicalPageAllocator::empty_transaction(),
));

fn allocator_transaction(
    allocator: &EarlyPhysicalPageAllocator,
) -> &'static mut EarlyPhysicalPageAllocator {
    // SAFETY: all callers run on the BSP under the scheduler's non-reentrant
    // mutation contract, so only one transaction can use this scratch state.
    let prospective = unsafe { &mut *ALLOCATOR_TRANSACTION.0.get() };
    prospective.copy_state_from(allocator);
    prospective
}

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
            owned_count: 0,
            mapped_count: 0,
        })
    }

    /// Returns the task-table slot that owns the mapping.
    #[must_use]
    pub const fn slot(&self) -> usize {
        self.slot as usize
    }
    /// Returns the first mapped virtual stack address.
    #[must_use]
    pub const fn virtual_start(&self) -> u64 {
        self.virtual_start
    }
    /// Returns the exclusive mapped virtual stack end.
    #[must_use]
    pub const fn virtual_end(&self) -> u64 {
        self.virtual_end
    }
    /// Returns the number of currently mapped leaves.
    #[must_use]
    pub const fn mapped_count(&self) -> usize {
        self.mapped_count
    }
    /// Returns the number of physical frames exclusively owned.
    #[must_use]
    pub const fn owned_count(&self) -> usize {
        self.owned_count
    }
    /// Returns whether no physical frame is owned.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.owned_count == 0
    }
    /// Returns a recorded physical frame for a mapped page.
    #[must_use]
    pub fn physical_page(&self, index: usize) -> Option<u64> {
        (index < self.owned_count).then(|| self.physical_pages[index])
    }
    /// Returns whether an address lies inside the mapped stack range.
    #[must_use]
    pub const fn contains(&self, address: u64) -> bool {
        self.virtual_start <= address && address < self.virtual_end
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
#[allow(clippy::too_many_lines)]
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
        mapping.physical_pages[index] = page.start_address();
        mapping.owned_count += 1;
        let Some(virtual_address) = layout.stack_start.checked_add((index as u64) * PAGE_SIZE)
        else {
            return rollback(
                mapping,
                address_space,
                allocator,
                free_baseline,
                mapped_baseline,
                TaskStackError::Layout(TaskStackLayoutError::AddressOverflow),
            );
        };
        let virtual_page = match VirtualPage::new(virtual_address) {
            Ok(page) => page,
            Err(error) => {
                return rollback(
                    mapping,
                    address_space,
                    allocator,
                    free_baseline,
                    mapped_baseline,
                    TaskStackError::Paging(error),
                );
            }
        };
        let frame = match PhysicalFrame::new(page.start_address(), address_space.width()) {
            Ok(frame) => frame,
            Err(error) => {
                return rollback(
                    mapping,
                    address_space,
                    allocator,
                    free_baseline,
                    mapped_baseline,
                    TaskStackError::Paging(error),
                );
            }
        };
        match address_space.map_page(virtual_page, frame, MappingPermissions::kernel_rw_nx()) {
            Ok(MapOutcome::Created) => {}
            Ok(MapOutcome::AlreadyPresent) => {
                return rollback(
                    mapping,
                    address_space,
                    allocator,
                    free_baseline,
                    mapped_baseline,
                    TaskStackError::AlreadyMapped,
                );
            }
            Err(error) => {
                return rollback(
                    mapping,
                    address_space,
                    allocator,
                    free_baseline,
                    mapped_baseline,
                    TaskStackError::Paging(error),
                );
            }
        }
        mapping.mapped_count += 1;
    }
    // SAFETY: Every byte in [stack_start, stack_end) has just been mapped writable,
    // supervisor-only, and exclusively belongs to this task-stack mapping.
    #[allow(unsafe_code)]
    // SAFETY: documented directly above; this is the sole raw write boundary.
    unsafe {
        core::ptr::write_bytes(layout.stack_start as *mut u8, 0, TASK_STACK_SIZE);
    }
    if validate_task_stack(mapping, address_space).is_err() {
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
    mapping: &TaskStackMapping,
    address_space: &ActiveAddressSpace,
) -> Result<(), TaskStackError> {
    if mapping.owned_count != TASK_STACK_PAGE_COUNT || mapping.mapped_count != TASK_STACK_PAGE_COUNT
    {
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
    {
        return Err(TaskStackError::CorruptMapping);
    }
    let mut padding = layout
        .upper_guard
        .checked_add(PAGE_SIZE)
        .ok_or(TaskStackLayoutError::AddressOverflow)?;
    while padding < layout.slot_end {
        if address_space.translate(padding)?.is_some() {
            return Err(TaskStackError::CorruptMapping);
        }
        padding = padding
            .checked_add(PAGE_SIZE)
            .ok_or(TaskStackLayoutError::AddressOverflow)?;
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
    validate_task_stack(mapping, address_space)?;
    let empty = TaskStackMapping::empty(mapping.slot())?;
    let prospective = allocator_transaction(allocator);
    for address in mapping.physical_pages {
        prospective.deallocate(PageRange::new(address, 1)?)?;
    }
    prospective.check_invariants()?;
    let mapped_baseline = address_space.mapped_pages();
    let (virtual_pages, physical_frames) = prevalidate_leaf_objects(mapping, address_space)?;
    for (unmapped, index) in (0..TASK_STACK_PAGE_COUNT).enumerate() {
        let returned = match address_space.unmap_page(virtual_pages[index]) {
            Ok(frame) => frame,
            Err(error) => {
                if restore_unmapped(address_space, &virtual_pages, &physical_frames, unmapped)
                    .is_err()
                    || address_space.mapped_pages() != mapped_baseline
                {
                    return Err(TaskStackError::RollbackFailed);
                }
                return Err(TaskStackError::Paging(error));
            }
        };
        if returned.address() != mapping.physical_pages[index] {
            let restore_current = address_space.map_page(
                virtual_pages[index],
                returned,
                MappingPermissions::kernel_rw_nx(),
            );
            if restore_current != Ok(MapOutcome::Created)
                || restore_unmapped(address_space, &virtual_pages, &physical_frames, unmapped)
                    .is_err()
                || address_space.mapped_pages() != mapped_baseline
            {
                return Err(TaskStackError::RollbackFailed);
            }
            return Err(TaskStackError::CorruptMapping);
        }
    }
    allocator.copy_state_from(prospective);
    *mapping = empty;
    Ok(())
}

/// Restores a stack mapping after a transactional reclaim must be undone.
///
/// # Errors
///
/// Returns an error if any original leaf cannot be remapped or if the address
/// space and allocator do not return to their pre-reclaim baselines.
pub fn restore_task_stack(
    original: &TaskStackMapping,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
    allocator_baseline: &EarlyPhysicalPageAllocator,
    mapped_baseline: u64,
) -> Result<(), TaskStackError> {
    let result = (|| {
        if original.owned_count != TASK_STACK_PAGE_COUNT
            || original.mapped_count != TASK_STACK_PAGE_COUNT
        {
            return Err(TaskStackError::CorruptMapping);
        }
        let mut mapped = 0usize;
        for index in 0..TASK_STACK_PAGE_COUNT {
            let virtual_page = VirtualPage::new(
                original
                    .virtual_start
                    .checked_add((index as u64) * PAGE_SIZE)
                    .ok_or(TaskStackLayoutError::AddressOverflow)?,
            )?;
            let physical_frame =
                PhysicalFrame::new(original.physical_pages[index], address_space.width())?;
            let map_error = match address_space.map_page(
                virtual_page,
                physical_frame,
                MappingPermissions::kernel_rw_nx(),
            ) {
                Ok(MapOutcome::Created) => {
                    mapped += 1;
                    None
                }
                Ok(MapOutcome::AlreadyPresent) => Some(PagingError::AlreadyMapped),
                Err(error) => Some(error),
            };
            if let Some(map_error) = map_error {
                let mut rollback_ok = true;
                for rollback_index in (0..mapped).rev() {
                    let page = VirtualPage::new(
                        original.virtual_start + (rollback_index as u64) * PAGE_SIZE,
                    )?;
                    rollback_ok &= address_space.unmap_page(page).is_ok();
                }
                let reclaimed_baseline = mapped_baseline
                    .checked_sub(TASK_STACK_PAGE_COUNT as u64)
                    .ok_or(TaskStackError::RollbackFailed)?;
                if !rollback_ok || address_space.mapped_pages() != reclaimed_baseline {
                    return Err(TaskStackError::RollbackFailed);
                }
                return Err(TaskStackError::Paging(map_error));
            }
        }
        if address_space.mapped_pages() != mapped_baseline {
            return Err(TaskStackError::RollbackFailed);
        }
        if let Err(error) = validate_task_stack(original, address_space) {
            for index in (0..TASK_STACK_PAGE_COUNT).rev() {
                let page = VirtualPage::new(original.virtual_start + (index as u64) * PAGE_SIZE)?;
                if address_space.unmap_page(page).is_err() {
                    return Err(TaskStackError::RollbackFailed);
                }
            }
            return Err(error);
        }
        Ok(())
    })();
    // Preserve allocator ownership metadata even when remapping fails. The
    // caller retains the original mapping metadata and poisons the runtime if
    // the page-table restoration itself was incomplete.
    allocator.copy_state_from(allocator_baseline);
    result
}

fn prevalidate_leaf_objects(
    mapping: &TaskStackMapping,
    address_space: &ActiveAddressSpace,
) -> Result<
    (
        [VirtualPage; TASK_STACK_PAGE_COUNT],
        [PhysicalFrame; TASK_STACK_PAGE_COUNT],
    ),
    TaskStackError,
> {
    let mut virtual_pages = [VirtualPage::new(mapping.virtual_start)?; TASK_STACK_PAGE_COUNT];
    let mut physical_frames =
        [PhysicalFrame::new(mapping.physical_pages[0], address_space.width())?;
            TASK_STACK_PAGE_COUNT];
    for index in 0..mapping.mapped_count {
        virtual_pages[index] = VirtualPage::new(
            mapping
                .virtual_start
                .checked_add((index as u64) * PAGE_SIZE)
                .ok_or(TaskStackLayoutError::AddressOverflow)?,
        )?;
        physical_frames[index] =
            PhysicalFrame::new(mapping.physical_pages[index], address_space.width())?;
    }
    Ok((virtual_pages, physical_frames))
}

fn restore_unmapped(
    address_space: &mut ActiveAddressSpace,
    virtual_pages: &[VirtualPage; TASK_STACK_PAGE_COUNT],
    physical_frames: &[PhysicalFrame; TASK_STACK_PAGE_COUNT],
    count: usize,
) -> Result<(), TaskStackError> {
    for index in 0..count {
        let outcome = address_space.map_page(
            virtual_pages[index],
            physical_frames[index],
            MappingPermissions::kernel_rw_nx(),
        )?;
        if outcome != MapOutcome::Created {
            return Err(TaskStackError::RollbackFailed);
        }
    }
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
    let prospective = allocator_transaction(allocator);
    for index in 0..mapping.owned_count {
        prospective
            .deallocate(PageRange::new(mapping.physical_pages[index], 1)?)
            .map_err(|_| TaskStackError::RollbackFailed)?;
    }
    let empty = TaskStackMapping::empty(mapping.slot())?;
    prospective
        .check_invariants()
        .map_err(|_| TaskStackError::RollbackFailed)?;
    if prospective.free_pages() != free_baseline {
        return Err(TaskStackError::RollbackFailed);
    }

    let leaf_objects = if mapping.mapped_count == 0 {
        None
    } else {
        Some(prevalidate_leaf_objects(mapping, address_space)?)
    };
    let restored_mapped_count = mapped_baseline
        .checked_add(mapping.mapped_count as u64)
        .ok_or(TaskStackError::RollbackFailed)?;

    for (unmapped, index) in (0..mapping.mapped_count).enumerate() {
        let (virtual_pages, physical_frames) = leaf_objects.as_ref().expect("mapped leaves exist");
        let Ok(returned) = address_space.unmap_page(virtual_pages[index]) else {
            if restore_unmapped(address_space, virtual_pages, physical_frames, unmapped).is_err()
                || address_space.mapped_pages() != restored_mapped_count
            {
                return Err(TaskStackError::RollbackFailed);
            }
            return Err(original);
        };
        if returned.address() != mapping.physical_pages[index] {
            let restored_current = address_space.map_page(
                virtual_pages[index],
                returned,
                MappingPermissions::kernel_rw_nx(),
            );
            if restored_current != Ok(MapOutcome::Created)
                || restore_unmapped(address_space, virtual_pages, physical_frames, unmapped)
                    .is_err()
                || address_space.mapped_pages() != restored_mapped_count
            {
                return Err(TaskStackError::RollbackFailed);
            }
            return Err(TaskStackError::CorruptMapping);
        }
    }
    if address_space.mapped_pages() != mapped_baseline {
        let (virtual_pages, physical_frames) = leaf_objects.as_ref().expect("mapped leaves exist");
        if restore_unmapped(
            address_space,
            virtual_pages,
            physical_frames,
            mapping.mapped_count,
        )
        .is_err()
        {
            return Err(TaskStackError::RollbackFailed);
        }
        return Err(TaskStackError::RollbackFailed);
    }
    allocator.copy_state_from(prospective);
    *mapping = empty;
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
    use crate::arch::x86_64::paging::{self, MAX_PAGE_TABLE_PAGES, MappingPlan};
    use crate::memory::{MemoryRegion, MemoryRegionKind, MemoryRegionSource, RegionTable};

    const BACKING_PAGES: usize = MAX_PAGE_TABLE_PAGES + TASK_STACK_PAGE_COUNT + 8;

    #[repr(align(4096))]
    struct AlignedBacking([u8; BACKING_PAGES * 4096]);
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

    #[test]
    fn restore_returns_to_pre_reclaim_mapped_baseline() {
        let backing = std::boxed::Box::new(AlignedBacking([0; BACKING_PAGES * 4096]));
        let backing_start = backing.0.as_ptr() as u64;
        let mut regions = RegionTable::new();
        regions
            .push(MemoryRegion {
                start: backing_start,
                byte_len: u64::try_from(backing.0.len()).unwrap(),
                kind: MemoryRegionKind::Usable,
                source: MemoryRegionSource::FinnOS,
                attributes: 0,
            })
            .unwrap();

        let mut allocator = EarlyPhysicalPageAllocator::from_memory_regions(&regions).unwrap();
        let mut address_space = paging::build(&MappingPlan::new(), &mut allocator, 52).unwrap();
        let layout = TaskStackLayout::for_slot(2).unwrap();
        let mut original = TaskStackMapping::empty(2).unwrap();

        for index in 0..TASK_STACK_PAGE_COUNT {
            let physical = allocator.allocate_page().unwrap().start_address();
            let virtual_address = layout.stack_start + u64::try_from(index).unwrap() * PAGE_SIZE;
            original.physical_pages[index] = physical;
            original.owned_count += 1;
            assert_eq!(
                address_space
                    .map_page(
                        VirtualPage::new(virtual_address).unwrap(),
                        PhysicalFrame::new(physical, 52).unwrap(),
                        MappingPermissions::kernel_rw_nx(),
                    )
                    .unwrap(),
                MapOutcome::Created
            );
            original.mapped_count += 1;
        }
        validate_task_stack(&original, &address_space).unwrap();

        let allocator_before = allocator.clone();
        let free_before = allocator.free_pages();
        let mapped_before = address_space.mapped_pages();
        let mut reclaimed = original.clone();
        reclaim_task_stack(&mut reclaimed, &mut address_space, &mut allocator).unwrap();
        assert!(reclaimed.is_empty());
        assert_eq!(
            address_space.mapped_pages() + u64::try_from(TASK_STACK_PAGE_COUNT).unwrap(),
            mapped_before
        );
        assert_eq!(
            allocator.free_pages(),
            free_before + u64::try_from(TASK_STACK_PAGE_COUNT).unwrap()
        );

        restore_task_stack(
            &original,
            &mut address_space,
            &mut allocator,
            &allocator_before,
            mapped_before,
        )
        .unwrap();
        assert_eq!(address_space.mapped_pages(), mapped_before);
        assert_eq!(allocator.free_pages(), free_before);
        allocator.check_invariants().unwrap();
        validate_task_stack(&original, &address_space).unwrap();
    }
}
