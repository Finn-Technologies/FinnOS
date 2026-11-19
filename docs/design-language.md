# FinnOS Design Language

**One design system for FinnOS across Desktop, Tablet, and Phone.**
Canonical machine-readable tokens live in [`ui/_tokens.fui`](../ui/_tokens.fui) and compile via [`tools/tokens.py`](../tools/tokens.py) into [`src/ui/generated_tokens.hpp`](../src/ui/generated_tokens.hpp).

> **Inspiration & Originality:** FinnOS draws architectural inspiration from **ColorOS 14 / iOS 16** (layered translucency, soft depth blur, continuous rounded geometry) and **HarmonyOS 2** (modular card/widget density and adaptive multi-form-factor surfaces), **without** copying any third-party assets, icons, wallpapers, typography, or trademarks. All FinnOS marks (`boot-mark`), wallpapers (`assets/wallpaper/`), and icon extensions (`assets/icons/`) are FinnOS-authored or MIT-licensed (`assets/icons/LICENSE-hugeicons.md`).
>
> **Implementation Status & Figma Scope:**
> - **Implemented (Desktop):** Desktop `libfinnui`, `finn-shell` (Dock, Home, Spotlight, Control Center, Open Windows, Notifications, Lock Screen), `finn-settings`, and first-party desktop apps (`>= 1025 dp`).
> - **Specified-Only (Not Yet Implemented):** Phone (`< 600 dp`) and Tablet (`600–1024 dp`) layouts, touch/gesture input in `finn-comp`/`finn-shell` (Audit **B2**), compositor Gaussian backdrop blur shader (`35 px` / `15 px`), continuous squircle clipping (`corner_smoothing = 0.6`), and focus rings on custom-painted `libfinnui` controls.
> - **Figma reality:** `FinnOS - Abhi` contains desktop (`1280×832`), OOBE, Notification, and Pointer frames only; **no phone or tablet screens exist in Figma** (see [`docs/figma-audit.md`](figma-audit.md) **FA-B4**).

---

## 1. Breakpoints & Form Factors (`finn::ui::breakpoints`)

| Token | Range (`dp`) | Form Factor | Navigation Shell | Minimum Touch / Pointer Hit Target | Status |
|---|---|---|---|---|---|
| `bp_phone_max` | `< 600` (`0–599 dp`) | **Phone** | Bottom bar / gesture pill + full-screen stacked views + bottom sheets | `44×44 dp` min (`48×48 dp` primary actions) | **Specified-Only** |
| `bp_tablet_min`–`bp_tablet_max` | `600–1024 dp` | **Tablet** | Collapsible sidebar / `FinnNavRail` + 2-pane split view + optional floating windows | `44×44 dp` min | **Specified-Only** |
| `bp_desktop_min` | `>= 1025 dp` | **Desktop** | Bottom 4-pill glass dock (`DockWindow`) + `FigmaSplit` CSD windows (`FinnWindow`) | `30–36 px` pointer controls (`44 px` dock) | **Implemented** |

*(Television scaling from `ui/_tokens.fui`: `tv_type_scale = 1.5`, `min_tv = 32 sp`, reserved/specified-only.)*

---

## 2. Color System (`finn::ui::colors`)

One semantic name resolves to two appearance halves (`_light` and `_dark`). Appearance is a system property, never a per-widget override. **One accent, one neutral:** sidebars and lists use `on` (`#007BFF` / `#0A84FF`) strictly for live/active state and `mute`/`dim` everywhere else (`docs/design.md`).

### 2.1 Surfaces & Borders

