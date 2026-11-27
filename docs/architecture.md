# Architecture

The processes, shared libraries, boot chain, compatibility layers, and honest current limits of FinnOS.

| Component | Owns | Runs as |
|---|---|---|
| `finn-boot` (`src/boot/`) | Freestanding UEFI GOP graphical Startup Manager (`finn-bootx64.efi`, `finn-bootaa64.efi`): UAPI BLS Type #1/#2 (UKI), boot-counting, Windows/macOS/Firmware detection, `EFI_LOAD_FILE2_PROTOCOL` multi-initrd, systemd BLI EFI vars | UEFI firmware |
| `finn-comp` (`src/comp/`) | Qt 6 `QWaylandQuickCompositor`: `xdg-shell`, `xdg-decoration`, `text-input-v3`, `wp_viewporter`, 8-layer `SceneLayer` stack, 4 virtual desktops (`WorkspaceModel`), `InputRouter`, CPU backdrop blur + Liquid Glass SDF (`CompositorBackdropItem`), `/run/finn/state/` IPC, systemd watchdog, zero app IDs | root |
| `libfinnui` & `libfinnui_qml` (`FinnSDK`) | Qt 6 C++20 + QML UI framework (`SOVERSION 1`): generated tokens from `ui/_tokens.fui`, `FinnThemeBridge`, 120 Hz analytic spring physics (`spring.hpp`), `AdaptiveFormFactor` (`Desktop`/`Tablet`/`Mobile`/`TV`), `FinnSquircle`, HugeIcons `image://finn-icon/`, 29 C++ widgets, 19 AOT-compiled `FinnUI 1.0` QML controls | shared libraries |
| `libfinnplatformtheme` & `libfinnstyle` | Custom Qt 6 `QPlatformTheme` and `QProxyStyle` injected via `QT_QPA_PLATFORMTHEME=finn` & `QT_STYLE_OVERRIDE=finn` | Qt 6 plugins |
| `finn-session` (`src/session/`) | Unified `AppRegistry` across 7 sources, `SO_PEERCRED` line-oriented launch/uninstall/compat broker (`posix_spawnp`) | `finn` |
| `finn-shell` (`src/shell/`) | Qt Quick System UI (`ShellCorona` + `DesktopShell.qml`): 4-pill dock, Home Menu, Spotlight search + inline math, Control Center, receding 3-card Notification stack, workspace Overview, multi-user Lock Screen, first-boot OOBE launch | `finn` |
| `finn-wallpaper` (`apps/wallpaper/`) | Standalone WebP wallpaper surface (`background` layer; on Wayland under `finn-comp`, the compositor renders the wallpaper directly unless `FINN_WALLPAPER_STANDALONE=1`) | `finn` |
| `finn-update` (`src/ota/`) | A/B OTA client (`finn_ota`): pure-Python RFC 8032 Ed25519 manifest verification, resumable HTTP Range downloads, `zstd --patch-from` deltas, `systemd-boot` & `GRUB` slot switching, `finn-boot-health.service` | root (units) / `finn` (read-only status) |
| `finn-darwin` (`src/darwin/`) | Clean-room 64-bit Mach-O/Universal loader, `objc_msgSend` asm, subset `libSystem`/Foundation/CoreGraphics/Swift/SwiftUI, `AppKit-on-Qt6` bridge, `.app`/`.pkg`/`.zip`/`.dmg` installer | `finn` |
| `finn-ohos` (`src/ohos/`) | Clean-room OpenHarmony 6.1 LTS (API 23) `.hap`/`.hsp`/`.app` installer, host-ABI NAPI `.so` loader, subset Panda `modules.abc` VM, `ArkUI-on-Qt6` + static bytecode UI reconstruction | `finn` |
| `finn-android` (`src/android/`) | Clean-room Android 17 (API 37) APK/split-APK installer, binary AXML & `resources.arsc` parser, DEX verifier/disassembler, 14-widget Qt 6 AXML layout previewer, 16-opcode integer micro-interpreter (no Waydroid) | `finn` |
| First-party apps (`apps/`) | 11 registry apps (`finn-settings`, `finn-files`, `finn-terminal`, `finn-notes`, `finn-store`, `finn-viewer`, `finn-monitor`, `finn-calculator`, `finn-archive`, `finn-capture`, optional `finn-flourish`) plus `finn-oobe` and `recovery-mode` | `finn` (or root for `recovery-mode` on live/recovery boot) |

