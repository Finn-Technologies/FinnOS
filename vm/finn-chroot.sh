#!/bin/bash
# FinnOS chroot config - runs inside /mnt via arch-chroot.
#
# It lays down the base image: the user, the services, the fonts, the
# assets and the sources, and then builds the compositor and the control
# plane from those sources. The build happens here, in the chroot, because
# the machine the disc is installed on is the one with the DRM headers and
# the toolchain.
#
# The payload comes from /root/finnos-src, which the installer copied off
# the install disc. If that is not there, it is fetched from the host.
set -x
HOST=http://192.168.44.1:8765
SRC=/root/finnos-src
post() { curl -s -X POST --data-binary @/tmp/finn-chroot.log "$HOST/log" -o /dev/null 2>/dev/null || true; }

# get <destination> <path-in-the-tree>
get() {
  if [ -f "$SRC/$2" ]; then
    install -D -m 644 "$SRC/$2" "$1"
  else
    curl -sLo "$1" "$HOST/$2"
  fi
}

echo "=== base config ==="
ln -sf /usr/share/zoneinfo/UTC /etc/localtime
hwclock --systohc 2>/dev/null || true
echo "en_US.UTF-8 UTF-8" > /etc/locale.gen
locale-gen
echo "LANG=en_US.UTF-8" > /etc/locale.conf
# What the machine calls itself comes from base/, the same files the disc
# carries and base/setup.sh installs: one copy of "what this machine is"
# rather than a second one written here. The pacman configuration is the
# base's too, so the desktop the base refuses is refused on the machine as
# well, and not only on the medium it was installed from.
get /etc/hostname base/airootfs/etc/hostname
get /etc/os-release base/airootfs/etc/os-release
get /etc/issue base/airootfs/etc/issue
get /etc/motd base/airootfs/etc/motd
get /etc/pacman.conf base/pacman.conf
printf '127.0.0.1 localhost\n::1 localhost\n127.0.1.1 finnos.localdomain finnos\n' > /etc/hosts
echo "root:finn" | chpasswd
post

echo "=== user ==="
id finn >/dev/null 2>&1 || useradd -m -G wheel,input,video,seat -s /bin/bash finn
echo "finn:finn" | chpasswd
printf 'finn ALL=(ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/finn
chmod 440 /etc/sudoers.d/finn
post

echo "=== services ==="
systemctl enable NetworkManager systemd-networkd sshd 2>/dev/null || true
systemctl enable seatd 2>/dev/null || true
systemctl enable systemd-timesyncd 2>/dev/null || true
post

echo "=== autologin on tty1 ==="
mkdir -p /etc/systemd/system/getty@tty1.service.d
printf '[Service]\nExecStart=\nExecStart=-/usr/bin/agetty --autologin finn --noclear %%I $TERM\n' > /etc/systemd/system/getty@tty1.service.d/autologin.conf
post

echo "=== Fixel Display fonts ==="
mkdir -p /usr/share/fonts/OTF
for f in FixelDisplay-Regular FixelDisplay-Medium FixelDisplay-SemiBold FixelDisplay-Bold; do
  get "/usr/share/fonts/OTF/$f.otf" "assets/fonts/FixelDisplay/$f.otf"
done
ls -la /usr/share/fonts/OTF/
fc-cache -f >/dev/null 2>&1 || true
fc-match "Fixel Display" || true
post

