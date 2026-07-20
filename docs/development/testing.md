# Testing

The canonical test instructions, verified results, coverage gaps, and CI plan are in [TESTING.md](../../TESTING.md).

The repository currently has Rust and Python host tests plus eight x86-64 QEMU integration images: first boot, exceptions, memory map, physical allocator, page tables, heap, timer interrupts, and cooperative tasks. ARM64 has separate serial-entry, synchronous-exception, fatal-diagnostic, memory-map/allocator, owned-page-table, and pinned GICv2 self-SGI images verified locally in the current worktree. This is not “metadata-only” testing. ARM64 generic-timer, task-context, external interrupt-routing, userspace, device, storage, networking, and UI tests do not exist because those implementations do not exist.

CI also boots the configured x86-64 release profile through the first-boot marker contract.
Bounded QEMU runs preserve serial output and their exact image/manifest/ELF evidence under
`build/out/`; CI uploads that directory when a smoke workflow fails.
