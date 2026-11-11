#!/bin/bash
# mini-version of deploy.sh (only restart the compositor)
set -euo pipefail

HOST="${FINN_HOST:-finn@10.211.55.6}"

# FINN_SSH_OPTS is extra options for ssh and scp, for a VM that is reached
# through a config file rather than an address: vm/lima-setup.sh sets it to
# "-F <the instance's ssh.config>". It is split on spaces, so a path in it
# cannot contain one.
# shellcheck disable=SC2086
rsh() { ssh ${FINN_SSH_OPTS:-} "$@"; }

if rsh "$HOST" 'test "$(id -u)" -eq 0' 2>/dev/null; then
    SUDO=""
elif rsh "$HOST" 'sudo -n true' 2>/dev/null; then
    SUDO="sudo"
else
    echo "error: $HOST is not reachable, is not root, or has no passwordless sudo" >&2
    echo "       connect as root on a bare rootfs: FINN_HOST=root@<address>" >&2
    exit 1
fi

echo "==> restarting the compositor"
    rsh "$HOST" "$SUDO systemctl restart finnd finn-comp"

echo "==> done"
