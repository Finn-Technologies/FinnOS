/*
 * finnstoreapp — the Finn Store view.
 *
 * There is no client surface protocol in this build, so the Store is another
 * view inside the compositor, like Settings and Files. That puts it in the
 * one process that owns the screen; it does not put it in charge of packages.
 *
 * Three rules hold the view together:
 *
 *   - The catalogue is read from the OS registry, never from a list typed
 *     into this file. An app that is not in the registry is not for sale,
 *     and an app the registry knows but this machine lacks the launcher for
 *     is shown as unavailable rather than as a button that would fail.
 *
 *   - Installed is finnpkg's answer. The view re-reads the install list after
 *     every action instead of flipping a local flag and hoping.
 *
 *   - The action goes through finnd. The Store asks the control plane to
 *     install; it does not edit /var/lib/finnpkg/installed itself, because
 *     then there would be two package managers and only one audit log.
 */
#define _GNU_SOURCE
#include "finnstoreapp.h"
#include "finnappui.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnapps.h"
#include "finnsock.h"
#include "finnproto.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <linux/input.h>

/* ---------------- metrics, in reference units ---------------- */

#define PAD           18.0f
#define HEADER_TOP    56.0f
#define FILTER_W     200.0f
#define FILTER_H      28.0f
#define CONTENT_TOP   112.0f
#define FOOTER_H      26.0f
#define PANE_LABEL_H  20.0f
#define ROW_H         54.0f
#define ROW_ACTION_W  72.0f
#define ROW_ACTION_H  24.0f
#define DETAIL_GAP    14.0f
#define DETAIL_W     300.0f
#define DETAIL_PAD    18.0f
#define DETAIL_ICON   52.0f
#define ACTION_H      34.0f

typedef struct {
    fsvg svg;
    int  loaded;
    char id[32];
} store_icon;

/* ---------------- state ---------------- */

static char g_font_dir[512] = "";
static char g_icon_dir[512] = "";
static char g_registry[512] = "";

static finn_font g_f_title, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static fsvg g_ic_search;
static int g_ready = 0;

static store_icon g_icons[FINN_APP_MAX];

static char g_filter[64] = "";
static int  g_filter_focused = 0;
static char g_sel_id[32] = "";
static int  g_scroll = 0;
static int  g_confirm_remove = 0;
static char g_status[512] = "";
static int  g_status_error = 0;
static int  g_last_rows = 1;
static unsigned g_rev = 1;

static void bump(void) { g_rev++; }

static int load_fonts(float scale) {
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_title, p, finnapp_font_px(scale, 15.0f)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, finnapp_font_px(scale, 12.0f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, finnapp_font_px(scale, 10.0f)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, finnapp_font_px(scale, 8.5f)) != 0) return -1;
    return 0;
}

/* ---------------- catalogue helpers ---------------- */

static int find_index(const char *id) {
    if (!id || !*id) return -1;
    int n = finn_apps_count();
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (a && !strcmp(a->id, id)) return i;
    }
    return -1;
}

/*
 * A catalogue entry is usable only when its launcher is actually executable.
 * finnpkg refuses to mark an app installed without one, and a Store button
 * that only produces that refusal would be pretending to be a capability.
 */
static int app_available(const finn_app *a) {
    return a && a->command[0] && access(a->command, X_OK) == 0;
}

static int matches(int i) {
    int n = finn_apps_count();
    if (i < 0 || i >= n) return 0;
    if (!g_filter[0]) return 1;
    const finn_app *a = finn_apps_at(i);
    if (!a) return 0;
    return strcasestr(a->name, g_filter) != NULL ||
           strcasestr(a->id, g_filter) != NULL ||
           strcasestr(a->note, g_filter) != NULL;
}

static int visible_count(void) {
    int n = finn_apps_count(), k = 0;
    for (int i = 0; i < n; i++) if (matches(i)) k++;
    return k;
}

static int visible_at(int k) {
    int n = finn_apps_count();
    if (k < 0) return -1;
    for (int i = 0; i < n; i++) {
        if (!matches(i)) continue;
        if (k-- == 0) return i;
    }
    return -1;
}

static int visible_index_of(int i) {
    if (i < 0) return -1;
    int n = finn_apps_count(), k = 0;
    for (int j = 0; j < n; j++) {
        if (!matches(j)) continue;
        if (j == i) return k;
        k++;
    }
    return -1;
}

static const finn_app *selected_app(void) {
    int i = find_index(g_sel_id);
    return i >= 0 ? finn_apps_at(i) : NULL;
}

static void ensure_selection(void) {
    int i = find_index(g_sel_id);
    if (i >= 0 && matches(i)) return;

    int first = visible_at(0);
    if (first >= 0) {
        const finn_app *a = finn_apps_at(first);
        snprintf(g_sel_id, sizeof g_sel_id, "%s", a->id);
    } else {
        g_sel_id[0] = '\0';
    }
    g_confirm_remove = 0;
}

