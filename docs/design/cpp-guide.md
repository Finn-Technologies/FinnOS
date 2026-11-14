# C++ in this tree

FinnOS uses **C++20** across all modules, paired with **Qt6** (`QtCore`,
`QtGui`, `QtWidgets`, `QtSvg`) for UI/shell/apps and clean C++20 + C wrappers
for `wlroots 0.20` (`src/comp`) and `finn-session`.

## Toolchain

```
-std=c++20 -Wall -Wextra -Werror
```

(`src/comp` and `src/session` also pass `-Wpedantic`.)
Core pure-C++ modules (`app_registry`, `compat`, `sys_files`, `search`,
`session/client`) compile standalone under `-fsanitize=address,undefined` via
`make test-unit`.

## Ownership & style rules

- Prefer `std::string`, `std::vector`, `std::optional`, `std::unique_ptr`, and Qt
  parent-child QObject ownership (`parent` parameter on widgets/timers).
- Use `src/common/paths.hpp` for shared paths and sockets.
- Palette values come from `ui/_tokens.fui` via `FinnTokens` /
  `FinnThemeManager`; never hardcode unrelated colors inside widgets.
- In Qt6 apps and SDK code:
  - Derive top-level app windows from `finn::ui::FinnWindow` (or
    `finn::ui::FinnLayerWindow` for `layer-shell` surfaces).
  - Connect to `FinnThemeManager::instance()` (`themeChanged`) whenever custom
    painting or stylesheets read `FinnTokens`.
  - Never block the UI thread; use `QProcess`, `QSocketNotifier`, or worker
    signals (`finn-store`, `finn-terminal`, `finn-files`).
- Comments explain *why*, never *what*.
