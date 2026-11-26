# Code map

Read this after [architecture.md](architecture.md). It is the shortest path
from "I just opened the repository" to "I know where a window is drawn and
where a key is handled".

## The ten-minute tour

1. [`src/common/paths.hpp`](../src/common/paths.hpp) — every shared path.
2. [`src/session/app_registry.cpp`](../src/session/app_registry.cpp) — catalogue and installed list.
3. [`src/session/main.cpp`](../src/session/main.cpp) — the launch broker.
4. [`src/ui/FinnSDK/FinnQml.hpp`](../src/ui/FinnSDK/FinnQml.hpp) & [`src/ui/FinnSDK/FinnSDK.hpp`](../src/ui/FinnSDK/FinnSDK.hpp) — public C++20 + Qt 6 SDK headers (`FinnUIQml` & `FinnUI`).
5. [`src/ui/qml/controls/FinnApplicationWindow.qml`](../src/ui/qml/controls/FinnApplicationWindow.qml) — Kirigami-inspired adaptive Desktop/Tablet/Mobile/TV app window.
6. [`src/qtplugin/`](../src/qtplugin/) — `finnplatformtheme` + `finnstyle` Qt6 plugins.
7. [`src/shell/core/shell_corona.cpp`](../src/shell/core/shell_corona.cpp) & [`src/shell/ui/SystemUI.qml`](../src/shell/ui/SystemUI.qml) — unified adaptive System UI (Desktop, Tablet, Mobile, TV).
8. [`src/comp/server.cpp`](../src/comp/server.cpp) & [`src/comp/core/workspace_model.cpp`](../src/comp/core/workspace_model.cpp) — Qt 6 Wayland compositor (KWin-inspired).
9. [`src/comp/shaders/dual_kawase_blur.frag`](../src/comp/shaders/dual_kawase_blur.frag) — GPU Dual-Kawase glass backdrop blur shader.
10. [`docs/motion.md`](motion.md) — durations, easings, and motion tokens.

## Processes and files

```
finn-comp (src/comp)
  main.cpp               argument parsing and process entry
  server.cpp/hpp         Qt 6 Wayland compositor server (with host preview fallback), lock/idle timers, sd_notify
  core/scene_stack.hpp   strict 8-layer scene stacking contract
  core/workspace_model.* virtual desktops, focus history, snap/maximize/minimize state, zero app IDs
  core/input_router.*    Super+L / Alt+Tab / Alt+F4 / TV D-pad spatial focus routing
  qml/CompositorScene.qml hardware-accelerated Qt Quick compositor scene
  shaders/dual_kawase_blur.frag GPU Dual-Kawase frosted glass shader

finn-session (src/session)
  main.cpp         unix socket, request verbs, posix_spawn, CLI
  client.cpp/hpp   client helper shared with the shell and apps
  app_registry.*   catalogue + /var/lib/finn/installed merger

libfinnui & libfinnui_qml / FinnSDK (src/ui)
  FinnSDK/         installed SDK umbrella headers (FinnSDK.hpp, FinnQml.hpp)
  finn/form_factor.hpp pure C++20 Desktop/Tablet/Mobile/TV form-factor & breakpoint engine
  finn/theme.*     FinnThemeManager reading settings & FINN_DARK live (including form_factor & sound)
  finn/tokens.hpp  typed FinnTokens + generated_tokens.hpp bridge
  finn/spring.hpp  analytic 120 Hz spring solver, SpringAnimator, rubber-band overscroll
  qml/finn_qml_types.* FinnThemeBridge, FinnSpringValue, FinnSquircle, FinnBootMark, FinnAvatar, FinnIconProvider
  qml/finn_qml_app.*   FinnQmlApp harness (Fixel Display, CLI form-factor/theme/screenshot)
  qml/controls/*.qml   19 AOT-compiled FinnUI 1.0 controls (FinnApplicationWindow, FinnGlassPanel, FinnSlider, etc.)

Qt6 platform customization (src/qtplugin)
  finn_platform_theme.*  libfinnplatformtheme.so (QT_QPA_PLATFORMTHEME=finn)
  finn_style.*           libfinnstyle.so proxy style (QT_STYLE_OVERRIDE=finn)

Compat & system helpers (src/compat, src/system)
  desktop_entry.*  XDG .desktop & Flatpak discovery + launch line building
  wine_runner.*    PE header inspection & per-app Wine prefix execution
  android_runner.* Clean-room Android 17 (finn-android) probe and launcher helper
  sys_files.*      settings (incl. form_factor & sound), users, mounts, /proc memory & CPU helpers
  desktop_services.* real NetworkManager/BlueZ/UPower/PipeWire probes + secure nmcli connect

finn-shell (src/shell)
  main.cpp                 unified System UI bootstrap
  core/shell_corona.*      Plasma Corona-inspired ShellCorona, AppCatalogModel, NotificationListModel
  ui/SystemUI.qml          live adaptive root switching Desktop / Tablet / Mobile / TV containments
  ui/common/*.qml          WallpaperLayer, NotificationStack (3-card receding stack), LockScreenOverlay
  ui/desktop/*.qml         DesktopShell, DesktopDock (4-pill), HomeMenuPopover, SpotlightPopover, ControlCenterPopover, OverviewPopover
  ui/tablet/TabletShell.qml hybrid top status bar + 56dp touch dock + floating popovers
  ui/mobile/MobileShell.qml Plasma Mobile homescreen, pull-down Action Drawer, horizontal card Task Switcher, 134x4dp gesture pill
  ui/tv/TvShell.qml        Plasma Bigscreen 10-foot shelves, 1.5x typography, D-pad focus rings, slide-over Quick Settings drawer
  search.*                 Spotlight search & arithmetic evaluator

finn-update & repo tooling (src/ota, ota, repo)
  src/ota/finn-update CLI entry for A/B OTA status, check, download, apply, commit, rollback
  src/ota/finn_ota/*  Ed25519 verify, HTTP Range download, zstd delta, systemd-boot/GRUB backends
  ota/                keygen, publish, grub-ab.cfg, example public key
  repo/               [finnos] pacman repo signing/publishing scripts & finnos-mirrorlist

apps/<id>
  wallpaper        background wlr-layer-shell surface rendering WebP wallpaper
  settings         system Settings (Appearance, Network, Bluetooth, Sound, Display, System Update, Compat, System)
  files            Finn Files (sidebar locations, search, preview, safe copy/symlink ops)
  terminal         Finn Terminal (PTY shell emulator with stateful UTF-8 decode)
  notes            Finn Notes (multi-tab UTF-8/Markdown editor with Save/Discard/Cancel guard)
  store            Finn Store (native/Flatpak/pacman/Wine/.finnapp manager + FinnSDK hub)
  viewer           Finn Viewer (raster + SVG zoom/pan/rotate viewer)
  monitor          Finn Monitor (CPU/RAM telemetry and PID-diffed process manager)
  recovery-mode    recovery-mode (FinnOS Recovery Mode: install, dual-boot, disk utility, boot repair, logs, floating PTY terminal)
  oobe             finn-oobe (first-time setup: language, Wi-Fi, real account setup, appearance)
  flourish         optional Arc-inspired QtWebEngine browser

tools & sdk
  tools/finn-sdk       CLI project creator, validator, signed .finnapp packager, simulator, builder
  tools/finn_open.cpp  URI/file/desktop/.exe launcher (finn-open)
  tools/finn-display   display geometry helper
  tools/tokens.py      ui/_tokens.fui -> src/ui/generated_tokens.hpp
  tools/check-branding.sh distribution branding & os-release lint guard
  sdk/schema/          finn-app.schema.json app manifest schema
  sdk/template/        standalone FinnSDK starter app
  sdk/tests/           host tests for packaging, verification, and simulation
```

