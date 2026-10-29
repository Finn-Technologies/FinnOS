# Hardware

## Verified in the rewrite

The reference for this rewrite is the aarch64 development VM: a Parallels
virtual GPU, a virtual keyboard and mouse, a 1280x800-class DRM output, and
`seatd` for the seat.

- `finn-comp` starts on the DRM backend through `seatd`, as root, and the
  session, shell and Finn Files run as `finn` against
  `/run/finn/wayland-0`.
- Layer-shell ordering, xdg-shell apps, screencopy (`grim`), xdg-output,
  the watchdog and the scene graph were exercised there.
- The headless backend was exercised with `wayland-info` and
  `weston-simple-shm`.
- Light and dark theme changes were captured.

## Not verified

- Physical x86_64 hardware. The previous tree ran on a Dell OptiPlex 3020;
  this rewrite has not been installed on it.
- Pointer and keyboard input through libinput/seatd. The devices exist and
  the code paths are in place, but no automated input was driven through
  them.
- Multi-monitor, per-output scale, HiDPI, rotation, touch, tablets and input
  methods.
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
