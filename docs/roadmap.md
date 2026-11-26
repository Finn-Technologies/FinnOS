# Roadmap

FinnOS has migrated its UI, shell, native applications, and developer SDK to
**Qt6 + C++20** (`FinnSDK`, `libfinnplatformtheme`, `libfinnstyle`) on top of
the `wlroots 0.20` compositor. These milestones track what has shipped vs. what
remains next.

## Shipped in the Qt6 + FinnSDK & Hardenings milestone

- **Design Tokens, Motion & Adaptive Form Factors**: `ui/_tokens.fui` + `tools/tokens.py` + `generated_tokens.hpp` semantic/spacing/radii/breakpoint tokens (`docs/design-language.md`), analytic spring physics (`src/ui/finn/spring.hpp`, `<FinnSDK/Spring.hpp>`), `FinnFormFactor` (`Desktop`/`Tablet`/`Phone`) adaptation in `FinnWindow`/`FinnSidebar`/`finn-shell`, keyboard focus rings, and `setAccessibleName` labels across `libfinnui` and apps.
- **Custom Qt6 OS integration**: `libfinnui` (`FinnSDK::FinnUI`), `QT_QPA_PLATFORMTHEME=finn`, and `QT_STYLE_OVERRIDE=finn`, driven by `ui/_tokens.fui` and Fixel Display.
- **Developer SDK (`FinnSDK`)**: `finn-sdk` CLI (`new`, `build`, `run`, `register`, `check`, `validate`, `keygen`, `package`, `verify`, `simulate`), `finn-app.schema.json`, CMake package (`find_package(FinnSDK)`), headers (`<FinnSDK/FinnSDK.hpp>`, `<FinnSDK/Spring.hpp>`), `sdk/template/`, and `sdk/tests/test_sdk.py`.
- **FinnUX shell (`finn-shell`)**: Dock, Home menu, Spotlight search + math evaluator, Control Center (appearance switch + async NetworkManager/BlueZ/UPower/PipeWire probes), notifications, adaptive phone/tablet layout (`src/shell/shell_layout.hpp`), and async multi-user Lock Screen.
- **Settings (`finn-settings`)**: `ChromeMode::FigmaSplit` sidebar, real hardware/system controls, System Update (A/B OTA frontend), High Contrast, Reduced Motion, and Pointer Size. *Note*: Text Scale, Color Filters, Sticky/Filter Keys settings persist to config, while compositor/font/XKB runtime enforcement and packaged `orca` screen reader remain incomplete.
- **Native Apps + Recovery Mode + OOBE + Wallpaper**: `Settings`, `Finn Files`, `Finn Terminal`, `Finn Notes`, `Finn Store` (with `.finnapp` verification), `Finn Viewer`, `Finn Monitor`, `recovery-mode` (FinnOS Recovery Mode wired to `finn-install.sh` for clean, A/B, dual-boot, reinstall, and boot-repair flows), `finn-oobe` (with real account/password/locale backend), `finn-wallpaper`, and optional QtWebEngine browser `apps/flourish`.
- **Compositor (`finn-comp`) & Compatibility**: wlroots 0.20 with `ext-session-lock-v1`, WM shortcut gating while locked, `WLR_INPUT_DEVICE_TOUCH` / `TABLET_TOOL` support, fullscreen layer stacking, XKB `kb_layout` loading, `wlr_fractional_scale_manager_v1`, on-demand `Xwayland`, PE inspection + Wine prefix launcher (`wine_runner`), and a clean-room Android 17 probe (`android_runner` / `finn-android`) that replaced the old Waydroid probe.
- **A/B OTA Updates & Repo Tooling**: `src/ota/finn-update` (Ed25519 manifest signing, `zstd` delta patching, `systemd-boot`/`GRUB` A/B slot switching, `finn-update-check.timer`, `finn-boot-health.service`), `vm/finn-install.sh --ab-slots` (`FINNOS_A`/`FINNOS_B`/`FINNOS_DATA`), and `repo/` signing/publishing scripts (`repo.finnos.org` and `finnos-keyring` ship placeholder trust roots until hosted infrastructure is provisioned).
- **FinnOS boot chain & Rebrand**: `finnos` Plymouth splash (`quiet splash vt.global_cursor_default=0`), `finn-oobe` first-boot setup, `tools/check-branding.sh` enforced in `make lint`, locked `root` (`passwd -l root`) on installed machines, and non-`NOPASSWD` sudoers unless `FINN_DEV_VM=1`.

## 1. The root authority — `finnd`

Machine-level privileged actions that should not live inside user apps or the
compositor remain future work:

- unix control socket `/run/finn/control.sock` (`0660 root:finn-authority`) with `SO_PEERCRED`;
- audited verbs for sysfs brightness, poweroff/reboot orchestration, system updates, and
  root-owned network/package actions;
