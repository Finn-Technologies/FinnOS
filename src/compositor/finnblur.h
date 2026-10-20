#ifndef FINNBLUR_H
#define FINNBLUR_H

#include <stdint.h>

/*
 * finnblur — the frosted backdrop of the shell.
 *
 * Why this is precomputed rather than blurred per frame: the only thing a
 * frosted surface ever sits on is the wallpaper, and the wallpaper is static
 * — it is decoded once at startup and it is the one surface the appearance
 * switch deliberately leaves alone. So the blur is not a per-frame effect at
 * all. Blurring it once, at 1/8 scale, costs nothing per frame and gives real
 * frosted glass for free: the GPU only ever uploads the finished chrome
 * texture, and a redraw costs exactly what it did before there was glass.
 *
 * The module is deliberately self-contained — it knows nothing about finndraw
 * or the layers that sample it — so it stays testable on its own and the
 * include graph stays acyclic.
 *
 * Colours are 0xAARRGGBB, matching the rest of the shell.
 */

/* The downsample factor. 1/8 keeps the small buffer at a few hundred KB even
 * at 4K, and three 2px box passes at this scale come out at roughly 40px of
 * blur at full resolution: frosted, not smeared. */
#define FINNBLUR_SCALE 8

/*
 * Build the backdrop from a screen-sized, opaque wallpaper buffer of packed
 * 0xAARRGGBB pixels. Safe to call again — any previous backdrop is released
 * first — so a mode change can simply rebuild it. Leaves the backdrop
 * not-ready when the input is unusable.
 */
void finnblur_build(const uint32_t *wall, int w, int h);

void finnblur_free(void);
int  finnblur_ready(void);

/*
 * Sample the backdrop at a screen coordinate — bilinear, clamped at the small
 * buffer's bounds. Returns an opaque 0xAARRGGBB colour, or 0 when no backdrop
 * has been built.
 */
uint32_t finnblur_at(int x, int y);

#endif
