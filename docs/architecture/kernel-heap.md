# Early kernel heap

FinnOS has a bounded early heap for ordinary kernel allocations after its
FinnOS-owned page tables are active. The heap is a fixed 1 MiB virtual range:

- Region base: `0x0000_2000_0000_0000`
- Lower guard: the base page, unmapped
- Heap start: `0x0000_2000_0000_1000`
- Heap end: heap start plus `1 MiB`
- Upper guard: the page at the exclusive heap end, unmapped

The 256 heap pages are backed by individually allocated physical pages from
the early physical page allocator. They are mapped through the active page
tables as supervisor-only, writable, non-executable, normal cacheable RAM.
The physical pages are retained for the kernel lifetime; they are not an
identity-mapped general physical-memory window.

After every mapping is validated, the complete virtual heap is zeroed. The
architecture-neutral allocator then places free-region headers inside the
free heap memory and uses an address-sorted first-fit list. Allocation
normalizes size and alignment, splits valid prefix and suffix regions, and
deallocation inserts and merges adjacent regions. Tiny fragments are never
silently lost. Statistics and a bounded invariant checker cover free bytes,
allocated bytes, peak usage, allocation counts, largest free region, cycles,
and sorted non-overlapping free regions.

The allocator is installed as the Rust `GlobalAlloc` only after heap mapping,
zeroing, initialization, and invariant validation succeed. `Box`, fallible
`Vec` reservation, `String`, and direct aligned allocations are exercised by
the QEMU heap test. Exhaustion returns null through `GlobalAlloc` and is tested
with explicit fallible allocation; heap code does not invoke an allocation
error hook or deliberately abort.

The lock uses `AtomicBool` and `UnsafeCell` with acquire/release ordering. It
does not allocate, log, or disable interrupts while held. This is a
single-core early-boot allocator: interrupt-context allocation, per-CPU heaps,
user heaps, slab caches, automatic growth, heap-page reclamation, NUMA, and a
general virtual-memory manager are not implemented. Stable Rust is used
without a third-party allocator dependency or nightly feature gate.
# Interrupt-context prohibition

The heap rejects explicit allocation and deallocation from interrupt context
before taking its spin lock. The global allocator returns null for interrupt
allocation and ignores interrupt deallocation, preventing single-core ISR
re-entry deadlock.
