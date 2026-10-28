# FinnOS documentation

This is the index. Every page here is written for someone who has to make a
decision: install the system, change it, debug it, or judge whether it is
telling the truth.

## If you want to…

| Task | Read |
|---|---|
| **Install FinnOS on a PC** | [install.md](install.md) — build the ISO, write the stick, run the installer, first boot |
| **Use the reference machine** | [dell-optiplex.md](dell-optiplex.md) — firmware, hardware, disk names and the verified status |
| **Work on the code** | [development.md](development.md) — the VM, `make deploy`, captures, logs, preview tools |
| **Know what is tested** | [testing.md](testing.md) — the suite, the C tests, CI, and what only hardware proves |
| **Understand the design** | [architecture.md](architecture.md) — processes, layers, data flow, the session, the honest gaps |
| **Drive the system from a script** | [control-protocol.md](control-protocol.md) — the wire, every verb, the replies |
| **Write a `.fui` screen** | [ui-language.md](ui-language.md) — the language, tokens, targets, the compiler |
| **Draw a shell surface** | [design/design-system.md](design/design-system.md) and [design/motion.md](design/motion.md) |
| **Audit the security model** | [security.md](security.md) — authority, the socket, the audit log, the gaps |
| **Update a running machine** | [updates.md](updates.md) — the runtime package and the OTA frontend |
| **Check hardware support** | [hardware.md](hardware.md) — graphics, audio, network, firmware, limits |
| **Fix a machine that will not boot** | [troubleshooting.md](troubleshooting.md) — consoles, logs, recovery |
| **Know what is next** | [roadmap.md](roadmap.md) — now, next, later, and what "done" means |
| **Send a change** | [../CONTRIBUTING.md](../CONTRIBUTING.md) and [../AGENTS.md](../AGENTS.md) |

## The pages in one table

| Page | What it owns |
|---|---|
| [architecture.md](architecture.md) | the two long-lived processes, the layers, the session, the registry, the backends, the gaps |
| [control-protocol.md](control-protocol.md) | the framing, the sockets, the authentication, every verb and its reply |
| [install.md](install.md) | the disc, the USB stick, BIOS and UEFI, the installer, first boot, recovery |
| [dell-optiplex.md](dell-optiplex.md) | the reference x86_64 machine and its firmware |
| [hardware.md](hardware.md) | what the image installs for hardware, and what it does not promise |
| [development.md](development.md) | the dev loop, the VM, the host-side tools, the services |
| [testing.md](testing.md) | the Python suite, the C tests, CI jobs, the untested surface |
| [security.md](security.md) | root-equivalent authority, the one door, the audit log, the boundaries |
| [updates.md](updates.md) | the runtime package, version checks, install, rollback, signing |
| [troubleshooting.md](troubleshooting.md) | boot, session, input, graphics, installer and package failures |
| [ui-language.md](ui-language.md) | `.fui`, the grammar, the tokens, the targets, `.fbin` |
| [design/design-system.md](design/design-system.md) | the material, palette, type, radii, spacing and the anatomy of every surface |
| [design/motion.md](design/motion.md) | the easings, the durations and the motion inventory |
| [roadmap.md](roadmap.md) | the state of each subsystem and the order of work |
| [audits/2026-10-working-os-review.md](audits/2026-10-working-os-review.md) | a point-in-time audit of an earlier tree; historical, not current documentation |

Component-level documentation lives beside the component:

| Page | What it owns |
|---|---|
| [../README.md](../README.md) | the front door |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | how to contribute |
| [../AGENTS.md](../AGENTS.md) | the working agreement for people and agents |
| [../NAMING.md](../NAMING.md) | the naming family |
| [../CHANGELOG.md](../CHANGELOG.md) | what changed, newest first |
| [../base/README.md](../base/README.md) | the base image: what is stripped, what replaces it, how the disc is built |
| [../vm/README.md](../vm/README.md) | the dev VM and the capture scripts |
| [../sdk/README.md](../sdk/README.md) | `fuic`, the Python client and the templates |
| [../sdk/finnsdk/fuic/FORMAT.md](../sdk/finnsdk/fuic/FORMAT.md) | the `.fbin` byte format |
| [../src/pkg/README.md](../src/pkg/README.md) | the package manager, the registry split and the `.finn` package |
| [../mcp/README.md](../mcp/README.md) | the MCP adapter, its tools and its configuration |

## How these pages are written

- **Present tense, declarative.** "The compositor owns the display" is a fact;
  "the compositor will own the display" is a wish.
- **A gap is documented, not hidden.** Every page that describes a subsystem
  ends with what it does not do. If a page and the code disagree, that is a
  bug in the page unless the code is the bug.
- **Examples use placeholders on purpose.** `/dev/sdX` and `diskN` are named
  as placeholders because a real example is a command somebody copies onto the
  wrong disk.
- **A number has one home.** The `VERSION` file is the version. The protocol
  header is the protocol. A page that quotes a number says where it comes from.
- **British spelling** is the house style: colour, behaviour, licence, centre.

## The images

The captures in [images/](images/) are the shell at the reference resolution:
desktop, Start Menu, Control Center, Notification Center, window and first-boot
setup, in both appearances where the surface has one. They are resized before
they are committed; a full-resolution capture is a working artefact, not
documentation.
