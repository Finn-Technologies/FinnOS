#!/bin/bash
# FinnOS universal offline-first installer & recovery backend.
#
#   finn-install --disk /dev/nvme0n1 --mode clean --table gpt --offline-only --yes
#   finn-install --disk /dev/nvme0n1 --mode dualboot-resize --resize-part /dev/nvme0n1p3 --resize-mb 150000 --yes
#   finn-install --disk /dev/nvme0n1 --mode dualboot-free --free-start-mb 130000 --free-end-mb 250000 --yes
#   finn-install --disk /dev/nvme0n1 --mode partition --root-part /dev/nvme0n1p4 --esp-action split-esp --yes
#   finn-install --disk /dev/nvme0n1 --mode reinstall --root-part /dev/nvme0n1p4 --yes
#   finn-install --disk /dev/nvme0n1 --mode repair-boot --root-part /dev/nvme0n1p4 --yes
#
# Installs directly from the local offline OS image carried inside the live
# installation media (/run/archiso/airootfs or airootfs.sfs) or a dedicated
# FINNOS_RECOVERY partition — zero internet access required.
#
# Automatically handles GPT and MBR disks, UEFI and Legacy BIOS firmware,
# dual-booting alongside Windows / macOS / Linux, tiny 100 MiB Windows ESPs
# (mounted at /boot/efi with GRUB UEFI so mkinitcpio never hits ENOSPC),
# missing EFI or GPT BIOS Boot partitions (generated automatically), trailing
# Windows Recovery partitions (bounded to exact [NEW_END, ORIG_END]), and MBR
# extended/logical partitions.
set -euo pipefail

HOST="${FINN_HOST_URL:-}"
ISO_MNT="${FINN_ISO_MNT:-/finnos-iso}"
MNT="${FINN_TARGET_MNT:-/mnt}"
LIVE_AIROOTFS="${FINN_LIVE_AIROOTFS:-/run/archiso/airootfs}"
LIVE_BOOTMNT="${FINN_LIVE_BOOTMNT:-/run/archiso/bootmnt}"
RECOVERY_MNT="${FINN_RECOVERY_MNT:-/run/finn/recovery}"
SYS_EFI="${FINN_SYS_EFI:-/sys/firmware/efi}"
LOG="${FINN_INSTALL_LOG:-/tmp/finn-install.log}"

SRC=""
DISK=""
DISK_ARG=""
HOSTNAME_ARG=""
TIMEZONE_ARG=""
INSTALL_MODE="clean"
TABLE_KIND="gpt"
FIRMWARE_ARG=""
ESP_ACTION="auto"
ROOT_PART_ARG=""
ESP_PART_ARG=""
RESIZE_PART_ARG=""
RESIZE_MB_ARG=""
FREE_START_MB=""
FREE_END_MB=""
IMAGE_ARG="${FINN_IMAGE_PATH:-}"
NEED_BIOS_BOOT=0
CREATE_RECOVERY_PART=0
OFFLINE_ONLY=0
AB_SLOTS=0
ASSUME_YES=0

# Mirror output to serial console when the kernel cmdline explicitly enabled ttyS0.
SERIAL=0
if grep -qw 'console=ttyS0' /proc/cmdline 2>/dev/null && [ -w /dev/ttyS0 ]; then
    SERIAL=1
fi
LOG_DIR=""
LOG_TEE=""
: >"$LOG"
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
    exec 3>&2
    exec >>"$LOG" 2>&1
    echo "warning: the installer log cannot be mirrored to this console" >&3
    exec 3>&-
fi

# Best-effort CI log posting only when an explicit host capture URL is active
# and --offline-only was NOT requested.
post() {
    [ "$OFFLINE_ONLY" = 0 ] || return 0
    [ -n "$HOST" ] || return 0
    curl -s --connect-timeout 2 --max-time 5 \
         -X POST --data-binary @"$LOG" "$HOST/log" -o /dev/null 2>/dev/null || true
}

post_line() {
    [ "$OFFLINE_ONLY" = 0 ] || return 0
    [ -n "$HOST" ] || return 0
    curl -s --connect-timeout 2 --max-time 5 \
         --data-binary "$1" "$HOST/log" -o /dev/null 2>/dev/null || true
}

STEP="arguments"
die() { echo "error: $*" >&2; exit 1; }

CLEANUP_MOUNTS=()
cleanup_temp_mounts() {
    local idx
    for (( idx=${#CLEANUP_MOUNTS[@]}-1; idx>=0; idx-- )); do
        umount "${CLEANUP_MOUNTS[$idx]}" 2>/dev/null || true
        rmdir "${CLEANUP_MOUNTS[$idx]}" 2>/dev/null || true
    done
    CLEANUP_MOUNTS=()
}

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

failed() {
    local rc=$?
    cleanup_temp_mounts
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

  --disk <device>            target whole disk, e.g. /dev/nvme0n1 or /dev/sda
  --mode <mode>              clean (default), dualboot-free, dualboot-resize,
                             partition, reinstall, repair-boot
  --table <gpt|mbr>          partition table for clean erase (default: gpt)
  --firmware <uefi|bios>     firmware mode override (default: auto-detect)
  --esp-action <action>      auto, share-boot, share-boot-efi, format-esp,
                             create-esp, split-esp, new-table, none
  --root-part <dev>          target/existing FinnOS root partition
  --esp-part <dev>           existing EFI System Partition to share/repair
  --resize-part <dev>        existing Windows/Linux partition to shrink
  --resize-mb <mib>          new size in MiB for --resize-part
  --free-start-mb <mib>      unallocated gap start in MiB (dualboot-free)
  --free-end-mb <mib>        unallocated gap end in MiB (dualboot-free)
  --image <path>             local offline OS image (dir, .sfs, or .tar.zst)
  --offline-only             strictly forbid any internet/network fetch
  --need-bios-boot           auto-create 1 MiB BIOS Boot (ef02) on GPT disk
  --create-recovery-part     create FINNOS_RECOVERY partition with offline image
  --hostname <name>          optional CLI override (default: finnos; set in OOBE)
  --timezone <tz>            optional CLI override (default: UTC; set in OOBE)
  --ab-slots                 partition for OTA A/B slots (FINNOS_A/B/DATA)
  --yes                      unattended: do not prompt for confirmation
  -h, --help                 show this help

There is no default disk, and finn-install never guesses one.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --disk)             [ $# -ge 2 ] || die "--disk needs a device"; DISK_ARG="$2"; shift 2 ;;
        --disk=*)           DISK_ARG="${1#*=}"; shift ;;
        --mode)             [ $# -ge 2 ] || die "--mode needs a value"; INSTALL_MODE="$2"; shift 2 ;;
        --mode=*)           INSTALL_MODE="${1#*=}"; shift ;;
        --table)            [ $# -ge 2 ] || die "--table needs gpt or mbr"; TABLE_KIND="$2"; shift 2 ;;
        --table=*)          TABLE_KIND="${1#*=}"; shift ;;
        --firmware)         [ $# -ge 2 ] || die "--firmware needs uefi or bios"; FIRMWARE_ARG="$2"; shift 2 ;;
        --firmware=*)       FIRMWARE_ARG="${1#*=}"; shift ;;
        --esp-action)       [ $# -ge 2 ] || die "--esp-action needs a value"; ESP_ACTION="$2"; shift 2 ;;
        --esp-action=*)     ESP_ACTION="${1#*=}"; shift ;;
        --root-part)        [ $# -ge 2 ] || die "--root-part needs a partition"; ROOT_PART_ARG="$2"; shift 2 ;;
        --root-part=*)      ROOT_PART_ARG="${1#*=}"; shift ;;
        --esp-part)         [ $# -ge 2 ] || die "--esp-part needs a partition"; ESP_PART_ARG="$2"; shift 2 ;;
        --esp-part=*)       ESP_PART_ARG="${1#*=}"; shift ;;
        --resize-part)      [ $# -ge 2 ] || die "--resize-part needs a partition"; RESIZE_PART_ARG="$2"; shift 2 ;;
        --resize-part=*)    RESIZE_PART_ARG="${1#*=}"; shift ;;
        --resize-mb)        [ $# -ge 2 ] || die "--resize-mb needs MiB"; RESIZE_MB_ARG="$2"; shift 2 ;;
        --resize-mb=*)      RESIZE_MB_ARG="${1#*=}"; shift ;;
        --free-start-mb)    [ $# -ge 2 ] || die "--free-start-mb needs MiB"; FREE_START_MB="$2"; shift 2 ;;
        --free-start-mb=*)  FREE_START_MB="${1#*=}"; shift ;;
        --free-end-mb)      [ $# -ge 2 ] || die "--free-end-mb needs MiB"; FREE_END_MB="$2"; shift 2 ;;
        --free-end-mb=*)    FREE_END_MB="${1#*=}"; shift ;;
        --image)            [ $# -ge 2 ] || die "--image needs a path"; IMAGE_ARG="$2"; shift 2 ;;
        --image=*)          IMAGE_ARG="${1#*=}"; shift ;;
        --offline-only)     OFFLINE_ONLY=1; shift ;;
        --need-bios-boot)   NEED_BIOS_BOOT=1; shift ;;
        --create-recovery-part) CREATE_RECOVERY_PART=1; shift ;;
        --hostname)         [ $# -ge 2 ] || die "--hostname needs a value"; HOSTNAME_ARG="$2"; shift 2 ;;
        --hostname=*)       HOSTNAME_ARG="${1#*=}"; shift ;;
        --timezone)         [ $# -ge 2 ] || die "--timezone needs a zone"; TIMEZONE_ARG="$2"; shift 2 ;;
        --timezone=*)       TIMEZONE_ARG="${1#*=}"; shift ;;
        --ab-slots)         AB_SLOTS=1; shift ;;
        --yes|-y)           ASSUME_YES=1; shift ;;
        -h|--help)          usage; exit 0 ;;
        *)                  usage >&2; die "unknown argument: $1" ;;
    esac
done

# Unattended kernel command-line parameters (used by automated QEMU/CI installs).
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

# Determine firmware mode honestly (UEFI vs Legacy BIOS).
if [ -z "$FIRMWARE_ARG" ]; then
    if [ -d "$SYS_EFI" ]; then
        FIRMWARE_ARG="uefi"
    else
        FIRMWARE_ARG="bios"
    fi
fi
[ "$FIRMWARE_ARG" = "uefi" ] || [ "$FIRMWARE_ARG" = "bios" ] || die "invalid --firmware: $FIRMWARE_ARG"

if [ -z "$DISK_ARG" ]; then
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
        if [ ! -b "$DISK_ARG" ] && [ "${FINN_ALLOW_FILE_DISK:-0}" != 1 ]; then
            echo "not a block device: $DISK_ARG" >&2
            continue
        fi
        break
    done
fi

is_dev_node() {
    [ -b "$1" ] || { [ "${FINN_ALLOW_FILE_DISK:-0}" = 1 ] && [ -e "$1" ]; }
}

