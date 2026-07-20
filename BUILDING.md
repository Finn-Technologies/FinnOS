# Building FinnOS

## Supported development environment

The verified x86 path uses Rust stable, Cargo, Python 3, QEMU x86-64, `qemu-img`, and x86-64 OVMF. The locally verified R3 path additionally uses QEMU AArch64 and AAVMF/EDK2. Linux additionally needs `mtools` and `dosfstools`; macOS image creation uses `hdiutil` and `qemu-img`.

The 2026-07-16 local audit used macOS on `aarch64-apple-darwin`, Rust/Cargo 1.97.1, Python 3.9.6, QEMU 11.0.2, and Homebrew OVMF. CI uses unpinned `ubuntu-latest`, Rust stable, distro QEMU/OVMF, `mtools`, and `dosfstools`. The workspace declares Rust 1.85 as its minimum, but that minimum was not independently reverified during the audit.

## Setup

```bash
rustup target add x86_64-unknown-none x86_64-unknown-uefi \
  aarch64-unknown-none aarch64-unknown-uefi
./tools/finn doctor
```

Environment overrides:

- `FINNOS_QEMU_X86_64`: QEMU executable
- `FINNOS_QEMU_AARCH64`: QEMU AArch64 executable
- `FINNOS_QEMU_IMG`: `qemu-img` executable
- `FINNOS_OVMF_CODE`: OVMF code firmware image
- `FINNOS_AAVMF_CODE`: AAVMF/EDK2 AArch64 code firmware image
- `FINNOS_BOOT_TIMEOUT_SECONDS`: integration-test timeout, default 45

## Commands

```bash
./tools/finn build        # development host workspace
./tools/finn check        # format, build, Clippy, Rust and Python tests
./tools/finn build-boot   # development kernel + UEFI loader + staged ESP
./tools/finn image        # 64 MiB FAT32 image
./tools/finn run          # graphical QEMU, no functional input
./tools/finn run-headless # serial QEMU until manually stopped
```

Build commands accept validated target and profile selections:

```bash
./tools/finn build --profile release
./tools/finn build-boot --target x86_64-qemu --profile release
./tools/finn image --target x86_64-qemu --profile release
./tools/finn test-boot --target x86_64-qemu --profile release
./tools/finn doctor --target arm64-qemu
./tools/finn test-boot --target arm64-qemu
```

`development` is the default profile. `Finnfile.toml` selects a target configuration under
`build/targets/`; target metadata supplies Cargo targets, artifact names, and QEMU details.
`arm64-qemu` implements R3 serial first boot only. Commands for x86-only exception,
memory, timer, and task modes reject it clearly rather than implying R4 parity.

## Outputs

The normal image flow creates:

```text
build/out/x86_64-qemu/esp/EFI/BOOT/BOOTX64.EFI
build/out/x86_64-qemu/esp/EFI/FINNOS/KERNEL.ELF
build/out/x86_64-qemu/finnos-x86_64-uefi.img
build/out/x86_64-qemu/manifest.txt
```

The corresponding ARM development paths use `build/out/arm64-qemu/` and
`build/out/arm64-qemu-test/`, with `BOOTAA64.EFI` and `finnos-arm64-uefi.img`.

Release output uses `build/out/x86_64-qemu-release/`; feature-specific tests insert their
mode before the profile, such as `build/out/x86_64-qemu-test-release/`. Every bounded QEMU
test writes `serial.log` beside its image and manifest.

`manifest.txt` hashes the kernel and loader but not the final image and does not record the source revision, compiler, dependencies, or provenance. Builds are not claimed reproducible.

## Known setup failures

- `doctor` exits nonzero if a Rust target, QEMU tool, or selected firmware cannot be found.
- OVMF/AAVMF paths vary by distribution; use the documented firmware override when discovery fails.
- Configuration errors name unknown targets/profiles, metadata drift, and R3-incompatible ARM test modes.
- Failed host image commands report the command and captured output.
- A successful build only proves compilation. Run QEMU tests to verify kernel behavior.
