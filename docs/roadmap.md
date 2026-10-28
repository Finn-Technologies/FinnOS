# The roadmap

Where FinnOS is, and what comes next. It is written so that the next person —
human or agent — knows what "done" means for each piece, and so that nobody has
to guess which parts are promises and which are code.

The rule for this page: **an item is in "Now" only if it is in the tree and
works.** Everything else is Next or Later, and saying so is not a weakness.

## Now

| Subsystem | What exists | What "done" already means |
|---|---|---|
| **the compositor** | DRM/KMS, page flips, a GPU path and a software path that draw the same surfaces, a watchdog, a single connector | it boots, paints and recovers on the reference machine |
| **the shell** | boot splash, first-boot setup, dock (both resting positions), Control Center, Notification Center and clock page, Start Menu, frosted windows | the surfaces share their glass, radii, type and motion; each is a cached layer keyed on everything that changes it |
| **the first-party apps** | Settings, Finn Files and Finn Store, drawn in C inside `finn-comp` | each reads real state and says when it cannot act |
| **the keyboard** | `finnkbd`: evdev, modifiers and locks from held keys, `SYN_DROPPED`, hotplug, `Ctrl+Alt+F1..F6`, `Alt+Tab`, the shell's Super chords | a keyboard plugged in after boot works; a VT switch returns with a full repaint |
| **the session** | gated first boot (`/var/lib/finn/setup-done`), one versioned atomic settings store, tty2 rescue console, `finn-failure.service`, watchdog | a failed compositor is visible and there is a stated way out |
| **the control plane** | `finnd`, `finnctl`, 29 verbs owned by `finnd` and 46 by the compositor, the audit log, `SO_PEERCRED`, systemd hardening | one door, one audit trail, report and change kept apart |
| **the registry** | the catalogue the Start Menu draws and the install list the package manager owns, kept in separate files | an app that is not installed says so, and a catalogue update never wipes an install |
| **the package manager** | `finnpkg`: native install/remove, `.finn` archives with `SHA256SUMS`, Flatpak, Wine registration and launch, winget download/verify/run | a refused package is refused with a reason, before anything is written |
| **the update path** | `finnos-runtime`, `ota.check/download/install/rollback/job`, SHA-256 verification, `pacman -U`, service restart, rollback from the cache | an update is versioned, verified, reversible, and honest about its scope |
| **external Wayland clients** | `finnwayland`: xdg-shell toplevels backed by `wl_shm`, configure/close/frame/pointer/keyboard routing into `finnwindow` | a real GTK app renders and accepts input through the host |
| **the SDK** | `fuic` (`.fui` → `.fbin`), the Python client, the app templates, the byte-level format spec | the compiler and the client are tested without a device |
| **the adapter** | `finn-mcp`: 23 tools over JSON-RPC on the host, reaching the device over `ssh` and `finnctl` | JSON and MCP stay outside the OS |
| **the base image** | the Arch-derived profile, package lists, the disc's two boot menus (syslinux and systemd-boot), the GRUB menu kept for manual use, the live root, `finn-install`, `mkfinniso`, the runtime package builder | the disc builds, boots, installs to a disk and the disk boots |
| **the reference machine** | a Dell OptiPlex 3020 installs, boots and draws the desktop | see [dell-optiplex.md](dell-optiplex.md) for exactly what was observed |