| Token | Light | Dark | Role | Status |
|---|---|---|---|---|
| `bone` | `#FBFBFD` | `#0D0F14` | Primary window/app background | Implemented |
| `surface_canvas` | `#F2F4F8` | `#111319` | Sidebar & secondary background well | Implemented |
| `tile` | `#FFFFFF` | `#161922` | Elevated cards, stat tiles, setting rows | Implemented |
| `tile_on` | `#DCEBFF` | `#223B61` | Selected card/nav pill fill | Implemented |
| `surface_input` | `#F4F6FA` | `#1C202B` | Text fields, secondary buttons, icon wells | Implemented |
| `surface_hover` | `#0E000000` (5.5% black) | `#14FFFFFF` (8% white) | Interactive pointer/hover veil | Implemented |
| `surface_pressed` | `#18000000` (9.4% black) | `#22FFFFFF` (13.3% white) | Interactive active/press veil | Implemented |
| `glass` | `#FBFBFD` (70% alpha `0xB2`) | `#161922` (89% alpha `0xE2`) | Frosted dock, popovers, panels | Implemented (shell popovers currently diverge; see `figma-audit.md`) |
| `nav_sel` | `#120F141A` (7% ink) | `#24FFFFFF` (14% white) | Selected badged sidebar row | Implemented |
| `hero_tint` | `#EBFFDF` | `#15221B` | `FinnHeroCard` identity surface | Implemented |
| `hair` | `#14000000` (8% black) | `#1CFFFFFF` (11% white) | 1 px hairline borders & `FinnDivider` | Implemented |

### 2.2 Text, Accent & Feedback

| Token | Light | Dark | Role | Status |
|---|---|---|---|---|
| `ink` | `#0F141A` | `#F2F4F8` | Primary headings, body, active glyphs | Implemented |
| `dim` | `#525866` | `#9EA6B5` | Subtitles, secondary labels, inactive rail icons | Implemented |
| `mute` | `#868E9B` | `#8891A0` | Captions, placeholders, disabled foreground | Implemented |
| `accent` | `#007BFF` | `#007BFF` | Brand primary CTA, toggles, focus rings (user-customizable in Settings) | Implemented |
| `on` | `#007BFF` | `#0A84FF` | Selected nav text/links (high-legibility dark-mode blue) | Implemented |
| `track` | `#80000000` | `#3B4048` | Unchecked toggle & slider track | Implemented |
| `knob` | `#FFFFFF` | `#EDE9E1` | Toggle & slider thumb | Implemented |
| `success` | `#16A34A` | `#22C55E` | Positive status badges & toasts | Implemented |
| `warning` | `#D97706` | `#F59E0B` | Caution badges & toasts | Implemented |
| `danger` | `#EF4444` | `#F87171` | Destructive actions & error states | Implemented |
| `danger_hover` | `#FF0202` | `#FF0202` | Window close & danger hover fill | Implemented |

**High Contrast mode** (`Settings -> Accessibility`): forces `glass` and `dock_pill` alpha to `255` (opaque), elevates `hair` to `220` alpha, and sharpens `dim`/`mute` contrast (`src/ui/finn/tokens.hpp:152`).

---

## 3. Typography (`Fixel Display`)

- **UI Family:** `Fixel Display` (`assets/fonts/FixelDisplay/*.otf`, weights `400 Regular`, `500 Medium`, `600 SemiBold`, `700 Bold`; `RegularItalic` shipped).
- **Script Fallback:** `Noto Sans`, `Noto Sans Devanagari`, `Noto Sans Arabic`, `Noto Sans CJK {SC,TC,JP,KR}`, `Noto Color Emoji`, `DejaVu Sans`.
- **Monospace:** `JetBrains Mono` -> `DejaVu Sans Mono` -> `Liberation Mono` -> `Noto Sans Mono` (`finn_mono_font`).
- **Rule:** Hierarchy comes primarily from **size** and **ink vs. dim/mute**, reserving `Medium`/`SemiBold` for buttons, badges, lock-screen clock, and wordmark (`NAMING.md`).

| Role | Size / Line Height (px) | Weight | `FinnLabel::Role` / Token | Usage |
|---|---|---|---|---|
| **Display / Lock Clock** | `58 / 64` (Lock), `34 / 40` (`time`) | `500` / `400` | `time` | Lock screen clock, hero telemetry |
| **Setup Prompt** | `24 / 30` | `400` | OOBE prompt | OOBE step prompts, section titles |
| **Title Large** | `18 / 24` | `500` (code) / `400` (`clock`) | `TitleLarge` / `clock` | Dialog/page display titles, dock clock (`16 px` in `TimePill`) |
| **Title** | `16 / 22` | `400` (Figma `20:64`) / `500` (`FinnLabel::Title`) | `Title` / `title` | Window sidebar title & page headers |
| **Headline** | `14 / 20` | `500` | `Headline` | Card headers, window titlebar, setting row titles |
| **Body** | `14 / 20` | `400` | `Body` | Primary body copy, list labels, setting values (`13 px` in `FinnNavItem`) |
| **Caption / Label** | `12 / 16` | `400` | `Caption` / `label` | Card captions, metadata, secondary descriptions |
| **Micro** | `10–11 / 14–15` | `400`–`600` | `label_sm` / `detail` | Badges (`8.8 pt`), timestamps, keyboard shortcuts (never `< 10 px` on desktop/tablet/phone; `8 px` in Figma `11:274` is deprecated) |
| **Mono** | `9–10 pt / 14` | `400` | `Mono` | Terminal, version tags, dirty/edited badges, path previews |

