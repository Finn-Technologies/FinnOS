/* finnwindow — the FinnOS window manager surface (Figma 12:2). */
#define _GNU_SOURCE
#include "finnwindow.h"
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
#include <strings.h>
#include <math.h>

#define WIN_GLASS       0.70f
#define WIN_PILL_GLASS  0.50f

/*
 * Metrics authored for the 1080p canvas, scaled from Figma 12:2 (1280x832 * 1.298077):
 *   Window: 500x300 r=15 -> 649.0 x 389.4 r=19.5
 *   Window Options pill: 72x24 r=8 at 8px inset from top/right ->
 *     93.5 x 31.2 r=10.4 at 10.4px inset
 *   3 icons inside Window Options pill: 16x16 (20.8), gap 6 (7.8), pad 6,4 (7.8, 5.2)
 *   Empty window label: "Empty window", 12px (15.6), FINN_INK at 70% opacity (0xB2)
 */
#define WIN_DEF_W       649.0f   /* reference 500 */
#define WIN_DEF_H       389.4f   /* reference 300 */
#define WIN_R            19.5f   /* reference 15  */

#define OPT_W            93.5f   /* reference 72  */
#define OPT_H            31.2f   /* reference 24  */
#define OPT_R            10.4f   /* reference 8   */
#define OPT_INSET        10.4f   /* reference 8   */
#define OPT_SLOT_CX      17.8f   /* reference 13.714 (6 + 15.429/2) */
#define OPT_ICON_EXP     11.7f   /* reference 9.00 ink inside 15.43 slot */
#define OPT_ICON_MIN     13.4f   /* reference 10.29 ink inside 15.43 slot */
#define OPT_ICON_CLS     10.0f   /* reference 7.71 ink inside 15.43 slot */

#define TITLEBAR_H       48.0f   /* drag region height */

typedef struct {
    int  id;
    int  used;
    char app_id[32];
    char title[64];
    int  x, y, w, h;
    int  saved_x, saved_y, saved_w, saved_h;
    int  maximized;
    int  minimized;
} finn_win;

static finn_win g_wins[FINNWINDOW_MAX];
static int      g_n_wins = 0;
static int      g_next_id = 1;

static fsvg g_ic_expand, g_ic_min, g_ic_close;
static int  g_ready = 0;

static finn_font g_f_title;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];
static char  g_icon_dir[512];
static char  g_registry[512];

static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx0 = 0, g_ly0 = 0;
static int g_screen_w = 1920, g_screen_h = 1080;
static float g_scale = 1.0f;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

static int g_ax = 0, g_ay = 0;

static finn_anim g_a_open;
static finn_anim g_a_hover, g_a_press;
static float     g_hover = 0.0f;
static int       g_hover_part = FINNWINDOW_HIT_NONE;
static int       g_hover_win = 0;
static int       g_press_part = FINNWINDOW_HIT_NONE;
static int       g_press_win = 0;

/* Dragging state */
static int g_dragging = 0;
static int g_drag_win = 0;
static int g_drag_dx = 0, g_drag_dy = 0;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_title);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_title, p, font_px(s, 15.6f)) != 0) {
        fprintf(stderr, "finnwindow: cannot load %s\n", p);
        return -1;
    }
    return 0;
}

int finnwindow_init(const char *icon_dir, const char *font_dir, const char *registry) {
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");
    snprintf(g_registry, sizeof g_registry, "%s",
             registry ? registry : FINN_APPS_CATALOGUE);

    char p[600];
    int ok = 1;
    snprintf(p, sizeof p, "%s/window_expand.svg", g_icon_dir);   ok &= fsvg_load(p, &g_ic_expand) == 0;
    snprintf(p, sizeof p, "%s/window_minimize.svg", g_icon_dir); ok &= fsvg_load(p, &g_ic_min) == 0;
    snprintf(p, sizeof p, "%s/window_close.svg", g_icon_dir);    ok &= fsvg_load(p, &g_ic_close) == 0;

    if (!ok) fprintf(stderr, "finnwindow: icon set incomplete in %s\n", g_icon_dir);
    g_ready = ok;
    return ok ? 0 : -1;
}

