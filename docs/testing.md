# Testing

Automated host suites, ASan/UBSan C++20 unit tests, VM verification, and honest gaps.

## Host Python suite (`make test`)

Runs `python3 -m unittest discover` in `tests/` (needs no display):
- `test_base.py` — base package manifests (`x86_64`, `aarch64`), boot chain & Plymouth `72×72` mark parity with OOBE, installer safety, branding guardrails, installed-app consistency.
- `test_oobe.py` — `tools/finn-oobe-apply` argument validation, marker writing, one-shot sudoers retirement, and OOBE completion checks.
- `test_ota.py` — RFC 8032 Ed25519 sign/verify, HTTP Range resume, `zstd` delta patching + fallback, staged rollout bucketing, and `systemd-boot`/`GRUB` A/B rollback simulation.
- `test_recovery_installer.py` — 15 end-to-end offline installer scenarios (`vm/finn-install.sh`, `vm/finn-chroot.sh`, GPT/MBR, UEFI/BIOS, dual-boot resize, A/B slots, reinstall, bootloader repair).
- `test_uefi_boot_harness.py` — live QEMU + EDK2 OVMF verification of `finn-bootx64.efi` GOP boot picker and systemd BLI EFI variables (skips or requires `qemu-system-x86_64` + OVMF).
- `test_icons.py`, `test_chroot_password.py`, `test_wine_prefix_path.py`, `test_downloadable_apps.py`, `test_release.py`, `test_x86_boot.py`.

## C++20 ASan/UBSan unit tests (`make test-unit`)

Built and executed under `-fsanitize=address,undefined` in `tests/unit/`:
- `test_compositor` — `SceneLayer`, `WorkspaceModel`, `InputRouter`
- `test_recovery` — `recovery_engine` (`lsblk`/parted parsing, SHA-512 verification, GPT/MBR & dual-boot planning)
- `test_bootloader` — `bls_parser` & `boot_ui` (BLS Type #1/#2, boot-counting, UKI extraction)
- `test_oobe` — 10-step `FullOobeFlow` state machine & capability gating
- `test_store` — curated catalog, Flathub/winget/Aurora provider parsing, store controller logic
- `test_app_registry` & `test_session_client` — 7-source app discovery & socket protocol
- `test_compat` & `test_windows_modules` — `.desktop`, Flatpak, AppImage, ELF probe, Wine prefix/MSI/PE/ICO/registry
- `test_darwin` — Mach-O loader, ObjC runtime, plist, Foundation/AppKit/Swift shims
- `test_ohos` — `.hap` ZIP reader, `module.json`, Panda `.abc` VM, ArkUI/NAPI
- `test_android` — APK/AXML/`resources.arsc` parser, DEX verifier/disassembler, installer
- `test_spring`, `test_animation`, `test_search`, `test_sys_files`, plus `test_tokens.py`

## Lint & ISO checks

```sh
make lint                 # git diff --check, generated_tokens.hpp freshness, zero app IDs in src/comp,
                          # Python compileall, and tools/check-branding.sh
base/mkfinniso --check    # ISO profile, boot menus, branding, and dual-unit parity (base/ vs airootfs/)
```

## Linux A/B OTA loopback test

```sh
sudo ota/verify-ab-linux.sh
```

Exercises real loopback ext4 A/B slots on both `SystemdBootBackend` and `GrubBootBackend` (upgrade + health-check commit, broken update + 3-boot rollback).

## VM & visual verification

On a running Linux VM (`vm/deploy.sh`):
- `finn-comp`, `finn-session`, `finn-shell`, and `finn-wallpaper` come up cleanly under systemd.
- `finn-session --list` and `finn-session --launch <id>` launch all 11 registry apps (`settings`, `files`, `terminal`, `notes`, `store`, `viewer`, `monitor`, `calculator`, `archive`, `capture`, optional `flourish`).
- `vm/shot.sh out.png` captures the live desktop for pixel verification.

## Honest testing gaps

- **Physical bare-metal DRM/KMS & `Qt6WaylandCompositor` runtime**: The rewritten Qt 6 compositor and QML shell have been verified via ASan/UBSan unit tests and Qt 6 offscreen rendering, with Linux VM testing ongoing; broad bare-metal GPU (Intel, AMD, NVIDIA) and multi-monitor hotplug matrices are untested.
- **Input hardware**: Real multi-touch touchscreens, Wacom/stylus tablets, and hardware suspend/resume cycles on bare metal are untested.
- **Real-disk A/B power-cycle rollback**: Verified on Linux loopback images (`ota/verify-ab-linux.sh`) and host tests, not yet across physical power-loss cycles on bare metal.
- **Third-party toolkit & guest coverage**: GTK3/GTK4/Electron/Steam/complex macOS/OpenHarmony/Android apps are only partially exercised; see the individual `docs/*-coverage.md` pages.
