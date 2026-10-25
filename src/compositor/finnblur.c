/* finnblur — the live frosted backdrop of the shell. */
#include "finnblur.h"

#include <stdlib.h>
#include <string.h>

/* Box radius, per separable pass. See finnblur.h for why three of these at
 * 1/8 scale read as a full-resolution frosted blur. */
#define BLUR_R 2
#define BLUR_PASSES 3

/* How far the passes reach, in small-buffer cells. A patch has to recompute
 * this much more than the rect it changed, or the cells at its edge would
 * still carry the colour of the surface that was just covered. */
#define BLUR_SPREAD (BLUR_R * BLUR_PASSES)

static uint32_t *g_live = NULL;   /* the blurred desktop, sampled by the glass */
static uint32_t *g_wall = NULL;   /* the wallpaper alone, blurred: the baseline */
static uint32_t *g_base = NULL;   /* the wallpaper alone, sharp: what a partial
                                   * rebuild puts back under the blocks it
                                   * recomputes, so those blocks are blurred
                                   * once rather than twice */
static uint32_t *g_tmp  = NULL;   /* one scratch buffer for the passes */
static int g_sw = 0, g_sh = 0;
static int g_ready = 0;
static unsigned g_gen = 1;
static uint32_t g_hash = 0;   /* fingerprint of the last live backdrop */

/* scratch for a regional blur, kept between frames */
static uint32_t *g_scratch_a = NULL, *g_scratch_b = NULL;
static size_t g_scratch_cells = 0;

int finnblur_ready(void) { return g_ready; }
unsigned finnblur_generation(void) { return g_gen; }

void finnblur_free(void) {
    free(g_live); g_live = NULL;
    free(g_wall); g_wall = NULL;
    free(g_base); g_base = NULL;
    free(g_tmp);  g_tmp = NULL;
    free(g_scratch_a); g_scratch_a = NULL;
    free(g_scratch_b); g_scratch_b = NULL;
    g_scratch_cells = 0;
    g_sw = g_sh = 0;
    g_ready = 0;
}

