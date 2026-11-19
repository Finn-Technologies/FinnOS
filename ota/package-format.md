# FinnOS OTA package format (implementable spec)

Status legend used throughout:

- **SHIPPED** — the behaviour exists in this tree; file/line references are to
  the code that implements it.
- **SPEC ONLY** — a precise, implementable design that is **not** in the tree.
  Nothing in this document may be read as claiming these features work today.

This page is the normative description of the FinnOS OTA **package** (signed
manifest + artifact bytes + A/B slot contract). The user-facing overview is
[`docs/ota.md`](../docs/ota.md), the publishing workflow is
[`docs/ota-publishing.md`](../docs/ota-publishing.md), and the executable
reference implementation is `src/ota/finn_ota/` plus `ota/publish`, `ota/keygen`
and the client `src/ota/finn-update`. When this page and the code disagree, the
code is the truth and this page must be fixed in the same change.

---

## 1. What exists today (SHIPPED)

| Concern | Implementation |
|---|---|
| Manifest schema v1, canonical signing bytes, Ed25519 sign/verify, artifact selection | `src/ota/finn_ota/manifest.py` (`SCHEMA_VERSION = 1`, `VALID_ARCHES = ("x86_64", "aarch64")`) |
| Pure-Python RFC 8032 + OpenSSL-compatible PKCS#8/SPKI PEM | `src/ota/finn_ota/ed25519.py` (verified against `openssl pkeyutl -rawin` in `ota/verify-ab-linux.sh:124-127`) |
| Channels `stable|beta|dev`, deterministic staged rollout bucket, `min_version` | `src/ota/finn_ota/rollout.py`, `src/ota/finn_ota/cli.py:158-276` |
| Resumable HTTP Range download, SHA-256/size gate | `src/ota/finn_ota/download.py` |
| `zstd -19 --patch-from` deltas, decompression, fallback to full | `ota/publish:126-157`, `src/ota/finn_ota/delta.py`, `src/ota/finn_ota/cli.py:327-380` |
| A/B slots, systemd-boot BLS `+N` boot counting, GRUB `grubenv` fallback, manual rollback | `src/ota/finn_ota/backend.py`, `ota/grub-ab.cfg`, `docs/ota.md:45-88` |
| Boot health gate before slot commit | `src/ota/finn_ota/health.py` (version file, `finn-comp.service`, `finn-session.service`, `/run/finn/wayland-0`) |
| Publisher, keygen, full end-to-end Linux verifier | `ota/publish`, `ota/keygen`, `ota/verify-ab-linux.sh` |
| Release automation | `.github/workflows/ota-release.yml` (workflow_dispatch, `--arch x86_64|aarch64`) |

The end-to-end verifier has been executed on the Lima `aarch64` dev VM with real
`ext4` images, `losetup` block devices, live OpenSSL Ed25519 interop and both
boot backends; the observed output is recorded in `docs/ota.md:276-308`.

---

## 2. Package layout

A published release is a directory tree plus one signed JSON document. No
single "package file" wraps a system update; the manifest is the trust root and
the artifacts are ordinary HTTPS files with `Accept-Ranges: bytes`.

```
<channel>/<arch>/<version>/
  manifest.json
  finnos-<version>-<arch>.img.zst                       (full artifact)
  finnos-<prev>-to-<version>-<arch>.zstd-delta          (0..n delta artifacts)
<channel>/<arch>/latest.json                            (rolling pointer; same signed document)
```

GitHub Releases mode writes `manifest-<arch>.json` into tag `v<version>` and
`latest-<arch>.json` into the `ota-<channel>` tag
(`ota/publish:104-111`, `:181-187`). The manifest URL is derived by
`OtaConfig.effective_manifest_url()` (`src/ota/finn_ota/config.py:65-72`).

### 2.1 Signed manifest (SHIPPED)

`manifest.json` is a JSON object. `ota/publish:165-176` builds it and
`src/ota/finn_ota/manifest.py:46-116` validates it.

