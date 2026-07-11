# Physical memory map

> Status: Implemented for x86-64 UEFI QEMU
> Implementation: `kernel/src/memory`

## Overview

FinnOS receives a raw UEFI memory map from the boot manager. The kernel parses this map, classifies each region by intended use, applies protected-range exclusions, normalizes the result, and emits deterministic summary diagnostics. The early physical page allocator consumes the resulting `Usable` regions; this map layer still does not reclaim firmware memory or create mappings.

## Raw UEFI descriptor handoff

The boot protocol transports:

- `memory_map.address`: physical address of the raw map buffer.
- `memory_map.byte_len`: used byte length of the buffer.
- `memory_map.descriptor_size`: bytes per descriptor (the stride).
- `memory_map.descriptor_version`: UEFI descriptor version.

The kernel does not assume the descriptor size equals a fixed Rust struct size. It advances through the buffer using `descriptor_size` and reads only the first 40 bytes of each descriptor.

## Safe decoding

The kernel validates metadata arithmetically and then creates a single `&[u8]` slice from the validated physical range. All descriptor decoding operates on that slice using explicit little-endian byte reads. No arbitrary firmware bytes are cast to aligned references.

## UEFI source types and FinnOS classifications

UEFI memory types are mapped conservatively:

| UEFI type | FinnOS classification |
|-----------|------------------------|
| Conventional | `Usable` (after exclusions) |
| Loader code/data, Boot-services code/data | `BootServices` |
| Runtime-services code/data | `RuntimeServices` |
| Reserved | `Reserved` |
| Unusable | `Unusable` |
| ACPI reclaimable | `AcpiReclaimable` |
| ACPI non-volatile | `AcpiNonVolatile` |
| MMIO, MMIO port space | `MemoryMappedIo` |
| PAL code | `Firmware` |
| Persistent memory | `Persistent` |
| Unknown | `UnknownFirmwareType` |

Boot-services memory is not reclaimed yet; it is classified separately so a future allocator can reclaim it safely.

## Protected ranges

After decoding firmware descriptors, the parser subtracts or reserves:

- `Kernel`: the loaded kernel image range.
- `BootInfo`: the `BootInfo` structure storage range.
- `MemoryMapStorage`: the raw UEFI memory-map buffer range.
- `Framebuffer`: the GOP framebuffer backing range.

These ranges are applied in a fixed priority order. Overlapping protected ranges are resolved by that order.

## Range splitting

A protected range may lie inside a larger firmware descriptor. The parser splits the containing region into up to three parts: the portion before the exclusion, the exclusion itself, and the portion after the exclusion. Adjacent regions with the same classification and compatible attributes are merged.

## Normalization

The final region table is:

- Sorted by physical start address.
- Non-overlapping.
- Free of zero-length entries.
- Merged where adjacent regions have the same classification and compatible attributes.
- Deterministic for identical input.

## No-allocation design

The parser uses a fixed-capacity region table (`MAX_MEMORY_REGIONS = 256`). It does not require a heap, `Vec`, or dynamic allocation. Host tests may use `Vec` only in test-only helper code.

## Capacity limits

If the final map would exceed `MAX_MEMORY_REGIONS`, the parser returns a structured `OutputCapacityExceeded` error and the kernel halts.

## Current limitations

- Memory is classified but not allocated.
- Boot-services memory is not yet reclaimed.
- The physical page allocator uses only `Usable` regions and does not reclaim boot-services or runtime-services memory.
- There are no FinnOS-owned page tables.
- There is no kernel heap.
- x86-64 UEFI QEMU only.

## Next step

The next Kernel Core step is FinnOS-owned x86-64 page tables, which can consume physical addresses returned by the allocator.
The classified ranges remain the source of physical-page ownership while paging is built; page-table storage is reserved from `Usable` pages and is not returned while active.
