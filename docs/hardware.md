# Hardware

## Verified environments

- **The aarch64 development VM (QEMU / Lima on Apple Silicon)**: `virtio-gpu` DRM output
  through `seatd`, virtio keyboard/pointer, `WLR_RENDERER=pixman`.
  `finn-comp`, `finn-session`, `finn-wallpaper`, `finn-shell`, and Qt6 native
  apps (`Settings`, `Finn Files`, `Finn Terminal`, `Finn Notes`, `Finn Store`,
  `Finn Viewer`, `Finn Monitor`) run on `/run/finn/wayland-0`.
- **Headless Wayland**: `WLR_BACKENDS=headless WLR_RENDERER=pixman finn-comp`
  for non-DRM environments and offscreen screenshots with `grim`.
- **Virtualization guest tools (`docs/guest-tools.md`)**: `base/packages.x86_64`
  carries `open-vm-tools` (VMware Tools), `virtualbox-guest-utils-nox`
  (VirtualBox Guest Additions without GTK3), and `qemu-guest-agent`, managed via
  `finn-guest-tools` and **Settings → Compatibility → Virtualization & Guest
  Tools**. `finn-comp` follows DRM preferred-mode updates for dynamic guest
  window resizing when no fixed `resolution=` override is pinned. X11/GTK3
  clipboard/drag-and-drop plugins (`gtkmm3`, `virtualbox-guest-utils`) are not
  installed by design.
- **x86_64 VMware Workstation 26.0.1 guest, 3D acceleration off**: an installed
  FinnOS system booted natively with `vmwgfx` DRM at its 1280×800 preferred mode;
  `finn-comp`, the dock, wallpaper and Qt6 apps ran through the pixman software
  renderer after the automatic fallback, and `vmrun checkToolsState` reported
  VMware Tools `running`. See `docs/guest-tools.md`.

## Not verified (honest hardware & runtime limits)

- **Bare-metal x86_64 GPU & multi-monitor**: physical bare-metal laptops and
  mixed multi-monitor per-output scales have not been verified yet.
- **Hardware 3D rendering in a VMware guest**: this host's VMware 3D path could
  not be enabled, so only the pixman software-renderer fallback was exercised
  inside VMware; GLES2 acceleration on a 3D-enabled VM is not verified here.
- **Wine on ARM64**: Upstream `aarch64` package repositories have no native x86 `wine` package by
  default. `finn::inspect_pe` reads PE headers (`x86`, `x86_64`, `ARM64`), and
  FinnOS honestly reports Wine as unavailable when `wine` / `wine64` is absent
  from `$PATH`.
- **Battery, Wi-Fi, Bluetooth, Brightness**: Settings and Control Center probe
  real `UPower`, `NetworkManager`, `BlueZ`, and `PipeWire` backends; virtual
  hardware without adapters or backlights renders cleanly as disabled/absent.
- **One output & client/compositor boundary**: no dmabuf zero-copy video
  decode path verified; Qt6 apps share `wl_shm` / Wayland buffers.
