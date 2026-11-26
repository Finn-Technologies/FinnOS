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
HOSTNAME_ARG=""
TIMEZONE_ARG=""
AB_SLOTS=0
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

# A line a CI runner must not race is posted on its own rather than read
# back out of the log: tee writes the log asynchronously.
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
usage: finn-install --disk <device> [options]

  --disk <device>      the target disk, e.g. /dev/nvme0n1
  --mode <mode>        clean (default), dualboot-resize, partition, reinstall, repair-boot
  --root-part <dev>    target/existing FinnOS root partition (partition/reinstall/repair-boot)
  --esp-part <dev>     existing EFI System Partition to share without formatting
  --resize-part <dev>  existing Windows/Linux partition to shrink (dualboot-resize)
  --resize-mb <mib>    new size in MiB for --resize-part (dualboot-resize)
  --hostname <name>    optional CLI override (default: finnos; configured in OOBE)
  --timezone <tz>      optional CLI override (default: UTC; configured in OOBE)
  --ab-slots           partition for OTA A/B root slots (FINNOS_A, FINNOS_B, FINNOS_DATA)
  --yes                unattended: do not ask for the device path again
  -h, --help           this

There is no default disk, and finn-install never picks one.
EOF
}

INSTALL_MODE="clean"
ROOT_PART_ARG=""
ESP_PART_ARG=""
RESIZE_PART_ARG=""
RESIZE_MB_ARG=""

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
        --mode)
            [ $# -ge 2 ] || die "--mode needs a value"
            INSTALL_MODE="$2"
            shift 2
            ;;
        --mode=*)
            INSTALL_MODE="${1#*=}"
            shift
            ;;
        --root-part)
            [ $# -ge 2 ] || die "--root-part needs a partition device"
            ROOT_PART_ARG="$2"
            shift 2
            ;;
        --esp-part)
            [ $# -ge 2 ] || die "--esp-part needs a partition device"
            ESP_PART_ARG="$2"
            shift 2
            ;;
        --resize-part)
            [ $# -ge 2 ] || die "--resize-part needs a partition device"
            RESIZE_PART_ARG="$2"
            shift 2
            ;;
        --resize-mb)
            [ $# -ge 2 ] || die "--resize-mb needs a size in MiB"
            RESIZE_MB_ARG="$2"
            shift 2
            ;;
        --hostname)
            [ $# -ge 2 ] || die "--hostname needs a value"
            HOSTNAME_ARG="$2"
            shift 2
            ;;
        --hostname=*)
            HOSTNAME_ARG="${1#*=}"
            shift
            ;;
        --timezone)
            [ $# -ge 2 ] || die "--timezone needs a zone, e.g. UTC"
            TIMEZONE_ARG="$2"
            shift 2
            ;;
        --timezone=*)
            TIMEZONE_ARG="${1#*=}"
            shift
            ;;
        --ab-slots)
            AB_SLOTS=1
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
        finn.disk=*)     [ -n "$DISK_ARG" ] || DISK_ARG="${word#finn.disk=}" ;;
        finn.hostname=*) [ -n "$HOSTNAME_ARG" ] || HOSTNAME_ARG="${word#finn.hostname=}" ;;
        finn.timezone=*) [ -n "$TIMEZONE_ARG" ] || TIMEZONE_ARG="${word#finn.timezone=}" ;;
        finn.ab)         AB_SLOTS=1 ;;
        finn.yes)        ASSUME_YES=1 ;;
    esac
done

if [ -n "$HOSTNAME_ARG" ]; then
    printf '%s' "$HOSTNAME_ARG" | grep -qE '^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$' ||
        die "invalid --hostname: $HOSTNAME_ARG"
