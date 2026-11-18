# UI Audit — Tokens, Typography, Focus, Keyboard Navigation & Accessibility

*Phase 1 audit on branch `audit-and-ota` (read-only host inspection).*

## Summary

- **Design system source of truth**: `ui/_tokens.fui` generates `src/ui/generated_tokens.hpp` (`finn::ui::colors::*`, radii `r_bubble/r_panel/r_note/r_card = 10.4`, `r_tile = 5.37`, `r_app = 7.67`, `r_hero = 39.0`, `r_pill = 999`, and light/dark palette pairs).
- **Accessibility compliance**: **0** `setAccessibleName` / `setAccessibleDescription` / `QAccessible` occurrences across all of `src/` and `apps/`. Screen-reader navigation via AT-SPI is effectively unlabelled across the entire desktop.
- **Keyboard focus & navigation**: Only **1** widget sets an explicit focus policy (`apps/terminal/main.cpp:99`); **0** `focusInEvent` / `hasFocus()` focus-ring draws exist in `libfinnui`, `finn-shell`, `finn-oobe`, `finn-files`, or `apps/flourish`.
- **Token bypasses**: `src/ui/finn/tokens.hpp` defines extra semantic colors (`surface_canvas`, `surface_input`, `surface_hover`, `surface_pressed`, `danger`, `danger_hover`, `success`, `warning`) and legacy integer radii (`kRadiusSm=4, kRadiusMd=6, kRadiusLg=8, kRadiusXl=12`) independently of `ui/_tokens.fui`. Worse, `src/shell/{dock.cpp,popovers.cpp,notifications.cpp}`, `src/comp/{server.cpp,window.cpp}`, `src/ui/finn/components.cpp`, `apps/oobe/`, and `apps/flourish/` bypass `FinnTokens` with raw RGB/RGBA literals and hardcoded `#007BFF` accents.

---

## 1. Mechanical Token-Bypass Table

> Excludes:
> - `src/ui/generated_tokens.hpp` (generated from `ui/_tokens.fui`)
> - Token definitions themselves inside `src/ui/finn/tokens.hpp` (noted separately in Section 2)
> - Intentional domain palettes: `FinnThemeManager::avatarPresets()` (`src/ui/finn/theme.cpp:334-341`), 256-color terminal ANSI table (`apps/terminal/main.cpp:74-88`), and multi-stop 3D shader/lamp lighting gradients in `apps/oobe/lamp_widget.hpp:778-944` (though `lamp_widget.hpp` should still derive backdrop canvas `#000000`/`#F5F5F5` from `t.bone`/`t.surface_canvas`).

