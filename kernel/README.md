# Finn Kernel crate

> Status: First-boot kernel foundation
> Implementation: Bootable x86-64 diagnostic kernel plus ARM64 serial, exception, early-memory, owned-MMU, and pinned GICv2 slices

The crate exposes kernel name and version metadata, boot-information validation, framebuffer geometry helpers, a bounded UEFI memory-map parser with an explicit unsafe physical-address boundary, and an allocation-free early physical page allocator. The freestanding binary enters on a dedicated stack, installs a FinnOS-owned x86-64 GDT, TSS, and IDT, dispatches breakpoint, invalid-opcode, double-fault, general-protection, and page-fault exceptions, writes COM1 diagnostics, validates `BootInfo`, parses and classifies the UEFI memory map, initializes the allocator from usable regions, draws a diagnostic framebuffer, and halts or exits QEMU in test mode.

Run `cargo check --workspace` and `cargo test --workspace`. See [kernel architecture](../docs/architecture/kernel.md).
The x86-64 kernel constructs its own four-level page tables, bounded physical allocator, guarded 1 MiB heap, xAPIC, and 100 Hz timer. It now also provides eight generation-tagged cooperative task slots, guarded 64 KiB task stacks, real SysV64 context switching, deferred stack reclamation, and a dedicated idle task. IOAPIC routing, device IRQs, preemption, user mode, drivers, and Peony remain unimplemented.

The AArch64 binary provides AAPCS64 entry, a 256 KiB early stack, PL011
diagnostics, EL1 vector installation, a raw GPR/FP/SIMD exception frame, and
isolated resumable/fatal synchronous-exception tests. R4.2 copies and validates
`BootInfo`, classifies the final UEFI map, constructs the shared allocator, and
verifies allocation/free outside protected ranges. R4.3 activates four-level
4 KiB EL1-only W^X tables and proves four precise translation/permission
aborts. R4.4 maps and initializes a pinned single-BSP QEMU GICv2 and proves one
real self-SGI acknowledge/EOI lifecycle. It does not yet provide a generic
timer, heap, task context, external interrupt discovery/routing, or broad
exception recovery.
