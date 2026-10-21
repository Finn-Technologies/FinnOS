#ifndef FINNDRAW_H
#define FINNDRAW_H

#include <stdint.h>
#include <stddef.h>
#include "finnvg.h"

/*
 * finndraw — the shared drawing primitives of the FinnOS shell.
 *
 * The dock and the Control Center are both composed into transparent
 * offscreen layers that the compositor blits over the wallpaper, so they
 * both need the same straight-alpha source-over, the same rounded-rect
 * coverage function and the same icon tint convention. Keeping one copy
 * stops the two surfaces from drifting apart visually.
 *
 * Colours are 0xAARRGGBB, matching the DRM XRGB8888 scanout buffer.
 */

/* tokens (ui/_tokens.fui)
 *
 * These are variables rather than constants because the shell has an
 * appearance: switching light/dark swaps every surface at once, and both the
 * dock and the Control Center pick the change up without being told. */
extern uint32_t FINN_BONE;     /* chrome surface                    */
extern uint32_t FINN_TILE;     /* inset tile                        */
extern uint32_t FINN_TILE_ON;  /* tile carrying an active control   */
extern uint32_t FINN_HAIR;     /* hairline                          */
extern uint32_t FINN_INK;      /* primary ink                       */
extern uint32_t FINN_MUTE;     /* secondary / inactive              */
extern uint32_t FINN_DIM;      /* detail text: stronger than mute   */
extern uint32_t FINN_ACCENT;   /* family accent                     */
extern uint32_t FINN_TRACK;    /* slider track                      */
extern uint32_t FINN_KNOB;     /* slider knob / glyph circle        */

/*
 * FINN_ON and FINN_FILL are the stronger versions of the accent/track pair.
 * A *filled* status circle or the filled part of a slider bar is a solid
 * shape, not text: the pastel FINN_ACCENT that reads well as a glyph on white
 * turns to mush as a 31px disc, and the reference measures a warm grey
 * (#998481) for the bar's lit portion. Keeping them as tokens lets the dark
 * palette lighten the bar without touching the accent used for text.
 */
extern uint32_t FINN_ON;       /* filled, "on" status circle        */
extern uint32_t FINN_FILL;     /* filled portion of a slider bar    */

/*
 * The tint of the frosted chrome (dock, Control Center).
 *
 * This is not FINN_BONE, although the dock's opaque ancestor used bone. The
 * reference resolves the dock and the panel to white at ~0.69 over the blurred
 * wallpaper: 0.7·246 + 0.3·97 puts the blue channel 17 points below the
 * reference, which turns the glass ochre instead of cream. The glass tint is
 * therefore near-white in the light appearance and the deep bone in the dark
 * one, where "frosted" has to mean darkened rather than brightened.
 */
extern uint32_t FINN_GLASS;

void finn_theme_set(int dark);
int  finn_theme_dark(void);

/*
 * Source-over in straight-alpha space. The naive "lerp toward the source"
 * form darkens anti-aliased pixels, because the destination of a fresh layer
 * is transparent black rather than the wallpaper. Dividing by the accumulated
 * alpha is what removes the dark rim around the bubbles.
 */
uint32_t finn_over(uint32_t dst, uint32_t src, float a);

/* Rounded-rect coverage by signed distance, so corners stay clean at any
 * radius and the edges are anti-aliased rather than thresholded. */
void finn_fill_rrect(uint32_t *b, int w, int h, float x, float y,
                     float rw, float rh, float r, uint32_t c);

/*
 * Fill a rounded rect with the blurred backdrop tinted by `tint`.
 * (ox,oy) is the layer's origin on screen, so the glass is sampled from the
 * wallpaper actually underneath this rect — a frosted surface that samples
 * the wrong place reads instantly as a sticker rather than as glass.
 *
 * Where the layer already carries an opaque surface the tint is laid over
 * that instead, so one frost over another compounds rather than replaces —
 * the same glass, laid down twice.
 *
 * Falls back to a plain full-opacity fill with `tint` when no backdrop has
 * been built, so the shell still draws sensibly without a blur.
 */
void finn_frost_rrect(uint32_t *b, int w, int h, int ox, int oy,
                      float x, float y, float rw, float rh, float r,
                      uint32_t tint, float alpha);

