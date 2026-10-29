#!/bin/bash
# mini-version of deploy.sh (only restart the compositor)
set -euo pipefail

HOST="${FINN_HOST:-finn@10.211.55.6}"

if ssh "$HOST" 'test "$(id -u)" -eq 0' 2>/dev/null; then
    SUDO=""
elif ssh "$HOST" 'sudo -n true' 2>/dev/null; then
    SUDO="sudo"
else
    echo "error: $HOST is not reachable, is not root, or has no passwordless sudo" >&2
    echo "       connect as root on a bare rootfs: FINN_HOST=root@<address>" >&2
    exit 1
fi

echo "==> restarting the compositor"
    ssh "$HOST" "$SUDO systemctl restart finnd finn-comp"

echo "==> done"
