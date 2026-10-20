#!/bin/bash
# FinnOS setup - runs INSIDE Arch VM (ARM dev or x86_64 release)
set -eux
# 1. remove any X11/GTK/Qt if present (standalone ecosystem)
pacman -Rns --noconfirm xorg-server xwayland gtk3 gtk4 qt5-base qt6-base 2>/dev/null || true
# 2. install base
pacman -Syu --noconfirm
pacman -S --noconfirm base linux linux-firmware systemd networkmanager sudo openssh mesa wayland wayland-protocols libdrm libinput seatd fontconfig ttf-dejavu git python python-pip cairo foot
# 3. finn user, no password sudo for dev
id finn 2>/dev/null || useradd -m -G wheel,input,video,seat -s /bin/bash finn
echo "finn ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/finn
# 4. enable seatd + network
systemctl enable seatd NetworkManager systemd-networkd
# 5. install finn-comp + apps + finnpkg (copied from host via shared folder or git)
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$DIR")"
install -Dm755 "$ROOT/finnpkg/finnpkg" /usr/local/bin/finnpkg
install -Dm755 "$ROOT/finn-comp/target/release/finn-comp" /usr/local/bin/finn-comp
cp -r "$ROOT/apps" /opt/finn-apps
mkdir -p /usr/share/finn/wallpaper
cp "$ROOT/assets/wallpaper/default.webp" /usr/share/finn/wallpaper/default.webp
mkdir -p /usr/share/fonts/OTF
cp "$ROOT/assets/fonts/FixelDisplay/"*.otf /usr/share/fonts/OTF/
fc-cache -f >/dev/null
fc-match "Fixel Display" || echo "WARN: Fixel Display not matched, fallback active"
# 6. systemd autologin + compositor autostart on tty1
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<'EOF'
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --autologin finn --noclear %I $TERM
EOF
cat > /home/finn/.bash_profile <<'EOF'
if [ -z "$WAYLAND_DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
  exec /usr/local/bin/finn-comp
fi
EOF
chown finn:finn /home/finn/.bash_profile
echo "FinnOS base ready. reboot."
