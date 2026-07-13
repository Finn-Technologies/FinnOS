//! A bounded first-fit allocator used by the early kernel heap.
//!
//! This module does not acquire physical memory and does not grow a virtual range. The caller
//! supplies one already-mapped, writable heap interval. Free-region metadata is stored in-band in
//! that interval so the allocator itself does not depend on the global allocator.
#![allow(unsafe_code)]

use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use core::ptr::null_mut;
use core::sync::atomic::{AtomicBool, Ordering};

const FREE_REGION_ALIGNMENT: usize = core::mem::align_of::<FreeRegion>();
const FREE_REGION_SIZE: usize = core::mem::size_of::<FreeRegion>();

#[repr(C)]
#[derive(Clone, Copy)]
struct FreeRegion {
    size: usize,
    next: usize,
}

/// Errors returned by the bounded kernel heap allocator.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HeapError {
    /// Allocation or deallocation was attempted from interrupt context.
    InterruptContextAllocationForbidden,
    /// The heap has already been initialized.
    AlreadyInitialized,
    /// The heap has not been initialized.
    NotInitialized,
    /// The supplied heap interval is invalid.
    InvalidHeapRange,
    /// Heap range arithmetic overflowed.
    HeapRangeOverflow,
    /// The heap start is not sufficiently aligned.
    HeapStartMisaligned,
    /// The heap cannot hold one free-region header.
    HeapTooSmall,
    /// A zero-sized allocation was requested.
    ZeroSizedAllocation,
    /// The requested alignment is invalid.
    InvalidAlignment,
    /// Layout arithmetic overflowed.
    LayoutOverflow,
    /// No free region can satisfy the request.
    OutOfMemory,
    /// A free fragment is too small to hold in-band metadata.
    FreeRegionTooSmall,
    /// A pointer is outside the initialized heap.
    PointerOutsideHeap,
    /// A pointer is not aligned to the supplied layout.
    PointerMisaligned,
    /// A returned allocation overlaps an existing free region.
    DeallocationOverlap,
    /// The allocator counters would underflow.
    CounterUnderflow,
    /// An allocator counter would overflow.
    CounterOverflow,
    /// The free-list contains a cycle or too many nodes.
    FreeListCycle,
    /// The free-list or its counters are inconsistent.
    CorruptHeapState,
    /// The lock could not be acquired by a non-blocking caller.
    LockUnavailable,
}

/// A copyable snapshot of heap usage.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HeapStats {
    /// Total bytes managed by the heap.
    pub total_bytes: usize,
    /// Bytes currently available for normalized allocations.
    pub free_bytes: usize,
    /// Bytes currently allocated, including normalized internal padding.
    pub allocated_bytes: usize,
    /// Highest observed normalized allocation usage.
    pub peak_allocated_bytes: usize,
    /// Number of successful allocations.
    pub allocation_count: usize,
    /// Number of successful deallocations.
    pub deallocation_count: usize,
    /// Number of failed allocation attempts.
    pub failed_allocation_count: usize,
    /// Number of free-list regions.
    pub free_region_count: usize,
    /// Size of the largest free-list region.
    pub largest_free_region: usize,
}

impl HeapStats {
    const fn empty() -> Self {
        Self {
            total_bytes: 0,
            free_bytes: 0,
            allocated_bytes: 0,
            peak_allocated_bytes: 0,
            allocation_count: 0,
            deallocation_count: 0,
            failed_allocation_count: 0,
            free_region_count: 0,
            largest_free_region: 0,
        }
    }
}

/// A normalized allocation request.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NormalizedLayout {
    /// Allocation size after minimum-size and metadata alignment rounding.
    pub size: usize,
    /// Effective alignment, including free-node alignment.
    pub align: usize,
}

/// An allocation-free first-fit heap allocator.
pub struct HeapAllocator {
    heap_start: usize,
    heap_end: usize,
    free_head: usize,
    stats: HeapStats,
    initialized: bool,
}