# Verify SHA-512 sidecar if present next to an offline image file.
verify_sha512_sidecar() {
    local target="$1"
    local sidecar="$2"
    [ -f "$sidecar" ] || return 0
    [ -f "$target" ] || die "checksum sidecar $sidecar exists, but image $target is missing"
    local expected actual
    expected="$(awk '{print tolower($1); exit}' "$sidecar")"
    [ "${#expected}" -eq 128 ] || die "malformed SHA-512 checksum file: $sidecar"
    if command -v sha512sum >/dev/null 2>&1; then
        actual="$(sha512sum "$target" | awk '{print tolower($1)}')"
    elif command -v shasum >/dev/null 2>&1; then
        actual="$(shasum -a 512 "$target" | awk '{print tolower($1)}')"
    else
        die "neither sha512sum nor shasum is available to verify $target"
    fi
    if [ "$actual" != "$expected" ]; then
        die "SHA-512 integrity check FAILED for $target (expected $expected, got $actual)"
    fi
    echo "image integrity verified (SHA-512): $(basename "$target")"
}

# ── Step 0: Locate & verify the local offline OS image ───────────────────────
step "[0/8] VERIFY OFFLINE OS IMAGE"
OFFLINE_ROOT=""
OFFLINE_IMAGE_FILE=""
OFFLINE_IMAGE_KIND=""
OFFLINE_KERNEL=""
OFFLINE_SFS_PATH=""
OFFLINE_SFS_SHA=""
ARCH_NOW="$(uname -m)"

find_kernel_near() {
    local base="$1"
    local kcand
    for kcand in \
        "$base/finn/boot/$ARCH_NOW/vmlinuz-linux" \
        "$base/finn/boot/$ARCH_NOW/vmlinuz-linux-aarch64" \
        "$base/vmlinuz-linux" \
        "$base/boot/vmlinuz-linux" \
        "$LIVE_BOOTMNT/finn/boot/$ARCH_NOW/vmlinuz-linux" \
        "$LIVE_BOOTMNT/finn/boot/$ARCH_NOW/vmlinuz-linux-aarch64" \
        "$LIVE_AIROOTFS"/usr/lib/modules/*/vmlinuz \
        /usr/lib/modules/*/vmlinuz; do
        if [ -f "$kcand" ]; then
            OFFLINE_KERNEL="$kcand"
            return 0
        fi
    done
    return 1
}

# 1. Explicit --image override
if [ -n "$IMAGE_ARG" ]; then
    if [ -d "$IMAGE_ARG/usr/bin" ] && [ -d "$IMAGE_ARG/var/lib/pacman/local" ]; then
        OFFLINE_ROOT="$IMAGE_ARG"
        OFFLINE_IMAGE_KIND="dir"
        find_kernel_near "$(dirname "$IMAGE_ARG")" || true
    elif [ -f "$IMAGE_ARG" ]; then
        OFFLINE_IMAGE_FILE="$IMAGE_ARG"
        case "$IMAGE_ARG" in
            *.sfs)
                OFFLINE_IMAGE_KIND="sfs"
                OFFLINE_SFS_PATH="$IMAGE_ARG"
                [ -f "${IMAGE_ARG%.sfs}.sha512" ] && OFFLINE_SFS_SHA="${IMAGE_ARG%.sfs}.sha512"
                [ -f "${IMAGE_ARG}.sha512" ] && OFFLINE_SFS_SHA="${IMAGE_ARG}.sha512"
                [ -n "$OFFLINE_SFS_SHA" ] && verify_sha512_sidecar "$OFFLINE_IMAGE_FILE" "$OFFLINE_SFS_SHA"
                ;;
            *.tar.zst|*.tar.gz|*.tar)
                OFFLINE_IMAGE_KIND="tar"
                [ -f "${IMAGE_ARG}.sha512" ] && verify_sha512_sidecar "$OFFLINE_IMAGE_FILE" "${IMAGE_ARG}.sha512"
                ;;
            *)
                die "unsupported offline image format: $IMAGE_ARG"
                ;;
        esac
        find_kernel_near "$(dirname "$IMAGE_ARG")" || true
    else
        die "specified --image does not exist or is invalid: $IMAGE_ARG"
    fi
fi

# 2. Active live squashfs root (/run/archiso/airootfs)
if [ -z "$OFFLINE_IMAGE_KIND" ] && [ -d "$LIVE_AIROOTFS/usr/bin" ] && [ -d "$LIVE_AIROOTFS/var/lib/pacman/local" ]; then
    OFFLINE_ROOT="$LIVE_AIROOTFS"
    OFFLINE_IMAGE_KIND="dir"
    if [ -f "$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sfs" ]; then
        OFFLINE_SFS_PATH="$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sfs"
        if [ -f "$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sha512" ]; then
            OFFLINE_SFS_SHA="$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sha512"
            verify_sha512_sidecar "$OFFLINE_SFS_PATH" "$OFFLINE_SFS_SHA"
        fi
    fi
    find_kernel_near "$LIVE_BOOTMNT" || true
fi

# 3. Raw squashfs on live bootmnt (/run/archiso/bootmnt/finn/<arch>/airootfs.sfs)
if [ -z "$OFFLINE_IMAGE_KIND" ] && [ -f "$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sfs" ]; then
    OFFLINE_IMAGE_FILE="$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sfs"
    OFFLINE_IMAGE_KIND="sfs"
    OFFLINE_SFS_PATH="$OFFLINE_IMAGE_FILE"
    if [ -f "$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sha512" ]; then
        OFFLINE_SFS_SHA="$LIVE_BOOTMNT/finn/$ARCH_NOW/airootfs.sha512"
        verify_sha512_sidecar "$OFFLINE_IMAGE_FILE" "$OFFLINE_SFS_SHA"
    fi
    find_kernel_near "$LIVE_BOOTMNT" || true
fi

# 4. Local Recovery Partition (mounted at /run/finn/recovery, /recovery, or discovered by LABEL=FINNOS_RECOVERY)
if [ -z "$OFFLINE_IMAGE_KIND" ]; then
    REC_SEARCH=("$RECOVERY_MNT" "/recovery" "/var/lib/finn/recovery")
    for rec_dir in "${REC_SEARCH[@]}"; do
        for cand in "$rec_dir/finnos.sfs" "$rec_dir/recovery.sfs" "$rec_dir/airootfs.sfs" "$rec_dir/finnos-rootfs.tar.zst"; do
            if [ -f "$cand" ]; then
                OFFLINE_IMAGE_FILE="$cand"
                case "$cand" in
                    *.sfs)
                        OFFLINE_IMAGE_KIND="sfs"
                        OFFLINE_SFS_PATH="$cand"
                        [ -f "${cand%.sfs}.sha512" ] && OFFLINE_SFS_SHA="${cand%.sfs}.sha512"
                        [ -n "$OFFLINE_SFS_SHA" ] && verify_sha512_sidecar "$cand" "$OFFLINE_SFS_SHA"
                        ;;
                    *.tar.zst)
                        OFFLINE_IMAGE_KIND="tar"
                        [ -f "${cand}.sha512" ] && verify_sha512_sidecar "$cand" "${cand}.sha512"
                        ;;
                esac
                find_kernel_near "$rec_dir" || true
                break 2
            fi
        done
    done
fi

if [ -z "$OFFLINE_IMAGE_KIND" ] && command -v blkid >/dev/null 2>&1; then
    for rdev in $(blkid -o device -t LABEL=FINNOS_RECOVERY 2>/dev/null || true); do
        TMP_REC="$(mktemp -d "${TMPDIR:-/tmp}/finn-rec-part.XXXXXX")"
        if mount -o ro "$rdev" "$TMP_REC" 2>/dev/null; then
            CLEANUP_MOUNTS+=("$TMP_REC")
            for cand in "$TMP_REC/finnos.sfs" "$TMP_REC/airootfs.sfs" "$TMP_REC/finnos-rootfs.tar.zst"; do
                if [ -f "$cand" ]; then
                    OFFLINE_IMAGE_FILE="$cand"
                    case "$cand" in
                        *.sfs)
                            OFFLINE_IMAGE_KIND="sfs"
                            OFFLINE_SFS_PATH="$cand"
                            [ -f "${cand%.sfs}.sha512" ] && OFFLINE_SFS_SHA="${cand%.sfs}.sha512"
                            [ -n "$OFFLINE_SFS_SHA" ] && verify_sha512_sidecar "$cand" "$OFFLINE_SFS_SHA"
                            ;;
                        *.tar.zst)
                            OFFLINE_IMAGE_KIND="tar"
                            [ -f "${cand}.sha512" ] && verify_sha512_sidecar "$cand" "${cand}.sha512"
                            ;;
                    esac
                    find_kernel_near "$TMP_REC" || true
                    break 2
                fi
            done
        else
            rmdir "$TMP_REC" 2>/dev/null || true
        fi
    done
fi

if [ -n "$OFFLINE_IMAGE_KIND" ]; then
    echo "offline OS image ready ($OFFLINE_IMAGE_KIND): ${OFFLINE_ROOT:-$OFFLINE_IMAGE_FILE} (kernel: ${OFFLINE_KERNEL:-in-root})"
elif [ "$OFFLINE_ONLY" = 1 ] && [ "$INSTALL_MODE" != "repair-boot" ]; then
    die "no local offline FinnOS image found (checked $LIVE_AIROOTFS, $LIVE_BOOTMNT, $RECOVERY_MNT, and LABEL=FINNOS_RECOVERY); refusing internet download under --offline-only"
fi

# Locate source tree for finn-chroot.sh, assets, and scripts.
if [ -n "${FINN_SRC_DIR:-}" ] && [ -f "${FINN_SRC_DIR}/vm/finn-chroot.sh" ]; then
    SRC="$FINN_SRC_DIR"
    echo "payload found: $SRC"
elif [ -f /opt/finnos/vm/finn-chroot.sh ]; then
    SRC=/opt/finnos
    echo "payload found in the live root: $SRC"
elif [ -n "$OFFLINE_ROOT" ] && [ -f "$OFFLINE_ROOT/opt/finnos/vm/finn-chroot.sh" ]; then
    SRC="$OFFLINE_ROOT/opt/finnos"
    echo "payload found in offline image: $SRC"
else
    mkdir -p "$ISO_MNT"
    for dev in $(blkid -o device -t LABEL=FINNOS 2>/dev/null || true); do
        mount "$dev" "$ISO_MNT" && break
    done
    if mountpoint -q "$ISO_MNT" 2>/dev/null && [ -f "$ISO_MNT/vm/finn-chroot.sh" ]; then
        SRC="$ISO_MNT"
        echo "install disc found on $ISO_MNT"
    else
        umount "$ISO_MNT" 2>/dev/null || true
        if [ "$OFFLINE_ONLY" = 1 ] && [ -z "$OFFLINE_IMAGE_KIND" ] && [ "$INSTALL_MODE" != "repair-boot" ]; then
            die "offline source tree (/opt/finnos) is missing"
        fi
    fi
fi
post

