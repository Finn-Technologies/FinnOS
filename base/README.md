# The FinnOS base

This directory is the operating system's base: the package set, the
configuration, the boot menus, the live root and the units that make a
machine FinnOS's rather than the distribution's it was assembled from.

The base is Arch Linux's. FinnOS does not fork pacman, it does not fork
systemd, and it does not maintain its own kernel: those are the parts that
already work, and a fork of them would be maintenance for its own sake.
What FinnOS does is edit the source base directly — the package list, the
configuration, the branding and the boot path — until what is left is one
display server, one control plane and nothing else between the kernel and
the screen.

The Arch release ISO is a different thing from this. That one is a live
desktop medium; this one is the same toolchain with the desktop taken out,
built from these files.

## What is stripped, and why

Everything here is a decision with a reason, and the reasons are the
product:

| Removed | Why |
|---|---|
| X11 — `xorg-server`, `xorg-server-common`, `xorg-xwayland` | The compositor owns DRM/KMS directly. An X server on the machine would be a second thing claiming the scanout buffer. |
| External Wayland compositors — `weston`, `sway`, `mutter`, `kwin`, `wlroots` | `finn-comp` owns DRM/KMS directly, so a second compositor is not installed. FinnOS includes a minimal in-process xdg-shell server for SHM toplevels; this is a client protocol inside `finn-comp`, not a competing display server. |
| GTK and Qt — `gtk2`, `gtk3`, `gtk4`, `qt5-base`, `qt6-base` | Toolkit stacks exist to draw for a display server. There is no second display server to draw for, and a toolkit in the image is a hundred megabytes of libraries that nothing links against. |
| The desktops — `gnome`, `plasma`, `sddm`, `gdm`, `lightdm` | A session manager would start a shell that is not FinnOS's. `finn-comp` is started by systemd, on tty1, before `getty`. |
| Arch branding | `os-release`, `issue`, `motd`, the hostname, the boot menus and the volume label all say FinnOS, because a person reading the console should be told what they are running. |
| A live desktop | The medium is a console that installs FinnOS. Shipping a shell on it that is not the shell the machine will run would be a demonstration, not a product. |

`pacman.conf` refuses those packages by name. That is a guard rail and not
a sandbox, and the file says so: an ignored package is not installed, not
even to satisfy a dependency — pacman logs `ignoring package …` and fails
the transaction when nothing else can satisfy it — but naming the package
on the command line installs it after a warning, and a package installed
from a file with `-U` never consults the list.

Wayland libraries and protocol XML are deliberately allowed: Mesa needs
`libwayland-client`, while `finn-comp` uses the server library to host limited
app surfaces. The package guardrail refuses competing compositors, not app
protocols. No Xwayland bridge is included; x86_64 `multilib` is kept separate
and appended only to the build/setup configuration for Wine.

## What replaces them

| Arch's | FinnOS's |
|---|---|
| X server or Wayland compositor | `finn-comp` — the display server and the shell, in one process (`src/compositor`) |
| `dbus`-mediated session services | `finnd` — one permissioned unix socket, `/run/finn/control.sock` (`src/control`) |
| A desktop's settings app | the Control Center, a surface of `finn-comp`, driven by `finnctl` |
| A store or an AUR helper | `finnpkg` and `/usr/share/finn/apps.registry` (`src/pkg`) |
| A toolkit and a UI framework | FinnUI: `.fui` in, `.fbin` out, compiled by `fuic` (`sdk/`) |

## The tree

```
README.md            this
profiledef.sh        the archiso profile: name, label, layout, boot modes
pacman.conf          the shared configuration, with competing desktops refused
pacman.multilib.conf the x86_64-only Wine dependency repository snippet
packages.x86_64      the release target's package set
packages.aarch64     the ARM dev machine's package set
packages.x86         -> packages.x86_64
packages.arm         -> packages.aarch64
mkfinniso            the ISO builder
setup.sh             the same base, applied to a running machine
finn-comp.service    the compositor's unit
finn-failure.service the unit that says the compositor stopped
finnd.service        the control plane's unit
finn-tmpfiles.conf   /run/finn, the IPC namespace
airootfs/            the live root: branding, hooks, units, autologin
boot/                the three boot menus
pkgbuilds/           the local packages: finn-filesystem, finnos-compose,
                     finnpkg, finnsdk
```

