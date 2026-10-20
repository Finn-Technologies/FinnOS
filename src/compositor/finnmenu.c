/* finnmenu — the FinnOS Start Menu: the system's catalogue of apps. */
#define _GNU_SOURCE
#include "finnmenu.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnapps.h"
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * The same glass and the same tiles as the Control Center and the clock
 * page, on purpose: these are three surfaces of one shell, and a menu that
 * invented its own weights or its own corner radius would read as a
 * different OS.
 */
#define MENU_GLASS       0.72f
#define MENU_TILE_GLASS  0.50f

/*
 * Metrics authored for the 1080p canvas.
 *
 * The Start Menu is the one surface that is not anchored to anything: it is
 * the system's front door, so it sits in the middle of the screen and the
 * desktop reads as a room it is standing in. Its width is therefore its own
 * — a launcher has to hold a grid, and a grid does not fit in the width of a
 * control panel — but everything inside it is the Control Center's language:
 * the same glass, the same corner radii, the same ink.
 */
#define MENU_W   760.0f
#define MENU_PAD  28.0f
#define MENU_GAP  24.0f
#define MENU_R    34.0f
#define TILE_R    22.0f

#define HEAD_H     30.0f   /* the "Applications" line                */
#define HEAD_GAP   24.0f
#define ICON_BOX   76.0f   /* the square an app's glyph sits in      */
#define ICON_INK   40.0f   /* the glyph's own size inside that square */
#define CAP_GAP    12.0f
#define CAP_H      22.0f   /* the caption's line                      */
#define DET_H      18.0f   /* the detail line under it                */
#define CELL_H     (ICON_BOX + CAP_GAP + CAP_H + DET_H)
#define COLS         4
#define ROW_GAP     18.0f
#define FOOT_H      46.0f   /* the power row                           */

/* motion: the Control Center's, so the surfaces move as one family */
#define OPEN_DUR     0.26f
#define CLOSE_DUR    0.18f
#define OPEN_SCALE   0.92f
#define HOVER_WASH    0.055f
#define PRESS_WASH    0.090f
#define HOVER_IN_DUR  0.11f
#define HOVER_OUT_DUR 0.20f
#define PRESS_IN_DUR  0.08f
#define PRESS_OUT_DUR 0.17f

/* ---------------- fonts ---------------- */
static finn_font g_f_head, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];

/* ---------------- the registry ---------------- */
static char g_registry[512];

/*
 * The app's own glyph, loaded once and kept against the id it belongs to.
 * The catalogue can change under the shell — finnpkg installs something while
 * the menu is closed — so the icon is reloaded when the id at that slot
 * changes rather than when the file on disk changes.
 */
typedef struct {
    fsvg svg;
    int  loaded;
    char id[32];
} app_icon;
static app_icon g_icons[FINN_APP_MAX];

static char g_icon_dir[512];

/* ---------------- layer ---------------- */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx0 = 0, g_ly0 = 0;
static int g_open = 0;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

/* the menu's centre, where it grows from */
static int g_ax = 0, g_ay = 0;

typedef struct { int x, y, w, h; } menu_rect;
static menu_rect g_r_panel, g_r_restart, g_r_shutdown;
static menu_rect g_r_app[FINN_APP_MAX];
static int       g_n_apps = 0;

/* ---------------- motion ---------------- */
static finn_anim g_a_open;
static finn_anim g_a_hover, g_a_press;
static float     g_hover = 0.0f;
static int       g_hover_idx = -1;     /* which control the pointer is on */
static int       g_press_idx = -1;
static int       g_press = FINNMENU_NONE;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_head);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_head, p, font_px(s, 17.0f)) != 0) {
        fprintf(stderr, "finnmenu: cannot load %s\n", p);
        return -1;
    }
    if (finn_font_load(&g_f_label, p, font_px(s, 12.5f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, font_px(s, 10.0f)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, font_px(s, 9.0f)) != 0) return -1;
    return 0;
}

/* ---------------- init ---------------- */

