# x86-64 Debug Build and Boot Runbook

This runbook describes the only executable FinnOS target currently verified: x86-64 QEMU `q35` with OVMF, debug profile. It does not establish release-image, ARM64, or physical-hardware support.

## Command Classes

- Read-only capture: `python3 .agents/scripts/capture_state.py`, Git status/log, version commands.
- Host-mutating setup: `rustup target add x86_64-unknown-none x86_64-unknown-uefi` only when `doctor` reports them missing.
- Build/image mutation: `./tools/finn build`, `build-boot`, and `image` write ignored `target/` and `build/out/` artifacts.
- Bounded runtime: every `test-*` command defaults to a 45-second QEMU timeout.
- Unbounded interactive runtime: `run` and `run-headless`; stop them manually and do not use them as CI gates.
- Aggregate gate: `./tools/finn check-all`; it performs builds and all eight QEMU tests.

## Baseline

```bash
python3 .agents/scripts/capture_state.py
./tools/finn doctor
./tools/finn check
./tools/finn image
./tools/finn test-boot
```

`doctor` requires Git, Cargo/Rust/rustfmt, Python 3, QEMU x86-64, `qemu-img`, both Rust targets, repository inputs, and discoverable OVMF. `Cargo.toml` declares Rust 1.85; the audit verified Rust 1.97.1, Python 3.9.6, and QEMU 11.0.2 but did not establish minimum Python/QEMU versions.

Common host setup examples, reviewed before execution because they modify the host:

```bash
# macOS with Homebrew
brew install qemu

# Ubuntu/Debian CI-equivalent packages
sudo apt-get update
sudo apt-get install -y qemu-system-x86 ovmf mtools dosfstools
```

Other distributions must provide equivalent QEMU, `qemu-img`, OVMF, FAT, and image tools. Unknown minimum QEMU/Python versions remain a documented reproducibility gap; capture exact versions instead of claiming a wider range.

Environment overrides implemented in `tools/finnlib/toolchain.py`:

- `FINNOS_QEMU_X86_64`
- `FINNOS_QEMU_IMG`
- `FINNOS_OVMF_CODE`
- `FINNOS_BOOT_TIMEOUT_SECONDS`

## Artifacts

Normal image:

```text
build/out/x86_64-qemu/manifest.txt
build/out/x86_64-qemu/esp/EFI/BOOT/BOOTX64.EFI
build/out/x86_64-qemu/esp/EFI/FINNOS/KERNEL.ELF
build/out/x86_64-qemu/finnos-x86_64-uefi.img
target/x86_64-unknown-none-normal/x86_64-unknown-none/debug/finn-kernel-x86_64
target/x86_64-unknown-uefi/debug/finn-boot-x86_64.efi
```

Test modes rebuild the matching kernel/loader as needed and overwrite files in their own ignored output directory. They do not clean other modes. Never symbolize against another mode/profile ELF. The wrapper prints serial output but does not persist a standalone log. Preserve failures under ignored `build/evidence/<task-or-timestamp>/` by redirecting complete stdout/stderr and copying the exact manifest/image/ELFs before another build; record those paths in the handoff.

## Eight Runtime Gates

```bash
./tools/finn test-boot
./tools/finn test-exceptions
./tools/finn test-memory-map
./tools/finn test-page-allocator
./tools/finn test-page-tables
./tools/finn test-heap
./tools/finn test-timer-interrupts
./tools/finn test-cooperative-tasks
```

The exact ordered required and forbidden marker contracts are executable source in `tools/finnlib/qemu.py`. The main boot stages are loader start/kernel found/valid/loaded/framebuffer/ExitBootServices, kernel entry and descriptor/exception readiness, BootInfo and memory, allocator, private paging, heap, PIC/APIC/timer, scheduler/task stacks, framebuffer, and `FINNOS:KERNEL:FIRST_BOOT_COMPLETE`.

| Command | Mode directory suffix |
|---|---|
| `test-boot` | `x86_64-qemu-test` |
| `test-exceptions` | `x86_64-qemu-exceptions` |
| `test-memory-map` | `x86_64-qemu-memory-map` |
| `test-page-allocator` | `x86_64-qemu-page-allocator` |
| `test-page-tables` | `x86_64-qemu-page-tables` |
| `test-heap` | `x86_64-qemu-heap` |
| `test-timer-interrupts` | `x86_64-qemu-timer-interrupts` |
| `test-cooperative-tasks` | `x86_64-qemu-cooperative-tasks` |

Each contains `manifest.txt`, staged `esp/`, and `finnos-x86_64-uefi.img`; the kernel target directory uses the same final suffix under `target/x86_64-unknown-none-<mode>/`. `./tools/finn check` rebuilds/checks host workspace artifacts and runs Rust/Python tests, including agent infrastructure tests; it does not run QEMU. Every `test-*` command builds its feature image, creates/overwrites that mode's FAT image, and runs bounded QEMU validation.

The x86 `isa-debug-exit` guest success value `0x10` appears to the Python harness as process status 33. The wrapper returns shell status 0 only when marker and status validation passes. Status 33 is an internal QEMU child result, not a shell command success code and not an ARM64 contract.

## Failure Evidence

Before rebuilding, preserve the exact image, manifest, boot manager, kernel ELF, complete printed QEMU command, OVMF/QEMU versions, full serial output, child status, timeout, first missing/out-of-order marker, and forbidden panic/error marker. Classify the first divergence as image, firmware, loader, handoff, entry, memory/paging, interrupt/scheduler, or harness. Use `fixing-boot-failure` for the decision tree.
