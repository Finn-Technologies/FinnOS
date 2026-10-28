# Installing FinnOS

This page takes you from a checkout to a machine that boots to the FinnOS
desktop. It assumes an x86_64 PC; the ARM image exists for development and is
built and deployed differently (see [development.md](development.md)).

The short version:

```sh
base/mkfinniso --check
sudo base/mkfinniso finnos.iso
sudo dd if=finnos.iso of=/dev/sdX bs=4M status=progress conv=fsync
# boot the stick, choose Install FinnOS, name the disk
```

The rest of the page is the detail, including the parts that go wrong.

## Before you start

| Need | Why |
|---|---|
| An x86_64 PC | the release target; the image is a general profile, not a model-specific one |
| A 4 GB USB stick | the whole device is erased; warnings about partitions do not apply |
| A wired or working wireless network | the installer pacstraps the base and builds the shell from source |
| Secure Boot **off** | the installer writes an unsigned GRUB or systemd-boot; firmware Secure Boot refuses it |
| A disk you are willing to erase | the installer has no default and will not pick one |

The reference machine, its firmware menu and its verified status are in
[dell-optiplex.md](dell-optiplex.md). The hardware the image installs for, and
the limits it does not hide, are in [hardware.md](hardware.md).

## Build the ISO

The disc is built from this tree. No Arch ISO is downloaded, no base image is
remastered, and the sources travel on the disc at `/opt/finnos`.

### On an Arch machine

```sh
base/mkfinniso --check
sudo base/mkfinniso finnos.iso
```

`mkfinniso` needs root, the `archiso` package, and the build-host tools:

| Tool | For |
|---|---|
| `archiso` | `mkarchiso`, the profile and the live root |
| `syslinux` | the BIOS boot sector and menu |
| `mtools`, `dosfstools` | the EFI system partition |
| `squashfs-tools` | the live root |
| `xorriso` | the hybrid ISO |

`mkfinniso --check` runs anywhere and checks the profile, the boot menus
against the profile's label and layout, the package lists, the `IgnorePkg`
guard rail, the duplicate unit copies and that nothing on the disc names another
distribution. It is what CI runs. Run it before you trust a disc.

### In CI

`.github/workflows/iso.yml` builds the same native image in a privileged
`archlinux:latest` container, because a GitHub runner is not an Arch machine.
A tag `v<version>` publishes the ISO to the GitHub Release the OTA frontend
reads. The workflow can also be run by hand, with an optional full install to a
disk.

### Without archiso

There is an `--mode=assemble` path that builds the live root and the menus from
the files here, but it needs a kernel and an initramfs that can find an archiso
disc — the `archiso` hook does that, and nothing in this tree can build one
without the archiso package. It refuses rather than writing a disc that stops at
the kernel. See [base/README.md](../base/README.md).

## Write the stick

The ISO is a hybrid BIOS+UEFI image: a raw copy makes a bootable stick. Copy the
whole device, never a partition, and eject before unplugging.

### Linux

```sh
lsblk -dno NAME,SIZE,MODEL,TRAN
sudo dd if=finnos.iso of=/dev/sdX bs=4M status=progress conv=fsync
echo $?
sudo eject /dev/sdX || sudo umount /dev/sdX* || true
```

### macOS

```sh
diskutil list external
STICK=/dev/diskN          # the whole USB device, not diskNs1
sudo diskutil unmountDisk "$STICK"
sudo dd if=finnos.iso of=/dev/rdiskN bs=4m status=progress
sudo diskutil eject "$STICK"
```

### Check it before booting

```sh
ls -lh finnos.iso
xorriso -indev finnos.iso -report_el_torito plain
```

A `dd` that is interrupted leaves a half-written stick: re-run the full copy,
do not resume it.

## Boot the disc

Use the firmware's one-time boot menu — F12 on a Dell — and choose the USB
entry. Either the UEFI or the BIOS entry works; the disc carries both syslinux
(BIOS) and systemd-boot (UEFI). It does not carry a GRUB boot path: GRUB
belongs to the installed machine, where the installer writes its menu.

