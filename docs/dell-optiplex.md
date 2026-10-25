# Dell OptiPlex local development

This is the path for running FinnOS on a real x86_64 PC rather than the dev
VM: a Dell OptiPlex (or any other x86_64 machine) is the target for the
local-dev loop once the compositor has to be judged on real hardware.

## Prerequisites

- A USB stick of at least 4 GB.
- The Arch Linux x86_64 ISO the disc is remastered from.
- Secure Boot disabled in the OptiPlex firmware (F2 at boot, then
  *Security → Secure Boot → Disabled*). The installer writes an unsigned
  GRUB, and a machine with Secure Boot on refuses it.
- The disk you are willing to erase named explicitly: the installer has
  no default disk and will not pick one.

## Build the ISO

```sh
mkdir -p .cache
curl -fsSLo .cache/archlinux-x86_64.iso \
  https://geo.mirror.pkgbuild.com/iso/latest/archlinux-x86_64.iso
vm/build-iso.sh finnos.iso
```

`vm/build-iso.sh` needs `xorriso`, `unsquashfs` and `mksquashfs`
(`squashfs-tools`). The result is a hybrid ISO that boots under UEFI and
BIOS, which matters on an OptiPlex that may be in either mode.

Check it before trusting it:

```sh
base/mkfinniso --check
vm/test-boot.sh finnos.iso
```

## Write the stick

```sh
sudo dd if=finnos.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with the whole device of the USB stick, never a
partition. On macOS use `/dev/rdiskN`.

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

The compositor is not enabled until its binary exists, so the machine boots
to a login console the first time. Build it there:

```sh
cd /opt/finnos
make
sudo make install
sudo systemctl enable --now finn-comp finnd
sudo systemctl set-default graphical.target
```

For the day-to-day loop over the network, set the dev VM's address to the
OptiPlex's and use the same commands:

```sh
FINN_HOST=finn@<optiplex-ip> make deploy
```

The installer's log uploader defaults to the VMware host address
(`192.168.44.1:8765`); override it with `FINN_HOST_URL` if you run a log
server elsewhere.
