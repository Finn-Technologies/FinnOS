/* finnwindow — the FinnOS window manager surface (Figma 12:2). */
#define _GNU_SOURCE
#include "finnwindow.h"
#include "finnsettingsapp.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnapps.h"
#include "finnblur.h"
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/*
 * Metrics authored for the Figma design system (1280x832 * 1.298077):
 *   Window: 500x300 r=8 (Figma 14:247/14:236)
 *   Window Options pill: 72x24 r=4.14 at 10px inset from top/right (Figma 14:237)
 *   3 icons inside Window Options: expand, minus-sign, x, 15.43px (Figma 14:238/240/242)
 *   Empty window label: "Empty window", 12px (Figma 14:245), FINN_INK at 70% opacity
 *   Settings window: 900x500 centered at (190, 166) in 1280x832 (Figma 14:330)
 */
#define REF             1.298077f
#define WIN_DEF_W       (500.0f * REF)   /* reference 500 */
#define WIN_DEF_H       (300.0f * REF)   /* reference 300 */
#define WIN_R           (8.0f * REF)     /* reference 8   */

#define OPT_W           (72.0f * REF)    /* reference 72  */
#define OPT_H           (24.0f * REF)    /* reference 24  */
#define OPT_R           (4.14f * REF)    /* reference 4.14 */
#define OPT_INSET       (10.0f * REF)    /* reference 10  */
#define OPT_SLOT_CX     (12.0f * REF)    /* reference 12: the icon centres */
#define OPT_ICON_PX     (15.43f * REF)   /* reference 15.43: the icon's box */

/*
 * Figma gives a window 0.70 of the frost and its options pill 0.50.
 *
 * Neither is the shell's shared weight, and the difference is not an accident
 * of the file: a window floats over whatever happens to be behind it rather
 * than over a room the shell has already dimmed, so its body carries a little
 * more tint than a panel does. The pill is then a *lift* off that body, not an
 * inset in it — at the tile weight the pill composited to a near-white chip
 * with the glass gone out of it, which is what a sticker looks like next to a
 * window.
 */
#define WIN_FROST       0.60f
#define OPT_FROST       0.42f

#define TITLEBAR_H       48.0f   /* drag region height */

/* The least of a window's titlebar that has to stay on screen: a window is
 * moved by its titlebar, so a titlebar out of reach is a window that can
 * never be moved back. Reference units, like every other metric here. */
#define WIN_GRAB         40.0f

/*
 * The most a window layer may be: 32 Mi pixels, four 4K frames' worth and
 * 128 MB once each pixel is four bytes. A 4K screen with a window hanging
 * off each edge is under 20 Mi, so a box past this is a geometry bug, not a
 * frame worth allocating for.
 */
#define FINNWINDOW_LAYER_MAX_PIXELS (32u * 1024u * 1024u)

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

/* The window whose close is still dissolving, or 0. It keeps its place in the
 * array until the track lands — see finnwindow_close. */
static int      g_closing = 0;

static fsvg g_ic_expand, g_ic_min, g_ic_close;
static fsvg g_ic_search, g_ic_wifi, g_ic_bluetooth;
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

/* The active Window Options button, kept apart from the body so a hover wash
 * never repaints the glass around it. */
static uint32_t *g_ovl = NULL;
static int g_ovw = 0, g_ovh = 0, g_ovx0 = 0, g_ovy0 = 0;
static uint32_t g_ov_key = 0;
static int g_ov_built = 0, g_ov_show = 0;
static unsigned g_ov_rev = 0;

/* The dock's height at the bottom edge, as last reported by the compositor.
 * It is what a maximized window leaves for the dock. */
static int g_dock_clearance = 0;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

/* Set when a layer was refused or could not be allocated, so the reason is
 * printed once per episode instead of on every frame of it. */
static int g_layer_refused = 0;

static int g_ax = 0, g_ay = 0;

/* Maximize/unmaximize zoom: the layer rect travels between the old window and
 * the new one. Minimize adds a second leg, from the window to its dock tile. */
static finn_anim g_a_zoom, g_a_min;
static int   g_zoom_active = 0;
static float g_zfx, g_zfy, g_zfw, g_zfh, g_ztx, g_zty, g_ztw, g_zth;
static int   g_min_active = 0, g_min_win = 0;
static float g_mfx, g_mfy, g_mfw, g_mfh, g_mtx, g_mty, g_mtw, g_mth;

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

static int g_settings_hit = 0;
static int g_settings_sub = 0;
static finn_state g_last_st;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_title);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_title, p, font_px(s, 12.0f * REF)) != 0) {
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
    snprintf(p, sizeof p, "%s/expand.svg", g_icon_dir);         ok &= fsvg_load(p, &g_ic_expand) == 0;
    snprintf(p, sizeof p, "%s/minus-sign.svg", g_icon_dir);     ok &= fsvg_load(p, &g_ic_min) == 0;
    snprintf(p, sizeof p, "%s/x.svg", g_icon_dir);              ok &= fsvg_load(p, &g_ic_close) == 0;
    snprintf(p, sizeof p, "%s/search-01.svg", g_icon_dir);       fsvg_load(p, &g_ic_search);
    snprintf(p, sizeof p, "%s/wifi-02.svg", g_icon_dir);         fsvg_load(p, &g_ic_wifi);
    snprintf(p, sizeof p, "%s/bluetooth.svg", g_icon_dir);       fsvg_load(p, &g_ic_bluetooth);

    if (!ok) fprintf(stderr, "finnwindow: icon set incomplete in %s\n", g_icon_dir);
    g_ready = ok;
    finnsettingsapp_init(g_icon_dir, g_font_dir);
    return ok ? 0 : -1;
}

