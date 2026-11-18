# FinnOS

A desktop operating system built on a minimal Linux base. FinnOS owns the display compositor, a custom-styled Qt6 + C++20 userland (`FinnSDK`, `libfinnplatformtheme.so`, `libfinnstyle.so`), the desktop shell, native apps, and Linux/Windows app compatibility — while leaving the kernel, systemd, pacman, Mesa, PipeWire, and firmware intact.

![The desktop](docs/images/desktop-light.png)

## What is here

- **finn-comp** — wlroots 0.20 Wayland + on-demand Xwayland compositor: DRM or headless, `xdg-shell`, `wlr-layer-shell`, `wlr-foreign-toplevel-management`, `wlr-screencopy`, `xdg-output`, move/resize/maximize, and a systemd watchdog.
- **FinnUI (`libfinnui`) & Custom Qt6 Plugins** — custom C++20 Qt6 runtime, custom `QPlatformTheme` (`libfinnplatformtheme.so`) and `QProxyStyle` (`libfinnstyle.so`), Figma-measured tokens generated from `ui/_tokens.fui`, live dark/light switching, custom CSD chrome (`FinnWindow`), LayerShell helper (`FinnLayerWindow`), and native component kit.
- **FinnSDK (`<FinnSDK/FinnSDK.hpp>`)** — installable CMake (`find_package(FinnSDK)`) and `pkg-config` (`finnsdk`) SDK plus `finn-sdk` CLI (`new`, `build`, `run`, `register`, `check`) and [`sdk/template`](sdk/template).
- **finn-session** — user broker (`$XDG_RUNTIME_DIR/finn/session.sock`): merges the native catalogue, FinnSDK manifests, Linux `.desktop` / Flatpak apps, and registered Windows `.exe` entries, and launches with `posix_spawn`.
- **finn-shell** — custom Qt6 + `LayerShellQt` desktop shell: floating dock with live running-window indicators, Home Menu, Spotlight Search with inline arithmetic, live Control Center, `org.freedesktop.Notifications` popup stack, and Lock Screen.
- **Native Everyday Apps** — **Settings**, **Finn Files**, **Finn Terminal**, **Finn Notes**, **Finn Store**, **Finn Viewer**, and **Finn Monitor**, plus **finn-wallpaper** and **finn-open**.
- **Linux & Windows Compatibility** — native Wayland, automatic Xwayland for X11 apps, Freedesktop `.desktop` & Flatpak management, and PE32/PE32+ binary inspection + Wine prefix execution on x86_64.

## Build

The compositor and Qt6 userland build on a pacman-based Linux host or in the Lima dev VM:

```sh
make                 # compositor, session, Qt6 plugins, FinnUI, shell, all native apps, tools
make test            # host Python tests: registry, tokens, icons, base-image wiring
make test-unit       # C++ ASan/UBSan unit tests
make lint            # formatting, generated tokens check, compositor app-id ban
sudo make install    # install binaries, Qt6 plugins, FinnSDK, assets, and sdk/template
vm/deploy.sh         # sync, build, install, and restart systemd session on the VM
vm/shot.sh out.png   # capture the VM desktop with grim
```

## Write a native app

```sh
finn-sdk new hello --name "Hello Finn"
finn-sdk build hello
finn-sdk run hello
```

Or in C++20 directly against `<FinnSDK/FinnSDK.hpp>`:

```cpp
#include <FinnSDK/FinnSDK.hpp>

int main(int argc, char** argv) {
    finn::ui::FinnApplication app(argc, argv, QStringLiteral("hello"),
                                  QStringLiteral("Hello"));
    finn::ui::FinnWindow win(QStringLiteral("Hello, FinnOS"),
                             QStringLiteral("app-grid"));
    win.setContentWidget(new finn::ui::FinnEmptyState(
        QStringLiteral("info"),
        QStringLiteral("Hello, FinnOS"),
        QStringLiteral("Built cleanly with FinnSDK (Qt6 + C++20).")));
    win.show();
    return app.exec();
}
```

See [docs/apps.md](docs/apps.md), [docs/sdk.md](docs/sdk.md), and [sdk/template](sdk/template).

## Documentation

- [docs/architecture.md](docs/architecture.md) — processes, custom Qt6 stack, display, compatibility, and honest gaps.
- [docs/code-map.md](docs/code-map.md) — module tour and request flows.
- [docs/apps.md](docs/apps.md) — writing native apps with `FinnSDK`.
- [docs/sdk.md](docs/sdk.md) — `finn-sdk` CLI and CMake packaging.
- [docs/session-protocol.md](docs/session-protocol.md) — the line-oriented launch socket.
- [docs/development.md](docs/development.md) — dev loop and Lima VM workflow.
- [docs/testing.md](docs/testing.md) — automated checks and manual hardware matrix.
- [docs/install.md](docs/install.md) — base image, packages, and systemd units.
- [docs/roadmap.md](docs/roadmap.md) — what comes next.
- [docs/rebrand-checklist.md](docs/rebrand-checklist.md) — upstream base hooks, branding checks, and license verification status.

## Licence

MIT ([LICENSE](LICENSE)) — see [NOTICE](NOTICE) and [CREDITS](CREDITS) for third-party components, fonts, icons, and base system attributions.
