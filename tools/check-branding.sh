#!/bin/bash
# Verify that user-visible FinnOS surfaces and documentation carry no stray
# upstream distribution branding ("Arch Linux", "archlinux", "ArchWiki") or
# stale repository slugs. Functional base hooks (archisobasedir, archisolabel,
# mkarchiso, arch-chroot, pacman, ID_LIKE=arch, CI container tags, and
# systemd unit dependencies) are explicitly outside user-visible branding.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FAIL=0

report_fail() {
    local label="$1"
    local output="$2"
    if [ -n "$output" ]; then
        echo "branding: FAIL ($label):" >&2
        printf '%s\n' "$output" >&2
        FAIL=1
    fi
}

# 1. Boot, identity, Plymouth, and live-root login surfaces.
boot_surfaces=(
    base/airootfs/etc/os-release
    base/airootfs/etc/issue
    base/airootfs/etc/motd
    base/installed/etc/issue
    base/installed/etc/motd
    base/airootfs/root
    base/airootfs/etc/plymouth
    base/airootfs/usr/share/plymouth
    base/boot
)

hits="$(grep -rnI -i -E 'arch[ _-]?linux|archwiki|wiki\.archlinux|github\.com/finnos/finnos' \
    "${boot_surfaces[@]}" 2>/dev/null || true)"
report_fail "boot/identity/login surfaces" "$hits"

# 2. Native apps, compositor/shell/UI source, and SDK sources (excluding CI workflow container specs).
hits="$(grep -rnI -i -E 'arch[ _-]?linux|archwiki|wiki\.archlinux|github\.com/finnos/finnos' \
    apps src sdk \
    --exclude-dir=build --exclude-dir='*.dSYM' --exclude-dir=.github \
    2>/dev/null || true)"
report_fail "apps/src/sdk" "$hits"

# 3. Top-level READMEs and docs (excluding NOTICE, CREDITS, rebrand-checklist.md,
# and the intentional engineering-base disclosure in docs/design/contract.md:8).
doc_hits="$(grep -rnI -i -E 'arch[ _-]?linux|archwiki|wiki\.archlinux|github\.com/finnos/finnos' \
    README.md CONTRIBUTING.md base/README.md docs \
    --exclude='rebrand-checklist.md' \
    --exclude-dir=audit \
    2>/dev/null \
    | grep -v '^docs/design/contract\.md:8:Arch Linux: the kernel, firmware, systemd, pacman' \
    || true)"
report_fail "README/docs" "$doc_hits"

# 4. Verify os-release identity and BUG_REPORT_URL match origin remote.
if ! grep -q '^ID=finnos$' base/airootfs/etc/os-release; then
    report_fail "os-release ID" "base/airootfs/etc/os-release is missing ID=finnos"
fi
if ! grep -q '^PRETTY_NAME="FinnOS"$' base/airootfs/etc/os-release; then
    report_fail "os-release PRETTY_NAME" "base/airootfs/etc/os-release is missing PRETTY_NAME=\"FinnOS\""
fi
if ! grep -q '^BUG_REPORT_URL="https://github.com/Finn-Technologies/FinnOS/issues"$' base/airootfs/etc/os-release; then
    report_fail "os-release BUG_REPORT_URL" "base/airootfs/etc/os-release BUG_REPORT_URL does not match Finn-Technologies/FinnOS"
fi

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi

echo "branding: clean (boot/identity, apps/src/sdk, README/docs, os-release)"
