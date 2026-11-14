# Install

Arch Linux provides the kernel, firmware, systemd, pacman, mesa, libdrm,
libinput, Wayland, and Qt6. FinnOS builds the compositor, session, Qt6
platform integration, shell, SDK, and native applications on top.

## On a machine

`base/setup.sh` configures an Arch Linux installation for FinnOS:

```sh
sudo base/setup.sh aarch64
```

It installs `base/packages.<arch>`, configures `finn`, installs systemd units,
fonts, wallpapers, icons, `apps.registry`, `/var/lib/finn/installed` (all 7
native apps: `settings`, `files`, `terminal`, `notes`, `store`, `viewer`,
`monitor`), enables linger for `finn`, writes `/etc/ld.so.conf.d/finn.conf`, and
enables:

- `finn-comp.service` on tty1 as `root` with `seatd` (`LIBSEAT_BACKEND=seatd`)
- `finn-session.service` as `finn`
- `finn-shell.service` as `finn`
- `finn-wallpaper.service` as `finn`
- `finn-failure.service` if the compositor fails

The session is enabled only when `finn-comp`, `finn-session`, `finn-shell`,
and `finn-wallpaper` all exist.

## The runtime package

`scripts/build-runtime-pkg.sh` packages `finnos-runtime` (`finn-comp`,
`finn-session`, `finn-shell`, `finn-open`, `finn-sdk`, `finn-display`,
`libfinnui`, `finnplatformtheme` + `finnstyle`, `FinnSDKConfig.cmake`,
the 7 native apps + `finn-wallpaper`, assets, `sdk/template`, and units).

## Units

| Unit | User | Role |
|---|---|---|
| `finn-comp.service` | `root` | wlroots DRM compositor, seatd, `/run/finn/wayland-0` |
| `finn-session.service` | `finn` | app registry, installed list, launch broker |
| `finn-shell.service` | `finn` | Qt6 layer-shell dock, Spotlight, Home, Control Center |
| `finn-wallpaper.service` | `finn` | background layer surface and WebP wallpaper |
| `finn-failure.service` | `root` | fallback console notice if compositor exits |

`/run/finn` and `/tmp/.X11-unix` are managed by `tmpfiles.d`
(`base/finn-tmpfiles.conf`), so restarting `finn-comp` does not wipe shared
sockets unexpectedly.

## What is not installed yet

There is no root `finnd` authority daemon and no OTA A/B update mechanism yet.
Finn Store surfaces pacman, Flatpak, Wine `.exe` registration, and FinnSDK app
scaffolding on top of the distribution tooling.
