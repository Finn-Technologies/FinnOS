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
./tools/finn test-exceptions --target arm64-qemu
./tools/finn test-arm64-exception-fatal --target arm64-qemu
./tools/finn test-memory-map --target arm64-qemu
./tools/finn test-page-tables --target arm64-qemu
./tools/finn test-arm64-gic --target arm64-qemu
```

`./tools/finn check-all` runs the default x86 doctor, quality gates, development image build, and all eight development-profile QEMU modes. Release and the ARM64 serial, exception, fatal-diagnostic, memory-map, page-table, and GIC modes are separate required CI checks. An x86 QEMU success status is 33 because `isa-debug-exit` encodes the guest success value `0x10`. ARM64 nonfatal tests use semihosting `SYS_EXIT_EXTENDED` status 0; the isolated fatal diagnostic test requires status 1. Each mode also requires its ordered marker contract and preserves `serial.log`, the image, staged ELFs, and `manifest.txt` under its `build/out/` directory.

## Verified results

The 2026-07-16 audit evidence covers the x86 commands. R3 ARM64 first boot is integrated with local and CI evidence recorded in its handoff. On 2026-07-18, the R4.1-R4.3 worktree passed ARM64 first boot, controlled/fatal exception modes, memory-map/allocator, and owned-page-table modes locally on QEMU 11.0.2 with Homebrew AAVMF. The paging run read back TTBR0/TCR/MAIR/SCTLR, kept TTBR1 disabled, and resumed exactly four armed null-read, guard-read, text-write, and data-execute aborts before status 0. On 2026-07-20, the isolated R4.4 GICv2 mode delivered SGI 1 through the real current-EL IRQ vector, acknowledged IAR token 1, wrote that exact token once to EOIR, restored interrupt depth and frame state, and exited status 0. Worktree CI remains unverified until integration. Expected faults and IRQs are pass conditions only in their isolated test images.

## Coverage and gaps

Current tests deeply cover one QEMU `q35`/OVMF/BSP path and bounded ARM64 serial, synchronous-exception, early-memory, owned-MMU, and pinned GICv2 self-SGI paths. They do not cover broader ARM fault recovery, FIQ/SError, generic timer IRQs, external interrupt routing, GIC discovery/GICv3, task parity, SMP, real hardware, alternate firmware, device I/O, userspace, persistent storage, networking, graphics, power failure, recovery, installation, updates, or security boundaries. There is no fuzzing, sanitizer/Miri program, code coverage, dependency audit, image-layout unit test, or reproducible-build comparison.

## CI plan

1. Preserve formatting, Clippy `-D warnings`, Rust/Python tests, cross-builds, and current boot modes as required checks.
2. Preserve the current failure-artifact upload, workflow concurrency, and explicit least-privilege permissions.
3. Keep actions pinned; additionally pin supported runner/toolchain/container versions and test the declared minimum Rust version.
4. Add loader/protocol fuzzing and malformed handoff/property tests before accepting untrusted boot inputs.
5. Verify the ARM64 R4.1-R4.4 jobs after integration, then add generic-timer and task-context parity tests incrementally.
6. Add SMP, alternate QEMU CPU/firmware, VirtIO device, userspace isolation, syscall, IPC, and fault-injection suites as milestones land.
7. Add filesystem crash/recovery, packet-stack conformance, Peony screenshot/accessibility, updater rollback, and release reproducibility tests before their maturity gates.

No subsystem may be marked complete solely because host unit tests pass. Each roadmap acceptance criterion names its execution-level verification.
