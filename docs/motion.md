# Motion

Motion is how the system says "this changed". It is not decoration, and it
never delays input. The same three durations and two curves are used
everywhere.

## The language

| Token | Value | Used for |
|---|---|---|
| `kMotionFast` | 140 ms | hover, press, toggle, slider, menu close |
| `kMotionStandard` | 220 ms | menu open, window entrance |
| `kMotionSlow` | 320 ms | dock entrance |

Entrances use `ease_out_cubic`: most of the movement happens early and the
last pixels settle. Popovers add a small `ease_out_back` overshoot so the
menu and control center land instead of stopping dead. Grids use
`stagger(progress, index, count)`: same final layout, items arrive 60 ms
apart. State changes use `approach`, an interruptible
exponential that reverses from wherever it is, so a pointer crossing a
button twice does not jump.

`FINN_REDUCED_MOTION=1` makes every animation instant. The preference is read
once per process; it is a preference, not a per-frame switch.

## What moves

| Surface | Motion |
|---|---|
| buttons, icon buttons, app tiles | background cross-fades between tile and tile-on; press shrinks by under a pixel |
| toggle | the knob slides and the track crosses from track to accent |
| slider | the fill and knob follow, and settle when set from code |
| text field | the border crosses from hair to accent |
| list rows and app rows | the hover tint fades in |
| dock and status pill | enter 16 px from below over 320 ms |
| app menu | slides up 10 px over 220 ms with a 60 ms per-item stagger; closes in 140 ms |
| control center | slides up 12 px over 220 ms; Wi-Fi, Bluetooth, brightness, volume and media stay disabled indicators (no finnd in v1) |
| app window | enters by sliding up 14 px over 220 ms; focus is immediate |
| boot splash & OOBE (`FinnRibbonAnimation`) | On boot and OOBE step 0 (`Mode::HiIntro`), 6 horizontal curved arcs tilt into a 3D stacked cylinder (0–0.40 s), orbit continuously as 6 eccentric torus circles with a rotating coral/amber/violet/blue gradient (0.40–1.35 s), coil into a handwriting nib that traces the cursive `"hi."` mark stroke-by-stroke (1.35–2.38 s), and only afterward reveal the top `FinnOS` branding and bottom circular `Next` button (`2.38–2.86 s`); on OOBE completion (`Mode::SlinkyWave`), the 6 rings unfurl into a 6-loop horizontal prolate epitrochoid slinky wave with a left-to-right gradient pulse; respects `FINN_REDUCED_MOTION=1` |
| theme | the compositor re-reads the setting once a second; apps every few frames |

## What does not move

- A control that cannot act does not play a "success" animation.
- An error is shown at once; it is not faded in as if it were a notification.
- Nothing animates in a loop. No surface moves without a reason.
- An animation never owns the input path: a click during a window's entrance
  reaches the window.

## Where it lives

- [`src/ui/finn/animation.hpp`](../src/ui/finn/animation.hpp) — the tokens,
  the curves and `Animation`.
- [`src/ui/canvas.cpp`](../src/ui/canvas.cpp) — widget state transitions and
  `Canvas::animating()`, which asks the app for the next frame.
- [`src/shell/main.cpp`](../src/shell/main.cpp) — dock and menu timelines.
- [`src/comp/server.cpp`](../src/comp/server.cpp) — the window entrance and
  the theme timer.

## Figma

The FinnOS file has no baked Figma Motion timelines; its presets are
`Position`, `Scale` and `Opacity` with `OUT_CUBIC`. The implementation keeps
those curves but shortens the default 0.5 s to the durations above, because a
desktop that waits half a second for a menu feels slower than the design
video does. The dock geometry follows the file: 42 px glass pills, 8 px
radius, 26 px task icons, a 139 px status pill.