`finnd` — a bespoke privileged system daemon — is not written. FinnOS integrates directly with standard upstream Linux mechanisms (systemd `logind`, `NetworkManager`, `BlueZ`, `PipeWire`/`wpctl`, `UPower`, sysfs, and audited one-shot helpers like `finn-oobe-apply`) when present, and marks missing hardware or services honestly disabled when absent.

## The boot chain

Firmware → `finn-boot` (or GRUB/syslinux) → kernel → Plymouth → `finn-comp` → `finn-shell`. Every visible part is FinnOS's:

- **UEFI graphical bootloader (`src/boot/`)**: `finn-bootx64.efi` and `finn-bootaa64.efi` render a GOP 32-bit BGRA Startup Manager on UEFI machines, parsing `/loader/loader.conf`, `/loader/entries/*.conf` (with systemd-boot boot-counting), and `/EFI/Linux/*.efi` UKIs, auto-detecting Windows/macOS/Firmware entries, exposing systemd Boot Loader Interface EFI variables, and offering a `T` text/debug fallback.
- **Boot menus & kernel parameters**: Live ISO menus (syslinux on x86_64 BIOS, systemd-boot on UEFI) and installed GRUB/systemd-boot configs pass `quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles`. `quiet` suppresses kernel console text, `splash` hands the framebuffer to Plymouth in the initramfs, `vt.global_cursor_default=0` hides the blinking cursor, and `plymouth.ignore-serial-consoles` prevents serial consoles (such as `ttyAMA0` on ARM VMs) from forcing Plymouth's text details theme.
- **Plymouth splash (`base/airootfs/usr/share/plymouth/themes/finnos/`)**: Draws the 72 × 72 px white Finn mark (`mark.png`, matching `finn::kBootMarkSizePx` in `src/common/paths.hpp`) on `#0B0E14` with `ShowDelay=0` and a progress-driven bar beneath it. Both live and installed initramfs images carry the theme and assets.
- **Recovery boot**: Passing `finn.recovery` on the kernel command line starts `finn-recovery.service` instead of `finn-comp.service` (`ConditionKernelCommandLine=!finn.recovery`), launching the offline `recovery-mode` installer and repair environment directly on `tty1`.
- **First-time setup (`finn-oobe`)**: Runs strictly on the first normal boot of an installed system (when `/var/lib/finn/oobe_complete` and `~/.config/finn/oobe_complete` are absent and the kernel command line is not a live/recovery boot). Its intro step draws the Finn mark at the exact same 72 px size and screen centre as the boot splash before playing the ribbon "hi." animation; normal boots go straight to the desktop.
- **Rescue console**: `Ctrl+Alt+F2` opens `getty@tty2`. What the firmware draws before the kernel starts is outside this tree.

## The display & compositor (`finn-comp`)

`finn-comp` (`src/comp/`) is a C++20 / Qt 6 `QWaylandQuickCompositor` built around a pure C++20 core (`finncomp_core`: `SceneLayer`, `WorkspaceModel`, `InputRouter`).

- **Display backend**: Runs on Qt QPA (`QT_QPA_PLATFORM=eglfs` with `LIBSEAT_BACKEND=seatd` on DRM/KMS Linux hardware, or `linuxfb`/`offscreen`). On non-Linux hosts without `Qt6WaylandCompositor` (e.g. macOS), `finn-comp` builds into a `--preview` Qt Quick harness (`CompositorScene.qml`).
- **Wayland protocols implemented**: `xdg-shell`, `xdg-decoration` (prefers client-side decoration; renders SSD chrome in QML when requested), `text-input-v3`, and `wp_viewporter`.
- **8-layer scene stack (`src/comp/core/scene_stack.hpp`)**:
  ```
  background → background layer → bottom layer → app toplevels → top layer → fullscreen → overlay → lock
  ```
