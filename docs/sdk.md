# FinnSDK

**FinnSDK** is the official C++20 + Qt6 application SDK for FinnOS (`0.2.0`). Rather
than shipping unmodified default Qt styling, FinnOS customizes Qt at three levels
so first-party and third-party native apps share the exact palette, typography,
motion, and Wayland CSD chrome designed for FinnOS:

1. **`FinnSDK::FinnUI` (`libfinnui.so`)** — `FinnApplication`, `FinnWindow`,
   `FinnLayerWindow`, `FinnThemeManager`, `FinnTokens`, spring/easing motion
   primitives, `SessionClient`, and custom widgets (`FinnCard`, `FinnButton`,
   `FinnToggle`, `FinnSlider`, `FinnSearchField`, `FinnSidebar`,
   `FinnSettingRow`, `FinnProgressBar`, `FinnToast`, `FinnDialog`,
   `FinnEmptyState`, `FinnDivider`, `FinnLabel`).
2. **`finnplatformtheme` (`libfinnplatformtheme.so`)** — Qt6 platform theme
   plugin (`QT_QPA_PLATFORMTHEME=finn`) supplying Fixel Display fonts, light/dark
   palettes from `ui/_tokens.fui`, and `FinnThemeManager`-backed hints.
3. **`finnstyle` (`libfinnstyle.so`)** — Qt6 `QProxyStyle` plugin
   (`QT_STYLE_OVERRIDE=finn`) rounding sliders, inputs, menus, scrollbars, and
   focus rings for standard Qt widgets too.

## Documentation

- [Getting Started](sdk/getting-started.md) — toolchain check, project scaffolding, build, run, and registration
- [API Overview](sdk/api-overview.md) — C++20/Qt6 headers, classes, signals, tokens, springs, and `SessionClient`
- [Packaging & Publishing](sdk/publishing.md) — `finn-app.json` schema, `.finnapp` deterministic archives, Ed25519 signing, and v1 permission model (declared-not-enforced)
- [Form-Factor Simulator](sdk/simulator.md) — `finn-sdk simulate` (`phone`, `tablet`, `desktop`) and `libfinnui` responsive proposal
- [ABI & Versioning Policy](sdk/abi-versioning.md) — `0.2.0` / `SOVERSION 0` compatibility and current limits
- [SDK Completeness Audit](sdk/audit.md) — baseline inventory, filled tooling, and honest list of what is still missing

## CLI workflow (`finn-sdk`)

```sh
finn-sdk check                              # verify toolchain, Qt6 plugins, CMake config, signing tools
finn-sdk new myapp --name "My Finn App"     # scaffold CMakeLists.txt, Makefile, main.cpp, .desktop, finn-app.json, CI
finn-sdk validate myapp                     # validate finn-app.json against sdk/schema/finn-app.schema.json
finn-sdk build myapp                        # cmake -G Ninja build
finn-sdk run myapp                          # run against /run/finn/wayland-0 with Finn theme
finn-sdk simulate --form-factor tablet myapp # simulate tablet viewport (use --headless with finn-comp)
finn-sdk keygen ~/.config/finn-dev/pub      # generate Ed25519 keypair outside the repo
finn-sdk package myapp --key ~/.config/finn-dev/pub.key.pem  # build deterministic signed .finnapp
finn-sdk verify myapp/dist/myapp-0.1.0-x86_64.finnapp --pubkey ~/.config/finn-dev/pub.pub.pem
finn-sdk register myapp "My Finn App" "$PWD/myapp/build/finn-myapp"
```

## CMake integration

Installed to `${PREFIX}/lib/cmake/FinnSDK` (`FinnSDKConfig.cmake`):

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(FinnSDK REQUIRED)

add_executable(finn-myapp main.cpp)
target_link_libraries(finn-myapp PRIVATE FinnSDK::FinnUI)
```

Headers install under `<FinnSDK/*.hpp>` (`#include <FinnSDK/FinnSDK.hpp>`).

## Minimal application

```cpp
#include <FinnSDK/FinnSDK.hpp>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char** argv) {
    finn::ui::FinnApplication app(argc, argv, QStringLiteral("myapp"),
                                  QStringLiteral("My App"));
    finn::ui::FinnWindow window(QStringLiteral("My App"),
                                QStringLiteral("native-apps"));
    window.resize(740, 480);

    auto* body = new QWidget(&window);
    auto* lay = new QVBoxLayout(body);
    lay->addWidget(new finn::ui::FinnLabel(
        QStringLiteral("Built with FinnSDK"),
        finn::ui::FinnLabel::Role::TitleLarge));
    window.setContentWidget(body);
    window.show();
    return app.exec();
}
```

## Tokens and live theming

`ui/_tokens.fui` is the single palette source compiled by `tools/tokens.py` into
`src/ui/generated_tokens.hpp`. Do not edit `generated_tokens.hpp` by hand.
`FinnThemeManager::instance()` watches `$HOME/.config/finn/settings` (and
`FINN_DARK` / `FINN_ACCENT` / `FINN_REDUCED_MOTION` overrides) and emits
`themeChanged(const FinnTokens&)` immediately on changes without restarts.

## Compatibility tiers

1. **Native (`FinnSDK`)** — Highest integration: custom `FinnWindow` CSD, live
   token styling, Fixel Display typography, `finn-session` launch support.
2. **Linux Desktop** — Wayland (`Qt6`, `GTK`), Xwayland (`DISPLAY`), and
   Flatpak apps discovered from XDG `.desktop` entries via `finn::scan_desktop_dirs`.
3. **Windows (`.exe`)** — PE headers inspected (`finn::inspect_pe`) and launched
   in isolated per-app Wine prefixes when Wine (`wine`/`wine64`) is present.
