# Roadmap

FinnOS is a platform in active development. These are the roadmap goals in
the order they should close.

## 1. The root authority — `finnd`

There is no audited door for power, brightness, volume, wireless, packages or
updates yet. When implemented, `finnd` should be small:

- a unix socket at `/run/finn/control.sock`, `0660 root:finn-authority`;
- `SO_PEERCRED` on every connection;
- verbs for state, power, display settings and updates;
- one audit line per call with the peer uid;
- no app launch: that is `finn-session`'s job;
- no TCP, no second door.

The shell can then grow a power menu and a brightness slider that act through
it, not by writing sysfs.

## 2. App sandbox

Each app in its own transient systemd user unit with
`NoNewPrivileges`, `PrivateTmp`, `ProtectSystem=strict`, a writable state
directory and no access to `/run/finn`. The session socket stays the only
launch path, and the app does not join the authority group.

The compositor should eventually stop running as root too: `seatd` already
grants DRM and evdev to a user in the `seat` group. That change needs the
brightness actuator behind `finnd` first.

## 3. Packages and updates

Package management and the OTA frontend are planned. The package manager
should maintain clean architectural boundaries: the catalogue and the installed
list are separate, the package manager is the only writer of the latter, and
a lit tile means the launcher exists. A/B slots and package signatures are
separate, larger decisions.

## 4. The desktop grows

- The Control Center and the full Home/All Apps view from the Figma file.
  v1 has the dock, the status pill and the app menu.
- A taskbar and a minimize/restore surface. v1 has maximize and close only;
  the window chrome draws a disabled minimize control that refuses clicks
  rather than stranding the window without a restore surface.
- Full-scene compositor backdrop blur. `libfinnui` implements client-side
  wallpaper backdrop blur (`Canvas::glass()`) with a 3-pass box blur and
  key-based caching; compositor-level blurring of windows behind other windows
  remains future work.
- A visible reduced-motion switch in Settings. `FINN_REDUCED_MOTION=1` is
  honoured today; the setting is not yet written by the UI.
- Server-side decoration or a protocol for a consistent header bar, if the
  client-side chrome proves inconsistent across toolkits.
- Multi-monitor and per-output scale.
- Clipboard history and drag-and-drop policy.
- A lock screen and session switching.
- A Store that is a real front end to the package manager.
- A browser, if it is wanted, as a sandboxed foreign app; not a custom pixel
  protocol.

## 5. Compatibility

Flatpak, Wine and winget are not in v1. If they return, they are launch
policies in `finn-session` with the same sandbox as a native app, not special
cases in the compositor.

## What "done" means

A change is done when:

- the app contract is unchanged: the compositor has no app ids;
- the host tests and the VM stack pass;
- a capture was looked at;
- the documentation says what is missing, without pretending.
