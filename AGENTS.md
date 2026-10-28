# AGENTS.md

How to work in this tree. It is written for an agent as much as for a person:
everything here is a rule, not a suggestion, and the reason is given so that
you can tell when the rule does not apply.

If you are a person, read [CONTRIBUTING.md](CONTRIBUTING.md) instead; it is the
same rules with less repetition.

## Before you change anything

1. **Read [`NAMING.md`](NAMING.md).** It is the naming rule for the whole
   family, and it is short. A new binary, module, app or file type has to fit
   it.
2. **Read [`docs/architecture.md`](docs/architecture.md).** It says what
   exists and, just as importantly, what does not. Its "what is not here yet"
   section is part of the product.
3. **Look at the code you are about to change.** The sources carry their
   reasoning in the comments. The style is deliberate: a comment explains
   *why*, never *what*, and the "what" is already in the code.
4. **Find the page that describes the thing you are changing.** If there is
   no page, write one; if the page is wrong, fix it in the same change.

## The tree in one page

```
src/core        data and plumbing only: protocol, sockets, state, registry,
                settings, audio, taskbar pins, the .fbin reader
src/compositor  finn-comp: DRM/KMS, the backends, and every shell surface
src/control     finnd (the daemon) and finnctl (the client)
src/pkg         finnpkg
src/tools       host-side preview tools; they never run on the device
ui              the .fui sources: shell, tokens, targets
apps            the .fui sources of the first-party apps
assets          fonts, wallpapers, icons, apps.registry
sdk             fuic, the Python client, the templates, FORMAT.md
mcp             finn-mcp, the MCP adapter; it runs on the host
base            packages, units, boot menus, live root, installer, mkfinniso
vm              the dev VM: install, deploy, capture, boot tests
docs            the architecture, the protocol, the guides, the design system
tests           the Python suite, the C tests, the fake control plane
```

## The rules

### Never fake a capability

This is the rule the rest of the system is built on, and it is the one most
easily broken by accident.

- A field in the system state has an absent value (`-1`), never a plausible
  default. A machine with no battery reports `-1` and the shell says "mains".
- A control that cannot act must not look like it can. A tile for an app that
  is not installed is drawn as what it is, and says so.
- A font, an icon, a wallpaper, a hardware sensor or an update feed that is
  missing is reported as missing, not substituted with something that looks
  plausible.
- If a feature does not exist, say that it does not exist — in the code, in
  the documentation and in the reply to whoever asked. The documentation is
  part of the product: `docs/architecture.md` has a section for what is
  missing, and it must stay true.

### One copy of a constant

A path, a socket name or a magic number that two files need belongs in one
header, and that header is in `src/core`. If you find yourself writing the
same literal in two places, one of them is wrong.

The names that already have one home: `FINN_CONTROL_SOCK`, `FINN_COMP_SOCK`,
`FINN_STATE_DIR`, `FINN_MAX_LINE` and the protocol magic in
`src/core/finnproto.h`; the registry paths in `src/core/finnapps.h`; the
settings path in `src/core/finnsettings.h`; the setup marker in
`src/compositor/finnsetup.h`.

### The core knows nothing about the rest

`src/core` is data and plumbing: the protocol, the socket helpers, the state
reader, the app registry, the settings store, the audio reader, the taskbar
pins. It may not grow a dependency on the compositor or on the control plane,
and neither of them may depend on the other. The one thing that crosses the
line is the protocol, and the protocol is one header.

If a shared file needs a fact from the compositor, the fact belongs in the
compositor; if the compositor needs the fact in a different shape, that is a
conversion at the edge, not a new dependency in the core.

### The control plane is the only door

Anything that starts a program, changes the machine's power, moves the pointer
or changes the display goes through `finnd`. The compositor does not spawn
programs and does not power the machine off by itself: it asks the control
plane, so that there is one audit trail and one place where the authority is.

Do not add a TCP listener. Do not add a second way in. The compositor's socket
is a provider, not a door: it is `0600`, owned by root, checks `SO_PEERCRED`,
and is reachable only with `finnctl --direct` for development.

### No AI in the code, and no model in the image

Nothing in the OS may talk to a model, ship weights, or require a network call
to a model service. FinnAI is opt-in and off by default. This is a product
decision, not a technical one, and it is not a problem to solve.

`finn-mcp` is the one adapter that exposes the control plane to an agent. It
runs on the host, speaks JSON-RPC there, and reaches the device over `ssh` and
`finnctl`. No JSON, no MCP and no model call is implemented inside a system
daemon.

### Naming

- A binary is lowercase and hyphenated: `finn-comp`, `finnpkg`. A binary name
  is never shown to a user.
- A platform name is one word, CamelCase: `FinnOS`, `FinnUI`, `FinnSDK`.
- An app is the brand and a plain English noun, written with a space and Title
  Case: **Finn Code**, **Finn Files**. Never an invented word.
- A system surface inside the OS is named for what it renders: `finnboot`,
  `finnsetup`, `finndock`, `finncontrol`, `finnclock`, `finnmenu`,
  `finnwindow`.
- A file type is the language, not the brand: `.fui` in, `.fbin` out.

### The protocol

The control protocol is line-oriented text, and it stays that way. JSON
belongs at the MCP boundary, in the adapter, on the host. If you need to add a
verb:

- add it to the daemon that owns it — `finnd` for state, power and apps;
  `finn-comp` for display and input;
- proxy it in `finnd` if it belongs to the compositor;
- document it in [`docs/control-protocol.md`](docs/control-protocol.md);
- add it to `finnctl`'s usage text;
- add a test if it can be tested without a display.

A new verb that changes the machine is not the same as one that reports on it.
`display.get` reports; `display.set` changes. Do not make a reporting verb
change anything, and do not make a changing verb pretend to only report.

