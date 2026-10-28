# The control protocol

The control plane speaks a small line protocol. It is deliberately text and
not JSON: JSON is needed only at the MCP boundary, where the adapter translates
it, and keeping the on-device daemons free of a JSON parser keeps them small
and keeps a fast-moving external spec out of the operating system.

It also means any shell can drive the system:

```sh
finnctl state.get
finnctl pointer.move x=300 y=400
finnctl pointer.click
finnctl screen.capture -o - > frame.ppm
```

There is no stdin form: `finnctl` takes the verb and its arguments on the
command line, so a request is one argv and not a parser for two shapes.

## The wire

One request per line, whitespace separated, `verb arg=value ...`:

```
state.set battery=42 charging=1
pointer.move x=300 y=400
```

A line is at most **4096 bytes** (`FINN_MAX_LINE` in
`src/core/finnproto.h`). A request that does not fit is refused by the client;
a line that arrives malformed is answered once and skipped, and the connection
stays open.

Four kinds of reply:

```
OK                                  the verb did what it said
OK <single-line payload>            a value
OK BINARY <len>\n<len raw bytes>    a binary payload
ERR <message>                       it did not
```

`OK BINARY` is used wherever the answer is really a document: a capture, a
package-manager listing, a search result, an update check. The daemon bounds a
binary payload at **32 MiB**; a provider that promises more is refused rather
than trusted.

A verb that takes prose rather than `key=value` pairs takes arguments
positionally, and `notify.post` separates its two with a pipe, because a pipe
is the one character a title will not contain:

```
notify.post <title>|<body>
```

`finnctl -o FILE` writes a binary reply to a file; `-o -` writes it to stdout.
The capture is a PPM, so no image library is needed to take one.

## The sockets

| Path | Owner | Mode | Serves |
|---|---|---|---|
| `/run/finn/control.sock` | `finnd` | `0660`, root and the `finn` group | the whole protocol |
| `/run/finn/comp.sock` | `finn-comp` | `0600`, root only | the display, input and machine verbs |

Both are unix sockets. There is no TCP listener in the system, and there will
not be one: remote control is an explicit decision by the user, not a default.
`sshd` is installed but disabled on an installed machine.

The compositor's socket is a provider, not a second door. `finn-comp` checks
`SO_PEERCRED` on every connection as well as the file mode. `finnctl --direct`
talks to it, prints on stderr that it is bypassing `finnd` and its audit log,
and needs root. There is no automatic fallback: a device whose control plane is
down is unreachable rather than quietly unaudited.

### Authentication

`finnd` accepts a connection when the peer is root or the `finn` user; anything
else is refused before a verb is parsed. The socket's group lets the desktop
account open it, and the uid check is what decides. The audit log is
opened at startup; if it cannot be opened, every verb except `ping` and
`version` is refused with `ERR audit unavailable`, because an unaudited call is
not one this daemon is allowed to serve.

`FINN_CONTROL_ALLOW_ANY=1` lets any uid connect. It is off unless the value is
exactly `1` — a variable that is merely present must not open the plane — and
it exists for the test harness and nothing else.

### One client at a time

`finnd` serves one connection at a time, and a connection may carry many
requests. A second client waits in the listen backlog rather than being served
concurrently. Multiplexing is a larger change than this daemon needs; the
verbs that can take a long time are started as systemd jobs instead of being
held open.

### Who owns what

`finnd` handles the verbs that must keep working while the compositor
restarts. It proxies the verbs that can only be answered by the process that
owns the display, the pointer or the hardware.

| Handled by `finnd` | Proxied to `finn-comp` |
|---|---|
| `ping`, `version` | `screen.*` |
| `state.*` | `pointer.*` |
| `power` | `key.*` |
| `app.*` | `sound.*` |
| `flatpak.*`, `wine.*`, `winget.*`, `ota.*` | `display.*`, `radio.*`, `appearance.*` |
| | `notify.*`, `window.*`, `files.*`, `settings.*`, `setup.*` |

## The verbs

### Session

| Verb | Reply | What it does |
|---|---|---|
| `ping` | `OK FINNCTL/1` | is the plane alive |
| `version` | `OK finnd=0.1 proto=FINNCTL/1` | what is on the other end |

`FINNCTL/1` is the protocol magic in `src/core/finnproto.h`. A change that
alters the framing or the meaning of an existing reply is a new magic, not a
quiet edit.

### State

