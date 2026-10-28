# Working on FinnOS

This page is the dev loop: the machine you build on, the commands you type, the
tools that show you what you did, and the logs that tell you what went wrong.
It assumes you have read [architecture.md](architecture.md) and skimmed
[CONTRIBUTING.md](../CONTRIBUTING.md).

## The build machine is not your laptop

The compositor needs DRM, GBM, EGL, FreeType, ALSA, Wayland server headers and
the Fixel fonts. The Mac (or any other host) is an editor; an **Arch Linux**
machine or the dev VM is where the shell is built and run. Nothing in the
compositor is compiled on macOS, and a build that succeeds there proves
nothing about the device.

There are three ways to have a build machine:

| Machine | Use it when | Connect with |
|---|---|---|
| the VMware dev VM (`vm/README.md`) | day-to-day shell work | `FINN_HOST=finn@<ip>` (default `finn@10.211.55.6`) |
| a bare Arch rootfs | you want a clean, minimal target | `FINN_HOST=root@<ip>` |
| real hardware (an OptiPlex) | the change has to be judged on a real panel | `FINN_HOST=finn@<ip>` after an install |

`vm/deploy.sh` prepares a bare Arch rootfs on the way: it probes for root or
passwordless sudo, initializes the pacman keyring and sync database, installs
the build dependencies, creates `finn` once its groups exist, and installs and
enables the units. A FinnOS dev image has all of that already.

### Build dependencies

On Arch:

```sh
sudo pacman -S --needed base-devel pkgconf libdrm libwebp freetype2 \
  alsa-lib mesa wayland wayland-protocols libxkbcommon
```

The compositor also needs `wayland-scanner` from the `wayland` package, which
generates the xdg-shell server code at build time. CI installs the equivalent
Debian packages; see `.github/workflows/ci.yml`.

## The loop

```sh
make deploy                  # build, install and restart on the target
vm/deploy.sh --no-restart    # build and install, leave the session running
vm/shot.sh /tmp/shot.png     # a capture of the frame
make test                    # the Python suite
make test-c                  # the C tests, under ASan and UBSan
make lint                    # trailing whitespace, tabs, final newlines, exec bits
```

`vm/deploy.sh` sends the tree over as a tar stream — the VM has no `rsync` —
builds it in `~/finnos`, installs to `/usr/local/bin`, copies the assets and
restarts `finn-comp` and `finnd`. `make install` is the same install step
without the restart.

`make deploy` reads `VM`; the `vm/*.sh` scripts read `FINN_HOST`. Both default
to `finn@10.211.55.6`, so for one target only one of them has to be set.

`scripts/verify.sh` runs the whole thing in order: tests, lint, deploy,
`screen.shell`, captures of the desktop and the Start Menu in both
appearances, and a scan of the journal for errors. It is the loop that says
"it works" rather than "it compiles".

### Targets

| Target | What it does |
|---|---|
| `make` | build the core, the compositor, the control plane and the host tools |
| `make core` | the static library only |
| `make compositor` | `finn-comp` |
| `make control` | `finnd`, `finnctl`, `finn-window` |
| `make tools` | `dockprev`, `vgtest` — host-only, no device |
| `make test` | the Python suite |
| `make test-c` | the C tests |
| `make lint` | the style checks |
| `make install` | install to `PREFIX` (default `/usr/local`), honouring `DESTDIR` |
| `make deploy` | build, install and restart on `VM` |
| `make clean` | remove build output |

### Environment

| Variable | Used by | Meaning |
|---|---|---|
| `FINN_HOST` | `vm/*.sh`, `scripts/verify.sh` | ssh target; default `finn@10.211.55.6` |
| `FINN_REMOTE` | `vm/deploy.sh` | build directory on the target; default `~/finnos` |
| `FINN_SHOT_DIR` | `scripts/verify.sh` | where captures are written; default `/tmp` |
| `VM` | `make deploy` | the ssh target; default `finn@10.211.55.6` |
| `FINN_CONTROL_SOCK` | `FinnClient` | use a different control socket |
| `FINN_CONTROL_ALLOW_ANY` | `finnd` | accept any uid; `1` only, test harness only |
| `FINN_SETUP_SKIP` / `FINN_SETUP_FORCE` | `finn-comp` | skip or force the first-boot screen |
| `FINN_MODE` | `finn-comp` | request a mode (`1920x1080`); automatic if absent or unavailable |
| `FINN_GPU` | `finn-comp` | `0` forces the software compositing path |
| `FINN_APPS` | `finn-comp` | an alternate application registry |
| `FINN_BOOT_HOLD` | `finn-comp` | hold the boot screen for that many seconds, under 60 |
| `FINN_BOOT_CAPTURE` | `finn-comp` | where to write the boot screen as a PPM |
| `FINN_APPS_CATALOGUE`, `FINN_APPS_INSTALLED`, `FINN_APPS_DIR` | `finnpkg` | alternate registry, install list and app directory, for tests |

## Looking at it

```sh
vm/shot.sh /tmp/desktop.png          # full resolution
vm/shot.sh /tmp/desktop.png 2        # every second pixel
```