# ── Step 1: Host prerequisite check (strictly offline when image is present) ─
step "[1/8] PREREQS"
if [ -z "$OFFLINE_IMAGE_KIND" ] && [ "$INSTALL_MODE" != "repair-boot" ]; then
    [ "$OFFLINE_ONLY" = 0 ] || die "cannot run online fallback when --offline-only is enabled"
    mkdir -p /etc/pacman.d
    if [ -n "$SRC" ] && [ -f "$SRC/repo/finnos-mirrorlist" ]; then
        install -m 644 "$SRC/repo/finnos-mirrorlist" /etc/pacman.d/finnos-mirrorlist
    else
        cat > /etc/pacman.d/finnos-mirrorlist <<'EOF'
# FinnOS pacman repository mirrorlist (/etc/pacman.d/finnos-mirrorlist)
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
    if [ -f /etc/pacman.d/mirrorlist ]; then
        if ! grep -q '^# FinnOS upstream fallback mirrorlist' /etc/pacman.d/mirrorlist; then
            {
                echo '# FinnOS upstream fallback mirrorlist (for [core]/[extra]/[multilib] only)'
                [ -n "${FINN_ARCH_FALLBACK_MIRROR:-}" ] && printf 'Server = %s\n' "$FINN_ARCH_FALLBACK_MIRROR"
                cat /etc/pacman.d/mirrorlist
            } > /etc/pacman.d/mirrorlist.new
            mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
        fi
    elif [ -n "${FINN_ARCH_FALLBACK_MIRROR:-}" ]; then
        {
            echo '# FinnOS upstream fallback mirrorlist (for [core]/[extra]/[multilib] only)'
            printf 'Server = %s\n' "$FINN_ARCH_FALLBACK_MIRROR"
        } > /etc/pacman.d/mirrorlist
    fi
    grep -qE '^[[:space:]]*Server[[:space:]]*=' /etc/pacman.d/mirrorlist 2>/dev/null ||
        die "no fallback mirror configured in /etc/pacman.d/mirrorlist"
    if [ "$ARCH_NOW" = 'x86_64' ]; then
        if ! grep -Fqx '[multilib]' /etc/pacman.conf; then
            if [ -n "$SRC" ] && [ -f "$SRC/base/pacman.multilib.conf" ]; then
                cat "$SRC/base/pacman.multilib.conf" >> /etc/pacman.conf
            else
                cat >> /etc/pacman.conf <<'EOF'

[multilib]
Include = /etc/pacman.d/mirrorlist
EOF
            fi
        fi
        grep -Fqx '[multilib]' /etc/pacman.conf ||
            die "[multilib] is not in /etc/pacman.conf; the x86_64 package list needs it for Wine"
    fi
    if [ ! -s /etc/pacman.d/gnupg/pubring.gpg ]; then
        echo "the live keyring is empty; initializing it"
        pacman-key --init 2>&1 | tail -5 || die "pacman-key --init failed"
        pacman-key --populate 2>&1 | tail -5 || die "pacman-key --populate failed"
    fi
    [ -s /etc/pacman.d/gnupg/pubring.gpg ] || die "live keyring is not initialized"
    if ! command -v pacstrap >/dev/null 2>&1 || ! command -v mkfs.vfat >/dev/null 2>&1 || ! command -v parted >/dev/null 2>&1; then
        pacman -Sy --noconfirm arch-install-scripts dosfstools parted 2>&1 | tail -5 ||
            die "installer tools failed to install"
    fi
    command -v pacstrap >/dev/null || die "pacstrap is not on this live system"
else
    echo "offline mode active: zero network calls; using local image and pre-installed disk tools"
fi
if [ "$INSTALL_MODE" != "repair-boot" ] && [ "$INSTALL_MODE" != "reinstall" ]; then
    command -v mkfs.ext4 >/dev/null || die "mkfs.ext4 is not on this live system"
    if [ "$FIRMWARE_ARG" = "uefi" ] || [ "$TABLE_KIND" = "gpt" ]; then
        command -v mkfs.vfat >/dev/null || die "mkfs.vfat is not on this live system"
    fi
fi
post

# ── Step 2: Validate target disk & protect live/recovery media ───────────────
step "[2/8] PREPARE STORAGE"
is_dev_node "$DISK_ARG" || die "$DISK_ARG is not a block device"
DISK="$(readlink -f "$DISK_ARG" 2>/dev/null || printf '%s' "$DISK_ARG")"
is_dev_node "$DISK" || die "$DISK_ARG does not resolve to a block device"
DISK_TYPE="$(lsblk -dno TYPE "$DISK" 2>/dev/null | head -n1 || true)"
case "$DISK_TYPE" in
    disk|loop|mpath|raid*) ;;
    *) die "$DISK is not a whole disk (type: ${DISK_TYPE:-unknown})" ;;
esac

# nvme0n1 -> nvme0n1p1, mmcblk0 -> mmcblk0p1, loop0 -> loop0p1, sda -> sda1, vda -> vda1.
case "$DISK" in
    *[0-9]) PART="${DISK}p" ;;
    *)      PART="$DISK" ;;
esac

disk_of() {
    local dev="$1" parent dev_dir
    dev="$(readlink -f "$dev" 2>/dev/null || printf '%s' "$dev")"
    dev_dir="$(dirname "$dev")"
    parent="$(lsblk -no PKNAME "$dev" 2>/dev/null | head -n1 || true)"
    printf '%s/%s\n' "$dev_dir" "${parent:-$(basename "$dev")}"
}

umount -R "$MNT" 2>/dev/null || true

