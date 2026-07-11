# ADR 0010: Fixed-capacity extent page allocator

## Status

Accepted.

## Decision

Early physical pages use a fixed-capacity, range-based allocator. It manages
only classified usable memory, aligns ranges inward to complete 4 KiB pages,
allocates with deterministic first-fit, retains immutable managed extents for
free validation, keeps sorted mutable free extents, and merges adjacent free
ranges. The implementation has no heap dependency.

## Context

The first kernel page allocator must be predictable, safe, and usable before
FinnOS owns page tables or has a kernel heap. The classified memory map is
already normalized and has bounded metadata.

## Alternatives

- A per-page bitmap uses compact state but requires sizing and maintaining a
  potentially large bitmap.
- A buddy allocator improves large-allocation behavior but adds more policy and
  metadata than this early milestone needs.
- A linked list embedded in free memory would write allocator metadata into
  physical pages that this milestone must not initialize.
- A heap-backed tree or vector is unavailable before the kernel heap.

The extent design matches the current bounded map, makes first-fit behavior
easy to test, and can be replaced or supplemented when virtual memory,
concurrency, or larger allocation policies arrive.
