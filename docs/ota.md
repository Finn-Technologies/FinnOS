# FinnOS Over-the-Air (OTA) Updates

This document specifies the FinnOS Phase 5 OTA architecture, manifest format,
cryptographic verification, boot-counting rollback, and `finn-update` client
interface.

## 1. Storage Layout Decision: A/B Root Slots vs. Btrfs Snapshots

FinnOS uses **A/B root partitions (`FINNOS_A` and `FINNOS_B`) paired with a
persistent data partition (`FINNOS_DATA`)**, rather than Btrfs subvolume
snapshots.

### Rationale from `vm/finn-install.sh`

- `vm/finn-install.sh` partitions disks with GPT (`512M` FAT32 EFI System
  Partition at `/boot`, `1M` BIOS boot partition, and `ext4` root) and
  `base/packages.*` does not include `btrfs-progs`. Using `ext4` A/B slots
  requires no new kernel filesystem drivers or initramfs hooks.
- **Fault isolation:** Writing a verified slot image to the inactive partition
  does not share filesystem metadata, B-trees, or superblocks with the running
  root. A power loss mid-write leaves the active slot untouched.
- **Kernel/root pairing:** Bootloader entries (`finnos-a` / `finnos-b`) bind
  each slot to its matching kernel and initramfs on the ESP (`/boot/finnos/a/`
  and `/boot/finnos/b/`).

### Partition Layout (A/B vs. Legacy Single-Slot)

| Partition | GPT Label | Filesystem | Mount / Role |
|---|---|---|---|
| `p1` | `FINNOS_EFI` | `vfat` (FAT32) | `/boot` (recommend `1024M` for dual kernel+initramfs pairs) |
| `p2` | BIOS boot | raw (`1M`) | GRUB `i386-pc` boot code on GPT |
| `p3` | `FINNOS_A` | `ext4` | Root slot A (immutable system image) |
| `p4` | `FINNOS_B` | `ext4` | Root slot B (immutable system image) |
| `p5` | `FINNOS_DATA` | `ext4` | Persistent `/var`, `/home`, `/etc/finn`, `/etc/machine-id` |

> [!NOTE]
> Installations created by the v1 `vm/finn-install.sh` single-root layout have
> only one `ext4` partition (`label=finnos`) and no `FINNOS_A` / `FINNOS_B`
> partition labels. `finn-update` detects this honestly as `"layout": "single"`
> and refuses A/B slot flashing (`exit 4`, `policy_refused`) rather than
> modifying a live root filesystem in place.

---

## 2. Boot Counting & Automatic Rollback

Updates always write to the **inactive** slot, arm that slot for a bounded
number of trial boots (`default: 3`), and promote it to active **only** after
the desktop session passes health verification.

### Primary UEFI Path: `systemd-boot` Boot Assessment

`SystemdBootBackend` (`src/ota/finn_ota/backend.py`) uses systemd's Boot
Loader Specification (BLS) `+tries` filename protocol:

1. Normal committed entries live at `/boot/loader/entries/finnos-a.conf` and
   `finnos-b.conf`.
2. `finn-update apply` writes or renames the inactive slot's entry to
   `finnos-<slot>+3.conf` and runs `bootctl set-oneshot finnos-<slot>+3.conf`.
3. On each boot attempt, `systemd-boot` decrements the counter in the filename
   (`+3` → `+2-1` → `+1-2` → `+0-3`).
4. If the booted system passes `finn-boot-health.service`, `finn-update commit`
   renames `finnos-<slot>+<n>-<m>.conf` to `finnos-<slot>.conf` and runs
   `bootctl set-default finnos-<slot>.conf`.
5. If all 3 trial boots fail before committing, `+0-3` is exhausted and
   `systemd-boot` selects the committed previous slot entry automatically.

### GRUB2 Fallback Path (`grubenv`)

Machines installed with GRUB (`vm/finn-chroot.sh` default for BIOS + EFI) use
`GrubBootBackend` and `/boot/grub/grubenv` (`ota/grub-ab.cfg`):

- `finn_active_slot`: last committed known-good slot (`a` or `b`)
- `finn_pending_slot`: trial slot armed by `finn-update apply` (`""`, `a`, or `b`)
- `finn_tries_left`: remaining trial boots (`0..3`)
- `finn_version_a`, `finn_version_b`: installed version per slot

At boot, GRUB decrements `finn_tries_left` (`3 -> 2 -> 1 -> 0`) and boots
`finn_pending_slot` with `finn.slot=<slot>`. If `finn_tries_left` reaches `0`
while `finn_pending_slot` is still non-empty, GRUB clears `finn_pending_slot`,
saves `grubenv`, and boots `finn_active_slot`.

