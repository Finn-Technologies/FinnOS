# FinnSDK API Overview

This reference is derived directly from the installed C++20 headers under `src/ui/FinnSDK/` (`<FinnSDK/*.hpp>`), `src/ui/finn/`, `src/session/client.hpp`, `src/system/sys_files.hpp`, and `src/common/paths.hpp`.

Link target: `FinnSDK::FinnUI` (`libfinnui.so.0.2.0`).

---

## Headers

| Umbrella Header | Underlying Header | Purpose |
|---|---|---|
| `<FinnSDK/FinnSDK.hpp>` | `ui/finn/sdk.hpp` + `session/client.hpp` | All-in-one include |
| `<FinnSDK/Application.hpp>` | `ui/finn/application.hpp` | `FinnApplication` |
| `<FinnSDK/Window.hpp>` | `ui/finn/window.hpp` | `FinnWindow`, `FinnWindowControls`, `FinnLayerWindow` |
| `<FinnSDK/Components.hpp>` | `ui/finn/components.hpp` | FinnUI widgets, dialogs, toasts, page stack |
| `<FinnSDK/Theme.hpp>` | `ui/finn/theme.hpp` | `FinnThemeManager`, `AvatarPreset` |
| `<FinnSDK/Tokens.hpp>` | `ui/finn/tokens.hpp` | `FinnTokens`, palette, fonts, icon/pointer lookup |
| `<FinnSDK/Animation.hpp>` | `ui/finn/animation.hpp` | Easing curves, `Animation`, `SpringValue`, `reduced_motion()` |
| `<FinnSDK/Spring.hpp>` | `ui/finn/spring.hpp` | Analytic spring solver, `SpringAnimator`, rubber-band, gesture tracker |
| `<FinnSDK/SessionClient.hpp>` | `session/client.hpp` | `finn::SessionClient`, `SessionApp`, `CompatStatus` |

---

## Application & Windows (`namespace finn::ui`)

### `FinnApplication` (`<FinnSDK/Application.hpp>`)
Subclass of `QApplication`. Sets `QT_QPA_PLATFORM=wayland;xcb`, `QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`, and `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` (when unset), loads `Fixel Display`, applies the Finn palette and `finn` style, and connects `FinnThemeManager::themeChanged` for live updates.

- `FinnApplication(int& argc, char** argv, const QString& app_id = {}, const QString& display_name = {}, const QString& version = "0.2.0")`
- `QString finnAppId() const`

### `FinnWindow` (`<FinnSDK/Window.hpp>`)
Frameless Wayland CSD `QWidget` with rounded corners, ambient shadow, 48px header bar, `FinnWindowControls` (Minimize/Maximize/Close), edge resize (`startSystemResize`), titlebar drag (`startSystemMove`), optional collapsible sidebar slot, and floating `FinnToast` overlay.

- `enum class ChromeMode { Standard, FigmaSplit }`
- `explicit FinnWindow(const QString& title = {}, const QString& icon_name = {}, QWidget* parent = nullptr)`
- `void setChromeMode(ChromeMode mode)`
- `void setWindowTitleText(const QString& title)`
- `void setHeaderWidget(QWidget* widget)`
- `void setSidebarWidget(QWidget* widget)`
- `void setContentWidget(QWidget* widget)`
- `void setSidebarCollapsed(bool collapsed)` / `bool isSidebarCollapsed() const`
- `void showToast(const QString& message, FinnToast::Severity severity = FinnToast::Severity::Info, int duration_ms = 2800)`
- Slot: `void toggleMaximized()`

### `FinnLayerWindow` (`<FinnSDK/Window.hpp>`)
`LayerShellQt::Window` wrapper for shell/overlay surfaces.

- `enum class Layer { Background, Bottom, Top, Overlay }`
- `enum Anchor { AnchorNone, AnchorTop, AnchorBottom, AnchorLeft, AnchorRight }` (`Anchors` flags)
- `enum class KeyboardMode { None, Exclusive, OnDemand }`
- Methods: `setLayer()`, `setAnchors()`, `setKeyboardMode()`, `setExclusiveZone()`, `setLayerMargins()`, `setDesiredSize()`, `setInputRegions(const QVector<QRect>&)`, `clearInputRegions()`

