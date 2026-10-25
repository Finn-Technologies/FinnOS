# The control protocol

The control plane speaks a small line protocol. It is deliberately text and
not JSON: JSON is needed only at the MCP boundary, where the adapter
translates it, and keeping the on-device daemons free of a JSON parser
keeps them small and keeps a fast-moving external spec out of the
operating system.

It also means any shell can drive the system:

```sh
finnctl state.set battery=42
finnctl screen.info
```

There is no stdin form: `finnctl` takes the verb and its arguments on the
command line, so a request is one argv and not a parser for two shapes.

## The wire

One request per line, whitespace separated, `verb arg=value ...`:

```
state.set battery=42 charging=1
```

Three kinds of reply:

```
OK                                  the verb did what it said
OK <single-line payload>            a value
OK BINARY <len>\n<len raw bytes>    a binary payload
ERR <message>                       it did not
```

A verb that takes prose rather than key=value pairs takes one argument
separated by a pipe, because a pipe is the one character a title will not
contain:

```
notify.post <title>|<body>
```

## The sockets

| Path | Owner | Mode |
|---|---|---|
| `/run/finn/control.sock` | `finnd` | 0660, root and the `finn` group |
| `/run/finn/comp.sock` | `finn-comp` | 0600, root only |

Both are unix sockets. There is no TCP listener in the system, and there
will not be one: remote control is an explicit decision by the user, not a
default.

The compositor's socket is a provider, not a second door. `finn-comp`
checks `SO_PEERCRED` on every connection as well as the file mode.
`finnctl --direct` talks to it and says on stderr that it is bypassing
`finnd` and its audit log; it needs root. There is no automatic fallback:
a device whose control plane is down is unreachable rather than quietly
unaudited.

## The verbs

### Session

| Verb | Reply | What it does |
|---|---|---|
| `ping` | `OK FINNCTL/1` | is the plane alive |
| `version` | `OK finnd=0.1 proto=FINNCTL/1` | what is on the other end |

### State

The system state: battery, charging, wireless, volume, brightness,
Bluetooth, wired link, appearance. Every field has an absent value
(`-1`) rather than a default, because the shell has to be able to say "this
machine does not have one".

| Verb | Reply | What it does |
|---|---|---|
| `state.get` | `OK battery=… charging=… wifi=… volume=… brightness=… bluetooth=… wired=… dark=…` | read the state |
| `state.set` | `OK` | write an override: `state.set battery=42 charging=1` |
| `state.clear` | `OK` | drop the overrides; back to the real hardware |

The overrides live in `/run/finn/state`. They are a real interface: a
session daemon can publish state through them, and a test can drive the
shell through every state deterministically.

### Display and input

Provided by the compositor, proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `screen.info` | `OK w=… h=… scale=…` | the framebuffer and the density |
| `screen.shell` | `OK cc=… openness=… menu=… clock=… dock=… pointer=…` | what the shell is showing |
| `screen.capture` | `OK BINARY …` | a PPM of the frame, cursor included; `step=2` halves it |
| `screen.pointer` | `OK x=… y=… buttons=…` | where the pointer is |
| `pointer.move` | `OK` | move the pointer: `pointer.move x=300 y=400` |
| `pointer.by` | `OK` | nudge it: `pointer.by dx=10 dy=-4` |
| `pointer.click` | `OK` | press and release |
| `pointer.down` / `pointer.up` | `OK` | hold it down, or let it go |
| `pointer.scroll` | `OK` | queue wheel clicks: `pointer.scroll clicks=-3` (or `dy=`); the shell delivers them to the window under the pointer |

A click is an edge the shell can see, so an injected click is pressed now
and released after the shell has handled it.

### Keyboard

The compositor reads evdev directly. These verbs inject into the same
event path, so a test can drive setup and the shell exactly as a keyboard
would. They do not deliver text to an application: there is no focused
client surface yet.

| Verb | Reply | What it does |
|---|---|---|
| `key.press` | `OK` | hold a key: `key.press code=enter` |
| `key.release` | `OK` | let it go: `key.release code=enter` |
| `key.tap` | `OK` | press and release |

The names are the US layout's (`a`, `f1`, `leftshift`, `enter`, `esc`,
`super`, …). An unknown name is `ERR unknown key '<name>'`, not a silent
no-op.

The shell's own chords are:

