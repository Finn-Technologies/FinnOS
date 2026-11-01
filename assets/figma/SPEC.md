# FinnOS Figma Design Specification

Exact design values extracted from Figma export file **FinnOS - Abhi**, page **Desktop** (id `2:283`).
All numerical values and property structures are literal values read directly from the Figma export manifest and JSON.

---

## 1. Output & Resolution Specification

- **Canvas Logical Dimensions**: `1280` x `832` (MacBook Air 13" aspect ratio)
- **HiDPI Render Target**: `2560` x `1664` at 2.0x integer scale factor (`choose_output_scale`)
- **Default Headless Resolution**: `2560` x `1664` @ 60Hz
- **Wallpaper Asset**: `image_9b926d601e27c4a5c897d2df2e024cc13d5da006.png` decoded to `/usr/share/finn/wallpaper/default.webp`

---

## 2. Bottom Taskbar (Dock Group)

Container width: `713`, Height: `44`, Position: centered horizontally `(width - 713) / 2`, `y = height - 44 - 15` (15px margin from bottom).
Contains 4 glass pills with 15px inter-pill spacing:

### A. Apps Pill
- **Dimensions**: `254` x `44`
- **Corner Radius**: `12`
- **Fill**: Solid White (`#FFFFFF`) with `0.70` opacity (dark: `#23262E` @ 70%)
- **Effect**: `BACKGROUND_BLUR` radius `35`
- **Slots**: 6 app icon slots
  - Slot size: `32` x `32`, Corner Radius: `8`
  - Gap: `10`, Left/Right padding: `6`, Top/Bottom padding: `6`
  - Active indicator: `16` x `2` blue bar (`#006FFF`) centered at `y + 40`

### B. Home Buttons Pill
- **Dimensions**: `130` x `44`
- **Corner Radius**: `12`
- **Fill**: Solid White (`#FFFFFF`) with `0.70` opacity
- **Effect**: `BACKGROUND_BLUR` radius `35`
- **Buttons** (28 x 28 each, padding: 8px left/right, gap: 15px):
  1. `search-01.svg` (Search toggle)
  2. `home-09.svg` (Home Menu toggle)
  3. `view-open-windows.svg` (Task view / Cycle windows)
- **Selected State**: 36 x 36 rounded rectangle (r=8) highlight under active button

### C. Hardware Options Pill
- **Dimensions**: `130` x `44`
- **Corner Radius**: `12`
- **Fill**: Solid White (`#FFFFFF`) with `0.70` opacity
- **Effect**: `BACKGROUND_BLUR` radius `35`
- **Status Indicators** (28 x 28 each, padding: 8px left/right, gap: 15px):
  1. `volume-02.svg`
  2. `wifi-02.svg`
  3. `battery.svg`
- **Selected State**: 124 x 38 rounded rectangle (r=9) highlight when Control Center is active

### D. Time Pill
- **Dimensions**: `154` x `44`
- **Corner Radius**: `12`
- **Fill**: Solid White (`#FFFFFF`) with `0.70` opacity
- **Effect**: `BACKGROUND_BLUR` radius `35`
- **Typography**: `Fixel Display`, 13px, color: ink (`#000000` / `#F4F0E8`)
- **Format**: Live date and 24h clock, e.g. `Sat 31 Oct  12:48`

---

## 3. Home Menu Popover (Node 4:24 / 4:60)

- **Dimensions**: `585` x `460`
- **Corner Radius**: `12`
- **Position**: Centered horizontally `(width - 585) / 2`, `y = dock_y - 15 - 460`
- **Fill**: Solid White (`#FFFFFF`) with `0.70` opacity
- **Effect**: `BACKGROUND_BLUR` radius `35`
- **Sections**:
  1. **Your favorite apps**:
     - 2 columns x 5 rows grid of 10 pinned apps (130 x 32 each)
     - 32 x 32 rounded icon container (r=8) with title label
  2. **Divider**:
     - Vertical separator at `x = 292`, height 205px
  3. **All Apps**:
     - 3 columns x 2 rows grid of 6 category cards (78 x 78 folder previews + label)
     - Categories: Productivity, System, Media, Utilities, Internet, Other
  4. **Recent activity**:
     - 4 columns x 2 rows grid of 8 activity items (132 x 32 each)
     - 32 x 32 icon, Title ("App/File"), Subtitle ("Last activity")
  5. **Bottom Bar** (585 x 50 at y+410):
     - User Avatar: 30 x 30 circle in `#1F4FFF` with white user glyph
     - Username: Login name ("Abhi") in Fixel Display 14px
     - Session controls: Restart (`refresh.svg`, 22x22) and Power (`power.svg`, 22x22)

---

## 4. Search / Spotlight Overlay (Node 4:226 / 5:16 / 5:2)

- **Search Bar**:
  - Dimensions: `300` x `44`, Corner Radius: `12`
  - Position: Centered horizontally `(width - 300) / 2`, `y = height * 0.28` (or 250 on 832h)
  - Fill: Solid White (`#FFFFFF`) with `0.70` opacity, `BACKGROUND_BLUR` 35
  - Icon: `search-01.svg` (28 x 28)
  - Placeholder: "Search or ask anything" (13px, mute)
- **Recommendations Card**:
  - Dimensions: `350` x `228`, Corner Radius: `12`
  - Position: Centered horizontally `(width - 350) / 2`, `y = bar.y + 44 + 15`
  - Fill: Solid White (`#FFFFFF`) with `0.70` opacity, `BACKGROUND_BLUR` 35
  - Recommendations header + row of 5 recommended apps (32 x 32 icons)
  - Recently asked header + 2x2 grid of 4 query chips (146 x 32 each)

---

## 5. Control Center Popover (Node 11:19 / 11:216)

- **Dimensions**: `444` x `300`
- **Corner Radius**: `12`
- **Position**: Right-aligned with taskbar group, `y = dock_y - 15 - 300`
- **Fill**: Solid White (`#FFFFFF`) with `0.70` opacity, `BACKGROUND_BLUR` 35
- **Cards**:
  1. **Wi-Fi Card**: `207` x `42` (r=6), blue circle icon (`#006FFF`) + "Wi-Fi" + "Home Wi-Fi"
  2. **Bluetooth Card**: `207` x `42` (r=6), gray circle icon (`#8B9099`) + "Bluetooth" + "Off"
  3. **Media Control Card**: `211` x `90` (r=6), album cover `78` x `78` + "Song" + "Author" + progress bar + "00:56"
  4. **Controls Card**: `424` x `180` (r=8)
     - Interactive Display Brightness Slider with `sun-01.svg`
     - Interactive Volume Slider with `volume-02.svg`
     - Live Dark Appearance Toggle Switch with `moon.svg` / `sun-01.svg`