/* Blend src over dst at (dx,dy), skipping fully transparent source pixels. */
void finn_blit(uint32_t *dst, int dw, int dh, const uint32_t *src, int sw, int sh,
               int dx, int dy);

/* Blend src over dst at (dx,dy), scaled to fit (rw,rh) and multiplied by a
 * global alpha. Bilinear, and sampled in premultiplied space: the source is
 * straight alpha whose transparent neighbourhood is black, so filtering it
 * as-is would smear a dark rim around anything that moves. */
void finn_blit_scaled(uint32_t *dst, int dw, int dh,
                      const uint32_t *src, int sw, int sh,
                      float x, float y, float rw, float rh, float alpha);

/* The Thanos dissolve: pixels above a noise threshold vanish, and the ones
 * nearest it glow warm as they go. Same geometry as finn_blit_scaled. */
void finn_blit_dissolve(uint32_t *dst, int dw, int dh,
                        const uint32_t *src, int sw, int sh,
                        float x, float y, float rw, float rh,
                        float alpha, float dust);

/* Crossfade two opaque frames: dst = lerp(a, b, w/256). Done with the packed
 * integer trick so a full-screen appearance switch costs about one memcpy. */
void finn_frame_blend(uint32_t *dst, const uint32_t *a, const uint32_t *b,
                      size_t n, uint32_t w256);

/*
 * Darken a whole frame by `amount` (0 = untouched, 1 = black).
 *
 * This is the scrim behind a modal surface: the desktop does not go away
 * when the Start Menu opens, it stops being the thing you are looking at.
 */
void finn_frame_dim(uint32_t *dst, size_t n, float amount);

/* The assets encode "inactive" as #969696 and "active" as black, so a single
 * palette maps every icon onto the current state colours. */
typedef struct { uint32_t active, inactive; } finn_palette;
uint32_t finn_tint_pal(uint32_t c, void *ud);

/*
 * Rasterising a 4x supersampled glyph is the most expensive thing the shell
 * does, and an animating surface redraws it sixty times a second, so rasters
 * are cached on (svg, size, tints, content revision).
 *
 * The revision matters: the wifi arcs, the battery charge bar and the volume
 * waves are all expressed by mutating the parsed shape before rasterising,
 * so two rasters of the same svg at the same size can legitimately differ.
 * Callers that mutate a shape must fold what they changed into `vkey`;
 * callers that do not pass 0.
 */
const uint32_t *finn_icon_ref(fsvg *svg, int px, uint32_t active,
                              uint32_t inactive, uint32_t vkey);

/* Rasterise and blit, tinted, at (x,y). */
void finn_icon_blit(fsvg *svg, uint32_t *dst, int dw, int dh,
                    int x, int y, int px, uint32_t active, uint32_t inactive,
                    uint32_t vkey);

/* Same, but hands back a private copy the caller may dim or clip (the battery
 * charge bar, the dimmed app placeholder). Overwritten by the next call. */
uint32_t *finn_icon_tile(fsvg *svg, int px, uint32_t active, uint32_t inactive,
                         uint32_t vkey);

/*
 * Placement by ink rather than by viewBox.
 *
 * `ink` is how many pixels the drawing's longer dimension should occupy and
 * (cx,cy) is where its centre belongs. Writes the raster size and the top-left
 * to blit it at, which is not the same as centring the raster: the asset's
 * drawing is offset inside its box by however much padding it was authored
 * with.
 *
 * This is what makes a tray or a grid read as one set — the assets fill their
 * boxes by different amounts, so equal viewBox sizes come out visibly unequal.
 *
 * Two assets that share one coordinate box (the battery and its charging bolt)
 * can be blitted through the same frame, which keeps their internal alignment.
 */
void finn_icon_frame(const fsvg *svg, int ink, int cx, int cy,
                     int *px, int *ox, int *oy);

/* finn_icon_frame() followed by a blit, for the common case. */
void finn_icon_blit_ink(fsvg *svg, uint32_t *dst, int dw, int dh,
                        int cx, int cy, int ink, uint32_t active,
                        uint32_t inactive, uint32_t vkey);

#endif
