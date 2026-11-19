# Packaging, Signing & Publishing FinnOS Apps

This page describes the `.finnapp` package format, `finn-app.json` manifest schema, Ed25519 signing workflow, permission declarations, and current distribution paths.

---

## 1. Application Manifest (`finn-app.json`)

Every packaged FinnSDK app declares its metadata in `finn-app.json` conforming to [`sdk/schema/finn-app.schema.json`](../../sdk/schema/finn-app.schema.json).

Required fields:
- `schema_version`: integer `1`
- `id`: lowercase hyphenated slug or reverse-DNS identifier (e.g. `counter`, `com.example.notes`)
- `name`: display name (1–80 chars)
- `version`: SemVer 2.0.0 string (e.g. `0.1.0`)
- `summary`: short description (1–200 chars)
- `executable`: relative path or binary name produced by CMake (e.g. `finn-counter`)

Optional fields:
- `icon`: built-in icon name (resolved under `/usr/share/finn/icons`) or relative path to an SVG/PNG asset
- `desktop_entry`: relative path to a `.desktop` file (e.g. `finn-app.desktop`)
- `category`: `System`, `Developer`, `Productivity`, `Media`, `Utility`, `Network`, `Graphics`, `Education`, or `Games`
- `sdk_min_version`: `MAJOR.MINOR.PATCH` (default `0.2.0`)
- `arch`: array of `x86_64`, `aarch64`
- `form_factors`: array of `desktop`, `tablet`, `phone`
- `permissions`: array of declared capabilities (see below)
- `license`, `author`, `homepage`, `assets`

Validate locally:

```sh
finn-sdk validate .
finn-sdk validate . --strict   # also verifies compiled executable and referenced icon exist
```

---

## 2. Permission Model (Declared-Not-Enforced in v1)

Supported `permissions` values in `finn-app.json`:

- `filesystem:home-read`
- `filesystem:home-write`
- `network:internet`
- `session:launch`
- `audio:playback`
- `audio:record`
- `bluetooth`
- `notifications`

> **Honest v1 boundary:** FinnOS does not yet have `finnd` or per-app OS sandboxing (`docs/security.md`, `docs/design/contract.md`). Native `FinnSDK` apps run with user `finn`'s full authority. Declaring `permissions` in `finn-app.json` documents app intent for users and prepares manifests for future transient systemd/namespace enforcement; it is **not** enforced at runtime today.

---

## 3. Deterministic `.finnapp` Archive Format (`finnapp-v1`)

A `.finnapp` file is a deterministic USTAR `tar` archive containing:

1. `manifest.json` — canonical UTF-8 JSON (`sort_keys=True`, compact separators, trailing newline) containing the validated `finn-app.json` fields plus a `package` object:
   ```json
   {
     "format": "finnapp-v1",
     "arch": "x86_64",
     "payload": "payload.tar.zst",
     "payload_sha256": "<64-hex-sha256>",
     "payload_size": 12345,
     "signed": true,
     "signature_alg": "ed25519",
     "built_at_epoch": 0
   }
   ```
2. `manifest.sig` — raw 64-byte Ed25519 signature over `manifest.json` (omitted when `--unsigned` is passed).
3. `payload.tar.zst` — `zstd`-compressed deterministic USTAR `tar` containing:
   - `bin/<executable>` (`0755`)
   - `share/applications/<id>.desktop` (`0644`, if `desktop_entry` is declared)
   - `share/icons/<icon>` (`0644`, if `icon` is a local file path)
   - `share/finn/apps/<id>/<asset>` (`0644`, for any listed `assets`)

Determinism guarantees:
- Archive entries are sorted lexicographically, normalized to `uid=0`/`gid=0` (`root:root`), modes `0755`/`0644`, and timestamp `SOURCE_DATE_EPOCH` (default `0`).
- `zstd -q -c -3` and RFC 8032 Ed25519 are both deterministic, so repeated `finn-sdk package` runs on identical inputs yield the exact same `.finnapp` SHA-256.

---

## 4. Key Generation, Packaging, and Verification

Generate a publisher Ed25519 keypair outside your source repository (never commit `.key.pem` files):

```sh
finn-sdk keygen ~/.config/finn-dev/publisher
```

Build, sign, and verify:

```sh
finn-sdk build .
finn-sdk package . --key ~/.config/finn-dev/publisher.key.pem
finn-sdk verify dist/counter-0.1.0-x86_64.finnapp --pubkey ~/.config/finn-dev/publisher.pub.pem
```

For local CI or unsigned development builds:

```sh
finn-sdk package . --unsigned
finn-sdk verify dist/counter-0.1.0-x86_64.finnapp --allow-unsigned
```

`finn-sdk verify` checks:
- Safe archive paths (rejects `..`, leading `/`, and symlinks/hardlinks)
- `manifest.json` schema and `package` block validity
- Exact `payload_sha256` and `payload_size` match against `payload.tar.zst`
- `zstd` decompression and inner tar member path safety
- Ed25519 signature via `openssl pkeyutl -verify -pubin -rawin` (unless `--allow-unsigned` is passed for an unsigned archive)

### Alignment with FinnOS OTA Signing
`.finnapp` packaging uses the same cryptographic primitives and serialization conventions as [FinnOS OTA Updates](../ota.md): Ed25519 (RFC 8032) over canonical UTF-8 JSON (`sort_keys=True`, `separators=(",", ":")`, `ensure_ascii=False`), standard OpenSSL PKCS#8 private PEM + SPKI public PEM keys (`openssl pkeyutl -rawin`), and `zstd` + SHA-256 artifact digests. When `finn-store` adds `.finnapp` trust pinning, publisher public keys can share the `/etc/finn/` key layout alongside `/etc/finn/ota/`.

---

## 5. Current Distribution & Store Status

What works today:
- **Local desktop registration:** `finn-sdk register <id> <name> <binary>` writes `$XDG_DATA_HOME/applications/finn-<id>.desktop`, which `finn-session` merges into Home Menu and Spotlight Search.
- **User native manifests:** `finn-session` also reads `~/.local/share/finn/apps/*.manifest` (`id|name|icon|command|note|source|category`).
- **Sandboxed Linux distribution:** Third-party apps requiring runtime sandboxing today should ship via Flatpak (`docs/architecture.md`).

What does **not** exist yet:
- There is no hosted Finn Store submission portal, package index server, or automated `.finnapp` update channel.
- `finn-store` (`apps/store/main.cpp`) does not yet install `.finnapp` bundles from disk or verify publisher keys.
