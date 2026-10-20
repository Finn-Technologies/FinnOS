#ifndef FINNVG_H
#define FINNVG_H

#include <stdint.h>

/*
 * finnvg — minimal SVG subset rasteriser for FinnOS iconography.
 *
 * Supports: <path> (M L H V C S Q T Z), <rect> (+rx), <circle>,
 * fill / stroke / stroke-width, transform (matrix, translate, scale,
 * rotate) and the colours #rgb, #rrggbb, rgb(), black, white, none.
 *
 * Subpaths are kept separate on purpose. The wifi asset is a single <path>
 * whose four subpaths are three arcs and a dot, so the shell can light them
 * up one at a time as signal improves without needing four files.
 *
 * Fill uses the non-zero winding rule. Stroke is rendered as the union of
 * round-capped capsules over the flattened polyline, which yields exact
 * round caps and round joins with no polygon offsetting.
 */

typedef struct { float x, y; } fv2;

typedef struct {
    fv2 *p;
    int  n, cap;
    int  closed;
    int  vis;            /* 1 = draw this subpath                    */
    uint32_t col;        /* 0 = inherit the shape colour             */
} fpoly;

typedef struct {
    fpoly   *polys;
    int      npolys, cpolys;
    uint32_t fill;       /* 0xAARRGGBB; alpha 0 means no fill          */
    uint32_t stroke;     /* 0xAARRGGBB; alpha 0 means no stroke        */
    float    stroke_w;
    int      visible;    /* set 0 to hide this shape entirely          */
    float    clip_x;     /* fill is discarded where x > clip_x (charge) */
    float    bx0, by0, bx1, by1;   /* geometry bbox, SVG user units    */
} fshape;

typedef struct {
    fshape *shapes;
    int     n, cap;
    float   vw, vh;
} fsvg;

/*
 * SVG colours are routed through this so one asset can be drawn in any state
 * palette (active ink / inactive grey / accent) without duplicating files.
 * Return 0xAARRGGBB; return 0 to drop the shape.
 */
typedef uint32_t (*fsvg_tint_fn)(uint32_t svg_color, void *ud);

int  fsvg_load(const char *path, fsvg *s);
void fsvg_free(fsvg *s);

/* Rasterise into an RGBA buffer (0xAARRGGBB) of w x h pixels. */
void fsvg_raster(const fsvg *s, uint32_t *dst, int w, int h,
                 fsvg_tint_fn tint, void *ud);

/*
 * The box the drawing actually occupies, in SVG user units: every shape's
 * geometry, stroke included, whether or not that shape is currently visible.
 *
 * The assets are not drawn to a common padding — start_button fills its 24
 * unit box, battery uses half of it, ethernet two thirds. Laying them out by
 * the viewBox therefore gives a row in which the same nominal size renders at
 * visibly different sizes. The shell lays out by ink instead, and this is the
 * measurement that makes that possible.
 *
 * Visibility is deliberately ignored: an icon must not change size because a
 * signal went from three arcs to two.
 */
void fsvg_ink(const fsvg *s, float *x0, float *y0, float *x1, float *y1);

/* Convenience: recolour everything to a single ink, preserving alpha. */
uint32_t fsvg_mono(uint32_t svg_color, void *ud);

#endif
