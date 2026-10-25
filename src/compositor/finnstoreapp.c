/*
 * finnstoreapp — the Finn Store view.
 *
 * There is no client surface protocol in this build, so the Store is another
 * view inside the compositor, like Settings and Files. That puts it in the
 * one process that owns the screen; it does not put it in charge of packages.
 *
 * The shape is the Microsoft Store's — a navigation rail, a header search, a
 * featured app and shelves on Home, a grid in Apps, a list in Library and a
 * full product page — because that is the arrangement a person already
 * knows. The material, type and colour are FinnOS's, and every shelf is
 * filled from the registry rather than from a list typed into this file.
 *
 * Three rules hold the view together:
 *
 *   - The catalogue is read from the OS registry. An app that is not in the
 *     registry is not for sale, and an app the registry knows but this
 *     machine lacks the launcher for is shown as unavailable rather than as
 *     a button that would fail.
 *
 *   - Installed is finnpkg's answer. The view re-reads the install list after
 *     every action instead of flipping a local flag and hoping.
 *
 *   - The action goes through finnd. The Store asks the control plane to
 *     install; it does not edit /var/lib/finnpkg/installed itself, because
 *     then there would be two package managers and only one audit log.
 *
 * There is no catalogue server here. The Store shows no rating, price,
 * download size, version, review, screenshot or update badge, because this
 * build has none of those things to report. Installing marks a catalogue
 * entry installed for the Start Menu; it does not download a package.
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

#define PAD             24.0f
#define NARROW_PAD      20.0f
#define RAIL_W         168.0f
#define RAIL_MIN       720.0f   /* narrower than this: tabs over the content */
#define RAIL_MIN_H     380.0f   /* too short for a rail: use tabs instead */

#define TITLE_BASE      76.0f
#define FILTER_W       240.0f
#define FILTER_H        32.0f
#define CONTENT_TOP    100.0f

#define FOOTER_RULE     30.0f
#define FOOTER_BASE     11.0f

#define NAV_TOP         60.0f
#define NAV_ITEM_H      42.0f
#define NAV_ITEM_GAP     6.0f
#define TAB_H           30.0f
#define TAB_GAP          6.0f

#define CARD_R           9.0f
#define CARD_GAP        12.0f
#define HERO_H         116.0f
#define SHELF_CARD_H   112.0f
#define GRID_CARD_H    158.0f
#define LIB_ROW_H       64.0f
#define LABEL_H         18.0f
#define SECTION_GAP     10.0f

#define HERO_GAP        10.0f
#define CARD_TOP_GAP     5.0f
#define GRID_MIN_W     185.0f
#define SHELF_MIN_W    140.0f

#define ACTION_H        34.0f
#define ACTION_MIN_W    62.0f

#define DETAIL_BACK_H   26.0f
#define DETAIL_BACK_W   62.0f
#define DETAIL_ICON_W   84.0f
#define DETAIL_ICON_N   64.0f
#define DETAIL_ACTION_W 118.0f

typedef struct {
    fsvg svg;
    int  loaded;
    char id[32];
} store_icon;

typedef struct {
    uint32_t *px;
    int w, h;
    int ox, oy;      /* screen coordinate of pixel 0,0 (frost samples this) */
} store_canvas;

enum { STORE_PAGE_HOME = 0, STORE_PAGE_APPS, STORE_PAGE_LIBRARY,
       STORE_PAGE_COUNT };
enum { BTN_QUIET = 0, BTN_INSTALL, BTN_REMOVE, BTN_REMOVE_CONFIRM,
       BTN_DISABLED };

/* ---------------- state ---------------- */

static char g_font_dir[512] = "";
static char g_icon_dir[512] = "";
static char g_registry[512] = "";

static finn_font g_f_hero, g_f_title, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static fsvg g_ic_store, g_ic_search, g_ic_home, g_ic_grid, g_ic_library;
static fsvg g_ic_back, g_ic_next, g_ic_clear;
static int g_ready = 0;

static store_icon g_icons[FINN_APP_MAX];

static char g_filter[64] = "";
static int  g_filter_focused = 0;
static int  g_page = STORE_PAGE_HOME;
static int  g_detail = 0;
static char g_sel_id[32] = "";
static int  g_scroll[STORE_PAGE_COUNT] = { 0, 0, 0 };
static int  g_detail_scroll = 0;
static int  g_max_scroll[STORE_PAGE_COUNT + 1] = { 0, 0, 0, 0 };
static char g_confirm_id[32] = "";
static char g_status[512] = "";
static int  g_status_error = 0;
static int  g_load_error = 0;
static unsigned g_rev = 1;
static int  g_reveal_pending = 0;
static int  g_last_cols = 1;

static uint32_t *g_scratch = NULL;
static size_t g_scratch_cap = 0;

static void bump(void) { g_rev++; }

static uint32_t with_alpha(uint32_t c, unsigned a) {
    return (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
}

static int load_fonts(float scale) {
    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    float s = scale * FINN_APP_REF;
    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_hero, p, finnapp_font_px(s, 22.0f)) != 0) return -1;
    if (finn_font_load(&g_f_title, p, finnapp_font_px(s, 16.0f)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, finnapp_font_px(s, 12.5f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, finnapp_font_px(s, 10.5f)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, finnapp_font_px(s, 8.5f)) != 0) return -1;
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

static const char *state_label(const finn_app *a) {
    if (!a) return "";
    if (a->installed) return "Installed";
    return app_available(a) ? "Available" : "Unavailable";
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

static int installed_visible_count(void) {
    int n = finn_apps_count(), k = 0;
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (a && a->installed && matches(i)) k++;
    }
    return k;
}

static int installed_visible_at(int k) {
    int n = finn_apps_count();
    if (k < 0) return -1;
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a || !a->installed || !matches(i)) continue;
        if (k-- == 0) return i;
    }
    return -1;
}

static int installed_visible_index_of(int i) {
    if (i < 0) return -1;
    int n = finn_apps_count(), k = 0;
    for (int j = 0; j < n; j++) {
        const finn_app *a = finn_apps_at(j);
        if (!a || !a->installed || !matches(j)) continue;
        if (j == i) return k;
        k++;
    }
    return -1;
}

static int selection_index(void) { return find_index(g_sel_id); }

/*
 * The selection is always a catalogue id, never an index: the registry can
 * be re-read under the view (F5, or after an install) and an index would
 * then point at a different app.
 */
static int ensure_selection(void) {
    int i = find_index(g_sel_id);

    if (g_detail) {
        if (i >= 0) return 0;
        g_detail = 0;
    }

    int licit = i >= 0 && matches(i);
    if (licit && g_page == STORE_PAGE_LIBRARY) {
        const finn_app *a = finn_apps_at(i);
        licit = a && a->installed;
    }
    if (licit) return 0;

    int first = (g_page == STORE_PAGE_LIBRARY)
              ? installed_visible_at(0) : visible_at(0);
    if (first >= 0) {
        const finn_app *a = finn_apps_at(first);
        snprintf(g_sel_id, sizeof g_sel_id, "%s", a->id);
    } else {
        g_sel_id[0] = '\0';
    }
    g_confirm_id[0] = '\0';
    g_reveal_pending = 1;
    return 1;
}

static void reload_catalogue(void) {
    int n = finn_apps_load(g_registry, NULL);
    g_load_error = (n < 0);
    if (find_index(g_confirm_id) < 0) g_confirm_id[0] = '\0';
    ensure_selection();
    bump();
}

/* ---------------- action ---------------- */

static void set_status(const char *text, int error) {
    snprintf(g_status, sizeof g_status, "%s", text ? text : "");
    g_status_error = error ? 1 : 0;
    bump();
}

static const char *action_label(const finn_app *a) {
    if (!a) return "";
    if (a->installed)
        return !strcmp(g_confirm_id, a->id) ? "Remove?" : "Remove";
    return app_available(a) ? "Install" : "Unavailable";
}

static int action_kind(const finn_app *a) {
    if (!a) return BTN_DISABLED;
    if (a->installed)
        return !strcmp(g_confirm_id, a->id) ? BTN_REMOVE_CONFIRM : BTN_REMOVE;
    return app_available(a) ? BTN_INSTALL : BTN_DISABLED;
}

static void do_action(int idx) {
    const finn_app *a = finn_apps_at(idx);
    if (!a) return;

    if (!a->installed && !app_available(a)) {
        set_status("That app's launcher is not on this machine, so it cannot be installed.",
                   1);
        return;
    }

    if (a->installed && strcmp(g_confirm_id, a->id) != 0) {
        snprintf(g_confirm_id, sizeof g_confirm_id, "%s", a->id);
        char msg[256];
        snprintf(msg, sizeof msg,
                 "Press Remove again to take %s off this machine.", a->name);
        set_status(msg, 0);
        return;
    }

    int install = !a->installed;
    char id[32], name[64];
    snprintf(id, sizeof id, "%s", a->id);
    snprintf(name, sizeof name, "%s", a->name);

    char req[96];
    snprintf(req, sizeof req, "%s %s", install ? "app.install" : "app.remove", id);

    char reply[512];
    if (finn_call(FINN_CONTROL_SOCK, req, reply, sizeof reply) != 0) {
        g_confirm_id[0] = '\0';
        set_status("The control plane is not reachable, so nothing was changed.", 1);
        return;
    }

    if (strncmp(reply, "OK", 2) != 0) {
        const char *why = reply + 3;
        while (*why == ' ') why++;
        g_confirm_id[0] = '\0';
        set_status(*why ? why : "The package manager refused the change.", 1);
        return;
    }

    g_confirm_id[0] = '\0';
    reload_catalogue();

    char msg[256];
    snprintf(msg, sizeof msg, "%s %s.", install ? "Installed" : "Removed", name);
    set_status(msg, 0);
}

/* ---------------- canvas & icons ---------------- */

static void c_fill(store_canvas *c, float x, float y, float w, float h,
                   float r, uint32_t col) {
    finn_fill_rrect(c->px, c->w, c->h, x, y, w, h, r, col);
}

static void c_stroke(store_canvas *c, float x, float y, float w, float h,
                     float r, float t, uint32_t col) {
    finn_stroke_rrect(c->px, c->w, c->h, x, y, w, h, r, t, col);
}

static void c_frost(store_canvas *c, float x, float y, float w, float h,
                    float r, uint32_t tint, float alpha) {
    finn_frost_rrect(c->px, c->w, c->h, c->ox, c->oy, x, y, w, h, r,
                     tint, alpha);
}

static void c_text(store_canvas *c, const finn_font *f, float x, float base,
                   const char *text, uint32_t col) {
    finn_text_draw(f, c->px, c->w, c->h, x, base, text, col);
}

