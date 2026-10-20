/* finnclock — the FinnOS clock page: the time, the date, and what happened. */
#include "finnclock.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnnotify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/*
 * The same glass and the same tiles as the Control Center, on purpose: these
 * two pages are the same surface language, and a clock page that invented its
 * own weights or its own corner radius would read as a different OS.
 */
#define PAGE_GLASS       0.70f
#define PAGE_TILE_GLASS  0.50f

/*
 * Metrics authored for the 1080p canvas, in the Control Center's units.
 *
 * Its width is the Control Center's exactly, so opening one page after the
 * other does not resize the surface the eye is already used to. There is no
 * calendar here on purpose: a month grid is a thing to consult, and this page
 * is a thing to glance at. The date is in the header, where it belongs.
 */
#define PAGE_W       351.0f
#define PAGE_PAD      10.4f
#define PAGE_GAP      10.4f
#define PAGE_R        20.8f
#define TILE_R         9.7f

#define CLOCK_H       74.0f
#define HEAD_H        18.0f
#define NOTE_H        46.7f
#define NOTE_GAP      10.4f
#define NOTE_SLOTS     3

#define PAGE_H (PAGE_PAD * 2.0f + CLOCK_H + PAGE_GAP + HEAD_H + PAGE_GAP + \
                (float)NOTE_SLOTS * NOTE_H + \
                (float)(NOTE_SLOTS - 1) * NOTE_GAP)

#define ANCHOR_GAP    19.5f   /* panel to dock, the Control Center's own gap */
#define INNER         12.0f   /* text inset inside a tile                   */

/* motion: the Control Center's, so the two pages move as one family */
#define OPEN_DUR     0.28f
#define CLOSE_DUR    0.18f
#define OPEN_SCALE   0.90f

/* ---------------- fonts + layer ---------------- */
static finn_font g_f_big, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];

static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx0 = 0, g_ly0 = 0;
static int g_open = 0;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

static finn_anim g_a_open;

typedef struct { int x, y, w, h; } clk_rect;
static clk_rect g_r_panel, g_r_clear;

/* the clock's corner, where the page grows from */
static int g_ax = 0, g_ay = 0;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_big);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_big, p, font_px(s, 34.0f)) != 0) {
        fprintf(stderr, "finnclock: cannot load %s\n", p);
        return -1;
    }
    if (finn_font_load(&g_f_label, p, font_px(s, 12.0f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, font_px(s, 10.0f)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, font_px(s, 8.5f)) != 0) return -1;
    return 0;
}

int finnclock_init(const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);
    return 0;
}

void finnclock_free(void) {
    finn_font_free(&g_f_big);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
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
        finn_anim_to(&g_a_open, 1.0f, OPEN_DUR, 0.0f, FINN_EASE_OUT_BACK);
    else
        finn_anim_to(&g_a_open, 0.0f, CLOSE_DUR, 0.0f, FINN_EASE_IN_OUT);
}

int finnclock_is_open(void) { return g_open; }
int finnclock_visible(void) { return finn_anim_v(&g_a_open) > 0.004f; }
float finnclock_openness(void) { return finn_anim_v(&g_a_open); }

int finnclock_settled(void) {
    return !finn_anim_running(&g_a_open) && finn_anim_v(&g_a_open) >= 0.999f;
}

int finnclock_tick(float dt) { return finn_anim_tick(&g_a_open, dt); }
int finnclock_running(void) { return finn_anim_running(&g_a_open); }

/* ---------------- content ----------------
 *
 * Everything on the page is read, never remembered: the time from the wall
 * clock, the list from finnnotify. That is why the page has no state of its
 * own to get out of step with the machine.
 */

static void now_tm(struct tm *tm) {
    time_t t = time(NULL);
    localtime_r(&t, tm);
}

static void fmt_time(char *out, size_t cap) {
    struct tm tm; now_tm(&tm);
    strftime(out, cap, "%H:%M", &tm);
}

static void fmt_date(char *out, size_t cap) {
    struct tm tm; now_tm(&tm);
    strftime(out, cap, "%A, %-d %B %Y", &tm);
}

static void fmt_metaline(char *out, size_t cap) {
    struct tm tm; now_tm(&tm);
    char wk[8] = "", z[16] = "";
    strftime(wk, sizeof wk, "%V", &tm);
    strftime(z, sizeof z, "%z", &tm);

    if (strlen(z) == 5 && (z[0] == '+' || z[0] == '-'))
        snprintf(out, cap, "Week %s  ·  UTC%c%.2s:%.2s", wk, z[0], z + 1, z + 3);
    else
        snprintf(out, cap, "Week %s", wk);
}

/* Copy at most `max_w` pixels of `s`, ending in an ellipsis when it had to
 * cut. A notification is written by whoever posted it and can be any length,
 * so the page has to assume it is longer than the tile it lands in. */
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

/* right-aligned text at `rx` */
static void text_r(uint32_t *L, int lw, int lh, const finn_font *f,
                   float rx, float base, const char *s, uint32_t c) {
    finn_text_draw(f, L, lw, lh, rx - finn_text_width(f, s), base, s, c);
}