## Flows

### A window is opened

```
dock/menu click or `finn-session --launch <id>` (or finn-open)
  -> finn-session checks catalogue + installed list (or DesktopEntry / WineRunner)
  -> posix_spawn / QProcess with WAYLAND_DISPLAY=/run/finn/wayland-0
  -> app connects as xdg_toplevel (or Xwayland surface)
  -> finn-comp `on_new_toplevel` places it in `toplevel_tree_`
  -> `on_toplevel_map` animates the entrance and announces it on `wlr-foreign-toplevel-management`
  -> finn-shell updates running dots and Window Switcher
```

The compositor never chooses an app; it manages scene rectangles.

### The theme changes

```
Settings or Control Center writes $HOME/.config/finn/settings
  -> FinnThemeManager detects file/timer update and emits themeChanged()
  -> libfinnui widgets, finnplatformtheme/finnstyle apps, and finn-shell restyle live
  -> finn-comp re-reads settings on its one-second timer and recolours the backdrop
```

No process is restarted.

### The session starts

```
systemd -> finn-comp (root, seatd, /run/finn/wayland-0)
        -> finn-session (finn, /run/user/1000/finn/session.sock)
        -> finn-wallpaper (finn, background layer-shell client)
        -> finn-shell (finn, top/overlay layer-shell client)
```

## Where to add things

| I want to… | Edit |
|---|---|
| add an app | `apps/<id>/`, `apps/CMakeLists.txt`, `assets/apps.registry`, `base/setup.sh`, `vm/finn-chroot.sh` |
| add an SDK widget | `src/ui/finn/components.hpp`, `components.cpp`, `src/qtplugin/finn_style.cpp` |
| change the dock/popovers | `src/shell/dock.cpp`, `src/shell/popovers.cpp` |
| change window behaviour | `src/comp/server.cpp`, `src/comp/window.cpp` |
| change launch/compat | `src/session/main.cpp`, `src/compat/{desktop_entry,wine_runner}.cpp` |
| change the palette | `ui/_tokens.fui`, then `make lint` (regenerates header check) |
| change motion | `src/ui/finn/animation.hpp`, and document it in [motion.md](motion.md) |
| add a path | `src/common/paths.hpp`, never a literal in two files |

## Rules that keep the map true

- The compositor has no app ids. `make lint` checks it.
- The shell and compositor never fabricate hardware state.
- A scene surface goes into `layer_trees_[current.layer]` or
  `toplevel_tree_`; never directly into `scene_->tree`.
- The only macro trick is `src/comp/wlroots.hpp`.
- Every listener belongs to a struct with a destructor that detaches it.
