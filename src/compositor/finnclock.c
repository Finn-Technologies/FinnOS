/* finnclock — the FinnOS Notification Center & clock page (Figma 13:31, 13:129). */
#include "finnclock.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnnotify.h"
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/*
 * Metrics authored for the 1080p canvas, scaled from Figma 13:31 & 13:129
 * (1280x832 reference units * 1.298077):
 *   panel 260x260 r=15 (338x338 r=19.5)
 *   header at x=14, y=14: time 18px (23.4) in FINN_INK, date 12px (15.6) at
 *     x=68 on the same baseline ("Weekday, " in FINN_INK, "18 October" in FINN_DIM)
 *   empty state: centered "No notifications" 10px (13.0) in FINN_DIM
 *   items state: up to 4 cards at x=14, y=45, 232x36 r=12 (301.2x46.7 r=15.6),
 *     gap 8 (10.4), each with a 24x24 r=6 (31.2x31.2 r=7.8) #006FFF badge at
 *     inset 6 (7.8) carrying the white FinnOS mark (14px -> 18.0), title 10px
 *     (13.0) in FINN_INK, body 8px (10.4) in FINN_DIM
 *   bottom-center "Clear all" pill at y=223, 55x23 r=100 (71.4x29.9) frosted 0.50
 */
#define PAGE_W        338.0f   /* reference 260 */
#define PAGE_H        338.0f   /* reference 260 */
#define PAGE_PAD       18.2f   /* reference 14 */
#define PAGE_R        (8.0f * 1.298077f)   /* radius 8 in shell family */
#define TILE_R        (8.0f * 1.298077f)   /* radius 8 for cards */

#define HEAD_BASELINE  41.5f   /* reference 32 (14 + 18) */
#define DATE_X         88.3f   /* reference 68 */

#define NOTE_Y0        58.4f   /* reference 45 */
#define NOTE_H         46.7f   /* reference 36 */
#define NOTE_GAP       10.4f   /* reference 8 */
#define NOTE_SLOTS      4

#define BADGE_INSET     7.8f   /* reference 6 */
#define BADGE_SIZE     31.2f   /* reference 24 */
#define BADGE_R       (4.0f * 1.298077f)  /* active well radius 4 */
#define BADGE_GLYPH    18.0f   /* reference 14 */
#define NOTE_TEXT_X    46.7f   /* reference 36 */

#define CLEAR_W        71.4f   /* reference 55 */
#define CLEAR_H        29.9f   /* reference 23 */
#define CLEAR_Y       289.5f   /* reference 223 */

#define ANCHOR_GAP     19.5f   /* reference 15 */

/* ---------------- assets + fonts + layer ---------------- */
static fsvg g_ic_start;
static int  g_has_icon = 0;

static finn_font g_f_time, g_f_date, g_f_label, g_f_sub;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];

static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx0 = 0, g_ly0 = 0;
static int g_open = 0;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

static finn_anim g_a_open;
static finn_anim g_a_hover;
static float     g_hover = 0.0f;
static int       g_hover_on = 0;

static finn_anim g_a_tick;
static float     g_tick = 1.0f;
static int       g_tick_min = -1;

static finn_anim g_a_note;
static float     g_note = 1.0f;
static unsigned  g_note_rev = 0;

typedef struct { int x, y, w, h; } clk_rect;
static clk_rect g_r_panel, g_r_clear;

static int g_ax = 0, g_ay = 0;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_time);
    finn_font_free(&g_f_date);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_time, p, font_px(s, 23.0f)) != 0) {
        fprintf(stderr, "finnclock: cannot load %s\n", p);
        return -1;
    }
    if (finn_font_load(&g_f_date, p, font_px(s, 15.0f)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, font_px(s, 13.0f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, font_px(s, 10.5f)) != 0) return -1;
    return 0;
}

int finnclock_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);
    if (icon_dir && *icon_dir) {
        char p[600];
        snprintf(p, sizeof p, "%s/boot-mark.svg", icon_dir);
        g_has_icon = (fsvg_load(p, &g_ic_start) == 0);
    }
    return 0;
}

void finnclock_free(void) {
    if (g_has_icon) {
        fsvg_free(&g_ic_start);
        g_has_icon = 0;
    }
    finn_font_free(&g_f_time);
    finn_font_free(&g_f_date);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_built = 0;
    g_font_scale = -1.0f;
}

/* ---------------- open / close ---------------- */

void finnclock_set_open(int open) {
    open = open ? 1 : 0;
    if (open == g_open) return;
    g_open = open;
    fprintf(stderr, "finnclock: clock page %s\n", open ? "open" : "closed");
    if (open)
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    else
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
}

int finnclock_is_open(void) { return g_open; }
int finnclock_visible(void) { return finn_anim_v(&g_a_open) > 0.004f; }
float finnclock_openness(void) { return finn_anim_v(&g_a_open); }