# Protect the running OS, live USB, and active recovery partition from EVER being erased.
for mnt in / /boot /boot/efi "$LIVE_BOOTMNT" "$LIVE_AIROOTFS" "$RECOVERY_MNT"; do
    [ -d "$mnt" ] || continue
    src="$(findmnt -no SOURCE -T "$mnt" 2>/dev/null || true)"
    case "$src" in
        /dev/*) ;;
        /*) [ "${FINN_ALLOW_FILE_DISK:-0}" = 1 ] || continue ;;
        *) continue ;;
    esac
    if [ "$(disk_of "$src")" = "$DISK" ]; then
        die "$DISK backs $mnt ($src); refusing to modify the active live/recovery medium"
    fi
done

# Unmount any leftover target-disk mounts or swap cleanly when --yes is passed,
# or refuse interactively if a partition on DISK is busy.
swaps="$(swapon --show=NAME --noheadings 2>/dev/null || true)"
while read -r name type; do
    [ -n "$name" ] || continue
    [ "/dev/$name" = "$DISK" ] && continue
    target="$(findmnt -rno TARGET -S "/dev/$name" 2>/dev/null || true)"
    if [ -n "$target" ]; then
        if [ "$ASSUME_YES" = 1 ]; then
            echo "unmounting target partition /dev/$name from $target"
            umount -R "$target" 2>/dev/null || umount -f "/dev/$name" 2>/dev/null ||
                die "/dev/$name on $DISK is mounted at $target and could not be unmounted"
        else
            die "/dev/$name on $DISK is mounted at $target; unmount it first"
        fi
    fi
    case " $(echo "$swaps") " in
        *" /dev/$name "*)
            if [ "$ASSUME_YES" = 1 ]; then
                swapoff "/dev/$name" 2>/dev/null || die "/dev/$name on $DISK is active swap and could not be disabled"
            else
                die "/dev/$name on $DISK is in use as swap; swapoff it first"
            fi
            ;;
    esac
done <<< "$(lsblk -lno NAME,TYPE "$DISK" 2>/dev/null | tail -n +2)"

if [ "$ASSUME_YES" != 1 ]; then
    echo
    echo "Mode: $INSTALL_MODE  Target: $DISK  Firmware: $FIRMWARE_ARG"
    lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT "$DISK" || true
    printf 'Type the device path (%s) to confirm, anything else aborts: ' "$DISK"
    confirm=""
    read -r confirm || true
    if [ "$confirm" != "$DISK" ] && [ "$confirm" != "$DISK_ARG" ]; then
        die "not confirmed; nothing was written"
    fi
fi

# Helpers for safe, overlap-free GPT & MBR partition creation.
refresh_disk_nodes() {
    sync
    partprobe "$DISK" 2>/dev/null || true
    udevadm settle --timeout=30 2>/dev/null || true
}

snapshot_parts() {
    local out_file="$1"
    lsblk -lno PATH,TYPE "$DISK" 2>/dev/null | awk '$2=="part"{print $1}' | sort > "$out_file"
}

find_new_part() {
    local before="$1" after="$2" newp=""
    snapshot_parts "$after"
    newp="$(comm -13 "$before" "$after" | head -n1 || true)"
    if [ -z "$newp" ] || ! is_dev_node "$newp"; then
        newp="$(lsblk -lno PATH,TYPE "$DISK" 2>/dev/null | awk '$2=="part"{p=$1} END{print p}')"
    fi
    printf '%s\n' "$newp"
}

# Create a partition strictly inside [start_mb, end_mb] on GPT or MBR.
# On MBR, automatically uses an extended + logical container when 3 primary
# partitions already exist so we never fail on the 4-primary limit.
# Usage: create_part_in_range <pt_type> <name> <fs_hint> <start_mb> <end_mb> <role:esp|bios_boot|linux>
CREATED_PART=""
create_part_in_range() {
    local pt_type="$1" name="$2" fs_hint="$3" s_mb="$4" e_mb="$5" role="$6"
    local before after newp pnum
    before="$(mktemp "${TMPDIR:-/tmp}/finn-pb.XXXXXX")"
    after="$(mktemp "${TMPDIR:-/tmp}/finn-pa.XXXXXX")"
    snapshot_parts "$before"

    if [ "$pt_type" = "dos" ] || [ "$pt_type" = "mbr" ] || [ "$pt_type" = "msdos" ]; then
        local prim_count ext_num part_kind
        prim_count="$(parted -ms "$DISK" unit MiB print 2>/dev/null | awk -F: '$1 ~ /^[1-4]$/ {c++} END{print c+0}')"
        ext_num="$(parted -ms "$DISK" unit MiB print 2>/dev/null | awk -F: '$1 ~ /^[1-4]$/ && $5=="extended" {print $1; exit}')"
        if [ -n "$ext_num" ]; then
            part_kind="logical"
        elif [ "$prim_count" -ge 4 ]; then
            rm -f "$before" "$after"
            die "MBR disk $DISK already has 4 primary partitions with no extended partition"
        elif [ "$prim_count" -eq 3 ] && [ "$role" != "esp" ]; then
            # Create extended partition filling the gap, then logical inside it (+1 MiB EBR gap).
            parted -s "$DISK" unit MiB mkpart extended "${s_mb}MiB" "${e_mb}MiB" ||
                { rm -f "$before" "$after"; die "parted mkpart extended failed on $DISK"; }
            refresh_disk_nodes
            snapshot_parts "$before"
            s_mb=$((s_mb + 1))
            part_kind="logical"
        else
            part_kind="primary"
        fi
        parted -s "$DISK" unit MiB mkpart "$part_kind" "$fs_hint" "${s_mb}MiB" "${e_mb}MiB" ||
            { rm -f "$before" "$after"; die "parted mkpart ($part_kind $fs_hint ${s_mb}-${e_mb}MiB) failed on $DISK"; }
    else
        # GPT: fix moved backup header first so parted never fails on cloned/resized disks.
        command -v sgdisk >/dev/null 2>&1 && sgdisk -e "$DISK" >/dev/null 2>&1 || true
        parted -s "$DISK" unit MiB mkpart "$name" "$fs_hint" "${s_mb}MiB" "${e_mb}MiB" ||
            { rm -f "$before" "$after"; die "parted mkpart ($name $fs_hint ${s_mb}-${e_mb}MiB) failed on $DISK"; }
    fi

    refresh_disk_nodes
    newp="$(find_new_part "$before" "$after")"
    rm -f "$before" "$after"
    is_dev_node "$newp" || die "new partition node did not appear on $DISK"
    pnum="$(lsblk -no PARTN "$newp" 2>/dev/null | tr -d '[:space:]')"
    if [ -z "$pnum" ]; then
        pnum="$(printf '%s' "$newp" | sed -E 's/.*[^0-9]([0-9]+)$/\1/')"
    fi

    if [ "$role" = "esp" ] && [ -n "$pnum" ]; then
        parted -s "$DISK" set "$pnum" esp on 2>/dev/null || parted -s "$DISK" set "$pnum" boot on 2>/dev/null || true
    elif [ "$role" = "bios_boot" ] && [ -n "$pnum" ]; then
        parted -s "$DISK" set "$pnum" bios_grub on 2>/dev/null || true
    fi
    CREATED_PART="$newp"
}

# Find largest unallocated gap on DISK >= min_mb MiB. Sets GAP_START_MB and GAP_END_MB.
GAP_START_MB=""
GAP_END_MB=""
find_free_gap_mb() {
    local min_mb="$1"
    GAP_START_MB=""
    GAP_END_MB=""
    local line
    line="$(parted -ms "$DISK" unit MiB print free 2>/dev/null | awk -F: -v min="$min_mb" '
        $5 == "free;" || $5 == "free" {
            gsub(/MiB/, "", $2); gsub(/MiB/, "", $3); gsub(/MiB/, "", $4);
            s = int($2) + 1; e = int($3); sz = e - s;
            if (sz >= min && sz > best) { best = sz; bs = s; be = e; }
        }
        END { if (best >= min) printf "%d %d\n", bs, be; }
    ')"
    if [ -n "$line" ]; then
        read -r GAP_START_MB GAP_END_MB <<< "$line"
    fi
}

# Resolve ESP mount point (/mnt/boot vs /mnt/boot/efi) so 100 MiB Windows ESPs NEVER overflow.
ESP=""
ESP_MOUNT_REL="/boot"
ROOT=""
BIOSBOOT=""
SLOT_B=""
DATA_PART=""
RECOVERY_PART=""

resolve_esp_mount_policy() {
    local esp_dev="$1"
    local pt_type="$2"
    [ -n "$esp_dev" ] || { ESP_MOUNT_REL=""; return 0; }
    if [ "$ESP_ACTION" = "share-boot-efi" ]; then
        ESP_MOUNT_REL="/boot/efi"
        return 0
    fi
    if [ "$ESP_ACTION" = "share-boot" ] || [ "$ESP_ACTION" = "new-table" ] || [ "$ESP_ACTION" = "create-esp" ] || [ "$ESP_ACTION" = "split-esp" ]; then
        if [ "$pt_type" = "dos" ] || [ "$pt_type" = "mbr" ]; then
            ESP_MOUNT_REL="/boot/efi"
        else
            ESP_MOUNT_REL="/boot"
        fi
        return 0
    fi
    # Auto-detect size of existing ESP: if < 300 MiB or MBR disk, mount at /boot/efi (GRUB UEFI).
    local sz_bytes=0
    sz_bytes="$(lsblk -bno SIZE "$esp_dev" 2>/dev/null | head -n1 || echo 0)"
    if [ "$pt_type" = "dos" ] || [ "$pt_type" = "mbr" ] || [ "${sz_bytes:-0}" -lt 314572800 ]; then
        ESP_MOUNT_REL="/boot/efi"
    else
        ESP_MOUNT_REL="/boot"
    fi
}

# ── Mode: Bootloader Repair ──────────────────────────────────────────────────
if [ "$INSTALL_MODE" = "repair-boot" ]; then
    is_dev_node "$ROOT_PART_ARG" || die "--root-part is required for repair-boot"
    PT_TYPE="$(blkid -s PTTYPE -o value "$DISK" 2>/dev/null || echo gpt)"
    ESP="$ESP_PART_ARG"

    # Auto-remediate missing or unformatted ESP on UEFI during Bootloader Repair!
    if [ "$FIRMWARE_ARG" = "uefi" ]; then
        if [ "$ESP_ACTION" = "create-esp" ] || [ -z "$ESP" ]; then
            if [ -z "$ESP" ]; then
                find_free_gap_mb 512
                if [ -n "$GAP_START_MB" ]; then
                    echo "auto-remediation: creating 512 MiB FAT32 EFI System Partition in unallocated space"
                    create_part_in_range "$PT_TYPE" "FINNOS_EFI" "fat32" "$GAP_START_MB" "$((GAP_START_MB + 512))" "esp"
                    ESP="$CREATED_PART"
                    mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on auto-created ESP $ESP"
                fi
            fi
        elif [ "$ESP_ACTION" = "format-esp" ] && [ -n "$ESP" ]; then
            echo "auto-remediation: formatting uninitialized ESP $ESP as FAT32"
            mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
        fi
    fi

    resolve_esp_mount_policy "$ESP" "$PT_TYPE"
    step "[2/4] MOUNT FOR BOOTLOADER REPAIR"
    mkdir -p "$MNT"
    mount "$ROOT_PART_ARG" "$MNT" || die "could not mount $ROOT_PART_ARG"
    mkdir -p "$MNT/boot"
    if [ -n "$ESP" ]; then
        is_dev_node "$ESP" || die "ESP partition $ESP is not a block device"
        mkdir -p "${MNT}${ESP_MOUNT_REL}"
        mount "$ESP" "${MNT}${ESP_MOUNT_REL}" || die "could not mount $ESP at ${MNT}${ESP_MOUNT_REL}"
    fi
    ROOTUUID="$(findmnt -no UUID "$MNT" 2>/dev/null || blkid -s UUID -o value "$ROOT_PART_ARG" 2>/dev/null || true)"
    [ -n "$ROOTUUID" ] || die "root partition has no UUID"

    step "[3/4] REPAIR BOOTLOADER"
    export ROOTUUID
    export FINN_DISK="$DISK"
    export FINN_FIRMWARE="$FIRMWARE_ARG"
    export FINN_ESP_MOUNT="$ESP_MOUNT_REL"
    export FINN_REPAIR_ONLY=1
    export FINN_OFFLINE=1
    if [ -n "$SRC" ] && [ -f "$SRC/vm/finn-chroot.sh" ]; then
        mkdir -p "$MNT/root"
        cp "$SRC/vm/finn-chroot.sh" "$MNT/root/finn-chroot.sh"
        chmod +x "$MNT/root/finn-chroot.sh"
        arch-chroot "$MNT" /root/finn-chroot.sh || die "bootloader repair failed"
    else
        arch-chroot "$MNT" /bin/bash -c "
            set -euo pipefail
            mkinitcpio -P 2>&1 | tail -5
            efi_ok=0
            if [ '$ESP_MOUNT_REL' = '/boot' ] && [ '$FIRMWARE_ARG' = 'uefi' ] && [ '$PT_TYPE' != 'dos' ] &&
               command -v bootctl >/dev/null 2>&1 && bootctl --esp-path=/boot install 2>&1 | tail -5; then
                mkdir -p /boot/loader/entries
                printf 'default finnos.conf\ntimeout 3\nconsole-mode max\neditor no\nauto-entries yes\nauto-firmware yes\n' > /boot/loader/loader.conf
                printf 'title FinnOS\nlinux /vmlinuz-linux\ninitrd /initramfs-linux.img\noptions root=UUID=%s rw quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles loglevel=3\n' '$ROOTUUID' > /boot/loader/entries/finnos.conf
                efi_ok=1
            elif [ -n '$ESP_MOUNT_REL' ] && [ '$FIRMWARE_ARG' = 'uefi' ] && command -v grub-install >/dev/null 2>&1; then
                EFI_TARGET=x86_64-efi
                [ \"\$(uname -m)\" = 'aarch64' ] && EFI_TARGET=arm64-efi
                grub-install --target=\"\$EFI_TARGET\" --efi-directory='$ESP_MOUNT_REL' --bootloader-id=FinnOS --removable 2>&1 | tail -5 &&
                    grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 && efi_ok=1
            fi
            bios_ok=0
            if [ \"\$(uname -m)\" = 'x86_64' ] && command -v grub-install >/dev/null 2>&1; then
                grub-install --target=i386-pc --recheck '$DISK' 2>&1 | tail -5 &&
                    grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 && bios_ok=1
            fi
            [ \"\$efi_ok\" = 1 ] || [ \"\$bios_ok\" = 1 ] || { echo 'error: bootloader repair failed' >&2; exit 1; }
        "
    fi
    umount -R "$MNT"
    step "[4/4] REPAIR COMPLETE"
    echo "FINN_BOOT_REPAIR_COMPLETE"
    exit 0
fi

# ── Partitioning & Formatting by Mode ────────────────────────────────────────
if [ "$INSTALL_MODE" = "dualboot-free" ]; then
    PT_TYPE="$(blkid -s PTTYPE -o value "$DISK" 2>/dev/null || echo gpt)"
    if [ -z "$FREE_START_MB" ] || [ -z "$FREE_END_MB" ]; then
        find_free_gap_mb 12288
        [ -n "$GAP_START_MB" ] || die "no unallocated free space >= 12 GiB found on $DISK"
        FREE_START_MB="$GAP_START_MB"
        FREE_END_MB="$GAP_END_MB"
    fi
    CURSOR_MB="$FREE_START_MB"
    ESP="$ESP_PART_ARG"

    step "[2/8] PARTITION UNALLOCATED SPACE (PRESERVING EXISTING OSES)"
    if [ "$FIRMWARE_ARG" = "uefi" ] && { [ "$ESP_ACTION" = "create-esp" ] || [ -z "$ESP" ]; }; then
        echo "auto-remediation: creating 512 MiB FAT32 EFI System Partition at ${CURSOR_MB}..$((CURSOR_MB + 512)) MiB"
        create_part_in_range "$PT_TYPE" "FINNOS_EFI" "fat32" "$CURSOR_MB" "$((CURSOR_MB + 512))" "esp"
        ESP="$CREATED_PART"
        mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
        CURSOR_MB=$((CURSOR_MB + 512))
    elif [ "$ESP_ACTION" = "format-esp" ] && [ -n "$ESP" ]; then
        mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
    fi

    if [ "$NEED_BIOS_BOOT" = 1 ] && [ "$PT_TYPE" = "gpt" ]; then
        echo "auto-remediation: creating 1 MiB BIOS Boot (ef02) partition at ${CURSOR_MB}..$((CURSOR_MB + 2)) MiB"
        create_part_in_range "$PT_TYPE" "BIOS_BOOT" "ext2" "$CURSOR_MB" "$((CURSOR_MB + 2))" "bios_boot"
        BIOSBOOT="$CREATED_PART"
        CURSOR_MB=$((CURSOR_MB + 2))
    fi

    [ $((FREE_END_MB - CURSOR_MB)) -ge 10240 ] || die "not enough unallocated space left for FinnOS root"
    create_part_in_range "$PT_TYPE" "FinnOS" "ext4" "$CURSOR_MB" "$FREE_END_MB" "linux"
    ROOT="$CREATED_PART"

    step "[3/8] FORMAT FINNOS ROOT (PRESERVING EXISTING OSES)"
    mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
    resolve_esp_mount_policy "$ESP" "$PT_TYPE"

    step "[4/8] MOUNT"
    mkdir -p "$MNT"
    mount "$ROOT" "$MNT" || die "could not mount $ROOT at $MNT"
    mkdir -p "$MNT/boot"
    if [ -n "$ESP" ]; then
        mkdir -p "${MNT}${ESP_MOUNT_REL}"
        mount "$ESP" "${MNT}${ESP_MOUNT_REL}" || die "could not mount $ESP at ${MNT}${ESP_MOUNT_REL}"
    fi
    findmnt -o TARGET,SOURCE,FSTYPE "$MNT"
    [ -n "$ESP" ] && findmnt -o TARGET,SOURCE,FSTYPE "${MNT}${ESP_MOUNT_REL}"

elif [ "$INSTALL_MODE" = "dualboot-resize" ]; then
    is_dev_node "$RESIZE_PART_ARG" || die "--resize-part is required for dualboot-resize"
    [ -n "$RESIZE_MB_ARG" ] || die "--resize-mb is required for dualboot-resize"
    if [ -n "$ESP_PART_ARG" ]; then
        is_dev_node "$ESP_PART_ARG" || die "ESP partition $ESP_PART_ARG is not a block device"
    fi
    PT_TYPE="$(blkid -s PTTYPE -o value "$DISK" 2>/dev/null || echo gpt)"
    PART_NUM="$(lsblk -no PARTN "$RESIZE_PART_ARG" 2>/dev/null | tr -d '[:space:]')"
    [ -n "$PART_NUM" ] || die "could not determine partition number for $RESIZE_PART_ARG"

    # Record EXACT original start & end BEFORE resizing so any trailing partition
    # (such as Windows Recovery / WinRE after C:) is NEVER overlapped!
    read -r START_MB ORIG_END_MB <<< "$(parted -ms "$DISK" unit MiB print 2>/dev/null | awk -F: -v n="$PART_NUM" '$1==n {gsub(/MiB/,"",$2); gsub(/MiB/,"",$3); print int($2), int($3)}')"
    [ -n "$START_MB" ] && [ -n "$ORIG_END_MB" ] || die "could not read boundaries of partition #$PART_NUM on $DISK"
    # 4 MiB safety cushion after the shrunk filesystem so resizepart never clips trailing blocks.
    NEW_END_MB=$((START_MB + RESIZE_MB_ARG + 4))
    [ "$NEW_END_MB" -lt "$ORIG_END_MB" ] || die "requested resize end (${NEW_END_MB} MiB) exceeds original end (${ORIG_END_MB} MiB)"

    step "[2/8] DUAL-BOOT NON-DESTRUCTIVE RESIZE"
    FS_KIND="$(blkid -s TYPE -o value "$RESIZE_PART_ARG" 2>/dev/null || true)"
    case "$FS_KIND" in
        ntfs)
            command -v ntfsresize >/dev/null || die "ntfsresize is required to shrink NTFS"
            ntfsfix -d "$RESIZE_PART_ARG" >/dev/null 2>&1 || true
            # Dry-run probe first to verify Windows isn't hibernated / Fast Startup locked.
            ntfsresize --info --force "$RESIZE_PART_ARG" >/dev/null 2>&1 ||
                die "NTFS volume $RESIZE_PART_ARG is dirty or hibernated; boot Windows and disable Fast Startup first"
            printf 'y\n' | ntfsresize --force --force --size "${RESIZE_MB_ARG}M" "$RESIZE_PART_ARG" ||
                die "ntfsresize failed on $RESIZE_PART_ARG"
            ;;
        ext4|ext3|ext2)
            rc=0
            e2fsck -f -p "$RESIZE_PART_ARG" || rc=$?
            [ "$rc" -le 2 ] || die "e2fsck reported uncorrected errors (exit $rc) on $RESIZE_PART_ARG"
            resize2fs "$RESIZE_PART_ARG" "${RESIZE_MB_ARG}M" || die "resize2fs failed on $RESIZE_PART_ARG"
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

    # Repair moved GPT backup header if present, then shrink partition entry cleanly.
    if [ "$PT_TYPE" = "gpt" ] && command -v sgdisk >/dev/null 2>&1; then
        sgdisk -e "$DISK" >/dev/null 2>&1 || true
    fi
    printf 'Yes\nYes\n' | parted ---pretend-input-tty "$DISK" unit MiB resizepart "$PART_NUM" "${NEW_END_MB}MiB" ||
        die "parted resizepart failed on $DISK partition $PART_NUM"
    refresh_disk_nodes

    CURSOR_MB="$NEW_END_MB"
    ESP="$ESP_PART_ARG"

    # Auto-create 512 MiB ESP inside freed range [NEW_END_MB, ORIG_END_MB] if UEFI lacks an ESP.
    if [ "$FIRMWARE_ARG" = "uefi" ] && { [ "$ESP_ACTION" = "create-esp" ] || [ -z "$ESP" ]; }; then
        echo "auto-remediation: creating 512 MiB FAT32 EFI System Partition in freed space (${CURSOR_MB}..$((CURSOR_MB + 512)) MiB)"
        create_part_in_range "$PT_TYPE" "FINNOS_EFI" "fat32" "$CURSOR_MB" "$((CURSOR_MB + 512))" "esp"
        ESP="$CREATED_PART"
        mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on auto-created ESP $ESP"
        CURSOR_MB=$((CURSOR_MB + 512))
    elif [ "$ESP_ACTION" = "format-esp" ] && [ -n "$ESP" ]; then
        mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
    fi

    # Auto-create 1 MiB BIOS Boot partition on GPT under Legacy BIOS if missing.
    if [ "$NEED_BIOS_BOOT" = 1 ] && [ "$PT_TYPE" = "gpt" ]; then
        echo "auto-remediation: creating 1 MiB BIOS Boot (ef02) partition at ${CURSOR_MB}..$((CURSOR_MB + 2)) MiB"
        create_part_in_range "$PT_TYPE" "BIOS_BOOT" "ext2" "$CURSOR_MB" "$((CURSOR_MB + 2))" "bios_boot"
        BIOSBOOT="$CREATED_PART"
        CURSOR_MB=$((CURSOR_MB + 2))
    fi

    # Strictly bound to ORIG_END_MB — never 100% — so trailing WinRE/Recovery partitions survive intact!
    create_part_in_range "$PT_TYPE" "FinnOS" "ext4" "$CURSOR_MB" "$ORIG_END_MB" "linux"
    ROOT="$CREATED_PART"

    step "[3/8] FORMAT FINNOS ROOT (PRESERVING EXISTING OS)"
    mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
    resolve_esp_mount_policy "$ESP" "$PT_TYPE"

    step "[4/8] MOUNT"
    mkdir -p "$MNT"
    mount "$ROOT" "$MNT" || die "could not mount $ROOT at $MNT"
    mkdir -p "$MNT/boot"
    if [ -n "$ESP" ]; then
        mkdir -p "${MNT}${ESP_MOUNT_REL}"
        mount "$ESP" "${MNT}${ESP_MOUNT_REL}" || die "could not mount $ESP at ${MNT}${ESP_MOUNT_REL}"
        findmnt -o TARGET,SOURCE,FSTYPE "${MNT}${ESP_MOUNT_REL}"
    fi
    findmnt -o TARGET,SOURCE,FSTYPE "$MNT"

elif [ "$INSTALL_MODE" = "partition" ] || [ "$INSTALL_MODE" = "reinstall" ]; then
    is_dev_node "$ROOT_PART_ARG" || die "--root-part is required for $INSTALL_MODE"
    if [ -n "$ESP_PART_ARG" ]; then
        is_dev_node "$ESP_PART_ARG" || die "ESP partition $ESP_PART_ARG is not a block device"
    fi
    PT_TYPE="$(blkid -s PTTYPE -o value "$DISK" 2>/dev/null || echo gpt)"
    ROOT="$ROOT_PART_ARG"
    ESP="$ESP_PART_ARG"

    if [ "$INSTALL_MODE" = "partition" ]; then
        # Auto-remediate missing EFI or BIOS Boot partition so TargetPartition NEVER fails!
        if [ "$FIRMWARE_ARG" = "uefi" ] && [ -z "$ESP" ]; then
            if [ "$ESP_ACTION" = "create-esp" ]; then
                find_free_gap_mb 512
                if [ -n "$GAP_START_MB" ]; then
                    echo "auto-remediation: creating 512 MiB FAT32 EFI partition in unallocated space"
                    create_part_in_range "$PT_TYPE" "FINNOS_EFI" "fat32" "$GAP_START_MB" "$((GAP_START_MB + 512))" "esp"
                    ESP="$CREATED_PART"
                    mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
                else
                    ESP_ACTION="split-esp"
                fi
            fi
            if [ "$ESP_ACTION" = "split-esp" ] || [ -z "$ESP" ]; then
                # Automatically split the target partition into 512 MiB FAT32 ESP + ext4 FinnOS root!
                PART_NUM="$(lsblk -no PARTN "$ROOT" 2>/dev/null | tr -d '[:space:]')"
                [ -n "$PART_NUM" ] || die "could not determine partition number of $ROOT to split for ESP"
                read -r P_START P_END <<< "$(parted -ms "$DISK" unit MiB print 2>/dev/null | awk -F: -v n="$PART_NUM" '$1==n {gsub(/MiB/,"",$2); gsub(/MiB/,"",$3); print int($2), int($3)}')"
                [ -n "$P_START" ] && [ -n "$P_END" ] || die "could not read boundaries of $ROOT"
                [ $((P_END - P_START)) -ge 12800 ] || die "$ROOT is too small to split into 512 MiB ESP + 12 GiB root"
                echo "auto-remediation: splitting $ROOT (${P_START}..${P_END} MiB) into 512 MiB FAT32 ESP + ext4 FinnOS root"
                parted -s "$DISK" rm "$PART_NUM" || die "failed to replace $ROOT while creating ESP"
                refresh_disk_nodes
                create_part_in_range "$PT_TYPE" "FINNOS_EFI" "fat32" "$P_START" "$((P_START + 512))" "esp"
                ESP="$CREATED_PART"
                mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on auto-split ESP $ESP"
                create_part_in_range "$PT_TYPE" "FinnOS" "ext4" "$((P_START + 512))" "$P_END" "linux"
                ROOT="$CREATED_PART"
            fi
        elif [ "$ESP_ACTION" = "format-esp" ] && [ -n "$ESP" ]; then
            echo "auto-remediation: formatting uninitialized ESP $ESP as FAT32"
            mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
        fi

        if [ "$NEED_BIOS_BOOT" = 1 ] && [ "$PT_TYPE" = "gpt" ]; then
            find_free_gap_mb 2
            if [ -n "$GAP_START_MB" ]; then
                echo "auto-remediation: creating 1 MiB BIOS Boot (ef02) partition in free space"
                create_part_in_range "$PT_TYPE" "BIOS_BOOT" "ext2" "$GAP_START_MB" "$((GAP_START_MB + 2))" "bios_boot"
                BIOSBOOT="$CREATED_PART"
            else
                # Carve 2 MiB from the start of the target partition being formatted.
                PART_NUM="$(lsblk -no PARTN "$ROOT" 2>/dev/null | tr -d '[:space:]')"
                read -r P_START P_END <<< "$(parted -ms "$DISK" unit MiB print 2>/dev/null | awk -F: -v n="$PART_NUM" '$1==n {gsub(/MiB/,"",$2); gsub(/MiB/,"",$3); print int($2), int($3)}')"
                if [ -n "$PART_NUM" ] && [ -n "$P_START" ] && [ -n "$P_END" ]; then
                    echo "auto-remediation: carving 1 MiB BIOS Boot (ef02) partition from $ROOT"
                    parted -s "$DISK" rm "$PART_NUM" || die "failed to carve BIOS Boot partition"
                    refresh_disk_nodes
                    create_part_in_range "$PT_TYPE" "BIOS_BOOT" "ext2" "$P_START" "$((P_START + 2))" "bios_boot"
                    BIOSBOOT="$CREATED_PART"
                    create_part_in_range "$PT_TYPE" "FinnOS" "ext4" "$((P_START + 2))" "$P_END" "linux"
                    ROOT="$CREATED_PART"
                fi
            fi
        fi

        step "[3/8] FORMAT SELECTED PARTITION (PRESERVING OTHER OSES)"
        mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
    else
        if [ "$FIRMWARE_ARG" = "uefi" ] && [ "$ESP_ACTION" = "format-esp" ] && [ -n "$ESP" ]; then
            mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
        elif [ "$FIRMWARE_ARG" = "uefi" ] && [ -z "$ESP" ] && [ "$ESP_ACTION" = "create-esp" ]; then
            find_free_gap_mb 512
            if [ -n "$GAP_START_MB" ]; then
                create_part_in_range "$PT_TYPE" "FINNOS_EFI" "fat32" "$GAP_START_MB" "$((GAP_START_MB + 512))" "esp"
                ESP="$CREATED_PART"
                mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
            fi
        fi
        step "[3/8] REINSTALL (PRESERVING USER DATA ON $ROOT)"
    fi

    resolve_esp_mount_policy "$ESP" "$PT_TYPE"
    step "[4/8] MOUNT"
    mkdir -p "$MNT"
    mount "$ROOT" "$MNT" || die "could not mount $ROOT at $MNT"
    mkdir -p "$MNT/boot"
    if [ -n "$ESP" ]; then
        mkdir -p "${MNT}${ESP_MOUNT_REL}"
        mount "$ESP" "${MNT}${ESP_MOUNT_REL}" || die "could not mount $ESP at ${MNT}${ESP_MOUNT_REL}"
        findmnt -o TARGET,SOURCE,FSTYPE "${MNT}${ESP_MOUNT_REL}"
    fi
    findmnt -o TARGET,SOURCE,FSTYPE "$MNT"

else
    # ── Clean Whole-Disk Erase (GPT default or explicit MBR) ─────────────────
    while read -r name; do
        [ -n "$name" ] || continue
        is_dev_node "/dev/$name" || continue
        wipefs -a "/dev/$name" 2>/dev/null || true
    done <<< "$(lsblk -lno NAME "$DISK" 2>/dev/null | tail -n +2)"
    wipefs -a "$DISK" || die "wipefs failed on $DISK"

    DISK_BYTES="$(lsblk -bno SIZE -d "$DISK" 2>/dev/null | head -n1 || true)"
    [ -n "$DISK_BYTES" ] || DISK_BYTES="$(blockdev --getsize64 "$DISK" 2>/dev/null || echo 0)"
    DISK_MIB=$((DISK_BYTES / 1024 / 1024))

    # Determine whether to allocate a local FINNOS_RECOVERY partition (default 4096 MiB if requested & room).
    REC_MIB=0
    if [ "$CREATE_RECOVERY_PART" = 1 ] && [ -n "$OFFLINE_SFS_PATH" ] && [ -f "$OFFLINE_SFS_PATH" ] && [ "$DISK_MIB" -ge 24576 ]; then
        SFS_BYTES="$(stat -c%s "$OFFLINE_SFS_PATH" 2>/dev/null || stat -f%z "$OFFLINE_SFS_PATH" 2>/dev/null || echo 0)"
        REC_MIB=$(( (SFS_BYTES / 1024 / 1024) + 512 ))
        [ "$REC_MIB" -lt 2048 ] && REC_MIB=2048
        if [ $((DISK_MIB - REC_MIB)) -lt 14336 ]; then
            REC_MIB=0
        fi
    fi

    if [ "$TABLE_KIND" = "mbr" ] && [ "$AB_SLOTS" = 0 ]; then
        # Clean MBR (DOS) partition table
        if [ "$FIRMWARE_ARG" = "uefi" ]; then
            ESP="${PART}1"
            ROOT="${PART}2"
            ESP_MOUNT_REL="/boot/efi"
            if [ "$REC_MIB" -gt 0 ]; then
                RECOVERY_PART="${PART}3"
                ROOT_MIB=$((DISK_MIB - 512 - REC_MIB - 8))
                printf 'label: dos\nsize=512M, type=ef\nsize=%sM, type=83, bootable\ntype=83\n' "$ROOT_MIB" |
                    sfdisk --wipe=always "$DISK" || die "sfdisk failed writing MBR+ESP table on $DISK"
            else
                printf 'label: dos\nsize=512M, type=ef\ntype=83, bootable\n' |
                    sfdisk --wipe=always "$DISK" || die "sfdisk failed writing MBR+ESP table on $DISK"
            fi
        else
            ESP=""
            ESP_MOUNT_REL=""
            ROOT="${PART}1"
            if [ "$REC_MIB" -gt 0 ]; then
                RECOVERY_PART="${PART}2"
                ROOT_MIB=$((DISK_MIB - REC_MIB - 8))
                printf 'label: dos\nsize=%sM, type=83, bootable\ntype=83\n' "$ROOT_MIB" |
                    sfdisk --wipe=always "$DISK" || die "sfdisk failed writing MBR table on $DISK"
            else
                printf 'label: dos\n,,83,*\n' |
                    sfdisk --wipe=always "$DISK" || die "sfdisk failed writing MBR table on $DISK"
            fi
        fi
    elif [ "$AB_SLOTS" = 1 ]; then
        ESP="${PART}1"
        BIOSBOOT="${PART}2"
        ROOT="${PART}3"
        SLOT_B="${PART}4"
        DATA_PART="${PART}5"
        ESP_MOUNT_REL="/boot"
        AVAIL_MIB=$((DISK_MIB - 1041 - REC_MIB))
        SLOT_MIB=$((AVAIL_MIB * 35 / 100))
        [ "$SLOT_MIB" -ge 4096 ] || die "$DISK is too small for A/B OTA slots (need >= 14 GiB, got ${DISK_MIB} MiB)"
        if [ "$REC_MIB" -gt 0 ]; then
            RECOVERY_PART="${PART}6"
            DATA_MIB=$((AVAIL_MIB - SLOT_MIB * 2))
            printf 'label: gpt\nsize=1024M, type=U, name="FINNOS_EFI"\nsize=1M, type=21686148-6449-6E6F-744E-656564454649, name="BIOS_BOOT"\nsize=%sM, type=L, name="FINNOS_A"\nsize=%sM, type=L, name="FINNOS_B"\nsize=%sM, type=L, name="FINNOS_DATA"\ntype=L, name="FINNOS_RECOVERY"\n' \
                "$SLOT_MIB" "$SLOT_MIB" "$DATA_MIB" |
                sfdisk --wipe=always "$DISK" || die "sfdisk did not write A/B+Recovery table on $DISK"
        else
            printf 'label: gpt\nsize=1024M, type=U, name="FINNOS_EFI"\nsize=1M, type=21686148-6449-6E6F-744E-656564454649, name="BIOS_BOOT"\nsize=%sM, type=L, name="FINNOS_A"\nsize=%sM, type=L, name="FINNOS_B"\ntype=L, name="FINNOS_DATA"\n' \
                "$SLOT_MIB" "$SLOT_MIB" |
                sfdisk --wipe=always "$DISK" || die "sfdisk did not write A/B partition table on $DISK"
        fi
    else
        # Standard GPT hybrid (boots BOTH UEFI via 512M ESP AND Legacy BIOS via 1M BIOS Boot)
        ESP="${PART}1"
        BIOSBOOT="${PART}2"
        ROOT="${PART}3"
        ESP_MOUNT_REL="/boot"
        if [ "$REC_MIB" -gt 0 ]; then
            RECOVERY_PART="${PART}4"
            ROOT_MIB=$((DISK_MIB - 513 - REC_MIB - 8))
            printf 'label: gpt\nsize=512M, type=U, name="FINNOS_EFI"\nsize=1M, type=21686148-6449-6E6F-744E-656564454649, name="BIOS_BOOT"\nsize=%sM, type=L, name="FINNOS"\ntype=L, name="FINNOS_RECOVERY"\n' "$ROOT_MIB" |
                sfdisk --wipe=always "$DISK" || die "sfdisk did not write GPT+Recovery table on $DISK"
        else
            printf 'label: gpt\n,512M,U\n,+1M,21686148-6449-6E6F-744E-656564454649\n,,L\n' |
                sfdisk --wipe=always "$DISK" || die "sfdisk did not write a partition table on $DISK"
        fi
    fi

    sfdisk -d "$DISK" >/dev/null || die "no partition table on $DISK"
    refresh_disk_nodes
    for _ in $(seq 1 30); do
        ok=1
        is_dev_node "$ROOT" || ok=0
        [ -z "$ESP" ] || is_dev_node "$ESP" || ok=0
        [ -z "$BIOSBOOT" ] || is_dev_node "$BIOSBOOT" || ok=0
        [ -z "$SLOT_B" ] || is_dev_node "$SLOT_B" || ok=0
        [ -z "$DATA_PART" ] || is_dev_node "$DATA_PART" || ok=0
        [ -z "$RECOVERY_PART" ] || is_dev_node "$RECOVERY_PART" || ok=0
        [ "$ok" = 1 ] && break
        sleep 1
    done
    is_dev_node "$ROOT" || die "root partition $ROOT did not appear on $DISK"
    [ -z "$ESP" ] || is_dev_node "$ESP" || die "ESP partition $ESP did not appear on $DISK"
    lsblk "$DISK" || true
    post

    step "[3/8] FORMAT"
    if [ -n "$ESP" ]; then
        mkfs.vfat -F32 -n FINNOS_EFI "$ESP" || die "mkfs.vfat failed on $ESP"
        [ "$(blkid -s TYPE -o value "$ESP")" = "vfat" ] || die "$ESP is not vfat"
    fi
    if [ "$AB_SLOTS" = 1 ]; then
        mkfs.ext4 -F -L FINNOS_A "$ROOT" || die "mkfs.ext4 failed on $ROOT"
        mkfs.ext4 -F -L FINNOS_B "$SLOT_B" || die "mkfs.ext4 failed on $SLOT_B"
        mkfs.ext4 -F -L FINNOS_DATA "$DATA_PART" || die "mkfs.ext4 failed on $DATA_PART"
        [ "$(blkid -s TYPE -o value "$SLOT_B")" = "ext4" ] || die "$SLOT_B is not ext4"
        [ "$(blkid -s TYPE -o value "$DATA_PART")" = "ext4" ] || die "$DATA_PART is not ext4"
    else
        mkfs.ext4 -F -L finnos "$ROOT" || die "mkfs.ext4 failed on $ROOT"
    fi
    [ "$(blkid -s TYPE -o value "$ROOT")" = "ext4" ] || die "$ROOT is not ext4"
    if [ -n "$RECOVERY_PART" ]; then
        mkfs.ext4 -F -L FINNOS_RECOVERY "$RECOVERY_PART" || die "mkfs.ext4 failed on $RECOVERY_PART"
    fi
    post

    step "[4/8] MOUNT"
    mkdir -p "$MNT"
    mount "$ROOT" "$MNT" || die "could not mount $ROOT at $MNT"
    mkdir -p "$MNT/boot"
    if [ -n "$ESP" ]; then
        mkdir -p "${MNT}${ESP_MOUNT_REL}"
        mount "$ESP" "${MNT}${ESP_MOUNT_REL}" || die "could not mount $ESP at ${MNT}${ESP_MOUNT_REL}"
        [ "$(findmnt -no SOURCE -T "${MNT}${ESP_MOUNT_REL}")" = "$ESP" ] || die "$ESP is not mounted at ${MNT}${ESP_MOUNT_REL}"
    fi
    [ "$(findmnt -no SOURCE -T "$MNT")" = "$ROOT" ] || die "$ROOT is not what is mounted at $MNT"
    if [ "$AB_SLOTS" = 1 ]; then
        mkdir -p "$MNT/data"
        mount "$DATA_PART" "$MNT/data" || die "could not mount $DATA_PART at $MNT/data"
        [ "$(findmnt -no SOURCE -T "$MNT/data")" = "$DATA_PART" ] || die "$DATA_PART is not mounted at $MNT/data"
        findmnt -o TARGET,SOURCE,FSTYPE "$MNT/data"
    fi
    findmnt -o TARGET,SOURCE,FSTYPE /mnt
    [ -n "$ESP" ] && findmnt -o TARGET,SOURCE,FSTYPE /mnt/boot 2>/dev/null || findmnt -o TARGET,SOURCE,FSTYPE "${MNT}${ESP_MOUNT_REL}" 2>/dev/null || true
    post
fi

# ── Step 5: Deploy FinnOS OS Image Offline (Zero Internet!) ──────────────────
step "[5/8] DEPLOY BASE SYSTEM"
if [ -n "$OFFLINE_IMAGE_KIND" ]; then
    echo "deploying FinnOS directly from local offline image ($OFFLINE_IMAGE_KIND) — zero internet access"

    # If reinstalling, snapshot user identity & Wi-Fi configs so they are 100% preserved.
    PRESERVE_DIR=""
    if [ "$INSTALL_MODE" = "reinstall" ]; then
        PRESERVE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/finn-preserve.XXXXXX")"
        for pfile in etc/passwd etc/shadow etc/group etc/gshadow etc/hostname etc/localtime etc/locale.conf etc/vconsole.conf var/lib/finn/oobe_complete; do
            if [ -e "$MNT/$pfile" ] || [ -L "$MNT/$pfile" ]; then
                mkdir -p "$PRESERVE_DIR/$(dirname "$pfile")"
                cp -a "$MNT/$pfile" "$PRESERVE_DIR/$pfile"
            fi
        done
        if [ -d "$MNT/etc/NetworkManager/system-connections" ]; then
            mkdir -p "$PRESERVE_DIR/etc/NetworkManager"
            cp -a "$MNT/etc/NetworkManager/system-connections" "$PRESERVE_DIR/etc/NetworkManager/"
        fi
    fi

    # Extract / copy from local directory, SquashFS file, or .tar.zst archive.
    EXTRACT_SRC="$OFFLINE_ROOT"
    EXTRACT_TMP_DIR=""
    if [ "$OFFLINE_IMAGE_KIND" = "sfs" ]; then
        if [ "$INSTALL_MODE" != "reinstall" ] && command -v unsquashfs >/dev/null 2>&1; then
            unsquashfs -f -d "$MNT" "$OFFLINE_IMAGE_FILE" || die "unsquashfs failed on $OFFLINE_IMAGE_FILE"
            EXTRACT_SRC=""
        else
            TMP_SFS="$(mktemp -d "${TMPDIR:-/tmp}/finn-sfs.XXXXXX")"
            if mount -o ro,loop "$OFFLINE_IMAGE_FILE" "$TMP_SFS" 2>/dev/null; then
                CLEANUP_MOUNTS+=("$TMP_SFS")
                EXTRACT_SRC="$TMP_SFS"
            elif command -v unsquashfs >/dev/null 2>&1; then
                rmdir "$TMP_SFS" 2>/dev/null || true
                # Kernel lacks loop-mount compression support (e.g. CONFIG_SQUASHFS_ZSTD);
                # extract into a staging directory on target storage, refresh system trees, then remove.
                EXTRACT_TMP_DIR="$MNT/.finn-sfs-stage"
                rm -rf "$EXTRACT_TMP_DIR"
                unsquashfs -f -d "$EXTRACT_TMP_DIR" "$OFFLINE_IMAGE_FILE" ||
                    { rm -rf "$EXTRACT_TMP_DIR"; die "unsquashfs fallback failed on $OFFLINE_IMAGE_FILE"; }
                EXTRACT_SRC="$EXTRACT_TMP_DIR"
            else
                rmdir "$TMP_SFS" 2>/dev/null || true
                die "could not loop-mount or unsquashfs $OFFLINE_IMAGE_FILE"
            fi
        fi
    elif [ "$OFFLINE_IMAGE_KIND" = "tar" ]; then
        if [ "$INSTALL_MODE" = "reinstall" ]; then
            EXTRACT_TMP_DIR="$MNT/.finn-tar-stage"
            rm -rf "$EXTRACT_TMP_DIR"
            mkdir -p "$EXTRACT_TMP_DIR"
            tar --zstd -xpf "$OFFLINE_IMAGE_FILE" -C "$EXTRACT_TMP_DIR" 2>/dev/null ||
                tar -xpf "$OFFLINE_IMAGE_FILE" -C "$EXTRACT_TMP_DIR" ||
                { rm -rf "$EXTRACT_TMP_DIR"; die "could not extract $OFFLINE_IMAGE_FILE"; }
            EXTRACT_SRC="$EXTRACT_TMP_DIR"
        else
            tar --zstd -xpf "$OFFLINE_IMAGE_FILE" -C "$MNT" 2>/dev/null ||
                tar -xpf "$OFFLINE_IMAGE_FILE" -C "$MNT" ||
                die "could not extract $OFFLINE_IMAGE_FILE to $MNT"
            EXTRACT_SRC=""
        fi
    fi

    if [ -n "$EXTRACT_SRC" ]; then
        if [ "$INSTALL_MODE" = "reinstall" ]; then
            for d in usr bin lib lib64 sbin var/lib/pacman opt/finnos; do
                if [ -e "$EXTRACT_SRC/$d" ]; then
                    mkdir -p "$MNT/$(dirname "$d")"
                    cp -a "$EXTRACT_SRC/$d" "$MNT/$(dirname "$d")/" || die "offline copy failed on $d"
                fi
            done
        else
            cp -a "$EXTRACT_SRC/." "$MNT/" || die "offline copy from $EXTRACT_SRC to $MNT failed"
        fi
        if [ -n "$EXTRACT_TMP_DIR" ] && [ -d "$EXTRACT_TMP_DIR" ]; then
            rm -rf "$EXTRACT_TMP_DIR"
        fi
    fi

    # Restore preserved user identity/config on reinstall.
    if [ -n "$PRESERVE_DIR" ] && [ -d "$PRESERVE_DIR" ]; then
        cp -a "$PRESERVE_DIR/." "$MNT/"
        rm -rf "$PRESERVE_DIR"
    fi

    mkdir -p "$MNT/boot" "$MNT/dev" "$MNT/proc" "$MNT/sys" "$MNT/run" "$MNT/tmp"
    chmod 1777 "$MNT/tmp"
    if [ -n "$OFFLINE_KERNEL" ] && [ -f "$OFFLINE_KERNEL" ]; then
        install -m 644 "$OFFLINE_KERNEL" "$MNT/boot/vmlinuz-linux"
    elif [ ! -f "$MNT/boot/vmlinuz-linux" ]; then
        for kcand in "$MNT"/usr/lib/modules/*/vmlinuz; do
            if [ -f "$kcand" ]; then
                install -m 644 "$kcand" "$MNT/boot/vmlinuz-linux"
                break
            fi
        done
    fi
    [ -f "$MNT/boot/vmlinuz-linux" ] || die "kernel vmlinuz-linux missing after offline image deployment"

    # Strip live-ISO-only hooks/units so normal boot starts the FinnOS desktop & OOBE.
    rm -f "$MNT/root/.zprofile" "$MNT/root/.bash_profile" "$MNT/root/.hushlogin"
    rm -rf "$MNT/etc/systemd/system/getty@tty1.service.d"
    rm -f "$MNT/etc/systemd/system/multi-user.target.wants/pacman-init.service" \
          "$MNT/etc/systemd/system/pacman-init.service" \
          "$MNT/etc/systemd/system/etc-pacman.d-gnupg.mount" \
          "$MNT/etc/mkinitcpio.conf.d/archiso.conf" \
          "$MNT/etc/machine-id"
    if [ "$INSTALL_MODE" != "reinstall" ]; then
        # Guarantee first-boot OOBE triggers on fresh/dual-boot installs.
        rm -f "$MNT/var/lib/finn/oobe_complete" "$MNT/home/finn/.config/finn/oobe_complete"
    fi

    # Populate dedicated FINNOS_RECOVERY partition with verified offline image if allocated.
    if [ -n "$RECOVERY_PART" ] && [ -n "$OFFLINE_SFS_PATH" ] && [ -f "$OFFLINE_SFS_PATH" ]; then
        TMP_REC_DST="$(mktemp -d "${TMPDIR:-/tmp}/finn-rec-dst.XXXXXX")"
        if mount "$RECOVERY_PART" "$TMP_REC_DST"; then
            echo "saving offline recovery image to $RECOVERY_PART (FINNOS_RECOVERY)"
            install -m 644 "$OFFLINE_SFS_PATH" "$TMP_REC_DST/finnos.sfs"
            if [ -n "$OFFLINE_SFS_SHA" ] && [ -f "$OFFLINE_SFS_SHA" ]; then
                sha_hex="$(awk 'NF>=1 {print tolower($1); exit}' "$OFFLINE_SFS_SHA")"
                [ -n "$sha_hex" ] && printf '%s  finnos.sfs\n' "$sha_hex" > "$TMP_REC_DST/finnos.sha512"
            elif command -v sha512sum >/dev/null 2>&1; then
                (cd "$TMP_REC_DST" && sha512sum finnos.sfs > finnos.sha512)
            fi
            install -m 644 "$MNT/boot/vmlinuz-linux" "$TMP_REC_DST/vmlinuz-linux"
            umount "$TMP_REC_DST" || true
        fi
        rmdir "$TMP_REC_DST" 2>/dev/null || true
    fi
    echo "offline base system deployment complete"