int finnmenu_init(const char *icon_dir, const char *font_dir,
                  const char *registry) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    snprintf(g_registry, sizeof g_registry, "%s",
             registry ? registry : FINN_APPS_CATALOGUE);
    return 0;
}

void finnmenu_free(void) {
    for (int i = 0; i < FINN_APP_MAX; i++) {
        if (g_icons[i].loaded) fsvg_free(&g_icons[i].svg);
        g_icons[i].loaded = 0;
        g_icons[i].id[0] = 0;
    }
    finn_font_free(&g_f_head);
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

void finnmenu_set_open(int open) {
    open = open ? 1 : 0;
    if (open == g_open) return;
    g_open = open;
    fprintf(stderr, "finnmenu: start menu %s\n", open ? "open" : "closed");

    if (open) {
        /* The catalogue is read when the menu opens, not when it is built:
         * the shell can sit open for an hour and the machine can still have
         * something installed in the meantime. */
        finn_apps_load(g_registry, NULL);
        g_built = 0;
        finn_anim_to(&g_a_open, 1.0f, OPEN_DUR, 0.0f, FINN_EASE_OUT_BACK);
    } else {
        finn_anim_to(&g_a_open, 0.0f, CLOSE_DUR, 0.0f, FINN_EASE_IN_OUT);
        g_press = FINNMENU_NONE;
        g_press_idx = -1;
        finn_anim_to(&g_a_press, 0.0f, PRESS_OUT_DUR, 0.0f, FINN_EASE_OUT);
    }
}

int finnmenu_is_open(void) { return g_open; }
int finnmenu_visible(void) { return finn_anim_v(&g_a_open) > 0.004f; }
float finnmenu_openness(void) { return finn_anim_v(&g_a_open); }

int finnmenu_settled(void) {
    return !finn_anim_running(&g_a_open) && finn_anim_v(&g_a_open) >= 0.999f;
}

int finnmenu_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_hover, dt);
    ch |= finn_anim_tick(&g_a_press, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finnmenu_running(void) {
    return finn_anim_running(&g_a_open) || finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_press);
}

/* ---------------- the catalogue ---------------- */

/*
 * The glyph for one app, loaded against the id it belongs to. A slot whose id
 * has changed — a catalogue that was reordered, an app that was replaced — is
 * reloaded, so a tile never shows the previous app's picture.
 */
static fsvg *icon_for(int i, const finn_app *a) {
    if (i < 0 || i >= FINN_APP_MAX || !a) return NULL;
    app_icon *ic = &g_icons[i];

    if (ic->loaded && !strcmp(ic->id, a->id)) return &ic->svg;

    if (ic->loaded) { fsvg_free(&ic->svg); ic->loaded = 0; }
    ic->id[0] = 0;

    if (!a->icon[0] || !g_icon_dir[0]) return NULL;

    char p[600];
    snprintf(p, sizeof p, "%s/%s", g_icon_dir, a->icon);
    if (fsvg_load(p, &ic->svg) != 0) {
        memset(&ic->svg, 0, sizeof ic->svg);
        return NULL;
    }
    ic->loaded = 1;
    snprintf(ic->id, sizeof ic->id, "%s", a->id);
    return &ic->svg;
}

/* ---------------- text helpers ---------------- */

static void text_r(uint32_t *L, int lw, int lh, const finn_font *f,
                   float right, float base, const char *s, uint32_t col) {
    finn_text_draw(f, L, lw, lh, right - finn_text_width(f, s), base, s, col);
}

/* Copy at most `max_w` pixels of `s`, ending in an ellipsis when it had to
 * cut. An app's name is written by whoever shipped it and can be any length,
 * so the caption has to assume it is longer than the cell it lands in. */
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
    MIX(g_n_apps);
    for (int i = 0; i < g_n_apps; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a) continue;
        for (const char *p = a->id; *p; p++) MIX(*p);
        MIX(a->installed ? 1 : 0);
        for (const char *p = a->name; *p; p++) MIX(*p);
    }
    MIX((int)(g_hover * 64.0f));
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
    MIX(g_press_idx + 2);
    #undef MIX
    return h ? h : 1u;
}

