# x86-64 interrupts and timer

FinnOS now owns the BSP local interrupt foundation for x86-64 QEMU. Vectors
0x00–0x1f are CPU exceptions, 0x20–0x2f are remapped but masked legacy PIC
vectors, 0x40 is the local APIC timer, and 0xff is the local APIC spurious
vector. The timer and spurious entries are ring-0 interrupt gates with IST 0;
there are no user-callable gates.

The assembly entries save all general-purpose registers, push a vector and a
synthetic zero error code, execute `cld`, call the Rust dispatcher with a
16-byte aligned SysV64 stack, restore the complete 184-byte frame, and return
with `iretq`. The supported ring-0/IST-0 hardware tail has saved RSP at `+160`,
saved SS at `+168`, and an alignment slot at `+176`; `iretq` consumes the return
fields through `+176`.
This milestone assumes ring-0-only execution and therefore does not use
`swapgs` or save SIMD state.

The legacy 8259 PIC is remapped to 0x20/0x28 and both masks are verified as
0xff. The BSP APIC is detected through CPUID leaf 1 and IA32_APIC_BASE;
x2APIC is rejected. Its physical page is mapped once at
`0x0000_3000_0000_0000` as supervisor-only, writable, NX, PCD+PWT MMIO.
Register access is bounded 32-bit volatile access.

The local APIC is software-enabled in xAPIC mode. PIT channel 2 is programmed
as a polled, speaker-disabled one-shot for a 10 ms reference. If the measured
decrement is `elapsed`, the periodic initial count is calculated with checked
integer arithmetic as `elapsed * 1000 / (reference_ms * target_hz)`, truncating
toward zero. For 10 ms and 100 Hz this is approximately the measured 10 ms
count, not counts per second. An independent 50 ms PIT window accepts 3–7
observed timer ticks in QEMU. Each timer entry increments an atomic 64-bit tick
counter and writes APIC EOI exactly once. Ticks convert to saturated monotonic
milliseconds; this is not wall-clock time and no sleep or timer-queue API exists.

Interrupt context is tracked by a non-allocating nesting counter. Heap
allocation and deallocation are rejected before the heap lock in that context;
the timer handler never allocates. Unexpected external vectors are diagnosed,
maskable interrupts are disabled, and normal builds halt fatally. Successful
normal boot enters an interruptible `hlt` idle loop after First Boot; fatal
paths use the separate `cli; hlt` loop. Neither is a scheduler.

The timer integration test waits with `hlt` for at least eight real local APIC
deliveries. It does not execute `int 0x40`. The only software interrupt in the
test is `int 0xff`, solely to verify the spurious return path; that path sends
no EOI. MADT parsing, IOAPIC routing, device IRQs, x2APIC, SMP, IPIs,
scheduling, preemption, user mode, and wall-clock time remain future work.
# Preemption-ready return path

The current proven QEMU contract is a 184-byte ring-0/IST-0 frame: the
160-byte saved-register/software prefix is followed by saved RSP, saved SS,
and one alignment slot. The dispatcher receives and returns the complete frame
pointer, validates `frame + 184 == saved_rsp`, and `iretq` consumes the return
fields through offset `+176`. This is an integration-tested FinnOS contract;
future CPL3 and alternate-IST layouts remain unsupported and require separate
frame types. The 100 Hz timer records a snapshot and sends EOI, but always
resumes the interrupted task; timer-driven scheduling is the next milestone.
