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
OUT="${1:-/tmp/finn-shot.png}"
REMOTE="/tmp/finn-shot-$$.png"

ssh "$HOST" "sudo -u finn env XDG_RUNTIME_DIR=/run/user/1000 \
    WAYLAND_DISPLAY=/run/finn/wayland-0 grim '$REMOTE'" >/dev/null
scp -q -o BatchMode=yes "$HOST:$REMOTE" "$OUT"
ssh "$HOST" "rm -f '$REMOTE'"
echo "$OUT"