impl HeapAllocator {
    /// Construct an uninitialized allocator.
    #[must_use]
    pub const fn empty() -> Self {
        Self {
            heap_start: 0,
            heap_end: 0,
            free_head: 0,
            stats: HeapStats::empty(),
            initialized: false,
        }
    }

    /// Normalize a Rust allocation layout without touching heap state.
    ///
    /// # Errors
    ///
    /// Returns an error for zero-sized layouts, invalid alignments, or arithmetic overflow.
    pub fn normalized_layout(layout: Layout) -> Result<NormalizedLayout, HeapError> {
        if layout.size() == 0 {
            return Err(HeapError::ZeroSizedAllocation);
        }
        let requested_align = layout.align();
        if !requested_align.is_power_of_two() {
            return Err(HeapError::InvalidAlignment);
        }
        let align = requested_align.max(FREE_REGION_ALIGNMENT);
        let size = layout.size().max(FREE_REGION_SIZE);
        let rounded = size
            .checked_add(FREE_REGION_ALIGNMENT - 1)
            .ok_or(HeapError::LayoutOverflow)?
            & !(FREE_REGION_ALIGNMENT - 1);
        Ok(NormalizedLayout {
            size: rounded,
            align,
        })
    }

    /// Initialize the allocator over an already-mapped virtual range.
    ///
    /// # Errors
    ///
    /// Returns an error when the range is invalid, too small, or already initialized.
    pub fn initialize(&mut self, start: usize, end: usize) -> Result<(), HeapError> {
        if self.initialized {
            return Err(HeapError::AlreadyInitialized);
        }
        if start == 0 || start >= end {
            return Err(HeapError::InvalidHeapRange);
        }
        if !start.is_multiple_of(FREE_REGION_ALIGNMENT) {
            return Err(HeapError::HeapStartMisaligned);
        }
        let size = end.checked_sub(start).ok_or(HeapError::HeapRangeOverflow)?;
        if size < FREE_REGION_SIZE || !end.is_multiple_of(FREE_REGION_ALIGNMENT) {
            return Err(HeapError::HeapTooSmall);
        }
        // SAFETY: The caller guarantees that this complete range is mapped writable and is
        // exclusively reserved for this allocator. The first bytes hold the initial node.
        unsafe {
            (start as *mut FreeRegion).write(FreeRegion { size, next: 0 });
        }
        self.heap_start = start;
        self.heap_end = end;
        self.free_head = start;
        self.stats = HeapStats {
            total_bytes: size,
            free_bytes: size,
            free_region_count: 1,
            largest_free_region: size,
            ..HeapStats::empty()
        };
        self.initialized = true;
        Ok(())
    }

    /// Return whether this allocator has been initialized.
    #[must_use]
    pub const fn is_initialized(&self) -> bool {
        self.initialized
    }

    /// Return a statistics snapshot.
    #[must_use]
    pub const fn stats(&self) -> HeapStats {
        self.stats
    }

