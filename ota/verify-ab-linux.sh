#!/bin/bash
# End-to-end Linux verification of FinnOS Phase 5 A/B OTA pipeline.
#
# Exercises real Linux tools inside the Lima VM (or any Linux host):
#   - ext4 loopback partitions / images (FINNOS_A, FINNOS_B, FINNOS_DATA)
#   - ota/keygen + Linux openssl Ed25519 verification
#   - ota/publish with zstd --patch-from delta generation
#   - HTTP Range download + SHA-256 + Ed25519 signature verification
#   - Scenario A: 0.2.0 -> 0.3.0 delta apply, boot into slot B, health-check commit
#   - Scenario B: 0.4.0-broken apply to slot A, 3 failed boots, automatic rollback to B
#   - Both SystemdBootBackend (+tries BLS entries) and GrubBootBackend (grubenv)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|aarch64) ;;
    *) echo "error: unsupported arch $ARCH (expected Linux x86_64 or aarch64)" >&2; exit 1 ;;
esac
[ "$(uname -s)" = "Linux" ] || { echo "error: ota/verify-ab-linux.sh must run on Linux" >&2; exit 1; }

for tool in python3 zstd openssl mkfs.ext4 e2fsck debugfs; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: missing required Linux tool: $tool" >&2
        exit 1
    }
done

WORK="$(mktemp -d /tmp/finn-ota-verify.XXXXXX)"
LOOP_A=""
LOOP_B=""
HTTP_PID=""
SUDO=""
if [ "$(id -u)" -ne 0 ] && sudo -n true 2>/dev/null; then
    SUDO="sudo"
fi

