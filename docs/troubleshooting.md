# Troubleshooting

Start with the journal of the process that is missing, then the socket.

## The screen is black

```sh
systemctl status finn-comp
journalctl -u finn-comp -b -n 50
```

The compositor needs `/dev/dri/card0`, a seat from `seatd`, and
`XDG_RUNTIME_DIR=/run/finn`. The unit sets all three. If it exits, the
failure unit writes one line to the console; `Ctrl+Alt+F2` gives a login.

A compositor that is alive but not painting is a different failure: the
watchdog in the unit stops it after 15 seconds without a frame.

## The session socket is missing

```sh
systemctl status finn-session
ls -l /run/user/1000/finn/session.sock
```

`finn-session` runs as `finn` and needs `/run/user/1000`. It is created by
logind; `loginctl enable-linger finn` is the persistent way to ask for it.
`/run/user/1000` must be owned by `finn` and mode `0700`.

## An app cannot connect to the display

```sh
ls -l /run/finn/wayland-0
echo "$WAYLAND_DISPLAY"
```

The socket is `0660 root:finn`. An app running as another user cannot use it.
The path is absolute; a relative `WAYLAND_DISPLAY` is resolved under
`XDG_RUNTIME_DIR`, which is not where the compositor put it.

## An app is not in the menu

The menu lists the catalogue and filters by the installed list:

```sh
grep '^hello' /usr/share/finn/apps.registry
grep -x hello /var/lib/finn/installed
test -x /usr/local/bin/finn-hello && echo executable
```

`finn-session --list` shows what the broker sees, including the installed
flag.

## A launch fails

```sh
finn-session --launch files
```

The error names the refusal: `unknown app`, `not installed`, `not a native
launch`, `command not executable`, or a missing `XDG_RUNTIME_DIR`.

## Text or icons are empty

`libfinnui` looks for the font at
`/usr/share/fonts/OTF/FixelDisplay-Regular.otf` and icons under
`/usr/share/finn/icons`. `FINN_FONT` and `FINN_ICON_DIR` override them for
development. A missing file is reported on stderr; it is not replaced.

## The theme does not change

Settings writes `$HOME/.config/finn/settings` (`dark=1`). The compositor also
reads `/home/finn/.config/finn/settings` and `/var/lib/finn/settings`. An
exported `FINN_DARK` wins over the file and is a development switch, not a
setting.

## The screenshot fails

`vm/shot.sh` runs grim as `finn` with
`XDG_RUNTIME_DIR=/run/user/1000` and
`WAYLAND_DISPLAY=/run/finn/wayland-0`. The compositor advertises
screencopy and xdg-output; a compositor that does not is not the current one.
