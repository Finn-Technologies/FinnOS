# Changelog

The shape of this file is the shape of the project: the system is small,
and the changes to it are worth reading.

## Unreleased

### The Start Menu

The shell had a dock, a Control Center and a clock page, and no way in to
the system's apps at all. There is now a fourth surface.

- **The Start Menu** (`finnmenu`): a centred card holding the app grid,
  each app's own state, and the power row. It opens from the dock's
  launcher button, and the desktop dims behind it.
- **The application registry** (`finnapps`): the catalogue the OS ships
  and the install list the package manager owns, kept apart so that one
  can change without the other. The Start Menu draws the catalogue; the
  control plane launches what is installed.
- **Honest states.** An app that is not installed is a muted plate with
  "Not installed" under its name, and pressing it says so.
- **One way in.** The menu asks the control plane to launch a program; it
  never spawns anything itself.

### The control plane

- `app.list` and `app.launch <id>`. The control plane is the only place in
  the system that starts user programs, so the shell, a script and an
  agent all go through the same audit trail.
- `finnd` now gives its socket to the `finn` group itself, and runs in
  root's own group. systemd takes `CAP_SETUID` away from a service whose
  group it had to change, and the control plane needs that capability to
  start an app as the user rather than as root.

### The SDK

- **`fuic`**, the `.fui` compiler: `.fui` in, `.fbin` out. The binary
  format is specified byte for byte in `sdk/findsdk/fuic/FORMAT.md`, and
  the writer and the reader are the same code, so the two cannot drift.
- **The dialect the tree actually uses.** Calls with named or positional
  arguments, `+` for composition, `->` for a per-target mapping, `@` for
  an attenuation, `;` as a statement separator, comma-continued values,
  and the theme and target declaration forms. Every `.fui` file in the
  tree compiles with no warnings.
- **A client library.** `findsdk.FinnClient` turns the protocol's verbs
  into calls, and `finn-sdk` puts the same verbs on the command line.
- **App templates**, and `fuic new` to make an app from one.
- **The MCP adapter** gained `finn_app_list` and `finn_app_launch`, so an
  agent can see the catalogue and start an app.
- Two client bugs fixed: the state field is `battery_pct` and not
  `battery`, and the device's `] ` separators were being left on the end
  of every app name and notification title.

### The tree

- `src/core`, `src/compositor`, `src/control`, `src/pkg`, `src/tools`: the
  tree now says what it is. The core is a library and knows nothing about
  the two binaries that use it.
- `finncc` is now `finncontrol`. "cc" was an abbreviation, and the house
  rule is that a module is named for what it renders.
- `fvtest` is now `vgtest`, and it draws a sheet of the whole icon set.
- The build artefacts that were committed into the tree are gone, and so is
  `compositor/desktop.rules` — it described a Wayland compositor with
  damage regions and AVIF wallpapers, which is not the system that
  exists. `docs/architecture.md` replaces it.
