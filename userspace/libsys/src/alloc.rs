//! Simple bump allocator descriptor for the `finn-libsys` skeleton.
//!
//! The allocator owns no global state and performs no system calls. It only
//! computes addresses inside a caller-provided `[base, base + len)` window,
//! so it is host-testable and safe to use before any QEMU or architecture
//! wiring exists. The caller retains ownership of the backing memory and must
//! ensure `base`/`len` describe a valid writable region before dereferencing
//! any returned pointer.

/// Bump allocator descriptor over a caller-owned memory window.
///
/// `base` is the window start address as an integer, `len` is the window
/// length in bytes, and `offset` is the number of bytes already handed out
/// (including alignment padding). The struct never dereferences memory, so
/// construction and address arithmetic are safe; only dereferencing a
/// returned pointer requires the caller to uphold validity.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BumpAllocator {
    base: usize,
    len: usize,
    offset: usize,
}

impl BumpAllocator {
    /// Create a descriptor over `[base, base + len)`.
    ///
    /// No validation of `base` is performed: the descriptor only does checked
    /// integer arithmetic. The caller must ensure the window is valid before
    /// dereferencing pointers returned by [`BumpAllocator::alloc`].
    #[must_use]
    pub const fn new(base: usize, len: usize) -> Self {
        Self {
            base,
            len,
            offset: 0,
        }
    }

    /// Create a descriptor from a raw pointer and length.
    ///
    /// This is a convenience wrapper over [`BumpAllocator::new`]; `base` may
    /// be null or dangling until the caller actually dereferences an
    /// allocation, but dereferencing always requires a valid window.
    #[must_use]
    pub fn from_ptr(base: *mut u8, len: usize) -> Self {
        Self {
            base: base as usize,
            len,
            offset: 0,
        }
    }

    /// Return the window start address.
    #[must_use]
    pub const fn base_addr(&self) -> usize {
        self.base
    }

    /// Return the total window capacity in bytes.
    #[must_use]
    pub const fn capacity(&self) -> usize {
        self.len
    }

    /// Return the number of bytes handed out so far (including padding).
    #[must_use]
    pub const fn used(&self) -> usize {
        self.offset
    }

    /// Return the number of bytes still available.
    ///
    /// Uses saturating subtraction so a (caller-corrupted or moved) `offset`
    /// past `len` reports zero instead of panicking.
    #[must_use]
    pub const fn remaining(&self) -> usize {
        self.len.saturating_sub(self.offset)
    }

    /// Reset the allocator, reclaiming the whole window.
    ///
    /// Previously returned pointers must no longer be used after a reset.
    pub const fn reset(&mut self) {
        self.offset = 0;
    }