else
    # Legacy online fallback (only reachable when --offline-only is NOT passed and no local image exists).
    PKGS=""
    PKGLISTS=()
    [ -n "$SRC" ] && PKGLISTS+=("$SRC/base/packages.$ARCH_NOW" "$SRC/base/packages.x86_64")
    PKGLISTS+=("/opt/finnos/base/packages.$ARCH_NOW" "/opt/finnos/base/packages.x86_64")
    for f in "${PKGLISTS[@]}"; do
        [ -f "$f" ] || continue
        PKGS="$(sed -e 's/#.*//' -e 's/[[:space:]]//g' "$f" | grep -v '^$' | tr '\n' ' ' || true)"
        echo "package list: $f"
        break
    done
    if [ -z "$PKGS" ]; then
        KERNEL=linux
        [ "$ARCH_NOW" = 'x86_64' ] || KERNEL=linux-aarch64
        PKGS="base $KERNEL linux-firmware systemd sudo openssh networkmanager wpa_supplicant
              grub os-prober efibootmgr mesa libdrm libinput seatd fontconfig ttf-dejavu
              git python vim nano base-devel pkgconf libwebp freetype2 alsa-lib
              alsa-ucm-conf arch-install-scripts dosfstools e2fsprogs parted
              gptfdisk util-linux kbd libglvnd pipewire pipewire-pulse wireplumber
              bluez bluez-utils jq zstd"
        if [ "$ARCH_NOW" = 'x86_64' ]; then
            PKGS="$PKGS vulkan-intel lib32-mesa lib32-vulkan-icd-loader
                  lib32-vulkan-intel wine winetricks wine-mono wine-gecko
                  intel-ucode amd-ucode open-vm-tools virtualbox-guest-utils-nox
                  qemu-guest-agent"
        fi
    fi
    case " $PKGS " in
        *" wine "*|*" lib32-"*)
            grep -Fqx '[multilib]' /etc/pacman.conf ||
                die "[multilib] is not enabled in /etc/pacman.conf; the x86_64 package list needs it for Wine"
            ;;
    esac
    staged=0
    for try in 1 2 3; do
        if pacstrap "$MNT" $PKGS; then staged=1; break; fi
        echo "pacstrap failed on try $try; retrying"
    done
    [ "$staged" = 1 ] || die "the base system did not install"
