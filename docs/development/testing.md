# Testing

> Status: Current workflow and future plan
> Implementation: Kernel metadata unit tests only

```bash
./tools/finn test
./tools/finn check
./tools/finn test-boot
./tools/finn test-exceptions
./tools/finn test-memory-map
./tools/finn test-page-allocator
./tools/finn test-page-tables
./tools/finn test-heap
./tools/finn test-timer-interrupts
./tools/finn test-cooperative-tasks
./tools/finn check-all
```

All integration tests except `test-page-allocator` run after timer activation; the allocator test intentionally exits before activation. `test-timer-interrupts` uses a dedicated image and validates real local-APIC delivery. Future categories include unit, kernel, integration, virtual-machine boot, driver conformance, fuzzing, fault injection, Peony visual, and update/recovery tests.
## Timer integration test

`./tools/finn test-timer-interrupts` builds a feature-specific kernel and waits
with `hlt` for at least eight periodic local-APIC timer deliveries. It checks
the independent 50 ms PIT frequency window, monotonic ticks, EOI, the spurious
return path, ABI call alignment, interrupt context, and real heap rejection in
simulated interrupt context. It never executes `int 0x40`; `int 0xff` is used
only for the spurious-dispatch test.

`./tools/finn test-cooperative-tasks` uses real guarded worker stacks and validates FIFO order, stack persistence, callee-saved registers, exit, reclamation, stale-ID rejection, idle execution, and timer continuity.
# Kernel preemption-context coverage

The host suite checks the 136-byte saved-register/software prefix, 176-byte raw
frame, bounded 0/4/8/12-byte alignment slack (188-byte maximum footprint), saved
RSP/SS tail, checked frame bounds, publication boundaries, overlap rejection,
phase capture freezing, and preemption nesting.
The dedicated QEMU suite adds real interrupts and timer delivery; it does not
replace the existing cooperative task test.