### Documentation is part of the product

The documentation is not a marketing layer over the code, and it is not
generated after the fact. It is the place where the reasons live that do not
fit in a comment.

| Page | Owns |
|---|---|
| [`README.md`](README.md) | the front door: what it is, how to install, where to go |
| [`docs/README.md`](docs/README.md) | the index: which page answers which question |
| [`docs/architecture.md`](docs/architecture.md) | the processes, the layers, the session, the honest gaps |
| [`docs/control-protocol.md`](docs/control-protocol.md) | the wire, every verb, the replies |
| [`docs/install.md`](docs/install.md) | the disc, the stick, the installer, recovery |
| [`docs/dell-optiplex.md`](docs/dell-optiplex.md) | the reference machine and what was verified on it |
| [`docs/development.md`](docs/development.md) | the dev loop, the VM, the tools, the logs |
| [`docs/testing.md`](docs/testing.md) | what is tested, how, and what is not |
| [`docs/security.md`](docs/security.md) | authority, audit, hardening, the gaps |
| [`docs/hardware.md`](docs/hardware.md) | what hardware works, and its limits |
| [`docs/updates.md`](docs/updates.md) | the runtime package and the OTA frontend |
| [`docs/troubleshooting.md`](docs/troubleshooting.md) | when it does not come up |
| [`docs/ui-language.md`](docs/ui-language.md) | `.fui`, tokens, targets, the compiler |
| [`docs/design/`](docs/design/) | the design system and the motion spec |
| [`docs/roadmap.md`](docs/roadmap.md) | what is next, and what "done" means |
| [`docs/audits/`](docs/audits/) | historical point-in-time audits; not current documentation |
| [`base/README.md`](base/README.md) | the base image and its decisions |
| [`vm/README.md`](vm/README.md) | the dev VM and the capture scripts |
| [`sdk/README.md`](sdk/README.md) | the SDK |
| [`src/pkg/README.md`](src/pkg/README.md) | the package manager and the registry split |
| [`mcp/README.md`](mcp/README.md) | the adapter and its tools |
| [`CHANGELOG.md`](CHANGELOG.md) | what changed, in the order it changed |

**When the code and a page disagree, the code is right only if the code is
right.** Decide which one is wrong, fix it, and say in the commit that you
did. Do not leave a contradiction for the next reader.

## The dev loop

The build machine is the VM, not the host.

```sh
make deploy                 # build, install, restart
vm/deploy.sh --no-restart   # build and install only
vm/shot.sh /tmp/shot.png    # look at it
make test                   # the Python suite
make test-c                 # the C tests, under ASan and UBSan
make lint                   # the style checks
```

`vm/deploy.sh` sends the tree over as a tar stream, builds it in `~/finnos`
and restarts the two services. The VM has no `rsync`. On a bare Arch rootfs it
also initializes the pacman keyring, installs the dependencies, creates
`finn` and installs and enables the units.

Before you claim that a change works, look at it: capture the frame and read
the image. A compositor change that compiles is not a compositor change that
works. For a state the shell has to be driven into, use `finnctl` — it is the
same client an agent or a test would use:

```sh
sudo finnctl pointer.move x=960 y=540
sudo finnctl pointer.click
sleep 1
sudo finnctl screen.shell
```

## The tests

`make test` runs the suite in `tests/`. It covers the parts that can be tested
without a display: the registry, the protocol, the compiler, the SDK client,
the package manager's refusal rules, the base image's wiring, the MCP adapter
and the tree's own invariants. It uses only the Python standard library, so it
runs on the host and in CI.

`make test-c` runs the C tests in `tests/c/`: line framing, the settings
store, the vector rasteriser, the blur buffer, the taskbar pins, the `.fbin`
reader, the compatibility validators, the Settings page metadata and the
drawing primitives. They are built with AddressSanitizer and
UndefinedBehaviorSanitizer by default.

The compositor itself is verified by looking at it. If you add a behaviour
that can be tested without a display, test it. If you cannot, say so in the
pull request rather than leaving it implicit.

## Committing

- One change per commit, and the message says *why* in the body.
- Do not commit build output. `.gitignore` covers it; if something gets
  through, remove it from the index.
- Do not commit captures. `docs/images/` holds the ones that belong in the
  documentation, and they are resized before they are added.
- Update `CHANGELOG.md` when the user-visible behaviour changes.
- A change to the docs alone is a real change; say what was wrong.

## What to be careful about

- **The GPU path and the software path must agree.** A change to a surface
  usually has to be made in both: the software path composites layers with
  `finn_blit`, the accelerated path pastes them as quads.
- **Cached layers have a build key.** A surface that redraws is keyed on
  everything that can put a different pixel in it. If you add a property to a
  surface, add it to its key, or it will not redraw when it should.
- **The shell is one family.** The dock, the Control Center, the clock page
  and the Start Menu share their glass, their corner radii, their type scale
  and their motion. A surface that invents its own looks like a different
  operating system.
- **The live root has no `/dev/fd`.** A process substitution (`<(…)`) fails
  before the loop runs on the install medium. Write a temporary file, or use a
  FIFO held open on a descriptor.
- **A unit whose binary is missing fails at every boot.** `base/setup.sh` and
  the installer enable the session only when both binaries exist. Keep that
  order.
- **The two copies of a unit must not drift.** `finn-comp.service`,
  `finnd.service`, `finn-failure.service` and `finn-tmpfiles.conf` exist in
  `base/` and `base/airootfs/`; `mkfinniso --check` compares them.
- **The install medium and the installed machine are different machines.**
  `base/airootfs` is the live root; `base/installed` is the branding an
  installed machine gets. Do not mix them.
- **A report is not a change.** Do not let `screen.info` or `state.get` acquire
  a side effect because it was convenient.