fi
post

# ── Step 6: Generate /etc/fstab ──────────────────────────────────────────────
step "[6/8] FSTAB"
mkdir -p "$MNT/etc"
rm -f "$MNT/etc/fstab"
genfstab -U "$MNT" > "$MNT/etc/fstab"
[ -s "$MNT/etc/fstab" ] || die "genfstab wrote no fstab"
ROOTUUID="$(findmnt -no UUID "$MNT" 2>/dev/null || blkid -s UUID -o value "$ROOT" 2>/dev/null || true)"
[ -n "$ROOTUUID" ] || die "the new root filesystem has no UUID; refusing to write a boot entry"
grep -q "UUID=$ROOTUUID" "$MNT/etc/fstab" || die "the fstab does not name the root UUID $ROOTUUID"
cat "$MNT/etc/fstab"
echo "root UUID: $ROOTUUID"
post

# ── Step 7: Configure Installed System & Bootloader inside chroot ────────────
step "[7/8] CHROOT & BOOTLOADER"
if [ -f /etc/pacman.d/finnos-mirrorlist ]; then
    install -Dm644 /etc/pacman.d/finnos-mirrorlist "$MNT/etc/pacman.d/finnos-mirrorlist"
fi
mkdir -p "$MNT/root"
if [ -n "$SRC" ]; then
    cp "$SRC/vm/finn-chroot.sh" "$MNT/root/finn-chroot.sh"
    mkdir -p "$MNT/root/finnos-src"
    tar -C "$SRC" -cf - --exclude='.git' --exclude='*.o' --exclude='*.a' . |
        tar -C "$MNT/root/finnos-src" -xf -
    echo "payload taken from local tree ($SRC)"
