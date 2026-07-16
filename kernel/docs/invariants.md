# Kernel invariants

> Status: Implemented early-kernel invariants plus planned user/kernel requirements

Future userspace/object invariants include validating user-controlled addresses; preventing stale handles from resolving to new objects; reducing capability rights during delegation unless authorized; and releasing or revoking resources on process destruction. They are not implemented because userspace, handles, and capabilities do not yet exist. The early-kernel invariants below are implemented and tested for the current single-core x86-64 QEMU scope.

The early physical page allocator additionally validates sorted, non-overlapping
managed and free extents, inward-aligned full pages, free-range containment,
merged adjacency, and `free_pages <= total_pages` after every mutation. It is a
single-core allocator and is not thread-safe.
The active x86-64 root is FinnOS-owned. Its table pages remain allocated for the lifetime of the kernel, all leaves are 4 KiB supervisor mappings, and the null page and early-stack guard pages are absent.
The early heap is a fixed, exclusively mapped 1 MiB RW+NX range with two absent guard pages. Its physical backing pages remain allocated, its free list is sorted and non-overlapping, and allocation is permitted only in ordinary single-core flow with interrupts disabled.
# Interrupt and timer invariants

The BSP local APIC is mapped only at `0x0000_3000_0000_0000` with supervisor,
RW, NX, PCD+PWT permissions. The legacy PIC remains remapped and fully masked.
The timer handler uses only atomics and volatile APIC EOI; it never allocates or
acquires the heap lock. Heap operations reject interrupt context before lock
acquisition. IF is enabled only after IDT gates, PIC masks, APIC mapping,
calibration, and periodic timer programming have been validated.
