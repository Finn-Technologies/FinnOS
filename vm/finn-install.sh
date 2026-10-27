#!/bin/bash
# FinnOS x86_64 install: the live environment onto a disk the operator names.
#
#   finn-install --disk /dev/nvme0n1
#   finn-install --disk /dev/sda --yes
#
# There is no default disk. The one thing an installer must never do is
# erase a disk nobody named, so the target is an argument, and an interactive
# install asks for the device path again before anything is written.
#
# A machine with no console can name the disk on the kernel command line
# instead, which is how the boot entry of an unattended install does it:
#
#   finn.disk=/dev/vda finn.yes
#
# /proc/cmdline is writable only by root (and by whoever can edit the boot
# entry, who can already erase the disk), and finn.disk is treated exactly
# like --disk: it must resolve to a whole disk that is not the running one.
# Without it the install is interactive, as before.
#
# The payload comes from the FINNOS install disc, which a VM or a machine has
# attached; if the disc is not there the script falls back to the host HTTP
# server, which is how the old workflow worked.
set -euo pipefail

HOST="${FINN_HOST_URL:-http://192.168.44.1:8765}"
ISO_MNT=/finnos-iso
LOG=/tmp/finn-install.log
SRC=""
DISK=""
DISK_ARG=""
ASSUME_YES=0

# A machine with no screen - a CI runner, a server, an install driven over a
# serial cable - has no tty1 to read. When the kernel was told to use a serial
# console, the log goes there as well, so the install can be watched and a
# test can see the installer start without a display. The condition is the
# kernel command line rather than the device node, because a machine can have
# a COM port with nothing attached to it.
#
# The mirror is a FIFO and tee, not a process substitution: a live root
# without /dev/fd would fail the substitution and exit before the first line.
# tee still writes to its own stdout, so the console the installer was started
# from keeps its copy; the serial device is an extra output, not a replacement.
SERIAL=0
if grep -qw 'console=ttyS0' /proc/cmdline 2>/dev/null && [ -w /dev/ttyS0 ]; then
    SERIAL=1
fi
LOG_DIR=""
LOG_TEE=""
: >"$LOG"
# fd 3 holds the FIFO open for both ends, so the writer below cannot block
# waiting for a reader that has not opened the pipe yet. tee closes its copy
# of 3 and reads to EOF when the installer closes its own.
if LOG_DIR="$(mktemp -d "${TMPDIR:-/tmp}/finn-install.XXXXXX")" &&
   mkfifo -m 600 "$LOG_DIR/log" &&
   exec 3<>"$LOG_DIR/log"; then
    if [ "$SERIAL" = 1 ]; then
        tee "$LOG" /dev/ttyS0 <"$LOG_DIR/log" 3>&- &
    else
        tee "$LOG" <"$LOG_DIR/log" 3>&- &
    fi
    LOG_TEE=$!
    exec >&3 2>&1
else
    # The log still has the installer's words; only the mirror is missing.
    # The warning goes to the console that was there before the redirection.
    exec 3>&2
    exec >>"$LOG" 2>&1
    echo "warning: the installer log cannot be mirrored to this console" >&3
    exec 3>&-
fi

# The log is posted to the host that serves the disc, when there is one. It
# is best effort: an install on a machine with no host server must not stop
# at a step because a POST timed out. The timeouts are what make "best
# effort" bounded; without them a machine with a default route but no host
# at 192.168.44.1 waits for the TCP stack at every step.
post() {
    curl -s --connect-timeout 2 --max-time 5 \
         -X POST --data-binary @"$LOG" "$HOST/log" -o /dev/null 2>/dev/null || true
}

# The completion line is posted on its own rather than read back out of the
# log: the log may still be growing, and a CI runner watching for the end of
# an unattended install must not race it.
post_line() {
    curl -s --connect-timeout 2 --max-time 5 \
         --data-binary "$1" "$HOST/log" -o /dev/null 2>/dev/null || true
}

STEP="arguments"
die() { echo "error: $*" >&2; exit 1; }

# Closing the log is what lets tee see EOF and write the last line; waiting
# for it is what lets the failure POST read a complete log. This runs on
# every exit, including the abort where nothing was written.
stop_log() {
    exec 1>&- 2>&- 3>&-
    if [ -n "$LOG_TEE" ]; then
        wait "$LOG_TEE" 2>/dev/null || true
        LOG_TEE=""
    fi
    if [ -n "$LOG_DIR" ]; then
        rm -rf "$LOG_DIR"
        LOG_DIR=""
    fi
}

