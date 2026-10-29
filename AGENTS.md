# AGENTS.md

How to work in this tree. It is written for an agent as much as for a person:
everything here is a rule, and the reason is given so you can tell when it does
not apply.

If you are a person, [CONTRIBUTING.md](CONTRIBUTING.md) is the shorter version.

## Before you change anything

1. Read [`NAMING.md`](NAMING.md).
2. Read [`docs/rewrite/contract.md`](docs/rewrite/contract.md) and
   [`docs/architecture.md`](docs/architecture.md). The "what is not here yet"
   section is part of the product.
3. Read the code you are about to change. A comment explains *why*, never
   *what*.
4. Find the page that describes the change; if there is none, write one, and
   if it is wrong, fix it in the same change.

## The tree

```
src/comp         finn-comp: wlroots 0.20, DRM/headless, xdg-shell, layer-shell
src/ui           libfinnui: canvas, text, vector, widgets, Wayland client, theme
src/session      finn-session: catalogue, installed list, launch, client
src/shell        finn-shell: dock, clock, app menu (layer-shell)
src/common       paths shared by two modules, and only that
apps/files       Finn Files
apps/settings    Settings
ui/_tokens.fui   the only source of the palette
tools/tokens.py  ui/_tokens.fui -> src/ui/generated_tokens.hpp
sdk/template     the app a contributor starts from
base             the Arch base, units, installer, ISO builder
vm               the dev VM: deploy, capture
assets           fonts, icons, wallpapers, apps.registry
docs             the product documentation
tests            the host suite and tests/rewrite
```

The old C userland is on the `pre-rewrite` tag. It is not a fallback and it is
not built.

## The rules

### Never fake a capability

A field with no value is absent, not a plausible default. A button that cannot
act is drawn disabled and refuses presses. A missing font, icon or feed is
reported as missing. If a feature does not exist, the code, the documentation
and the reply all say so.

### The compositor does not know apps

`src/comp` contains no app id. An app is a program with an `xdg_toplevel`; the
registry says what it is called; `finn-session` launches it. `make lint` fails
if a known app id appears under `src/comp`. This is the rule that keeps the old
switchboard from growing back.

### One copy of a fact

A path or socket name two modules need lives in `src/common/paths.hpp`. The
display socket is `/run/finn/wayland-0`; the session socket is
`$XDG_RUNTIME_DIR/finn/session.sock`; the catalogue, the installed list and the
settings path are there too.

The palette is generated from `ui/_tokens.fui` by `tools/tokens.py`.
`src/ui/generated_tokens.hpp` is checked in so the tree builds without Python;
`make lint` fails when it is stale. Do not edit the generated header.

### Authority

`finn-comp` is root because DRM and evdev need it. It opens devices through
`seatd` (`LIBSEAT_BACKEND=seatd`), not directly. The display socket is
`0660 root:finn` in `/run/finn`, because an app running as `finn` cannot reach
root's `$XDG_RUNTIME_DIR`; clients use an absolute `WAYLAND_DISPLAY`.

`finn-session` runs as `finn`, checks `SO_PEERCRED`, and launches with
`posix_spawn`. It has no machine authority. There is no `finnd` in v1, no TCP
listener and no second door.

### The scene has one order

```
background → background layer → bottom layer → app toplevels → top layer → overlay
```

Layer surfaces go to `layer_trees_[current.layer]`; app toplevels go to
`toplevel_tree_`. Do not add a surface to `scene_->tree` directly; that is how
the menu ended up behind an app window.

### The protocol is line-oriented

The session protocol is one request line and one reply line. JSON belongs at
the MCP boundary on the host, never inside a daemon. A reporting verb must not
change anything; a changing verb must not pretend to only report.

### No AI in the code or the image

There is no model, no weights and no inference code in the tree, and nothing
needs a network call to a model. FinnAI is opt-in, off by default and not
implemented.

### wlroots is C

`src/comp/wlroots.hpp` pre-includes the standard headers and erases `static`
and `namespace` for the duration of the C includes. That is the only place
allowed to do it. If wlroots ships clean headers, delete the shim.

### Naming

Follow [`NAMING.md`](NAMING.md). A binary is lowercase-hyphenated; an app is
"Finn" plus a plain noun except Settings; the library is `libfinnui`.

### Documentation is the product

When code and a page disagree, decide which is wrong, fix it in the same
change, and say so. `docs/architecture.md` and `docs/roadmap.md` have sections
for what does not exist; they must stay true.

## The dev loop

The build machine is the VM, not the host.

```sh
make                 # runtime, compositor, session, shell, apps
make test            # Python: base, catalogue, tokens
make test-rewrite    # C++ unit tests under ASan/UBSan
make lint            # style, stale tokens, no app ids in src/comp
vm/deploy.sh         # build, install, restart the session on the VM
vm/shot.sh out.png   # capture with grim
```

Before you claim a change works, look at it. A compositor change that compiles
is not a compositor change that works. If you cannot test the DRM path or
input, say so; [`docs/testing.md`](docs/testing.md) keeps that list.

## Tests

`make test` runs the Python suite: the base-image wiring, the catalogue and the
generated tokens. `make test-rewrite` builds the registry test with
AddressSanitizer and UndefinedBehaviorSanitizer. There is no `tests/c` any
more; the compositor is verified on the VM.

If you add behaviour that can be tested without a display, test it. If you
cannot, say so in the change rather than leaving it implicit.

## Committing

- One change per commit; the body says why.
- Do not commit build output; `.gitignore` covers most of it.
- Do not commit captures; `docs/images/` holds the ones that belong in the
  documentation.
- Update `CHANGELOG.md` when the user-visible behaviour changes.
- A docs-only change is a real change; say what was wrong.

## What to be careful about

- **Layer order and input regions.** A full-screen overlay without an input
  region eats every click.
- **Socket ownership.** A socket under `/run/user/0` is unreachable by `finn`;
  the display lives in `/run/finn` for that reason.
- **Scene node lifetime.** Every listener belongs to a struct with a
  destructor that detaches it; the first headless run found a wlroots
  assertion when one did not.
- **The theme is read, not cached forever.** The compositor re-reads it once a
  second; `libfinnui` every few frames. Do not add a restart requirement.
- **The token header is generated.** Run `make lint` after touching
  `ui/_tokens.fui`.
- **The two copies of a unit must not drift.** `finn-comp.service`,
  `finn-session.service`, `finn-shell.service`, `finn-failure.service` and
  `finn-tmpfiles.conf` exist in `base/` and `base/airootfs/`; `mkfinniso
  --check` compares them.
- **A unit whose binary is missing fails at every boot.** `base/setup.sh`
  enables the session only when all three binaries exist. Keep that order.
- **The install medium and the installed machine are different machines.**
  `base/airootfs` is the live root; `base/installed` is the branding an
  installed machine gets. Do not mix them.
- **A report is not a change.** Do not let a reporting verb acquire a side
  effect because it was convenient.
