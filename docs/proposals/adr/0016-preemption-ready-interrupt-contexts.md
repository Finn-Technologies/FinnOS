# ADR 0016: Preemption-ready interrupt contexts

## Decision

Use a complete, fixed ring-0 interrupt-return frame and dispatcher-selected
return pointer. The current QEMU contract is a 136-byte prefix plus a 40-byte
raw `iretq` tail (176 bytes), followed by measured 0–15-byte alignment
slack (191-byte maximum footprint). Keep it separate from cooperative
`TaskContext`, and introduce allocation-free nested preemption-disable state
with deferred requests.

## Consequences

The timer continues to return to the same task. This preserves the cooperative
scheduler while creating a validated boundary for a later preemptive scheduler.
The design is BSP-only and does not cover user-mode frames or FPU/SIMD state.

## Rejected alternatives

- Reusing the cooperative context omits asynchronous GPR and return-frame state.
- Reading scheduler `current` in the ISR misattributes stacks during switching.
- Retaining a raw frame pointer would create an invalid long-lived ownership claim.
- Disabling interrupts for all critical sections is too broad; bounded guards keep
  IF enabled and defer only future scheduling work.
- Scheduling directly in the timer handler would mutate policy from interrupt
  context and is the next milestone, not this one.
- Saving FPU/SIMD state now would expand the contract without a consumer.
- Deferring the whole foundation would leave interrupt return and attribution
  unvalidated.
