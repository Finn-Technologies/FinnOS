# Security

## The boundary

| Component | Authority |
|---|---|
| `finn-comp` | `root`: opens DRM and evdev through `seatd` and owns scanout. |
| `finn-session` | `finn`: reads registry/installed lists and spawns user apps. |
| `finn-shell` | `finn`: layer-shell client (`dock`, Home menu, Search, Control Center). |
| native & foreign apps | `finn`: Wayland / Xwayland / Wine clients with the user's authority. |
| `finnd` | not implemented yet (planned root control plane). |

The display socket is `/run/finn/wayland-0`, `0660`, group `finn`. The session
socket is `$XDG_RUNTIME_DIR/finn/session.sock`, `0600`, validated with
`SO_PEERCRED`. There is no TCP listener.

`finn-comp` exposes `wl_compositor`, `wl_shm`, `wl_subcompositor`,
`wl_data_device_manager`, `xdg_wm_base`, `wlr-layer-shell`,
`wlr-foreign-toplevel-management`, `screencopy`, `xdg-output`, and on-demand
`Xwayland` (`Xwayland` is started lazily for X11 clients inside systemd's shared
`/tmp/.X11-unix`).

## What an app can do

An app runs as `finn`:

- read and write the user's home directory and session socket;
- request launches via `finn-session` or `finn-open`;
- render Qt6, Wayland, or X11 windows inside its surface.

## Honest gaps

- **No per-app sandbox yet.** Apps are not isolated in transient systemd user
  units or seccomp namespaces yet.
- **No `finnd` root authority yet.** Privileged machine controls (poweroff,
  sysfs brightness) are not elevated behind a separate audited socket.
- **No package signatures / A/B slots.** System package updates rely on Arch's
  pacman signing and repositories rather than A/B image slots.
- **Compositor runs as root.** Dropping root from `finn-comp` waits on moving
  device/power hooks into `finnd` + `seatd`.
- **No AI.** There is no bundled inference model or external AI telemetry.
