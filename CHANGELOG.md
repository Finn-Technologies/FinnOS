# Changelog

The shape of this file is the shape of the project: the system is small,
and the changes to it are worth reading.

## Unreleased

### Single-chrome window decoration and toolkit integration

- `finn-comp` advertises `zxdg_decoration_manager_v1` (server-side mode) and
  `org_kde_kwin_server_decoration_manager` (server mode) so client toolkits
  negotiate server-side window framing.
- `finnd` passes `GTK_CSD=0` (`--env=GTK_CSD=0`) to launched applications and
  Flatpaks, ensuring applications that honour server-side decoration or
  `GTK_CSD=0` (GTK3, standard GTK4 `GtkWindow`s, Qt, SDL, winit) open with
  Finn's titlebar as the single window chrome.
- Wine applications receive undecorated Wayland framing (`Decorated=N`) and
  Light/Dark palette synchronization matching FinnOS system appearance via
  `Control Panel\Colors`.

### Finn Browser

- Added first-party web browser (**Finn Browser**, `browser` in
  `assets/apps.registry` and `/var/lib/finnpkg/installed`).
- `finnbrowserapp` in `finn-comp` draws native tabs, address bar, navigation
  buttons, and find bar directly into the compositor's frame.
- `/usr/local/bin/finn-browser` drives headless Chromium over
  `--remote-debugging-pipe` on fds 3 & 4 and streams decoded `0xFFRRGGBB` frames
  over `/run/finn/browser.sock` (`0660`, `root:finn`, `SO_PEERCRED`-checked).
- When headless Chromium (`chromium` or Flatpak `org.chromium.Chromium`) is not
  installed, the browser window opens cleanly and reports that the engine is
  unavailable.

### The shell is drawn at one scale, from the Figma frames

- `finn_ui_scale()` is now the one density factor for every surface:
  `min(width / 1280, height / 832)`. The dock, menu, Control Center, boot,
  setup, window, Finn Files and Finn Store no longer carry private scales, and
  the design units in the C sources line up with the 1280 × 832 Figma canvas
  again.
- The Start Menu is rebuilt around the frames: a split `Apps | Pinned` header,
  rows 36 high on a 41 pitch, a `View all` action, Recent activity, the
  account row, and three views — Home, All Apps and Search. The right panel's
  two grey placeholders are now a clock/date widget and a machine-status
  widget, because a blank card claimed a capability the design did not have.
- The Control Center matches `Desktop - Control Center`: the 345 × 35
  brightness row, the 35-unit appearance circle, the 385 × 35 volume row and
  the `edit-01` pen in the footer. The pen opens Settings' Personalization
  page.
- Settings matches the Settings frame: a 250-unit sidebar with its 220 × 30
  search field, opaque content paper, `#F9F9F9` cards, and the About hero and
  statistic tiles. Missing sensor data is reported as "Not reported", never
  as an invented model name.
- The window body is the frame's 70% glass; the dark appearance lays the same
  glass at 84% so it stays glass without losing the ink. Settings, Finn Files
  and Finn Store keep that sheet on their sidebars instead of laying a second
  frost, and Settings' pages and the Files listing draw opaque paper on the
  content column so a wallpaper stripe cannot run through a row of text.
- Finn Store and Finn Files align with the Settings (`186:3941`) shell language:
  a 250-unit glass sidebar with 220 × 30 search pills and 41-pitch navigation
  rows, an opaque `#FFFFFF` content sheet on the right (`#1A1D24` in dark mode)
  with `#F9F9F9` (`#232730` dark) 5 px cards, and unified 24-unit headers.
  Finn Store's titlebar is now draggable across its empty header space just like
  Settings and Finn Files.
- Control Center and Settings sliders now clip their fill bars against the
  outer pill radius (`finn_fill_rrect_clipped_left`), keeping both the low-end
  left cap and 100% right cap cleanly rounded, and omit the white fill circle
  when a slider is at `0%` or unavailable (`pct <= 0`). Sliders also respond to
  mouse-wheel scrolling (`±5%` per notch) and snap immediately while dragging
  without waiting on the 2 Hz `/run/finn/state` poll.
- Multi-window compositing caches each open window in its own layer slot with
  an incremental blurred-backdrop cache in `finnwindow.c`, so dragging or
  interacting with one window with multiple apps open no longer re-blurs and
  rerasters every background window each frame.