static inline uint32_t pack(int r, int g, int b) {
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/*
 * Horizontal box pass, edges clamped. Alpha is already 0xFF everywhere, so it
 * is re-stamped rather than averaged — carrying it through would only cost
 * time and let rounding nibble at it.
 *
 * The window slides: five taps at x and five at x+1 share four of their
 * indices, so subtract the one that left and add the one that arrived.
 * Clamping at the edges is the same index rule on both sides, so the
 * running sum is exactly the sum the nested loop would have produced.
 */
static void box_h(const uint32_t *in, uint32_t *out, int w, int h) {
    int n = 2 * BLUR_R + 1;
    for (int y = 0; y < h; y++) {
        const uint32_t *row = in + (size_t)y * w;
        uint32_t *o = out + (size_t)y * w;

        int rs = 0, gs = 0, bs = 0;
        for (int k = -BLUR_R; k <= BLUR_R; k++) {
            int xx = k < 0 ? 0 : (k >= w ? w - 1 : k);
            uint32_t c = row[xx];
            rs += (int)(c & 0xFFu);
            gs += (int)((c >> 8) & 0xFFu);
            bs += (int)((c >> 16) & 0xFFu);
        }

        for (int x = 0; x < w; x++) {
            o[x] = pack(rs / n, gs / n, bs / n);
            if (x + 1 >= w) break;

            int xo = x - BLUR_R;
            if (xo < 0) xo = 0;
            int xi = x + BLUR_R + 1;
            if (xi >= w) xi = w - 1;

            uint32_t c = row[xo];
            rs -= (int)(c & 0xFFu);
            gs -= (int)((c >> 8) & 0xFFu);
            bs -= (int)((c >> 16) & 0xFFu);

            c = row[xi];
            rs += (int)(c & 0xFFu);
            gs += (int)((c >> 8) & 0xFFu);
            bs += (int)((c >> 16) & 0xFFu);
        }
    }
}

/* Vertical box pass, edges clamped. Same sliding window, one column at a
 * time; the seed repeats the top cell the way the clamped loop did. */
static void box_v(const uint32_t *in, uint32_t *out, int w, int h) {
    int n = 2 * BLUR_R + 1;
    for (int x = 0; x < w; x++) {
        int rs = 0, gs = 0, bs = 0;
        for (int k = -BLUR_R; k <= BLUR_R; k++) {
            int yy = k < 0 ? 0 : (k >= h ? h - 1 : k);
            uint32_t c = in[(size_t)yy * w + x];
            rs += (int)(c & 0xFFu);
            gs += (int)((c >> 8) & 0xFFu);
            bs += (int)((c >> 16) & 0xFFu);
        }

        for (int y = 0; y < h; y++) {
            out[(size_t)y * w + x] = pack(rs / n, gs / n, bs / n);
            if (y + 1 >= h) break;

            int yo = y - BLUR_R;
            if (yo < 0) yo = 0;
            int yi = y + BLUR_R + 1;
            if (yi >= h) yi = h - 1;

            uint32_t c = in[(size_t)yo * w + x];
            rs -= (int)(c & 0xFFu);
            gs -= (int)((c >> 8) & 0xFFu);
            bs -= (int)((c >> 16) & 0xFFu);

            c = in[(size_t)yi * w + x];
            rs += (int)(c & 0xFFu);
            gs += (int)((c >> 8) & 0xFFu);
            bs += (int)((c >> 16) & 0xFFu);
        }
    }
}

/* Three separable passes over the whole small buffer. */
static void blur_all(uint32_t *small, uint32_t *tmp, int sw, int sh) {
    for (int p = 0; p < BLUR_PASSES; p++) {
        box_h(small, tmp, sw, sh);
        box_v(tmp, small, sw, sh);
    }
}

/*
 * Box-average a screen-sized image down to the small scale. Opaque input: the
 * alpha channel is not carried, and every sample is stamped 0xFF. The last
 * block on each edge is short when the size is not a multiple of the factor,
 * which is why the averages count their own taps.
 */
static void downsample_rect(const uint32_t *src, int w, int h,
                            int csx0, int csy0, int csx1, int csy1,
                            uint32_t *small) {
    if (csx0 < 0) csx0 = 0;
    if (csy0 < 0) csy0 = 0;
    if (csx1 > g_sw - 1) csx1 = g_sw - 1;
    if (csy1 > g_sh - 1) csy1 = g_sh - 1;
    for (int sy = csy0; sy <= csy1; sy++) {
        int y0 = sy * FINNBLUR_SCALE;
        int y1 = y0 + FINNBLUR_SCALE; if (y1 > h) y1 = h;
        for (int sx = csx0; sx <= csx1; sx++) {
            int x0 = sx * FINNBLUR_SCALE;
            int x1 = x0 + FINNBLUR_SCALE; if (x1 > w) x1 = w;
            int rs = 0, gs = 0, bs = 0;
            /* The tap count is the block's area; the last block on an edge
             * is short, so count it once here instead of once per pixel. */
            int n = (y1 - y0) * (x1 - x0);
            for (int yy = y0; yy < y1; yy++) {
                const uint32_t *row = src + (size_t)yy * w;
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t c = row[xx];
                    rs += (int)(c & 0xFFu);
                    gs += (int)((c >> 8) & 0xFFu);
                    bs += (int)((c >> 16) & 0xFFu);
                }
            }
            if (n < 1) n = 1;
            small[(size_t)sy * g_sw + sx] = pack(rs / n, gs / n, bs / n);
        }
    }
}

static void downsample(const uint32_t *src, int w, int h, uint32_t *small) {
    downsample_rect(src, w, h, 0, 0, g_sw - 1, g_sh - 1, small);
}

/* Allocate the three small buffers for this geometry, keeping them if the
 * geometry has not moved. Returns 0 when there is nothing to work with. */
