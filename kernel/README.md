# Finn Kernel crate

> Status: First-boot kernel foundation
> Implementation: Bootable x86-64 QEMU diagnostic kernel

The crate exposes kernel name and version metadata, boot-information validation, framebuffer geometry helpers, a safe UEFI memory-map parser, and unit tests. The freestanding binary enters on a dedicated stack, installs a FinnOS-owned x86-64 GDT, TSS, and IDT, dispatches breakpoint, invalid-opcode, double-fault, general-protection, and page-fault exceptions, writes COM1 diagnostics, validates `BootInfo`, parses and classifies the UEFI memory map, draws a diagnostic framebuffer, and halts or exits QEMU in test mode.

Run `cargo check --workspace` and `cargo test --workspace`. See [kernel architecture](../docs/architecture/kernel.md).
