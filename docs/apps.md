# Writing a native app with FinnSDK

FinnOS applications are standard Qt6 + C++20 GUI programs built against **`FinnSDK::FinnUI`** (`libfinnui`), automatically styled with FinnOS's Figma-matched platform theme and widgets, and launched by `finn-session`.

## Quick start with `finn-sdk`

```sh
finn-sdk check
finn-sdk new my-app --name "Finn Hello"
finn-sdk build my-app
finn-sdk run my-app
```

Register your built binary in the local user launcher (`~/.local/share/finn/apps/`):

```sh
finn-sdk register my-app/my-app.desktop
```

## Minimal C++20 example

```cpp
#include <FinnSDK/FinnSDK.hpp>

int main(int argc, char** argv) {
    finn::ui::FinnApplication app(
        argc, argv, QStringLiteral("my-app"), QStringLiteral("Finn Hello"));

    finn::ui::FinnWindow window(
        QStringLiteral("Finn Hello"), QStringLiteral("app-grid"));
    window.resize(760, 520);

    auto* empty = new finn::ui::FinnEmptyState(
        QStringLiteral("info"),
        QStringLiteral("Hello from FinnSDK"),
        QStringLiteral("Custom frameless window chrome, Fixel Display typography, and live theme switching included."));
    window.setContentWidget(empty);
    window.show();
    return app.exec();
}
```

## Building with CMake (`sdk/template`)

```cmake
cmake_minimum_required(VERSION 3.22)
project(my-finn-app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(FinnSDK REQUIRED)

add_executable(my-finn-app main.cpp)
target_link_libraries(my-finn-app PRIVATE FinnSDK::FinnUI)
```

See [`sdk/template`](../sdk/template) for a complete starter with CMake, Makefile (`pkg-config finnsdk`), and Freedesktop `.desktop` manifest.

## Public API highlights (`<FinnSDK/FinnSDK.hpp>`)

| Class / Header | Role |
|---|---|
| `finn::ui::FinnApplication` | Configures Wayland display, app id, Fixel Display fonts, FinnOS `QPalette`, and theme polling |
| `finn::ui::FinnWindow` | Custom frameless CSD Wayland window with Figma traffic-light controls, collapsible sidebar, edge resize, and toast overlay |
| `finn::ui::FinnLayerWindow` | `LayerShellQt` panel wrapper with explicit click-through input masking (`setInputRegions`) |
| `finn::ui::FinnThemeManager` | Live light/dark/accent/scale/reduced-motion state synced with `~/.config/finn/settings` |
| Widgets (`Components.hpp`) | `FinnCard`, `FinnGlassPanel`, `FinnLabel`, `FinnButton`, `FinnIconButton`, `FinnToggle`, `FinnSlider`, `FinnSearchField`, `FinnTextField`, `FinnProgressBar`, `FinnProgressRing`, `FinnSidebar`, `FinnNavItem`, `FinnSettingRow`, `FinnBadge`, `FinnDivider`, `FinnTabBar`, `FinnBreadcrumbs`, `FinnEmptyState`, `FinnToast`, `FinnDialog` |
| `finn::SessionClient` | Typed client for `finn-session` (`list`, `launch`, `open_target`, `run_windows`, `compat_status`, `power_action`) |

## App contract & honesty rules

- Do not assume elevated privileges or private compositor sockets; communicate with the system via Wayland, XDG portals/DBus, or `SessionClient`.
- Never fake hardware or service availability: use `FinnButton::setEnabled(false)` and `FinnBadge` status indicators honestly when a capability is missing.
- Keep `src/comp` free of application names.
