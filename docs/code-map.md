# Code map

A ten-minute tour of the FinnOS tree, its IPC flows, and where to change what.

## Tree overview

```
src/boot/        finn-bootx64.efi & finn-bootaa64.efi (freestanding UEFI GOP bootloader, BLS/UKI)
src/comp/        finn-comp (Qt 6 QWaylandQuickCompositor, WorkspaceModel, InputRouter, CPU blur/glass)
src/ui/          libfinnui (29 C++ widgets), libfinnui_qml (19 FinnUI 1.0 QML controls), FinnSDK headers
src/qtplugin/    libfinnplatformtheme.so (QPlatformTheme) & libfinnstyle.so (QProxyStyle)
src/session/     finn-session (unified AppRegistry, SO_PEERCRED broker, SessionClient)
src/shell/       finn-shell (ShellCorona, DesktopShell.qml, LockScreenOverlay.qml, search, OOBE flow)
src/system/      sys_files (/proc, /sys, settings), desktop_services (D-Bus/NM/BlueZ/PipeWire), guest_tools
src/recovery/    recovery_engine (pure C++20 offline installer/dual-boot/disk planner + SHA-512)
src/compat/      Linux (.desktop/Flatpak/AppImage/ELF/sandbox), Windows (PE/ICO/MSI/Wine/wl_clamp), runners
src/darwin/      finn-darwin (64-bit Mach-O loader, objc_msgSend asm, Foundation/AppKit-on-Qt6)
src/ohos/        finn-ohos (OpenHarmony 6.1 LTS .hap/.hsp/.app installer, Panda VM, ArkUI-on-Qt6, NAPI)
src/android/     finn-android (Android 17 APK/AXML/ARSC/DEX inspector, 14-widget Qt 6 previewer)
src/ota/         finn-update & finn_ota (Ed25519 manifests, HTTP Range, zstd deltas, systemd-boot/GRUB)
src/common/      paths.hpp (shared paths & kBootMarkSizePx) & liquid_glass.hpp (CPU SDF glass)
apps/<id>/       11 registry apps (settings, files, terminal, notes, store, viewer, monitor,
                 calculator, archive, capture, flourish) + wallpaper, oobe, recovery-mode
ui/_tokens.fui   single source of truth for palette, spacing, radii, motion, breakpoints
tools/           tokens.py, finn-sdk, finn-win, finn-display, finn-guest-tools, finn-emulator,
                 finn-oobe-apply, finn-open, finn-cursors, coff2efi.py, check-branding.sh
sdk/             finn-app.schema.json, sdk/template/, sdk/tests/
repo/            [finnos] pacman repository signing & publishing scripts
ota/             OTA keygen, publish, grub-ab.cfg, verify-ab-linux.sh
base/            mkfinniso, profiledef.sh, systemd units, Plymouth theme, packages, PKGBUILDs, setup.sh
vm/              deploy.sh, shot.sh, lima.yaml, create-dev-vm.sh, finn-install.sh, finn-chroot.sh
tests/           host Python suite (tests/test_*.py) and C++20 ASan/UBSan unit tests (tests/unit/)
```

## Key runtime flows

### 1. App launch (Dock / Home Menu / Spotlight / `finn-open`)
1. `finn-shell` (`ShellCorona::launchApp`) or `finn-open` connects via `SessionClient` (`src/session/client.cpp`) to `$XDG_RUNTIME_DIR/finn/session.sock`.
2. `finn-session` (`src/session/main.cpp`) validates `SO_PEERCRED` (`uid == getuid() || uid == 0`) and looks up the id in `RegistryCache` (`src/session/app_registry.cpp`).
3. `finn-session` builds a clean launch environment via `finn::compat::build_linux_launch_env()` (sets `WAYLAND_DISPLAY=/run/finn/wayland-0`, `QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`, strips `LD_*`/plugin hijack vars) and spawns the target with `posix_spawnp` + `POSIX_SPAWN_SETSID`.
4. The app connects to `/run/finn/wayland-0`, creates an `xdg_toplevel`, and `finn-comp` (`src/comp/server.cpp` + `core/workspace_model.cpp`) inserts it into `SceneLayer::Toplevel` without ever learning its app id.
5. `finn-comp` updates `/run/finn/state/windows.tsv`; `finn-shell` watches `/run/finn/state/` and updates the dock's running-window indicators.

