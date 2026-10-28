# Troubleshooting

When it does not come up, this page is the sequence to follow. It is ordered by
where you are standing: the machine will not boot, the desktop is missing, one
part of the desktop is wrong, or a command said no.

Two consoles make everything on this page possible:

| Console | How to reach it | What it is |
|---|---|---|
| `tty1` | the screen | the compositor's; there is no login here |
| `tty2` | `Ctrl+Alt+F2` | `getty@tty2`, the rescue console |
| serial | `console=ttyS0,115200` | the install medium and a serial install both mirror to it |

`Ctrl+Alt+F1` returns to the session: the compositor's evdev reader keeps
reading while the VT is switched away, so the chord works from `tty2` too.
`tty1` belongs to `finn-comp`, which is why a login prompt is on `tty2`.

## Start here: three commands

On the machine, at `tty2`:

```sh
systemctl status finn-comp finnd
sudo journalctl -b -u finn-comp -u finnd --no-pager | tail -80
sudo tail -20 /var/log/finn/control.log
```

`systemctl status` says whether the unit is running, failed or has hit its start
limit. The journal says why. The control log says what was asked of the control
plane and what it answered.

What each outcome means:

| `systemctl status` | Meaning | Go to |
|---|---|---|
| `finn-comp` active, screen black or frozen | it is running but not painting | [The frame is frozen](#the-frame-is-frozen) |
| `finn-comp` failed, `finn-failure` ran | it started and stopped | [The compositor will not start](#the-compositor-will-not-start) |
| `finn-comp` inactive (dead), nothing in the journal | it was never enabled, or its binary is missing | [The units are not installed](#the-units-are-not-installed) |
| `finnd` failed, `finn-comp` fine | the shell is up without its control plane | [The control plane is down](#the-control-plane-is-down) |
| both active, no shell surfaces | the compositor is up but the session did not start | [The session did not start](#the-session-did-not-start) |

## The machine does not boot

### It stops at the firmware

- **Secure Boot.** The installer writes an unsigned GRUB or systemd-boot; a
  machine with Secure Boot on refuses it. Turn it off in the firmware, or enrol
  your own key.
- **The boot entry is missing.** Press the firmware's one-time boot menu (F12
  on a Dell) and look for the disk. If the entry is gone, boot the install
  medium and reinstall the bootloader; see
  [install.md](install.md#the-installed-system-does-not-boot).
- **The disk order changed.** A machine that boots the USB stick again instead
  of its disk is a firmware setting, not a FinnOS problem.

### It boots but never reaches the desktop

The kernel and systemd are fine; the session is not. `Ctrl+Alt+F2` and read the
journal:

```sh
sudo journalctl -b --no-pager | tail -100
systemctl --failed
systemctl status finn-comp
```

The usual causes, in order:

1. **No DRM device.** A VM without 3D acceleration, or a GPU the kernel did not
   bind, leaves `/dev/dri` empty. `ls /dev/dri` confirms it. The compositor
   needs a card; there is no headless fallback.
2. **A missing dependency.** `ldd /usr/local/bin/finn-comp | grep 'not found'`
   names the library. The runtime package depends on the graphics, font and
   audio packages; a machine where they were removed will not start it.
3. **A missing font or asset.** Fixel Display lives in
   `/usr/share/fonts/OTF`. A binary installed without `make install` will not
   find it. Reinstall the package or the tree.
4. **A stale binary.** A build that failed halfway leaves the old binary in
   place. `vm/deploy.sh --no-restart` and then restart the units.

### The install medium does not boot

- The medium is written to a partition instead of the whole device. Write it
  again with `dd` to `/dev/sdX` (not `sdX1`). See [install.md](install.md).
- The firmware is in the wrong mode for the entry you chose. The disc carries
  BIOS (syslinux) and UEFI (systemd-boot); try the other entry.
- The stick is half-written. An interrupted `dd` is not resumable; copy the
  image again.
- The console is blank but the machine is alive: the disc mirrors to
  `ttyS0,115200`. With a serial cable you will see the boot.

## The compositor will not start

`finn-comp.service` has a start limit and `OnFailure=finn-failure.service`.
When it gives up, `finn-failure` prints one line on the console the compositor
was painting:

```
FinnOS: the compositor stopped; press Ctrl+Alt+F2 for a console
```

That line means the unit has stopped retrying. `Ctrl+Alt+F2`, then:

```sh
systemctl status finn-comp
sudo journalctl -u finn-comp -b --no-pager | tail -120
sudo systemctl reset-failed finn-comp
sudo systemctl start finn-comp
```

Read the first error, not the last: a crash loop often ends in "start request
repeated too quickly", which is the limit, not the cause.

| Message in the journal | Cause |
|---|---|
| `cannot open /dev/dri/card0` | no DRM device, or the user is not root |
| `cannot find a connected connector` | the panel is off, or the cable is in a port the driver did not bind |
| `gbm: failed to create a surface` | the GPU path did not come up; the software path should take over, and if it did not, the EGL setup is the problem |
| `font` / `FT_New_Face` | Fixel Display is missing from `/usr/share/fonts/OTF` |
| `cannot bind /run/finn/comp.sock` | another compositor is running, or `/run/finn` is not writable |
| `permission denied` on an input device | the process is not root, or the seat is held elsewhere |

A compositor that is running but has no input is a different problem; see
[Input](#input).

## The frame is frozen

The watchdog restarts a compositor that is alive but not painting. A frozen
frame with a live process therefore means one of:

1. **It is blocked on something.** `finnctl screen.shell` is answered by the
   compositor itself, through the control plane; if `screen.shell` answers, the
   compositor is alive even though the frame is not changing.
   `sudo strace -p $(pidof finn-comp) -f -e trace=ioctl,poll` shows where it
   waits.
2. **The display lost sync.** A modeset that failed leaves the last frame on
   the panel. Restart the unit: `sudo systemctl restart finn-comp`.
3. **The frame is correct and nothing changed.** The compositor redraws on
   change, not at a fixed frame rate. An untouched desktop is supposed to be
   still.

If the watchdog is not restarting it, check that the unit really has
`WatchdogSec=` and that the compositor is sending `WATCHDOG=1`:

```sh
systemctl show finn-comp -p WatchdogUSec
sudo journalctl -u finn-comp | grep -i watchdog
```

## The control plane is down

```sh
systemctl status finnd
sudo journalctl -u finnd -b --no-pager | tail -60
ls -l /run/finn/control.sock
```

| Symptom | Cause |
|---|---|
| `cannot bind /run/finn/control.sock` | a stale socket or a second `finnd`; `ss -xlp \| grep control` names the owner |
| `refusing … the audit log is not open` | `/var/log/finn` is not writable, or the log could not be opened; every verb except `ping` and `version` is refused on purpose |
| `permission denied` from `finnctl` | the caller is neither root nor the `finn` user |
| `connection refused` | `finnd` is not running |

`finnctl ping` and `finnctl version` are the two verbs that answer without an
audit log, because they are how you find out whether the daemon is there at
all. Everything else refusing is the intended behaviour, not a second bug.

Do not "fix" an audit failure by making the log world-writable. The daemon
opens it `0600`; a pre-existing `0644` log is repaired on the next start.

## The session did not start

If both units are active but the shell shows only a wallpaper:

```sh
sudo finnctl setup.get
sudo finnctl screen.shell
sudo finnctl app.list
```

- `setup.get` returning `active=1 done=0` means the first-boot screen is up.
  That is the session working. `FINN_SETUP_SKIP=1` is the development escape;
  `setup.complete` writes the marker.
- `screen.shell` reporting `menu=0 cc=0 clock=0` is a normal resting desktop.
  Open the Start Menu with `pointer.click` at the dock's launcher or with a
  `Super` tap.
- `app.list` showing an app as `available` means its id is not in
  `/var/lib/finnpkg/installed`. The package manager records an app there only
  after checking that its launcher is executable, so that is the registry
  telling the truth, not a broken tile.

## Input

### The pointer does not move

```sh
ls /dev/input/
sudo libinput list-devices | head -40
sudo journalctl -u finn-comp | grep -i pointer
```

`finnpointer` reads evdev directly. A pointer that does not move on a machine
whose `/dev/input/event*` exists is usually one of:

- the device is claimed by another process (X, a display manager, a VM tool);
- the compositor is running without root and cannot open the node;
- the device is a touchpad the compositor treats as a relative device and the
  pointer is at a screen edge.

`finnctl pointer.move x=100 y=100` bypasses the hardware entirely. If that
works, the compositor is fine and the device is the problem.

### The keyboard does not type

```sh
sudo journalctl -u finn-comp | grep -i kbd
sudo finnctl key.tap code=enter
```

`finnkbd` derives modifiers and locks from held keys, handles `SYN_DROPPED`,
and picks up keyboards plugged in after boot. If a keyboard stops after a
hotplug, look for `SYN_DROPPED` in the journal: the reader discards the
in-flight state and resynchronises.

`key.tap` injecting a key and the shell reacting proves the compositor's key
path. A key that does nothing at the shell but works through `key.tap` is a
device or permissions problem, not a shell problem.

### `Ctrl+Alt+F2` does nothing

The chord is handled by the compositor's evdev reader while `finn-comp` is
running; it switches VT itself and drops DRM master on the way out. Nothing in
the tree disables the console keyboard, so the kernel's own VT switching is
still there when the compositor has failed. Either way, what you reach is
`getty@tty2`; if the switch does nothing, the getty was never enabled. Check:

```sh
systemctl status getty@tty2
sudo systemctl enable --now getty@tty2
```

## Graphics

### It draws, but slowly

The compositor chooses the accelerated path when GBM/EGL can give it a
scanout-capable buffer and the software path otherwise. `screen.info` reports
the density it is using, not the backend. `journalctl -u finn-comp | grep -i
'gbm\|egl\|software'` says which path came up.

On an Intel machine the accelerated path is `i915` plus Mesa. If `vulkan-intel`
or `lib32-mesa` is missing, 32-bit applications and Wine degrade first; the
compositor itself keeps working.

### The colours or the blur are wrong

Both rendering paths must agree. A surface that looks right on one machine and
wrong on another is usually a change made in only one of them. This is a bug,
not a setting; capture the frame (`vm/shot.sh /tmp/frame.png`) and report which
machine and which backend.

### Multi-monitor

There is none. The compositor picks the first connected connector with a mode
and drives it. A second monitor stays dark. That is a documented limit, not a
configuration error; see [hardware.md](hardware.md).

## Audio

```sh
aplay -l                       # does ALSA see a card?
wpctl status                   # does PipeWire see a sink?
sudo finnctl sound.get         # what does the shell think?
```

`sound.get` returning `volume=-1` means there is no mixer: the shell says the
machine has no audio rather than showing a slider that pretends. `sound.set`
answers `ERR hardware unavailable` for the same reason.

Applications use PipeWire through `pipewire-pulse`; the shell's own mixer reads
ALSA directly. A machine where `aplay -l` lists a card but `wpctl status` lists
no sink has a PipeWire problem, not a FinnOS one:

```sh
systemctl --user status pipewire pipewire-pulse wireplumber
```

## Network

```sh
nmcli device status
nmcli connection show
ip addr
```

`NetworkManager` owns connections. A machine with no wireless adapter reports
`wifi=-1`; the Control Center and Settings show "Not available" rather than a
dead toggle. That is the truth, not a failure.

If `nmcli` says the device is unmanaged, the interface is not claimed by
NetworkManager: check `/etc/NetworkManager/NetworkManager.conf` and the unit.

`sshd` is installed and **disabled** on an installed machine. Enabling it is a
decision:

```sh
sudo systemctl enable --now sshd
```

A bare rootfs prepared by `vm/deploy.sh` is the exception, and only in how it is
reached: the script creates `finn` with a locked password and passwordless
sudo, so the machine is entered as `root` over SSH or with a key. That is a
build-machine convenience. Never expose it to a network you do not control.

## Applications

### A tile is grey

The app is not installed, or its launcher is not executable. Both are reported
honestly:

```sh
sudo finnctl app.list
finnpkg list
ls -l /usr/local/bin/finn-*
```

A remote catalogue row (`source=flatpak` or `winget`) has no local command by
design; it is a discovery record, not a native app.

### An app will not launch

```sh
sudo finnctl app.launch <id>
sudo journalctl -u 'finn-app-*' --no-pager | tail -60
```

`app.launch` refuses `ERR unknown app`, `ERR not installed` and `ERR no runnable
command` rather than starting something that is not there. A launched app runs
in its own transient systemd unit; the journal for it is under its unit name.

### A Flatpak opens a blank window or crashes

The compositor hosts only xdg-shell `wl_shm` toplevels. Popups, subsurfaces,
dmabuf, touch, clipboard data transfer and drag-and-drop are not implemented.
An app that needs them can start and then fail to draw. This is a known
protocol limit, not a packaging problem; see
[architecture.md](architecture.md#what-is-not-here-yet).

Check the job first, then the app:

```sh
sudo finnctl flatpak.job <id>
flatpak list --system
flatpak run --system <app-id>
```

### Wine or winget

Both are x86_64-only. `wine.status` reports what the machine really has.
`winget.install` reports that the **installer exited**, not that the
application is installed; GUI behaviour is unverified. There is no host-app
auto-discovery: after an installer runs, select the executable with
`finnpkg wine set`.

## Updates

### `ota.check` says it cannot reach the release

```sh
finnctl ota.check
finnpkg update check
curl -sI https://api.github.com/repos/Finn-Technologies/FinnOS/releases/latest
```

No network, a proxy, or GitHub rate-limiting all produce an honest error rather
than a made-up version. `ota.status` is the cached answer and never blocks on
the network; `ota.check` is the one bounded lookup.

### The update job failed

```sh
finnctl ota.job <id>
sudo journalctl -u 'finn-ota-*' --no-pager | tail -60
pacman -Q finnos-runtime
```

A failed `pacman -U` is reported with pacman's own error, and the restart step
is not run. Check what is installed with `pacman -Q finnos-runtime` before
retrying or rolling back. If the two services do not come back after a
successful transaction, `Ctrl+Alt+F2` and restart them. The full recovery path,
including rolling back from the package cache, is in
[updates.md](updates.md#if-the-update-goes-wrong).

### There is no update available for this architecture

The release workflow builds `finnos-runtime` for x86_64 only. An aarch64
machine's `ota.check` says so instead of taking a wrong-architecture package;
the ARM development VM updates through `vm/deploy.sh`.

## The package manager

`finnpkg` refuses rather than half-installing. The message is the diagnosis:

| Message | Meaning |
|---|---|
| `'<id>' is not an app id` | lowercase letters, digits and `-`, starting with a letter, 32 characters at most |
| `'<name>' cannot be installed: its command is not on this machine` | the catalogue entry names a launcher that is not executable |
| `'<file>' contains a symlink` (or hard link, or device) | `.finn` archives may not carry them |
| `'<file>' has no SHA256SUMS` | hashes are the only check in this build; the archive is refused |
| `'<file>' failed its hash check` | the archive does not match its own sums |
| `this needs root: it writes /var/lib/finnpkg/installed` | run it with `sudo`, or through `finnd`, which runs it as the right user |

`finnpkg install <file.finn>` lists the archive before unpacking, refuses
absolute paths and `..`, unpacks into a fresh temporary directory, requires
`manifest.toml` and a `SHA256SUMS` that covers every file, and only then records
the id. It has no signature check.

## The installer

The installer's own log is `/tmp/finn-install.log` **on the live medium**, not
on the installed disk. It also mirrors its output to the console it was started
from, and posts progress to `FINN_HOST_URL` (`http://192.168.44.1:8765`) when a
dev server is listening.

| Message | Cause |
|---|---|
| `no partition table` | the disk was not writable, or it is not the disk you think |
| `the partition nodes did not appear` | udev did not settle; check `lsblk`, then retry |
| `mkfs.vfat failed` | the ESP is mounted somewhere else |
| `not enough free disk space` (pacman) | the package list plus the build does not fit; 8 GB is the comfortable minimum |
| `pacman` signature failure | the keyring did not initialise; the installer stops rather than installing unverified packages |

The installer has no repair mode. If it stops, boot the medium again, name the
disk, and install again. See [install.md](install.md).

## `make deploy` fails

```sh
FINN_HOST=finn@<address> vm/deploy.sh
```

| Message | Meaning |
|---|---|
| `Permission denied (publickey,password)` | the address or the account is wrong; the default is `finn@10.211.55.6` |
| `sudo: a password is required` | the target cannot sudo without a password; connect as `root` instead, and `vm/deploy.sh` will prepare the rootfs |
| `rsync: command not found` | not from this script: it uses a tar stream on purpose, because the VM has no `rsync` |
| a build error in `finn-comp.c` | the target is missing a build dependency; the script installs the set on a bare rootfs, but not on a machine it did not prepare |

The build machine must be Arch (or the dev VM). The host is an editor; see
[development.md](development.md).

## Captures

`vm/shot.sh` converts the PPM on the host with Pillow. Without Pillow it leaves
the PPM where it was and says so:

```sh
python3 -m pip install --user Pillow
```

The device needs nothing installed to take a capture; the conversion is the
host's job.

If the capture is black, the compositor is not painting. If it is the wrong
size, `screen.info` reports the mode the compositor chose: the largest
advertised mode within its scanout cap, or the connector's first mode.

## Collecting a report

For a bug report, these are the useful facts:

```sh
cat /usr/share/finn/version
uname -a
lspci -k | grep -A3 -E 'VGA|3D|Ethernet|Network'
ls /dev/dri
cat /proc/cmdline
systemctl status finn-comp finnd --no-pager
journalctl -b -u finn-comp -u finnd --no-pager | tail -120
sudo tail -40 /var/log/finn/control.log
```

If it is a rendering problem, a capture. If it is a hardware problem, say
whether the machine is a VM, an install medium or a real install, and say what
you did **not** verify. An honest "I only tested in QEMU" is worth more than an
implied hardware test.

Do not open a public issue for a privilege-boundary problem; see
[security.md](security.md#reporting-a-problem).

## Related

- [install.md](install.md) — the disc, the installer and recovery
- [hardware.md](hardware.md) — what the image installs and what it does not
- [updates.md](updates.md) — the runtime package and its rollback
- [development.md](development.md) — the dev loop and the logs
- [security.md](security.md) — the authority model and its gaps