- Dark mode renders consistently across the taskbar (`finndock`), Notification
  Center (`finnclock`), and Settings sidebar: dark pill fills, active wells,
  hover states, and calendar cells replace hardcoded white fills, and
  notifications posted while the clock panel is closed initialize their text
  alpha so they appear legible on first open.
- The dock's focused tile is the frame's 34 × 34 white well behind the 26 px
  icon, with the 4 × 1.5 blue indicator. With no weather source, the 139-wide
  time pill keeps its width and shows the date in the second column.
- Recent activity is real: `finnd` records a launch only after it starts the
  app, in `/run/finn/state/recent`, newest first and deduplicated.

### A launch no longer stalls the frame that shows the window

- The compositor used to wait on the control plane's `app.launch` reply on its
  event loop. `finnd` starts apps through a transient systemd unit, which can
  take hundreds of milliseconds, so the window appeared only after the unit
  was up. The request is now written to the socket and its reply is collected
  on a later loop pass; the window appears first and the notification still
  reports what `finnd` actually said.

### The GPU path asks the machine what it can scan out

- `finngpu` negotiates XRGB8888 or ARGB8888 against the CRTC's planes instead
  of assuming XRGB, and picks the EGL config that matches the chosen visual.
- Framebuffers are built with `drmModeAddFB2WithModifiers` first, so a tiled
  buffer is accepted on drivers that require its modifier; linear AddFB2,
  legacy AddFB and a dma-buf import are the fallbacks.
- The EGL display tries `eglGetPlatformDisplayEXT`, the core EGL 1.5 entry
  point, and `eglGetDisplay`, so a driver that ships only one of them still
  gets the accelerated path.

### The documentation is rewritten around the system that exists

- `README.md`, `CONTRIBUTING.md` and `AGENTS.md` are rewritten as a front
  door, a contributor guide and a working agreement, with the tree, the dev
  loop and the rules stated once each.
- `docs/` gains an index (`docs/README.md`) and four new pages: `install.md`
  (the disc, the stick, the installer, recovery), `testing.md` (the suites,
  CI, and what only hardware proves), `security.md` (authority, audit, the
  gaps) and `troubleshooting.md` (boot, session, input, graphics, updates).
- `docs/ota.md` is now `docs/updates.md`, because the page describes a
  runtime package and an OTA frontend, not a stub.
- The old working-OS review moves to
  `docs/audits/2026-10-working-os-review.md` and carries a historical banner;
  it is no longer mistaken for current documentation.
- Claims that had drifted from the code are corrected: the control plane is
  enabled and local-only rather than "off by default"; the runtime update path
  exists; `finnpkg` is not a stub; Flatpak is in both package manifests; the
  compositor has a limited in-process Wayland host.
- `ui/_targets.fui` and `ui/Desktop.fui` no longer say there is no window
  manager or keyboard. The Desktop target names the window manager and the
  keyboard that exist; the other three targets remain specifications.
- The syslinux menu's help line no longer says "erase the first disk", which
  the installer has not done since it started requiring a named disk.

### `fuic new` writes a manifest `finnpkg` can read

- The template is now `manifest.toml` with `key = value` pairs, the shape
  `finnpkg install` reads, instead of a pipe-separated `.manifest` that
  nothing read and that still called the package manager a stub.

### The deploy script can prepare a bare rootfs

- `vm/deploy.sh` initializes the pacman keyring and sync database, installs
  the build dependencies, creates `finn`, and installs and enables the
  units. A fresh Arch Linux ARM rootfs no longer needs those steps by hand
  before the first deploy.

### The install medium boots with a console and asks for its disk

- The install entries put the kernel on `tty0` and mirror the console to
  `ttyS0`, so a serial log shows the boot and the installer instead of a
  blank screen.
- The installer mirrors its own output through a FIFO instead of a process
  substitution, so it still starts on a live root that does not carry
  `/dev/fd`; the console it was started from keeps its copy.
- The disc turns systemd's first-boot wizard off. The live root carries an
  uninitialized machine-id, and the wizard used to take `tty1` and ask for a
  timezone instead of letting the installer start.
- Booting **Install FinnOS** without a disk now lists the disks and asks
  for one, then asks again before writing. It no longer exits with usage
  on the disc's own default entry.
- The disk layout names the BIOS boot partition by its GPT type GUID. The
  short `B` alias is a DOS type, and `sfdisk` refused to add the partition,
  so the install stopped at the disk step.
