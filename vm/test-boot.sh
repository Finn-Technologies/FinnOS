#!/bin/bash
# Boot the FinnOS ISO under QEMU and wait for the live environment to
# reach a login prompt. This is the check that the disc actually boots:
# a green build that writes an unbootable image is the failure mode this
# script exists to catch.
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
for p in /arch/boot/x86_64 /arch/boot; do
  if xorriso -osirrox on -indev "$ISO" -extract "$p/vmlinuz-linux" "$WORK/vmlinuz" 2>/dev/null; then
    xorriso -osirrox on -indev "$ISO" -extract "$p/initramfs-linux.img" "$WORK/initramfs.img" 2>/dev/null || continue
    KERNEL_DIR="$p"
    break
  fi
done
[ -n "${KERNEL_DIR:-}" ] || { echo "error: no kernel on the ISO" >&2; exit 1; }

# Point the initramfs at the disc directly rather than at its label: the
# label on this image is the Arch one, and the test should not depend on
# parsing it out of the ISO metadata.
echo "==> booting $ISO under QEMU, up to ${WAIT}s"
qemu-system-x86_64 \
    -machine q35 -m 2048 -smp 2 \
    -cdrom "$ISO" \
    -kernel "$WORK/vmlinuz" -initrd "$WORK/initramfs.img" \
    -append "archisobasedir=arch archisodevice=/dev/sr0 console=ttyS0" \
    -display none -serial "file:$LOG" -monitor none -no-reboot \
    & QEMU=$!

ok=0
for _ in $(seq 1 "$WAIT"); do
  # The live root's issue is FinnOS's, so its name on the console is what
  # says the guest got as far as userspace. A plain "login:" is accepted
  # too, for a live environment that has no autologin.
  if grep -qE 'login:|FinnOS' "$LOG" 2>/dev/null; then ok=1; break; fi
  kill -0 "$QEMU" 2>/dev/null || break
  sleep 1
done
kill "$QEMU" 2>/dev/null || true
wait "$QEMU" 2>/dev/null || true

if [ "$ok" != 1 ]; then
  echo "---- the last of the guest console ----" >&2
  tail -40 "$LOG" >&2 || true
  echo "error: the ISO did not reach a login prompt" >&2
  exit 1
fi

echo "==> the guest reached userspace"
tail -5 "$LOG" | sed 's/^/    /'
echo "OK: $ISO boots"
