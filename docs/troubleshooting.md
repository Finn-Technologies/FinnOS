# Troubleshooting

Start with the journal of the unit that is missing, then inspect the sockets and `/run/finn/state/`.

## The screen is black

```sh
systemctl status finn-comp
journalctl -u finn-comp -b -n 50 --no-pager
```

`finn-comp` needs `/dev/dri/card*` (or `/dev/fb0` with `QT_QPA_PLATFORM=linuxfb`), `seatd` (`LIBSEAT_BACKEND=seatd`), and `XDG_RUNTIME_DIR=/run/finn`. Press `Ctrl+Alt+F2` for a rescue console, or boot with `finn.recovery` on the kernel command line to enter **FinnOS Recovery Mode**.

## The session socket is missing

```sh
systemctl --user status finn-session
ls -l /run/user/1000/finn/session.sock
```

Ensure `loginctl enable-linger finn` has run so `/run/user/1000` exists.

## An app cannot connect to the display

```sh
ls -l /run/finn/wayland-0
echo "$WAYLAND_DISPLAY"
```

The socket is `0660 root:finn`. Clients use an absolute `WAYLAND_DISPLAY=/run/finn/wayland-0`.

## Shared libraries or Qt 6 plugins are not found

Confirm `/etc/ld.so.conf.d/finn.conf` lists `/usr/local/lib` (`sudo ldconfig`) and `finn-sdk check` reports `[ok]` for `libfinnui`, `libfinnui_qml`, `libfinnplatformtheme.so`, `libfinnstyle.so`, and the `FinnSDK` CMake package.

## Text or icons look wrong

Check `/usr/share/fonts/OTF/FixelDisplay-Regular.otf` and `/usr/share/finn/icons/`. `FINN_FONT` and `FINN_ICON_DIR` override paths during development.

## The theme does not update

Settings, Control Center, and CLI edits write `$HOME/.config/finn/settings` (`dark=1`, `accent=...`, `reduced_motion=1`, `high_contrast=1`). `FinnThemeManager`, `FinnThemeBridge`, and `libfinnplatformtheme` watch that file live; an exported `FINN_DARK` environment variable overrides the file.

## Clicks do not reach apps behind the shell

`finn-shell` runs as a fullscreen transparent Wayland overlay and applies `QWindow::setMask()` to pass clicks outside the dock and open popovers through to apps. Check `journalctl --user -u finn-shell -n 50` and `/run/finn/state/blur-rects.tsv`.

## Screenshots fail

`finn-capture` writes `/run/finn/state/capture-request` and waits for `finn-comp` to write the PNG + `.done` marker into `~/Pictures/Screenshots`. Ensure `/run/finn/state` is `0775 root:finn`. On setups with a `wlr-screencopy`-capable compositor, `vm/shot.sh` also uses `grim` with `WAYLAND_DISPLAY=/run/finn/wayland-0`.
