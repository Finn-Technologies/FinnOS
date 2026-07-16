# Testing FinnOS

## Required checks

```bash
./tools/finn check
./tools/finn test-boot
./tools/finn test-exceptions
./tools/finn test-memory-map
./tools/finn test-page-allocator
./tools/finn test-page-tables
./tools/finn test-heap
./tools/finn test-timer-interrupts
./tools/finn test-cooperative-tasks
```

`./tools/finn check-all` runs the doctor, quality gates, image build, and all eight QEMU modes. A QEMU success status is 33 because `isa-debug-exit` encodes the guest success value `0x10`.

## Verified results

On 2026-07-16, all commands above passed. Rust ran 63 unit tests (4 protocol, 5 loader, 54 kernel), Python ran 33 tests, and every QEMU mode produced the required ordered serial markers and test exit. The page-table test intentionally faults an unmapped address and the exception test intentionally executes invalid opcode; those faults are pass conditions only in their test images.

## Coverage and gaps

Current tests deeply cover one QEMU `q35`/OVMF/BSP path. They do not cover ARM64, SMP, real hardware, alternate firmware, device I/O, userspace, persistent storage, networking, graphics, power failure, recovery, installation, updates, or security boundaries. There is no fuzzing, sanitizer/Miri program, code coverage, dependency audit, image-layout unit test, or reproducible-build comparison.

## CI plan

1. Preserve formatting, Clippy `-D warnings`, Rust/Python tests, cross-builds, and current boot modes as required checks.
2. Upload serial logs, manifests, ELF files, and images on failure; add workflow concurrency and explicit least-privilege permissions.
3. Pin actions and supported toolchain/container versions; test the declared minimum Rust version.
4. Add loader/protocol fuzzing and malformed handoff/property tests before accepting untrusted boot inputs.
5. Add ARM64 build and serial-first-boot jobs as soon as code exists.
6. Add SMP, alternate QEMU CPU/firmware, VirtIO device, userspace isolation, syscall, IPC, and fault-injection suites as milestones land.
7. Add filesystem crash/recovery, packet-stack conformance, Peony screenshot/accessibility, updater rollback, and release reproducibility tests before their maturity gates.

No subsystem may be marked complete solely because host unit tests pass. Each roadmap acceptance criterion names its execution-level verification.
