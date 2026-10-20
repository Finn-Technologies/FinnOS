#!/bin/bash
# FinnOS ARM64 install: archboot live env -> /dev/sda
exec > >(tee /tmp/finn-install.log) 2>&1
set -x
DISK=/dev/sda
HOST=http://10.211.55.2:8765
post() { curl -s -X POST --data-binary @/tmp/finn-install.log "$HOST/log" -o /dev/null 2>/dev/null || true; }

echo "===== [1/8] PREREQS ====="
pacman -Sy --noconfirm arch-install-scripts dosfstools parted 2>&1 | tail -5 || {
  sed -i 's/^SigLevel.*/SigLevel = Never/' /etc/pacman.conf
  pacman -Sy --noconfirm arch-install-scripts dosfstools parted 2>&1 | tail -5
}
command -v pacstrap mkfs.vfat
post

echo "===== [2/8] PARTITION $DISK ====="
umount -R /mnt 2>/dev/null || true
swapoff -a 2>/dev/null || true
wipefs -a "$DISK" || true
printf 'label: gpt\n,512M,U\n,,L\n' | sfdisk --wipe=always "$DISK"
sync; partprobe "$DISK" 2>/dev/null || true; sleep 3
lsblk "$DISK"
post

echo "===== [3/8] FORMAT ====="
mkfs.vfat -F32 "${DISK}1"
mkfs.ext4 -F -L finnos "${DISK}2"
post

echo "===== [4/8] MOUNT ====="
mkdir -p /mnt
mount "${DISK}2" /mnt
mkdir -p /mnt/boot
mount "${DISK}1" /mnt/boot
findmnt -o TARGET,SOURCE /mnt /mnt/boot
post

echo "===== [5/8] PACSTRAP ====="
yes | pacstrap /mnt base linux linux-firmware \
  systemd sudo openssh networkmanager \
  grub efibootmgr \
  mesa wayland wayland-protocols libdrm libinput seatd \
  fontconfig ttf-dejavu git python vim nano
post

echo "===== [6/8] FSTAB ====="
genfstab -U /mnt >> /mnt/etc/fstab
cat /mnt/etc/fstab
post

echo "===== [7/8] CHROOT ====="
mkdir -p /mnt/root
curl -sLo /mnt/root/finn-chroot.sh "$HOST/vm/finn-chroot.sh"
chmod +x /mnt/root/finn-chroot.sh
arch-chroot /mnt /root/finn-chroot.sh
post

echo "===== [8/8] COMPLETE ====="
curl -s -X POST --data-binary @/tmp/finn-install.log "$HOST/log" -o /dev/null 2>/dev/null || true
echo "FINN_INSTALL_COMPLETE"
