# Preemption-ready interrupt contexts

The x86-64 external interrupt entry builds a raw `repr(C)` 176-byte resumable
frame. Its 136-byte saved-register/software prefix contains all fifteen
general-purpose registers, vector, synthetic error code, RIP, CS, and RFLAGS;
the 40-byte raw return tail contains saved RSP and saved SS through the final
`iretq` field. QEMU's measured ring-0/IST-0 delivery permits bounded alignment
slack of 0 through 15 bytes after that raw frame, so the attributed footprint
is 176 through 191 bytes. The saved RSP
value—not a slot address—is the exact pre-interrupt RSP, and validation accepts
only those measured relationships with bounded 0–15-byte slack (176–191-byte
footprint). The dispatcher returns the raw pointer
unchanged.
The kernel data selector `0x10` is validated as saved SS. Future CPL3 or
nonzero-IST entries require a separate frame contract. This is the measured
and integration-tested FinnOS QEMU ring-0/IST-0 contract, not a claim about
the system-programming volume of the AMD64 manual.

The Rust dispatcher receives a mutable complete frame and returns the frame to restore.
Today it always returns the original pointer, so timer delivery resumes the
interrupted task. This is distinct from `TaskContext`, which is only the SysV64
call-boundary state used by cooperative switching.

Timer ticks may set a deferred reschedule request through bounded preemption
guards. Requests remain pending while nesting is nonzero and are consumed only
in ordinary context at depth zero. No timer ISR selects a task, mutates the
runnable queue, switches CR3, or performs an interrupt-time context switch.
FPU/SIMD state and timer-driven switching remain future work.