void finnmenu_render(int screen_w, int screen_h, float scale,
                     int dock_x, int dock_y, int dock_w, int dock_h,
                     const finn_state *st) {
    (void)dock_x; (void)dock_y; (void)dock_w; (void)dock_h;

    float s = scale;
    if (g_font_scale != s) {
        if (load_fonts(s) != 0) return;
        g_font_scale = s;
    }

    int n = finn_apps_count();
    if (n > FINN_APP_MAX) n = FINN_APP_MAX;
    g_n_apps = n;

    int rows = (n + COLS - 1) / COLS;
    if (rows < 1) rows = 1;

    float grid_h = (float)rows * CELL_H + (float)(rows - 1) * ROW_GAP;
    float panel_h = MENU_PAD * 2.0f + HEAD_H + HEAD_GAP + grid_h +
                    MENU_GAP + FOOT_H;

    int pw = rs(MENU_W, s), ph = rs(panel_h, s);

    /*
     * The middle of the screen. The Start Menu is not a panel that belongs
     * to a control in the dock — it is the system's front door, and a front
     * door is in the middle of the wall you walk through.
     */
    int margin = rs(12, s);
    int px0 = (screen_w - pw) / 2;
    int py0 = (screen_h - ph) / 2;
    if (px0 < margin) px0 = margin;
    if (py0 < margin) py0 = margin;
    if (px0 + pw > screen_w - margin) px0 = screen_w - margin - pw;
    if (py0 + ph > screen_h - margin) py0 = screen_h - margin - ph;

    g_lx0 = px0;
    g_ly0 = py0;

    /* it grows out of its own middle */
    g_ax = px0 + pw / 2;
    g_ay = py0 + ph / 2;

    uint32_t key = build_key(s, pw, ph, px0, py0, pw, ph, st);
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
                     (float)pw, (float)ph, s * MENU_R, FINN_GLASS, MENU_GLASS);

    int x0 = rs(MENU_PAD, s);
    int y = rs(MENU_PAD, s);
    int full = pw - 2 * x0;

    g_r_panel.x = 0; g_r_panel.y = 0; g_r_panel.w = pw; g_r_panel.h = ph;
    g_r_restart.w = g_r_restart.h = 0;
    g_r_shutdown.w = g_r_shutdown.h = 0;

    /* ---- the header ---- */
    finn_text_draw(&g_f_head, L, lw, lh,
                   (float)x0, (float)(y + rs(20, s)), "Applications", FINN_INK);

    int ready = finn_apps_installed_count();
    char head[64];
    if (n == 0)
        snprintf(head, sizeof head, "no apps");
    else
        snprintf(head, sizeof head, "%d of %d ready", ready, n);
    text_r(L, lw, lh, &g_f_micro, (float)(x0 + full),
           (float)(y + rs(19, s)), head, FINN_MUTE);

    y += rs(HEAD_H, s) + rs(HEAD_GAP, s);

    /* ---- the grid ---- */
    float cell_w = (float)full / (float)COLS;
    float box = s * ICON_BOX;

    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a) break;

        int col = i % COLS, row = i / COLS;
        float cx = (float)x0 + (float)col * cell_w;
        float cy = (float)y + (float)row * (CELL_H + ROW_GAP) * s;
        float bw = cell_w - s * 18.0f;

        /* the tile is centred in its cell, so a row of tiles reads as a row
         * rather than as a staircase */
        float bx = cx + (cell_w - box) * 0.5f;

        g_r_app[i].x = (int)bx;
        g_r_app[i].y = (int)cy;
        g_r_app[i].w = (int)box;
        g_r_app[i].h = (int)box;

        /*
         * The tile is the app's own square: the accent for an app the machine
         * can open, a quiet wash for one it cannot. A muted tile says "not
         * here" more honestly than a coloured one that does nothing when
         * pressed.
         */
        uint32_t glyph = a->installed ? FINN_KNOB : FINN_MUTE;
        uint32_t cap_col = a->installed ? FINN_INK : FINN_DIM;

        if (a->installed)
            finn_fill_rrect(L, lw, lh, bx, cy, box, box, s * TILE_R, FINN_ON);
        else
            finn_fill_rrect(L, lw, lh, bx, cy, box, box, s * TILE_R,
                            (FINN_INK & 0x00FFFFFFu) | 0x1A000000u);

        /* the hover and press washes, the shell's only answer to a pointer */
        if (a->installed) {
            float wash = (HOVER_WASH * g_hover * (g_hover_idx == i ? 1.0f : 0.0f) +
                          PRESS_WASH * finn_anim_v(&g_a_press) *
                          (g_press_idx == i ? 1.0f : 0.0f));
            if (wash > 0.004f) {
                unsigned al = (unsigned)(wash * 255.0f + 0.5f);
                if (al > 255) al = 255;
                finn_fill_rrect(L, lw, lh, bx, cy, box, box, s * TILE_R,
                                (FINN_INK & 0x00FFFFFFu) | (al << 24));
            }
        }

        fsvg *svg = icon_for(i, a);
        if (svg) {
            int ipx, iox, ioy;
            finn_icon_frame(svg, rs(ICON_INK, s),
                            (int)lroundf(bx + box * 0.5f),
                            (int)lroundf(cy + box * 0.5f), &ipx, &iox, &ioy);
            const uint32_t *t = finn_icon_ref(svg, ipx, glyph, glyph, 0);
            if (t) finn_blit(L, lw, lh, t, ipx, ipx, iox, ioy);
        }

        char cap[64];
        fit_str(&g_f_label, a->name, bw, cap, sizeof cap);
        float capw = finn_text_width(&g_f_label, cap);
        float cap_base = cy + box + s * CAP_GAP + rs(11, s);
        finn_text_draw(&g_f_label, L, lw, lh,
                       bx + (box - capw) * 0.5f, cap_base, cap, cap_col);

        /* the detail line: what the app is, or the truth about this machine */
        const char *detail = a->installed ? a->note : "Not installed";
        char det[64];
        fit_str(&g_f_micro, detail, bw, det, sizeof det);
        float detw = finn_text_width(&g_f_micro, det);
        finn_text_draw(&g_f_micro, L, lw, lh,
                       bx + (box - detw) * 0.5f, cap_base + rs(12, s),
                       det, FINN_MUTE);
    }

    y += rs(grid_h, s) + rs(MENU_GAP, s);

    /* ---- the footer ---- */
    finn_fill_rrect(L, lw, lh, (float)x0, (float)y, (float)full,
                    (float)rs(1, s), 0.0f, FINN_HAIR);
    y += rs(1, s) + rs(20, s);

    const char *restart = "Restart";
    const char *shutdown = "Shut down";
    float rw = finn_text_width(&g_f_label, restart);
    float sw = finn_text_width(&g_f_label, shutdown);

    /* the wordmark: this is FinnOS's own front door */
    finn_text_draw(&g_f_head, L, lw, lh, (float)x0, (float)(y + rs(15, s)),
                   "FinnOS", FINN_INK);

    finn_text_draw(&g_f_label, L, lw, lh,
                   (float)(x0 + full - sw - rw - rs(24, s)),
                   (float)(y + rs(13, s)), restart, FINN_DIM);
    text_r(L, lw, lh, &g_f_label, (float)(x0 + full), (float)(y + rs(13, s)),
           shutdown, FINN_DIM);

    g_r_restart.x = x0 + full - (int)sw - (int)rw - rs(24, s) - rs(8, s);
    g_r_restart.y = y - rs(6, s);
    g_r_restart.w = (int)rw + rs(16, s);
    g_r_restart.h = rs(26, s);

    g_r_shutdown.x = x0 + full - (int)sw - rs(8, s);
    g_r_shutdown.y = y - rs(6, s);
    g_r_shutdown.w = (int)sw + rs(16, s);
    g_r_shutdown.h = rs(26, s);
}