static void c_icon_ink(store_canvas *c, fsvg *ic, float cx, float cy,
                       float ink, uint32_t active, uint32_t inactive) {
    if (!ic || ic->vw <= 0) return;
    finn_icon_blit_ink(ic, c->px, c->w, c->h,
                       (int)lroundf(cx), (int)lroundf(cy),
                       (int)lroundf(ink), active, inactive, 0);
}

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

static void draw_app_icon(store_canvas *c, const finn_app *a, int i,
                          float x, float y, float sz) {
    fsvg *ic = icon_for(i, a);
    if (ic && ic->vw > 0) {
        finn_icon_blit(ic, c->px, c->w, c->h,
                       (int)lroundf(x), (int)lroundf(y),
                       (int)lroundf(sz), 0, 0, 0);
        return;
    }

    /* No asset is not an empty square: the tile still has to say which app it
     * belongs to, so the first letter of the name stands in. */
    c_fill(c, x, y, sz, sz, sz * 0.22f, FINN_TILE_ON);
    char initial[2] = { a && a->name[0] ? a->name[0] : '?', 0 };
    float cap = finn_text_cap_height(&g_f_label);
    float tw = finn_text_width(&g_f_label, initial);
    c_text(c, &g_f_label, x + (sz - tw) * 0.5f,
           y + sz * 0.5f + cap * 0.5f, initial, FINN_INK);
}

/* ---------------- layout ---------------- */

typedef struct {
    float pad;
    int   rail;
    float rail_w;
    float rail_y;
    float title_x, title_base;
    float filter_x, filter_y, filter_w, filter_h;
    float tabs_x, tabs_y, tabs_w, tabs_h;
    float content_x, content_y, content_w, content_bottom;
    float footer_rule_y, footer_base;
    int   footer_visible;
    float scrollbar_x;
    int   grid_cols, shelf_cols;
} store_layout;

static int cols_for(float cw, float s, float minw) {
    float gap = s * CARD_GAP;
    int c = (int)floorf((cw + gap) / (s * minw + gap));
    if (c < 1) c = 1;
    if (c > 3) c = 3;
    return c;
}

static int shelf_cols_for(float cw, float s) {
    float gap = s * CARD_GAP;
    int c = (int)floorf((cw + gap) / (s * SHELF_MIN_W + gap));
    if (c < 1) c = 1;
    if (c > 4) c = 4;
    return c;
}

static void layout_for(int ww, int wh, float s, store_layout *L) {
    memset(L, 0, sizeof *L);
    float ref_w = (float)ww / s;
    float ref_h = (float)wh / s;

    L->rail = ref_w >= RAIL_MIN && ref_h >= RAIL_MIN_H;
    L->pad = s * (L->rail ? PAD : NARROW_PAD);
    L->filter_h = s * FILTER_H;
    L->tabs_h = s * TAB_H;
    L->rail_y = s * 46.0f;

    if (L->rail) {
        L->rail_w = s * RAIL_W;
        L->title_x = L->rail_w + s * PAD;
        L->title_base = s * TITLE_BASE;
        L->filter_w = s * FILTER_W;
        L->filter_y = s * 52.0f;
        L->filter_x = (float)ww - L->pad - L->filter_w;
        L->content_x = L->rail_w + s * PAD;
        L->content_w = (float)ww - L->content_x - L->pad;
        L->content_y = s * CONTENT_TOP;
        L->tabs_w = 0.0f;
    } else {
        L->title_x = L->pad;
        L->title_base = s * 72.0f;
        float title_w = finn_text_width(&g_f_title, "Finn Store");
        float max_filter = (float)ww - L->pad * 2.0f - title_w - s * 16.0f;
        float want = s * FILTER_W;
        if (max_filter >= s * 140.0f) {
            L->filter_w = want < max_filter ? want : max_filter;
            L->filter_y = s * 52.0f;
            L->filter_x = (float)ww - L->pad - L->filter_w;
            L->tabs_y = s * 92.0f;
            L->content_y = s * 128.0f;
        } else {
            L->filter_w = (float)ww - L->pad * 2.0f;
            L->filter_y = s * 82.0f;
            L->filter_x = L->pad;
            L->tabs_y = s * 122.0f;
            L->content_y = s * 158.0f;
        }
        L->tabs_x = L->pad;
        L->tabs_w = (float)ww - L->pad * 2.0f;
        L->content_x = L->pad;
        L->content_w = (float)ww - L->pad * 2.0f;
    }

    if (L->content_w < s * 80.0f) L->content_w = s * 80.0f;
    L->grid_cols = cols_for(L->content_w, s, GRID_MIN_W);
    L->shelf_cols = shelf_cols_for(L->content_w, s);

    L->footer_rule_y = (float)wh - s * FOOTER_RULE;
    L->footer_base = (float)wh - s * FOOTER_BASE;
    L->footer_visible = (L->footer_rule_y - L->content_y) >= s * 44.0f;
    L->content_bottom = L->footer_visible ? L->footer_rule_y - s * 8.0f
                                          : (float)wh - s * 6.0f;
    if (L->content_bottom < L->content_y) L->content_bottom = L->content_y;
    L->scrollbar_x = L->content_x + L->content_w + s * 5.0f;
    if (L->scrollbar_x > (float)ww - s * 6.0f)
        L->scrollbar_x = (float)ww - s * 6.0f;
}

static void nav_item_rect(const store_layout *L, int i, float s,
                          float *x, float *y, float *w, float *h) {
    if (L->rail) {
        *x = s * 12.0f;
        *w = L->rail_w - s * 24.0f;
        *y = L->rail_y + s * NAV_TOP +
             (float)i * (s * NAV_ITEM_H + s * NAV_ITEM_GAP);
        *h = s * NAV_ITEM_H;
    } else {
        float gap = s * TAB_GAP;
        *w = (L->tabs_w - gap * 2.0f) / 3.0f;
        *x = L->tabs_x + (float)i * (*w + gap);
        *y = L->tabs_y;
        *h = L->tabs_h;
    }
}

static void filter_clear_rect(const store_layout *L, float s,
                              float *x, float *y, float *w, float *h) {
    *w = s * 26.0f;
    *h = s * 26.0f;
    *x = L->filter_x + L->filter_w - *w - s * 3.0f;
    *y = L->filter_y + (L->filter_h - *h) * 0.5f;
}

/* ---------------- drawing helpers ---------------- */

static void card_bg(store_canvas *c, float x, float y, float w, float h,
                    float s, int selected) {
    float r = s * CARD_R;
    c_frost(c, x, y, w, h, r, FINN_GLASS, 0.72f);
    if (finn_theme_dark())
        c_fill(c, x, y, w, h, r, 0x0AFFFFFFu);
    if (selected) {
        c_fill(c, x, y, w, h, r,
               finn_theme_dark() ? 0x1C0A84FFu : 0x10006FFFu);
        c_stroke(c, x, y, w, h, r, 1.5f, FINN_ON);
    } else {
        c_stroke(c, x, y, w, h, r, 1.0f, FINN_HAIR);
    }
}

/* The quiet well a page shows instead of a list when it has nothing to list. */
static void quiet_well(store_canvas *c, float x, float y, float w, float h,
                       float s, const char *line, const char *sub) {
    c_fill(c, x, y, w, h, s * CARD_R,
           finn_theme_dark() ? 0x1AFFFFFFu : 0x0D000000u);
    float cap = finn_text_cap_height(&g_f_sub);
    float tx = x + s * 16.0f;
    float maxw = w - s * 32.0f;
    char fitted[256];
    float base = sub && sub[0] ? y + h * 0.5f - s * 2.0f
                               : y + h * 0.5f + cap * 0.5f;
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, line, maxw);
    c_text(c, &g_f_sub, tx, base, fitted, FINN_DIM);
    if (sub && sub[0]) {
        finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, sub, maxw);
        c_text(c, &g_f_micro, tx, base + s * 17.0f, fitted, FINN_MUTE);
    }
    (void)cap;
}

static void draw_status_pill(store_canvas *c, float x, float y, float s,
                             const finn_app *a) {
    const char *label = state_label(a);
    int installed = a && a->installed;
    int available = app_available(a);
    float tw = finn_text_width(&g_f_micro, label);
    float dot_w = installed ? s * 10.0f : 0.0f;
    float ph = s * 22.0f;
    float pw = tw + s * 18.0f + dot_w;

    uint32_t bg, ink;
    if (installed) {
        bg = finn_theme_dark() ? 0x2E0A84FFu : 0x14006FFFu;
        ink = FINN_INK;
    } else if (available) {
        bg = finn_theme_dark() ? 0x1AFFFFFFu : 0x0E000000u;
        ink = FINN_DIM;
    } else {
        bg = finn_theme_dark() ? 0x12FFFFFFu : 0x08000000u;
        ink = FINN_MUTE;
    }
    c_fill(c, x, y, pw, ph, ph * 0.5f, bg);
    if (installed)
        c_fill(c, x + s * 9.0f, y + ph * 0.5f - s * 2.0f, s * 4.0f, s * 4.0f,
               s * 2.0f, FINN_ON);
    c_text(c, &g_f_micro, x + s * 9.0f + dot_w,
           y + ph * 0.5f + finn_text_cap_height(&g_f_micro) * 0.5f,
           label, ink);
}

/* A status as a dot and a word, for a card's bottom line. */
static void draw_status_line(store_canvas *c, float x, float base, float s,
                             const finn_app *a) {
    int installed = a && a->installed;
    int available = app_available(a);
    uint32_t ink = installed ? FINN_INK : (available ? FINN_DIM : FINN_MUTE);
    float cap = finn_text_cap_height(&g_f_micro);
    c_fill(c, x, base - cap * 0.5f - s * 1.5f, s * 4.0f, s * 4.0f,
           s * 2.0f, installed ? FINN_ON : FINN_MUTE);
    c_text(c, &g_f_micro, x + s * 10.0f, base, state_label(a), ink);
}

