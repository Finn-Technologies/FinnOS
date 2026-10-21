#!/bin/bash
# Capture the FinnOS desktop from the dev VM.
#
#   vm/shot.sh out.png          a full-resolution capture
#   vm/shot.sh out.png 2        every second pixel (half resolution)
#
# The capture comes back as a binary PPM over the control plane and is
# converted here, so nothing has to be installed on the device for a
# screenshot to work. Pillow does the conversion; without it the raw PPM is
# left where it was and its path is printed.
set -euo pipefail

HOST="${FINN_HOST:-finn@192.168.44.128}"
OUT="${1:-/tmp/finn-shot.png}"
STEP="${2:-1}"

TMP="$(mktemp -t finnshot).ppm"
ssh "$HOST" "sudo finnctl screen.capture step=$STEP" > "$TMP"

if python3 -c "import PIL" >/dev/null 2>&1; then
    python3 - "$TMP" "$OUT" <<'PY'
import sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
Image.open(src).save(dst)
print(dst)
PY
    rm -f "$TMP"
else
    echo "no Pillow on this host: leaving the PPM at $TMP"
    exit 1
fi
