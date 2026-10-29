# The FinnOS rewrite contract

This page is the rule for the rewrite. The C userland before it is preserved
on the `pre-rewrite` tag. A change that makes the code and this page disagree
is not finished.

## The base stays

Arch Linux: the kernel, firmware, systemd, pacman, mesa, libdrm, libinput and
Wayland. The installer, the ISO build and the package list in `base/` stay.
Everything above the session is rewritten.

## Five programs

| Program | Owns | Runs as |
|---|---|---|
| `finn-comp` | outputs, seats, toplevels, window management, effects | root (DRM) |
| `libfinnui` | the UI runtime: layout, widgets, text, vector, tokens | library |
| `finn-shell` | dock, Control Center, clock page, Start Menu, notifications | `finn` |
| `finn-session` | registry, installed list, launch, sandbox | `finn` |
| `finnd` | power, state, packages, updates, audit | root |

`libfinnui` is not a daemon. The shell and the first-party apps link it.

The compositor knows nothing about app ids, packages, the shell or the Start
Menu. If a module needs to know what an app is called, it is in the wrong
process.

## Authority

Two sockets, two owners.

| Socket | Owner | Mode | Serves |
|---|---|---|---|
| `/run/finn/control.sock` | `finnd` | `0660 root:finn-authority` | machine authority |
| `$XDG_RUNTIME_DIR/finn/session.sock` | `finn-session` | `0600 finn` | app launch |

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
`src/compositor`.

## Display

wlroots 0.20. One output and one scale in v1. `xdg-shell` for apps,
`layer-shell` for shell surfaces, `xdg-decoration` server-side for toplevels.
The compositor never crops or guesses a client's own headerbar; a client that
draws client-side decorations keeps them.

All rendering goes through `wlr_renderer` and the scene graph. There is no
second software path to keep in step with the accelerated one.

## Sandbox

Every app runs in its own transient systemd user unit. The baseline is
`NoNewPrivileges=yes`, `PrivateTmp=yes`, `ProtectSystem=strict`,
`ProtectHome=read-only` with an explicit writable state directory, and no
access to `/run/finn`.

An app that needs a file uses the XDG directories. An app that needs the
machine asks `finn-session`, and only the shell is allowed to ask. Sandbox
rules are defaults, not suggestions: a launch that cannot be sandboxed is a
launch that does not happen.

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
