#!/bin/bash
# Generate a FinnOS package-signing GPG keypair locally OUTSIDE the repository.
#
#   repo/gen-signing-key.sh [options]
#
# Private keys must NEVER live inside the git working tree. This script refuses
# any --key-dir that resolves inside the repository root, creates a 0700 GnuPG
# home outside the repo, generates an Ed25519 signing key, and exports ONLY
# the public keyring files needed by base/pkgbuilds/finnos-keyring.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
DEFAULT_KEY_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/finnos/signing-keys"

KEY_DIR="${FINN_KEY_DIR:-$DEFAULT_KEY_DIR}"
EXPORT_DIR=""
KEY_NAME="FinnOS Package Signing Key"
KEY_EMAIL="security@finnos.org"
KEY_EXPIRE="2y"
NO_PASSPHRASE=0
DRY_RUN=0

usage() {
    cat <<EOF
usage: gen-signing-key.sh [options]

  --key-dir <dir>     directory OUTSIDE the repo for private GnuPG state
                      (default: $DEFAULT_KEY_DIR)
  --export-dir <dir>  where to write public keyring files (finnos.gpg,
                      finnos.asc, finnos-trusted). Default: <key-dir>/public
  --name <name>       GPG Real Name (default: "$KEY_NAME")
  --email <email>     GPG Email (default: "$KEY_EMAIL")
  --expire <period>   key lifetime, e.g. 2y (default: $KEY_EXPIRE)
  --no-passphrase     skip passphrase prompt (for ephemeral test keys only)
  --dry-run           validate paths and print actions without running gpg
  -h, --help          show this help
EOF
}

die() { echo "error: $*" >&2; exit 1; }

canonical_path() {
    local target="$1"
    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$target"
    elif command -v realpath >/dev/null 2>&1; then
        realpath -m "$target" 2>/dev/null || realpath "$target"
    else
        # Fallback for POSIX shells when directory exists
        (cd "$target" 2>/dev/null && pwd -P) || printf '%s\n' "$target"
    fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        --key-dir)
            [ $# -ge 2 ] || die "--key-dir requires a path"
            KEY_DIR="$2"
            shift 2
            ;;
        --export-dir)
            [ $# -ge 2 ] || die "--export-dir requires a path"
            EXPORT_DIR="$2"
            shift 2
            ;;
        --name)
            [ $# -ge 2 ] || die "--name requires a value"
            KEY_NAME="$2"
            shift 2
            ;;
        --email)
            [ $# -ge 2 ] || die "--email requires a value"
            KEY_EMAIL="$2"
            shift 2
            ;;
        --expire)
            [ $# -ge 2 ] || die "--expire requires a value"
            KEY_EXPIRE="$2"
            shift 2
            ;;
        --no-passphrase)
            NO_PASSPHRASE=1
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
        *)
            usage >&2
            die "unknown option: $1"
            ;;
    esac
done

KEY_DIR_ABS="$(canonical_path "$KEY_DIR")"
REPO_ROOT_ABS="$(canonical_path "$REPO_ROOT")"

case "$KEY_DIR_ABS" in
    "$REPO_ROOT_ABS"|"$REPO_ROOT_ABS"/*)
        die "refusing to store private signing keys inside the git repository ($KEY_DIR_ABS)"
        ;;
esac

if [ -z "$EXPORT_DIR" ]; then
    EXPORT_DIR="$KEY_DIR_ABS/public"
fi
EXPORT_DIR_ABS="$(canonical_path "$EXPORT_DIR")"

GNUPGHOME_DIR="$KEY_DIR_ABS/gnupg"

if [ "$DRY_RUN" = 1 ]; then
    echo "[dry-run] repo root:   $REPO_ROOT_ABS"
    echo "[dry-run] key dir:     $KEY_DIR_ABS (verified outside repo)"
    echo "[dry-run] gnupghome:   $GNUPGHOME_DIR"
    echo "[dry-run] export dir:  $EXPORT_DIR_ABS"
    echo "[dry-run] identity:    $KEY_NAME <$KEY_EMAIL> (ed25519, expire=$KEY_EXPIRE)"
    exit 0
fi

command -v gpg >/dev/null 2>&1 || die "gpg is required to generate signing keys"

install -d -m 700 "$KEY_DIR_ABS" "$GNUPGHOME_DIR"
install -d -m 755 "$EXPORT_DIR_ABS"

chmod 700 "$GNUPGHOME_DIR"
export GNUPGHOME="$GNUPGHOME_DIR"

PARAM_FILE="$(mktemp)"
trap 'rm -f "$PARAM_FILE"' EXIT

{
    printf 'Key-Type: eddsa\n'
    printf 'Key-Curve: ed25519\n'
    printf 'Key-Usage: sign\n'
    printf 'Name-Real: %s\n' "$KEY_NAME"
    printf 'Name-Email: %s\n' "$KEY_EMAIL"
    printf 'Expire-Date: %s\n' "$KEY_EXPIRE"
    if [ "$NO_PASSPHRASE" = 1 ]; then
        printf '%%no-protection\n'
    fi
    printf '%%commit\n'
} > "$PARAM_FILE"

echo "==> generating Ed25519 signing key in $GNUPGHOME_DIR"
gpg --batch --generate-key "$PARAM_FILE"

FPR="$(gpg --with-colons --list-secret-keys "$KEY_EMAIL" | awk -F: '/^fpr:/ { print $10; exit }')"
[ -n "$FPR" ] || die "could not read fingerprint of generated key"

echo "==> exporting public keyring to $EXPORT_DIR_ABS"
gpg --armor --export "$FPR" > "$EXPORT_DIR_ABS/finnos.asc"
gpg --export "$FPR" > "$EXPORT_DIR_ABS/finnos.gpg"
printf '%s:4:\n' "$FPR" > "$EXPORT_DIR_ABS/finnos-trusted"

cat <<EOF

Signing key generated outside the repository.
  Fingerprint:   $FPR
  Private state: $GNUPGHOME_DIR  (NEVER commit this directory)
  Public export: $EXPORT_DIR_ABS/finnos.gpg, finnos.asc, finnos-trusted

Next steps:
  1. Copy only the public keyring files into base/pkgbuilds/finnos-keyring/:
       cp "$EXPORT_DIR_ABS/finnos.gpg" "$EXPORT_DIR_ABS/finnos-trusted" \\
          "$REPO_ROOT_ABS/base/pkgbuilds/finnos-keyring/"
  2. Update sha256sums in base/pkgbuilds/finnos-keyring/PKGBUILD (updpkgsums).
  3. Publish signed packages with:
       GNUPGHOME="$GNUPGHOME_DIR" repo/publish.sh --gpg-key "$FPR" ...
EOF
