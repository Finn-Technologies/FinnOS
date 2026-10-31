# FinnOS Figma Design Specification

Exact design values extracted from Figma file **FinnOS**, page **FinnOS - Desktop** (id `44:2`).
All numerical values and property structures are literal values read directly from Figma node data via Figwright. Nothing has been guessed. Where a property is not present on a node, it is explicitly marked as `not found`.

---

## 1. Dock

Values inspected from Dock nodes `186:4173`, `186:4231`, `218:25`, `218:84`, `186:4254`, `186:4272`:

- **Width**: `116`
- **Height**: `42`
- **Corner Radius**: `8`
- **Corner Smoothing**: `0.6000000238418579` (Figma iOS-style squircle smoothing)
- **Fills**:
  - Type: `SOLID`
  - Color: `{"r": 1, "g": 1, "b": 1}` (`#FFFFFF`)
  - Opacity: `0.6499999761581421` (~65%)
- **Strokes**: `not found`
- **Shadow**: `not found`
- **Blur Radius**:
  - Effect Type: `BACKGROUND_BLUR`
  - Radius: `15`
- **Clips Content**: `true`

---

## 2. Taskbar

Values inspected from Taskbar nodes `186:4240`, `186:3891`, `218:93`, `240:276`:

### Taskbar Container
- **Width**: `115`
- **Height**: `42`
- **Corner Radius**: `8`
- **Corner Smoothing**: `0.6000000238418579`
- **Fills**:
  - Type: `SOLID`
  - Color: `{"r": 1, "g": 1, "b": 1}` (`#FFFFFF`)
  - Opacity: `0.6499999761581421` (~65%)
- **Strokes**: `not found`
- **Shadow**: `not found`
- **Blur Radius**:
  - Effect Type: `BACKGROUND_BLUR`
  - Radius: `15`

### Taskbar App Icons
- **Icon Container Width**: `26`
- **Icon Container Height**: `26`
- **Icon Container Corner Radius**: `5.909091472625732`
- **Icon Container Corner Smoothing**: `0.6000000238418579`
- **Icon Container Stroke**:
  - Type: `SOLID`
  - Color: `{"r": 0, "g": 0, "b": 0}` (`#000000`)
  - Opacity: `0.10000000149011612` (10%)
  - Stroke Weight: `0.5416666865348816`
  - Stroke Align: `INSIDE`
- **Inner Icon Glyph Sizes**:
  - `settings-02` (node `186:4242`): `18.35293960571289` x `18.35293960571289` (target aspect ratio: 24 x 24)
  - `folder-library` (node `186:4249`): `18.352941513061523` x `18.352941513061523` (target aspect ratio: 24 x 24)
  - `image 1` (Arc raster, node `186:4247`): `20` x `16` (target aspect ratio: 3840 x 3185)
- **Active App Background**:
  - Node: `bg` (node `186:3892` inside `186:3891`)
  - Size: `34` x `34` (positioned at `x: 77, y: 4`)
  - Corner Radius: `4` (cornerSmoothing: `0.6000000238418579`)
  - Fill: `SOLID` White (`{"r": 1, "g": 1, "b": 1}`) opacity `1`

### Indicator (Running App Indicator)
- **Node ID**: `186:3893` (inside Taskbar `186:3891`, under active Settings icon)
- **Type**: `VECTOR`
- **Position**: `x: 92, y: 40`
- **Width**: `4`
- **Height**: `0`
- **Stroke**:
  - Type: `SOLID`
  - Color: `{"r": 0.12090595066547394, "g": 0.3099111318588257, "b": 1}` (`#1F4FFF`)
  - Opacity: `1`
  - Stroke Weight: `1.5`
  - Stroke Align: `CENTER`
  - Stroke Cap: `ROUND`
- **Fills**: `not found`
- **Shadow**: `not found`

---

## 3. Time & Weather Pill

Values inspected from Time & Weather nodes `186:4183`, `218:36`, `186:3989`, `186:4050`:

- **Width**: `139`
- **Height**: `42`
- **Corner Radius**: `8`
- **Corner Smoothing**: `0.6000000238418579`
- **Fills**:
  - Type: `SOLID`
  - Color: `{"r": 1, "g": 1, "b": 1}` (`#FFFFFF`)
  - Opacity: `0.6499999761581421` (~65%)
- **Strokes**: `not found`
- **Shadow**: `not found`
- **Blur Radius**:
  - Effect Type: `BACKGROUND_BLUR`
  - Radius: `15`
- **Elements Inside**:
  - **Clock ("15:48")** (node `186:4190`):
    - Position: `x: 10, y: 16`
    - Size: `39` x `11`
    - Font Family: `Fixel Display`
    - Font Style: `Regular`
    - Font Size: `16`
    - Color: `{"r": 0, "g": 0, "b": 0}` (`#000000`)
  - **Weather Icon ("sun-03")** (node `186:4187`):
    - Position: `x: 60, y: 9`
    - Size: `24` x `24`
    - Color: `{"r": 1, "g": 0.8364708423614502, "b": 0.10802283883094788}` (`#FFD51B`)
    - Stroke Weight: `1.6363636255264282`
  - **Weather Text Container** (node `186:4184`):
    - Position: `x: 93, y: 11`
    - Size: `36` x `20`
    - Auto Layout: `VERTICAL`, `itemSpacing: 5`, `primaryAxisAlignItems: CENTER`
    - **Temperature ("23°C")** (node `186:4185`):
      - Font Family: `Fixel Display`
      - Font Style: `Regular`
      - Font Size: `12`
      - Color: `{"r": 0.16746169328689575, "g": 0.16746169328689575, "b": 0.16746169328689575}`
    - **Condition ("Sunny")** (node `186:4186`):
      - Font Family: `Fixel Display`
      - Font Style: `Regular`
      - Font Size: `10`
      - Color: `{"r": 0.37620192766189575, "g": 0.37620192766189575, "b": 0.37620192766189575}`

---

## 4. Window

Values inspected from Settings Window (`186:3906`), Empty Windows (`186:4019`, `186:4029`), and Prototype Frame 2 (`240:270`):

- **Corner Radius**: `8`
- **Corner Smoothing**: `0.6000000238418579`
- **Fills**:
  - Settings Window (`186:3906`): `SOLID` White (`{"r": 1, "g": 1, "b": 1}`), opacity `0.699999988079071` (70%)
  - Empty Window (`186:4019`): `SOLID` White (`{"r": 1, "g": 1, "b": 1}`), opacity `1` (100%)
  - Prototype Shell Frame 2 (`240:270`): `SOLID` White (`{"r": 1, "g": 1, "b": 1}`), opacity `0.800000011920929` (80%)
- **Strokes**: `not found`
- **Shadow**:
  - Effect Type: `DROP_SHADOW`
  - Radius: `250`
  - Color: `{"r": 0, "g": 0, "b": 0, "a": 0.15000000596046448}` (`rgba(0, 0, 0, 0.15)`)
  - Offset X: `0`
  - Offset Y: `4`
  - Spread: `0`
- **Blur Radius**:
  - Effect Type: `BACKGROUND_BLUR`
  - Radius: `15` (nodes `186:3906`, `186:4019`); `40` (prototype shell node `240:270`)
- **Titlebar Height**:
  - `not found` as a distinct structural frame. In the FinnOS window chrome design, windows do not have a dedicated monolithic titlebar rectangle spanning the top. Instead, windows are borderless client canvases with floating pill-style window control frames:
    - Floating pill `window-controls-frame` (node `240:352` / `240:355`): width `83`, height `24`, positioned at top-right with margin top `8`, margin right `8` (`x: 601, y: 8` within width `692`).
    - Alternate floating pill `Window Options` (node `186:3932` / `186:4020`): width `72`, height `24`, cornerRadius `4.139999866485596`.

---

## 5. Window Controls

