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
- **Native Apps + Installer + OOBE + Wallpaper**: `Settings`, `Finn Files`, `Finn Terminal`, `Finn Notes`, `Finn Store` (with `.finnapp` verification), `Finn Viewer`, `Finn Monitor`, `finn-installer` (wired to `finn-install.sh --disk --hostname --timezone --ab-slots --yes`), `finn-oobe` (with real account/password/locale backend), `finn-wallpaper`, and optional QtWebEngine browser `apps/flourish`.
- **Compositor (`finn-comp`) & Compatibility**: wlroots 0.20 with `ext-session-lock-v1`, WM shortcut gating while locked, `WLR_INPUT_DEVICE_TOUCH` / `TABLET_TOOL` support, fullscreen layer stacking, XKB `kb_layout` loading, `wlr_fractional_scale_manager_v1`, on-demand `Xwayland`, PE inspection + Wine prefix launcher (`wine_runner`), and Waydroid probe (`android_runner`).
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