The system state: battery, charging, wireless, volume, brightness, Bluetooth,
wired link, appearance. Every field has an absent value (`-1`) rather than a
default, because the shell has to be able to say "this machine does not have
one".

| Verb | Reply | What it does |
|---|---|---|
| `state.get` | `OK battery=… charging=… wifi=… volume=… brightness=… bluetooth=… wired=… dark=…` | read the state |
| `state.set` | `OK` | write an override: `state.set battery=42 charging=1` |
| `state.clear` | `OK` | drop every override; back to the real hardware |

`state.set` accepts the eight names in the reply. An unknown key is ignored;
if none is recognised the answer is `ERR no recognised keys`.

`state.get` asks the compositor first, because the compositor merges the
overrides with the real hardware and knows about the mixer. If the compositor
is down, `finnd` reads the hardware itself and still answers.

The overrides live in `/run/finn/state`. They are a real interface: a session
daemon can publish state through them, and a test can drive the shell through
every state deterministically. They are not durable, and `state.clear` is the
way back.

### Display and input

Provided by the compositor, proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `screen.info` | `OK w=… h=… scale=…` | the framebuffer and the density |
| `screen.shell` | `OK cc=… openness=… cc_anim=… clock=… clock_openness=… menu=… menu_openness=… dock_anim=… dock=x,y,w,h pointer=x,y buttons=… mods=…` | what the shell is showing |
| `screen.capture` | `OK BINARY …` | a PPM of the frame, cursor included; `step=2` halves it |
| `screen.pointer` | `OK x=… y=… buttons=…` | where the pointer is |
| `pointer.move` | `OK` | move the pointer: `pointer.move x=300 y=400` |
| `pointer.by` | `OK` | nudge it: `pointer.by dx=10 dy=-4` |
| `pointer.click` | `OK` | press and release: `pointer.click button=left` |
| `pointer.down` / `pointer.up` | `OK` | hold a button down, or let it go |
| `pointer.scroll` | `OK` | queue wheel clicks: `pointer.scroll clicks=-3` (or `dy=`); the shell delivers them to the window under the pointer |

`button=` is `left`, `right` or `middle`; anything else is treated as left.
A click is an edge the shell can see, so an injected click is pressed now and
released after the shell has handled it.

`screen.shell` is the state a script waits on rather than sleeping and hoping:
`cc` is the Control Center, `clock` the Notification Center, `menu` the Start
Menu, `dock` the dock's resolved rect, `pointer` the cursor, `buttons` the held
button bits and `mods` the modifier mask.

### Keyboard

The compositor reads evdev directly. These verbs inject into the same event
path, so a test can drive setup and the shell exactly as a keyboard would.

| Verb | Reply | What it does |
|---|---|---|
| `key.press` | `OK` | hold a key: `key.press code=enter` |
| `key.release` | `OK` | let it go: `key.release code=enter` |
| `key.tap` | `OK` | press and release: `key.tap code=super` |

The names are the US layout's (`a`, `f1`, `leftshift`, `enter`, `esc`,
`super`, `left`, `right`, `up`, `down`, …), from the table in
`src/compositor/finnkbd.c`. An unknown name is
`ERR unknown key '<name>'`, not a silent no-op.

Injected keys follow the real event path: the shell's own chords are handled
by the compositor, and an imported Wayland toplevel receives them through the
seat. They do not synthesise text, and there is no text-input-v3 or input
method.

The shell's own chords:

| Chord | What it does |
|---|---|
| `Super` (tap) | open or close the Start Menu |
| `Super+Left` / `Super+Right` | snap the focused window to that half |
| `Super+Shift+Left` / `Super+Shift+Right` | snap it to that quarter |
| `Super+Up` | maximise; pressed again restores |
| `Super+Down` | restore |
| `Alt+Tab` / `Shift+Alt+Tab` | cycle focus forward or back |
| `Ctrl+Alt+F1`…`F6` | switch VT |

### The machine

| Verb | Reply | What it does |
|---|---|---|
| `sound.get` | `OK volume=… element=…` | the output level and the mixer it is on |
| `sound.set` | `OK` / `ERR hardware unavailable` | set the output level for real: `sound.set volume=40` |
| `display.get` | `OK brightness=…` | the backlight |
| `display.set` | `OK` / `ERR hardware unavailable` | set the backlight for real |
| `radio.set` | `OK` / `ERR hardware unavailable` | `radio.set wifi=1 bluetooth=0`; a missing adapter refuses |
| `appearance.set` | `OK` / `ERR could not save the appearance` | `appearance.set dark=1`; the value is durable |