int finnclock_settled(void) {
    return !finn_anim_running(&g_a_open) && finn_anim_v(&g_a_open) >= 0.999f;
}

int finnclock_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_hover, dt);
    ch |= finn_anim_tick(&g_a_tick, dt);
    ch |= finn_anim_tick(&g_a_note, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    float tk = finn_anim_v(&g_a_tick);
    if (tk != g_tick) { g_tick = tk; ch = 1; }
    float np = finn_anim_v(&g_a_note);
    if (np != g_note) { g_note = np; ch = 1; }
    return ch;
}

int finnclock_running(void) {
    return finn_anim_running(&g_a_open) || finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_tick) || finn_anim_running(&g_a_note);
}

void finnclock_hover_at(int px, int py) {
    int on = 0;
    if (g_open && g_layer && finn_anim_v(&g_a_open) >= 0.90f &&
        g_r_clear.w > 0) {
        int x = px - g_lx0, y = py - g_ly0;
        on = (x >= g_r_clear.x && y >= g_r_clear.y &&
              x < g_r_clear.x + g_r_clear.w &&
              y < g_r_clear.y + g_r_clear.h);
    }
    if (on == g_hover_on) return;
    g_hover_on = on;
    if (on)
        finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_hover, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

/* ---------------- content ---------------- */

static void now_tm(struct tm *tm) {
    time_t t = time(NULL);
    localtime_r(&t, tm);
}

static void fmt_time(char *out, size_t cap) {
    struct tm tm; now_tm(&tm);
    strftime(out, cap, "%H:%M", &tm);
}

static void fmt_weekday(char *out, size_t cap) {
    struct tm tm; now_tm(&tm);
    strftime(out, cap, "%A, ", &tm);
}

static void fmt_daymonth(char *out, size_t cap) {
    struct tm tm; now_tm(&tm);
    strftime(out, cap, "%-d %B", &tm);
}

static void fit_str(const finn_font *f, const char *s, float max_w,
                    char *out, size_t cap) {
    snprintf(out, cap, "%s", s ? s : "");
    if (!f || !*out || finn_text_width(f, out) <= max_w) return;

    size_t n = strlen(out);
    while (n > 0) {
        n--;
        snprintf(out, cap, "%.*s...", (int)n, s);
        if (finn_text_width(f, out) <= max_w) return;
    }
    out[0] = 0;
}

static void tile(uint32_t *L, int lw, int lh, float s, clk_rect r) {
    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                     (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                     s * TILE_R, FINN_GLASS, FINN_GLASS_TILE);
}

/* ---------------- layout + render ---------------- */

static uint32_t build_key(float s, int pw, int ph,
                          int dx, int dy, int dw, int dh,
                          const finn_state *st) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(pw); MIX(ph);
    MIX(g_lx0); MIX(g_ly0);
    MIX(dx); MIX(dy); MIX(dw); MIX(dh);
    MIX(st->dark + 2);
    MIX((int)finn_notify_rev());
    MIX((int)(g_hover * 64.0f));
    MIX((int)(g_tick * 64.0f));
    MIX((int)(g_note * 64.0f));
    time_t t = time(NULL);
    struct tm tm; localtime_r(&t, &tm);
    MIX(tm.tm_min); MIX(tm.tm_hour); MIX(tm.tm_mday); MIX(tm.tm_mon); MIX(tm.tm_year);
    #undef MIX
    return h ? h : 1u;
}