- The installer mounts the new root before creating `/mnt/boot`, so the
  ESP mount point is on the new root instead of being covered by it.
- The mount step lists `/mnt` and `/mnt/boot` with one `findmnt` call each.
  `findmnt` takes a single target, and passing both made the step exit
  without a message before the base system was installed.
- The disk checks no longer use process substitutions. A live root without
  `/dev/fd` leaves such a loop with no input, so a partition already
  mounted or in use as swap was not refused.
- The chroot installs the icons from `assets/icons/shell` and
  `assets/icons/apps`, where the tree keeps them. It looked in
  `src/compositor/icons`, so it fell back to fetching a stale list from
  the host and stopped at the payload step on a machine with no host
  server.
- The live root mounts `/etc/pacman.d/gnupg` on a tmpfs and populates the
  keyring at boot, before the `tty1` autologin starts the installer. The
  squashfs is read-only and carries no keyring, so the installer's first
  signed `pacman -Sy` used to fail.
- The installer no longer answers a broken keyring by writing
  `SigLevel = Never`. It initializes an empty keyring when it can, and
  stops with a message instead of installing unverified packages.

### The installed app list matches the catalogue

- An installed machine marks Settings, Files and Store as installed: the
  three catalogue entries whose launchers the image actually ships.

### The installed machine does not call itself a live medium

- The login issue and motd on an installed system now come from
  `base/installed`, not the install medium's `base/airootfs`. The old
  files told the user to run `finn-install` on the machine they had just
  installed.

### The installed disk is booted, not just written

- An install driven over a serial console keeps that console: the installed
  GRUB and systemd-boot entries carry `console=ttyS0`, and the serial getty
  is enabled. An install from a screen gets no serial console.
- The opt-in full-install CI job now detaches the ISO, boots the disk the
  installer wrote, and waits for the installed system's login prompt. A
  disk that installs but does not boot fails the run instead of passing on
  the installer's own checks.

### Host-side tools follow the icon layout

- `vgtest` takes `assets/icons` and resolves both `shell/` and `apps/`, so
  the sheet covers the catalogue's icons and no longer looks for
  `settings-app.svg` under `shell/`. `docs/development.md` names the same
  path.

### The ISO is FinnOS, built from this tree

- There is no downloaded base image and no remaster path. `vm/build-iso.sh`
  is gone; `base/mkfinniso` natively (or CI, which runs the same native
  build in a privileged Arch container) is the only way to write the
  medium, from the profile, the package list, the live root and the boot
  menus in `base/`.
- CI verifies the native disc: the `/finn` live root, the FinnOS syslinux
  and systemd-boot menus, the payload, and the boot-to-installer QEMU run.
- The VMware dev-VM script takes the one FinnOS disc; the stock-Arch
  two-disc fallback is gone.

### Wayland client frame copies

- Reuse each external window's owned pixel allocation when its buffer dimensions stay the same, instead of allocating and freeing the full SHM surface on every commit.
- Copy opaque, unscaled SHM content through row-sized `memcpy` spans, retaining the rounded lower corners and clipping at screen edges. Alpha surfaces and resized/scaled content keep the general blend path.

### x86-64 runtime coverage

- Both architecture manifests now include `wpa_supplicant` for NetworkManager Wi-Fi; the installer fallback package set stays consistent.

### Shared theme tokens stay in step

- The compositor's shared palette now follows the checked-in light/dark tokens, including the active tile and dark accent values.
- Frosted panel and tile passes now match the theme's 0.65 panel weight and 0.85 effective tile density; a regression test checks both the values and the compounding math.
- Figma's Window and Home effects are now measured and rendered: a black 3% shadow at (0, 4), blur 100, plus the 15-unit backdrop blur. Home's material now matches its measured 65% fill instead of 84%; the shadow remains scoped to those surfaces, not treated as a global elevation token.
### Multi-catalog App Store registry

- **Flatpak and Windows catalogue entries.** The registry carries entries
  whose `source` is `flatpak` or `winget`, including x86_64 and ARM64
  Flatpak entries; the Store's Linux and Windows pages render them, with
  live install state from the control plane.
- **No browser placeholder.** The registry no longer lists Finn Browser:
  there is no browser view and no `finn-browser` launcher, so the entry
  could only ever be shown as unavailable. It joins the catalogue when its
  view is real.

