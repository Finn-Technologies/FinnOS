# The FinnOS design system

This is the design language of the shell: the palette, the type, the
radii, the spacing and the anatomy of each surface. It is written from the
code that draws them — `src/compositor/` — and every number here can be
found there.

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

They are not windows. There is no window manager, so there is nothing to
be a window, and a surface that pretended otherwise would be a lie with a
rectangle in it. They are *surfaces*: rasterised into transparent layers
and composited into the same frame as the wallpaper, so a surface costs a
blit rather than a round trip over a socket.

They share one material (frosted glass), one set of corner radii, one type
scale and one set of timings. A surface that invented its own would read
as a different operating system.

### The material

The material is **glass**: the blurred wallpaper brought toward bone.

There are exactly two weights of it, and the arithmetic matters:

| Pass | Weight | What it is |
|---|---|---|
| the panel | **0.70** | the Control Center, the clock page and the Start Menu |
| a tile over a panel | **0.50** | the inset tiles inside a panel |
| the tray's well | **0.53** | the dock's inset, a second pass over the pill |

The tile is not a second colour. `finn_frost_rrect()` mixes toward the
tint over whatever the layer already carries, so a tile laid over a panel
*compounds*: 0.70 + 0.30 × 0.50 ≈ **0.85**. That is the whole reason it
is a second pass rather than one fill of 0.85 — it is the same glass, laid
down twice, and it deepens the way glass does.

The dock's glass is 0.70 as well, and the tray's well is a second pass at
0.53 over it: 0.70 + 0.30 × 0.53 ≈ **0.86**.

**There are no shadows.** The reference's edges are a hard one-pixel step,
and a soft drop shadow under a frosted panel would fight the glass: the
panel would look like it was floating *above* the wallpaper rather than
being *made of* it. Every edge in the shell is the anti-aliased edge of a
rounded rectangle.

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

The palette lives in `src/compositor/finndraw.c` and nowhere else. It is a
set of globals rather than constants because the shell has an appearance:
switching light/dark swaps every surface at once, and every surface picks
the change up without being told.

### Light

| Token | Value | What it is for |
|---|---|---|
| `bone` | `#F6F1E7` | the chrome surface — *defined, not yet drawn with* |
| `tile` | `#E7E1D5` | an inset tile — *defined, not yet drawn with* |
| `tile_on` | `#D3E6FF` | a tile carrying an active control — *defined, not yet drawn with* |
| `hair` | `#22FFFFFF` | the hairline: a rule at 13% white |
| `ink` | `#1A1D24` | primary ink: names, values, the wordmark |
| `mute` | `#969696` | secondary ink: inactive glyphs, detail lines |
| `dim` | `#7B7776` | detail text — stronger than mute, weaker than ink |
| `accent` | `#7AB8FF` | the family accent — *defined, not yet drawn with* |
| `track` | `#CFC7B8` | a slider track — *defined, not yet drawn with* |
| `knob` | `#FFFFFF` | a slider's thumb, and the glyph over a filled disc |
| `on` | `#006FFF` | a filled, "on" status disc |
| `fill` | `#998481` | the lit portion of a slider bar |
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
| `accent` | `#7AB8FF` | the family accent — the same in both appearances |
| `track` | `#3B4048` | a slider track |
| `knob` | `#EDE9E1` | a slider's thumb |
| `on` | `#0A84FF` | a filled, "on" status disc |
| `fill` | `#EDE9E1` | the lit portion of a slider bar |
| `glass` | `#23262E` | the deep bone: glass goes deep, not bright |

### Why `on` and `fill` are separate from `accent`

The family accent `#7AB8FF` is a *pastel*, and pastels do not survive
being made into a solid shape. A 31-pixel disc of `#7AB8FF` on bone reads
as a smear; a filled slider bar of the same colour reads as a mistake.
So a filled, "on" shape has its own token:

- **`on`** — `#006FFF` light, `#0A84FF` dark. This is the accent at full
  strength, and it is what the Start Menu's installed app tiles and the
  Control Center's active status discs are painted with.
- **`fill`** — `#998481` light, `#EDE9E1` dark. A warm grey measured off the
  reference, used for the lit portion of a slider. The unfilled portion of
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

All sizes are in pixels on the 1080p canvas; the compositor scales them by
the screen's density. The name is the role, not the size.