| Field | Type | Meaning |
|---|---|---|
| `schema_version` | int | Must be `1`. |
| `version` | SemVer string | Target version, e.g. `0.3.0`. |
| `channel` | `stable|beta|dev` | Must equal the requested channel. |
| `arch` | `x86_64|aarch64` | Optional in schema, but publisher always emits it. |
| `published_at` | RFC 3339 UTC string | Wall-clock publish time (see §5 — not reproducible). |
| `min_version` | SemVer string | Clients below this are blocked (`blocked_min_version`). |
| `rollout` | int `0..100` | Deterministic staged rollout percentage. |
| `release_notes` | string | Free text. |
| `artifacts` | non-empty array | At least one `delta_from: null` (full) artifact. |
| `signature` | object | `{alg: "ed25519", key_id: <16 hex>, sig: <base64 64-byte>}`. |

Each artifact carries:

| Field | Meaning |
|---|---|
| `kind` | `full` or `delta` (advisory; selection uses `delta_from`). |
| `compression` | `zstd` (both artifact kinds). |
| `url` | HTTPS URL (or local path for tests). |
| `sha256`, `size` | SHA-256 and byte length of the **compressed** artifact. |
| `uncompressed_sha256`, `uncompressed_size` | SHA-256 and byte length of the **raw slot image** after decompression/delta patch. |
| `delta_from` | `null` for full; source SemVer for a delta. |

**Canonical signing input (SHIPPED):** the manifest object with the `signature`
field removed, serialized as `json.dumps(payload, sort_keys=True,
separators=(",", ":"), ensure_ascii=False).encode("utf-8")`
(`manifest.py:28-33`). Signing and verification both use these exact bytes.
`key_id` is `sha256(public_key)[:16]` (`ed25519.py:158-162`). Trusted keys are
read from `/etc/finn/ota/pubkey.pem`, or every `*.pem`/`*.pub` in that directory
for rotation (`manifest.py:169-184`).

---

## 3. A/B slots and flashing (SHIPPED)

The installed layout uses `FINNOS_EFI` (FAT32 ESP), a 1 MiB BIOS boot partition,
`FINNOS_A`, `FINNOS_B` (ext4 root slots) and `FINNOS_DATA` (persistent
`/var`, `/home`, `/etc/finn`, `/etc/machine-id`). A single-root v1 install is
honestly reported as `layout: "single"` and `apply` refuses with exit 4 rather
than writing a live root (`docs/ota.md:26-42`, `cli.py:327-334`).

Apply sequence (`cli.py:327-380`, `delta.py:24-106`):

1. Verify manifest and rollout policy; download artifact to
   `/var/lib/finn/ota/downloads/<version>/`; gate on compressed `size`/`sha256`.
2. Materialize a raw image at `/var/lib/finn/ota/staging/<version>-<slot>.img`.
   Delta: `zstd -d --patch-from=<booted slot device> <delta> -o <staging>`;
   full: `zstd -d`. Gate on `uncompressed_size` and `uncompressed_sha256`.
3. On delta failure, automatically download and verify the full artifact and
   stage that instead (`delta_fallback_to_full: true` in the result).
4. Flash the **inactive** slot, then arm a trial boot with 3 attempts via
   `mark_bootable_once()`.
5. `finn-update health-check --wait 60 --commit` promotes the trial slot only
   after the version file, both units and the Wayland socket are healthy
   (`health.py:47-99`, `base/finn-boot-health.service`).

Boot counting: systemd-boot uses BLS filenames `finnos-<slot>+3.conf` →
`+2-1` → `+1-2` → `+0-3` (`SystemdBootBackend`); GRUB uses `finn_active_slot`,
`finn_pending_slot`, `finn_tries_left` in `/boot/grub/grubenv` with the counter
decrement in `ota/grub-ab.cfg`. When tries are exhausted without a commit, the
previous committed slot boots. `finn-update rollback --reason <text>` is the
explicit manual counterpart (`cli.py:581-584`).

**Honest limit:** GRUB's pre-boot `save_env` writes a fixed 1024-byte block
without a journaled copy-on-write log; power loss during that write is only
mitigated by preallocation/alignment (`docs/ota.md:83-88`).

---

## 4. Delta updates (SHIPPED)

