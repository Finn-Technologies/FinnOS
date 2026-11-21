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
- **x86_64 VMware Workstation 26.0.1 guest, 3D acceleration on**: an installed
  FinnOS system booted natively with `vmwgfx` DRM at its 1280×800 preferred mode;
  `finn-comp` initialised EGL/GLES2 on `SVGA3D` after the host's Vulkan renderer
  was pointed at Mesa lavapipe (`vulkan-swrast`), and Qt6 apps rendered in live
  `grim` captures. With 3D off, the same system used the pixman software fallback
  and showed the dock. `vmrun checkToolsState` reported VMware Tools `running`.
  See `docs/guest-tools.md`.

## Not verified (honest hardware & runtime limits)

- **Bare-metal x86_64 GPU & multi-monitor**: physical bare-metal laptops and
  mixed multi-monitor per-output scales have not been verified yet.
- **Dock and lock screen under VMware 3D**: with the GLES2 renderer on `vmwgfx`,
  layer-shell surfaces above the background are mapped but not visible in the
  output commit; toplevels and the wallpaper render. The pixman fallback shows
  them, so this is specific to the wlroots 0.20 GLES2 + vmwgfx combination.
- **Wine on ARM64**: Upstream `aarch64` package repositories have no native x86 `wine` package by
  default. `finn::inspect_pe` reads PE headers (`x86`, `x86_64`, `ARM64`), and
  FinnOS honestly reports Wine as unavailable when `wine` / `wine64` is absent
  from `$PATH`.
- **Battery, Wi-Fi, Bluetooth, Brightness**: Settings and Control Center probe
  real `UPower`, `NetworkManager`, `BlueZ`, and `PipeWire` backends; virtual
  hardware without adapters or backlights renders cleanly as disabled/absent.
- **One output & client/compositor boundary**: no dmabuf zero-copy video
  decode path verified; Qt6 apps share `wl_shm` / Wayland buffers.
