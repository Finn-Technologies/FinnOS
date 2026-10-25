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

## Flatpak and Wine command-line status

Flatpak package management is separate from the FinnOS-native registry:

```sh
finnpkg flatpak search calculator
finnpkg flatpak list
finnpkg flatpak install org.gnome.Calculator
finnpkg flatpak run org.gnome.Calculator
finnpkg flatpak remove org.gnome.Calculator
```

Flatpak package operations use the system installation. `search` requests
application, name, description and remote columns, so the source of each
result is preserved. `install <app-id> [remote]` defaults to Flathub for the
CLI; the Store passes the remote from the selected search result. Remote names
are validated, non-Flathub remotes must already be configured system-wide, and
Flathub is added only from the official signed repository or refused if its
configured URL is unexpected. App IDs are validated as reverse-DNS identifiers
and passed as separate process arguments, not shell text. Removing a Flatpak
does not delete its per-user data.

Wine support is x86_64-only and gives each app a private prefix:

```sh
finnpkg wine install paint "$HOME/Downloads/Paint-Setup.exe"
finnpkg wine set paint "$HOME/.local/share/finn/wine/paint/prefix/drive_c/Program Files/Paint/Paint.exe"
finnpkg wine list
finnpkg wine run paint
```

`install` registers the installer, creates a private per-app prefix under the
user's Finn data directory, and starts it through the audited `finnd` launch
path. Installers run asynchronously; after setup, the user must locate the
installed `.exe` and use `set` to point the registration at it. FinnOS does not
auto-discover application paths. `register` remains available for an existing
or portable `.exe`; Wine initializes a new prefix on first launch. `list`
shows the CLI registrations; control-plane clients use `finnctl wine.list`,
which returns validated, private registration IDs as newline-separated binary
payload data, bounded to 64 KiB. `run` sends only the app id to
`finnctl wine.run`; finnd verifies the registration and launches Wine as Finn
in a transient systemd unit with `WINEPREFIX` set to that app's prefix. The
executable and prefix are passed as separate arguments/environment values,
not shell text. The aarch64 build refuses these operations and no ARM Wine is
promised. Real Wine GUI execution has not yet been validated on FinnOS.

`finnpkg flatpak run <app-id>` validates the reverse-DNS id and delegates to
the audited `finnd flatpak.run` verb; it never launches the app directly or
trusts a caller-provided display. The daemon verifies the system installation
before starting the app in FinnOS's Wayland session.

`finnd` exposes system Flatpak operations through the audited control socket:
`flatpak.list` and `flatpak.search` return the package manager's text, while
`flatpak.install <app-id> [remote]` and `flatpak.remove <app-id>` queue systemd
jobs so downloads do not block the control plane. `flatpak.job <id>` reports
the job state. `flatpak.run <app-id>` verifies installation and starts the app
as Finn with the Wayland session environment.

Finn Store lists installed Flatpak apps, searches configured remotes, passes a
result's remote through to the install job, and offers Open for installed
Library entries. The in-process host supports only xdg-shell `wl_shm`
toplevels; popups, subsurfaces, dmabuf, touch, clipboard, drag-and-drop and
other protocols remain unsupported. On an ARM Linux test VM, the current
control path launched an installed GNOME Calculator into a standalone test
server; its 410×666 surface rendered, and pointer plus keyboard events changed
the displayed expression. This was not a booted FinnOS desktop or a Store
search/install/launch walkthrough, so full desktop compatibility is still
unverified.

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
