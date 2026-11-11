#!/bin/bash
# Capture the FinnOS desktop from the dev VM with wlroots screencopy.
#
#   vm/shot.sh out.png
#
# The capture is taken by grim as the finn user against the compositor's
# display socket, then copied back. It works whether the compositor is on
# DRM or headless: both advertise zwlr_screencopy_manager_v1.
set -euo pipefail

HOST="${FINN_HOST:-finn@10.211.55.6}"

# FINN_SSH_OPTS is extra options for ssh and scp, for a VM that is reached
# through a config file rather than an address: vm/lima-setup.sh sets it to
# "-F <the instance's ssh.config>". It is split on spaces, so a path in it
# cannot contain one.
# shellcheck disable=SC2086
rsh() { ssh ${FINN_SSH_OPTS:-} "$@"; }
OUT="${1:-/tmp/finn-shot.png}"
REMOTE="/tmp/finn-shot-$$.png"

rsh "$HOST" "sudo -u finn env XDG_RUNTIME_DIR=/run/user/1000 \
    WAYLAND_DISPLAY=/run/finn/wayland-0 grim '$REMOTE'" >/dev/null
# shellcheck disable=SC2086
scp ${FINN_SSH_OPTS:-} -q -o BatchMode=yes "$HOST:$REMOTE" "$OUT"
# The capture is finn's file in a sticky /tmp, so only finn can remove it;
# the ssh user is not finn on every VM.
rsh "$HOST" "sudo -u finn rm -f '$REMOTE'"
echo "$OUT"
