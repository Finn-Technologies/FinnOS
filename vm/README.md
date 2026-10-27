# FinnOS dev VM

A working Arch Linux x86_64 install that boots FinnOS, running in VMware
Workstation. It is the build machine: the compositor needs the DRM headers,
GBM, EGL and FreeType, and this is where the shell is actually run.

The machine is the distribution's until `base/setup.sh` has run on it: that
is what makes it FinnOS's (the package set, the units, the assets, the
branding), and it is the same directory the install medium is built from.

## The VM

| | |
|---|---|
| Name | `FinnOS-dev` |
| Guest | Arch Linux x86_64 |
| Resources | 4 GB RAM, 2 vCPU, 32 GB disk - the i3-4130 / 4 GB target |
| Disk | `/dev/sda1` 512M EFI (vfat) + `/dev/sda2` 1M BIOS boot + `/dev/sda3` root (ext4, label `finnos`) |
| Bootloader | GRUB: the BIOS boot code in the MBR, and an EFI loader on the partition |
| Console | VMware console, autologin `finn` on tty1 |
| Network | VMware NAT, DHCP. The guest IP is assigned by VMware; read it with `ip addr` |
| GPU | Intel HD Graphics (Mesa) - the accelerated path, GBM + EGL 1.5 |

## Credentials

- `finn` / `finn` (passwordless sudo), `root` / `finn`
- SSH: `ssh finn@<guest-ip>` (port 22, password auth enabled).
- Root SSH is publickey-only (the Arch default, `prohibit-password`).

Override the address used by the scripts with `FINN_HOST`:

```sh
FINN_HOST=finn@192.168.44.128 vm/deploy.sh
```

## The day-to-day loop

```sh
make deploy                  # build, install, restart
vm/shot.sh /tmp/shot.png     # look at it
```

`vm/deploy.sh` sends the tree over as a tar stream - the VM has no
`rsync` - builds it in `~/finnos`, installs it to `/usr/local/bin`,
copies both wallpapers to `/usr/share/finn/wallpaper/` and restarts
`finn-comp` and `finnd`. Building a shell does not install the data it
draws, so the wallpapers are copied here rather than left from an older
deploy.

On a bare Arch Linux ARM rootfs there is no `finn` user and no sudo.
Connect as root and the script also initializes the pacman keyring,
installs the build dependencies, creates `finn` and installs and enables
the units:

```sh
FINN_HOST=root@<guest-ip> vm/deploy.sh
```

To put the rest of the base on the machine - the package set, the icons
and the branding - run `sudo base/setup.sh` on it, which is what an
install does.

To drive the shell from a script:

```sh
ssh finn@<guest-ip> '
  sudo finnctl pointer.move x=1196 y=1506
  sudo finnctl pointer.click
  sleep 1
  sudo finnctl screen.shell
'
```

## Installing a fresh VM

The FinnOS ISO is bootable: it is a live medium built from this tree -
the profile, the package list, the live root and the three boot menus in
`base/`, with the FinnOS sources inside it at `/opt/finnos`. One disc, no
host HTTP server, no downloaded base image. The default entry is
**Install FinnOS**, which boots the live environment and hands over to
the installer, and **FinnOS live shell** is the way in without installing.

1. Build the FinnOS ISO. On an Arch machine (or the OptiPlex once it
   runs FinnOS), the native build writes the medium from `base/` alone:

   ```sh
   sudo base/mkfinniso finnos.iso
   ```

   It needs root, the `archiso` package, and the build-host tools named in
   `base/README.md`. Where there is no Arch machine, CI builds the same
   disc in a privileged Arch container — see `.github/workflows/iso.yml`.
   Either way the tree is checked beforehand with
   `base/mkfinniso --check`.

3. Check that it boots, if you have QEMU:

   ```sh
   vm/test-boot.sh finnos.iso
   ```

   This boots the disc and waits for the guest to reach a login prompt.

   To see the menu itself rather than the kernel's output, take a frame
   of the console:

   ```sh
   vm/menu-shot.sh finnos.iso /tmp/menu
   ```

   It starts QEMU with the display off and asks the monitor for a picture
   at a few moments, so the frames land in `/tmp/menu`; open the one that
   shows the menu.

4. Create the VMware VM:

   ```sh
   vm/create-dev-vm.sh
   ```

5. Boot the VM. The default entry, **Install FinnOS**, starts the
   installer from the live shell after ten seconds; **FinnOS live shell**
   leaves you at the prompt, where `finn-install` does the same thing.

   `vm/finn-install.sh` takes the payload from `/opt/finnos` in the live
   root, partitions, formats, pacstraps and runs `vm/finn-chroot.sh`,
   which configures the machine, installs the fonts, both wallpapers and
   every icon, sets up the bootloader, and builds the compositor and the
   control plane from the sources on the disc — the machine the disc is
   installed on is the one with the DRM headers and the toolchain.

   A separate data disc labelled `FINNOS` still works, and if there is
   no disc the installer falls back to the host HTTP server.

6. On a machine that was installed before the installer built the shell,
   or after a change to the sources, build and install it by hand:

   ```sh
   sudo pacman -S --needed base-devel pkgconf libdrm libwebp freetype2 alsa-lib mesa
   cd /opt/finnos && make && sudo make install
   sudo systemctl restart finn-comp finnd
   ```

## What is on the machine

- `/usr/local/bin/finn-comp` - the compositor
- `/usr/local/bin/finnd`, `finnctl` - the control plane and its client
- `/usr/local/bin/finnpkg` - the package manager
- `/usr/share/finn/wallpaper/default.webp` - the wallpaper `finn-comp` opens
- `/usr/share/finn/wallpaper/setup.webp` - the wallpaper the first-run screen draws
- `/usr/share/finn/icons/*.svg` - the icon set
- `/usr/share/finn/apps.registry` - the catalogue of apps
- `/usr/share/fonts/OTF/FixelDisplay-*.otf` - `fc-match "Fixel Display"` resolves
- `/run/finn/` - the IPC namespace: `control.sock`, `comp.sock`, `state/`

## The services

```sh
systemctl status finn-comp finnd
sudo journalctl -u finn-comp -f
sudo tail -f /var/log/finn/control.log
```

The compositor writes to the journal, never to the TTY it is painting on.
Every control-plane call is audited with the peer uid.

## Making one

`vm/create-dev-vm.sh` creates an empty VMware VM with the right shape
for the target. The FinnOS disc is attached by the script; pass an Arch
ISO as the third argument for the two-disc fallback.
