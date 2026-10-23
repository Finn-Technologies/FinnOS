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
# The sources go over as a tar stream rather than with rsync: the VM is a
# minimal Arch install and does not have rsync on it.
set -euo pipefail

HOST="${FINN_HOST:-finn@192.168.44.128}"
REMOTE="${FINN_REMOTE:-~/finnos}"
RESTART=1
[ "${1:-}" = "--no-restart" ] && RESTART=0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

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

echo "==> building"
ssh "$HOST" "cd $REMOTE && make clean >/dev/null && make -j\$(nproc) 2>&1 | tail -40"

echo "==> installing"
# The wallpapers are data, not build output, so `make install` does not
# put them on the machine; they are copied here or a deploy leaves the
# shell running with a wallpaper from an older install. Both of them:
# finn-comp opens default.webp, and the first-run screen draws setup.webp.
ssh "$HOST" "cd $REMOTE && sudo make install && \
             sudo install -Dm644 assets/wallpaper/default.webp /usr/share/finn/wallpaper/default.webp && \
             sudo install -Dm644 assets/wallpaper/setup.webp /usr/share/finn/wallpaper/setup.webp && \
             sudo install -d /var/lib/finnpkg && \
             printf 'settings\nfiles\narc\nwindow\n' | sudo tee /var/lib/finnpkg/installed >/dev/null && \
             sudo chgrp -R finn /usr/share/finn /var/lib/finnpkg"

if [ "$RESTART" = "1" ]; then
    echo "==> restarting the compositor"
    ssh "$HOST" "sudo systemctl restart finnd finn-comp && sleep 2 && \
                 systemctl is-active finn-comp finnd && \
                 sudo journalctl -u finn-comp -n 10 --no-pager -o cat"
fi

echo "==> done"