# Every exit says what it was doing. An install that stops half way leaves a
# machine somebody has to finish by hand, so the last line of the log names
# the step and not only the command that returned non-zero. The log is closed
# before it is posted, because tee writes it asynchronously.
failed() {
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FINN_INSTALL_FAILED: step \"$STEP\" exited $rc"
    fi
    stop_log
    if [ "$rc" -ne 0 ]; then
        post
    fi
}
trap failed EXIT

step() {
    STEP="$1"
    printf '\n===== %s =====\n' "$1"
    post
}

usage() {
    cat <<EOF
usage: finn-install --disk <device> [--yes]

  --disk <device>  the disk to erase and install onto, e.g. /dev/nvme0n1
  --yes            unattended: do not ask for the device path again
  -h, --help       this

There is no default disk, and finn-install never picks one.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --disk)
            [ $# -ge 2 ] || die "--disk needs a device, e.g. --disk /dev/sda"
            DISK_ARG="$2"
            shift 2
            ;;
        --disk=*)
            DISK_ARG="${1#*=}"
            shift
            ;;
        --yes|-y)
            ASSUME_YES=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "unknown argument: $1"
            ;;
    esac
done

# The boot entry of an unattended install names the disk and the "yes" on the
# kernel command line, because a machine with no console cannot answer the
# questions. Only a word that starts with finn. is read, and only /proc/cmdline
# is consulted; the argument form still wins, and a cmdline without finn.disk
# leaves the install interactive.
for word in $(cat /proc/cmdline 2>/dev/null || true); do
    case "$word" in
        finn.disk=*) [ -n "$DISK_ARG" ] || DISK_ARG="${word#finn.disk=}" ;;
        finn.yes)    ASSUME_YES=1 ;;
    esac
done

if [ -z "$DISK_ARG" ]; then
    # The default boot entry has no disk to name, so the installer asks for
    # one. It is still never guessed: the answer is typed at the console and
    # then typed again at the confirmation below. An EOF or Ctrl-C is not a
    # failed install, because nothing was attempted and nothing was written.
    echo "No disk was named. Disks on this machine:"
    lsblk -dno NAME,SIZE,TYPE,MODEL 2>/dev/null | sed 's#^#  /dev/#' || true
    echo
    while :; do
        printf 'Disk to install onto (e.g. /dev/sda), or Ctrl-C to abort:\n'
        if ! read -r DISK_ARG; then
            trap - EXIT
            echo
            echo "no disk named; nothing was written" >&2
            stop_log
            exit 2
        fi
        [ -n "$DISK_ARG" ] || continue
        if [ ! -b "$DISK_ARG" ]; then
            echo "not a block device: $DISK_ARG" >&2
            continue
        fi
        break
    done
fi

# 0. the payload.
step "[0/8] FIND THE PAYLOAD"
# The bootable disc carries the tree in the live root, which is where the
# installer normally runs from. A separate data disc labelled FINNOS still
# works for the two-disc flow, and the host server is the last resort.
if [ -f /opt/finnos/vm/finn-chroot.sh ]; then
    SRC=/opt/finnos
    echo "payload found in the live root: $SRC"
else
    mkdir -p "$ISO_MNT"
    # Anything that carries the disc's label, wherever it is attached: a
    # second CD-ROM, a USB stick, a partition. The label is the whole
    # contract, so the device is looked up rather than guessed.
    for dev in $(blkid -o device -t LABEL=FINNOS 2>/dev/null || true); do
        mount "$dev" "$ISO_MNT" && break
    done
    if mountpoint -q "$ISO_MNT" && [ -f "$ISO_MNT/vm/finn-chroot.sh" ]; then
        SRC="$ISO_MNT"
        echo "install disc found on $ISO_MNT"
    else
        umount "$ISO_MNT" 2>/dev/null || true
        echo "no install disc; falling back to $HOST"
    fi
fi
post

# 1. the tools.
step "[1/8] PREREQS"
# The live environment ships a list of mirrors and the first one is not
# always the quickest for the machine that is installing; the geo mirror
# is anycast and picks one near the caller. The rest stay in the list for
# the machine that is installed.
if [ -f /etc/pacman.d/mirrorlist ]; then
    {
        echo 'Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch'
        grep -v '^Server = https://geo.mirror.pkgbuild.com' /etc/pacman.d/mirrorlist
    } > /etc/pacman.d/mirrorlist.new
    mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
