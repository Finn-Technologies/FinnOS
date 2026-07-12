# Finn Kernel architecture

> Status: Initial architectural direction
> Implementation: x86-64 first-boot path exists; kernel core is not implemented

FinnOS is pursuing a hybrid microkernel direction. The privileged kernel is expected to own scheduling, virtual memory, interrupts, timers, IPC primitives, capability enforcement, and typed kernel-object management. Drivers and services should run in user space where practical.

The x86-64 exception foundation is now in place: a FinnOS-owned GDT, TSS with a dedicated double-fault IST stack, IDT, and assembly exception-entry stubs dispatch breakpoint, invalid-opcode, double-fault, general-protection, and page-fault exceptions. See [x86-64 exceptions](x86_64-exceptions.md) for details.

The kernel now parses and classifies the raw UEFI memory map into architecture-neutral `MemoryRegion` records. The parser validates descriptor metadata, decodes descriptors safely from `&[u8]`, maps UEFI types to conservative FinnOS classifications, and excludes the kernel image, `BootInfo` storage, raw memory-map storage, and framebuffer before producing a sorted, non-overlapping, normalized region table. See [physical memory map](physical-memory-map.md) for details.

The kernel also initializes a fixed-capacity early physical page allocator from the classified usable regions. It performs deterministic first-fit allocation, validated deallocation, and adjacent free-range merging without a heap. See [physical page allocation](physical-page-allocation.md). The x86-64 paging layer consumes reserved pages from this allocator for its fixed-capacity table pool.

Planned concerns include architecture boundaries, failure behavior, and security and performance tradeoffs. Exact scheduler algorithms, syscall numbers, object layouts, and memory policies are unresolved. The current implementation boots only the diagnostic x86-64 path and contains no scheduling, virtual memory manager, IPC, or kernel heap beyond boot validation, exception dispatch, memory-map parsing, early physical page allocation, serial output, and framebuffer diagnostics.

Non-goals for this stage are compatibility with another kernel and premature ABI commitments. Open questions include service restart semantics, resource accounting, and the final object model.
After physical allocation, the x86-64 path builds and activates a FinnOS-owned identity-mapped address space. See [x86-64 virtual memory](x86_64-virtual-memory.md).