These do not write an override — they change the machine. That is the
difference between "show me 40%" and "be 40%". Each reports the actuator's own
result, so a control cannot light up on a machine that cannot act.

`radio.set` needs at least one of `wifi=` or `bluetooth=`; if any requested
radio fails, the whole call is `ERR hardware unavailable`, because half a
change reported as `OK` is a lie.

### Notifications

| Verb | Reply | What it does |
|---|---|---|
| `notify.post` | `OK` | post a notification: `notify.post <title>\|<body>` |
| `notify.list` | `OK count=… [title\|body\|time]…` | what has happened this session |
| `notify.clear` | `OK` | forget it all |

The queue is the shell's own, and it is open to any part of the system: the
compositor posts the hardware transitions it observes, and a service can post
on its own behalf through the control plane. It starts empty on every start, so
the page can say "nothing has happened" and mean it.

### Power

| Verb | Reply | What it does |
|---|---|---|
| `power reboot` | `OK` | restart the machine |
| `power restart` | `OK` | restart the machine (the same operation as reboot) |
| `power shutdown` / `poweroff` | `OK` | power the machine off |

The word is a closed set: anything else, or no word at all, is an `ERR` and
changes nothing. The reply is sent after `systemctl` has run, so it says what
happened rather than what was intended. There is no verb that restarts only the
compositor: a session that needs restarting is a failed session, and systemd's
`Restart=on-failure` is the recovery path.

### Applications

| Verb | Reply | What it does |
|---|---|---|
| `app.list` | `OK count=… [id\|state\|name]…` | the catalogue and what is installed |
| `app.launch <id>` | `OK launched <id>` | start it, as the user |
| `app.install <id>` | `OK installed <id>` / `ERR <reason>` | mark a catalogue app installed, through `finnpkg` |
| `app.remove <id>` | `OK removed <id>` / `ERR <reason>` | take an app off the install list, through `finnpkg` |

`app.list` reports `installed` or `available`; the registry split is why those
are different words. `app.launch` refuses `ERR unknown app`, `ERR not installed`
or `ERR no runnable command` rather than starting something that is not there.

`app.install` and `app.remove` are the only writers of the install list, and
they do not write it themselves: they run `finnpkg`, which owns the list and
refuses an app whose launcher is not executable. The control plane checks the
id shape and that the catalogue knows the app before it runs anything, and
passes the package manager's own first line back as the reason for a refusal.
The wait is bounded at 1.5 seconds; a package manager that hangs is killed so it
cannot hold the single control connection open.

An app is started by systemd, as the user, in its own transient unit: it is
tracked, logged and cleaned up by the init system rather than by a daemon
holding its process. The catalogue's command is passed as one argv element,
never through a shell.

### Windows

Provided by the compositor (`finnwindow`), proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `window.list` | `OK count=… [id\|app\|state\|title]…` | list open windows and their state (`normal`, `minimized`, `maximized`, `snapped`) |
| `window.open [app=<id>\|title=<title>]` | `OK id=…` / `ERR window limit reached (8)` | open an empty window or an app window; the ninth is refused, never evicted |
| `window.close [id=<id>]` | `OK` | close the window, or the top visible window when `id` is omitted |
| `window.focus [id=<id>]` | `OK` | unminimise and raise the window to the front |
| `window.minimize [id=<id>]` | `OK` | toggle the window's minimised state |
| `window.maximize [id=<id>]` | `OK` | toggle the window's maximised state |
| `window.restore [id=<id>]` | `OK` | leave maximise or snap and return to the floating rect |
| `window.get [id=<id>]` | `OK id=… state=… x=… y=… w=… h=…` | the resolved id, state and rect; without `id`, the focused window |
| `window.move id=<id> x=<x> y=<y>` | `OK` | move the top-left corner; a maximised or snapped window returns to its floating size first |
| `window.resize id=<id> w=<w> h=<h>` | `OK` | set the size, clamped to the smallest usable window and to the screen |
| `window.snap id=<id> edge=<edge>` | `OK` | tile to `left`, `right`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `top` (maximise) or `restore`; the same edge twice restores |

### Finn Files

Provided by the compositor (`finnfilesapp`), proxied by the control plane. The
view reads the real filesystem as the desktop user and is read-only in this
build, because the compositor's home mount is read-only.