static void reload_catalogue(void) {
    finn_apps_load(g_registry, NULL);
    ensure_selection();
    bump();
}

/* ---------------- action ---------------- */

static void set_status(const char *text, int error) {
    snprintf(g_status, sizeof g_status, "%s", text ? text : "");
    g_status_error = error ? 1 : 0;
    bump();
}

static void do_action(void) {
    const finn_app *a = selected_app();
    if (!a) return;

    if (!a->installed && !app_available(a)) {
        set_status("That app's launcher is not on this machine, so it cannot be installed.",
                   1);
        return;
    }

    if (a->installed && !g_confirm_remove) {
        g_confirm_remove = 1;
        char msg[256];
        snprintf(msg, sizeof msg,
                 "Press Remove again to take %s off this machine.", a->name);
        set_status(msg, 0);
        return;
    }

    int install = !a->installed;
    char req[96];
    snprintf(req, sizeof req, "%s %s", install ? "app.install" : "app.remove", a->id);

    char reply[512];
    if (finn_call(FINN_CONTROL_SOCK, req, reply, sizeof reply) != 0) {
        set_status("The control plane is not reachable, so nothing was changed.", 1);
        return;
    }

    if (strncmp(reply, "OK", 2) != 0) {
        const char *why = reply + 3;
        while (*why == ' ') why++;
        set_status(*why ? why : "The package manager refused the change.", 1);
        return;
    }

    char name[64];
    snprintf(name, sizeof name, "%s", a->name);
    g_confirm_remove = 0;
    reload_catalogue();

    char msg[256];
    snprintf(msg, sizeof msg, "%s %s.", install ? "Installed" : "Removed", name);
    set_status(msg, 0);
}

/* ---------------- icons ---------------- */

static void free_icons(void) {
    for (int i = 0; i < FINN_APP_MAX; i++) {
        if (g_icons[i].loaded) fsvg_free(&g_icons[i].svg);
        g_icons[i].loaded = 0;
        g_icons[i].id[0] = '\0';
    }
}