fi
# Wine's 32-bit GL and Vulkan stack lives in [multilib]. The live root is
# Arch's, and its pacman.conf may have the section or not; the disc carries
# the same snippet base/setup.sh appends, so the machine that is installed
# and the machine that ran setup.sh get the same repositories. The section
# is checked by name rather than by the append's exit code, because a file
# that already has [multilib] must not get a second copy of it.
if [ "$(uname -m)" = 'x86_64' ]; then
    if ! grep -Fqx '[multilib]' /etc/pacman.conf; then
        if [ -n "$SRC" ] && [ -f "$SRC/base/pacman.multilib.conf" ]; then
            cat "$SRC/base/pacman.multilib.conf" >> /etc/pacman.conf
        else
            # No disc and no tree: the section is two lines, and a live root
            # without it cannot resolve the x86_64 package list at all.
            cat >> /etc/pacman.conf <<'EOF'

[multilib]
Include = /etc/pacman.d/mirrorlist
EOF
        fi
    fi
    grep -Fqx '[multilib]' /etc/pacman.conf ||
        die "[multilib] is not in /etc/pacman.conf; the x86_64 package list needs it for Wine"
    echo "multilib: [multilib] enabled"
fi
if ! pacman -Sy --noconfirm arch-install-scripts dosfstools parted 2>&1 | tail -5; then
    sed -i 's/^SigLevel.*/SigLevel = Never/' /etc/pacman.conf
    pacman -Sy --noconfirm arch-install-scripts dosfstools parted 2>&1 | tail -5 ||
        die "the installer's own tools did not install"
fi
command -v pacstrap >/dev/null || die "pacstrap is not on this live system"
command -v mkfs.vfat >/dev/null || die "mkfs.vfat is not on this live system"
post

# 2. the disk. Nothing is written until the target has been named twice, the
#    plan has been printed, and the disk has been shown not to be the one the
#    machine is running from.
step "[2/8] THE DISK"
[ -b "$DISK_ARG" ] || die "$DISK_ARG is not a block device"
DISK="$(readlink -f "$DISK_ARG")"
[ -b "$DISK" ] || die "$DISK_ARG does not resolve to a block device"
# A partition is not a disk: /dev/sda3 would give sda3p1 and a table inside
# somebody else's filesystem.
[ "$(lsblk -dno TYPE "$DISK")" = "disk" ] || die "$DISK is not a whole disk"

# nvme0n1 -> nvme0n1p1, mmcblk0 -> mmcblk0p1, sda -> sda1, vda -> vda1.
case "$DISK" in
    *[0-9]) PART="${DISK}p" ;;
    *)      PART="$DISK" ;;
esac
ESP="${PART}1"
BIOSBOOT="${PART}2"
ROOT="${PART}3"

# The whole disk a device belongs to: a partition answers with its parent, a
# mapper device with the disk underneath it, a whole-disk filesystem with
# itself.
disk_of() {
    local dev="$1" parent
    dev="$(readlink -f "$dev" 2>/dev/null || printf '%s' "$dev")"
    parent="$(lsblk -no PKNAME "$dev" 2>/dev/null | head -n1 || true)"
    printf '/dev/%s\n' "${parent:-$(basename "$dev")}"
}

# A leftover /mnt from a failed run is the target, not a running system;
# unmount it before asking what this machine is running from so a retry is
# not refused by the last attempt's mounts.
umount -R /mnt 2>/dev/null || true

