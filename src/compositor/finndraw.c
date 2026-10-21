/* finndraw — the shared drawing primitives of the FinnOS shell. */
#include "finndraw.h"
#include "finnblur.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline float clampf255(float v) {
    return v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
}

/* ---------------- palette ---------------- */

uint32_t FINN_BONE, FINN_TILE, FINN_TILE_ON, FINN_HAIR;
uint32_t FINN_INK, FINN_MUTE, FINN_DIM, FINN_ACCENT, FINN_TRACK, FINN_KNOB;
uint32_t FINN_ON, FINN_FILL, FINN_GLASS;

static int g_dark = 0;

void finn_theme_set(int dark) {
    dark = dark ? 1 : 0;
    if (dark == g_dark && FINN_BONE) return;
    g_dark = dark;

    if (!dark) {
        FINN_BONE    = 0xFFF6F1E7u;   /* #F6F1E7 */
        FINN_TILE    = 0xFFE7E1D5u;
        FINN_TILE_ON = 0xFFD3E6FFu;
        FINN_HAIR    = 0x22FFFFFFu;
        FINN_INK     = 0xFF1A1D24u;   /* #1A1D24 */
        FINN_MUTE    = 0xFF969696u;   /* #969696 */
        FINN_DIM     = 0xFF7B7776u;   /* #7B7776, measured off the reference's detail line */
        FINN_ACCENT  = 0xFF7AB8FFu;   /* #7AB8FF */
        FINN_TRACK   = 0xFFCFC7B8u;
        FINN_KNOB    = 0xFFFFFFFFu;
        FINN_ON      = 0xFF006FFFu;   /* #006FFF */
        FINN_FILL    = 0xFF998481u;   /* #998481, measured off the reference */
        FINN_GLASS   = 0xFFFFFFFFu;   /* see FINN_GLASS in the header */
    } else {
        FINN_BONE    = 0xFF23262Eu;
        FINN_TILE    = 0xFF2F333Cu;
        FINN_TILE_ON = 0xFF2B4A78u;
        FINN_HAIR    = 0x1AFFFFFFu;
        FINN_INK     = 0xFFF4F0E8u;
        FINN_MUTE    = 0xFF8B9099u;
        FINN_DIM     = 0xFFA9A59Eu;
        FINN_ACCENT  = 0xFF7AB8FFu;
        FINN_TRACK   = 0xFF3B4048u;
        FINN_KNOB    = 0xFFEDE9E1u;
        FINN_ON      = 0xFF0A84FFu;   /* #0A84FF */
        FINN_FILL    = 0xFFEDE9E1u;   /* #EDE9E1 */
        FINN_GLASS   = 0xFF23262Eu;   /* the dark bone: glass goes deep, not bright */
    }
}

int finn_theme_dark(void) { return g_dark; }

uint32_t finn_over(uint32_t dst, uint32_t src, float a) {
    if (a <= 0.0f) return dst;
    float sa = a * (float)((src >> 24) & 0xFF) / 255.0f;
    if (sa <= 0.0f) return dst;
    float da = (float)((dst >> 24) & 0xFF) / 255.0f;
    float ra = sa + da * (1.0f - sa);
    if (ra <= 0.0001f) return 0;
    float k = da * (1.0f - sa);
    float inv = 1.0f / ra;

    float d0 = (float)(dst & 0xFF), d1 = (float)((dst >> 8) & 0xFF), d2 = (float)((dst >> 16) & 0xFF);
    float s0 = (float)(src & 0xFF), s1 = (float)((src >> 8) & 0xFF), s2 = (float)((src >> 16) & 0xFF);

    uint32_t r0 = (uint32_t)(clampf255((s0 * sa + d0 * k) * inv) + 0.5f);
    uint32_t r1 = (uint32_t)(clampf255((s1 * sa + d1 * k) * inv) + 0.5f);
    uint32_t r2 = (uint32_t)(clampf255((s2 * sa + d2 * k) * inv) + 0.5f);
    uint32_t ra8 = (uint32_t)(ra * 255.0f + 0.5f);
    return (ra8 << 24) | (r2 << 16) | (r1 << 8) | r0;
}

static inline float sdf_rrect(float x, float y, float cx, float cy,
                              float hw, float hh, float r) {
    float dx = fabsf(x - cx) - (hw - r), dy = fabsf(y - cy) - (hh - r);
    float ox = fmaxf(dx, 0.0f), oy = fmaxf(dy, 0.0f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(dx, dy), 0.0f) - r;
}