fi
if [ -n "$TIMEZONE_ARG" ]; then
    case "$TIMEZONE_ARG" in
        /*|*..*|*[[:space:]]*) die "invalid --timezone: $TIMEZONE_ARG" ;;
    esac
fi

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
OFFLINE_ROOT=""
OFFLINE_KERNEL=""
if [ -d /run/archiso/airootfs/usr/bin ] && [ -d /run/archiso/airootfs/var/lib/pacman/local ]; then
    OFFLINE_ROOT="/run/archiso/airootfs"
    for kcand in \
        "/run/archiso/bootmnt/finn/boot/$(uname -m)/vmlinuz-linux" \
        "/run/archiso/bootmnt/finn/boot/$(uname -m)/vmlinuz-linux-aarch64" \
        /run/archiso/airootfs/usr/lib/modules/*/vmlinuz \
        /usr/lib/modules/*/vmlinuz; do
        if [ -f "$kcand" ]; then
            OFFLINE_KERNEL="$kcand"
            break
        fi
    done
    echo "offline live image found: $OFFLINE_ROOT (kernel: ${OFFLINE_KERNEL:-in-root})"
fi
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
# Install the FinnOS repository mirrorlist (/etc/pacman.d/finnos-mirrorlist)
# from the tree (or fallback template when fetched over HTTP). Optional
# FINN_REPO_URL prepends a local/staging mirror for [finnos].
mkdir -p /etc/pacman.d
if [ -n "$SRC" ] && [ -f "$SRC/repo/finnos-mirrorlist" ]; then
    install -m 644 "$SRC/repo/finnos-mirrorlist" /etc/pacman.d/finnos-mirrorlist
else
    cat > /etc/pacman.d/finnos-mirrorlist <<'EOF'
# FinnOS pacman repository mirrorlist (/etc/pacman.d/finnos-mirrorlist)
# Placeholder URL until live repository hosting is provisioned (see docs/repo.md).
Server = https://repo.finnos.org/$repo/os/$arch
EOF
fi
if [ -n "${FINN_REPO_URL:-}" ]; then
    {
        printf '# Operator override via FINN_REPO_URL\nServer = %s\n' "$FINN_REPO_URL"
        cat /etc/pacman.d/finnos-mirrorlist
    } > /etc/pacman.d/finnos-mirrorlist.new
    mv /etc/pacman.d/finnos-mirrorlist.new /etc/pacman.d/finnos-mirrorlist
fi

# Upstream Arch mirrors in /etc/pacman.d/mirrorlist remain ONLY as an explicit
# fallback for [core], [extra], and (x86_64) [multilib] packages that FinnOS
# does not yet rebuild (kernel, systemd, glibc, mesa, Qt6, Wine; see
# docs/repo.md). Do not hard-code a single upstream geo mirror here: keep the
# live medium's mirrorlist, or prepend FINN_ARCH_FALLBACK_MIRROR if supplied.
if [ -f /etc/pacman.d/mirrorlist ]; then
    if ! grep -q '^# FinnOS upstream fallback mirrorlist' /etc/pacman.d/mirrorlist; then
        {
            echo '# FinnOS upstream fallback mirrorlist (for [core]/[extra]/[multilib] only)'
            echo '# Kept only for packages FinnOS does not yet rebuild; see docs/repo.md.'
            if [ -n "${FINN_ARCH_FALLBACK_MIRROR:-}" ]; then
                printf 'Server = %s\n' "$FINN_ARCH_FALLBACK_MIRROR"
            fi
            cat /etc/pacman.d/mirrorlist
        } > /etc/pacman.d/mirrorlist.new
        mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
    elif [ -n "${FINN_ARCH_FALLBACK_MIRROR:-}" ]; then
        {
            printf 'Server = %s\n' "$FINN_ARCH_FALLBACK_MIRROR"
            cat /etc/pacman.d/mirrorlist
        } > /etc/pacman.d/mirrorlist.new
        mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
    fi
elif [ -n "${FINN_ARCH_FALLBACK_MIRROR:-}" ]; then
    {
        echo '# FinnOS upstream fallback mirrorlist (for [core]/[extra]/[multilib] only)'
        echo '# Kept only for packages FinnOS does not yet rebuild; see docs/repo.md.'
        printf 'Server = %s\n' "$FINN_ARCH_FALLBACK_MIRROR"
    } > /etc/pacman.d/mirrorlist
fi
if [ -z "$OFFLINE_ROOT" ] && [ "$INSTALL_MODE" != "repair-boot" ]; then
    if ! grep -qE '^[[:space:]]*Server[[:space:]]*=' /etc/pacman.d/mirrorlist 2>/dev/null; then
        die "no fallback mirror configured in /etc/pacman.d/mirrorlist (set FINN_ARCH_FALLBACK_MIRROR or populate /etc/pacman.d/mirrorlist)"
    fi
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
# The live root's keyring is a tmpfs populated by pacman-init.service. If
# that unit did not run - an older disc, an installer started by hand - the
# keyring is empty and a signed pacman sync fails. Initializing it here is
# the recovery; disabling signature checking is not. An install whose
# packages were not verified is not an install this script will perform.
if [ -z "$OFFLINE_ROOT" ] && [ "$INSTALL_MODE" != "repair-boot" ]; then
    if [ ! -s /etc/pacman.d/gnupg/pubring.gpg ]; then
        echo "the live keyring is empty; initializing it"
        if ! pacman-key --init 2>&1 | tail -5; then
            die "the live keyring could not be initialized; refusing to install without signature checking"
        fi
        if ! pacman-key --populate 2>&1 | tail -5; then
            die "the live keyring could not be populated; refusing to install without signature checking"
        fi
    fi
    [ -s /etc/pacman.d/gnupg/pubring.gpg ] ||
        die "the live keyring is not initialized; refusing to install without signature checking"
fi
# If the live medium's /etc/pacman.conf enables [finnos] against an offline
# repo.finnos.org placeholder (and no FINN_REPO_URL override is set), disable
# [finnos] in the live pacman.conf so pacman -Sy / pacstrap can fall back to
# upstream [core]/[extra] without hard-failing.
if grep -qE '^\[finnos\][[:space:]]*$' /etc/pacman.conf 2>/dev/null; then
    LIVE_ARCH="$(uname -m)"
    if [ -n "${FINN_REPO_URL:-}" ]; then
        awk -v url="$FINN_REPO_URL" '
          /^\[finnos\]/ { in_finnos=1 }
          in_finnos && /^\[/ && !/^\[finnos\]/ { in_finnos=0 }
          in_finnos && /^Server[[:space:]]*=/ { print "Server = " url; next }
          { print }
        ' /etc/pacman.conf > /etc/pacman.conf.new && mv /etc/pacman.conf.new /etc/pacman.conf
    elif ! curl -fsI --connect-timeout 2 --max-time 4 "https://repo.finnos.org/finnos/os/${LIVE_ARCH}/finnos.db" >/dev/null 2>&1; then
        echo "note: [finnos] repo (repo.finnos.org) is offline/unprovisioned; disabling [finnos] in live pacman.conf"
        awk '
          /^\[finnos\]/ { in_finnos=1; print "# [finnos] (offline fallback)"; next }
          in_finnos && /^\[/ { in_finnos=0 }
          in_finnos && /^(SigLevel|Server)/ { print "# " $0; next }
          { print }
        ' /etc/pacman.conf > /etc/pacman.conf.new && mv /etc/pacman.conf.new /etc/pacman.conf
    fi
fi
if [ -z "$OFFLINE_ROOT" ] && [ "$INSTALL_MODE" != "repair-boot" ]; then
    if ! command -v pacstrap >/dev/null 2>&1 ||
       ! command -v mkfs.vfat >/dev/null 2>&1 ||
       ! command -v parted >/dev/null 2>&1; then
        if ! pacman -Sy --noconfirm arch-install-scripts dosfstools parted 2>&1 | tail -5; then
            die "the installer's own tools did not install; the live keyring or a mirror is broken"
        fi
    fi
    command -v pacstrap >/dev/null || die "pacstrap is not on this live system"
fi
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
SLOT_B=""
DATA_PART=""
if [ "$AB_SLOTS" = 1 ]; then
    SLOT_B="${PART}4"
    DATA_PART="${PART}5"
fi

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
# A here-string, not a process substitution: the live root may not carry
# /dev/fd, and a failed substitution would skip the check without a word.
while read -r name type; do
    [ -n "$name" ] || continue
    if [ "/dev/$name" = "$DISK" ]; then continue; fi
    target="$(findmnt -rno TARGET -S "/dev/$name" 2>/dev/null || true)"
    [ -z "$target" ] || die "/dev/$name on $DISK is mounted at $target; unmount it first"
    case " $(echo $swaps) " in
        *" /dev/$name "*) die "/dev/$name on $DISK is in use as swap; swapoff it first" ;;
    esac
done <<< "$(lsblk -lno NAME,TYPE "$DISK" | tail -n +2)"

echo
echo "This will erase every partition on $DISK and install FinnOS:"
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT "$DISK" || true
if [ "$AB_SLOTS" = 1 ]; then
    DISK_BYTES="$(lsblk -bno SIZE -d "$DISK" | head -n1 || true)"
    [ -n "$DISK_BYTES" ] || DISK_BYTES="$(blockdev --getsize64 "$DISK" 2>/dev/null || echo 0)"
    DISK_MIB=$((DISK_BYTES / 1024 / 1024))
    # Reserve 1024 MiB ESP + 1 MiB BIOS boot + 16 MiB alignment slack.
    AVAIL_MIB=$((DISK_MIB - 1041))
    SLOT_MIB=$((AVAIL_MIB * 35 / 100))
    [ "$SLOT_MIB" -ge 4096 ] || die "$DISK is too small for A/B OTA slots (need >= 14 GiB, got ${DISK_MIB} MiB)"
    echo "  ${ESP}  1024M  EFI system partition, mounted at /boot"
    echo "  ${BIOSBOOT}  1M     BIOS boot (GRUB on a GPT disk)"
    echo "  ${ROOT}  ${SLOT_MIB}M  Root Slot A, ext4, label FINNOS_A"
    echo "  ${SLOT_B}  ${SLOT_MIB}M  Root Slot B, ext4, label FINNOS_B"
    echo "  ${DATA_PART}  rest   Persistent data, ext4, label FINNOS_DATA (/data)"
else
    echo "  ${ESP}  512M  EFI system partition, mounted at /boot"
    echo "  ${BIOSBOOT}  1M  BIOS boot (GRUB on a GPT disk)"
    echo "  ${ROOT}  the rest, ext4, label finnos"
fi
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

# Non-destructive dual-boot, reinstall, and bootloader repair modes.
if [ "$INSTALL_MODE" = "repair-boot" ]; then
    [ -b "$ROOT_PART_ARG" ] || die "--root-part is required for repair-boot"
    step "[2/4] MOUNT FOR BOOTLOADER REPAIR"
    umount -R /mnt 2>/dev/null || true
    mount "$ROOT_PART_ARG" /mnt || die "could not mount $ROOT_PART_ARG"
    mkdir -p /mnt/boot
    if [ -n "$ESP_PART_ARG" ]; then
        [ -b "$ESP_PART_ARG" ] || die "ESP partition $ESP_PART_ARG is not a block device"
        mount "$ESP_PART_ARG" /mnt/boot || die "could not mount $ESP_PART_ARG"
    fi
    ROOTUUID="$(findmnt -no UUID /mnt || true)"
    [ -n "$ROOTUUID" ] || die "root partition has no UUID"
    PT_TYPE="$(blkid -s PTTYPE -o value "$DISK" 2>/dev/null || true)"
    step "[3/4] REPAIR BOOTLOADER"
    arch-chroot /mnt /bin/bash -c "
        set -euo pipefail
        mkinitcpio -P 2>&1 | tail -5
        efi_ok=0
        if [ -n '$ESP_PART_ARG' ] && [ -d /sys/firmware/efi ] && [ '$PT_TYPE' != 'dos' ] &&
           command -v bootctl >/dev/null 2>&1 &&
           bootctl --esp-path=/boot install 2>&1 | tail -5; then
            if [ -f /usr/share/finn/boot/finn-bootx64.efi ]; then
                install -Dm644 /usr/share/finn/boot/finn-bootx64.efi /boot/EFI/BOOT/BOOTX64.EFI
                install -Dm644 /usr/share/finn/boot/finn-bootx64.efi /boot/EFI/systemd/systemd-bootx64.efi
            elif [ -f /usr/share/finn/boot/finn-bootaa64.efi ]; then
                install -Dm644 /usr/share/finn/boot/finn-bootaa64.efi /boot/EFI/BOOT/BOOTAA64.EFI
                install -Dm644 /usr/share/finn/boot/finn-bootaa64.efi /boot/EFI/systemd/systemd-bootaa64.efi
            fi
            mkdir -p /boot/loader/entries
            printf 'default finnos.conf\ntimeout 3\nconsole-mode max\neditor no\nauto-entries yes\nauto-firmware yes\n' > /boot/loader/loader.conf
            printf 'title FinnOS\nlinux /vmlinuz-linux\ninitrd /initramfs-linux.img\noptions root=UUID=%s rw quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles loglevel=3\n' '$ROOTUUID' > /boot/loader/entries/finnos.conf
            efi_ok=1
        elif [ -n '$ESP_PART_ARG' ] && [ -d /sys/firmware/efi ] && command -v grub-install >/dev/null 2>&1; then
            EFI_TARGET=x86_64-efi
            [ \"\$(uname -m)\" = 'aarch64' ] && EFI_TARGET=arm64-efi
            if grub-install --target=\"\$EFI_TARGET\" --efi-directory=/boot --bootloader-id=FinnOS --removable 2>&1 | tail -5; then
                grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 || true
                efi_ok=1
            fi
        fi
        bios_ok=0
        if [ \"\$(uname -m)\" = 'x86_64' ] && command -v grub-install >/dev/null 2>&1; then
            if grub-install --target=i386-pc --recheck '$DISK' 2>&1 | tail -5; then
                grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 || true
                bios_ok=1
            fi
        fi
        [ \"\$efi_ok\" = 1 ] || [ \"\$bios_ok\" = 1 ] || { echo 'error: bootloader repair failed for both UEFI and BIOS' >&2; exit 1; }
    "
    umount -R /mnt
    step "[4/4] REPAIR COMPLETE"
    echo "FINN_BOOT_REPAIR_COMPLETE"
    exit 0
fi

if [ "$INSTALL_MODE" = "dualboot-resize" ]; then
    [ -b "$RESIZE_PART_ARG" ] || die "--resize-part is required for dualboot-resize"
    [ -n "$RESIZE_MB_ARG" ] || die "--resize-mb is required for dualboot-resize"
    if [ -n "$ESP_PART_ARG" ]; then
        [ -b "$ESP_PART_ARG" ] || die "ESP partition $ESP_PART_ARG is not a block device"
    fi
    step "[2/8] DUAL-BOOT NON-DESTRUCTIVE RESIZE"
    FS_KIND="$(blkid -s TYPE -o value "$RESIZE_PART_ARG" 2>/dev/null || true)"
    case "$FS_KIND" in
        ntfs)
            command -v ntfsresize >/dev/null || die "ntfsresize is required to shrink NTFS"
            ntfsfix -d "$RESIZE_PART_ARG" >/dev/null 2>&1 || true
            printf 'y\n' | ntfsresize --force --force --size "${RESIZE_MB_ARG}M" "$RESIZE_PART_ARG" || die "ntfsresize failed (disable Windows Fast Startup / hibernation first)"
            ;;
        ext4|ext3|ext2)
            e2fsck -f -p "$RESIZE_PART_ARG" || true
            resize2fs "$RESIZE_PART_ARG" "${RESIZE_MB_ARG}M" || die "resize2fs failed"
            ;;
        btrfs)
            command -v btrfs >/dev/null || die "btrfs-progs is required to shrink btrfs"
            TMP_BTRFS="$(mktemp -d "${TMPDIR:-/tmp}/finn-btrfs.XXXXXX")"
            mount "$RESIZE_PART_ARG" "$TMP_BTRFS" || { rmdir "$TMP_BTRFS" 2>/dev/null || true; die "could not mount $RESIZE_PART_ARG for btrfs resize"; }
            if ! btrfs filesystem resize "${RESIZE_MB_ARG}M" "$TMP_BTRFS"; then
                umount "$TMP_BTRFS" 2>/dev/null || true
                rmdir "$TMP_BTRFS" 2>/dev/null || true
                die "btrfs filesystem resize failed"
            fi
            umount "$TMP_BTRFS" || die "could not unmount $TMP_BTRFS"
            rmdir "$TMP_BTRFS" 2>/dev/null || true
            ;;
        *)
            die "cannot safely shrink filesystem type '$FS_KIND' on $RESIZE_PART_ARG"
            ;;
    esac
    PART_NUM="$(lsblk -no PARTN "$RESIZE_PART_ARG" 2>/dev/null | tr -d '[:space:]')"
    [ -n "$PART_NUM" ] || die "could not determine partition number for $RESIZE_PART_ARG"
    START_MB="$(parted -ms "$DISK" unit MiB print | awk -F: -v n="$PART_NUM" '$1==n {gsub(/MiB/,"",$2); print int($2)}')"
    # Leave a 4 MiB safety cushion after the shrunk filesystem so resizepart never clips trailing blocks.
    END_MB=$((START_MB + RESIZE_MB_ARG + 4))
    PARTS_BEFORE="$(mktemp "${TMPDIR:-/tmp}/finn-parts-before.XXXXXX")"
    PARTS_AFTER="$(mktemp "${TMPDIR:-/tmp}/finn-parts-after.XXXXXX")"
    lsblk -lno PATH,TYPE "$DISK" | awk '$2=="part"{print $1}' | sort > "$PARTS_BEFORE"
    printf 'Yes\nYes\n' | parted ---pretend-input-tty "$DISK" unit MiB resizepart "$PART_NUM" "${END_MB}MiB" || die "parted resizepart failed"
    PT_TYPE="$(blkid -s PTTYPE -o value "$DISK" 2>/dev/null || true)"
    if [ "$PT_TYPE" = "dos" ]; then
        parted -s "$DISK" unit MiB mkpart primary ext4 "${END_MB}MiB" 100% || die "parted mkpart failed (MBR disks allow at most 4 primary partitions)"
    else
        parted -s "$DISK" unit MiB mkpart FinnOS ext4 "${END_MB}MiB" 100% || die "parted mkpart failed"
    fi
    partprobe "$DISK" 2>/dev/null || true
    udevadm settle --timeout=30 2>/dev/null || true
    lsblk -lno PATH,TYPE "$DISK" | awk '$2=="part"{print $1}' | sort > "$PARTS_AFTER"
    ROOT="$(comm -13 "$PARTS_BEFORE" "$PARTS_AFTER" | head -n1 || true)"
    rm -f "$PARTS_BEFORE" "$PARTS_AFTER"
    [ -b "$ROOT" ] || ROOT="$(lsblk -lno PATH,TYPE "$DISK" | awk '$2=="part"{p=$1} END{print p}')"
    ESP="$ESP_PART_ARG"
    step "[3/8] FORMAT FINNOS ROOT (PRESERVING EXISTING OS)"
    mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
    step "[4/8] MOUNT"
    mount "$ROOT" /mnt || die "could not mount $ROOT at /mnt"
    mkdir -p /mnt/boot
    if [ -n "$ESP" ]; then
        mount "$ESP" /mnt/boot || die "could not mount $ESP at /mnt/boot"
        findmnt -o TARGET,SOURCE,FSTYPE /mnt/boot
    fi
    findmnt -o TARGET,SOURCE,FSTYPE /mnt
elif [ "$INSTALL_MODE" = "partition" ] || [ "$INSTALL_MODE" = "reinstall" ]; then
    [ -b "$ROOT_PART_ARG" ] || die "--root-part is required for $INSTALL_MODE"
    if [ -n "$ESP_PART_ARG" ]; then
        [ -b "$ESP_PART_ARG" ] || die "ESP partition $ESP_PART_ARG is not a block device"
    fi
    ROOT="$ROOT_PART_ARG"
    ESP="$ESP_PART_ARG"
    if [ "$INSTALL_MODE" = "partition" ]; then
        step "[3/8] FORMAT SELECTED PARTITION (PRESERVING OTHER OSES)"
        mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
    else
        step "[3/8] REINSTALL (PRESERVING USER DATA ON $ROOT)"
    fi
    step "[4/8] MOUNT"
    mount "$ROOT" /mnt || die "could not mount $ROOT at /mnt"
    mkdir -p /mnt/boot
    if [ -n "$ESP" ]; then
        mount "$ESP" /mnt/boot || die "could not mount $ESP at /mnt/boot"
        findmnt -o TARGET,SOURCE,FSTYPE /mnt/boot
    fi
    findmnt -o TARGET,SOURCE,FSTYPE /mnt
else
# Clean whole-disk erase path below.
# The signatures go first, and only on this device and its own partitions:
# wiping the table does not touch what is inside a partition, and a stale
# filesystem superblock is what makes a machine come back as what it was.
while read -r name; do
    [ -b "/dev/$name" ] || continue
    wipefs -a "/dev/$name" || die "wipefs failed on /dev/$name"
done <<< "$(lsblk -lno NAME "$DISK" | tail -n +2)"
wipefs -a "$DISK" || die "wipefs failed on $DISK"

if [ "$AB_SLOTS" = 1 ]; then
    printf 'label: gpt\nsize=1024M, type=U, name="FINNOS_EFI"\nsize=1M, type=21686148-6449-6E6F-744E-656564454649, name="BIOS_BOOT"\nsize=%sM, type=L, name="FINNOS_A"\nsize=%sM, type=L, name="FINNOS_B"\ntype=L, name="FINNOS_DATA"\n' \
        "$SLOT_MIB" "$SLOT_MIB" |
        sfdisk --wipe=always "$DISK" ||
        die "sfdisk did not write A/B partition table on $DISK"
else
    # Three partitions: an EFI system partition for /boot, a one-megabyte BIOS
    # boot partition, and the root. The BIOS boot partition is where GRUB puts
    # its boot code on a GPT disk; without it a machine that starts with a BIOS
    # has an install it cannot reach, and the firmware is not always willing
    # to boot an EFI disk on its own. Its type is named by GUID: the short "B"
    # alias is a DOS type, and sfdisk refuses to add a GPT partition with it.
    printf 'label: gpt\n,512M,U\n,+1M,21686148-6449-6E6F-744E-656564454649\n,,L\n' |
        sfdisk --wipe=always "$DISK" ||
        die "sfdisk did not write a partition table on $DISK"
fi
sfdisk -d "$DISK" >/dev/null || die "no partition table on $DISK"

sync
partprobe "$DISK" 2>/dev/null || true
udevadm settle --timeout=30 2>/dev/null || true
for _ in $(seq 1 30); do
    if [ "$AB_SLOTS" = 1 ]; then
        [ -b "$ESP" ] && [ -b "$BIOSBOOT" ] && [ -b "$ROOT" ] && [ -b "$SLOT_B" ] && [ -b "$DATA_PART" ] && break
    else
        [ -b "$ESP" ] && [ -b "$BIOSBOOT" ] && [ -b "$ROOT" ] && break
    fi
    sleep 1
done
if [ "$AB_SLOTS" = 1 ]; then
    [ -b "$ESP" ] && [ -b "$BIOSBOOT" ] && [ -b "$ROOT" ] && [ -b "$SLOT_B" ] && [ -b "$DATA_PART" ] ||
        die "the A/B partition nodes did not appear on $DISK after partitioning"
else
    [ -b "$ESP" ] && [ -b "$BIOSBOOT" ] && [ -b "$ROOT" ] ||
        die "the partition nodes did not appear on $DISK after partitioning"
fi
lsblk "$DISK"
post

# 3. the filesystems.
step "[3/8] FORMAT"
mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
if [ "$AB_SLOTS" = 1 ]; then
    mkfs.ext4 -F -L FINNOS_A "$ROOT" || die "mkfs.ext4 failed on $ROOT"
    mkfs.ext4 -F -L FINNOS_B "$SLOT_B" || die "mkfs.ext4 failed on $SLOT_B"
    mkfs.ext4 -F -L FINNOS_DATA "$DATA_PART" || die "mkfs.ext4 failed on $DATA_PART"
    [ "$(blkid -s TYPE -o value "$SLOT_B")" = "ext4" ] || die "$SLOT_B is not ext4"
    [ "$(blkid -s TYPE -o value "$DATA_PART")" = "ext4" ] || die "$DATA_PART is not ext4"
else
    mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
fi
[ "$(blkid -s TYPE -o value "$ESP")"  = "vfat" ] || die "$ESP is not vfat"
[ "$(blkid -s TYPE -o value "$ROOT")" = "ext4" ] || die "$ROOT is not ext4"
post

# 4. the mounts, checked by source rather than assumed from the exit code.
step "[4/8] MOUNT"
mount "$ROOT" /mnt || die "could not mount $ROOT at /mnt"
# /mnt/boot is made on the new root, after the root is mounted: a directory
# made before it would be covered by the root mount and the ESP would have
# nowhere to go.
mkdir -p /mnt/boot
mount "$ESP" /mnt/boot || die "could not mount $ESP at /mnt/boot"
[ "$(findmnt -no SOURCE -T /mnt)" = "$ROOT" ] || die "$ROOT is not what is mounted at /mnt"
[ "$(findmnt -no SOURCE -T /mnt/boot)" = "$ESP" ] || die "$ESP is not what is mounted at /mnt/boot"
if [ "$AB_SLOTS" = 1 ]; then
    mkdir -p /mnt/data
    mount "$DATA_PART" /mnt/data || die "could not mount $DATA_PART at /mnt/data"
    [ "$(findmnt -no SOURCE -T /mnt/data)" = "$DATA_PART" ] || die "$DATA_PART is not what is mounted at /mnt/data"
    findmnt -o TARGET,SOURCE,FSTYPE /mnt/data
fi
# findmnt takes one target. Asking for both at once is a usage error that
# returns 1 without a message, and set -e would stop the install here.
findmnt -o TARGET,SOURCE,FSTYPE /mnt
findmnt -o TARGET,SOURCE,FSTYPE /mnt/boot
post
fi

# 5. the base system.
step "[5/8] BASE SYSTEM"
if [ -n "$OFFLINE_ROOT" ]; then
    echo "installing FinnOS offline directly from live image ($OFFLINE_ROOT) — no internet required"
    if [ "$INSTALL_MODE" = "reinstall" ]; then
        # Refresh system directories while preserving /home, OOBE marker, and Wi-Fi profiles.
        for d in usr bin lib lib64 sbin var/lib/pacman opt/finnos; do
            if [ -e "$OFFLINE_ROOT/$d" ]; then
                mkdir -p "/mnt/$(dirname "$d")"
                cp -a "$OFFLINE_ROOT/$d" "/mnt/$(dirname "$d")/" || die "offline copy failed on $d"
            fi
        done
    else
        cp -a "$OFFLINE_ROOT/." /mnt/ || die "offline copy from $OFFLINE_ROOT to /mnt failed"
    fi
    mkdir -p /mnt/boot /mnt/dev /mnt/proc /mnt/sys /mnt/run /mnt/tmp
    chmod 1777 /mnt/tmp
    if [ -n "$OFFLINE_KERNEL" ] && [ -f "$OFFLINE_KERNEL" ]; then
        install -m 644 "$OFFLINE_KERNEL" /mnt/boot/vmlinuz-linux
    elif [ ! -f /mnt/boot/vmlinuz-linux ]; then
        for kcand in /mnt/usr/lib/modules/*/vmlinuz; do
            if [ -f "$kcand" ]; then
                install -m 644 "$kcand" /mnt/boot/vmlinuz-linux
                break
            fi
        done
    fi
    [ -f /mnt/boot/vmlinuz-linux ] || die "kernel vmlinuz-linux missing after offline copy"
    # Strip live-ISO-only hooks/units so the installed machine boots into FinnOS desktop, not live recovery.
    rm -f /mnt/root/.zprofile /mnt/root/.bash_profile /mnt/root/.hushlogin
    rm -rf /mnt/etc/systemd/system/getty@tty1.service.d
    rm -f /mnt/etc/systemd/system/multi-user.target.wants/pacman-init.service \
          /mnt/etc/systemd/system/pacman-init.service \
          /mnt/etc/systemd/system/etc-pacman.d-gnupg.mount \
          /mnt/etc/mkinitcpio.conf.d/archiso.conf \
          /mnt/etc/machine-id
    echo "offline base system deployment complete"
