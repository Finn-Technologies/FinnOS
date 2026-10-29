# Security

## The boundary

| Thing | Authority |
|---|---|
| `finn-comp` | root. It opens DRM and evdev through `seatd` and owns the scanout. |
| `finn-session` | `finn`. It reads two files and calls `posix_spawn`; it cannot change the machine. |
| `finn-shell` | `finn`. A layer-shell client. |
| apps | `finn`. A normal Wayland client with the user's full authority. |
| `finnd` | not written. There is no root control plane in v1. |

The display socket is `/run/finn/wayland-0`, `0660`, group `finn`. The session
socket is `$XDG_RUNTIME_DIR/finn/session.sock`, `0600`, and the server checks
`SO_PEERCRED` against its own uid. There is no TCP listener and no second
way in.

The compositor runs as root because DRM and evdev need it. It runs under
systemd hardening (`NoNewPrivileges`, `PrivateTmp`, `ProtectHome=read-only`,
`ProtectKernelModules`, `ProtectControlGroups`, `RestrictRealtime`,
`RestrictAddressFamilies=AF_UNIX`) and has a watchdog. Its Wayland protocol
surface is small on purpose: `wl_compositor`, `wl_shm`, `wl_subcompositor`,
`wl_data_device_manager`, `xdg_wm_base`, `wlr-layer-shell`, screencopy and
xdg-output. There is no dmabuf import, no touch, no input method, no Xwayland
and no foreign-toolkit bridge inside the compositor.

## What an app can do

An app runs as `finn`. It can:

- read and write the user's home directory and the session socket;
- launch another app through `finn-session`;
- draw any pixels in its own surface;
- ask for any window size the protocol allows.

It cannot raise its uid, open the compositor's private sockets, or change the
machine's power state, because nothing in v1 can.

## The gaps, stated plainly

- **No sandbox.** There is no per-app unit, namespace or seccomp filter. A
  malicious app is the user. This is the next security change.
- **No root authority daemon.** There is no audited door for power, state,
  packages or updates; there is also no way to abuse one.
- **No package signatures and no A/B slots.** There is no update path in v1.
- **No privilege drop from the compositor.** It stays root for DRM; a bug in
  the compositor's protocol handling is a root bug. The protocol list is kept
  small for that reason.
- **No AI.** There is no model, no weights and no network call to a model in
  the tree, and the policy is that any future AI is opt-in and off by default.

The honest v1 answer to "can an app take over the machine?" is: it can do
whatever `finn` can do, and nothing more. The honest answer to "is that enough?"
is no; the [roadmap](roadmap.md) says what closes it.
