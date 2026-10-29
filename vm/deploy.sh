#!/bin/bash
# Deploy the FinnOS sources to the dev VM, build them there, and restart the
# session.
#
#   vm/deploy.sh              build and install everything
#   vm/deploy.sh --no-restart build and install, leave the session alone
#
# The VM is the build machine: it has the DRM headers, the GPU stack, the
# fonts and a wayland-scanner. The host is only an editor.
set -euo pipefail

HOST="${FINN_HOST:-finn@10.211.55.6}"
REMOTE="${FINN_REMOTE:-~/finnos}"
RESTART=1
[ "${1:-}" = "--no-restart" ] && RESTART=0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if ssh "$HOST" 'test "$(id -u)" -eq 0' 2>/dev/null; then
    SUDO=""
elif ssh "$HOST" 'sudo -n true' 2>/dev/null; then
    SUDO="sudo"
else
    echo "error: $HOST is not reachable, is not root, or has no passwordless sudo" >&2
    exit 1
fi

echo "==> syncing $ROOT -> $HOST:$REMOTE"
tar --no-mac-metadata -C "$ROOT" -cf - \
    --exclude='.git' \
    --exclude='.commandcode' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='src/comp/finn-comp' \
    --exclude='src/comp/build' \
    --exclude='src/session/finn-session' \
    --exclude='src/shell/finn-shell' \
    --exclude='apps/*/finn-*' \
    --exclude='tests/rewrite/test_app_registry' \
    . | ssh "$HOST" "rm -rf $REMOTE && mkdir -p $REMOTE && tar -C $REMOTE -xf -"

echo "==> preparing the build environment"
ssh "$HOST" "
    set -e
    if [ -z \"\$($SUDO pacman-key --list-keys 2>/dev/null)\" ]; then
        $SUDO pacman-key --init
        $SUDO pacman-key --populate
    fi
    $SUDO pacman -S --needed --noconfirm --ignore=systemd --ignore=systemd-libs \
        base-devel pkgconf wlroots0.20 wayland wayland-protocols \
        libxkbcommon freetype2 fontconfig libdrm libinput mesa seatd grim
"

echo "==> preparing the dev user"
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
    $SUDO loginctl enable-linger finn 2>/dev/null || true
"

echo "==> building"
ssh "$HOST" "cd $REMOTE && make clean >/dev/null && make -j\$(nproc) 2>&1 | tail -40"

echo "==> installing"
ssh "$HOST" "cd $REMOTE && $SUDO make install && \
             $SUDO install -d /var/lib/finnpkg && \
             printf 'settings\\nfiles\\n' | $SUDO tee /var/lib/finnpkg/installed >/dev/null && \
             $SUDO chgrp -R finn /usr/share/finn /var/lib/finnpkg"

echo "==> installing the session units"
ssh "$HOST" "cd $REMOTE && \
             $SUDO install -Dm644 base/finn-comp.service /etc/systemd/system/finn-comp.service && \
             $SUDO install -Dm644 base/finn-session.service /etc/systemd/system/finn-session.service && \
             $SUDO install -Dm644 base/finn-shell.service /etc/systemd/system/finn-shell.service && \
             $SUDO install -Dm644 base/finn-failure.service /etc/systemd/system/finn-failure.service && \
             $SUDO install -Dm644 base/finn-tmpfiles.conf /usr/lib/tmpfiles.d/finn.conf && \
             $SUDO systemd-tmpfiles --create /usr/lib/tmpfiles.d/finn.conf && \
             $SUDO systemctl daemon-reload && \
             $SUDO rm -f /etc/systemd/system/finnd.service && \
             $SUDO systemctl enable finn-comp finn-session finn-shell getty@tty2.service && \
             $SUDO systemctl set-default graphical.target"

if [ "$RESTART" = "1" ]; then
    echo "==> restarting the session"
    ssh "$HOST" "$SUDO systemctl restart finn-comp && sleep 2 && \
                 $SUDO systemctl restart finn-session finn-shell && sleep 1 && \
                 systemctl is-active finn-comp finn-session finn-shell && \
                 $SUDO journalctl -u finn-comp -n 8 --no-pager -o cat"
fi

echo "==> done"
