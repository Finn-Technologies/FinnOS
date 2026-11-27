# Development

Building, testing, deploying to the dev VM, and inspecting logs on FinnOS.

## Build machine & dependencies

The full graphical build runs on a pacman-based Linux host (`x86_64` or `aarch64`) or inside the Lima/VMware dev VM. Host Python tests and pure-C++20 unit tests (`make test`, `make test-unit`, `make lint`) also run on macOS.

```sh
sudo pacman -S base-devel cmake ninja pkgconf \
    qt6-base qt6-declarative qt6-wayland qt6-svg qt6-shadertools \
    qt6-5compat layer-shell-qt \
    wayland wayland-protocols libxkbcommon libinput libdrm mesa seatd \
    systemd-libs freetype2 fontconfig zlib zstd
```

Optional dependencies:
- `qt6-webengine` — enables building **Flourish** (`apps/flourish`)
- `clang` + `lld` — builds the freestanding UEFI bootloader (`make boot`)

## Make targets

```sh
make                 # session, compositor, Qt6 plugins, FinnUI/FinnUIQml, shell, all apps, tools, compat runtimes
make boot            # freestanding UEFI bootloader (finn-bootx64.efi, finn-bootaa64.efi)
make compositor      # finn-comp only
make shell           # finn-shell only
make ui              # libfinnui & libfinnui_qml
make qtplugin        # libfinnplatformtheme & libfinnstyle
make apps            # all native apps (wallpaper, settings, files, terminal, notes, store,
                     # viewer, monitor, calculator, archive, capture, recovery-mode, oobe)
make session         # finn-session (pure C++20)
make darwin          # finn-darwin standalone (headless)
make ohos            # finn-ohos standalone (headless)
make android         # finn-android standalone (headless)
make test            # host Python test suite
make test-unit       # C++20 ASan/UBSan unit tests
make bench           # host blur kernel microbenchmark
make lint            # diff hygiene, generated tokens check, compositor app-id ban, branding check
sudo make install    # install binaries, plugins, FinnSDK, assets, systemd units, and sdk/template
```

## Editing design tokens

`ui/_tokens.fui` is the single source of truth for palette, spacing, radii, blur, motion, and breakpoints. After editing it:

```sh
python3 tools/tokens.py ui/_tokens.fui src/ui/generated_tokens.hpp
make lint
```

## Dev VM workflow

```sh
vm/deploy.sh               # sync tree to VM, build, sudo make install, restart systemd session
vm/shot.sh out.png         # capture the live VM desktop
vm/restart-compositor.sh   # restart finn-comp cleanly on the VM
```

Override the target host with `FINN_HOST=finn@<ip>` (defaults to `VM ?= finn@10.211.55.6` in `Makefile`). See [vm/README.md](../vm/README.md) for Lima (Apple Silicon) and VMware Workstation (`x86_64`) setup.

## Running `finn-comp` in preview / offscreen mode

On macOS or Linux hosts without DRM/KMS:

```sh
QT_QPA_PLATFORM=offscreen ./build/src/comp/finn-comp --preview
```

On a Linux TTY with DRM/KMS:

```sh
LIBSEAT_BACKEND=seatd QT_QPA_PLATFORM=eglfs sudo ./build/src/comp/finn-comp
```

## Checking logs on the VM

```sh
journalctl -u finn-comp -u finn-recovery -n 100 --no-pager
journalctl --user -u finn-session -u finn-shell -u finn-wallpaper -n 100 --no-pager
```