cleanup() {
    if [ -n "$HTTP_PID" ]; then
        kill "$HTTP_PID" 2>/dev/null || true
        wait "$HTTP_PID" 2>/dev/null || true
    fi
    if [ -n "$LOOP_A" ]; then
        $SUDO losetup -d "$LOOP_A" 2>/dev/null || true
    fi
    if [ -n "$LOOP_B" ]; then
        $SUDO losetup -d "$LOOP_B" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "==> workspace: $WORK (arch=$ARCH)"

# 1. Build three real ext4 slot filesystem images:
#    - 0.2.0 (initial slot A)
#    - 0.3.0 (good delta update)
#    - 0.4.0-broken (empty /usr/share/finn/version -> fails health-check)
build_rootfs_ext4() {
    local version="$1"
    local out_img="$2"
    local label="$3"
    local broken="${4:-0}"
    local dir="$WORK/rootfs-$version"
    mkdir -p "$dir/usr/share/finn" "$dir/usr/lib"
    if [ "$broken" = "1" ]; then
        : > "$dir/usr/share/finn/version"
    else
        printf '%s\n' "$version" > "$dir/usr/share/finn/version"
    fi
    # Shared deterministic 1 MiB base blob + version-specific tail so
    # zstd --patch-from creates a compact binary delta.
    python3 -c '
import hashlib, sys
ver, path = sys.argv[1], sys.argv[2]
base = b"".join(hashlib.sha256(f"finn-base-{i}".encode()).digest() for i in range(32768))
tail = (f"FINNOS_SLOT_PAYLOAD_{ver}\n".encode() * 2048)[:65536]
with open(path, "wb") as f:
    f.write(base + tail)
' "$version" "$dir/usr/lib/finn-core.bin"

    truncate -s 16M "$out_img"
    mkfs.ext4 -q -F -L "$label" -d "$dir" "$out_img"
    e2fsck -fn "$out_img" >/dev/null
}

IMG_020="$WORK/finnos-0.2.0.ext4.img"
IMG_030="$WORK/finnos-0.3.0.ext4.img"
IMG_040_BROKEN="$WORK/finnos-0.4.0-broken.ext4.img"
SLOT_A_FILE="$WORK/slot_a.ext4.img"
SLOT_B_FILE="$WORK/slot_b.ext4.img"
DATA_FILE="$WORK/data.ext4.img"

build_rootfs_ext4 "0.2.0" "$IMG_020" "FINNOS_A" 0
build_rootfs_ext4 "0.3.0" "$IMG_030" "FINNOS_B" 0
build_rootfs_ext4 "0.4.0" "$IMG_040_BROKEN" "FINNOS_A" 1

cp "$IMG_020" "$SLOT_A_FILE"
truncate -s 16M "$SLOT_B_FILE"
mkfs.ext4 -q -F -L FINNOS_B "$SLOT_B_FILE"
truncate -s 8M "$DATA_FILE"
mkfs.ext4 -q -F -L FINNOS_DATA "$DATA_FILE"

SLOT_A_DEV="$SLOT_A_FILE"
SLOT_B_DEV="$SLOT_B_FILE"
HAS_LOSETUP=0
if command -v losetup >/dev/null 2>&1 && { [ "$(id -u)" -eq 0 ] || [ -n "$SUDO" ]; }; then
    LOOP_A="$($SUDO losetup --find --show "$SLOT_A_FILE")"
    LOOP_B="$($SUDO losetup --find --show "$SLOT_B_FILE")"
    if [ -n "$SUDO" ]; then
        $SUDO chmod 666 "$LOOP_A" "$LOOP_B"
    fi
    HAS_LOSETUP=1
    echo "==> loop block devices attached: A=$LOOP_A B=$LOOP_B"
fi

# 2. Generate Ed25519 keys outside git tree & verify Linux OpenSSL interop.
mkdir -p "$WORK/keys" "$WORK/repo"
KEY_OUT="$(python3 "$ROOT/ota/keygen" --out-dir "$WORK/keys" --name ota)"
echo "$KEY_OUT"
PRIV_KEY="$WORK/keys/ota-private.pem"
PUB_KEY="$WORK/keys/ota-public.pem"

printf 'finnos-ota-openssl-interop-check\n' > "$WORK/interop.bin"
openssl pkeyutl -sign -inkey "$PRIV_KEY" -rawin -in "$WORK/interop.bin" -out "$WORK/interop.sig"
openssl pkeyutl -verify -pubin -inkey "$PUB_KEY" -rawin -in "$WORK/interop.bin" -sigfile "$WORK/interop.sig" >/dev/null
echo "==> OpenSSL Ed25519 sign/verify interop: PASS"

# 3. Start local HTTP server and publish 0.3.0 (with delta from 0.2.0).
PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"
BASE_URL="http://127.0.0.1:$PORT"
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$WORK/repo" >/dev/null 2>&1 &
HTTP_PID=$!
sleep 0.4

echo "==> publishing 0.3.0 (with zstd --patch-from 0.2.0)"
python3 "$ROOT/ota/publish" \
    --version 0.3.0 \
    --channel stable \
    --arch "$ARCH" \
    --image "$IMG_030" \
    --prev-image "$IMG_020" \
    --prev-version 0.2.0 \
    --key "$PRIV_KEY" \
    --base-url "$BASE_URL" \
    --out-dir "$WORK/repo" \
    --rollout 100 \
    --notes "Good 0.3.0 delta release"

# 4. Run Scenario A & Scenario B across SystemdBootBackend and GrubBootBackend.
export FINN_OTA_ROOT="$ROOT"
export FINN_VERIFY_WORK="$WORK"
export FINN_VERIFY_ARCH="$ARCH"
export FINN_VERIFY_BASE_URL="$BASE_URL"
export FINN_VERIFY_PUBKEY="$PUB_KEY"
export FINN_VERIFY_PRIVKEY="$PRIV_KEY"
export FINN_VERIFY_SLOT_A="$SLOT_A_FILE"
export FINN_VERIFY_SLOT_B="$SLOT_B_FILE"
export FINN_VERIFY_LOOP_A="${LOOP_A:-}"
export FINN_VERIFY_LOOP_B="${LOOP_B:-}"
export FINN_VERIFY_HAS_LOSETUP="$HAS_LOSETUP"
export FINN_VERIFY_SLOT_A_FILE="$SLOT_A_FILE"
export FINN_VERIFY_SLOT_B_FILE="$SLOT_B_FILE"
export FINN_VERIFY_IMG_020="$IMG_020"
export FINN_VERIFY_IMG_030="$IMG_030"
export FINN_VERIFY_IMG_040="$IMG_040_BROKEN"

python3 - <<'PY'
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

root = os.environ["FINN_OTA_ROOT"]
work = os.environ["FINN_VERIFY_WORK"]
arch = os.environ["FINN_VERIFY_ARCH"]
base_url = os.environ["FINN_VERIFY_BASE_URL"]
pubkey = os.environ["FINN_VERIFY_PUBKEY"]
privkey = os.environ["FINN_VERIFY_PRIVKEY"]
slot_a_dev = os.environ["FINN_VERIFY_SLOT_A"]
slot_b_dev = os.environ["FINN_VERIFY_SLOT_B"]
slot_a_file = os.environ["FINN_VERIFY_SLOT_A_FILE"]
slot_b_file = os.environ["FINN_VERIFY_SLOT_B_FILE"]
img_020 = os.environ["FINN_VERIFY_IMG_020"]
img_030 = os.environ["FINN_VERIFY_IMG_030"]
img_040 = os.environ["FINN_VERIFY_IMG_040"]

sys.path.insert(0, os.path.join(root, "src", "ota"))
from finn_ota import cli  # noqa: E402
from finn_ota.backend import GrubBootBackend, SystemdBootBackend  # noqa: E402
from finn_ota.config import OtaConfig  # noqa: E402
from finn_ota.download import sha256_file  # noqa: E402
from finn_ota.health import run_health_check  # noqa: E402

def extract_version_from_ext4(img_file: str) -> str:
    proc = subprocess.run(
        ["debugfs", "-R", "cat /usr/share/finn/version", img_file],
        capture_output=True,
        text=True,
        check=True,
    )
    return proc.stdout.strip()

def check_ext4_fsck(img_file: str) -> None:
    subprocess.run(["e2fsck", "-fn", img_file], check=True, stdout=subprocess.DEVNULL)

def make_apply_args() -> argparse.Namespace:
    return argparse.Namespace(
        channel="stable",
        manifest_url=None,
        pubkey=pubkey,
        machine_id="finnos-lima-verify-0001",
        force_rollout=True,
        allow_downgrade=False,
        tries=3,
    )

def reset_slot_images() -> None:
    with open(img_020, "rb") as src, open(slot_a_dev, "r+b" if os.path.exists(slot_a_dev) and not os.path.isfile(slot_a_dev) else "wb") as dst:
        shutil.copyfileobj(src, dst)
        dst.flush()
        os.fsync(dst.fileno())
    # Re-create clean empty slot B ext4
    subprocess.run(["mkfs.ext4", "-q", "-F", "-L", "FINNOS_B", slot_b_file], check=True)
    if slot_b_dev != slot_b_file:
        with open(slot_b_file, "rb") as src, open(slot_b_dev, "r+b") as dst:
            shutil.copyfileobj(src, dst)
            dst.flush()
            os.fsync(dst.fileno())

# Determine if live systemd session is available in Lima VM for health-check
use_live_systemd = (
    subprocess.run(["systemctl", "is-active", "--quiet", "finn-comp.service"]).returncode == 0
    and subprocess.run(["systemctl", "is-active", "--quiet", "finn-session.service"]).returncode == 0
    and os.path.exists("/run/finn/wayland-0")
)
print(f"==> health-check systemd probe mode: {'live finn-comp+finn-session+wayland-0' if use_live_systemd else 'skip_systemd (no live session)'}")

# --- Backend 1: SystemdBootBackend ---
reset_slot_images()
sb_root = os.path.join(work, "sb")
sb_boot = os.path.join(sb_root, "boot")
sb_entries = os.path.join(sb_boot, "loader", "entries")
sb_state = os.path.join(sb_root, "state")
os.makedirs(sb_entries, exist_ok=True)
os.makedirs(sb_state, exist_ok=True)
sb_cmdline = os.path.join(sb_root, "cmdline")
sb_ver = os.path.join(sb_root, "version")
sb_mid = os.path.join(sb_root, "machine-id")
with open(sb_cmdline, "w") as f:
    f.write("root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash\n")
with open(sb_ver, "w") as f:
    f.write("0.2.0\n")
with open(sb_mid, "w") as f:
    f.write("finnos-lima-verify-0001\n")
with open(os.path.join(sb_entries, "finnos-a.conf"), "w") as f:
    f.write("title FinnOS (A)\n")

bootctl_shim = os.path.join(sb_root, "bootctl")
bootctl_log = os.path.join(sb_root, "bootctl.log")
with open(bootctl_shim, "w") as f:
    f.write(f'#!/bin/sh\necho "$@" >> "{bootctl_log}"\nexit 0\n')
os.chmod(bootctl_shim, 0o755)

sb = SystemdBootBackend(
    boot_dir=sb_boot,
    cmdline_path=sb_cmdline,
    version_path=sb_ver,
    slot_a=slot_a_dev,
    slot_b=slot_b_dev,
    bootctl_bin=bootctl_shim,
    state_dir=sb_state,
)
cfg_sb = OtaConfig(
    channel="stable",
    base_url=base_url,
    arch=arch,
    pubkey_path=pubkey,
    state_dir=sb_state,
    version_path=sb_ver,
    machine_id_path=sb_mid,
    backend="systemd-boot",
    boot_tries=3,
)

# Scenario A (systemd-boot): 0.2.0 -> 0.3.0 delta apply + boot + commit
res_a = cli._do_apply(cfg_sb, sb, make_apply_args())
assert res_a["status"] == "reboot_required"
assert res_a["staged_slot"] == "b"
assert res_a["used_delta"] is True
assert not res_a["delta_fallback_to_full"]
assert sha256_file(slot_b_dev) == sha256_file(img_030), "slot B mismatch after delta apply"
check_ext4_fsck(slot_b_file)
assert extract_version_from_ext4(slot_b_file) == "0.3.0"
assert os.path.isfile(os.path.join(sb_entries, "finnos-b+3.conf"))

# Simulate systemd-boot decrementing +3 -> +2-1 and booting slot B
os.replace(os.path.join(sb_entries, "finnos-b+3.conf"), os.path.join(sb_entries, "finnos-b+2-1.conf"))
with open(sb_cmdline, "w") as f:
    f.write("root=PARTLABEL=FINNOS_B rw finn.slot=b quiet splash\n")
with open(sb_ver, "w") as f:
    f.write(extract_version_from_ext4(slot_b_file) + "\n")

hc_a = run_health_check(version_path=sb_ver, skip_systemd=not use_live_systemd)
assert hc_a.healthy, f"expected healthy slot B: {hc_a}"
committed = sb.commit_booted_slot()
assert committed == "b"
st_a = sb.get_status()
assert st_a.active_slot == "b" and st_a.slot_committed is True
assert os.path.isfile(os.path.join(sb_entries, "finnos-b.conf"))
assert not os.path.exists(os.path.join(sb_entries, "finnos-b+2-1.conf"))
print("[PASS] Scenario A (systemd-boot): 0.2.0 -> 0.3.0 delta applied to slot B, ext4 fsck+sha256 verified, health-check committed B")

# Scenario B (systemd-boot): publish 0.4.0-broken (empty version file inside ext4),
# apply to slot A, fail 3 trial boots, verify automatic rollback to slot B (0.3.0).
subprocess.run(
    [
        sys.executable, os.path.join(root, "ota", "publish"),
        "--version", "0.4.0",
        "--channel", "stable",
        "--arch", arch,
        "--image", img_040,
        "--key", privkey,
        "--base-url", base_url,
        "--out-dir", os.path.join(work, "repo"),
        "--rollout", "100",
        "--notes", "Deliberately broken 0.4.0",
    ],
    check=True,
    capture_output=True,
)
res_b = cli._do_apply(cfg_sb, sb, make_apply_args())
assert res_b["staged_slot"] == "a"
assert sha256_file(slot_a_dev) == sha256_file(img_040)
assert extract_version_from_ext4(slot_a_file) == ""
assert os.path.isfile(os.path.join(sb_entries, "finnos-a+3.conf"))

# Simulate 3 failed trial boots on slot A (+3 -> +2-1 -> +1-2 -> +0-3)
transitions = [
    ("finnos-a+3.conf", "finnos-a+2-1.conf", 2),
    ("finnos-a+2-1.conf", "finnos-a+1-2.conf", 1),
    ("finnos-a+1-2.conf", "finnos-a+0-3.conf", 0),
]
for old_e, new_e, expected_tries in transitions:
    os.replace(os.path.join(sb_entries, old_e), os.path.join(sb_entries, new_e))
    with open(sb_cmdline, "w") as f:
        f.write("root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash\n")
    with open(sb_ver, "w") as f:
        f.write(extract_version_from_ext4(slot_a_file))  # empty -> unhealthy
    hc_bad = run_health_check(version_path=sb_ver, skip_systemd=not use_live_systemd)
    assert not hc_bad.healthy and not hc_bad.checks["version_file"]
    st_mid = sb.get_status()
    assert st_mid.tries_left == expected_tries
    assert st_mid.active_slot == "b"

# After 0 tries remain, systemd-boot automatically falls back to finnos-b.conf
with open(sb_cmdline, "w") as f:
    f.write("root=PARTLABEL=FINNOS_B rw finn.slot=b quiet splash\n")
with open(sb_ver, "w") as f:
    f.write(extract_version_from_ext4(slot_b_file) + "\n")
st_rb = sb.get_status()
assert st_rb.booted_slot == "b"
assert st_rb.active_slot == "b"
assert st_rb.slot_versions["b"] == "0.3.0"
assert extract_version_from_ext4(slot_b_file) == "0.3.0"
print("[PASS] Scenario B (systemd-boot): 0.4.0 broken image failed 3 health checks, auto-rolled back to slot B (0.3.0)")

# --- Backend 2: GrubBootBackend ---
# Republish 0.3.0 as latest so we can run full Scenario A + Scenario B on GrubBootBackend too
subprocess.run(
    [
        sys.executable, os.path.join(root, "ota", "publish"),
        "--version", "0.3.0",
        "--channel", "stable",
        "--arch", arch,
        "--image", img_030,
        "--prev-image", img_020,
        "--prev-version", "0.2.0",
        "--key", privkey,
        "--base-url", base_url,
        "--out-dir", os.path.join(work, "repo"),
        "--rollout", "100",
        "--notes", "Good 0.3.0 delta release (GRUB check)",
    ],
    check=True,
    capture_output=True,
)

reset_slot_images()
gb_root = os.path.join(work, "gb")
gb_boot = os.path.join(gb_root, "boot")
gb_state = os.path.join(gb_root, "state")
os.makedirs(os.path.join(gb_boot, "grub"), exist_ok=True)
os.makedirs(gb_state, exist_ok=True)
gb_cmdline = os.path.join(gb_root, "cmdline")
gb_ver = os.path.join(gb_root, "version")
gb_mid = os.path.join(gb_root, "machine-id")
with open(gb_cmdline, "w") as f:
    f.write("root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash\n")
with open(gb_ver, "w") as f:
    f.write("0.2.0\n")
with open(gb_mid, "w") as f:
    f.write("finnos-lima-verify-0001\n")

gb = GrubBootBackend(
    boot_dir=gb_boot,
    cmdline_path=gb_cmdline,
    version_path=gb_ver,
    slot_a=slot_a_dev,
    slot_b=slot_b_dev,
)
gb._write_grubenv_vars({"finn_active_slot": "a", "finn_version_a": "0.2.0", "finn_tries_left": "0"})
cfg_gb = OtaConfig(
    channel="stable",
    base_url=base_url,
    arch=arch,
    pubkey_path=pubkey,
    state_dir=gb_state,
    version_path=gb_ver,
    machine_id_path=gb_mid,
    backend="grub",
    boot_tries=3,
)

# Scenario A (GRUB)
gres_a = cli._do_apply(cfg_gb, gb, make_apply_args())
assert gres_a["staged_slot"] == "b" and gres_a["used_delta"] is True
assert sha256_file(slot_b_dev) == sha256_file(img_030)
env = gb._read_grubenv()
assert env["finn_pending_slot"] == "b" and env["finn_tries_left"] == "3"

# Simulate GRUB pre-boot decrement 3 -> 2 and boot slot B
gb._write_grubenv_vars({"finn_tries_left": "2"})
with open(gb_cmdline, "w") as f:
    f.write("root=PARTLABEL=FINNOS_B rw finn.slot=b quiet splash\n")
with open(gb_ver, "w") as f:
    f.write(extract_version_from_ext4(slot_b_file) + "\n")
ghc_a = run_health_check(version_path=gb_ver, skip_systemd=not use_live_systemd)
assert ghc_a.healthy
assert gb.commit_booted_slot() == "b"
env = gb._read_grubenv()
assert env["finn_active_slot"] == "b" and env.get("finn_pending_slot", "") == "" and env["finn_tries_left"] == "0"
print("[PASS] Scenario A (grub): 0.2.0 -> 0.3.0 delta applied to slot B, grubenv updated, health-check committed B")

# Scenario B (GRUB): apply broken 0.4.0 to slot A, exhaust 3 tries, auto-rollback to B (0.3.0)
subprocess.run(
    [
        sys.executable, os.path.join(root, "ota", "publish"),
        "--version", "0.4.0",
        "--channel", "stable",
        "--arch", arch,
        "--image", img_040,
        "--key", privkey,
        "--base-url", base_url,
        "--out-dir", os.path.join(work, "repo"),
        "--rollout", "100",
        "--notes", "Deliberately broken 0.4.0",
    ],
    check=True,
    capture_output=True,
)
gres_b = cli._do_apply(cfg_gb, gb, make_apply_args())
assert gres_b["staged_slot"] == "a"
for left in ("2", "1", "0"):
    gb._write_grubenv_vars({"finn_tries_left": left})
    with open(gb_cmdline, "w") as f:
        f.write("root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash\n")
    with open(gb_ver, "w") as f:
        f.write(extract_version_from_ext4(slot_a_file))
    ghc_bad = run_health_check(version_path=gb_ver, skip_systemd=not use_live_systemd)
    assert not ghc_bad.healthy

# 4th boot: grub-ab.cfg sees finn_tries_left == 0 with pending_slot non-empty -> clears pending and boots active_slot (b)
gb._write_grubenv_vars({"finn_pending_slot": "", "finn_tries_left": "0"})
with open(gb_cmdline, "w") as f:
    f.write("root=PARTLABEL=FINNOS_B rw finn.slot=b quiet splash\n")
with open(gb_ver, "w") as f:
    f.write(extract_version_from_ext4(slot_b_file) + "\n")
gst_rb = gb.get_status()
assert gst_rb.booted_slot == "b" and gst_rb.active_slot == "b" and gst_rb.slot_versions["b"] == "0.3.0"
print("[PASS] Scenario B (grub): 0.4.0 broken image failed 3 health checks, auto-rolled back to slot B (0.3.0)")

# --- Stage 2: Real Linux block-device (losetup /dev/loopX) verification ---
if os.environ.get("FINN_VERIFY_HAS_LOSETUP") == "1":
    loop_a = os.environ["FINN_VERIFY_LOOP_A"]
    loop_b = os.environ["FINN_VERIFY_LOOP_B"]
    subprocess.run(
        [
            sys.executable, os.path.join(root, "ota", "publish"),
            "--version", "0.3.0",
            "--channel", "stable",
            "--arch", arch,
            "--image", img_030,
            "--prev-image", img_020,
            "--prev-version", "0.2.0",
            "--key", privkey,
            "--base-url", base_url,
            "--out-dir", os.path.join(work, "repo"),
            "--rollout", "100",
            "--notes", "Good 0.3.0 release (loop blockdev check)",
        ],
        check=True,
        capture_output=True,
    )
    with open(img_020, "rb") as src, open(loop_a, "r+b") as dst:
        shutil.copyfileobj(src, dst)
        dst.flush()
        os.fsync(dst.fileno())
    subprocess.run(["mkfs.ext4", "-q", "-F", "-L", "FINNOS_B", slot_b_file], check=True)

    blk_root = os.path.join(work, "blk")
    blk_boot = os.path.join(blk_root, "boot")
    blk_entries = os.path.join(blk_boot, "loader", "entries")
    blk_state = os.path.join(blk_root, "state")
    os.makedirs(blk_entries, exist_ok=True)
    os.makedirs(blk_state, exist_ok=True)
    blk_cmdline = os.path.join(blk_root, "cmdline")
    blk_ver = os.path.join(blk_root, "version")
    blk_mid = os.path.join(blk_root, "machine-id")
    with open(blk_cmdline, "w") as f:
        f.write("root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash\n")
    with open(blk_ver, "w") as f:
        f.write("0.2.0\n")
    with open(blk_mid, "w") as f:
        f.write("finnos-lima-verify-0001\n")
    with open(os.path.join(blk_entries, "finnos-a.conf"), "w") as f:
        f.write("title FinnOS (A)\n")

    sb_blk = SystemdBootBackend(
        boot_dir=blk_boot,
        cmdline_path=blk_cmdline,
        version_path=blk_ver,
        slot_a=loop_a,
        slot_b=loop_b,
        bootctl_bin=bootctl_shim,
        state_dir=blk_state,
    )
    cfg_blk = OtaConfig(
        channel="stable",
        base_url=base_url,
        arch=arch,
        pubkey_path=pubkey,
        state_dir=blk_state,
        version_path=blk_ver,
        machine_id_path=blk_mid,
        backend="systemd-boot",
        boot_tries=3,
    )
    bres = cli._do_apply(cfg_blk, sb_blk, make_apply_args())
    assert bres["staged_slot"] == "b"
    # Note: on Linux block devices os.path.getsize(dev)==0 and `zstd --patch-from`
    # requires a regular file, so cli._can_delta() falls back to full artifact.
    assert bres["used_delta"] is False
    os.sync()
    assert sha256_file(loop_b) == sha256_file(img_030)
    check_ext4_fsck(loop_b)
    assert extract_version_from_ext4(loop_b) == "0.3.0"
    print(f"[PASS] Block-device streaming ({loop_a} -> {loop_b}): 0.3.0 streamed to loop block device, e2fsck+sha256 verified (used_delta=False fallback)")

print("ALL LINUX A/B OTA VERIFICATION CHECKS PASSED")
PY
