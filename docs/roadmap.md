# Roadmap

What has shipped in FinnOS as of now, and what remains next. Every item below reflects the current tree honestly.

## Shipped

- **Custom Graphical UEFI Bootloader (`src/boot/`)**: Freestanding C `finn-bootx64.efi` and `finn-bootaa64.efi` Startup Manager with GOP framebuffer UI, UAPI BLS Type #1 (`/loader/entries/*.conf` + boot-counting) and Type #2 (UKI `.osrel`/`.cmdline`), auto Windows/macOS/Firmware detection, `EFI_LOAD_FILE2_PROTOCOL` multi-initrd handoff, systemd BLI EFI variables, and `T` text fallback — verified under ASan/UBSan and QEMU + OVMF.
- **Silent Boot Chain & 100% Offline Recovery Mode (`base/`, `src/recovery/`, `apps/recovery-mode/`)**: `finnos` Plymouth theme (72 × 72 px mark matching `finn::kBootMarkSizePx`), `finn-recovery.service` (`finn.recovery`), pure C++20 recovery planner with FIPS 180-4 SHA-512 image verification, universal GPT/MBR and UEFI/Legacy BIOS support, non-destructive dual-boot resize, A/B slot partitioning, data-preserving reinstall, Disk Utility, Bootloader Repair, World Keyboards, Wi-Fi, System Logs, and embedded PTY terminal.
- **First-Boot-Only OOBE (`apps/oobe/`, `tools/finn-oobe-apply`)**: 10-step onboarding gated to the first normal boot of an installed machine (`/var/lib/finn/oobe_complete` + `~/.config/finn/oobe_complete`), responsive across Desktop/Tablet/Phone, with interactive 3D pendant lamp and audited one-shot root provisioning.
- **Qt 6 Wayland Compositor (`src/comp/`, `finn-comp`)**: Migrated off wlroots onto C++20 / Qt 6 `QWaylandQuickCompositor` (`xdg-shell`, `xdg-decoration`, `text-input-v3`, `wp_viewporter`), 8-layer `SceneLayer` stack, 4 virtual desktops (`WorkspaceModel`), `InputRouter`, 120 Hz analytic spring animations, CPU backdrop blur + Liquid Glass SDF refraction (`CompositorBackdropItem`), multi-finger trackpad gestures, `/run/finn/state/` IPC, and zero app IDs (`make lint` enforced).
- **`FinnUI` & `FinnSDK` (`src/ui/`, `src/qtplugin/`, `sdk/`)**: `libfinnui` (`SOVERSION 1`, 29 C++ widgets) and `libfinnui_qml` (`SOVERSION 1`, 19 AOT-compiled `FinnUI 1.0` QML controls including adaptive `FinnApplicationWindow`), `QT_QPA_PLATFORMTHEME=finn` & `QT_STYLE_OVERRIDE=finn`, generated tokens (`ui/_tokens.fui`), 120 Hz analytic spring physics (`spring.hpp`), `AdaptiveFormFactor` (`Desktop`/`Tablet`/`Mobile`/`TV`), `finn-sdk` CLI (scaffolding, validation, signed `.finnapp` packaging, simulator), and CMake/pkg-config packaging.
- **Qt Quick Desktop Shell (`src/shell/`, `finn-shell`)**: `ShellCorona` + `DesktopShell.qml` featuring the 4-pill floating dock, Home Menu with folders and recents, Spotlight search with inline arithmetic, Control Center, receding 3-card Notification stack, workspace Overview, multi-user Lock Screen (`unix_chkpwd`/`passwd -S`/`su`/`PrepareForSuspend`), and first-boot OOBE launch.
- **11 Native Registry Apps + 3 System Surfaces (`apps/`)**: **Settings**, **Finn Files**, **Finn Terminal**, **Finn Notes**, **Finn Store** (Curated, live Flathub API v2, Aurora/Play FDFE + F-Droid, `winget-pkgs` with SHA-256, and local package sideloading), **Finn Viewer**, **Finn Monitor**, **Finn Calculator**, **Finn Archive**, **Finn Capture**, optional **Flourish** (`Qt6::WebEngineQuick`), plus `finn-wallpaper`, `finn-oobe`, and `recovery-mode`.
- **A/B OTA Updates & First-Party Repo Tooling (`src/ota/`, `ota/`, `repo/`)**: `finn-update` / `finn_ota` with pure-Python RFC 8032 Ed25519 signing/verification, resumable HTTP Range downloads, `zstd --patch-from` deltas, `systemd-boot` and `GRUB` A/B slot backends, `finn-boot-health.service`, `ota/publish`, and `[finnos]` pacman repository scripts.
- **Multi-Ecosystem Compatibility (`src/compat/`, `src/darwin/`, `src/ohos/`, `src/android/`)**: Native Linux (`.desktop`, Flatpak, AppImage Type 1/2, ELF probe, `.finnapp`), Windows (PE/ICO/MSI/Wine/Proton/Hangover ARM64 WoW64 + `wl_clamp.c`), clean-room macOS partial runtime (`finn-darwin`), clean-room OpenHarmony 6.1 LTS partial runtime (`finn-ohos`), and clean-room Android 17 inspector & AXML layout previewer (`finn-android`).
- **Virtualization Guest Integration (`finn-guest-tools`, `src/system/guest_tools.*`)**: Honest hypervisor detection and VMware Tools, VirtualBox Guest Additions (`-nox`), and QEMU Guest Agent integration.

