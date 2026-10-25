#!/bin/bash
# Create a FinnOS dev VM for VMware Workstation (x86_64).
#
#   vm/create-dev-vm.sh [vm-name] [finnos-iso]
#
# The FinnOS ISO is produced by `sudo base/mkfinniso` (or by CI, which
# runs the same native build in a privileged Arch container) and is
# bootable on its own: it carries the live environment, the installer
# and the payload. There is no stock-Arch fallback disc.
#
# VMware Workstation is the target hypervisor; there is no Parallels
# dependency anywhere in this tree.
set -eux
VM="${1:-FinnOS-dev}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${2:-$ROOT/finnos.iso}"
MEM=4096 # match i3-4130 4GB target
CPUS=2
HDD=32768
WORKDIR="${VMWARE_VM_DIR:-$HOME/Virtual Machines/$VM}"
mkdir -p "$WORKDIR"

if [ ! -f "$ISO" ]; then
  echo "not found: $ISO" >&2
  echo "Build it first: sudo base/mkfinniso $ISO" >&2
  exit 1
fi

cat > "$WORKDIR/$VM.vmx" <<EOF
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "20"
displayName = "$VM"
guestOS = "other5xlinux-64"
memsize = "$MEM"
numvcpus = "$CPUS"
firmware = "bios"
# The machine starts with a BIOS. VMware's UEFI firmware exposes no
# variables of its own and will not boot an EFI system partition from a
# disk, so an install it cannot reach is the same as no install; the
# installer writes the BIOS boot code as well as the EFI loader, and
# the BIOS path works on both kinds of machine.
bios.bootOrder = "cdrom,hdd"
scsi0.present = "TRUE"
scsi0.virtualDev = "lsilogic"
scsi0:0.present = "TRUE"
scsi0:0.fileName = "$VM.vmdk"
scsi0:0.deviceType = "disk"
ide1:0.present = "TRUE"
ide1:0.fileName = "$ISO"
ide1:0.deviceType = "cdrom-image"
ide1:0.startConnected = "TRUE"
ethernet0.present = "TRUE"
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "e1000"
usb.present = "TRUE"
sound.present = "FALSE"
EOF

# Create an empty virtual disk if vmware-vdiskmanager is available.
if command -v vmware-vdiskmanager >/dev/null 2>&1; then
  vmware-vdiskmanager -c -s "${HDD}MB" -a lsilogic -t 0 \
    "$WORKDIR/$VM.vmdk" >/dev/null
else
  echo "vmware-vdiskmanager not found: create the disk in the VMware UI,"
  echo "or install VMware Workstation's CLI tools."
fi

echo "VM created at: $WORKDIR/$VM.vmx"
echo "Open it in VMware Workstation and power on."