/*
 * The anti-aliased coverage of one pixel centre against a rounded rect. Both
 * the plain fill and the frosted surface resolve their edge through this one
 * function, so the two can never drift apart at the corners.
 */
static inline float rrect_cov(float px, float py, float cx, float cy,
                              float hw, float hh, float r) {
    float a = 0.5f - sdf_rrect(px, py, cx, cy, hw, hh, r);
    if (a <= 0.0f) return 0.0f;
    return a > 1.0f ? 1.0f : a;
}

void finn_fill_rrect(uint32_t *b, int w, int h, float x, float y,
                     float rw, float rh, float r, uint32_t c) {
    if (rw <= 0.0f || rh <= 0.0f) return;
    if (r > rw * 0.5f) r = rw * 0.5f;
    if (r > rh * 0.5f) r = rh * 0.5f;

    int x0 = (int)floorf(x - 1), x1 = (int)ceilf(x + rw + 1);
    int y0 = (int)floorf(y - 1), y1 = (int)ceilf(y + rh + 1);
    float cx = x + rw * 0.5f, cy = y + rh * 0.5f, hw = rw * 0.5f, hh = rh * 0.5f;
    for (int yy = y0; yy <= y1; yy++) {
        if (yy < 0 || yy >= h) continue;
        for (int xx = x0; xx <= x1; xx++) {
            if (xx < 0 || xx >= w) continue;
            float a = rrect_cov(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, r);
            if (a <= 0.0f) continue;
            b[(size_t)yy * w + xx] = finn_over(b[(size_t)yy * w + xx], c, a);
        }
    }
}

void finn_frost_rrect(uint32_t *b, int w, int h, int ox, int oy,
                      float x, float y, float rw, float rh, float r,
                      uint32_t tint, float alpha) {
    if (rw <= 0.0f || rh <= 0.0f) return;

    /*
     * The glass has to have a backdrop. If the blur could not be built the
     * shell should still draw something honest, so fall back to the plain
     * surface at full strength rather than a hole where the dock should be.
     */
    if (!finnblur_ready()) {
        finn_fill_rrect(b, w, h, x, y, rw, rh, r, tint);
        return;
    }

    if (r > rw * 0.5f) r = rw * 0.5f;
    if (r > rh * 0.5f) r = rh * 0.5f;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    int x0 = (int)floorf(x - 1), x1 = (int)ceilf(x + rw + 1);
    int y0 = (int)floorf(y - 1), y1 = (int)ceilf(y + rh + 1);
    float cx = x + rw * 0.5f, cy = y + rh * 0.5f, hw = rw * 0.5f, hh = rh * 0.5f;

    /*
     * The pixels whose centres are at least half a pixel inside every edge
     * have coverage exactly 1, so their result is the tint mixed with the
     * backdrop and stamped opaque — no distance field, no source-over. On a
     * maximized window that is nearly four million pixels and the difference
     * between a smooth snap and a dropped frame.
     *
     * The inner rect is conservative: it starts a full radius inside the
     * straight edges and therefore stays clear of the corner arcs.
     */
    float m = r > 0.5f ? r : 0.5f;
    int ix0 = (int)ceilf(x + m);
    int ix1 = (int)floorf(x + rw - m);
    int iy0 = (int)ceilf(y + m);
    int iy1 = (int)floorf(y + rh - m);
    if (ix1 > x1) ix1 = x1;
    if (iy1 > y1) iy1 = y1;

    float t0 = (float)(tint & 0xFFu);
    float t1 = (float)((tint >> 8) & 0xFFu);
    float t2 = (float)((tint >> 16) & 0xFFu);
    float ia = 1.0f - alpha;

    for (int yy = y0; yy <= y1; yy++) {
        if (yy < 0 || yy >= h) continue;
        int inner_row = yy >= iy0 && yy < iy1;
        for (int xx = x0; xx <= x1; xx++) {
            if (xx < 0 || xx >= w) continue;

            /*
             * The tint is laid over whatever the surface already carries.
             * Where the layer is still transparent that base is the frosted
             * backdrop — the wallpaper underneath, which is what makes the
             * surface read as glass rather than as a sticker. Where a surface
             * has already been drawn it is that surface, which is what lets
             * the tray's well be the same glass laid down a second time and
             * compound to 0.70 + 0.30·0.53 ≈ 0.86 instead of replacing the
             * pill at 0.53. (ox+xx, oy+yy) is the screen pixel this layer
             * pixel lands on, so the glass is sampled from the wallpaper
             * actually underneath the rect.
             */
            size_t idx = (size_t)yy * w + xx;
            uint32_t d = b[idx];
            uint32_t base = (d >> 24) ? d : finnblur_at(ox + xx, oy + yy);

            /* Per-channel mix toward the tint. The mixed colour is opaque:
             * the tint's own alpha is not part of the mix, because the
             * frost's only transparency is the edge coverage. */
            float m0 = (float)(base & 0xFFu) * ia + t0 * alpha;
            float m1 = (float)((base >> 8) & 0xFFu) * ia + t1 * alpha;
            float m2 = (float)((base >> 16) & 0xFFu) * ia + t2 * alpha;
            uint32_t c = 0xFF000000u | ((uint32_t)(clampf255(m2) + 0.5f) << 16) |
                         ((uint32_t)(clampf255(m1) + 0.5f) << 8) |
                         (uint32_t)(clampf255(m0) + 0.5f);

            if (inner_row && xx >= ix0 && xx < ix1) {
                b[idx] = c;
                continue;
            }

            float cov = rrect_cov(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, r);
            if (cov <= 0.0f) continue;
            b[idx] = finn_over(d, c, cov);
        }
    }
}