## Next

### 1. Finish wiring the Qt 6 compositor (`finn-comp`)
- Complete `startXwaylandIfEnabled()` so `Xwayland -rootless -noreset` actually starts and publishes `/run/finn/xdisplay`.
- Wire `src/comp/shaders/dual_kawase_blur.frag` into a QML `ShaderEffect` to move backdrop blur off CPU onto the GPU when GL/Vulkan is available (retaining CPU fallback for software rendering).
- Connect `CompositorServer::handleShortcutKey()` to keyboard/event-filter input, handle `SwitchVT2`, and reset `session_locked_` cleanly on unlock.
- Implement or bind native Wayland protocols (`wlr-layer-shell`, `wlr-screencopy`, `wlr-foreign-toplevel-management`, `ext-session-lock-v1`) so `finn-shell`, `grim`, and `finn-capture` no longer rely on `/run/finn/state/` file IPC.
- Route `libinput` device opens through `seatd` rather than direct `::open()`, and add multi-monitor arrangement + mixed per-output HiDPI scaling.

### 2. Complete adaptive `finn-shell` & desktop services integration
- Implement `TabletShell.qml`, `MobileShell.qml`, and `TvShell.qml` (currently empty 6-line `Item` stubs).
- Link `FinnDesktopServices` (`src/system/desktop_services.cpp`) into `finn-shell` so Control Center Bluetooth, MPRIS media, and real Wi-Fi SSID display work in the shell (matching `Settings` and `OOBE`).
- Add an `org.freedesktop.Notifications` D-Bus daemon to `finn-shell` so external apps can post notifications.
- Forward `pageId` in `openSettings(pageId)` so shell actions open the exact Settings pane.

### 3. Privilege boundary (`finnd`) & native app sandbox
- Introduce a minimal root control daemon (`/run/finn/control.sock`, `0660 root:finn-authority`, `SO_PEERCRED`, no TCP, no app launch) for audited sysfs brightness, power, network, and system update operations.
- Run native apps in transient systemd user units or enforced user-namespace/seccomp profiles with enforced `finn-app.json` permissions instead of `linux_sandbox=env-only`.

### 4. Production trust, verified boot & hosted infrastructure
- Provision real GPG/Ed25519 production keys outside git, replace the empty placeholder keyring in `base/pkgbuilds/finnos-keyring/`, and host `repo.finnos.org` + signed OTA channels.
- Add UEFI Secure Boot (Shim/SBAT), `dm-verity`, and TPM2 sealing to `finn-boot` and A/B slots, and verify real-disk A/B power-cycle rollback on target hardware.

### 5. Accessibility, localization, telephony & deeper compatibility
- Enforce `text_scale`, `color_filter`, XKB `sticky_keys`/`filter_keys`, `orca` screen reader packaging, `QTranslator` localization bundles, and `ModemManager` telephony for mobile hardware.
- Expand `finn-darwin` (Swift stdlib/SwiftUI, Metal-over-Vulkan, WebKit, CoreData, Security/Keychain), `finn-ohos` (full ArkTS execution and hardware/distributed kits), and `finn-android` (broader Dalvik/ART/framework execution beyond static inspection and AXML preview).

## What "done" means

A change is done when:
- `src/comp` contains zero app IDs (`make lint`);
- host tests (`make test`, `make test-unit`, `make lint`, `base/mkfinniso --check`) pass;
- visual changes are verified with actual captures;
- documentation accurately states both what works and what remains missing.