/*
 * One glass plate per window slot: a window that redraws its contents (a
 * keystroke in the search field, a page change) asks for the same frost over
 * and over, and on a maximized window that frost is millions of backdrop
 * samples. The plate is that work parked between rasters.
 */
static finn_frost_plate g_body_plate[FINNWINDOW_MAX];

void finnwindow_free(void) {
    finnsettingsapp_free();
    fsvg_free(&g_ic_expand);
    fsvg_free(&g_ic_min);
    fsvg_free(&g_ic_close);
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_wifi);
    fsvg_free(&g_ic_bluetooth);
    finn_font_free(&g_f_title);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_layer_refused = 0;
    free(g_ovl);
    g_ovl = NULL;
    g_ovw = g_ovh = 0;
    g_ov_built = g_ov_show = 0;
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

void finnwindow_set_dock_clearance(int clearance) {
    if (clearance < 0) clearance = 0;
    g_dock_clearance = clearance;
}

int finnwindow_overlaps_band(int y_top) {
    for (int i = 0; i < g_n_wins; i++) {
        const finn_win *w = &g_wins[i];
        if (!w->used || w->minimized) continue;
        if (w->maximized) return 1;
        if (w->y + w->h >= y_top) return 1;
    }
    return 0;
}

/*
 * The dock's resting top edge, in scanout pixels. The clearance the
 * compositor reports is the dock's height, and finndock_rest_top turns it
 * into the edge the dock answers for — the same edge finnwindow_overlaps_band
 * is asked about, so the drag limit and the dock's sink cannot disagree. A
 * clearance of zero means the dock has not been measured yet, and then the
 * screen's bottom edge is the only limit there is.
 */
static int dock_band_top(void) {
    if (g_dock_clearance <= 0) return g_screen_h;
    return finndock_rest_top(g_screen_h, g_scale);
}

/*
 * Keep a window where it can still be moved. The titlebar is the only part
 * that can be grabbed, so at least WIN_GRAB of its height and of its width
 * stays inside the screen and the rest of the window may hang off an edge.
 * The bottom of the titlebar stops at the dock's resting top: the dock is
 * drawn over the windows, so a titlebar under it is a titlebar out of reach,
 * even though the window's body may sit in that band.
 */
static void clamp_window_pos(const finn_win *w, int *x, int *y) {
    if (g_screen_w <= 0 || g_screen_h <= 0) return;

    int grab = rs(WIN_GRAB, g_scale);
    if (grab < 1) grab = 1;
    int tb = rs(TITLEBAR_H, g_scale);
    if (tb < grab) tb = grab;

    int xmin = grab - w->w;
    int xmax = g_screen_w - grab;
    if (xmax < xmin) xmax = xmin;

    int ymin = grab - tb;
    int ymax = dock_band_top() - tb;
    if (ymax < ymin) ymax = ymin;

    if (*x < xmin) *x = xmin;
    else if (*x > xmax) *x = xmax;
    if (*y < ymin) *y = ymin;
    else if (*y > ymax) *y = ymax;
}

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
                while (ap[n] && ap[n] != ' ' && ap[n] != '\t') {
                    /* An id that does not fit would open a window for an app
                     * nobody asked for, so the request is refused instead. */
                    if (n >= (int)sizeof app_id - 1) return -1;
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
                if (g_closing == id) g_closing = 0;
                bring_to_front(i);
                g_built = 0;
                finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
                return id;
            }
        }
    }

    /* A window that is still dissolving is gone as far as a new arrival is
     * concerned. Adding one beside it would drag the close back — the layer
     * would carry both, and the track meant to end the old window would end up
     * fading in the new one. */
    if (g_closing) {
        int ci = find_idx(g_closing);
        if (ci >= 0) {
            for (int i = ci; i < g_n_wins - 1; i++)
                g_wins[i] = g_wins[i + 1];
            g_n_wins--;
        }
        g_closing = 0;
        finn_anim_jump(&g_a_open, 0.0f);
        g_built = 0;
    }

    /* The limit is a refusal, not an eviction: the window that is already
     * open stays exactly where it is, and the caller is told why the new
     * one did not appear. Reusing the app_id above is not affected — that
     * request does not add a window. */
    if (g_n_wins >= FINNWINDOW_MAX) return FINNWINDOW_ERR_LIMIT;

    int is_settings = (strcasecmp(app_id, "settings") == 0 || strcasecmp(title, "Settings") == 0);
    int ww = is_settings ? rs(860.0f * REF, g_scale) : rs(WIN_DEF_W, g_scale);
    int wh = is_settings ? rs(480.0f * REF, g_scale) : rs(WIN_DEF_H, g_scale);
    if (ww < 300) { ww = is_settings ? 1168 : 649; wh = is_settings ? 649 : 389; }

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
    clamp_window_pos(w, &w->x, &w->y);
    w->saved_x = w->x; w->saved_y = w->y; w->saved_w = ww; w->saved_h = wh;

    g_built = 0;
    finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    return w->id;
}