    /// Allocate according to a Rust layout.
    ///
    /// # Errors
    ///
    /// Returns an error when the allocator is uninitialized, the layout is invalid, the free list
    /// is corrupt, or no region can satisfy the request.
    pub fn allocate(&mut self, layout: Layout) -> Result<*mut u8, HeapError> {
        let normalized = match Self::normalized_layout(layout) {
            Ok(value) => value,
            Err(error) => {
                self.bump_failed_count();
                return Err(error);
            }
        };
        if !self.initialized {
            self.bump_failed_count();
            return Err(HeapError::NotInitialized);
        }
        let mut previous = 0usize;
        let mut current = self.free_head;
        let limit = self.node_limit();
        for _ in 0..limit {
            if current == 0 {
                break;
            }
            // SAFETY: Invariants require every traversed node to be inside the heap and large
            // enough for a FreeRegion. The checker is available to diagnose broken state.
            let node = unsafe { *(current as *const FreeRegion) };
            let node_end = match current.checked_add(node.size) {
                Some(value) if value <= self.heap_end && node.size >= FREE_REGION_SIZE => value,
                _ => {
                    self.bump_failed_count();
                    return Err(HeapError::CorruptHeapState);
                }
            };
            let Some(mut candidate) = align_up(current, normalized.align) else {
                self.bump_failed_count();
                return Err(HeapError::LayoutOverflow);
            };
            loop {
                let prefix = candidate.saturating_sub(current);
                if prefix != 0 && prefix < FREE_REGION_SIZE {
                    candidate = match candidate.checked_add(normalized.align) {
                        Some(value) => value,
                        None => break,
                    };
                    continue;
                }
                let Some(allocation_end) = candidate.checked_add(normalized.size) else {
                    break;
                };
                if allocation_end > node_end {
                    break;
                }
                let suffix = node_end - allocation_end;
                if suffix != 0 && suffix < FREE_REGION_SIZE {
                    break;
                }
                self.remove_and_split(previous, current, node, candidate, allocation_end);
                self.stats.free_bytes = self
                    .stats
                    .free_bytes
                    .checked_sub(normalized.size)
                    .ok_or(HeapError::CounterUnderflow)?;
                self.stats.allocated_bytes = self
                    .stats
                    .total_bytes
                    .checked_sub(self.stats.free_bytes)
                    .ok_or(HeapError::CounterUnderflow)?;
                self.stats.peak_allocated_bytes = self
                    .stats
                    .peak_allocated_bytes
                    .max(self.stats.allocated_bytes);
                self.stats.allocation_count = self
                    .stats
                    .allocation_count
                    .checked_add(1)
                    .ok_or(HeapError::CounterOverflow)?;
                self.refresh_free_stats()?;
                return Ok(candidate as *mut u8);
            }
            previous = current;
            current = node.next;
        }
        self.bump_failed_count();
        Err(HeapError::OutOfMemory)
    }

    /// Deallocate a block using the original Rust layout.
    ///
    /// # Errors
    ///
    /// Returns an error for an invalid layout, an outside or misaligned pointer, an overlapping
    /// free region, or inconsistent allocator state.
    pub fn deallocate(&mut self, pointer: *mut u8, layout: Layout) -> Result<(), HeapError> {
        let normalized = Self::normalized_layout(layout)?;
        if !self.initialized {
            return Err(HeapError::NotInitialized);
        }
        let address = pointer as usize;
        if address < self.heap_start || address >= self.heap_end {
            return Err(HeapError::PointerOutsideHeap);
        }
        if !address.is_multiple_of(normalized.align) {
            return Err(HeapError::PointerMisaligned);
        }
        let end = address
            .checked_add(normalized.size)
            .ok_or(HeapError::LayoutOverflow)?;
        if end > self.heap_end {
            return Err(HeapError::PointerOutsideHeap);
        }
        self.insert_free_region(address, normalized.size)?;
        self.stats.free_bytes = self
            .stats
            .free_bytes
            .checked_add(normalized.size)
            .ok_or(HeapError::CounterOverflow)?;
        self.stats.allocated_bytes = self
            .stats
            .total_bytes
            .checked_sub(self.stats.free_bytes)
            .ok_or(HeapError::CounterUnderflow)?;
        self.stats.deallocation_count = self
            .stats
            .deallocation_count
            .checked_add(1)
            .ok_or(HeapError::CounterOverflow)?;
        self.refresh_free_stats()?;
        Ok(())
    }

