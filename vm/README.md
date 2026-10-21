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

To put the base itself on the machine - the package set, the units, the
icons, the branding - run `sudo base/setup.sh` on it, which is what an
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

The FinnOS ISO is bootable: it is the Arch live environment with the
FinnOS tree inside it at `/opt/finnos`. One disc, no host HTTP server.
The live root's branding, its installer wrapper and the hook that starts
the installer come from `base/airootfs`, so the disc built this way and
the one `base/mkfinniso` builds behave the same.

1. Download the Arch Linux ISO the disc is remastered from:

   ```sh
   curl -Lo .cache/archlinux-x86_64.iso \
     https://geo.mirror.pkgbuild.com/iso/latest/archlinux-x86_64.iso
   ```

2. Build the FinnOS ISO on the host or any Linux machine:

   ```sh
   vm/build-iso.sh
   ```

   It needs `xorriso`, `unsquashfs` and `mksquashfs`. The boot images and
   the volume label are copied from the Arch ISO, so the result boots
   under UEFI (VMware) and BIOS. Both boot menus are rewritten: the
   default entry is **Install FinnOS**, which boots the live environment
   and hands over to the installer, and **FinnOS live shell** is the way
   in without installing.

   This remasters the Arch live environment with the FinnOS tree inside
   it, and it is the one that runs where there is no Arch machine. On an
   Arch machine there is a native build, which writes the medium from
   `base/` alone - the profile, the package list, the live root and the
   three boot menus:

   ```sh
   sudo base/mkfinniso finnos.iso
   ```

   Either builder is checked beforehand with `base/mkfinniso --check`.
   `base/README.md` is the account of what each of them is and needs.

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
