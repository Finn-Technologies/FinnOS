# Platform tooling: x86_64 / ARM64 build matrix and phone requirements

This page records **what is genuinely supported today**, what is only partially
wired, and what would have to be built for FinnOS to run on a smartphone. It is
a companion to [`hardware.md`](hardware.md) (verified environments),
[`testing.md`](testing.md) (what the suite actually covers),
[`roadmap.md`](roadmap.md) (planned work) and
[`ota/package-format.md`](../ota/package-format.md) (release artifacts).

Status words: **shipped** (in the tree and exercised), **partial** (code exists
but not end-to-end verified), **absent** (does not exist — no plausible default
may be substituted).

---

## 1. Build and target matrix

| Host | Target / artifact | Backend | Status | Evidence |
|---|---|---|---|---|
| Linux x86_64 (GitHub runner, pinned base-devel CI container) | native runtime, compositor, session, shell, apps | native `make -j` | **shipped** | `.github/workflows/ci.yml:27-53`; `make test`, `make test-unit`, `make lint`, `check-branding.sh` |
| Linux x86_64 | install ISO + installer, BIOS and UEFI boot | QEMU (`qemu-system-x86_64`, OVMF), `tests/test_x86_boot.py` | **shipped** | `.github/workflows/iso.yml:63-284`; `vm/create-dev-vm.sh` targets VMware Workstation x86_64 |
| Linux x86_64 | x86_64 OTA slot image + signed manifest | `ota/publish --arch x86_64` | **shipped (publisher)**; no CI builds the image | `ota/publish:65-66`, `.github/workflows/ota-release.yml:19-26` |
| Linux x86_64 | aarch64 cross-built image | — | **absent** | no cross toolchain or image job in CI; only `base/mkfinniso --check --arch=aarch64` (`iso.yml:43`) validates the ARM profile tree |
| Apple Silicon (Darwin arm64) | aarch64 Linux dev VM | Lima + QEMU (HVF), `virtio-gpu` DRM, `WLR_RENDERER=pixman` | **shipped (dev VM only)** | `vm/lima.yaml:8-28`, `vm/README.md:17-49`, `vm/lima-setup.sh`, `vm/deploy.sh` |
| Apple Silicon / Linux ARM64 | aarch64 install ISO | `base/mkfinniso --arch=aarch64` profile | **partial** | profile and `--check` exist (`base/mkfinniso:7,59,87-91,461,617,628`); no CI image build or boot test |
| Linux aarch64 | aarch64 OTA slot image + signed manifest | `ota/publish --arch aarch64` | **shipped (publisher)**; no CI image | `ota/verify-ab-linux.sh` executed on Lima aarch64 and recorded in `docs/ota.md:266-303` |
| macOS host (x86_64 or ARM64) | any FinnOS build | — | **absent** | the host is the editor; the VM is the build machine (`AGENTS.md`, `vm/README.md:1-4`). No pacman/wlroots0.20/Qt6/seatd build host on macOS |
| Any host | x86_64 guest under emulation on ARM64 | `qemu-system-x86_64` TCG | **partial** | boots, but slow and no GPU; probed by `tools/finn-emulator check` |
| Any host | aarch64 userland on x86_64 | `qemu-aarch64-static` | **absent in tree** | may be installed externally; runs userland only, never a kernel/compositor |

### 1.1 Toolchain that is actually used

Both `base/packages.x86_64` and `base/packages.aarch64` are Arch package lists
(`base/packages.x86` and `base/packages.arm` are symlinks checked by
`.github/workflows/iso.yml:44-49`). The build uses CMake + Ninja + `g++`
(C++20), `wlroots0.20`, `wayland`, `wayland-protocols`, `libxkbcommon`,
`libdrm`, `libinput`, `mesa`, `seatd`, Qt6 (`qt6-base`, `qt6-wayland`,
`qt6-svg`, `qt6-declarative`, `layer-shell-qt`) and `xorg-xwayland`
(`ci.yml:39-43`). `libfinnui` ships `libfinnui.so.0.2.0` with `SOVERSION 0`
and the `FinnSDK::FinnUI` CMake target (`docs/sdk/audit.md:12-18`).

No separate cross-sysroot exists. A genuine ARM64 build today means building
**on** an aarch64 machine (the Lima VM), not cross-compiling from x86_64.

### 1.2 Architecture-specific compatibility realities