| Chord | What it does |
|---|---|
| `Super` (tap) | open or close the Start Menu |
| `Super+Left` / `Super+Right` | snap the focused window to that half |
| `Super+Shift+Left` / `Super+Shift+Right` | snap it to that quarter |
| `Super+Up` | maximize; pressed again restores |
| `Super+Down` | restore |
| `Alt+Tab` / `Shift+Alt+Tab` | cycle focus forward or back |
| `Ctrl+Alt+F1`…`F6` | switch VT |

### The machine

| Verb | Reply | What it does |
|---|---|---|
| `sound.get` | `OK volume=… element=…` | the output level and the mixer it is on |
| `sound.set` | `OK` / `ERR hardware unavailable` | set the output level for real; a machine with no mixer refuses |
| `display.get` | `OK brightness=…` | the backlight |
| `display.set` | `OK` / `ERR hardware unavailable` | set the backlight for real |
| `radio.set` | `OK` / `ERR hardware unavailable` | `radio.set wifi=1 bluetooth=0`; a missing adapter refuses |
| `appearance.set` | `OK` / `ERR could not save the appearance` | `appearance.set dark=1`; the value is durable |

These do not write an override — they change the machine. That is the
difference between "show me 40%" and "be 40%". Each reports the actuator's
own result, so a control cannot light up on a machine that cannot act.

### Notifications

| Verb | Reply | What it does |
|---|---|---|
| `notify.post` | `OK` | post a notification: `notify.post <title>\|<body>` |
| `notify.list` | `OK count=… [title\|body\|time]…` | what has happened this session |
| `notify.clear` | `OK` | forget it all |

The queue is the shell's own, and it is open to any part of the system:
the compositor posts the hardware transitions it observes, and a service can
post on its own behalf through the control plane. It starts empty on every
start, so the page can say "nothing has happened" and mean it.

### Power

| Verb | Reply | What it does |
|---|---|---|
| `power reboot` | `OK` | restart the machine |
| `power restart` | `OK` | restart the machine (the same operation as reboot) |
| `power shutdown` / `poweroff` | `OK` | power the machine off |

The word is a closed set: anything else, or no word at all, is an `ERR`
and changes nothing. The reply is sent after `systemctl` has run, so it
says what happened rather than what was intended. There is no verb that
restarts only the compositor: a session that needs restarting is a failed
session, and systemd's `Restart=on-failure` is the recovery path.

### Applications

| Verb | Reply | What it does |
|---|---|---|
| `app.list` | `OK count=… [id\|state\|name]…` | the catalogue and what is installed |
| `app.launch <id>` | `OK launched <id>` | start it, as the user |
| `app.install <id>` | `OK installed <id>` / `ERR <reason>` | mark a catalogue app installed, through `finnpkg` |
| `app.remove <id>` | `OK removed <id>` / `ERR <reason>` | take an app off the install list, through `finnpkg` |

The control plane is the only place in the system that starts user
programs. The compositor asks it, a script asks it, an agent asks it, and
every one of those goes through the same audit log.

`app.install` and `app.remove` are the only writers of the install list,
and they do not write it themselves: they run `finnpkg`, which owns the
list and refuses an app whose launcher is not executable. The control
plane checks the id shape and that the catalogue knows the app before it
runs anything, and passes the package manager's own first line back as
the reason for a refusal. The wait is bounded at 1.5 seconds; a package
manager that hangs is killed so it cannot hold the single control
connection open.

An app is started by systemd, as the user, in its own transient unit: that
is how a program is started on a modern Linux, and it means the app is
tracked, logged and cleaned up by the init system rather than by a daemon
holding its process. The unit is collected when the app exits.

### Flatpak package management

Flatpak package operations are owned by `finnpkg` and routed through `finnd`,
so they use the same authenticated socket and audit log as other system
changes. Listing and search return the package manager's multi-line output as
an `OK BINARY` payload. Install and remove start a systemd service and return
its job id immediately; they do not block the control connection while
Flatpak downloads or applies a transaction.

| Verb | Reply | What it does |
|---|---|---|
| `flatpak.list` | `OK BINARY …` | list apps in the system Flatpak installation |
| `flatpak.search <term>` | `OK BINARY …` | search configured system remotes; rows include the originating remote |
| `flatpak.install <app-id> [remote]` | `OK job=<id>` | start an install job from the named configured remote (defaults to canonical Flathub) |
| `flatpak.remove <app-id>` | `OK job=<id>` | start a removal job; Flatpak user data is preserved |
| `flatpak.job <id>` | `OK job=<id> state=…` | report `queued`, `running`, `complete` or `failed` from systemd |
| `flatpak.run <app-id>` | `OK launched <app-id>` / `ERR …` | verify the app is system-installed and start it as Finn in the Wayland session |