| `file:line` | Literal | Token / Replacement it should use |
|---|---|---|
| `src/comp/server.cpp:227` | `{0.105f, 0.118f, 0.149f, 1.0f}` | `finn::ui::colors::bone_dark` (converted to float RGBA) |
| `src/comp/server.cpp:230` | `{0.918f, 0.925f, 0.941f, 1.0f}` | `finn::ui::colors::bone_light` (converted to float RGBA) |
| `src/comp/server.cpp:1281` | `{0.918f, 0.925f, 0.941f, 1.0f}` | `finn::ui::colors::bone_light` |
| `src/comp/window.cpp:152-166` | Hardcoded SSD dark floats (`0.22f..0.94f`, `1,1,1,0.10f/0.20f`) | `colors::tile_dark`, `colors::hair_dark`, `colors::ink_dark`, `tokens.hpp` `danger_hover` |
| `src/comp/window.cpp:168-183` | Hardcoded SSD light floats (`0.94f/0.98f`, `0.08f..0.20f`, `0,0,0,0.08f/0.16f`) | `colors::tile_light`, `colors::hair_light`, `colors::ink_light`, `tokens.hpp` `danger_hover` |
| `src/ui/finn/components.cpp:121` | `QColor(Qt::white)` | `t.on` (foreground on accent/badge) |
| `src/ui/finn/components.cpp:215` | `QColor(0xEE, 0xF2, 0xF7)` | `t.surface_pressed` or `t.tile_on` |
| `src/ui/finn/components.cpp:217` | `QColor(0xF8, 0xFA, 0xFC)` | `t.surface_hover` or `t.tile` |
| `src/ui/finn/components.cpp:227` | `QColor(255, 255, 255, t.dark ? 10 : 140)` | `t.hair` |
| `src/ui/finn/components.cpp:460` | `QColor(0xE6, 0xEA, 0xF0)` | `t.surface_pressed` |
| `src/ui/finn/components.cpp:463` | `QColor(0xF1, 0xF4, 0xF9)` | `t.surface_hover` |
| `src/ui/finn/components.cpp:485` | `QColor(255, 255, 255, 50)` | `t.hair` |
| `src/ui/finn/components.cpp:765` | `QColor(0, 0, 0, 0x99)` / `QColor(0, 0, 0, 0x80)` | `t.surface_pressed` or new `t.scrim_strong` token |
| `src/ui/finn/components.cpp:766` | `QColor(0, 0, 0, 0x66)` / `QColor(0, 0, 0, 0x4D)` | `t.fill` or new `t.scrim` token |
| `src/ui/finn/components.cpp:780` | `QColor(0xF2, 0xF4, 0xF8)` / `QColor(0xFF, 0xFF, 0xFF)` | `t.ink` (dark) / `t.on` |
| `src/ui/finn/components.cpp:791` | `QColor(0, 0, 0)` / `QColor(0, 0, 0, 120)` | `t.ink` / `t.mute` |
| `src/ui/finn/components.cpp:966` | `QColor(0x6C, 0x6C, 0x6C, 102)` | `t.track` |
| `src/ui/finn/components.cpp:972` | `QColor(Qt::white)` | `t.knob` or `t.on` |
| `src/ui/finn/components.cpp:1104` | `QColor(0x3A, 0x40, 0x50)` | `t.tile` / `t.surface_elevated` |
| `src/ui/finn/components.cpp:1346` | `QColor(255, 255, 255, 128)` / `QColor(0, 0, 0, 128)` | `t.mute` or `t.dim` |
| `src/ui/finn/components.cpp:1462` | `QColor(255, 255, 255, 128)` / `QColor(0, 0, 0, 128)` | `t.mute` or `t.dim` |
| `src/ui/finn/components.cpp:2199-2206` | `QColor(147, 51, 234, ...)` / `(249, 115, 22, ...)` / `(0, 123, 255, ...)` / `(228, 194, 255, ...)` / `(255, 206, 188, ...)` / `(210, 228, 255, ...)` | Derive primary stop from `t.accent` / dedicated ribbon palette tokens in `_tokens.fui` |
| `src/ui/finn/components.cpp:2380` | `QColor(0, 0, 0)` | `tok.ink` |
| `src/ui/finn/components.cpp:2381` | `QColor(0xE2, 0xE8, 0xF0)` / `QColor(0x1D, 0x1B, 0x26)` | `tok.ink` |
| `src/ui/finn/components.cpp:2385` | `QColor(0x00, 0x7B, 0xFF)` | `QColor::fromRgba(colors::accent_light)` / `tok.accent` |
| `src/ui/finn/components.cpp:2430` | `QColor(0xFF, 0xFF, 0xFF)` | `tok.on` |
| `src/ui/finn/components.cpp:2660` | `QColor(0xE2, 0xE8, 0xF0)` / `QColor(0x1D, 0x1B, 0x26)` | `tok.ink` |
| `src/ui/finn/components.cpp:2664` | `QColor(0x00, 0x7B, 0xFF)` | `QColor::fromRgba(colors::accent_light)` / `tok.accent` |
| `src/ui/finn/components.cpp:2751` | `QColor(167, 139, 250)` / `QColor(139, 92, 246)` | `tok.accent` |
| `src/ui/finn/window.cpp:51` | `QColor(0xFF, 0x02, 0x02), Qt::white` | `tokens().danger_hover`, `tokens().on` |
| `src/ui/finn/window.cpp:487` | `QColor(255, 255, 255, t.dark ? 18 : 190)` | `t.hair` |
| `src/ui/finn/theme.cpp:291` | `"#007BFF"` | `QColor::fromRgba(colors::accent_light).name()` |
| `src/ui/finn/theme.cpp:490` | `QColor(0, 0, 0, resolved_tint.alpha())` | `resolved_tint` |
| `src/system/sys_files.hpp:26` | `"#007BFF"` | Shared constant derived from `colors::accent_light` |
| `src/system/sys_files.cpp:250` | `"#007BFF"` | `kDefaultAccentHex` from `colors::accent_light` |
| `src/qtplugin/finn_platform_theme.cpp:73` | `QColor(0, 0, 0, 110)` | `tokens_.dim` / `tokens_.mute` |
| `src/qtplugin/finn_style.cpp:259` | `QColor(0xE8, 0xEC, 0xF2)` | `t.surface_pressed` |
| `src/qtplugin/finn_style.cpp:261` | `QColor(0xF1, 0xF4, 0xF8)` | `t.surface_hover` |
| `src/shell/dock.cpp:29` | `QColor(0x28, 0x2C, 0x37, 0xCC)` / `QColor(255, 255, 255, 0xB2)` | `t.dock_pill` / `t.glass` |
| `src/shell/dock.cpp:33` | `QColor(0x3A, 0x40, 0x50)` / `QColor(255, 255, 255)` | `t.tile` |
| `src/shell/dock.cpp:187-190` | `QColor(255,255,255,42/24/48)` / `QColor(0,0,0,28)` | `t.surface_hover` / `t.hair` |
| `src/shell/dock.cpp:230` | `QColor(t.ink.red(), t.ink.green(), t.ink.blue(), 150)` | `t.dim` |
| `src/shell/dock.cpp:409-410` | `QColor(255, 255, 255, 26)` / `QColor(255, 255, 255, 82)` | `t.fill` |
| `src/shell/dock.cpp:434` | `QColor(t.ink.red(), t.ink.green(), t.ink.blue(), 150)` | `t.dim` |
| `src/shell/dock.cpp:539-545` | `QColor(0x3A,0x40,0x50,0xCC)` / `QColor(255,255,255,0xB2/22/72)` | `t.dock_pill` / `t.fill` |
| `src/shell/dock.cpp:631-632` | `QColor(255, 255, 255, 20/64)` | `t.fill` |
| `src/shell/dock.cpp:647, 649` | `QColor(255,255,255,0xB2)` / `QColor(0,0,0,0xB2)` / `QColor(255,255,255)` / `QColor(0,0,0)` | `t.dim` and `t.ink` |
| `src/shell/popovers.cpp:48-60` | `popoverGlassFill`, `popoverCardFill`, `popoverSubcardFill`, `popoverDimInk` raw literals (`0x28,0x2C,0x37,0xD9`, `255,255,255,0xB2/0x1A/0x14/0x66`, `0,0,0,0xB2`) | `t.glass`, `t.tile`, `t.fill`, `t.dim` |
| `src/shell/popovers.cpp:72` | `QColor(255, 255, 255, 28/96)` | `t.hair` |
| `src/shell/popovers.cpp:128` | `QColor(0xF2, 0xF4, 0xF8)` / `QColor(0, 0, 0)` | `t.ink` |
| `src/shell/popovers.cpp:515-517` | `QColor(0xF2, 0xF4, 0xF8)` / `QColor(0,0,0)` / `QColor(255,255,255,22)` / `QColor(0,0,0,26)` | `t.ink` and `t.hair` |
| `src/shell/popovers.cpp:548` | `QColor(255, 255, 255, 0x33)` / `QColor(0, 0, 0, 0x33)` | `t.hair` |
| `src/shell/popovers.cpp:568-569` | `QColor(255, 255, 255, 42/26/148/102)` | `t.surface_hover` / `t.tile` |
| `src/shell/popovers.cpp:735` | `QColor(0xEF, 0x44, 0x44)` | `t.danger` |
| `src/shell/popovers.cpp:1127, 1175` | `QColor(0xF2,0xF4,0xF8)` / `QColor(0,0,0)` / `QColor(255,255,255,28)` / `QColor(0,0,0,24)` | `t.ink` / `t.hair` |
| `src/shell/popovers.cpp:1554, 1557-1558` | `QColor(0xF2,0xF4,0xF8)` / `QColor(0,0,0)`, `QColor(0x00,0x7B,0xFF)`, `QColor(0xAB,0xAB,0xAB)` | `t.ink`, `t.accent`, `t.track` (or `t.mute`) |
| `src/shell/popovers.cpp:1632-1633` | `QColor(255, 255, 255, 0x1F)` / `QColor(0xD9, 0xD9, 0xD9)` | `t.track` |
| `src/shell/popovers.cpp:1652-1655` | `QColor(255,255,255,44/190)` / `QColor(0,0,0,36/160)` | `t.track` / `t.dim` |
| `src/shell/popovers.cpp:1877` | `QColor(0xFF, 0x02, 0x02), Qt::white` | `t.danger_hover`, `t.on` |
| `src/shell/popovers.cpp:2042-2048` | `QColor(245,247,250)` / `QColor(17,19,24)` / `QColor(255,255,255,40)` / `QColor(0,0,0,28)` | `QColor::fromRgba(colors::ink_dark)` / `colors::bone_dark` / `t.fill` |
| `src/shell/popovers.cpp:2296, 2316` | `QColor(0x0B, 0x0E, 0x14)`, `QColor(0,0,0,48)` / `QColor(255,255,255,20)` | `t.bone` and `t.fill` |
| `src/shell/popovers.cpp:2384, 2394` | `QColor(0x16, 0x19, 0x22)` / `QColor(0xF5, 0xF5, 0xF5)` / `QColor(0, 0, 0)` | `t.surface_canvas` / `t.bone`, `t.ink` |
| `src/shell/notifications.cpp:83` | `QColor(255, 255, 255, 36)` / `QColor(0, 0, 0, 22)` | `t.surface_hover` |
| `src/shell/notifications.cpp:124-128` | `QColor(255,255,255,34/64/48)` / `QColor(0,0,0,20/42/32)` | `t.fill`, `t.surface_hover`, `t.surface_pressed` |
| `src/shell/notifications.cpp:203-205` | `QColor(255,255,255)` / `QColor(0,0,0)` / `0xB2` alpha literals | `t.ink`, `t.dim` |
| `src/shell/notifications.cpp:222-225` | `QColor(0x28,0x2C,0x37,0xD9)` / `QColor(255,255,255,0xCC)` / `QColor(255,255,255,28/96)` | `t.glass`, `t.hair` |
| `apps/settings/main.cpp:553-555` | `"#007BFF"`, `"#7C3AED"`, `"#059669"`, `"#D97706"`, `"#DC2626"` | `colors::accent_light` + named accent preset list in `tokens.hpp` |
| `apps/files/main.cpp:165` | `QColor(0xF8, 0xFA, 0xFC)` | `t.surface_hover` or `t.tile` |
| `apps/files/main.cpp:184` | `QColor(255, 255, 255, 128)` / `QColor(0, 0, 0, 128)` | `t.mute` |
| `apps/files/main.cpp:667` | `QColor::fromRgba(finn::ui::colors::mute_light)` | `FinnThemeManager::instance().tokens().mute` (dark mode uses light token!) |
| `apps/notes/main.cpp:391-392` | `"#666B75"`, `"#353B48"`, `"#E5E7EB"` | `t.dim.name()`, `t.hair.name()` |
| `apps/viewer/main.cpp:211-215` | `QColor(206,212,222)`, `(235,238,244)`, `(46,52,64)`, `(226,231,239)`, `(250,251,253)`, `(62,70,86)` | `t.tile`, `t.surface_canvas`, `t.surface_elevated` |
| `apps/wallpaper/main.cpp:67` | `QColor(8, 12, 20, 70)` | `t.fill` / `t.bone` with alpha |
| `apps/oobe/main.cpp:136, 208-237` | `QColor(0,0,0)`, `0x16181E`, `0xFFFFFF`, `0x222630`, `0xEBEBEB` | `t.ink`, `t.tile`, `t.surface_pressed` |
| `apps/oobe/main.cpp:308-319` | `0x262B38`, `0xFFFFFF`, `0x323848`, `0xECEEF2`, `0x2D3343`, `0xFAFBFC`, `0x181820` | `t.tile`, `t.surface_pressed`, `t.surface_hover`, `t.hair`, `t.ink` |
| `apps/oobe/main.cpp:395-403` | `QColor(0x20,0x24,0x2E,...)`, `QColor(255,255,255,...)`, `QColor(0,0,0)` | `t.tile_on`, `t.ink` |
| `apps/oobe/main.cpp:473-503` | `0x000000`, `0xFFFFFF`, `255,255,255,28/22/130`, `0,0,0,18/12/110` | `t.bone`, `t.hair`, `t.ink`, `t.dim` |
| `apps/oobe/main.cpp:544-575` | `0x222734`, `0xFFFFFF`, `0x2C3242`, `0xEEF0F4`, `0x272D3B`, `0xFAFAFC`, `0xF5F5F5`, `0x181820` | `t.tile`, `t.surface_pressed`, `t.surface_hover`, `t.hair`, `t.surface_canvas`, `t.ink` |
| `apps/oobe/main.cpp:654-755` | `0x282E3D`, `0xFFFFFF`, `QColor(0,0,0)`, `0x181820`, `255,255,255,90`, `0,0,0,60` | `t.surface_input`, `t.ink`, `t.hair` |
| `apps/oobe/main.cpp:909, 1236-1242, 1535` | `0x000000`, `0xF5F5F5`, `0xF4F6FA`, `0x111318`, `0x007BFF` | `t.surface_canvas`, `t.ink`, `t.accent` |
| `apps/oobe/main.cpp:1612, 1983, 2101` | `QColor(0,0,0)`, `QColor(0x18,0x18,0x20)` | `t.ink` |
| `apps/oobe/main.cpp:1919` | `QColor(0xFB,0x71,0x85)` / `QColor(0xE1,0x1D,0x48)` | `t.danger` |
| `apps/flourish/tab_model.cpp:33, 35, 37, 143` | `0x007BFF`, `0x22C55E`, `0xF59E0B` | `t.accent`, `t.success`, `t.warning` |
| `apps/flourish/browser_window.cpp:562, 754-758` | `0x007BFF`, `0x22C55E`, `0xF59E0B`, `0xEC4899`, `0x8B5CF6` | `t.accent`, `t.success`, `t.warning` + `FinnThemeManager::avatarPresets()` |
| `apps/flourish/intro_overlay.cpp:234` | `QColor(0x00, 0x7B, 0xFF)` | `t.accent` |
| `apps/flourish/command_bar.cpp:196` | `QColor(0, 0, 0, 120)` / `QColor(0, 0, 0, 60)` | `t.fill` (or scrim token) |
| `apps/flourish/sidebar_panel.cpp:38` | `QColor(0x20, 0x23, 0x2B)` / `QColor(0xF3, 0xF5, 0xF8)` | `t.surface_hover` |
| `apps/flourish/start_page.hpp:27-30` | `rgba(255,255,255,0.12/0.06)`, `rgba(0,0,0,0.09/0.04)` | `t.hair` and `t.surface_hover` formatted via `rgba(...)` |