Generated at publish time with `zstd -19 -f --patch-from=<prev raw image>`
(`ota/publish:137-142`). Applicable only when
`current_version == artifact.delta_from` **exactly** (SemVer string equality,
including prerelease; `manifest.py:207-221`) and the booted slot is a readable
regular file. On Linux loop block devices, `os.path.getsize()` returns `0` and
`zstd --patch-from` rejects a non-regular file, so the client falls back to the
full artifact — this is measured, not assumed (`docs/ota.md:298-303`,
`ota/verify-ab-linux.sh:554-560`). Deltas are never applied across `arch`.

---

## 5. Reproducible build ID (SPEC ONLY — not implemented)

**Gap:** `published_at` is wall-clock time and the ext4 slot images are not
created with fixed UUIDs/timestamps, so two builds of the same source do not
produce identical bytes and there is no build identity that can be checked
against source. Do not describe current releases as reproducible.

Implementable design, backward-compatible with schema v1 (the validator does
not forbid extra fields, and all fields except `signature` are signed):

1. Produce the raw slot image deterministically:
   `SOURCE_DATE_EPOCH=<git commit time>` for every file mtime;
   `mkfs.ext4 -U <ARCH-UUID> -E hash_seed=<seed>,root_owner=0:0 -d <root> <img>`
   with per-arch fixed UUIDs. Record the tool versions (`zstd --version`,
   `mkfs.ext4 -V`) in the build provenance.
2. Compute a content-addressed build ID over a sorted manifest of inputs —
   every staged file's relative path, octal mode and SHA-256, plus the digests
   of `base/packages.<arch>`, `ui/_tokens.fui` and the compositor git tree:

   ```text
   inputs = sorted([ (path, mode, sha256(file)), ... ])
   build_id = "sha256:" + hex(sha256(canonical_json({
       "inputs": inputs,
       "arch": arch,
       "source_epoch": SOURCE_DATE_EPOCH,
       "tool_versions": {...}
   })))
   ```

3. Add optional signed fields to the manifest: `build_id`, `source_commit`
   (full git SHA), `source_tree_dirty` (bool), and replace/augment
   `published_at` with `built_at` taken from `SOURCE_DATE_EPOCH`. Keep
   `published_at` for channel ordering. An older client verifies these bytes
   unchanged because they are inside the canonical signing input.
4. Verification command to add: `finn-update verify-build --manifest … --source
   <dir>` recomputes `build_id` from the local tree and refuses a mismatch.
5. Release gate in CI: build twice in two clean containers and assert
   `sha256(finnos-<version>-<arch>.img.zst)` is equal before publishing.

---

## 6. x86_64 and ARM64 artifacts (partly SHIPPED, partly SPEC ONLY)

SHIPPED: `VALID_ARCHES = ("x86_64", "aarch64")`; `detect_arch()` maps
`arm64 → aarch64` (`config.py:35-41`); `ota/publish --arch` accepts both;
`.github/workflows/ota-release.yml` offers both; manifests carry a per-arch
`latest` pointer; `finn-update check` rejects a manifest whose `arch` differs
from the host (`cli.py:187-191`).

Gaps that must be closed before this is a real dual-arch pipeline:

- CI builds and boot-tests **only x86_64** (`.github/workflows/iso.yml:74-88,
  248-254`). The aarch64 path is `base/mkfinniso --check --arch=aarch64` — a
  tree check, not an image build or boot (`iso.yml:43`). No aarch64 slot image
  is published.
- There is no unified multi-arch manifest; one signed document per arch is
  required. A device must never accept an artifact whose `arch` differs from
  `detect_arch()` — keep that check mandatory.
- No cross-arch/signature-agility test exists (release key rotation is
  documented in `docs/ota-publishing.md:28-36` but only exercised on one arch).
- No anti-rollback monotonic counter: downgrade protection is only the signed
  `min_version` field, which a compromised signer could lower. Secure Boot /
  TPM2 sealing is absent (§8).

---

## 7. Verification commands (all runnable today on a Linux host)

