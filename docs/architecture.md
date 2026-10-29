# The architecture of FinnOS

FinnOS is an operating system for a small, quiet machine: an i3-class desktop
or laptop, a screen, a pointer, a keyboard and a battery if there is one. It is
built around a few decisions that everything else follows from.

- **The compositor owns the display.** Direct DRM/KMS and page flips; no X11
  and no external Wayland compositor. `finn-comp` also hosts a deliberately
  limited in-process xdg-shell server for application windows. The shell is
  drawn by the same process that owns the scanout buffer.
- **The control plane owns the authority.** One permissioned entry point for
  state, input, display, power, windows, packages and apps. It is a unix
  socket and never TCP, every call is audited, and it is the only thing that
  starts a user program or changes the machine.
- **Nothing is faked.** A machine with no wireless adapter says so. An app
  that is not installed says so. Every field in the system state has an
  "absent" value rather than a plausible default.
- **No AI ships.** There is no model, no weights and no inference code in the
  tree, and nothing in the OS needs a network call to a model. Any future
  FinnAI is opt-in and off by default.
- **The base is Arch Linux.** The kernel, firmware, pacman and systemd are the
  distribution's. FinnOS replaces the display server, the session and the
  desktop, and leaves the parts that already work alone.

![The desktop](images/desktop-light.png)

## The shape

Two long-lived processes, and as many transient application units as the
session has open windows.

```
  scripts, the SDK,          ┌────────────────────────────────────────┐
  finn-mcp on the host  ────▶│  finnd — the control plane             │
                             │  /run/finn/control.sock   0660         │
                             │  owned by root, group finn            │
                             │  every call logged with its peer uid   │
                             └───────┬───────────────────────┬────────┘
                                     │                       │
              owns state, power,     │                       │ proxies display,
              apps, packages, OTA    │                       │ input, sound,
                                     │                       │ appearance, windows
                     ┌───────────────▼───────┐               │
                     │  systemd-run          │               │
                     │  one transient unit   │               │
                     │  per launched app     │               │
                     └───────────────────────┘               │
                                                             ▼
                             ┌────────────────────────────────────────┐
                             │  finn-comp — the compositor            │
                             │  /run/finn/comp.sock      0600, root   │
                             │  owns the scanout buffer, the pointer, │
                             │  the keyboard and every shell surface  │
                             └────────────────────────────────────────┘
```

`finnd` is the API. It handles the verbs that must keep working when the
compositor restarts — state, power, apps, packages, updates — and proxies the
rest to `finn-comp`, which is the only thing that can move the pointer, read
the framebuffer or touch the display hardware.

The shell is not a third process. The dock, the Control Center, the clock page,
the Start Menu and the windows are *surfaces*: they are rasterised into cached
layers and composited into the same frame as the wallpaper, so a surface costs
a blit rather than a round trip over a socket. That is a deliberate trade: one
process does more, and the desktop never waits for itself.

The one piece of the control path that runs where the agent runs is the MCP
adapter. The device speaks a small line protocol that any shell can drive; JSON
exists only at that outer boundary.

### The binaries

| Binary | What it is | Where it runs |
|---|---|---|
| `finn-comp` | the compositor: DRM/KMS, the backends, the shell surfaces | device |
| `finnd` | the control plane: the one permissioned entry point | device |
| `finnctl` | the control-plane client; every verb is scriptable | device |
| `finnpkg` | the package manager and the OTA frontend | device |
| `finn-window` | a small client that opens or focuses a window | device |
| `finn-browser` | the headless Chromium driver and frame streamer for Finn Browser | device |
| `finn-mcp` | the MCP adapter | host |

## The layers

```
        clients      finnctl · finnsdk · finn-mcp (host) · shell scripts
                            │  line protocol
        control       finnd
                            │  proxy of display/input/machine verbs
        display       finn-comp · finnvg · finntext · finndraw · finnblur
                      finnanim · finnpointer · finnkbd · finngpu
                      finndock · finncontrol · finnclock · finnmenu
                      finnwindow · finnsettingsapp · finnfilesapp
                      finnstoreapp · finnbrowserapp · finnsetup · finnboot · finnwayland
                            │  DRM/KMS, evdev, ALSA, sysfs
        kernel        Arch Linux: linux, mesa, firmware, systemd
```

