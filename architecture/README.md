# Architecture ports

> Status: Initial architecture-port plan
> Implementation: No architecture port exists

Architecture-neutral code should be the default. Architecture-specific behavior must not leak unnecessarily into generic kernel modules. FinnOS intends to support x86-64 and ARM64; each port will eventually provide CPU initialization, page tables, exceptions, interrupts, timers, context switching, atomics, cache maintenance, firmware entry, shutdown, and restart.

See [x86-64 notes](x86_64/README.md).
