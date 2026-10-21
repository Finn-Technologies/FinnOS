/* finnmenu — the FinnOS Start Menu: the system's catalogue of apps. */
#define _GNU_SOURCE
#include "finnmenu.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
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
#define MENU_GLASS       0.70f
#define MENU_TILE_GLASS  0.50f

/*
 * Metrics authored for the 1080p canvas, aligned with the Figma surface
 * language (r=15 panel -> 19.5, r=12 tiles -> 15.6, 0.70 panel glass,
 * 0.50 inset tile and footer pills).
 */
#define MENU_W   760.0f
#define MENU_PAD  24.0f
#define MENU_GAP  20.0f
#define MENU_R    19.5f   /* reference r=15 */
#define TILE_R    15.6f   /* reference r=12 */

#define HEAD_H     30.0f   /* the "Applications" line                */
#define HEAD_GAP   20.0f
#define ICON_BOX   68.0f   /* the square an app's glyph sits in      */
#define ICON_INK   36.0f   /* the glyph's own size inside that square */
#define CAP_GAP    12.0f
#define CAP_H      22.0f   /* the caption's line                      */
#define DET_H      18.0f   /* the detail line under it                */
#define CELL_PAD   14.0f
#define CELL_H     (CELL_PAD * 2.0f + ICON_BOX + CAP_GAP + CAP_H + DET_H)
#define COLS         4
#define ROW_GAP     14.0f
#define FOOT_H      46.0f   /* the power row                           */

/* motion: the Control Center's, so the surfaces move as one family. The
 * numbers live in finnmotion.h, which is where every surface's motion
 * lives; the menu is the same object as the panel, with different
 * contents. */

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

/* ---------------- the panel and its cells ----------------
 *
 * The layer is built in two pieces, because the grid moves and the glass
 * does not.
 *
 * The glass — the panel, the header and the footer — is rasterised once per
 * geometry and kept. The app cells are rasterised one at a time and kept
 * too, because the grid's arrival moves them: re-rasterising eight names
 * sixty times a second would cost far more than the motion is worth, and
 * the shell is meant to be cheap enough to leave running.
 *
 * What is left per frame is a copy of the glass and eight blits.
 */
static uint32_t *g_frost = NULL;      /* the glass, the header and the footer */
static int       g_fw = 0, g_fh = 0;
static uint32_t  g_frost_key = 0;
static int       g_frost_built = 0;

typedef struct {
    uint32_t *px;
    int       w, h;
    uint32_t  key;
    int       ok;
} cell_raster;
static cell_raster g_cell[FINN_APP_MAX];

/* where each cell sits inside the layer */
static int g_cell_x[FINN_APP_MAX], g_cell_y[FINN_APP_MAX];

/* ---------------- motion ---------------- */
static finn_anim g_a_open;
static finn_anim g_a_scrim;      /* the room dimming behind the menu */
static finn_anim g_a_grid;        /* the apps arriving into the grid */
static finn_anim g_a_hover, g_a_press;
static float     g_hover = 0.0f;
static int       g_hover_idx = -1;     /* which control the pointer is on */
static int       g_press_idx = -1;
static int       g_press = FINNMENU_NONE;

/*
 * The grid's arrival.
 *
 * The panel is the room; the apps are the people coming in. They arrive
 * after the room does, one after another, each rising a little into its
 * place — which is what a grid of things that was not there a moment ago
 * looks like.
 *
 * The step is small on purpose: eight tiles at a quarter of a second each
 * would read as a queue, and the point is that the menu fills, not that it
 * performs.
 */
#define GRID_DELAY 0.060f   /* the room is ready before anyone walks in */
#define GRID_STEP  0.022f   /* between two arrivals                        */
#define GRID_CELL  0.200f   /* one arrival                                 */
#define GRID_RISE  10.0f    /* how far below its place a tile starts      */

