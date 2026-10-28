# Contributing to FinnOS

FinnOS is a small operating system, and the tree is small on purpose. The bar
for a change is that it fits the shape of what is already there — not that it
is the largest possible change, and not that it is the smallest.

Thank you for reading this far. The rest of the page is the shortest path from
"something is wrong" to "it is merged".

## Before you write code

Read these three, in this order:

1. **[AGENTS.md](AGENTS.md)** — the working agreement. It explains *why* the
   rules exist, which is what lets you tell when a rule does not apply.
2. **[NAMING.md](NAMING.md)** — the naming family. It is short. A new binary,
   module, app or file type has to fit it.
3. **[docs/architecture.md](docs/architecture.md)** — what exists, and what
   does not. The second half matters as much as the first.

If you are changing a shell surface, also read
[docs/design/design-system.md](docs/design/design-system.md) and
[docs/design/motion.md](docs/design/motion.md). The dock, the Control Center,
the clock page and the Start Menu share their glass, their radii, their type
scale and their motion; a surface that invents its own looks like a different
operating system.

## Ways to contribute

- **Report a bug.** Say what you ran, what you expected, what happened, and
  whether it was a VM, an install medium or real hardware. A capture helps.
- **Report hardware.** The release target is a Dell OptiPlex 3020, but the
  image is a general x86_64 profile. `lspci -k`, `dmesg`, the output of
  `systemctl status finn-comp finnd` and a photograph of the screen are all
  useful. Say what you did **not** verify.
- **Write documentation.** Docs drift is a real bug here; the rule is that the
  documentation is part of the product.
- **Write code.** Start with an issue if the change is structural. Small,
  well-reasoned fixes do not need one.
- **Test on hardware.** CI can prove the disc boots and the installer writes a
  disk. It cannot prove the compositor looks right on a real panel.

## Set up a development machine

The compositor needs DRM, GBM, EGL, FreeType, ALSA and the Fixel fonts. The
build machine is Arch Linux (or the Arch dev VM), not macOS. The Mac can be the
editor; it is not the compiler.

### Host tools only (no compositor)

The Python suite, the compiler, the client and the MCP adapter run anywhere
Python 3.8+ runs:

```sh
python3 -m pip install -e sdk      # optional: puts fuic and finn-sdk on PATH
make test
make lint
```

The suite has no third-party dependencies. That is deliberate: a suite that
needs a wheel is a suite nobody runs.

### The full loop

You need an Arch machine with the build dependencies. `vm/deploy.sh` prepares a
bare Arch rootfs when you connect as root, or install the packages yourself:

```sh
sudo pacman -S --needed base-devel pkgconf libdrm libwebp freetype2 \
  alsa-lib mesa wayland wayland-protocols libxkbcommon
```

Then one of:

```sh
# the VMware dev VM described in vm/README.md
make deploy

# a bare Arch rootfs
FINN_HOST=root@<address> vm/deploy.sh
```

### The loop

```sh
make deploy                 # build, install and restart on the target
vm/shot.sh /tmp/shot.png    # look at the frame
make test                   # the Python suite
make test-c                 # the C tests, under ASan and UBSan
make lint                   # style
scripts/verify.sh           # tests, lint, deploy, captures, the journal
```

`vm/deploy.sh --no-restart` builds and installs without restarting the
session. For the shell, the fast loop is the host-side previewer:

```sh
make -C src/tools
src/tools/dockprev out.ppm 1080 2 87 0 3 40
src/tools/vgtest assets/icons sheet.ppm 2
```

## What "done" means

A change is done when all of these are true:

1. **It builds** — `make` and `make tools`, with no new warnings.
2. **The tests pass** — `make test`, plus `make test-c` if you touched C.
3. **The style is clean** — `make lint`.
4. **You looked at it** — if it changes a pixel, a capture is the proof. A
   compositor change that compiles is not a compositor change that works.
5. **The documentation is true** — if behaviour changed, the page that
   describes it changed in the same commit. `docs/architecture.md` has a
   "what is not here yet" section, and it must stay true.
6. **The commit says why** — one change per commit, and the body explains the
   reason rather than restating the diff.