### Finn Store, and a package manager with one owner

- **A store that lists what the OS actually has.** Finn Store reads the
  same registry as the Start Menu. Installed is finnpkg's answer, re-read
  after every action; an entry whose launcher is missing is shown as
  unavailable instead of offering an install that would fail.
- **Installing goes through the control plane.** `app.install` and
  `app.remove` validate the id, confirm the catalogue knows it, and run
  `finnpkg`; the Store never edits the install list. finnpkg's refusal
  is passed back in its own words rather than replaced by a generic
  failure.
- **Removing asks twice.** The first press turns the button into
  "Remove?"; the second performs it. Installing is one press, because it
  changes only the install list.
- **The catalogue ships with the system.** This build does not download
  native packages, and the Store says so rather than drawing a progress bar for
  a network it does not have.
- **Flatpak package management and launch go through the audit log.** `finnd`
  exposes system list/search, queues install/remove transactions as systemd
  jobs, verifies installed state and starts installed apps as Finn in the
  Wayland session. Finn Store passes each search row's remote through the
  validated install job; Library entries offer Open. `finnpkg flatpak run`
  now delegates through the same audited verb instead of refusing launch or
  starting a process itself. The x86_64 base manifest includes Flatpak and
  setup attempts to configure canonical system Flathub; the aarch64 manifest
  does not include Flatpak.
- **A limited in-process Wayland client host joins the window stack.**
  `finn-comp` implements xdg-shell toplevels backed by `wl_shm`, with
  compositor-managed configure, focus, pointer, keyboard, frame and close
  routing. Current sources build on an ARM Linux VM. An installed GNOME
  Calculator launched through the Flatpak control path rendered a 410×666
  surface; after activation and frame callbacks, pointer and keyboard input
  changed its display to `78`. This used a standalone test server, not a booted
  FinnOS desktop. Popups, subsurfaces, dmabuf, touch, scaling, clipboard and
  drag-and-drop remain unsupported; x86_64 hardware and real Wine GUI behavior
  are not verified.
- **Three source filters.** FinnOS Native, Flatpak Apps and Windows Apps are
  selectable in the Store. The Windows Apps Library lists validated Wine
  registrations and opens them through `finnd wine.run`. `finnpkg wine install`
  now creates a private prefix and starts a chosen installer through finnd;
  `wine set` selects the installed executable for later launches. Setup and
  executable selection remain CLI steps, and real Wine GUI validation is still
  outstanding on FinnOS.
- **Launching a native app does not invoke a shell.** `finnd` passes the
  catalogue's executable path to `systemd-run` as one argument. A regression
  test verifies shell metacharacters remain inert, while preserving the
  transient, user-owned systemd unit.


### Finn Store, Microsoft-Store-style redesign

- **Nav rail, Home, Apps and Library.** The Store now opens on a navigation
  rail with Home, Apps and Library destinations, matching the shape a
  person already knows from the Microsoft Store, while keeping FinnOS
  material, type and colour.
- **Detail page and icons.** Selecting a catalogue entry opens a full
  product page; app icons are loaded from the shared icon set so the
  Store shows the same glyphs as the Start Menu.

### Finn Files, reading the machine as the person at it

- **A real browser of the real filesystem.** Finn Files opens on the
  desktop user's home, lists folders and files, sorts folders first, and
  navigates by click, double-click, keyboard, the up button or the places
  sidebar. It hides dotfiles and caps a listing at 512 entries, saying so
  when it does.
- **It reads as the user, not as the compositor.** The compositor is root
  because it owns DRM. Every directory read in Files drops the filesystem
  uid to the session user for the duration, so the kernel's permissions
  decide what can be seen: `/etc/shadow` is not a preview.
- **Text previews, honestly bounded.** Selecting a file shows its name,
  size, date and the first screenful of text. A binary or oversized file
  says there is no text preview instead of drawing noise. The preview does
  not claim to render images.
- **Read-only, and it says so.** The compositor's home mount is read-only,
  so the footer says "Read-only" rather than offering a delete that would
  fail. Copy, move and rename are not implemented.
- **The wheel works.** `finn_pointer` already collected wheel notches;
  they now go to the window under the pointer, and `pointer.scroll` queues
  one for a test. The Files list scrolls, and the control plane can open a
  folder with `files.open`, read the view with `files.get`, and select an
  entry with `files.select`.

