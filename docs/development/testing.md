# Testing

The canonical test instructions, verified results, coverage gaps, and CI plan are in [TESTING.md](../../TESTING.md).

The repository currently has Rust and Python host tests plus eight x86-64 QEMU integration images: first boot, exceptions, memory map, physical allocator, page tables, heap, timer interrupts, and cooperative tasks. This is not “metadata-only” testing. ARM64, userspace, devices, storage, networking, and UI tests do not exist because those implementations do not exist.