`src/core` is the shared library beneath the two daemons: the protocol header,
the socket helpers, the line framing, the state reader, the app registry, the
settings store, the audio reader, the taskbar pins and the `.fbin` reader. It is
data and plumbing only. The rule the split follows:

> **the core knows nothing about the compositor or the control plane, and
> neither of them knows about the other.** The one thing that crosses the line
> is the protocol, and the protocol is one header
> (`src/core/finnproto.h`).

## The display path

### One process, two backends

The compositor has a software path and an accelerated path, and it picks
between them by probing rather than by assuming. GBM and EGL are opened, the
plane list is asked which 32-bit scanout format the CRTC takes, and the EGL
config is matched against that visual. A buffer is handed to KMS with its
modifier when the driver supplies one, with linear AddFB2, legacy AddFB and a
dma-buf import as fallbacks. A machine that cannot give back a scanout-capable
buffer lands on the software path with nothing else changed.

Both paths compose the same surfaces, in the same order, from the same cached
layers. The difference is that the software path `memcpy`s the layers into a
dumb buffer while the accelerated path pastes them as quads. That is why a
surface change usually has to be made in both places.

The display is a single CRTC and a single connector. There is no multi-monitor
support: the compositor picks the first connected connector that has a mode and
drives it.

### Frames are earned, not scheduled

Nothing here runs at a fixed frame rate. A frame is composed when something
actually changed: the wallpaper is decoded once, the dock is rasterised only
when its state moves, the cursor is composited over a cached base, and an
untouched desktop redraws nothing at all. The page flip is the clock — the next
frame is due when the last one has landed.

The animation tracks shorten the poll while they move. When nothing is moving,
the loop still wakes a few times a second to notice a new minute on the dock
clock and to feed the watchdog; it does not draw unless one of those changed.

The compositor feeds systemd's watchdog (`sd_notify(WATCHDOG=1)`) from the
frame loop. A process that is alive but has stopped painting is restarted
rather than left as a frozen image.

### The shell surfaces

All surfaces are drawn in the same language: the same frosted glass, the same
corner radii, the same type scale, the same motion. They are rasterised into
transparent layers, and the compositor blits them.

| Surface | Module | Anchored to | Opens from |
|---|---|---|---|
| boot screen | `finnboot` | centred on black | compositor wake-up |
| setup screen | `finnsetup` | full screen | first boot or `setup.show` |
| dock | `finndock` | the bottom of the screen | always there; sinks flush when a window reaches it |
| Control Center | `finncontrol` | the tray pill | the tray's status block |
| Notification Center and clock page | `finnclock` | the clock | the clock itself |
| Start Menu | `finnmenu` | the middle of the screen | the dock's middle launcher button |
| windows | `finnwindow` | free-floating on the desktop | the dock's taskbar button, the Start Menu, or `window.open` |
| Settings | `finnsettingsapp` | a window | the Start Menu, the Control Center, `settings.page` |
| Finn Files | `finnfilesapp` | a window | the Start Menu or `files.open` |
| Finn Store | `finnstoreapp` | a window | the Start Menu |
| Finn Browser | `finnbrowserapp` | a window | the Start Menu, the dock, or `window.open` |

The Start Menu is the one surface that is not anchored to a control: it is the
system's front door, so it stands in the middle of the room and the desktop
dims behind it.

The dock has two resting positions, and they are one piece of geometry with
the windows. At rest it floats clear of the bottom edge as three bubbles. When
a window is maximised, or when any window reaches the band the dock rests in,
the dock sinks flush to the bottom edge and its bubbles become one surface with
the controls closer together. A maximised window ends exactly where that band
begins, so the whole screen above the dock belongs to it and nothing is hidden
behind the dock.

The blur is live, not a wallpaper proxy: `finnblur` keeps a blurred copy of
what the compositor has actually drawn — windows included — at one eighth
scale, and rebuilds it only when the desktop underneath changes. A frosted
surface therefore samples the window behind it, not the wallpaper behind both.

### External application surfaces

`finnwayland` is an in-process xdg-shell server. It imports ARGB8888/XRGB8888
`wl_shm` toplevels into `finnwindow`, routes configure, close, frame callbacks,
focus, pointer, wheel and keyboard events, and applies the window manager's
existing resize, snap, maximise and restore behaviour.

