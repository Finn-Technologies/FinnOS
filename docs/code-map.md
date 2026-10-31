# Code map

Read this after [architecture.md](architecture.md). It is the shortest path
from "I just opened the repository" to "I know where a window is drawn and
where a key is handled".

## The ten-minute tour

1. [`src/common/paths.hpp`](../src/common/paths.hpp) — every shared path.
2. [`src/session/app_registry.cpp`](../src/session/app_registry.cpp) — what a
   catalogue entry is.
3. [`src/session/main.cpp`](../src/session/main.cpp) — the launch broker.
4. [`src/ui/finn/ui.hpp`](../src/ui/finn/ui.hpp) — the public app API.
5. [`src/ui/app.cpp`](../src/ui/app.cpp) — the Wayland client behind that API.
6. [`src/shell/main.cpp`](../src/shell/main.cpp) — the dock and menu.
7. [`src/comp/server.cpp`](../src/comp/server.cpp) — the display server.
8. [`src/comp/wlroots.hpp`](../src/comp/wlroots.hpp) — the one C shim.
9. [`base/`](../base/) — how the session is installed.
10. [`docs/motion.md`](motion.md) — why things move the way they do.

## Processes and files

```
finn-comp (src/comp)
  main.cpp        argument parsing and the process entry
  server.cpp      display, scenes, seats, toplevels, windows, animations
  server.hpp      the class; private nested Output/Toplevel/Keyboard/Layer
  wlroots.hpp     the C-header shim (the only macro trick in the tree)

finn-session (src/session)
  main.cpp        socket, request handling, posix_spawn, CLI
  client.cpp      the client the shell and scripts use
  app_registry.*  catalogue + installed list

libfinnui (src/ui)
  finn/ui.hpp     the public API: App, LayerApp, Canvas, Input, Theme
  finn/animation.hpp  motion tokens, easing, Animation
  canvas.cpp      software pixels: shapes, text, icons, widgets
  app.cpp         Wayland connection, buffers, input, xkb, theme
  theme.cpp       the generated colour tokens
  raster/         the proven SVG and Fixel text rasterisers

finn-shell (src/shell)
  main.cpp        layer-shell overlay, dock, status, menu, motion

apps/<id>
  main.cpp        one App subclass, one draw method
  wallpaper       a background LayerApp that decodes the WebP
```

## Flows

### A window is opened

```
dock/menu click or `finn-session --launch files`
  → finn-session checks the catalogue + installed list
  → posix_spawn with WAYLAND_DISPLAY=/run/finn/wayland-0
  → app connects, creates xdg_toplevel
  → finn-comp `on_new_toplevel` creates a scene tree in toplevel_tree_
  → `on_toplevel_map` starts the 220 ms entrance and focuses it
  → app draws into a wl_shm buffer; the scene composites it
```

The compositor never chooses an app; it chooses a rectangle.

### A key or pointer event arrives

```
libinput → wlr_seat → focused surface → libfinnui keyboard/pointer listener
  → Input for the frame → Canvas widget → App::draw
```

The shell's layer surface is a surface like any other. It asks for keyboard
focus only while the menu is visible; otherwise the keys belong to the app.

### The theme changes

```
Settings writes $HOME/.config/finn/settings
  → libfinnui re-reads it on its next frame
  → the shell re-reads it on its one-second tick
  → finn-comp re-reads it on its one-second timer and recolours the background
```

No process is restarted.

### The session starts

```
systemd → finn-comp (root, seatd, /run/finn/wayland-0)
        → finn-session (finn, /run/user/1000/finn/session.sock)
        → finn-shell (finn, a layer-shell client)
```

`base/setup.sh` enables the three only when all three binaries exist, and
enables linger so `/run/user/1000` exists first.

## Where to add things

| I want to… | Edit |
|---|---|
| add an app | `apps/<id>/`, one registry line, one installed line |
| add a widget | `src/ui/finn/ui.hpp`, `canvas.cpp`; test the state, not just the pixels |
| change the dock | `src/shell/main.cpp`, `draw_dock` |
| change window behaviour | `src/comp/server.cpp`, `add_toplevel` / `focus` / `animate_windows` |
| change launch | `src/session/main.cpp`, `launch_reply` |
| change the palette | `ui/_tokens.fui`, then `make lint` (the header is generated) |
| change motion | `src/ui/finn/animation.hpp`, and document it in [motion.md](motion.md) |
| add a path | `src/common/paths.hpp`, never a literal in two files |

## Rules that keep the map true

- The compositor has no app ids. `make lint` checks it.
- The shell has no special case for a first-party app.
- A scene surface goes into `layer_trees_[current.layer]` or
  `toplevel_tree_`; never directly into `scene_->tree`.
- The only macro trick is the wlroots C shim. If you find yourself reaching
  for another, the boundary is in the wrong place.
- Every listener belongs to a struct with a destructor that detaches it.
- Motion is a surface decision, not a per-widget invention; use the tokens.