---

## 4. Spacing Scale (4 px / 4 dp Grid — `finn::ui::spacing`)

Every margin, padding, and stack/grid gap must snap to the **4 px** scale (with `2 px` strictly reserved for hairline offsets or tight inline icon/text nudges).

| Token | Value | Canonical Usage |
|---|---|---|
| `sp_0` | `0 px` | Flush edge-to-edge split dividers |
| `sp_half` | `2 px` | Sub-label vertical gap (`FinnSettingRow`), scrollbar margin |
| `sp_1` | `4 px` | Compact tab/breadcrumb gaps, window-control inner padding |
| `sp_2` | `8 px` | Grid card gaps, dock icon-to-icon spacing target, toast vertical gap |
| `sp_3` | `12 px` | Standard card/popover inner padding, window outer margin, sidebar horizontal inset |
| `sp_4` | `16 px` | Page horizontal/vertical section padding, dock pill-to-pill target gap, modal button spacing |
| `sp_5` | `20 px` | Dialog margins, section breaks |
| `sp_6` | `24 px` | Page top padding (`FigmaSplit`), large card groups |
| `sp_8` | `32 px` | Empty-state breathing room, split-pane header offset |
| `sp_10` | `40 px` | Lock screen margins, OOBE column gutters |
| `sp_12` | `48 px` | Title bar height, OOBE major section spacing |
| `sp_16` | `64 px` | OOBE/tablet hero vertical rhythm |

*(Note: Existing Figma frames contain non-4px gaps such as `5/6/10/15 px`; see [`docs/figma-audit.md`](figma-audit.md) **FA-M6** for remediation.)*

---

## 5. Radii, Corner Smoothing, Blur & Elevation

### 5.1 Radii & Smoothing (`finn::ui::radii`)

| Token | Value | `kRadius*` Alias | Corner Smoothing | Surfaces |
|---|---|---|---|---|
| `r_xs` | `4.0 px` | `kRadiusSm` | `0.6` (spec) | Checkboxes, window control buttons, album-art inset, scrollbar thumb |
| `r_sm` | `6.0 px` | `kRadiusMd` | `0.6` (spec) | `FinnButton`, `FinnStatCard`, `FinnHeroCard`, `FinnSettingRow`, Control Center sub-tiles, `FinnWindowControls` pill |
| `r_md` | `8.0 px` | `kRadiusLg` | `0.6` (spec) | App icon tiles (`32×32`), hover highlight fills, `FinnCard` default, `FinnToast`, `FinnSurface` default |
| `r_lg` | `12.0 px` | `kRadiusXl` | `0.6` (spec) | `FinnWindow` frame, Dock pills (`44 px`), `HomeMenuPopover`, `SpotlightPopover` results, `ControlCenterPopover`, `OpenWindowsPopover` |
| `r_xl` | `20.0 px` | — | `0.6` (spec) | `NotificationCard`, Tablet/Phone modal sheets & full-width drawers |
| `r_hero` | `39.0 px` | — | `0.6` (spec) | OOBE hero cards (`ref 30`) |
| `r_pill` | `999.0 px` | — | `0.0` (capsule) | `FinnToggle`, pill `FinnSlider`, pill `FinnSearchField`, `FinnBadge`, OOBE `56×56` circle nav |

**Implementation status:** Qt `QPainter::drawRoundedRect` currently renders circular arcs (`smoothing = 0`). Continuous squircle rendering (`corner_smoothing = 0.6`) is **specified-only**.

### 5.2 Blur (`finn::ui::blur`)

