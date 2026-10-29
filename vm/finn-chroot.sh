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
#
# Two things are handed in rather than guessed, because inside the chroot
# there is no kernel to ask:
#   ROOTUUID   the UUID of the filesystem mounted at /, for every boot entry
#   FINN_DISK  the whole disk, for the BIOS boot code (grub i386-pc)
# FINN_PASSWORD is optional; without it the two accounts get a generated
# password, which is printed once at the end. A supplied password is never
# echoed into the installation log.
set -euo pipefail
HOST=http://192.168.44.1:8765
SRC=/root/finnos-src

post() { curl -s -X POST --data-binary @/tmp/finn-chroot.log "$HOST/log" -o /dev/null 2>/dev/null || true; }

STEP="start"
PW=""
CREDS_SHOWN=0

# Saying "complete" over a machine with no password is a machine nobody can
# log in to, so the one secret is printed exactly once - at the end on
# success, and on failure through the trap, so a half-built machine that
# still boots to a console is not a lockout.
credentials() {
    [ -n "$PW" ] || return 0
    [ "$CREDS_SHOWN" = 0 ] || return 0
    CREDS_SHOWN=1
    if [ -n "${FINN_PASSWORD:-}" ]; then
        echo "  operator-supplied credentials are omitted from this log."
        return 0
    fi
    cat <<EOF

  Credentials, shown once:
    user  finn   password  $PW
    root  root   password  $PW
  Both accounts have the same password. Change it with: passwd
  sshd is installed and not enabled.
EOF
}

failed() {
    local rc=$?
    [ "$rc" -eq 0 ] && return 0
    echo "FINN_CHROOT_FAILED: \"$STEP\" exited $rc" >&2
    credentials >&2
}
trap failed EXIT

step() {
    STEP="$1"
    printf '\n=== %s ===\n' "$1"
    post
}

# get <destination> <path-in-the-tree>
get() {
    if [ -f "$SRC/$2" ]; then
        install -D -m 644 "$SRC/$2" "$1"
    else
        curl -fsSLo "$1" "$HOST/$2"
    fi
}

: "${ROOTUUID:?the installer must pass ROOTUUID}"
: "${FINN_DISK:?the installer must pass FINN_DISK}"
echo "ROOTUUID=$ROOTUUID"
echo "FINN_DISK=$FINN_DISK"

step "base config"
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
# The live medium's issue and motd describe the medium; the machine gets
# the installed pair, which does not tell the user to run the installer.
get /etc/issue base/installed/etc/issue
get /etc/motd base/installed/etc/motd
get /etc/pacman.conf base/pacman.conf
if [ "$(uname -m)" = 'x86_64' ]; then
    MULTILIB_CONF="$(mktemp)"
    get "$MULTILIB_CONF" base/pacman.multilib.conf
    cat "$MULTILIB_CONF" >> /etc/pacman.conf
    rm -f "$MULTILIB_CONF"
    # The section is what makes Wine's 32-bit dependencies resolvable
    # after the install; a pacman.conf without it is a machine that cannot
    # update or reinstall what it was built with.
    grep -Fqx '[multilib]' /etc/pacman.conf ||
        { echo "error: [multilib] was not applied to /etc/pacman.conf" >&2; exit 1; }
fi
printf '127.0.0.1 localhost\n::1 localhost\n127.0.1.1 finnos.localdomain finnos\n' > /etc/hosts

step "accounts"
# There used to be a default here - root:finn, finn:finn, passwordless sudo -
# and on any network that is remote root after one guess. There is none now:
# the password is the one the installer was given, or a random one, and sudo
# asks for it.
id finn >/dev/null 2>&1 || useradd -m -G wheel,input,video,seat -s /bin/bash finn
PW="${FINN_PASSWORD:-}"
if [ -z "$PW" ]; then
    PW="$(head -c 12 /dev/urandom | base64 | tr -d '\n')"
fi
case "$PW" in
    *:*|*$'\n'*) echo "error: the password may not contain ':' or a newline" >&2; exit 1 ;;
esac
printf 'root:%s\nfinn:%s\n' "$PW" "$PW" | chpasswd
printf 'finn ALL=(ALL) ALL\n' > /etc/sudoers.d/finn
chmod 0440 /etc/sudoers.d/finn

step "services"
systemctl enable NetworkManager 2>/dev/null || true
systemctl enable seatd 2>/dev/null || true
systemctl enable systemd-timesyncd 2>/dev/null || true
# bluetooth.service carries ConditionPathIsDirectory=/sys/class/bluetooth,
# so enabling it on a machine with no radio starts nothing; with a USB
# dongle it is what the Control Center's Bluetooth tile drives.
systemctl enable bluetooth 2>/dev/null ||
    echo "WARN: bluetooth.service is not installed"
