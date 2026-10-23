# A working-OS review

Reviewed at `1fab5c4`, October 2026 (historical point-in-time audit of the
pre-hardening tree; see `CHANGELOG.md` for what has been resolved since).
This is a review of the system as it was at `1fab5c4`, not a wish list:
every finding below was read in the code at the cited path. The suite at
`1fab5c4` passed (174 tests) and the style check was clean; those
tests covered the compiler, the SDK and the adapter, not the compositor,
the control plane, input or the installer.

## The verdict

FinnOS is a strong shell prototype with a real compositor, a real control
protocol and an unusually honest set of documents. It is not yet an
operating system you can live in, because three things are missing at the
foundation:

1. **No application runtime.** Nothing can draw into a window. The only
   app in the catalogue opens an empty frosted rectangle.
2. **No keyboard.** The compositor reads a pointer and nothing else. No
   password, no search, no terminal, no text.
3. **No durable session.** First boot never runs, settings are scattered
   across `/run` files, and a failed compositor has no recovery path.

On top of that there are concrete defects that make an installed machine
fragile or insecure: a second unauthenticated control socket, default
credentials, an installer that can format the wrong disk, and a display
path that freezes silently.

The good news is that the hard part — owning DRM/KMS, page flips, the
shell, a GPU path and a software path in one process — is already built
and works. The next phase is not a rewrite. It is finishing the system
around the compositor.

## Scorecard

| Area | State | Why |
|---|---|---|
| Display and compositing | **good** | Real DRM/KMS, page flips, GPU and software backends, cached layers |
| Shell and visual language | **good** | Dock, Control Center, clock page, Start Menu and windows share one design system |
| Control plane | **partial** | Unix-only and peer-authenticated, but blocking, single-client and bypassable through `comp.sock` |
| Input | **poor** | Pointer only; no keyboard, no hotplug, no focus |
| Application runtime | **missing** | No surface protocol, no `.fbin` renderer, one empty window |
| First boot and settings | **broken** | Setup is skipped by default; settings are ad-hoc files |
| Boot and recovery | **fragile** | A conditional unit can be skipped forever; no rescue console |
| Security | **poor for a shipped image** | Default passwords, unauthenticated second door, shell injection in the adapter |
| Installer and update | **dangerous / missing** | Hardcoded disk, empty root UUID fallback, no OTA |
| Tests and CI | **partial** | Python suite is good; C, input, DRM and install are untested |

## What already holds up

These are worth protecting while the rest changes.

- **The compositor is real.** It opens DRM/KMS, picks a mode, owns the
  scanout buffer and page-flips. The GPU path (GBM/EGL/GLES) and the
  software path compose the same surfaces, and the backend is probed
  rather than assumed (`src/compositor/finn-comp.c:1250-1266`).
- **Frames are demand-driven.** Cached layers, build keys and an idle
  loop that blocks on input are the right architecture. A surface costs a
  blit, not a socket round trip.
- **The control protocol is one small, line-oriented wire.** `finnd`
  authenticates with `SO_PEERCRED` and has no TCP listener
  (`src/control/finnd.c:64-70, 390-403`). That is the correct posture.
- **The registry split is honest.** The OS catalogue and the package
  manager's install list are separate, and `app.launch` refuses unknown,
  uninstalled and non-runnable apps with distinct messages
  (`src/core/finnapps.c:41-59`, `src/control/finnd.c:298-309`).
- **Absent hardware is mostly handled correctly.** Battery, Wi-Fi,
  Bluetooth and brightness report `-1` and the shell says "Mains", "No
  adapter" or "n/a" instead of inventing a value.
- **The documentation says what is missing.** `docs/architecture.md`'s
  "What is not here yet" is more honest than most shipping projects.
- **The animation and caching work is sound.** The tracks are
  demand-driven, the window body key deliberately excludes pointer state,
  and the overlay layer keeps hover from repainting the glass.

## P0 — what blocks "a working OS"

### P0.1 There is no application runtime