| Verb | Reply | What it does |
|---|---|---|
| `files.get` | `OK path=… selected=…` | the folder the view is showing and the selected entry, or `-` |
| `files.open <path>` | `OK path=…` / `ERR <reason>` | show a folder (absolute or resolved from `/`); opens the Files window if it is not already open. The rest of the line is the path, so spaces are allowed |
| `files.select <name>` | `OK` / `ERR no such item in this folder` | select an entry in the current folder; the filter is cleared |

### Settings

Provided by the compositor (`finnsettingsapp`), proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `settings.page [name=<page>]` | `OK` / `ERR no such page` | navigate to `wifi`, `bluetooth`, `network`, `personalization`, `about` or `update`, or a searchable legacy `display`, `sound` or `power` page; opens the Settings window if it is not already open |
| `settings.get` | `OK page=<page>` | the name of the currently active page |

### First-boot setup

Provided by the compositor (`finnsetup`), proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `setup.get` | `OK active=… done=… lang=…` | whether the screen is active, whether setup has completed, and the selected language |
| `setup.show` | `OK` | show the first-boot setup screen |
| `setup.complete` | `OK` / `ERR could not save the setup` | write the marker atomically and dismiss the screen; a failed write keeps it open |

The marker is `/var/lib/finn/setup-done`. Its absence means the screen opens: a
fresh install cannot skip setup by accident. `setup.show` reopens it,
`FINN_SETUP_SKIP=1` is the positive development escape and
`FINN_SETUP_FORCE=1` shows it again. The language control is a display-name
choice only — there is no locale or keymap consumer yet.

### Flatpak

Flatpak operations are owned by `finnpkg` and routed through `finnd`, so they
use the same authenticated socket and audit log as every other system change.
Listing and search return the package manager's multi-line output as an
`OK BINARY` payload. Install and remove start a systemd service and return its
job id immediately; they do not block the control connection while Flatpak
downloads or applies a transaction.

| Verb | Reply | What it does |
|---|---|---|
| `flatpak.list` | `OK BINARY …` | list apps in the system Flatpak installation |
| `flatpak.search <term>` | `OK BINARY …` | search configured system remotes; rows include the originating remote |
| `flatpak.install <app-id> [remote]` | `OK job=<id>` | start an install job from the named configured remote (defaults to canonical Flathub) |
| `flatpak.remove <app-id>` | `OK job=<id>` | start a removal job; Flatpak user data is preserved |
| `flatpak.job <id>` | `OK job=<id> <state>` | report `queued`, `running`, `complete` or `failed` from systemd |
| `flatpak.run <app-id>` | `OK launched <app-id>` / `ERR …` | verify the app is system-installed and start it as Finn in the Wayland session |

Application ids and remote names are validated before a job is started; a
non-Flathub remote must already be configured in the system installation, and
Flathub is checked against its canonical repository URL. Use `finnctl
flatpak.job` with the returned id to observe completion; systemd may eventually
garbage-collect old unit state, after which the job id expires.

A completed package transaction does not prove that an app will render or
behave correctly. The host supports only xdg-shell `wl_shm` toplevels; apps
that need popups, subsurfaces, dmabuf or other unsupported protocols may fail.
Flatpak is in both package manifests. Wine is x86_64-only; aarch64 refuses
Wine actions and promises no ARM Wine.

### Wine

Wine is included only in the x86_64 image. FinnOS exposes a minimal lifecycle
for a user-provided, already-installed or portable Windows application rather
than pretending that running an installer is a completed app installation.

| Verb | Reply | What it does |
|---|---|---|
| `wine.status` | `OK wine.status arch=… available=… backend=… version=… prefix=… [reason=…]` | what Windows runtime this machine really has |
| `wine.list` | `OK BINARY …` | list validated app ids whose registrations and prefixes are private to Finn; newline-separated, capped at 64 KiB |
| `wine.run <id>` | `OK launched <id>` / `ERR …` | validate a registered app and launch it as Finn with its own `WINEPREFIX` |

Use `finnpkg wine install|set|register|list|run` to manage registrations. A
missing Wine data root is an empty list; unvalidated, incomplete or non-private
entries are omitted. `finnpkg wine run <id>` forwards the id through `finnctl`,
so GUI processes still pass the authenticated, audited `finnd` boundary.
`finnd` validates the id and private registration, then uses `systemd-run` argv
arguments to start Wine with the registered executable as Finn, `WINEPREFIX`,
the Wayland session environment and a transient unit.

