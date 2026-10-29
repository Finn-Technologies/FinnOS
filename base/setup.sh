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
set -euo pipefail
set -x

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

# 1. remove competing desktop stacks if they are already on the machine.
#    FinnOS has its own compositor and hosts Wayland clients; protocol
#    libraries and the optional Xwayland compatibility bridge are allowed.
pacman -Rns --noconfirm xorg-server xorg-server-common \
    gtk3 gtk4 gtkmm3 qt5-base qt6-base 2>/dev/null || true

# 2. the configuration: the repo list with the desktop ignored by name.
#    Kept once as a backup, because a machine whose pacman.conf is
#    FinnOS's can still be looked at to see what it was.
[ -f /etc/pacman.conf.finn-backup ] || cp /etc/pacman.conf /etc/pacman.conf.finn-backup
PACMAN_CONF_TMP="$(mktemp)"
trap 'rm -f "$PACMAN_CONF_TMP"' EXIT
cat "$HERE/pacman.conf" > "$PACMAN_CONF_TMP"
if [ "$ARCH" = "x86_64" ]; then
    cat "$HERE/pacman.multilib.conf" >> "$PACMAN_CONF_TMP"
fi
install -m644 "$PACMAN_CONF_TMP" /etc/pacman.conf
rm -f "$PACMAN_CONF_TMP"

# The x86_64 list names lib32-mesa and wine, which live in [multilib]. A
# missing repository section is not a smaller install: pacman cannot resolve
# the list at all, and the failure arrives halfway through it. It is checked
# here, before the first package is fetched, so the error names the cause.
if [ "$ARCH" = "x86_64" ]; then
    grep -qE '^\[multilib\][[:space:]]*$' /etc/pacman.conf ||
        { echo "error: [multilib] is not enabled in /etc/pacman.conf; the x86_64 package list needs it for Wine" >&2; exit 1; }
fi

# 3. the packages. --needed so a machine that is already FinnOS's is only
#    topped up, and so this can be run again after the list changes.
pacman -Syu --noconfirm
# shellcheck disable=SC2046
pacman -S --needed --noconfirm $(list "$PKGS")

# 4. the dev user: in the groups that own input, video and the seat, with
#    sudo that does not ask. This is the dev machine, and vm/deploy.sh drives
#    `sudo` over ssh with no terminal to ask on. It is not the installed
#    image: the installer sets a generated password and sudo asks for it,
#    because a passwordless root on a machine with a network is remote root
#    after one guess.
id finn 2>/dev/null || useradd -m -G wheel,input,video,seat,render -s /bin/bash finn
install -d -m755 /etc/sudoers.d
printf 'finn ALL=(ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/finn
chmod 0440 /etc/sudoers.d/finn

# 5. the services the base needs before anything is drawn. sshd is installed
#    (the package list carries it so a machine can be reached without a
#    console) and deliberately not enabled: a network login is a decision
#    someone makes with `systemctl enable --now sshd`, not a default.
#
#    bluetooth.service carries its own ConditionPathIsDirectory on
#    /sys/class/bluetooth, so enabling it on a machine with no radio starts
#    nothing; on a machine with a dongle it is what makes the Bluetooth tile
#    in the Control Center more than an rfkill switch.
systemctl enable seatd
systemctl enable NetworkManager
systemctl enable bluetooth 2>/dev/null || echo "WARN: bluetooth.service is not on this image"
systemctl enable systemd-timesyncd 2>/dev/null || true

# 6. the system units, straight from this directory. finn-failure.service is
#    pulled in by finn-comp's OnFailure=, so it is installed beside it.
#
#    tty1 belongs to the compositor, not to a login: the old drop-in
#    autologged finn in there, but finn-comp conflicts with getty@tty1 and
#    comes up first, so the getty never ran and the file only ever hid that.
#    It is removed where an older base left one behind.
rm -f /etc/systemd/system/getty@tty1.service.d/autologin.conf
rmdir /etc/systemd/system/getty@tty1.service.d 2>/dev/null || true
install -Dm644 "$HERE/finn-comp.service"    /etc/systemd/system/finn-comp.service
install -Dm644 "$HERE/finn-session.service" /etc/systemd/system/finn-session.service
install -Dm644 "$HERE/finn-shell.service"   /etc/systemd/system/finn-shell.service
install -Dm644 "$HERE/finn-failure.service" /etc/systemd/system/finn-failure.service
install -Dm644 "$HERE/finn-tmpfiles.conf"   /usr/lib/tmpfiles.d/finn.conf
rm -f /etc/systemd/system/finnd.service
systemctl daemon-reload

