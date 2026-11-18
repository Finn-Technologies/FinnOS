# Flourish

**Flourish** is a Chromium-based web browser for FinnOS built with the official **FinnSDK** (`FinnSDK::FinnUI`, `<FinnSDK/FinnSDK.hpp>`) and `Qt6::WebEngineWidgets`, combining an Arc-inspired vertical workspace layout with FinnOS's FigmaSplit chrome and live token palette.

## Features

- **FinnWindow (`ChromeMode::FigmaSplit`)**: Frameless Wayland CSD with rounded corners, hairline border, floating `FinnWindowControls`, and collapsible left sidebar (`Ctrl+S` / `Ctrl+B`).
- **Arc-style Sidebar (`SidebarPanel`)**:
  - Back, forward, reload/stop, and downloads drawer trigger at the top.
  - Clickable URL pill (`Ctrl+L`) showing HTTPS security status (`shield`) and current host.
  - **Favorites** 3-column quick-access tile grid.
  - **Pinned** and **Today** tab sections with favicons, loading rings (`FinnProgressRing`), and right-click context actions (Pin, Favorite, Split View, Duplicate, Reload, Close).
  - **Spaces** switcher at the bottom (Personal, Work, Dev, plus custom Spaces via `FinnDialog::prompt`), persisted to `~/.config/finn/flourish-session.json`.
- **Command Bar (`Ctrl+T` / `Ctrl+L`)**: Centered `FinnGlassPanel` overlay with `FinnSearchField` for instant URL navigation, Google search, tab switching, Space switching, and browser actions.
- **Split View (`Ctrl+Shift+D`)**: Side-by-side `QSplitter` view of two tabs.
- **Find in Page (`Ctrl+F`)** & **Downloads**: Floating `FinnGlassPanel` overlays with real `QWebEnginePage::findText` match counts and `QWebEngineDownloadRequest` tracking to `~/Downloads` with `FinnToast` notifications.
- **Live Theme Sync**: Follows `FinnThemeManager::instance().themeChanged` for bone/ink surfaces, `flourish://home` start page, and `QWebEngineSettings::ForceDarkMode`.

## Building with FinnSDK

Flourish builds both standalone via `finn-sdk` and inside the FinnOS CMake tree when `qt6-webengine` (`Qt6::WebEngineWidgets`) is installed:

```sh
finn-sdk build apps/flourish
finn-sdk run apps/flourish
```

Or with `make` inside `apps/flourish/`:

```sh
make -C apps/flourish
make -C apps/flourish register
```
