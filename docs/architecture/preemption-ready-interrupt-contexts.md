# Preemption-ready interrupt contexts

The x86-64 external interrupt entry builds a `repr(C)` 160-byte frame containing
all fifteen general-purpose registers, vector, synthetic error code, RIP, CS,
and RFLAGS in assembly stack order. For the current CPL0/IST0 contract the CPU
does not push RSP or SS; the interrupted RSP is therefore the frame address plus
160 bytes. User-mode frame extensions are intentionally not supported.

The Rust dispatcher receives a mutable frame and returns the frame to restore.
Today it always returns the original pointer, so timer delivery resumes the
interrupted task. This is distinct from `TaskContext`, which is only the SysV64
call-boundary state used by cooperative switching.

Timer ticks may set a deferred reschedule request through bounded preemption
guards. Requests remain pending while nesting is nonzero and are consumed only
in ordinary context at depth zero. No timer ISR selects a task, mutates the
runnable queue, switches CR3, or performs an interrupt-time context switch.
FPU/SIMD state and timer-driven switching remain future work.
