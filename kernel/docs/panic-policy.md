# Panic policy

> Status: Preliminary policy
> Implementation: Kernel fatal exceptions halt or exit QEMU

Unexpected kernel exceptions (invalid opcode, double fault, general-protection fault, page fault, and unhandled vectors) currently print deterministic diagnostics and halt or exit QEMU via the debug-exit device. Recoverable user-process failure, restartable service failure, restartable driver failure, and session failure are not yet implemented. Exact policies and telemetry remain unresolved.
Paging initialization failures are reported as structured `PAGE_TABLE_ERROR` markers and follow the existing QEMU failure or halt path.
Heap initialization failures are reported as structured `HEAP_ERROR` markers and follow the same path. Heap exhaustion returns null through `GlobalAlloc`; kernel integration tests use fallible or explicit allocation APIs rather than intentionally invoking an uncontrolled allocation abort.