elif [ -f "$MNT/opt/finnos/vm/finn-chroot.sh" ]; then
    cp "$MNT/opt/finnos/vm/finn-chroot.sh" "$MNT/root/finn-chroot.sh"
    echo "payload taken from offline image (/opt/finnos)"
elif [ "$OFFLINE_ONLY" = 0 ] && [ -n "$HOST" ]; then
    curl -sLo "$MNT/root/finn-chroot.sh" "$HOST/vm/finn-chroot.sh"
    [ -s "$MNT/root/finn-chroot.sh" ] || die "the chroot script could not be fetched from $HOST"
else
    die "finn-chroot.sh not found in local payload or offline image"
fi
chmod +x "$MNT/root/finn-chroot.sh"

export ROOTUUID
export FINN_DISK="$DISK"
export FINN_FIRMWARE="$FIRMWARE_ARG"
export FINN_ESP_MOUNT="$ESP_MOUNT_REL"
export FINN_OFFLINE="$([ -n "$OFFLINE_IMAGE_KIND" ] || [ "$OFFLINE_ONLY" = 1 ] && echo 1 || echo 0)"
export FINN_REINSTALL="$([ "$INSTALL_MODE" = "reinstall" ] && echo 1 || echo 0)"
export FINN_PASSWORD="${FINN_PASSWORD:-}"
export FINN_HOSTNAME="${HOSTNAME_ARG:-${FINN_HOSTNAME:-finnos}}"
export FINN_TIMEZONE="${TIMEZONE_ARG:-${FINN_TIMEZONE:-UTC}}"
export FINN_AB_SLOTS="$AB_SLOTS"
export FINN_SERIAL="$SERIAL"

