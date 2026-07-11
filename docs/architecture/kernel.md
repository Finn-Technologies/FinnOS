# Finn Kernel architecture

> Status: Initial architectural direction
> Implementation: x86-64 first-boot path exists; kernel core is not implemented

FinnOS is pursuing a hybrid microkernel direction. The privileged kernel is expected to own scheduling, virtual memory, interrupts, timers, IPC primitives, capability enforcement, and typed kernel-object management. Drivers and services should run in user space where practical.

Planned concerns include architecture boundaries, failure behavior, and security and performance tradeoffs. Exact scheduler algorithms, syscall numbers, object layouts, and memory policies are unresolved. The current implementation boots only the diagnostic x86-64 path and contains no scheduling, physical or virtual memory manager, IPC, or kernel core beyond boot validation, serial output, and framebuffer diagnostics.

Non-goals for this stage are compatibility with another kernel and premature ABI commitments. Open questions include service restart semantics, resource accounting, and the final object model.