Two names for two package lists is not a compatibility shim that was
forgotten: `packages.x86` and `packages.arm` are the names the dev VM
scripts have always used, and they are symlinks so that there is one copy
of the list.

What is in `airootfs` is the live root and only the live root:

```
airootfs/etc/os-release          NAME and PRETTY_NAME "FinnOS", ID=finnos
airootfs/etc/issue, motd         what a person reads at the console
airootfs/etc/hostname            finnos
airootfs/etc/mkinitcpio.conf     the live hooks: kms, then archiso
airootfs/etc/mkinitcpio.d/       one image, no fallback, one disc
airootfs/etc/systemd/system/     finn-comp.service, finnd.service,
                                 finn-failure.service,
                                 getty@tty1 autologin as root,
                                 NetworkManager enabled
airootfs/usr/lib/sysusers.d/    the live `finn` group, before tmpfiles runs
airootfs/usr/lib/tmpfiles.d/    `/run/finn`, the IPC namespace
airootfs/usr/local/bin/          finn-install: the name the boot entry calls
airootfs/root/.bash_profile      the finn.install hook, for bash and zsh
```

The compositor's unit is on the disc and not enabled: the medium is a
console that installs FinnOS, and the machine it installs on is where the
shell is built. A unit enabled with no binary to run is a unit that fails
at every boot.

The unit does not test for the GPU once and give up. It wants
`dev-dri-card0.device`, so systemd waits for the node to exist and a node
that never appears is a failed start with a start limit and an `OnFailure=`
unit behind it, rather than a condition that is silently false for the
whole boot. `card0` is the trigger, not the only device the compositor can
open: it enumerates `/dev/dri/card*` itself.

`boot/` contains the menu configurations. The x86_64 profile selects BIOS
/syslinux and UEFI/systemd-boot boot modes; the ARM profile selects UEFI only.
The GRUB configuration is included for manual use, but GRUB is not a separate
`mkarchiso` boot mode and has not been verified as an ISO boot path:

```
boot/syslinux/syslinux.cfg       BIOS menu; syslinux
boot/loader/loader.conf          UEFI menu; systemd-boot
boot/loader/entries/01-…         Install FinnOS, with finn.install
boot/loader/entries/02-…         FinnOS live shell
boot/grub/grub.cfg               GRUB configuration for manual use
```

## Installing onto a machine

The medium is a console that installs FinnOS. **Install FinnOS** boots it
and `finn-install` writes the disk that is named on the command line —
there is no default, because the default is the one that erases the machine
somebody meant to keep:

```sh
finn-install --disk /dev/nvme0n1      # asks for the path again, typed
finn-install --disk /dev/sda --yes    # unattended, no second question
```

It resolves the device first (so `/dev/disk/by-id/...` works), and refuses
a partition, a disk that backs `/`, `/boot`, `/mnt` or the running live
medium, and a disk with a mounted partition or swap on it. The partition
names follow the device: `nvme0n1p1`, `mmcblk0p1`, `sda1`. Every step is
checked rather than assumed, and `FINN_INSTALL_COMPLETE` is printed only
after looking at the disk that was written: `/usr/local/bin/finn-comp` must
be executable there and a boot entry must name the root by its UUID.

The x86_64 release includes both `intel-ucode` and `amd-ucode`; the
reference CPU does not narrow the profile to one vendor. The image's UEFI
boot path is not Secure Boot signed, so firmware Secure Boot must be disabled.
The live ISO uses systemd-boot; the installer prefers GRUB's UEFI loader and
falls back to systemd-boot when GRUB cannot register its EFI entry.

The password is not on the disc. Without `FINN_PASSWORD` the chroot
generates one for `root` and `finn` and prints it once at the end of the
install; with it, that value is used and is never on a command line.
`sshd` is installed and left disabled, and the machine is on
`graphical.target` with `getty@tty2` behind it for the console.

