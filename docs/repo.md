# FinnOS Package Repository, Mirrors, and Signing Keys

FinnOS uses `pacman` as its package manager on top of an upstream Arch package
base. This document describes the first-party `[finnos]` repository layout, GPG
trust chain, key custody rules, package publishing workflow, the upstream
fallback boundary, and how package signing relates to OTA system updates.

> [!NOTE]
> **Current status:** `https://repo.finnos.org/$repo/os/$arch` is a placeholder
> URL; live public repository hosting is not provisioned yet. Likewise,
> `base/pkgbuilds/finnos-keyring/finnos.gpg` is an empty placeholder until the
> release operator generates a keypair outside the git tree with
> [`repo/gen-signing-key.sh`](../repo/gen-signing-key.sh). Nothing in this tree
> ships private keys or fakes an active trust root.

## Repository configuration and order

[`base/pacman.conf`](../base/pacman.conf) places `[finnos]` **before**
`[core]` and `[extra]`:

```ini
[finnos]
SigLevel = Required
Server = https://repo.finnos.org/$repo/os/$arch
```

Because `pacman` resolves a package name from the first configured repository
that provides it, placing `[finnos]` first guarantees that FinnOS-built or
rebuilt packages (`finn-filesystem`, `finnos-compose`, `finnos-keyring`,
`finnos-runtime`) take precedence over upstream repositories.

### Mirrorlists

- **FinnOS mirrorlist (`/etc/pacman.d/finnos-mirrorlist`):**
  Template at [`repo/finnos-mirrorlist`](../repo/finnos-mirrorlist). Installed by
  `vm/finn-install.sh`, `vm/finn-chroot.sh`, `base/setup.sh`, and the
  `finnos-keyring` package (`backup=('etc/pacman.d/finnos-mirrorlist')`). For
  local or LAN testing, point `Server =` at a directory staged by
  `repo/publish.sh` (for example `file:///srv/finnos/repo/$repo/os/$arch`) and
  switch `[finnos]` in `/etc/pacman.conf` to
  `Include = /etc/pacman.d/finnos-mirrorlist`.
- **Upstream fallback mirrorlist (`/etc/pacman.d/mirrorlist`):**
  Used **only** by `[core]`, `[extra]`, and (on `x86_64`) `[multilib]`. Do not
  mix upstream Arch mirrors into `finnos-mirrorlist`: `pacman` expands `$repo`
  to `finnos`, which upstream Arch mirrors do not serve.

`vm/finn-install.sh` no longer hard-codes `geo.mirror.pkgbuild.com`; it installs
`/etc/pacman.d/finnos-mirrorlist`, preserves the live medium's
`/etc/pacman.d/mirrorlist` (with an optional `FINN_ARCH_FALLBACK_MIRROR`
override), and marks it explicitly as an upstream fallback.

## Trust chain and key custody

`[finnos]` sets `SigLevel = Required`, which requires valid GPG detached
signatures (`.sig`) on **both** every package tarball (`*.pkg.tar.zst.sig`) and
the repository databases (`finnos.db.tar.zst.sig`, `finnos.files.tar.zst.sig`).

### Key generation and storage rules

1. **Private keys never enter git.** [`repo/gen-signing-key.sh`](../repo/gen-signing-key.sh)
   canonicalizes `--key-dir` (default `${XDG_DATA_HOME:-$HOME/.local/share}/finnos/signing-keys`)
   and refuses to run if the target path resolves inside the FinnOS repository
   tree.
2. **Algorithm and expiry.** `gen-signing-key.sh` creates an Ed25519 signing
   key (`eddsa` / `ed25519`, default 2-year expiry, passphrase-protected unless
   `--no-passphrase` is explicitly passed for ephemeral testing).
3. **Public keyring package (`finnos-keyring`).**
   [`base/pkgbuilds/finnos-keyring/`](../base/pkgbuilds/finnos-keyring/PKGBUILD)
   packages only the public artifacts into `/usr/share/pacman/keyrings/`:
   - `finnos.gpg` — public key material exported by `gen-signing-key.sh`;
   - `finnos-trusted` — `<fingerprint>:4:` lines imported by
     `pacman-key --populate finnos`;
   - `finnos-revoked` — revoked key fingerprints (checked on upgrade).
   Its `build()` step refuses to package an empty `finnos.gpg` placeholder so a
   non-functional keyring cannot be shipped by accident.
4. **Key rotation and revocation.** Before an active key expires or if a key is
   compromised, add the replacement public key to `finnos.gpg` and
   `finnos-trusted`, add the retired/compromised fingerprint to `finnos-revoked`
   when revoking, bump `finnos-keyring`, and sign the new `finnos-keyring`
   package with a still-trusted key before retiring the old key.

## Package lifecycle: `PKGBUILD` to signed repository

```text
  PKGBUILD (base/pkgbuilds/* or scripts/build-runtime-pkg.sh)
      │
      ▼  makepkg (Arch host / container, non-root)
  <pkg>-<ver>-<rel>-<arch>.pkg.tar.zst
      │
      ▼  repo/publish.sh --arch <arch> --gpg-key <FPR>
      ├── gpg --detach-sign  ──▶  <pkg>.pkg.tar.zst.sig
      ├── repo-add --verify --sign  ──▶  finnos.db.tar.zst + .sig
      │                                  finnos.files.tar.zst + .sig
      └── rsync -avz --delete-after --delay-updates  ──▶  mirror host
      │
      ▼  client machine (`[finnos]` SigLevel = Required)
  pacman -Syu  ──▶  verifies db.sig & pkg.sig against pacman-key (finnos-keyring)
```