- **Workspaces & motion**: 4 virtual desktops, MRU focus, snap left/right/quarters, maximize/minimize/fullscreen, show desktop, KWin-style Overview grid, multi-finger trackpad gestures (via Qt gestures and optional `libinput`), and 120 Hz (60 Hz on software backend) analytic spring animations (`SpringAnimator`).
- **Backdrop blur & Liquid Glass**: `CompositorServer::sampleBlurredBackdrop()` composites quarter-resolution wallpaper and lower window buffers through a 2-pass CPU separable box blur, and `CompositorBackdropItem` runs CPU SDF refraction (`finn::glass::render_liquid_glass()`) at half resolution. (`src/comp/shaders/dual_kawase_blur.frag` is compiled via `qt6_add_shaders` when available, but is not yet wired into QML.)
- **File-based IPC in `/run/finn/state/` (`0775 root:finn`)**:
  - `windows.tsv` & `window-cmd` (`focus`, `minimize`, `toggle`, `close`) — live window list and dock control
  - `capture-request` — triggers `output_window_->grabWindow()` PNG capture + `<png>.done`
  - `lock-request` — triggers `finn-shell` Lock Screen on idle or lock requests
  - `shell-action` (`home`, `search`) & `blur-rects.tsv` — shell triggers and live frosted-glass regions

## The lock screen

`LockScreenOverlay.qml` in `finn-shell` paints an opaque, cover-fitted lock wallpaper (falling back to the desktop wallpaper, then `/usr/share/finn/wallpaper/default.webp`), a real user list (`/var/lib/finn/primary_user` + `getpwent` UID 500..65533), and real authentication via `unix_chkpwd` (current user), `passwd -S` (`NP` passwordless check), or `su` over a POSIX PTY (other users), plus reboot/poweroff via `finn-session`.

Triggers:
- **Manual**: Home Menu / Lock action.
- **Idle / Compositor request**: `finn-comp` writes `/run/finn/state/lock-request`; `finn-shell` watches `/run/finn/state/`, opens the overlay, and consumes the file.
- **Suspend**: `finn-shell` listens for systemd-logind's `PrepareForSuspend` D-Bus signal and locks before sleep when `lock_on_sleep` is enabled.

## Authority

- **Display socket**: `/run/finn/wayland-0` (`0660 root:finn`). `finn-comp` runs as root so it can access DRM/KMS and input devices; user apps run as `finn` and connect using an absolute `WAYLAND_DISPLAY=/run/finn/wayland-0`.
- **State directory**: `/run/finn/state` (`0775 root:finn`) for lock, capture, window-control, and blur-rect IPC.
- **Session socket**: `$XDG_RUNTIME_DIR/finn/session.sock` (`0600`, user `finn`), enforced via `SO_PEERCRED` (`uid == getuid() || uid == 0`), spawning apps with `posix_spawnp`.
- **One-shot OOBE helper**: `tools/finn-oobe-apply` is invoked once by `finn-oobe` via a scoped `/etc/sudoers.d/finn-oobe` rule that retires itself upon writing `/var/lib/finn/oobe_complete`.
- There is no TCP listener and no hidden second door.

## The app model & compatibility

An app in FinnOS is any program opening an `xdg_toplevel`. Nothing under `src/comp` knows any app id (`make lint` enforces this).