/* how long the whole arrival takes, in seconds */
static float g_grid_total = GRID_CELL;

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
        free(g_cell[i].px);
        g_cell[i].px = NULL;
        g_cell[i].w = g_cell[i].h = 0;
        g_cell[i].ok = 0;
    }
    finn_font_free(&g_f_head);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
    free(g_layer);
    g_layer = NULL;
    free(g_frost);
    g_frost = NULL;
    g_lw = g_lh = 0;
    g_fw = g_fh = 0;
    g_built = 0;
    g_frost_built = 0;
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
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
        /* The room dims first and the door opens after it, so the menu is
         * never a bright card that suddenly has a dark room behind it. */
        finn_anim_to(&g_a_scrim, 1.0f, FINN_MOTION_SCRIM_IN, 0.0f, FINN_EASE_OUT);

        /* and the apps walk in after the room is ready */
        int count = finn_apps_count();
        if (count > FINN_APP_MAX) count = FINN_APP_MAX;
        float total = GRID_DELAY + (float)(count - 1) * GRID_STEP + GRID_CELL;
        if (count < 1) total = GRID_DELAY + GRID_CELL;
        g_grid_total = total;
        finn_anim_jump(&g_a_grid, 0.0f);
        finn_anim_to(&g_a_grid, 1.0f, total, 0.0f, FINN_EASE_LINEAR);
    } else {
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
        /* and the room brightens first when the door shuts */
        finn_anim_to(&g_a_scrim, 0.0f, FINN_MOTION_SCRIM_OUT, 0.0f, FINN_EASE_IN_OUT);
        /* the grid does not walk back out: it is simply there when the room
         * is empty, and the room closing takes it with it */
        finn_anim_jump(&g_a_grid, 1.0f);
        g_press = FINNMENU_NONE;
        g_press_idx = -1;
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    }
}

int finnmenu_is_open(void) { return g_open; }
int finnmenu_visible(void) { return finn_anim_v(&g_a_open) > 0.004f; }
float finnmenu_openness(void) { return finn_anim_v(&g_a_open); }

/* How dark the room is behind the menu, 0..1 of FINN_MOTION_SCRIM. The
 * compositor dims the frame by this much, so the menu is the only lit thing
 * in the room. */
float finnmenu_scrim(void) {
    return FINN_MOTION_SCRIM * finn_anim_v(&g_a_scrim);
}

int finnmenu_settled(void) {
    return !finn_anim_running(&g_a_open) && finn_anim_v(&g_a_open) >= 0.999f;
}