### Windows that can be arranged, and a Meta key with two jobs

- **A window resizes from any edge or corner.** The border under the pointer
  is the handle; a drag applies every move to the edge the gesture started
  on, clamps at a minimum that still fits the titlebar controls, and stops at
  the dock. There is no separate grip drawn on the glass.
- **Dragging a window to the edge snaps it.** The top edge maximises, the
  left and right halves tile, and the corners tile a quarter. The commit
  happens on release, so the window stays under the pointer while it is
  carried; the same edge twice, or `window.restore`, puts back the floating
  rect it came from.
- **The titlebar is a gesture.** A second click within 400 ms toggles
  maximize and back. A snapped window is not un-snapped by a click that
  never moves.
- **Super+arrow tiles the focused window.** Left and right are halves,
  Shift+Left and Shift+Right the top quarters, Up maximises and Down
  restores. Meta therefore opens the Start Menu on *release*: a press that
  is followed by another key is a modifier, not a menu key.
- **The control plane can arrange windows too.** `window.resize`,
  `window.snap`, `window.restore` and `window.get` join the existing window
  verbs, and `window.list` reports a snapped window as `snapped` rather than
  as a normal one.

### Settings that are the Figma frame, and glass that knows what is behind it

- **Every surface frosts the desktop, not the wallpaper.** `finnblur` now
  keeps a live, blurred copy of what the compositor has actually drawn —
  windows included — and every frosted rect samples it. A window opened over
  another window shows the one underneath through its glass; the Start Menu
  over a window shows that window; the dock shows whatever it is docked over.
  The blur is built at 1/8 scale with three 2px box passes and is rebuilt only
  when the desktop under it changes, so an idle shell still costs nothing.
- **Settings is drawn in the frame's own units.** The app's layout is the
  900x500 frame, and `ui_scale()` maps one design unit onto the compositor's
  pixels at a fixed ratio, so a 250px sidebar is 28% of the window and a 13px
  label is a 13px label. Previously the sidebar took 34% of the window while
  its contents were drawn at 78% — some things too big, some too small, and a
  fifth of the content column unused.
- **A maximised window gets more room, not larger text.** The scale no longer
  follows the window's current width: maximising keeps every size and lets
  the content column grow to 780 design units and stop, left-aligned under the
  sidebar, instead of zooming the whole page off the screen.
- **The clock pill is only as wide as the clock.** It is authored 139 wide to
  hold time and weather; on a machine with no weather source that second half
  was empty air beside the time. The pill now measures its own text when
  `read_weather()` finds nothing, and goes back to 139 the moment a source
  appears — the width is part of the dock's build key.
- **The titles came down and up a size.** "Settings" and the page's own title
  share one baseline at 60, a size above the frame's 16, and the rows sit on a
  36px pitch instead of 40 — the frame's airiness at the frame's width, not at
  a window's worth of empty space. Card heights follow their rows.
- **One sheet, three depths.** The sidebar is the body glass laid down at 92%,
  a card is the body at 72% with a hairline, and the page behind them is the
  window's own 62%. The sidebar no longer paints itself opaque white over
  glass, which is what made the window read as two windows.
- **Cards have a rhythm.** Every page is a column of cards on the same grid:
  a 10px caption at 22, rows 38 tall with their value flush right, a hairline
  inset by the card's padding, and a quiet well for the sentence a page has
  instead when there is nothing to list. Wi-Fi and Bluetooth gained an
  interface/controller card that reports the device the compositor actually
  found, and About's system card uses the same rows.
- **The sidebar is the frame's sidebar.** 250 wide, the 17px title at 15,42,
  a 30px search field with the magnifier at its left, 18px circular accent
  chips, 32px rows on a 40px pitch, and group captions 18 above their first
  row. Wider than the frame's own spacing, because the frame is drawn at 1280
  and the window renders at 0.9 of it.
- **Focus is visible.** The search field keeps the accent ring while it holds
  the caret, so a click that lands in it says so.
- **The Start Menu is a sheet of paper.** Its panel is 84% rather than the
  shell's 60%: over a window, 60% let the window's labels read through the
  menu. The blur still comes through; the text does not.
- **Finn Files uses the asset that was drawn for it.** `assets/icons/apps/files.svg`
  is the file manager's icon in the dock and the menu, and every icon the
  shell draws now lives under `assets/icons/` — `shell/` for the chrome,
  `apps/` for applications — installed flat into `/usr/share/finn/icons`.