---

## Theme & Tokens (`<FinnSDK/Theme.hpp>`, `<FinnSDK/Tokens.hpp>`)

### `FinnThemeManager`
Singleton (`FinnThemeManager::instance()`) watching system (`/var/lib/finn/settings`) and user (`~/.config/finn/settings`) settings and emitting `themeChanged(const finn::ui::FinnTokens&)`.

- **Accessors**: `tokens()`, `settings()`, `isDark()`, `reducedMotion()`, `wallpaperPath()`, `lockWallpaperPath()`, `avatarColor()`, `avatarPresets()`, `dockPins()`
- **Rendering helpers**: `avatarPixmap()`, `iconPixmap()`, `pointerPixmap()`, `icon()`, `paintIcon()`
- **Mutators** (write settings): `setDark()`, `setReducedMotion()`, `setHighContrast()`, `setColorFilter()`, `setScreenReader()`, `setStickyKeys()`, `setFilterKeys()`, `setCursorSize()`, `setTextScale()`, `setAutoLockMinutes()`, `setLockOnSleep()`, `setAccent()`, `setAvatarColor()`, `setWallpaper()`, `setLockWallpaper()`, `setDockPins()`, `setScale()`, `reload()`
- **Signal**: `void themeChanged(const finn::ui::FinnTokens& tokens)`

### `FinnTokens` & Helpers (`ui/finn/tokens.hpp`)
- Radii constants: `kRadiusSm` (`4.0`), `kRadiusMd` (`6.0`), `kRadiusLg` (`8.0`), `kRadiusXl` (`12.0`)
- Color fields on `FinnTokens`: `bone`, `tile`, `surface_canvas`, `surface_elevated`, `surface_input`, `surface_hover`, `surface_pressed`, `tile_on`, `nav_sel`, `hero_tint`, `hair`, `ink`, `mute`, `dim`, `accent`, `track`, `knob`, `on`, `fill`, `glass`, `dock_pill`, `danger`, `danger_hover`, `success`, `warning`
- Functions: `tokens_from_settings()`, `load_tokens()`, `build_finn_palette()`, `primary_font_family()`, `finn_font()`, `finn_font_px()`, `finn_mono_font()`, `scrollbar_qss()`, `resolve_icon_path()`, `resolve_pointer_path()`
- Generated palette constants live in `ui/generated_tokens.hpp` under `finn::ui::colors`.

---

## Components (`<FinnSDK/Components.hpp>`)

All widgets below live in `namespace finn::ui`:

- **Surfaces & Layout**:
  - `FinnSurface` (`Role`: `Canvas`, `Bone`, `Tile`, `TileOn`, `Transparent`)
  - `FinnCard` (rounded tile, optional `clickable`, `setSelected()`, signal `clicked()`)
  - `FinnGlassPanel` (translucent glass panel with `setRadius()`, `setOpacityAlpha()`)
  - `FinnDivider` (`Qt::Horizontal` / `Qt::Vertical`)
  - `FinnSettingRow` (title, subtitle, icon, trailing widget slot, `setIconBadgeColor()`)
  - `FinnPageStack` / `FinnAnimatedStack` (`QStackedWidget` with `setAnimatedTransitions(bool)`)
  - Free helpers: `install_stack_animator(QStackedWidget*)`, `animate_stack_switch(QStackedWidget*, int)`
- **Typography & Status**:
  - `FinnLabel` (`Role`: `TitleLarge`, `Title`, `Headline`, `Body`, `Caption`, `Mono`; `Tone`: `Ink`, `Dim`, `Mute`, `Accent`, `Success`, `Warning`, `Danger`, `OnAccent`)
  - `FinnBadge` (`Tone`: `Neutral`, `Accent`, `Success`, `Warning`, `Danger`)
  - `FinnStatCard`, `FinnHeroCard`
  - `FinnProgressBar`, `FinnProgressRing` (determinate `>= 0` or indeterminate `< 0`)
  - `FinnEmptyState` (icon, title, description, optional action button + `actionClicked()` signal)
  - `FinnToast` (`Severity`: `Info`, `Success`, `Warning`, `Error`)