int finnmenu_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_scrim, dt);
    ch |= finn_anim_tick(&g_a_grid, dt);
    ch |= finn_anim_tick(&g_a_hover, dt);
    ch |= finn_anim_tick(&g_a_press, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finnmenu_running(void) {
    return finn_anim_running(&g_a_open) || finn_anim_running(&g_a_scrim) ||
           finn_anim_running(&g_a_grid) ||
           finn_anim_running(&g_a_hover) || finn_anim_running(&g_a_press);
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
    MIX(g_hover_idx + 2);
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
    MIX(g_press_idx + 2);
    /* the grid's arrival: every cell moves, so the layer moves */
    MIX((int)(finn_anim_v(&g_a_grid) * 256.0f));
    #undef MIX
    return h ? h : 1u;
}

/* ---------------- the glass ----------------
 *
 * The panel, the header and the footer. None of it moves while the menu is
 * opening, so it is rasterised once per geometry and copied into the layer;
 * only the grid is redrawn while the apps arrive.
 */
static void build_frost(float s, int pw, int ph, int n,
                        float wash_r, float wash_s) {
    uint32_t *L = g_frost;
    int lw = pw, lh = ph;
    int x0 = rs(MENU_PAD, s);
    int y = rs(MENU_PAD, s);
    int full = pw - 2 * x0;

    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0, 0.0f, 0.0f,
                     (float)pw, (float)ph, s * MENU_R, FINN_GLASS, MENU_GLASS);

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

    /* ---- the footer ---- */
    int rows = (n + COLS - 1) / COLS;
    if (rows < 1) rows = 1;
    float grid_h = (float)rows * CELL_H + (float)(rows - 1) * ROW_GAP;

    y += rs(HEAD_H, s) + rs(HEAD_GAP, s);
    y += rs(grid_h, s) + rs(MENU_GAP, s);

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

    /*
     * The power row answers the pointer the way every other control in the
     * shell does: a wash of ink behind the word, at the same strength the
     * dock uses. Restart and Shut down are the two things the menu can do
     * that are not an app, and they are drawn like everything else rather
     * than as a line of quiet text that happens to be clickable.
     */
    {
        int pill_h = rs(30, s);
        int pill_pad = rs(14, s);
        int rw_pill = (int)rw + pill_pad * 2;
        int sw_pill = (int)sw + pill_pad * 2;
        int sx_pill = x0 + full - sw_pill;
        int rx_pill = sx_pill - rs(10, s) - rw_pill;
        int py_pill = y - rs(8, s);
        float prad = (float)pill_h * 0.5f;

        finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                         (float)rx_pill, (float)py_pill,
                         (float)rw_pill, (float)pill_h,
                         prad, FINN_GLASS, MENU_TILE_GLASS);
        finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                         (float)sx_pill, (float)py_pill,
                         (float)sw_pill, (float)pill_h,
                         prad, FINN_GLASS, MENU_TILE_GLASS);

        if (wash_r > 0.004f) {
            unsigned al = (unsigned)(wash_r * 255.0f + 0.5f);
            finn_fill_rrect(L, lw, lh, (float)rx_pill, (float)py_pill,
                            (float)rw_pill, (float)pill_h,
                            prad, (FINN_INK & 0x00FFFFFFu) | (al << 24));
        }
        if (wash_s > 0.004f) {
            unsigned al = (unsigned)(wash_s * 255.0f + 0.5f);
            finn_fill_rrect(L, lw, lh, (float)sx_pill, (float)py_pill,
                            (float)sw_pill, (float)pill_h,
                            prad, (FINN_INK & 0x00FFFFFFu) | (al << 24));
        }

        finn_text_draw(&g_f_label, L, lw, lh,
                       (float)(rx_pill + pill_pad),
                       (float)(y + rs(13, s)), restart, FINN_INK);
        finn_text_draw(&g_f_label, L, lw, lh,
                       (float)(sx_pill + pill_pad),
                       (float)(y + rs(13, s)), shutdown, FINN_INK);

        g_r_restart.x = rx_pill;
        g_r_restart.y = py_pill;
        g_r_restart.w = rw_pill;
        g_r_restart.h = pill_h;

        g_r_shutdown.x = sx_pill;
        g_r_shutdown.y = py_pill;
        g_r_shutdown.w = sw_pill;
        g_r_shutdown.h = pill_h;
    }
}

/* ---------------- a cell ----------------
 *
 * One app: its tile, its glyph, its name and what it is. The cell is
 * rasterised once and kept, because the grid's arrival moves it and the
 * name under it does not change.
 */