| Role | Size | Where |
|---|---|---|
| `time` | 34 | the clock page's time |
| `title` | 17 | the Start Menu's "Applications" and the "FinnOS" wordmark |
| `clock` | 18 | the tray's clock |
| `label` | 12 – 12.5 | a control's name, an app's name, "Notifications" |
| `label_sm` | 11 | a label that could not fit at 12 — the Control Center only |
| `detail` | 10 | a control's detail line, an app's note |
| `micro` | 8.5 – 9 | the smallest line: a status tile's detail, a tile's "Not installed" |

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
| the dock's bubbles and the tray's pill | 19.5 | the reference's r15, scaled |
| the Control Center's panel | 20.8 | the reference's r16 |
| the Control Center's tiles | 9.7 | the reference's r7.5 |
| the clock page's panel | 20.8 | the same as the Control Center's, by design |
| the clock page's tiles | 9.7 | the same as the Control Center's, by design |
| the Start Menu's panel | 34 | the menu is a room, and a room has soft corners |
| the Start Menu's app tiles | 22 | |
| a slider row | half its height | a row that is a pill |

The Control Center and the clock page have *identical* radii, because
they are the same panel with different contents: opening one after the
other must not make the surface under your eyes change shape.

---

## Spacing

The rhythm is 4px, with the exceptions the reference forced.

### The Control Center and the clock page

Both are authored in the Control Center's units, which are the
reference's divided by 0.770 — the density its 1280×832 frame implies
against a 1080p canvas.

