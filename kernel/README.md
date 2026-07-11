# Finn Kernel crate

> Status: First-boot kernel foundation
> Implementation: Bootable x86-64 QEMU diagnostic kernel

The crate exposes kernel name and version metadata, boot-information validation, framebuffer geometry helpers, and unit tests. The freestanding binary enters on a dedicated stack, writes COM1 diagnostics, validates `BootInfo`, draws a diagnostic framebuffer, and halts or exits QEMU in test mode.

Run `cargo check --workspace` and `cargo test --workspace`. See [kernel architecture](../docs/architecture/kernel.md).