    /// Check all free-list and counter invariants without allocating.
    ///
    /// # Errors
    ///
    /// Returns an error when the allocator is uninitialized or any free-list, cycle, range, or
    /// counter invariant is violated.
    pub fn check_invariants(&self) -> Result<(), HeapError> {
        if !self.initialized {
            return Err(HeapError::NotInitialized);
        }
        if self.heap_start >= self.heap_end {
            return Err(HeapError::CorruptHeapState);
        }
        if self.free_head == 0 {
            if self.stats.free_bytes == 0
                && self.stats.free_region_count == 0
                && self.stats.largest_free_region == 0
                && self.stats.allocated_bytes == self.stats.total_bytes
            {
                return Ok(());
            }
            return Err(HeapError::CorruptHeapState);
        }
        let mut current = self.free_head;
        let mut previous_end = self.heap_start;
        let mut free_bytes = 0usize;
        let mut region_count = 0usize;
        let mut largest = 0usize;
        for _ in 0..self.node_limit() {
            if current == 0 {
                if free_bytes != self.stats.free_bytes
                    || region_count != self.stats.free_region_count
                    || largest != self.stats.largest_free_region
                    || self.stats.allocated_bytes
                        != self.stats.total_bytes.saturating_sub(free_bytes)
                {
                    return Err(HeapError::CorruptHeapState);
                }
                return Ok(());
            }
            if current < self.heap_start || current >= self.heap_end {
                return Err(HeapError::CorruptHeapState);
            }
            // SAFETY: The address and minimum node size were checked against the owned heap.
            let node = unsafe { *(current as *const FreeRegion) };
            let end = current
                .checked_add(node.size)
                .ok_or(HeapError::CorruptHeapState)?;
            if node.size < FREE_REGION_SIZE
                || end > self.heap_end
                || current < previous_end
                || (current == previous_end && current != self.heap_start)
                || !current.is_multiple_of(FREE_REGION_ALIGNMENT)
            {
                return Err(HeapError::CorruptHeapState);
            }
            free_bytes = free_bytes
                .checked_add(node.size)
                .ok_or(HeapError::CounterOverflow)?;
            region_count = region_count
                .checked_add(1)
                .ok_or(HeapError::CounterOverflow)?;
            largest = largest.max(node.size);
            previous_end = end;
            current = node.next;
        }
        Err(HeapError::FreeListCycle)
    }

    fn node_limit(&self) -> usize {
        self.heap_end
            .saturating_sub(self.heap_start)
            .checked_div(FREE_REGION_SIZE)
            .unwrap_or(0)
            .saturating_add(1)
    }

    const fn bump_failed_count(&mut self) {
        self.stats.failed_allocation_count = self.stats.failed_allocation_count.saturating_add(1);
    }

    fn refresh_free_stats(&mut self) -> Result<(), HeapError> {
        let mut current = self.free_head;
        let mut count = 0usize;
        let mut largest = 0usize;
        for _ in 0..self.node_limit() {
            if current == 0 {
                self.stats.free_region_count = count;
                self.stats.largest_free_region = largest;
                return Ok(());
            }
            // SAFETY: The list was just modified through validated region operations and the
            // bounded traversal prevents a malformed cycle from looping forever.
            let node = unsafe { *(current as *const FreeRegion) };
            if node.size < FREE_REGION_SIZE {
                return Err(HeapError::CorruptHeapState);
            }
            count = count.checked_add(1).ok_or(HeapError::CounterOverflow)?;
            largest = largest.max(node.size);
            current = node.next;
        }
        Err(HeapError::FreeListCycle)
    }

    fn remove_and_split(
        &mut self,
        previous: usize,
        current: usize,
        node: FreeRegion,
        allocation_start: usize,
        allocation_end: usize,
    ) {
        let prefix = allocation_start - current;
        let suffix = current + node.size - allocation_end;
        let suffix_address = if suffix != 0 {
            allocation_end
        } else {
            node.next
        };
        if prefix != 0 {
            // SAFETY: Prefix and suffix are disjoint free regions inside the owned heap.
            unsafe {
                (current as *mut FreeRegion).write(FreeRegion {
                    size: prefix,
                    next: suffix_address,
                });
                if suffix != 0 {
                    (suffix_address as *mut FreeRegion).write(FreeRegion {
                        size: suffix,
                        next: node.next,
                    });
                }
            }
            return;
        }
        if suffix != 0 {
            // SAFETY: The suffix is large enough for its in-band node and inside the heap.
            unsafe {
                (suffix_address as *mut FreeRegion).write(FreeRegion {
                    size: suffix,
                    next: node.next,
                });
            }
        }
        if previous == 0 {
            self.free_head = suffix_address;
        } else {
            // SAFETY: `previous` is a node found by the bounded free-list traversal.
            unsafe {
                (*(previous as *mut FreeRegion)).next = suffix_address;
            }
        }
    }

