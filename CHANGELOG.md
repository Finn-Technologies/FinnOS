# Changelog

The shape of this file is the shape of the project: the system is small,
and the changes to it are worth reading.

## Unreleased

### A session you can configure

- **First boot is gated on the marker.** `/var/lib/finn/setup-done` is now
  written atomically (settings, fsync, rename) and only after the language
  is stored. Its absence opens setup on every boot until it completes;
  `setup.complete` reports a failed write instead of claiming success.
  `FINN_SETUP_SKIP=1` is the positive development escape and
  `FINN_SETUP_FORCE=1` shows the screen again.
- **One versioned settings store.** `/var/lib/finn/settings`, `key=value`
  with a `version=1` first line, written through a temporary file and a
  rename. Volume, appearance and the setup language live there. The mixer
  is the truth for volume; a machine with no mixer and no remembered level
  reports `-1` instead of inventing 50%.
- **A keyboard.** `finnkbd` reads evdev directly, derives modifiers and
  locks from held keys, handles `SYN_DROPPED`, and grows its device list as
  keyboards are plugged in. Setup is navigable with Tab/arrows and
  Enter/Space; `Esc` closes the open panels; Super opens the Start Menu;
  `Alt+Tab` cycles windows; `Ctrl+Alt+F1..F6` switches VT.
- **A way out and a way back.** `getty@tty2` is the rescue console, the
  compositor drops DRM master when another VT takes the screen and takes it
  back with a full modeset when it returns, and `base/setup.sh` enables
  `graphical.target` rather than leaving the desktop off.

### Failure is visible

- **A flip that does not land is repaired or ends the session.** A page flip
  is bounded, a failure tries `drmModeSetCrtc`, and five failures in a row
  exit non-zero so systemd can restart the compositor. A buffer is released
  only after the frame replacing it landed.
- **A watchdog on the frame loop.** `finn-comp.service` has
  `WatchdogSec=15`; the compositor sends `WATCHDOG=1` during start-up and
  every five seconds after, so a process that is alive but no longer
  painting is restarted. `finn-failure.service` puts the reason and the
  rescue-console chord on the console the compositor was using.
- **The card is found, not assumed.** The compositor enumerates
  `/dev/dri/card*` and uses the one with a connected display; the unit waits
  for the DRM device unit rather than testing `card0` once.

### The one door

- **`comp.sock` is root-only.** Mode `0600`, and `finn-comp` checks
  `SO_PEERCRED` on every connection. `finnctl` and the SDK lost their
  automatic fallback; `--direct` is an explicit development flag that says
  on stderr what it bypasses.
- **The control plane frames lines.** `finn_conn` buffers per connection, so
  a request split across packets is not executed twice, an overlong line is
  answered and skipped, and one connection can carry many requests.
- **The actuators tell the truth.** `sound.set`, `display.set` and
  `radio.set` report the hardware's result; `power` accepts only
  reboot/restart/shutdown/poweroff, and answers after `systemctl`; the audit
  log is `0600` and escapes peer text.

### The installer

- `finn-install` requires `--disk`, refuses the live medium, asks for typed
  confirmation (or `--yes`), and runs under `set -euo pipefail`. The root
  UUID comes from the mounted target, not the new root's empty directory.
- The chroot image has no default password and no passwordless sudo. The
  account's password is generated randomly and printed once; sshd is off by
  default; `sudo` requires a password.

### The tests

- `make test-c` builds and runs the C tests under ASan and UBSan: line
  framing against a socket pair, and the settings store against a temporary
  directory.

### The Figma Redesign: Centered Navigation & Floating Islands Dock

- **Centered Navigation Pill (`finndock`)**: The middle navigation island
  (Search, Home launcher, and Window Manager) is strictly anchored to the
  exact screen center `(screen_w - p2_w) / 2`, keeping the primary navigation
  balanced regardless of open taskbar items or status chip changes.
