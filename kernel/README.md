# Finn Kernel crate

> Status: First-boot kernel foundation
> Implementation: Bootable x86-64 diagnostic kernel plus minimal ARM64 serial entry

The crate exposes kernel name and version metadata, boot-information validation, framebuffer geometry helpers, a safe UEFI memory-map parser, and an allocation-free early physical page allocator. The freestanding binary enters on a dedicated stack, installs a FinnOS-owned x86-64 GDT, TSS, and IDT, dispatches breakpoint, invalid-opcode, double-fault, general-protection, and page-fault exceptions, writes COM1 diagnostics, validates `BootInfo`, parses and classifies the UEFI memory map, initializes the allocator from usable regions, draws a diagnostic framebuffer, and halts or exits QEMU in test mode.

Run `cargo check --workspace` and `cargo test --workspace`. See [kernel architecture](../docs/architecture/kernel.md).
The x86-64 kernel constructs its own four-level page tables, bounded physical allocator, guarded 1 MiB heap, xAPIC, and 100 Hz timer. It now also provides eight generation-tagged cooperative task slots, guarded 64 KiB task stacks, real SysV64 context switching, deferred stack reclamation, and a dedicated idle task. IOAPIC routing, device IRQs, preemption, user mode, drivers, and Peony remain unimplemented.

The separate R3 AArch64 binary provides only AAPCS64 entry, a 64 KiB early
stack, PL011 diagnostics, a panic marker, and semihosting termination for the
bounded QEMU test. It does not yet initialize or expose the x86 kernel
foundation's memory, exception, interrupt, timer, heap, or task semantics.
