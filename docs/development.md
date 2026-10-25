# Working on FinnOS

## The build machine is the VM

The compositor needs the DRM headers, the GPU stack, FreeType and the
fonts. The Mac is an editor; the Arch VM in Parallels is where the shell
is built and run. Nothing is compiled on the host.

```sh
make deploy                 # build, install and restart on the VM
make deploy -- --no-restart # build and install, leave it running
```

`vm/deploy.sh` sends the tree over as a tar stream, builds it in
`~/finnos`, installs it to `/usr/local/bin`, and restarts the two services.
The VM has no `rsync`, which is why it is a tar stream.

The VM's address is in `vm/README.md`. Override it with `FINN_HOST`:

```sh
FINN_HOST=finn@192.168.1.20 vm/deploy.sh
```

## Looking at it

```sh
vm/shot.sh /tmp/desktop.png          # a capture of the frame
vm/shot.sh /tmp/desktop.png 2        # half resolution
```

The capture comes back over the control plane as a PPM and is converted on
the host with Pillow. The device needs nothing installed for it to work.

To drive the shell from a script, `finnctl` is the same client an agent
would use:

```sh
ssh finn@10.211.55.6 '
  sudo finnctl pointer.move x=1196 y=1506
  sudo finnctl pointer.click
  sleep 1
  sudo finnctl screen.shell
'
```

`screen.shell` reports what the shell is showing — which surfaces are open,
how open they are, where the pointer is — so a script can wait for a state
instead of guessing at a sleep.

## The host-side tools

Two programs in `src/tools` render the real drawing code without a VM:

```sh
make -C src/tools
src/tools/dockprev out.ppm 1080 2 87 0 3 40    # the dock
src/tools/vgtest src/compositor/icons sheet.ppm 2   # the icon set
```

They are the fast loop: change a number, run the tool, look at the image.
`vgtest` writes a sheet of every icon in the set, which is how a change to
the vector rasteriser is judged.

## The services

```sh
ssh finn@10.211.55.6 '
  systemctl status finn-comp finnd
  sudo journalctl -u finn-comp -f
  sudo journalctl -u finnd -f
  sudo tail -f /var/log/finn/control.log     # the audit trail
'
```

The compositor writes to the journal, never to the TTY it is painting on.
Every control-plane call is audited with the peer uid.

## The tests

```sh
make test
```

The suite covers the parts that can be tested without a display: the
application registry, the control protocol, the `.fui` compiler and the
SDK's client library. `make test-c` adds the C tests that need a compiler
but no device: the line framing and the settings store, under ASan and
UBSan. The compositor itself is still tested by looking at it — there is
no client surface to assert a frame against, so a visual change needs a
capture.

## Real hardware

For running on an x86_64 PC instead of the VM — a Dell OptiPlex in
particular — see `docs/dell-optiplex.md`. The same ISO and installer are
used; the only differences are Secure Boot (off) and the disk name.

## Style

The C is written in the house style of the existing sources, and those
sources say why they are the way they are. A few rules that are easy to
break by accident:

- **Comment the reason, not the mechanism.** The tree is full of comments
  that explain *why* a thing is the way it is, because the "what" is
  already in the code. Keep it that way.
- **Never fake a capability.** If the machine has no battery, the value is
  `-1` and the shell says "mains". Do not invent a default.
- **One copy of a constant.** A path or a magic number that two files
  need belongs in one header.
- **The core knows nothing about the compositor.** `src/core` is data and
  plumbing; it may not grow a dependency on either of the two binaries.

## The naming

`NAMING.md` is the rule, and it is short on purpose. If you are adding a
name — a binary, a module, an app, a file type — read it first. The two
that bite most often:

- a binary is lowercase and hyphenated (`finn-comp`), and a binary name is
  never shown to a user;
- an app is the brand and a plain English noun, written with a space
  (**Finn Code**), never an invented word.