void finnclock_render(int screen_w, int screen_h, float scale,
                      int dock_x, int dock_y, int dock_w, int dock_h,
                      const finn_state *st) {
    float s = scale;
    if (g_font_scale != s) {
        if (load_fonts(s) != 0) return;
        g_font_scale = s;
    }

    int pw = rs(PAGE_W, s), ph = rs(PAGE_H, s);

    /*
     * Notification Center anchors to the clock pill: its right edge meets
     * the clock's right edge, growing out of the clock corner.
     */
    int kx = 0, ky = 0, kw = 0, kh = 0;
    finndock_clock(&kx, &ky, &kw, &kh);
    if (kw <= 0 || kh <= 0) {
        int tx = 0, ty = 0, tw = 0, th = 0;
        finndock_tray(&tx, &ty, &tw, &th);
        kx = tx; ky = ty; kw = tw; kh = th;
    }

    int px0 = (kx + kw) - pw;
    int py0 = ky - rs(ANCHOR_GAP, s) - ph;

    int margin = rs(12, s);
    if (px0 < margin) px0 = margin;
    if (py0 < margin) py0 = margin;
    if (px0 + pw > screen_w - margin) px0 = screen_w - margin - pw;
    if (py0 + ph > screen_h - margin) py0 = screen_h - margin - ph;

    g_lx0 = px0;
    g_ly0 = py0;
    g_ax = kx + kw / 2;
    g_ay = ky;

    uint32_t key = build_key(s, pw, ph, dock_x, dock_y, dock_w, dock_h, st);
    if (g_built && key == g_build_key) return;
    g_build_key = key;
    g_built = 1;
    g_rev++;

    if (pw != g_lw || ph != g_lh) {
        free(g_layer);
        g_layer = malloc((size_t)pw * ph * 4);
        g_lw = pw; g_lh = ph;
    }
    if (!g_layer) return;
    memset(g_layer, 0, (size_t)pw * ph * 4);

    uint32_t *L = g_layer;
    int lw = pw, lh = ph;

    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0, 0.0f, 0.0f,
                     (float)pw, (float)ph, s * PAGE_R, FINN_GLASS, FINN_GLASS_PANEL);

    int x0 = rs(PAGE_PAD, s);
    int full = pw - 2 * x0;

    g_r_panel.x = 0; g_r_panel.y = 0; g_r_panel.w = pw; g_r_panel.h = ph;
    g_r_clear.w = 0;

    /* ---- Header: "15:34" (18px FINN_INK) + "Sunday, " (12px FINN_INK) + "18 October" (12px FINN_DIM) ---- */
    char tbuf[16], wday[32], dmon[32];
    fmt_time(tbuf, sizeof tbuf);
    fmt_weekday(wday, sizeof wday);
    fmt_daymonth(dmon, sizeof dmon);

    int minute = (int)(time(NULL) / 60);
    if (minute != g_tick_min) {
        g_tick_min = minute;
        if (g_open) {
            finn_anim_jump(&g_a_tick, 0.0f);
            finn_anim_to(&g_a_tick, 1.0f, FINN_MOTION_CLOCK_TICK, 0.0f,
                         FINN_EASE_LINEAR);
        }
    }
    float dip = 1.0f - 0.55f * sinf(3.14159265f * g_tick);
    uint32_t time_ink = (FINN_INK & 0x00FFFFFFu) |
                        ((unsigned)(dip * 255.0f + 0.5f) << 24);

    float hbase = s * HEAD_BASELINE;
    finn_text_draw(&g_f_time, L, lw, lh, (float)x0, hbase, tbuf, time_ink);

    float dx = s * DATE_X;
    float tw_time = finn_text_width(&g_f_time, tbuf) + s * 13.0f;
    if ( (float)x0 + tw_time > dx ) dx = (float)x0 + tw_time;

    finn_text_draw(&g_f_date, L, lw, lh, dx, hbase, wday, FINN_INK);
    float wday_w = finn_text_width(&g_f_date, wday);
    finn_text_draw(&g_f_date, L, lw, lh, dx + wday_w, hbase, dmon, FINN_DIM);

    int notes = finn_notify_count();

    if (notes == 0) {
        /* Figma 13:31: centered "No notifications" (10px FINN_DIM) */
        const char *empty_s = "No notifications";
        float cap = finn_text_cap_height(&g_f_label);
        float ew = finn_text_width(&g_f_label, empty_s);
        finn_text_draw(&g_f_label, L, lw, lh,
                       ((float)pw - ew) * 0.5f,
                       (float)ph * 0.5f + cap * 0.5f,
                       empty_s, FINN_DIM);
    } else {
        /* Figma 13:129: notification cards + bottom-center "Clear all" pill */
        unsigned rev = finn_notify_rev();
        if (rev != g_note_rev) {
            g_note_rev = rev;
            if (g_open) {
                finn_anim_jump(&g_a_note, 0.0f);
                finn_anim_to(&g_a_note, 1.0f, FINN_MOTION_NOTE, 0.0f,
                             FINN_EASE_OUT);
            }
        }

        int y0 = rs(NOTE_Y0, s);
        int nh = rs(NOTE_H, s);
        int ng = rs(NOTE_GAP, s);
        int shown = notes < NOTE_SLOTS ? notes : NOTE_SLOTS;

        for (int i = 0; i < shown; i++) {
            const finn_note *n = finn_notify_at(i);
            if (!n) break;

            clk_rect rn = { x0, y0 + i * (nh + ng), full, nh };

            float p = (i == 0) ? g_note : 1.0f;
            if (p < 0.999f) {
                float mix = FINN_GLASS_PANEL + (FINN_GLASS_TILE - FINN_GLASS_PANEL) * p;
                finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                                 (float)rn.x, (float)rn.y, (float)rn.w, (float)rn.h,
                                 s * TILE_R, FINN_GLASS, mix);
            } else {
                tile(L, lw, lh, s, rn);
            }

            unsigned al = (unsigned)(p * 255.0f + 0.5f);
            uint32_t ink = (FINN_INK & 0x00FFFFFFu) | (al << 24);
            uint32_t dim = (FINN_DIM & 0x00FFFFFFu) | (((al * ((FINN_DIM >> 24) & 0xFFu)) / 255u) << 24);
            uint32_t badge_col = (FINN_ON & 0x00FFFFFFu) | (al << 24);
            uint32_t knob_col  = (FINN_KNOB & 0x00FFFFFFu) | (al << 24);

            /* Left #006FFF app badge (24x24 r=6) with white FinnOS mark */
            float bx = (float)rn.x + s * BADGE_INSET;
            float by = (float)rn.y + s * BADGE_INSET;
            float bs = s * BADGE_SIZE;
            finn_fill_rrect(L, lw, lh, bx, by, bs, bs, s * BADGE_R, badge_col);
            if (g_has_icon) {
                finn_icon_blit_ink(&g_ic_start, L, lw, lh,
                                   (int)lroundf(bx + bs * 0.5f),
                                   (int)lroundf(by + bs * 0.5f),
                                   rs(BADGE_GLYPH, s), knob_col, knob_col, 0);
            }

            float tx_off = s * NOTE_TEXT_X;
            float avail = (float)rn.w - tx_off - s * 10.0f;
            char title[80], body[200];
            fit_str(&g_f_label, n->title, avail, title, sizeof title);
            fit_str(&g_f_sub, n->body, avail, body, sizeof body);

            finn_text_draw(&g_f_label, L, lw, lh,
                           (float)rn.x + tx_off, (float)(rn.y + rs(21, s)),
                           title, ink);
            finn_text_draw(&g_f_sub, L, lw, lh,
                           (float)rn.x + tx_off, (float)(rn.y + rs(35, s)),
                           body, dim);
        }

        /* Bottom-center "Clear all" pill (Figma 13:129: 55x23 r=100 at y=223) */
        int cw = rs(CLEAR_W, s);
        int ch = rs(CLEAR_H, s);
        int cx = (pw - cw) / 2;
        int cy = rs(CLEAR_Y, s);
        g_r_clear.x = cx;
        g_r_clear.y = cy;
        g_r_clear.w = cw;
        g_r_clear.h = ch;

        float crad = (float)ch * 0.5f;
        finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                         (float)cx, (float)cy, (float)cw, (float)ch,
                         crad, FINN_GLASS, FINN_GLASS_TILE);

        if (g_hover > 0.004f) {
            finn_fill_rrect(L, lw, lh, (float)cx, (float)cy,
                            (float)cw, (float)ch, crad,
                            (FINN_INK & 0x00FFFFFFu) |
                            ((unsigned)(FINN_MOTION_HOVER_WASH * g_hover * 255.0f + 0.5f) << 24));
        }

        const char *clr = "Clear all";
        float cwid = finn_text_width(&g_f_label, clr);
        float cap = finn_text_cap_height(&g_f_label);
        finn_text_draw(&g_f_label, L, lw, lh,
                       (float)cx + ((float)cw - cwid) * 0.5f,
                       (float)cy + (float)ch * 0.5f + cap * 0.5f,
                       clr, FINN_INK);
    }
}