---

## 2. Hardcoded Font Sizes, Radii & Spacing

1. **Dual diverging radii system (`src/ui/finn/tokens.hpp` vs `ui/_tokens.fui`)**:
   - `ui/_tokens.fui` exports `r_bubble = 10.4`, `r_panel = 10.4`, `r_note = 10.4`, `r_card = 10.4`, `r_tile = 5.37`, `r_app = 7.67`, `r_hero = 39.0`, `r_pill = 999`.
   - `src/ui/finn/tokens.hpp` instead defines `kRadiusSm = 4`, `kRadiusMd = 6`, `kRadiusLg = 8`, `kRadiusXl = 12`, which widgets across `src/shell/` and `apps/` consume instead of the generated tokens.
2. **Hardcoded pixel/point font sizes bypassing `FinnLabel::Role`**:
   - `src/shell/popovers.cpp:65` defines its own `popoverFont(int px)` used in ~25 places (`10..16px`) instead of `FinnLabel::Role`; `src/shell/popovers.cpp:2029-2031` hardcodes `15.0`, `58.0`, `13.5` pt; `src/shell/popovers.cpp:2396` hardcodes `20px`.
   - `apps/oobe/main.cpp:45` defines `oobe_font(int pixel_size)` used for `12..26px` directly instead of `FinnLabel` typography roles.
   - `src/ui/finn/components.cpp:331-346, 1157, 1197, 1349, 1356, 1430` mixes `finn_font_px(18/16/14/12)`, `finn_font(10.5/10/9/8.8/8.2)`, and `setPixelSize(16)` — and because `text_scale` is never multiplied inside `finn_font()`/`finn_font_px()`, changing Text Scale in Settings has zero visual effect (`M16`).
   - `apps/installer/main.cpp:387, 475, 643, 691` hardcode `24px`/`20px`; `apps/files/main.cpp:187, 191` hardcode `13px`/`12px`; `apps/flourish/intro_overlay.cpp:60-175` & `sidebar_panel.cpp:109` hardcode `9.0..12.0pt`.