The capture comes back over the control plane as a PPM and is converted on the
host with Pillow. The device needs nothing installed for it to work. Without
Pillow, `vm/shot.sh` leaves the PPM where it was and says so.

To drive the shell into a state, use `finnctl` — the same client an agent or a
test uses:

```sh
ssh finn@10.211.55.6 '
  sudo finnctl pointer.move x=1196 y=1506
  sudo finnctl pointer.click
  sleep 1
  sudo finnctl screen.shell
'
```

`screen.shell` reports what the shell is showing — which surfaces are open, how
open they are, where the pointer is — so a script can wait for a state instead
of guessing at a sleep. `state.set` drives the hardware state the same way:

```sh
sudo finnctl state.set battery=12 charging=0
sudo finnctl state.clear
```

### The host-side preview tools

Two programs in `src/tools` render the real drawing code without a VM:

```sh
make -C src/tools
src/tools/dockprev out.ppm 1080 2 87 0 3 40    # the dock
src/tools/vgtest assets/icons sheet.ppm 2       # every icon in the set
```

They are the fast loop: change a number, run the tool, look at the image.
`vgtest` writes a sheet of every icon under `assets/icons`, which is how a
change to the vector rasteriser is judged.

## The services and the logs

```sh
systemctl status finn-comp finnd
sudo journalctl -u finn-comp -f
sudo journalctl -u finnd -f
sudo tail -f /var/log/finn/control.log     # the audit trail
finnctl log 100                            # the compositor's journal, read on the machine
```

The compositor writes to the journal, never to the TTY it is painting on: a log
line on the screen the compositor is composing would be erased by the next
frame. Every control-plane call is audited with the peer uid.

When the compositor fails repeatedly, systemd stops it at the start limit and
`finn-failure.service` writes one line to `/dev/console`. `Ctrl+Alt+F2` is the
rescue console. See [troubleshooting.md](troubleshooting.md).

## Testing while you work

```sh
make test                       # the Python suite, no device, no dependencies
python3 -m unittest tests.test_compiler -v
make test-c                     # the C tests, sanitizers on by default
make test-c SAN=                # the same, on a compiler without sanitizers
```

The suite's fake control plane (`tests/fakeserver.py`, `tests/support.py`) is
also the easiest way to exercise a client change without a device. The C tests
build on macOS for the parts that do not need Linux headers; the Settings page
test uses compile-only shims.

What is and is not covered is [testing.md](testing.md); do not assume the
compositor is covered by either suite.

## Working on the shell

The shell is C inside `finn-comp`, not a separate process. A change usually
touches:

1. the surface module (`finndock.c`, `finncontrol.c`, `finnclock.c`,
   `finnmenu.c`, `finnwindow.c`, or one of the app modules);
2. the shared drawing primitives (`finndraw.c`) or the type (`finntext.c`);
3. the build key of the cached layer, if a new property can change a pixel;
4. both the software and the accelerated path, if the change is in
   compositing;
5. the `.fui` source in `ui/` that the surface was drawn from, so the design
   and the code do not drift.

Then capture it. A shell change with no capture is not a shell change that has
been seen.

## Working on the control plane

The protocol is one header and two daemons. A new verb is:

1. handled in `finnd` (state, power, apps, packages, updates) or
   `finn-comp` (display, input, machine, shell surfaces);
2. proxied in `finnd` if the compositor answers it (see the prefix list in
   `finnd.c`);
3. added to `finnctl`'s usage text;
4. documented in [control-protocol.md](control-protocol.md);
5. tested against the fake server if it is a client-side change, or with a
   C test if it is a pure function.

If the verb changes the machine, it must report the actuator's result, not the
intent. If it only reports, it must not change anything.

## Working on the base image

The ISO build is documented in [install.md](install.md) and
[base/README.md](../base/README.md). The short version:

```sh
base/mkfinniso --check
sudo base/mkfinniso finnos.iso
vm/test-boot.sh finnos.iso
```

`--check` validates the profile, the menus, the package lists, the `IgnorePkg`
guard rail, the duplicate unit copies and the branding without building
anything. It is also what CI runs. A change to `base/` that breaks the check
will not produce a disc.

Remember that the live root has no `/dev/fd`: a `<(…)` process substitution
fails before the loop runs. Use a temporary file, or a FIFO held open on a
descriptor, as the installer does.

## Real hardware

For an x86_64 PC instead of the VM — a Dell OptiPlex in particular — see
[install.md](install.md) and [dell-optiplex.md](dell-optiplex.md). The same ISO
and installer are used; the differences are Secure Boot (off), the disk name
and the fact that the machine is the one being judged.

Once it is installed, the day-to-day loop is the same:

```sh
VM=finn@<optiplex-ip> make deploy
```

## Style

The style rules are in [CONTRIBUTING.md](../CONTRIBUTING.md#style). The two
that matter most while you are in the code:

- **Comment the reason, not the mechanism.** The tree is full of comments that
  explain *why* a thing is the way it is, because the "what" is already in the
  code.
- **Never fake a capability.** A missing battery is `-1`, a missing app is
  "available", a missing sensor is absent. The shell is allowed to say "this
  machine does not have one"; it is not allowed to invent a number.