static int reserve(int w, int h) {
    if (!w || !h || w < FINNBLUR_SCALE || h < FINNBLUR_SCALE) return 0;
    int sw = (w + FINNBLUR_SCALE - 1) / FINNBLUR_SCALE;
    int sh = (h + FINNBLUR_SCALE - 1) / FINNBLUR_SCALE;
    if (sw == g_sw && sh == g_sh && g_live && g_wall && g_tmp) return 1;

    uint32_t *live = malloc((size_t)sw * sh * 4);
    uint32_t *wall = malloc((size_t)sw * sh * 4);
    uint32_t *base = malloc((size_t)sw * sh * 4);
    uint32_t *tmp  = malloc((size_t)sw * sh * 4);
    if (!live || !wall || !base || !tmp) {
        free(live); free(wall); free(base); free(tmp);
        return 0;
    }
    free(g_live); free(g_wall); free(g_base); free(g_tmp);
    g_live = live; g_wall = wall; g_base = base; g_tmp = tmp;
    g_sw = sw; g_sh = sh;
    return 1;
}

void finnblur_wall(const uint32_t *wall, int w, int h) {
    g_ready = 0;
    if (!wall || !reserve(w, h)) return;

    downsample(wall, w, h, g_base);
    memcpy(g_wall, g_base, (size_t)g_sw * g_sh * 4);
    blur_all(g_wall, g_tmp, g_sw, g_sh);
    memcpy(g_live, g_wall, (size_t)g_sw * g_sh * 4);
    g_ready = 1;
    g_gen++;
}

/* The small buffer's fingerprint, kept so an unchanged backdrop does not
 * invalidate the glass that samples it. */
static void bump_generation(void) {
    uint32_t hsh = 2166136261u;
    for (size_t i = 0; i < (size_t)g_sw * g_sh; i++) {
        hsh ^= g_live[i];
        hsh *= 16777619u;
    }
    if (hsh != g_hash) {
        g_hash = hsh;
        g_gen++;
    }
}

void finnblur_live(const uint32_t *desktop, int w, int h) {
    g_ready = 0;
    if (!desktop || !reserve(w, h)) return;

    downsample(desktop, w, h, g_live);
    blur_all(g_live, g_tmp, g_sw, g_sh);
    g_ready = 1;

    /*
     * A frame that rebuilt the same picture must not invalidate every
     * surface's glass: the generation is the surfaces' cue to redraw, and a
     * dock hover would otherwise re-rasterise an open Start Menu. Comparing
     * the small buffer is a few dozen microseconds against that.
     */
    bump_generation();
}

void finnblur_reset(void) {
    if (!g_ready || !g_live || !g_wall) return;
    memcpy(g_live, g_wall, (size_t)g_sw * g_sh * 4);
}

/*
 * Recompute the blur over a small-buffer rect, with enough of a margin that
 * the cells inside the rect are true. The margin is what the passes would
 * have pulled in from outside, so it is copied from the current live buffer
 * and discarded on the way back.
 */
static void blur_region(int sx0, int sy0, int sx1, int sy1) {
    if (sx1 < sx0 || sy1 < sy0) return;
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > g_sw - 1) sx1 = g_sw - 1;
    if (sy1 > g_sh - 1) sy1 = g_sh - 1;

    int ex0 = sx0 - BLUR_SPREAD; if (ex0 < 0) ex0 = 0;
    int ey0 = sy0 - BLUR_SPREAD; if (ey0 < 0) ey0 = 0;
    int ex1 = sx1 + BLUR_SPREAD; if (ex1 > g_sw - 1) ex1 = g_sw - 1;
    int ey1 = sy1 + BLUR_SPREAD; if (ey1 > g_sh - 1) ey1 = g_sh - 1;

    int ew = ex1 - ex0 + 1, eh = ey1 - ey0 + 1;
    if (ew <= 0 || eh <= 0) return;

    /* The scratch is kept between frames: a window being dragged runs this on
     * every frame, and two mallocs per frame is allocator traffic that shows
     * up as jitter in the drag. */
    size_t cells = (size_t)ew * eh;
    if (cells > g_scratch_cells) {
        uint32_t *na = realloc(g_scratch_a, cells * 4);
        if (na) g_scratch_a = na;
        uint32_t *nb = na ? realloc(g_scratch_b, cells * 4) : NULL;
        if (nb) g_scratch_b = nb;
        if (!na || !nb) return;
        g_scratch_cells = cells;
    }
    uint32_t *a = g_scratch_a, *b = g_scratch_b;
    if (!a || !b) return;

    for (int y = 0; y < eh; y++)
        memcpy(a + (size_t)y * ew, g_live + (size_t)(ey0 + y) * g_sw + ex0,
               (size_t)ew * 4);

    for (int p = 0; p < BLUR_PASSES; p++) {
        box_h(a, b, ew, eh);
        box_v(b, a, ew, eh);
    }

    for (int y = sy0; y <= sy1; y++)
        memcpy(g_live + (size_t)y * g_sw + sx0, a + (size_t)(y - ey0) * ew + (sx0 - ex0),
               (size_t)(sx1 - sx0 + 1) * 4);
}