The menu has two entries:

| Entry | What it does |
|---|---|
| **Install FinnOS** | boots the live environment and hands the console to `finn-install` after ten seconds. Press Ctrl-C to stay in the shell |
| **FinnOS live shell** | the same live environment, no installer. This is the entry for looking at a machine before erasing it, or for driving an install by hand |

Both entries put the kernel on `tty0` and mirror the console to `ttyS0`, so a
serial cable shows the boot and the installer instead of a blank screen. The
live environment turns systemd's first-boot wizard off; the disc is not a
machine to configure, it is a machine that installs one.

The live shell runs as root. The disc carries an uninitialized `machine-id` and
a read-only squashfs, which is why the keyring is initialized at boot and
`/etc/pacman.d/gnupg` is mounted on a tmpfs: without that, the first signed
`pacman -Sy` fails.

## Run the installer

Find the disk:

```sh
lsblk -dno NAME,SIZE,TYPE,MODEL
```

An NVMe disk is `/dev/nvme0n1`; a SATA disk is `/dev/sda`; a USB stick you do
**not** want is also a `/dev/sdX`. Then:

```sh
finn-install --disk /dev/nvme0n1          # asks for the path again
finn-install --disk /dev/sda --yes        # unattended, no second question
```

There is no default disk, and the interactive install asks you to type the path
again before anything is written. That is not ceremony: the one thing an
installer must never do is erase a disk nobody named.

The installer resolves the device first, so `/dev/disk/by-id/...` works, and
refuses:

- a partition rather than a whole disk;
- a disk that backs `/`, `/boot`, `/mnt` or the running live medium;
- a disk with a mounted partition or swap on it.

### What it does

| Step | What happens |
|---|---|
| 1 | validate the disk, and ask again unless `--yes` |
| 2 | partition: a 512 MiB EFI system partition, a 1 MiB BIOS boot partition, and the rest as ext4 labelled `finnos` |
| 3 | format and check the filesystems |
| 4 | mount the new root and the ESP, and verify that those are what is mounted |
| 5 | pacstrap the base package list; enable `[multilib]` before Wine |
| 6 | copy the tree to `/opt/finnos` and run the chroot: users, passwords, locale, hostname, units, fonts, wallpapers, icons, registry, bootloader |
| 7 | build the compositor and the control plane on the machine — the machine with the DRM headers and the toolchain is the one being installed |
| 8 | look at the disk that was written, then print `FINN_INSTALL_COMPLETE` |

The final check is not the installer's own opinion: `/usr/local/bin/finn-comp`
must be executable on the new root, and a boot entry must name the root by its
UUID. Only then is the install complete.

Partition names follow the device: `nvme0n1p1`, `mmcblk0p1`, `sda1`. The
bootloader is GRUB for UEFI, with systemd-boot as the fallback when GRUB cannot
register its EFI entry; BIOS uses GRUB's boot code in the MBR.

### The password

Without `FINN_PASSWORD`, the chroot generates one for `root` and for `finn` and
prints it once at the end of the install. With it, that value is used and does
not appear on a command line. The password is not on the disc.

`sshd` is installed and left disabled. A network login is a decision somebody
makes on the machine, not a default.

### Finishing

When the installer prints `FINN_INSTALL_COMPLETE`, it has checked the disk and
unmounted it. Power the machine off, remove the install medium, and boot the
disk. The installer does not reboot for you.

### If it stops

The installer writes `/tmp/finn-install.log` and mirrors its output to the
console it was started from. It also posts progress to a log server at
`FINN_HOST_URL` (default `http://192.168.44.1:8765`); that is a convenience for
the dev VM and is harmless when no server is listening.

Every failure is a message on the console and a non-zero exit, not a quiet
half-install. The common ones are:

| Message | Cause |
|---|---|
| `no partition table` | the disk was not writable, or it is not the disk you think |
| `the partition nodes did not appear` | udev did not settle; check `lsblk`, then retry |
| `mkfs.vfat failed` | the ESP is mounted somewhere else |
| `not enough free disk space` (pacman) | the package list plus the build does not fit; 8 GB is the comfortable minimum |
| `pacman` signature failure | the keyring did not initialize; the installer stops rather than installing unverified packages |

## First boot

The installed machine comes up on `graphical.target`, with `finn-comp` on tty1
and `finnd` behind it. The first boot shows the setup screen: the language, the
appearance and a start button. Setup is a gate: `/var/lib/finn/setup-done` is
written atomically and only after the language is saved, so an interrupted
setup opens again next boot. `FINN_SETUP_SKIP=1` is the development escape.

`Ctrl+Alt+F2` is the rescue console (`getty@tty2`). tty1 belongs to the
compositor, so tty1 is not a way in.

If the build on the target failed — no network for dependencies, a toolchain
missing — the machine boots to a login console instead, and says so. On the
machine:

```sh
cd /opt/finnos
make
sudo make install
sudo systemctl enable --now finn-comp finnd
sudo systemctl set-default graphical.target
systemctl status finn-comp finnd
```

## After the install

```sh
# a network login, if you want one
sudo systemctl enable --now sshd

# the day-to-day dev loop from another machine
VM=finn@<address> make deploy

# the runtime update check
finnctl ota.check
```

The installed machine has no known default password, `sudo` asks for the
password, and `sshd` is off until someone turns it on. That is the intended
state; do not "fix" it by shipping a default account.

The runtime package update path is in [updates.md](updates.md). The kernel,
firmware and bootloader still move with `pacman -Syu`; the OTA path replaces
the userland.

## Unattended installs

A machine with no screen — a CI runner, a server, an install over a serial
cable — can name the disk on the kernel command line:

```
finn.disk=/dev/vda finn.yes
```

The kernel command line is set before boot by whoever can edit the boot
entry — which is the same authority that can already erase the disk — and
`finn.disk` is treated exactly like `--disk`. Without it the install is
interactive. This is how the CI full-install job drives the installer, and how
the serial-console install works.

## Recovery

### The installed system does not boot

Boot the stick again, choose **FinnOS live shell**, and inspect the disk:

```sh
lsblk -f
blkid
# mount the root and the ESP, then chroot
mount /dev/nvme0n1p3 /mnt
mount /dev/nvme0n1p1 /mnt/boot
arch-chroot /mnt
```

Then reinstall the bootloader (`grub-install` and `grub-mkconfig` for UEFI, or
`grub-install --target=i386-pc` for BIOS), check `/etc/fstab` against `blkid`,
and check the kernel and initramfs are present. The installer's own log is at
`/tmp/finn-install.log` on the live medium, not on the installed disk.

### The desktop does not come up but the machine does

`Ctrl+Alt+F2`, log in as `finn` or `root`, and read:

```sh
systemctl status finn-comp finnd
sudo journalctl -u finn-comp -n 100 --no-pager
sudo journalctl -u finnd -n 100 --no-pager
```

The most common causes are no DRM device (a VM without 3D, or a GPU the kernel
did not bind), a missing font, and a stale binary. The compositor has a start
limit and `finn-failure.service`, so a crash loop stops and says so rather than
resetting the console forever. See [troubleshooting.md](troubleshooting.md).

### Start over

The installer is destructive by design and there is no "repair" mode. Boot the
stick, name the disk, and install again. If you want to keep the old system,
do not: there is no upgrade-in-place path from an older FinnOS, and the OTA
path only replaces the runtime package on a machine that already boots.

## What "installed" proves

A finished install proves that the disc booted, the installer wrote the disk it
was given, the base packages installed, the sources built on the target, the
bootloader registered, and a boot entry names the root by UUID.

It does not prove that the desktop renders correctly on your panel, that your
wireless adapter works, or that every app will launch. Those are hardware
observations; make them, and report what you saw — including what you did not
test.
