# Updates

FinnOS updates are GitHub Releases. A release carries a pacman package named
`finnos-runtime`; the Settings page and `finnpkg update` both read the same
release, verify the same SHA-256 and install the same file. There is no second
update path and no hidden download.

This is a **runtime update**: it replaces the userland FinnOS owns. It is not an
A/B slot switch, and it does not replace the kernel, the firmware, GRUB or the
rest of the Arch base. [Why not A/B](#why-not-ab) says why, and
[roadmap.md](roadmap.md) keeps the design for when it exists.

## What the package contains

`finnos-runtime` carries the part of the system that can be replaced while the
machine is running:

- `/usr/local/bin/finn-comp`, `finnd`, `finnctl`, `finn-window` and the app
  launchers
- `/usr/local/bin/finnpkg` and the compatibility helpers
- `/usr/share/finn` (registry, icons, wallpaper, version)
- `/usr/share/fonts/OTF` (Fixel Display)
- the `finn-comp`, `finnd` and `finn-failure` units, and the sysusers and
  tmpfiles entries

It does **not** carry the kernel, the initramfs, the firmware, GRUB or the rest
of the Arch base. Those still move with `pacman -Syu`.

The machine can boot the previous runtime by rolling back the package. It
cannot boot a previous kernel that is no longer installed.

## The release

A tag `v<version>` builds:

| Asset | What it is |
|---|---|
| `finnos-runtime-<version>-x86_64.pkg.tar.zst` | the package |
| `finnos-runtime-<version>-x86_64.pkg.tar.zst.sha256` | the checksum |
| the install ISO | from the ISO workflow |

The version in the tree is the `VERSION` file. The workflow refuses to publish
a tag whose name does not match `v$(cat VERSION)`, so the asset name, the
package version and the tag cannot drift.

The package is built in an `archlinux:latest` container by
[`scripts/build-runtime-pkg.sh`](../scripts/build-runtime-pkg.sh), as a normal
user because `makepkg` refuses root. The script copies the tree into a scratch
directory, builds it, stages the install with `make install DESTDIR=…`, and
writes the package with `makepkg`.

The release workflow builds **x86_64 only**: there is no official aarch64
`archlinux` container for a GitHub runner to pull. An aarch64 machine's
`ota.check` says so honestly instead of taking a wrong-architecture package; the
ARM development VM updates through `vm/deploy.sh`.

## Checking

`ota.check` asks GitHub for the latest release:

```
https://api.github.com/repos/Finn-Technologies/FinnOS/releases/latest
```

It looks for the asset that matches the machine's architecture, reads the tag,
and compares it with the installed `finnos-runtime` version (or
`/usr/share/finn/version` when the package predates the runtime package). If
there is no release, no matching asset, or the network is unavailable, it says
that. It never invents a version number.

```sh
finnctl ota.check
finnpkg update check
```

The reply is a bounded payload with the current version, the latest version,
whether an update is available, the asset URL, its checksum, the release notes
and — when it failed — the reason. The time of the check is stored in
`/var/lib/finnpkg/ota/check` and appears in `ota.status`.

## Installing

```sh
finnctl ota.download
finnctl ota.install
finnctl ota.job <id>
```

1. `ota.download` downloads the package and the `.sha256` into
   `/var/cache/finnpkg/ota/`.
2. The SHA-256 is checked before anything is unpacked.
3. `ota.install` runs `pacman -U --noconfirm --overwrite '*'` on the package.
   It records the version it replaced; rollback needs that package to still be
   in the OTA cache or `/var/cache/pacman/pkg`. Pacman's cache is not a
   guarantee, so a machine that cleans it has no previous runtime to restore.
4. A detached `systemd-run` restarts `finn-comp` and `finnd` after the reply
   has been sent. The compositor comes back with the new binaries; the Settings
   page reconnects and re-reads `ota.status`.

`pacman -U` is the transaction. It is not a progress bar invented by the shell:
the job state is the systemd unit's state, and the package manager's own error
is shown if the transaction fails.

The apply verbs are jobs rather than blocking calls because the control plane
must answer *before* pacman replaces the package that `finnd` itself lives in.

## Rolling back

```sh
finnctl ota.rollback
```

`ota.rollback` reinstalls the recorded previous `finnos-runtime` package from
the OTA cache or `/var/cache/pacman/pkg` with the same `pacman -U` path, then
restarts the two services. If no previous version was recorded, or its file is
no longer cached, it refuses rather than installing something else. It does not
roll back the kernel, the firmware or anything else the runtime package never
contained.

## Why not A/B?

A/B slots are the right design for a machine that must never fail to boot. They
need a partition layout, a bootloader that can switch slots, and a confirmation
step in the new system. FinnOS does not have that layout yet, and pretending it
does would be worse than saying what it has.

The runtime package is versioned, verified, reversible and honest about its
scope. The A/B design remains in [roadmap.md](roadmap.md) as future work.

## Security boundary

- Downloads are HTTPS only, with a bounded timeout and size.
- The SHA-256 comes from the release asset, and the package is verified before
  `pacman` sees it.
- **There is no signing key in the image yet.** The checksum protects against a
  truncated or corrupted download; it does not protect against a compromised
  GitHub release. A detached signature is the next step, and the UI does not
  claim one exists.
- The control plane runs the install as root through a tracked systemd unit, so
  the action is audited like every other privileged action.
- The package is installed with `pacman -U`, so it is subject to pacman's own
  file and database checks; it is not a raw overwrite of `/usr`.

The general security model, including what the `finn` account can do, is in
[security.md](security.md).

## If the update goes wrong

The compositor and the control plane are restarted after the transaction. If
they do not come back:

1. `Ctrl+Alt+F2` reaches `getty@tty2`.
2. Log in as `finn` or `root`.
3. Check the package and the services:

   ```sh
   pacman -Q finnos-runtime
   systemctl status finn-comp finnd
   sudo journalctl -u finn-comp -n 100 --no-pager
   sudo journalctl -u finnd -n 100 --no-pager
   ```

4. Roll back from the console:

   ```sh
   find /var/cache/finnpkg/ota /var/cache/pacman/pkg \
        -name 'finnos-runtime-*.pkg.tar.zst'
   sudo pacman -U /path/to/the/older/finnos-runtime-<version>-x86_64.pkg.tar.zst
   sudo systemctl restart finn-comp finnd
   ```

   The OTA cache keeps one directory per version; the pacman cache keeps the
   package it replaced. Either is a valid rollback source.

5. If the package database is unhappy, `pacman -Qkk finnos-runtime` reports
   what no longer matches.

The runtime package cannot make the machine unbootable by itself: the kernel,
the initramfs and the bootloader are outside it. A machine that does not reach
a login prompt after an update is a boot-path problem, not an OTA problem; see
[install.md](install.md#recovery).
