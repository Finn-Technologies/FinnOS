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
