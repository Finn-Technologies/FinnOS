# ADR 0014: Cooperative kernel tasks

## Status

Accepted for the x86-64 QEMU kernel foundation.

## Decision

Use eight fixed generation-tagged slots, a fixed FIFO runnable queue, one shared kernel address space, and single-BSP cooperative round robin. Slot 0 is bootstrap and slot 1 is idle. Other tasks receive guarded 64 KiB stacks backed by individual pages. Context switching saves SysV64 callee-saved GPRs and resumes with `ret`. Normal return becomes deferred exit and reclamation. The APIC timer stays active but never schedules. Use stable Rust and no external scheduler dependency.

## Rationale

Cooperative switching isolates stack, ABI, queue, lifecycle, and reclamation correctness before preemption adds interrupt-time synchronization. Fixed storage is deterministic and generation tags reject stale identities after reuse.

## Alternatives

No tasks, a shared stack, heap or contiguous stacks, dynamic tables, priorities, async-only execution, external schedulers, timer-ISR scheduling, user processes, and per-task address spaces were deferred as larger or less bounded first milestones.