    fn insert_free_region(&mut self, start: usize, size: usize) -> Result<(), HeapError> {
        if size < FREE_REGION_SIZE || !start.is_multiple_of(FREE_REGION_ALIGNMENT) {
            return Err(HeapError::FreeRegionTooSmall);
        }
        let end = start.checked_add(size).ok_or(HeapError::LayoutOverflow)?;
        let mut previous = 0usize;
        let mut current = self.free_head;
        for _ in 0..self.node_limit() {
            if current == 0 || current >= start {
                break;
            }
            // SAFETY: The traversal is bounded and assumes the existing list passed its last
            // invariant check; malformed nodes are rejected by the overlap checks below.
            let node = unsafe { *(current as *const FreeRegion) };
            let node_end = current
                .checked_add(node.size)
                .ok_or(HeapError::CorruptHeapState)?;
            if node_end > start {
                return Err(HeapError::DeallocationOverlap);
            }
            previous = current;
            current = node.next;
        }
        if current != 0 {
            // SAFETY: `current` is the next bounded list node.
            let node = unsafe { *(current as *const FreeRegion) };
            if end > current {
                return Err(HeapError::DeallocationOverlap);
            }
            if node.size < FREE_REGION_SIZE {
                return Err(HeapError::CorruptHeapState);
            }
        }
        let merge_previous = if previous != 0 {
            // SAFETY: `previous` is a node returned by the bounded traversal.
            let node = unsafe { *(previous as *const FreeRegion) };
            previous
                .checked_add(node.size)
                .ok_or(HeapError::CorruptHeapState)?
                == start
        } else {
            false
        };
        let merge_next = current != 0 && end == current;
        if merge_previous {
            // SAFETY: The previous region is adjacent and its header is valid.
            unsafe {
                let previous_node = &mut *(previous as *mut FreeRegion);
                previous_node.size = previous_node
                    .size
                    .checked_add(size)
                    .ok_or(HeapError::CounterOverflow)?;
                if merge_next {
                    let next_node = *(current as *const FreeRegion);
                    previous_node.size = previous_node
                        .size
                        .checked_add(next_node.size)
                        .ok_or(HeapError::CounterOverflow)?;
                    previous_node.next = next_node.next;
                }
            }
            return Ok(());
        }
        // SAFETY: The new node is inside the heap and has no overlap with adjacent nodes.
        unsafe {
            (start as *mut FreeRegion).write(FreeRegion {
                size: size
                    + if merge_next {
                        (*(current as *const FreeRegion)).size
                    } else {
                        0
                    },
                next: if merge_next {
                    (*(current as *const FreeRegion)).next
                } else {
                    current
                },
            });
            if previous == 0 {
                self.free_head = start;
            } else {
                (*(previous as *mut FreeRegion)).next = start;
            }
        }
        Ok(())
    }
}

fn align_up(value: usize, alignment: usize) -> Option<usize> {
    value
        .checked_add(alignment.checked_sub(1)?)
        .map(|value| value & !(alignment - 1))
}

/// A lock wrapper for the single-core early heap.
pub struct LockedHeap {
    locked: AtomicBool,
    inner: UnsafeCell<HeapAllocator>,
}

// SAFETY: Access to the allocator state is serialized by `locked`; the kernel currently has one
// core and does not allocate from interrupt context.
unsafe impl Sync for LockedHeap {}