static void draw_button(store_canvas *c, float x, float y, float w, float h,
                        float s, const char *label, int kind) {
    float r = h * 0.5f;
    uint32_t fill, ink;
    int stroked = 0;
    uint32_t stroke = FINN_HAIR;

    switch (kind) {
    case BTN_INSTALL:
        fill = FINN_ON;
        ink = 0xFFFFFFFFu;
        break;
    case BTN_REMOVE:
        fill = finn_theme_dark() ? 0x1AFFFFFFu : 0x0C000000u;
        ink = FINN_INK;
        stroked = 1;
        break;
    case BTN_REMOVE_CONFIRM:
        fill = finn_theme_dark() ? 0x1AFFFFFFu : 0x0C000000u;
        ink = FINN_INK;
        stroked = 1;
        stroke = FINN_ON;
        break;
    case BTN_QUIET:
        fill = finn_theme_dark() ? 0x14FFFFFFu : 0x08000000u;
        ink = FINN_DIM;
        stroked = 1;
        break;
    default:
        fill = finn_theme_dark() ? 0x10FFFFFFu : 0x08000000u;
        ink = FINN_MUTE;
        break;
    }

    c_fill(c, x, y, w, h, r, fill);
    if (stroked) c_stroke(c, x, y, w, h, r, 1.0f, stroke);

    char fitted[64];
    finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, label, w - s * 8.0f);
    float tw = finn_text_width(&g_f_label, fitted);
    c_text(c, &g_f_label, x + (w - tw) * 0.5f,
           y + h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
           fitted, ink);
}

static float button_width(const finn_app *a, float s) {
    float tw = finn_text_width(&g_f_label, action_label(a));
    float w = tw + s * 16.0f;
    if (w < s * ACTION_MIN_W) w = s * ACTION_MIN_W;
    if (w > s * 110.0f) w = s * 110.0f;
    return w;
}

/* A word-wrapped paragraph, drawn from its top. */
static int draw_wrapped(store_canvas *c, const finn_font *f, const char *text,
                        float x, float top, float maxw, float line_h,
                        uint32_t col) {
    if (!text || !*text) return 0;
    char line[256] = "";
    const char *p = text;
    int drawn = 0;
    float y = top;

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
            float cap = finn_text_cap_height(f);
            c_text(c, f, x, y + cap, line, col);
            y += line_h;
            drawn++;
            snprintf(line, sizeof line, "%.*s", (int)wl, word);
        } else if (!line[0] && finn_text_width(f, trial) > maxw) {
            char fitted[256];
            finnapp_ellipsis(f, fitted, sizeof fitted, trial, maxw);
            float cap = finn_text_cap_height(f);
            c_text(c, f, x, y + cap, fitted, col);
            y += line_h;
            drawn++;
            line[0] = '\0';
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }

    if (line[0]) {
        char fitted[256];
        finnapp_ellipsis(f, fitted, sizeof fitted, line, maxw);
        float cap = finn_text_cap_height(f);
        c_text(c, f, x, y + cap, fitted, col);
        drawn++;
    }
    return drawn;
}

static int wrap_lines(const finn_font *f, const char *text, float maxw) {
    if (!text || !*text) return 0;
    char line[256] = "";
    const char *p = text;
    int lines = 0;

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
            lines++;
            snprintf(line, sizeof line, "%.*s", (int)wl, word);
        } else if (!line[0] && finn_text_width(f, trial) > maxw) {
            lines++;
            line[0] = '\0';
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }
    if (line[0]) lines++;
    return lines;
}

/* ---------------- header, rail and footer ---------------- */

static void draw_filter(store_canvas *c, const store_layout *L, float s,
                        int focused) {
    float x = L->filter_x, y = L->filter_y;
    float w = L->filter_w, h = L->filter_h;
    float r = h * 0.5f;

    c_fill(c, x, y, w, h, r,
           finn_theme_dark() ? 0x24FFFFFFu : 0x12000000u);
    c_stroke(c, x, y, w, h, r, focused ? 1.5f : 1.0f,
             focused ? FINN_ON : FINN_HAIR);

    c_icon_ink(c, &g_ic_search, x + s * 16.0f, y + h * 0.5f,
               s * 13.0f, FINN_MUTE, FINN_MUTE);

    float tx = x + s * 34.0f;
    float clear_x = x + w;
    int has_clear = g_filter[0] != '\0';
    float text_right = x + w - (has_clear ? s * 34.0f : s * 12.0f);

    if (!g_filter[0]) {
        c_text(c, &g_f_sub, tx, y + h * 0.5f +
               finn_text_cap_height(&g_f_sub) * 0.5f,
               "Search apps", FINN_MUTE);
    } else {
        char fitted[128];
        finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, g_filter,
                         text_right - tx);
        c_text(c, &g_f_sub, tx, y + h * 0.5f +
               finn_text_cap_height(&g_f_sub) * 0.5f,
               fitted, FINN_INK);
        if (focused) {
            float caret_x = tx + finn_text_width(&g_f_sub, fitted);
            if (caret_x > text_right) caret_x = text_right;
            float cap = finn_text_cap_height(&g_f_sub);
            c_fill(c, caret_x + s * 1.5f, y + h * 0.5f - cap * 0.55f,
                   s * 1.5f, cap * 1.1f, s * 0.75f, FINN_ON);
        }
    }

    if (has_clear) {
        float cx = clear_x - s * 16.0f;
        float cy = y + h * 0.5f;
        c_fill(c, cx - s * 9.0f, cy - s * 9.0f, s * 18.0f, s * 18.0f,
               s * 9.0f, finn_theme_dark() ? 0x1AFFFFFFu : 0x0E000000u);
        c_icon_ink(c, &g_ic_clear, cx, cy, s * 9.0f, FINN_MUTE, FINN_MUTE);
    }
}

static void draw_header(store_canvas *c, const store_layout *L, float s,
                        int focused) {
    c_text(c, &g_f_title, L->title_x, L->title_base, "Finn Store", FINN_INK);
    draw_filter(c, L, s, focused);
}

static void draw_rail(store_canvas *c, const store_layout *L, float s) {
    float h = L->footer_rule_y - L->rail_y;
    if (h <= 0.0f) return;

    c_frost(c, 0.0f, L->rail_y, L->rail_w, h, 0.0f, FINN_GLASS, 0.68f);
    if (finn_theme_dark())
        c_fill(c, 0.0f, L->rail_y, L->rail_w, h, 0.0f, 0x08FFFFFFu);
    c_fill(c, L->rail_w - 1.0f, L->rail_y, 1.0f, h, 0.0f, FINN_HAIR);

    /* The app's own mark, colour and all: the rail belongs to the Store. */
    if (g_ic_store.vw > 0)
        finn_icon_blit(&g_ic_store, c->px, c->w, c->h,
                       (int)lroundf(s * 20.0f), (int)lroundf(L->rail_y + s * 14.0f),
                       (int)lroundf(s * 26.0f), 0, 0, 0);

    static const char *labels[3] = { "Home", "Apps", "Library" };
    fsvg *icons[3] = { &g_ic_home, &g_ic_grid, &g_ic_library };

    for (int i = 0; i < 3; i++) {
        float x, y, w, hgt;
        nav_item_rect(L, i, s, &x, &y, &w, &hgt);
        int active = (i == g_page);

        if (active) {
            c_fill(c, x, y, w, hgt, s * 8.0f,
                   finn_theme_dark() ? 0x2A0A84FFu : 0x1A006FFFu);
            c_fill(c, x, y + s * 12.0f, s * 2.5f, s * 18.0f, s * 1.25f,
                   FINN_ON);
        }

        float cy = y + hgt * 0.5f;
        uint32_t ink = active ? FINN_ON : FINN_DIM;
        c_icon_ink(c, icons[i], x + s * 20.0f, cy, s * 16.0f, ink, ink);
        c_text(c, &g_f_label, x + s * 36.0f,
               cy + finn_text_cap_height(&g_f_label) * 0.5f,
               labels[i], active ? FINN_INK : FINN_DIM);
    }
}

static void draw_tabs(store_canvas *c, const store_layout *L, float s) {
    static const char *labels[3] = { "Home", "Apps", "Library" };
    fsvg *icons[3] = { &g_ic_home, &g_ic_grid, &g_ic_library };

    for (int i = 0; i < 3; i++) {
        float x, y, w, h;
        nav_item_rect(L, i, s, &x, &y, &w, &h);
        int active = (i == g_page);
        if (active) {
            c_fill(c, x, y, w, h, s * 8.0f,
                   finn_theme_dark() ? 0x2A0A84FFu : 0x1A006FFFu);
        }

        float tw = finn_text_width(&g_f_sub, labels[i]);
        float icon = s * 13.0f;
        float group = icon + s * 5.0f + tw;
        float gx = x + (w - group) * 0.5f;
        float cy = y + h * 0.5f;
        uint32_t ink = active ? FINN_ON : FINN_DIM;
        c_icon_ink(c, icons[i], gx + icon * 0.5f, cy, icon, ink, ink);
        c_text(c, &g_f_sub, gx + icon + s * 5.0f,
               cy + finn_text_cap_height(&g_f_sub) * 0.5f,
               labels[i], active ? FINN_INK : FINN_DIM);
    }
}

static void draw_footer(store_canvas *c, const store_layout *L, float s,
                        int total, int installed) {
    if (!L->footer_visible) return;

    c_fill(c, 0.0f, L->footer_rule_y, (float)c->w, 1.0f, 0.0f, FINN_HAIR);

    char left[128];
    if (g_filter[0] && g_page == STORE_PAGE_LIBRARY)
        snprintf(left, sizeof left, "%d of %d installed",
                 installed_visible_count(), installed);
    else if (g_filter[0])
        snprintf(left, sizeof left, "%d of %d apps",
                 visible_count(), total);
    else
        snprintf(left, sizeof left, "%d app%s, %d installed",
                 total, total == 1 ? "" : "s", installed);

    char fitted[256];
    float right_limit = (float)c->w - L->pad;
    float left_x = L->content_x;
    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, left,
                     right_limit - left_x - s * 120.0f);
    c_text(c, &g_f_micro, left_x,
           L->footer_base + finn_text_cap_height(&g_f_micro), fitted, FINN_MUTE);

    const char *note = g_status[0] ? g_status
                     : (g_load_error ? "Catalogue unavailable"
                                     : "The catalogue ships with FinnOS");
    float maxw = right_limit - left_x - s * 80.0f;
    if (maxw < s * 40.0f) maxw = s * 40.0f;
    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, note, maxw);
    float nw = finn_text_width(&g_f_micro, fitted);
    c_text(c, &g_f_micro, right_limit - nw,
           L->footer_base + finn_text_cap_height(&g_f_micro), fitted,
           g_status[0] && g_status_error ? FINN_INK : FINN_MUTE);
}

static void draw_scrollbar(store_canvas *c, const store_layout *L, float s,
                           float vy, float vh, int scroll, int max) {
    if (max <= 0 || vh <= 0.0f) return;
    float thumb_h = vh * vh / (vh + (float)max * s);
    if (thumb_h < s * 24.0f) thumb_h = s * 24.0f;
    if (thumb_h > vh) thumb_h = vh;
    float t = (float)scroll / (float)max;
    float ty = vy + (vh - thumb_h) * t;
    c_fill(c, L->scrollbar_x, ty, s * 3.0f, thumb_h, s * 1.5f,
           with_alpha(FINN_MUTE, 0x66));
}