/* ---------------- compositing ---------------- */

void finnmenu_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnmenu_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnmenu_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    float a = v * 1.15f;
    if (a > 1.0f) a = 1.0f;
    float k = OPEN_SCALE + (1.0f - OPEN_SCALE) * v;

    /* The menu grows out of its own middle, so the scaled rectangle keeps
     * that middle where it is: the centre moves with the scale, and the
     * edges follow it. */
    float ax = (float)g_ax, ay = (float)g_ay;
    float cx = ax + ((float)g_lx0 + (float)g_lw * 0.5f - ax) * k;
    float cy = ay + ((float)g_ly0 + (float)g_lh * 0.5f - ay) * k;

    if (x) *x = cx - (float)g_lw * k * 0.5f;
    if (y) *y = cy - (float)g_lh * k * 0.5f;
    if (w) *w = (float)g_lw * k;
    if (h) *h = (float)g_lh * k;
    if (alpha) *alpha = a;
}

void finnmenu_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_layer) return;
    float x, y, w, h, alpha;
    finnmenu_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_scaled(dst, dw, dh, g_layer, g_lw, g_lh, x, y, w, h, alpha);
}

const uint32_t *finnmenu_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx0;
    if (y) *y = g_ly0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_rev;
    return finnmenu_visible() ? g_layer : NULL;
}

