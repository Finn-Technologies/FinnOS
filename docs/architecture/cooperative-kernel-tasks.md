# Cooperative kernel tasks

FinnOS uses a single-BSP, fixed-capacity cooperative scheduler. Eight generation-tagged slots contain bootstrap, idle, and up to six ordinary kernel tasks. Bootstrap keeps the early kernel stack; every other live task owns a 64 KiB RW, NX, supervisor-only stack backed by sixteen individually allocated pages.

Task stack slots begin at `0x0000280000000000`, are 128 KiB apart, and retain unmapped lower and upper guards plus unmapped padding. Creation maps transactionally. Returning task functions become `Exited`; another running task unmaps their leaves, returns recorded frames, and advances the generation before reuse. Intermediate page-table pages remain reserved while leaf and free-page baselines are restored.

The FIFO queue and task table allocate no heap memory. A switch saves `rsp`, `rbx`, `rbp`, and `r12`–`r15`; caller-saved registers follow SysV64. A synthetic frame resumes through `ret` into the trampoline. All tasks share CR3, and switching changes neither CR3 nor IF.

Scheduler mutation is forbidden in interrupt context. Timer interrupts return to the interrupted task and never schedule. Normal boot blocks bootstrap and switches to idle, which executes `hlt` and cooperatively yields after timer wakeup.

This is cooperative rather than preemptive, single-BSP, kernel-only, and shared-address-space. Priorities, sleeping, processes, user mode, system calls, IPC, drivers, Peony, and ARM64 remain unimplemented.
