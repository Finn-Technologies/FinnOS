# Contributing

FinnOS is a focused operating system and the tree is kept coherent on purpose. A change should fit the shape of what is there and be honest about what it implements and what it does not.

## Read first

1. [AGENTS.md](AGENTS.md) — the working agreement, for people and agents.
2. [NAMING.md](NAMING.md) — the naming family.
3. [docs/design/contract.md](docs/design/contract.md) — the architecture contract, authority boundaries, app contract, and non-goals.
4. [docs/architecture.md](docs/architecture.md) — what exists right now and what does not.

## Set up

The build machine is a pacman-based Linux host (such as the dev VM). Your host machine can be the editor.

```sh
sudo pacman -S base-devel cmake ninja pkgconf \
    qt6-base qt6-declarative qt6-wayland qt6-svg qt6-shadertools \
    qt6-5compat layer-shell-qt \
    wayland wayland-protocols libxkbcommon libinput libdrm mesa seatd \
    systemd-libs freetype2 fontconfig zlib zstd
make
make test
make test-unit
make lint
```

Optional packages: `qt6-webengine` (builds `apps/flourish`), `clang` + `lld` (builds `src/boot` UEFI bootloader via `make boot`).

`vm/deploy.sh` syncs, builds, installs, and restarts the VM session. `vm/shot.sh` captures it.

## What "done" means

1. It builds cleanly (`-Wall -Wextra -Werror` on C++20 targets).
2. `make test`, `make test-unit`, and `make lint` pass.
3. You looked at the pixels if the change touches UI or compositing: a capture is the proof.
4. The documentation page that describes the behaviour changed in the same commit.
5. The commit body says why.
6. If you could not test the DRM/KMS path, input, or a compatibility runtime, say so explicitly.

## Style

C++20 with a clean, predictable subset: value types, `std::string`, `std::vector`, `std::optional`, `std::unique_ptr`, `std::string_view`, and standard Qt 6 parent-child ownership for `QObject`/`QWidget` hierarchies. No unowned raw pointers, no template metaprogramming, no exceptions across module boundaries. See [docs/design/cpp-guide.md](docs/design/cpp-guide.md).

- Four spaces, braces on the same line, `snake_case` names, `PascalCase` types, namespace `finn`.
- A comment explains *why*, never *what*.
- Shell scripts are `#!/bin/bash` with `set -euo pipefail`.
- British spelling in prose: colour, behaviour, licence, centre.

## The app rule

The compositor must not learn app ids. An app is a program opening an `xdg_toplevel`; the registry says what it is called; `finn-session` launches it. `make lint` fails if a known app id appears under `src/comp`. Do not add a special case for an app to the compositor.

## The authority rule

The display socket is `/run/finn/wayland-0`, `0660 root:finn`. The session socket is user-only (`0600`) and checks `SO_PEERCRED`. There is no TCP listener. `finn-session` runs as `finn` and has no machine authority; do not move app launch into a root process.

## Adding an app

Put it in `apps/<id>/`, wire it in `apps/CMakeLists.txt` and `Makefile`, add its line to `assets/apps.registry`, and add the id to `/var/lib/finn/installed` for the image. Native apps link `FinnSDK::FinnUIQml` (`import FinnUI 1.0`, `FinnApplicationWindow`) or `FinnSDK::FinnUI`; normal foreign Wayland clients are also first class.

## Documentation

The documentation is part of the product. Say what is missing. The honest gaps live in [docs/architecture.md](docs/architecture.md), [docs/security.md](docs/security.md), and [docs/roadmap.md](docs/roadmap.md); keep them true.

## Security

Do not open a public issue for a privilege-boundary problem. [docs/security.md](docs/security.md) is the model to test against.

## Licence

MIT. By contributing you agree your contribution is licensed the same way.
