# Preemption-ready interrupt contexts

The x86-64 external interrupt entry builds a `repr(C)` 184-byte resumable frame.
Its 160-byte saved-register/software prefix contains all fifteen general-purpose
registers, vector, synthetic error code, RIP, CS, and RFLAGS. QEMU's current
long-mode ring-0/IST-0 delivery supplies a hardware tail at offsets `+160`
(saved RSP) and `+168` (saved SS), followed by an eight-byte alignment slot;
the `iretq` fields occupy through `+176`. The saved RSP value—not a slot
address—is the exact pre-interrupt RSP, and the current contract requires
`frame_pointer + 184 == saved_rsp`. The kernel data selector `0x10` is
validated as saved SS. Future CPL3 or nonzero-IST entries require a separate
frame contract. This is the measured and integration-tested FinnOS QEMU
ring-0/IST-0 contract, not a claim about the system-programming volume of the
AMD64 manual.

The Rust dispatcher receives a mutable complete frame and returns the frame to restore.
Today it always returns the original pointer, so timer delivery resumes the
interrupted task. This is distinct from `TaskContext`, which is only the SysV64
call-boundary state used by cooperative switching.

Timer ticks may set a deferred reschedule request through bounded preemption
guards. Requests remain pending while nesting is nonzero and are consumed only
in ordinary context at depth zero. No timer ISR selects a task, mutates the
runnable queue, switches CR3, or performs an interrupt-time context switch.
FPU/SIMD state and timer-driven switching remain future work.
