# The FinnOS motion specification

Every duration, every easing and every wash in the shell is written down
here, and every one of them is in `src/compositor/finnmotion.h`. That
header is the single copy: a surface that needs a number of its own says
so in that file, rather than carrying a private constant next to its
neighbour's.

The reason is simple. The dock, the Control Center, the clock page and the
Start Menu are four surfaces of one shell. They are the same glass, the
same corner radii and the same type scale, so they have to move as one
family too. A panel that opens in 0.26 seconds next to one that opens in
0.28 reads as two shells that happen to share a screen.

---

## The rules

Five rules decide every number in this document.

**1. Opening is slower than closing.**
A surface takes `FINN_MOTION_OPEN` (0.28 s) to arrive and
`FINN_MOTION_CLOSE` (0.18 s) to leave. The close is about two thirds of
the open, and that ratio is deliberate in both directions: a surface that
takes as long to leave as to arrive feels stuck, and one that leaves
instantly feels snatched away. You asked for something; you get it; you
did not ask for it and it is gone.

**2. Nothing moves under the pointer.**
A control answers the pointer with a *wash of ink* — a translucent
patch of the surface's own ink over the control — and never with a change
of size or position. A control that flinches away from the finger is a
control that has moved out from under it. The wash is 0.055 of ink for a
hover and 0.090 for a press: the press is darker, because a hover is a
hint and a press is a confirmation.

**3. The pointer's answer is quick, and the pointer's withdrawal is slow.**
The wash arrives in 0.11 s and leaves over 0.20 s. A control that is
hovered and then abandoned fades rather than snaps off, and the fade is
slower than the arrival so the eye can follow it out.

**4. Nothing runs on a timer.**
A duration here is the length of a *track*, not a period. The compositor
asks for a frame while a track is moving and goes back to blocking on
input — at zero frames per second — the moment the last one settles. The
wallpaper is decoded once; the dock is rasterised only when its state
moves; an untouched desktop redraws nothing at all.

**5. A surface that changes has to redraw.**
Every surface is a cached layer with a **build key**: everything that can
put a different pixel in the layer is folded into one number, and the
layer is only re-rasterised when that number moves. An animated property
that is not in the key is an animated property that does not animate.

---

## The easings

Four curves, in `finnanim.c`. They are the shapes the shell actually
uses: everything you touch is either settling, being dismissed, or
popping.

| Curve | Shape | Where |
|---|---|---|
| `FINN_EASE_OUT` | quick off the mark, settles softly | every wash, every value that follows the hardware |
| `FINN_EASE_IN` | winds up, arrives at speed | *not used by the shell today* |
| `FINN_EASE_IN_OUT` | symmetric, slow at both ends | a surface closing |
| `FINN_EASE_OUT_BACK` | eases past the target and comes back, peaking around 1.10 | a surface opening |

There is deliberately **no bounce-and-wobble**. A panel that overshoots
twice reads as a demo rather than as a system.

`FINN_EASE_OUT_BACK` overshoots by about 10% and returns. That is the
only "spring" in the shell, and it is used exactly once per surface: on
the way *in*. On the way out the shell uses `FINN_EASE_IN_OUT`, which
starts slowly and leaves — the opposite character, which is what a
dismissal should feel like.

---

## The inventory

### Surfaces — the Control Center, the clock page, the Start Menu

All three are the same object with different contents, and they move
identically.