static fsvg *icon_for(int i, const finn_app *a) {
    if (!a || i < 0 || i >= FINN_APP_MAX) return NULL;
    store_icon *ic = &g_icons[i];

    if (ic->loaded && !strcmp(ic->id, a->id)) return &ic->svg;
    if (ic->loaded) { fsvg_free(&ic->svg); ic->loaded = 0; }
    ic->id[0] = '\0';

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

static void draw_app_icon(uint32_t *L, int lw, int lh, const finn_app *a, int i,
                          float x, float y, float sz, float s) {
    (void)s;
    fsvg *ic = icon_for(i, a);
    if (ic && ic->vw > 0) {
        finn_icon_blit(ic, L, lw, lh, (int)lroundf(x), (int)lroundf(y),
                       (int)lroundf(sz), 0, 0, 0);
        return;
    }

    /* No asset is not an empty square: the tile still has to say which app it
     * belongs to, so the first letter of the name stands in. */
    finn_fill_rrect(L, lw, lh, x, y, sz, sz, sz * 0.22f, FINN_TILE_ON);
    char initial[2] = { a && a->name[0] ? a->name[0] : '?', 0 };
    float cap = finn_text_cap_height(&g_f_label);
    float tw = finn_text_width(&g_f_label, initial);
    finn_text_draw(&g_f_label, L, lw, lh,
                   x + (sz - tw) * 0.5f, y + sz * 0.5f + cap * 0.5f,
                   initial, FINN_ON);
}

/* ---------------- layout ---------------- */

typedef struct {
    float pad;
    float title_x, title_base;
    float filter_x, filter_y, filter_w, filter_h;
    float top, bottom, cw;
    int   wide;
    float list_x, list_y, list_w, label_y, rows_y, rows_h;
    int   rows;
    float detail_x, detail_y, detail_w, detail_h;
    float action_x, action_y, action_w, action_h;
    float foot_y;
} store_layout;

static void layout_for(int ww, int wh, float s, store_layout *L) {
    memset(L, 0, sizeof *L);
    L->pad = s * PAD;
    L->title_x = L->pad;
    L->title_base = s * HEADER_TOP + 18.0f * s;
    L->top = s * CONTENT_TOP;
    L->bottom = (float)wh - s * FOOTER_H;
    if (L->bottom < L->top + s * 80.0f) L->bottom = L->top + s * 80.0f;
    L->cw = (float)ww - L->pad * 2.0f;
    if (L->cw < s * 100.0f) L->cw = s * 100.0f;

    L->filter_h = s * FILTER_H;
    L->filter_y = s * HEADER_TOP;
    L->filter_w = s * FILTER_W;
    float title_w = finn_text_width(&g_f_title, "Finn Store");
    float max_filter = (float)ww - L->pad * 2.0f - title_w - s * 16.0f;
    if (L->filter_w > max_filter) L->filter_w = max_filter;
    if (L->filter_w < s * 110.0f) L->filter_w = 0.0f;
    L->filter_x = (float)ww - L->pad - L->filter_w;

    L->wide = L->cw >= s * 640.0f;
    L->list_x = L->pad;
    L->list_y = L->top;
    L->list_w = L->cw;

    if (L->wide) {
        float dw = s * DETAIL_W;
        if (dw > L->cw * 0.46f) dw = L->cw * 0.46f;
        L->detail_w = dw;
        L->detail_x = L->pad + L->cw - dw;
        L->detail_y = L->top;
        L->detail_h = L->bottom - L->top;
        L->list_w = L->cw - dw - s * DETAIL_GAP;

        L->action_x = L->detail_x + s * DETAIL_PAD;
        L->action_w = L->detail_w - s * DETAIL_PAD * 2.0f;
        L->action_h = s * ACTION_H;
        L->action_y = L->detail_y + L->detail_h - s * DETAIL_PAD - L->action_h;
    }

    L->label_y = L->list_y + finn_text_cap_height(&g_f_micro);
    L->rows_y = L->list_y + s * PANE_LABEL_H;
    L->rows_h = L->bottom - L->rows_y;
    L->rows = (int)(L->rows_h / (s * ROW_H));
    if (L->rows < 1) L->rows = 1;
    L->foot_y = (float)wh - s * 19.0f;
}

static void clamp_scroll(const store_layout *L) {
    int n = visible_count();
    int max = n - L->rows;
    if (max < 0) max = 0;
    if (g_scroll > max) g_scroll = max;
    if (g_scroll < 0) g_scroll = 0;
}

/* ---------------- drawing helpers ---------------- */

static void draw_filter(uint32_t *L, int lw, int lh, const store_layout *lay,
                        float s, float ox, float oy) {
    if (lay->filter_w <= 0.0f) return;

    float fx = lay->filter_x + ox, fy = lay->filter_y + oy;
    finn_fill_rrect(L, lw, lh, fx, fy,
                    lay->filter_w, lay->filter_h, s * 7.0f,
                    finn_theme_dark() ? 0x14FFFFFFu : 0x0C000000u);
    finn_stroke_rrect(L, lw, lh, fx, fy,
                      lay->filter_w, lay->filter_h, s * 7.0f, 1.0f, FINN_HAIR);

    float tx = fx + s * 10.0f;
    if (g_ic_search.vw > 0) {
        int sz = finnapp_rs(12.0f, s);
        finn_icon_blit_ink(&g_ic_search, L, lw, lh,
                           (int)lroundf(fx + s * 17.0f),
                           (int)lroundf(fy + lay->filter_h * 0.5f),
                           sz, FINN_MUTE, FINN_MUTE, 0);
        tx += s * 19.0f;
    }

    const char *text = g_filter[0] ? g_filter : "Search apps";
    float avail = fx + lay->filter_w - s * 10.0f - tx;
    char fitted[128];
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, text, avail);
    float cap = finn_text_cap_height(&g_f_sub);
    finn_text_draw(&g_f_sub, L, lw, lh, tx,
                   fy + lay->filter_h * 0.5f + cap * 0.5f,
                   fitted, g_filter[0] ? FINN_INK : FINN_MUTE);

    if (g_filter_focused) {
        float tw = finn_text_width(&g_f_sub, g_filter);
        float cx = tx + tw + s * 2.0f;
        if (cx < fx + lay->filter_w - s * 8.0f)
            finn_fill_rrect(L, lw, lh, cx,
                            fy + s * 7.0f, 1.0f,
                            lay->filter_h - s * 14.0f, 0.0f, FINN_ON);
    }
}

static void draw_status_pill(uint32_t *L, int lw, int lh, float x, float y,
                             float s, const finn_app *a) {
    int available = app_available(a);
    const char *label = a->installed ? "Installed"
                                    : (available ? "Available" : "Unavailable");
    uint32_t bg = a->installed
                    ? 0x1F006FFFu
                    : (finn_theme_dark() ? 0x1AFFFFFFu : 0x10000000u);
    uint32_t ink = a->installed ? FINN_ON : (available ? FINN_DIM : FINN_MUTE);

    float cap = finn_text_cap_height(&g_f_sub);
    float tw = finn_text_width(&g_f_sub, label);
    float pw = tw + s * 20.0f;
    float ph = s * 22.0f;
    finn_fill_rrect(L, lw, lh, x, y, pw, ph, ph * 0.5f, bg);
    finn_text_draw(&g_f_sub, L, lw, lh, x + (pw - tw) * 0.5f,
                   y + ph * 0.5f + cap * 0.5f, label, ink);
}