impl LockedHeap {
    /// Construct an uninitialized locked heap.
    #[must_use]
    pub const fn empty() -> Self {
        Self {
            locked: AtomicBool::new(false),
            inner: UnsafeCell::new(HeapAllocator::empty()),
        }
    }

    /// Initialize the wrapped allocator exactly once.
    ///
    /// # Errors
    ///
    /// Returns the underlying allocator initialization error.
    pub fn initialize(&self, start: usize, end: usize) -> Result<(), HeapError> {
        self.with_lock(|heap| heap.initialize(start, end))
    }

    /// Allocate using the wrapped allocator.
    ///
    /// # Errors
    ///
    /// Returns the underlying allocator allocation error.
    pub fn allocate(&self, layout: Layout) -> Result<*mut u8, HeapError> {
        if crate::interrupt::in_interrupt_context() {
            return Err(HeapError::InterruptContextAllocationForbidden);
        }
        self.with_lock(|heap| heap.allocate(layout))
    }

    /// Deallocate using the wrapped allocator.
    ///
    /// # Errors
    ///
    /// Returns the underlying allocator deallocation error.
    ///
    /// # Safety
    ///
    /// The pointer and layout must describe an allocation returned by this heap.
    pub unsafe fn deallocate(&self, pointer: *mut u8, layout: Layout) -> Result<(), HeapError> {
        if crate::interrupt::in_interrupt_context() {
            return Err(HeapError::InterruptContextAllocationForbidden);
        }
        self.with_lock(|heap| heap.deallocate(pointer, layout))
    }

    /// Return a statistics snapshot.
    pub fn stats(&self) -> HeapStats {
        self.with_lock(|heap| heap.stats())
    }

    /// Validate the wrapped allocator.
    ///
    /// # Errors
    ///
    /// Returns the underlying invariant error.
    pub fn check_invariants(&self) -> Result<(), HeapError> {
        self.with_lock(|heap| heap.check_invariants())
    }

    fn with_lock<T>(&self, operation: impl FnOnce(&mut HeapAllocator) -> T) -> T {
        while self
            .locked
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {}
        // SAFETY: The acquire exchange grants exclusive access until the release below.
        let result = unsafe { operation(&mut *self.inner.get()) };
        self.locked.store(false, Ordering::Release);
        result
    }
}