| | |
|---|---|
| **opens** | over **0.28 s**, `FINN_EASE_OUT_BACK` |
| **closes** | over **0.18 s**, `FINN_EASE_IN_OUT` |
| **scale** | from **0.92** of its settled size to 1.0 |
| **opacity** | ramps to full at 87% of the open (the alpha is `value × 1.15`, so the back-ease's overshoot is absorbed rather than clipped) |

**What it animates from, and to.** The surface arrives *from the corner it
is anchored to*, growing out of it:

- the **Control Center** grows out of the tray pill's bottom-left corner,
  because the pill is the thing that opens it;
- the **clock page** grows out of the clock's own corner, because the
  clock is the thing that opens it;
- the **Start Menu** grows out of **its own middle**, because it is the
  one surface that is not anchored to anything. It is the system's front
  door; a front door is in the middle of the wall you walk through.

The scale is about the anchor, not about the surface's own corner: the
anchor stays where it is and the edges follow it out.

**Why 0.92.** A surface that grows from nothing reads as a page turning.
A surface that grows by 8% reads as *that* surface arriving, and 8% is
enough to be seen on a 351-pixel panel and on a 760-pixel card alike. The
two are the same fraction on purpose: the Start Menu is twice the width of
the Control Center, and if each grew by its own number of pixels they
would not read as the same motion.

**Why the back-ease on the way in and not the way out.** The overshoot
is the surface arriving with a little energy — it is flung open rather
than dissolved into place. On the way out that energy would be wrong:
the surface is being dismissed, and a dismissal that springs is a
dismissal that has to be waited out.

### The scrim behind the Start Menu

| | |
|---|---|
| **strength** | 0.30 black over the whole frame |
| **arrives** | over **0.20 s**, `FINN_EASE_OUT` |
| **leaves** | over **0.14 s**, `FINN_EASE_IN_OUT` |

**What it does.** The desktop does not go away when the menu opens, it
stops being the thing you are looking at. The scrim is what makes the
menu the only lit thing in the room.

**Why it leads.** The scrim is shorter than the menu's open in both
directions — 0.20 against 0.28, and 0.14 against 0.18 — so the room is
already dim when the menu appears, and already brightening when the menu
starts to go. The scrim goes down *before* the menu goes up.

This is the one motion in the shell that is not a response to a control,
and it is the only one that touches the whole frame: on the software path
it is a multiply over the composited frame, and on the accelerated path it
is one black quad stretched over the screen.

### The tray well

The dock's inset under the status block is the mark that ties the pill to
the panel above it, so it rises with the panel's own openness rather
than being painted into every frame of the dock's life. It is a second
frost pass at 0.53 over the pill's 0.70, so the glass compounds to 0.86.

While the panel is shut, the same patch carries the hover and press
washes instead, and those fade out as the well takes over: a hover says
"this opens something" and a press says "you got it", and neither of them
*is* the open panel, so neither is as strong as the panel's own anchor.

The clock's end of the pill has the same well, for the clock page.

### The pointer

| | |
|---|---|
| **hover wash** | 0.055 of the surface's ink |
| **press wash** | 0.090 of the surface's ink |
| **hover in** | 0.11 s, `FINN_EASE_OUT` |
| **hover out** | 0.20 s, `FINN_EASE_OUT` |
| **press in** | 0.08 s, `FINN_EASE_OUT` |
| **press out** | 0.18 s, `FINN_EASE_OUT` |

These are the shell's numbers, and every surface uses them: the dock's
tray and clock, the launcher bubble, the Control Center's toggles, the
clock page's Clear, the Start Menu's app tiles and its power row.

**A control whose hardware is absent takes no wash at all** — not a
hover, not a press. There is nothing behind it to promise, and a control
that lights up under the finger and then does nothing is a lie told in
two steps.

### The slider's value

| | |
|---|---|
| **follows the hardware** | over **0.20 s**, `FINN_EASE_OUT` |

A slider's bar *travels* to the hardware's reading rather than jumping to
it, so a change made elsewhere on the machine is seen rather than
discovered. The first time the panel is built, the bar is primed at the
hardware's value with no motion at all — a control that animates on the
frame it appears is a control that is still settling while you are
reading it.

### The Start Menu's grid

| | |
|---|---|
| **the first tile arrives** | 0.060 s after the menu starts opening |
| **between two arrivals** | 0.022 s |
| **one arrival** | 0.200 s, `FINN_EASE_OUT` |
| **the rise** | 10 px below its place, easing into it |

**What it animates from, and to.** Each app tile fades up into place and
rises the last ten pixels into it. The panel is the room; the apps are
the people coming in, and they arrive after the room is ready.

**Why the step is 22 milliseconds.** Eight tiles at a quarter of a second
each would read as a queue. At 22 milliseconds the last tile starts
moving 154 milliseconds after the first, and the grid reads as *filling*,
not as *performing*.

**Why it is affordable.** The Start Menu is the densest surface in the
shell, and re-rasterising it sixty times a second would cost more than the
motion is worth. So the layer is built in two pieces:

- the **glass** — the panel, the header and the footer — is rasterised
  once per geometry and copied;
- the **cells** — each app's tile, glyph, name and note — are rasterised
  one at a time and kept.

What is left per frame is a copy of the glass and eight blits.

### The clock page's minute

| | |
|---|---|
| **the dip** | over **0.26 s**, the ink of the time falls to 45% and comes back |

The minute is the only change in the shell that happens on its own, so it
gets the only motion that is not an answer to something the person did.
The ink of the time dips a little and returns.

**Why the digits do not move.** A clock that slides its numbers every
minute is a clock you cannot stop looking at. The dip is the whole
acknowledgement: it says "the minute turned" without asking for anything.

### A notification arriving

| | |
|---|---|
| **the tile settles** | over **0.22 s**, `FINN_EASE_OUT` |

The newest tile on the clock page is an inset that *deepens* into place.
Its glass starts at the panel's own weight of 0.70 — which is exactly the
panel's colour, so it is invisible — and comes down to the tile's weight
of 0.50 as its words arrive.

**Why it deepens rather than appears.** A box that simply appears reads
as a glitch. An inset that deepens reads as something settling into the
page. And because the panel's glass and the tile's glass are the same
material, there is a weight at which the tile *is* the panel, and the
arrival starts there.

### The appearance switch

| | |
|---|---|
| **the dissolve** | over **0.36 s**, `FINN_EASE_IN_OUT` |

**What it dissolves.** The chrome, and only the chrome. The wallpaper
underneath is not part of the dissolve: the picture is what the room looks
like, and the shell re-grades *on top of* it. An appearance change that
crossfaded the picture would read as the screen dimming rather than as
the furniture changing.

**Why 0.36 and not 0.45.** The old length was long enough that two
palettes were visible at once for nearly half a second, and the chrome
looked like a ghost of itself rather than like a room being re-lit. At
0.36 the crossfade is over before the eye starts to look for the second
palette.

**How it is done.** The raster of the chrome as it was is banked, and the
new chrome is composited offscreen; the two whole images are then
crossfaded. Drawing the new dock straight over the old one would
attenuate the old chrome a second time underneath it and let it show
through the new. The software path does it with a packed-integer blend of
two frame buffers; the accelerated path does it in the texture unit. It
costs a render target for the 0.36 seconds it runs.

---

## What is deliberately still

Some things do not move, and the absence is a decision.

**The window area.** Windows exist (`finnwindow`), but they are the shell's
own: `ui/Desktop.fui` describes a client window area, and the compositor
does not draw one because no client can attach a surface to it. The design
system does not pretend it exists.

**The dock's bubbles.** They do not lift, grow or shrink under the
pointer. They take a wash. A dock that springs its icons upward is a
different operating system's dock.

**The wallpaper.** It is decoded once and it never changes. The
appearance switch leaves it alone.

**The tray's contents.** The battery's charge level and the wifi arcs
change as the hardware changes, and they change instantly. They are
gauges, not animations.

**The pointer.** The cursor sprite is composited over a cached base and
does not trail, stretch or lag. It is the one thing on screen that must
never lie about where the pointer is.

---

## The cost of a frame

Motion in this shell is not free, and the compositor is built so that it
barely is. The rules that keep it that way:

- **A surface that is only transforming does not re-rasterise.** The
  raster is built once and the *transform* animates: the software path
  composites it through `finn_blit_scaled()`, the accelerated path hands
  the same rectangle to the GPU as a quad. One description of where the
  surface is, used by both.
- **A surface whose contents animate keeps its glass.** This is what the
  Start Menu's split between glass and cells buys: the frost is not
  recomputed for every frame of the arrival.
- **Text is the most expensive thing the shell draws.** A name is
  rasterised by FreeType when it changes and not otherwise, which is why
  the Start Menu's cells are cached individually.
- **The frame rate is the page flip.** The compositor never runs ahead of
  the display: the next frame is due when the last one has landed, and a
  track that is still moving only shortens the poll.

---

## A table, for the impatient

| Thing | From | To | Duration | Easing |
|---|---|---|---|---|
| a surface opens | 0.92 scale, transparent | 1.0 scale, opaque | 0.28 s | out-back |
| a surface closes | 1.0 scale, opaque | 0.92 scale, transparent | 0.18 s | in-out |
| the scrim arrives | 0 | 0.30 black | 0.20 s | out |
| the scrim leaves | 0.30 black | 0 | 0.14 s | in-out |
| a hover arrives | no wash | 0.055 ink | 0.11 s | out |
| a hover leaves | 0.055 ink | no wash | 0.20 s | out |
| a press arrives | no wash | 0.090 ink | 0.08 s | out |
| a press leaves | 0.090 ink | no wash | 0.18 s | out |
| a slider follows the hardware | its old value | the new value | 0.20 s | out |
| an app tile arrives | 10 px low, transparent | in place, opaque | 0.20 s | out |
| the minute turns | full ink | 45% ink, and back | 0.26 s | linear |
| a notification arrives | the panel's glass | the tile's glass | 0.22 s | out |
| the appearance switches | the old chrome | the new chrome | 0.36 s | in-out |
