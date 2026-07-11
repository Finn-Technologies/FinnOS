# Architecture

> Status: Initial architectural direction
> Implementation: Not yet implemented beyond kernel metadata

FinnOS is planned as a layered system:

```text
Applications
Peony Framework and Shell
Native Frameworks
System Services
User-space Runtime
Finn Kernel
Hardware and Firmware
```

Architecture documents distinguish accepted direction, planned behavior, unresolved questions, and implementation status. Read [kernel](kernel.md), [processes](processes.md), [IPC](ipc.md), [capabilities](capabilities.md), [drivers](drivers.md), and [Peony](peony.md). Compatibility environments may come later, but will not define native FinnOS architecture.