**Evidence.** The entire catalogue is one line
(`assets/apps.registry:19`): `window | Finn Window | ...`. A window is a
geometry and a title (`src/compositor/finnwindow.c:44-53`); there is no
client buffer, no damage region and no surface socket. `finnwindow_render`
draws one centred label and never consults `app_id`
(`src/compositor/finnwindow.c:866-878`). The only app binary,
`finn-window`, asks the control plane for the same empty window
(`src/control/finn-window.c:13-19`). No `.fbin` reader exists outside the
compiler tests.

**Failure.** Press a Start Menu tile: a glass rectangle appears, says
"Empty window", and the launch starts a program that asks for another
empty rectangle. There is no terminal, editor, files, settings or browser.
The machine cannot be used to do a task.

**Direction.** Build the surface protocol the roadmap already describes:
a second unix socket for binary surface streams (shared memory or dmabuf),
`surface.create/attach/damage/commit/destroy`, a surface list with
z-order and per-surface damage, and paste in both backends. Make `.fbin`
the client scene graph, then ship a terminal and settings app as real
clients. This is the single highest-leverage change in the tree.

### P0.2 There is no keyboard

**Evidence.** `src/compositor/finnpointer.c:51-84` only probes devices
with `REL_X/REL_Y` or `ABS_X/ABS_Y`; `:186-196` handles only the mouse
buttons and `BTN_TOUCH`. There is no keymap, modifier state or focus id,
and `key.*` verbs are reserved but absent.

**Failure.** No Wi-Fi password, no search, no filename, no terminal, no
name in setup, no keyboard navigation. The system is pointer-only in a
world where nearly every task needs text.

**Direction.** Add a `finnkbd` evdev reader (or extend the pointer
reader), probe `EVIOCGBIT(EV_KEY)`, carry a keymap (xkbcommon or a small
US table plus a `keymap` setting), track modifiers and locks, add a focus
id to the compositor, and deliver keys to the focused surface. Add
`Ctrl+Alt+F1..F6` VT handling in the same change, because it is also the
recovery path (P0.4/P0.8).

### P0.3 First boot never runs, and completion is not durable

**Evidence.** When `/var/lib/finn/setup-done` is absent — every fresh
install — `finnsetup_init` sets `g_completed = 1; g_open = 0`
(`src/compositor/finnsetup.c:225-230`). The only way in is the
undocumented `FINN_SETUP_FORCE=1`. `setup.get` then reports
`active=0 done=1` on a machine that has never run setup
(`src/compositor/finn-comp.c:582-589`). `finnsetup_complete` writes the
marker with `fopen("w")`, ignores the result, and sets `g_completed = 1`
before the write (`src/compositor/finnsetup.c:270-279`).

**Failure.** A new install boots straight to the desktop with no
language, appearance or account step. Even when setup is forced, the
language choice changes only the pill's own text; nothing localises the
shell, and `finnsetup_init` never reads the `lang=` line back.

**Direction.** Missing marker means open, not done. Gate the session on
setup. Write the marker atomically (`O_CREAT|O_EXCL`, `fsync`, `rename`)
and do not report complete if the write failed. Apply the language to a
real locale/keymap, or label the control as a display-name-only choice
until it does. Keep a positive `FINN_SETUP_SKIP=1` for development, and
document it.

### P0.4 Boot can silently skip the compositor, and there is no rescue

**Evidence.** `base/finn-comp.service:5-8` is ordered after the
deprecated `systemd-udev-settle.service` and gated on
`ConditionPathExists=/dev/dri/card0`. A failed condition is not restarted
by `Restart=always`. The binary also hardcodes `card0`
(`src/compositor/finn-comp.c:1223`). `base/setup.sh:137-142` installs the
units but does not enable them and never sets the default target. The
chroot installer enables them but also does not set
`graphical.target` (`vm/finn-chroot.sh:108`). `finn-comp` conflicts with
`getty@tty1` (`base/finn-comp.service:7`) and there is no `getty@tty2`.

**Failure.** If `card0` is not present at the instant the condition is
evaluated — or the GPU is `card1` — the compositor is skipped and the
machine lands on a text console permanently. If it crash-loops, the
console is reset every second and there is no alternate VT. A machine
installed with `base/setup.sh` has no desktop at all until someone
enables it by hand.