**Honest GRUB limit:** `grub-editenv` from userspace updates `grubenv` via
atomic write + `fsync` + `rename`, whereas GRUB's pre-boot `save_env` writes
directly to the 1024-byte `grubenv` block on FAT32/ext4 without a journaled
copy-on-write log. Power loss during the brief pre-boot `save_env` sector
write is mitigated by keeping `grubenv` preallocated and 1024-byte aligned.

---

## 3. Signed Manifests & Verified-Boot Honest Limits

### Signature Format

- Algorithm: **Ed25519** (RFC 8032).
- Key format: standard OpenSSL PKCS#8 private PEM (`BEGIN PRIVATE KEY`) and
  SPKI public PEM (`BEGIN PUBLIC KEY`), verified in `finn-update` by a
  zero-dependency pure-Python RFC 8032 verifier (`src/ota/finn_ota/ed25519.py`)
  that interoperates bit-for-bit with `openssl pkeyutl -rawin`.
- Trusted public keys are loaded from `/etc/finn/ota/pubkey.pem` (or all
  `*.pem` / `*.pub` files if `/etc/finn/ota/` is a directory, supporting key
  rotation).
- Canonical signing input: the manifest JSON object **excluding** `"signature"`,
  serialized with `sort_keys=True`, `separators=(",", ":")`, `ensure_ascii=False`,
  UTF-8 encoded.

### Verified-Boot Chain & Honest Limits

`finn-update` guarantees **OTA payload authenticity and integrity before slot
flashing**:

1. Manifest JSON signature is verified against pinned Ed25519 public keys.
2. Downloaded archive size and SHA-256 must match the signed manifest.
3. Decompressed / delta-patched slot image size and `uncompressed_sha256` must
   match the signed manifest before writing to the inactive slot.

**What is NOT implemented yet (honest limits):**

- **UEFI Secure Boot:** FinnOS does not yet enroll custom KEK/DB keys or sign
  `systemd-boot` / GRUB / UKI kernels for hardware-enforced Secure Boot.
- **`dm-verity` & TPM2 LUKS:** Root slot partitions (`ext4`) are not yet wrapped
  in `dm-verity` Merkle trees or bound to TPM2 PCRs. An attacker with offline
  block-device access can tamper with `/boot` or a partition after installation.

---

## 4. Channels, Staged Rollouts & Delta Updates

### Channels

- `stable`: default production releases.
- `beta`: release candidates for opt-in testing.
- `dev`: continuous integration / nightly builds.

### Deterministic Staged Rollouts

Each manifest carries `"rollout": 0..100`. Devices compute a deterministic
bucket in `0..99`:

```text
bucket = int.from_bytes(sha256(f"{machine_id}:{channel}:{version}")[:4], "big") % 100
eligible = (rollout == 100) or (rollout > 0 and bucket < rollout)
```

- `rollout = 0` halts distribution immediately (`deferred_rollout` / `rollout_paused`).
- Including `channel` and `version` prevents a machine from permanently landing
  in the last 1% across every release, while keeping eligibility deterministic
  for any single release.
- `min_version` enforces stepping-stone upgrades: if `current_version < min_version`,
  `finn-update` blocks direct installation (`blocked_min_version`) and requires
  installing an intermediate release first.

### Delta Updates (`zstd --patch-from`)

`ota/publish` generates a `zstd --patch-from=<prev_image>` delta artifact
alongside the compressed full `.img.zst`. When `current_version == delta_from`
and the active slot device is readable, `finn-update` downloads and applies the
delta (`src/ota/finn_ota/delta.py`). If delta patching fails for any reason
(including base mismatch or `uncompressed_sha256` mismatch), `finn-update apply`
automatically falls back to downloading and verifying the `full` artifact.

### Resumable HTTP Range Downloads

`src/ota/finn_ota/download.py` writes to `<artifact>.part` and sends
`Range: bytes=<offset>-` on resume:

- `206 Partial Content` with matching `Content-Range`: appends to `.part`.
- `200 OK` (range ignored) or `416`: restarts cleanly from byte `0`.
- Only after exact `size` and `sha256` match is `.part` atomically renamed.

---

## 5. Manifest JSON Specification

