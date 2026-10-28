# The FinnOS design system

This is the design language of the shell: the palette, the type, the
radii, the spacing and the anatomy of each surface. It is written from the checked-in theme tokens in `ui/_tokens.fui`; the
compositor maps those tokens into its drawing code. The live FinnOS Figma
file currently has seven named top-level frames on Page 1 and no Variables
collection. It contains Boot, Desktop, Home, Control Center, Empty Window,
stacked Empty Windows and Settings; there is no Store or compatibility-app
frame. Finn Store's redesign is therefore a new FinnOS design decision rather
than a direct Figma copy. Layer-level colors and effects still need sampling
for full palette parity; source comparisons and runtime screenshots are still
needed before the OS can be called design-complete.

## Measured Figma frame geometry

All seven named top-level frames measure 1280 × 832. Their canvas positions
use 40-unit gutters: Boot at (0, 0), Desktop at (1320, 0), Desktop - Home at
(2640, 0), Desktop - Control Center at (3960, 0), Empty Window at (1320, 872),
Empty Window behind an Empty Window at (2640, 872), and Settings at (1320, 1744).
These are Figma canvas placements, not component offsets within FinnOS.

It is not a wish list. Where a token exists but nothing draws with it yet,
this document says so; where the code has to work around a limitation, it
says that too.

The motion is a separate document, because it is a separate kind of
decision: [motion.md](motion.md).

---

## The shape of the thing

FinnOS's shell is four surfaces over a wallpaper:

| Surface | What it is | Where it lives |
|---|---|---|
| the dock | the chrome at the bottom of the screen | `finndock.c` |
| the Control Center | the panel that opens from the tray | `finncontrol.c` |
| the clock page | the panel that opens from the clock | `finnclock.c` |
| the Start Menu | the system's front door | `finnmenu.c` |

They are not windows. Windows are a separate surface (`finnwindow`), and
these are *surfaces*: rasterised into transparent layers and composited
into the same frame as the wallpaper, so a surface costs a blit rather than
a round trip over a socket.

They share one material (frosted glass), one set of corner radii, one type
scale and one set of timings. A surface that invented its own would read
as a different operating system.

### The material

The material is **glass**: the blurred wallpaper brought toward bone.

The shared material tokens are defined in `ui/_tokens.fui`; the compositor
uses the panel weight directly and derives the second tile pass so the
resulting density matches the theme:

| Token | Weight | What it is |
|---|---:|---|
| `glass_panel` | **0.65** | the panel's first pass |
| `glass_tile_pass` | **0.5714286** | the second pass over a panel |
| `glass_tile` | **0.85 effective** | the resulting density after both passes |
| `glass_well` | **0.53** | the dock's inset, a second pass over the pill |

The tile is not a second colour. `finn_frost_rrect()` mixes toward the
tint over whatever the layer already carries, so the density compounds:
0.65 + (1 − 0.65) × 0.5714286 ≈ **0.85**. It is the same glass, laid down
twice, and it deepens the way glass does.

The dock uses the same 0.65 panel pass, and the tray's well is a second
pass at 0.53 over it: 0.65 + (1 − 0.65) × 0.53 ≈ **0.84**.

The window surface has its own Figma-authored weight: **0.70** for the body,
over a 15-unit backdrop blur. The Window Options pill is not a second frost;
it is a solid `#F9F9F9` chip lifted off the sheet. In the dark appearance the
sheet is laid heavier, at **0.84**: at 0.70 the daylight in the wallpaper is
still too strong under white ink, and at 1.0 the sheet stops being glass at
all. The sidebar keeps that sheet; a content column that has to be read —
Settings' pages, the Files listing — is opaque paper on top of it, not more
glass.