**Direction.** Use device units (`Wants/After=dev-dri-card0.device`) or a
udev rule with `SYSTEMD_WANTS`, enumerate `/dev/dri/card*`, add a
`finn-session.target`, set `graphical.target`, and keep `getty@tty2` as a
rescue console. Change `Restart=always` to `on-failure` with a start
limit and an `OnFailure=` unit that says what happened.

### P0.5 A display failure freezes the frame silently

**Evidence.** On `drmModePageFlip` failure the GPU path logs and sets
`g_flip_done = 1` without updating `cur`
(`src/compositor/finn-comp.c:1684-1691`); the software path does the same
with no log at all (`:1718-1722`). `wait_flip` gives up after 100 ms
(`:203-212`). `finngpu_release` frees the old buffer regardless of
whether the flip landed (`src/compositor/finngpu.c:512-515`).

**Failure.** One failed flip or a lost GBM buffer freezes the last image.
The process is alive, systemd sees success, input does nothing, and a
screenshot returns the frozen frame. There is no fallback and no exit.

**Direction.** Count consecutive flip and lock failures, log the first
with `errno`, try a full `drmModeSetCrtc`, fall back to the software path,
and exit non-zero after a bound so systemd can restart. Only release a
buffer after its flip completed. Add a watchdog heartbeat.

### P0.6 Security: the "one door" is not the only door

**Evidence.**

- `finn-comp` listens on `/run/finn/comp.sock` with mode `0660` and the
  service runs `Group=finn` (`src/compositor/finn-comp.c:1410`,
  `base/finn-comp.service:18-19`). The dispatcher never calls
  `SO_PEERCRED` and logs nothing. Any process running as the desktop user
  can move the pointer, click, capture the screen, flip radios, change
  state and open windows without `finnd` seeing it.
- `finnctl --direct` and the automatic fallback use that path
  (`src/control/finnctl.c:73, 102-105`; `sdk/finnsdk/client.py:114-126`).
  Killing `finnd` leaves a stale socket, and the fallback takes over
  automatically (the CLI prints a note on stderr; the SDK does not).
- The installer sets `root:finn`, `finn:finn`, passwordless sudo and
  enables sshd (`vm/finn-chroot.sh:43-54`). On any network that is remote
  root after one guess.
- The MCP adapter interpolates free-form `title`, `id` and `button` into
  an `ssh` command (`mcp/finn-mcp:66, 323, 348`). OpenSSH joins those
  arguments with spaces and hands the result to the remote login shell, so
  `title="x; reboot"` runs on the device. It also disables host-key
  verification (`mcp/finn-mcp:47-54`).
- `finnpkg install` takes the package `id` from the tarball and uses it
  in `mkdir -p "$APPS_DIR/$id"` and `rm -rf "$APPS_DIR/$id"`
  (`src/pkg/finnpkg:172-188`); `id=../../etc/...` writes as root. It also
  ignores the manifest's command, so an "installed" app cannot be
  launched.
- `FINN_CONTROL_ALLOW_ANY` is tested with `getenv`, so `=0` also opens
  the plane to every uid (`src/control/finnd.c:395`).
- `power` defaults to reboot on any unknown word, writes `OK` before
  acting and ignores the result (`src/control/finnd.c:257-267`). A typo
  reboots; `power` with no argument reboots; a refused `systemctl`
  reports success.

**Direction.** Make `comp.sock` root-only (`0600`) and put every changing
verb behind `finnd`, or add the same `SO_PEERCRED` check and a shared
audit log. Delete the automatic fallback and keep `--direct` behind a
development flag. Remove default passwords and passwordless sudo from the
shipped image; make setup create the account. Quote or, better, stop
using the shell in the MCP transport; pin the host key. Validate the
package id and archive paths, verify a signature or hash, and refuse to
extract as root without it. Test the allow-any value for `"1"` exactly.
Validate the power word and report after the command.

