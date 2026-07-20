# UEFI boot

> Status: First-boot implementation
> Implementation: x86-64 boot manager is smoke-tested; ARM64 R3 is locally verified

The `finn-boot-uefi` package reads `EFI/FINNOS/KERNEL.ELF`, validates ELF64 headers, selected machine, and load ranges, allocates and copies PT_LOAD segments, collects available firmware metadata, retains the final memory map, exits boot services, and transfers control to the separate kernel. The x86 image uses `EFI/BOOT/BOOTX64.EFI`; ARM64 uses `EFI/BOOT/BOOTAA64.EFI` and permits an absent GOP for its headless R3 path.

The loader uses a QEMU-only identity-mapped 32 MiB kernel layout because the tested OVMF configuration does not expose the 16 MiB range. It does not implement verified boot, relocation, recovery, or hardware support. Legacy BIOS is not an initial target.

Malformed ELF input is rejected before allocation: validation covers the ELF64 identity, selected x86-64/AArch64 machine, program-header bounds, file and memory ranges, segment alignment, overlapping load ranges, and executable entry membership. The precise firmware, loader, handoff-pointer, and inherited-mapping trust assumptions are documented in [the boot protocol reference](../../docs/reference/boot-protocol.md#trust-and-threat-assumptions).
