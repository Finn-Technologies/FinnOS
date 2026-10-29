# Architecture

The programs and the library. There is no hidden fifth process.

| Program | Owns | Runs as |
|---|---|---|
| `finn-comp` | DRM/headless output, seats, xdg-shell, layer-shell, window management, screencopy | root |
| `libfinnui` | the app runtime: canvas, text, vector, widgets, Wayland client | a library |
| `finn-shell` | the dock, clock and app menu, as a layer-shell client | `finn` |
| `finn-session` | the registry, the installed list and app launch | `finn` |
| first-party apps | their own windows | `finn` |

`finnd` — the root authority for power, state, packages and updates — is not
written yet. [roadmap.md](roadmap.md) says what that means for v1.

## The display

`finn-comp` uses wlroots 0.20. The backend is `drm` in a session or `headless`
for tests. It advertises `wl_compositor`, `wl_shm`, `wl_subcompositor`,
`wl_data_device_manager`, `xdg_wm_base`, `zwlr_layer_shell_v1`,
`zwlr_screencopy_manager_v1` and `zxdg_output_manager_v1`.

The scene graph is ordered once:

```
background → background layer → bottom layer → app toplevels → top layer → overlay
```

That is why a layer-shell surface stays above app windows without the
compositor knowing an app id. The background is a `wlr_scene_rect`; the theme
is re-read once a second so a settings change is visible without a restart.

There is no server-side decoration. The compositor does not draw a titlebar
and does not advertise a decoration protocol; a GTK app keeps its own header
bar, and a native app draws the frame `libfinnui` provides. There is exactly
one chrome per window.

Window management is in the compositor: move and resize requests from the
client, maximize to the usable box, focus on click, `Alt+F1` to cycle and
`Alt+Escape` to quit. A `wlr_scene` commit happens only when something
changed.

## Authority

- The display socket is `/run/finn/wayland-0`, `0660`, group `finn`. It is in
  the shared runtime directory because the compositor is root and the apps are
  `finn`; root's `$XDG_RUNTIME_DIR` is not reachable by an app. Clients use
  the absolute path in `WAYLAND_DISPLAY`.
- Apps are started by `finn-session`, which runs as `finn` and listens on
  `$XDG_RUNTIME_DIR/finn/session.sock`, `0600`. The protocol is in
  [session-protocol.md](session-protocol.md).
- The session never asks root for anything. It reads the catalogue and the
  installed list and calls `posix_spawn`. The app inherits the user's
  authority and nothing more.
- There is no TCP listener and no second door.

## The app model

An app is a program that opens an `xdg_toplevel`. That is the contract. The
registry carries the metadata:

```
id | Name | icon | command | note | source
```

The OS ships `/usr/share/finn/apps.registry`; the package manager owns
`/var/lib/finnpkg/installed`. `finn-session` combines them. Adding an app
changes its registry line and its own directory; nothing under `src/comp` may
contain an app id, and `make lint` fails if it does.

First-party apps link `libfinnui`. Foreign apps are unmodified toolkits and
read the same display socket.

## Rendering and performance

An app draws into a shared-memory ARGB buffer through `Canvas`; `App` owns the
`wl_surface`, the buffer pool, the frame callback and the input translation.
It draws on input, on a resize and when it asks for another frame. The shell
ticks once a second for the clock, not sixty times.

`Canvas` caches glyphs per size and icons per (path, size, tint). A frame is
therefore a blend pass, not a text layout pass. The compositor composites the
app surfaces, the shell layers and the background through the wlroots renderer
and only commits damaged output.

## The launch path

```
shell / script → finn-session (as finn) → posix_spawn → app → xdg_toplevel
```

The app gets `WAYLAND_DISPLAY=/run/finn/wayland-0`, the user's
`XDG_RUNTIME_DIR`, `HOME`, `PATH`, `XDG_SESSION_TYPE=wayland`,
`XDG_CURRENT_DESKTOP=FinnOS` and any `FINN_DARK`, `FINN_ICON_DIR` or
`FINN_FONT` the session was given.

## What is not here yet

- **No finnd.** Power, brightness, volume, wireless, package installs, OTA
  updates and the audit log do not exist in v1. The compositor does not
  change the machine's power state.
- **No sandbox.** Apps run as `finn` with the user's full authority. A
  per-app transient unit or a namespace sandbox is the next security step.
- **No package manager.** The installed list is written by the installer.
  Store, Flatpak, Wine and winget are not shipped.
- **No browser.** The previous browser work is on `pre-rewrite`, not ported.
- **One output, one scale.** No multi-monitor, no per-output scaling, no
  automatic HiDPI, no touch or input methods.
- **No screen lock, session switching or logout control.**
- **No server-side decoration.** Client-side chrome only.
- **No clipboard manager policy.** `wl_data_device_manager` is advertised;
  the session does not implement a clipboard history.
- **No A/B update slots and no package signatures.**