static uint32_t *scratch_begin(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    size_t need = (size_t)w * (size_t)h;
    if (need > g_scratch_cap) {
        uint32_t *p = realloc(g_scratch, need * sizeof(uint32_t));
        if (!p) return NULL;
        g_scratch = p;
        g_scratch_cap = need;
    }
    memset(g_scratch, 0, need * sizeof(uint32_t));
    return g_scratch;
}

/*
 * An offscreen buffer has to start as the surface it is going to replace,
 * or the glass drawn into it frosts the wallpaper instead of the window and
 * the cards read as stickers on a different material. The window body has
 * already been painted into the layer by the time a view renders, so the
 * scratch borrows exactly those pixels and puts them back under the new
 * content.
 */
static void copy_from_layer(uint32_t *dst, int dw, int dh,
                            const uint32_t *src, int sw, int sh,
                            int ox, int oy) {
    for (int y = 0; y < dh; y++) {
        int sy = oy + y;
        if (sy < 0 || sy >= sh) continue;
        int x0 = ox < 0 ? -ox : 0;
        int x1 = dw;
        if (ox + x1 > sw) x1 = sw - ox;
        if (x1 <= x0) continue;
        memcpy(dst + (size_t)y * dw + x0, src + (size_t)sy * sw + ox + x0,
               (size_t)(x1 - x0) * sizeof(uint32_t));
    }
}

/* ---------------- Home ---------------- */

typedef struct {
    int hero;
    int top_apps[FINN_APP_MAX];
    int top_n;
    int inst_apps[FINN_APP_MAX];
    int inst_n;
    int nvis;
    float top_label_y, top_y;
    float inst_label_y, inst_y, inst_h;
    float content_h;
} home_geom;

static int hero_index(void) {
    int n = finn_apps_count();
    int first_avail = -1, first = -1;
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a || !matches(i)) continue;
        if (first < 0) first = i;
        if (a->installed) return i;
        if (first_avail < 0 && app_available(a)) first_avail = i;
    }
    return first_avail >= 0 ? first_avail : first;
}

static float home_link_width(const char *label, float s) {
    return finn_text_width(&g_f_micro, label) + s * 18.0f;
}

static void home_link_rect(const store_layout *L, const char *label, float s,
                           float label_y, float *x, float *y,
                           float *w, float *h) {
    *w = home_link_width(label, s);
    *x = L->content_w - *w;
    *y = label_y * s;
    *h = s * LABEL_H;
}

static void home_build(const store_layout *L, float s, home_geom *H) {
    (void)s;
    memset(H, 0, sizeof *H);
    H->nvis = visible_count();
    if (H->nvis == 0) {
        H->hero = -1;
        H->content_h = 74.0f;
        return;
    }

    H->hero = hero_index();

    int sel_k = visible_index_of(selection_index());
    if (sel_k < 0) sel_k = 0;
    int cap = L->shelf_cols;
    int start = 0;
    if (sel_k >= cap) start = sel_k - cap + 1;
    int maxstart = H->nvis - cap;
    if (maxstart < 0) maxstart = 0;
    if (start > maxstart) start = maxstart;
    H->top_n = H->nvis - start;
    if (H->top_n > cap) H->top_n = cap;
    for (int k = 0; k < H->top_n; k++)
        H->top_apps[k] = visible_at(start + k);

    int inst_total = installed_visible_count();
    H->inst_n = inst_total < cap ? inst_total : cap;
    for (int k = 0; k < H->inst_n; k++)
        H->inst_apps[k] = installed_visible_at(k);

    H->top_label_y = HERO_H + HERO_GAP;
    H->top_y = H->top_label_y + LABEL_H + CARD_TOP_GAP;
    H->inst_label_y = H->top_y + SHELF_CARD_H + SECTION_GAP;
    H->inst_y = H->inst_label_y + LABEL_H + CARD_TOP_GAP;
    H->inst_h = H->inst_n > 0 ? SHELF_CARD_H : 54.0f;
    H->content_h = H->inst_y + H->inst_h;
}

static void draw_link(store_canvas *c, const store_layout *L, const char *label,
                      float s, float label_y, float scroll_px) {
    float x, y, w, h;
    home_link_rect(L, label, s, label_y, &x, &y, &w, &h);
    y -= scroll_px;
    float cap = finn_text_cap_height(&g_f_micro);
    c_text(c, &g_f_micro, x, y + cap, label, FINN_DIM);
    c_icon_ink(c, &g_ic_next, x + w - s * 5.0f, y + h * 0.5f,
               s * 10.0f, FINN_MUTE, FINN_MUTE);
}

static void draw_hero(store_canvas *c, const store_layout *L, float s,
                      float y, const finn_app *a, int idx) {
    float w = L->content_w;
    card_bg(c, 0.0f, y, w, s * HERO_H, s, 0);
    c_fill(c, 0.0f, y, w, s * HERO_H, s * CARD_R,
           finn_theme_dark() ? 0x0E0A84FFu : 0x07006FFFu);

    float icon = s * 64.0f;
    draw_app_icon(c, a, idx, s * 18.0f, y + (s * HERO_H - icon) * 0.5f, icon);

    float tx = s * 18.0f + icon + s * 16.0f;
    float view_w = s * 58.0f, view_h = s * 26.0f;
    float view_x = w - s * 18.0f - view_w;
    float view_y = y + (s * HERO_H - view_h) * 0.5f;
    draw_button(c, view_x, view_y, view_w, view_h, s, "View", BTN_QUIET);

    float maxw = view_x - s * 14.0f - tx;
    if (maxw < s * 40.0f) maxw = s * 40.0f;

    c_text(c, &g_f_micro, tx, y + s * 20.0f,
           "FEATURED APP", FINN_MUTE);

    char fitted[128];
    finnapp_ellipsis(&g_f_hero, fitted, sizeof fitted, a->name, maxw);
    c_text(c, &g_f_hero, tx, y + s * 48.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                     a->note[0] ? a->note : a->id, maxw);
    c_text(c, &g_f_sub, tx, y + s * 68.0f, fitted, FINN_DIM);

    draw_status_pill(c, tx, y + s * 78.0f, s, a);
}

static void draw_shelf_card(store_canvas *c, float x, float y, float w,
                            float s, const finn_app *a, int idx, int selected) {
    float h = s * SHELF_CARD_H;
    card_bg(c, x, y, w, h, s, selected);
    draw_app_icon(c, a, idx, x + s * 12.0f, y + s * 12.0f, s * 38.0f);

    float maxw = w - s * 24.0f;
    char fitted[128];
    finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, a->name, maxw);
    c_text(c, &g_f_label, x + s * 12.0f, y + s * 70.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted,
                     a->note[0] ? a->note : a->id, maxw);
    c_text(c, &g_f_micro, x + s * 12.0f, y + s * 86.0f, fitted, FINN_MUTE);

    draw_status_line(c, x + s * 12.0f, y + s * 101.0f, s, a);
}

static void draw_home(store_canvas *c, const store_layout *L, float s,
                      const home_geom *H, float scroll_px) {
    if (H->nvis == 0) {
        float y = -scroll_px;
        if (g_load_error)
            quiet_well(c, 0.0f, y, L->content_w, s * 74.0f, s,
                       "The app catalogue could not be read.",
                       "The registry is missing or unreadable.");
        else if (g_filter[0])
            quiet_well(c, 0.0f, y, L->content_w, s * 74.0f, s,
                       "No app matches that search.",
                       "Clear the search to see the catalogue.");
        else
            quiet_well(c, 0.0f, y, L->content_w, s * 74.0f, s,
                       "The catalogue is empty.",
                       "This machine's registry has no entries.");
        return;
    }

    const finn_app *hero = H->hero >= 0 ? finn_apps_at(H->hero) : NULL;
    if (hero) draw_hero(c, L, s, -scroll_px, hero, H->hero);

    float y = H->top_label_y * s - scroll_px;
    float cap = finn_text_cap_height(&g_f_micro);
    c_text(c, &g_f_micro, 0.0f, y + cap, "TOP APPS", FINN_MUTE);
    draw_link(c, L, "All apps", s, H->top_label_y, scroll_px);

    float gap = s * CARD_GAP;
    float slot = (L->content_w - gap * (float)(L->shelf_cols - 1)) /
                 (float)L->shelf_cols;
    for (int k = 0; k < H->top_n; k++) {
        int idx = H->top_apps[k];
        const finn_app *a = finn_apps_at(idx);
        if (!a) continue;
        float cx = (float)k * (slot + gap);
        draw_shelf_card(c, cx, H->top_y * s - scroll_px, slot, s, a, idx,
                        idx == selection_index());
    }

    y = H->inst_label_y * s - scroll_px;
    c_text(c, &g_f_micro, 0.0f, y + cap, "INSTALLED", FINN_MUTE);
    draw_link(c, L, "Library", s, H->inst_label_y, scroll_px);

    if (H->inst_n == 0) {
        const char *line = g_filter[0] ? "No installed app matches that search."
                                       : "No installed apps yet.";
        const char *sub = g_filter[0] ? "Clear the search to see the library."
                                      : "Install one from Apps.";
        quiet_well(c, 0.0f, H->inst_y * s - scroll_px, L->content_w,
                   s * 54.0f, s, line, sub);
    } else {
        for (int k = 0; k < H->inst_n; k++) {
            int idx = H->inst_apps[k];
            const finn_app *a = finn_apps_at(idx);
            if (!a) continue;
            float cx = (float)k * (slot + gap);
            draw_shelf_card(c, cx, H->inst_y * s - scroll_px, slot, s, a, idx, 0);
        }
    }
}

/* ---------------- Apps ---------------- */

typedef struct {
    int nvis;
    float list_y;
    float content_h;
} apps_geom;

static void apps_build(const store_layout *L, float s, apps_geom *G) {
    (void)s;
    G->nvis = visible_count();
    G->list_y = LABEL_H + 8.0f;
    if (G->nvis == 0) {
        G->content_h = 74.0f;
        return;
    }
    int rows = (G->nvis + L->grid_cols - 1) / L->grid_cols;
    G->content_h = G->list_y + (float)rows * GRID_CARD_H +
                   (float)(rows - 1) * CARD_GAP;
}

static float grid_slot(const store_layout *L, float s) {
    int cols = L->grid_cols;
    return (L->content_w - s * CARD_GAP * (float)(cols - 1)) / (float)cols;
}