static void draw_button(uint32_t *L, int lw, int lh, float x, float y,
                        float w, float h, float s, const char *label,
                        const finn_app *a) {
    int available = app_available(a);
    int installed = a->installed;

    uint32_t fill, ink;
    if (!installed && !available) {
        fill = finn_theme_dark() ? 0x14FFFFFFu : 0x0C000000u;
        ink = FINN_MUTE;
    } else if (!installed) {
        fill = FINN_ON;
        ink = 0xFFFFFFFFu;
    } else {
        fill = finn_theme_dark() ? 0x1AFFFFFFu : 0x10000000u;
        ink = FINN_INK;
    }

    finn_fill_rrect(L, lw, lh, x, y, w, h, s * 8.0f, fill);
    if (installed || !available)
        finn_stroke_rrect(L, lw, lh, x, y, w, h, s * 8.0f, 1.0f, FINN_HAIR);

    float cap = finn_text_cap_height(&g_f_label);
    float tw = finn_text_width(&g_f_label, label);
    finn_text_draw(&g_f_label, L, lw, lh, x + (w - tw) * 0.5f,
                   y + h * 0.5f + cap * 0.5f, label, ink);
}

/* A word-wrapped paragraph, drawn until the vertical budget runs out. */
static int draw_wrapped(uint32_t *L, int lw, int lh, const finn_font *f,
                        const char *text, float x, float y, float maxw,
                        float line_h, float limit_y, uint32_t col) {
    if (!text || !*text) return 0;
    char line[256] = "";
    const char *p = text;
    int drawn = 0;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *word = p;
        while (*p && *p != ' ') p++;
        size_t wl = (size_t)(p - word);

        char trial[256];
        if (line[0]) snprintf(trial, sizeof trial, "%s %.*s", line, (int)wl, word);
        else snprintf(trial, sizeof trial, "%.*s", (int)wl, word);

        if (line[0] && finn_text_width(f, trial) > maxw) {
            if (y + line_h > limit_y) break;
            float cap = finn_text_cap_height(f);
            finn_text_draw(f, L, lw, lh, x, y + cap, line, col);
            y += line_h;
            drawn++;
            snprintf(line, sizeof line, "%.*s", (int)wl, word);
        } else if (!line[0] && finn_text_width(f, trial) > maxw) {
            if (y + line_h > limit_y) break;
            char fitted[256];
            finnapp_ellipsis(f, fitted, sizeof fitted, trial, maxw);
            float cap = finn_text_cap_height(f);
            finn_text_draw(f, L, lw, lh, x, y + cap, fitted, col);
            y += line_h;
            drawn++;
            line[0] = '\0';
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }

    if (line[0] && y + line_h <= limit_y) {
        char fitted[256];
        finnapp_ellipsis(f, fitted, sizeof fitted, line, maxw);
        float cap = finn_text_cap_height(f);
        finn_text_draw(f, L, lw, lh, x, y + cap, fitted, col);
        drawn++;
    }
    return drawn;
}

/* ---------------- render ---------------- */