echo "=== the payload: wallpaper, icons, registry, the package manager ==="
mkdir -p /usr/share/finn/wallpaper /usr/share/finn/icons /var/lib/finnpkg /usr/local/bin
get /usr/share/finn/wallpaper/default.webp assets/wallpaper/default.webp
get /usr/share/finn/wallpaper/setup.webp assets/wallpaper/setup.webp
get /usr/share/finn/apps.registry assets/apps.registry
echo window > /var/lib/finnpkg/installed
# Every icon, when the payload is the tree. A list of names is a second
# place to forget one, and an icon the shell cannot find is a tile drawn
# as an empty square. The list below is only for the host-server flow,
# where the tree is not on the disc: it is written out to match what is
# there today, and the tree copy is the one that is complete.
if compgen -G "$SRC/src/compositor/icons/*.svg" >/dev/null 2>&1; then
  for f in "$SRC"/src/compositor/icons/*.svg; do
    install -D -m644 "$f" "/usr/share/finn/icons/$(basename "$f")"
  done
else
  for icon in accessibility battery battery_bolt bluetooth chevron_down chevron_right \
              code cursor edit ethernet files language moon notes settings start_button \
              store studio sun sync terminal vault volume wifi window_close \
              window_expand window_minimize; do
    get "/usr/share/finn/icons/$icon.svg" "src/compositor/icons/$icon.svg"
  done
fi
get /usr/local/bin/finnpkg src/pkg/finnpkg
chmod +x /usr/local/bin/finnpkg
chown -R root:finn /usr/share/finn /var/lib/finnpkg
ls /usr/share/finn/icons/
post

echo "=== the system units ==="
get /etc/systemd/system/finn-comp.service base/finn-comp.service
get /etc/systemd/system/finnd.service base/finnd.service
get /usr/lib/tmpfiles.d/finn.conf base/finn-tmpfiles.conf
systemctl daemon-reload
systemctl enable finn-comp finnd 2>/dev/null || true
post

echo "=== the sources, so the machine can build the compositor ==="
mkdir -p /opt/finnos
if [ -d "$SRC" ]; then
  tar -C "$SRC" -cf - . | tar -C /opt/finnos -xf -
  chown -R finn:finn /opt/finnos
  echo "SOURCES=installed"
else
  if curl -sfLo /tmp/finnos.tar.gz "$HOST/finnos.tar.gz"; then
    tar -C /opt/finnos -xzf /tmp/finnos.tar.gz
    chown -R finn:finn /opt/finnos
    echo "SOURCES=installed"
  else
    echo "WARN: the sources were not available; push them with vm/deploy.sh"
    echo "SOURCES=missing"
  fi
fi
post

echo "=== build the shell and the control plane ==="
# The disc carries the sources; the machine they land on is the one that
# has the DRM headers and the toolchain, so the build happens here. Without
# it the machine would boot with no compositor to start.
if [ -d /opt/finnos ]; then
  cd /opt/finnos
  if make -j"$(nproc)" >/tmp/finn-build.log 2>&1; then
    if make install >>/tmp/finn-build.log 2>&1; then
      echo "SHELL=built"
    else
      echo "WARN: the install failed; see /tmp/finn-build.log"
      tail -20 /tmp/finn-build.log
      echo "SHELL=notinstalled"
    fi
  else
    echo "WARN: the build failed; see /tmp/finn-build.log"
    tail -20 /tmp/finn-build.log
    echo "SHELL=notbuilt"
  fi
else
  echo "WARN: no sources to build; the shell will not start"
  echo "SHELL=nosources"
fi
chown -R finn:finn /opt/finnos
post

echo "=== initramfs + bootloader ==="
mkinitcpio -P 2>&1 | tail -5
post
ROOTUUID=$(blkid -s UUID -o value /mnt)
echo "ROOTUUID=$ROOTUUID"
KERNEL=vmlinuz-linux
ls -la /boot/ | head -20
if grub-install --target=x86_64-efi --efi-directory=/boot --bootloader-id=FinnOS --removable 2>&1 | tail -5; then
  grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5
  echo "BOOTLOADER=grub-efi"
else
  bootctl --esp-path=/boot install 2>&1 | tail -5 || true
  mkdir -p /boot/loader/entries
  printf 'default finnos.conf\ntimeout 3\n' > /boot/loader/loader.conf
  printf 'title FinnOS\nlinux %s\ninitrd /initramfs-linux.img\noptions root=UUID=%s rw quiet loglevel=3\n' "$KERNEL" "$ROOTUUID" > /boot/loader/entries/finnos.conf
  echo "BOOTLOADER=systemd-boot"
fi
# A machine that starts with a BIOS rather than EFI needs the boot code
# in the MBR as well: the EFI loader on the partition is not enough, and
# the install would be on the disk with no way to reach it. On a GPT disk
# GRUB keeps that code in the BIOS boot partition the installer made.
if grub-install --target=i386-pc /dev/sda 2>&1 | tail -5; then
  grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5
  echo "BOOTLOADER=grub-bios"
else
  echo "WARN: no BIOS boot code; this machine has to start with EFI"
fi
post

curl -s -X POST --data-binary @/tmp/finn-chroot.log "$HOST/log" -o /dev/null 2>/dev/null || true
# The bootloader is in place either way, so the machine boots; but a
# machine without a shell is not a finished install, and the installer has
# to hear that from here.
if [ "$SHELL" != "built" ]; then
  echo "FINN_CHROOT_FAILED: the shell was not built"
  exit 1
fi
echo "FINN_CHROOT_COMPLETE"