```sh
# 1. Key generation OUTSIDE the git tree (refuses in-tree paths, mode 0600).
ota/keygen --out-dir /secure/offline/finnos-keys --name release

# 2. Publish a signed release with a delta (requires zstd, openssl not needed at publish time).
ota/publish --version 0.3.0 --channel stable --arch x86_64 \
  --image /build/finnos-0.3.0-x86_64.img \
  --prev-image /build/finnos-0.2.0-x86_64.img --prev-version 0.2.0 \
  --key /secure/offline/finnos-keys/release-private.pem \
  --base-url https://mirror.example.org --out-dir /srv/ota-staging

# 3. Client-side signed-manifest check (exit 2 = verification failure).
finn-update check --json --manifest-url https://mirror.example.org/stable/x86_64/latest.json \
  --pubkey /etc/finn/ota/pubkey.pem

# 4. Full end-to-end A/B proof: ext4 images, loop devices, OpenSSL Ed25519
#    interop, zstd delta, HTTP Range, systemd-boot AND GRUB, 3-failed-boot
#    rollback. Requires Linux + python3 zstd openssl mkfs.ext4 e2fsck debugfs.
sudo ota/verify-ab-linux.sh

# 5. Offline verification of a downloaded artifact against a signed manifest:
python3 - <<'PY'
import json, sys
sys.path.insert(0, "src/ota")
from finn_ota.manifest import load_public_keys, verify_manifest
m = json.load(open("manifest.json"))
print("verified key:", verify_manifest(m, load_public_keys("/etc/finn/ota/pubkey.pem")))
PY
sha256sum finnos-0.3.0-x86_64.img.zst     # compare with artifact.sha256
zstd -dc finnos-0.3.0-x86_64.img.zst | sha256sum   # compare uncompressed_sha256

# 6. Direct OpenSSL interop check (same canonical bytes, no Python verifier):
python3 -c 'import json;m=json.load(open("manifest.json"));m.pop("signature");import sys;sys.stdout.buffer.write(json.dumps(m,sort_keys=True,separators=(",",":"),ensure_ascii=False).encode())' > payload.bin
python3 -c 'import base64,json;open("sig.bin","wb").write(base64.b64decode(json.load(open("manifest.json"))["signature"]["sig"]))'
openssl pkeyutl -verify -pubin -inkey pubkey.pem -rawin -in payload.bin \
  -sigfile sig.bin >/dev/null
```

Exit codes: `0` ok, `1` operational/health, `2` verification, `3`
network/download/manifest, `4` policy refusal (`cli.py:35-39`).

---

## 8. Real gaps (SPEC ONLY — do not claim these work)

| Gap | Consequence | Needed work |
|---|---|---|
| UEFI Secure Boot, `dm-verity`, TPM2 sealing, UKI signing | An attacker with offline disk access can modify `/boot` or a slot after install; there is no hardware root of trust | Enroll KEK/DB, sign loaders/kernels, hash-tree root slots, seal boot state to PCRs; measured in `docs/audit/remaining-issues.md:74-75` |
| No bare-metal power-cycle A/B proof | Boot-counter decrement is simulated via loopback/backend state, not firmware | Multi-partition device, real UEFI/BIOS reboot, kill-power during write |
| `published_at` non-deterministic, no `build_id` | Releases are not byte-reproducible; cannot prove source→artifact | §5 |
| No rollback telemetry | A bricked trial boot is detected but not reported centrally | Opt-in health report over the existing HTTPS channel, no new daemon (respect `docs/architecture.md:16`) |
| Single-arch CI image pipeline | aarch64 artifacts are a publisher option only | Cross/native ARM CI image build + boot test |
| No signed initramfs/kernel manifest entries | Only the root image is hashed | Extend artifact kinds to `kernel`/`initramfs` with their own digests, still signed by the same manifest |
| Key rotation untested across arch/pipeline | Misconfigured trust store can brick updates | Automate transitional-image test for both arches |
| GRUB pre-boot `save_env` window | Tiny power-loss corruption window | Prefer systemd-boot on UEFI; keep GRUB as documented fallback only |

Nothing in this section may be implemented by weakening the honesty rules: a
missing key, a missing artifact or an unverified slot is reported as missing or
refused, never substituted with a plausible default.
