# Install

The base image and the session are still Arch's package manager's job. The
rewrite replaces the session, not the distribution.

## On a machine

`base/setup.sh` makes an Arch installation FinnOS's:

```sh
sudo base/setup.sh aarch64
```

It installs the package list, the `finn` user, the system units, the fonts,
the wallpapers, the icons and the registry. It writes
`/var/lib/finnpkg/installed` with the two native apps, enables linger for
`finn` so `/run/user/1000` exists before the session starts, and enables:

- `finn-comp.service` on tty1, as root, with `seatd` and `LIBSEAT_BACKEND=seatd`;
- `finn-session.service` as `finn`;
- `finn-shell.service` as `finn`;
- `finn-failure.service` for a compositor that stops.

The session is enabled only when all three binaries exist. A machine without
them boots to a login console and the script says what is missing.

## The runtime package

`scripts/build-runtime-pkg.sh` builds `finnos-runtime`, the userland package
that can be replaced while the machine is running. It contains the four
programs, `libfinnui`, the registry, the icons, the wallpapers, the fonts and
the units. The kernel, firmware and bootloader remain the distribution's.

## Units

| Unit | User | Why |
|---|---|---|
| `finn-comp.service` | root | DRM and evdev through seatd; the display socket |
| `finn-session.service` | finn | catalogue, installed list, launch |
| `finn-shell.service` | finn | layer-shell dock and menu |
| `finn-failure.service` | root | one line on the console when the session stops |

`/run/finn` is created by tmpfiles, not by a unit's `RuntimeDirectory`, so
restarting the compositor does not unlink the session's other sockets.

## What is not installed

There is no `finnd`, no package manager, no Store, no Flatpak/Wine and no
OTA frontend in v1. The old `finnpkg` is on the `pre-rewrite` tag. Installing
an app today means putting its binary on the machine, adding its registry line
and adding its id to `/var/lib/finnpkg/installed`.
