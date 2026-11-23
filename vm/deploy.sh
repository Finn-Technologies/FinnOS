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

# FINN_SSH_OPTS is extra options for ssh and scp, for a VM that is reached
# through a config file rather than an address: vm/lima-setup.sh sets it to
# "-F <the instance's ssh.config>". It is split on spaces, so a path in it
# cannot contain one.
# shellcheck disable=SC2086
rsh() { ssh ${FINN_SSH_OPTS:-} "$@"; }
REMOTE="${FINN_REMOTE:-~/finnos}"
RESTART=1
[ "${1:-}" = "--no-restart" ] && RESTART=0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if rsh "$HOST" 'test "$(id -u)" -eq 0' 2>/dev/null; then
    SUDO=""
elif rsh "$HOST" 'sudo -n true' 2>/dev/null; then
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
    --exclude='src/darwin/finn-darwin' \
    --exclude='src/ohos/finn-ohos' \
    --exclude='src/shell/finn-shell' \
    --exclude='apps/*/finn-*' \
    --exclude='build' \
    --exclude='tests/unit/test_app_registry' \
    --exclude='tests/unit/test_animation' \
    --exclude='tests/unit/test_session_client' \
    --exclude='tests/unit/test_search' \
    --exclude='tests/unit/test_compat' \
    --exclude='tests/unit/test_darwin' \
    --exclude='tests/unit/test_ohos' \
    --exclude='tests/unit/test_sys_files' \
    . | rsh "$HOST" "mkdir -p $REMOTE && tar -C $REMOTE -xf -"

