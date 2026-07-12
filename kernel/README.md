# Finn Kernel crate

> Status: First-boot kernel foundation
> Implementation: Bootable x86-64 QEMU diagnostic kernel

The crate exposes kernel name and version metadata, boot-information validation, framebuffer geometry helpers, a safe UEFI memory-map parser, and an allocation-free early physical page allocator. The freestanding binary enters on a dedicated stack, installs a FinnOS-owned x86-64 GDT, TSS, and IDT, dispatches breakpoint, invalid-opcode, double-fault, general-protection, and page-fault exceptions, writes COM1 diagnostics, validates `BootInfo`, parses and classifies the UEFI memory map, initializes the allocator from usable regions, draws a diagnostic framebuffer, and halts or exits QEMU in test mode.

Run `cargo check --workspace` and `cargo test --workspace`. See [kernel architecture](../docs/architecture/kernel.md).
The x86-64 kernel now constructs and activates its own four-level identity-mapped page tables. Table pages come from the fixed-capacity physical allocator pool; kernel code is read-only executable, data and stack are writable NX, and the null page plus early-stack guard pages remain absent. After activation it maps a fixed 1 MiB supervisor-only RW+NX heap backed by 256 individually allocated pages and installs the bounded first-fit allocator as the global allocator.
