# Updates

FinnOS updates are GitHub Releases. A release carries a pacman package named
`finnos-runtime`; the Settings page and `finnpkg update` both read the same
release, verify the same SHA-256, and install the same file. There is no
second update path and no hidden download.

## What the package contains

`finnos-runtime` carries the part of the system that can be replaced while the
machine is running:

- `/usr/local/bin/finn-comp`, `finnd`, `finnctl`, `finn-window` and the app
  launchers
- `/usr/local/bin/finnpkg` and the compatibility helpers
- `/usr/share/finn` (registry, icons, wallpaper, version)
- `/usr/share/fonts/OTF` (Fixel Display)
- the `finn-comp`, `finnd` and `finn-failure` units, the sysusers and tmpfiles
  entries

It does **not** carry the kernel, the initramfs, the firmware, GRUB or the
rest of the Arch base. Those still move with `pacman -Syu`. This is a
userland update, not a block-level A/B switch, and the Settings page says so.
The machine can boot the previous runtime by rolling back the package; it
cannot boot a previous kernel that is no longer installed.

## The release

A tag `v<version>` builds:

- `finnos-runtime-<version>-x86_64.pkg.tar.zst`
- the same name with `.sha256`
- the install ISO from the ISO workflow

The version in the tree is the `VERSION` file. The workflow refuses to publish
a tag whose name does not match `v$(cat VERSION)`, so the asset name, the
package version and the tag cannot drift.

The package is built in an `archlinux:latest` container by
`scripts/build-runtime-pkg.sh`, as a normal user because `makepkg` refuses
root. The script copies the tree into a scratch directory, builds it, stages
the install with `make install DESTDIR=...`, and writes the package with
`makepkg`.

## Checking

`ota.check` asks GitHub for the latest release:

```
https://api.github.com/repos/Finn-Technologies/FinnOS/releases/latest
```

It looks for the asset that matches the machine's architecture, reads the
tag, and compares it with the installed `finnos-runtime` version (or
`/usr/share/finn/version` when the package predates the runtime package).
If there is no release, no matching asset, or the network is unavailable, it
says that. It never invents a version number.

## Installing

1. `ota.download` downloads the package and the `.sha256` into
   `/var/cache/finnpkg/ota/`.
2. The SHA-256 is checked before anything is unpacked.
3. `ota.install` runs `pacman -U --noconfirm --overwrite` on the package.
   The previous package is kept in the cache.
4. A detached `systemd-run` restarts `finn-comp` and `finnd` after the reply
   has been sent. The compositor comes back with the new binaries; the
   Settings page reconnects and re-reads `ota.status`.

`pacman -U` is the transaction. It is not a progress bar invented by the
shell: the job state is the systemd unit's state, and the package manager's
own error is shown if the transaction fails.

## Rolling back

`ota.rollback` reinstalls the previous `finnos-runtime` package from the cache
with the same `pacman -U` path, then restarts the two services. If there is no
previous package, it says so.

## Why not A/B?

A/B slots are the right design for a machine that must never fail to boot.
They need a partition layout, a bootloader that can switch slots, and a
confirmation step in the new system. FinnOS does not have that layout yet, and
pretending it does would be worse than saying what it has. The runtime package
is versioned, verified, reversible and honest about its scope. The A/B design
remains in [roadmap.md](roadmap.md) as future work.

## Security boundary

- Downloads are HTTPS only, with a bounded timeout and size.
- The SHA-256 comes from the release asset, and the package is verified before
  `pacman` sees it.
- There is no signing key in the image yet. The checksum protects against a
  truncated or corrupted download; it does not protect against a compromised
  GitHub release. A detached signature is the next step, and the UI does not
  claim one exists.
- The control plane runs the install as root through a tracked systemd unit,
  so the action is audited like every other privileged action.
