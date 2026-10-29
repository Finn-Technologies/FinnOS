# The base image

`base/` is the Arch Linux base FinnOS installs onto. The rewrite replaces the
session above it, not the distribution.

- `packages.*` — the package lists, one name per line.
- `pacman.conf`, `pacman.multilib.conf` — the repository configuration.
- `finn-comp.service`, `finn-session.service`, `finn-shell.service`,
  `finn-failure.service` — the session units.
- `finn-tmpfiles.conf` — `/run/finn`, the shared IPC directory.
- `airootfs/` — the live installation medium's root.
- `installed/` — the branding an installed machine gets.
- `setup.sh` — turns an Arch installation into FinnOS.
- `mkfinniso` — builds the ISO from this tree.
- `pkgbuilds/` — the local package recipes (`finn-filesystem`,
  `finnos-compose`).

## The session

`finn-comp` runs as root on tty1 and opens DRM and evdev through `seatd`. It
binds `/run/finn/wayland-0` (`0660 root:finn`) and feeds the systemd
watchdog. `finn-session` and `finn-shell` run as `finn`; the broker listens on
`/run/user/1000/finn/session.sock`. `base/setup.sh` enables linger for `finn`
so `/run/user/1000` exists before the first login.

There is no `finnd` in v1. App launch is the session broker's job and the
broker has no machine authority. The old `finnpkg`, `.fui` compiler, SDK and
MCP adapter were removed with the old tree; see `docs/roadmap.md`.

## Building the disc

```sh
base/mkfinniso --check
sudo base/mkfinniso finnos.iso
```

The check compares the units in `base/` with the copies in `base/airootfs/`,
checks the package lists and the profile, and refuses a drifted unit. The
installer builds the session from `/opt/finnos` on the target disk, because
that is the machine with the DRM headers and the toolchain.

## The units are one file, twice

`finn-comp.service`, `finn-session.service`, `finn-shell.service`,
`finn-failure.service` and `finn-tmpfiles.conf` exist in `base/` and in
`base/airootfs/`. They are the same file; `mkfinniso --check` fails when they
drift. A unit enabled with no binary fails at every boot, so the installer
enables the session only after the build has produced all three binaries and
the linger file for `finn`.