echo "==> preparing the build environment"
rsh "$HOST" "
    set -e
    $SUDO rm -f /var/lib/pacman/db.lck
    if [ -z \"\$($SUDO pacman-key --list-keys 2>/dev/null)\" ]; then
        $SUDO pacman-key --init
        $SUDO pacman-key --populate
    fi
    $SUDO pacman -S --needed --noconfirm --ignore=systemd --ignore=systemd-libs \
        base-devel cmake ninja pkgconf wayland wayland-protocols \
        libxkbcommon freetype2 fontconfig libwebp libdrm libinput mesa seatd \
        grim qt6-base qt6-wayland qt6-svg qt6-imageformats qt6-tools qt6-declarative \
        qt6-shadertools qt6-webengine layer-shell-qt xorg-xwayland ttf-dejavu
"

echo "==> preparing the dev user"
rsh "$HOST" "
    set -e
    if ! id -u finn >/dev/null 2>&1; then
        $SUDO useradd -m -G wheel,input,video,render -s /bin/bash finn
        if getent group seat >/dev/null; then
            $SUDO usermod -aG seat finn
        fi
        $SUDO passwd -d finn
        $SUDO install -d -m755 /etc/sudoers.d
        echo 'finn ALL=(ALL) NOPASSWD: ALL' | $SUDO tee /etc/sudoers.d/finn >/dev/null
        $SUDO chmod 0440 /etc/sudoers.d/finn
    fi
    $SUDO loginctl enable-linger finn 2>/dev/null || true
"

echo "==> building"
rsh "$HOST" "cd $REMOTE && if [ \"${FINN_CLEAN:-0}\" = \"1\" ]; then make clean >/dev/null; fi && make -j\$(nproc) 2>&1 | tail -40"

echo "==> installing"
rsh "$HOST" "cd $REMOTE && $SUDO make install && \
             $SUDO QT_QPA_PLATFORM=offscreen /usr/local/bin/finn-cursors /usr/share/finn/pointers /usr/share/icons && \
             $SUDO install -d /var/lib/finn && \
             printf 'settings\\nfiles\\nterminal\\nnotes\\nstore\\nviewer\\nmonitor\\ncalculator\\narchive\\ncapture\\nflourish\\n' | $SUDO tee /var/lib/finn/installed >/dev/null && \
             printf '/usr/local/lib\\n' | $SUDO tee /etc/ld.so.conf.d/finn.conf >/dev/null && \
             $SUDO ldconfig && \
             $SUDO chgrp -R finn /usr/share/finn /var/lib/finn"

echo "==> installing the session units & FinnOS identity"
rsh "$HOST" "cd $REMOTE && \
             $SUDO install -Dm644 base/airootfs/etc/os-release /etc/os-release && \
             $SUDO install -Dm644 base/airootfs/etc/os-release /usr/lib/os-release && \
             $SUDO install -Dm644 base/installed/etc/issue /etc/issue && \
             $SUDO install -Dm644 base/installed/etc/motd /etc/motd && \
             $SUDO hostnamectl set-hostname finnos 2>/dev/null || printf 'finnos\n' | $SUDO tee /etc/hostname >/dev/null && \
             $SUDO install -Dm644 base/finn-comp.service /etc/systemd/system/finn-comp.service && \
             $SUDO install -Dm644 base/finn-session.service /etc/systemd/system/finn-session.service && \
             $SUDO install -Dm644 base/finn-shell.service /etc/systemd/system/finn-shell.service && \
             $SUDO install -Dm644 base/finn-wallpaper.service /etc/systemd/system/finn-wallpaper.service && \
             $SUDO install -Dm644 base/finn-failure.service /etc/systemd/system/finn-failure.service && \
             $SUDO install -Dm644 base/finn-tmpfiles.conf /usr/lib/tmpfiles.d/finn.conf && \
             if [ -e /dev/fb0 ] && $SUDO grep -q 'virgl.*: no' /sys/kernel/debug/dri/0/virtio-gpu-features 2>/dev/null; then \
                 $SUDO install -d -m755 /etc/finn /etc/systemd/system/finn-comp.service.d && \
                 $SUDO rm -f /etc/systemd/system/finn-comp.service.d/10-qemu-fb.conf && \
                 if [ ! -f /etc/finn/kms.json ]; then \
                     printf '{\n  \"device\": \"/dev/dri/card0\",\n  \"outputs\": [\n    { \"name\": \"Virtual1\", \"mode\": \"1440x900\" },\n    { \"name\": \"Virtual-1\", \"mode\": \"1440x900\" }\n  ]\n}\n' | $SUDO tee /etc/finn/kms.json >/dev/null; \
                 fi && \
                 printf '[Service]\nEnvironment=QT_QPA_PLATFORM=linuxfb\nEnvironment=QT_QPA_FB_DRM=1\nEnvironment=QT_QPA_KMS_CONFIG=/etc/finn/kms.json\nEnvironment=QT_QPA_EGLFS_KMS_CONFIG=/etc/finn/kms.json\nEnvironment=QT_QUICK_BACKEND=software\n' | $SUDO tee /etc/systemd/system/finn-comp.service.d/renderer.conf >/dev/null; \
             fi && \
             $SUDO systemd-tmpfiles --create /usr/lib/tmpfiles.d/finn.conf && \
             $SUDO systemctl daemon-reload && \
             $SUDO systemctl enable seatd finn-comp finn-session finn-shell finn-wallpaper getty@tty2.service && \
             $SUDO systemctl set-default graphical.target"

if [ "$RESTART" = "1" ]; then
    echo "==> restarting the session"
    rsh "$HOST" "$SUDO systemctl start seatd && \
                 $SUDO systemctl stop getty@tty1.service finn-failure.service 2>/dev/null || true && \
                 $SUDO systemctl reset-failed finn-comp finn-session finn-shell finn-wallpaper 2>/dev/null || true && \
                 $SUDO systemctl restart finn-comp && sleep 2 && \
                 $SUDO systemctl restart finn-session finn-shell finn-wallpaper && sleep 1 && \
                 systemctl is-active finn-comp finn-session finn-shell finn-wallpaper && \
                 $SUDO journalctl -u finn-comp -n 8 --no-pager -o cat"
fi

echo "==> done"
