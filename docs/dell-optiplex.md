# Dell OptiPlex local development

This is the path for running FinnOS on a real x86_64 PC rather than the dev
VM: a Dell OptiPlex (or any other x86_64 machine) is the target for the
local-dev loop once the compositor has to be judged on real hardware.

## Prerequisites

- A USB stick of at least 4 GB (the whole device will be erased).
- Secure Boot disabled in the OptiPlex firmware (F2 at boot, then
  *Security → Secure Boot → Disabled*). The installer writes an unsigned
  GRUB, and a machine with Secure Boot on refuses it.
- The disk you are willing to erase named explicitly: the installer has
  no default disk and will not pick one.

No Arch ISO is downloaded: the FinnOS ISO is built natively from the
`base/` tree in this repo (profile, package lists, live root, boot
menus) with the FinnOS sources at `/opt/finnos`. The base is
source-level Arch — a modified package set and configuration — not a
remaster of a downloaded image. See `base/README.md`.

## Build the ISO

On an Arch machine (or the OptiPlex once it runs FinnOS):

```sh
base/mkfinniso --check
sudo base/mkfinniso finnos.iso
```

It needs root, the `archiso` package, and the build-host tools named in
`base/README.md` (`syslinux` for BIOS, `mtools`/`dosfstools` for UEFI,
`squashfs-tools` + `xorriso` for the image). The result is a hybrid ISO
that boots under UEFI and BIOS, which matters on an OptiPlex that may
be in either mode.

Where there is no Arch machine, CI builds the exact same disc in a
privileged Arch container — see `.github/workflows/iso.yml` — and the
release carries the ISO as an artifact. Never write a stick from a
third-party Arch ISO: only a `mkfinniso` disc is FinnOS.

Check it before trusting it:

```sh
base/mkfinniso --check
vm/test-boot.sh finnos.iso
vm/menu-shot.sh finnos.iso /tmp/menu
```

## Write the stick (bootable USB)

The ISO is a hybrid BIOS+UEFI image: a raw copy makes a bootable stick.
Copy the whole device, never a partition, and eject before unplugging.

Linux:

```sh
lsblk -dno NAME,SIZE,MODEL,TRAN
sudo dd if=finnos.iso of=/dev/sdX bs=4M status=progress conv=fsync
echo $?
sudo eject /dev/sdX || sudo umount /dev/sdX* || true
```

macOS (the stick was connected to a Mac):

```sh
diskutil list external
STICK=/dev/diskN   # the whole USB device, not diskNs1
sudo diskutil unmountDisk "$STICK"
sudo dd if=finnos.iso of=/dev/rdiskN bs=4m status=progress
sudo diskutil eject "$STICK"
```

Verify before booting (size + El Torito):

```sh
ls -lh finnos.iso
xorriso -indev finnos.iso -report_el_torito plain
```

A `dd` that is interrupted leaves a half-written stick: re-run the full
copy, do not resume it.

## Boot and install

1. Plug the stick into the OptiPlex and boot it (F12 for the one-time boot
   menu). Choose the USB entry; either the UEFI or the BIOS one works.
2. The disc boots into the live environment and, on the default entry,
   hands over to `finn-install` after ten seconds. Press Ctrl-C to stay in
   the shell instead.
3. Find the disk:

   ```sh
   lsblk -dno NAME,SIZE,TYPE,MODEL
   ```

   An OptiPlex with an NVMe SSD is `/dev/nvme0n1`; a SATA disk is
   `/dev/sda`.
4. Run the installer against the named disk:

   ```sh
   finn-install --disk /dev/nvme0n1 --yes
   ```

   The installer partitions the disk (512M ESP, 1M BIOS boot, the rest
   ext4 labelled `finnos`), pacstraps the base, installs GRUB for both
   UEFI and BIOS, and runs `base/setup.sh`.
5. Reboot when it prints `FINN_INSTALL_COMPLETE`.

## After the install

The installer builds the shell on the machine (`/opt/finnos` →
`/usr/local/bin/finn-comp`), enables `finn-comp` + `finnd`, and sets
graphical boot — so a finished install (`FINN_INSTALL_COMPLETE` +
`FINN_CHROOT_COMPLETE`) reboots straight into FinnOS. No manual build.

If the build failed on the target (no network for deps, toolchain
missing), it boots to a login console instead. Then, on the machine:

```sh
cd /opt/finnos
make
sudo make install
sudo systemctl enable --now finn-comp finnd
sudo systemctl set-default graphical.target
systemctl status finn-comp finnd
```

For the day-to-day loop over the network, set the dev VM's address to the
OptiPlex's and use the same commands:

```sh
FINN_HOST=finn@<optiplex-ip> make deploy
```

The installer's log uploader defaults to the VMware host address
(`192.168.44.1:8765`); override it with `FINN_HOST_URL` if you run a log
server elsewhere.