Values inspected from Component Set `close-button-frame` (`240:174`) and Assembly Component `window-controls-frame` (`240:352`):

### Floating Container (`window-controls-frame`, node `240:352`)
- **Width**: `83`
- **Height**: `24`
- **Corner Radius**: `4`
- **Fill**: `SOLID` White (`{"r": 1, "g": 1, "b": 1}`), opacity `1`
- **Shadow**:
  - Effect Type: `DROP_SHADOW`
  - Radius: `20`
  - Color: `{"r": 0, "g": 0, "b": 0, "a": 0.10000000149011612}` (`rgba(0, 0, 0, 0.10)`)
  - Offset X: `0`
  - Offset Y: `4`
  - Spread: `0`
- **Auto Layout**:
  - Mode: `HORIZONTAL`
  - Padding Top: `2`
  - Padding Right: `2`
  - Padding Bottom: `2`
  - Padding Left: `2`
  - Item Spacing: `2`
  - Primary Axis Align: `MIN`
  - Counter Axis Align: `MIN`

### Individual Button Dimensions (All Variants)
- **Width**: `25`
- **Height**: `20`
- **Corner Radius**: `2`
- **Auto Layout**:
  - Mode: `HORIZONTAL`
  - Padding (Top/Right/Bottom/Left): `8`
  - Item Spacing: `8`
  - Align Items: `CENTER`
- **Inner Icon Frame Size**: `14` x `14`

### Button Variants & Colors

| Variant Name | Node ID | State | Background Fill(s) | Glyph Stroke Color | Glyph Stroke Weight | Glyph Details |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `type=minimize, hover?=no` | `240:175` | Default | `SOLID` White (`#FFFFFF`, opacity `1`) | `{"r": 0.0784313753247261, "g": 0.10588235408067703, "b": 0.20392157137393951}` (`#141B34`), opacity `1` | `1.1666666269302368` | `minus-sign` (node `240:185`), vector width `9.333333015441895`, height `0` |
| `type=minimize, hover?=yes` | `240:217` | Hover | 1. `SOLID` White (`#FFFFFF`, opacity `1`)<br>2. `SOLID` Black (`#000000`, opacity `0.029999999329447746`) | `{"r": 0.0784313753247261, "g": 0.10588235408067703, "b": 0.20392157137393951}` (`#141B34`), opacity `1` | `1.1666666269302368` | `minus-sign` (node `240:218`) |
| `type=maximize, hover?=no` | `240:173` | Default | `SOLID` White (`#FFFFFF`, opacity `1`) | `{"r": 0.0784313753247261, "g": 0.10588235408067703, "b": 0.20392157137393951}` (`#141B34`), opacity `1` | `1.1666666269302368` | `expand` (node `240:181`), two corner vectors each `4.083333492279053` x `4.083333492279053` |
| `type=maximize, hover?=yes` | `240:208` | Hover | 1. `SOLID` White (`#FFFFFF`, opacity `1`)<br>2. `SOLID` Black (`#000000`, opacity `0.029999999329447746`) | `{"r": 0.0784313753247261, "g": 0.10588235408067703, "b": 0.20392157137393951}` (`#141B34`), opacity `1` | `1.1666666269302368` | `expand` (node `240:209`) |
| `type=close, hover?=no` | `240:178` | Default | `SOLID` White (`#FFFFFF`, opacity `1`) | `{"r": 0.0784313753247261, "g": 0.10588235408067703, "b": 0.20392157137393951}` (`#141B34`), opacity `1` | `1.1666666269302368` | `close-button` (node `240:179`), vector size `7` x `7` |
| `type=close, hover?=yes` | `240:223` | Hover | `SOLID` Red (`{"r": 1, "g": 0.0084306001663208, "b": 0.0084306001663208}`, `#FF0202`), opacity `1` | `{"r": 1, "g": 1, "b": 1}` (`#FFFFFF`), opacity `1` | `1.1666666269302368` | `close-button` (node `240:224`) |

---

