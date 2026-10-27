#!/bin/bash
# Deploy the FinnOS sources to the dev VM, build them there, and restart the
# compositor.
#
#   vm/deploy.sh              build and install everything
#   vm/deploy.sh --no-restart build and install, leave the compositor alone
#
# The VM is the build machine: it has the DRM headers, the GPU stack and the
# fonts, and it is where the compositor actually runs. The host is only an
# editor, so nothing is compiled here.
#
# It also prepares a bare Arch Linux ARM rootfs: the pacman keyring and sync
# database, the build dependencies, the finn user, and the units. Connect to
# such a rootfs as root (FINN_HOST=root@<address>); a FinnOS dev image can
# keep using finn with its passwordless sudo.
#
# The sources go over as a tar stream rather than with rsync: the VM is a
# minimal Arch install and does not have rsync on it.
set -euo pipefail

HOST="${FINN_HOST:-finn@10.211.55.6}"
REMOTE="${FINN_REMOTE:-~/finnos}"
RESTART=1
[ "${1:-}" = "--no-restart" ] && RESTART=0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# The rootfs case has no sudo, and the dev image has no root login. Probe
# once: an empty SUDO is the root case, and a user that would have to type a
# password cannot be driven over a non-interactive ssh at all.
if ssh "$HOST" 'test "$(id -u)" -eq 0' 2>/dev/null; then
    SUDO=""
elif ssh "$HOST" 'sudo -n true' 2>/dev/null; then
    SUDO="sudo"
else
    echo "error: $HOST is not reachable, is not root, or has no passwordless sudo" >&2
    echo "       connect as root on a bare rootfs: FINN_HOST=root@<address>" >&2
    exit 1
fi

echo "==> syncing $ROOT -> $HOST:$REMOTE"
tar --no-mac-metadata -C "$ROOT" -cf - \
    --exclude='.git' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='src/compositor/finn-comp' \
    --exclude='src/control/finnd' \
    --exclude='src/control/finnctl' \
    --exclude='src/control/finn-window' \
    --exclude='src/tools/vgtest' \
    --exclude='src/tools/dockprev' \
    . | ssh "$HOST" "rm -rf $REMOTE && mkdir -p $REMOTE && tar -C $REMOTE -xf -"

echo "==> preparing the build environment"

# A bare Arch Linux ARM rootfs arrives without a populated keyring or sync
# databases. The dev image has both, so these are no-ops there. On a fresh
# rootfs the keyring has to exist before the first signed pacman run, and
# the first full sync is what creates the databases.
ssh "$HOST" "
    set -e
    if [ -z \"\$($SUDO pacman-key --list-keys 2>/dev/null)\" ]; then
        $SUDO pacman-key --init
        $SUDO pacman-key --populate
    fi
    if [ ! -d /var/lib/pacman/sync ] || [ -z \"\$(ls -A /var/lib/pacman/sync 2>/dev/null)\" ]; then
        $SUDO pacman -Syu --noconfirm
    fi
    $SUDO pacman -S --needed --noconfirm \
        base-devel pkgconf libdrm libwebp freetype2 alsa-lib mesa \
        wayland wayland-protocols libxkbcommon
"

echo "==> preparing the dev user"

# A rootfs has no finn. useradd leaves the password locked; the account is
# for file ownership and the services, and the host reaches the VM as root
# or with a key. The groups are the ones base/setup.sh uses; seat only
# exists once seatd is installed.
ssh "$HOST" "
    set -e
    if ! id -u finn >/dev/null 2>&1; then
        $SUDO useradd -m -G wheel,input,video,render -s /bin/bash finn
        if getent group seat >/dev/null; then
            $SUDO usermod -aG seat finn
        fi
        $SUDO passwd -l finn
        $SUDO install -d -m755 /etc/sudoers.d
        echo 'finn ALL=(ALL) NOPASSWD: ALL' | $SUDO tee /etc/sudoers.d/finn >/dev/null
        $SUDO chmod 0440 /etc/sudoers.d/finn
    fi
"

echo "==> installing the system units"

# The same files base/setup.sh installs. The tmpfiles file belongs in
# /usr/lib/tmpfiles.d, not /etc: an /etc copy would shadow the one in the
# tree and survive a change to it.
ssh "$HOST" "cd $REMOTE && \
             $SUDO install -Dm644 base/finnd.service /etc/systemd/system/finnd.service && \
             $SUDO install -Dm644 base/finn-comp.service /etc/systemd/system/finn-comp.service && \
             $SUDO install -Dm644 base/finn-failure.service /etc/systemd/system/finn-failure.service && \
             $SUDO install -Dm644 base/finn-tmpfiles.conf /usr/lib/tmpfiles.d/finn.conf && \
             $SUDO systemd-tmpfiles --create /usr/lib/tmpfiles.d/finn.conf && \
             $SUDO systemctl daemon-reload"

echo "==> building"
ssh "$HOST" "cd $REMOTE && make clean >/dev/null && make -j\$(nproc) 2>&1 | tail -40"

echo "==> installing"
# The wallpapers are data, not build output, so `make install` does not
# put them on the machine; they are copied here or a deploy leaves the
# shell running with a wallpaper from an older install. Both of them:
# finn-comp opens default.webp, and the first-run screen draws setup.webp.
ssh "$HOST" "cd $REMOTE && $SUDO make install && \
             $SUDO install -Dm644 assets/wallpaper/default.webp /usr/share/finn/wallpaper/default.webp && \
             $SUDO install -Dm644 assets/wallpaper/setup.webp /usr/share/finn/wallpaper/setup.webp && \
             $SUDO install -d /var/lib/finnpkg && \
             if ! $SUDO test -f /var/lib/finnpkg/installed; then \
                 for app in settings files store; do \
                     $SUDO /usr/local/bin/finnpkg install \$app; \
                 done; \
             fi && \
             $SUDO chgrp -R finn /usr/share/finn /var/lib/finnpkg"

# The units are installed now, and the binaries they start exist. Enabling
# them here rather than before the build keeps a failed build from leaving a
# fresh rootfs that tries to start a session it does not have.
echo "==> enabling the session"
ssh "$HOST" "$SUDO systemctl enable finn-comp finnd getty@tty2.service && \
             $SUDO systemctl set-default graphical.target"

if [ "$RESTART" = "1" ]; then
    echo "==> restarting the compositor"
    ssh "$HOST" "$SUDO systemctl restart finnd finn-comp && sleep 2 && \
                 systemctl is-active finn-comp finnd && \
                 $SUDO journalctl -u finn-comp -n 10 --no-pager -o cat"
fi

echo "==> done"