static void grid_rect(const store_layout *L, float s, int k,
                      float *x, float *y, float *w, float *h) {
    float slot = grid_slot(L, s);
    float gap = s * CARD_GAP;
    int row = k / L->grid_cols;
    int col = k % L->grid_cols;
    *x = (float)col * (slot + gap);
    *y = (LABEL_H + 8.0f) + (float)row * (s * GRID_CARD_H + gap);
    *w = slot;
    *h = s * GRID_CARD_H;
}

static void grid_action_rect(const store_layout *L, float s, int k,
                             const finn_app *a, float *x, float *y,
                             float *w, float *h) {
    float cx, cy, cw, ch;
    grid_rect(L, s, k, &cx, &cy, &cw, &ch);
    *w = button_width(a, s);
    *h = s * 28.0f;
    *x = cx + cw - s * 14.0f - *w;
    *y = cy + s * 118.0f;
}

static void draw_grid_card(store_canvas *c, const store_layout *L, float s,
                           int k, const finn_app *a, int idx, int selected) {
    float x, y, w, h;
    grid_rect(L, s, k, &x, &y, &w, &h);
    card_bg(c, x, y, w, h, s, selected);
    draw_app_icon(c, a, idx, x + s * 14.0f, y + s * 14.0f, s * 46.0f);

    float maxw = w - s * 28.0f;
    char fitted[128];
    finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, a->name, maxw);
    c_text(c, &g_f_label, x + s * 14.0f, y + s * 78.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                     a->note[0] ? a->note : a->id, maxw);
    c_text(c, &g_f_sub, x + s * 14.0f, y + s * 95.0f, fitted, FINN_MUTE);

    c_fill(c, x + s * 14.0f, y + s * 108.0f, w - s * 28.0f, 1.0f,
           0.0f, FINN_HAIR);

    float row_y = y + s * 118.0f;
    draw_status_line(c, x + s * 14.0f,
                     row_y + s * 14.0f +
                     finn_text_cap_height(&g_f_micro) * 0.5f,
                     s, a);

    float ax, ay, aw, ah;
    grid_action_rect(L, s, k, a, &ax, &ay, &aw, &ah);
    draw_button(c, ax, ay, aw, ah, s, action_label(a), action_kind(a));
}

static void draw_apps(store_canvas *c, const store_layout *L, float s,
                      const apps_geom *G, float scroll_px) {
    float cap = finn_text_cap_height(&g_f_micro);
    c_text(c, &g_f_micro, 0.0f, -scroll_px + cap, "ALL APPS", FINN_MUTE);

    char count[64];
    if (g_filter[0])
        snprintf(count, sizeof count, "%d match%s", G->nvis,
                 G->nvis == 1 ? "" : "es");
    else
        snprintf(count, sizeof count, "%d app%s", G->nvis,
                 G->nvis == 1 ? "" : "s");
    float cw = finn_text_width(&g_f_micro, count);
    c_text(c, &g_f_micro, L->content_w - cw, -scroll_px + cap, count, FINN_MUTE);

    if (G->nvis == 0) {
        if (g_load_error)
            quiet_well(c, 0.0f, -scroll_px, L->content_w, s * 74.0f, s,
                       "The app catalogue could not be read.",
                       "The registry is missing or unreadable.");
        else
            quiet_well(c, 0.0f, -scroll_px, L->content_w, s * 74.0f, s,
                       g_filter[0] ? "No app matches that search."
                                   : "The catalogue is empty.",
                       g_filter[0] ? "Try a different name or id."
                                   : "This machine's registry has no entries.");
        return;
    }

    for (int k = 0; k < G->nvis; k++) {
        int idx = visible_at(k);
        const finn_app *a = finn_apps_at(idx);
        if (!a) continue;
        int selected = idx == selection_index();
        float x, y, w, h;
        grid_rect(L, s, k, &x, &y, &w, &h);
        draw_grid_card(c, L, s, k, a, idx, selected);
        (void)h;
    }
}

/* ---------------- Library ---------------- */

typedef struct {
    int n;
    float list_y;
    float content_h;
} library_geom;

static void library_build(const store_layout *L, float s, library_geom *G) {
    (void)L; (void)s;
    G->n = installed_visible_count();
    G->list_y = LABEL_H + 8.0f;
    if (G->n == 0) {
        G->content_h = 78.0f;
        return;
    }
    G->content_h = G->list_y + (float)G->n * LIB_ROW_H +
                   (float)(G->n - 1) * 8.0f;
}

static void library_row_rect(const store_layout *L, float s, int k,
                             float *x, float *y, float *w, float *h) {
    (void)L;
    *x = 0.0f;
    *y = (LABEL_H + 8.0f) + (float)k * (s * LIB_ROW_H + s * 8.0f);
    *w = L->content_w;
    *h = s * LIB_ROW_H;
}

static void library_action_rect(const store_layout *L, float s, int k,
                                const finn_app *a, float *x, float *y,
                                float *w, float *h) {
    (void)a;
    float rx, ry, rw, rh;
    library_row_rect(L, s, k, &rx, &ry, &rw, &rh);
    *w = s * 68.0f;
    *h = s * 28.0f;
    *x = rx + rw - s * 14.0f - *w;
    *y = ry + (rh - *h) * 0.5f;
}

static void draw_library_row(store_canvas *c, const store_layout *L, float s,
                             int k, const finn_app *a, int idx, int selected) {
    float x, y, w, h;
    library_row_rect(L, s, k, &x, &y, &w, &h);
    card_bg(c, x, y, w, h, s, selected);

    float icon = s * 38.0f;
    draw_app_icon(c, a, idx, x + s * 14.0f, y + (h - icon) * 0.5f, icon);

    float ax, ay, aw, ah;
    library_action_rect(L, s, k, a, &ax, &ay, &aw, &ah);

    float text_x = x + s * 64.0f;
    float text_max = ax - s * 12.0f - text_x;
    if (L->content_w >= s * 420.0f) {
        /* The row already says "Installed" by being in Library; the pill is
         * only there to keep the state explicit when there is room. */
        float pw = finn_text_width(&g_f_micro, "Installed") + s * 28.0f;
        draw_status_pill(c, ax - s * 12.0f - pw,
                         y + (h - s * 22.0f) * 0.5f, s, a);
        text_max = ax - s * 12.0f - pw - s * 12.0f - text_x;
    }
    if (text_max < s * 30.0f) text_max = s * 30.0f;

    char fitted[128];
    finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, a->name, text_max);
    c_text(c, &g_f_label, text_x, y + s * 27.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                     a->note[0] ? a->note : a->id, text_max);
    c_text(c, &g_f_sub, text_x, y + s * 44.0f, fitted, FINN_MUTE);

    draw_button(c, ax, ay, aw, ah, s, action_label(a), action_kind(a));
}

static void draw_library(store_canvas *c, const store_layout *L, float s,
                         const library_geom *G, float scroll_px) {
    float cap = finn_text_cap_height(&g_f_micro);
    c_text(c, &g_f_micro, 0.0f, -scroll_px + cap, "INSTALLED APPS", FINN_MUTE);

    char count[64];
    snprintf(count, sizeof count, "%d app%s", G->n, G->n == 1 ? "" : "s");
    float cw = finn_text_width(&g_f_micro, count);
    c_text(c, &g_f_micro, L->content_w - cw, -scroll_px + cap, count, FINN_MUTE);

    if (G->n == 0) {
        if (g_load_error)
            quiet_well(c, 0.0f, -scroll_px, L->content_w, s * 78.0f, s,
                       "The app catalogue could not be read.",
                       "The registry is missing or unreadable.");
        else if (g_filter[0])
            quiet_well(c, 0.0f, -scroll_px, L->content_w, s * 78.0f, s,
                       "No installed app matches that search.",
                       "Clear the search to see the library.");
        else
            quiet_well(c, 0.0f, -scroll_px, L->content_w, s * 78.0f, s,
                       "Nothing is installed from the catalogue.",
                       "Install an app from Apps; the Store does not download.");
        return;
    }

    for (int k = 0; k < G->n; k++) {
        int idx = installed_visible_at(k);
        const finn_app *a = finn_apps_at(idx);
        if (!a) continue;
        draw_library_row(c, L, s, k, a, idx, idx == selection_index());
    }
}

/* ---------------- detail ---------------- */

typedef struct {
    float back_x, back_y, back_w, back_h;
    float action_x, action_y, action_w, action_h;
    float vp_y, vp_h;
} detail_geom;

static int detail_stacked(const store_layout *L, float s) {
    return L->content_w < s * 420.0f;
}

static void detail_metrics(const store_layout *L, float s, detail_geom *G) {
    G->back_w = s * DETAIL_BACK_W;
    G->back_h = s * DETAIL_BACK_H;
    G->back_x = L->content_x;
    G->back_y = L->content_y;

    G->action_h = s * ACTION_H;
    G->action_w = s * DETAIL_ACTION_W;
    if (G->action_w > L->content_w) G->action_w = L->content_w;
    G->action_x = L->content_x + L->content_w - G->action_w;
    G->action_y = L->content_bottom - G->action_h;

    G->vp_y = L->content_y + G->back_h + s * 8.0f;
    G->vp_h = G->action_y - s * 8.0f - G->vp_y;
    if (G->vp_h < 0.0f) G->vp_h = 0.0f;
}

static const char *detail_explain(const finn_app *a) {
    if (!a) return "";
    if (a->installed)
        return "This app is marked installed for this machine. "
               "The Start Menu opens it from its tile.";
    if (app_available(a))
        return "This app is in the catalogue but is not installed. "
               "Installing marks it available to the Start Menu.";
    return "This app is in the catalogue, but its launcher is not on this "
           "machine. It cannot be installed here.";
}

#define DETAIL_NOTE \
    "Finn Store installs from the catalogue that ships with FinnOS. " \
    "This build does not download packages."

static float detail_content_h(const store_layout *L, float s,
                              const finn_app *a) {
    int stacked = detail_stacked(L, s);
    float block = stacked ? 152.0f : DETAIL_ICON_W;
    int lines = wrap_lines(&g_f_sub, detail_explain(a), L->content_w);
    int flines = wrap_lines(&g_f_micro, DETAIL_NOTE, L->content_w);
    float divider = block + 14.0f;
    float about_top = divider + 14.0f;
    float body_top = about_top + 20.0f;
    float body_end = body_top + (float)lines * 15.0f;
    float meta_y = body_end + 14.0f;
    float foot_y = meta_y + 84.0f + 12.0f;
    return foot_y + (float)flines * 12.0f + 8.0f;
}

