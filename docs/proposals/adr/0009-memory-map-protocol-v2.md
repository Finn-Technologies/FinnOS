# ADR 0009: Boot protocol version 2 for UEFI memory-map parsing

## Status

Accepted

## Context

The kernel needs to reserve the `BootInfo` structure itself when classifying physical memory. Version 1 of the boot protocol only transported the kernel image, framebuffer, and raw memory-map ranges. Without the `BootInfo` storage range, the kernel could not safely exclude its own handoff structure from future allocation.

## Decision

Increment the boot protocol version to 2 and add a `boot_info_storage: PhysicalRange` field to `BootInfo` immediately after `kernel_image`. The boot manager populates this field with the address and size of the page allocated for `BootInfo`. The kernel validation rejects version 1 structures and any structure whose size does not match the version 2 layout.

## Rationale

- The kernel must not treat the `BootInfo` page as usable RAM.
- Adding the range explicitly keeps the protocol self-describing.
- A version bump preserves compatibility checks and makes mismatches explicit.

## Alternatives considered

- Deriving the `BootInfo` range from the incoming pointer at runtime. Rejected because the kernel cannot know the allocation size without trusting the structure size, which is exactly what the new field encodes.
- Adding a generic reserved-ranges array. Rejected to keep the protocol simple for the current milestone.

## Consequences

- Both boot manager and kernel must be updated together.
- Old version 1 structures are explicitly rejected with `UnsupportedVersion`.
- The memory-map parser can now reserve `BootInfo` storage alongside the kernel image, memory-map storage, and framebuffer.

## Security impact

Prevents accidental reuse of the boot handoff page as general-purpose memory.

## Compatibility impact

Incompatible with version 1. The boot manager and kernel must agree on version 2.

## Follow-up work

- Physical page allocator will consume the classified memory map.
- Future protocol extensions should continue to bump the version for incompatible layout changes.
