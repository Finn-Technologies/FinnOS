# Architecture

The processes and shared libraries that make up FinnOS.

| Component | Owns | Runs as |
|---|---|---|
| `finn-comp` | DRM/headless output, seats, xdg-shell, layer-shell, foreign-toplevel, Xwayland bridge, screencopy | root |
| `libfinnui` (`FinnSDK`) | Qt6 C++20 UI library: tokens, theme manager, custom CSD chrome (`FinnWindow`), LayerShell windows, custom widgets, `SessionClient` | shared library |
| `libfinnplatformtheme` & `libfinnstyle` | Custom Qt6 `QPlatformTheme` and `QProxyStyle` injected via `QT_QPA_PLATFORMTHEME=finn` & `QT_STYLE_OVERRIDE=finn` | Qt6 plugins |
| `finn-session` | Unified app catalogue, `.desktop`/Flatpak/Wine discovery, launch broker, runtime compat probe | `finn` |
| `finn-shell` | Dock, active-window indicators, Home Menu, Spotlight Search, Control Center, notifications, lock screen | `finn` |
| `finn-wallpaper` | Background WebP wallpaper surface (`background` layer) | `finn` |
| First-party apps | `finn-settings`, `finn-files`, `finn-terminal`, `finn-notes`, `finn-store`, `finn-viewer`, `finn-monitor` | `finn` |

`finnd` — a bespoke privileged system daemon — is not written. Instead, FinnOS integrates directly with standard upstream Linux mechanisms (systemd `logind`, `NetworkManager`, `BlueZ`, `PipeWire`/`wpctl`, `UPower`, and sysfs) when present, and marks missing hardware/services honestly disabled when absent.

## The display

`finn-comp` uses wlroots 0.20. The backend is `drm` in a hardware session or `headless` for VM/automated verification. It advertises `wl_compositor`, `wl_shm`, `wl_subcompositor`, `wl_data_device_manager`, `xdg_wm_base`, `zwlr_layer_shell_v1`, `zwlr_foreign_toplevel_manager_v1`, `zwlr_screencopy_manager_v1`, `zxdg_output_manager_v1`, and Xwayland on demand when X11 support is enabled.

The scene graph has one fixed stacking order:

```
background → background layer → bottom layer → app toplevels → top layer → overlay
```

`finn-wallpaper` binds to the `background` layer, decodes `/usr/share/finn/wallpaper/default.webp` through Qt, and applies theme dimming. `finn-shell` surfaces bind to `top` (dock) and `overlay` (popovers, toasts, lock screen) with strict pointer input masks so transparent overlay regions pass clicks cleanly to apps beneath.

There is no server-side decoration. Native `FinnSDK` apps use `FinnWindow` (custom frameless client-side decoration with Figma window controls, collapsible sidebar slot, edge resize, and drag-move via Wayland `startSystemMove`/`startSystemResize`). Foreign Wayland/Qt/GTK/X11 clients keep their native decorations or window frames cleanly.

Window management keys:
- `Alt+F1`: cycle focus across app toplevels
- `Super` / `Meta` (or Dock Home button): Home Menu
- `Alt+Space`: Spotlight Search
- `Ctrl+Alt+F2`: rescue console (`getty@tty2`)

A developer restarts the compositor deliberately with `systemctl restart finn-comp`.

## Authority

- The Wayland display socket lives at `/run/finn/wayland-0` (`0660`, group `finn`), and Xwayland sockets live under `/tmp/.X11-unix` (`1777 root:root`) because `finn-comp` runs as root (using `seatd` via `LIBSEAT_BACKEND=seatd`) while all user services and apps run as user `finn`. Services omit `PrivateTmp=yes` so Xwayland and Wine/X11 clients share `/tmp/.X11-unix`.
- `finn-session` runs as `finn` on `$XDG_RUNTIME_DIR/finn/session.sock` (`0600`), checks `SO_PEERCRED`, and spawns launched apps via `posix_spawn`.
- There is no TCP listener and no hidden second door.

## The app model & compatibility

An app in FinnOS is a program opening an `xdg_toplevel` (Wayland) or Xwayland surface bridged into the scene. Nothing under `src/comp` knows any app id (`make lint` enforces this).

`finn-session` aggregates four real sources into one launcher model:
1. **First-party native apps** from `/usr/share/finn/apps.registry` + `/var/lib/finn/installed`.
2. **Third-party `FinnSDK` apps** registered in `~/.local/share/finn/apps/`.
3. **Linux applications** discovered from Freedesktop `.desktop` directories and Flatpak exports.
4. **Windows executables** registered in `~/.local/share/finn/windows.conf` and launched via Wine when available on `x86_64`.

## Custom Qt6 stack & theming

FinnOS does not leave Qt6 with stock desktop widgets. Both `FinnSDK` apps and unpatched third-party Qt6 apps run with:
- `QT_QPA_PLATFORMTHEME=finn` (`libfinnplatformtheme.so`): applies FinnOS palette generated from `ui/_tokens.fui`, loads `Fixel Display` fonts, maps semantic icons from `/usr/share/finn/icons/`, and watches `/var/lib/finn/settings` + `~/.config/finn/settings` for live dark/light and accent updates without restarting apps.
- `QT_STYLE_OVERRIDE=finn` (`libfinnstyle.so`): custom `QProxyStyle` rendering flat hairline borders, consistent radii (`kRadiusSm/Md/Lg`), clean button/input/slider/checkbox states, and refined scrollbars matching Figma tokens.

## What is not here yet

- **No bespoke privileged `finnd` daemon.** Package installation inside `Finn Store` supports unprivileged per-user Flatpak operations (`flatpak --user`); system-wide `pacman` installs give exact commands for the user to authorize in `Finn Terminal`.
- **No weather forecast backend.** The clock pill displays real local time without fabricated temperatures or forecasts.
- **No OS-level native app sandbox.** Native C++20/Qt6 apps run directly with user `finn` privileges; sandboxed distribution uses `flatpak`.
- **No first-party native web browser binary.** Browsers (such as Firefox or Chromium) are installed/launched as Linux desktop or Flatpak apps.
- **Single output & integer scaling focus; no multi-monitor layout editor, custom compositor Gaussian blur shader, or A/B OTA partition slots.**
- **ARM64 Wine runtime**: `base/packages.aarch64` does not invent unavailable upstream `wine` packages; `probe_wine_runtime()` inspects binaries/prefixes honestly and keeps launch buttons disabled until a compatible runtime (`wine` + optional translator) exists on the machine.