/*
 * A window that leaves keeps its place in the array until the close track
 * lands.
 *
 * The dissolve animates the body layer, and it is the window's own pixels that
 * are in it. Removing the window first left the track running over glass that
 * no longer held it: the window vanished on the first frame, and the 180ms
 * that were meant to scatter it were spent dissolving an empty layer.
 *
 * Only the last visible window has those pixels to itself. With a stack behind
 * it the layer is the stack, and scaling or scattering it would take the
 * windows that are staying along with it, so there the window leaves at once.
 */
int finnwindow_close(int id) {
    int idx = find_idx(id);
    if (idx < 0) return -1;

    if (!g_closing && finnwindow_visible_count() == 1 &&
        finn_anim_v(&g_a_open) > 0.85f) {
        g_closing = id;
        g_built = 0;
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
        return 0;
    }

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
    /* Focus is the pointer's way back into a window, so a window that was
     * leaving is simply staying. */
    if (g_closing == id) g_closing = 0;
    g_wins[idx].minimized = 0;
    bring_to_front(idx);
    g_built = 0;
    finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    return 0;
}

int finnwindow_cycle_focus(int dir) {
    if (g_n_wins == 0) return -1;
    if (g_n_wins == 1) return finnwindow_focus(g_wins[0].id);

    /*
     * The array is stored in z-order, with the focused window at the top
     * (g_wins[g_n_wins - 1]). A forward step (Alt+Tab) wants to reveal the
     * window directly underneath; pushing the current top to the bottom
     * advances the whole stack by one without losing order across repeated
     * presses. A backward step (Shift+Alt+Tab) is the inverse permutation:
     * bringing the bottom window to the front with finnwindow_focus shifts
     * the rest down and restores the previous top.
     */
    if (dir >= 0) {
        finn_win top = g_wins[g_n_wins - 1];
        for (int i = g_n_wins - 1; i > 0; i--)
            g_wins[i] = g_wins[i - 1];
        g_wins[0] = top;
        return finnwindow_focus(g_wins[g_n_wins - 1].id);
    }
    return finnwindow_focus(g_wins[0].id);
}

int finnwindow_minimize(int id) {
    if (g_min_active) return 0;
    if (g_closing == id) return 0;
    int idx = find_idx(id);
    if (idx < 0) return -1;
    finn_win *w = &g_wins[idx];

    /* Fold into the taskbar tile: one visible window means the layer is that
     * window, so the shared rect can carry the move and the scale. */
    if (!w->minimized && finnwindow_visible_count() == 1 && !g_min_active) {
        int ax = 0, ay = 0;
        finndock_app_icon(&ax, &ay);
        int tw = rs(26.0f, g_scale), th = tw;
        g_mfx = (float)w->x; g_mfy = (float)w->y;
        g_mfw = (float)w->w; g_mfh = (float)w->h;
        g_mtx = (float)(ax - tw / 2); g_mty = (float)(ay - th / 2);
        g_mtw = (float)tw; g_mth = (float)th;
        g_min_active = 1;
        g_min_win = w->id;
        g_zoom_active = 0;
        finn_anim_jump(&g_a_min, 0.0f);
        finn_anim_to(&g_a_min, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_IN);
        return 0;
    }

    w->minimized = !w->minimized;
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
    int oldx = w->x, oldy = w->y, oldw = w->w, oldh = w->h;
    if (!w->maximized) {
        w->saved_x = w->x; w->saved_y = w->y;
        w->saved_w = w->w; w->saved_h = w->h;

        /*
         * A maximized window is the whole screen less the dock. There is no
         * floating margin left: the dock has already sunk flush to the
         * bottom edge by the time the window is resting, so the one band it
         * occupies is the one the window leaves it.
         */
        int bottom = g_dock_clearance > 0 ? g_dock_clearance
                                          : rs(115.0f, g_scale);
        w->x = 0;
        w->y = 0;
        w->w = g_screen_w;
        w->h = g_screen_h - bottom;
        if (w->h < 1) w->h = 1;
        w->maximized = 1;
    } else {
        w->x = w->saved_x; w->y = w->saved_y;
        w->w = w->saved_w; w->h = w->saved_h;
        w->maximized = 0;
    }
    bring_to_front(idx);
    g_built = 0;

    /* One visible window means the layer is that window, so the compositor's
     * rect can carry the whole zoom. With a stack behind it the window still
     * snaps, because moving the shared layer would drag the others along. */
    if (finnwindow_visible_count() == 1 && !g_min_active) {
        g_zfx = (float)oldx; g_zfy = (float)oldy;
        g_zfw = (float)oldw; g_zfh = (float)oldh;
        g_ztx = (float)w->x; g_zty = (float)w->y;
        g_ztw = (float)w->w; g_zth = (float)w->h;
        if (g_zfx != g_ztx || g_zfy != g_zty || g_zfw != g_ztw || g_zfh != g_zth) {
            finn_anim_jump(&g_a_zoom, 0.0f);
            finn_anim_to(&g_a_zoom, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT);
            g_zoom_active = 1;
        }
    }
    return 0;
}