# The machine this is running on is never the target. / is the live root
# (overlay or a live medium): both resolve to the device the firmware booted,
# and erasing that removes the installer mid-install.
for mnt in / /boot /boot/efi /run/archiso/bootmnt /mnt; do
    [ -d "$mnt" ] || continue
    src="$(findmnt -no SOURCE -T "$mnt" 2>/dev/null || true)"
    case "$src" in
        /dev/*) ;;
        *) continue ;;
    esac
    if [ "$(disk_of "$src")" = "$DISK" ]; then
        die "$DISK backs $mnt ($src); refusing to erase the machine it is running on"
    fi
done

# Any partition already in use is somebody's data: refuse rather than pull it
# out from under them. Swap does not appear in the mount table, so it is read
# from swapon separately.
swaps="$(swapon --show=NAME --noheadings 2>/dev/null || true)"
while read -r name type; do
    [ -n "$name" ] || continue
    if [ "/dev/$name" = "$DISK" ]; then continue; fi
    target="$(findmnt -rno TARGET -S "/dev/$name" 2>/dev/null || true)"
    [ -z "$target" ] || die "/dev/$name on $DISK is mounted at $target; unmount it first"
    case " $(echo $swaps) " in
        *" /dev/$name "*) die "/dev/$name on $DISK is in use as swap; swapoff it first" ;;
    esac
done < <(lsblk -lno NAME,TYPE "$DISK" | tail -n +2)

echo
echo "This will erase every partition on $DISK and install FinnOS:"
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT "$DISK" || true
echo "  ${ESP}  512M  EFI system partition, mounted at /boot"
echo "  ${BIOSBOOT}  1M  BIOS boot (GRUB on a GPT disk)"
echo "  ${ROOT}  the rest, ext4, label finnos"
echo
if [ "$ASSUME_YES" = 1 ]; then
    echo "unattended install (--yes): writing to $DISK"
else
    printf 'Type the device path (%s) to confirm, anything else aborts: ' "$DISK"
    confirm=""
    read -r confirm || true
    if [ "$confirm" != "$DISK" ] && [ "$confirm" != "$DISK_ARG" ]; then
        die "not confirmed; nothing was written"
    fi
fi

# The signatures go first, and only on this device and its own partitions:
# wiping the table does not touch what is inside a partition, and a stale
# filesystem superblock is what makes a machine come back as what it was.
while read -r name; do
    [ -b "/dev/$name" ] || continue
    wipefs -a "/dev/$name" || die "wipefs failed on /dev/$name"
done < <(lsblk -lno NAME "$DISK" | tail -n +2)
wipefs -a "$DISK" || die "wipefs failed on $DISK"

# Three partitions: an EFI system partition for /boot, a one-megabyte BIOS
# boot partition, and the root. The BIOS boot partition is where GRUB puts
# its boot code on a GPT disk; without it a machine that starts with a BIOS
# has an install it cannot reach, and the firmware is not always willing
# to boot an EFI disk on its own.
printf 'label: gpt\n,512M,U\n,+1M,B\n,,L\n' | sfdisk --wipe=always "$DISK" ||
    die "sfdisk did not write a partition table on $DISK"
sfdisk -d "$DISK" >/dev/null || die "no partition table on $DISK"

sync
partprobe "$DISK" 2>/dev/null || true
udevadm settle --timeout=30 2>/dev/null || true
for _ in $(seq 1 30); do
    [ -b "$ESP" ] && [ -b "$BIOSBOOT" ] && [ -b "$ROOT" ] && break
    sleep 1
done
[ -b "$ESP" ] && [ -b "$BIOSBOOT" ] && [ -b "$ROOT" ] ||
    die "the partition nodes did not appear on $DISK after partitioning"
lsblk "$DISK"
post

# 3. the filesystems.
step "[3/8] FORMAT"
mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
[ "$(blkid -s TYPE -o value "$ESP")"  = "vfat" ] || die "$ESP is not vfat"
[ "$(blkid -s TYPE -o value "$ROOT")" = "ext4" ] || die "$ROOT is not ext4"
post

# 4. the mounts, checked by source rather than assumed from the exit code.
step "[4/8] MOUNT"
mkdir -p /mnt/boot
mount "$ROOT" /mnt || die "could not mount $ROOT at /mnt"
mount "$ESP" /mnt/boot || die "could not mount $ESP at /mnt/boot"
[ "$(findmnt -no SOURCE -T /mnt)" = "$ROOT" ] || die "$ROOT is not what is mounted at /mnt"
[ "$(findmnt -no SOURCE -T /mnt/boot)" = "$ESP" ] || die "$ESP is not what is mounted at /mnt/boot"
findmnt -o TARGET,SOURCE,FSTYPE /mnt /mnt/boot
post

# 5. the base system.
step "[5/8] PACSTRAP"
# The toolchain is in the base install, because the chroot builds the
# compositor: the disc carries the sources, and the machine they land on is
# the one that can build them. The dev loop builds there too, so removing
# the compiler again would only mean putting it back.
#
# The list is the base's own: base/packages.<arch>, which is what the disc
# was built from and what base/setup.sh installs. A machine installed from
# the disc and a machine that ran setup.sh therefore get the same system
# rather than two lists that drift apart. There is no Wayland compositor in
# it: the wayland client library arrives as a dependency of Mesa, which the
# list does name, and the base's pacman.conf allows that one by name.
PKGS=""
PKGLISTS=()
if [ -n "$SRC" ]; then
    PKGLISTS+=("$SRC/base/packages.$(uname -m)" "$SRC/base/packages.x86_64")
fi
PKGLISTS+=(/opt/finnos/base/packages.$(uname -m) /opt/finnos/base/packages.x86_64)
for f in "${PKGLISTS[@]}"; do
    [ -f "$f" ] || continue
    PKGS="$(sed -e 's/#.*//' -e 's/[[:space:]]//g' "$f" | grep -v '^$' | tr '\n' ' ' || true)"
    echo "package list: $f"
    break
