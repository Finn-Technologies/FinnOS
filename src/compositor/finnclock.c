/* finnclock — the FinnOS clock page: the time, the date, and what happened. */
#include "finnclock.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
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

/* motion: the Control Center's, so the two pages move as one family.
 * The numbers live in finnmotion.h, which is where every surface's motion
 * lives. */

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

/* the pointer's answer on the Clear control */
static finn_anim g_a_hover;
static float     g_hover = 0.0f;
static int       g_hover_on = 0;

/*
 * The minute turning.
 *
 * The page shows a minute, and a minute is the only thing in the shell that
 * changes without anybody asking. It gets the only motion that is not an
 * answer: the time dips a little and comes back, so the change is noticed
 * without being announced. It is not a flip and it is not a slide — a clock
 * that animates every minute is a clock you cannot stop looking at.
 */
static finn_anim g_a_tick;
static float     g_tick = 1.0f;
static int       g_tick_min = -1;

/*
 * A notification arriving.
 *
 * The newest tile is an inset that deepens into place: its glass starts at
 * the panel's own weight, which is the panel's own colour — so it is
 * invisible — and comes down to the tile's weight as the words arrive. A
 * box that simply appears reads as a glitch; an inset that deepens reads
 * as something settling into the page.
 */
static finn_anim g_a_note;
static float     g_note = 1.0f;
static unsigned  g_note_rev = 0;

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

/*
 * The pointer arrived. The Clear control is the only one on the page that
 * does anything, so it is the only one that answers.
 */
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
    MIX((int)(g_hover * 64.0f));
    MIX((int)(g_tick * 64.0f));
    MIX((int)(g_note * 64.0f));
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

    /*
     * The minute turning, on the page and only on the page: the ink of the
     * time dips and comes back. The dip is the whole acknowledgement — the
     * digits themselves do not move, because a clock that slides its numbers
     * every minute is a clock you cannot stop looking at.
     */
    int minute = (int)(time(NULL) / 60);
    if (minute != g_tick_min) {
        g_tick_min = minute;
        if (g_open) {
            /* the track has to start at zero: it is already parked at one, and
             * a track asked to go where it already is does not move */
            finn_anim_jump(&g_a_tick, 0.0f);
            finn_anim_to(&g_a_tick, 1.0f, FINN_MOTION_CLOCK_TICK, 0.0f,
                         FINN_EASE_LINEAR);
        }
    }
    float dip = 1.0f - 0.55f * sinf(3.14159265f * g_tick);
    uint32_t time_ink = (FINN_INK & 0x00FFFFFFu) |
                        ((unsigned)(dip * 255.0f + 0.5f) << 24);

    finn_text_draw(&g_f_big, L, lw, lh,
                   (float)(rc.x + inner), (float)(rc.y + rs(44, s)), tbuf, time_ink);
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

        int pad = rs(6, s);
        g_r_clear.x = (int)cbx - pad;
        g_r_clear.y = rh2.y - pad;
        g_r_clear.w = (int)cwid + pad * 2;
        g_r_clear.h = rh2.h + pad * 2;

        /* the pointer's answer, the same patch the dock puts under the
         * finger: a wash of ink behind the word, not a change of the word */
        if (g_hover > 0.004f)
            finn_fill_rrect(L, lw, lh, (float)g_r_clear.x, (float)g_r_clear.y,
                            (float)g_r_clear.w, (float)g_r_clear.h,
                            s * TILE_R,
                            (FINN_INK & 0x00FFFFFFu) |
                            ((unsigned)(FINN_MOTION_HOVER_WASH * g_hover * 255.0f + 0.5f) << 24));

        text_r(L, lw, lh, &g_f_sub, (float)(rh2.x + rh2.w), cby, clr, FINN_INK);
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
        /*
         * A notification arriving. The newest tile is the one that settles:
         * its glass starts at the panel's own weight — which is the panel's
         * own colour, so it is invisible — and comes down to the tile's
         * weight as its words arrive.
         */
        unsigned rev = finn_notify_rev();
        if (rev != g_note_rev) {
            g_note_rev = rev;
            if (g_open) {
                finn_anim_jump(&g_a_note, 0.0f);
                finn_anim_to(&g_a_note, 1.0f, FINN_MOTION_NOTE, 0.0f,
                             FINN_EASE_OUT);
            }
        }

        int shown = notes < NOTE_SLOTS ? notes : NOTE_SLOTS;
        for (int i = 0; i < shown; i++) {
            const finn_note *n = finn_notify_at(i);
            if (!n) break;

            clk_rect rn = { x0, y + i * (rs(NOTE_H, s) + rs(NOTE_GAP, s)),
                            full, rs(NOTE_H, s) };

            /* the newest tile is the one arriving; the rest are already there */
            float p = (i == 0) ? g_note : 1.0f;
            if (p < 0.999f) {
                float mix = PAGE_GLASS + (PAGE_TILE_GLASS - PAGE_GLASS) * p;
                finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                                 (float)rn.x, (float)rn.y, (float)rn.w, (float)rn.h,
                                 s * TILE_R, FINN_GLASS, mix);
            } else {
                tile(L, lw, lh, s, rn);
            }

            unsigned al = (unsigned)(p * 255.0f + 0.5f);
            uint32_t ink  = (FINN_INK  & 0x00FFFFFFu) | (al << 24);
            uint32_t mute = (FINN_MUTE & 0x00FFFFFFu) | (al << 24);
            uint32_t dim  = (FINN_DIM  & 0x00FFFFFFu) | (al << 24);

            char title[80], body[200];
            fit_str(&g_f_label, n->title, (float)(rn.w - inner * 2 - rs(40, s)),
                    title, sizeof title);
            fit_str(&g_f_sub, n->body, (float)(rn.w - inner * 2),
                    body, sizeof body);

            finn_text_draw(&g_f_label, L, lw, lh,
                           (float)(rn.x + inner), (float)(rn.y + rs(19, s)),
                           title, ink);

            struct tm nt;
            localtime_r(&n->at, &nt);
            char when[16];
            strftime(when, sizeof when, "%H:%M", &nt);
            text_r(L, lw, lh, &g_f_micro, (float)(rn.x + rn.w - inner),
                   (float)(rn.y + rs(19, s)), when, mute);

            finn_text_draw(&g_f_sub, L, lw, lh,
                           (float)(rn.x + inner), (float)(rn.y + rs(37, s)),
                           body, dim);
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
