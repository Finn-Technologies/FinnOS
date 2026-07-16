# UEFI boot

> Status: First-boot implementation
> Implementation: x86-64 UEFI boot manager is implemented and smoke-tested

The `finn-boot-uefi` package reads `EFI/FINNOS/KERNEL.ELF`, validates ELF64 x86-64 headers and load ranges, allocates and copies PT_LOAD segments, collects GOP and ACPI metadata, retains the final memory map, exits boot services, and transfers control to the separate kernel. The image expects `EFI/BOOT/BOOTX64.EFI` and `EFI/FINNOS/KERNEL.ELF`.

The loader uses a QEMU-only identity-mapped 32 MiB kernel layout because the tested OVMF configuration does not expose the 16 MiB range. It does not implement verified boot, relocation, recovery, or hardware support. Legacy BIOS is not an initial target.

Malformed ELF input is rejected before allocation: validation covers the ELF64/x86-64 identity, program-header bounds, file and memory ranges, segment alignment, overlapping load ranges, and executable entry membership. The precise firmware, loader, handoff-pointer, and inherited-mapping trust assumptions are documented in [the boot protocol reference](../../docs/reference/boot-protocol.md#trust-and-threat-assumptions).
