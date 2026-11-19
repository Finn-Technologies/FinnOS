# Running FinnOS in emulators, and simulating FinnOS apps

This page describes the **real** ways to run FinnOS or a FinnSDK app without
dedicated hardware, and the limits of each. The probe helper is
[`tools/finn-emulator`](../tools/finn-emulator); the dev-VM overview is
[`vm/README.md`](README.md); the hardware limits are in
[`docs/hardware.md`](../docs/hardware.md).

"Emulated" never means "verified on hardware". A QEMU GPU is not a laptop GPU,
and a simulated phone viewport is not a phone.

---

## 1. Backend capability matrix

| Host | Backend | Guest / workload | Works today | Does not work |
|---|---|---|---|---|
| Linux x86_64 | QEMU system (`qemu-system-x86_64`), KVM if `/dev/kvm`, else TCG | x86_64 FinnOS ISO, BIOS (syslinux) or UEFI (OVMF) install and live shell | **yes** — CI builds and boots the ISO (`iso.yml:63-284`, `tests/test_x86_boot.py`) | no host GPU frame pacing, no USB device realism beyond QEMU |
| Linux x86_64 / macOS x86_64 | VMware Workstation, `vm/create-dev-vm.sh` | x86_64 FinnOS ISO | **yes (dev)** — creates a `.vmx` with BIOS boot, NAT, 4 GB/2 vCPU (`create-dev-vm.sh:29-58`) | VMware UEFI exposes no usable variables for the ESP, so the VM is deliberately BIOS-only (`:38-43`) |
| Linux x86_64 | headless compositor (`WLR_BACKENDS=headless WLR_RENDERER=pixman finn-comp`) | Wayland clients, screenshots with `grim` | **yes** — `docs/hardware.md:10-11`, `docs/sdk/simulator.md:45` | no DRM/input device; not a full OS boot |
| Apple Silicon (arm64) | Lima + QEMU (`vm/lima.yaml`, HVF acceleration) | aarch64 Arch Linux dev VM with `virtio-gpu` DRM, seatd, full session | **yes (dev VM)** — `vm/lima-setup.sh`, `vm/deploy.sh`, `vm/shot.sh` (`vm/README.md:17-49`) | no aarch64 ISO is built or boot-tested in CI; `vz` is avoided because the 2022 cloud image dies under it (`lima.yaml:5-7`) |
| Apple Silicon / Linux ARM64 | `qemu-system-aarch64 -M virt -cpu cortex-a57` | aarch64 guest with a supplied kernel + initramfs | **partial** — emulator usable, but FinnOS publishes no aarch64 ISO/initramfs pair | no install medium, no boot test in CI |
| Linux/macOS with Qt6 | Qt `offscreen` platform plugin | FinnSDK widget rendering, theme/tokens, form-factor presets | **yes (app-level)** — see §4 | no compositor, Wayland shell, touch, gestures or IME |
| x86_64 host | `qemu-aarch64-static` | aarch64 userland only | **not shipped** | cannot boot a kernel or run `finn-comp` |
| Any host | `tools/finn-emulator` | probes all of the above and reports honestly | **yes** (probe only) | never starts or changes the running Lima VM; `boot` prints a command |

Run `tools/finn-emulator check` and `tools/finn-emulator backends` to see the
status on the current host rather than trusting a table.

---

## 2. x86_64 hosts

### 2.1 Boot the FinnOS ISO under QEMU

The ISO is built by `base/mkfinniso` and is bootable on its own — live
environment, installer and payload, no stock-Arch fallback disc
(`vm/create-dev-vm.sh:4-11`). On a Linux host with `qemu-system-x86_64` and
OVMF:

```sh
sudo base/mkfinniso "$PWD/finnos.iso"          # or use the CI artifact
qemu-system-x86_64 -m 4096 -smp 2 -accel kvm \
  -cdrom finnos.iso \
  -netdev user,id=n0 -device e1000,netdev=n0 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/ovmf/x64/OVMF.fd
# Without /dev/kvm: replace -accel kvm with -accel tcg (slow).
# Without OVMF: drop the -drive line to boot the BIOS/syslinux path.
```

`tools/finn-emulator boot --arch x86_64 --iso finnos.iso` prints this command
with the paths filled in, and refuses if QEMU is missing. CI additionally
boots both paths with `python3 -m unittest tests.test_x86_boot.QemuBootTest -v`
and captures the menus with `vm/menu-shot.sh` (`iso.yml:248-264`).

### 2.2 VMware Workstation

`vm/create-dev-vm.sh [vm-name] [finnos-iso]` writes a `.vmx` for VMware
Workstation (no Parallels dependency) with `firmware = "bios"`, CDROM first in
the boot order and a 32 GB LSILogic disk. If `vmware-vdiskmanager` is absent it
prints an honest message asking you to create the disk in the UI
(`create-dev-vm.sh:60-67`).

### 2.3 Headless compositor

```sh
WLR_BACKENDS=headless WLR_RENDERER=pixman XDG_RUNTIME_DIR=/run/user/1000 \
  WAYLAND_DISPLAY=finn-headless-0 finn-comp &
grim -o <output> shot.png
```

Useful for compositor/shell verification without a DRM device; this is the mode
`finn-sdk simulate --headless` and `tools/finn-emulator simulate --headless`
use on an isolated socket (the latter creates a temporary `XDG_RUNTIME_DIR` and
a `finn-sim-<pid>` socket). It is not a full OS boot and has no physical input.

