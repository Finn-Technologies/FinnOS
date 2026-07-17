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
./tools/finn test-boot --target x86_64-qemu --profile release
./tools/finn test-boot --target arm64-qemu
```

`./tools/finn check-all` runs the default x86 doctor, quality gates, development image build, and all eight development-profile QEMU modes. Release and ARM64 first boot are separate required CI checks. An x86 QEMU success status is 33 because `isa-debug-exit` encodes the guest success value `0x10`. The ARM64 R3 test uses semihosting `SYS_EXIT_EXTENDED` and requires status 0 plus its smaller ordered loader/serial-entry marker contract. Each bounded QEMU command preserves `serial.log`, the image, staged ELFs, and `manifest.txt` under its `build/out/` directory.

## Verified results

The 2026-07-16 audit evidence covers the x86 commands. On 2026-07-17, the ARM64 R3 development smoke test passed locally on QEMU 11.0.2 with Homebrew AAVMF, reaching `FINNOS:KERNEL:ARM64_SERIAL_READY` and status 0. Its CI job is implemented but remains unverified until integration. The page-table test intentionally faults an unmapped address and the exception test intentionally executes invalid opcode; those faults are pass conditions only in their test images.

## Coverage and gaps

Current tests deeply cover one QEMU `q35`/OVMF/BSP path and only ARM64 serial entry. They do not cover ARM64 memory/exception/interrupt/timer/task parity, SMP, real hardware, alternate firmware, device I/O, userspace, persistent storage, networking, graphics, power failure, recovery, installation, updates, or security boundaries. There is no fuzzing, sanitizer/Miri program, code coverage, dependency audit, image-layout unit test, or reproducible-build comparison.

## CI plan

1. Preserve formatting, Clippy `-D warnings`, Rust/Python tests, cross-builds, and current boot modes as required checks.
2. Preserve the current failure-artifact upload, workflow concurrency, and explicit least-privilege permissions.
3. Keep actions pinned; additionally pin supported runner/toolchain/container versions and test the declared minimum Rust version.
4. Add loader/protocol fuzzing and malformed handoff/property tests before accepting untrusted boot inputs.
5. Verify the implemented ARM64 build and serial-first-boot jobs after integration, then add R4 parity tests incrementally.
6. Add SMP, alternate QEMU CPU/firmware, VirtIO device, userspace isolation, syscall, IPC, and fault-injection suites as milestones land.
7. Add filesystem crash/recovery, packet-stack conformance, Peony screenshot/accessibility, updater rollback, and release reproducibility tests before their maturity gates.

No subsystem may be marked complete solely because host unit tests pass. Each roadmap acceptance criterion names its execution-level verification.
