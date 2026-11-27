#!/bin/bash
# Pre-install Hangover ARM64 Wine (Wine + FEX/Box64 WoW64 + DXVK) on aarch64 FinnOS.
# On x86_64, native pacman wine/winetricks/wine-mono/wine-gecko are used instead.
set -euo pipefail

if [ "$(uname -m)" != "aarch64" ]; then
    echo "install-hangover-arm64: skipping on $(uname -m) (x86_64 uses native wine)"
    exit 0
fi

if [ -x /opt/hangover/usr/bin/wine ] && [ -x /usr/local/bin/wine ]; then
    echo "install-hangover-arm64: already installed ($(/usr/local/bin/wine --version 2>/dev/null || echo ok))"
    exit 0
fi

HANGOVER_VER="${HANGOVER_VER:-11.16}"
HANGOVER_URL="${HANGOVER_URL:-https://github.com/AndreRH/hangover/releases/download/hangover-${HANGOVER_VER}/hangover_${HANGOVER_VER}_debian13_trixie_arm64.tar}"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

echo "==> downloading Hangover ${HANGOVER_VER} ARM64"
curl -fsSL -o "$TMPDIR/hangover.tar" "$HANGOVER_URL"
tar -C "$TMPDIR" -xf "$TMPDIR/hangover.tar"

echo "==> extracting Hangover packages into /opt/hangover"
mkdir -p /opt/hangover
for deb in "$TMPDIR"/*.deb; do
    [ -f "$deb" ] || continue
    DEB_WORK="$TMPDIR/deb_$(basename "$deb" .deb)"
    mkdir -p "$DEB_WORK"
    (cd "$DEB_WORK" && ar x "$deb")
    for data in "$DEB_WORK"/data.tar.*; do
        [ -f "$data" ] && tar -C /opt/hangover -xf "$data"
    done
done

# Extract DXVK if bundled in the release tarball
for dxvk in "$TMPDIR"/dxvk-*.tar.gz; do
    if [ -f "$dxvk" ]; then
        mkdir -p /usr/share/dxvk
        tar -C /usr/share/dxvk --strip-components=1 -xzf "$dxvk" || true
    fi
done

# Install clean /usr/local/bin/wine, wine64, wineserver wrappers
for tool in wine wine64 wineserver wineboot winecfg; do
    if [ -x "/opt/hangover/usr/bin/$tool" ]; then
        cat > "/usr/local/bin/$tool" <<EOF
#!/bin/sh
export LD_LIBRARY_PATH="/opt/hangover/usr/lib/aarch64-linux-gnu:/opt/hangover/usr/lib/aarch64-linux-gnu/wine\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export WINEDLLPATH="/opt/hangover/usr/lib/aarch64-linux-gnu/wine"
exec "/opt/hangover/usr/bin/$tool" "\$@"
EOF
        chmod 755 "/usr/local/bin/$tool"
    fi
done
if [ ! -x /usr/local/bin/wine64 ] && [ -x /usr/local/bin/wine ]; then
    ln -sf /usr/local/bin/wine /usr/local/bin/wine64
fi

echo "==> Hangover ARM64 Wine ready: $(/usr/local/bin/wine --version 2>/dev/null || echo installed)"
