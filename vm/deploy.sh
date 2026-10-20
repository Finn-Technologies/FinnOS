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

HOST="${FINN_HOST:-finn@10.211.55.6}"
REMOTE="${FINN_REMOTE:-~/finnos}"
RESTART=1
[ "${1:-}" = "--no-restart" ] && RESTART=0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "==> syncing $ROOT -> $HOST:$REMOTE"
tar --no-mac-metadata -C "$ROOT" -cf - \
    --exclude='.git' \
    --exclude='*.o' \
    --exclude='finn-comp/finn-comp' \
    --exclude='finn-comp/finnd' \
    --exclude='finn-comp/finnctl' \
    --exclude='finn-comp/dockprev' \
    --exclude='finn-comp/fvtest' \
    . | ssh "$HOST" "rm -rf $REMOTE && mkdir -p $REMOTE && tar -C $REMOTE -xf -"

echo "==> building"
ssh "$HOST" "cd $REMOTE/finn-comp && make clean >/dev/null && make -j\$(nproc) 2>&1 | tail -40"

echo "==> installing"
ssh "$HOST" "cd $REMOTE/finn-comp && sudo make install && \
             sudo install -d /var/lib/finnpkg && \
             sudo install -m644 $REMOTE/assets/apps.registry /usr/share/finn/apps.registry && \
             sudo chgrp -R finn /usr/share/finn /var/lib/finnpkg"

if [ "$RESTART" = "1" ]; then
    echo "==> restarting the compositor"
    ssh "$HOST" "sudo systemctl restart finnd finn-comp && sleep 2 && \
                 systemctl is-active finn-comp finnd && \
                 sudo journalctl -u finn-comp -n 10 --no-pager -o cat"
fi

echo "==> done"
