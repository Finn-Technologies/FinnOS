# FinnSDK API Overview

Reference for `FinnSDK::FinnUIQml` (`libfinnui_qml.so.1.0.0`, QML `import FinnUI 1.0`) and `FinnSDK::FinnUI` (`libfinnui.so.1.0.0`, `<FinnSDK/*.hpp>`).

## QML API (`FinnSDK::FinnUIQml`, `import FinnUI 1.0`)

Include `<FinnSDK/FinnQml.hpp>` and use `finn::ui::FinnQmlApp` to bootstrap a QML app with FinnOS theme watching, `FinnTheme` context bridge, `image://finn-icon/<name>` SVG icon provider, and Fixel Display fonts.

### C++ QML Types (`src/ui/qml/finn_qml_types.hpp`)
- **`FinnThemeBridge`** (`FinnTheme`): Live dark/light mode, accent colour, high contrast, reduced motion, form factor (`desktop`/`tablet`/`mobile`/`tv`), and all tokens generated from `ui/_tokens.fui`.
- **`FinnSpringValue`**: QML wrapper over `finn::ui::SpringAnimator` (`kSnappy`, `kSmooth`, `kBouncy`, `kGentle`, `kOverscroll`).
- **`FinnSquircle`**: Superellipse (`cornerSmoothing=0.6`) painted item with Liquid Glass support.
- **`FinnBootMarkItem`**: Vector 4-dot + sparkle Finn mark.
- **`FinnAvatarItem`**: User avatar circle with initials and accent tint.
- **`FinnIconProvider`**: `image://finn-icon/<name>` SVG loader from `/usr/share/finn/icons/`.

### AOT-Compiled QML Controls (19 controls in `src/ui/qml/controls/`)
- **Window & layout**: `FinnApplicationWindow` (adaptive Desktop split-pane + 88×28 window controls pill, Tablet NavRail, Mobile bottom nav, TV 10-foot overscan), `FinnWindowControls`, `FinnGlassPanel`, `FinnCard`, `FinnHeroCard`, `FinnStatCard`, `FinnDivider`.
- **Navigation & feedback**: `FinnNavItem`, `FinnBadge`, `FinnEmptyState`, `FinnFocusRing`, `FinnIcon`, `FinnToast`.
- **Inputs & settings**: `FinnButton`, `FinnIconButton`, `FinnSearchField`, `FinnSettingRow`, `FinnSlider`, `FinnToggle`.

## C++20 Widget API (`FinnSDK::FinnUI`, `<FinnSDK/FinnSDK.hpp>`)

### Public headers (`src/ui/FinnSDK/`)
- `<FinnSDK/FinnSDK.hpp>` — umbrella header
- `<FinnSDK/Application.hpp>` — `FinnApplication` (installs Fixel Display fonts, watches settings, configures theme)
- `<FinnSDK/Window.hpp>` — `FinnWindow` (custom CSD chrome), `FinnLayerWindow` (optional `LayerShellQt`), `FinnWindowControls`
- `<FinnSDK/Components.hpp>` — 29 widget classes (`FinnSurface`, `FinnCard`, `FinnGlassPanel`, `FinnLabel`, `FinnButton`, `FinnIconButton`, `FinnToggle`, `FinnSlider`, `FinnSearchField`, `FinnTextField`, `FinnProgressBar`, `FinnProgressRing`, `FinnNavItem`, `FinnSidebar`, `FinnStatCard`, `FinnHeroCard`, `FinnNavRail`, `FinnSettingRow`, `FinnBadge`, `FinnDivider`, `FinnTabBar`, `FinnBreadcrumbs`, `FinnEmptyState`, `FinnToast`, `FinnPageStack`, `FinnDialog`, `FinnRibbonAnimation`, etc.)
- `<FinnSDK/Tokens.hpp>` & `<FinnSDK/Theme.hpp>` — `FinnTokens`, `FinnThemeManager` (`themeChanged` signal)
- `<FinnSDK/Spring.hpp>` & `<FinnSDK/Animation.hpp>` — analytic `SpringAnimator`, `SpringAnimator2D`, `ReducedCrossfade`, `rubber_band`, `PressFeedback`, `project_fling`
- `<FinnSDK/SessionClient.hpp>` — `SessionClient` for `finn-session` IPC (`ping`, `list_apps`, `launch`, `open`, `uninstall`, `compat_status`)
- `<FinnSDK/FinnQml.hpp>` — `FinnQmlApp` QML host helper
- `src/ui/finn/form_factor.hpp` — pure C++20 `AdaptiveFormFactor` (`Desktop`, `Tablet`, `Mobile`, `TV`) & `resolve_adaptive_form_factor()`
