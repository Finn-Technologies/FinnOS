# FinnOS Package Repository Tooling

This directory holds the layout definition, mirrorlist template, signing key
generator, and publishing script for the first-party `[finnos]` pacman
repository.

> [!NOTE]
> `https://repo.finnos.org/$repo/os/$arch` is a placeholder URL. Live public
> repository hosting and production GPG signing keys are not provisioned in this
> repository yet. Use `publish.sh` to stage a local or LAN repository and point
> `/etc/pacman.d/finnos-mirrorlist` at it until `repo.finnos.org` is hosted.

## Files

- `finnos-mirrorlist` — template for `/etc/pacman.d/finnos-mirrorlist` (used by
  `[finnos]`; upstream Arch fallback mirrors remain separate in
  `/etc/pacman.d/mirrorlist`).
- `gen-signing-key.sh` — generates an Ed25519 GPG signing keypair **outside**
  the git working tree and exports only public keyring files.
- `publish.sh` — stages `.pkg.tar.zst` packages into
  `<repo-dir>/finnos/os/<arch>/`, signs packages and `finnos.db.tar.zst` with
  `gpg` and `repo-add`, and optionally syncs to a mirror via `rsync`.

## Repository layout

```text
<repo-dir>/
└── finnos/
    └── os/
        ├── x86_64/
        │   ├── finnos.db -> finnos.db.tar.zst
        │   ├── finnos.db.tar.zst
        │   ├── finnos.db.tar.zst.sig
        │   ├── finnos.files -> finnos.files.tar.zst
        │   ├── finnos.files.tar.zst
        │   ├── finnos.files.tar.zst.sig
        │   ├── <pkg>.pkg.tar.zst
        │   └── <pkg>.pkg.tar.zst.sig
        └── aarch64/
            └── ...
```

## Quick start (on an Arch build host)

1. Generate a signing key outside the repository:
   ```sh
   repo/gen-signing-key.sh
   ```
2. Copy the exported public files (`finnos.gpg`, `finnos-trusted`) into
   `base/pkgbuilds/finnos-keyring/` and update `sha256sums` before building
   `finnos-keyring`.
3. Build first-party packages (`base/pkgbuilds/*`, `scripts/build-runtime-pkg.sh`)
   with `makepkg`.
4. Publish signed packages to a local staging directory or remote rsync target:
   ```sh
   repo/publish.sh --arch x86_64 --gpg-key <FPR> --repo-dir /srv/finnos/repo \
     path/to/*.pkg.tar.zst
   ```

See [docs/repo.md](../docs/repo.md) for the full trust chain, key custody rules,
upstream fallback inventory, and relationship to OTA updates.
