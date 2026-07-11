# Testing

> Status: Current workflow and future plan
> Implementation: Kernel metadata unit tests only

```bash
./tools/finn test
./tools/finn check
./tools/finn test-boot
./tools/finn test-exceptions
./tools/finn test-memory-map
./tools/finn check-all
```

`test-boot` runs the normal First Boot smoke test; `test-exceptions` builds a separate image with the `qemu-test-exceptions` feature and verifies controlled breakpoint and invalid-opcode behavior; `test-memory-map` builds a separate image with the `qemu-test-memory-map` feature and verifies that the kernel parses, classifies, and summarizes the UEFI memory map. Future categories include unit, kernel, integration, virtual-machine boot, driver conformance, fuzzing, fault injection, Peony visual, and update/recovery tests. Crate-local tests should remain near their implementation.
