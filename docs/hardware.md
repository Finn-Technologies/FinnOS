# Hardware

## Verified environments

- **The aarch64 development VM (QEMU / Lima on Apple Silicon)**: `virtio-gpu` DRM output
  through `seatd`, virtio keyboard/pointer, `WLR_RENDERER=pixman`.
  `finn-comp`, `finn-session`, `finn-wallpaper`, `finn-shell`, and Qt6 native
  apps (`Settings`, `Finn Files`, `Finn Terminal`, `Finn Notes`, `Finn Store`,
  `Finn Viewer`, `Finn Monitor`) run on `/run/finn/wayland-0`.
- **Headless Wayland**: `WLR_BACKENDS=headless WLR_RENDERER=pixman finn-comp`
  for non-DRM environments and offscreen screenshots with `grim`.

## Not verified (honest hardware & runtime limits)

- **Bare-metal x86_64 GPU & multi-monitor**: physical bare-metal laptops and
  mixed multi-monitor per-output scales have not been verified yet.
- **Wine on ARM64**: Arch Linux ARM64 has no native x86 `wine` package by
  default. `finn::inspect_pe` reads PE headers (`x86`, `x86_64`, `ARM64`), and
  FinnOS honestly reports Wine as unavailable when `wine` / `wine64` is absent
  from `$PATH`.
- **Battery, Wi-Fi, Bluetooth, Brightness**: Settings and Control Center probe
  real `UPower`, `NetworkManager`, `BlueZ`, and `PipeWire` backends; virtual
  hardware without adapters or backlights renders cleanly as disabled/absent.
- **One output & client/compositor boundary**: no dmabuf zero-copy video
  decode path verified; Qt6 apps share `wl_shm` / Wayland buffers.
