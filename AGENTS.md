# AGENTS.md

How to work in this tree. It is written for an agent as much as for a person:
everything here is a rule, and the reason is given so you can tell when it does
not apply.

If you are a person, [CONTRIBUTING.md](CONTRIBUTING.md) is the shorter version.

## Before you change anything

1. Read [`NAMING.md`](NAMING.md).
2. Read [`docs/design/contract.md`](docs/design/contract.md) and
   [`docs/architecture.md`](docs/architecture.md). The "What is not here yet"
   section is part of the product.
3. Read the code you are about to change. A comment explains *why*, never
   *what*.
4. Find the page that describes the change; if there is none, write one, and
   if it is wrong, fix it in the same change.

## The tree

```
src/boot         finn-boot: freestanding UEFI GOP bootloader (x86_64, aarch64), BLS/UKI parser
src/comp         finn-comp: Qt 6 QWaylandQuickCompositor, WorkspaceModel, InputRouter, CPU blur/glass
src/ui           libfinnui & libfinnui_qml (FinnSDK): tokens, theme, springs, widgets, FinnUI 1.0 QML
src/qtplugin     libfinnplatformtheme.so & libfinnstyle.so (Qt 6 platform theme & style plugins)
src/session      finn-session: unified AppRegistry (7 sources), SO_PEERCRED launch broker, SessionClient
src/shell        finn-shell: Qt Quick System UI (ShellCorona, DesktopShell, LockScreen, OOBE flow, search)
src/system       sys_files (/proc, /sys, settings), desktop_services (D-Bus/NM/BlueZ/PipeWire), guest_tools
src/recovery     pure C++20 zero-Qt recovery & dual-boot planner with SHA-512 verification
src/compat       Linux (.desktop/Flatpak/AppImage/ELF/sandbox), Windows (PE/ICO/MSI/Wine/wl_clamp), runners
src/darwin       finn-darwin: clean-room 64-bit Mach-O loader, ObjC/Foundation/AppKit-on-Qt6 runtime
src/ohos         finn-ohos: clean-room OpenHarmony 6.1 LTS (.hap/.hsp/.app, Panda VM, ArkUI-on-Qt6, NAPI)
src/android      finn-android: clean-room Android 17 APK/AXML/DEX inspector & AXML-to-Qt6 layout previewer
src/ota          finn-update & finn_ota: Ed25519 A/B OTA updater, HTTP Range, zstd delta, boot backends
src/common       paths.hpp (shared paths) and liquid_glass.hpp (header-only CPU SDF glass renderer)
apps/<id>        11 registry apps (settings, files, terminal, notes, store, viewer, monitor,
                 calculator, archive, capture, flourish) + wallpaper, oobe, recovery-mode
ui/_tokens.fui   the only source of the palette and design tokens
tools/           tokens.py, finn-sdk, finn-win, finn-display, finn-guest-tools, finn-emulator,
                 finn-oobe-apply, finn-open, finn-cursors, coff2efi.py, check-branding.sh
sdk/             finn-app.schema.json, sdk/template, host SDK tests
repo/            first-party [finnos] pacman repository signing & publishing scripts
ota/             OTA keygen, publisher, grub-ab.cfg, and loopback A/B verification script
base/            ISO builder (mkfinniso), systemd units, Plymouth theme, packages, PKGBUILDs, setup.sh
vm/              dev VM deploy/capture, offline installer (finn-install.sh, finn-chroot.sh)
assets/          fonts, icons, pointers, 3D models, wallpapers, apps.registry, MIME definitions
docs/            the product documentation
tests/           host Python suite and tests/unit (C++20 ASan/UBSan)
```

## The rules

### Never fake a capability

A field with no value is absent, not a plausible default. A button that cannot
act is drawn disabled and refuses presses. A missing font, icon, hardware
device, or runtime is reported as missing. If a feature does not exist, the
code, the documentation, and the reply all say so.

### The compositor does not know apps

`src/comp` contains no app id. An app is a program with an `xdg_toplevel`; the
registry says what it is called; `finn-session` launches it. `make lint` fails
if a known app id appears under `src/comp`. This is the rule that keeps the old
switchboard from growing back.

### One copy of a fact

A path or socket name two modules need lives in `src/common/paths.hpp`. The
display socket is `/run/finn/wayland-0`; the session socket is
`$XDG_RUNTIME_DIR/finn/session.sock`; the catalogue, the installed list, OOBE
markers, boot mark size (`kBootMarkSizePx = 72`), and settings paths are there too.

The palette and design tokens are generated from `ui/_tokens.fui` by
`tools/tokens.py`. `src/ui/generated_tokens.hpp` is checked in so the tree
builds without Python; `make lint` fails when it is stale. Do not edit the
generated header.

### Authority