void finn_blit(uint32_t *dst, int dw, int dh, const uint32_t *src, int sw, int sh,
               int dx, int dy) {
    for (int y = 0; y < sh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= dh) continue;
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= dw) continue;
            uint32_t c = src[(size_t)y * sw + x];
            if (!(c >> 24)) continue;
            dst[(size_t)ty * dw + tx] = finn_over(dst[(size_t)ty * dw + tx], c, 1.0f);
        }
    }
}

uint32_t finn_tint_pal(uint32_t c, void *ud) {
    finn_palette *p = ud;
    uint32_t rgb = c & 0x00FFFFFFu;
    uint32_t col = (rgb == 0x00969696u) ? p->inactive : p->active;
    return col | (c & 0xFF000000u);
}

/* ---------------- scaled blit ---------------- */

static float g_blit_dust = 0.0f;

void finn_blit_scaled(uint32_t *dst, int dw, int dh,
                      const uint32_t *src, int sw, int sh,
                      float x, float y, float rw, float rh, float alpha) {
    if (!dst || !src || sw <= 0 || sh <= 0) return;
    if (rw <= 0.0f || rh <= 0.0f || alpha <= 0.0f) return;

    int x0 = (int)floorf(x) - 1, x1 = (int)ceilf(x + rw) + 1;
    int y0 = (int)floorf(y) - 1, y1 = (int)ceilf(y + rh) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dw) x1 = dw;
    if (y1 > dh) y1 = dh;

    float sx = (float)sw / rw, sy = (float)sh / rh;
    float lastu = (float)(sw - 1), lastv = (float)(sh - 1);

    for (int py = y0; py < y1; py++) {
        /* sampling at pixel centres makes the identity transform exact */
        float v = ((float)py + 0.5f - y) * sy - 0.5f;
        /* the dissolve drifts toward the top-right: sample down-left */
        if (g_blit_dust > 0.001f) v += g_blit_dust * 0.05f * sy;
        if (v < 0.0f) v = 0.0f;
        if (v > lastv) v = lastv;
        int v0 = (int)v, v1 = v0 + 1;
        float fy = v - (float)v0;
        if (v1 > sh - 1) v1 = sh - 1;

        const uint32_t *r0 = src + (size_t)v0 * sw;
        const uint32_t *r1 = src + (size_t)v1 * sw;
        uint32_t *drow = dst + (size_t)py * dw;

        for (int px = x0; px < x1; px++) {
            float u = ((float)px + 0.5f - x) * sx - 0.5f;
            if (g_blit_dust > 0.001f) u -= g_blit_dust * 0.06f * sx;
            if (u < 0.0f) u = 0.0f;
            if (u > lastu) u = lastu;
            int u0 = (int)u, u1 = u0 + 1;
            float fx = u - (float)u0;
            if (u1 > sw - 1) u1 = sw - 1;

            uint32_t t00 = r0[u0], t10 = r0[u1], t01 = r1[u0], t11 = r1[u1];
            float w00 = (1.0f - fx) * (1.0f - fy), w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy,             w11 = fx * fy;

            /* weight each tap by its own alpha, then divide the colour back
             * out, so transparent black neighbours cannot darken the edge */
            float a00 = (float)((t00 >> 24) & 0xFFu), a10 = (float)((t10 >> 24) & 0xFFu);
            float a01 = (float)((t01 >> 24) & 0xFFu), a11 = (float)((t11 >> 24) & 0xFFu);
            float A = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
            if (A <= 0.5f) continue;

            float dust_edge = 1.0f;
            if (g_blit_dust > 0.001f) {
                unsigned hh = (unsigned)px * 374761393u + (unsigned)py * 668265263u;
                hh = (hh ^ (hh >> 13)) * 1274126177u;
                float n = (float)((hh >> 8) & 0xFFFFu) / 65535.0f;
                /* the top-right corner is the far end of the wind: it leaves
                 * first, and the cut sweeps down-left behind it */
                float nx = ((float)px + 0.5f - x) / rw;
                float ny = ((float)py + 0.5f - y) / rh;
                if (nx < 0.0f) nx = 0.0f; else if (nx > 1.0f) nx = 1.0f;
                if (ny < 0.0f) ny = 0.0f; else if (ny > 1.0f) ny = 1.0f;
                float sweep = nx * 0.72f + (1.0f - ny) * 0.28f;
                float cut = g_blit_dust * (1.35f - 0.72f * sweep);
                if (n < cut) continue;
                dust_edge = (n - cut) * 8.0f;
                if (dust_edge > 1.0f) dust_edge = 1.0f;
            }

            float R = (float)(t00 & 0xFFu) * a00 * w00 + (float)(t10 & 0xFFu) * a10 * w10
                    + (float)(t01 & 0xFFu) * a01 * w01 + (float)(t11 & 0xFFu) * a11 * w11;
            float G = (float)((t00 >> 8) & 0xFFu) * a00 * w00 + (float)((t10 >> 8) & 0xFFu) * a10 * w10
                    + (float)((t01 >> 8) & 0xFFu) * a01 * w01 + (float)((t11 >> 8) & 0xFFu) * a11 * w11;
            float B = (float)((t00 >> 16) & 0xFFu) * a00 * w00 + (float)((t10 >> 16) & 0xFFu) * a10 * w10
                    + (float)((t01 >> 16) & 0xFFu) * a01 * w01 + (float)((t11 >> 16) & 0xFFu) * a11 * w11;

            float inv = 1.0f / A;
            R *= inv; G *= inv; B *= inv;
            if (dust_edge < 1.0f) {
                float warm = (1.0f - dust_edge) * 0.85f;
                R += (222.0f - R) * warm;
                G += (160.0f - G) * warm;
                B += ( 70.0f - B) * warm;
            }
            float sa = A * (1.0f / 255.0f) * alpha;

            /* the scanout is always opaque, so over-compositing collapses to
             * a lerp and does not need the general (and slower) finn_over */
            uint32_t dp = drow[px];
            float ia = 1.0f - sa;
            uint32_t r8 = (uint32_t)(clampf255(R * sa + (float)(dp & 0xFFu) * ia) + 0.5f);
            uint32_t g8 = (uint32_t)(clampf255(G * sa + (float)((dp >> 8) & 0xFFu) * ia) + 0.5f);
            uint32_t b8 = (uint32_t)(clampf255(B * sa + (float)((dp >> 16) & 0xFFu) * ia) + 0.5f);
            drow[px] = 0xFF000000u | (b8 << 16) | (g8 << 8) | r8;
        }
    }
}