```json
{
  "schema_version": 1,
  "version": "0.3.0",
  "channel": "stable",
  "arch": "x86_64",
  "published_at": "2026-11-18T19:00:00Z",
  "min_version": "0.1.0",
  "rollout": 25,
  "release_notes": "Fixes and security updates.",
  "artifacts": [
    {
      "kind": "full",
      "compression": "zstd",
      "url": "https://ota.finnos.org/stable/x86_64/0.3.0/finnos-0.3.0-x86_64.img.zst",
      "sha256": "<64-hex>",
      "size": 524288000,
      "uncompressed_sha256": "<64-hex>",
      "uncompressed_size": 4294967296,
      "delta_from": null
    },
    {
      "kind": "delta",
      "compression": "zstd",
      "url": "https://ota.finnos.org/stable/x86_64/0.3.0/finnos-0.2.0-to-0.3.0-x86_64.zstd-delta",
      "sha256": "<64-hex>",
      "size": 18432000,
      "uncompressed_sha256": "<64-hex>",
      "uncompressed_size": 4294967296,
      "delta_from": "0.2.0"
    }
  ],
  "signature": {
    "alg": "ed25519",
    "key_id": "<16-hex-sha256-of-pubkey>",
    "sig": "<base64-64-byte-ed25519-signature>"
  }
}
```

---

## 6. `finn-update` CLI & JSON Interface

Binary: `src/ota/finn-update` (installed target `/usr/local/bin/finn-update`).

| Subcommand | Role |
|---|---|
| `finn-update status --json` | Read-only status: version, channel, layout (`ab`\|`single`), active/booted/pending slots, tries, cached `available_update`. |
| `finn-update check --json` | Fetch & verify manifest, evaluate `min_version` and rollout bucket, update `/var/lib/finn/ota/status_cache.json`. |
| `finn-update download --json` | Verify manifest + resumable HTTP Range download + SHA-256 check. |
| `finn-update apply --json` | Check + download + decompress/delta-patch + flash inactive slot + arm 3 boot tries. |
| `finn-update commit --json` | Mark currently booted slot as committed active slot. |
| `finn-update rollback --json` | Revert boot selection to previous known-good slot. |
| `finn-update health-check [--wait 60] [--commit] --json` | Verify `finn-comp.service`, `finn-session.service`, `/run/finn/wayland-0`, and `/usr/share/finn/version`; commit only if healthy. |
| `finn-update config [--channel stable\|beta\|dev] [--auto-check true\|false] --json` | Read or update `/etc/finn/ota.conf`. |

### Exit Codes

- `0`: success
- `1`: operational / backend / health-check failure
- `2`: cryptographic verification failure (`verification_failed`)
- `3`: network, download, SHA-256, or manifest schema error
- `4`: policy refusal (`deferred_rollout`, `min_version`, or `single`-slot layout)

---

## 7. Systemd Units

- `base/finn-update-check.timer`: runs 5 minutes after boot and every 6 hours
  (with 15m jitter, `Persistent=true`).
- `base/finn-update-check.service`: `Type=oneshot`, guarded by
  `ConditionPathExists=/etc/finn/ota/pubkey.pem`, runs
  `finn-update check --quiet --json`.
- `base/finn-boot-health.service`: runs `After=finn-comp.service finn-session.service`
  in `graphical.target` and executes
  `finn-update health-check --wait 60 --commit --json`. If the compositor fails
  and enters `finn-failure.service`, the slot is **not** committed and the
  bootloader rolls back automatically after boot tries exhaust.

---

## 8. Verification Status

- **Host-tested (`tests/test_ota.py`, macOS):** 14/14 tests pass — Ed25519
  signing/verification and OpenSSL interop, keygen refusal inside git repo,
  `ota/publish` full+delta builds, HTTP `206` Range resume and `200` fallback,
  tampered artifact and bad signature rejection, staged-rollout bucketing and
  `min_version` checks, 3-boot failure exhaustion + automatic rollback via
  `FakeBootBackend`, and `SystemdBootBackend`/`GrubBootBackend` logic against
  temporary `/boot` trees.
- **Lima VM `finnos` (`aarch64` Linux):**
  - `python3 -m unittest -v tests/test_ota.py`: 14/14 pass on Linux `aarch64`
    (including Linux OpenSSL Ed25519 interop and `zstd --patch-from`).
  - `finn-update status --json`: honestly reports `layout: "single"` on the
    VM's non-A/B disk (`vda2`), real `/etc/machine-id` bucket, and `0.2.0`.
  - `finn-update health-check --json`: verified live against active
    `finn-comp.service`, `finn-session.service`, `/run/finn/wayland-0`, and
    `/usr/share/finn/version` (`healthy: true`).
  - `systemd-analyze verify`: syntax-checked `base/finn-update-check.service`,
    `base/finn-update-check.timer`, and `base/finn-boot-health.service`.
- **Real A/B disk boot & rollback:** Unproven on a real reboot — the Lima VM
  and `vm/finn-install.sh` currently use a single root partition rather than
  `FINNOS_A`/`FINNOS_B` partitions.