Application ids and remote names are validated before a job is started; a
non-Flathub remote must already be configured in the system installation, and
Flathub is checked against its canonical repository URL. Use `finnctl flatpak.job`
with the returned id to observe completion; systemd may eventually garbage
collect old unit state, after which the job id expires. Finn Store reads
`flatpak.list`, searches with `flatpak.search`, and passes each result's remote
through to the audited install job rather than silently substituting Flathub.
It refreshes job state on F5 rather than blocking while a transaction runs.
A completed package transaction does not prove that an app will render or
behave correctly. The source now includes a limited xdg-shell/`wl_shm`
client host and an audited launch path, but apps that need popups, subsurfaces,
dmabuf or other unsupported protocols may fail. Flatpak is in the x86_64
image; the aarch64 manifest does not include it. This path still needs a Linux
build and a real install/discovery/launch test.

### Wine app lifecycle

Wine is included only in the x86_64 image. FinnOS deliberately exposes a
minimal lifecycle for a user-provided, already-installed or portable Windows
application rather than pretending that running an installer is a completed
app installation:

| Verb | Reply | What it does |
|---|---|---|
| `wine.list` | `OK BINARY …` | list validated IDs whose registrations and prefixes are private to Finn; newline-separated, capped at 64 KiB |
| `wine.run <id>` | `OK launched <id>` / `ERR …` | validate a registered app and launch it as Finn with its own `WINEPREFIX` |

Use `finnpkg wine register <id> <absolute-exe-path>` to register the app, and
`finnpkg wine list` to list registrations from the command line. The
control-plane verb `finnctl wine.list` returns validated IDs from Finn-owned
private registrations in a bounded binary-framed payload for clients such as
Finn Store. A missing Wine data root is an empty list; unvalidated, incomplete
or non-private entries are omitted. Registration stores
the executable path and creates a private per-app prefix directory under Finn's
home; Wine initializes the prefix when first launched. `finnpkg wine run <id>`
forwards the id through `finnctl`, so GUI processes still pass through the
authenticated, audited `finnd` boundary. `finnd` validates the id and private
registration, then uses `systemd-run` argv arguments to start Wine with the
registered executable as Finn, `WINEPREFIX`, the Wayland session environment
and a transient unit. There is no installer command or host-app auto-discovery. The
list and run verbs enforce the x86_64-only architecture guard; this does not
promise ARM Wine. Actual Wine GUI behavior remains unverified on the Linux
target.

### Windows

Provided by the compositor (`finnwindow`), proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `window.list` | `OK count=… [id\|app\|state\|title]…` | list open windows and their state (`normal`, `minimized`, `maximized`, `snapped`) |
| `window.open [app=<id>\|title=<title>]` | `OK id=…` / `ERR window limit reached (8)` | open an empty window or an app window; the ninth is refused, never evicted |
| `window.close [id=<id>]` | `OK` | close the window (or the top visible window when `id` is omitted) |
| `window.focus [id=<id>]` | `OK` | unminimize and raise the window to the front |
| `window.minimize [id=<id>]` | `OK` | toggle the window's minimized state |
| `window.maximize [id=<id>]` | `OK` | toggle the window's maximized state |
| `window.restore [id=<id>]` | `OK` | leave maximize or snap and return to the floating rect |
| `window.get [id=<id>]` | `OK id=… state=… x=… y=… w=… h=…` | the window's resolved id, state and rect; without `id`, the focused window |
| `window.move id=<id> x=<x> y=<y>` | `OK` | move the window's top-left corner; a maximized or snapped window returns to its floating size first |
| `window.resize id=<id> w=<w> h=<h>` | `OK` | set the window's size, clamped to the smallest usable window and to the screen |
| `window.snap id=<id> edge=<edge>` | `OK` | tile the window to `left`, `right`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `top` (maximize) or `restore`; the same edge twice restores |

### Finn Files

Provided by the compositor (`finnfilesapp`), proxied by the control plane.
The view reads the real filesystem as the desktop user and is read-only in
this build, because the compositor's home mount is read-only.