### 2. Window control, blur regions, capture & session lock
`finn-comp` and `finn-shell` / `finn-capture` coordinate via `/run/finn/state/` (`0775 root:finn`):
- **Window focus/minimize/close**: `finn-shell` writes a line to `/run/finn/state/window-cmd`; `finn-comp` reads and executes it on `WorkspaceModel`.
- **Frosted glass & click-through**: `finn-shell` publishes active dock/popover rects to `/run/finn/state/blur-rects.tsv` and sets `QWindow::setMask()` so clicks outside UI pills pass through to apps; `CompositorBackdropItem` (`src/comp/backdrop_item.cpp`) blurs the lower scene and applies `finn::glass::render_liquid_glass()`.
- **Screenshots**: `finn-capture` writes `/run/finn/state/capture-request`; `finn-comp` captures `output_window_->grabWindow()` to PNG and writes `<png>.done`.
- **Session lock**: `finn-comp` (on idle) or `finn-shell` (Home Menu / `logind` `PrepareForSuspend`) triggers `LockScreenOverlay.qml`, which authenticates via `unix_chkpwd` / `passwd -S` / `su`.

### 3. Live theme & token propagation
1. Edit `ui/_tokens.fui` → run `python3 tools/tokens.py ui/_tokens.fui src/ui/generated_tokens.hpp` → verify with `make lint`.
2. At runtime, `Settings` writes `~/.config/finn/settings` (or `/var/lib/finn/settings`).
3. `FinnThemeManager` (`src/ui/finn/theme.cpp`), `FinnThemeBridge` (`src/ui/qml/finn_qml_types.cpp`), `libfinnplatformtheme` (`src/qtplugin/finn_platform_theme.cpp`), and `finn-comp` watch the settings files and reload dark/light mode, accent colour, high contrast, and reduced motion live without restarting apps.

### 4. First-boot OOBE & Offline Recovery Mode
- **Live ISO / `finn.recovery`**: `finn-recovery.service` launches `recovery-mode` (`apps/recovery-mode/main.cpp`), which uses `src/recovery/recovery_engine.cpp` to probe disks, verify offline images via SHA-512, and execute `vm/finn-install.sh` + `vm/finn-chroot.sh`.
- **First normal boot**: `ShellCorona::maybeLaunchFirstBootOobe()` checks `finn::should_run_oobe_on_boot()` (`/var/lib/finn/oobe_complete` and `~/.config/finn/oobe_complete` absent, non-live cmdline), writes a capability probe (`src/shell/oobe_capability_probe.hpp`), and spawns `finn-oobe`, which finishes by running `tools/finn-oobe-apply`.

## Where to change what

| Task | Files to touch |
|---|---|
| Change palette, radii, spacing, motion, or breakpoints | `ui/_tokens.fui` → regenerate `src/ui/generated_tokens.hpp` with `tools/tokens.py` |
| Add a shared QML control | `src/ui/qml/controls/Finn*.qml`, `src/ui/CMakeLists.txt` |
| Add a shared C++ widget | `src/ui/finn/components.{hpp,cpp}`, `src/ui/FinnSDK/` |
| Adjust stock Qt 6 app styling | `src/qtplugin/finn_platform_theme.{hpp,cpp}`, `src/qtplugin/finn_style.{hpp,cpp}` |
| Change compositor windowing or workspaces | `src/comp/core/workspace_model.{hpp,cpp}`, `src/comp/server.{hpp,cpp}`, `src/comp/qml/WaylandCompositorScene.qml` |
| Change desktop dock, popovers, or lock screen | `src/shell/core/shell_corona.{hpp,cpp}`, `src/shell/ui/desktop/*.qml`, `src/shell/ui/common/*.qml` |
| Add a session protocol verb or app source | `src/session/main.cpp`, `src/session/app_registry.{hpp,cpp}`, `src/session/client.{hpp,cpp}`, `docs/session-protocol.md` |
| Add a first-party native app | `apps/<id>/`, `apps/CMakeLists.txt`, `Makefile`, `assets/apps.registry`, `assets/icons/apps/<id>.svg`, `NAMING.md` |
| Change installer / dual-boot planning | `src/recovery/recovery_engine.{hpp,cpp}`, `apps/recovery-mode/main.cpp`, `vm/finn-install.sh`, `vm/finn-chroot.sh`, `tests/unit/test_recovery.cpp` |
| Change UEFI bootloader | `src/boot/{efi_main,bls_parser,boot_ui}.c`, `tests/unit/test_bootloader.cpp`, `tests/test_uefi_boot_harness.py` |
| Change OTA update client or publisher | `src/ota/finn_ota/*.py`, `ota/publish`, `tests/test_ota.py` |
| Change systemd units or base packages | `base/finn-*.service` **and** `base/airootfs/etc/systemd/system/finn-*.service`, `base/packages.{x86_64,aarch64}` → verify with `base/mkfinniso --check` |

## Hard rules enforced by `make lint`
- Zero known app IDs under `src/comp/`.
- `src/ui/generated_tokens.hpp` matches `tools/tokens.py ui/_tokens.fui`.
- Clean `git diff --check` and Python bytecode compilation of `tools/` and `tests/`.
- Zero stray upstream distribution branding in user-visible code and docs (`tools/check-branding.sh`).