static void draw_meta_row(store_canvas *c, float x, float y, float w,
                          float s, const char *label, const char *value) {
    float cap_l = finn_text_cap_height(&g_f_sub);
    float cap_v = finn_text_cap_height(&g_f_label);
    float middle = y + s * 14.0f;
    c_text(c, &g_f_sub, x + s * 14.0f, middle + cap_l * 0.5f,
           label, FINN_MUTE);

    float lw = finn_text_width(&g_f_sub, label);
    float maxw = w - s * 28.0f - lw - s * 12.0f;
    char fitted[192];
    finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, value,
                     maxw > s * 20.0f ? maxw : s * 20.0f);
    float vw = finn_text_width(&g_f_label, fitted);
    c_text(c, &g_f_label, x + w - s * 14.0f - vw,
           middle + cap_v * 0.5f, fitted, FINN_INK);
}

static void draw_detail_content(store_canvas *c, const store_layout *L,
                                float s, const finn_app *a, int idx,
                                int stacked, float scroll_px) {
    float sy = -scroll_px;
    float block;

    if (stacked) {
        draw_app_icon(c, a, idx, 0.0f, sy, s * DETAIL_ICON_N);
        float tx = 0.0f;
        float ty = sy + s * 76.0f;
        char fitted[128];
        finnapp_ellipsis(&g_f_hero, fitted, sizeof fitted, a->name,
                         L->content_w);
        c_text(c, &g_f_hero, tx, ty + s * 20.0f, fitted, FINN_INK);
        finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                         a->note[0] ? a->note : a->id, L->content_w);
        c_text(c, &g_f_sub, tx, ty + s * 38.0f, fitted, FINN_DIM);
        draw_status_pill(c, tx, ty + s * 48.0f, s, a);
        block = 152.0f;
    } else {
        draw_app_icon(c, a, idx, 0.0f, sy, s * DETAIL_ICON_W);
        float tx = s * 100.0f;
        float far = L->content_w - tx;
        char fitted[128];
        finnapp_ellipsis(&g_f_hero, fitted, sizeof fitted, a->name, far);
        c_text(c, &g_f_hero, tx, sy + s * 26.0f, fitted, FINN_INK);
        finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                         a->note[0] ? a->note : a->id, far);
        c_text(c, &g_f_sub, tx, sy + s * 48.0f, fitted, FINN_DIM);
        draw_status_pill(c, tx, sy + s * 58.0f, s, a);
        block = DETAIL_ICON_W;
    }

    float divider = block + 14.0f;
    c_fill(c, 0.0f, sy + s * divider, L->content_w, 1.0f, 0.0f, FINN_HAIR);

    float about_top = divider + 14.0f;
    c_text(c, &g_f_micro, 0.0f,
           sy + s * about_top + finn_text_cap_height(&g_f_micro),
           "ABOUT THIS APP", FINN_MUTE);

    float body_top = about_top + 20.0f;
    int lines = draw_wrapped(c, &g_f_sub, detail_explain(a), 0.0f,
                             sy + s * body_top, L->content_w, s * 15.0f,
                             FINN_DIM);

    float meta_y = body_top + (float)lines * 15.0f + 14.0f;
    float meta_h = 84.0f;
    c_fill(c, 0.0f, sy + s * meta_y, L->content_w, s * meta_h,
           s * CARD_R, finn_theme_dark() ? 0x1AFFFFFFu : 0x0D000000u);
    c_stroke(c, 0.0f, sy + s * meta_y, L->content_w, s * meta_h,
             s * CARD_R, 1.0f, FINN_HAIR);

    draw_meta_row(c, 0.0f, sy + s * meta_y, L->content_w, s,
                  "Catalogue id", a->id);
    c_fill(c, s * 14.0f, sy + s * (meta_y + 28.0f),
           L->content_w - s * 28.0f, 1.0f, 0.0f, FINN_HAIR);
    draw_meta_row(c, 0.0f, sy + s * (meta_y + 28.0f), L->content_w, s,
                  "Launcher", a->command[0] ? a->command : "None");
    c_fill(c, s * 14.0f, sy + s * (meta_y + 56.0f),
           L->content_w - s * 28.0f, 1.0f, 0.0f, FINN_HAIR);
    draw_meta_row(c, 0.0f, sy + s * (meta_y + 56.0f), L->content_w, s,
                  "Source", "Built-in catalogue");

    float foot_y = meta_y + meta_h + 12.0f;
    draw_wrapped(c, &g_f_micro, DETAIL_NOTE, 0.0f, sy + s * foot_y,
                 L->content_w, s * 12.0f, FINN_MUTE);
}

static void draw_detail_fixed(store_canvas *c, const store_layout *L,
                              float s, const detail_geom *G,
                              const finn_app *a) {
    /* Back is a quiet pill rather than a bare arrow: it stays visible over
     * the bright icon artwork at the top of the page. */
    float bx = G->back_x, by = G->back_y;
    float bh = G->back_h, bw = G->back_w;
    c_fill(c, bx, by, bw, bh, bh * 0.5f,
           finn_theme_dark() ? 0x1AFFFFFFu : 0x0A000000u);
    c_stroke(c, bx, by, bw, bh, bh * 0.5f, 1.0f, FINN_HAIR);
    c_icon_ink(c, &g_ic_back, bx + s * 13.0f, by + bh * 0.5f,
               s * 11.0f, FINN_DIM, FINN_DIM);
    c_text(c, &g_f_sub, bx + s * 24.0f,
           by + bh * 0.5f + finn_text_cap_height(&g_f_sub) * 0.5f,
           "Back", FINN_DIM);

    c_fill(c, G->back_x, G->action_y - s * 10.0f, L->content_w, 1.0f,
           0.0f, FINN_HAIR);

    if (g_confirm_id[0] && a && !strcmp(g_confirm_id, a->id)) {
        float maxw = G->action_x - s * 14.0f - G->back_x;
        if (maxw > s * 40.0f) {
            char fitted[128];
            finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                             "Press Remove again to confirm.", maxw);
            c_text(c, &g_f_sub, G->back_x,
                   G->action_y + G->action_h * 0.5f +
                   finn_text_cap_height(&g_f_sub) * 0.5f,
                   fitted, FINN_DIM);
        }
    }

    draw_button(c, G->action_x, G->action_y, G->action_w, G->action_h, s,
                action_label(a), action_kind(a));
}

/* ---------------- render ---------------- */

static uint32_t *g_ui = NULL;
static size_t g_ui_cap = 0;

static uint32_t *ui_begin(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    size_t need = (size_t)w * (size_t)h;
    if (need > g_ui_cap) {
        uint32_t *p = realloc(g_ui, need * sizeof(uint32_t));
        if (!p) return NULL;
        g_ui = p;
        g_ui_cap = need;
    }
    memset(g_ui, 0, need * sizeof(uint32_t));
    return g_ui;
}

void finnstoreapp_render(uint32_t *layer, int lw, int lh,
                         int lx0, int ly0, int wx, int wy, int ww, int wh,
                         float scale, const finn_state *st, int focused) {
    (void)st;
    if (!g_ready || !g_font_dir[0]) return;
    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }

    float s = scale * FINN_APP_REF;
    store_layout L;
    layout_for(ww, wh, s, &L);
    ensure_selection();
    g_last_cols = L.grid_cols;

    int total = finn_apps_count();
    int installed = finn_apps_installed_count();

    home_geom HG;
    apps_geom AG;
    library_geom LG;
    detail_geom DG;
    memset(&HG, 0, sizeof HG);
    memset(&AG, 0, sizeof AG);
    memset(&LG, 0, sizeof LG);
    memset(&DG, 0, sizeof DG);

    const finn_app *det = NULL;
    int det_idx = -1;
    float content_h = 0.0f, viewport_h = (L.content_bottom - L.content_y) / s;

    if (g_detail) {
        det_idx = selection_index();
        det = det_idx >= 0 ? finn_apps_at(det_idx) : NULL;
        if (!det) {
            g_detail = 0;
        } else {
            detail_metrics(&L, s, &DG);
            content_h = detail_content_h(&L, s, det);
            viewport_h = DG.vp_h / s;
        }
    }

    if (!g_detail) {
        if (g_page == STORE_PAGE_HOME) {
            home_build(&L, s, &HG);
            content_h = HG.content_h;
        } else if (g_page == STORE_PAGE_APPS) {
            apps_build(&L, s, &AG);
            content_h = AG.content_h;
        } else {
            library_build(&L, s, &LG);
            content_h = LG.content_h;
        }
    }
    if (viewport_h < 0.0f) viewport_h = 0.0f;

    int vi = g_detail ? STORE_PAGE_COUNT : g_page;
    int *scrollp = g_detail ? &g_detail_scroll : &g_scroll[g_page];
    int maxscroll = (int)ceilf(content_h - viewport_h);
    if (maxscroll < 0) maxscroll = 0;
    g_max_scroll[vi] = maxscroll;
    if (*scrollp > maxscroll) *scrollp = maxscroll;
    if (*scrollp < 0) *scrollp = 0;

    if (g_reveal_pending && !g_detail) {
        int sel = selection_index();
        float ty = -1.0f, th = 0.0f;
        if (sel >= 0) {
            if (g_page == STORE_PAGE_HOME && HG.nvis > 0) {
                ty = HG.top_y;
                th = SHELF_CARD_H;
            } else if (g_page == STORE_PAGE_APPS) {
                int k = visible_index_of(sel);
                if (k >= 0) {
                    ty = AG.list_y +
                         (float)(k / L.grid_cols) * (GRID_CARD_H + CARD_GAP);
                    th = GRID_CARD_H;
                }
            } else if (g_page == STORE_PAGE_LIBRARY) {
                int k = installed_visible_index_of(sel);
                if (k >= 0) {
                    ty = LG.list_y + (float)k * (LIB_ROW_H + 8.0f);
                    th = LIB_ROW_H;
                }
            }
        }
        if (ty >= 0.0f) {
            if ((float)*scrollp > ty)
                *scrollp = (int)floorf(ty);
            else if ((float)*scrollp + viewport_h < ty + th)
                *scrollp = (int)ceilf(ty + th - viewport_h);
        }
        if (*scrollp > maxscroll) *scrollp = maxscroll;
        if (*scrollp < 0) *scrollp = 0;
        g_reveal_pending = 0;
    }

    float scroll_px = (float)(*scrollp) * s;

    /* Chrome. It is drawn into its own scratch so the window's rounded edge
     * and the content's scroll clip are the only two clips in this surface. */
    uint32_t *ui = ui_begin(ww, wh);
    if (!ui) return;
    copy_from_layer(ui, ww, wh, layer, lw, lh, wx - lx0, wy - ly0);
    store_canvas uc = { ui, ww, wh, lx0 + wx, ly0 + wy };

    if (L.rail) draw_rail(&uc, &L, s);
    else draw_tabs(&uc, &L, s);
    draw_header(&uc, &L, s, focused && g_filter_focused);
    draw_footer(&uc, &L, s, total, installed);

    if (g_detail && det) {
        draw_detail_fixed(&uc, &L, s, &DG, det);
        draw_scrollbar(&uc, &L, s, DG.vp_y, DG.vp_h,
                       g_detail_scroll, g_max_scroll[STORE_PAGE_COUNT]);
    } else {
        draw_scrollbar(&uc, &L, s, L.content_y,
                       L.content_bottom - L.content_y,
                       g_scroll[g_page], g_max_scroll[g_page]);
    }

    finn_blit(layer, lw, lh, ui, ww, wh, wx - lx0, wy - ly0);

    /* Scrolled content. The scratch is exactly the content box, so a card
     * that is partly out of view is clipped by the box rather than painted
     * over the header or the footer. */
    if (g_detail && det) {
        int cw = (int)lroundf(L.content_w);
        int ch = (int)lroundf(DG.vp_h);
        uint32_t *cp = scratch_begin(cw, ch);
        if (cp) {
            int cx = (int)lroundf(L.content_x);
            int cy = (int)lroundf(DG.vp_y);
            copy_from_layer(cp, cw, ch, layer, lw, lh,
                            wx - lx0 + cx, wy - ly0 + cy);
            store_canvas cc = { cp, cw, ch, lx0 + wx + cx, ly0 + wy + cy };
            draw_detail_content(&cc, &L, s, det, det_idx,
                                detail_stacked(&L, s), scroll_px);
            finn_blit(layer, lw, lh, cp, cw, ch,
                      wx - lx0 + cx, wy - ly0 + cy);
        }
    } else {
        int cw = (int)lroundf(L.content_w);
        int ch = (int)lroundf(L.content_bottom - L.content_y);
        uint32_t *cp = scratch_begin(cw, ch);
        if (cp) {
            int cx = (int)lroundf(L.content_x);
            int cy = (int)lroundf(L.content_y);
            copy_from_layer(cp, cw, ch, layer, lw, lh,
                            wx - lx0 + cx, wy - ly0 + cy);
            store_canvas cc = { cp, cw, ch, lx0 + wx + cx, ly0 + wy + cy };
            if (g_page == STORE_PAGE_HOME)
                draw_home(&cc, &L, s, &HG, scroll_px);
            else if (g_page == STORE_PAGE_APPS)
                draw_apps(&cc, &L, s, &AG, scroll_px);
            else
                draw_library(&cc, &L, s, &LG, scroll_px);
            finn_blit(layer, lw, lh, cp, cw, ch,
                      wx - lx0 + cx, wy - ly0 + cy);
        }
    }
}