else
# Online fallback when not booted from the live squashfs image.
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
              intel-ucode amd-ucode open-vm-tools virtualbox-guest-utils-nox
              qemu-guest-agent"
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
fi
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
install -Dm644 /etc/pacman.d/finnos-mirrorlist /mnt/etc/pacman.d/finnos-mirrorlist
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
export FINN_HOSTNAME="${HOSTNAME_ARG:-${FINN_HOSTNAME:-finnos}}"
export FINN_TIMEZONE="${TIMEZONE_ARG:-${FINN_TIMEZONE:-UTC}}"
export FINN_AB_SLOTS="$AB_SLOTS"
# A machine installed over a serial console keeps that console. The live
# medium knows because it was told; the chroot has no kernel command line
# to read, so the answer is handed to it.
export FINN_SERIAL="$SERIAL"
arch-chroot /mnt /root/finn-chroot.sh ||
    die "the machine could not be configured; see the chroot log above"
post

# 8. the install is a claim, so it is checked on the disk that was written:
#    a runnable compositor, a session broker and a shell, and a boot entry that
#    names the root by UUID. Anything less is not an install.
step "[8/8] VERIFY"
[ -x /mnt/usr/local/bin/finn-comp ] ||
    die "the compositor was not installed: /usr/local/bin/finn-comp is missing or not executable"