### P0.7 The installer can wipe the wrong disk or produce an unbootable one

**Evidence.** `vm/finn-install.sh:9` hardcodes `DISK=/dev/sda` and
`wipefs -a "$DISK"` (`:62`) with no argument, confirmation or check that
`/dev/sda` is not the live medium. The script uses `set -x` without `-e`
(`:8`), so a failed `sfdisk` still proceeds to `mkfs`. Inside the chroot,
`ROOTUUID=$(blkid -s UUID -o value /mnt)` reads the *new root's empty
directory*, not the mounted target (`vm/finn-chroot.sh:158`), and the
systemd-boot fallback writes `root=UUID=` (`:169`). BIOS boot code is
installed to the hardcoded `/dev/sda` even for NVMe (`:176`). The installer
prints `FINN_INSTALL_COMPLETE` even when the shell build failed
(`vm/finn-install.sh:150-158`, `vm/finn-chroot.sh:135-151`).

**Failure.** An NVMe machine gets no BIOS boot code; a multi-disk machine
may lose the wrong disk; a failed partition step formats whatever exists;
the fallback boot entry is unbootable. The machine still says it
installed successfully.

**Direction.** Require `--disk`, refuse the live medium, require typed
confirmation, use `set -euo pipefail`, check every step, compute the root
UUID with `findmnt -no UUID /` before chroot and pass it in, derive the
BIOS disk from the target, and fail if `finn-comp` is not installed and
runnable. Add a CI job that installs into a VM disk and boots it.

### P0.8 There is no way out of a broken session

**Evidence.** `finn-comp` conflicts with `getty@tty1`
(`base/finn-comp.service:7`) and the image only configures `tty1`.
There is no VT handling, no DPMS, no lid/suspend, no lock, no
`logind` integration (`grep` for `logind`/`suspend`/`DPMS`/`lock` across
`src/`, `base/` and `vm/` finds nothing). The service is `Restart=always`
with a one-second delay.

**Failure.** A hung compositor resets the console every second and there
is no keyboard shortcut to another VT. On a laptop, closing the lid does
nothing.

**Direction.** Keep `tty2` free, handle VT leave/enter and DRM master,
add a rescue target or `OnFailure`, and implement lid/suspend/blanking
through logind.

## P1 — what daily use needs

### P1.1 The frame loop blocks on control calls and capture

`menu_launch` calls `ctl_ask` synchronously before opening the window
(`src/compositor/finn-comp.c:986-1003`); power does the same
(`:1007-1015`). `screen.capture` writes a multi-megabyte PPM inline from
the frame loop (`:1695-1699`), and `finn_write_all` busy-waits on
`EAGAIN` (`src/core/finnsock.c:96-109`). A slow capture client stalls the
desktop; a 2-second control reply freezes the shell.

Make control calls asynchronous: queue the request, poll the reply fd,
and keep painting with a pending state. Make capture a registered
`POLLOUT` stream with a cursor and a disconnect check.

### P1.2 `finnd` is one client, blocking, and logs failures as `OK`

`serve()` is called synchronously in the accept loop
(`src/control/finnd.c:390-405`), so one in-flight request blocks every
other client. `relay()` reads a binary payload on a blocking provider
socket with no timeout and trusts `atol` (`:73-97`). Proxied requests are
logged as `"OK"` whenever any reply line comes back, even if it was
`ERR ...` (`:326-328`). A provider that dies mid-payload hangs the whole
control plane.

Multiplex connections with `poll` (or one thread per connection with a
deadline), bound the relay by length and time, and log the actual reply
status.

### P1.3 The line reader dispatches truncated lines

`finn_read_line` returns whatever arrived on `EAGAIN` as a complete line,
and an empty `EAGAIN` ends the session
(`src/core/finnsock.c:117-135`). Accepted sockets are non-blocking
(`:43-50`). A request split across packets is executed twice: `state.set
bat` then the rest as a new verb. Because the same `-1` means "nothing
pending", the daemon usually closes after one reply, so the documented
"many requests per connection" is false.