- **`finn-darwin` (Mach-O / macOS apps):** runs unmodified 64-bit Mach-O apps
  natively **only on the matching CPU architecture** — arm64 on aarch64 and
  x86_64 on x86_64. It uses per-arch `objc_msgSend` assembly and has no Rosetta
  class translator. SwiftUI, the full Swift standard library, Metal/MetalKit,
  WebKit and CoreData are explicitly unimplemented; `finn-darwin diagnose`
  reports every missing framework and symbol before launch
  (`docs/architecture.md:148`, `docs/darwin-framework-coverage.md:105-117`).
  A real `swiftc`-compiled "hello world" is diagnosed, not run
  (`darwin-framework-coverage.md:170-171`).
- **Swift / Metal on x86_64 FinnOS:** **absent**. There is no Swift compiler,
  no Metal shader compiler/device, no Apple SDK and no `xcrun` in either
  package list. The "core Swift ABI" in `src/darwin` is a set of low-level
  runtime shims (heap/ARC/once), not a language toolchain. Do not list Swift
  or Metal as a supported FinnSDK language or graphics backend.
- **Wine on ARM64:** **absent** as a native package; `finn::inspect_pe` reads
  PE headers and FinnOS honestly reports Wine unavailable when `wine`/`wine64`
  is missing (`docs/hardware.md:17-20`). No x86-on-ARM emulation layer is
  shipped.
- **OpenHarmony (`finn-ohos`):** `.hap`/`.hsp`/`.app` installation and the
  ArkUI-on-Qt6 bridge exist, but hardware/distributed kits (ArkWeb,
  telephony, NFC, camera, sensors) and signature enforcement are absent
  (`docs/architecture.md:149`, `docs/ohos-kit-coverage.md`).

### 1.3 Concrete CI targets (PROPOSED — not present in the repo today)

These jobs close the gaps above without inventing capabilities. Each is a
separate target so a failure names the platform that broke.

```yaml
# Proposed additions; .github/workflows/ci.yml and iso.yml are the current files.
jobs:
  native-x86_64:        # exists today as ci.yml jobs `tests` + `build`
    runs-on: ubuntu-latest
    container: "$FINN_CI_BASE_IMAGE"   # base-devel CI image; tag pinned in the workflow
    steps: [{ run: "make -j$(nproc) && make test && make test-unit && make lint" }]
  native-aarch64:       # NEW: native runner, not cross-compile
    runs-on: ubuntu-24.04-arm        # or self-hosted ARM64
    container: "$FINN_CI_BASE_IMAGE"   # base-devel CI image; tag pinned in the workflow  # aarch64 tag
    steps: [{ run: "make -j$(nproc) && make test && make test-unit" }]
  iso-x86_64:           # exists today as iso.yml job `iso`
    steps: [{ run: "base/mkfinniso finnos.iso && tests.test_x86_boot.QemuBootTest" }]
  iso-aarch64:          # NEW: build + boot an ARM image
    runs-on: ubuntu-24.04-arm
    steps:
      - { run: "base/mkfinniso --arch=aarch64 out/finnos-aarch64.iso" }
      - { run: "qemu-system-aarch64 -M virt -cpu cortex-a57 ... -kernel Image.gz -initrd initramfs.img" }
  ota-dual-arch:        # NEW: publish both arches into one staging tree
    needs: [iso-x86_64, iso-aarch64]
    steps:
      - { run: "for a in x86_64 aarch64; do ota/publish --arch $a --image ...; done" }
      - { run: "python3 -m unittest -v tests/test_ota.py" }
  sdk-artifacts:        # current SDK CI builds x86_64 unsigned only
    strategy: { matrix: { arch: [x86_64, aarch64] } }
    steps: [{ run: "finn-sdk package . --unsigned --arch ${{ matrix.arch }} && finn-sdk verify ... --allow-unsigned" }]
```

Every new target must verify an artifact, not just compile: hash the image,
run `base/mkfinniso --check --arch=<arch>`, and for OTA run
`ota/verify-ab-linux.sh` on a Linux runner (it refuses to run on non-Linux —
`ota/verify-ab-linux.sh:20`).

---

## 2. Smartphone support requirements

A phone is not a smaller desktop. The list below separates what the current
tree already has from what a phone would require. **Nothing here claims phone
support exists.**

### 2.1 Display panel, touch and tablet input

SHIPPED (protocol level):

- `finn-comp` (wlroots 0.20) advertises pointer/keyboard,
  `WLR_INPUT_DEVICE_TOUCH` and `WLR_INPUT_DEVICE_TABLET_TOOL` input, plus
  `wlr_fractional_scale_v1`, screencopy and Xwayland
  (`docs/architecture.md:91-93`, `docs/roadmap.md:16`).
- `FinnFormFactor::{Desktop,Tablet,Phone}` exists in `libfinnui` and
  `finn-shell` adapts its layout across those breakpoints
  (`src/shell/shell_layout.hpp`, `docs/sdk/simulator.md:1-13`).