The **Window** and **Home** surfaces do not share a shadow. Figma gives the
window a 15% black shadow at (0, 4) with blur 250, because it floats above the
desktop; Home takes the shell's quieter 3% shadow at (0, 4) with blur 100.
Both use the same uniform 15-unit background blur. Window rendering applies
the shadow beneath floating windows and omits it for maximized windows; Home
paints its shadow beneath the panel, including while the panel animates open.
Home's fill is 65%, while the Window body is 70%. The existing
1/8-resolution, three-pass backdrop blur is scaled to the Figma 15-unit
radius. This is a measured per-surface effect, not a global elevation token;
other surfaces should only gain shadows when their own Figma layer effects
are measured.

### The glass tint

The tint is not the same in the two appearances, and the reason is worth
stating because it is counter-intuitive:

- **light**: near-white (`#FFFFFF`). Frosted glass over a bright wallpaper
  gets *brighter* where it is denser.
- **dark**: the deep bone (`#23262E`). Frosted glass over a dark wallpaper
  gets *darker* where it is denser.

"Blurred toward bone" would make the dark appearance a milky panel, which
is the one thing a dark shell must not look like.

---

## The palette

The palette is specified by `ui/_tokens.fui` and mapped into the shared
C globals in `src/compositor/finndraw.c`. A regression test checks both
appearances against the token file so those copies cannot quietly drift.
The globals are selected at runtime because switching light/dark swaps
every surface at once.

### Light

| Token | Value | What it is for |
|---|---|---|
| `bone` | `#FFFFFF` | the chrome surface |
| `tile` | `#FFFFFF` | an inset tile |
| `tile_on` | `#D3E6FF` | a tile carrying an active control |
| `hair` | `#1A000000` | the hairline: a rule at 10% black |
| `ink` | `#000000` | primary ink: names, values, the wordmark |
| `mute` | `#969696` | secondary ink: inactive glyphs, detail lines |
| `dim` | `#5D5C5C` | detail text — stronger than mute, weaker than ink |
| `accent` | `#006FFF` | the family accent |
| `track` | `#40000000` | a 25% black slider track |
| `knob` | `#FFFFFF` | a slider's thumb, and the glyph over a filled disc |
| `on` | `#006FFF` | a filled, "on" status disc |
| `fill` | `#FFFFFF` | the lit portion of a slider bar |
| `glass` | `#FFFFFF` | the tint of the frosted chrome |

### Dark

| Token | Value | What it is for |
|---|---|---|
| `bone` | `#23262E` | the chrome surface |
| `tile` | `#2F333C` | an inset tile |
| `tile_on` | `#2B4A78` | a tile carrying an active control |
| `hair` | `#1AFFFFFF` | the hairline: a rule at 10% white |
| `ink` | `#F4F0E8` | primary ink — ivory, never pure white |
| `mute` | `#8B9099` | secondary ink |
| `dim` | `#A9A59E` | detail text |
| `accent` | `#006FFF` | the family accent — the same in both appearances |
| `track` | `#3B4048` | a slider track |
| `knob` | `#EDE9E1` | a slider's thumb |
| `on` | `#0A84FF` | a filled, "on" status disc |
| `fill` | `#EDE9E1` | the lit portion of a slider bar |
| `glass` | `#23262E` | the deep bone: glass goes deep, not bright |

### Why `on` and `fill` are separate from `accent`

The family accent is distinct from the other color roles. A filled, "on"
shape has its own token so status controls can use the appearance-specific
full-strength value:

- **`on`** — `#006FFF` light, `#0A84FF` dark. This is the accent at full
  strength, used for active controls and installed-app emphasis in the
  Start Menu and Control Center.
- **`fill`** — `#FFFFFF` light, `#EDE9E1` dark. It is the appearance-specific
  lit portion of a slider. The unfilled portion of
  the same bar is `fill` at 22% — a fifth of it over the glass reads as
  "this much is not in use" and inherits the appearance without a track
  token of its own.

### The knob ink is not the ink

