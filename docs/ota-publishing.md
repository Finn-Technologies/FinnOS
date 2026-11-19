# Publishing FinnOS OTA Updates

This guide describes key generation, key custody, release packaging, signing,
and server layout for FinnOS OTA repositories using `ota/keygen` and
`ota/publish`.

---

## 1. Key Generation & Custody

Signing keys use **Ed25519** in standard OpenSSL PKCS#8 (private) and SPKI
(public) PEM formats.

Generate a release signing keypair **outside** the FinnOS git repository:

```sh
ota/keygen --out-dir /secure/offline/finnos-ota-keys --name finnos-release
```

- `ota/keygen` explicitly refuses to write private keys inside a git worktree
  and creates `*-private.pem` with mode `0600`.
- **Never commit private keys** to git or store them on the public HTTP mirror.
- Install the generated `*-public.pem` on FinnOS images at
  `/etc/finn/ota/pubkey.pem` (or inside `/etc/finn/ota/*.pem` to trust multiple
  keys during key rotation).
- `ota/finnos-ota-pubkey.pem.example` is a documentation placeholder only.

### Key Rotation

1. Ship a release signed by the **old** key whose root image already includes
   both `old.pem` and `new.pem` under `/etc/finn/ota/`.
2. Set `min_version` on subsequent releases signed by `new.pem` to at least
   that transitional version so no client upgrades directly without the new
   public key.

---

## 2. Building & Signing a Release (`ota/publish`)

```sh
ota/publish \
  --version 0.3.0 \
  --channel stable \
  --arch x86_64 \
  --image /build/finnos-0.3.0-x86_64.img \
  --prev-image /build/finnos-0.2.0-x86_64.img \
  --prev-version 0.2.0 \
  --min-version 0.1.0 \
  --rollout 10 \
  --notes-file /build/RELEASE_NOTES.md \
  --key /secure/offline/finnos-ota-keys/finnos-release-private.pem \
  --github-repo Finn-Technologies/FinnOS \
  --out-dir /srv/finnos-ota-staging \
  --upload-github
```

For automated publishing from GitHub Actions, trigger
`.github/workflows/ota-release.yml` (`workflow_dispatch`) with the
`FINN_OTA_SIGNING_KEY` repository secret configured. Custom static HTTPS
mirrors remain supported via `--base-url https://mirror.example.org`.

What `ota/publish` does:

1. Compresses `--image` with `zstd -19` (or inspects an already compressed
   `.img.zst` to record `uncompressed_size` and `uncompressed_sha256`).
2. If `--prev-image` and `--prev-version` are supplied, generates a binary
   delta via `zstd -19 --patch-from=<prev_image>`.
3. Builds the manifest JSON (`schema_version: 1`) with SHA-256 digests, exact
   byte sizes, and GitHub Release asset URLs (`https://github.com/Finn-Technologies/FinnOS/releases/download/v<version>/<file>`).
4. Signs canonical manifest JSON bytes with the Ed25519 private key and
   verifies the resulting signature immediately against the derived public key.
5. Writes `<version>/manifest-<arch>.json` and updates the rolling channel
   pointer `ota-<channel>/latest-<arch>.json` (and `<channel>/<arch>/latest.json`
   for custom mirrors). With `--upload-github`, uploads the versioned assets to
   release tag `v<version>` first and then updates the `ota-<channel>` release
   asset `latest-<arch>.json` atomically via `gh release upload --clobber`.

---

## 3. Repository Upload Layout

```text
<out-dir>/
├── stable/
│   ├── x86_64/
│   │   ├── latest.json
│   │   ├── 0.2.0/
│   │   │   ├── manifest.json
│   │   │   └── finnos-0.2.0-x86_64.img.zst
│   │   └── 0.3.0/
│   │       ├── manifest.json
│   │       ├── finnos-0.3.0-x86_64.img.zst
│   │       └── finnos-0.2.0-to-0.3.0-x86_64.zstd-delta
│   └── aarch64/
│       └── ...
├── beta/
│   └── ...
└── dev/
    └── ...
```

### Server & Staged Rollout Operations

- **HTTP Requirements:** Serve over HTTPS with `Accept-Ranges: bytes` and RFC
  7233 `206 Partial Content` support so `finn-update` can resume interrupted
  downloads.
- **Upload order:** Sync the `<version>/` directory **before** updating
  `latest.json` so clients never see a manifest pointing to missing artifacts:
  ```sh
  rsync -av --ignore-existing /srv/finnos-ota-staging/stable/x86_64/0.3.0/ \
      ota-cdn:/var/www/ota/stable/x86_64/0.3.0/
  rsync -av /srv/finnos-ota-staging/stable/x86_64/latest.json \
      ota-cdn:/var/www/ota/stable/x86_64/latest.json
  ```
- **Progressive rollout:** Publish initially with `--rollout 5`, monitor boot
  health telemetry/bug reports, then re-sign `manifest.json` / `latest.json` at
  `25` → `50` → `100` (or re-publish with `--rollout 0` to halt immediately).
