#!/bin/bash
# Build the FinnOS runtime package that the OTA frontend installs.
#
#   scripts/build-runtime-pkg.sh [outdir]
#
# The package is a normal pacman package named `finnos-runtime`. It carries
# the compositor, the control plane, the package manager, the shell assets and
# the units — the part of the system that can be replaced while the machine is
# running. The kernel, firmware and the rest of the Arch base are deliberately
# not in it: those still move with pacman, and the Settings page says so.
#
# The build machine must be Arch (or an Arch container): makepkg, fakeroot and
# the development headers are the toolchain, not something this script should
# install behind the caller's back. The GitHub workflow uses the official
# archlinux image for exactly that reason.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
ARCH="$(uname -m)"

die() { echo "error: $*" >&2; exit 1; }

[ -n "$VERSION" ] || die "VERSION is empty"
case "$VERSION" in
    *[!0-9A-Za-z._+~-]*) die "VERSION contains a character pacman will not accept: $VERSION" ;;
esac
[ "$ARCH" = "x86_64" ] || [ "$ARCH" = "aarch64" ] || \
    die "this script builds x86_64 or aarch64 packages, not $ARCH"

for tool in makepkg fakeroot sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || \
        die "need $tool; run this on Arch or in the archlinux container"
done

if [ "$(id -u)" = "0" ]; then
    die "makepkg refuses to run as root; run it as the build user"
fi

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# makepkg wants a directory it owns. The tree is copied instead of referenced
# so the package cannot pick up an uncommitted file from somewhere else, and
# so the build is the same when the checkout is read-only.
BUILD="$WORK/finnos-runtime"
mkdir -p "$BUILD"
tar -C "$ROOT" -cf - \
    --exclude='.git' \
    --exclude='.cache' \
    --exclude='dist' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='src/comp/finn-comp' \
    --exclude='src/comp/build' \
    --exclude='src/session/finn-session' \
    --exclude='src/shell/finn-shell' \
    --exclude='src/ui/libfinnui.a' \
    --exclude='src/ui/build' \
    --exclude='apps/files/finn-files' \
    --exclude='apps/settings/finn-settings' \
    . | tar -C "$BUILD" -xf -

# makepkg builds from its srcdir, not from the directory the PKGBUILD sits
# in, so the tree goes in as a tarball and is extracted there. A PKGBUILD
# with no source array leaves an empty srcdir and the build cds into
# nothing, which is exactly how the first release job failed.
tar -C "$WORK" -czf "$WORK/finnos-runtime.tar.gz" finnos-runtime
mv "$WORK/finnos-runtime.tar.gz" "$BUILD/finnos-runtime.tar.gz"

cat > "$BUILD/PKGBUILD" <<EOF
pkgname=finnos-runtime
pkgver=$VERSION
pkgrel=1
pkgdesc="The FinnOS compositor, app runtime, session and shell"
arch=('$ARCH')
url="https://github.com/Finn-Technologies/FinnOS"
license=('MIT')
source=('finnos-runtime.tar.gz')
sha256sums=('SKIP')
depends=('glibc' 'wlroots0.20' 'systemd-libs' 'freetype2' 'fontconfig'
         'libdrm' 'libinput' 'libxkbcommon' 'wayland' 'mesa' 'libglvnd' 'seatd')
makedepends=('base-devel' 'pkgconf' 'wayland')
options=(!strip)

build() {
    cd "\$srcdir/finnos-runtime"
    make -j"\$(nproc)"
}

package() {
    cd "\$srcdir/finnos-runtime"
    make install DESTDIR="\$pkgdir" PREFIX=/usr/local

    install -Dm644 VERSION "\$pkgdir/usr/share/finn/version"

    install -Dm644 base/airootfs/etc/systemd/system/finn-comp.service \\
        "\$pkgdir/usr/lib/systemd/system/finn-comp.service"
    install -Dm644 base/airootfs/etc/systemd/system/finn-session.service \\
        "\$pkgdir/usr/lib/systemd/system/finn-session.service"
    install -Dm644 base/airootfs/etc/systemd/system/finn-shell.service \\
        "\$pkgdir/usr/lib/systemd/system/finn-shell.service"
    install -Dm644 base/airootfs/etc/systemd/system/finn-failure.service \\
        "\$pkgdir/usr/lib/systemd/system/finn-failure.service"
    install -Dm644 base/airootfs/usr/lib/sysusers.d/finn.conf \\
        "\$pkgdir/usr/lib/sysusers.d/finn.conf"
    install -Dm644 base/airootfs/usr/lib/tmpfiles.d/finn.conf \\
        "\$pkgdir/usr/lib/tmpfiles.d/finn.conf"
}
EOF

echo "==> building finnos-runtime $VERSION-$ARCH"
( cd "$BUILD" && makepkg --nodeps --nocheck --force --cleanbuild )

PKG="$(find "$BUILD" -maxdepth 1 -name "finnos-runtime-$VERSION-*-$ARCH.pkg.tar.zst" -print -quit)"
[ -n "$PKG" ] || die "makepkg produced no package"

# The release asset name is the one the OTA frontend looks for. The package's
# internal version keeps pkgrel, because that is what pacman records.
ASSET="finnos-runtime-$VERSION-$ARCH.pkg.tar.zst"
cp "$PKG" "$OUT/$ASSET"
( cd "$OUT" && sha256sum "$ASSET" > "$ASSET.sha256" )
echo "==> $OUT/$ASSET"
echo "==> $OUT/$ASSET.sha256"
