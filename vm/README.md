# The dev VM

The Linux VM is the primary build and visual-verification machine: it has DRM/KMS, Mesa, Qt 6 WaylandCompositor, `seatd`, fonts, and a display. Your host machine can be the editor.

```sh
vm/deploy.sh              # sync, build, install, restart the session
vm/deploy.sh --no-restart # build and install only
vm/shot.sh out.png        # capture the VM desktop
```

`deploy.sh` streams the tree over SSH as a tar archive, installs build dependencies, builds in `~/finnos`, runs `sudo make install`, stages `/etc/sudoers.d/finn-oobe`, installs the systemd units, enables linger for `finn`, and restarts `finn-comp`/`finn-session`/`finn-shell`.

## On an Apple Silicon Mac (`aarch64`)

```sh
brew install lima qemu
vm/lima-setup.sh          # create the VM, upgrade it, deploy, start the session
```

`vm/lima-setup.sh` creates a Lima instance named `finnos` from `vm/lima.yaml` (QEMU `aarch64` with display), brings it up to date, and deploys the tree. Export the printed host variables to reuse the other scripts:

```sh
export FINN_HOST=lima-finnos FINN_SSH_OPTS="-F $HOME/.lima/finnos/ssh.config"
vm/deploy.sh
vm/shot.sh out.png
```

Notes specific to the Lima `aarch64` VM:
- Uses QEMU rather than Apple `Virtualization.framework`.
- `/boot` is small, so kernel device trees and `Image.gz` are excluded via `NoExtract` in `/etc/pacman.conf`.
- The kernel command line carries `net.ifnames=0` for `eth0`.
- When running without host GPU acceleration, `finn-comp` can use Qt's software backend (`QT_QUICK_BACKEND=software` / `QT_QPA_PLATFORM=linuxfb`).

## The session it starts

- `finn-comp` as root on `tty1`, `QT_QPA_PLATFORM=eglfs`, `LIBSEAT_BACKEND=seatd`, `XDG_RUNTIME_DIR=/run/finn`, serving `/run/finn/wayland-0`.
- `finn-session`, `finn-shell`, and `finn-wallpaper` as `finn`, with `XDG_RUNTIME_DIR=/run/user/1000` and `WAYLAND_DISPLAY=/run/finn/wayland-0`.
- `finn-shell` renders the desktop dock, Home Menu, Spotlight, Control Center, notifications, Overview, and Lock Screen.

Only `finn-comp` needs DRM/input access; `finn-session` and `finn-shell` are normal Wayland clients and can be restarted without restarting the display.

## Driving & inspecting the VM

```sh
sudo -u finn env XDG_RUNTIME_DIR=/run/user/1000 finn-session --list
sudo -u finn env XDG_RUNTIME_DIR=/run/user/1000 finn-session --launch files
```

Pointer and keyboard injection for dev-VM verification live in [tools/vm-input/README.md](../tools/vm-input/README.md) (`uinput-pointer.py` + `ydotool`, dev-only tooling not shipped in the OS image).

## Installing a fresh VM from ISO

The live ISO built by `base/mkfinniso` boots directly into graphical **FinnOS Recovery Mode** (`apps/recovery-mode/`), backed by `src/recovery/recovery_engine.cpp`, `vm/finn-install.sh`, and `vm/finn-chroot.sh`. It installs 100% offline from the live squashfs image (with SHA-512 verification), supports GPT/MBR, UEFI (`finn-boot` / `systemd-boot` / GRUB UEFI) and Legacy BIOS, dual-boot resize, A/B OTA slots (`--ab-slots`), and data-preserving reinstall, then defers personal account/locale/appearance setup to first-boot `finn-oobe`.

See [emulators.md](emulators.md) and [docs/guest-tools.md](../docs/guest-tools.md) for VMware Workstation, VirtualBox, and QEMU emulator matrices.
