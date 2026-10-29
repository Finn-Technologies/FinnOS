# The FinnOS rewrite contract

This page is the rule for the rewrite. The C userland before it is preserved
on the `pre-rewrite` tag. A change that makes the code and this page disagree
is not finished.

## The base stays

Arch Linux: the kernel, firmware, systemd, pacman, mesa, libdrm, libinput and
Wayland. The installer, the ISO build and the package list in `base/` stay.
Everything above the session is rewritten.

## The programs

| Program | Owns | Runs as |
|---|---|---|
| `finn-comp` | outputs, seats, toplevels, window management, effects | root (DRM) |
| `libfinnui` | the UI runtime: layout, widgets, text, vector, tokens | library |
| `finn-shell` | dock, clock, app menu | `finn` |
| `finn-session` | registry, installed list, launch | `finn` |
| `finnd` | power, state, packages, updates, audit | root — not written in v1 |

`libfinnui` is not a daemon. The shell and the first-party apps link it.

The compositor knows nothing about app ids, packages, the shell or the Start
Menu. If a module needs to know what an app is called, it is in the wrong
process.

## Authority

Two sockets, two owners.

| Socket | Owner | Mode | Serves |
|---|---|---|---|
| `$XDG_RUNTIME_DIR/finn/session.sock` | `finn-session` | `0600 finn` | app launch |
| `/run/finn/control.sock` | `finnd` (reserved) | `0660 root:finn-authority` | machine authority, not written in v1 |

Apps join neither group and never talk to `finnd`. The shell asks
`finn-session` to launch. There is no TCP listener and no second door.

The display socket is a display, not authority: `/run/finn/wayland-0`,
`0660`, group `finn`. It lives in the shared runtime directory because the
compositor runs as root and the apps run as `finn`; `$XDG_RUNTIME_DIR` for
root is not reachable by any app. Clients use the absolute path in
`WAYLAND_DISPLAY`.

## The app contract

An app is a program that connects to the compositor and creates an
`xdg_toplevel`. That is the whole contract. The compositor cannot tell a
first-party app from a GTK app and must not try.

Metadata lives in `assets/apps.registry`:

```
id | Name | icon | command | note | source
```

The OS ships the catalogue. `finn-session` reads `/var/lib/finnpkg/installed`
for what is on the machine. The catalogue never says whether an app is
installed, and the installed list never carries metadata.

First-party apps live in `apps/<id>/` and link `libfinnui`. Foreign apps are
unmodified GTK, Qt, Flatpak or Wine programs.

Adding an app changes two things: its registry line and its own directory.
CI enforces this with a test that fails if a first-party app id appears under
`src/comp`.

## Display

wlroots 0.20. One output and one scale in v1. `xdg-shell` for apps,
`layer-shell` for shell surfaces. There is no server-side decoration: the
compositor draws no titlebar, a native app draws the frame `libfinnui`
provides, and a foreign toolkit keeps its own header bar. There is exactly one
chrome per window.

All rendering goes through `wlr_renderer` and the scene graph. There is no
second software path to keep in step with the accelerated one.

## Sandbox

The sandbox is the next change, not the current state. In v1 an app runs as
`finn` with the user's full authority; `finn-session` spawns it, and the
session has no machine authority. The target is a transient systemd user unit
per app with `NoNewPrivileges=yes`, `PrivateTmp=yes`,
`ProtectSystem=strict`, an explicit writable state directory and no access to
`/run/finn`. An app that needs the machine will ask the shell, and the shell
will ask `finnd`.

## Honesty

A field with no value is absent, never a plausible default. A tile for an app
that is not installed is drawn as not installed and refuses presses. A font,
icon, sensor or update feed that is missing is reported as missing. If a
feature does not exist, the code, the documentation and the reply all say so.

## One copy of a fact

A path, socket name or magic number that two modules need lives in exactly
one header in `src/common`. The protocol magic and the socket paths are the
first entries. A literal repeated in two places is a bug in one of them.

The control protocol stays line-oriented text. JSON belongs at the MCP
boundary on the host, never inside a daemon.

## Non-goals for v1

No browser. No multi-monitor and no per-output scale. No screen lock, session
switching or logout control. No touch, input methods or drag-and-drop. No A/B
update slots and no package signatures. A feature that is not in this list is
not automatically in scope; it still needs a page and a test.
