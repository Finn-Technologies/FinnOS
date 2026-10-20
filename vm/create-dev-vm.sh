#!/bin/bash
# Create FinnOS dev VM in Parallels (ARM host -> ARM dev VM fast; x86_64 release ISO optional slow emulation)
set -eux
VM="${1:-FinnOS-dev}"
ISO_ARM="$HOME/Downloads/arch-arm.iso" # placeholder if user supplies Debian/Manjaro ARM ISO
ISO_X86="$HOME/Downloads/archlinux-x86_64.iso"
MEM=4096 # match i3-4130 4GB target
CPUS=2
HDD=32768
prlctl list -a | grep -q "$VM" && prlctl delete "$VM" || true
prlctl create "$VM" --ostype linux --distribution linux --no-hdd
prlctl set "$VM" --memsize $MEM --cpus $CPUS --efi-boot on --videosize 128
prl_disk_tool create --hdd "$HOME/Parallels/$VM.hdd" --size $HDD 2>/dev/null || \
  prlctl set "$VM" --device-add hdd --size $HDD
echo "Attach ISO manually: prlctl set $VM --device-add cdrom --image <iso>"
echo "Then: prlctl start $VM"
echo "Inside VM: mount shared FinnOS repo, run arch-base/setup.sh, build finn-comp, reboot to desktop."
echo "x86_64 release (i3-4130): download $ISO_X86 and boot with emulation (slow on M-series, use for release test only)."