If you could not do one of these, say so in the pull request. An honest "I
could only test this in QEMU" is worth more than an implied hardware test.

## Style

### C

The house style is the style of the sources already there: four spaces, braces
on the same line, `_` in names, no tabs. A comment explains *why*, never
*what* — the "what" is already in the code.

```c
/* The marker is the completion gate: /var/lib/finn/setup-done exists only
 * after the language has been saved, so a failed write keeps the screen up. */
```

Rules that are easy to break by accident:

- **Never fake a capability.** If the machine has no battery, the value is
  `-1` and the shell says "mains". Do not invent a default.
- **One copy of a constant.** A path or magic number two files need belongs in
  one header, and that header is in `src/core`.
- **`src/core` knows nothing about the compositor or the control plane.**
  Neither of them may depend on the other. The one thing that crosses the line
  is the protocol, and the protocol is one header (`src/core/finnproto.h`).
- **Add to the build key.** A cached surface that redraws is keyed on
  everything that can put a different pixel in it. A new property on a surface
  that is not in its key will not redraw when it should.
- **The GPU path and the software path must agree.** A surface change usually
  has to be made in both.

### Shell and Python

Shell scripts are `#!/bin/bash` with `set -euo pipefail`, quoted expansions and
no process substitution where the script may run on a live root: a live root
has no `/dev/fd`, so `<(…)` fails before the loop starts. Use a temporary file.

Python is standard-library only, 3.8+, four-space indentation, and no
dependencies in the SDK or the adapter.

### Documentation

- Write in the declarative present: "the compositor owns the display", not
  "the compositor will own the display".
- Say what is missing. A stub says it is a stub, and so does the page that
  describes it.
- Use tables for sets of facts, prose for reasoning.
- Link to the code when a claim is easy to disbelieve.
- British spelling is the house style: colour, behaviour, licence, centre.

## Adding a control-plane verb

The control protocol is line-oriented text and it stays that way. JSON belongs
at the MCP boundary, in the adapter, on the host. If you need a new verb:

1. Add it to the daemon that owns it — `finnd` for state, power, apps, packages
   and updates; `finn-comp` for display and input.
2. Proxy it in `finnd` if it belongs to the compositor.
3. Document it in [docs/control-protocol.md](docs/control-protocol.md).
4. Add it to `finnctl`'s usage text.
5. Decide whether it **reports** or **changes**, and do not blur the two. A
   reporting verb must not change anything; a changing verb must not pretend
   to only report.
6. Add a test if it can be tested without a display; if it cannot, say so.

## Naming

[NAMING.md](NAMING.md) is the rule. The two that bite most often:

- A binary is lowercase and hyphenated (`finn-comp`), and a binary name is
  never shown to a user.
- An app is the brand and a plain English noun, written with a space and Title
  Case (**Finn Code**, **Finn Files**), never an invented word.

A system surface inside the OS is named for what it renders: `finnboot`,
`finnsetup`, `finndock`, `finncontrol`, `finnclock`, `finnmenu`,
`finnwindow`.

## Commits and pull requests

- **One change per commit.** A commit that fixes a bug and reformats a file is
  two commits.
- **The subject says what changed; the body says why.** Keep the subject under
  about 72 characters.
- **Do not commit build output or captures.** `.gitignore` covers most of it.
  `docs/images/` is for the captures that belong in the documentation, and
  they are resized before they are added.
- **Update `CHANGELOG.md` when the user-visible behaviour changes.** Add to
  `## Unreleased`; do not invent a version.
- **A pull request says:**
  - what changed and why;
  - what you ran, and what you saw;
  - what you could not test;
  - anything you deliberately left out.

Reviewers look for: the honesty rules, the one-door rule, the core boundary,
the naming family, the build key, both rendering paths, and whether the docs
moved with the code.

## Reporting security issues

Do not open a public issue for a privilege-boundary problem. The control plane
is root-equivalent authority; a bypass is a security bug. Contact the
maintainers privately, and read [docs/security.md](docs/security.md) for the
model you are testing against.

## Licence

MIT. By contributing you agree that your contribution is licensed under the
same terms. See [LICENSE](LICENSE).
