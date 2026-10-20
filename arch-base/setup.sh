#!/bin/bash
# FinnOS base setup — runs INSIDE the Arch VM, after the base image is on
# the disk.
#
#   arch-base/setup.sh            the dev VM: ARM64
#   arch-base/setup.sh x86_64     the release target
#
# It does four things, in order: make the machine FinnOS's rather than
# Arch's, put the system units in place, put the assets in place, and make
# the compositor come up on the console.
set -eux

ARCH="${1:-aarch64}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"

# 1. remove any X11/GTK/Qt if present (standalone ecosystem)
pacman -Rns --noconfirm xorg-server xwayland gtk3 gtk4 qt5-base qt6-base 2>/dev/null || true

# 2. install base
pacman -Syu --noconfirm
pacman -S --noconfirm base linux linux-firmware systemd networkmanager sudo openssh \
    mesa gbm egl libdrm libinput seatd fontconfig ttf-dejavu ttf-liberation \
    python python-pip git tar base-devel pkgconf

# 3. finn user, no password sudo for dev
id finn 2>/dev/null || useradd -m -G wheel,input,video,seat -s /bin/bash finn
echo "finn ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/finn
chmod 440 /etc/sudoers.d/finn

# 4. enable seatd + network
systemctl enable seatd NetworkManager systemd-networkd

# 5. the system units
install -Dm644 "$HERE/finn-comp.service" /etc/systemd/system/finn-comp.service
install -Dm644 "$HERE/finnd.service"      /etc/systemd/system/finnd.service
install -Dm644 "$HERE/finn-tmpfiles.conf" /usr/lib/tmpfiles.d/finn.conf
install -Dm755 "$ROOT/src/pkg/finnpkg"    /usr/local/bin/finnpkg
systemctl daemon-reload
systemctl enable finn-comp finnd

# 6. the assets: fonts, wallpaper, icons, the app registry
mkdir -p /usr/share/fonts/OTF /usr/share/finn/wallpaper /usr/share/finn/icons /var/lib/finnpkg
cp "$ROOT/assets/fonts/FixelDisplay/"*.otf /usr/share/fonts/OTF/
cp "$ROOT/assets/wallpaper/default.webp" /usr/share/finn/wallpaper/default.webp
cp "$ROOT/src/compositor/icons/"*.svg     /usr/share/finn/icons/
install -m644 "$ROOT/assets/apps.registry" /usr/share/finn/apps.registry
fc-cache -f >/dev/null
fc-match "Fixel Display" || echo "WARN: Fixel Display not matched, fallback active"

# 7. the shell's data: the icons' own directory, and the install list the
#    package manager owns
chown -R root:finn /usr/share/finn /var/lib/finnpkg
chmod 755 /usr/share/finn /var/lib/finnpkg

# 8. autologin on tty1, and the compositor takes the console
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<'EOF'
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --autologin finn --noclear %I $TERM
EOF

# 9. the sources, so the machine can build the compositor
mkdir -p /opt/finnos
cp -r "$ROOT/src" "$ROOT/assets" "$ROOT/ui" "$ROOT/apps" /opt/finnos/ 2>/dev/null || true
chown -R finn:finn /opt/finnos

echo "FinnOS base ready on $ARCH. Build with: cd /opt/finnos && make && sudo make install"