static void build_cell(int i, const finn_app *a, float s,
                       int cw, int ch, float cell_w, float box, float wash,
                       float fx, float fy) {
    cell_raster *c = &g_cell[i];

    if (c->w != cw || c->h != ch) {
        free(c->px);
        c->px = malloc((size_t)cw * ch * 4);
        c->w = cw; c->h = ch;
    }
    if (!c->px) { c->ok = 0; return; }
    memset(c->px, 0, (size_t)cw * ch * 4);

    uint32_t *L = c->px;
    int lw = cw, lh = ch;

    float card_pad = s * 6.0f;
    float card_x = fx + card_pad;
    float card_y = fy;
    float card_w = cell_w - card_pad * 2.0f;
    float card_h = (float)ch - s * 2.0f;

    /* Subtle frosted card backing matching Figma r=12 tiles */
    finn_fill_rrect(L, lw, lh, card_x, card_y, card_w, card_h, s * TILE_R,
                    (FINN_GLASS & 0x00FFFFFFu) | (a->installed ? 0x38000000u : 0x18000000u));

    float tx = (cell_w - box) * 0.5f + fx;
    float bw = card_w - s * 14.0f;
    float ty = fy + s * CELL_PAD;

    uint32_t glyph = a->installed ? FINN_KNOB : FINN_MUTE;
    uint32_t cap_col = a->installed ? FINN_INK : FINN_DIM;

    if (a->installed)
        finn_fill_rrect(L, lw, lh, tx, ty, box, box, s * TILE_R, FINN_ON);
    else
        finn_fill_rrect(L, lw, lh, tx, ty, box, box, s * TILE_R,
                        (FINN_INK & 0x00FFFFFFu) | 0x1A000000u);

    if (a->installed && wash > 0.004f) {
        unsigned al = (unsigned)(wash * 255.0f + 0.5f);
        finn_fill_rrect(L, lw, lh, card_x, card_y, card_w, card_h, s * TILE_R,
                        (FINN_INK & 0x00FFFFFFu) | (al << 24));
    }

    fsvg *svg = icon_for(i, a);
    if (svg) {
        int ipx, iox, ioy;
        finn_icon_frame(svg, rs(ICON_INK, s),
                        (int)lroundf(tx + box * 0.5f),
                        (int)lroundf(ty + box * 0.5f), &ipx, &iox, &ioy);
        const uint32_t *t = finn_icon_ref(svg, ipx, glyph, glyph, 0);
        if (t) finn_blit(L, lw, lh, t, ipx, ipx, iox, ioy);
    }

    char cap[64];
    fit_str(&g_f_label, a->name, bw, cap, sizeof cap);
    float capw = finn_text_width(&g_f_label, cap);
    float cap_base = ty + box + s * CAP_GAP + rs(11, s);
    finn_text_draw(&g_f_label, L, lw, lh,
                   tx + (box - capw) * 0.5f, cap_base, cap, cap_col);

    /* the detail line: what the app is, or the truth about this machine */
    const char *detail = a->installed ? a->note : "Not installed";
    char det[64];
    fit_str(&g_f_micro, detail, bw, det, sizeof det);
    float detw = finn_text_width(&g_f_micro, det);
    finn_text_draw(&g_f_micro, L, lw, lh,
                   tx + (box - detw) * 0.5f, cap_base + rs(12, s),
                   det, FINN_MUTE);

    c->ok = 1;
}