# tty2 is the rescue console: the compositor owns tty1, so tty1 is not a way
# in, and Ctrl+Alt+F2 reaches a login whether or not the shell is running.
systemctl enable getty@tty2.service 2>/dev/null ||
    echo "WARN: could not enable getty@tty2.service; there is no rescue console"

# 7. the session. Enabling a unit whose binary is not on the machine is a
#    machine that fails at every boot, so the target and the units are only
#    turned on once the session actually exists. Until then the machine
#    boots to a login console and the script says what is missing.
missing=""
[ -x /usr/local/bin/finn-comp ]    || missing="$missing finn-comp"
[ -x /usr/local/bin/finn-session ] || missing="$missing finn-session"
[ -x /usr/local/bin/finn-shell ]   || missing="$missing finn-shell"
if [ -z "$missing" ]; then
    # The session runs as finn and needs /run/user/1000 to exist before the
    # units start; linger is what tells logind to create it without a login.
    loginctl enable-linger finn 2>/dev/null ||
        echo "WARN: could not enable linger for finn; the session needs /run/user/1000"
    systemctl enable finn-comp finn-session finn-shell
    systemctl set-default graphical.target
    echo "session enabled: finn-comp on tty1, finn-session and finn-shell behind it"
else
    echo "not enabled, missing:$missing"
    echo "build it: cd /opt/finnos && make && sudo make install"
    echo "then enable it: sudo systemctl enable --now finn-comp finn-session finn-shell"
    echo "and:            sudo systemctl set-default graphical.target"
fi

# 8. the assets: the fonts, both wallpapers, every icon, and the app
#    registry the shell draws its tiles from. The compositor starts with
#    default.webp; setup.webp is the wallpaper the first-run screen shows,
#    and it is installed beside it so that screen has one on any machine.
mkdir -p /usr/share/fonts/OTF /usr/share/finn/wallpaper /usr/share/finn/icons /var/lib/finnpkg
cp "$ROOT"/assets/fonts/FixelDisplay/*.otf /usr/share/fonts/OTF/
install -m644 "$ROOT/assets/wallpaper/default.webp" /usr/share/finn/wallpaper/default.webp
install -m644 "$ROOT/assets/wallpaper/setup.webp"   /usr/share/finn/wallpaper/setup.webp
cp "$ROOT"/assets/icons/shell/*.svg "$ROOT"/assets/icons/apps/*.svg /usr/share/finn/icons/
install -m644 "$ROOT/assets/apps.registry" /usr/share/finn/apps.registry
printf 'settings\nfiles\n' > /var/lib/finnpkg/installed
fc-cache -f >/dev/null
fc-match "Fixel Display" || echo "WARN: Fixel Display not matched, fallback active"
ls /usr/share/finn/icons/ | wc -l

# 9. the branding. os-release is what tools read to know what this machine
#    is; issue and motd are what a person reads at the console. The issue
#    and motd under airootfs are the install medium's, and say so; a machine
#    installed from that medium gets the installed pair instead.
[ -f /etc/os-release.finn-backup ] || cp /etc/os-release /etc/os-release.finn-backup 2>/dev/null || true
install -m644 "$HERE/airootfs/etc/os-release" /etc/os-release
install -m644 "$HERE/installed/etc/issue" /etc/issue
install -m644 "$HERE/installed/etc/motd" /etc/motd

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

echo
echo "FinnOS base ready on $ARCH."
echo "  session    /usr/local/bin/finn-comp on tty1"
echo "  broker     finn-session as finn, Wayland at /run/finn/wayland-0"
echo "  shell      finn-shell as a layer-shell client"
echo "  rescue     Ctrl+Alt+F2, a getty on tty2"
echo "  sshd       installed, not enabled"
echo "  accounts   finn (wheel, passwordless sudo here), root"
if [ -n "$missing" ]; then
    echo
    echo "The session is not enabled yet; the machine boots to a login console."
fi