void finnwindow_free(void) {
    fsvg_free(&g_ic_expand);
    fsvg_free(&g_ic_min);
    fsvg_free(&g_ic_close);
    finn_font_free(&g_f_title);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_built = 0;
    g_font_scale = -1.0f;
    g_ready = 0;
}

/* ---------------- window array helpers ---------------- */

static int find_idx(int id) {
    if (id <= 0) {
        for (int i = g_n_wins - 1; i >= 0; i--)
            if (g_wins[i].used && !g_wins[i].minimized) return i;
        return g_n_wins > 0 ? g_n_wins - 1 : -1;
    }
    for (int i = 0; i < g_n_wins; i++)
        if (g_wins[i].used && g_wins[i].id == id) return i;
    return -1;
}

static void bring_to_front(int idx) {
    if (idx < 0 || idx >= g_n_wins - 1) return;
    finn_win tmp = g_wins[idx];
    for (int i = idx; i < g_n_wins - 1; i++)
        g_wins[i] = g_wins[i + 1];
    g_wins[g_n_wins - 1] = tmp;
}

int finnwindow_count(void) { return g_n_wins; }

int finnwindow_visible_count(void) {
    int c = 0;
    for (int i = 0; i < g_n_wins; i++)
        if (g_wins[i].used && !g_wins[i].minimized) c++;
    return c;
}

int finnwindow_visible(void) {
    return finn_anim_v(&g_a_open) > 0.004f && g_layer != NULL;
}

int finnwindow_open(const char *arg) {
    finn_apps_load(g_registry, NULL);

    /* Parse optional app=<id>, title=<title>, or positional argument */
    char app_id[32] = "";
    char title[64] = "";

    if (arg) {
        while (*arg == ' ') arg++;
        const char *ap = strstr(arg, "app=");
        const char *tp = strstr(arg, "title=");
        if (ap || tp) {
            if (ap) {
                ap += 4;
                int n = 0;
                while (ap[n] && ap[n] != ' ' && ap[n] != '\t' && n < (int)sizeof app_id - 1) {
                    app_id[n] = ap[n];
                    n++;
                }
                app_id[n] = 0;
            }
            if (tp) {
                tp += 6;
                int n = 0;
                while (tp[n] && n < (int)sizeof title - 1) {
                    /* stop if another key= token follows */
                    if (tp[n] == ' ' && strncmp(tp + n + 1, "app=", 4) == 0) break;
                    title[n] = tp[n];
                    n++;
                }
                title[n] = 0;
            }
        } else if (*arg && strcasecmp(arg, "empty") != 0 &&
                   strcasecmp(arg, "Empty window") != 0) {
            const finn_app *a = finn_apps_find(arg);
            if (a) {
                snprintf(app_id, sizeof app_id, "%s", a->id);
                snprintf(title, sizeof title, "%s", a->name);
            } else if (strcasecmp(arg, "settings") == 0 ||
                       strcasecmp(arg, "Finn Settings") == 0) {
                snprintf(app_id, sizeof app_id, "settings");
                snprintf(title, sizeof title, "Finn Settings");
            } else {
                snprintf(title, sizeof title, "%s", arg);
            }
        }
    }

    if (app_id[0] && !title[0]) {
        const finn_app *a = finn_apps_find(app_id);
        if (a) snprintf(title, sizeof title, "%s", a->name);
        else if (strcasecmp(app_id, "settings") == 0)
            snprintf(title, sizeof title, "Finn Settings");
        else
            snprintf(title, sizeof title, "%s", app_id);
    }

    /* If an app window for this app_id is already open, focus & unminimize it */
    if (app_id[0]) {
        for (int i = 0; i < g_n_wins; i++) {
            if (g_wins[i].used && strcmp(g_wins[i].app_id, app_id) == 0) {
                g_wins[i].minimized = 0;
                int id = g_wins[i].id;
                bring_to_front(i);
                g_built = 0;
                finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
                return id;
            }
        }
    }

    if (g_n_wins >= FINNWINDOW_MAX) {
        /* Reuse oldest slot */
        for (int i = 0; i < FINNWINDOW_MAX - 1; i++)
            g_wins[i] = g_wins[i + 1];
        g_n_wins = FINNWINDOW_MAX - 1;
    }

    int ww = rs(WIN_DEF_W, g_scale);
    int wh = rs(WIN_DEF_H, g_scale);
    if (ww < 300) { ww = 649; wh = 389; }

    /* Center exactly as in Figma 12:2 (with slight cascade when multiple windows are open) */
    int vis = finnwindow_visible_count();
    int offset = (vis % 4) * rs(24.0f, g_scale);
    int wx = (g_screen_w - ww) / 2 + offset;
    int wy = (g_screen_h - wh) / 2 + offset;

    finn_win *w = &g_wins[g_n_wins++];
    memset(w, 0, sizeof *w);
    w->id = g_next_id++;
    w->used = 1;
    snprintf(w->app_id, sizeof w->app_id, "%s", app_id);
    snprintf(w->title, sizeof w->title, "%s", title[0] ? title : "Empty window");
    w->x = wx; w->y = wy; w->w = ww; w->h = wh;
    w->saved_x = wx; w->saved_y = wy; w->saved_w = ww; w->saved_h = wh;

    g_built = 0;
    finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    return w->id;
}

