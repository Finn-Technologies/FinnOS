# FinnOS

An operating system for a small, quiet machine: an i3-class desktop or laptop,
a screen, a pointer, a keyboard, and a battery if there is one.

FinnOS draws its own desktop. There is no X11 and no external Wayland
compositor: `finn-comp` owns the DRM scanout buffer, rasterises the shell
itself and page-flips the result. It hosts a deliberately limited in-process
Wayland server so that ordinary Linux apps can still open a window. The
control plane is one permissioned unix socket that any script — and, if you
choose, an agent — can drive.

![The desktop](docs/images/desktop-light.png)

Its base is Arch Linux: the kernel, firmware and userspace packages are the
distribution's. Everything above the kernel's interfaces is FinnOS — one
compositor, one control plane, one shell, one application registry and one
package manager.

## Status

| | |
|---|---|
| **Version** | `0.1.1`; [`VERSION`](VERSION) is the source of truth |
| **Release target** | x86_64, reference machine Dell OptiPlex 3020 |
| **Development target** | aarch64, in a virtual machine |
| **Real hardware** | The x86_64 ISO installs and boots to the desktop on an OptiPlex 3020. See [docs/dell-optiplex.md](docs/dell-optiplex.md) |
| **CI** | The test suite, the build and the style checks on every push; the ISO, its QEMU boot and an optional full install-plus-boot on demand and on every tag |
| **Known gaps** | No `.fbin` renderer, no A/B update slots, no package signatures, limited Wayland client protocols. Every one is listed in [docs/architecture.md](docs/architecture.md#what-is-not-here-yet) |

FinnOS is usable, not finished. The shell, the control plane, the installer and
the runtime update path work on real hardware. The native `.fbin` application
runtime, A/B update slots and package signing do not exist yet, and the
documentation says so rather than implying otherwise.

## What is here

| Component | What it is |
|---|---|
| **the compositor** — `finn-comp` | DRM/KMS, page flips, a GPU path and a software path, the wallpaper, and the shell: boot splash (`finnboot`), first-boot setup (`finnsetup`), dock (`finndock`), [Control Center](docs/images/control-center-light.png) (`finncontrol`), [Notification Center](docs/images/notification-center-light.png) (`finnclock`), [Start Menu](docs/images/start-menu-light.png) (`finnmenu`), frosted [windows](docs/images/window-light.png) (`finnwindow`), Settings, Files and Store |
| **the control plane** — `finnd`, `finnctl` | One permissioned entry point for state, input, display, power, windows, setup and apps, audited on every call |
| **the registry** — `assets/apps.registry` | The catalogue of apps the OS ships; the install list is the package manager's |
| **the package manager** — `finnpkg` | Native app install/remove, Flatpak, Wine and winget lifecycles, and the runtime OTA frontend |
| **the SDK** — `fuic`, `finnsdk` | The `.fui` compiler and the Python client |
| **the adapter** — `finn-mcp` | The control plane as MCP tools; it runs on the host, never on the device |
| **the base OS** — `base/` | The Arch-derived rootfs, bootloaders, kernel preset, installer (`finn-install`) and the `mkfinniso` builder |

## Install it on a machine

You need an x86_64 PC, a 4 GB USB stick, a network connection and Secure Boot
turned off. Build the ISO from this tree on an Arch machine — or take the ISO
from a tagged GitHub Release:

```sh
base/mkfinniso --check
sudo base/mkfinniso finnos.iso
```

Write the whole device, not a partition:

```sh
sudo dd if=finnos.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Boot the stick. **Install FinnOS** starts the installer after ten seconds;
**FinnOS live shell** leaves you at a prompt. The installer has no default
disk, because the default disk is the one somebody meant to keep:

```sh
finn-install --disk /dev/nvme0n1          # asks for the path again
finn-install --disk /dev/sda --yes        # unattended
```

The full walkthrough — firmware settings, disk names, recovery, the OptiPlex
specifics — is [docs/install.md](docs/install.md).

## Work on it

The compositor needs DRM, GBM, EGL, FreeType, ALSA and the Fixel fonts, so the
build machine is an Arch machine or the dev VM, not a Mac. The loop is:

```sh
make deploy                 # build, install and restart on the dev VM
vm/shot.sh /tmp/shot.png    # look at the frame
make test                   # the suite
make test-c                 # the C tests, under ASan and UBSan
make lint                   # the style checks
```

`make deploy` sends the tree to the VM as a tar stream, builds it in
`~/finnos`, installs it to `/usr/local/bin` and restarts `finn-comp` and
`finnd`. `vm/shot.sh` brings a PPM back over the control plane.

The dev loop, the host-side preview tools and the VM details are in
[docs/development.md](docs/development.md). If you are changing the shell,
read [AGENTS.md](AGENTS.md) and the design system first.

## The rules it is built on

- **Nothing is faked.** A machine with no wireless adapter says so. An app
  that is not installed says so. Every field in the system state has an absent
  value (`-1`) rather than a plausible default, and a control that cannot act
  never looks like it can.
- **No AI ships.** There is no model, no weights and no inference code in the
  tree, and nothing in the OS needs a network call to a model. Any future
  FinnAI is opt-in and off by default.
- **The control plane is local-only.** It runs, because the shell uses it, but
  it binds a unix socket and never TCP, is authenticated per connection, and
  is audited with the peer uid. Remote access is a decision the user makes by
  enabling `sshd`, not a default.
- **One naming family.** [NAMING.md](NAMING.md) is short, and it is the rule.

## The tree

```
src/core        the shared library — protocol, sockets, state, registry, settings
src/compositor  finn-comp: the display server and the shell
src/control     finnd and finnctl
src/pkg         finnpkg
src/tools       host-side preview tools; they never run on the device
ui              the .fui sources: the shell, the tokens, the targets
apps            the .fui sources of the first-party apps
assets          fonts, wallpaper, icons, the application registry
sdk             FinnSDK: fuic, the Python client, the templates
mcp             the MCP adapter
base            the base image: packages, units, live root, boot menus, installer
vm              the dev VM: install, deploy, capture, boot tests
docs            this system's documentation
tests           the Python suite, the C tests and the fake control plane
```

## The documentation

Start at [docs/README.md](docs/README.md), which indexes everything. The pages
most people want first:

| | |
|---|---|
| [docs/architecture.md](docs/architecture.md) | how the processes, the layers and the data fit together, and what is not here yet |
| [docs/control-protocol.md](docs/control-protocol.md) | the wire, every verb, and the shape of a session |
| [docs/install.md](docs/install.md) | build the disc, write the stick, install, first boot, recovery |
| [docs/development.md](docs/development.md) | the dev loop, the VM, the host-side tools, the logs |
| [docs/testing.md](docs/testing.md) | what the suite covers, what CI runs, and what only hardware can prove |
| [docs/ui-language.md](docs/ui-language.md) | `.fui`, the tokens, the targets and the compiler |
| [sdk/README.md](sdk/README.md) | FinnSDK: the compiler, the client, the templates |
| [CONTRIBUTING.md](CONTRIBUTING.md) | how to send a change |
| [AGENTS.md](AGENTS.md) | the working agreement, for people and for agents |
| [NAMING.md](NAMING.md) | the naming rules |

## Licence

MIT. See [LICENSE](LICENSE).