/* ---------------- compositing ---------------- */

void finnclock_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnclock_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnclock_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    float a = v * FINN_MOTION_FADE;
    if (a > 1.0f) a = 1.0f;
    float k = FINN_MOTION_OPEN_SCALE + (1.0f - FINN_MOTION_OPEN_SCALE) * v;

    float ax = (float)g_ax, ay = (float)g_ay;
    if (x) *x = ax + ((float)g_lx0 - ax) * k;
    if (y) *y = ay + ((float)g_ly0 - ay) * k;
    if (w) *w = (float)g_lw * k;
    if (h) *h = (float)g_lh * k;
    if (alpha) *alpha = a;
}

void finnclock_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_layer) return;
    float x, y, w, h, alpha;
    finnclock_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_scaled(dst, dw, dh, g_layer, g_lw, g_lh, x, y, w, h, alpha);
}

const uint32_t *finnclock_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx0;
    if (y) *y = g_ly0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_rev;
    return finnclock_visible() ? g_layer : NULL;
}

/* ---------------- input ---------------- */

static int in_rect(const clk_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int finnclock_hit(int px, int py) {
    if (!g_open || !g_layer || !finnclock_visible()) return FINNCLOCK_NONE;
    if (finn_anim_v(&g_a_open) < 0.90f) return FINNCLOCK_NONE;

    int x = px - g_lx0, y = py - g_ly0;
    if (x < 0 || y < 0 || x >= g_lw || y >= g_lh) return FINNCLOCK_NONE;

    if (g_r_clear.w > 0 && in_rect(&g_r_clear, x, y)) return FINNCLOCK_CLEAR;
    return in_rect(&g_r_panel, x, y) ? FINNCLOCK_PANEL : FINNCLOCK_NONE;
}
