# finnpkg

The package manager and the OTA frontend. It owns one thing: the list of
what is on this machine.

```
/usr/share/finn/apps.registry   the catalogue, shipped with the OS
/var/lib/finnpkg/installed      what is installed here
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

An app is installed when its command can actually be run. `finnpkg
install` refuses to mark an app installed whose program is not on the
machine — a lit tile that does nothing when it is pressed is exactly the
kind of lie this system does not tell.

## The .finn package

A `.finn` package is a tarball with a manifest and the app's files:

```
manifest.toml
<the app's files>
```

The manifest is a flat list of `key = value` pairs:

```toml
id = "notes"
name = "Finn Notes"
version = "0.1.0"
command = "/usr/bin/finn-notes"
```

It is unpacked into `/opt/finn-apps/<id>/`, and the id goes on the install
list.

## OTA

`finnpkg update` is a stub, and it says so. The design it is aiming at is
in [docs/roadmap.md](../../docs/roadmap.md): an A/B slot switch, where
the new system is written to the inactive slot, the bootloader is told
which one to boot, and the switch is confirmed by the new system coming
up. A machine that is often running on battery, in a bag, with nobody
watching, cannot be updated in place.
