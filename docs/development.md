# Development

The host is an editor. The build machine is an Arch machine or the dev VM; it
has the DRM, Wayland, wlroots, FreeType and input headers the compositor needs.

## Build and test

```sh
make                 # libfinnui, finn-comp, finn-session, finn-shell, apps
make test            # host tests: registry, tokens, base wiring
make test-rewrite    # the C++ unit test, under ASan and UBSan
make lint            # style, generated tokens, no app ids in the compositor
```

Dependencies:

```sh
sudo pacman -S base-devel pkgconf wlroots0.20 libsystemd wayland \
    wayland-protocols libxkbcommon freetype2 fontconfig libdrm libinput \
    mesa seatd grim
```

## Deploy to the VM

```sh
vm/deploy.sh
vm/shot.sh /tmp/shot.png
```

`vm/deploy.sh` sends the tree as a tar stream (the VM has no rsync), builds it
in `~/finnos`, installs it, installs the units, enables linger for `finn` and
restarts the session. `vm/shot.sh` captures with grim against
`/run/finn/wayland-0`.

## Run it by hand

The compositor is the only program that needs the display device. The session,
the shell and the apps are clients and can be started one at a time.

Headless, in a private runtime directory:

```sh
export XDG_RUNTIME_DIR=/tmp/finn-dev
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

WLR_BACKENDS=headless WLR_RENDERER=pixman src/comp/finn-comp &

XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
WAYLAND_DISPLAY=wayland-0 src/session/finn-session &

XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
WAYLAND_DISPLAY=wayland-0 src/shell/finn-shell &

XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
WAYLAND_DISPLAY=wayland-0 src/session/finn-session --launch files
```

Capture it:

```sh
XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY=wayland-0 grim /tmp/shot.png
```

DRM needs a seat. On a machine with `seatd` running, the compositor opens the
GPU through it; the display socket is `/run/finn/wayland-0`, so the clients
use an absolute `WAYLAND_DISPLAY` and the user's own `XDG_RUNTIME_DIR`:

```sh
export XDG_RUNTIME_DIR=/run/finn
WLR_BACKENDS=drm LIBSEAT_BACKEND=seatd src/comp/finn-comp

sudo -u finn env XDG_RUNTIME_DIR=/run/user/1000 \
    WAYLAND_DISPLAY=/run/finn/wayland-0 src/session/finn-session
```

The unit files in `base/` are the canonical version of those environments.

## Looking at a change

A compositor change that compiles is not a compositor change that works.
Capture the frame and read it. The old `finnctl` path is gone; use
`vm/shot.sh` or grim.

## Logs

- `journalctl -u finn-comp -b`
- `journalctl -u finn-session -b`
- `journalctl -u finn-shell -b`
- a headless run logs to stderr.

The compositor logs the layer-surface names and the toplevel map/unmap
events at `INFO`; a missing map is the first thing to look for.

## App development

Use [sdk/template](../sdk/template), link against the installed
`libfinnui.a`, and keep the app in its own directory. See
[apps.md](apps.md).
