# Early physical page allocation

> Status: Implemented for x86-64 UEFI QEMU

FinnOS has an early, single-core physical page allocator. It manages only
memory-map regions classified as `Usable`; protected, firmware, device,
reserved, persistent, and unknown memory remain unavailable.

The page size is 4 KiB. Usable regions are aligned inward: the start is
rounded up and the exclusive end is rounded down, so no partial page can
overlap a protected region. The allocator retains immutable, sorted managed
extents and a mutable, sorted free-extent list. Adjacent usable regions are
merged during initialization.

Metadata is fixed-capacity (`256` managed extents and `256` free extents). No
heap, `Vec`, bitmap, raw pointer, or privileged instruction is required. A
capacity error is returned rather than silently dropping a region.

Allocation is deterministic first-fit from the lowest physical address. A
request can allocate one page or multiple contiguous pages, and never crosses
an unmanaged gap. Deallocation validates that the complete range belongs to a
single original managed extent, rejects overlap with free memory, inserts in
address order, and merges adjacent ranges on either side. These checks detect
double frees without relying only on the current free list.

The allocator validates sorted non-overlapping managed and free extents,
full-page alignment, containment, nonzero ranges, merged adjacency, and page
counters after mutations. Physical addresses are returned but not written,
mapped, or initialized.

This allocator is not thread-safe and is intended only for the current
single-core early-boot phase. Boot-services and runtime-services memory are
not reclaimed. There is no user-space allocator, NUMA support, or huge-page
allocation policy.

The FinnOS-owned x86-64 paging layer now reserves a fixed 64-page table pool
from this allocator. Those physical pages remain allocated while the active
address space exists. This pool is page-table storage, not a general kernel
heap. Only the explicitly required kernel mappings are created; physical pages
are not generally usable through arbitrary virtual mappings.

The early kernel heap separately reserves 256 backing pages from this allocator;
those pages are mapped only through the explicitly required heap virtual range.
Further Kernel Core work remains scheduling, interrupt, process, and IPC
foundations.