int finnwindow_move(int id, int x, int y) {
    int idx = find_idx(id);
    if (idx < 0) return -1;
    finn_win *w = &g_wins[idx];

    /* The control plane may place a window anywhere it likes, except
     * somewhere the pointer cannot get it back. */
    clamp_window_pos(w, &x, &y);
    w->x = x;
    w->y = y;
    w->maximized = 0;
    g_built = 0;
    return 0;
}

void finnwindow_taskbar_toggle(void) {
    if (g_min_active) return;
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
    ch |= finn_anim_tick(&g_a_zoom, dt);
    ch |= finn_anim_tick(&g_a_min, dt);
    ch |= finnsettingsapp_tick(dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }

    if (g_zoom_active && !finn_anim_running(&g_a_zoom) &&
        finn_anim_v(&g_a_zoom) >= 0.999f) {
        g_zoom_active = 0;
    }

    if (g_min_active && !finn_anim_running(&g_a_min)) {
        int idx = find_idx(g_min_win);
        if (idx >= 0) g_wins[idx].minimized = 1;
        g_min_active = 0;
        g_min_win = 0;
        if (finnwindow_visible_count() == 0)
            finn_anim_jump(&g_a_open, 0.0f);
        g_built = 0;
        ch = 1;
    }

    /* The close track has landed, so the window it was dissolving can leave
     * the array. A window that came back while it was leaving is on its way
     * in again, and stays. */
    if (g_closing && !finn_anim_running(&g_a_open)) {
        if (finn_anim_v(&g_a_open) > 0.5f) {
            g_closing = 0;
        } else {
            int idx = find_idx(g_closing);
            if (idx >= 0) {
                for (int i = idx; i < g_n_wins - 1; i++)
                    g_wins[i] = g_wins[i + 1];
                g_n_wins--;
            }
            g_closing = 0;
            g_built = 0;
        }
        ch = 1;
    }
    return ch;
}

int finnwindow_running(void) {
    return finn_anim_running(&g_a_open) ||
           finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_press) ||
           finn_anim_running(&g_a_zoom) ||
           finn_anim_running(&g_a_min);
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
    /* The fold and the maximize zoom both draw an interpolated rect, so a
     * hit tested against the destination would land on a control that is
     * not where it is drawn. Nothing inside the rect takes a click until it
     * lands — and the hover clears with it, because hover_at asks this. */
    if (g_min_active || g_zoom_active) return FINNWINDOW_HIT_NONE;
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

        int is_settings = (strcasecmp(w->title, "Settings") == 0 ||
                           strcasecmp(w->app_id, "settings") == 0);
        if (is_settings) {
            int sub = 0;
            int sht = finnsettingsapp_hit(w->x, w->y, w->w, w->h, px, py, g_scale, &sub);
            if (sht == FINNSETTINGS_HIT_TITLEBAR)
                return FINNWINDOW_HIT_TITLEBAR;
            if (sht != FINNSETTINGS_HIT_NONE) {
                g_settings_hit = sht;
                g_settings_sub = sub;
                return FINNWINDOW_HIT_SETTINGS_CTRL;
            }
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
    g_ov_built = 0;
    if (part != FINNWINDOW_HIT_NONE)
        finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_hover, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

void finnwindow_press(int hit_part, int win_id, int px, int py) {
    int idx = find_idx(win_id);
    int raised = 0;
    if (idx >= 0) {
        raised = idx != g_n_wins - 1;
        bring_to_front(idx);
        idx = g_n_wins - 1;
    }
    g_press_part = hit_part;
    g_press_win = win_id;
    /* Only a real z-order change touches the body; the wash is an overlay. */
    if (raised) g_built = 0;
    g_ov_built = 0;

    if (hit_part == FINNWINDOW_HIT_TITLEBAR && idx >= 0 && !g_wins[idx].maximized) {
        g_dragging = 1;
        g_drag_win = g_wins[idx].id;
        g_drag_dx = px - g_wins[idx].x;
        g_drag_dy = py - g_wins[idx].y;
    } else {
        g_dragging = 0;
    }

    if (hit_part == FINNWINDOW_HIT_SETTINGS_CTRL && idx >= 0) {
        finnsettingsapp_press(g_settings_hit, g_settings_sub, px, py, g_scale, &g_last_st);
        g_built = 0;
    }

    if (hit_part >= FINNWINDOW_HIT_EXPAND && hit_part <= FINNWINDOW_HIT_CLOSE)
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
}

int finnwindow_drag(int px, int py) {
    int s_drag = finnsettingsapp_drag(px, py, g_scale, &g_last_st);
    if (s_drag) g_built = 0;

    if (!g_dragging) return s_drag;
    int idx = find_idx(g_drag_win);
    if (idx < 0) { g_dragging = 0; return s_drag; }
    int nx = px - g_drag_dx;
    int ny = py - g_drag_dy;

    /* Pointer dragging and window.move share one rule: the titlebar stays
     * grabbable on screen and stops at the dock's resting top. */
    clamp_window_pos(&g_wins[idx], &nx, &ny);

    if (nx != g_wins[idx].x || ny != g_wins[idx].y) {
        g_wins[idx].x = nx;
        g_wins[idx].y = ny;
        g_built = 0;
        return 1;
    }
    return s_drag;
}

void finnwindow_release(void) {
    g_dragging = 0;
    g_press_part = FINNWINDOW_HIT_NONE;
    g_press_win = 0;
    g_ov_built = 0;
    finnsettingsapp_release();
    finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
}

int finnwindow_key(const finn_key_event *e) {
    int res = finnsettingsapp_key(e);
    if (res) g_built = 0;
    return res;
}

/* ---------------- render ---------------- */

/* The body key deliberately leaves out the pointer: the hover and press
 * washes live on the overlay below, and the body's own pixels do not move
 * when the pointer does. It also leaves out the status fields — the window
 * glass does not draw Wi-Fi, battery or volume, and a volume-slider drag
 * must not repaint a full-screen window sixty times a second. */
static uint32_t build_key(float s, int lw, int lh, const finn_state *st) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(lw); MIX(lh); MIX(g_lx0); MIX(g_ly0);
    MIX(st->dark + 2);
    MIX(g_n_wins);
    for (int i = 0; i < g_n_wins; i++) {
        MIX(g_wins[i].id);
        MIX(g_wins[i].x); MIX(g_wins[i].y);
        MIX(g_wins[i].w); MIX(g_wins[i].h);
        MIX(g_wins[i].minimized); MIX(g_wins[i].maximized);
        for (const char *p = g_wins[i].title; *p; p++) MIX(*p);
        for (const char *p = g_wins[i].app_id; *p; p++) MIX(*p);
        if (strcasecmp(g_wins[i].title, "Settings") == 0 ||
            strcasecmp(g_wins[i].app_id, "settings") == 0) {
            MIX(finnsettingsapp_build_key(s, st));
        }
    }
    #undef MIX
    return h ? h : 1u;
}

