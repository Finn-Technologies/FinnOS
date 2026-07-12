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
./tools/finn check-all
```

`test-boot`, `test-exceptions`, and `test-memory-map` run after FinnOS page-table and heap activation. `test-page-allocator` intentionally exits before activation because it tests the physical allocator independently. `test-page-tables` uses a dedicated image and validates the owned root, permissions, guards, scratch mapping, unmapping, and page-fault test state. `test-heap` validates the global allocator and common Rust allocation types in a dedicated QEMU image. Future categories include unit, kernel, integration, virtual-machine boot, driver conformance, fuzzing, fault injection, Peony visual, and update/recovery tests.