The glyph on a white knob is a fixed dark ink, `#1A1D24`, in *both*
appearances. It cannot be `ink`, because `ink` flips to near-white in the
dark appearance and would disappear into the white circle. The status
discs are the other way round — a light `knob` glyph over an `on` or `mute`
disc — so one token cannot serve both. `CC_KNOB_INK` in `finncontrol.c`
is that exception, and it is the only one.

---

## Type

**Fixel Display. One family. One weight.**

The shell loads `FixelDisplay-Regular.otf` and nothing else. Hierarchy
comes from **size** and from **ink against dim**, never from a heavier
cut of the face: four weights are shipped, and the shell uses one of them.

Bold is not used for hierarchy because the reference does not use it either.
The reference separates a label from its detail line by *value*, not by
weight — and value is a thing the shell can do better than weight, because
it survives being scaled.

### The scale

Every surface is authored against the same 1280 × 832 canvas. The compositor
maps a design unit to the scanout with `finn_ui_scale()`:

```
scale = min(screen_width / 1280, screen_height / 832)
```

That is 1.923 on the 2560 × 1600 development VM, 1.298 on 1080p, and exactly
1.0 on a 1280 × 832 display. A surface never invents its own factor; using
the width alone made the dock too wide on a 16:10 panel, and the height alone
made it overflow a short one. The name is the role, not the size.

| Role | Size | Where |
|---|---|---|
| `time` | 34 | the Start Menu's clock widget |
| `title` | 16 | a page title, the Start Menu header, a Settings heading |
| `clock` | 16 | the dock's time |
| `label` | 14 | a Start Menu app row |
| `label_sm` | 12 | a Settings row, a Control Center label |
| `detail` | 10 – 12 | a control's detail line, an app's note, a caption |
| `micro` | 8.5 – 10 | the smallest line: a status tile's detail, "Not installed" |

Two details worth knowing:

- **The label steps down, it never truncates.** The Control Center loads
  the same weight at four sizes and picks the largest that fits the tile,
  so a long name gets smaller rather than being cut. The tiles are
  repeated units, so a tile that shrank on its own would read as a
  different kind of tile — the longest name in the set sets the size for
  all of them.
- **A name that is too long for its cell is ellipsised.** An app's name is
  written by whoever shipped it and can be any length, so the caption
  assumes it is longer than the cell it lands in.

### The wordmark

"FinnOS", in Fixel Display, never letterspaced, never all-caps, never
with a space. It is set at the `title` size in `ink` and it is the only
thing on the Start Menu that is not a control.

---

## Radii

One rule: a surface's radius is a fraction of its smallest dimension,
and a tile's radius is a fraction of the tile.

| Element | Radius | Note |
|---|---|---|
| the dock's bubbles and tray pills | 8 design px | Figma `Desktop - Home`; `finndock.c` |
| the Control Center panel | 8 design px (10.4 at the 1080 reference) | Figma `Desktop - Control Center / Home` |
| the Control Center tiles | 4.14 design px (5.38 at the 1080 reference) | Figma tile corners; `finncontrol.c` |
| the clock page panel | 8 design px (10.4 at the 1080 reference) | Code-defined; no separate Figma frame |
| the clock page cards | 8 design px (10.4 at the 1080 reference) | Code-defined; no separate Figma frame |
| the Start Menu (Home) panel | 8 design px (10.4 at the 1080 reference) | Figma `Home`, 800 × 500 |
| the Start Menu's right panel | 4.14 design px (5.38 at the 1080 reference) | Figma `Right Panel` |
| the Start Menu's account row | 4.14 design px (5.38 at the 1080 reference) | Figma `Account Options` |
| a slider row | half its height | a row that is a pill |

The Control Center and Start Menu panels both use the measured 8 px radius.
The clock page uses the same outer radius in code, but its cards have a
separate code-defined radius and are not shown as a dedicated Figma frame.

---

## Spacing

The rhythm is 4px, with the exceptions the reference forced.

### The Control Center

Figma Page 1's `Desktop - Control Center` is a 1280 × 832 frame. Its `Home`
panel is 405 × 500 at (721, 260), radius 8. Child geometry in the panel is:

| Element | Size and position |
|---|---|
| Wi-Fi tile | 188 × 40 at (10, 10) |
| Bluetooth tile | 188 × 40 at (207, 10) |
| Brightness row | 345 × 35 at (10, 60) |
| Appearance control | 35 × 35 at (360, 60) |
| Volume row | 385 × 35 at (10, 105) |
| Status/footer row | 385 × 40 at (10, 450) |

The brightness row has no lit portion in the reference: the white disc at its
start carries the sun, and the track behind it is black at 25%. The volume row
is the same track with the white lit portion marking the level. The
appearance control is a 35-unit circle, not a rounded square.

The substantial gap below the sliders is intentional space in the Figma
frame. The panel and bottom footer keep their 500/450-unit geometry. The
source scales these measurements with `finn_ui_scale()`, not with a fixed
1080p factor.

### The Start Menu (Home)

The Figma `Desktop - Home` canvas is 1280 × 832. Its Home panel is
800 × 500 at (240, 260), with an 8 px radius. Inside it, the right panel is
385 × 480 at (405, 10), radius 4.14; the account-options row is 385 × 40 at
(10, 450), also radius 4.14. The left column is 375 wide at x=20. Its header
is at (20, 25); the pinned list starts at y=56, and the recent list at y=225
under a label at y=194. Rows are 36 high on a 41 pitch: a 26 px icon at
(5, 5), a 14 px label at x=41, and a 10 px detail line on the same baseline.
Those are the authored constants in `finnmenu.c`.

The menu has three views, all in that frame's grammar:

- **Home** — pinned apps, recent activity, and two live widgets in the right
  panel where the frame's grey cards were placeholders.
- **All Apps** — the full catalogue at the same row rhythm, with a back
  chevron at the header's left.
- **Search** — a 375 × 36 field at (20, 56), results below it, and the chosen
  result's preview in the right panel. Typing never leaves the menu.

### The dock

Figma `Desktop - Home` measures the four islands relative to its 1280 × 832
frame:

| Island | Position | Size | Notes |
|---|---:|---:|---|
| Taskbar | (451, 775) | 115 × 42 | Three app items; 10 px internal spacing |
| Navigation | (581, 775) | 116 × 42 | 5 px internal spacing |
| Time & Weather | (712, 775) | 139 × 42 | Clock and weather |
| Status / tray | (866, 775) | 116 × 42 | 15 px internal spacing |

All four have radius 8; outer gaps are 15 px and bottom margin is 15 px.
The Navigation island is the compositor's centered anchor. The Taskbar is
content-sized. Pixel measurements from the exported frame resolve its item
layout: three 26 px tiles on a 36 px center step (10 px gaps), with 8.5 px
outer insets, produce the measured 115 px width. `finndock.c` uses those
values and centers the 26 px tiles vertically in the 42 px island. Code also
defines 24 px navigation icons, a 42 px active launcher well and 16–18 px
status glyphs.

### Boot and windows

The Figma `Boot` board is a 1280 × 832 black frame. Its 50 × 50 mark sits
at (615, 391); the 150 × 5 progress track starts at (565, 541), with a 15 px
white lit segment over a 40%-opacity `#6C6C6C` track. These match the metrics
in `finnboot.c`.

The `Empty Window` board is also 1280 × 832. Its centered default window is
500 × 300 with radius 8; the options pill is 72 × 24, radius 4.14, inset 10,
with three 15.43 px icons. The centered “Empty window” label is 12 px at 70%
ink opacity, matching `finnwindow.c`.

The stacked-window board contains two 500 × 300 windows, radius 8, at
(390, 266) and (539, 189). The second is offset +149/−77 design units from
the centered first window; `finnwindow.c` now uses this measured cascade.

### Icons

SVG vector graphics, **placed by their ink and not by their viewBox**.