/* The button the pointer is on, if any. A press wins over a hover because it
 * is the newer intent and the pressed window has just been raised. */
static int active_option(int *out_idx, int *out_part) {
    int id = 0, part = FINNWINDOW_HIT_NONE;
    if (g_press_win > 0 && g_press_part >= FINNWINDOW_HIT_EXPAND &&
        g_press_part <= FINNWINDOW_HIT_CLOSE) {
        id = g_press_win; part = g_press_part;
    } else if (g_hover_win > 0 && g_hover_part >= FINNWINDOW_HIT_EXPAND &&
               g_hover_part <= FINNWINDOW_HIT_CLOSE) {
        id = g_hover_win; part = g_hover_part;
    } else {
        return 0;
    }

    int idx = find_idx(id);
    if (idx < 0 || !g_wins[idx].used || g_wins[idx].minimized) return 0;
    *out_idx = idx;
    *out_part = part;
    return 1;
}

static uint32_t overlay_key(float s, const finn_state *st, int idx, int part,
                            int slot_w, int slot_h) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(st->dark + 2);
    MIX(g_wins[idx].id); MIX(part); MIX(slot_w); MIX(slot_h);
    MIX((int)(g_hover * 256.0f));
    MIX((int)(finn_anim_v(&g_a_press) * 256.0f));
    #undef MIX
    return h ? h : 1u;
}

/*
 * The three controls are drawn in their own ink.
 *
 * The assets already carry Figma's stroke — a navy (#141B34), not the neutral
 * the labels use — so on the light glass they are left untinted and read as
 * ink on the window rather than as grey furniture beside it. The dark
 * appearance has no navy to fall back on, because there the glass is the deep
 * bone; there the controls take the same ink as the text.
 *
 * They are placed by their 15.43px box and not by their ink: minus-sign and x
 * cover different fractions of that box, and centring each by its own ink
 * would space the three unevenly across the pill.
 */
static void opt_icon_blit(fsvg *svg, uint32_t *dst, int dw, int dh,
                          int cx, int cy, float s) {
    if (!svg || svg->vw <= 0.0f) return;
    int px = rs(OPT_ICON_PX, s);
    if (px < 1) px = 1;
    uint32_t ink = finn_theme_dark() ? FINN_INK : 0u;
    finn_icon_blit(svg, dst, dw, dh, cx - px / 2, cy - px / 2, px,
                   ink, ink, 0);
}

/* Draw the active button's wash and its icon into the overlay layer. The body
 * still carries the same icon (so the other two buttons are untouched), and
 * this copy sits over its identical twin: the wash stays under the ink, just
 * as it did when both lived in the body. */