- no app launching (that stays `finn-session`) and no TCP listener.

## 2. App sandbox

Each app running in a transient systemd user unit (or user namespace/seccomp
profile) with `NoNewPrivileges`, `ProtectSystem=strict`, scoped state dirs,
and no direct access to `/run/finn` outside controlled sockets. Once `finnd`
handles DRM/power/brightness actuators, `finn-comp` can also drop root via `seatd`.

## 3. Remaining Display, Accessibility & Platform Work

- Compositor GPU shader backdrop blur behind translucent `layer-shell` and glass surfaces.
- Multi-monitor arrangement editor and mixed per-output fractional/integer scaling.
- Wayland IME text input (`zwp_text_input_v3`), clipboard history UI, and runtime accessibility enforcement (`text_scale` across fonts, `color_filter` compositor shader, XKB `sticky_keys`/`filter_keys`, packaged `orca`, and `QTranslator` localization).
- **Verified boot hardening** (Secure Boot, `dm-verity`, TPM2 sealing) and live hosted `repo.finnos.org` + real-hardware/VM A/B slot reboot rollback verification.
- **Cellular modem/telephony stack** (`ModemManager`) for phone hardware.
- **Expanded macOS (`finn-darwin`) & OpenHarmony 6.1 LTS (`finn-ohos`) runtime coverage**: `src/darwin/` provides the clean-room 64-bit Mach-O loader, `libSystem`, Objective-C 2.0, Foundation, CoreGraphics/QuartzCore, core Swift ABI, AppKit-on-Qt6 bridge, and HTTP/local `.dmg`/`.pkg`/`.zip`/`.app` installer; `src/ohos/` provides OpenHarmony 6.1 LTS (API 23) `.hap`/`.hsp`/`.app` installation, Panda `modules.abc` VM, `UIAbility`, ArkUI-on-Qt6, and NAPI host bridge. Full Swift stdlib/SwiftUI, Metal-over-Vulkan/OpenGL, WebKit, CoreData, and OpenHarmony hardware/distributed kits (`ArkWeb`, `DistributedDataObject`, Telephony/NFC/Camera/Sensor) remain ongoing iterative work.

## What "done" means

A change is done when:

- `src/comp` contains zero app ids (`make lint`);
- host tests (`make test`, `make test-unit`, `make lint`, `base/mkfinniso --check`) pass;
- the VM build installs cleanly and `vm/shot.sh` screenshots are visually verified;
- docs accurately state both implemented features and remaining gaps.

## Shipped in the full-OS-completion integration pass

- **First-class OOBE across form factors**: `apps/oobe/main.cpp` now derives
  `Phone`/`Tablet`/`Desktop` from the same breakpoints and `FINN_FORM_FACTOR`
  override as `finn-shell`. Desktop keeps the Figma `36:53` split exactly;
  tablet uses a narrow split with a 24 dp gutter; phone stacks the prompt over
  a full-width 44 dp-touch-target list and moves Back/Next into a bottom
  safe-area strip. Content columns no longer assume a 1280×832 screen.
- **Reference research**: [`docs/reference-designs.md`](reference-designs.md)
  records sourced, specific findings from macOS Setup Assistant, Windows 11
  OOBE/Mica, HarmonyOS 2 atomic services, iOS 16 Lock Screen, ColorOS 14 and
  iOS 17 motion, each with what FinnOS adopts, adapts or declines. The four
  Figma OOBE frames were read with `get_design_context`; they carry no Figma
  Motion, so no animation is attributed to them.
- **The headless OOBE flow drives the real app**: `finn-shell` writes a line-oriented capability preflight (`src/shell/oobe_capability_probe.hpp`) from real probes — `nmcli` device list, `pkexec`/`useradd`, an actual config write, `org.a11y.Bus` — and `finn-oobe` uses `FullOobeFlow` gates for all eight steps. Offline/local-only/private-by-default are named explicit states; nothing is marked connected or created on an unmeasured capability. A phone capture caught and fixed a region-catalogue mismatch that had wrongly disabled Next.
- **Figma drift fixes verified on the VM**: glass weights, dock group width, Control Center height/footer, Home/notification type and tile geometry now follow `docs/figma-live-drift.md`; real captures were read for dock, Home, Control Center, notifications and OOBE at 500/800 dp.
- **Measured redraw fix**: the layer-shadow blur is now a sliding-window O(w·h)
  kernel (`src/shell/blur_kernel.hpp`) instead of an O(w·h·r) neighbour scan.
  `make bench` runs the shipping kernel against the old algorithm on the host:
  **2.5–3.6× faster, bit-for-bit identical**. See
  [`docs/performance.md`](performance.md). Shell app-list changes are debounced
  (200 ms) so one install no longer rebuilds the registry/dock several times.
  `FINN_SHELL_TIMINGS=1` prints VM startup checkpoints; no unmeasured numbers
  are published.
