# First-boot architecture

> Status: Initial implementation plan
> Implementation: x86-64 UEFI execution is implemented and smoke-tested

The implemented boundary is UEFI firmware → FinnOS Boot Manager → separately linked ELF64 Finn Kernel. The boot manager validates the kernel, loads `PT_LOAD` ranges into interim identity-mapped physical addresses beginning at 32 MiB, gathers the UEFI memory map and GOP framebuffer, retains their backing storage, exits boot services, and transfers a version-one `BootInfo` pointer using the System V x86-64 ABI. The tested OVMF configuration rejected the 16 MiB range, so the relocation is explicit in the linker script and documentation.

The kernel is intended to enter on a dedicated 64 KiB stack, initialize COM1, validate the handoff, render a diagnostic framebuffer, and halt. The current trust model assumes firmware and the loader’s validation; verified boot is future work. Failure paths must report typed errors before the boot-services boundary and serial diagnostics afterward.

The interim address layout is QEMU-only. There is no physical-memory allocator, virtual-memory manager, interrupt ownership, scheduler, user space, driver framework, or Peony implementation.