static void render_overlay(int idx, int part, float s) {
    const finn_win *w = &g_wins[idx];
    int ox, oy, ow, oh;
    opt_pill_rect(w, s, &ox, &oy, &ow, &oh);
    int slot_w = ow / 3;
    int b = part - FINNWINDOW_HIT_EXPAND;
    int lx = ox + b * slot_w - g_ovx0;
    int ly = oy - g_ovy0;

    float wash = 0.0f;
    if (g_hover_win == w->id && g_hover_part == part)
        wash += FINN_MOTION_HOVER_WASH * g_hover;
    if (g_press_win == w->id && g_press_part == part)
        wash += FINN_MOTION_PRESS_WASH * finn_anim_v(&g_a_press);
    if (wash > 0.004f) {
        unsigned al = (unsigned)(wash * 255.0f + 0.5f);
        if (al > 255) al = 255;
        finn_fill_rrect(g_ovl, g_ovw, g_ovh, (float)lx, (float)ly,
                        (float)slot_w, (float)oh, s * OPT_R,
                        (FINN_INK & 0x00FFFFFFu) | (al << 24));
    }

    int cy = oy + oh / 2 - g_ovy0;
    int cx;
    if (part == FINNWINDOW_HIT_EXPAND) {
        cx = ox + rs(OPT_SLOT_CX, s) - g_ovx0;
        opt_icon_blit(&g_ic_expand, g_ovl, g_ovw, g_ovh, cx, cy, s);
    } else if (part == FINNWINDOW_HIT_MINIMIZE) {
        cx = ox + ow / 2 - g_ovx0;
        opt_icon_blit(&g_ic_min, g_ovl, g_ovw, g_ovh, cx, cy, s);
    } else {
        cx = ox + ow - rs(OPT_SLOT_CX, s) - g_ovx0;
        opt_icon_blit(&g_ic_close, g_ovl, g_ovw, g_ovh, cx, cy, s);
    }
}

/*
 * The union box is the layer's size, and the layer is the one allocation in
 * this module that a position can grow without bound. `lw*lh` is checked in
 * 64 bits before either factor becomes an int, and the cap is the line
 * between a large frame and a geometry bug. The refusal is reported once:
 * a bad position would otherwise print on every frame it lasted.
 */
static int layer_fits(int64_t lw, int64_t lh) {
    if (lw > 0 && lh > 0 && lw * lh <= (int64_t)FINNWINDOW_LAYER_MAX_PIXELS)
        return 1;
    if (!g_layer_refused) {
        fprintf(stderr, "finnwindow: refusing a %lldx%lld window layer "
                        "(cap %u pixels)\n",
                (long long)lw, (long long)lh,
                (unsigned)FINNWINDOW_LAYER_MAX_PIXELS);
        g_layer_refused = 1;
    }
    return 0;
}

/*
 * Make the body layer the size of the union box. The new raster is taken
 * beside the old one and swapped in only once it is there, so a refused or
 * failed allocation leaves the last frame exactly as it was rather than
 * blanking the screen.
 */
static int layer_reserve(int lw, int lh) {
    if (g_layer && lw == g_lw && lh == g_lh) return 1;
    if (!layer_fits(lw, lh)) return 0;

    uint32_t *p = malloc((size_t)lw * (size_t)lh * 4);
    if (!p) {
        if (!g_layer_refused) {
            fprintf(stderr, "finnwindow: cannot allocate a %dx%d window layer\n",
                    lw, lh);
            g_layer_refused = 1;
        }
        return 0;
    }

    free(g_layer);
    g_layer = p;
    g_lw = lw;
    g_lh = lh;
    g_layer_refused = 0;
    return 1;
}