/*
 * Rebuild the live backdrop where the desktop changed, rather than all of it.
 *
 * A window that moves, redraws or animates only repaints its own rect, so the
 * blocks that rect covers are put back to the sharp wallpaper and blurred
 * again, with enough margin that the passes around them see real neighbours.
 * That is a few thousand cells instead of every cell on the screen — the
 * difference between a drag that holds its frame rate and one that does not.
 */
void finnblur_live_rect(const uint32_t *desktop, int w, int h,
                        int rx, int ry, int rw, int rh) {
    if (!desktop || rw <= 0 || rh <= 0 || !g_live || !reserve(w, h)) return;
    g_ready = 0;

    int sx0 = rx / FINNBLUR_SCALE, sy0 = ry / FINNBLUR_SCALE;
    int sx1 = (rx + rw - 1) / FINNBLUR_SCALE;
    int sy1 = (ry + rh - 1) / FINNBLUR_SCALE;

    int ex0 = sx0 - BLUR_SPREAD, ey0 = sy0 - BLUR_SPREAD;
    int ex1 = sx1 + BLUR_SPREAD, ey1 = sy1 + BLUR_SPREAD;
    if (ex0 < 0) ex0 = 0;
    if (ey0 < 0) ey0 = 0;
    if (ex1 > g_sw - 1) ex1 = g_sw - 1;
    if (ey1 > g_sh - 1) ey1 = g_sh - 1;
    if (ex1 < ex0 || ey1 < ey0) { g_ready = 1; return; }

    for (int sy = ey0; sy <= ey1; sy++)
        memcpy(g_live + (size_t)sy * g_sw + ex0, g_base + (size_t)sy * g_sw + ex0,
               (size_t)(ex1 - ex0 + 1) * 4);

    downsample_rect(desktop, w, h, ex0, ey0, ex1, ey1, g_live);

    /* blur_region supplies the margin the passes need; it is already inside
     * the block that was just reset and resampled */
    blur_region(sx0, sy0, sx1, sy1);

    g_ready = 1;
    bump_generation();
}

