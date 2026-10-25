# The roadmap

Where FinnOS is, and what comes next. Written so that the next person —
human or agent — knows what "done" means for each piece.

## Now

| | |
|---|---|
| the compositor | DRM/KMS, page flips, a GPU path and a software path |
| the shell | the dock, the Control Center, the clock page, the Start Menu |
| the keyboard | evdev reader, modifiers and locks, hotplug, `Ctrl+Alt+F1..F6`, `Alt+Tab` |
| the session | gated first boot, one atomic settings store, tty2 rescue console, watchdog |
| the control plane | `finnd`, `finnctl`, the audit log, the protocol |
| the registry | the catalogue the Start Menu draws, the install list the package manager owns |
| the SDK | `fuic` (the `.fui` compiler), the Python client, the app templates |
| the adapter | `finn-mcp`, the agent-facing half of the control plane |

## Next

### Compatibility packages

Finn Store lists installed Flatpaks, searches configured remotes, starts
audited install/remove jobs, and offers Open for installed Library entries.
`finnd flatpak.run` now verifies system installation and starts the app as Finn
in the Wayland session. The x86_64 base manifest includes Flatpak; setup tries
to register canonical system Flathub and warns if unavailable. The aarch64
manifest still omits Flatpak. Current core, compositor and control sources
build on an ARM Linux VM. An installed GNOME Calculator launched through the
Flatpak control path rendered a 410×666 SHM surface on a standalone
FinnWayland test server; pointer and keyboard input produced `78`. This was
not a booted FinnOS desktop or a full Store search/install/launch walkthrough.
Popups, subsurfaces, dmabuf, touch, clipboard and drag-and-drop remain
unsupported. Wine is x86_64-only and supports registering an existing `.exe`,
listing registrations, and launching through audited `finnd` with a private
prefix per app. It has no installer or automatic discovery flow, and real Wine
GUI behavior remains unverified.

### Windows and external clients

The window manager now hosts external Wayland xdg-shell toplevels backed by
SHM; imported buffers join the existing window stack, and finn-comp routes
configure, close, frame, pointer and keyboard events. A real GNOME Calculator
Flatpak was launched against the server in an ARM Linux test VM; click and key
input changed its expression. This verifies a client path in isolation, not a
booted FinnOS desktop. Next, verify the full Store search/install/open flow on
a booted target and implement protocol pieces apps require (including
clipboard/DND where needed). Wine still needs a real x86_64 runtime and GUI test. The Store lists validated
registrations in Library and launches them through `wine.run`; registering an
existing `.exe` remains a CLI step, and installers are not managed.

### Keyboard

The reader is there: `finnkbd` owns evdev, derives modifiers and locks from
held keys rather than from the event, handles `SYN_DROPPED`, picks up a
keyboard plugged in later, and exposes `key.press/release/tap`. The shell
uses it for setup, `Ctrl+Alt+F1..F6`, `Alt+Tab`, `Esc` and the Super key.

The compositor forwards keyboard input to a focused external Wayland surface
as well as the built-in search/filter fields. A GNOME Calculator test confirmed
basic key entry through the isolated Wayland server. Full FinnOS desktop
validation remains open; text-input-v3, input methods and clipboard are not
provided, and a terminal application is not included.

### Apps that draw

`app.launch` starts a program. A limited external Wayland host now exists for
traditional GUI clients; current sources build on ARM Linux, and an installed
GTK Calculator rendered and accepted basic input on a standalone test server.
A booted FinnOS desktop remains unverified. A `.fui` app does not yet become a
client: the `.fbin` is its scene graph, and FinnOS still needs a native renderer
that composes it directly into `finn-comp`.

The first-party apps are named and waiting:

| App | What it is |
|---|---|
| **Finn Code** | editor and IDE |
| **Finn Files** | file manager |
| **Finn Store** | app distribution, the `finnpkg` GUI |
| **Finn Notes** | notes |
| **Finn Studio** | the visual `.fui` designer |
| **Finn Sync** | device-to-device sync |
| **Finn Vault** | credentials and keys |
| **Finn Terminal** | the shell host |

### Packaging and OTA

`finnpkg` installs, lists and removes a `.finn` package. It does not
update: `finnpkg update` is a stub. The design it is aiming at:

- a `.finn` package is a tarball with a manifest, a `.fbin` and assets;
- the manifest names the app, the id it installs as, and the files it
  owns;
- installation records the id in `/var/lib/finnpkg/installed`, which is
  the same list the Start Menu reads;
- an update is an **A/B slot** switch: the new system is written to the
  inactive slot, the bootloader is told which one to boot, and the switch
  is confirmed by the new system coming up. A failed boot falls back.

The A/B switch is the only safe way to update a machine that is often
running on battery, in a bag, with nobody watching.

### FinnAI

Opt-in, local, and never in the base image. When it arrives it will be a
service the user turns on, with weights fetched on demand through
`finnpkg`, and it will talk to the system only through the control plane —
the same door every other client uses.


### Multi-catalog scaffolding (Store)

The Store now carries Flatpak and Windows tabs in addition to Home, Apps and
Library. Each reads catalogue entries filtered by a new  field
( / ); a registry without that field yields "No packages".

## Later

- **The other targets.** Mobile, MobileXL and TV are specified in
  `ui/_targets.fui` and there is no code behind them. The compositor's
  density model is already in place; the input model is not.
- **The UI language, finished.** `fuic` compiles a `.fui` file to a
  `.fbin`. FinnOS core now has a bounded C reader for the compiled scene
  tree, but the renderer that draws it and the native app host do not exist
  yet; the token table is still kept in two places by hand.
- **A second shell.** The compositor draws the shell in C. A shell that
  loads its own layout would let the OS be themed without touching the
  compositor — but it would also mean a shell crash takes the display
  with it, so it is a trade, not a plan.