- **4-Island Floating Dock Architecture**:
  - Pill 1 (Taskbar): Quick-launch app chips (`settings`, `files`, `arc`)
    with active indicator dots, plus running app pills displaying icon, title,
    and close button.
  - Pill 2 (Navigation): Centered Search, Home launcher (with active 42x42 well
    indicator when Start Menu is open), and Window Manager overview icon.
  - Pill 3 (Clock & Weather): Live time (`HH:MM`), formatted date, and weather
    chip (sun icon + temperature).
  - Pill 4 (Status & Quick Controls): Wi-Fi/Ethernet, Bluetooth, and battery
    percentage (`▮ 100%`) opening the Control Center.
- **Native Vector Icon Suite & Gradient Preservation**:
  - Full-color brand chips (`app_arc`, `app_files`, `app_settings`, `avatar`,
    `sun_weather`) render with native SVG fills and multi-stop gradients
    preserved (`active = 0, inactive = 0` bypasses tinting).
  - Clean cubic Bézier arcs (`M/L/C/Z`) replacing elliptical arc commands
    (`A/a`) unsupported by `finnvg`.
  - Secondary stroke/fill tinting for two-tone system glyphs (`windows`,
    `battery`, `volume`, `power`).
  - App registry and installed state synchronized with Figma app suite
    (`settings`, `files`, `arc`, `window`).
  - Start Menu user avatar header matching Figma `9:51` with gradient avatar chip
    and user handle.

### The Figma Shell & Window Manager

- **Boot splash (`finnboot`)**: the 4-shape FinnOS mark and progress track
  over `#000000`, stepped through compositor initialization (`ui/Boot.fui`).
- **First-boot setup (`finnsetup`)**: full-screen onboarding with the
  rounded left hero card (`setup.webp`), ambient `LAYER_BLUR` glow,
  language selector pill, accessibility appearance toggle, and `Start`
  action in both Light and Dark appearances (`ui/Setup.fui`), plus
  `setup.get`, `setup.show`, and `setup.complete`.
- **Frosted windows (`finnwindow`)**: `500x300` (`r=15`) frosted glass
  windows with the top-right `72x24` (`r=8`) window controls pill
  (expand, minimize, close), titlebar drag clamped above the dock band,
  a focus/raise model, `Alt+Tab` cycling, and an empty-window state
  (`ui/Window.fui`), plus `window.list`, `window.open`, `window.close`,
  `window.focus`, `window.minimize`, `window.maximize`, and `window.move`.
  The ninth window is refused rather than silently replacing one.
  There are no built-in app views: an app tile opens an empty window, and
  the client surface protocol that would fill it does not exist yet.
- **A dock that makes room.** A maximized window now fills the screen above
  the dock, edge to edge. When a window is maximized or reaches the dock's
  resting band, the dock sinks flush to the bottom edge and its three
  bubbles close into one surface with the controls drawn closer together.
  When the window leaves, the dock rises back to its floating position.
- **Control Center (`finncontrol`) & Notification Center (`finnclock`)**:
  aligned 1:1 with the Figma `260x260` (`r=15`) frosted panels, with the
  2-tile connectivity row, brightness and volume sliders, battery/edit/
  settings footer row (`ui/ControlCenter.fui`), and inline time + date
  header, notification cards, and `Clear all` pill
  (`ui/NotificationCenter.fui`).
- **Base OS (`base/`) & FinnSDK (`sdk/finnsdk`)**: self-contained FinnOS
  rootfs, bootloader configs (systemd-boot, GRUB, syslinux), `finn-install`
  installer, and `mkfinniso` builder in `base/`; renamed `sdk/findsdk` to
  `sdk/finnsdk` per `NAMING.md`.

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
  format is specified byte for byte in `sdk/finnsdk/fuic/FORMAT.md`, and
  the writer and the reader are the same code, so the two cannot drift.
- **The dialect the tree actually uses.** Calls with named or positional
  arguments, `+` for composition, `->` for a per-target mapping, `@` for
  an attenuation, `;` as a statement separator, comma-continued values,
  and the theme and target declaration forms. Every `.fui` file in the
  tree compiles with no warnings.
- **A client library.** `finnsdk.FinnClient` turns the protocol's verbs
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