| Token | Radius | Target Surfaces | Status |
|---|---|---|---|
| `blur_glass` | `35 px` | Dock pills, Control Center, Home Menu, Spotlight, Settings translucent shell | **Specified-Only** (`finn-comp` backdrop blur shader unimplemented; renders translucent `t.glass` fill) |
| `blur_notification` | `15 px` | `NotificationCard` | **Specified-Only** |
| `blur_progressive` | `20 px` | OOBE `Languages` bottom scroll fade (`37:96`) | **Implemented** (CPU `box_blur_image` in `apps/oobe/main.cpp:778`) |

### 5.3 Elevation (`finn::ui::elevation`)

FinnOS relies on **1 px `hair` borders + translucent layering** first, and subtle ambient shadows only for floating surfaces:

| Level | Token | Shadow Spec | Surfaces | Status |
|---|---|---|---|---|
| `0` | `elev_flat` | None (`1 px` `hair` optional) | Embedded lists, dividers, flat surfaces | Implemented |
| `1` | `elev_card` | None (`1 px` `hair` + top inner specular rim) | `FinnCard`, `FinnStatCard`, `FinnHeroCard`, `FinnSettingRow` | Implemented |
| `4` | `elev_window` | `(0, 4)` soft ambient shadow (`3%` Figma / 6-ring `QPainter` in `FinnWindow`) | `FinnWindow`, `FinnDialog` | Implemented on `FinnWindow` |
| `4` | `elev_popover` | `(0, 4)` blur `35 px`, `5%` alpha | Dock, `HomeMenuPopover`, `SpotlightPopover`, `ControlCenterPopover`, `OpenWindowsPopover` | **Specified-Only** (no compositor drop shadow on layer surfaces) |
| `4` | `elev_notification` | `(0, 4)` blur `25 px`, `5%` alpha | `NotificationCard`, `FinnToast` | **Specified-Only** |

---

## 6. Iconography

- **UI Glyphs (`assets/icons/shell/`):** Drawn on a **`24×24`** pixel grid with **`1.5 px` uniform stroke**, rounded caps and joins (`stroke-linecap="round"`, `stroke-linejoin="round"`), and **`stroke="currentColor"`** (or `#000000`) so `FinnThemeManager` tints them dynamically for Light/Dark/Accent/Disabled. (Existing SVGs with `14–60 px` viewBoxes or `1.4–3.0 px` strokes are logged in [`docs/figma-audit.md`](figma-audit.md) for normalization.)
- **App Icons (`assets/icons/apps/`):** Multi-color rounded-square app tiles (`r_md = 8 px` at `32 px`, `26×26` SVG source with `5.91 px` corner radius). `FinnThemeManager` never flattens multi-color app tiles.
- **Standard Rendered Sizes:**
  - `14 px`: chevrons, tiny inline indicators
  - `16 px`: `FinnButton`, `FinnIconButton` (toolbar), `FinnNavItem` plain, `FinnSettingRow` glyph
  - `17–20 px`: `FinnNavItem` badged glyph (`17 px` inside `26×26` circle), `FinnNavRail` (`20 px`), window title icon (`18–20 px`)
  - `22–24 px`: `NotificationCard` glyph, popover footer actions, `FinnHeroCard` mark, tablet/phone nav icons
  - `28–32 px`: Dock glyphs (`28 px`), Dock & Home app tiles (`32 px`)
  - `36–40 px`: `FinnEmptyState` (`36 px` inside `44 px` box), `NotificationCard` app tile (`40 px`)

---

## 7. Motion

Motion tokens, physics curves, and per-surface transitions are owned by [`docs/motion.md`](motion.md) and [`docs/motion-spec.md`](motion-spec.md):
- **Durations (`src/ui/finn/animation.hpp`):** `kMotionFast` (`140 ms`), `kMotionStandard` (`220 ms`), `kMotionSlow` (`320 ms`), `kMotionReducedCrossfade` (`80 ms <= 100 ms`).
- **Springs (`src/ui/finn/spring.hpp`):** `springs::kSnappy` (`484/44/1`), `springs::kSmooth` (`324/36/1`), `springs::kBouncy` (`360/24/1`), `springs::kGentle` (`196/28/1`), `springs::kOverscroll` (`280/32/1`), plus `rubber_band`, `PressFeedback`, and `GestureProgressTracker`.
- **Reduced Motion:** Configured live in Settings -> Accessibility (`reduced_motion=1`) or `FINN_REDUCED_MOTION=1`; replaces springs/slides/shakes with instant state changes or `<= 80 ms` opacity crossfades.