## 6. Sidebar Rules (Node `244:391`)

Values inspected from sticky note node `244:391` and referenced frame `app-sidebar` (`244:388`):

### Sticky Note Container (`244:391`)
- **Width**: `240`
- **Height**: `239`
- **Corner Radius**: `16`
- **Corner Smoothing**: `0.6000000238418579`
- **Fill**: `SOLID` `{"r": 0.9912352561950684, "g": 0.9098843932151794, "b": 0.6861693859100342}` (`#FDE8AF`)
- **Padding**: `4`

### Inner Card (`244:392`, `Frame 3`)
- **Width**: `232`
- **Height**: `231`
- **Corner Radius**: `12`
- **Corner Smoothing**: `0.6000000238418579`
- **Fill**: `SOLID` `{"r": 0.9431551694869995, "g": 0.8444998860359192, "b": 0.5773084163665771}` (`#F0D793`)
- **Auto Layout**: `VERTICAL`, `padding: 16`, `itemSpacing: 8`

### Exact Note Text
1. **Title** (node `244:393`):
   - Characters: `"Sidebar Rules:"`
   - Font Family: `Inter`
   - Font Style: `Bold`
   - Font Size: `16`
   - Line Height: `27` (unit: `PIXELS`)
   - Fill: `SOLID` Black (`#000000`), opacity `1`
2. **Rule Body** (node `244:394`):
   - Characters: `"Top left area will be always reserved for App’s name (like Windows)"`
   - Font Family: `Inter`
   - Font Style: `Medium`
   - Font Size: `14`
   - Line Height: `21` (unit: `PIXELS`)
   - Fill: `SOLID` Black (`#000000`), opacity `0.699999988079071` (70%)

### Reference Implementation: `app-sidebar` (node `244:388`)
- **Width**: `187`
- **Height**: `431`
- **App Name Area** (node `244:390`):
  - Position: `x: 22, y: 18` (top-left margin: 22px left, 18px top)
  - Characters: `"App Name"`
  - Font Family: `Inter`, Style: `Bold`, Size: `16`
- **Sidebar Content Area** (node `244:389`):
  - Position: `x: 39, y: 207`
  - Characters: `"Sidebar content"`
  - Font Family: `Inter`, Style: `Bold`, Size: `14`

---

## 7. Control Center Panel

Values inspected from Control Center container node `Home` (`218:44`) inside frame `Desktop - Control Center` (`218:24`):

### Panel Container (`Home`, node `218:44`)
- **Width**: `405`
- **Height**: `500`
- **Corner Radius**: `8`
- **Corner Smoothing**: `0.6000000238418579`
- **Fills**:
  - Type: `SOLID`
  - Color: `{"r": 1, "g": 1, "b": 1}` (`#FFFFFF`)
  - Opacity: `0.6499999761581421` (~65%)
- **Strokes**: `not found`
- **Shadow**:
  - Effect Type: `DROP_SHADOW`
  - Radius: `100`
  - Color: `{"r": 0, "g": 0, "b": 0, "a": 0.029999999329447746}` (`rgba(0, 0, 0, 0.03)`)
  - Offset X: `0`
  - Offset Y: `4`
  - Spread: `0`
- **Blur Radius**:
  - Effect Type: `BACKGROUND_BLUR`
  - Radius: `15`

### Spacing & Children Layout

- **Container Padding**: `10` from panel boundaries.
- **Top Row - Network Tiles**:
  - `Wi-Fi` (node `218:78`):
    - Position: `x: 10, y: 10`
    - Size: `188` x `40`
    - Corner Radius: `4.139999866485596`
  - `Bluetooth` (node `218:72`):
    - Position: `x: 207, y: 10`
    - Size: `188` x `40`
    - Corner Radius: `4.139999866485596`
  - **Horizontal Spacing between Wi-Fi and Bluetooth**: `9` (`207 - (10 + 188) = 9`)