    /// Allocate `size` bytes with `align` alignment.
    ///
    /// `align` must be a non-zero power of two. On success the internal
    /// offset advances past alignment padding plus `size` and the aligned
    /// address is returned. A zero `size` still aligns the bump pointer and
    /// returns the aligned address without consuming additional bytes beyond
    /// padding.
    ///
    /// Returns `None` without mutating internal state when `align` is
    /// invalid, when address arithmetic overflows, or when the window is
    /// exhausted.
    #[must_use]
    pub fn alloc(&mut self, size: usize, align: usize) -> Option<*mut u8> {
        if align == 0 || !align.is_power_of_two() {
            return None;
        }
        let mask = align.wrapping_sub(1);
        let aligned = self.offset.checked_add(mask)? & !mask;
        let end = aligned.checked_add(size)?;
        if end > self.len {
            return None;
        }
        let addr = self.base.checked_add(aligned)?;
        self.offset = end;
        Some(addr as *mut u8)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn backing<const N: usize>() -> ([u8; N], usize) {
        let buf = [0u8; N];
        let base = buf.as_ptr() as usize;
        // Prevent `buf` from being dropped before the address is used by
        // forgetting length coupling: callers copy `base` and keep `buf`
        // alive in their own scope.
        (buf, base)
    }

    #[test]
    fn new_descriptor_reports_capacity_and_zero_use() {
        let alloc = BumpAllocator::new(0x1000, 64);
        assert_eq!(alloc.base_addr(), 0x1000);
        assert_eq!(alloc.capacity(), 64);
        assert_eq!(alloc.used(), 0);
        assert_eq!(alloc.remaining(), 64);
    }

    #[test]
    fn from_ptr_roundtrips_address() {
        let (_buf, base) = backing::<32>();
        let alloc = BumpAllocator::from_ptr(base as *mut u8, 32);
        assert_eq!(alloc.base_addr(), base);
        assert_eq!(alloc.capacity(), 32);
        assert_eq!(alloc.used(), 0);
    }

    #[test]
    fn sequential_allocs_bump_without_overlap() {
        let (_buf, base) = backing::<64>();
        let mut alloc = BumpAllocator::new(base, 64);
        let first = alloc.alloc(16, 1).expect("first alloc");
        let second = alloc.alloc(16, 1).expect("second alloc");
        assert_eq!(first as usize, base);
        assert_eq!(second as usize, base + 16);
        assert_eq!(alloc.used(), 32);
        assert_eq!(alloc.remaining(), 32);
    }

    #[test]
    fn alloc_respects_alignment() {
        let (_buf, base) = backing::<64>();
        let mut alloc = BumpAllocator::new(base, 64);
        let first = alloc.alloc(1, 1).expect("unaligned head");
        assert_eq!(first as usize, base);
        let aligned = alloc.alloc(8, 8).expect("aligned body");
        assert_eq!((aligned as usize) % 8, 0);
        // One byte plus seven bytes of padding plus eight bytes.
        assert_eq!(alloc.used(), 16);
    }

    #[test]
    fn alloc_rejects_invalid_alignment() {
        let (_buf, base) = backing::<64>();
        let mut alloc = BumpAllocator::new(base, 64);
        assert!(alloc.alloc(8, 0).is_none());
        assert!(alloc.alloc(8, 3).is_none());
        assert!(alloc.alloc(8, 6).is_none());
        // Rejected allocations must not advance the bump pointer.
        assert_eq!(alloc.used(), 0);
    }

    #[test]
    fn alloc_rejects_exhaustion_without_mutation() {
        let (_buf, base) = backing::<16>();
        let mut alloc = BumpAllocator::new(base, 16);
        assert!(alloc.alloc(16, 1).is_some());
        assert_eq!(alloc.used(), 16);
        assert!(alloc.alloc(1, 1).is_none());
        assert_eq!(alloc.used(), 16);
        assert_eq!(alloc.remaining(), 0);
    }

    #[test]
    fn alloc_rejects_overflow_without_mutation() {
        // `base + aligned` overflows `usize` on the second allocation.
        let mut alloc = BumpAllocator::new(usize::MAX - 2, 16);
        assert!(alloc.alloc(4, 1).is_some());
        let used = alloc.used();
        assert!(alloc.alloc(8, 1).is_none());
        assert_eq!(alloc.used(), used);
        // `aligned + size` overflows `usize`.
        let mut saturating = BumpAllocator::new(0x1000, usize::MAX);
        saturating.offset = usize::MAX - 1;
        assert!(saturating.alloc(8, 1).is_none());
        assert_eq!(saturating.used(), usize::MAX - 1);
    }

    #[test]
    fn zero_size_alloc_aligns_without_consuming_size() {
        let (_buf, base) = backing::<64>();
        let mut alloc = BumpAllocator::new(base, 64);
        assert!(alloc.alloc(3, 1).is_some());
        let before = alloc.used();
        let ptr = alloc.alloc(0, 8).expect("zero-size alloc");
        assert_eq!((ptr as usize) % 8, 0);
        // Only alignment padding is consumed.
        assert!(alloc.used() >= before);
        assert!(alloc.used() <= before + 7);
    }

    #[test]
    fn reset_reclaims_window() {
        let (_buf, base) = backing::<32>();
        let mut alloc = BumpAllocator::new(base, 32);
        assert!(alloc.alloc(32, 1).is_some());
        assert!(alloc.alloc(1, 1).is_none());
        alloc.reset();
        assert_eq!(alloc.used(), 0);
        assert_eq!(alloc.remaining(), 32);
        assert!(alloc.alloc(1, 1).is_some());
    }
}