`finn-comp` runs as root for DRM/EGLFS and input device access (`LIBSEAT_BACKEND=seatd`).
The display socket is `0660 root:finn` in `/run/finn`, because an app running
as `finn` cannot reach root's `$XDG_RUNTIME_DIR`; clients use an absolute
`WAYLAND_DISPLAY`.

`finn-session` runs as `finn`, checks `SO_PEERCRED`, and launches with
`posix_spawnp`. It has no machine authority. There is no `finnd` daemon, no TCP
listener, and no second door.

### The scene has one order

```
background → background layer → bottom layer → app toplevels → top layer → fullscreen → overlay → lock
```

Defined in `src/comp/core/scene_stack.hpp` (`SceneLayer`, 8 layers). Keep all
window and overlay Z-ordering inside `SceneLayer` rather than ad-hoc Z values.

### The protocol is line-oriented

The session protocol is one request line and one reply line (or `OK count=<N>`
followed by `<N>` lines for `APP.LIST`). JSON belongs at external API boundaries
(Flathub, OTA manifests, `finn-emulator report`), never inside `finn-session`. A
reporting verb must not change anything; a changing verb must not pretend to
only report.

### No AI in the code or the image

There is no model, no weights, and no inference code in the tree, and nothing
needs a network call to a model. FinnAI is opt-in, off by default, and not
implemented.

### Naming

Follow [`NAMING.md`](NAMING.md). A binary is lowercase-hyphenated; an app is
"Finn" plus a plain noun except **Settings** and **Flourish**; the libraries
are `libfinnui` and `libfinnui_qml`.

### Documentation is the product

When code and a page disagree, decide which is wrong, fix it in the same
change, and say so. `docs/architecture.md` and `docs/roadmap.md` have sections
for what does not exist; they must stay true.

## The dev loop

The full build machine is a pacman-based Linux host or dev VM.

```sh
make                 # session, compositor, Qt6 plugins, FinnUI, shell, apps, tools, compat runtimes
make boot            # UEFI bootloader (finn-bootx64.efi, finn-bootaa64.efi)
make test            # Python: base, OOBE, OTA, recovery, icons, release
make test-unit       # C++20 unit tests under ASan/UBSan
make lint            # diff hygiene, stale tokens, no app ids in src/comp, branding check
vm/deploy.sh         # sync, build, install, restart the session on the VM
vm/shot.sh out.png   # capture the VM desktop
```

Before you claim a change works, look at it. A compositor or UI change that
compiles is not a change that works. If you cannot test the DRM/KMS path or
input, say so; [`docs/testing.md`](docs/testing.md) keeps that list.

## Tests

`make test` runs the Python suite: base-image wiring, OOBE, OTA, offline
recovery installer, icons, release tooling, and QEMU/OVMF bootloader checks.
`make test-unit` builds and runs the C++20 unit tests under AddressSanitizer
and UndefinedBehaviorSanitizer.

If you add behaviour that can be tested without a display, test it. If you
cannot, say so in the change rather than leaving it implicit.

## Committing

- One change per commit; the body says why.
- Do not commit build output; `.gitignore` covers most of it.
- Do not commit captures; `docs/images/` holds the ones that belong in the
  documentation.
- Update `CHANGELOG.md` when user-visible behaviour changes.
- A docs-only change is a real change; say what was wrong.

## What to be careful about

- **Layer order and click-through masks.** `finn-shell` runs as a fullscreen
  Wayland overlay and uses `QWindow::setMask()` to pass clicks through to apps.
  A missing mask eats every click.
- **Socket ownership.** A socket under `/run/user/0` is unreachable by `finn`;
  the display lives in `/run/finn` (`0660 root:finn`) for that reason.
- **The theme is read, not cached forever.** `finn-comp`, `libfinnui`,
  `libfinnui_qml`, and `libfinnplatformtheme` watch `/var/lib/finn` and
  `~/.config/finn` for live dark/light, accent, and reduced-motion updates. Do
  not add a restart requirement.
- **The token header is generated.** Run `make lint` after touching
  `ui/_tokens.fui`.
- **The two copies of a unit must not drift.** Systemd units in `base/` and
  `base/airootfs/etc/systemd/system/` plus `finn-tmpfiles.conf` must match;
  `base/mkfinniso --check` compares them.
- **A unit whose binary is missing fails at every boot.** `base/setup.sh`
  enables the session only when the required binaries exist. Keep that order.
- **The install medium and the installed machine are different machines.**
  `base/airootfs` is the live root; `base/installed` is the branding an
  installed machine gets. Do not mix them.
- **A report is not a change.** Do not let a reporting verb acquire a side
  effect because it was convenient.
- **Branding check.** `tools/check-branding.sh` (run by `make lint`) rejects
  stray upstream distribution branding in user-visible strings and docs.