int finnwindow_close(int id) {
    int idx = find_idx(id);
    if (idx < 0) return -1;
    for (int i = idx; i < g_n_wins - 1; i++)
        g_wins[i] = g_wins[i + 1];
    g_n_wins--;
    g_built = 0;
    if (finnwindow_visible_count() == 0)
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
    return 0;
}

int finnwindow_focus(int id) {
    int idx = find_idx(id);
    if (idx < 0) return -1;
    g_wins[idx].minimized = 0;
    bring_to_front(idx);
    g_built = 0;
    finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    return 0;
}

int finnwindow_minimize(int id) {
    int idx = find_idx(id);
    if (idx < 0) return -1;
    g_wins[idx].minimized = !g_wins[idx].minimized;
    g_built = 0;
    if (finnwindow_visible_count() == 0)
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
    else
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    return 0;
}

int finnwindow_maximize(int id) {
    int idx = find_idx(id);
    if (idx < 0) return -1;
    finn_win *w = &g_wins[idx];
    w->minimized = 0;
    if (!w->maximized) {
        w->saved_x = w->x; w->saved_y = w->y;
        w->saved_w = w->w; w->saved_h = w->h;
        int m = rs(24.0f, g_scale);
        int bottom = rs(115.0f, g_scale);
        w->x = m;
        w->y = m;
        w->w = g_screen_w - m * 2;
        w->h = g_screen_h - m - bottom;
        w->maximized = 1;
    } else {
        w->x = w->saved_x; w->y = w->saved_y;
        w->w = w->saved_w; w->h = w->saved_h;
        w->maximized = 0;
    }
    bring_to_front(idx);
    g_built = 0;
    return 0;
}

int finnwindow_move(int id, int x, int y) {
    int idx = find_idx(id);
    if (idx < 0) return -1;
    g_wins[idx].x = x;
    g_wins[idx].y = y;
    g_wins[idx].maximized = 0;
    g_built = 0;
    return 0;
}

void finnwindow_taskbar_toggle(void) {
    if (g_n_wins == 0) {
        finnwindow_open("window");
        return;
    }
    /* If any window is visible, minimize the top visible window; otherwise restore all */
    if (finnwindow_visible_count() > 0) {
        for (int i = g_n_wins - 1; i >= 0; i--) {
            if (g_wins[i].used && !g_wins[i].minimized) {
                g_wins[i].minimized = 1;
                break;
            }
        }
        g_built = 0;
        if (finnwindow_visible_count() == 0)
            finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
    } else {
        for (int i = 0; i < g_n_wins; i++)
            g_wins[i].minimized = 0;
        g_built = 0;
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    }
}

int finnwindow_list(char *buf, size_t cap) {
    int count = 0;
    for (int i = 0; i < g_n_wins; i++)
        if (g_wins[i].used) count++;

    int used = snprintf(buf, cap, "OK count=%d", count);
    if (used < 0 || (size_t)used >= cap) return -1;

    for (int i = 0; i < g_n_wins && (size_t)used + 96 < cap; i++) {
        const finn_win *w = &g_wins[i];
        if (!w->used) continue;
        const char *st = w->minimized ? "minimized"
                       : (w->maximized ? "maximized" : "normal");
        int n = snprintf(buf + used, cap - (size_t)used,
                         " [%d|%s|%s|%s]",
                         w->id,
                         w->app_id[0] ? w->app_id : "none",
                         st,
                         w->title);
        if (n < 0 || (size_t)(used + n) >= cap - 2) break;
        used += n;
    }
    snprintf(buf + used, cap - (size_t)used, "\n");
    return 0;
}