void finn_blit_dissolve(uint32_t *dst, int dw, int dh,
                        const uint32_t *src, int sw, int sh,
                        float x, float y, float rw, float rh,
                        float alpha, float dust) {
    g_blit_dust = dust;
    finn_blit_scaled(dst, dw, dh, src, sw, sh, x, y, rw, rh, alpha);
    g_blit_dust = 0.0f;
}

void finn_frame_blend(uint32_t *dst, const uint32_t *a, const uint32_t *b,
                      size_t n, uint32_t w256) {
    if (w256 == 0)   { memcpy(dst, a, n * 4); return; }
    if (w256 >= 256) { memcpy(dst, b, n * 4); return; }

    uint32_t iw = 256u - w256;
    for (size_t i = 0; i < n; i++) {
        uint32_t x = a[i], y = b[i];
        uint32_t rb = ((((x & 0x00FF00FFu) * iw) + ((y & 0x00FF00FFu) * w256)) >> 8) & 0x00FF00FFu;
        uint32_t g  = ((((x & 0x0000FF00u) * iw) + ((y & 0x0000FF00u) * w256)) >> 8) & 0x0000FF00u;
        dst[i] = 0xFF000000u | rb | g;
    }
}

void finn_frame_dim(uint32_t *dst, size_t n, float amount) {
    if (!dst || amount <= 0.0f) return;
    if (amount > 1.0f) amount = 1.0f;
    float k = 1.0f - amount;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = dst[i];
        uint32_t r = (uint32_t)((float)(c & 0xFFu) * k);
        uint32_t g = (uint32_t)((float)((c >> 8) & 0xFFu) * k);
        uint32_t b = (uint32_t)((float)((c >> 16) & 0xFFu) * k);
        dst[i] = (c & 0xFF000000u) | (b << 16) | (g << 8) | r;
    }
}