done
if [ -z "$PKGS" ]; then
    echo "no package list on the disc; using the installer's own"
    # The kernel is named for the architecture, as the disc's list is:
    # Arch Linux ARM has no `linux` package, and no Wine at all.
    KERNEL=linux
    [ "$(uname -m)" = 'x86_64' ] || KERNEL=linux-aarch64
    PKGS="base $KERNEL linux-firmware systemd sudo openssh networkmanager wpa_supplicant
          grub efibootmgr mesa libdrm libinput seatd fontconfig ttf-dejavu
          git python vim nano base-devel pkgconf libwebp freetype2 alsa-lib
          alsa-ucm-conf arch-install-scripts dosfstools e2fsprogs parted
          gptfdisk util-linux kbd libglvnd pipewire pipewire-pulse wireplumber
          bluez bluez-utils jq zstd"
    if [ "$(uname -m)" = 'x86_64' ]; then
        # Wine and its 32-bit GL/Vulkan stack are x86_64's, and they are in
        # [multilib], which step 1 puts in front of pacman.
        PKGS="$PKGS vulkan-intel lib32-mesa lib32-vulkan-icd-loader
              lib32-vulkan-intel wine winetricks wine-mono wine-gecko
              intel-ucode amd-ucode"
    fi
fi

# The x86_64 list names wine and the lib32-* stack, which live in
# [multilib]. pacstrap resolves the list against the live system's
# pacman.conf, not the one the chroot gets later, so a missing section is
# named here rather than surfacing as "target not found: wine" halfway
# through the transaction.
case " $PKGS " in
    *" wine "*|*" lib32-"*)
        grep -Fqx '[multilib]' /etc/pacman.conf ||
            die "[multilib] is not enabled in /etc/pacman.conf; the x86_64 list needs it for Wine"
        ;;
esac

# A mirror that stalls must not fail the install. pacman keeps what it has
# downloaded, so a retry only fetches what is missing, and it picks another
# mirror for it. Three tries, and then it is a real failure.
#
# pacstrap runs pacman with --noconfirm by itself; there is no "yes |" here,
# because with pipefail a yes that is killed by SIGPIPE would report the
# whole install as failed after it succeeded.
staged=0
for try in 1 2 3; do
    if pacstrap /mnt $PKGS; then staged=1; break; fi
    echo "pacstrap failed on try $try; retrying"
done
[ "$staged" = 1 ] || die "the base system did not install"
post

# 6. the fstab, and the UUID the boot entries will name. The UUID is read
#    from the new root while it is mounted: inside the chroot that path is
#    the mounted filesystem, and an empty one is not allowed into a boot
#    entry.
step "[6/8] FSTAB"
rm -f /mnt/etc/fstab
genfstab -U /mnt > /mnt/etc/fstab
[ -s /mnt/etc/fstab ] || die "genfstab wrote no fstab"
ROOTUUID="$(findmnt -no UUID /mnt || true)"
[ -n "$ROOTUUID" ] || die "the new root filesystem has no UUID; refusing to write a boot entry"
grep -q "UUID=$ROOTUUID" /mnt/etc/fstab || die "the fstab does not name the root UUID $ROOTUUID"
cat /mnt/etc/fstab
echo "root UUID: $ROOTUUID"
post

# 7. the chroot: the machine is configured there, and it builds the shell.
step "[7/8] CHROOT"
mkdir -p /mnt/root
if [ -n "$SRC" ]; then
    # The disc carries the whole tree: the chroot script, the assets and the
    # sources, so nothing has to come over the network.
    cp "$SRC/vm/finn-chroot.sh" /mnt/root/finn-chroot.sh
    mkdir -p /mnt/root/finnos-src
    tar -C "$SRC" -cf - --exclude='.git' --exclude='*.o' --exclude='*.a' . |
        tar -C /mnt/root/finnos-src -xf -
    echo "payload taken from the install disc"
