#!/bin/bash
# Publish signed FinnOS packages into a pacman repository layout and optionally
# sync to a remote mirror with rsync.
#
#   repo/publish.sh --arch x86_64 --gpg-key <FPR> [--repo-dir <dir>] \
#                   [--rsync user@host:/srv/http/repo/] pkg1.pkg.tar.zst ...
#
# Repository layout produced:
#   <repo-dir>/finnos/os/<arch>/
#     finnos.db.tar.zst (+ .sig, finnos.db symlink)
#     finnos.files.tar.zst (+ .sig, finnos.files symlink)
#     <pkgname>-<pkgver>-<pkgrel>-<arch>.pkg.tar.zst (+ .sig)
set -euo pipefail

DEFAULT_REPO_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/finnos/repo"
REPO_DIR="${FINN_REPO_DIR:-$DEFAULT_REPO_DIR}"
ARCH=""
GPG_KEY="${FINN_GPG_KEY:-}"
GNUPGHOME_ARG="${GNUPGHOME:-}"
RSYNC_DEST=""
RESIGN=0
DRY_RUN=0
PKGS=()

usage() {
    cat <<EOF
usage: publish.sh [options] <pkg.pkg.tar.zst>...

  --arch <x86_64|aarch64>  target repository architecture (required)
  --gpg-key <keyid|fpr>    GPG signing key fingerprint or ID (or FINN_GPG_KEY)
  --gnupghome <dir>        GnuPG home containing the signing key
  --repo-dir <dir>         local staging root (default: $DEFAULT_REPO_DIR)
  --rsync <target>         optional rsync destination (e.g. deploy@host:/srv/repo/)
  --re-sign                overwrite existing .sig files on package tarballs
  --dry-run                validate inputs and print planned commands
  -h, --help               show this help
EOF
}

die() { echo "error: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            [ $# -ge 2 ] || die "--arch requires x86_64 or aarch64"
            ARCH="$2"
            shift 2
            ;;
        --gpg-key)
            [ $# -ge 2 ] || die "--gpg-key requires a key fingerprint or ID"
            GPG_KEY="$2"
            shift 2
            ;;
        --gnupghome)
            [ $# -ge 2 ] || die "--gnupghome requires a directory"
            GNUPGHOME_ARG="$2"
            shift 2
            ;;
        --repo-dir)
            [ $# -ge 2 ] || die "--repo-dir requires a directory"
            REPO_DIR="$2"
            shift 2
            ;;
        --rsync)
            [ $# -ge 2 ] || die "--rsync requires a destination"
            RSYNC_DEST="$2"
            shift 2
            ;;
        --re-sign)
            RESIGN=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            PKGS+=("$@")
            break
            ;;
        -*)
            usage >&2
            die "unknown option: $1"
            ;;
        *)
            PKGS+=("$1")
            shift
            ;;
    esac
done

case "$ARCH" in
    x86_64|aarch64) ;;
    *) die "--arch must be x86_64 or aarch64 (got '${ARCH:-empty}')" ;;
esac

[ -n "$GPG_KEY" ] || die "--gpg-key (or FINN_GPG_KEY) is required because [finnos] uses SigLevel = Required"
[ "${#PKGS[@]}" -gt 0 ] || die "at least one .pkg.tar.zst package path is required"

for pkg in "${PKGS[@]}"; do
    [ -f "$pkg" ] || die "package not found: $pkg"
    case "$pkg" in
        *.pkg.tar.*) ;;
        *) die "unexpected package filename (expected *.pkg.tar.*): $pkg" ;;
    esac
done

ARCH_DIR="$REPO_DIR/finnos/os/$ARCH"
DB_PATH="$ARCH_DIR/finnos.db.tar.zst"

if [ "$DRY_RUN" = 1 ]; then
    echo "[dry-run] arch dir:  $ARCH_DIR"
    echo "[dry-run] db path:   $DB_PATH"
    echo "[dry-run] gpg key:   $GPG_KEY"
    for pkg in "${PKGS[@]}"; do
        base="$(basename "$pkg")"
        echo "[dry-run] stage:     $pkg -> $ARCH_DIR/$base"
        echo "[dry-run] sign:      gpg --detach-sign --local-user $GPG_KEY -o $ARCH_DIR/$base.sig $ARCH_DIR/$base"
    done
    echo "[dry-run] index:     repo-add --verify --sign --key $GPG_KEY $DB_PATH ${PKGS[*]##*/}"
    if [ -n "$RSYNC_DEST" ]; then
        echo "[dry-run] sync:      rsync -avz --delete-after --delay-updates $REPO_DIR/finnos/ $RSYNC_DEST"
    fi
    exit 0
fi

for tool in gpg repo-add; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is required (run on Arch Linux or in an Arch container)"
done
if [ -n "$RSYNC_DEST" ]; then
    command -v rsync >/dev/null 2>&1 || die "rsync is required when --rsync is set"
fi

if [ -n "$GNUPGHOME_ARG" ]; then
    [ -d "$GNUPGHOME_ARG" ] || die "gnupghome directory does not exist: $GNUPGHOME_ARG"
    export GNUPGHOME="$GNUPGHOME_ARG"
fi

install -d -m 755 "$ARCH_DIR"

STAGED_PKGS=()
for pkg in "${PKGS[@]}"; do
    base="$(basename "$pkg")"
    dest="$ARCH_DIR/$base"
    install -m 644 "$pkg" "$dest"
    if [ "$RESIGN" = 1 ] || [ ! -f "$dest.sig" ]; then
        rm -f "$dest.sig"
        gpg --batch --yes --detach-sign --local-user "$GPG_KEY" --output "$dest.sig" "$dest"
    fi
    STAGED_PKGS+=("$dest")
done

echo "==> updating signed pacman database $DB_PATH"
repo-add --verify --sign --key "$GPG_KEY" "$DB_PATH" "${STAGED_PKGS[@]}"

if [ -n "$RSYNC_DEST" ]; then
    echo "==> syncing $REPO_DIR/finnos/ -> $RSYNC_DEST"
    rsync -avz --delete-after --delay-updates "$REPO_DIR/finnos/" "$RSYNC_DEST"
fi

echo "==> published ${#STAGED_PKGS[@]} package(s) to $ARCH_DIR"
