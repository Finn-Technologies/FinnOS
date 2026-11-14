# Troubleshooting

Start with the journal of the unit that is missing, then inspect the sockets.

## The screen is black

```sh
systemctl status finn-comp
journalctl -u finn-comp -b -n 50
```

`finn-comp` needs `/dev/dri/card*`, `seatd`, and `XDG_RUNTIME_DIR=/run/finn`.

## The session socket is missing

```sh
systemctl status finn-session
ls -l /run/user/1000/finn/session.sock
```

Ensure `loginctl enable-linger finn` has run so `/run/user/1000` exists.

## An app cannot connect to the display

```sh
ls -l /run/finn/wayland-0
echo "$WAYLAND_DISPLAY"
```

The socket is `0660 root:finn`. Clients use an absolute `WAYLAND_DISPLAY=/run/finn/wayland-0`.

## Shared libraries or Qt6 plugins are not found

Confirm `/etc/ld.so.conf.d/finn.conf` lists `/usr/local/lib` (`sudo ldconfig`)
and `finn-sdk check` reports `[ok]` for `libfinnplatformtheme.so`,
`libfinnstyle.so`, `liblayer-shell.so`, and `FinnSDK CMake package`.

## Text or icons look wrong

Check `/usr/share/fonts/OTF/FixelDisplay-Regular.otf` and
`/usr/share/finn/icons/`. `FINN_FONT` and `FINN_ICON_DIR` override paths during
development.

## The theme does not update

Settings, Control Center, and CLI edits write `$HOME/.config/finn/settings`
(`dark=1`, `accent=...`, `reduced_motion=1`). `FinnThemeManager` watches that
file live; an exported `FINN_DARK` environment variable overrides the file.

## The screenshot fails

`vm/shot.sh` runs `grim` as `finn` with `XDG_RUNTIME_DIR=/run/user/1000` and
`WAYLAND_DISPLAY=/run/finn/wayland-0`.
