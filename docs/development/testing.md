# Testing

> Status: Current workflow and future plan
> Implementation: Kernel metadata unit tests only

```bash
./tools/finn test
./tools/finn check
./tools/finn test-boot
./tools/finn test-exceptions
./tools/finn check-all
```

`test-boot` runs the normal First Boot smoke test; `test-exceptions` builds a separate image with the `qemu-test-exceptions` feature and verifies controlled breakpoint and invalid-opcode behavior. Future categories include unit, kernel, integration, virtual-machine boot, driver conformance, fuzzing, fault injection, Peony visual, and update/recovery tests. Crate-local tests should remain near their implementation.