/* ---------------- input ---------------- */

static int in_rect(const menu_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int finnmenu_hit(int px, int py, int *app_index) {
    if (app_index) *app_index = -1;
    if (!g_open || !g_layer || !finnmenu_visible()) return FINNMENU_NONE;
    if (finn_anim_v(&g_a_open) < 0.90f) return FINNMENU_NONE;

    int x = px - g_lx0, y = py - g_ly0;
    if (x < 0 || y < 0 || x >= g_lw || y >= g_lh) return FINNMENU_NONE;

    for (int i = 0; i < g_n_apps; i++) {
        if (!in_rect(&g_r_app[i], x, y)) continue;
        if (app_index) *app_index = i;
        return FINNMENU_APP;
    }
    if (in_rect(&g_r_restart, x, y))   return FINNMENU_RESTART;
    if (in_rect(&g_r_shutdown, x, y))  return FINNMENU_SHUTDOWN;
    return in_rect(&g_r_panel, x, y) ? FINNMENU_PANEL : FINNMENU_NONE;
}

/*
 * The pointer moved. Only a control that can be pressed answers: a tile for
 * an app that is not installed takes no wash, because there is nothing
 * behind it to promise anything.
 */
void finnmenu_hover_at(int px, int py) {
    int idx = -1;
    int what = finnmenu_hit(px, py, &idx);

    int next = -1;
    if (what == FINNMENU_APP) {
        const finn_app *a = finn_apps_at(idx);
        /* a tile for an app that is not installed takes no wash: there is
         * nothing behind it to promise anything */
        if (a && a->installed) next = idx;
    }
    else if (what == FINNMENU_RESTART)  next = FINN_APP_MAX;
    else if (what == FINNMENU_SHUTDOWN) next = FINN_APP_MAX + 1;

    if (next == g_hover_idx) return;
    g_hover_idx = next;
    if (next >= 0)
        finn_anim_to(&g_a_hover, 1.0f, HOVER_IN_DUR, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_hover, 0.0f, HOVER_OUT_DUR, 0.0f, FINN_EASE_OUT);
}

void finnmenu_press(int what) {
    g_press = what;
    if (what == FINNMENU_NONE) {
        g_press_idx = -1;
        finn_anim_to(&g_a_press, 0.0f, PRESS_OUT_DUR, 0.0f, FINN_EASE_OUT);
    } else {
        if (what == FINNMENU_APP) g_press_idx = g_hover_idx;
        else if (what == FINNMENU_RESTART)  g_press_idx = FINN_APP_MAX;
        else if (what == FINNMENU_SHUTDOWN) g_press_idx = FINN_APP_MAX + 1;
        finn_anim_to(&g_a_press, 1.0f, PRESS_IN_DUR, 0.0f, FINN_EASE_OUT);
    }
}
