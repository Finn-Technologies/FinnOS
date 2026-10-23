# finnpkg

The package manager. It owns one thing: the list of what is on this machine.

```
/usr/share/finn/apps.registry   the catalogue, shipped with the OS
/var/lib/finnpkg/installed      what is installed here
/opt/finn-apps/<id>/            the files a package brought
```

The catalogue says which apps exist, what they are called and what runs
them. It never says whether they are here — that is the package manager's
own list, and it is the list the **Start Menu** reads to decide whether a
tile is lit or grey.

Keeping them apart means a catalogue update never wipes the user's
installs, and a package manager never has to edit a file the OS ships.

## Using it

```sh
finnpkg list                  # what is installed
finnpkg search notes          # what the catalogue has
finnpkg info notes            # everything known about one app
finnpkg install notes         # install an app from the catalogue
finnpkg install thing.finn    # install a package file
finnpkg remove notes          # uninstall it
finnpkg update                # check the system for updates
```

An id is a plain lowercase word — a letter, then letters, digits or `-`,
32 characters at most. Every command that takes an id checks it before the
id is used as a directory name or as a line in the install list, so `..`,
`/`, an empty string or `Notes` are refused, not resolved.

`list` prints the catalogue's apps that are marked installed; nothing
outside the catalogue is ever listed.

## Installing from the catalogue

`finnpkg install <id>` is for an app whose files the image already has.
The id has to be in the catalogue, and its command has to be on the
machine and executable: an app is installed when its program can actually
be run, and a lit tile that does nothing when it is pressed is exactly the
kind of lie this system does not tell. The id is then added to
`/var/lib/finnpkg/installed`.

## The .finn package

A `.finn` package is a tarball with a manifest, the hashes and the app's
files:

```
manifest.toml   the app's id, name and version
SHA256SUMS      `sha256sum -c` input covering every other file
<the app's files>
```

The manifest is a flat list of `key = value` pairs:

```toml
id = "notes"
name = "Finn Notes"
version = "0.1.0"
command = "/usr/bin/finn-notes"
```

`finnpkg install thing.finn` then:

1. Lists the archive before unpacking it, and refuses an absolute path, a
   `..` component, a symlink, a hard link or a device node.
2. Unpacks it into a fresh temporary directory — never into the
   destination — with `--no-same-owner --no-same-permissions`, and checks
   that every path that landed is still under that directory.
3. Requires the manifest and a valid id, and requires the id to be in the
   OS catalogue: `finnapps` resolves a launch command from the catalogue,
   so an app it has never heard of could be installed and never start.
4. Requires `SHA256SUMS` to list every file in the package except itself,
   and verifies it with `sha256sum -c`, or `shasum -a 256 -c` where that is
   what the machine has.
5. Copies the checked tree into `/opt/finn-apps/<id>/` through a staging
   directory, and only then records the id as installed.

A package without `SHA256SUMS`, with a hash that does not match, or with a
file the hashes do not cover is refused, and nothing is copied.

**There is no signature check.** This build has no keyring and no
signature verifier, so the hashes are the only check there is: they prove
the files arrived as they were packaged, not who packaged them. A package
from an untrusted source is not made trustworthy by installing it.

`finnpkg remove <id>` refuses an id that is not a plain catalogue id and
an app that is not installed, then removes the directory and the install
line. If a catalogue update ever drops an app that is still installed, it
has to be removed by hand: the package manager will not delete a directory
for an id the catalogue no longer describes.

## What is not here yet

- **Signatures.** See above: integrity only, no authorship.
- **A command from the manifest.** A package's files land in
  `/opt/finn-apps/<id>`, but the launch command is still whatever the
  catalogue names. The manifest's `command` is not installed onto the
  PATH or read by `finnapps`.
- **Dependencies, versions and upgrades.** One version per id, no
  dependency graph, and `install` over an installed app replaces it.
- **OTA.** `finnpkg update` is a stub, and it says so. The design it is
  aiming at is in [docs/roadmap.md](../../docs/roadmap.md): an A/B slot
  switch, where the new system is written to the inactive slot, the
  bootloader is told which one to boot, and the switch is confirmed by the
  new system coming up. A machine that is often running on battery, in a
  bag, with nobody watching, cannot be updated in place.