- The Lima dev VM exercises `virtio-gpu` DRM through seatd with
  `WLR_RENDERER=pixman` (`docs/hardware.md:4-9`).

ABSENT / required for a real phone:

- **Panel and touch hardware enablement:** DSI/MIPI panel driver, display
  timings, PWM backlight, touchscreen controller via libinput, rotation and
  orientation sensors. None is in `base/packages.*` or verified on hardware.
- **Gestures and IME:** no `wl_touch` gesture recognizer, pinch/swipe,
  `QTouchEvent` handling, `zwp_text_input_v3` or on-screen keyboard
  (`docs/sdk/audit.md:51,65`, `docs/roadmap.md:41`). A phone without these is
  not operable.
- **Adaptive app chrome:** `FinnWindow` does not currently read
  `FINN_FORM_FACTOR` / `FINN_SIM_*` and apps keep their requested desktop size
  (`docs/sdk/simulator.md:46`). Touch-target minimums (≥44 dp) and edge-to-edge
  layout are proposals only (`simulator.md:54-63`).
- **Form-factor constraints:** no safe-area/notch handling, no system status
  bar or gesture navigation surface, no per-orientation shell reflow on device.

### 2.2 Modem and telephony

**Absent.** There is no `ModemManager`, `ofono`, dialer, SMS client, SIM/APN
configuration, emergency-call path or audio-routing policy anywhere in the
tree. `docs/roadmap.md:43` lists "Cellular modem/telephony stack
(`ModemManager`) for phone hardware" as future work, and
`docs/audit/remaining-issues.md:80-81` repeats it. The `telephony` entry in the
OpenHarmony kit list is a host bridge gap, not an implementation
(`docs/ohos-kit-coverage.md`). A phone claim without this stack would be a
faked capability.

Required (none implemented): ModemManager + a supported WWAN modem, carrier
APN profile database, SIM PIN/Unlock UI, voice call + SMS applications,
emergency dialing compliance, radio power states, and IMS/VoLTE where required
by the carrier.

### 2.3 Power management

Present today only through upstream services when they exist: logind (idle
lock/suspend), UPower (battery), NetworkManager, BlueZ and PipeWire probes in
Settings/Control Center. Missing hardware renders honestly disabled
(`docs/hardware.md:21-23`, `docs/architecture.md:16`).

Required for a phone (absent):

- Battery saver / thermal policy daemon and per-app wake locks.
- Screen-off policy tied to proximity sensor and display sink; correct
  suspend/resume ordering with the compositor.
- Brightness actuator behind the reserved `finnd` root authority — the
  `sysfs` brightness verb is only a roadmap item (`docs/roadmap.md:20-28`);
  `finn-comp` currently holds root for DRM and must not become an app-facing
  power service.
- Verified suspend/resume of panel, touch and GPU on real hardware; no device
  test exists.

### 2.4 Bootloader and verified boot

The current chain is firmware → FinnOS boot menu → kernel → Plymouth →
compositor, with a silent Finn-mark splash (`docs/architecture.md:18-52`).
A/B OTA logic exists (signed manifest, slot switching, boot counting) but:

- No UEFI Secure Boot key enrollment or signed loaders/kernels.
- No `dm-verity` root hash trees or TPM2 sealing; an offline attacker can
  modify `/boot` or a slot (`docs/ota.md:117-124`).
- No phone bootloader path (fastboot/recovery/AVB-style verification), no
  hardware key provisioning, no monotonic anti-rollback counter.
- No bare-metal A/B power-cycle proof (`docs/ota.md:304-308`).

A phone cannot honestly be described as "secure" or "verified boot" until all
four are closed; see [OTA gaps](../ota/package-format.md#8-real-gaps-spec-only--do-not-claim-these-work).

### 2.5 What "phone support" would mean (exit criteria)

1. Boots on a named device with a downstream panel/touch driver and reaches a
   usable compositor and shell at a phone viewport.
2. Touch, gestures and virtual keyboard work in first-party apps without a
   pointer or physical keyboard; 44 dp minimum hit targets verified.
3. Modem stack reports a real SIM, registers on a network, and places a call
   and an SMS through a first-party app — or the capability is not claimed.
4. Suspend/resume, battery and thermal states survive a multi-day soak test.
5. Boot chain verifies kernel/root with hardware-backed keys and enforces
   anti-rollback; A/B rollback proven with a real power cycle.
6. `docs/hardware.md`, `docs/testing.md` and this page name the device and the
   remaining gaps; a simulator run does not count as device support.