void finnblur_patch(const uint32_t *layer, int lw, int lh, int lx, int ly,
                    int rx, int ry, int rw, int rh) {
    if (!g_ready || !layer || rw <= 0 || rh <= 0) return;

    int sx0 = rx / FINNBLUR_SCALE, sy0 = ry / FINNBLUR_SCALE;
    int sx1 = (rx + rw - 1) / FINNBLUR_SCALE;
    int sy1 = (ry + rh - 1) / FINNBLUR_SCALE;
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > g_sw - 1) sx1 = g_sw - 1;
    if (sy1 > g_sh - 1) sy1 = g_sh - 1;
    if (sx1 < sx0 || sy1 < sy0) return;

    /*
     * Each small cell averages the same block of the layer the downsample
     * would have averaged, weighted by the layer's own alpha so the rounded
     * corners fade toward the backdrop that was already there instead of
     * leaving a transparent notch in it.
     */
    for (int sy = sy0; sy <= sy1; sy++) {
        int y0 = sy * FINNBLUR_SCALE;
        int y1 = y0 + FINNBLUR_SCALE; if (y1 > ry + rh) y1 = ry + rh;
        if (y0 < ry) y0 = ry;
        for (int sx = sx0; sx <= sx1; sx++) {
            int x0 = sx * FINNBLUR_SCALE;
            int x1 = x0 + FINNBLUR_SCALE; if (x1 > rx + rw) x1 = rx + rw;
            if (x0 < rx) x0 = rx;

            int rs = 0, gs = 0, bs = 0, as = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                int lyy = yy - ly;
                if (lyy < 0 || lyy >= lh) { n++; continue; }
                const uint32_t *row = layer + (size_t)lyy * lw;
                for (int xx = x0; xx < x1; xx++) {
                    int lxx = xx - lx;
                    n++;
                    if (lxx < 0 || lxx >= lw) continue;
                    uint32_t c = row[lxx];
                    int a = (int)(c >> 24);
                    if (!a) continue;
                    rs += (int)(c & 0xFFu) * a;
                    gs += (int)((c >> 8) & 0xFFu) * a;
                    bs += (int)((c >> 16) & 0xFFu) * a;
                    as += a;
                }
            }
            if (!n) continue;

            size_t idx = (size_t)sy * g_sw + sx;
            if (!as) continue;   /* fully transparent block: keep what was there */

            int cov = as / n;    /* 0..255 coverage of this cell by the layer */
            int cr = rs / as, cg = gs / as, cb = bs / as;
            uint32_t d = g_live[idx];
            int dr = (int)(d & 0xFFu), dg = (int)((d >> 8) & 0xFFu), db = (int)((d >> 16) & 0xFFu);

            int rr = (cr * cov + dr * (255 - cov) + 127) / 255;
            int gg = (cg * cov + dg * (255 - cov) + 127) / 255;
            int bb = (cb * cov + db * (255 - cov) + 127) / 255;
            g_live[idx] = pack(clamp255(rr), clamp255(gg), clamp255(bb));
        }
    }

    blur_region(sx0, sy0, sx1, sy1);

    /*
     * The baked window is now part of the backdrop, so every cached plate
     * that sampled the old one is stale. Without this the window above would
     * reuse a frost computed before the window below was baked in — the
     * sharp-through-glass artefact this module exists to avoid.
     */
    bump_generation();
}

uint32_t finnblur_at(int x, int y) {
    if (!g_ready || !g_live) return 0;

    /* Map the screen coordinate into the small buffer's index space, sampling
     * at pixel centres so the 1/8 grid lines up with the full-res one. */
    float fx = ((float)x + 0.5f) / (float)FINNBLUR_SCALE - 0.5f;
    float fy = ((float)y + 0.5f) / (float)FINNBLUR_SCALE - 0.5f;
    float mx = (float)(g_sw - 1), my = (float)(g_sh - 1);
    if (fx < 0.0f) fx = 0.0f;
    if (fx > mx)   fx = mx;
    if (fy < 0.0f) fy = 0.0f;
    if (fy > my)   fy = my;

    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1; if (x1 > g_sw - 1) x1 = g_sw - 1;
    int y1 = y0 + 1; if (y1 > g_sh - 1) y1 = g_sh - 1;
    float tx = fx - (float)x0, ty = fy - (float)y0;

    uint32_t c00 = g_live[(size_t)y0 * g_sw + x0];
    uint32_t c10 = g_live[(size_t)y0 * g_sw + x1];
    uint32_t c01 = g_live[(size_t)y1 * g_sw + x0];
    uint32_t c11 = g_live[(size_t)y1 * g_sw + x1];

    float w00 = (1.0f - tx) * (1.0f - ty), w10 = tx * (1.0f - ty);
    float w01 = (1.0f - tx) * ty,             w11 = tx * ty;

    int r = (int)((float)(c00 & 0xFFu) * w00 + (float)(c10 & 0xFFu) * w10 +
                  (float)(c01 & 0xFFu) * w01 + (float)(c11 & 0xFFu) * w11 + 0.5f);
    int g = (int)((float)((c00 >> 8) & 0xFFu) * w00 + (float)((c10 >> 8) & 0xFFu) * w10 +
                  (float)((c01 >> 8) & 0xFFu) * w01 + (float)((c11 >> 8) & 0xFFu) * w11 + 0.5f);
    int b = (int)((float)((c00 >> 16) & 0xFFu) * w00 + (float)((c10 >> 16) & 0xFFu) * w10 +
                  (float)((c01 >> 16) & 0xFFu) * w01 + (float)((c11 >> 16) & 0xFFu) * w11 + 0.5f);
    return pack(clamp255(r), clamp255(g), clamp255(b));
}
