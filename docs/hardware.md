# Hardware

## Verified platforms

The reference environment is the aarch64 development VM: a Parallels
virtual GPU, a virtual keyboard and mouse, a 1280x800-class DRM output, and
`seatd` for the seat.

- `finn-comp` starts on the DRM backend through `seatd`, as root, and the
  session, shell and Finn Files run as `finn` against
  `/run/finn/wayland-0`.
- Layer-shell ordering, xdg-shell apps, screencopy (`grim`), xdg-output,
  the watchdog and the scene graph were exercised there.
- The headless backend was exercised with `wayland-info` and
  `weston-simple-shm`.
- Light and dark theme changes were captured, including the wallpaper and
  the live switch between appearances.
- Pointer input was driven with `ydotool`/`uinput`: the app menu opened, a
  row launched Finn Files, and the maximize/restore controls resized it.

A second environment is the same tree in QEMU on an Apple Silicon Mac,
created by `vm/lima-setup.sh`: a `virtio-gpu` DRM output at 1440x900, a
virtio keyboard and mouse, and the Pixman renderer.

- `finn-comp` starts on the DRM backend through `seatd`, and the session,
  shell, wallpaper, Finn Files and Settings run against it.
- With the default renderer there (GLES2 on llvmpipe) the QEMU window shows
  stale blocks near the top-left corner while a `grim` capture of the same
  frame is clean. `WLR_RENDERER=pixman` does not show them; the setup script
  sets it. Why the GLES2 path does this on `virtio-gpu` was not found.
- Pointer input was driven through QEMU's virtio mouse: a click on a dock
  icon launched Settings, and a click on the search button opened the search
  overlay.
- Keyboard input was driven through QEMU's virtio keyboard: Escape closed
  the search overlay. That is one key reaching a layer surface; text entry
  and modifiers were not driven.

## Not verified

- Physical x86_64 hardware: bare-metal testing has not been run on reference
  hardware yet.
- Pointer input through libinput/seatd: exercised with a virtual pointer on
  the VM (menu, launch, maximize, restore). The physical Parallels pointer was
  not separately driven.
- Keyboard input through libinput/seatd beyond Escape on a layer surface
  (see above): keys to an app window, text and modifiers have not been
  driven.
- Multi-monitor, per-output scale, rotation, tablets and input methods.
- HiDPI: integer output scale (2x at 2400 px wide or more, or
  `FINN_OUTPUT_SCALE`) was checked in the VM by forcing 2x and reading the
  capture. Whether Parallels offers a native 2x mode was not checked.
- Touch: `finn-comp` forwards `wl_touch` and `libfinnui` maps the first touch
  point to a press. The VM has no touch device, so no touch event was driven.
- Suspend, resume, screen lock and brightness. The compositor does not write
  sysfs.
- Audio, wireless, Bluetooth and battery reporting: there is no `finnd` or
  state reader in v1.
- GPU acceleration outside the VM's virtual GPU. wlroots picks GLES2 or
  Pixman; only the latter was forced in tests.

## Limits that are design, not bugs

- One output and one scale.
- No dmabuf import: clients hand over `wl_shm` buffers.
- No server-side decorations: the client draws its own chrome.
- No Xwayland and no foreign toolkit inside the compositor. A GTK app is a
  normal Wayland client and keeps its own header bar.

If a machine does not work, [troubleshooting.md](troubleshooting.md) starts
with the compositor's journal and the display socket.