1. **Build:** Run `makepkg` in `base/pkgbuilds/<name>/` (or run
   `scripts/build-runtime-pkg.sh` for `finnos-runtime`) on a pacman/makepkg
   build host or container.
2. **Sign and index:** Run `repo/publish.sh --arch <arch> --gpg-key <FPR> ...`.
   The script stages packages under `<repo-dir>/finnos/os/<arch>/`, generates a
   binary `.sig` alongside each package tarball, and updates the signed
   database with `repo-add --verify --sign --key <FPR>`.
3. **Sync:** Passing `--rsync <user@host:/srv/repo/>` transfers the tree with
   `--delay-updates --delete-after` so clients do not observe a half-updated
   database and missing package tarballs.

## What must be rebuilt to drop upstream `[core]` and `[extra]`

Upstream Arch `[core]`, `[extra]`, and (on `x86_64`) `[multilib]` remain
enabled in [`base/pacman.conf`](../base/pacman.conf) until FinnOS rebuilds and
hosts the full OS package closure in `[finnos]`. Dropping upstream mirrors
requires rebuilding and maintaining signed `[finnos]` packages for everything
currently listed in [`base/packages.x86_64`](../base/packages.x86_64) and
[`base/packages.aarch64`](../base/packages.aarch64) plus their transitive
dependencies:

1. **Core userland & toolchain:** `base` (`glibc`, `bash`, `coreutils`,
   `openssl`, `pacman`, etc.), `systemd`, `systemd-sysvcompat`, `sudo`,
   `util-linux`, `kbd`, `base-devel`, `cmake`, `ninja`, `pkgconf`, `python`,
   `git`, `jq`, `zstd`, `curl`, `openssh`, `nano`, `vim`.
2. **Kernel, firmware, boot & disk tools:** `linux` (`x86_64`),
   `linux-aarch64` (`aarch64`), `linux-firmware`, `intel-ucode`, `amd-ucode`,
   `plymouth`, `grub`, `efibootmgr`, `dosfstools`, `e2fsprogs`, `parted`,
   `gptfdisk`, `arch-install-scripts`.
3. **Display, Wayland, Mesa & input:** `libdrm`, `libinput`, `seatd`, `mesa`,
   `libglvnd`, `vulkan-intel` (`x86_64`), `wayland`, `wayland-protocols`,
   `libxkbcommon`, `wlroots0.20` (`x86_64`), `xorg-xwayland`,
   `xdg-desktop-portal`, `xdg-desktop-portal-wlr`.
4. **Qt6 UI & media stack:** `qt6-base`, `qt6-wayland`, `qt6-svg`,
   `qt6-imageformats`, `qt6-tools`, `qt6-declarative`, `qt6-webengine`,
   `layer-shell-qt`, `freetype2`, `fontconfig`, `libwebp`, `ttf-dejavu`,
   `noto-fonts`, `noto-fonts-cjk`, `noto-fonts-emoji`, `desktop-file-utils`,
   `shared-mime-info`.
5. **Audio, Bluetooth, network & power:** `alsa-lib`, `alsa-ucm-conf`,
   `pipewire`, `pipewire-pulse`, `wireplumber`, `bluez`, `bluez-utils`,
   `networkmanager`, `wpa_supplicant`, `upower`.
6. **App compatibility (`flatpak` + `x86_64` multilib/Wine):** `flatpak`, and on
   `x86_64`: `wine`, `winetricks`, `wine-mono`, `wine-gecko`, `lib32-mesa`,
   `lib32-vulkan-icd-loader`, `lib32-vulkan-intel`.

Until those packages are rebuilt and hosted in `[finnos]`, removing `[core]`,
`[extra]`, or `[multilib]` would leave an unbuildable ISO and unresolvable
system upgrades.

## Relationship to OTA updates

Package-level `pacman` verification and OTA image/slot updates solve different
layers of the update problem:

- **`[finnos]` + `pacman` (`finnos-keyring`)** distributes individual packages
  and incremental runtime/userland updates using GnuPG signatures
  (`repo/gen-signing-key.sh`, `repo/publish.sh`) checked by `pacman-key` from
  `/usr/share/pacman/keyrings/finnos.gpg`.
- **OTA updates ([`docs/ota.md`](ota.md))** verify canonical JSON update
  manifests signed with RFC 8032 Ed25519 (`ota/keygen`, `ota/publish`, verified
  by `finn-update` against `/etc/finn/ota/pubkey.pem`) plus full/delta artifact
  SHA-256 checksums before flashing an inactive A/B root slot with boot-count
  rollback.

Keeping the `pacman` GPG keyring (`repo/gen-signing-key.sh`) and the OTA
Ed25519 manifest key (`ota/keygen`) separate limits blast radius: compromising
one build pipeline does not automatically compromise the other, and A/B OTA
slot updates remain atomic and rollback-safe even when `pacman` packages are
built independently.