void finnwindow_render(int screen_w, int screen_h, float scale,
                       int dock_x, int dock_y, int dock_w, int dock_h,
                       const finn_state *st) {
    (void)dock_x; (void)dock_y; (void)dock_w; (void)dock_h;
    if (!g_ready) return;

    g_screen_w = screen_w;
    g_screen_h = screen_h;
    g_scale = scale;
    if (st) g_last_st = *st;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }

    int vis = finnwindow_visible_count();
    if (vis == 0 && !finnwindow_visible()) return;

    /*
     * The union bounding box of the visible windows. It is int64 because a
     * window may hang off an edge by all but its titlebar, so a corner is
     * not inside the screen and the sum of a position and a size is not a
     * position an int has to hold: the box is measured first and refused if
     * it is not a frame, rather than allowed to wrap into a negative one.
     */
    int64_t min_x = screen_w, min_y = screen_h, max_x = 0, max_y = 0;
    for (int i = 0; i < g_n_wins; i++) {
        if (!g_wins[i].used || g_wins[i].minimized) continue;
        int64_t x0 = g_wins[i].x, y0 = g_wins[i].y;
        int64_t x1 = x0 + g_wins[i].w, y1 = y0 + g_wins[i].h;
        if (x0 < min_x) min_x = x0;
        if (y0 < min_y) min_y = y0;
        if (x1 > max_x) max_x = x1;
        if (y1 > max_y) max_y = y1;
    }

    if (vis <= 0 || max_x <= min_x || max_y <= min_y) {
        /* Keep last geometry while close animation finishes */
        return;
    }

    int64_t uw = max_x - min_x;
    int64_t uh = max_y - min_y;
    if (!layer_fits(uw, uh)) return;

    /* The cap above is what makes these two safe to narrow. */
    int lw = (int)uw;
    int lh = (int)uh;

    int old_lx0 = g_lx0, old_ly0 = g_ly0;
    int old_ax = g_ax, old_ay = g_ay;
    g_lx0 = (int)min_x;
    g_ly0 = (int)min_y;
    g_ax = (int)((min_x + max_x) / 2);
    g_ay = (int)((min_y + max_y) / 2);

    uint32_t key = build_key(scale, lw, lh, st);
    int body_dirty = !g_built || key != g_build_key || lw != g_lw || lh != g_lh;

    /* The active control and the slot it occupies. Its layer is tiny, so a
     * hover can be rebuilt every frame without touching the glass. */
    int oi = -1, op = FINNWINDOW_HIT_NONE;
    int has_ov = active_option(&oi, &op);
    int nb_x0 = 0, nb_y0 = 0, nb_w = 0, nb_h = 0;
    uint32_t okey = 0;
    int ov_dirty = 0;
    if (has_ov) {
        int ox, oy, ow, oh;
        opt_pill_rect(&g_wins[oi], scale, &ox, &oy, &ow, &oh);
        int slot_w = ow / 3;
        int b = op - FINNWINDOW_HIT_EXPAND;
        nb_x0 = ox + b * slot_w - 2;
        nb_y0 = oy - 2;
        nb_w = slot_w + 4;
        nb_h = oh + 4;
        okey = overlay_key(scale, st, oi, op, slot_w, oh);
        ov_dirty = !g_ov_built || okey != g_ov_key ||
                   nb_x0 != g_ovx0 || nb_y0 != g_ovy0 ||
                   nb_w != g_ovw || nb_h != g_ovh;
    } else {
        ov_dirty = g_ov_show;
    }

    if (!body_dirty && !ov_dirty) return;

    if (body_dirty) {
        if (!layer_reserve(lw, lh)) {
            /* The last frame stays where it was drawn: the union moved, but
             * the layer it would have gone into does not exist. */
            g_lx0 = old_lx0; g_ly0 = old_ly0;
            g_ax = old_ax; g_ay = old_ay;
            return;
        }
        g_build_key = key;
        g_built = 1;
        g_rev++;

        memset(g_layer, 0, (size_t)lw * (size_t)lh * 4);

        /*
         * A window frosts what is under it, and what is under it is the
         * desktop — not the desktop with this same window already on it. The
         * backdrop is put back to the surfaces drawn so far before anything
         * is rastered, or the glass samples its own picture back out and the
         * window reads as a pale ghost of itself. Each window then bakes
         * itself in below, so the next one in z-order frosts this one.
         */
        finnblur_reset();

        float s = scale;

        for (int i = 0; i < g_n_wins; i++) {
            const finn_win *w = &g_wins[i];
            if (!w->used || w->minimized) continue;

            int wx = w->x - g_lx0;
            int wy = w->y - g_ly0;

            /* Window frosted glass body (Figma 14:236: r=8, frost 0.70). A
             * maximized window is the screen above the dock, so it has no
             * corners to round: a rounded corner there would leave a sliver of
             * wallpaper between the window and the dock's bar. */
            float body_r = w->maximized ? 0.0f : s * WIN_R;
            finn_frost_rrect_plate(&g_body_plate[i], g_layer, lw, lh, g_lx0, g_ly0,
                                   (float)wx, (float)wy, (float)w->w, (float)w->h,
                                   body_r, FINN_GLASS, WIN_FROST);
            finn_stroke_rrect(g_layer, lw, lh, (float)wx, (float)wy,
                              (float)w->w, (float)w->h, body_r, 1.0f, FINN_HAIR);

            /* Top-right Window Options pill (Figma 14:237: 72x24 r=4.14,
             * inset 10, frost 0.50) */
            int ox_abs, oy_abs, ow, oh;
            opt_pill_rect(w, s, &ox_abs, &oy_abs, &ow, &oh);
            int ox = ox_abs - g_lx0;
            int oy = oy_abs - g_ly0;

            finn_frost_rrect(g_layer, lw, lh, g_lx0, g_ly0,
                             (float)ox, (float)oy, (float)ow, (float)oh,
                             s * OPT_R, FINN_GLASS, OPT_FROST);
            finn_stroke_rrect(g_layer, lw, lh, (float)ox, (float)oy,
                              (float)ow, (float)oh, s * OPT_R, 1.0f, FINN_HAIR);

            /* Icon centres inside the 72x24 pill (Figma 14:238/240/242:
             * 15.43px boxes at 12, 36 and 60 from the pill's left edge) */
            int cy_opt = oy + oh / 2;
            int cx_exp = ox + rs(OPT_SLOT_CX, s);
            int cx_min = ox + ow / 2;
            int cx_cls = ox + ow - rs(OPT_SLOT_CX, s);

            opt_icon_blit(&g_ic_expand, g_layer, lw, lh, cx_exp, cy_opt, s);
            opt_icon_blit(&g_ic_min, g_layer, lw, lh, cx_min, cy_opt, s);
            opt_icon_blit(&g_ic_close, g_layer, lw, lh, cx_cls, cy_opt, s);

            if (strcasecmp(w->title, "Settings") == 0 || strcasecmp(w->app_id, "settings") == 0) {
                int focused = (i == g_n_wins - 1);
                finnsettingsapp_render(g_layer, lw, lh, g_lx0, g_ly0,
                                       w->x, w->y, w->w, w->h,
                                       screen_w, screen_h,
                                       s, st, focused);
            } else {
                /* Centered window label (Figma 14:245: 12px Fixel Display Regular, FINN_INK at 70% opacity) */
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

            /*
             * The window is drawn, so it is now part of what is behind the
             * next one. Baking it here — window by window, in z-order — is
             * what lets a window frost another window instead of frosting
             * the wallpaper through it. The body is opaque over its whole
             * rect, so the blur can read the layer directly.
             */
            finnblur_patch(g_layer, lw, lh, g_lx0, g_ly0,
                           w->x, w->y, w->w, w->h);
        }
    }

    if (has_ov) {
        if (nb_w != g_ovw || nb_h != g_ovh) {
            free(g_ovl);
            g_ovl = malloc((size_t)nb_w * nb_h * 4);
            g_ovw = nb_w; g_ovh = nb_h;
        }
        if (!g_ovl) { g_ov_show = 0; g_ov_built = 0; return; }
        g_ovx0 = nb_x0; g_ovy0 = nb_y0;
        g_ov_key = okey;
        g_ov_built = 1;
        g_ov_show = 1;
        g_ov_rev++;
        memset(g_ovl, 0, (size_t)g_ovw * g_ovh * 4);
        render_overlay(oi, op, scale);
    } else {
        g_ov_show = 0;
        g_ov_built = 0;
    }
}

