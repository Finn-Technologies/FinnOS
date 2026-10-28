# Testing

FinnOS is tested in three layers, and each one answers a different question:

| Layer | Question it answers | Command |
|---|---|---|
| the Python suite | do the registry, the protocol, the compiler, the client, the package manager and the tree's invariants behave? | `make test` |
| the C tests | do the framing, the settings store, the rasteriser, the blur, the taskbar, the `.fbin` reader, the validators, the Settings metadata and the drawing primitives behave, under sanitizers? | `make test-c` |
| a capture | does the shell look right? | `vm/shot.sh`, `scripts/verify.sh` |

There is no fourth layer that pretends to be all three. A compositor change that
compiles is not a compositor change that works, and this page says so in the
same place it says what the suites cover.

## Running it

```sh
make test                 # the Python suite; no device, no dependencies
make test-c               # the C tests; a compiler, no device
make lint                 # style
scripts/verify.sh         # tests, lint, deploy, captures, journal
```

`make test` runs `python3 -m unittest discover -v -s tests -t .`. It uses only
the Python standard library, so it runs on macOS, Linux and CI without a wheel.

`make test-c` builds nine binaries in `tests/c/` with
`-fsanitize=address,undefined` by default. `make test-c SAN=` builds the same
tests without sanitizers on a compiler that does not have them.

`scripts/verify.sh` is the end-to-end loop: it runs the suite, the style
checks, deploys to `FINN_HOST`, asks `screen.shell` for the state, captures the
desktop and the Start Menu in both appearances, and greps the journal for
`error`, `failed` and `segmentation`. It needs a running target; that is the
point.

## The Python suite

At the time of writing: **270 tests**, of which 3 are skipped unless an ISO and
QEMU are supplied. The suite is the source of truth for the number; it grows
with the code.

| File | What it covers |
|---|---|
| `test_compiler.py`, `test_lexer.py`, `test_parser.py`, `test_theme.py`, `test_fbin.py` | the `.fui` language and every source in `ui/` and `apps/`; the `.fbin` writer, reader and round trip |
| `test_cli.py` | `fuic` and `finn-sdk` command lines |
| `test_client.py` | `FinnClient` against a fake control plane, including errors, absent fields and the direct socket |
| `test_mcp.py` | the adapter's tool list, JSON-RPC, app/window/setup tools and transport |
| `test_finnpkg.py` | the package manager's accepted and refused archives, ids and install list |
| `test_base.py` | the base image's package lists, units and installer wiring |
| `test_chroot_password.py` | the chroot's generated-password path |
| `test_icons.py` | every icon the shell loads at runtime exists in the repo |
| `test_release.py` | the runtime package the OTA frontend installs: name, version, assets |
| `test_vg.py` | the SVG engine builds and the vector tests pass |
| `test_x86_boot.py` | ISO profile, package lists, UEFI entries, and — with `FINNOS_ISO` — QEMU boot and an opt-in full install |
| `fakeserver.py`, `support.py` | the fake control plane and the shared helpers |

The fake control plane is a small, honest responder: it answers the verbs the
client tests need and refuses the rest. It is not a model of `finnd`, and a test
that passes against it has proved the client, not the daemon.

## The C tests

| Binary | What it covers |
|---|---|
| `test_finnconn` | line framing, against a socket pair |
| `test_finnsettings` | the versioned settings store, atomic writes and refusals |
| `test_finnvg` | the vector rasteriser and the SVG parser |
| `test_finnblur` | the live frosted backdrop and its scale |
| `test_finntaskbar` | the taskbar pin list |
| `test_finnfbin` | the `.fbin` reader |
| `test_finncompat` | the Flatpak and Wine validators, argv launch and the systemd job ids |
| `test_finnsettingsapp_pages` | Settings page navigation metadata |
| `test_finndraw` | the shared drawing primitives and the palette |

They are built with the sanitizers on purpose: a bounds bug in a framing reader
or a line-oriented settings parser is exactly the kind that a normal run does
not show. On macOS the Settings page test uses compile-only shims for the Linux
headers it does not need at runtime.

## CI

| Workflow | What it runs |
|---|---|
| `.github/workflows/ci.yml` | `make test`, `make lint`, `make`, `make tools`, and `git diff --exit-code` (the tree must be clean after a build) |
| `.github/workflows/iso.yml` | the base checks, the native ISO build in a privileged Arch container, the QEMU boot to the installer under BIOS and UEFI, the menu capture, and — on demand or with `full_install` — an unattended install and a boot of the installed disk |
| `.github/workflows/runtime.yml` | the tag/`VERSION` agreement, the `finnos-runtime` package and its checksum, published to the release |

The ISO workflow runs on every `v*` tag and on demand. The full install is
opt-in because it is slow; the boot test is not. A disk that installs but does
not boot fails the run instead of passing on the installer's own checks.

## What is not tested

These are the honest gaps. Do not assume a green run covers them.

- **The compositor's pixels.** DRM/KMS, the shell surfaces, the blur, the
  animation and the two backends are verified by capturing the frame and
  looking at it. There is no headless render target a test can assert against:
  the Wayland host can be driven by a real client, but that is a manual
  walkthrough, not a suite.
- **Input on real devices.** `finnkbd` and `finnpointer` are evdev readers; the
  suite cannot press a real key. A keyboard that stops after a hotplug, or a
  pointer that drifts, is a hardware observation.
- **The installer on real hardware.** CI installs to a virtual disk and boots
  it; it does not install to an NVMe disk, a USB stick or a machine with Secure
  Boot on.
- **Flatpak and Wine beyond their validators.** A GNOME Calculator was launched
  into a standalone Wayland test server on an ARM VM; a booted FinnOS
  search → install → open walkthrough, and a real Wine GUI, remain unverified.
- **Updates across a version boundary.** The runtime package's contents and the
  version agreement are tested; applying one to a long-running machine is not.
- **Suspend, resume and multi-monitor.** Not implemented or not verified.

If you add a behaviour that can be tested without a display, test it. If you
cannot, say so in the pull request instead of leaving it implicit.

## Adding a test

- **Python:** put it in the matching `tests/test_*.py`, use `unittest`, and use
  `tests/support.py` for temporary trees and the fake server. No third-party
  imports.
- **C:** add a binary to `tests/c/Makefile`'s `BINS` and `run` target, keep it
  sanitizer-clean, and make it return non-zero on failure.
- **A tree invariant:** `test_base.py`, `test_icons.py` and `test_release.py`
  are the place for "these two files must agree" and "the image must carry
  this". They are cheap and they catch drift.
- **A visual change:** add a capture to `scripts/verify.sh` only if the state
  can be reached deterministically through `finnctl`; otherwise a capture in
  the pull request is the proof, and the screenshot is not committed unless it
  belongs in `docs/images/`.
- **A QEMU test:** name the ISO through `FINNOS_ISO` and make the test skip
  cleanly when it is absent, as `test_x86_boot.py` does.

The style and commit rules are in [CONTRIBUTING.md](../CONTRIBUTING.md).