/* ---------------- animation & input ---------------- */

int finnwindow_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_hover, dt);
    ch |= finn_anim_tick(&g_a_press, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finnwindow_running(void) {
    return finn_anim_running(&g_a_open) ||
           finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_press);
}

static void opt_pill_rect(const finn_win *w, float s,
                          int *ox, int *oy, int *ow, int *oh) {
    int pw = rs(OPT_W, s);
    int ph = rs(OPT_H, s);
    int ins = rs(OPT_INSET, s);
    *ox = w->x + w->w - ins - pw;
    *oy = w->y + ins;
    *ow = pw;
    *oh = ph;
}

int finnwindow_hit(int px, int py, int *out_win_id) {
    if (out_win_id) *out_win_id = 0;
    if (finnwindow_visible_count() == 0 || finn_anim_v(&g_a_open) < 0.85f)
        return FINNWINDOW_HIT_NONE;

    /* Test top-to-bottom in z-order */
    for (int i = g_n_wins - 1; i >= 0; i--) {
        const finn_win *w = &g_wins[i];
        if (!w->used || w->minimized) continue;
        if (px < w->x || py < w->y || px >= w->x + w->w || py >= w->y + w->h)
            continue;

        if (out_win_id) *out_win_id = w->id;

        int ox, oy, ow, oh;
        opt_pill_rect(w, g_scale, &ox, &oy, &ow, &oh);
        if (px >= ox && py >= oy && px < ox + ow && py < oy + oh) {
            int rel = px - ox;
            if (rel < ow / 3)       return FINNWINDOW_HIT_EXPAND;
            if (rel < (ow * 2) / 3) return FINNWINDOW_HIT_MINIMIZE;
            return FINNWINDOW_HIT_CLOSE;
        }

        if (py < w->y + rs(TITLEBAR_H, g_scale))
            return FINNWINDOW_HIT_TITLEBAR;

        return FINNWINDOW_HIT_BODY;
    }
    return FINNWINDOW_HIT_NONE;
}

void finnwindow_hover_at(int px, int py) {
    int wid = 0;
    int part = finnwindow_hit(px, py, &wid);
    if (part != FINNWINDOW_HIT_EXPAND &&
        part != FINNWINDOW_HIT_MINIMIZE &&
        part != FINNWINDOW_HIT_CLOSE) {
        part = FINNWINDOW_HIT_NONE;
        wid = 0;
    }
    if (part == g_hover_part && wid == g_hover_win) return;
    g_hover_part = part;
    g_hover_win = wid;
    g_built = 0;
    if (part != FINNWINDOW_HIT_NONE)
        finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_hover, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

void finnwindow_press(int hit_part, int win_id, int px, int py) {
    int idx = find_idx(win_id);
    if (idx >= 0) {
        bring_to_front(idx);
        idx = g_n_wins - 1;
    }
    g_press_part = hit_part;
    g_press_win = win_id;
    g_built = 0;

    if (hit_part == FINNWINDOW_HIT_TITLEBAR && idx >= 0 && !g_wins[idx].maximized) {
        g_dragging = 1;
        g_drag_win = g_wins[idx].id;
        g_drag_dx = px - g_wins[idx].x;
        g_drag_dy = py - g_wins[idx].y;
    } else {
        g_dragging = 0;
    }

    if (hit_part >= FINNWINDOW_HIT_EXPAND && hit_part <= FINNWINDOW_HIT_CLOSE)
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
}

int finnwindow_drag(int px, int py) {
    if (!g_dragging) return 0;
    int idx = find_idx(g_drag_win);
    if (idx < 0) { g_dragging = 0; return 0; }
    int nx = px - g_drag_dx;
    int ny = py - g_drag_dy;
    int margin = rs(12.0f, g_scale);
    if (nx < margin) nx = margin;
    if (ny < margin) ny = margin;
    if (nx + g_wins[idx].w > g_screen_w - margin)
        nx = g_screen_w - margin - g_wins[idx].w;
    if (ny + g_wins[idx].h > g_screen_h - margin)
        ny = g_screen_h - margin - g_wins[idx].h;
    if (nx != g_wins[idx].x || ny != g_wins[idx].y) {
        g_wins[idx].x = nx;
        g_wins[idx].y = ny;
        g_built = 0;
        return 1;
    }
    return 0;
}

void finnwindow_release(void) {
    g_dragging = 0;
    g_press_part = FINNWINDOW_HIT_NONE;
    g_press_win = 0;
    g_built = 0;
    finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
}

/* ---------------- render ---------------- */

static uint32_t build_key(float s, int lw, int lh, const finn_state *st) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(lw); MIX(lh); MIX(g_lx0); MIX(g_ly0);
    MIX(st->dark + 2); MIX(st->wifi + 2); MIX(st->wired + 2);
    MIX(st->battery_pct + 2); MIX(st->volume + 2);
    MIX(g_n_wins);
    for (int i = 0; i < g_n_wins; i++) {
        MIX(g_wins[i].id);
        MIX(g_wins[i].x); MIX(g_wins[i].y);
        MIX(g_wins[i].w); MIX(g_wins[i].h);
        MIX(g_wins[i].minimized); MIX(g_wins[i].maximized);
        for (const char *p = g_wins[i].title; *p; p++) MIX(*p);
        for (const char *p = g_wins[i].app_id; *p; p++) MIX(*p);
    }
    MIX(g_hover_part); MIX(g_hover_win);
    MIX((int)(g_hover * 64.0f));
    MIX(g_press_part); MIX(g_press_win);
    MIX((int)(finn_anim_v(&g_a_press) * 256.0f));
    #undef MIX
    return h ? h : 1u;
}