The assets fill their viewBoxes by anything from a third to all of it,
so a dock laid out by viewBox puts icons of visibly different weight in
one row. `finn_icon_frame()` takes the ink size — how many pixels the
drawing's longer dimension should occupy — and centres the drawing *as
drawn*, which is not the same as centring its box.

Stroke 1.5, round caps, no fills except where a shape is a shape (the
volume speaker, the battery's charge bar, or full-color chips).

- **System Glyph Tinting**: Monochromatic system glyphs (`search`, `home`,
  `windows`, `wifi`, `bluetooth`, `battery`, `volume`, `power`, `refresh`)
  carry two tones: active ink and inactive muted ink (`#969696`, `#8D8D8D`,
  `#555555`). They adapt to light and dark appearances dynamically.
- **Full-Color App Chips**: Multi-color brand chips (`app_files`, `app_arc`,
  `app_settings`, `avatar`, `sun_weather`) are drawn with `active = 0,
  inactive = 0` to preserve native SVG fills, paths, and multi-stop
  gradients without monochrome tint overrides.
- **Vector Path Constraints**: `finnvg` parses clean cubic Béziers (`M`, `L`,
  `C`, `Z`) with inline hex fills. Elliptical arc commands (`A`/`a`),
  `<pattern>`, and `url(#...)` gradient paint servers are normalized to
  native Bézier curves or direct hex definitions before rasterisation.

---

## The anatomy of each surface

### The dock

Four floating frosted islands at the bottom of the screen, with the middle
navigation island strictly centered horizontally:

```
┌──────────────────┐  ┌─────────────┐  ┌────────────────┐  ┌──────────────┐
│ [⚙] [📁] [🌈] [Win] │  │ [🔍][🏠][⊞]  │  │ 21:47 Thu 23 ☀ │  │  🛜 ᛒ ▮ 100% │
└──────────────────┘  └─────────────┘  └────────────────┘  └──────────────┘
    1. Taskbar          2. Centered       3. Clock &          4. Status /
                          Navigation         Weather             Quick Controls
```

- **Pill 1 is the Taskbar**: quick-launch app chips (`settings`, `files`,
  `arc`) with active running indicator dots underneath, followed by any
  running application pills showing their icon, truncated title, and close
  button.
- **Pill 2 is the Navigation island**: anchored directly to
  `(screen_w - p2_w) / 2` so it remains precisely centered regardless of the
  number of running windows. It holds Search, Home (which opens the Start
  Menu, lighting up with a distinct white/bone well when open), and Window
  Manager overview.
- **Pill 3 is the Clock & Weather**: displays current time (`HH:MM`),
  formatted date, and live weather conditions (sun icon + temperature).
  Clicking opens the Notification Center / Clock panel.
- **Pill 4 is the Status island**: displays network connectivity (Wi-Fi/Ethernet),
  Bluetooth status, and battery percentage (`▮ 100%`). Clicking toggles the
  Control Center.
- **The dock adapts when windows maximize**: When a window is maximized or
  reaches the dock's resting band, the dock sinks flush to the bottom edge
  and its islands condense smoothly to maximize window canvas.

### The Control Center

Figma's Control Center Home panel is a 405 × 500 frame with Wi-Fi and
Bluetooth tiles at the top, a brightness row with an appearance control, a
volume row, a large intentional widget area, and a status/footer row at the
bottom. Its exact measured geometry is listed in Spacing above. The old
compact 200-unit panel placed the footer too high and did not match Figma.

```
  ┌───────────────────────────────┐
  │ [Wi-Fi]         [Bluetooth]   │
  │ Sun ━━━━━━━━━━━━━━━ Sun       │  brightness / appearance
  │ Vol ━━━━━━━━━━━━━━━━━━━       │  volume
  │                               │
  │        widget space           │
  │ Battery / charge      Edit ⚙ │
  └───────────────────────────────┘
```

- **A status tile is the reference's unit**: a lighter frosted tile, a
  filled circle carrying the glyph, then a name with a grey detail under
  it. The circle is the state — `on` when the control is on, `mute` when
  it is off or the hardware is absent. The name and detail remain readable,
  so "No adapter" is information rather than a broken-looking switch.
- **Wi-Fi and Bluetooth are distinct tiles.** The network tile can report
  the active interface; it is not a second Bluetooth or wired toggle.
- **A slider row is one pill, not a disc with a track beside it.** Its
  left end carries the control glyph and the value emerges from beneath it.
  Nothing is drawn as an active level when the machine has no level to set.
- **The sliders do not squeeze when pressed.** A toggle can give under the
  finger; a slider's surface stays still while its value travels across it.

### The clock page

The notification/clock page is currently code-defined as a 338 × 338 panel.
Figma Page 1 has no dedicated clock-page frame, so its layout below describes
implementation intent, not verified design parity.

```
  ┌───────────────────────────────┐
  │ 21:47                         │   the time, and the date under it
  │ Tuesday, 20 October 2026  W43 │
  │ Notifications            Clear │
  │ ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬ │
  └───────────────────────────────┘
```

There is **no calendar** on it, on purpose: a month grid is a thing to
consult, and this page is a thing to glance at. The date lives in the
header, where it belongs.

When nothing has happened, the page says so — "No new notifications" and
what that means — rather than showing an empty list it cannot fill. The
notification queue is empty at every start, by design.

### The Start Menu (Home)

The Figma Home panel is 800 × 500 at (240, 260) in the 1280 × 832 desktop
frame. Its left side is an app list headed `Apps | Pinned`, with a `View all`
action; the right side is a widget area whose two cards carry the clock/date
and machine status; the bottom-left account row carries the avatar/name and
restart/power controls.
This is not a four-column launcher grid. The measured panel, right-panel,
account-row and row coordinates are recorded above and match the authored
values in `finnmenu.c`, which now works in design units rather than a private
scale. The two grey cards in the frame carry live widgets because a blank
placeholder is not a capability; the rest of the panel is the frame.

---

## Honesty, which is a design rule

Three of the rules above are really one rule: **a control that cannot act
must not look like it can.**

- The Start Menu's app list is populated from two sources:
  `/usr/share/finn/apps.registry` says which apps exist, and
  `/var/lib/finnpkg/installed` says which of them are on this machine.
  The shell draws the difference without changing the Figma list layout.
- The Control Center's tiles are bound to the machine's state. A machine
  with no wireless adapter shows "No adapter" over a grey circle — not a
  switch that lies. Such a tile takes no press, because there is nothing
  behind it to promise.
- The dock's icon islands follow the Figma Home layout; do not add a blank
  placeholder where the reference shows an app or system icon.
- The tray shows only the hardware that exists.
- The sliders read "n/a" when there is no level to set, and they cannot be
  dragged when there is none.

---

## Where this document and the code meet

The C palette and `.fui` theme remain separate representations, and
`tests/test_theme.py` checks their values and material-pass arithmetic. That
proves consistency with the checked-in tokens, not Figma color parity: the
Variables view is empty. Home, Control Center, Boot, and single-window
geometry have been measured. The Settings sidebar now follows Figma's five
visible entries: Wi-Fi, Bluetooth, Network, Personalization, and System
Information. Network reports live interface link state only; the frame does not
show separate Network or Personalization page compositions, so those bodies
adapt existing system status and appearance components rather than claiming
pixel-for-pixel unseen screens. Display, Sound, and Power remain searchable and
control-addressable. System Information uses the Figma hero/four-card composition
with live RAM, root-storage and CPU values; GPU details remain explicitly
unavailable. The measured stacked-window positions now match Figma; z-order and
other runtime interactions remain unverified. Neither the clock page, Finn
Files nor Finn Store has a dedicated Figma frame; all three now use the
canonical `finn_ui_scale()`, and Finn Files and Finn Store share Settings'
window-sheet sidebar and active-row treatment.