It is a compatibility layer, not a Wayland desktop. Popups, subsurfaces,
linux-dmabuf, touch, non-unit scale, transformed buffers, clipboard and
drag-and-drop are not implemented, and some protocol requests are no-ops. A
client that creates a clipboard data-device global is given an inert one,
because GTK refuses a display without it; no data crosses.

This path was exercised with a real GTK Calculator through a standalone test
server on an ARM Linux VM. It has not been verified as a full desktop
walkthrough on installed FinnOS.

### Single-Chrome Windows and Third-Party Toolkits

FinnOS aims for single-chrome windows where application windows use the system's
native titlebars and decorations rather than duplicated or mismatched foreign
titlebars.

**What works**:
- `finn-comp` advertises `zxdg_decoration_manager_v1` (server-side mode) and
  `org_kde_kwin_server_decoration_manager` (server mode).
- `finnd` passes `GTK_CSD=0` to launched apps and Flatpaks (`--env=GTK_CSD=0`).
- Toolkits that honour server-side decoration or `GTK_CSD=0` (standard GTK3 apps,
  standard GTK4 `GtkWindow`s without a custom titlebar, Qt/SDL/winit clients
  using `xdg-decoration`, and Wine apps configured with `Decorated=N` and Finn
  Light/Dark `Control Panel\Colors`) open with Finn's titlebar as the only
  chrome.

**What still shows foreign chrome and why**:
- GTK4 / libadwaita applications that explicitly set a custom client titlebar
  widget (`gtk_window_set_titlebar()` or `AdwApplicationWindow` /
  `AdwHeaderBar`, such as GNOME Calculator `org.gnome.Calculator`)
  unconditionally render their own headerbar inside the client surface and
  ignore both `zxdg_decoration_manager_v1` /
  `org_kde_kwin_server_decoration_manager` and `GTK_CSD=0`. Because FinnOS
  never crops or guesses client headerbars out of the pixel stream, these apps
  still display their internal `AdwHeaderBar` beneath Finn's titlebar.

## The control plane

`finnd` is root-equivalent authority over a running system. It can move the
pointer, read the framebuffer, start programs and power the machine off. That
is why it is:

- **local-only**, bound to a unix socket and never to TCP;
- **authenticated per connection** with `SO_PEERCRED`: root, or the `finn`
  user;
- **audited** to `/var/log/finn/control.log`, with the peer uid on every call;
- **run under systemd hardening** (`NoNewPrivileges`, `ProtectHome`,
  `ProtectKernelTunables`, `SystemCallFilter=@system-service`, and more).

The service runs on an installed machine, because the shell talks to it. What
is *not* a default is remote access: there is no TCP listener, `sshd` is
installed but disabled, and nothing outside the machine can reach the socket
until someone on the machine enables a network login.

The socket belongs to root and to the `finn` group, and to nobody else. The
daemon runs in root's own group, and the `finn` group is set on the socket
itself, which is where it matters. It does not change uid: it asks systemd for
a transient unit, and systemd starts the app as the user.

The compositor's socket is not a second door. It is `/run/finn/comp.sock`,
mode `0600`, owned by root, and `finn-comp` checks `SO_PEERCRED` on every
accepted connection as well. `finnctl --direct` is the explicit development
path to it; there is no automatic fallback, because a device whose control
plane is down should be unreachable rather than quietly unaudited.

An app is started by systemd, as the user, in its own transient unit. The
control plane asks systemd to do it rather than changing uid itself: the
daemon is hardened with `NoNewPrivileges`, and an app that systemd started is
an app systemd knows about, logs and cleans up. The catalogue's command is
passed as one argument, never through a shell.

The protocol is documented in [control-protocol.md](control-protocol.md); the
security model is in [security.md](security.md).

## The application registry

Two files, two owners:

| File | Owner | Says |
|---|---|---|
| `/usr/share/finn/apps.registry` | the OS | which apps exist, what they are called, what runs them |
| `/var/lib/finnpkg/installed` | the package manager | which of them are on this machine |