---

## 8. Component States Across Desktop, Tablet & Phone

Every interactive component in the FinnOS design language specifies **all 6 interactive states** plus **3 data states** (`Empty`, `Error`, `Loading`).

### 8.1 Interactive State Matrix

| Component | Default | Hover (Desktop / Pointer) | Pressed / Active | Disabled | Focus (Keyboard / Switch) | Selected / Checked |
|---|---|---|---|---|---|---|
| **`FinnButton` (Primary/Secondary/Ghost/Danger)** | Token fill (`accent` / `surface_input` / `transparent` / `danger`) | Lighten 10% (`Primary`), `surface_hover` (`Ghost`), `danger_hover` (`Danger`) | Darken 15% + `0.96` `PressFeedback` scale | `surface_canvas` bg, `mute` text/icon, no hover/cursor | `2 px` `accent` ring (`1.5 px` outer offset) | `tile_on` fill + `accent` border + `on` text |
| **`FinnIconButton`** | `transparent` bg, `ink` glyph | `surface_hover` (or `#FF0202` for window close) | `surface_pressed` + `0.96` scale | `mute` glyph (`40%` opacity on color icon) | `2 px` `accent` outer ring | `tile_on` bg + optional `accent` dot |
| **`FinnCard` / `FinnSettingRow`** | `tile` bg + `1 px` `hair` border | Subtle tile lighten (`clickable` only) | Light press tint + `0.98` card scale | `50%` content opacity, `surface_canvas` fill, non-interactive | `1.5 px` `accent` border | `tile_on` bg + `1.4 px` `accent` border |
| **`FinnToggle`** | `track` bg + `knob` left | Subtle `surface_hover` halo around knob | Knob widens slightly (`+2 px` spec) during press | `mute` track when on; `40%` alpha track+knob when off | `2 px` `accent` ring outside track | `accent` track + `knob` right |
| **`FinnSlider` (Standard / Pill)** | `track` bg + `accent`/`fill` bar + `knob` | Knob radius `7 -> 8 px` (`Standard`); subtle veil (`Pill`) | Active drag tracking (`kSnappy` settle) | `mute` fill, `50%` pill track, pointer disabled | `2 px` `accent` ring on thumb/pill | Value reflected continuously (`0–100%`) |
| **`FinnSearchField` / `FinnTextField`** | `surface_input` + `1 px` `hair` | `dim` border subtle darken (spec) | Caret active | `surface_canvas` bg + `mute` text | `1.5 px` `accent` border | Clear (`×`) icon visible when non-empty |
| **`FinnNavItem` / `FinnNavRail`** | `transparent` + `ink` text + `mute`/`accent` icon | `surface_hover` pill | `surface_pressed` | `mute` text + `40%` icon | `1.5 px` `accent` inner border | `tile_on` / `nav_sel` pill + `on` text (+ left `accent` bar on `FinnNavRail`) |
| **Dock App / Control Tile** | `glass` pill / app icon | `-3.5 px` lift + hover veil (desktop); disabled on touch | `0.92` scale + pressed veil | Dimmed (`45%` alpha) + "Unavailable" subtitle | `2 px` `accent` ring | Running/active `accent` bar or `tile_on` pill |

### 8.2 Data States (Required on All Content Surfaces)

| State | Pattern | Component / Rules |
|---|---|---|
| **Empty** | Centered `FinnEmptyState` (`36 px` muted icon, `Headline` title, `Body`/`dim` explanation, optional single `Primary` action `FinnButton`). | Implemented in `libfinnui`; required in every list/grid/search view. |
| **Error** | Inline `FinnToast(Error)` for transient action failures; persistent inline banner (`danger` 15% tint + `1 px` `danger` border + retry action) for blocking load failures. **Never** fade errors gently or fake success (`docs/motion.md`). | `FinnToast` implemented; inline error banner **specified-only**. |
| **Loading** | Determinate `FinnProgressBar` when progress is known; `FinnProgressRing` (or subtle static skeleton tile under `reduced_motion`) when indeterminate. | Implemented in `libfinnui`. |

### 8.3 Sizing Adjustments by Form Factor

