#!/bin/bash
# FinnOS chroot config (runs inside /mnt via arch-chroot)
exec > >(tee /tmp/finn-chroot.log) 2>&1
set -x
HOST=http://10.211.55.2:8765
post() { curl -s -X POST --data-binary @/tmp/finn-chroot.log "$HOST/log" -o /dev/null 2>/dev/null || true; }

echo "=== base config ==="
ln -sf /usr/share/zoneinfo/UTC /etc/localtime
hwclock --systohc 2>/dev/null || true
echo "en_US.UTF-8 UTF-8" > /etc/locale.gen
locale-gen
echo "LANG=en_US.UTF-8" > /etc/locale.conf
echo "finnos" > /etc/hostname
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

echo "=== autologin + compositor autostart ==="
mkdir -p /etc/systemd/system/getty@tty1.service.d
printf '[Service]\nExecStart=\nExecStart=-/usr/bin/agetty --autologin finn --noclear %%I $TERM\n' > /etc/systemd/system/getty@tty1.service.d/autologin.conf
cat > /home/finn/.bash_profile <<'EOF'
if [ -z "$WAYLAND_DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
  exec /usr/local/bin/finn-comp
fi
EOF
chown finn:finn /home/finn/.bash_profile
post

echo "=== Fixel Display fonts ==="
mkdir -p /usr/share/fonts/OTF
for f in FixelDisplay-Regular FixelDisplay-Medium FixelDisplay-SemiBold FixelDisplay-Bold; do
  curl -sLo "/usr/share/fonts/OTF/$f.otf" "$HOST/assets/fonts/FixelDisplay/$f.otf"
done
ls -la /usr/share/fonts/OTF/
fc-cache -f >/dev/null 2>&1 || true
fc-match "Fixel Display" || true
post

echo "=== wallpaper + apps + finnpkg ==="
mkdir -p /usr/share/finn/wallpaper /opt/finn-apps /usr/local/bin
curl -sLo /usr/share/finn/wallpaper/default.webp "$HOST/assets/wallpaper/default.webp"
for a in Desktop _tokens _targets; do curl -sLo "/opt/finn-apps/$a.fui" "$HOST/ui/$a.fui"; done
for a in launcher/Launcher settings/Settings files/Files; do
  mkdir -p "/opt/finn-apps/$(dirname $a)"
  curl -sLo "/opt/finn-apps/$a.fui" "$HOST/apps/$a.fui"
done
curl -sLo /usr/local/bin/finnpkg "$HOST/finnpkg/finnpkg" && chmod +x /usr/local/bin/finnpkg
ls -R /opt/finn-apps
post

echo "=== finn-comp (boot placeholder until Rust build) ==="
cat > /usr/local/bin/finn-comp <<'EOF'
#!/bin/bash
# FinnOS compositor placeholder - proves the shell handoff works on boot.
clear
echo "finn-comp 0.1 - FinnOS"
echo "target: Desktop | wallpaper: /usr/share/finn/wallpaper/default.webp"
echo "font: $(fc-match -f '%{family}' 'Fixel Display' 2>/dev/null || echo Fixel)"
echo
echo "layers: wallpaper < windows < dock < overlay"
echo "Super+Space = launcher (stub)"
exec sleep infinity
EOF
chmod +x /usr/local/bin/finn-comp
post

echo "=== initramfs + bootloader ==="
mkinitcpio -P 2>&1 | tail -5
post
ROOTUUID=$(blkid -s UUID -o value /dev/sda2)
echo "ROOTUUID=$ROOTUUID"
KERNEL=/Image
[ -f /boot/Image ] || KERNEL=/vmlinuz-linux
echo "KERNEL=$KERNEL"
ls -la /boot/ | head -20
if grub-install --target=arm64-efi --efi-directory=/boot --bootloader-id=FinnOS --removable 2>&1 | tail -5; then
  grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5
  echo "BOOTLOADER=grub"
else
  bootctl --esp-path=/boot install 2>&1 | tail -5 || true
  mkdir -p /boot/loader/entries
  printf 'default finnos.conf\ntimeout 3\n' > /boot/loader/loader.conf
  printf 'title FinnOS\nlinux %s\ninitrd /initramfs-linux.img\noptions root=UUID=%s rw quiet loglevel=3\n' "$KERNEL" "$ROOTUUID" > /boot/loader/entries/finnos.conf
  echo "BOOTLOADER=systemd-boot"
fi
post

curl -s -X POST --data-binary @/tmp/finn-chroot.log "$HOST/log" -o /dev/null 2>/dev/null || true
echo "FINN_CHROOT_COMPLETE"