There is no host-app auto-discovery: after an installer runs, the user selects
the installed `.exe` with `finnpkg wine set`. The list and run verbs enforce
the x86_64-only architecture guard; aarch64 refuses Wine actions and promises
no ARM Wine. Actual Wine GUI execution has not been validated on FinnOS.

### winget

The Windows catalogue and its installer path, owned by `finnpkg` and exposed
through `finnd`.

| Verb | Reply | What it does |
|---|---|---|
| `winget.search <term>` | `OK BINARY …` | search the Windows package catalogue |
| `winget.info <id>` | `OK BINARY …` | resolve the latest installer manifest for one package |
| `winget.install <id>` | `OK winget.install job=<id>` | download, verify and run the installer as a tracked job |
| `winget.job <id>` | `OK winget.job <id> <state>` | report the job's systemd state |

`winget.install` reports that the installer exited, **not** that the
application is installed. There is no post-install discovery and no GUI
validation. Package ids are validated before use; the download is bounded and
its checksum is checked before the installer runs.

### System update

The OTA frontend installs the `finnos-runtime` package; see
[updates.md](updates.md) for what it contains and what it does not.

| Verb | Reply | What it does |
|---|---|---|
| `ota.status` | `OK ota.status current=… latest=… available=… state=… last_check=… downloaded=… rollback=… previous=… notes=… error=…` | cached state, read from `finnpkg`; never blocks on the network |
| `ota.check` | `OK BINARY …` | one bounded GitHub release lookup; the payload is the check result |
| `ota.download` | `OK ota.download job=<id>` | queue the download and checksum job |
| `ota.install` | `OK ota.install job=<id>` | queue the `pacman -U` job |
| `ota.rollback` | `OK ota.rollback job=<id>` | queue the restore of the previous runtime package |
| `ota.job <id>` | `OK ota.job <id> <state>` | report the job's systemd state |

The apply verbs are jobs because the control plane must answer before pacman
replaces the package `finnd` lives in. The restart of the two services is a
detached unit started after the reply has been sent.

## The shape of a session

```
client                     finnd                      finn-comp
  |  state.get                |                           |
  |-------------------------->|  (answered here or from   |
  |                           |   the provider)           |
  |  OK battery=87 ...        |                           |
  |<--------------------------|                           |
  |  screen.capture           |                           |
  |-------------------------->|-- proxy ------------------>|
  |                           |                           |  (a frame is composed)
  |  OK BINARY 3072000        |<-- relay ------------------|
  |<--------------------------|                           |
```

`finnd` serves one connection at a time, and a connection may carry many
requests. Every request is logged with the peer uid. A proxied request is
logged with the provider's own status, so the audit line says what the
compositor answered rather than what the control plane hoped.

The audit log is one line per call:

```
2026-10-27T20:14:03 uid=1000 state.set battery=42 -> OK
```

Non-printable bytes in the request are escaped, so a newline in a client's
argument cannot forge a second log line.

## The client

`finnctl` is the reference client and the same client an agent or a test uses:

```sh
finnctl ping
finnctl version
finnctl state.get
finnctl app.list
finnctl app.launch store
finnctl window.open app=files title=Home
finnctl screen.capture -o /tmp/shot.ppm
finnctl log 100                 # the compositor's journal, read locally
```

`finnctl log` is not a protocol verb: it reads `journalctl -u finn-comp` (or
the compositor's fallback log) on the machine where it runs.

The Python client is `FinnClient` in `sdk/`; it wraps the common verbs with
methods of the same names. The packaging, Flatpak, Wine, winget and update
families are driven with `finnctl` and `finnpkg`. See
[sdk/README.md](../sdk/README.md).

## Security posture

`finnd` is root-equivalent authority. It is therefore local-only, bound to a
unix socket, authenticated per connection with `SO_PEERCRED`, audited, and run
under systemd hardening. The compositor's own socket is `0600` root-only and
checks `SO_PEERCRED`, so it is not a way around the audit log.

The full model — what an attacker on the `finn` account can do, what is not
signed, and what the update path does and does not prove — is in
[security.md](security.md).

## Adding a verb

1. Add it to the daemon that owns it, under a namespace that already exists.
2. Proxy it in `finnd` if the compositor answers it.
3. Document it here, in the same commit.
4. Add it to `finnctl`'s usage text.
5. Keep report and change apart: `display.get` reports, `display.set` changes.
6. Bound the reply. A binary payload is capped; a single-line payload must fit
   in 4096 bytes or be moved to `OK BINARY`.