// SAFETY: `alloc` is called by Rust only for layouts supplied by the allocation API. The wrapped
// allocator returns null on every unsupported or exhausted request and never allocates internally.
unsafe impl GlobalAlloc for LockedHeap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if crate::interrupt::in_interrupt_context() {
            return null_mut();
        }
        self.allocate(layout).unwrap_or(null_mut())
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        if crate::interrupt::in_interrupt_context() {
            return;
        }
        // SAFETY: GlobalAlloc requires callers to provide a pointer/layout pair originating from
        // this allocator; the checked implementation still rejects malformed ranges.
        let _ = unsafe { self.deallocate(pointer, layout) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[repr(align(4096))]
    struct AlignedBuffer<const N: usize>([u8; N]);

    #[allow(clippy::large_stack_arrays)]
    fn allocator() -> (std::boxed::Box<AlignedBuffer<65_536>>, HeapAllocator) {
        let buffer = std::boxed::Box::new(AlignedBuffer([0; 65_536]));
        let start = buffer.0.as_ptr() as usize;
        let end = start + buffer.0.len();
        let mut allocator = HeapAllocator::empty();
        allocator.initialize(start, end).unwrap();
        (buffer, allocator)
    }

    #[test]
    fn initialization_and_statistics() {
        let (_buffer, mut allocator) = allocator();
        assert_eq!(allocator.stats().free_bytes, 65_536);
        assert_eq!(allocator.stats().free_region_count, 1);
        assert!(allocator.check_invariants().is_ok());
        let start = allocator.heap_start;
        let end = allocator.heap_end;
        assert_eq!(
            allocator.initialize(start, end),
            Err(HeapError::AlreadyInitialized)
        );
    }

    #[test]
    fn allocation_alignment_and_reuse() {
        let (_buffer, mut allocator) = allocator();
        let layout = Layout::from_size_align(31, 256).unwrap();
        let pointer = allocator.allocate(layout).unwrap();
        assert_eq!((pointer as usize) % 256, 0);
        unsafe { pointer.write_bytes(0x5a, layout.size()) };
        allocator.deallocate(pointer, layout).unwrap();
        assert_eq!(allocator.stats().free_bytes, 65_536);
        assert!(allocator.check_invariants().is_ok());
    }

    #[test]
    fn fragmentation_merges_both_sides() {
        let (_buffer, mut allocator) = allocator();
        let layout = Layout::from_size_align(128, 8).unwrap();
        let a = allocator.allocate(layout).unwrap();
        let b = allocator.allocate(layout).unwrap();
        let c = allocator.allocate(layout).unwrap();
        allocator.deallocate(b, layout).unwrap();
        let d = allocator
            .allocate(Layout::from_size_align(64, 8).unwrap())
            .unwrap();
        assert_eq!(d, b);
        allocator
            .deallocate(d, Layout::from_size_align(64, 8).unwrap())
            .unwrap();
        allocator.deallocate(a, layout).unwrap();
        allocator.deallocate(c, layout).unwrap();
        assert_eq!(allocator.stats().free_bytes, 65_536);
        assert!(allocator.check_invariants().is_ok());
    }

    #[test]
    fn exhaustion_leaves_state_valid() {
        let (_buffer, mut allocator) = allocator();
        let layout = Layout::from_size_align(1025, 8).unwrap();
        let mut pointers = [null_mut(); 64];
        let mut count = 0;
        while count < pointers.len() {
            match allocator.allocate(layout) {
                Ok(pointer) => {
                    pointers[count] = pointer;
                    count += 1;
                }
                Err(HeapError::OutOfMemory) => break,
                Err(error) => panic!("unexpected allocator error: {error:?}"),
            }
        }
        assert!(count > 0 && count < pointers.len());
        assert_eq!(allocator.allocate(layout), Err(HeapError::OutOfMemory));
        for pointer in pointers.into_iter().take(count) {
            allocator.deallocate(pointer, layout).unwrap();
        }
        assert_eq!(allocator.stats().free_bytes, 65_536);
        assert!(allocator.check_invariants().is_ok());
    }

    #[test]
    fn invalid_requests_are_rejected() {
        let (_buffer, mut allocator) = allocator();
        assert_eq!(
            allocator.allocate(Layout::from_size_align(0, 8).unwrap()),
            Err(HeapError::ZeroSizedAllocation)
        );
        assert_eq!(
            allocator.deallocate(
                core::ptr::dangling_mut::<u8>(),
                Layout::from_size_align(8, 8).unwrap()
            ),
            Err(HeapError::PointerOutsideHeap)
        );
    }

    #[test]
    fn exact_fit_accepts_empty_free_list_and_restores_heap() {
        let (_buffer, mut allocator) = allocator();
        let layout = Layout::from_size_align(65_536, 8).unwrap();
        let pointer = allocator.allocate(layout).unwrap();
        let stats = allocator.stats();
        assert_eq!(stats.free_bytes, 0);
        assert_eq!(stats.allocated_bytes, stats.total_bytes);
        assert_eq!(stats.free_region_count, 0);
        assert_eq!(stats.largest_free_region, 0);
        assert!(allocator.check_invariants().is_ok());
        assert_eq!(allocator.allocate(layout), Err(HeapError::OutOfMemory));
        assert_eq!(allocator.stats().failed_allocation_count, 1);
        assert_eq!(allocator.stats().free_bytes, 0);
        allocator.deallocate(pointer, layout).unwrap();
        assert_eq!(allocator.stats().free_bytes, 65_536);
        assert_eq!(allocator.stats().free_region_count, 1);
        assert_eq!(allocator.stats().largest_free_region, 65_536);
        assert!(allocator.check_invariants().is_ok());
    }

    #[test]
    fn initialization_rejects_invalid_ranges_and_repeated_initialization() {
        let mut invalid = HeapAllocator::empty();
        assert_eq!(
            invalid.initialize(0, 4096),
            Err(HeapError::InvalidHeapRange)
        );
        assert_eq!(
            invalid.initialize(4096, 4096),
            Err(HeapError::InvalidHeapRange)
        );
        assert_eq!(
            invalid.initialize(1, 8192),
            Err(HeapError::HeapStartMisaligned)
        );
        assert_eq!(invalid.initialize(4096, 4104), Err(HeapError::HeapTooSmall));
    }

    #[test]
    fn normalization_rejects_zero_size_and_handles_metadata_rounding() {
        assert_eq!(
            HeapAllocator::normalized_layout(Layout::from_size_align(0, 8).unwrap()),
            Err(HeapError::ZeroSizedAllocation)
        );
        let normalized =
            HeapAllocator::normalized_layout(Layout::from_size_align(1, 1).unwrap()).unwrap();
        assert!(normalized.size >= FREE_REGION_SIZE);
        assert_eq!(normalized.align, FREE_REGION_ALIGNMENT);
        let aligned =
            HeapAllocator::normalized_layout(Layout::from_size_align(1, 65_536).unwrap()).unwrap();
        assert_eq!(aligned.align, 65_536);
    }

    #[test]
    fn failed_allocation_only_increments_failure_counter() {
        let (_buffer, mut allocator) = allocator();
        let layout = Layout::from_size_align(65_536, 8).unwrap();
        let pointer = allocator.allocate(layout).unwrap();
        let before = allocator.stats();
        assert_eq!(allocator.allocate(layout), Err(HeapError::OutOfMemory));
        let after = allocator.stats();
        assert_eq!(after.free_bytes, before.free_bytes);
        assert_eq!(after.allocated_bytes, before.allocated_bytes);
        assert_eq!(after.free_region_count, before.free_region_count);
        assert_eq!(
            after.failed_allocation_count,
            before.failed_allocation_count + 1
        );
        allocator.deallocate(pointer, layout).unwrap();
    }

    #[test]
    fn empty_free_list_counter_mismatches_are_rejected() {
        let (_buffer, mut allocator) = allocator();
        let layout = Layout::from_size_align(65_520, 8).unwrap();
        let _pointer = allocator.allocate(layout).unwrap();
        allocator.stats.free_bytes = 1;
        assert_eq!(
            allocator.check_invariants(),
            Err(HeapError::CorruptHeapState)
        );
        allocator.stats.free_bytes = 0;
        allocator.stats.free_region_count = 1;
        assert_eq!(
            allocator.check_invariants(),
            Err(HeapError::CorruptHeapState)
        );
    }

    #[test]
    #[allow(clippy::large_stack_arrays)]
    fn locked_heap_rejects_interrupt_context_before_lock() {
        use crate::interrupt::InterruptContextGuard;
        let mut storage = [0_u8; 65_536];
        let heap = LockedHeap::empty();
        let start = storage.as_mut_ptr() as usize;
        heap.initialize(start, start + storage.len()).unwrap();
        let layout = Layout::from_size_align(32, 8).unwrap();
        let pointer = heap.allocate(layout).unwrap();
        let before = heap.stats();
        let guard = InterruptContextGuard::enter().unwrap();
        assert_eq!(
            heap.allocate(layout),
            Err(HeapError::InterruptContextAllocationForbidden)
        );
        assert_eq!(
            unsafe { heap.deallocate(pointer, layout) },
            Err(HeapError::InterruptContextAllocationForbidden)
        );
        assert_eq!(heap.stats().allocated_bytes, before.allocated_bytes);
        drop(guard);
        unsafe {
            heap.deallocate(pointer, layout).unwrap();
        }
        assert!(heap.check_invariants().is_ok());
    }
}
