# Finn Kernel architecture

> Status: Initial architectural direction
> Implementation: x86-64 foundations plus local ARM64 exception, early-memory, owned-MMU, and pinned GICv2 slices; kernel core remains in progress

FinnOS is pursuing a hybrid microkernel direction. The privileged kernel is expected to own scheduling, virtual memory, interrupts, timers, IPC primitives, capability enforcement, and typed kernel-object management. Drivers and services should run in user space where practical.

The AArch64 executable reaches a linked early stack and PL011 serial marker. Its
locally verified R4.1 slice measures EL1, installs VBAR before handoff pointer
checking, saves a raw frame, and resumes one controlled `BRK`; see
[AArch64 synchronous exceptions](aarch64-exceptions.md). The R4.2 slice reuses
the shared handoff validator, protected memory classifier, and physical-page
allocator. R4.3 transactionally reserves table pages and activates a bounded
EL1-only W^X identity map; see [AArch64 virtual memory](aarch64-virtual-memory.md).
R4.4 maps and initializes a fixed single-BSP QEMU GICv2 and validates one real
self-SGI lifecycle; see [AArch64 GIC](aarch64-gic.md). It does not yet implement
generic-timer, task-context, or external interrupt-routing semantics.

The x86-64 exception foundation is now in place: a FinnOS-owned GDT, TSS with a dedicated double-fault IST stack, IDT, and assembly exception-entry stubs dispatch breakpoint, invalid-opcode, double-fault, general-protection, and page-fault exceptions. See [x86-64 exceptions](x86_64-exceptions.md) for details.

The kernel now parses and classifies the raw UEFI memory map into architecture-neutral `MemoryRegion` records. The parser validates descriptor metadata, decodes descriptors safely from `&[u8]`, maps UEFI types to conservative FinnOS classifications, and excludes the kernel image, `BootInfo` storage, raw memory-map storage, and framebuffer before producing a sorted, non-overlapping, normalized region table. See [physical memory map](physical-memory-map.md) for details.

The kernel also initializes a fixed-capacity early physical page allocator from the classified usable regions. It performs deterministic first-fit allocation, validated deallocation, and adjacent free-range merging without a heap. See [physical page allocation](physical-page-allocation.md). Both architecture paging layers consume reserved pages from this allocator for fixed-capacity table pools.

The x86-64 path maps a fixed early kernel heap and installs its bounded first-fit global allocator after paging activation. It also runs up to eight generation-tagged cooperative ring-0 tasks on guarded stacks with real SysV64 context switches, exit, reaping, and idle. Interrupt entry now constructs a complete 176-byte ring-0 return frame, permits the dispatcher to select the validated return-frame pointer, attributes interrupts from published stack ranges, and records deferred reschedule requests behind bounded nesting guards. The timer still returns to the interrupted task: there is no timer-driven switch, blocking scheduler, or user-mode frame support. Exact preemptive scheduler algorithms, syscall numbers, object layouts, and user-memory policies remain unresolved.

Non-goals for this stage are compatibility with another kernel and premature ABI commitments. Open questions include service restart semantics, resource accounting, and the final object model.
After physical allocation, the x86-64 path builds and activates a FinnOS-owned identity-mapped address space. See [x86-64 virtual memory](x86_64-virtual-memory.md).
The current BSP then owns a xAPIC periodic timer at 100 Hz and exposes
monotonic ticks. See [x86-64 interrupts and timer](x86_64-interrupts-and-timer.md).
# Kernel Core interrupt boundary

The current x86-64 Kernel Core includes a single-BSP xAPIC periodic timer and
monotonic ticks. General external IRQ routing, IOAPIC/MADT support, SMP,
preemptive scheduling, user mode, and device drivers remain future work. The current deferred-request boundary is preparation for preemption, not preemption itself.