# sshd is installed and disabled: a network login is a decision somebody
# makes on the machine, not something an install turns on.
# tty2 is the rescue console. tty1 belongs to the compositor, so the old
# autologin drop-in there never ran; it is removed rather than replaced.
rm -f /etc/systemd/system/getty@tty1.service.d/autologin.conf
rmdir /etc/systemd/system/getty@tty1.service.d 2>/dev/null || true
systemctl enable getty@tty2.service 2>/dev/null ||
    echo "WARN: getty@tty2 could not be enabled; there is no rescue console"

step "Fixel Display fonts"
mkdir -p /usr/share/fonts/OTF
for f in FixelDisplay-Regular FixelDisplay-Medium FixelDisplay-SemiBold FixelDisplay-Bold; do
    get "/usr/share/fonts/OTF/$f.otf" "assets/fonts/FixelDisplay/$f.otf"
done
ls -la /usr/share/fonts/OTF/
fc-cache -f >/dev/null 2>&1 || true
fc-match "Fixel Display" || true
post

step "the payload: wallpaper, icons, registry, the package manager"
mkdir -p /usr/share/finn/wallpaper /usr/share/finn/icons /var/lib/finnpkg /usr/local/bin
get /usr/share/finn/wallpaper/default.webp assets/wallpaper/default.webp
get /usr/share/finn/wallpaper/setup.webp assets/wallpaper/setup.webp
get /usr/share/finn/apps.registry assets/apps.registry
printf 'settings\nfiles\nstore\nbrowser\n' > /var/lib/finnpkg/installed
# Every icon, when the payload is the tree. A list of names is a second
# place to forget one, and an icon the shell cannot find is a tile drawn
# as an empty square. The list below is only for the host-server flow,
# where the tree is not on the disc: it is written out to match what is
# there today, and the tree copy is the one that is complete.
if compgen -G "$SRC/assets/icons/shell/*.svg" >/dev/null 2>&1; then
    for f in "$SRC"/assets/icons/shell/*.svg "$SRC"/assets/icons/apps/*.svg; do
        install -D -m644 "$f" "/usr/share/finn/icons/$(basename "$f")"
    done
else
    for icon in avatar battery bluetooth boot-mark chevron-right cursor \
                display edit-01 expand home-09 info internet minus-sign moon \
                paint-brush power refresh search-01 settings-02 sun-01 \
                sun-03 view-open-windows volume-02 wifi-02 x; do
        get "/usr/share/finn/icons/$icon.svg" "assets/icons/shell/$icon.svg"
    done
    for icon in app-grid browser chevron-left files library settings-app \
                store windows; do
        get "/usr/share/finn/icons/$icon.svg" "assets/icons/apps/$icon.svg"
    done
fi
get /usr/local/bin/finnpkg src/pkg/finnpkg
chmod +x /usr/local/bin/finnpkg
if [ -x /usr/bin/flatpak ]; then
    if ! /usr/local/bin/finnpkg flatpak init; then
        echo "WARN: Flathub could not be configured; Flatpak search will be unavailable"
    fi
else
    echo "WARN: Flatpak is not packaged for this target; Flatpak apps are unavailable"
fi
chown -R root:finn /usr/share/finn /var/lib/finnpkg
ls /usr/share/finn/icons/

step "the system units"
get /etc/systemd/system/finn-comp.service base/finn-comp.service
get /etc/systemd/system/finnd.service base/finnd.service
get /etc/systemd/system/finn-failure.service base/finn-failure.service
get /usr/lib/tmpfiles.d/finn.conf base/finn-tmpfiles.conf
# A chroot has no systemd of its own to reload, and the target's systemd
# reads its units at boot; the reload is for the live environment, so it is
# not a reason to fail an install that is otherwise correct.
systemctl daemon-reload 2>/dev/null || true
# Not enabled here: a unit whose binary does not exist yet fails at every
# boot, and the binary does not exist until the build below has run.

step "the sources, so the machine can build the compositor"
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

step "build the shell and the control plane"
# The disc carries the sources; the machine they land on is the one that
# has the DRM headers and the toolchain, so the build happens here. Without
# it the machine would boot with no compositor to start.
SHELL_STATE=missing
if [ -d /opt/finnos ]; then
    cd /opt/finnos
    rc=0
    make -j"$(nproc)" >/tmp/finn-build.log 2>&1 || rc=$?
    if [ "$rc" -eq 0 ]; then
        make install >>/tmp/finn-build.log 2>&1 || rc=$?
    fi
    if [ "$rc" -eq 0 ] && [ -x /usr/local/bin/finn-comp ]; then
        SHELL_STATE=installed
        echo "SHELL=installed"
    else
        echo "WARN: the build or the install failed; see /tmp/finn-build.log"
        tail -30 /tmp/finn-build.log || true
    fi
else
    echo "WARN: no sources to build; the shell will not start"
fi
chown -R finn:finn /opt/finnos

# The session is turned on only now, when there is something to turn on.
# A machine that reaches this point without a compositor keeps its console:
# tty2 has a getty either way.
if [ "$SHELL_STATE" = installed ]; then
    systemctl enable finn-comp finnd
    systemctl set-default graphical.target
    # Enabled is a file on disk, so it is checked as one: a session that was
    # never enabled is a machine that boots to a console after saying the
    # install worked.
    [ -e /etc/systemd/system/graphical.target.wants/finn-comp.service ] ||
        { echo "error: finn-comp was not enabled for graphical.target" >&2; exit 1; }
    [ -e /etc/systemd/system/graphical.target.wants/finnd.service ] ||
        { echo "error: finnd was not enabled for graphical.target" >&2; exit 1; }
    echo "SESSION=enabled"
else
    echo "SESSION=not-enabled: /usr/local/bin/finn-comp is missing"
fi
post

step "initramfs + bootloader"
mkinitcpio -P 2>&1 | tail -5
[ -f /boot/vmlinuz-linux ] || { echo "error: no kernel in /boot" >&2; exit 1; }
[ -f /boot/initramfs-linux.img ] || { echo "error: no initramfs in /boot" >&2; exit 1; }
ls -la /boot/

# The kernel command line is written out here rather than left to whatever
# the package shipped, so the timeout and the distributor are FinnOS's. The
# root= is not written here: grub-mkconfig derives it from the filesystem it
# can see, and the check at the end refuses a menu that does not name the
# UUID the installer measured.
#
# An install driven over a serial console keeps that console on the machine
# it wrote. Dropping it would leave the person who installed the machine
# with no way to see the boot that follows. An install from a screen gets
# no serial console: this is the installer's answer, not a default.
serial_args=""
if [ "${FINN_SERIAL:-0}" = 1 ]; then
    serial_args=" console=ttyS0,115200 console=tty0"
    systemctl enable serial-getty@ttyS0.service 2>/dev/null ||
        echo "WARN: serial-getty@ttyS0 could not be enabled"
    echo "SERIAL=console"
else
    echo "SERIAL=none"
fi
mkdir -p /etc/default
cat > /etc/default/grub <<EOF
GRUB_DEFAULT=0
GRUB_TIMEOUT=3
GRUB_DISTRIBUTOR="FinnOS"
GRUB_CMDLINE_LINUX_DEFAULT="quiet loglevel=3$serial_args"
GRUB_DISABLE_OS_PROBER=true
EOF

efi=0
if grub-install --target=x86_64-efi --efi-directory=/boot \
        --bootloader-id=FinnOS --removable 2>&1 | tail -5; then
    grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 ||
        { echo "error: grub-mkconfig failed" >&2; exit 1; }
    efi=1
    echo "BOOTLOADER=grub-efi"
else
    echo "grub-efi did not install; trying systemd-boot"
    if bootctl --esp-path=/boot install 2>&1 | tail -5; then
        mkdir -p /boot/loader/entries
        printf 'default finnos.conf\ntimeout 3\n' > /boot/loader/loader.conf
        printf 'title FinnOS\nlinux /vmlinuz-linux\ninitrd /initramfs-linux.img\noptions root=UUID=%s rw quiet loglevel=3%s\n' \
            "$ROOTUUID" "$serial_args" > /boot/loader/entries/finnos.conf
        efi=1
        echo "BOOTLOADER=systemd-boot"
    fi
fi

# A machine that starts with a BIOS rather than EFI needs the boot code in
# the MBR as well: the EFI loader on the partition is not enough, and the
# install would be on the disk with no way to reach it. On a GPT disk GRUB
# keeps that code in the BIOS boot partition the installer made. The disk is
# the one the installer was told to write, never a guess.
bios=0
if grub-install --target=i386-pc --recheck "$FINN_DISK" 2>&1 | tail -5; then
    grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 || true
    bios=1
    echo "BOOTLOADER=grub-bios"
else
    echo "WARN: no BIOS boot code on $FINN_DISK; this machine has to start with EFI"
fi
[ "$efi" = 1 ] || [ "$bios" = 1 ] ||
    { echo "error: no bootloader was installed" >&2; exit 1; }

# A boot menu is the only way into the machine, so an install that produced
# none is not an install. The test is the UUID, not the presence of a file:
# a grub.cfg with root=UUID= (empty) boots nothing.
if [ -f /boot/grub/grub.cfg ] && grep -q "root=UUID=$ROOTUUID" /boot/grub/grub.cfg; then
    :
elif compgen -G '/boot/loader/entries/*.conf' >/dev/null &&
     grep -q "root=UUID=$ROOTUUID" /boot/loader/entries/*.conf; then
    :
else
    echo "error: no boot menu names root=UUID=$ROOTUUID" >&2
    exit 1
fi

post
echo
# The bootloader is in place either way, so the machine boots to a console;
# but a machine without a shell is not a finished install, and the installer
# has to hear that from here. This is the only success marker in the script.
if [ "$SHELL_STATE" != installed ]; then
    echo "FINN_CHROOT_FAILED: the compositor is not installed; the machine boots to a login console"
    exit 1
fi
credentials
echo "FINN_CHROOT_COMPLETE"
