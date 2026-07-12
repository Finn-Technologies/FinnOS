# Kernel invariants

> Status: Preliminary requirements
> Implementation: Not enforced yet

Planned baseline invariants include validating user-controlled addresses; preventing stale handles from resolving to new objects; avoiding executable and writable mapping overlap by default; forbidding blocking work in interrupt context; reducing capability rights during delegation unless authorized; releasing or revoking resources on process destruction; and documenting ownership and synchronization for kernel data structures.

The early physical page allocator additionally validates sorted, non-overlapping
managed and free extents, inward-aligned full pages, free-range containment,
merged adjacency, and `free_pages <= total_pages` after every mutation. It is a
single-core allocator and is not thread-safe.
The active x86-64 root is FinnOS-owned. Its table pages remain allocated for the lifetime of the kernel, all leaves are 4 KiB supervisor mappings, and the null page and early-stack guard pages are absent.
The early heap is a fixed, exclusively mapped 1 MiB RW+NX range with two absent guard pages. Its physical backing pages remain allocated, its free list is sorted and non-overlapping, and allocation is permitted only in ordinary single-core flow with interrupts disabled.
