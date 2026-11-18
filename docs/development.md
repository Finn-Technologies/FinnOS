# Development

The host can run pure C++20 unit tests and Python checks. Full Wayland, Qt6,
and compositor builds run on a pacman-based Linux host or the dev VM.

## Build and test

```sh
make              # finn-comp, finn-session, libfinnui, qtplugins, finn-shell, apps, tools
make test         # host Python tests: base wiring, catalogue, tokens, icons
make test-unit    # C++20 unit tests under ASan and UBSan
make lint         # style, generated tokens check, no app ids in src/comp
make install      # installs binaries, libfinnui, FinnSDK CMake config, Qt6 plugins, assets
```

Package dependencies (matching `base/packages.*`):

```sh
sudo pacman -S base-devel cmake ninja pkgconf wlroots0.20 libsystemd wayland \
    wayland-protocols libxkbcommon libdrm libinput seatd grim mesa \
    qt6-base qt6-wayland qt6-svg qt6-imageformats qt6-tools qt6-declarative layer-shell-qt \
    xorg-xwayland xdg-desktop-portal xdg-desktop-portal-wlr \
    desktop-file-utils shared-mime-info upower flatpak \
    pipewire pipewire-pulse networkmanager bluez
```

## Deploy to the VM

```sh
vm/deploy.sh
vm/shot.sh /tmp/shot.png
```

On Apple Silicon using Lima (`vm/lima-setup.sh`), pass the Lima connection
variables printed by setup:

```sh
export FINN_HOST=lima-finnos FINN_SSH_OPTS="-F $HOME/.lima/finnos/ssh.config"
vm/deploy.sh
vm/shot.sh /tmp/shot.png
```

`vm/deploy.sh` streams the source tree over tar to `~/finnos`, installs any
missing packages, runs `make` and `sudo make install`, runs `ldconfig`,
updates `/var/lib/finn/installed`, installs the units, enables linger for
`finn`, and restarts the session.

## Run it by hand

Headless, in a private runtime directory:

```sh
export XDG_RUNTIME_DIR=/tmp/finn-dev
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

WLR_BACKENDS=headless WLR_RENDERER=pixman src/comp/finn-comp &

XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
WAYLAND_DISPLAY=wayland-0 src/session/finn-session &

XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland \
QT_QPA_PLATFORMTHEME=finn QT_STYLE_OVERRIDE=finn \
build/src/shell/finn-shell &

XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
WAYLAND_DISPLAY=wayland-0 src/session/finn-session --launch files
```

Capture a frame:

```sh
XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY=wayland-0 grim /tmp/shot.png
```

On DRM (`seatd`), `finn-comp` exports `/run/finn/wayland-0` (`0660 root:finn`)
and clients run as `finn` with `WAYLAND_DISPLAY=/run/finn/wayland-0`.
See the systemd unit files under `base/` for canonical environments.

## Logs

- `journalctl -u finn-comp -b`
- `journalctl -u finn-session -b`
- `journalctl -u finn-shell -b`
- `journalctl -u finn-wallpaper -b`

## App development with FinnSDK

Check your environment and scaffold an app inside FinnOS:

```sh
finn-sdk check
finn-sdk new hello --name "Hello FinnOS"
finn-sdk build hello
finn-sdk run hello
```

Or copy [`sdk/template`](../sdk/template) and link `FinnSDK::FinnUI` in CMake.
See [sdk.md](sdk.md) and [apps.md](apps.md).
