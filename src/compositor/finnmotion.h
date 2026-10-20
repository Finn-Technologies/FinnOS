#ifndef FINNMOTION_H
#define FINNMOTION_H

/*
 * finnmotion — the shell's motion, in one place.
 *
 * The dock, the Control Center, the clock page and the Start Menu are four
 * surfaces of one shell. They are glass, they are the same corner radii and
 * they carry the same type scale, so they have to move as one family too: a
 * panel that opens in 0.26s next to one that opens in 0.28s reads as two
 * shells that happen to share a screen.
 *
 * Every duration, every wash and every easing the shell uses is here. A
 * surface that needs its own number should either not need one or should
 * say why in this file — not carry a private constant next to its
 * neighbour's.
 *
 * The rules the numbers encode:
 *
 *   - Opening is slower than closing. A surface takes FINN_MOTION_OPEN to
 *     arrive and FINN_MOTION_CLOSE to leave, and the close is about two
 *     thirds of the open. The asymmetry is the point: a surface that takes
 *     as long to leave as to arrive feels like it is stuck, and one that
 *     leaves instantly feels like it was snatched away.
 *
 *   - Nothing moves under the pointer. A control answers a pointer with a
 *     wash of ink, never with a change of size or position: a control that
 *     flinches away from the finger is a control that has moved out from
 *     under it.
 *
 *   - The pointer's answer is quick and the pointer's withdrawal is slow.
 *     The wash arrives in FINN_MOTION_HOVER_IN and leaves over
 *     FINN_MOTION_HOVER_OUT, so a control that is hovered and then
 *     abandoned fades rather than snaps off.
 *
 *   - Nothing runs on a timer. A duration here is the length of a track, not
 *     a period: the compositor asks for a frame while a track is moving and
 *     goes back to blocking on input when the last one settles.
 */

#include "finnanim.h"

/* ---------------- surfaces ----------------
 *
 * Every surface that opens and closes shares these four numbers: the
 * Control Center, the clock page and the Start Menu are the same object
 * with different contents.
 */
#define FINN_MOTION_OPEN        0.28f  /* a surface arriving            */
#define FINN_MOTION_CLOSE       0.18f  /* a surface leaving             */
#define FINN_MOTION_OPEN_SCALE  0.92f  /* the scale it arrives from     */
#define FINN_MOTION_FADE        1.15f  /* the alpha ramp's reach        */

/* ---------------- the scrim behind the Start Menu ----------------
 *
 * The room dims before the door opens and brightens before it shuts, so the
 * menu is always the only lit thing in the room and the eye is led to it
 * rather than to the place it used to be.
 */
#define FINN_MOTION_SCRIM       0.30f  /* how dark the room goes        */
#define FINN_MOTION_SCRIM_IN    0.20f  /* the dim arriving              */
#define FINN_MOTION_SCRIM_OUT   0.14f  /* the dim lifting               */

/* ---------------- the pointer ----------------
 *
 * One wash, one set of durations, for every surface. The dock, the Control
 * Center, the clock page and the Start Menu all answer the pointer the
 * same way, because they are the same shell.
 */
#define FINN_MOTION_HOVER_WASH  0.055f
#define FINN_MOTION_PRESS_WASH  0.090f
#define FINN_MOTION_HOVER_IN    0.11f
#define FINN_MOTION_HOVER_OUT   0.20f
#define FINN_MOTION_PRESS_IN    0.08f
#define FINN_MOTION_PRESS_OUT   0.18f

/* ---------------- values ----------------
 *
 * A slider's bar travels to the hardware's reading rather than jumping to
 * it, so a change made elsewhere on the machine is seen rather than
 * discovered.
 */
#define FINN_MOTION_FILL        0.20f

/* ---------------- the appearance switch ----------------
 *
 * The chrome dissolves from one palette to the other. It is short: the
 * wallpaper underneath does not change, so a long crossfade would leave two
 * palettes visible at once for half a second and the furniture would look
 * like a ghost of itself rather than like a room being re-lit.
 */
#define FINN_MOTION_THEME       0.36f

/* ---------------- the clock ----------------
 *
 * The minute turns on the page with a short dip of ink. It is the only
 * change in the shell that happens on its own, so it gets the only motion
 * that is not a response to something the person did.
 */
#define FINN_MOTION_CLOCK_TICK  0.26f

/* ---------------- a notification arriving ----------------
 *
 * The newest tile on the clock page is an inset that deepens into place
 * rather than a box that appears. The tile's glass starts at the panel's
 * own weight — which is exactly the panel's colour, so it is invisible —
 * and comes down to the tile's weight as the words arrive.
 */
#define FINN_MOTION_NOTE        0.22f

#endif