| | |
|---|---|
| panel | 351 × 351 (the reference's 260 square, plus 4% for the longest label) |
| padding | 10.4 |
| gap between rows | 10.4 |
| a status tile | 150.6 × 46.7, two to a row, 15.6 apart |
| a slider row | 330.2 × 38.9 |
| the slider's thumb | 48 wide — the row's own left cap |
| the status disc | 31.2 across, 7.8 in from the tile's edge |
| the disc's glyph | 18 of ink |
| the slider's glyph | 21 of ink |
| the gap to the tile's text | 6.5 |
| the panel to the dock | 19.5 |

The panel is **square**, 351 × 351, and its content is stacked at the top.
The empty strip below the controls is deliberate: the reference has it,
because that strip is where widgets would go, and this shell has none. A
panel that hugged its contents would be a different shape from the
reference's.

### The Start Menu

The menu is the one surface with a width of its own: a launcher has to
hold a grid, and a grid does not fit in the width of a control panel.
Everything inside it is the Control Center's language.

| | |
|---|---|
| panel | 760 wide; the height follows the number of app rows |
| padding | 28 |
| the gap to the footer | 24 |
| the header | 30, then 24 before the grid |
| a grid cell | 176 wide (the panel's inner width ÷ 4) |
| an app's tile | 76 square, centred in its cell |
| an app's glyph | 40 of ink inside its 76 |
| the gap to the caption | 12 |
| the caption's line | 22 |
| the detail line | 18 |
| the gap between rows | 18 |
| the footer | 46 |

### The dock

| | |
|---|---|
| a bubble | 65 square |
| the gap between the dock's parts | 19.5 |
| the gap from the bottom of the screen | 34 |
| the pill's padding | 21 |
| the tray's glyphs | 22 of ink, 23.4 apart |
| the battery's glyph | 19 of ink — a filled gauge reads heavier than an outline |
| the clock | 18 of ink, 27 after the tray's glyphs |
| the inset's margin inside the pill | 9 |

### Icons

24-unit SVG, **placed by their ink and not by their viewBox**.

The assets fill their 24-unit boxes by anything from a third to all of it,
so a dock laid out by viewBox puts icons of visibly different weight in
one row. `finn_icon_frame()` takes the ink size — how many pixels the
drawing's longer dimension should occupy — and centres the drawing *as
drawn*, which is not the same as centring its box.

Stroke 1.5, round caps, no fills except where a shape is a shape (the
volume speaker, the battery's charge bar).

The icons carry two colours in the file: `#969696` for "inactive" and
black for "active". A single palette maps every icon onto the current
state's colours, which is why an icon that means two things (the wifi
arcs, the battery bar) is mutated before it is rasterised and carries its
own revision in the cache key.

---

## The anatomy of each surface

### The dock

Two bubbles and a pill, centred at the bottom of the screen.

```
        ┌────┐  ┌────┐  ┌──────────────────────────────┐
        │ ▦  │  │ ▦  │  │  ◀))  ▮    21:47               │
        └────┘  └────┘  └──────────────────────────────┘
         empty   the     the tray: the status block and the clock
         slot    launcher
```

- **The first bubble is the reserved slot** for pinned and running apps.
  This shell can pin and run nothing yet, so the slot is drawn *empty*:
  its glyph is in `mute` rather than `ink`, and it takes no press wash.
  A slot that lights up under the finger and then does nothing is a
  promise the shell cannot keep.
- **The second bubble is the launcher**, and it opens the Start Menu.
- **The pill** holds two controls and nothing else: the status block
  (which opens the Control Center) and the clock (which opens the clock
  page). The pads either side of them and the gap between them are
  *surface*, not controls, and take no wash — a hover that appeared under
  them would be the dock claiming a target it does not have.
- **The tray carries only what the machine has.** A desktop with no
  battery gets no battery glyph; a machine with no wireless hardware gets
  no wifi glyph. This is the adaptive-UI rule at the smallest scale.
- **The clock is a control, not a label.** It takes a hover wash, and its
  target reaches halfway into the gap before it, because a control should
  be easier to hit than the ink it is drawn from.

### The Control Center

A square panel above the tray, its left edge flush with the pill's.

```
  ┌───────────────────────────────┐
  │ ⬤ Wi-Fi          ⬤ Bluetooth   │   two status tiles
  │   Connected       No adapter   │
  │ ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬ │   a slider row: a pill
  │ ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬ │
  │ ⬤ Appearance      ⬤ Power       │
  └───────────────────────────────┘
```

- **A status tile is the reference's unit**: a lighter frosted tile, a
  filled circle carrying the glyph, then a name with a grey detail under
  it. The circle is the state — `on` when the control is on, `mute` when
  it is off or the hardware is absent. The name and the detail keep their
  colours, so "No adapter" reads as information rather than as damage.
- **One connection slot, not two.** When the machine has no radio the slot
  shows the wire it actually has, instead of standing empty next to a
  second "Wired" tile. Both interfaces would otherwise compete for the
  same sentence, and the honest one is the one carrying traffic. A wire
  is not a switch: that tile is inert while it shows one.
- **A slider row is one pill, not a disc with a track beside it.** Its
  left 48 pixels are white — the row's own left cap — and the value
  emerges from under it with a square edge instead of a curve. Nothing is
  drawn at all when there is no level to draw, so an absent control is a
  bare track reading "n/a", never a fake 100%.
- **The sliders do not squeeze when pressed.** A toggle is a button and can
  give under the finger; the point of a slider is that the surface stays
  still while the value travels across it.

### The clock page

The same panel, the same width, the same radii — opened from the clock
instead of the tray.

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

### The Start Menu

A card in the middle of the screen, because it is the front door and a
front door is in the middle of the wall you walk through.

```
  ┌──────────────────────────────────────────────┐
  │ Applications                        1 of 8 ready │
  │   ▣      ▣      ▣      ▣                    │
  │  Code   Files  Notes  Studio                 │
  │          Not installed                        │
  │  ▣      ▣      ▣      ▣                     │
  │ ─────────────────────────────────────────── │
  │ FinnOS                          Restart  Shut down │
  └──────────────────────────────────────────────┘
```

- **A grid of four columns.** The cell's tile is centred in it, so a row
  of tiles reads as a row rather than as a staircase.
- **An installed app is a coloured tile**: `on` behind a `knob` glyph, the
  name in `ink`. **An app that is not installed is a quiet wash** — 10%
  ink — with a `mute` glyph, the name in `dim`, and the detail line
  reading **"Not installed"**. A muted tile says "not here" more honestly
  than a coloured one that does nothing when it is pressed, and it takes
  no hover and no press.
- **The header says what is true**: "1 of 8 ready" — how many of the
  catalogue's apps this machine can actually open.
- **The footer is the wordmark and the power row.** "Restart" and "Shut
  down" are the two things the menu can do that are not an app, and they
  are drawn like every other control rather than as a line of quiet text
  that happens to be clickable.

---

## Honesty, which is a design rule

Three of the rules above are really one rule: **a control that cannot act
must not look like it can.**

- The Start Menu's app tiles come from two files with two owners:
  `/usr/share/finn/apps.registry` says which apps exist, and
  `/var/lib/finnpkg/installed` says which of them are on this machine.
  The shell draws the difference.
- The Control Center's tiles are bound to the machine's state. A machine
  with no wireless adapter shows "No adapter" over a grey circle — not a
  switch that lies. Such a tile takes no press, because there is nothing
  behind it to promise.
- The dock's first bubble is an empty slot and is drawn as one.
- The tray shows only the hardware that exists.
- The sliders read "n/a" when there is no level to set, and they cannot be
  dragged when there is none.

---

## Where this document and the code meet

The palette in `finndraw.c` and the theme in `ui/_tokens.fui` are two
copies of the same thing, and they are kept in step by hand. That is a
debt the compiler will eventually pay. The compositor's copy is the
truth; the `.fui` copy is the design system's statement of intent.