/* ---------------- input ---------------- */

static int point_in(float px, float py, float x, float y, float w, float h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

int finnstoreapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                     float scale, int *out_sub) {
    if (out_sub) *out_sub = -1;
    if (px < wx || py < wy || px >= wx + ww || py >= wy + wh)
        return FINNSTORE_HIT_NONE;
    if (!g_ready || g_font_scale < 0.0f) return FINNSTORE_HIT_NONE;

    float s = scale * FINN_APP_REF;
    store_layout L;
    layout_for(ww, wh, s, &L);

    float x = (float)(px - wx);
    float y = (float)(py - wy);
    if (y < s * 44.0f) return FINNSTORE_HIT_NONE;   /* the titlebar belongs to the WM */

    for (int i = 0; i < 3; i++) {
        float nx, ny, nw, nh;
        nav_item_rect(&L, i, s, &nx, &ny, &nw, &nh);
        if (point_in(x, y, nx, ny, nw, nh)) {
            if (out_sub) *out_sub = i;
            return FINNSTORE_HIT_NAV;
        }
    }

    if (point_in(x, y, L.filter_x, L.filter_y, L.filter_w, L.filter_h)) {
        if (g_filter[0]) {
            float cx, cy, cw, ch;
            filter_clear_rect(&L, s, &cx, &cy, &cw, &ch);
            if (point_in(x, y, cx, cy, cw, ch))
                return FINNSTORE_HIT_FILTER_CLEAR;
        }
        return FINNSTORE_HIT_FILTER;
    }

    if (g_detail) {
        detail_geom G;
        detail_metrics(&L, s, &G);
        if (point_in(x, y, G.back_x, G.back_y, G.back_w, G.back_h))
            return FINNSTORE_HIT_BACK;
        if (point_in(x, y, G.action_x, G.action_y, G.action_w, G.action_h))
            return FINNSTORE_HIT_DETAIL_ACTION;
        return FINNSTORE_HIT_BODY;
    }

    if (y < L.content_y || y >= L.content_bottom ||
        x < L.content_x || x >= L.content_x + L.content_w)
        return FINNSTORE_HIT_BODY;

    float lx = x - L.content_x;
    float ly = y - L.content_y + (float)g_scroll[g_page] * s;
    float gap = s * CARD_GAP;

    if (g_page == STORE_PAGE_HOME) {
        home_geom H;
        home_build(&L, s, &H);
        if (H.nvis == 0) return FINNSTORE_HIT_BODY;

        if (H.hero >= 0 && point_in(lx, ly, 0.0f, 0.0f,
                                    L.content_w, s * HERO_H)) {
            if (out_sub) *out_sub = H.hero;
            return FINNSTORE_HIT_CARD;
        }

        float ax, ay, aw, ah;
        home_link_rect(&L, "All apps", s, H.top_label_y, &ax, &ay, &aw, &ah);
        if (point_in(lx, ly, ax, ay, aw, ah)) {
            if (out_sub) *out_sub = STORE_PAGE_APPS;
            return FINNSTORE_HIT_SEE_ALL;
        }
        home_link_rect(&L, "Library", s, H.inst_label_y, &ax, &ay, &aw, &ah);
        if (point_in(lx, ly, ax, ay, aw, ah)) {
            if (out_sub) *out_sub = STORE_PAGE_LIBRARY;
            return FINNSTORE_HIT_SEE_ALL;
        }

        float slot = (L.content_w - gap * (float)(L.shelf_cols - 1)) /
                     (float)L.shelf_cols;
        for (int k = 0; k < H.top_n; k++) {
            float cx = (float)k * (slot + gap);
            if (point_in(lx, ly, cx, H.top_y * s, slot, s * SHELF_CARD_H)) {
                if (out_sub) *out_sub = H.top_apps[k];
                return FINNSTORE_HIT_CARD;
            }
        }
        for (int k = 0; k < H.inst_n; k++) {
            float cx = (float)k * (slot + gap);
            if (point_in(lx, ly, cx, H.inst_y * s, slot, s * SHELF_CARD_H)) {
                if (out_sub) *out_sub = H.inst_apps[k];
                return FINNSTORE_HIT_CARD;
            }
        }
        return FINNSTORE_HIT_BODY;
    }

    if (g_page == STORE_PAGE_APPS) {
        apps_geom G;
        apps_build(&L, s, &G);
        for (int k = 0; k < G.nvis; k++) {
            float cx, cy, cw, ch;
            grid_rect(&L, s, k, &cx, &cy, &cw, &ch);
            if (!point_in(lx, ly, cx, cy, cw, ch)) continue;
            int idx = visible_at(k);
            const finn_app *a = finn_apps_at(idx);
            if (!a) return FINNSTORE_HIT_BODY;
            float ax, ay, aw, ah;
            grid_action_rect(&L, s, k, a, &ax, &ay, &aw, &ah);
            if (point_in(lx, ly, ax, ay, aw, ah)) {
                if (out_sub) *out_sub = idx;
                return FINNSTORE_HIT_CARD_ACTION;
            }
            if (out_sub) *out_sub = idx;
            return FINNSTORE_HIT_CARD;
        }
        return FINNSTORE_HIT_BODY;
    }

    library_geom G;
    library_build(&L, s, &G);
    for (int k = 0; k < G.n; k++) {
        float cx, cy, cw, ch;
        library_row_rect(&L, s, k, &cx, &cy, &cw, &ch);
        if (!point_in(lx, ly, cx, cy, cw, ch)) continue;
        int idx = installed_visible_at(k);
        const finn_app *a = finn_apps_at(idx);
        if (!a) return FINNSTORE_HIT_BODY;
        float ax, ay, aw, ah;
        library_action_rect(&L, s, k, a, &ax, &ay, &aw, &ah);
        if (point_in(lx, ly, ax, ay, aw, ah)) {
            if (out_sub) *out_sub = idx;
            return FINNSTORE_HIT_CARD_ACTION;
        }
        if (out_sub) *out_sub = idx;
        return FINNSTORE_HIT_CARD;
    }
    return FINNSTORE_HIT_BODY;
}

/* @@PART6@@ */

/* ---------------- navigation and selection ---------------- */

static int page_count_for(int page) {
    return page == STORE_PAGE_LIBRARY ? installed_visible_count()
                                      : visible_count();
}

static int page_at(int page, int k) {
    return page == STORE_PAGE_LIBRARY ? installed_visible_at(k)
                                      : visible_at(k);
}

static int page_index_of(int page, int idx) {
    return page == STORE_PAGE_LIBRARY ? installed_visible_index_of(idx)
                                      : visible_index_of(idx);
}

/*
 * The selection is the only thing the arrows move. It also cancels a pending
 * remove: the second press is a confirmation of the app the person is still
 * looking at, and moving away is how they say no.
 */
static int select_index(int idx) {
    const finn_app *a = finn_apps_at(idx);
    if (!a) return 0;
    /* Selecting the app that is already selected is not a new intent: it
     * must not eat the pending second press that confirms a removal. */
    if (!strcmp(g_sel_id, a->id)) return 0;

    snprintf(g_sel_id, sizeof g_sel_id, "%s", a->id);
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    g_reveal_pending = 1;
    bump();
    return 1;
}

static void set_page(int page) {
    if (page < 0 || page >= STORE_PAGE_COUNT) return;

    g_page = page;
    g_detail = 0;
    g_detail_scroll = 0;
    g_filter_focused = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    ensure_selection();
    g_reveal_pending = 1;
    bump();
}

static void open_detail(int idx) {
    const finn_app *a = finn_apps_at(idx);
    if (!a) return;

    snprintf(g_sel_id, sizeof g_sel_id, "%s", a->id);
    g_detail = 1;
    g_detail_scroll = 0;
    g_filter_focused = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    bump();
}

static void close_detail(void) {
    if (!g_detail) return;
    g_detail = 0;
    g_detail_scroll = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    g_reveal_pending = 1;
    bump();
}

static void cycle_page(int delta) {
    int page = (g_page + delta + STORE_PAGE_COUNT) % STORE_PAGE_COUNT;
    set_page(page);
}

