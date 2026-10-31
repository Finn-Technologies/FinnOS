# FinnOS

A small, honest desktop operating system built on Arch Linux. FinnOS replaces
the display server, the session and the desktop; it leaves the kernel, systemd,
pacman, mesa and the firmware alone.

![The desktop](docs/images/desktop-light.png)

The userland is written in C++ with a custom compositor and UI runtime.

## What is here

- **finn-comp** — a wlroots 0.20 compositor: DRM or headless, xdg-shell for
  apps, layer-shell for the shell, window movement and resize, maximize,
  screencopy, xdg-output and a systemd watchdog.
- **libfinnui** — the app runtime: a software canvas with rounded shapes,
  Fixel text, SVG icons and small widgets, plus the Wayland client, buffers,
  input and frame callbacks.
- **finn-session** — the user broker: the catalogue, the installed list and
  launch, and nothing else.
- **finn-shell** — a layer-shell overlay with a dock, clock and app menu.
- **Finn Files** and **Settings** — the first native apps.
- **finn-wallpaper** — the background layer, decoding the WebP in the
  userland and dimming it in the dark appearance.
- **finnd** — not written yet. Power, state, packages, updates and the audit
  trail are in [docs/roadmap.md](docs/roadmap.md), not in the tree.

## Build

The build machine is an Arch machine or the dev VM, not a Mac: the compositor
needs the DRM, Wayland, FreeType and wlroots headers.

```sh
make                 # runtime, compositor, session, shell, apps
make test            # host tests: registry, tokens, base wiring
make test-unit    # the C++ unit tests
make lint
vm/deploy.sh         # build, install and restart on the VM
vm/shot.sh out.png   # capture the VM desktop
```

Dependencies: `wlroots0.20`, `libsystemd`, `wayland`, `wayland-protocols`,
`libxkbcommon`, `freetype2`, `libdrm`, `libinput`, `mesa`, `seatd`, `pkgconf`,
and a C++20 compiler.

## Write an app

```cpp
#include <finn/ui.hpp>

class Hello final : public finn::ui::App {
public:
    Hello() : App("Hello", 640, 420) {}
    void draw(finn::ui::Canvas& canvas) override {
        const auto content = content_rect();
        canvas.text(content.x + 24, content.y + 48, "Hello, FinnOS", 24,
                    canvas.theme().ink);
    }
};

int main() {
    Hello app;
    return app.run();
}
```

Build it against `libfinnui`, add one line to `assets/apps.registry`, and the
app is a normal Wayland client. The compositor never learns its name. See
[docs/apps.md](docs/apps.md) and [sdk/template](sdk/template).

## Documentation

- [docs/architecture.md](docs/architecture.md) — the processes, the display,
  the app model and the honest gaps.
- [docs/code-map.md](docs/code-map.md) — the ten-minute tour and the flows.
- [docs/design.md](docs/design.md) — the Figma frames, measurements and Figwright.
- [docs/apps.md](docs/apps.md) — the app contract and the runtime API.
- [docs/motion.md](docs/motion.md) — durations, curves and what moves.
- [docs/session-protocol.md](docs/session-protocol.md) — the launch socket.
- [docs/development.md](docs/development.md) — the dev loop and the VM.
- [docs/testing.md](docs/testing.md) — what is tested and what is not.
- [docs/install.md](docs/install.md) — the base image and the session units.
- [docs/security.md](docs/security.md) — authority and its gaps.
- [docs/hardware.md](docs/hardware.md) — what was verified on hardware.
- [docs/troubleshooting.md](docs/troubleshooting.md) — when it does not come up.
- [docs/roadmap.md](docs/roadmap.md) — what is next.
- [docs/design/](docs/design/) — the architecture contract, milestones and C++
  guide.

## Licence

MIT. See [LICENSE](LICENSE).