void finnstoreapp_render(uint32_t *layer, int lw, int lh,
                         int lx0, int ly0, int wx, int wy, int ww, int wh,
                         float scale, const finn_state *st, int focused) {
    (void)st;
    (void)focused;
    if (!g_ready) return;
    if (!g_font_dir[0]) return;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }

    /* The catalogue can be changed by finnctl while this window is open, so
     * the selection is reconciled before it is used as an index. */
    ensure_selection();

    float s = scale * FINN_APP_REF;
    store_layout lay;
    layout_for(ww, wh, s, &lay);
    g_last_rows = lay.rows;
    int sel = find_index(g_sel_id);
    int sel_row = visible_index_of(sel);
    if (sel_row >= 0) {
        if (sel_row < g_scroll) g_scroll = sel_row;
        if (sel_row >= g_scroll + lay.rows) g_scroll = sel_row - lay.rows + 1;
    }
    clamp_scroll(&lay);

    float ox = (float)(wx - lx0);
    float oy = (float)(wy - ly0);
    #define LX(v) ((v) + ox)
    #define LY(v) ((v) + oy)

    /* Header. */
    float tc = finn_text_cap_height(&g_f_title);
    finn_text_draw(&g_f_title, layer, lw, lh, LX(lay.title_x),
                   LY(lay.title_base), "Finn Store", FINN_INK);
    finn_text_draw(&g_f_micro, layer, lw, lh,
                   LX(lay.title_x), LY(lay.title_base + s * 16.0f),
                   "Applications the system knows about", FINN_MUTE);
    (void)tc;

    draw_filter(layer, lw, lh, &lay, s, ox, oy);

    /* List pane. */
    float label_y = lay.label_y;
    finn_text_draw(&g_f_micro, layer, lw, lh, LX(lay.list_x),
                   LY(label_y), "APPLICATIONS", FINN_MUTE);

    int n_total = finn_apps_count();
    int nvis = visible_count();
    char count[64];
    if (g_filter[0]) snprintf(count, sizeof count, "%d of %d", nvis, n_total);
    else snprintf(count, sizeof count, "%d", n_total);
    float count_w = finn_text_width(&g_f_micro, count);
    float count_x = lay.list_x + lay.list_w - count_w;
    if (lay.wide) count_x -= 0.0f;
    finn_text_draw(&g_f_micro, layer, lw, lh, LX(count_x),
                   LY(label_y), count, FINN_MUTE);

    float rh = s * ROW_H;
    for (int k = 0; k < lay.rows; k++) {
        int idx = visible_at(g_scroll + k);
        if (idx < 0) break;
        const finn_app *a = finn_apps_at(idx);
        if (!a) break;

        float ry = lay.rows_y + (float)k * rh;
        int is_sel = idx == sel;

        if (is_sel)
            finn_fill_rrect(layer, lw, lh, LX(lay.list_x + s * 3.0f),
                            LY(ry + s * 1.0f), lay.list_w - s * 6.0f,
                            rh - s * 2.0f, s * 6.0f, 0x2E006FFFu);

        draw_app_icon(layer, lw, lh, a, idx,
                      LX(lay.list_x + s * 12.0f),
                      LY(ry + (rh - s * 28.0f) * 0.5f), s * 28.0f, s);

        float text_x = lay.list_x + s * 50.0f;
        float right = lay.list_x + lay.list_w - s * 12.0f;
        float action_w = 0.0f;
        if (!lay.wide) action_w = s * (ROW_ACTION_W + 12.0f);
        float name_max = right - action_w - text_x;
        if (name_max < s * 60.0f) name_max = s * 60.0f;

        char name[128];
        finnapp_ellipsis(&g_f_label, name, sizeof name, a->name, name_max);
        float lc = finn_text_cap_height(&g_f_label);
        finn_text_draw(&g_f_label, layer, lw, lh, LX(text_x),
                       LY(ry + rh * 0.5f - s * 2.0f),
                       name, is_sel ? FINN_INK : FINN_DIM);

        char note[192];
        finnapp_ellipsis(&g_f_micro, note, sizeof note,
                         a->note[0] ? a->note : a->id, name_max);
        float mc = finn_text_cap_height(&g_f_micro);
        finn_text_draw(&g_f_micro, layer, lw, lh, LX(text_x),
                       LY(ry + rh * 0.5f + s * 12.0f + mc * 0.2f),
                       note, FINN_MUTE);

        if (lay.wide) {
            const char *state = a->installed ? "Installed"
                              : (app_available(a) ? "Available" : "Unavailable");
            uint32_t sc = a->installed ? FINN_ON
                         : (app_available(a) ? FINN_MUTE : FINN_MUTE);
            float sw = finn_text_width(&g_f_micro, state);
            finn_text_draw(&g_f_micro, layer, lw, lh,
                           LX(right - sw), LY(ry + rh * 0.5f + mc * 0.3f),
                           state, sc);
        } else {
            float aw = s * ROW_ACTION_W;
            float ah = s * ROW_ACTION_H;
            float ax = right - aw;
            float ay = ry + (rh - ah) * 0.5f;
            const char *label;
            if (a->installed) label = (is_sel && g_confirm_remove) ? "Remove?" : "Remove";
            else if (app_available(a)) label = "Install";
            else label = "Unavailable";

            if (!a->installed && !app_available(a)) {
                float lw2 = finn_text_width(&g_f_micro, label);
                finn_text_draw(&g_f_micro, layer, lw, lh,
                               LX(ax + (aw - lw2) * 0.5f),
                               LY(ay + ah * 0.5f + finn_text_cap_height(&g_f_micro) * 0.5f),
                               label, FINN_MUTE);
            } else {
                draw_button(layer, lw, lh, LX(ax), LY(ay), aw, ah, s, label, a);
            }
        }

        /* The row is a selection target, not a button: the button is drawn
         * separately and hit separately, so a click on the row never installs
         * by accident. */
        (void)lc;
    }

    if (nvis == 0) {
        float lc2 = finn_text_cap_height(&g_f_label);
        const char *line = g_filter[0] ? "No app matches that search"
                                       : "The catalogue is empty";
        finn_text_draw(&g_f_label, layer, lw, lh,
                       LX(lay.list_x + lay.list_w * 0.5f -
                          finn_text_width(&g_f_label, line) * 0.5f),
                       LY(lay.rows_y + s * 24.0f + lc2), line, FINN_DIM);
    } else if (nvis > lay.rows) {
        float track_h = lay.rows_h - s * 10.0f;
        float th = track_h * (float)lay.rows / (float)nvis;
        if (th < s * 24.0f) th = s * 24.0f;
        float maxscroll = (float)(nvis - lay.rows);
        float t = maxscroll > 0 ? (float)g_scroll / maxscroll : 0.0f;
        float ty = lay.rows_y + s * 5.0f + (track_h - th) * t;
        finn_fill_rrect(layer, lw, lh,
                        LX(lay.list_x + lay.list_w - s * 5.0f),
                        LY(ty), s * 3.0f, th, s * 1.5f, FINN_MUTE);
    }

    /* Detail pane. */
    if (lay.wide) {
        finn_frost_rrect(layer, lw, lh, lx0, ly0,
                         LX(lay.detail_x), LY(lay.detail_y),
                         lay.detail_w, lay.detail_h, s * 8.0f,
                         FINN_GLASS, 0.72f);
        finn_stroke_rrect(layer, lw, lh, LX(lay.detail_x), LY(lay.detail_y),
                          lay.detail_w, lay.detail_h, s * 8.0f, 1.0f, FINN_HAIR);

        float px = lay.detail_x + s * DETAIL_PAD;
        float py = lay.detail_y + s * DETAIL_PAD;
        float inner_w = lay.detail_w - s * DETAIL_PAD * 2.0f;

        if (sel < 0 || sel >= finn_apps_count()) {
            float lc2 = finn_text_cap_height(&g_f_sub);
            finn_text_draw(&g_f_sub, layer, lw, lh, LX(px), LY(py + lc2),
                           "Select an app to see what it is.", FINN_MUTE);
        } else {
            const finn_app *a = finn_apps_at(sel);
            draw_app_icon(layer, lw, lh, a, sel,
                          LX(px), LY(py), s * DETAIL_ICON, s);

            float tx = px + s * DETAIL_ICON + s * 12.0f;
            float tw = lay.detail_w - (tx - lay.detail_x) - s * DETAIL_PAD;
            if (tw < s * 40.0f) tw = s * 40.0f;
            char name[128];
            finnapp_ellipsis(&g_f_title, name, sizeof name, a->name, tw);
            float tc2 = finn_text_cap_height(&g_f_title);
            finn_text_draw(&g_f_title, layer, lw, lh, LX(tx),
                           LY(py + tc2), name, FINN_INK);

            char note[192];
            finnapp_ellipsis(&g_f_micro, note, sizeof note,
                             a->note[0] ? a->note : a->id, tw);
            finn_text_draw(&g_f_micro, layer, lw, lh, LX(tx),
                           LY(py + s * 22.0f + finn_text_cap_height(&g_f_micro)),
                           note, FINN_MUTE);

            draw_status_pill(layer, lw, lh, LX(px), LY(py + s * DETAIL_ICON + s * 12.0f),
                             s, a);

            float divider_y = py + s * DETAIL_ICON + s * 46.0f;
            finn_fill_rrect(layer, lw, lh, LX(px), LY(divider_y),
                            inner_w, 1.0f, 0.0f, FINN_HAIR);

            float body_y = divider_y + s * 18.0f;
            float body_limit = lay.action_y - s * 12.0f;
            finn_text_draw(&g_f_micro, layer, lw, lh, LX(px), LY(body_y),
                           "ABOUT THIS APP", FINN_MUTE);
            body_y += s * 16.0f;

            const char *explain;
            if (a->installed) {
                explain = "This app is marked installed for this machine. "
                          "The Start Menu opens it from its tile.";
            } else if (app_available(a)) {
                explain = "This app is in the catalogue but is not installed. "
                          "Installing marks it available to the Start Menu.";
            } else {
                explain = "This app is in the catalogue, but its launcher is "
                          "not on this machine. It cannot be installed here.";
            }

            float line_h = s * 14.0f;
            draw_wrapped(layer, lw, lh, &g_f_sub, explain, LX(px), LY(body_y),
                         inner_w, line_h, LY(body_limit), FINN_DIM);
            body_y += line_h * 4.0f;

            if (body_y + line_h < body_limit)
                draw_wrapped(layer, lw, lh, &g_f_micro,
                             "Finn Store installs from the catalogue that ships "
                             "with FinnOS. This build does not download packages.",
                             LX(px), LY(body_y), inner_w, s * 12.0f,
                             LY(body_limit), FINN_MUTE);

            const char *label;
            if (a->installed) label = g_confirm_remove ? "Remove?" : "Remove";
            else if (app_available(a)) label = "Install";
            else label = "Unavailable";

            draw_button(layer, lw, lh, LX(lay.action_x), LY(lay.action_y),
                        lay.action_w, lay.action_h, s, label, a);
        }
    }

    /* Footer. */
    {
        char left[128];
        int installed = 0;
        for (int i = 0; i < n_total; i++) {
            const finn_app *a = finn_apps_at(i);
            if (a && a->installed) installed++;
        }
        snprintf(left, sizeof left, "%d app%s, %d installed",
                 n_total, n_total == 1 ? "" : "s", installed);

        const char *note = g_status[0] ? g_status : "Catalogue ships with FinnOS";
        float nw = finn_text_width(&g_f_micro, note);
        float avail = lay.cw - nw - s * 16.0f;
        char fitted[256];
        finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, left,
                         avail > s * 40.0f ? avail : s * 40.0f);

        float lc2 = finn_text_cap_height(&g_f_micro);
        finn_text_draw(&g_f_micro, layer, lw, lh, LX(lay.pad),
                       LY(lay.foot_y + lc2), fitted, FINN_MUTE);

        char status[512];
        finnapp_ellipsis(&g_f_micro, status, sizeof status, note,
                         lay.cw - s * 180.0f);
        float sw = finn_text_width(&g_f_micro, status);
        finn_text_draw(&g_f_micro, layer, lw, lh,
                       LX(lay.pad + lay.cw - sw),
                       LY(lay.foot_y + lc2), status,
                       g_status_error ? FINN_INK : FINN_MUTE);
    }

    #undef LX
    #undef LY
}