Keep a per-connection buffer, dispatch only on `\n`, read in chunks, cap
the line with an explicit error, and treat "nothing pending" as wait, not
end.

### P1.4 State is scanned every frame, and the volume is a lie

`finn_state_read` runs on every loop iteration
(`src/compositor/finn-comp.c:1569`) and scans power supply, wireless,
network, backlight and opens/closes the ALSA mixer each time
(`src/core/finnstate.c:277-283`, `src/core/finnaudio.c:53-71`). The
volume override is read before the mixer and is written by every
`sound.set`, so after one slider touch the mixer is never consulted
again. With no mixer and no persisted value, `read_volume` returns `50`
(`src/core/finnstate.c:254-263`) — the one field that breaks the absent
rule. `sound.set` ignores `finn_audio_set` and returns success
(`:328-339`); `radio.set` ignores the rfkill result
(`src/compositor/finn-comp.c:470-476`); `display.get` can report a stale
override after `display.set`.

Cache state with a TTL and invalidate on udev/netlink. Make the mixer the
source of truth, return `-1` when there is no mixer, and make
`sound.set`/`radio.set` report the actuator's failure exactly as
`display.set` already does. Do not write the `/run` override from the
UI.

### P1.5 The input layer is pointer-only and not hot-plug safe

Devices are enumerated only at startup
(`src/compositor/finnpointer.c:88-101`); `SYN_DROPPED` is not handled
(`:186-196`); there is a four-device cap (`finnpointer.h:24`); and
`pointer.scroll` replies `OK` while doing nothing
(`src/compositor/finn-comp.c:529-533`).

Add an inotify/udev monitor, handle `SYN_DROPPED` by re-reading key and
absolute state, grow or allocate the device list, and either route scroll
or answer `ERR`. The keyboard work in P0.2 belongs in the same module.

### P1.6 Windows act on press, and hit-testing ignores the animation

`finn-comp.c:1143-1150` calls maximize/minimize/close on button-down, so
the press wash is never seen, dragging off cannot cancel, and Close is
irreversible on a stray down. The Control Center row does the same
(`:1095-1102`). During the maximize zoom and the minimize fold, the drawn
rect is interpolated but `finnwindow_hit` tests the destination
(`src/compositor/finnwindow.c:529-533, 907-940`).

Record the press and act on release only when the release lands on the
same part. Compute one rect for drawing and hit-testing, or disable hits
for the duration of the zoom/fold as the minimize path already does.

### P1.7 Dragging rebuilds and re-uploads the whole window layer

`finnwindow_drag` sets `g_built = 0` and position is in the body key, so
every pointer packet re-rasterises the union layer and, on the GPU path,
re-uploads it (`src/compositor/finnwindow.c:607-627, 812-823`). The zoom
already shows the right shape: move the cached raster with a transform
and rebuild once on release. Also clamp the drag to the dock's resting
top, not `screen_h - margin`.

### P1.8 Settings are scattered and not atomic

Volume lives in `/var/lib/finn/volume`, setup in
`/var/lib/finn/setup-done`, appearance in `/etc/finn/appearance`, the
install list in `/var/lib/finnpkg/installed`, and overrides in
`/run/finn/state/*`. Every write is `fopen("w")` with no `fsync` or
rename (`src/core/finnstate.c:30-36`). A crash can leave an empty file
and the shell silently falls back to defaults.

Introduce one versioned settings store with atomic writes, defaults,
validation and a migration field. Have setup, appearance, volume and the
session read from it.

### P1.9 The software path can freeze the dock clock

The damage test consumes `finndock_clock_tick()` and proceeds, but the
software branch recomposes only when `schanged || shell_changed || anim`
(`src/compositor/finn-comp.c:1671-1672, 1701-1702`). When only the minute
changed, it memcpys the stale base and flips it, so the clock freezes
until something else damages the frame. The GPU path is unaffected.

Hoist the tick into a local and include it in the compose condition.

### P1.10 The docs have drifted from the code

- `docs/architecture.md:224-228` and `CHANGELOG.md:20-21` claim built-in
  views for Settings, Terminal, Notes, Files, Code, Store, Studio, Sync
  and Vault. There is one empty label.
