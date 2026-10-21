#!/bin/bash
# Capture the boot menu of an image.
#
#   vm/menu-shot.sh finnos.iso [out-dir]
#
# The menu is drawn on the VGA console, which a build runner does not have,
# so the machine is started with the display off and the monitor is asked
# for a frame at a few moments. The VGA device still renders in that mode,
# so the frame is the same one a person would see before the countdown runs
# out. A few frames are taken because the menu appears at a time that
# depends on the machine.
#
# The frames are left next to each other; pick the one with the menu and
# look at it, instead of assuming the rewrite produced what was intended.
set -euo pipefail

ISO="${1:?usage: menu-shot.sh <iso> [out-dir]}"
DIR="${2:-$(pwd)}"
mkdir -p "$DIR"

command -v qemu-system-x86_64 >/dev/null 2>&1 ||
  { echo "error: need qemu-system-x86_64" >&2; exit 1; }

# The monitor is stdin; a frame every few seconds, then the machine is
# stopped. It boots from the CD the way a BIOS machine would.
{
  sleep 4;  echo "screendump $DIR/frame-04.ppm"
  sleep 3;  echo "screendump $DIR/frame-07.ppm"
  sleep 3;  echo "screendump $DIR/frame-10.ppm"
  sleep 3;  echo "screendump $DIR/frame-13.ppm"
  sleep 1;  echo quit
} | qemu-system-x86_64 -m 2048 -cdrom "$ISO" -boot d \
      -display none -monitor stdio -no-reboot >/dev/null 2>&1 || true

# PPM is what screendump always writes; PNG is what a person can open.
for f in "$DIR"/frame-*.ppm; do
  [ -f "$f" ] || continue
  if command -v magick >/dev/null 2>&1; then
    magick "$f" "${f%.ppm}.png"
  elif command -v convert >/dev/null 2>&1; then
    convert "$f" "${f%.ppm}.png"
  elif command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -loglevel error -y -i "$f" "${f%.ppm}.png"
  fi
done

ls -l "$DIR"/frame-* 2>/dev/null || { echo "error: no frames were written" >&2; exit 1; }