- **Second Row - Brightness & Mode**:
  - **Vertical Spacing below Network row**: `10` (`60 - (10 + 40) = 10`)
  - `Brightness Slider` (node `218:67`):
    - Position: `x: 10, y: 60`
    - Size: `345` x `35`
    - Corner Radius: `100` (pill)
  - `Dark/Light toggle` (node `218:63`):
    - Position: `x: 360, y: 60`
    - Size: `35` x `35`
    - Corner Radius: `100` (circular pill)
  - **Horizontal Spacing between Brightness and Dark/Light toggle**: `5` (`360 - (10 + 345) = 5`)
- **Third Row - Volume**:
  - **Vertical Spacing below Brightness row**: `10` (`105 - (60 + 35) = 10`)
  - `Volume Slider` (node `218:57`):
    - Position: `x: 10, y: 105`
    - Size: `385` x `35`
    - Corner Radius: `100` (pill)
- **Bottom Row - Account Options**:
  - `Account Options` (node `218:45`):
    - Position: `x: 10, y: 450`
    - Size: `385` x `40`
    - Corner Radius: `4.139999866485596`
    - **Bottom Margin to Panel Bottom**: `10` (`500 - (450 + 40) = 10`)

---

## 8. Fonts and Font Sizes

Values extracted across all 88 `TEXT` nodes on the page:

### Operating System UI (Font Family: `Fixel Display`)

| Family | Style | Font Size | Line Height | Usage Example | Node ID |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `Fixel Display` | `Regular` | `10` | `AUTO` | Secondary weather condition (`"Sunny"`) | `186:3877` |
| `Fixel Display` | `Regular` | `12` | `AUTO` | Weather temperature (`"23°C"`), Empty window label (`"Empty window"`), Hardware specs (`"NVIDIA RTX 3050"`, `"Intel Core i3 4130"`, `"512GB (286GB free)"`, `"8GB DDR4 2130mhz"`) | `186:3876`, `186:4028`, `186:3910` |
| `Fixel Display` | `Regular` | `14` | `AUTO` | User account name (`"Abhi"`), App titles in menus (`"Settings"`, `"Files"`, `"Arc"`) | `218:136`, `218:169` |
| `Fixel Display` | `Regular` | `16` | `AUTO` | Clock (`"15:48"`), Section header (`"Settings"`), System branding segment (`"FinnOS "`) | `186:3881`, `186:3977`, `186:3925` |
| `Fixel Display` | `Light` | `16` | `AUTO` | System branding subtitle segment (`"One Beta"`) | `186:3925` (seg 2) |

### Design Canvas / Annotations / Prototyping (Font Family: `Inter`)

| Family | Style | Font Size | Line Height | Usage Example | Node ID |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `Inter` | `Medium` | `10` | `AUTO` | Component variant annotation labels (`"Hover"`, `"Default"`, `"State"`, `"Type"`, `"Maximize"`, `"Minimize"`, `"Close"`) | `240:212`, `240:213`, `240:353` |
| `Inter` | `Medium` | `14` | `21px` | Sticky note rule explanation (`"Top left area will be always reserved for App’s name (like Windows)"`) | `244:394` |
| `Inter` | `Bold` | `14` | `AUTO` | Prototype placeholder (`"Sidebar content"`) | `244:389` |
| `Inter` | `Bold` | `16` | `27px` | Sticky note heading (`"Sidebar Rules:"`) | `244:393` |
| `Inter` | `Bold` | `16` | `AUTO` | Window shell prototype placeholders (`"App Name"`, `"Sidebar"`, `"Content"`) | `240:390`, `240:338`, `240:339` |
| `Inter` | `Bold` | `27` | `AUTO` | Section label (`"Prototype"`) | `240:387` |
| `Inter` | `Bold` | `56` | `AUTO` | Section heading (`"Window Controls buttons"`, `"Window Controls - Component"`) | `240:165`, `240:340` |
| `Inter` | `Bold` | `64` | `AUTO` | Top-level section title (`"Window Shell/Chrome"`) | `240:145` |