- `docs/ui-language.md:93`, `docs/development.md:87`,
  `base/README.md:240` and `docs/roadmap.md:19-38` say there is no window
  manager. There is one; what is missing is the client protocol and the
  keyboard.
- `docs/architecture.md:208` calls the catalogue "the whole family"; it
  has one entry. The icons for the other eight exist but are unused.
- `docs/architecture.md:145` says setup opens on first boot; it does not.
- `src/pkg/README.md` and `docs/roadmap.md` disagree about whether
  `finnpkg` is a stub; install/list/remove exist, OTA does not.
- `base/boot/loader/loader.conf:5-7` says all menus have two entries; GRUB
  and syslinux have a third "speech" entry, and the image contains no
  screen reader.
- `docs/control-protocol.md:12` advertises `printf ... | finnctl`;
  `finnctl` never reads stdin.

Fix these in the same commit as the behaviour, or remove the claim. The
"what is not here yet" section is the model to follow.

### P1.11 Tests and CI do not exercise the product

The 174 tests cover the compiler, parser, theme, SDK and adapter against
a fake server. There are no C unit tests, no sanitizer job, no fuzzing,
no DRM/input tests, and the ISO job boots the live medium to a login
prompt without running the installer or checking that the desktop comes
up.

Add a `make test-c` with ASan/UBSan, unit tests for `finnsock` framing,
state, registry, SVG parsing and `window.*` bounds, a fuzz target for
`finn_read_line` and `fsvg_load`, and an install-and-boot VM job that
asserts `finn-comp` is active and the frame is not the boot screen.

## P2 — hardening and polish

- **Parser memory safety.** `finnvg` assigns `realloc` results directly
  and writes through them (`src/compositor/finnvg.c:41-60`), does not
  check `ftell` (`:617-624`), and can overflow `w * SS` before the
  allocation (`:500-508`). Check every allocation and size.
- **GPU table and fallback.** Sixteen texture slots for roughly
  seventeen live textures causes a full `glTexImage2D` cliff during
  theme fades (`src/compositor/finngpu.c:37, 287-300`). Size it or use an
  LRU. Handle `finngpu_readback` allocation failure instead of sending a
  stale frame (`:377-391`).
- **Display model.** One connector, `crtcs[0]`, density from height only
  (`src/compositor/finn-comp.c:1230-1244`). Add an output abstraction,
  hotplug, physical-DPI scaling and multiple CRTCs.
- **Window bounds.** `finnwindow_move` stores unvalidated coordinates and
  computes the union box in `int` (`src/compositor/finnwindow.c:420-428,
  767-775`), then allocates `lw * lh * 4` (`:819`). Clamp, use `int64_t`,
  and cap the layer.
- **Notification queue.** Eight are kept, four are drawn
  (`src/compositor/finnnotify.h:20`, `finnclock.c:388`), and "Clear all"
  discards the hidden ones.
- **Window limit.** The ninth window silently evicts the oldest
  (`finnwindow.c:285-290`). Refuse with an error instead.
- **One glass constant.** `0.70f` and `0.50f` are duplicated under four
  names in `finnwindow.c`, `finncontrol.c`, `finnclock.c` and
  `finnmenu.c`; the Start Menu keeps private motion numbers under a
  comment saying motion lives in `finnmotion.h`.
- **Accessibility.** Nothing implements reduced motion. The first-boot
  "accessibility" button toggles dark mode, and the boot menus advertise
  speech that the image does not contain (`base/boot/grub/grub.cfg:37-39`,
  `base/packages.*`).
- **Audit log.** Opened per call, mode 0644, unescaped peer text, no
  rotation, and `if (!f) return` executes privileged calls unlogged
  (`src/control/finnd.c:52-62`).
- **Service hardening.** `finn-comp.service` has no `NoNewPrivileges`,
  `Protect*`, `CapabilityBoundingSet`, `RestrictAddressFamilies` or
  `PrivateTmp`. `finnd.service` claims "socket-activated only" while
  binding its own socket and retaining `CAP_SETUID`.