static void filter_changed(void) {
    /*
     * Typing is a request to look through the catalogue, so a product page
     * gets out of the way and the filtered grid takes over. On Home, Apps and
     * Library the filter trims the page that is already open.
     */
    if (g_detail) {
        g_detail = 0;
        g_detail_scroll = 0;
        g_page = STORE_PAGE_APPS;
    }

    g_scroll[g_page] = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    ensure_selection();
    g_reveal_pending = 1;
    bump();
}

static void filter_append(uint32_t keysym) {
    size_t len = strlen(g_filter);
    if (len + 1 >= sizeof g_filter) return;
    g_filter[len] = (char)keysym;
    g_filter[len + 1] = '\0';
    g_filter_focused = 1;
    filter_changed();
}

static int filter_backspace(void) {
    size_t len = strlen(g_filter);
    if (!len) return 0;
    g_filter[len - 1] = '\0';
    g_filter_focused = 1;
    filter_changed();
    return 1;
}

static void move_selection(int delta) {
    if (!delta) return;
    int n = page_count_for(g_page);
    if (n == 0) return;

    int k = page_index_of(g_page, selection_index());
    if (k < 0) k = delta > 0 ? -1 : n;
    k += delta;
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    select_index(page_at(g_page, k));
}

static void jump_selection(int to_end) {
    int n = page_count_for(g_page);
    if (n == 0) return;

    int changed = select_index(page_at(g_page, to_end ? n - 1 : 0));
    g_reveal_pending = 1;
    if (!changed) bump();
}

/* ---------------- input ---------------- */

int finnstoreapp_press(int hit, int sub, int px, int py, float scale) {
    (void)px; (void)py; (void)scale;

    switch (hit) {
    case FINNSTORE_HIT_FILTER:
        if (g_filter_focused) return 0;
        g_filter_focused = 1;
        bump();
        return 1;
    case FINNSTORE_HIT_FILTER_CLEAR:
        g_filter[0] = '\0';
        g_filter_focused = 1;
        filter_changed();
        return 1;
    case FINNSTORE_HIT_NAV:
        set_page(sub);
        return 1;
    case FINNSTORE_HIT_CARD:
        open_detail(sub);
        return 1;
    case FINNSTORE_HIT_CARD_ACTION:
        if (sub >= 0) {
            select_index(sub);
            do_action(sub);
        }
        return 1;
    case FINNSTORE_HIT_SEE_ALL:
        set_page(sub);
        return 1;
    case FINNSTORE_HIT_BACK:
        close_detail();
        return 1;
    case FINNSTORE_HIT_DETAIL_ACTION: {
        int idx = selection_index();
        if (idx >= 0) do_action(idx);
        return 1;
    }
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

int finnstoreapp_key(const finn_key_event *e) {
    if (!e->pressed) return 0;

    /* Tab is the page ring in both states: it is the one key that moves
     * between views rather than within one. */
    if (e->code == KEY_TAB) {
        cycle_page((e->mods & FINN_MOD_SHIFT) ? -1 : 1);
        return 1;
    }

    if (g_filter_focused) {
        if (e->code == KEY_ESC) {
            if (g_filter[0]) {
                g_filter[0] = '\0';
                filter_changed();
            } else {
                g_filter_focused = 0;
                bump();
            }
            return 1;
        }
        if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
            g_filter_focused = 0;
            bump();
            return 1;
        }
        if (e->code == KEY_BACKSPACE) {
            filter_backspace();
            return 1;
        }
        if ((e->mods & FINN_MOD_CTRL) && e->code == KEY_A) {
            g_filter[0] = '\0';
            filter_changed();
            return 1;
        }
        if (e->code == KEY_UP)   { move_selection(-1); return 1; }
        if (e->code == KEY_DOWN) { move_selection(1);  return 1; }
        if (!(e->mods & (FINN_MOD_CTRL | FINN_MOD_ALT | FINN_MOD_SUPER)) &&
            e->keysym >= 32 && e->keysym <= 126) {
            filter_append(e->keysym);
            return 1;
        }
        return 1;
    }

    switch (e->code) {
    case KEY_UP:
        if (g_detail) {
            int max = g_max_scroll[STORE_PAGE_COUNT];
            if (g_detail_scroll > 0) {
                g_detail_scroll -= 36;
                if (g_detail_scroll < 0) g_detail_scroll = 0;
                if (g_detail_scroll > max) g_detail_scroll = max;
                g_reveal_pending = 0;
                bump();
            }
        } else {
            move_selection(-1);
        }
        return 1;
    case KEY_DOWN:
        if (g_detail) {
            int max = g_max_scroll[STORE_PAGE_COUNT];
            if (g_detail_scroll < max) {
                g_detail_scroll += 36;
                if (g_detail_scroll > max) g_detail_scroll = max;
                g_reveal_pending = 0;
                bump();
            }
        } else {
            move_selection(1);
        }
        return 1;
    case KEY_LEFT:
        if (!g_detail) move_selection(-1);
        return 1;
    case KEY_RIGHT:
        if (!g_detail) move_selection(1);
        return 1;
    case KEY_HOME:
        if (g_detail) {
            if (g_detail_scroll != 0) {
                g_detail_scroll = 0;
                bump();
            }
        } else {
            jump_selection(0);
        }
        return 1;
    case KEY_END:
        if (g_detail) {
            int max = g_max_scroll[STORE_PAGE_COUNT];
            if (g_detail_scroll != max) {
                g_detail_scroll = max;
                bump();
            }
        } else {
            jump_selection(1);
        }
        return 1;
    case KEY_ENTER:
    case KEY_KPENTER:
        if (g_detail) {
            int idx = selection_index();
            if (idx >= 0) do_action(idx);
        } else {
            int idx = selection_index();
            if (idx >= 0) open_detail(idx);
        }
        return 1;
    case KEY_ESC:
        if (g_detail) {
            close_detail();
            return 1;
        }
        if (g_filter[0]) {
            g_filter[0] = '\0';
            filter_changed();
            return 1;
        }
        return 0;
    case KEY_BACKSPACE:
        return filter_backspace();
    case KEY_F5:
        reload_catalogue();
        return 1;
    default:
        if (!(e->mods & (FINN_MOD_CTRL | FINN_MOD_ALT | FINN_MOD_SUPER)) &&
            e->keysym >= 32 && e->keysym <= 126) {
            filter_append(e->keysym);
            return 1;
        }
        return 0;
    }
}

int finnstoreapp_scroll(int clicks, float scale) {
    (void)scale;
    if (!clicks) return 0;

    int vi = g_detail ? STORE_PAGE_COUNT : g_page;
    int *scrollp = g_detail ? &g_detail_scroll : &g_scroll[g_page];
    int max = g_max_scroll[vi];
    int before = *scrollp;
    int next = before - clicks * 44;

    if (next < 0) next = 0;
    if (next > max) next = max;
    if (next == before) return 0;

    *scrollp = next;
    g_reveal_pending = 0;
    bump();
    return 1;
}

/* ---------------- state identity ---------------- */

uint32_t finnstoreapp_build_key(float scale) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)

    MIX((int)(scale * 1000.0f));
    MIX(g_rev);
    MIX(g_page);
    MIX(g_detail);
    MIX(g_filter_focused);
    MIX(g_status_error);
    MIX(g_load_error);
    MIX(g_last_cols);
    MIX(g_confirm_id[0]);
    for (const char *p = g_filter; *p; p++) MIX(*p);
    for (const char *p = g_sel_id; *p; p++) MIX(*p);
    for (const char *p = g_status; *p; p++) MIX(*p);
    for (const char *p = g_confirm_id; *p; p++) MIX(*p);

    MIX(g_detail_scroll);
    for (int i = 0; i < STORE_PAGE_COUNT; i++) {
        MIX(g_scroll[i]);
        MIX(g_max_scroll[i]);
    }
    MIX(g_max_scroll[STORE_PAGE_COUNT]);

    int n = finn_apps_count();
    MIX(n);
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a) continue;
        MIX(a->installed);
        MIX(app_available(a));
        for (const char *p = a->id; *p; p++) MIX(*p);
        for (const char *p = a->name; *p; p++) MIX(*p);
        for (const char *p = a->icon; *p; p++) MIX(*p);
        for (const char *p = a->command; *p; p++) MIX(*p);
        for (const char *p = a->note; *p; p++) MIX(*p);
    }

    #undef MIX
    return h ? h : 1u;
}

/* ---------------- lifecycle ---------------- */

const char *finnstoreapp_selection(void) { return g_sel_id; }
const char *finnstoreapp_status(void) { return g_status; }

static void load_view_icon(fsvg *ic, const char *name) {
    char p[600];
    snprintf(p, sizeof p, "%s/%s", g_icon_dir, name);
    fsvg_load(p, ic);
}

int finnstoreapp_init(const char *icon_dir, const char *font_dir,
                      const char *registry) {
    if (g_ready) finnstoreapp_free();

    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");
    snprintf(g_registry, sizeof g_registry, "%s",
             registry ? registry : FINN_APPS_CATALOGUE);

    load_view_icon(&g_ic_store, "store.svg");
    load_view_icon(&g_ic_search, "search-01.svg");
    load_view_icon(&g_ic_home, "home-09.svg");
    load_view_icon(&g_ic_grid, "app-grid.svg");
    load_view_icon(&g_ic_library, "library.svg");
    load_view_icon(&g_ic_back, "chevron-left.svg");
    load_view_icon(&g_ic_next, "chevron-right.svg");
    load_view_icon(&g_ic_clear, "x.svg");

    reload_catalogue();
    g_ready = 1;
    return 0;
}

void finnstoreapp_free(void) {
    free_icons();

    fsvg_free(&g_ic_store);
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_home);
    fsvg_free(&g_ic_grid);
    fsvg_free(&g_ic_library);
    fsvg_free(&g_ic_back);
    fsvg_free(&g_ic_next);
    fsvg_free(&g_ic_clear);

    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
    g_font_scale = -1.0f;

    free(g_scratch);
    g_scratch = NULL;
    g_scratch_cap = 0;
    free(g_ui);
    g_ui = NULL;
    g_ui_cap = 0;

    g_ready = 0;
    g_font_dir[0] = '\0';
    g_icon_dir[0] = '\0';
    g_registry[0] = '\0';

    g_filter[0] = '\0';
    g_filter_focused = 0;
    g_page = STORE_PAGE_HOME;
    g_detail = 0;
    g_sel_id[0] = '\0';
    memset(g_scroll, 0, sizeof g_scroll);
    memset(g_max_scroll, 0, sizeof g_max_scroll);
    g_detail_scroll = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    g_load_error = 0;
    g_rev = 1;
    g_reveal_pending = 0;
    g_last_cols = 1;
}