void finnwindow_render(int screen_w, int screen_h, float scale,
                       int dock_x, int dock_y, int dock_w, int dock_h,
                       const finn_state *st) {
    (void)dock_x; (void)dock_y; (void)dock_w; (void)dock_h;
    if (!g_ready) return;

    g_screen_w = screen_w;
    g_screen_h = screen_h;
    g_scale = scale;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }

    int vis = finnwindow_visible_count();
    if (vis == 0 && !finnwindow_visible()) return;

    /* Compute union bounding box of visible windows */
    int min_x = screen_w, min_y = screen_h, max_x = 0, max_y = 0;
    for (int i = 0; i < g_n_wins; i++) {
        if (!g_wins[i].used || g_wins[i].minimized) continue;
        if (g_wins[i].x < min_x) min_x = g_wins[i].x;
        if (g_wins[i].y < min_y) min_y = g_wins[i].y;
        if (g_wins[i].x + g_wins[i].w > max_x) max_x = g_wins[i].x + g_wins[i].w;
        if (g_wins[i].y + g_wins[i].h > max_y) max_y = g_wins[i].y + g_wins[i].h;
    }

    if (vis > 0 && max_x > min_x && max_y > min_y) {
        g_lx0 = min_x;
        g_ly0 = min_y;
        g_ax = (min_x + max_x) / 2;
        g_ay = (min_y + max_y) / 2;
    } else {
        /* Keep last geometry while close animation finishes */
        if (!g_layer) return;
        return;
    }

    int lw = max_x - min_x;
    int lh = max_y - min_y;

    uint32_t key = build_key(scale, lw, lh, st);
    if (g_built && key == g_build_key && lw == g_lw && lh == g_lh) return;
    g_build_key = key;
    g_built = 1;
    g_rev++;

    if (lw != g_lw || lh != g_lh) {
        free(g_layer);
        g_layer = malloc((size_t)lw * lh * 4);
        g_lw = lw; g_lh = lh;
    }
    if (!g_layer) return;
    memset(g_layer, 0, (size_t)lw * lh * 4);

    float s = scale;
    float press_v = finn_anim_v(&g_a_press);

    for (int i = 0; i < g_n_wins; i++) {
        const finn_win *w = &g_wins[i];
        if (!w->used || w->minimized) continue;

        int wx = w->x - g_lx0;
        int wy = w->y - g_ly0;

        /* Window frosted glass body (Figma 12:2: r=15, frost 0.70) */
        finn_frost_rrect(g_layer, lw, lh, g_lx0, g_ly0,
                         (float)wx, (float)wy, (float)w->w, (float)w->h,
                         s * WIN_R, FINN_GLASS, WIN_GLASS);

        /* Top-right Window Options pill (Figma 12:2: 72x24 r=8, inset 8, frost 0.50) */
        int ox_abs, oy_abs, ow, oh;
        opt_pill_rect(w, s, &ox_abs, &oy_abs, &ow, &oh);
        int ox = ox_abs - g_lx0;
        int oy = oy_abs - g_ly0;

        finn_frost_rrect(g_layer, lw, lh, g_lx0, g_ly0,
                         (float)ox, (float)oy, (float)ow, (float)oh,
                         s * OPT_R, FINN_GLASS, WIN_PILL_GLASS);

        /* 3 buttons inside Window Options: expand (slot 0), minimize (slot 1), close (slot 2) */
        int slot_w = ow / 3;
        for (int b = 0; b < 3; b++) {
            int part = (b == 0) ? FINNWINDOW_HIT_EXPAND
                     : (b == 1) ? FINNWINDOW_HIT_MINIMIZE
                                : FINNWINDOW_HIT_CLOSE;
            float wash = 0.0f;
            if (g_hover_win == w->id && g_hover_part == part)
                wash += FINN_MOTION_HOVER_WASH * g_hover;
            if (g_press_win == w->id && g_press_part == part)
                wash += FINN_MOTION_PRESS_WASH * press_v;
            if (wash > 0.004f) {
                unsigned al = (unsigned)(wash * 255.0f + 0.5f);
                if (al > 255) al = 255;
                finn_fill_rrect(g_layer, lw, lh,
                                (float)(ox + b * slot_w), (float)oy,
                                (float)slot_w, (float)oh, s * OPT_R,
                                (FINN_INK & 0x00FFFFFFu) | (al << 24));
            }
        }

        /* Icon centers inside the 72x24 pill (Figma 12:2: 15.43x15.43 slots at pad 6,4 gap 6) */
        int cy_opt = oy + oh / 2;
        int cx_exp = ox + rs(OPT_SLOT_CX, s);
        int cx_min = ox + ow / 2;
        int cx_cls = ox + ow - rs(OPT_SLOT_CX, s);

        finn_icon_blit_ink(&g_ic_expand, g_layer, lw, lh, cx_exp, cy_opt,
                           rs(OPT_ICON_EXP, s), FINN_INK, FINN_INK, 0);
        finn_icon_blit_ink(&g_ic_min, g_layer, lw, lh, cx_min, cy_opt,
                           rs(OPT_ICON_MIN, s), FINN_INK, FINN_INK, 0);
        finn_icon_blit_ink(&g_ic_close, g_layer, lw, lh, cx_cls, cy_opt,
                           rs(OPT_ICON_CLS, s), FINN_INK, FINN_INK, 0);

        /* Centered window label (Figma 12:2: 12px Fixel Display Regular, FINN_INK at 70% opacity) */
        const char *label = (!w->title[0] ||
                             strcasecmp(w->title, "Empty window") == 0 ||
                             strcasecmp(w->title, "Finn Window") == 0)
                          ? "Empty window"
                          : w->title;
        float tw = finn_text_width(&g_f_title, label);
        float cap = finn_text_cap_height(&g_f_title);
        uint32_t col = (FINN_INK & 0x00FFFFFFu) | 0xB2000000u; /* 70% opacity */
        finn_text_draw(&g_f_title, g_layer, lw, lh,
                       (float)wx + ((float)w->w - tw) * 0.5f,
                       (float)wy + (float)w->h * 0.5f + cap * 0.5f,
                       label, col);
    }
}

void finnwindow_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnwindow_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnwindow_transform(float *x, float *y, float *w, float *h, float *alpha) {
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

void finnwindow_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_layer) return;
    float x, y, w, h, alpha;
    finnwindow_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_scaled(dst, dw, dh, g_layer, g_lw, g_lh, x, y, w, h, alpha);
}

const uint32_t *finnwindow_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx0;
    if (y) *y = g_ly0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_rev;
    return finnwindow_visible() ? g_layer : NULL;
}