3. **Hardcoded palette definitions in `src/ui/finn/tokens.hpp:95-149`**:
   - `surface_canvas` (`0x111319` / `0xF2F4F8`), `surface_input` (`0x1C202B` / `0xF4F6FA`), `danger`, `danger_hover`, `success`, `warning` are hardcoded in C++ rather than generated from `ui/_tokens.fui`.

---

## 3. Accessibility, Focus States & Keyboard Navigation

| Area | Current State | Evidence |
|---|---|---|
| **Accessibility names / descriptions** | **0 widgets** set `accessibleName`, `accessibleDescription`, or `QAccessible` roles | `grep -rnE "setAccessible\|QAccessible" src/ apps/` → 0 matches |
| **Keyboard focus policy** | Only `TerminalWidget` sets a focus policy; custom clickable widgets accept pointer-only interaction | `grep -rn "setFocusPolicy" src/ apps/` → only `apps/terminal/main.cpp:99` |
| **Visible focus rings** | Custom-painted controls (`FinnButton`, `FinnToggle`, `FinnCard`, `FinnSlider`, `FinnIconButton`, `FinnNavItem`, `RailButton`, `OobeOptionRow`, `OobeAppearanceCard`, `FileEntryCard`) never inspect `hasFocus()` in `paintEvent` | `grep -rnE "hasFocus\|focusInEvent" src/ apps/` → 0 matches |
| **Shell keyboard navigation** | `DockWindow`, `HomeMenuPopover`, and `ControlCenterPopover` paint hit-tested rects without child widgets or arrow/Tab focus traversal (`src/shell/dock.cpp`, `src/shell/popovers.cpp`) | Dock pills (`HomeButtonsPill`, `HardwarePill`, `TimePill`) & Control Center cards cannot be tabbed to or activated with `Return`/`Space` |
| **Disabled states** | Interactive custom widgets like `FileEntryCard` (`apps/files/main.cpp`), `UrlPillButton` / `FavoriteTile` / `TabRowWidget` (`apps/flourish/sidebar_panel.cpp`), `OobeOptionRow` (`apps/oobe/main.cpp`) do not dim or block when disabled; Installer's Install button stays enabled while `pkexec finn-install` runs (`apps/installer/main.cpp:735-758`); Store's detail button is unconditionally disabled for non-installed apps (`apps/store/main.cpp:429`) | Verified in cited files |