`finn-session` aggregates seven real sources into one launcher model (`AppRegistry`, cached with filesystem mtime invalidation):
1. **First-party native apps** from `/usr/share/finn/apps.registry` + `/var/lib/finn/installed`.
2. **Third-party `FinnSDK` apps** registered in `~/.local/share/finn/apps/` (`.finnapp` bundles installed via `src/compat/finnapp_installer` or `finn-sdk register`).
3. **Linux applications** discovered recursively from Freedesktop `.desktop` directories (including Wine `winemenubuilder` shortcuts), Flatpak exports (plus `.flatpakref`/`.flatpak`/`.flatpakrepo` via `flatpak --user`), and verified AppImages in `~/Applications`, `~/.local/share/finn/appimages`, and `/opt`. `src/compat/appimage_runner` validates Type 1/2 magic bytes and host CPU architecture, probes FUSE, and falls back to `--appimage-extract-and-run` for Type 2. See [docs/linux-app-coverage.md](linux-app-coverage.md).
4. **Windows executables** (`.exe`/`.msi`; `.msix`/`.appx` detected and refused) via `finn-win` + Wine/Proton/Hangover ARM64 WoW64 (`scripts/install-hangover-arm64.sh`) and `src/compat/wl_clamp.c`, with MZ/PE inspection, real `RT_GROUP_ICON`/`.ico` extraction (`pe_icon.cpp`), per-app prefixes, and Uninstall-key registry discovery. See [docs/windows-app-coverage.md](windows-app-coverage.md).
5. **macOS applications** (`.app` bundles in `~/.local/share/finn/macos/Applications` or `~/Applications`) launched via **`finn-darwin`** (`src/darwin/`): clean-room 64-bit Mach-O/Universal dynamic loader (`LC_DYLD_CHAINED_FIXUPS`, `LC_DYLD_INFO_ONLY`, `@rpath` dylibs), aarch64/x86_64 `objc_msgSend` assembly trampolines, subset `libSystem`/CoreFoundation/Foundation/CoreGraphics/QuartzCore/Swift/SwiftUI, `AppKit-on-Qt6` bridge, and `.app`/`.pkg`/`.zip`/`.dmg` installer. See [docs/darwin-framework-coverage.md](darwin-framework-coverage.md).
6. **OpenHarmony 6.1 LTS applications (API Level 23)** installed in `~/.local/share/finn/ohos/` and launched via **`finn-ohos`** (`src/ohos/`): native zlib `.hap`/`.hsp`/`.app` + HTTP/HTTPS installer, `module.json`/`pack.info`/`resources.index` parser, host-ABI NAPI `.so` `dlopen` bridge, subset Panda `modules.abc` VM (`UIAbility` + core `@ohos.*` kits), and `ArkUI-on-Qt6` renderer with static bytecode/manifest UI reconstruction fallback. See [docs/ohos-kit-coverage.md](ohos-kit-coverage.md).
7. **Android 17 applications (API 37)** installed in `~/.local/share/finn/android/` and inspected/previewed via **`finn-android`** (`src/android/`): native zlib APK/split-APK (`.apk`/`.xapk`/`.apkm`/`.apks`) installer, binary AXML & `resources.arsc` parser, DEX verifier/disassembler, 14-widget Qt 6 AXML layout previewer, and 16-opcode integer micro-interpreter — no Waydroid. See [docs/android-coverage.md](android-coverage.md).

## Custom Qt 6 stack, tokens & motion

Both `FinnSDK` apps and unpatched third-party Qt 6 apps run with:
- `QT_QPA_PLATFORMTHEME=finn` (`libfinnplatformtheme.so`): applies the palette generated from `ui/_tokens.fui`, loads `Fixel Display`, maps icons from `/usr/share/finn/icons/`, and watches `/var/lib/finn` + `~/.config/finn` for live theme, accent, and reduced-motion updates without restarting apps.
- `QT_STYLE_OVERRIDE=finn` (`libfinnstyle.so`): custom `QProxyStyle` painting tokenized radii, 1 px hairlines, pill sliders, capsule scrollbars, and vector indicators.
- `src/ui/finn/spring.hpp`: analytic damped-harmonic-oscillator spring solver (`SpringAnimator`, `kSnappy`/`kSmooth`/`kBouncy`/`kGentle`/`kOverscroll`, rubber-band overscroll) exposed to both C++ widgets and QML (`FinnSpringValue`), with live reduced-motion fallbacks.
- `src/compat/finn_qt_theme.hpp`: applies the live FinnOS palette, typography, and `finn` widget style to `finn-darwin` and `finn-ohos` windows.

## What is not here yet

