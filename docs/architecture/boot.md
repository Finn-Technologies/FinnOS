# First-boot architecture

> Status: Implemented x86-64 QEMU boot path
> Implementation: x86-64 UEFI execution is implemented and smoke-tested

The implemented boundary is UEFI firmware → FinnOS Boot Manager → separately linked ELF64 Finn Kernel. The boot manager validates the kernel, loads `PT_LOAD` ranges into interim identity-mapped physical addresses beginning at 32 MiB, gathers the UEFI memory map and GOP framebuffer, retains their backing storage, exits boot services, and transfers a version-two `BootInfo` pointer using the System V x86-64 ABI. The tested OVMF configuration rejected the 16 MiB range, so the relocation is explicit in the linker script and documentation.

Version two of the boot protocol adds `boot_info_storage`, a `PhysicalRange` describing the page allocated for the `BootInfo` structure itself. The kernel uses this range to reserve its own handoff storage when classifying memory.

The kernel enters on a dedicated 64 KiB stack, initializes exception state, validates the handoff, classifies memory, installs private page tables and a heap, starts a BSP timer and cooperative scheduler, renders a diagnostic framebuffer, and idles. The current trust model assumes firmware and the loader’s validation; verified boot is future work. Failure paths report typed errors before the boot-services boundary and serial diagnostics afterward.

The interim address layout is QEMU-only. A bounded physical allocator, kernel-only virtual memory, BSP timer, and cooperative ring-0 scheduler exist. There is no user space, device IRQ routing, driver framework, storage, networking, ARM64 implementation, or Peony implementation.
