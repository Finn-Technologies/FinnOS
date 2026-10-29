# Roadmap

The rewrite is a platform, not a finished distribution. These are the gaps in
the order they should close.

## 1. The root authority — `finnd`

There is no audited door for power, brightness, volume, wireless, packages or
updates. The old `finnd` was root-equivalent and tied to the old tree; it is
on `pre-rewrite`. The new one should be small:

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

`finnpkg` and the OTA frontend were removed with the old tree. The new
package manager should keep the old rules: the catalogue and the installed
list are separate, the package manager is the only writer of the latter, and
a lit tile means the launcher exists. A/B slots and package signatures are
separate, larger decisions.

## 4. The desktop grows

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
