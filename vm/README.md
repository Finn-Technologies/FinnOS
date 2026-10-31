# The dev VM

The VM is the build machine: it has DRM, the GPU stack, `wayland-scanner`,
`seatd`, the fonts and a real display. The host can be the editor.

```sh
vm/deploy.sh              # sync, build, install, restart the session
vm/deploy.sh --no-restart # build and install only
vm/shot.sh out.png        # grim against /run/finn/wayland-0
```

`deploy.sh` sends the tree as a tar stream (the VM has no rsync), installs the
build dependencies, builds in `~/finnos`, installs to `/usr/local`, installs
the units, enables linger for `finn` and restarts
`finn-comp`/`finn-session`/`finn-shell`.

## The session it starts

- `finn-comp` as root on tty1, `WLR_BACKENDS=drm`,
  `LIBSEAT_BACKEND=seatd`, `XDG_RUNTIME_DIR=/run/finn`.
- `finn-session` and `finn-shell` as `finn`, with
  `XDG_RUNTIME_DIR=/run/user/1000` and
  `WAYLAND_DISPLAY=/run/finn/wayland-0`.
- The shell's layer surface is the dock, clock and app menu.

The compositor's unit is the only one that needs the device. The session and
the shell are normal clients and can be restarted without restarting the
display.

## Looking at it

`vm/shot.sh` is the proof. A compositor change that compiles is not a change
that works. The capture is taken as `finn` with grim against the compositor's
screencopy and xdg-output globals.

If you need to drive it by hand, the verbs are the session's:

```sh
sudo -u finn env XDG_RUNTIME_DIR=/run/user/1000 finn-session --list
sudo -u finn env XDG_RUNTIME_DIR=/run/user/1000 finn-session --launch files
```

Input injection is not implemented; pointer and keyboard paths are verified
only by the fact that the device is opened and the seat exists. Treat an
input change as unverified until a test drives it.

## Installing a fresh VM

The FinnOS ISO is built from `base/` and contains the sources at
`/opt/finnos`. Boot it, choose **Install FinnOS**, and give the installer the
disk explicitly; it has no default. `vm/finn-install.sh` partitions and
pacstraps, and `vm/finn-chroot.sh` installs the assets, builds the session,
enables the three units and the linger file, and verifies the binaries before
claiming the install worked.

There is no `finnd` and no package manager in v1; installing an app means its
binary, its registry line and its id in `/var/lib/finn/installed`.
