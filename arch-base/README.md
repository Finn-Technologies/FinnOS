# FinnOS base image

The base is Arch Linux. FinnOS ships its own display server and shell, and
everything else — the init system, the toolchain, the network stack — is
the distribution's. What is *not* here matters as much as what is: no X11,
no Wayland, no GTK, no Qt, no XWayland. A machine running FinnOS has no
display server but the one it was built with.

| File | What it is |
|---|---|
| `setup.sh` | the in-VM setup: packages, the `finn` user, the units, the assets |
| `finn-comp.service` | the compositor's unit |
| `finnd.service` | the control plane's unit |
| `finn-tmpfiles.conf` | `/run/finn`: the IPC namespace and the state overrides |
| `packages.arm` | the package set for the ARM64 dev VM |
| `packages.x86` | the package set for the x86_64 release target |

## The two targets

| | |
|---|---|
| **ARM64** | the dev VM: Apple Silicon under Parallels. The GPU is Apple M4 through virgl. |
| **x86_64** | the release target: an i3-4130 with HD Graphics 4400 and 4 GB of RAM. |

The compositor's budget on that machine is under 120 MB of RAM and 60 MB
of VRAM at 1080p, and an idle desktop redraws nothing at all.

## Running it

```sh
ssh finn@10.211.55.6 'sudo arch-base/setup.sh'
```

Then build the compositor on the machine and install it:

```sh
cd /opt/finnos && make && sudo make install
sudo systemctl restart finn-comp finnd
```

Or from the host, which is the usual way:

```sh
make deploy
```

## The console

The compositor owns tty1 and the display seat, and the unit says so: it
comes up before `getty@tty1`, conflicts with it, and runs with
`StandardInput=tty` so the diagnostics land in the journal rather than on
the screen the compositor is painting. A machine with no `/dev/dri/card0`
simply does not start it — `ConditionPathExists` takes care of that.
