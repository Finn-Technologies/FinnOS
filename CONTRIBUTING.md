# Contributing

FinnOS is a small operating system and the tree is small on purpose. A change
fits the shape of what is there; it does not need to be the largest change.

## Read first

1. [AGENTS.md](AGENTS.md) — the working agreement, for people and agents.
2. [NAMING.md](NAMING.md) — the naming family.
3. [docs/rewrite/contract.md](docs/rewrite/contract.md) — the five programs,
   authority, the app contract and the v1 non-goals.
4. [docs/architecture.md](docs/architecture.md) — what exists and what does
   not.

## Set up

The build machine is Arch or the dev VM. The host can be the editor.

```sh
sudo pacman -S base-devel pkgconf wlroots0.20 libsystemd wayland \
    wayland-protocols libxkbcommon freetype2 fontconfig libdrm libinput \
    mesa seatd grim
make
make test
make test-rewrite
make lint
```

`vm/deploy.sh` builds, installs and restarts the VM session. `vm/shot.sh`
captures it.

## What "done" means

1. It builds with no new warnings.
2. `make test`, `make test-rewrite` and `make lint` pass.
3. You looked at the pixels if the change touches them: a capture is the proof.
4. The page that describes the behaviour changed in the same commit.
5. The commit body says why.
6. If you could not test the DRM path, input, or a toolkit, say so.

## Style

C++20, and a deliberately small subset: classes, `std::string`, `std::vector`,
`std::optional`, `std::unique_ptr`, `std::string_view`. No owning raw
pointers, no `new`/`delete`, no template metaprogramming, no exceptions across
module boundaries. One class per file pair. See
[docs/rewrite/cpp-guide.md](docs/rewrite/cpp-guide.md).

- Four spaces, braces on the same line, `snake_case` names, `PascalCase`
  types, namespace `finn`.
- A comment explains why, never what.
- Shell scripts are `#!/bin/bash` with `set -euo pipefail`.
- British spelling: colour, behaviour, licence, centre.

## The app rule

The compositor must not learn app ids. An app is a program with an
`xdg_toplevel`; the registry says what it is called; `finn-session` launches
it. `make lint` fails if a known id appears under `src/comp`. Do not add a
special case for an app to the window manager.

## The authority rule

The display socket is `/run/finn/wayland-0`, `0660 root:finn`. The session
socket is user-only and checks `SO_PEERCRED`. There is no TCP listener.
`finn-session` runs as `finn` and has no machine authority; do not move app
launch into a root process.

## Adding an app

Put it in `apps/<id>/`, add its registry line, and add the id to
`/var/lib/finnpkg/installed` for the image. It must link `libfinnui` or be a
normal foreign Wayland client; both are first class.

## Documentation

The documentation is part of the product. Say what is missing. The honest
gaps live in [docs/architecture.md](docs/architecture.md),
[docs/security.md](docs/security.md) and [docs/roadmap.md](docs/roadmap.md);
keep them true.

## Security

Do not open a public issue for a privilege-boundary problem.
[docs/security.md](docs/security.md) is the model to test against.

## Licence

MIT. By contributing you agree your contribution is licensed the same way.