### A window that closes is a window that dissolves

- **Close no longer animates an empty layer.** The 180ms close track dissolved
  the body layer, but the window was taken out of the array before the first
  frame of it: the window vanished at once and the track spent itself on glass
  that no longer held it. The last visible window now keeps its place until the
  track lands, so the scatter, its warm embers and the 92% settle are seen.
  A window brought back mid-close (focus, or its own app opened again) stays.
- **The Window Options pill is the design's pill.** 72x24 at a 10px inset with
  15.43px boxes at 12, 36 and 60, at 50% white over the body's 70% — it had been
  thinned to 20px and carried the shared 85% tile weight, which composited to a
  near-white chip. The three controls keep Figma's own navy (#141B34) instead of
  the neutral label ink, and are placed by their 15.43px box rather than by their
  ink, so the row is spaced evenly.
- **Settings' magnifier is the glyph, not its frame.** Figma's 18px frame holds a
  13.5px drawing; blitting 18 drew the frame, a third larger and heavier than the
  field's design.

### Pixel-accurate Start Menu and Control Center

- **Start Menu (Figma 4:172 / 8:20).** 800x500 logical, r=8 frosted glass at 65%
  white, anchored 15px above the dock and horizontally centered on screen:
  - Scrim removed: the desktop behind the open menu remains bright and clean.
  - 385x480 r=4.14 Right Panel at (405, 10) in 85% white frosted glass.
  - 385x40 r=4.14 Account Options bar at (10, 450) displaying 30x30 avatar,
    real session user name capitalized dynamically from session state / passwd,
    and interactive Restart and Shutdown action buttons.
  - "View all" pill (88x26) with vector `chevron-right.svg` glyph.
  - Single application row for Settings with 26x26 app icon, 14px label gap,
    and active hover/press washes that launches the Settings desktop app.
- **Control Center (Figma 9:63 / 9:82).** 405x500 logical, r=8 frosted glass at 65%
  white, anchored 15px above dock and centered over Pill 4 (Status tray):
  - Inset 188x40 Wi-Fi and Bluetooth status tiles with honest state reporting
    ("Not available" with muted status disc when hardware is absent; inert when absent).
  - Knob-less filled pill sliders (385x35) for Brightness and Volume.
  - Volume slider is interactive and draggable, updating real system volume via ALSA.
  - Brightness slider honestly reports "Not available" with crisp inert styling when
    backlight hardware is absent.
  - Footer bar (385x40 at y=450) with battery percentage / Mains indicator,
    disabled `edit-01.svg` glyph, and interactive `settings-02.svg` button that opens
    the Settings desktop app.

### Pixel-perfect window chrome and Settings desktop application

- **Pixel-perfect window chrome (Figma 14:247/14:236).** 500x300 r=8 frosted
  glass window with top-right 72x24 r=4.14 Window Options pill (10px inset)
  carrying 15.43px expand, minus-sign, and close vector glyphs, and centered
  12px Fixel Display Regular "Empty window" label at 70% opacity.
- **Settings desktop application (Figma 14:330/14:289).** A 900x500 window
  centered on the desktop, draggable by its titlebar and maximizable /
  minimizable / closable. Features a 250px shared-glass sidebar with r=8 outer
  corners, "Settings" title at (15, 44), interactive search field at (15, 70)
  with active focus ring, blinking caret, backspace filtering, and the Figma
  section headers (NETWORK, PERSONALIZATION, SYSTEM).
- **The five visible Settings pages follow the Figma frame:**
  - *Wi-Fi:* hardware presence, connection state, and functional toggle.
  - *Bluetooth:* controller status, honest absent state, and toggle.
  - *Network:* live interface link state from sysfs; configuration remains read-only.
  - *Personalization:* display scale and Light/Dark appearance toggle persisted
    to `/var/lib/finn/settings`.
  - *System Information:* kernel release, device name, system uptime, total RAM,
    root-storage and CPU details; GPU telemetry is unavailable.
  - Legacy Display, Sound and Power pages remain searchable and control-addressable.
- **Control protocol navigation.** Added `settings.page name=<page>` and
  `settings.get` to `finn-comp`, proxied by `finnd`, with CLI support in
  `finnctl` and SDK client methods.

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