Keeping them apart is what lets the shell show an app that is honestly "not
installed" without the package manager having to edit a file the OS ships — and
it keeps a catalogue update from wiping the user's install list.

The catalogue ships four native entries: **Settings** (`settings-app.svg`),
**Finn Files** (`files.svg`), **Finn Store** (`store.svg`) and **Finn Browser**
(`browser.svg`). All four are first-party applications drawn inside the
compositor. It also carries remote catalogue entries whose `source` is
`flatpak` or `winget`; those are discovery records for the Store, not native
apps, and they have no local command.

An app joins the native catalogue when its view is real. An entry is not a
promise that the window is useful. **Finn Browser** combines native shell chrome
with an isolated web engine: `finnbrowserapp` in `finn-comp` draws native tabs,
address bar, navigation, and find bar directly into the compositor's frame,
while `/usr/local/bin/finn-browser` drives headless Chromium over
`--remote-debugging-pipe` on fds 3 & 4 and streams decoded `0xFFRRGGBB` frames
over `/run/finn/browser.sock` (`0660`, `root:finn`, `SO_PEERCRED`-checked).

An app is "installed" when the package manager has recorded its id, and it
records that only after checking that the launcher is executable. A lit tile
that does nothing when pressed is exactly the kind of lie this system does not
tell.

## The session

A session starts with `finn-comp` on tty1, under systemd, after the DRM device
unit exists. Four pieces make it a session rather than a demo.

**Setup is a gate, not a suggestion.** `/var/lib/finn/setup-done` is written
atomically (temporary file, fsync, rename) and only after the language has been
saved. Its absence means setup opens, on every boot, until it completes.
`FINN_SETUP_SKIP=1` is the positive development escape; `FINN_SETUP_FORCE=1`
shows the screen again. The language control is a display-name choice only:
there is no locale or keymap consumer yet, and the screen says so. The
appearance field is a Light/Dark choice; there is no separate accessibility or
reduced-motion subsystem yet.

**Settings are one versioned store.** `/var/lib/finn/settings` is line-oriented
`key=value`, stamped with `version=1`, written through a temporary file and a
rename. Volume, appearance and the setup language live there. The mixer is the
truth for volume whenever there is one; the stored level is what a machine
without a mixer remembers, and with neither the answer is absent (`-1`).

**The keyboard and the console.** `finnkbd` reads evdev directly, tracks its
own modifier and lock state, handles `SYN_DROPPED`, and picks up keyboards
plugged in after boot. `Ctrl+Alt+F1..F6` switches VT; `getty@tty2` is the
rescue console, and the same chord returns to the session because the evdev
reader still sees it there. DRM master is dropped on the way out and retaken on
the way back, with a full modeset so the returning session repaints.

**A failed compositor is visible and recoverable.** `finn-comp.service` has a
start limit and `OnFailure=finn-failure.service`; the failure unit writes one
line to `/dev/console` and the journal — "FinnOS: the compositor stopped; press
Ctrl+Alt+F2 for a console" — so a broken session is visible on the screen the
compositor was painting and there is a stated way out. The watchdog restarts a
compositor that is alive but no longer painting.

State has two layers. `finnstate` reads the real hardware — battery, backlight,
wireless, Bluetooth, wired link, volume — and reports `-1` for anything absent.
`/run/finn/state` holds overrides written by `state.set`, which is a real
interface: a session daemon can publish state through it, and a test can drive
the shell through every state deterministically. `state.clear` returns to the
hardware. The overrides are not durable and they are not hardware.

Nothing in the session reads a model, and nothing needs the network.

## The base image

`base/` is the source of the operating system: the package set, the
configuration, the boot menus, the live root, the units and the installer. The
image is built natively from that tree with `mkarchiso`; no base image is
downloaded and no third-party ISO is remastered. `base/README.md` explains what
is stripped and why.

Two machines come out of one tree:

- the **live medium** (`base/airootfs`): a console that installs FinnOS, with
  the sources at `/opt/finnos` and the installer on the live shell;
- the **installed machine** (`base/installed` plus `base/setup.sh`): the
  package set, the units, the branding and the session.

The installer partitions the named disk, enables `[multilib]` when it installs
Wine, pacstraps the base, copies the tree, builds the compositor on the
machine, installs GRUB or systemd-boot, and prints `FINN_INSTALL_COMPLETE` only
after looking at the disk it wrote.

