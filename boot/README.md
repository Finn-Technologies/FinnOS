# Boot

> Status: Planned sequence
> Implementation: x86-64 handoff is smoke-tested; ARM64 serial handoff is locally verified

```text
Firmware → FinnOS Boot Manager → Component verification → Kernel loading
→ Initial system image → Kernel entry → User-space startup → Peony session
```

The implemented path locates the boot volume, reads and validates `KERNEL.ELF`, loads its segments, collects GOP and ACPI metadata, exits boot services, and calls the kernel. The current physical layout is QEMU-only and uses 32 MiB because the tested OVMF configuration reserves the 16 MiB range. Recovery and verified boot remain planned.

For ARM64 R3, the same package produces `BOOTAA64.EFI`, validates
`EM_AARCH64`, loads the minimal kernel at its QEMU-only address, exits boot
services, and transfers the handoff with AAPCS64. Architecture parity remains
outside this serial-entry slice.