void finnwindow_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnwindow_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnwindow_transform(float *x, float *y, float *w, float *h, float *alpha) {
    if (g_min_active) {
        float m = finn_anim_v(&g_a_min);
        float a = m < 0.72f ? 1.0f : (1.0f - m) / 0.28f;
        if (a < 0.0f) a = 0.0f;
        if (x) *x = g_mfx + (g_mtx - g_mfx) * m;
        if (y) *y = g_mfy + (g_mty - g_mfy) * m;
        if (w) *w = g_mfw + (g_mtw - g_mfw) * m;
        if (h) *h = g_mfh + (g_mth - g_mfh) * m;
        if (alpha) *alpha = a;
        return;
    }
    if (g_zoom_active) {
        float m = finn_anim_v(&g_a_zoom);
        if (x) *x = g_zfx + (g_ztx - g_zfx) * m;
        if (y) *y = g_zfy + (g_zty - g_zfy) * m;
        if (w) *w = g_zfw + (g_ztw - g_zfw) * m;
        if (h) *h = g_zfh + (g_zth - g_zfh) * m;
        if (alpha) *alpha = 1.0f;
        return;
    }

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
    finn_blit_dissolve(dst, dw, dh, g_layer, g_lw, g_lh,
                       x, y, w, h, alpha, finnwindow_dust());
}

float finnwindow_dust(void) {
    if (g_a_open.to > 0.5f) return 0.0f;
    float d = 1.0f - finn_anim_v(&g_a_open);
    return d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
}

const uint32_t *finnwindow_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx0;
    if (y) *y = g_ly0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_rev;
    return finnwindow_visible() ? g_layer : NULL;
}

void finnwindow_overlay_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float lx, ly, lw, lh, la;
    finnwindow_transform(&lx, &ly, &lw, &lh, &la);
    float sx = g_lw > 0 ? lw / (float)g_lw : 1.0f;
    float sy = g_lh > 0 ? lh / (float)g_lh : 1.0f;

    if (x) *x = lx + ((float)g_ovx0 - (float)g_lx0) * sx;
    if (y) *y = ly + ((float)g_ovy0 - (float)g_ly0) * sy;
    if (w) *w = (float)g_ovw * sx;
    if (h) *h = (float)g_ovh * sy;
    if (alpha) *alpha = la;
}

void finnwindow_overlay_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_ov_show || !g_ovl) return;
    float x, y, w, h, alpha;
    finnwindow_overlay_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_dissolve(dst, dw, dh, g_ovl, g_ovw, g_ovh,
                       x, y, w, h, alpha, finnwindow_dust());
}

const uint32_t *finnwindow_overlay_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_ovx0;
    if (y) *y = g_ovy0;
    if (w) *w = g_ovw;
    if (h) *h = g_ovh;
    if (rev) *rev = g_ov_rev;
    return (g_ov_show && finnwindow_visible()) ? g_ovl : NULL;
}
