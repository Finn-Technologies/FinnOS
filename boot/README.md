# Boot

> Status: Planned sequence
> Implementation: x86-64 UEFI-to-kernel handoff is implemented and smoke-tested

```text
Firmware → FinnOS Boot Manager → Component verification → Kernel loading
→ Initial system image → Kernel entry → User-space startup → Peony session
```

The implemented path locates the boot volume, reads and validates `KERNEL.ELF`, loads its segments, collects GOP and ACPI metadata, exits boot services, and calls the kernel. The current physical layout is QEMU-only and uses 32 MiB because the tested OVMF configuration reserves the 16 MiB range. Recovery and verified boot remain planned.