---

## 4. Worst UIs (Ranked)

1. **`src/shell/popovers.cpp` & `src/shell/dock.cpp` (`ControlCenterPopover`, `HomeMenuPopover`, `LockScreenWindow`, `DockWindow`)**: Highest concentration of token bypasses (~35 raw color calls, hardcoded `popoverFont(...)`, `#007BFF` at `popovers.cpp:1557`), manual rect hit-testing with zero keyboard navigation/focus rings, hardcoded `BootSplashWindow` dead code (`2319-2412`), and broken `LockScreenWindow` auth/shortcut containment (`B6`, `B7`).
2. **`apps/oobe/main.cpp` (`OobeWindow`)**: ~30 raw color/font bypasses (`oobe_font`, `#000000`/`#F5F5F5`, hardcoded rose error `0xFB7185`), no keyboard navigation on `OobeOptionRow`/`OobeAppearanceCard`, plaintext `FinnDialog::prompt` for Wi-Fi passwords (`2147`), CPU-heavy 3-pass software blur inside `ProgressiveBlurOverlay::paintEvent` (`906-962`), and a fake Account setup page (`1899-1940, 2318-2335`).
3. **`apps/installer/main.cpp` (`InstallerWindow`)**: Clean layout appearance, but functionally broken backend invocation (`755-757`), non-disabled Install button during destructive run, only 4 hardcoded timezones (`569-570`), no user/password/keyboard UI, and claims `btrfs + FAT32 ESP` (`499`) while backend formats `ext4`.
4. **`apps/settings/main.cpp` (`SettingsWindow`)**: Tears down and recreates Wi-Fi and Bluetooth widget trees every 3 seconds (`1118-1167`), losing scroll/hover/focus state; shows disk size under `"Memory"` (`1040`); exposes 5 unimplemented Accessibility controls (`text_scale`, `color_filter`, `sticky_keys`, `filter_keys`, unpackaged `orca`).
5. **`apps/flourish/` (`BrowserWindow`, `SidebarPanel`, `StartPage`)**: Mixes `FinnTokens` with raw `0x007BFF` space accents (`tab_model.cpp`, `browser_window.cpp`), raw hover hexes (`sidebar_panel.cpp:38`), and HTML string CSS (`start_page.hpp:27-30`) that ignore high-contrast and accent customization.
