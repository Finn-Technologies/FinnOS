# QEMU development

> Status: Implemented for x86-64 UEFI QEMU
> Implementation: Boot and integration tests run in QEMU with OVMF

`tools/finn` discovers `FINNOS_OVMF_CODE` first, then standard Homebrew and Linux OVMF paths. `FINNOS_QEMU_X86_64` overrides QEMU and `FINNOS_BOOT_TIMEOUT_SECONDS` controls the smoke-test timeout, defaulting to 45 seconds.

An x86-64 guest on Apple silicon must use software emulation; the command must not request KVM or HVF. Homebrew’s code-only OVMF image is passed as a read-only pflash drive. Serial is attached to stdio. Test mode adds `isa-debug-exit` at port `0xf4`: value `0x10` maps to host status 33 and `0x11` maps to 35. `./tools/finn test-exceptions` builds a separate image under `build/out/x86_64-qemu-exceptions/` and verifies controlled breakpoint and invalid-opcode behavior. Manual runs keep the display open and can be stopped with Ctrl+C.

`./tools/finn test-page-allocator` builds an isolated image under
`build/out/x86_64-qemu-page-allocator/` and validates allocation, reuse,
deallocation, double-free rejection, allocator invariants, and QEMU status
33. It intentionally completes before page-table activation because it tests
the allocator independently.

`./tools/finn test-page-tables` builds an isolated image under
`build/out/x86_64-qemu-page-tables/` and validates the FinnOS-owned root,
mapping permissions, null-page protection, stack guard pages, scratch mapping
and unmapping, and real vector-14 page-fault delivery.

`./tools/finn test-heap` builds an isolated image under
`build/out/x86_64-qemu-heap/` and validates heap mapping, guard pages, direct
alignment, `Box`, fallible `Vec`, `String`, fragmentation, exhaustion, reuse,
statistics, and allocator invariants. All test images use QEMU status 33 for
success; the heap test never performs allocation from interrupt context.
# Local APIC timer evidence

QEMU uses the existing q35/UEFI path. The timer test requires status 33 and
numeric serial evidence for the APIC base, PIT reference, calibrated APIC
count, and at least eight increasing ticks. No software timer device or
synthetic clock is used.

# Cooperative task evidence

`./tools/finn test-cooperative-tasks` uses isolated target and image directories. Status 33 is accepted only after exact worker ordering, register preservation, reclamation, generation reuse, a real idle probe, and timer continuity validate.
# Preemption-context test

Run `./tools/finn test-preemption-context` to exercise the real 0x41 software
interrupt, real APIC timer delivery, bootstrap/worker/idle stack attribution,
full-GPR preservation, the 184-byte saved-RSP/SS frame tail, phase-frozen
snapshots, and deferred request behavior. The validator requires status 33,
ordered markers, exact pre/saved/post RSP equality, complete-frame evidence,
unchanged CR3, and no task switch from the timer handler.