[ -x /mnt/usr/local/bin/finn-session ] ||
    die "the session broker was not installed: /usr/local/bin/finn-session is missing or not executable"
[ -x /mnt/usr/local/bin/finn-shell ] ||
    die "the shell was not installed: /usr/local/bin/finn-shell is missing or not executable"
[ -e /mnt/etc/systemd/system/graphical.target.wants/finn-comp.service ] ||
    die "finn-comp is installed but not enabled, so the machine would boot to a console"
[ -e /mnt/etc/systemd/system/graphical.target.wants/finn-session.service ] ||
    die "finn-session is installed but not enabled"
[ -e /mnt/etc/systemd/system/graphical.target.wants/finn-shell.service ] ||
    die "finn-shell is installed but not enabled"

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
               wine-mono wine-gecko open-vm-tools virtualbox-guest-utils-nox \
               qemu-guest-agent; do
        installed_pkg "$pkg" || die "$pkg is not installed on $DISK"
    done
    HARDWARE="x86_64: firmware, microcode, mesa, vulkan, wine, audio, bluetooth"
else
    installed_pkg linux-aarch64 ||
        die "the linux-aarch64 kernel is not installed on $DISK"
    HARDWARE="aarch64: firmware, mesa, audio, bluetooth"
fi
echo "HARDWARE=$HARDWARE"
# The test that watches an unattended install looks for this line; the log
# post above the boot entry can still be behind tee when it arrives.
post_line "HARDWARE=$HARDWARE"

bootentry=""
if [ -f /mnt/boot/grub/grub.cfg ] &&
   { grep -q "root=UUID=$ROOTUUID" /mnt/boot/grub/grub.cfg || grep -q "root=PARTLABEL=FINNOS_A" /mnt/boot/grub/grub.cfg; }; then
    bootentry=grub
elif compgen -G '/mnt/boot/loader/entries/*.conf' >/dev/null &&
     { grep -q "root=UUID=$ROOTUUID" /mnt/boot/loader/entries/*.conf || grep -q "root=PARTLABEL=FINNOS_A" /mnt/boot/loader/entries/*.conf; }; then
    bootentry=systemd-boot
fi
[ -n "$bootentry" ] || die "no boot entry on $DISK names root=UUID=$ROOTUUID or root=PARTLABEL=FINNOS_A"

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
