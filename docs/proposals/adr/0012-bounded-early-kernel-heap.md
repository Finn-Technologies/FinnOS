# ADR 0012: Bounded early kernel heap

## Status

Accepted for x86-64 UEFI QEMU.

## Decision

Use a fixed 1 MiB virtual heap at `0x0000_2000_0000_0000`, surrounded by
unmapped guard pages. Back its 256 4 KiB leaves with separately allocated
physical pages and map them through the active address space as supervisor-only
RW+NX memory. Zero the range before installing a stable-Rust global allocator.

The allocator is an address-sorted first-fit free list with in-band metadata,
checked layout normalization, splitting, merging, statistics, and invariant
validation. It uses a small acquire/release lock but does not support
interrupt-context allocation. The heap has no automatic growth and has no
third-party allocator dependency.

## Alternatives

- A bump allocator is simpler but cannot support normal deallocation or reuse.
- A buddy allocator adds policy and metadata that are unnecessary for this
  bounded early phase.
- Slab allocation is premature before stable object types and caches exist.
- An external linked-list allocator crate adds a dependency and obscures the
  invariants required at this stage.
- Physically contiguous backing would waste the virtual-memory flexibility
  already provided by FinnOS page tables.
- An identity-mapped heap would couple allocator addresses to physical layout
  and weaken the separation between virtual heap and physical frames.
- Lazy growth and a large dynamic VM region require demand paging and reclaim
  policy that are explicitly out of scope.
- Per-CPU heaps require SMP and scheduler foundations that do not exist yet.

The fixed first-fit design is deterministic, small, testable, and appropriate
as temporary early-kernel infrastructure. It may later be supplemented or
replaced by specialized allocators when concurrency, object lifetimes, and
virtual-memory growth policies are established.