- **Controls & Inputs**:
  - `FinnButton` (`Variant`: `Secondary`, `Primary`, `Ghost`, `Danger`)
  - `FinnIconButton` (square icon button, `setActive()`, `setBadgeDot()`, `setCustomHoverColor()`)
  - `FinnToggle` (switch control)
  - `FinnSlider` (`setRange()`, `setValue()`, `setPillStyle()`, signal `valueChanged(int)`)
  - `FinnSearchField` (`setPillMode()`, `focusInput()`, `lineEdit()`, signals `textChanged`, `submitted`)
  - `FinnTextField` (`QLineEdit` subclass)
- **Navigation & Dialogs**:
  - `FinnNavItem`, `FinnSidebar` (header, optional search field, pinned items, items, footer, signal `selected(const QString& id)`)
  - `FinnNavRail` (vertical icon rail, signal `selected(const QString& id)`)
  - `FinnTabBar` (`setTabs()`, `setCurrentIndex()`, signal `tabChanged(int)`)
  - `FinnBreadcrumbs` (`setSegments()`, signal `segmentActivated(const QString& target)`)
  - `FinnDialog` (`FinnDialog::confirm(...)`, `FinnDialog::prompt(...)`)
  - `FinnRibbonAnimation` (`Mode::HiIntro`, `Mode::SlinkyWave`, used by OOBE)

---

## Motion & Springs (`<FinnSDK/Animation.hpp>`, `<FinnSDK/Spring.hpp>`)

Pure C++20 (no Qt dependency):

- **Durations**: `kMotionFast` (`0.14s`), `kMotionStandard` (`0.22s`), `kMotionSlow` (`0.32s`), `kMotionReducedCrossfade` (`0.08s`)
- **Reduced motion**: `reduced_motion()` (reads `FINN_REDUCED_MOTION` and `$HOME/.config/finn/settings` / `/var/lib/finn/settings`)
- **Easing & Helpers**: `clamp_unit()`, `ease_out_cubic()`, `ease_in_out_cubic()`, `ease_out_back()`, `stagger()`, `approach()`, `Animation`, `SpringValue`
- **Analytic Springs (`ui/finn/spring.hpp`)**:
  - `SpringParams` (presets in `finn::ui::springs`: `kSnappy`, `kSmooth`, `kBouncy`, `kGentle`, `kOverscroll`)
  - `SpringSolver::solve_analytic(...)`, `SpringAnimator`, `SpringAnimator2D`, `ReducedCrossfade`
  - `rubber_band()`, `rubber_band_inverse()`, `rubber_band_clamp()`, `rubber_band_for_motion()`
  - `press_down_scale()`, `elastic_release_curve()`, `elastic_press_scale()`, `PressFeedback`
  - `project_fling()`, `snap_target_from_fling()`, `GestureProgressTracker`, `FrameTimeRecorder`

---

## Session IPC (`<FinnSDK/SessionClient.hpp>`, `namespace finn`)

Talks to `finn-session` over `$XDG_RUNTIME_DIR/finn/session.sock` (override via `FINN_SESSION_SOCK`):

- `struct SessionApp { id, name, icon, installed, source, category, note }`
- `struct CompatStatus { arch, wayland_display, x_display, xwayland_ready, wine_ready, flatpak_ready, wine_summary }`
- `std::string session_socket_path()`
- `class SessionClient`:
  - `bool connect(std::string* error)` / `void close()`
  - `bool list(std::vector<SessionApp>* apps, std::string* error)`
  - `bool launch(std::string_view id, std::string* error)`
  - `bool launch_with_arg(std::string_view id, std::string_view arg, std::string* error)`
  - `bool open_target(std::string_view target, std::string* error)`
  - `bool run_windows(std::string_view exe_path, std::string_view prefix, std::string* error)`
  - `bool compat_status(CompatStatus* out, std::string* error)`
  - `bool power_action(std::string_view action, std::string* error)`