else
    curl -sLo /mnt/root/finn-chroot.sh "$HOST/vm/finn-chroot.sh"
    [ -s /mnt/root/finn-chroot.sh ] || die "the chroot script could not be fetched from $HOST"
    echo "payload taken from $HOST"
fi
chmod +x /mnt/root/finn-chroot.sh

# The chroot cannot ask the kernel what this machine booted, so the two
# things it must not guess are handed to it: the root UUID for every boot
# entry and the whole disk for the BIOS boot code. They are exported rather
# than put on the command line, so a password passed through is not in the
# process list.
export ROOTUUID
export FINN_DISK="$DISK"
export FINN_PASSWORD="${FINN_PASSWORD:-}"
arch-chroot /mnt /root/finn-chroot.sh ||
    die "the machine could not be configured; see the chroot log above"
post

# 8. the install is a claim, so it is checked on the disk that was written:
#    a runnable compositor, a runnable control plane and a boot entry that
#    names the root by UUID. Anything less is not an install.
step "[8/8] VERIFY"
[ -x /mnt/usr/local/bin/finn-comp ] ||
    die "the compositor was not installed: /usr/local/bin/finn-comp is missing or not executable"
[ -x /mnt/usr/local/bin/finnd ] ||
    die "the control plane was not installed: /usr/local/bin/finnd is missing or not executable"
[ -e /mnt/etc/systemd/system/graphical.target.wants/finn-comp.service ] ||
    die "finn-comp is installed but not enabled, so the machine would boot to a console"

# The hardware stack is checked on the disk rather than inferred from the
# list that was requested: the list is a file on the disc, and the disk is
# what boots. pacman's local database is the record of what resolved and
# installed; the retry loop and the fallback list are exactly where a set
# can drift from what was asked for.
installed_pkg() {
    compgen -G "/mnt/var/lib/pacman/local/$1-[0-9]*" >/dev/null
}
for pkg in linux-firmware mesa libglvnd pipewire pipewire-pulse wireplumber \
           bluez bluez-utils jq zstd; do
    installed_pkg "$pkg" || die "$pkg is not installed on $DISK"
done
if [ "$(uname -m)" = 'x86_64' ]; then
    installed_pkg linux || die "the linux kernel is not installed on $DISK"
    for pkg in intel-ucode amd-ucode vulkan-intel lib32-mesa \
               lib32-vulkan-icd-loader lib32-vulkan-intel wine winetricks \
               wine-mono wine-gecko; do
        installed_pkg "$pkg" || die "$pkg is not installed on $DISK"
    done
    HARDWARE="x86_64: firmware, microcode, mesa, vulkan, wine, audio, bluetooth"
else
    installed_pkg linux-aarch64 ||
        die "the linux-aarch64 kernel is not installed on $DISK"
    HARDWARE="aarch64: firmware, mesa, audio, bluetooth"
fi
echo "HARDWARE=$HARDWARE"

bootentry=""
if [ -f /mnt/boot/grub/grub.cfg ] && grep -q "root=UUID=$ROOTUUID" /mnt/boot/grub/grub.cfg; then
    bootentry=grub
elif compgen -G '/mnt/boot/loader/entries/*.conf' >/dev/null &&
     grep -q "root=UUID=$ROOTUUID" /mnt/boot/loader/entries/*.conf; then
    bootentry=systemd-boot
fi
[ -n "$bootentry" ] || die "no boot entry on $DISK names root=UUID=$ROOTUUID"

# An ESP with no loader on it is a machine the firmware cannot start, even
# with a correct grub.cfg on the root filesystem.
if [ -d /mnt/boot/EFI ]; then
    compgen -G '/mnt/boot/EFI/BOOT/BOOT*.EFI' >/dev/null ||
        compgen -G '/mnt/boot/EFI/systemd/systemd-boot*' >/dev/null ||
        die "the EFI system partition on $DISK has no EFI loader"
fi

sync
umount -R /mnt || echo "WARN: /mnt is still mounted; unmount it before rebooting"

echo "FINN_INSTALL_COMPLETE"
echo "  disk       $DISK"
echo "  root UUID  $ROOTUUID"
echo "  boot       $bootentry"
echo "  console    Ctrl+Alt+F2 if the compositor does not come up"
post_line "FINN_INSTALL_COMPLETE disk=$DISK root=$ROOTUUID boot=$bootentry"
