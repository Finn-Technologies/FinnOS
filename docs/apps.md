# Writing Apps for FinnOS

FinnOS supports two first-party native Qt 6 / C++20 app paths via `FinnSDK`, plus seamless integration for Linux, Windows, macOS, OpenHarmony, and Android packages.

## Quickstart with `finn-sdk`

```sh
finn-sdk new hello --name "Hello Finn"
finn-sdk validate hello/finn-app.json
finn-sdk build hello
finn-sdk run hello
finn-sdk register hello
```

See [docs/sdk/getting-started.md](sdk/getting-started.md) and [sdk/template](../sdk/template).

## Option 1: Qt Quick / QML (`FinnSDK::FinnUIQml`, recommended)

Everyday FinnOS apps (Settings, Finn Files, Finn Terminal, Finn Notes, Finn Store, Finn Viewer, Finn Monitor, Finn Calculator, Finn Archive, Finn Capture, Flourish) use `FinnSDK::FinnUIQml` (`import FinnUI 1.0`) with C++20 `QObject` / `QAbstractListModel` controllers:

```cmake
cmake_minimum_required(VERSION 3.22)
project(hello-finn LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(FinnSDK REQUIRED)

add_executable(hello-finn main.cpp)
target_link_libraries(hello-finn PRIVATE FinnSDK::FinnUIQml)
```

In QML, `FinnApplicationWindow` adapts automatically across `Desktop`, `Tablet`, `Mobile`, and `TV` (`AdaptiveFormFactor` in `src/ui/finn/form_factor.hpp`) and exposes all 19 AOT-compiled `FinnUI 1.0` controls (`FinnButton`, `FinnCard`, `FinnGlassPanel`, `FinnNavItem`, `FinnSettingRow`, `FinnSearchField`, `FinnSlider`, `FinnToggle`, `FinnStatCard`, `FinnHeroCard`, `FinnBadge`, `FinnEmptyState`, `FinnToast`, etc.) plus `FinnTheme` and `image://finn-icon/<name>`.

## Option 2: C++20 Widgets (`FinnSDK::FinnUI`)

Pure C++20 widget apps (such as `finn-oobe`, `recovery-mode`, and `finn-wallpaper`) link `FinnSDK::FinnUI` and include `<FinnSDK/FinnSDK.hpp>`:

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
        QStringLiteral("Built with FinnSDK (Qt 6 + C++20).")));
    win.show();
    return app.exec();
}
```

## App catalogue & registration

- **First-party apps** live in `apps/<id>/`, are declared in `assets/apps.registry` (`/usr/share/finn/apps.registry`), and are listed in `/var/lib/finn/installed`.
- **Third-party `FinnSDK` apps** register in `~/.local/share/finn/apps/` (directly via `finn-sdk register` or by installing a signed `.finnapp` bundle via `Finn Store` or `src/compat/finnapp_installer`).
- **Foreign ecosystems** are discovered automatically by `finn-session` from Freedesktop `.desktop` files, Flatpak exports, verified AppImages, Wine prefixes/`windows.conf`, `~/.local/share/finn/macos/Applications`, `~/.local/share/finn/ohos/`, and `~/.local/share/finn/android/`.

See [docs/sdk/api-overview.md](sdk/api-overview.md) and [docs/sdk/publishing.md](sdk/publishing.md) for full API and packaging details.
