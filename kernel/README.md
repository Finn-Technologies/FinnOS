# Finn Kernel crate

> Status: First-boot kernel foundation
> Implementation: Bootable x86-64 QEMU diagnostic kernel

The crate exposes kernel name and version metadata, boot-information validation, framebuffer geometry helpers, a safe UEFI memory-map parser, and an allocation-free early physical page allocator. The freestanding binary enters on a dedicated stack, installs a FinnOS-owned x86-64 GDT, TSS, and IDT, dispatches breakpoint, invalid-opcode, double-fault, general-protection, and page-fault exceptions, writes COM1 diagnostics, validates `BootInfo`, parses and classifies the UEFI memory map, initializes the allocator from usable regions, draws a diagnostic framebuffer, and halts or exits QEMU in test mode.

Run `cargo check --workspace` and `cargo test --workspace`. See [kernel architecture](../docs/architecture/kernel.md).
The x86-64 kernel constructs its own four-level page tables, bounded physical allocator, guarded 1 MiB heap, xAPIC, and 100 Hz timer. It now also provides eight generation-tagged cooperative task slots, guarded 64 KiB task stacks, real SysV64 context switching, deferred stack reclamation, and a dedicated idle task. IOAPIC routing, device IRQs, preemption, user mode, drivers, Peony, and ARM64 remain unimplemented.
# Preemption-ready interrupt contexts

The kernel now validates a fixed 160-byte ring-0 interrupt frame, attributes
interrupts by published stack ranges, and returns through the dispatcher’s
selected frame pointer. The timer still resumes the same task; nested
preemption guards only defer a bounded reschedule request.