---

## 3. ARM64 hosts

The supported ARM64 path today is the **Lima dev VM**, not an ARM64 install ISO.
`vm/lima.yaml` pins:

- `vmType: qemu`, `arch: aarch64`, 4 CPUs, 6 GiB RAM, 30 GiB disk;
- an upstream `aarch64` Arch Linux cloud image with a pinned SHA-512 digest;
- no host mounts: `vm/deploy.sh` sends the tree as a tar stream;
- `video.display: cocoa`, because Lima only adds `virtio-gpu` (the DRM device
  `finn-comp` drives on tty1) when there is a display — `display: none` gives a
  VM with no `/dev/dri` and a compositor that cannot start (`lima.yaml:18-25`);
- `WLR_RENDERER=pixman` via a `finn-comp.service.d` drop-in, because QEMU's
  llvmpipe GLES2 path shows stale blocks that `grim` does not capture
  (`vm/README.md:46-49`).

```sh
brew install lima qemu                 # macOS host
vm/lima-setup.sh                       # create 'finnos', upgrade image, deploy, start session
export FINN_HOST=lima-finnos FINN_SSH_OPTS="-F $HOME/.lima/finnos/ssh.config"
vm/deploy.sh                           # rebuild/redeploy after changes
vm/shot.sh out.png                     # proof: a real frame, not a compile result
```

Running `vm/lima-setup.sh` again only redeploys. The session in this VM is
`finn-comp` as root on tty1 through seatd, with `finn-session` and `finn-shell`
as user `finn` (`vm/README.md:51-62`). **Do not touch the running VM when only
probing backends** — `tools/finn-emulator` never does.

For a bare QEMU aarch64 guest (advanced; no FinnOS ISO exists):

```sh
qemu-system-aarch64 -M virt -cpu cortex-a57 -m 4096 -smp 4 \
  -kernel <aarch64-Image.gz> -initrd <initramfs.img> \
  -append "console=ttyAMA0 quiet splash vt.global_cursor_default=0" \
  -device virtio-gpu-pci -nographic
```

`base/mkfinniso --arch=aarch64` validates the ARM profile tree and CI runs that
check, but no aarch64 image is built or booted by CI
(`.github/workflows/iso.yml:43`). See `docs/platform-tooling.md:1.3` for the
concrete CI targets that would close this gap.

---

## 4. FinnOS app simulator (Qt offscreen)

This runs **an app**, not the OS. It is the fastest way to check widget layout,
Finn theming and viewport presets on any host that has Qt6 + a built FinnSDK
app. On a Linux build host with `qt6-base` (which ships
`platforms/libqoffscreen.so`):

```sh
finn-sdk build sdk/template                        # produces build/finn-<app>
QT_QPA_PLATFORM=offscreen \
QT_QPA_PLATFORMTHEME=finn QT_STYLE_OVERRIDE=finn \
FINN_FORM_FACTOR=phone FINN_SIM_WIDTH=390 FINN_SIM_HEIGHT=844 FINN_SIM_SCALE=2 \
  sdk/template/build/finn-<app>
```

The one-command form does the build and sets the same variables:

```sh
tools/finn-emulator simulate --app sdk/template --form-factor phone --dry-run
tools/finn-emulator simulate --app sdk/template --form-factor tablet
```

`--dry-run` prints the resolved environment and command without building or
launching. The default launch uses `QT_QPA_PLATFORM=offscreen`; `--headless`
instead starts `finn-comp` with `WLR_BACKENDS=headless` and runs the app as a
Wayland client (requires `finn-comp` in `PATH`). Presets come from
`docs/sdk/simulator.md:9-13`: phone 390×844 @2×, tablet 768×1024 @2×, desktop
1440×900 @1× by default.

**Honest limits:**

- Offscreen mode has no compositor, no layer shell, no Wayland input, no
  touch/gestures, no IME and no real screen scale; the shell and window chrome
  are not the compositor's.
- `FinnWindow` does not yet auto-resize from `FINN_FORM_FACTOR`; the simulation
  only sets the environment — see `docs/sdk/simulator.md:43-48`.
- No `FinnTest::Harness` or shipped screenshot helper exists for third-party
  widgets (`docs/sdk/audit.md:68`); capture a widget with Qt's own
  `QWidget::grab()` in the app if you need a frame.
- On the macOS editor host the offscreen plugin is only *presumed* from
  `qt6-base`; `tools/finn-emulator check` marks it `partial` when `qmake` is
  unavailable and never claims it is confirmed.

---

## 5. What no backend here can emulate

- ARM64 macOS binaries, Metal or SwiftUI (no Apple SDK/frameworks).
- Modem, SIM, telephony, SMS — no `ModemManager`/ofono stack
  (`docs/roadmap.md:43`).
- Touch gestures, pinch/zoom and the on-screen keyboard (`zwp_text_input_v3`).
- Real DRM GPU frame pacing, multi-monitor and per-output scaling.
- Secure Boot, `dm-verity` and TPM2 verified boot.

A simulator run therefore never satisfies the smartphone exit criteria in
`docs/platform-tooling.md:2.5`. Use the emulator to develop and to reproduce
bugs; verify changes on the device or the dev VM and say explicitly which one
you used.