/* How far along its arrival one cell is, 0..1. */
static float cell_progress(int i) {
    float v = finn_anim_v(&g_a_grid);
    if (v >= 1.0f) return 1.0f;
    float t = v * g_grid_total;
    float p = (t - (GRID_DELAY + (float)i * GRID_STEP)) / GRID_CELL;
    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    return finn_ease_at(FINN_EASE_OUT, p);
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

    int x0 = rs(MENU_PAD, s);
    int y = rs(MENU_PAD, s);
    int full = pw - 2 * x0;

    g_r_panel.x = 0; g_r_panel.y = 0; g_r_panel.w = pw; g_r_panel.h = ph;

    float cell_w = (float)full / (float)COLS;
    float box = s * ICON_BOX;
    int grid_y = y + rs(HEAD_H, s) + rs(HEAD_GAP, s);

    /* the power row's washes: they are part of the glass */
    float press_v = finn_anim_v(&g_a_press);
    float wash_r = (FINN_MOTION_HOVER_WASH * g_hover *
                    (g_hover_idx == FINN_APP_MAX ? 1.0f : 0.0f) +
                    FINN_MOTION_PRESS_WASH * press_v *
                    (g_press_idx == FINN_APP_MAX ? 1.0f : 0.0f));
    float wash_s = (FINN_MOTION_HOVER_WASH * g_hover *
                    (g_hover_idx == FINN_APP_MAX + 1 ? 1.0f : 0.0f) +
                    FINN_MOTION_PRESS_WASH * press_v *
                    (g_press_idx == FINN_APP_MAX + 1 ? 1.0f : 0.0f));

    /* ---- the glass ---- */
    {
        uint32_t fk = 2166136261u;
        #define MIX(v) do { fk ^= (uint32_t)(v); fk *= 16777619u; } while (0)
        MIX((int)(s * 1000.0f)); MIX(pw); MIX(ph); MIX(g_lx0); MIX(g_ly0);
        MIX(st->dark + 2); MIX(n); MIX(finn_apps_installed_count());
        MIX((int)(wash_r * 255.0f)); MIX((int)(wash_s * 255.0f));
        #undef MIX
        if (!fk) fk = 1u;

        if (!g_frost_built || fk != g_frost_key ||
            pw != g_fw || ph != g_fh) {
            if (pw != g_fw || ph != g_fh) {
                free(g_frost);
                g_frost = malloc((size_t)pw * ph * 4);
                g_fw = pw; g_fh = ph;
            }
            if (!g_frost) { g_frost_built = 0; return; }
            g_frost_key = fk;
            g_frost_built = 1;
            memset(g_frost, 0, (size_t)pw * ph * 4);
            build_frost(s, pw, ph, n, wash_r, wash_s);
        }
    }

    /* ---- the cells ---- */
    int cw = (int)ceilf(cell_w), ch = (int)ceilf(CELL_H * s);
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a) break;

        int col = i % COLS, row = i / COLS;
        int cols_in_row = n - row * COLS;
        if (cols_in_row > COLS) cols_in_row = COLS;
        float row_x0 = (float)x0 + (float)(COLS - cols_in_row) * cell_w * 0.5f;
        float cx = row_x0 + (float)col * cell_w;
        float cy = (float)grid_y + (float)row * (CELL_H + ROW_GAP) * s;

        g_r_app[i].x = (int)(cx + s * 6.0f);
        g_r_app[i].y = (int)cy;
        g_r_app[i].w = (int)(cell_w - s * 12.0f);
        g_r_app[i].h = ch - rs(2, s);

        g_cell_x[i] = (int)floorf(cx);
        g_cell_y[i] = (int)floorf(cy);

        /* what the cell is drawn from: its app, its size and its wash */
        float wash = 0.0f;
        if (a->installed)
            wash = (FINN_MOTION_HOVER_WASH * g_hover * (g_hover_idx == i ? 1.0f : 0.0f) +
                    FINN_MOTION_PRESS_WASH * press_v * (g_press_idx == i ? 1.0f : 0.0f));

        uint32_t ck = 2166136261u;
        #define MIX(v) do { ck ^= (uint32_t)(v); ck *= 16777619u; } while (0)
        MIX(i); MIX(cw); MIX(ch); MIX((int)(s * 1000.0f));
        MIX(st->dark + 2); MIX(a->installed ? 1 : 0);
        for (const char *p = a->name; *p; p++) MIX(*p);
        if (a->installed)
            for (const char *p = a->note; *p; p++) MIX(*p);
        MIX((int)(wash * 255.0f));
        #undef MIX
        if (!ck) ck = 1u;

        if (!g_cell[i].ok || g_cell[i].key != ck) {
            g_cell[i].key = ck;
            build_cell(i, a, s, cw, ch, cell_w, box, wash,
                       cx - (float)g_cell_x[i], cy - (float)g_cell_y[i]);
        }
    }

    /* ---- the layer ---- */
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

    /* the glass first, then the apps on top of it */
    memcpy(g_layer, g_frost, (size_t)pw * ph * 4);

    for (int i = 0; i < n; i++) {
        if (!g_cell[i].ok) continue;
        float p = cell_progress(i);
        if (p <= 0.004f) continue;
        int dy = (int)lroundf((1.0f - p) * GRID_RISE * s);
        finn_blit_scaled(g_layer, g_lw, g_lh, g_cell[i].px,
                         g_cell[i].w, g_cell[i].h,
                         (float)g_cell_x[i], (float)(g_cell_y[i] + dy),
                         (float)g_cell[i].w, (float)g_cell[i].h, p);
    }
}

/* ---------------- compositing ---------------- */

void finnmenu_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnmenu_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnmenu_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    float a = v * FINN_MOTION_FADE;
    if (a > 1.0f) a = 1.0f;
    float k = FINN_MOTION_OPEN_SCALE + (1.0f - FINN_MOTION_OPEN_SCALE) * v;

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
        finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_hover, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

void finnmenu_press(int what) {
    g_press = what;
    if (what == FINNMENU_NONE) {
        g_press_idx = -1;
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    } else {
        if (what == FINNMENU_APP) g_press_idx = g_hover_idx;
        else if (what == FINNMENU_RESTART)  g_press_idx = FINN_APP_MAX;
        else if (what == FINNMENU_SHUTDOWN) g_press_idx = FINN_APP_MAX + 1;
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
    }
}