- **Compositor gaps (`finn-comp`)**:
  - `startXwaylandIfEnabled()` creates a `QProcess` for `Xwayland -rootless -noreset` but does not yet call `start()` or write `/run/finn/xdisplay`.
  - `dual_kawase_blur.frag` is compiled when `Qt6ShaderTools` is present, but is not yet wired to a QML `ShaderEffect` — backdrop blur and Liquid Glass run on CPU.
  - `wlr-layer-shell`, `wlr-screencopy`, `wlr-foreign-toplevel-management`, and `ext-session-lock-v1` are not bound (`src/protocol/*.xml` are unused); file-based IPC under `/run/finn/state/` is used instead.
  - Global compositor keyboard shortcuts (`handleShortcutKey()`) are not yet connected to QML/event-filter input, and `session_locked_` in `CompositorServer` is not yet cleared on unlock.
  - Physical DRM/KMS (`eglfs_kms`) multi-monitor arrangement, mixed per-output HiDPI, and `seatd`-mediated libinput opening (`startLibinputGestures()` currently calls `::open()` directly) remain incomplete.
- **Shell gaps (`finn-shell`)**:
  - `TabletShell.qml`, `MobileShell.qml`, and `TvShell.qml` are currently 6-line empty `Item` stubs; only `DesktopShell.qml` is implemented (while `FinnApplicationWindow`, `finn-oobe`, and `recovery-mode` do adapt across form factors).
  - There is no `org.freedesktop.Notifications` D-Bus daemon yet; `NotificationListModel` is in-process only.
  - `ShellCorona` does not yet link `FinnDesktopServices`, so Control Center Bluetooth and MPRIS media probes remain unavailable in the shell (they work in `Settings` and `OOBE`), and Wi-Fi reports `"Connected"`/`"Off"` rather than the active SSID.
  - `openSettings(pageId)` launches `finn-settings` without forwarding `pageId`.
- **Android execution (`finn-android`)**: Parses and verifies real APKs and previews classic AXML layouts in Qt 6, but does **not** execute real third-party Android apps (no ART, Binder, Zygote, JNI, Jetpack Compose runtime, or GMS; only a 16-opcode integer micro-interpreter exists). No Waydroid fallback.
- **macOS (`finn-darwin`) & OpenHarmony (`finn-ohos`) coverage**: `finn-darwin` runs simple same-arch 64-bit Mach-O C/ObjC/AppKit (and trivial SwiftUI) binaries; Metal, WebKit, CoreData, Combine, Security/Keychain, and full SwiftUI are unimplemented. `finn-ohos` installs `.hap`/`.hsp`/`.app`, loads host-ABI NAPI `.so`, and runs a subset Panda VM / static UI reconstruction; hardware/distributed kits (`ArkWeb`, Telephony, NFC, Camera, Sensor, MapKit) are unbridged.
- **No OS-level native app sandbox or enforced `.finnapp` permissions**: Native apps run with user `finn` privileges (`linux_sandbox=env-only`). `linux_sandbox` strips loader/plugin hijack variables and can plan an optional `bwrap` command if `bwrap` is installed, but this is not an enforced permission sandbox. AppImages are not signature-verified, and there is no GTK theme for third-party GTK apps.
- **Verified boot, hosted repo & production OTA keys**: `finn-boot`, `finn-update`, and `repo/` scripts are implemented, but Secure Boot / Shim / SBAT, `dm-verity`, TPM2 sealing, live `repo.finnos.org` hosting (`finnos-keyring` ships an empty placeholder keyring), and production OTA signing keys are not yet provisioned.
- **Partial accessibility & localization**: High Contrast and Reduced Motion apply live; `text_scale`, `color_filter`, `sticky_keys`, `filter_keys`, `orca` screen reader integration, `QTranslator` localization bundles, and cellular modem/telephony (`ModemManager`) are not yet implemented.
- **No bespoke `finnd` daemon and no weather backend**: Privileged system-wide `pacman` installs are not driven by `Finn Store` (which supports Curated, Flathub/Flatpak, Android APKs, `winget`, and local sideloading). The clock pill displays real local time without fabricated weather.
- **No AI in the tree**: FinnAI is reserved, off by default, and not implemented.
