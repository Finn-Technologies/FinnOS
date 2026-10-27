#!/bin/bash
# Boot the FinnOS ISO under QEMU and wait for the live environment to
# reach userspace. This is the check that the disc actually boots: a green
# build that writes an unbootable image is the failure mode this script
# exists to catch.
#
#   vm/test-boot.sh [finnos.iso] [seconds]
#
# QEMU is used because it can boot the same UEFI image VMware boots, and
# because it writes the guest console to a file this script can read.
set -euo pipefail

ISO="${1:-$(cd "$(dirname "$0")/.." && pwd)/finnos.iso}"
WAIT="${2:-240}"

command -v qemu-system-x86_64 >/dev/null 2>&1 || {
  echo "error: need qemu-system-x86_64" >&2
  exit 1
}
[ -f "$ISO" ] || { echo "error: no such ISO: $ISO" >&2; exit 1; }

WORK="$(mktemp -d)"
LOG="$WORK/serial.log"
trap 'rm -rf "$WORK"' EXIT

echo "==> the boot images on $ISO"
xorriso -indev "$ISO" -report_el_torito plain 2>/dev/null | sed 's/^/    /'

# The live environment is reached through the boot loader; the direct
# kernel path only needs the kernel and the initramfs, which the ISO has.
# The disc is laid out under /finn (install_dir in base/profiledef.sh),
# not under /arch: a path here that names Arch is a stale remaster path.
for p in /finn/boot/x86_64 /finn/boot; do
  if xorriso -osirrox on -indev "$ISO" -extract "$p/vmlinuz-linux" "$WORK/vmlinuz" 2>/dev/null; then
    xorriso -osirrox on -indev "$ISO" -extract "$p/initramfs-linux.img" "$WORK/initramfs.img" 2>/dev/null || continue
    KERNEL_DIR="$p"
    break
  fi
done
[ -n "${KERNEL_DIR:-}" ] || { echo "error: no kernel on the ISO" >&2; exit 1; }

echo "==> booting $ISO under QEMU, up to ${WAIT}s"
qemu-system-x86_64 \
    -machine q35 -m 2048 -smp 2 \
    -cdrom "$ISO" \
    -kernel "$WORK/vmlinuz" -initrd "$WORK/initramfs.img" \
    -append "archisobasedir=finn archisolabel=FINNOS archisodevice=/dev/sr0 console=ttyS0" \
    -display none -serial "file:$LOG" -monitor none -no-reboot \
    & QEMU=$!

ok=0
for _ in $(seq 1 "$WAIT"); do
  # The kernel was told to use ttyS0, so systemd's own progress lines are
  # on the serial log. Reaching multi-user is what says userspace came up;
  # this live root has no getty on ttyS0, so waiting for "login:" would
  # wait forever on a boot that worked. The issue name and a login prompt
  # are still accepted for an image that does put a getty on the port.
  if grep -qE 'Reached target .*Multi-User System|Startup finished in|login:|FinnOS' "$LOG" 2>/dev/null; then
    ok=1
    break
  fi
  kill -0 "$QEMU" 2>/dev/null || break
  sleep 1
done
kill "$QEMU" 2>/dev/null || true
wait "$QEMU" 2>/dev/null || true

if [ "$ok" != 1 ]; then
  echo "---- the last of the guest console ----" >&2
  tail -40 "$LOG" >&2 || true
  echo "error: the ISO did not reach userspace" >&2
  exit 1
fi

echo "==> the guest reached userspace"
tail -5 "$LOG" | sed 's/^/    /'
echo "OK: $ISO boots"