arch-chroot "$MNT" /root/finn-chroot.sh ||
    die "the machine could not be configured; see the chroot log above"
post

# ── Step 8: Verify Installed System Integrity on Disk ────────────────────────
step "[8/8] VERIFY"
[ -x "$MNT/usr/local/bin/finn-comp" ] ||
    die "the compositor was not installed: /usr/local/bin/finn-comp is missing or not executable"
[ -x "$MNT/usr/local/bin/finn-session" ] ||
    die "the session broker was not installed: /usr/local/bin/finn-session is missing or not executable"
[ -x "$MNT/usr/local/bin/finn-shell" ] ||
    die "the shell was not installed: /usr/local/bin/finn-shell is missing or not executable"
[ -e "$MNT/etc/systemd/system/graphical.target.wants/finn-comp.service" ] ||
    die "finn-comp is installed but not enabled"
[ -e "$MNT/etc/systemd/system/graphical.target.wants/finn-session.service" ] ||
    die "finn-session is installed but not enabled"
[ -e "$MNT/etc/systemd/system/graphical.target.wants/finn-shell.service" ] ||
    die "finn-shell is installed but not enabled"

installed_pkg() {
    compgen -G "$MNT/var/lib/pacman/local/$1-[0-9]*" >/dev/null
}
if [ "${FINN_SKIP_PKG_VERIFY:-0}" != 1 ]; then
    for pkg in linux-firmware mesa libglvnd pipewire pipewire-pulse wireplumber \
               bluez bluez-utils jq zstd; do
        installed_pkg "$pkg" || die "$pkg is not installed on $DISK"
    done
    if [ "$ARCH_NOW" = 'x86_64' ]; then
        installed_pkg linux || die "the linux kernel is not installed on $DISK"
        for pkg in intel-ucode amd-ucode vulkan-intel lib32-mesa \
                   lib32-vulkan-icd-loader lib32-vulkan-intel wine winetricks \
                   wine-mono wine-gecko open-vm-tools virtualbox-guest-utils-nox \
                   qemu-guest-agent; do
            installed_pkg "$pkg" || die "$pkg is not installed on $DISK"
        done
        HARDWARE="x86_64: firmware, microcode, mesa, vulkan, wine, audio, bluetooth"
    else
        installed_pkg linux-aarch64 || die "the linux-aarch64 kernel is not installed on $DISK"
        HARDWARE="aarch64: firmware, mesa, audio, bluetooth"
    fi
    echo "HARDWARE=$HARDWARE"
    post_line "HARDWARE=$HARDWARE"
fi

bootentry=""
if [ -f "$MNT/boot/grub/grub.cfg" ] &&
   { grep -q "root=UUID=$ROOTUUID" "$MNT/boot/grub/grub.cfg" || grep -q "root=PARTLABEL=FINNOS_A" "$MNT/boot/grub/grub.cfg"; }; then
    bootentry=grub
elif compgen -G "$MNT/boot/loader/entries/*.conf" >/dev/null &&
     { grep -q "root=UUID=$ROOTUUID" "$MNT"/boot/loader/entries/*.conf || grep -q "root=PARTLABEL=FINNOS_A" "$MNT"/boot/loader/entries/*.conf; }; then
    bootentry=systemd-boot
fi
[ -n "$bootentry" ] || die "no boot entry on $DISK names root=UUID=$ROOTUUID or root=PARTLABEL=FINNOS_A"

# Verify UEFI loader binary exists on whichever ESP mount point was used (/boot or /boot/efi).
if [ -n "$ESP" ] && [ -n "$ESP_MOUNT_REL" ] && [ -d "${MNT}${ESP_MOUNT_REL}/EFI" ]; then
    compgen -G "${MNT}${ESP_MOUNT_REL}/EFI/BOOT/BOOT*.EFI" >/dev/null ||
        compgen -G "${MNT}${ESP_MOUNT_REL}/EFI/systemd/systemd-boot*" >/dev/null ||
        compgen -G "${MNT}${ESP_MOUNT_REL}/EFI/FinnOS/grub*.efi" >/dev/null ||
        die "the EFI system partition on $DISK has no EFI loader"
fi

sync
umount -R "$MNT" || echo "WARN: $MNT is still mounted; unmount it before rebooting"
cleanup_temp_mounts

echo "FINN_INSTALL_COMPLETE"
echo "  disk       $DISK"
echo "  root UUID  $ROOTUUID"
echo "  boot       $bootentry"
echo "  firmware   $FIRMWARE_ARG"
echo "  console    Ctrl+Alt+F2 if the compositor does not come up"
post_line "FINN_INSTALL_COMPLETE disk=$DISK root=$ROOTUUID boot=$bootentry"
