/* finnblur — the frosted backdrop of the shell. */
#include "finnblur.h"

#include <stdlib.h>

/* Box radius, per separable pass. See finnblur.h for why three of these at
 * 1/8 scale read as a full-resolution frosted blur. */
#define BLUR_R 2

static uint32_t *g_small = NULL;
static int g_sw = 0, g_sh = 0;
static int g_ready = 0;

int finnblur_ready(void) { return g_ready; }

void finnblur_free(void) {
    free(g_small);
    g_small = NULL;
    g_sw = g_sh = 0;
    g_ready = 0;
}

static inline uint32_t pack(int r, int g, int b) {
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

/*
 * Horizontal box pass, edges clamped. Alpha is already 0xFF everywhere, so it
 * is re-stamped rather than averaged — carrying it through would only cost
 * time and let rounding nibble at it.
 */
static void box_h(const uint32_t *in, uint32_t *out, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint32_t *row = in + (size_t)y * w;
        uint32_t *o = out + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            int rs = 0, gs = 0, bs = 0, n = 0;
            for (int k = -BLUR_R; k <= BLUR_R; k++) {
                int xx = x + k;
                if (xx < 0) xx = 0;
                if (xx >= w) xx = w - 1;
                uint32_t c = row[xx];
                rs += (int)(c & 0xFFu);
                gs += (int)((c >> 8) & 0xFFu);
                bs += (int)((c >> 16) & 0xFFu);
                n++;
            }
            o[x] = pack(rs / n, gs / n, bs / n);
        }
    }
}

/* Vertical box pass, edges clamped. */
static void box_v(const uint32_t *in, uint32_t *out, int w, int h) {
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int rs = 0, gs = 0, bs = 0, n = 0;
            for (int k = -BLUR_R; k <= BLUR_R; k++) {
                int yy = y + k;
                if (yy < 0) yy = 0;
                if (yy >= h) yy = h - 1;
                uint32_t c = in[(size_t)yy * w + x];
                rs += (int)(c & 0xFFu);
                gs += (int)((c >> 8) & 0xFFu);
                bs += (int)((c >> 16) & 0xFFu);
                n++;
            }
            out[(size_t)y * w + x] = pack(rs / n, gs / n, bs / n);
        }
    }
}

void finnblur_build(const uint32_t *wall, int w, int h) {
    finnblur_free();
    if (!wall || w < FINNBLUR_SCALE || h < FINNBLUR_SCALE) return;

    int sw = (w + FINNBLUR_SCALE - 1) / FINNBLUR_SCALE;
    int sh = (h + FINNBLUR_SCALE - 1) / FINNBLUR_SCALE;
    uint32_t *small = malloc((size_t)sw * sh * 4);
    uint32_t *tmp   = malloc((size_t)sw * sh * 4);
    if (!small || !tmp) { free(small); free(tmp); return; }

    /* Box-average the wallpaper down to the small scale. The wallpaper is
     * opaque, so alpha is not carried: every sample is stamped 0xFF. The last
     * block on each edge is short when the size is not a multiple of the
     * factor, which is why the averages count their own taps. */
    for (int sy = 0; sy < sh; sy++) {
        int y0 = sy * FINNBLUR_SCALE;
        int y1 = y0 + FINNBLUR_SCALE; if (y1 > h) y1 = h;
        for (int sx = 0; sx < sw; sx++) {
            int x0 = sx * FINNBLUR_SCALE;
            int x1 = x0 + FINNBLUR_SCALE; if (x1 > w) x1 = w;
            int rs = 0, gs = 0, bs = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint32_t *row = wall + (size_t)yy * w;
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t c = row[xx];
                    rs += (int)(c & 0xFFu);
                    gs += (int)((c >> 8) & 0xFFu);
                    bs += (int)((c >> 16) & 0xFFu);
                    n++;
                }
            }
            small[(size_t)sy * sw + sx] = pack(rs / n, gs / n, bs / n);
        }
    }

    /* Three separable passes, horizontal then vertical each time. The scratch
     * buffer is what keeps a pass separable instead of an O(r) gather per
     * pixel per axis. */
    for (int p = 0; p < 3; p++) {
        box_h(small, tmp, sw, sh);
        box_v(tmp, small, sw, sh);
    }
    free(tmp);

    g_small = small;
    g_sw = sw;
    g_sh = sh;
    g_ready = 1;
}

uint32_t finnblur_at(int x, int y) {
    if (!g_ready || !g_small) return 0;

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

    uint32_t c00 = g_small[(size_t)y0 * g_sw + x0];
    uint32_t c10 = g_small[(size_t)y0 * g_sw + x1];
    uint32_t c01 = g_small[(size_t)y1 * g_sw + x0];
    uint32_t c11 = g_small[(size_t)y1 * g_sw + x1];

    float w00 = (1.0f - tx) * (1.0f - ty), w10 = tx * (1.0f - ty);
    float w01 = (1.0f - tx) * ty,             w11 = tx * ty;

    int r = (int)((float)(c00 & 0xFFu) * w00 + (float)(c10 & 0xFFu) * w10 +
                  (float)(c01 & 0xFFu) * w01 + (float)(c11 & 0xFFu) * w11 + 0.5f);
    int g = (int)((float)((c00 >> 8) & 0xFFu) * w00 + (float)((c10 >> 8) & 0xFFu) * w10 +
                  (float)((c01 >> 8) & 0xFFu) * w01 + (float)((c11 >> 8) & 0xFFu) * w11 + 0.5f);
    int b = (int)((float)((c00 >> 16) & 0xFFu) * w00 + (float)((c10 >> 16) & 0xFFu) * w10 +
                  (float)((c01 >> 16) & 0xFFu) * w01 + (float)((c11 >> 16) & 0xFFu) * w11 + 0.5f);
    return pack(r, g, b);
}