/* ---------------- input ---------------- */

static int select_index(int i) {
    int n = finn_apps_count();
    if (i < 0 || i >= n) return 0;
    const finn_app *a = finn_apps_at(i);
    if (!a) return 0;
    if (!strcmp(g_sel_id, a->id)) {
        if (g_confirm_remove) { g_confirm_remove = 0; bump(); return 1; }
        return 0;
    }
    snprintf(g_sel_id, sizeof g_sel_id, "%s", a->id);
    g_confirm_remove = 0;
    g_status[0] = '\0';
    g_status_error = 0;
    bump();
    return 1;
}

int finnstoreapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                     float scale, int *out_sub) {
    if (out_sub) *out_sub = -1;
    if (px < wx || py < wy || px >= wx + ww || py >= wy + wh)
        return FINNSTORE_HIT_NONE;
    if (!g_ready || g_font_scale < 0.0f) return FINNSTORE_HIT_NONE;

    float s = scale * FINN_APP_REF;
    store_layout lay;
    layout_for(ww, wh, s, &lay);

    float x = (float)(px - wx);
    float y = (float)(py - wy);

    /* The titlebar belongs to the window manager. */
    if (y < s * 48.0f) return FINNSTORE_HIT_NONE;

    if (lay.filter_w > 0.0f &&
        x >= lay.filter_x && x < lay.filter_x + lay.filter_w &&
        y >= lay.filter_y && y < lay.filter_y + lay.filter_h)
        return FINNSTORE_HIT_FILTER;

    if (lay.wide &&
        x >= lay.action_x && x < lay.action_x + lay.action_w &&
        y >= lay.action_y && y < lay.action_y + lay.action_h) {
        const finn_app *a = selected_app();
        if (a && (a->installed || app_available(a)))
            return FINNSTORE_HIT_ACTION;
        return FINNSTORE_HIT_BODY;
    }

    if (x >= lay.list_x && x < lay.list_x + lay.list_w &&
        y >= lay.rows_y && y < lay.rows_y + lay.rows_h) {
        int k = (int)((y - lay.rows_y) / (s * ROW_H));
        int idx = visible_at(g_scroll + k);
        if (idx >= 0) {
            const finn_app *a = finn_apps_at(idx);
            if (a && !lay.wide) {
                float aw = s * ROW_ACTION_W;
                float ah = s * ROW_ACTION_H;
                float ax = lay.list_x + lay.list_w - s * 12.0f - aw;
                float ry = lay.rows_y + (float)k * s * ROW_H;
                float ay = ry + (s * ROW_H - ah) * 0.5f;
                if ((a->installed || app_available(a)) &&
                    x >= ax && x < ax + aw && y >= ay && y < ay + ah) {
                    if (out_sub) *out_sub = idx;
                    return FINNSTORE_HIT_ROW_ACTION;
                }
            }
            if (out_sub) *out_sub = idx;
            return FINNSTORE_HIT_ROW;
        }
    }

    return FINNSTORE_HIT_BODY;
}

