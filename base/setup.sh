#!/bin/bash
# FinnOS base setup — runs INSIDE the machine, with the base image already
# on its disk.
#
#   base/setup.sh                the architecture it is run on
#   base/setup.sh x86_64         the release target
#   base/setup.sh aarch64        the ARM dev machine
#
# It makes the machine FinnOS's rather than the distribution's it was
# installed from, and it takes everything from this directory: the package
# list, the pacman configuration, the units, the assets and the branding.
# What it does not do is build the shell — that is `make && make install`
# in /opt/finnos, on a machine that has the DRM headers and a display.
#
# The units it installs are the same files the disc carries; the only copy
# that is not used here is airootfs/etc/mkinitcpio.conf, which is the live
# medium's initramfs and would be wrong on a machine that boots from a
# partition.
set -eux

case "${1:-}" in
    x86_64|aarch64) ARCH="$1" ;;
    "")
        ARCH="$(uname -m)"
        if [ "$ARCH" = "arm64" ]; then ARCH=aarch64; fi
        ;;
    *) echo "usage: setup.sh [x86_64|aarch64]" >&2; exit 2 ;;
esac

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
PKGS="$HERE/packages.$ARCH"
[ -f "$PKGS" ] || { echo "no package list for $ARCH: $PKGS" >&2; exit 1; }

# The package lists are mkarchiso's format: one name per line, comments
# only at the start of a line. Read them the same way the disc does, so a
# machine and the medium cannot end up with different sets.
list() { sed -e 's/#.*//' -e 's/[[:space:]]//g' "$1" | grep -v '^$'; }

# 1. remove the second display stack if it is already on the machine. The
#    base has no X server, no Wayland compositor, no GTK and no Qt; a
#    machine that has them is running something that is not FinnOS. The
#    wayland *library* is not in this list: Mesa links it, so it is on the
#    machine by design, and removing it would only take Mesa's EGL with it.
pacman -Rns --noconfirm xorg-server xorg-server-common xorg-xwayland \
    gtk3 gtk4 gtkmm3 qt5-base qt6-base 2>/dev/null || true

# 2. the configuration: the repo list with the desktop ignored by name.
#    Kept once as a backup, because a machine whose pacman.conf is
#    FinnOS's can still be looked at to see what it was.
[ -f /etc/pacman.conf.finn-backup ] || cp /etc/pacman.conf /etc/pacman.conf.finn-backup
install -m644 "$HERE/pacman.conf" /etc/pacman.conf

# 3. the packages. --needed so a machine that is already FinnOS's is only
#    topped up, and so this can be run again after the list changes.
pacman -Syu --noconfirm
# shellcheck disable=SC2046
pacman -S --needed --noconfirm $(list "$PKGS")

# 4. the dev user: in the groups that own input, video and the seat, with
#    no password for sudo. This is the account the shell and the control
#    plane are driven from.
id finn 2>/dev/null || useradd -m -G wheel,input,video,seat,render -s /bin/bash finn
printf 'finn ALL=(ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/finn
chmod 440 /etc/sudoers.d/finn

# 5. the services the base needs before anything is drawn.
systemctl enable seatd
systemctl enable NetworkManager
systemctl enable systemd-timesyncd 2>/dev/null || true
systemctl enable sshd 2>/dev/null || true

# 6. the system units, straight from this directory.
install -Dm644 "$HERE/finn-comp.service" /etc/systemd/system/finn-comp.service
install -Dm644 "$HERE/finnd.service"     /etc/systemd/system/finnd.service
install -Dm644 "$HERE/finn-tmpfiles.conf" /usr/lib/tmpfiles.d/finn.conf
systemctl daemon-reload

# 7. the assets: the fonts, both wallpapers, every icon, and the app
#    registry the shell draws its tiles from. The compositor starts with
#    default.webp; setup.webp is the wallpaper the first-run screen shows,
#    and it is installed beside it so that screen has one on any machine.
mkdir -p /usr/share/fonts/OTF /usr/share/finn/wallpaper /usr/share/finn/icons /var/lib/finnpkg
cp "$ROOT"/assets/fonts/FixelDisplay/*.otf /usr/share/fonts/OTF/
install -m644 "$ROOT/assets/wallpaper/default.webp" /usr/share/finn/wallpaper/default.webp
install -m644 "$ROOT/assets/wallpaper/setup.webp"   /usr/share/finn/wallpaper/setup.webp
cp "$ROOT"/src/compositor/icons/*.svg /usr/share/finn/icons/
install -m755 "$ROOT/src/pkg/finnpkg" /usr/local/bin/finnpkg
install -m644 "$ROOT/assets/apps.registry" /usr/share/finn/apps.registry
echo window > /var/lib/finnpkg/installed
fc-cache -f >/dev/null
fc-match "Fixel Display" || echo "WARN: Fixel Display not matched, fallback active"
ls /usr/share/finn/icons/ | wc -l

# 8. the branding. os-release is what tools read to know what this machine
#    is; issue and motd are what a person reads at the console.
[ -f /etc/os-release.finn-backup ] || cp /etc/os-release /etc/os-release.finn-backup 2>/dev/null || true
install -m644 "$HERE/airootfs/etc/os-release" /etc/os-release
install -m644 "$HERE/airootfs/etc/issue" /etc/issue
install -m644 "$HERE/airootfs/etc/motd" /etc/motd

# The hostname is the machine's, not the image's: a fleet of machines all
# called finnos is a fleet nobody can tell apart. It is set only when it
# still has the name the installer gave it, and FINN_KEEP_HOSTNAME=1 says
# to leave even that alone.
if [ "${FINN_KEEP_HOSTNAME:-0}" = "1" ]; then
    echo "keeping the hostname: $(cat /etc/hostname 2>/dev/null || hostname)"
elif [ "$(cat /etc/hostname 2>/dev/null || echo finnos)" = "finnos" ]; then
    hostnamectl set-hostname finnos 2>/dev/null || echo finnos > /etc/hostname
    echo "hostname set to finnos"
else
    echo "hostname left as $(hostname): it is not the default one"
fi

# 9. autologin on tty1, as the finn user. The compositor takes the console
#    from there: finn-comp.service conflicts with getty@tty1 and comes up
#    first, so the login is what is left when the shell is not running.
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<'EOF'
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --autologin finn --noclear %I $TERM
EOF
chmod 644 /etc/systemd/system/getty@tty1.service.d/autologin.conf

# 10. the sources, so the machine can build the compositor and the control
#     plane. This is the machine with the DRM headers.
mkdir -p /opt/finnos
for d in src assets ui apps sdk docs base vm; do
    cp -r "$ROOT/$d" /opt/finnos/ 2>/dev/null || true
done
for f in Makefile README.md NAMING.md; do
    cp "$ROOT/$f" /opt/finnos/ 2>/dev/null || true
done
chown -R finn:finn /opt/finnos

# The units are installed but not enabled: enabling a unit whose binary is
# not on the machine is a machine that fails at every boot. Build first,
# then `systemctl enable --now finn-comp finnd`.
echo "FinnOS base ready on $ARCH."
echo "Build the shell: cd /opt/finnos && make && sudo make install"
echo "Then enable it:  sudo systemctl enable --now finn-comp finnd"