## The units are one file, twice

`finn-comp.service`, `finnd.service`, `finn-failure.service` and
`finn-tmpfiles.conf` exist in two places: here, where `setup.sh` installs
them from, and in `airootfs/`, where the disc carries them. They are the
same file. `mkfinniso --check` compares them and refuses to build when they
have drifted, because a disc that ships a unit the machine does not have is
a bug that only shows up after an install. The failure unit is on the disc
even though the live medium never starts a compositor: `finn-comp.service`
names it in `OnFailure=`, and a unit set that names a unit it does not carry
is incomplete.

The failure unit is a oneshot. Both `setup.sh` and the installer's chroot
install it. It writes one line to `/dev/console` and to the journal —
"FinnOS: the compositor stopped; press Ctrl+Alt+F2 for a console" — so a
broken session is visible on the screen the compositor was painting and
there is a stated way out.

## Building the medium

Two ways, and the builder says which one it used.

**Native**, from this tree alone, on an Arch machine:

```sh
sudo base/mkfinniso finnos.iso
```

It stages the profile for `mkarchiso`, which resolves `packages.<arch>`
against the repositories, runs `mkinitcpio` with the hooks in
`airootfs/etc/mkinitcpio.conf`, and writes all three boot menus. The
sources go into the live root at `/opt/finnos`, where the installer reads
them. This is the real build and needs a network.

It needs root, the `archiso` package, and build-host tools including
`syslinux` for the BIOS boot sector, `mtools` and `dosfstools` for the EFI
system partition, and `squashfs-tools` plus `xorriso` for the image. The
`packages.<arch>` files are the installed-system manifests; `mkfinniso`
adds `archiso`, `mkinitcpio-archiso` and (on x86_64) `syslinux` to the live
profile only. `mkinitcpio-archiso` supplies the `archiso` and
`archiso_loop_mnt` hooks named by the live root's `mkinitcpio.conf`; none of
these ISO-only packages are installed to the target system.

**Assemble**, when there is no archiso:

```sh
base/mkfinniso --mode=assemble --kernel=/boot/vmlinuz-linux \
  --initramfs=/boot/archiso-initramfs.img finnos.iso
```

It builds the live root from `airootfs` with `mksquashfs`, writes the boot
menus from `boot/` with `xorriso`, and needs a kernel and an initramfs
that can find an archiso disc — the archiso hook is what does that, and
nothing in this tree can build one without the archiso package. The
script checks the initramfs for the hook and refuses rather than writing a
disc that stops at the kernel. It also needs `syslinux` for the BIOS boot
and `systemd` + `mtools` + `dosfstools` for UEFI; without them it writes
what it can and says what is missing, and the last line of its output says
which boot images the disc actually got.

Either way, the tree is checked first, and the check runs anywhere:

```sh
base/mkfinniso --check
```

It is the CI's job too: it verifies the profile, the boot menus against
the profile's label and layout, the package lists, the `IgnorePkg` guard
rail, the unit copies, and that nothing on the disc names another
distribution.

There is no third way. The ISO workflow builds the same native image in a
privileged Arch container, because a GitHub runner is not an Arch
machine — see `.github/workflows/iso.yml`. No base image is ever
downloaded: every disc is FinnOS, built from this tree.

## Applying the base to a running machine

```sh
sudo base/setup.sh            # the machine's own architecture
sudo base/setup.sh x86_64     # the release target
```

It reads the same `packages.<arch>` the disc does, installs
`pacman.conf` (keeping the previous one as `/etc/pacman.conf.finn-backup`),
removes any X server, Wayland compositor, GTK or Qt stack that is on the
machine (the `wayland` client library stays, because `mesa` links it),
creates the
`finn` user, enables `seatd` and `NetworkManager` (with `wpa_supplicant`
installed as its Wi-Fi backend), and installs the units (the compositor, the
control plane and the failure unit), the fonts, both
wallpapers, every icon, the app registry, `finnpkg`, and the branding:
`os-release`, `issue`, `motd` and the hostname.

