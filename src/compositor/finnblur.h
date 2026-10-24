#ifndef FINNBLUR_H
#define FINNBLUR_H

#include <stdint.h>

/*
 * finnblur — the frosted backdrop of the shell.
 *
 * A frosted surface blurs *what is behind it*, and what is behind it is not
 * only the wallpaper: a window can sit behind the Start Menu, the dock can
 * sit over a window, and a window can sit over another window. So the
 * backdrop is a live picture of the desktop, not a picture of the wallpaper.
 *
 * The compositor is the only thing that knows what the desktop currently
 * looks like, so it drives this module in three steps per frame:
 *
 *   finnblur_reset()          back to the wallpaper, before windows draw
 *   finnwindow_render()       windows frost, then bake themselves in
 *                             through finnblur_patch()
 *   finnblur_live(backdrop)   the desktop without the chrome, blurred
 *
 * The blur is built at 1/8 scale with three 2px box passes, which comes out
 * at roughly 40px of blur at full resolution: frosted, not smeared. A live
 * rebuild costs a downsample of the screen and a few passes over a small
 * buffer, and only happens when the desktop underneath actually changed.
 *
 * `finnblur_generation()` moves whenever the live blur changed. Surfaces
 * fold it into their build key, so a surface whose backdrop moved redraws
 * instead of keeping yesterday's glass.
 *
 * The module is deliberately self-contained — it knows nothing about finndraw
 * or the layers that sample it — so it stays testable on its own and the
 * include graph stays acyclic.
 *
 * Colours are 0xAARRGGBB, matching the rest of the shell.
 */

/* The downsample factor. 1/8 keeps the small buffer at a few hundred KB even
 * at 4K. */
#define FINNBLUR_SCALE 8

/*
 * Build the wallpaper baseline from a screen-sized, opaque wallpaper buffer
 * of packed 0xAARRGGBB pixels. Safe to call again — a mode change simply
 * rebuilds the baseline — and it leaves the live backdrop equal to it.
 */
void finnblur_wall(const uint32_t *wall, int w, int h);

/*
 * Rebuild the live backdrop from the composed desktop (wallpaper and the
 * windows over it, without the chrome). Leaves the backdrop not-ready when
 * the input is unusable.
 */
void finnblur_live(const uint32_t *desktop, int w, int h);

/*
 * The same, for a desktop that changed only inside (rx,ry,rw,rh) in screen
 * coordinates. Every other cell of the live backdrop is left alone, which is
 * what lets a window be dragged or redrawn without paying for a whole-screen
 * rebuild on each frame.
 */
void finnblur_live_rect(const uint32_t *desktop, int w, int h,
                        int rx, int ry, int rw, int rh);

/*
 * Restore the live backdrop to the wallpaper baseline, so the first window of
 * a frame frosts the wallpaper rather than the previous frame's glass. Has no
 * effect, and does not move the generation, before a baseline exists.
 */
void finnblur_reset(void);

/*
 * Bake a rectangle of an already-drawn layer into the live backdrop, so the
 * next window in the z-order frosts this one rather than the wallpaper.
 *
 * (lx,ly) is the layer's screen origin and (rx,ry,rw,rh) is the baked rect in
 * screen coordinates. The rect is expected to be an opaque window body; the
 * rounded corners it leaves transparent are blended rather than punched
 * through. Screen pixels outside the layer are ignored.
 */
void finnblur_patch(const uint32_t *layer, int lw, int lh, int lx, int ly,
                    int rx, int ry, int rw, int rh);

void finnblur_free(void);
int  finnblur_ready(void);

/* Moves whenever the live backdrop changed. Never zero. */
unsigned finnblur_generation(void);

/*
 * Sample the backdrop at a screen coordinate — bilinear, clamped at the small
 * buffer's bounds. Returns an opaque 0xAARRGGBB colour, or 0 when no backdrop
 * has been built.
 */
uint32_t finnblur_at(int x, int y);

#endif
