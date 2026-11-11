#!/bin/bash
# Create the FinnOS dev VM on an Apple Silicon Mac with Lima, and deploy the
# tree into it.
#
#   vm/lima-setup.sh             create (or reuse) the VM, then vm/deploy.sh
#   FINN_LIMA_NAME=other ...     a second instance beside the first
#
# Needs `limactl` and `qemu-system-aarch64` (brew install lima qemu).
#
# The image Lima can fetch is Arch Linux ARM from March 2022, and bringing
# it to today is where a hand-made VM goes wrong, so the steps are here
# once, with the reason beside each. It is safe to run again: a VM that is
# already upgraded is only redeployed.
set -euo pipefail

NAME="${FINN_LIMA_NAME:-finnos}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

for tool in limactl qemu-system-aarch64; do
    command -v "$tool" >/dev/null 2>&1 ||
        { echo "error: $tool is not installed (brew install lima qemu)" >&2; exit 1; }
done

guest() { limactl shell --workdir / "$NAME" sudo bash -c "$1"; }

if ! limactl list -q 2>/dev/null | grep -qx "$NAME"; then
    echo "==> creating the Lima instance $NAME"
    limactl create --name="$NAME" --tty=false "$ROOT/vm/lima.yaml"
fi

if [ "$(limactl list --format '{{.Status}}' "$NAME")" != "Running" ]; then
    echo "==> starting $NAME"
    limactl start --tty=false "$NAME"
fi

if ! guest 'test -f /var/lib/finn-lima-upgraded'; then
    echo "==> upgrading the 2022 image"
    guest '
        set -e
        # /boot is 200 MB and a current kernel package wants about 130 MB
        # of it, half of that device trees for boards this VM is not. QEMU
        # hands the guest its own tree, and startup.nsh boots Image, not
        # Image.gz, so neither is extracted, and the old copies go first so
        # the new kernel is never written to a full partition.
        rm -rf /boot/dtbs /boot/Image.gz /boot/initramfs-linux-fallback.img
        sed -i "s|^#NoExtract.*|NoExtract = boot/dtbs/* boot/Image.gz|" /etc/pacman.conf
        grep -q "^NoExtract = boot/dtbs" /etc/pacman.conf

        # The keyring first: the 2014 one cannot verify today'"'"'s packages.
        pacman -Sy --noconfirm archlinuxarm-keyring
        pacman-key --populate archlinuxarm

        # Everything in one transaction, kernel included. A current pacman
        # sandboxes its downloads with Landlock, which the 5.17 kernel does
        # not have, so an upgrade that leaves the kernel for later cannot
        # fetch it without --disable-sandbox.
        #
        # The space check is off for this one transaction: it counts the
        # device trees NoExtract is about to skip, asks for 170 MB of a
        # 200 MB partition and refuses. What is written is about 70 MB.
        sed -i "s/^CheckSpace/#CheckSpace/" /etc/pacman.conf
        pacman -Su --noconfirm --overwrite "*"
        sed -i "s/^#CheckSpace/CheckSpace/" /etc/pacman.conf
        test -s /boot/Image
        test -s /boot/initramfs-linux.img

        # A current systemd renames eth0 to enp0s*, and the image'"'"'s
        # network configuration matches eth0: without this the VM boots
        # with no network and Lima never reaches it.
        grep -q net.ifnames=0 /boot/startup.nsh ||
            sed -i "s/initramfs-linux.img/initramfs-linux.img net.ifnames=0/" /boot/startup.nsh
        grep -q net.ifnames=0 /boot/startup.nsh

        # cloud-init moved to a single process behind two new units, and an
        # upgrade does not enable them; Lima waits for cloud-init at boot.
        systemctl enable cloud-init-main.service cloud-init-network.service

        install -d /var/lib
        touch /var/lib/finn-lima-upgraded
    '
    echo "==> restarting $NAME on the new kernel"
    limactl stop "$NAME"
    limactl start --tty=false "$NAME"
fi

# Without a host GPU wlroots picks GLES2 on llvmpipe, and what QEMU shows
# then has stale blocks the compositor's own frame does not (grim is clean,
# the window is not). Pixman is the renderer the tree is tested with.
guest '
    set -e
    install -d /etc/systemd/system/finn-comp.service.d
    printf "[Service]\nEnvironment=WLR_RENDERER=pixman\n" \
        > /etc/systemd/system/finn-comp.service.d/renderer.conf
    systemctl daemon-reload
'

DIR="$(limactl list --format '{{.Dir}}' "$NAME")"
export FINN_HOST="lima-$NAME"
export FINN_SSH_OPTS="-F $DIR/ssh.config"

"$ROOT/vm/deploy.sh"

echo
echo "The VM is $NAME. To deploy or capture again:"
echo "  export FINN_HOST=$FINN_HOST FINN_SSH_OPTS=\"$FINN_SSH_OPTS\""
echo "  vm/deploy.sh"
echo "  vm/shot.sh out.png"