- **Build and packaging.** No `-Werror`, FORTIFY, stack protector or
  sanitizer build. `make install` installs only the two binaries, not
  fonts, icons, registry or `finnpkg`. `finnos-compose`'s PKGBUILD claims
  to install the icons and registry and does not.

## Quick wins (an afternoon, low risk)

These are small, testable changes that remove lies or obvious bugs. They
are not a substitute for P0.

1. **Setup default.** Missing `setup-done` means open; keep a positive
   `FINN_SETUP_SKIP=1` for development. Write the marker atomically.
2. **Volume absent.** Return `-1` when there is no mixer; let the Control
   Center draw "n/a". Make `sound.set` return the mixer's result.
3. **Radio truth.** Return the rfkill result from `radio.set`; refuse when
   the adapter is absent.
4. **Power truth.** Validate the three words, error on anything else,
   report the command's result after it runs.
5. **Pointer scroll.** Return `ERR` instead of `OK` until it scrolls.
6. **Software clock.** Include the minute tick in the compose condition.
7. **Press on release.** Move window and Control Center actions to the
   release path.
8. **Hit-test during animation.** Disable hits while `g_zoom_active`, as
   the minimize path already does.
9. **Docs.** Remove the nine built-in views, the "no window manager"
   lines, the "whole family" catalogue claim, and the stdin example.
10. **MCP quoting.** Use `shlex.quote` for every remote argument and pin
    the host key. This is small and closes a root-equivalent hole.

## The order I would work in

**Milestone 0 — make the machine honest and safe (1 week).**
Quick wins 1-10, plus `comp.sock` root-only and the installer's disk/UUID/
`set -e` fixes. Add a test for `finn_read_line` framing and for the
`FINN_CONTROL_ALLOW_ANY` value. This is the week that turns "demo" into
"does not lie or lose the disk".

**Milestone 1 — a session you can configure (1-2 weeks).**
Keyboard and focus; setup gated on first boot with an account step; one
atomic settings store; `graphical.target`, a device unit and a rescue
VT. A fresh install can be completed with the keyboard, rebooted, and
remember its choices.

**Milestone 2 — an OS you can use (3-4 weeks).**
Surface protocol, damage tracking, a terminal client and a settings
client. Windows gain resize, focus, stacking and keyboard delivery.
Async control RPC and capture. This is the milestone that makes the
machine useful.

**Milestone 3 — a system you can trust (ongoing).**
Flip-failure recovery, a watchdog, hotplug, ASan/UBSan CI, fuzzing, an
install-and-boot VM job, signed packages, and only then the A/B OTA.
Keep the roadmap's "what is not here yet" in lockstep with every change.

## Definition of done for "a working OS"

A machine is working when all of these are true:

- A fresh install boots to setup; the setup can be completed with the
  keyboard alone; a reboot keeps the choices.
- The user can open a terminal, type a command and see its output, and
  open a second app and switch focus between them.
- Windows move, resize, maximize, minimize and close, and the pointer can
  reach every control.
- Unplugging and replugging the pointer or keyboard works; a dropped
  input event does not leave a button stuck.
- Killing the compositor either restarts it cleanly or leaves a usable
  console, and a failed flip does not freeze the screen forever.
- No process other than root can reach the pointer or the framebuffer
  without an audit entry; no default password exists on the image; no
  package can write outside its directory.
- The installer refuses the wrong disk and an unbootable image, and CI
  installs and boots the image on every change.

Until then, the honest description is the one the docs already use: a
compositor and a shell, with the rest on the roadmap. The fastest way to
change that is to stop adding surfaces and start building the two things
every surface is waiting for: a keyboard and a client surface protocol.

## How this was checked

- `make test`: 174 tests, OK.
- `make lint`: style clean.
- Source read at `1fab5c4` for every finding above.
- No boot or install was run for this review; the runtime claims
  (software clock, GPU/software ordering) are read from the code paths and
  are cheap to confirm on the VM with `FINN_GPU=0` and a one-minute idle.