The gaps inside "Now" are listed in
[architecture.md](architecture.md#what-is-not-here-yet), and they are part of
the product too.

## Next

These are the pieces that exist partly, or that daily use needs before the
system is finished. Each one says what "done" means.

### A native `.fbin` renderer and app host

`fuic` compiles a `.fui` screen to a `.fbin`, and `src/core/finnfbin.c` can
validate and decode the scene tree. The compositor does not draw one.

**Done means:** a `.fbin` opens in a window, is composited by `finn-comp` with
the same blur, clipping and input routing as a C surface, and a `.fui` app can
be installed and launched from the Start Menu without a C module.

Until then the `.fui` sources are the design language and the target, and the
working first-party apps stay in C. See
[ui-language.md](ui-language.md).

### The rest of the Wayland client surface

The host supports xdg-shell toplevels backed by `wl_shm`. Popups, subsurfaces,
`linux-dmabuf`, touch, non-unit scale, transformed buffers, clipboard data
transfer, drag-and-drop and text-input-v3 are not implemented; some requests
are accepted and ignored.

**Done means:** an ordinary GTK or Qt application opens its menus (popups),
draws its dialogs (subsurfaces), can copy and paste, and can be scaled on a
HiDPI panel. A clipboard data-device global currently exists but is inert,
because GTK refuses a display without one.

### A verified Flatpak walkthrough on x86_64

The commands, the Jobs, the remote-provenance checks and the launch path exist.
A GNOME Calculator was launched into a standalone Wayland test server on an ARM
VM; a booted FinnOS search → install → open walkthrough on the reference machine
has not been done.

**Done means:** on an installed OptiPlex, Finn Store searches Flathub, installs
an app, shows the job, lists it in the Library, and opens it into a window that
accepts pointer and keyboard input. Failures are reported with the app named.

### Wine GUI validation

Wine is a registration lifecycle, not an installer manager: the user selects an
installed executable with `finnpkg wine set`, and launch goes through the
audited `wine.run` verb with a private prefix per app.

**Done means:** a known Windows application installs into a private prefix,
launches into a FinnOS window, and its input and rendering are recorded. Real
Wine GUI behaviour on FinnOS is currently unverified.

### A terminal

There is a real console on tty2, but no terminal window in the desktop.

**Done means:** a first-party terminal or an external terminal emulator opens
in a window, reads and writes a real PTY as `finn`, and its text input works
through the same keyboard path as everything else. Text input is the hard part:
there is no text-input-v3 and no input method yet.

### Screen lock and session control

There is one graphical session for one user, no lock screen, no fast user
switching and no logout control in the shell.

**Done means:** the session can be locked without killing the compositor, the
lock surface cannot be bypassed by a client, and unlocking returns to the same
windows. This needs a decision about where authentication lives; `finnd` is
root-equivalent and is not an authentication service.

### Update signing and A/B slots

The runtime package is versioned, checksum-verified and reversible. The
checksum does not prove who built the package, and there is no second slot to
boot if an update breaks the boot path.

**Done means, in two steps:**

1. a detached signature over the runtime package, verified before `pacman -U`,
   with the public key in the image and a documented rotation;
2. an A/B layout: the new system is written to the inactive slot, the
   bootloader is told which one to boot, the new system confirms itself, and a
   failed boot falls back.

The A/B switch is the right design for a machine that is often on battery, in a
bag, with nobody watching. It is in [updates.md](updates.md#why-not-ab) as a
design, not in the code. Secure Boot signing belongs with it.

### Display coverage

One CRTC, one connector, one scale.

**Done means:** at least two outputs driven at once, a per-output scale that the
shell honours, and an automatic choice on a HiDPI panel. `screen.info` grows to
report per-output state. This is real work in the compositor, the blur buffer
and every surface's geometry.

### The other targets

Mobile, MobileXL and TV are specified in `ui/_targets.fui`. Only Desktop has
code behind it; the compositor's density model is in place and the input model
is not.

**Done means:** for one non-Desktop target, every surface adapts to its
viewport, density and type scale, the input model matches the target (touch,
remote), and the target is exercised on a device or a faithful emulator.

### One copy of the tokens

`ui/_tokens.fui` and the compositor's `finndraw.c` / `finnmotion.h` carry the
same palette and timings by hand. `fuic` resolves tokens at compile time, but
the C palette is the truth the shell draws with.

**Done means:** the token source generates or is checked against the C values by
a test, so a token change is one change and drift fails CI.

## Later

- **A second shell.** The compositor draws the shell in C. A shell that loads
  its own layout would let the OS be themed without touching the compositor —
  but a shell crash would take the display with it, so it is a trade, not a
  plan.
- **More first-party apps.** The names are decided — **Finn Code**, **Finn
  Notes**, **Finn Studio**, **Finn Sync**, **Finn Vault**, **Finn Terminal** —
  and none of them exists. An app joins the registry when its view is real; a
  name in a document is not a promise.
- **FinnAI.** There is no FinnAI today. If it arrives it will be opt-in,
  local, and never in the base image: a service the user turns on, with weights
  fetched on demand through `finnpkg`, talking to the system only through the
  control plane. The policy is that it is off by default; that is the intended
  state, not a missing feature.
- **Printing, scanning and fingerprint readers.** There is no settings surface
  and no configuration promise. A machine that needs them uses the Arch
  packages and its own configuration.
- **Multi-user.** The `finn` user is the trust boundary. More than one
  graphical user needs a per-session control plane, which is a design change,
  not a flag.

## How work is chosen

1. **A lie is worse than a gap.** If a surface claims a capability it does not
   have, fixing the claim comes before adding the capability.
2. **A bug on the reference machine outranks a feature on another target.**
   The OptiPlex is the machine the release is judged on.
3. **A change that makes a subsystem honest and testable is worth more than one
   that makes it larger.**
4. **Nothing in "Next" is a date.** The order is the order things become
   possible, not a schedule.

The release target and its verified status are in
[dell-optiplex.md](dell-optiplex.md). The honest inventory of what does not
exist is in [architecture.md](architecture.md#what-is-not-here-yet).
