# Code map

Read this after [architecture.md](architecture.md). It is the shortest path
from "I just opened the repository" to "I know where a window is drawn and
where a key is handled".

## The ten-minute tour

1. [`src/common/paths.hpp`](../src/common/paths.hpp) — every shared path.
2. [`src/session/app_registry.cpp`](../src/session/app_registry.cpp) — catalogue and installed list.
3. [`src/session/main.cpp`](../src/session/main.cpp) — the launch broker.
4. [`src/ui/FinnSDK/FinnSDK.hpp`](../src/ui/FinnSDK/FinnSDK.hpp) — public C++20 + Qt6 SDK header.
5. [`src/ui/finn/window.cpp`](../src/ui/finn/window.cpp) — custom CSD window chrome and live theme updates.
6. [`src/qtplugin/`](../src/qtplugin/) — `finnplatformtheme` + `finnstyle` Qt6 plugins.
7. [`src/shell/main.cpp`](../src/shell/main.cpp) — dock, Spotlight search, Home menu, Control Center, toasts.
8. [`src/comp/server.cpp`](../src/comp/server.cpp) — the wlroots 0.20 compositor.
9. [`src/comp/wlroots.hpp`](../src/comp/wlroots.hpp) — the one C shim.
10. [`docs/motion.md`](motion.md) — durations, easings, and motion tokens.

## Processes and files

```
finn-comp (src/comp)
  main.cpp         argument parsing and process entry
  server.cpp/hpp   display, scene layers, pointer/touch/tablet, ext-session-lock-v1, output, timers
  window.cpp       xdg_toplevel + Xwayland + fullscreen stacking, SSD, entrance motion
  layer.cpp        wlr-layer-shell surface placement and exclusive zones
  xwayland_shim.c  minimal C helper for Xwayland surface properties
  wlroots.hpp      the C-header shim (the only macro trick in the tree)

finn-session (src/session)
  main.cpp         unix socket, request verbs, posix_spawn, CLI
  client.cpp/hpp   client helper shared with the shell and apps
  app_registry.*   catalogue + /var/lib/finn/installed merger

libfinnui / FinnSDK (src/ui)
  FinnSDK/         installed SDK umbrella & forwarding headers
  finn/application.* Qt6 Wayland client bootstrap, Fixel Display font, Finn style
  finn/window.*    custom frameless CSD FinnWindow (`FinnFormFactor`) + FinnLayerWindow
  finn/theme.*     FinnThemeManager reading settings & FINN_DARK live
  finn/tokens.hpp  typed FinnTokens + generated_tokens.hpp bridge
  finn/spring.hpp  analytic spring solver, SpringAnimator, rubber-band overscroll
  finn/components.* FinnButton, FinnCard, FinnToggle, FinnSlider, FinnSearchField, etc.
  finn/animation.hpp motion tokens, easing curves, and live reduced_motion() reader

Qt6 platform customization (src/qtplugin)
  finn_platform_theme.*  libfinnplatformtheme.so (QT_QPA_PLATFORMTHEME=finn)
  finn_style.*           libfinnstyle.so proxy style (QT_STYLE_OVERRIDE=finn)

Compat & system helpers (src/compat, src/system)
  desktop_entry.*  XDG .desktop & Flatpak discovery + launch line building
  wine_runner.*    PE header inspection & per-app Wine prefix execution
  android_runner.* Clean-room Android 17 (finn-android) probe and launcher helper
  sys_files.*      settings, users, mounts, /proc memory & CPU helpers
  desktop_services.* real NetworkManager/BlueZ/UPower/PipeWire probes + secure nmcli connect

finn-shell (src/shell)
  main.cpp         Qt6 layer-shell bootstrap and compositor surface coordination
  shell_layout.hpp Phone/Tablet/Desktop breakpoints, tokenized colors, focus rings
  dock.*           responsive bottom dock / mobile gesture bar, running dots, clock/status pills
  popovers.*       Home menu, Control Center, Window overview, async LockScreenWindow
  search.*         Spotlight search & evaluator
  foreign_windows.* wlr-foreign-toplevel-management tracker
  notifications.*  floating FinnToast notifications

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
  installer        finn-installer (GUI wizard invoking finn-install.sh --disk --hostname --timezone)
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