It does not set a password: whoever runs `setup.sh` is already root and has
one. On this path `finn` keeps passwordless sudo
(`finn ALL=(ALL) NOPASSWD: ALL`), because the dev loop drives `sudo` from
`vm/deploy.sh` over ssh, where there is no terminal to be asked on. A
machine installed from the disc is not this machine: the installer
generates a password, sudo asks for it, and nothing on the shipped image
has a known password.

`sshd` is installed and not enabled. A network login is a decision somebody
makes on the machine (`systemctl enable --now sshd`), not a default.

The hostname is set only when the machine still has the default one, and
`FINN_KEEP_HOSTNAME=1` leaves it alone whatever it is. A machine whose
name a person chose is that person's machine.

The units are installed always, and enabled only when the binaries they run
exist: enabling a unit whose binary is not on the machine is a machine that
fails at every boot. So the script checks for `/usr/local/bin/finn-comp` and
`/usr/local/bin/finnd`, and when both are there it enables them and sets
`graphical.target` as the default target. When they are not, it says which
one is missing and leaves the machine booting to its console. Build first —

```sh
cd /opt/finnos && make && sudo make install
sudo base/setup.sh            # again, or:
sudo systemctl enable --now finn-comp finnd
sudo systemctl set-default graphical.target
```

`getty@tty2` is enabled either way, as the rescue console: the compositor
owns tty1 (`finn-comp.service` conflicts with `getty@tty1`), so tty1 is not
a way in, and Ctrl+Alt+F2 is. The old tty1 autologin drop-in is removed
where an older base left one behind — the getty it configured never ran.

`airootfs/etc/mkinitcpio.conf` is not part of this. It is the live
medium's initramfs, and its archiso hooks would be wrong on a machine that
boots from a partition.

## The packages

`base/pkgbuilds/` holds the local side of the base — the packages that
come from this tree rather than from a repository:

| Package | What it is |
|---|---|
| `finn-filesystem` | the directories FinnOS owns, and `/run/finn` |
| `finnos-compose` | `finn-comp`, `finnd`, `finnctl`, their units, the icons, the wallpapers and the fonts |
| `finnpkg` | the package manager |
| `finnsdk` | the `.fui` compiler and the control-plane client |

They are not in `packages.<arch>`, because a package list that names a
package no repository has is a list that does not resolve. Each builds
from the checkout it sits in:

```sh
FINNOS_SRC=/path/to/finnos makepkg -f
```

Everything they install goes under `/usr/local`, which is the tree's own
convention: `src/compositor/Makefile`'s default prefix, the `ExecStart` in
`finn-comp.service` and these packages all name `/usr/local/bin/finn-comp`,
and the failure to avoid is two of them disagreeing.

## What is not here

- **The external client host is a limited implementation.** `finn-comp`
  exposes an in-process xdg-shell server and accepts SHM toplevels, with
  compositor-managed windows and routed focus/input. Popup roles, subsurfaces,
  dmabuf, touch, scaling, clipboard and drag-and-drop are not implemented, and
  the new path has not yet been built or run on the Linux target. See
  `docs/architecture.md` for the exact boundaries.
- **No X11 or external Wayland compositor.** The `wayland` client/server
  libraries are on the disc for Mesa and FinnOS's limited app-surface server;
  FinnOS still owns the display directly through DRM/KMS. Xwayland and other
  competing display servers are not part of the base.
- **No AI, and no weights.** FinnAI is opt-in and off by default, and
  nothing in this directory talks to a model.
- **No OTA.** `finnpkg update` is a stub. The A/B slots are a design in
  `docs/roadmap.md`, not a feature.
- **No repository.** `packages.<arch>` is resolved from Arch's mirrors.
  FinnOS has no mirror of its own yet, so a machine installed from this
  base updates from Arch — minus the packages `pacman.conf` refuses.
- **No syslinux on ARM.** The ARM profile boots through UEFI only, which
  is what an ARM board has.