/* ---------------- icon raster cache ---------------- */

typedef struct {
    fsvg    *svg;
    int      px;
    uint32_t active, inactive, vkey;
    int      bytes;
    int      lru;
    uint32_t *buf;
} icon_entry;

#define ICON_CACHE 32
static icon_entry g_icons[ICON_CACHE];
static int g_icon_clock = 0;

const uint32_t *finn_icon_ref(fsvg *svg, int px, uint32_t active,
                              uint32_t inactive, uint32_t vkey) {
    if (!svg || px <= 0) return NULL;

    icon_entry *victim = NULL;
    int oldest = 0, have_free = 0;

    for (int i = 0; i < ICON_CACHE; i++) {
        icon_entry *e = &g_icons[i];
        if (e->buf && e->svg == svg && e->px == px && e->active == active &&
            e->inactive == inactive && e->vkey == vkey) {
            e->lru = ++g_icon_clock;
            return e->buf;
        }
        if (!e->buf) {
            if (!have_free) { victim = e; have_free = 1; }
        } else if (!victim || e->lru < oldest) {
            oldest = e->lru;
            victim = e;
        }
    }
    if (!victim) return NULL;

    size_t need = (size_t)px * (size_t)px * 4u;
    if (!victim->buf || (size_t)victim->bytes < need) {
        uint32_t *nb = realloc(victim->buf, need);
        if (!nb) return NULL;
        victim->buf = nb;
        victim->bytes = (int)need;
    }
    memset(victim->buf, 0, need);

    finn_palette p = { active, inactive };
    fsvg_raster(svg, victim->buf, px, px, finn_tint_pal, &p);

    victim->svg = svg;
    victim->px = px;
    victim->active = active;
    victim->inactive = inactive;
    victim->vkey = vkey;
    victim->lru = ++g_icon_clock;
    return victim->buf;
}

void finn_icon_blit(fsvg *svg, uint32_t *dst, int dw, int dh,
                    int x, int y, int px, uint32_t active, uint32_t inactive,
                    uint32_t vkey) {
    const uint32_t *t = finn_icon_ref(svg, px, active, inactive, vkey);
    if (t) finn_blit(dst, dw, dh, t, px, px, x, y);
}

void finn_icon_frame(const fsvg *svg, int ink, int cx, int cy,
                     int *px, int *ox, int *oy) {
    float x0, y0, x1, y1;
    fsvg_ink(svg, &x0, &y0, &x1, &y1);

    float vw = svg->vw > 0.0f ? svg->vw : 24.0f;
    float vh = svg->vh > 0.0f ? svg->vh : 24.0f;
    float iw = x1 - x0, ih = y1 - y0;
    float span = iw > ih ? iw : ih;

    if (ink < 1) ink = 1;

    /* the raster is the viewBox at whatever size makes the ink come out right */
    int p = span > 0.0f
          ? (int)lroundf((float)ink * (vw > vh ? vw : vh) / span)
          : ink;
    if (p < 1) p = 1;

    float kx = (float)p / vw, ky = (float)p / vh;
    *px = p;
    *ox = (int)lroundf((float)cx - (x0 + x1) * 0.5f * kx);
    *oy = (int)lroundf((float)cy - (y0 + y1) * 0.5f * ky);
}

void finn_icon_blit_ink(fsvg *svg, uint32_t *dst, int dw, int dh,
                        int cx, int cy, int ink, uint32_t active,
                        uint32_t inactive, uint32_t vkey) {
    int px, ox, oy;
    finn_icon_frame(svg, ink, cx, cy, &px, &ox, &oy);
    finn_icon_blit(svg, dst, dw, dh, ox, oy, px, active, inactive, vkey);
}

static uint32_t *g_scratch = NULL;
static int g_scratch_px = 0;

uint32_t *finn_icon_tile(fsvg *svg, int px, uint32_t active, uint32_t inactive,
                         uint32_t vkey) {
    const uint32_t *ref = finn_icon_ref(svg, px, active, inactive, vkey);
    if (!ref) return NULL;
    if (g_scratch_px < px) {
        free(g_scratch);
        g_scratch = malloc((size_t)px * px * 4);
        if (!g_scratch) { g_scratch_px = 0; return NULL; }
        g_scratch_px = px;
    }
    memcpy(g_scratch, ref, (size_t)px * px * 4);
    return g_scratch;
}