static void tile(uint32_t *L, int lw, int lh, float s, clk_rect r) {
    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                     (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                     s * TILE_R, FINN_GLASS, PAGE_TILE_GLASS);
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
    /* the page shows a minute, so it redraws exactly when the minute moves
     * and at midnight, when the date and the day name move with it */
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

    /* Anchored to the clock's own corner: the right edge of the panel meets
     * the right edge of the pill, and the page grows out of the clock. */
    int kx = 0, ky = 0, kw = 0, kh = 0;
    finndock_clock(&kx, &ky, &kw, &kh);
    if (kw <= 0 || kh <= 0) {          /* dock has not been laid out yet */
        kx = dock_x + dock_w; ky = dock_y; kw = 0; kh = dock_h;
    }

    int px0 = kx + kw - pw;
    int py0 = ky - rs(ANCHOR_GAP, s) - ph;

    int margin = rs(12, s);
    if (px0 < margin) px0 = margin;
    if (py0 < margin) py0 = margin;
    if (px0 + pw > screen_w - margin) px0 = screen_w - margin - pw;
    if (py0 + ph > screen_h - margin) py0 = screen_h - margin - ph;

    g_lx0 = px0;
    g_ly0 = py0;
    g_ax = kx + kw;
    g_ay = ky + kh;

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
                     (float)pw, (float)ph, s * PAGE_R, FINN_GLASS, PAGE_GLASS);

    int x0 = rs(PAGE_PAD, s);
    int inner = rs(INNER, s);
    int y = rs(PAGE_PAD, s);
    int full = pw - 2 * x0;

    g_r_panel.x = 0; g_r_panel.y = 0; g_r_panel.w = pw; g_r_panel.h = ph;
    g_r_clear.w = 0;

    /* ---- the time ---- */
    clk_rect rc = { x0, y, full, rs(CLOCK_H, s) };
    tile(L, lw, lh, s, rc);

    char tbuf[16], dbuf[64], mbuf[64];
    fmt_time(tbuf, sizeof tbuf);
    fmt_date(dbuf, sizeof dbuf);
    fmt_metaline(mbuf, sizeof mbuf);

    finn_text_draw(&g_f_big, L, lw, lh,
                   (float)(rc.x + inner), (float)(rc.y + rs(44, s)), tbuf, FINN_INK);
    finn_text_draw(&g_f_sub, L, lw, lh,
                   (float)(rc.x + inner), (float)(rc.y + rs(62, s)), dbuf, FINN_DIM);
    text_r(L, lw, lh, &g_f_micro, (float)(rc.x + rc.w - inner),
           (float)(rc.y + rs(62, s)), mbuf, FINN_MUTE);

    y += rc.h + rs(PAGE_GAP, s);

    /* ---- what happened ---- */
    clk_rect rh2 = { x0, y, full, rs(HEAD_H, s) };
    int notes = finn_notify_count();

    finn_text_draw(&g_f_label, L, lw, lh,
                   (float)rh2.x, (float)(rh2.y + rs(14, s)), "Notifications", FINN_INK);

    if (notes > 0) {
        const char *clr = "Clear";
        float cwid = finn_text_width(&g_f_sub, clr);
        float cbx = (float)(rh2.x + rh2.w) - cwid;
        float cby = (float)(rh2.y + rs(14, s));
        text_r(L, lw, lh, &g_f_sub, (float)(rh2.x + rh2.w), cby, clr, FINN_INK);

        int pad = rs(6, s);
        g_r_clear.x = (int)cbx - pad;
        g_r_clear.y = rh2.y - pad;
        g_r_clear.w = (int)cwid + pad * 2;
        g_r_clear.h = rh2.h + pad * 2;
    }

    y += rh2.h + rs(PAGE_GAP, s);

    if (notes == 0) {
        /* Nothing has happened since the shell started, and the page says so
         * rather than showing an empty list it cannot fill. */
        int block = rs(NOTE_H * NOTE_SLOTS + NOTE_GAP * (NOTE_SLOTS - 1), s);
        float cap1 = finn_text_cap_height(&g_f_sub);
        float cap2 = finn_text_cap_height(&g_f_micro);
        float cx = (float)(x0 + full) * 0.5f;
        float cy = (float)y + (float)block * 0.5f;

        const char *a = "No new notifications";
        const char *b = "Nothing has happened since this session started.";
        finn_text_draw(&g_f_sub, L, lw, lh, cx - finn_text_width(&g_f_sub, a) * 0.5f,
                       cy - rs(2, s) + cap1 * 0.5f, a, FINN_DIM);
        finn_text_draw(&g_f_micro, L, lw, lh, cx - finn_text_width(&g_f_micro, b) * 0.5f,
                       cy + rs(16, s) + cap2 * 0.5f, b, FINN_MUTE);
    } else {
        int shown = notes < NOTE_SLOTS ? notes : NOTE_SLOTS;
        for (int i = 0; i < shown; i++) {
            const finn_note *n = finn_notify_at(i);
            if (!n) break;

            clk_rect rn = { x0, y + i * (rs(NOTE_H, s) + rs(NOTE_GAP, s)),
                            full, rs(NOTE_H, s) };
            tile(L, lw, lh, s, rn);

            char title[80], body[200];
            fit_str(&g_f_label, n->title, (float)(rn.w - inner * 2 - rs(40, s)),
                    title, sizeof title);
            fit_str(&g_f_sub, n->body, (float)(rn.w - inner * 2),
                    body, sizeof body);

            finn_text_draw(&g_f_label, L, lw, lh,
                           (float)(rn.x + inner), (float)(rn.y + rs(19, s)),
                           title, FINN_INK);

            struct tm nt;
            localtime_r(&n->at, &nt);
            char when[16];
            strftime(when, sizeof when, "%H:%M", &nt);
            text_r(L, lw, lh, &g_f_micro, (float)(rn.x + rn.w - inner),
                   (float)(rn.y + rs(19, s)), when, FINN_MUTE);

            finn_text_draw(&g_f_sub, L, lw, lh,
                           (float)(rn.x + inner), (float)(rn.y + rs(37, s)),
                           body, FINN_DIM);
        }
    }
}

/* ---------------- compositing ---------------- */

void finnclock_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnclock_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnclock_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    float a = v * 1.15f;
    if (a > 1.0f) a = 1.0f;
    float k = OPEN_SCALE + (1.0f - OPEN_SCALE) * v;

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