| Verb | Reply | What it does |
|---|---|---|
| `files.get` | `OK path=… selected=…` | the folder the view is showing and the selected entry, or `-` |
| `files.open <path>` | `OK path=…` / `ERR <reason>` | show a folder (absolute or resolved from `/`); opens the Files window if it is not already open. The rest of the line is the path, so spaces are allowed |
| `files.select <name>` | `OK` / `ERR no such item in this folder` | select an entry in the current folder; the filter is cleared |

### Finn Store

Provided by the compositor (`finnstoreapp`), not by a control verb. The
Store lists the same native catalogue as `app.list`, reads installed state
from finnpkg's list, and asks finnd for `app.install` / `app.remove`. A
catalogue app whose launcher is not executable is shown as unavailable
and has no action, because installing it would only produce finnpkg's
refusal. Removing asks for a second press. The category selector also has Flatpak Apps and Windows Apps views. The
Flatpak view lists installed apps, searches the control plane's configured
remotes on a background worker, carries each search row's remote into the
install request, and starts audited asynchronous install/remove jobs. Results
without usable source metadata cannot be installed. The UI applies query
results on its frame loop, and F5 checks a running job. The Library offers Open
for installed apps through `flatpak.run`. On an ARM Linux test VM, the current
control path launched GNOME Calculator into a standalone FinnWayland server;
the 410×666 surface responded to pointer and keyboard input. This was not a
booted FinnOS desktop or a full Store search/install/open walkthrough. The
Windows Apps view lists validated registrations in Library and exposes Open
actions through `wine.run`; registering an existing `.exe` remains a CLI step.
The path remains x86_64-only, has no installer flow, and has not had real Wine
GUI runtime validation.

### Settings

Provided by the compositor (`finnsettingsapp`), proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `settings.page [name=<page>]` | `OK` / `ERR no such page` | navigate to `wifi`, `bluetooth`, `network`, `personalization`, `about`, or a searchable legacy `display`, `sound`, or `power` page; opens the settings window if not already open |
| `settings.get` | `OK page=<page>` | read the name of the currently active settings page |

### First-boot setup

Provided by the compositor (`finnsetup`), proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `setup.get` | `OK active=… done=… lang=…` | read whether the setup screen is active, completed, and its selected language |
| `setup.show` | `OK` | show the first-boot setup screen |
| `setup.complete` | `OK` / `ERR could not save the setup` | write the marker atomically and dismiss the screen; a failed write keeps it open |

The marker's absence means the screen opens: a fresh install cannot skip
setup by accident. `setup.show` reopens it, and `FINN_SETUP_SKIP=1` is the
positive development escape. The language control is a display-name choice
only — there is no locale or keymap consumer yet.

## The shape of a session

```
client                     finnd                      finn-comp
  |  state.get                |                           |
  |-------------------------->|  (answered here)          |
### System update

| Verb | Reply | What it does |
|---|---|---|
| `ota.status` | `OK ota.status ...` | Runtime OTA state from finnpkg, never blocks |
| `ota.check` | release lookup reply | One bounded GitHub release lookup |
| `ota.download`, `ota.install`, `ota.rollback` | `OK ota.<action> job=<id>` | Queue a systemd OTA job |
| `ota.job <id>` | job state | Poll a queued OTA job |

  |  OK battery=87 ...        |                           |
  |<--------------------------|                           |
  |  screen.capture           |                           |
  |-------------------------->|-- proxy ------------------>|
  |                           |                           |  (a frame is composed)
  |  OK BINARY 3072000        |<-- relay ------------------|
  |<--------------------------|                           |
```

`finnd` serves one connection at a time, and a connection may carry many
requests. Every request is logged with the peer uid.

## Security posture

`finnd` is root-equivalent authority. It is therefore off by default, bound
to a unix socket only, authenticated per connection with `SO_PEERCRED`, and
audited. It is run under systemd hardening, and the audit log is at
`/var/log/finn/control.log`.

There is one development escape hatch, `FINN_CONTROL_ALLOW_ANY=1`, which
lets any uid connect. It is off unless the value is exactly `1` — a
variable that is merely present must not open the plane — and it exists for
the test harness and nothing else. The compositor's own socket is `0600`
root-only and checks `SO_PEERCRED`, so it is not a way around the audit
log.