| Dimension | Desktop (`>= 1025 dp`) — *Implemented* | Tablet (`600–1024 dp`) — *Specified-Only* | Phone (`< 600 dp`) — *Specified-Only* |
|---|---|---|---|
| **Button height** | `32 px` (`FinnButton`) | `40 dp` | `44 dp` |
| **Input / Search height** | `30–36 px` | `40 dp` | `44 dp` |
| **Nav row / Setting row min height** | `34–52 px` | `44–56 dp` | `48–64 dp` |
| **Toggle size** | `38×22 px` | `44×26 dp` | `50×28 dp` |
| **Dock / Bottom Bar height** | `44 px` centered 4-pill dock | `56 dp` centered dock or auto-hide rail | `24 dp` gesture pill / `56 dp` bottom tab bar |

---

## 9. Tablet Patterns (`600–1024 dp` — Specified-Only)

1. **Adaptive Sidebar & Rail (`FinnSidebar` <-> `FinnNavRail`):**
   - At `840–1024 dp` (landscape tablet): persistent `250 dp` `FinnSidebar` + `1 px` `hair` vertical divider + fluid content pane (`FigmaSplit`).
   - At `600–839 dp` (portrait tablet): `FinnSidebar` collapses by default into a `68 dp` `FinnNavRail` (already available in `libfinnui`) or an overlay drawer (`sp_3` padding, `r_lg` `12 dp` radius, `35 px` blur) toggled from the top-left `sidebar-toggle` button.
2. **Two-Pane Split View:**
   - Master list (`300–340 dp` min width) + detail pane (`>= 320 dp`) separated by a `1 px` `FinnDivider`. Selecting an item updates the detail pane via `FinnPageStack`; on portrait narrowing `< 600 dp`, the split collapses into a push/pop stack.
3. **Floating & Snapped Windows:**
   - Default apps open maximized or in balanced 2-app half-split (`50% / 50%` or `60% / 40%` with `8 dp` gutter).
   - Floating windows (`r_lg = 12 dp`, `elev_window`, min `360×320 dp`) support drag by a `44 dp` top handle bar and edge-snap gestures using `GestureProgressTracker` + `springs::kSmooth`.
4. **System Overlays:**
   - Control Center (`444×300 dp`), Home Menu (grid up to `585 dp`), and Spotlight (`350 dp`) anchor centered or top-right (respecting safe-area insets) with tap-outside scrim dismissal.

---

## 10. Phone Patterns (`< 600 dp` — Specified-Only)

1. **Single-Column Navigation & Stack:**
   - One visible pane at a time inside `FinnPageStack`. Multi-column desktop grids (`FinnStatCard` 2×2, `HomeMenu` 4-col) reflow to `1` or `2` columns with `16 dp` (`sp_4`) horizontal page margins.
   - `FinnSidebar` becomes the root index screen; tapping an item pushes the detail screen with a `44 dp` back-button header.
2. **Bottom Navigation & Action Bar:**
   - Primary app sections (3–5 destinations) live in a `56 dp` bottom bar (`glass` fill, `1 px` `hair` top border, `44×44 dp` targets, icon + `11 sp` label).
   - System navigation uses a bottom gesture handle (`134×4 dp` pill centered `8 dp` above bottom edge) for Home (swipe up), App Switcher (swipe up & pause via `springs::kSmooth`), and Back (edge swipe with `rubber_band` preview).
3. **Bottom Sheets (`r_xl = 20 dp` top corners):**
   - Popovers (`Control Center`, `Spotlight`, context menus, `FinnDialog` confirmations) present as full-width bottom sheets anchored to the bottom edge with a `36×4 dp` drag handle (`sp_2` top inset), swipe-to-dismiss via `GestureProgressTracker` + `springs::kSnappy`, and safe-area bottom padding.
4. **Control Center & Notifications on Phone:**
   - **Notifications:** Full-width (`screen_width - 32 dp`), `r_xl = 20 dp`, `sp_3` (`12 dp`) inner padding, swipe-up or swipe-horizontal to dismiss.
   - **Control Center:** Accessed via top-right swipe-down sheet; 2-column quick toggles (`Wi-Fi`, `Bluetooth`, `Dark Mode`, etc.) + full-width `44 dp` pill sliders (`Brightness`, `Volume`).
5. **Scroll & Overscroll:**
   - All scrollable lists use touch drag + `project_fling` + `rubber_band` overscroll + `springs::kOverscroll` return (hard-clamped when `reduced_motion` is enabled).
