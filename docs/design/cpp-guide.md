# C++ in this tree

FinnOS uses **C++20** across all modules, paired with **Qt 6** (`QtCore`,
`QtGui`, `QtWidgets`, `QtQuick`, `QtQml`, `QtSvg`, `QtWaylandCompositor`) for
compositing, shell, UI, and native apps, plus zero-Qt pure C++20 libraries for
`finn-session`, `src/comp/core`, `src/compat`, `src/recovery`, `src/system`
(`sys_files`, `guest_tools`), and headless compatibility runtimes.

## Toolchain

```
-std=c++20 -Wall -Wextra -Werror
```

Core pure-C++20 modules (`app_registry`, `compat`, `sys_files`, `search`,
`session/client`, `recovery_engine`, `workspace_model`, `input_router`,
`spring`, `darwin`, `ohos`, `android`, `boot`) compile standalone under
`-fsanitize=address,undefined` via `make test-unit`.

## Ownership & style rules

- Prefer value types, `std::string`, `std::vector`, `std::optional`,
  `std::unique_ptr`, `std::string_view`, and Qt parent-child `QObject`
  ownership (`parent` parameter on widgets/timers/models).
- Use `src/common/paths.hpp` for shared paths, sockets, and constants.
- Palette, spacing, radii, and motion values come from `ui/_tokens.fui` via
  `FinnTokens` / `FinnThemeManager` / `FinnThemeBridge`; never hardcode
  unrelated colours inside widgets or QML controls.
- In Qt 6 apps and SDK code:
  - Prefer **QML (`FinnSDK::FinnUIQml`, `import FinnUI 1.0`, `FinnApplicationWindow`)**
    for new everyday apps, backed by C++20 `QObject` / `QAbstractListModel`
    controllers.
  - Use **`FinnSDK::FinnUI` (`FinnWindow` / `FinnLayerWindow`)** for pure
    C++ widget apps such as `finn-oobe`, `recovery-mode`, and `finn-wallpaper`.
  - Connect to `FinnThemeManager::instance()` (`themeChanged`) whenever custom
    painting reads `FinnTokens`.
  - Never block the UI thread; use `QProcess`, `QSocketNotifier`,
    `QNetworkAccessManager`, or worker signals (`finn-store`, `finn-terminal`,
    `finn-files`, `finn-settings`).
- Comments explain *why*, never *what*.
