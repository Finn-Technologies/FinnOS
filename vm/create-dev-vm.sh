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
floppy0.present = "FALSE"
sound.present = "FALSE"
vmci0.present = "TRUE"
svga.autodetect = "TRUE"
tools.syncTime = "TRUE"
tools.upgrade.policy = "manual"
isolation.tools.copy.disable = "FALSE"
isolation.tools.paste.disable = "FALSE"
EOF

# Guest SVGA3D (EGL/GLES2 in finn-comp) needs a host Vulkan device that meets
# VMware 26's device limits. Older Intel GPUs have incomplete Mesa Vulkan
# support; install vulkan-swrast (lavapipe) and set FINN_VM_VULKAN_DEVICE to
# its device name to force it. Without that, VMware falls back to 2D and
# finn-comp uses its pixman renderer instead.
if [ "${FINN_VM_3D:-1}" = "1" ]; then
  cat >> "$WORKDIR/$VM.vmx" <<EOF2
svga3d.enable = "TRUE"
mks.enable3d = "TRUE"
mks.gl.allowUnsupportedDrivers = "TRUE"
mks.gl.allowBlacklistedDrivers = "TRUE"
mks.gl.allowSoftwareAndVMwareDrivers = "TRUE"
mks.vk.allowUnsupportedDevices = "TRUE"
EOF2
  if [ -n "${FINN_VM_VULKAN_DEVICE:-}" ]; then
    printf 'mks.vk.forceDevice = "%s"\n' "$FINN_VM_VULKAN_DEVICE" >> "$WORKDIR/$VM.vmx"
  fi
fi

# Create an empty virtual disk if vmware-vdiskmanager or qemu-img is available.
if command -v vmware-vdiskmanager >/dev/null 2>&1; then
  vmware-vdiskmanager -c -s "${HDD}MB" -a lsilogic -t 0 \
    "$WORKDIR/$VM.vmdk" >/dev/null
elif command -v qemu-img >/dev/null 2>&1; then
  qemu-img create -f vmdk -o adapter_type=lsilogic "$WORKDIR/$VM.vmdk" "${HDD}M" >/dev/null
else
  echo "vmware-vdiskmanager and qemu-img not found: create the disk in the VMware UI,"
  echo "or install VMware Workstation's CLI tools."
fi

echo "VM created at: $WORKDIR/$VM.vmx"
echo "Open it in VMware Workstation and power on."