int finnstoreapp_press(int hit, int sub, int px, int py, float scale) {
    (void)px; (void)py; (void)scale;

    switch (hit) {
    case FINNSTORE_HIT_FILTER:
        g_filter_focused = 1;
        bump();
        return 1;
    case FINNSTORE_HIT_ROW:
        return select_index(sub);
    case FINNSTORE_HIT_ROW_ACTION:
        select_index(sub);
        do_action();
        return 1;
    case FINNSTORE_HIT_ACTION:
        do_action();
        return 1;
    case FINNSTORE_HIT_BODY:
        if (g_filter_focused) {
            g_filter_focused = 0;
            bump();
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static void filter_changed(void) {
    g_scroll = 0;
    g_confirm_remove = 0;
    g_status[0] = '\0';
    g_status_error = 0;
    ensure_selection();
    bump();
}

static void move_selection(int delta) {
    int n = visible_count();
    if (n == 0) return;
    int i = find_index(g_sel_id);
    int k = visible_index_of(i);
    if (k < 0) k = delta > 0 ? -1 : n;
    k += delta;
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    select_index(visible_at(k));
}

static void jump_selection(int to_end) {
    int n = visible_count();
    if (n == 0) return;
    select_index(visible_at(to_end ? n - 1 : 0));
    g_scroll = to_end ? n - 1 : 0;
    bump();
}

int finnstoreapp_key(const finn_key_event *e) {
    if (!e->pressed) return 0;

    if (g_filter_focused) {
        if (e->code == KEY_ESC) {
            if (g_filter[0]) g_filter[0] = '\0';
            else g_filter_focused = 0;
            filter_changed();
            return 1;
        }
        if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
            g_filter_focused = 0;
            bump();
            return 1;
        }
        if (e->code == KEY_BACKSPACE) {
            size_t len = strlen(g_filter);
            if (len) g_filter[len - 1] = '\0';
            filter_changed();
            return 1;
        }
        if (e->code == KEY_UP)   { move_selection(-1); return 1; }
        if (e->code == KEY_DOWN) { move_selection(1);  return 1; }
        if (e->keysym >= 32 && e->keysym <= 126 &&
            strlen(g_filter) + 1 < sizeof g_filter) {
            size_t len = strlen(g_filter);
            g_filter[len] = (char)e->keysym;
            g_filter[len + 1] = '\0';
            filter_changed();
            return 1;
        }
        return 1;
    }

    switch (e->code) {
    case KEY_UP:        move_selection(-1); return 1;
    case KEY_DOWN:      move_selection(1);  return 1;
    case KEY_PAGEUP:
        g_scroll -= 5;
        if (g_scroll < 0) g_scroll = 0;
        bump();
        return 1;
    case KEY_PAGEDOWN:
        g_scroll += 5;
        bump();
        return 1;
    case KEY_HOME:      jump_selection(0); return 1;
    case KEY_END:       jump_selection(1); return 1;
    case KEY_ENTER:
    case KEY_KPENTER:   do_action(); return 1;
    case KEY_BACKSPACE:
        if (g_filter[0]) {
            size_t len = strlen(g_filter);
            g_filter[len - 1] = '\0';
            filter_changed();
            return 1;
        }
        return 0;
    case KEY_ESC:
        if (g_filter[0]) {
            g_filter[0] = '\0';
            filter_changed();
            return 1;
        }
        return 0;
    case KEY_F5:
        reload_catalogue();
        return 1;
    default:
        if (e->keysym >= 32 && e->keysym <= 126 &&
            strlen(g_filter) + 1 < sizeof g_filter) {
            size_t len = strlen(g_filter);
            g_filter[len] = (char)e->keysym;
            g_filter[len + 1] = '\0';
            g_filter_focused = 1;
            filter_changed();
            return 1;
        }
        return 0;
    }
}

int finnstoreapp_scroll(int clicks, float scale) {
    (void)scale;
    if (!clicks) return 0;
    int before = g_scroll;
    int n = visible_count();
    int max = n - g_last_rows;
    if (max < 0) max = 0;
    g_scroll -= clicks * 2;
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll > max) g_scroll = max;
    if (g_scroll != before) { bump(); return 1; }
    return 0;
}

/* ---------------- control-plane view ---------------- */

const char *finnstoreapp_selection(void) { return g_sel_id; }
const char *finnstoreapp_status(void) { return g_status; }

uint32_t finnstoreapp_build_key(float scale) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(scale * 1000.0f));
    MIX(g_rev);
    for (const char *p = g_filter; *p; p++) MIX(*p);
    for (const char *p = g_sel_id; *p; p++) MIX(*p);
    for (const char *p = g_status; *p; p++) MIX(*p);
    MIX(g_scroll); MIX(g_filter_focused); MIX(g_confirm_remove);
    MIX(g_status_error); MIX(g_last_rows);
    int n = finn_apps_count();
    MIX(n);
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a) continue;
        MIX(a->installed);
        MIX(app_available(a));
        for (const char *p = a->id; *p; p++) MIX(*p);
        for (const char *p = a->name; *p; p++) MIX(*p);
    }
    #undef MIX
    return h ? h : 1u;
}

int finnstoreapp_init(const char *icon_dir, const char *font_dir,
                      const char *registry) {
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");
    snprintf(g_registry, sizeof g_registry, "%s",
             registry ? registry : FINN_APPS_CATALOGUE);

    char p[600];
    snprintf(p, sizeof p, "%s/search-01.svg", g_icon_dir);
    fsvg_load(p, &g_ic_search);

    reload_catalogue();
    g_ready = 1;
    return 0;
}

void finnstoreapp_free(void) {
    free_icons();
    fsvg_free(&g_ic_search);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
    g_font_scale = -1.0f;
    g_ready = 0;
    g_filter[0] = '\0';
    g_sel_id[0] = '\0';
    g_status[0] = '\0';
    g_scroll = 0;
    g_confirm_remove = 0;
    g_filter_focused = 0;
    g_last_rows = 1;
}