## Updates

A GitHub release carries a `finnos-runtime` pacman package: the compositor,
the control plane, `finnpkg`, the registry, the icons, the wallpapers, the
fonts and the units. `ota.check` reads the release, `ota.download` verifies its
SHA-256, `ota.install` applies it with `pacman -U`, and `ota.rollback` restores
the previous package from the cache. The kernel, firmware and bootloader still
move with `pacman -Syu`.

This is a userland update, not an A/B slot switch. The machine can roll the
runtime package back; it cannot boot a kernel that is no longer installed. The
A/B design is in [roadmap.md](roadmap.md), not in the code. See
[updates.md](updates.md).

## What is not here yet

Honesty is a property of the documentation too. These are the parts of the
system that do not exist yet, stated plainly so that nobody looks for them.

- **No `.fbin` renderer or native app host.** The core can validate and decode
  a compiled scene tree (`src/core/finnfbin.c`), and `fuic` compiles `.fui` to
  `.fbin`, but the compositor does not rasterise a `.fbin` into a window and
  does not launch one. The first-party apps that work are drawn in C inside
  `finn-comp`; the `.fui` sources are the design language and the target, not
  the running implementation.
- **External Wayland clients are deliberately limited.** `finnwayland`
  implements xdg-shell toplevels backed by `wl_shm`. Popups, subsurfaces,
  linux-dmabuf, touch, non-unit scale, transformed buffers, clipboard data
  transfer and drag-and-drop are not supported; text-input-v3 and input
  methods are not provided. Some requests are accepted and ignored. The path
  has been exercised on a standalone test server, not across a full installed
  desktop.
- **One display, one scale.** A single CRTC and connector; no multi-monitor, no
  per-output scaling, and no automatic HiDPI selection. `screen.info` reports
  the density the compositor is actually using.
- **No screen lock and no session switching.** There is one graphical session
  for one user, started by systemd. `Ctrl+Alt+F2` gives a text console; there
  is no lock screen, no fast user switching and no logout control in the shell.
- **No terminal application in the catalogue.** A real console is available on
  tty2; the desktop has no terminal window.
- **Flatpak is a compatibility path, not a verified store.** The commands, the
  Jobs, the remote-provenance checks and the launch path exist, and a GNOME
  Calculator was launched into a standalone test server. A booted FinnOS
  search → install → open walkthrough has not been done. Apps that need
  unsupported protocols can fail.
- **Wine is a registration lifecycle, not an installer manager.** `finnpkg
  wine install|set|register|list|run` and the audited `wine.run` path exist on
  x86_64 only. There is no host-app auto-discovery; the user selects the
  installed executable. Real Wine GUI behaviour has not been validated on
  FinnOS. aarch64 refuses Wine actions and promises no ARM Wine.
- **winget is a catalogue and installer runner, not an app manager.** Search,
  info, download, verify and run exist; the job reports that the installer
  exited, not that the application is installed. GUI behaviour is unverified.
- **No A/B update slots, and no package signatures.** The runtime package is
  versioned, checksum-verified, reversible and honest about its scope. The
  checksum protects against a truncated or corrupted download; it does not
  prove who built the package.
- **Secure Boot is not signed.** The installer writes a normal GRUB or
  systemd-boot setup; firmware Secure Boot must be disabled or given your own
  key.
- **No printing, scanning or fingerprint support.** There is no settings
  surface for them and no promise that the Arch packages are configured.
- **The other targets are specifications.** Mobile, MobileXL and TV exist in
  `ui/_targets.fui` and there is no code behind them. The compositor's density
  model is in place; the input model is not.
- **The token table is kept in two places by hand.** `ui/_tokens.fui` and the
  compositor's `finndraw.c` / `finnmotion.h` must be kept in step. `fuic`
  resolves tokens at compile time, but the C palette is the truth the shell
  draws with.
- **No FinnAI, and no AI anywhere.** There is no model, no weights and no
  inference code in the tree; the policy is that any future FinnAI is opt-in
  and off by default. That is the intent, not an omission.

If a page here and the code disagree, the code is right only if the code is
right: fix whichever one is wrong, and say so.
