# Hardware

Verified virtualized environments and honest bare-metal hardware limits.

## Supported architectures

- **`x86_64`** — Primary target: live ISO (`bios.syslinux` + `uefi.systemd-boot`), `finn-bootx64.efi`, Wine/Proton/DXVK/VKD3D, VMware/VirtualBox/QEMU guest tools, and GitHub Actions runtime/ISO builds.
- **`aarch64`** — Secondary target: live ISO (`uefi.systemd-boot`), `finn-bootaa64.efi`, Apple Silicon Lima/QEMU dev VM, and optional Hangover ARM64 WoW64 (`scripts/install-hangover-arm64.sh`).

32-bit `x86` and 32-bit `arm` are not supported (`base/packages.x86` and `base/packages.arm` are compatibility symlinks only).

## Verified environments

| Environment | Graphics / Display | Notes |
|---|---|---|
| **VMware Workstation / Fusion (`x86_64`)** | `vmwgfx` (SVGA3D with 3D enabled, or software fallback) | `open-vm-tools` pre-installed in `packages.x86_64`; see [docs/guest-tools.md](guest-tools.md) |
| **QEMU / KVM + Lima (`aarch64` & `x86_64`)** | `virtio_gpu` / `virgl` / software | Primary dev VM (`vm/lima.yaml`); QEMU + EDK2 OVMF verifies `finn-bootx64.efi` |
| **VirtualBox (`x86_64`)** | `vboxvideo` / VMSVGA | `virtualbox-guest-utils-nox` pre-installed in `packages.x86_64` |
| **macOS Host (preview / offscreen)** | Qt 6 offscreen (`--preview`) | Runs host Python tests, C++20 ASan/UBSan unit tests, and QML visual verification |

## Honest bare-metal & hardware limits

- **Bare-metal GPUs (Intel, AMD, NVIDIA)**: Not broadly verified on physical machines yet. `finn-comp` relies on Qt 6 QPA (`eglfs` / `linuxfb`); proprietary NVIDIA GBM/EGLStreams quirks are untested.
- **Multi-monitor & mixed HiDPI**: No graphical display arrangement editor or mixed per-output fractional scaling yet.
- **Touchscreens, styluses & mobile radios**: `InputRouter` handles touch/gesture events in software, but physical touchscreens, pen tablets, and cellular modems (`ModemManager`) are untested/unimplemented.
- **Power & suspend/resume**: `LockScreenOverlay.qml` listens for `logind` `PrepareForSuspend`, and `Settings` reads real sysfs backlight/battery and `NetworkManager`/`BlueZ`/`PipeWire`, but physical laptop S0ix/S3 suspend-resume reliability is not yet verified across hardware models.
- **Audio, cameras & printers**: PipeWire audio works via `wpctl` in `Settings`; camera/webcam privacy controls and CUPS printer management are not yet built into Settings.
