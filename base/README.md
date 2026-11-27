# The base image

`base/` is the minimal pacman-based Linux system base FinnOS installs onto: the
package lists, systemd units, Plymouth boot splash, ISO builder, and local
PKGBUILDs that sit beneath the FinnOS session.

- `packages.x86_64`, `packages.aarch64` — supported architecture package lists (`packages.x86` and `packages.arm` are compatibility symlinks only).
- `pacman.conf`, `pacman.multilib.conf` — repository configuration (excluding `xorg-server`, `gtk3`, `gtk4`, `qt5-base`).
- `finn-comp.service`, `finn-recovery.service`, `finn-session.service`, `finn-shell.service`, `finn-wallpaper.service`, `finn-failure.service`, `finn-boot-health.service`, `finn-update-check.service`, `finn-update-check.timer` — system and session units.
- `finn-tmpfiles.conf` — `/run/finn` and `/run/finn/state` (`0775 root:finn`).
- `airootfs/` — the live installation medium's root, including the `finnos` Plymouth theme (`72×72` `mark.png`) that draws the boot mark from inside the initramfs.
- `installed/` — the branding (`issue`, `motd`) and partition-boot `mkinitcpio.conf` an installed machine gets.
- `setup.sh` — configures a minimal pacman-based rootfs for FinnOS.
- `mkfinniso` — builds and validates the live ISO (`native` or `assemble` mode).
- `pkgbuilds/` — local package recipes (`finn-filesystem`, `finnos-compose`, `finnos-keyring` placeholder).

## The boot

Every boot entry passes `quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles`: Plymouth starts in the initramfs with the 72×72 white Finn mark on `#0B0E14`, and there is no text console or blinking cursor between the firmware logo and the splash. Both the live and installed initramfs carry the theme. Passing `finn.recovery` starts `finn-recovery.service` (offline `recovery-mode`) instead of `finn-comp.service`. See `docs/architecture.md`, "The boot chain".

## The session

`finn-comp` (Qt 6 `QWaylandQuickCompositor`) runs as root on `tty1` with `LIBSEAT_BACKEND=seatd`, binds `/run/finn/wayland-0` (`0660 root:finn`), and feeds the systemd watchdog. `finn-session`, `finn-shell`, and `finn-wallpaper` run as `finn`; the session broker listens on `$XDG_RUNTIME_DIR/finn/session.sock`. `base/setup.sh` enables linger for `finn` so `/run/user/1000` exists before the first login.

There is no `finnd` daemon. App launch is `finn-session`'s job and the broker has no machine authority. A/B OTA updates (`finn-update`), `FinnSDK` (`finn-sdk`), and `[finnos]` repo tooling (`repo/`) are implemented in this tree; see `docs/ota.md`, `docs/sdk.md`, and `docs/roadmap.md`.

## Building the ISO

```sh
base/mkfinniso --check
sudo base/mkfinniso --arch x86_64
```

`mkfinniso --check` compares the units in `base/` with `base/airootfs/etc/systemd/system/`, checks the package lists, boot menus, and branding, and refuses any drifted unit.

## The units are one file, twice

`finn-comp.service`, `finn-recovery.service`, `finn-session.service`, `finn-shell.service`, `finn-wallpaper.service`, `finn-failure.service`, `finn-boot-health.service`, `finn-update-check.service`, `finn-update-check.timer`, and `finn-tmpfiles.conf` exist in `base/` and in `base/airootfs/`. They must remain identical; `base/mkfinniso --check` fails when they drift. `base/setup.sh` enables the session only after the required binaries exist and linger is enabled for `finn`.
