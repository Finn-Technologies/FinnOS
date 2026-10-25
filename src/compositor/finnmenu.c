/* finnmenu — the FinnOS Start Menu (ui/StartMenu.fui / Desktop - Home). */
#define _GNU_SOURCE
#include "finnmenu.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnapps.h"
#include "finnappui.h"
#include "finnblur.h"
#include "finnvg.h"
#include "finnsettingsapp.h"
#include "finntaskbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pwd.h>
#include <linux/input.h>
#include <sys/types.h>

#define REF          1.298077f

#define MENU_W       (800.0f * REF)
#define MENU_H       (500.0f * REF)
#define MENU_R       (8.0f * REF)

#define LEFT_PAD     (20.0f * REF)
#define HEAD_Y       (25.0f * REF)
#define SEARCH_X     (205.0f * REF)
#define SEARCH_Y     (17.0f * REF)
#define SEARCH_W     (178.0f * REF)
#define SEARCH_H     (28.0f * REF)
#define SEARCH_R     (7.0f * REF)

#define APPLIST_Y    (56.0f * REF)
#define APP_ROW_H    (26.0f * REF)
#define APP_ROW_GAP  (10.0f * REF)
#define APP_ICON_SZ  (26.0f * REF)
#define APP_LABEL_GAP (14.0f * REF)
#define APP_TEXT_X   (LEFT_PAD + APP_ICON_SZ + APP_LABEL_GAP)
#define ROW_RIGHT    (395.0f * REF)      /* the account row's right edge */

#define RIGHT_X      (405.0f * REF)
#define RIGHT_Y      (10.0f * REF)
#define RIGHT_W      (385.0f * REF)
#define RIGHT_H      (480.0f * REF)
#define RIGHT_R      (4.14f * REF)

#define ACCOUNT_X    (10.0f * REF)
#define ACCOUNT_Y    (450.0f * REF)
#define ACCOUNT_W    (385.0f * REF)
#define ACCOUNT_H    (40.0f * REF)
#define ACCOUNT_R    (4.14f * REF)

#define AVATAR_X     (5.0f * REF)
#define AVATAR_SZ    (30.0f * REF)
#define NAME_X       (45.0f * REF)
#define RESTART_X    (328.0f * REF)
#define POWER_X      (356.0f * REF)
#define BTN_ICO_SZ   (18.0f * REF)

#define ANCHOR_GAP   (15.0f * REF)

/* The Home frame is a 65% white fill over the same 15-unit backdrop blur as
 * the Window surface; keep its measured material weight rather than adding a
 * menu-specific opacity step. */
#define MENU_GLASS   0.65f

/* ---------------- fonts ---------------- */
static finn_font g_f_head, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];
static char  g_icon_dir[512];
static char  g_registry[512];

/* ---------------- icons ---------------- */
static fsvg g_ic_avatar, g_ic_refresh, g_ic_power, g_ic_search;
static fsvg g_ic_app_settings, g_ic_chevron_right;

typedef struct {
    fsvg svg;
    int  loaded;
    char id[32];
} app_icon;
static app_icon g_icons[FINN_APP_MAX];

/* ---------------- layer & state ---------------- */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx0 = 0, g_ly0 = 0;
static int g_open = 0;
static int g_ax = 0, g_ay = 0;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

typedef struct { int x, y, w, h; } menu_rect;
static menu_rect g_r_panel, g_r_restart, g_r_shutdown, g_r_search, g_r_pin;
static menu_rect g_r_row[FINN_APP_MAX];
static int       g_n_rows = 0;

/*
 * The pin list, read when the menu opens. It is a file, and the menu redraws
 * for every hover; reading it per frame would put the settings store in the
 * path of a pointer move. The compositor calls finnmenu_pins_changed() when
 * it writes a pin, so the cache cannot go stale under the open menu.
 */
static char g_pins[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX];
static int  g_n_pins = -1;

static void pins_load(void) {
    int n = finntaskbar_pins(g_pins, FINNTASKBAR_MAX);
    g_n_pins = n < 0 ? 0 : n;
}

static int menu_is_pinned(const char *id) {
    if (g_n_pins < 0) pins_load();
    if (!id) return 0;
    for (int i = 0; i < g_n_pins; i++)
        if (!strcmp(g_pins[i], id)) return 1;
    return 0;
}

void finnmenu_pins_changed(void) {
    pins_load();
    g_built = 0;
}

/* ---------------- unified search ---------------- */

#define MENU_RESULT_MAX 32
#define MENU_ROW_MAX    8
#define MENU_QUERY_MAX  64

typedef struct {
    int kind;
    int index;
    int score;
} menu_hit;

static char g_query[MENU_QUERY_MAX] = "";
static int  g_search_focused = 0;
static int  g_sel = -1;
static int  g_n_matches = 0;
static menu_hit g_results[MENU_RESULT_MAX];

/* The actions the shell itself can take without opening a window. They are
 * here rather than in the app list because they are not applications. */
static const struct {
    const char *title;
    const char *note;
    const char *keywords;
} g_actions[] = {
    { "Restart",    "Restart the machine",        "restart reboot power" },
    { "Shut down",  "Power the machine off",      "shut down shutdown power off" },
};
#define ACTION_COUNT ((int)(sizeof(g_actions) / sizeof(g_actions[0])))

/* ---------------- motion ---------------- */
static finn_anim g_a_open;
static finn_anim g_a_hover, g_a_press;
static finn_anim g_a_pin;
static float     g_hover = 0.0f;
static int       g_hover_idx = -1;
static int       g_hover_pin = 0;
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
    if (finn_font_load(&g_f_head, p, font_px(s, 16.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, font_px(s, 14.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, font_px(s, 12.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, font_px(s, 10.0f * REF)) != 0) return -1;
    return 0;
}

static void get_logged_in_user(char *out, size_t sz) {
    /* 1. Explicit override if present in /run/finn/state/user or /run/finn/user */
    char buf[64] = {0};
    FILE *f = fopen("/run/finn/state/user", "r");
    if (!f) f = fopen("/run/finn/user", "r");
    if (f) {
        if (fgets(buf, sizeof buf, f)) {
            char *nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);
        if (buf[0]) {
            snprintf(out, sz, "%s", buf);
            return;
        }
    }

    /* 2. $USER or $SUDO_USER from environment if not root */
    const char *env_u = getenv("SUDO_USER");
    if (!env_u || !strcmp(env_u, "root")) env_u = getenv("USER");
    if (env_u && strcmp(env_u, "root") != 0 && env_u[0]) {
        snprintf(out, sz, "%s", env_u);
        if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
        return;
    }

    /* 3. getpwuid for uid 1000 (standard desktop session user) */
    struct passwd *pw = getpwuid(1000);
    if (!pw) pw = getpwnam("finn");
    if (!pw) pw = getpwnam("abhi");
    if (pw) {
        if (pw->pw_gecos && pw->pw_gecos[0]) {
            char gecos[64];
            snprintf(gecos, sizeof gecos, "%s", pw->pw_gecos);
            char *comma = strchr(gecos, ',');
            if (comma) *comma = '\0';
            if (gecos[0]) {
                snprintf(out, sz, "%s", gecos);
                return;
            }
        }
        snprintf(out, sz, "%s", pw->pw_name);
        if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
        return;
    }

    /* 4. Fallback */
    snprintf(out, sz, "Finn");
}

static void control_wash_r(uint32_t *L, int lw, int lh, menu_rect r, float rad,
                           float amount) {
    if (amount <= 0.004f) return;
    unsigned a = (unsigned)(amount * 255.0f + 0.5f);
    if (a > 255) a = 255;
    if (a == 0) return;
    finn_fill_rrect(L, lw, lh, (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                    rad, (FINN_INK & 0x00FFFFFFu) | (a << 24));
}

int finnmenu_init(const char *icon_dir, const char *font_dir, const char *registry) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    snprintf(g_registry, sizeof g_registry, "%s", registry ? registry : FINN_APPS_CATALOGUE);

    char p[600];
    snprintf(p, sizeof p, "%s/avatar.svg", g_icon_dir);          fsvg_load(p, &g_ic_avatar);
    snprintf(p, sizeof p, "%s/refresh.svg", g_icon_dir);         fsvg_load(p, &g_ic_refresh);
    snprintf(p, sizeof p, "%s/power.svg", g_icon_dir);           fsvg_load(p, &g_ic_power);
    snprintf(p, sizeof p, "%s/search-01.svg", g_icon_dir);       fsvg_load(p, &g_ic_search);
    snprintf(p, sizeof p, "%s/settings-app.svg", g_icon_dir);    fsvg_load(p, &g_ic_app_settings);
    snprintf(p, sizeof p, "%s/chevron-right.svg", g_icon_dir);   fsvg_load(p, &g_ic_chevron_right);

    return 0;
}

void finnmenu_free(void) {
    fsvg_free(&g_ic_avatar);
    fsvg_free(&g_ic_refresh);
    fsvg_free(&g_ic_power);
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_app_settings);
    fsvg_free(&g_ic_chevron_right);

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

void finnmenu_set_open(int open) {
    open = open ? 1 : 0;
    if (open == g_open) return;
    g_open = open;

    if (open) {
        finn_apps_load(g_registry, NULL);
        pins_load();
        g_sel = -1;
        g_built = 0;
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    } else {
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
        g_press = FINNMENU_NONE;
        g_press_idx = -1;
        g_query[0] = '\0';
        g_search_focused = 0;
        g_sel = -1;
        g_built = 0;
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
        g_hover_pin = 0;
        finn_anim_jump(&g_a_pin, 0.0f);
        g_r_pin.x = g_r_pin.y = g_r_pin.w = g_r_pin.h = 0;
    }
}

int finnmenu_is_open(void) { return g_open; }
int finnmenu_visible(void) { return finn_anim_v(&g_a_open) > 0.004f; }
float finnmenu_openness(void) { return finn_anim_v(&g_a_open); }

/* The Figma reference shows NO dimming/scrim behind the Start Menu. */
float finnmenu_scrim(void) { return 0.0f; }

int finnmenu_settled(void) {
    return !finn_anim_running(&g_a_open) && finn_anim_v(&g_a_open) >= 0.999f;
}

int finnmenu_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_hover, dt);
    ch |= finn_anim_tick(&g_a_press, dt);
    ch |= finn_anim_tick(&g_a_pin, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finnmenu_running(void) {
    return finn_anim_running(&g_a_open) ||
           finn_anim_running(&g_a_hover) || finn_anim_running(&g_a_press) ||
           finn_anim_running(&g_a_pin);
}

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

/* ---------------- unified search ---------------- */

static int g_dark = 0;

static int action_count(void) { return ACTION_COUNT + 1; }

/*
 * The appearance action's label is the change, not the state: a menu that
 * says "Dark appearance" while the shell is already dark is a report, and a
 * row in a launcher has to be a verb.
 */
static void action_text(int index, char *title, size_t tcap,
                        char *note, size_t ncap) {
    if (index == FINNMENU_ACTION_RESTART) {
        snprintf(title, tcap, "%s", g_actions[0].title);
        snprintf(note, ncap, "%s", g_actions[0].note);
    } else if (index == FINNMENU_ACTION_SHUTDOWN) {
        snprintf(title, tcap, "%s", g_actions[1].title);
        snprintf(note, ncap, "%s", g_actions[1].note);
    } else {
        snprintf(title, tcap, "Switch to %s appearance",
                 g_dark ? "light" : "dark");
        snprintf(note, ncap, "Applies immediately and is remembered.");
    }
}

static int tokens_match(const char *hay, const char *query) {
    const char *p = query;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t len = (size_t)(p - start);
        if (len > 63) return 0;

        char token[64];
        memcpy(token, start, len);
        token[len] = '\0';

        /* strcasestr takes a NUL-terminated needle, and the token is one. */
        const char *found = strcasestr(hay, token);
        if (!found) return 0;
    }
    return 1;
}

/* -1 when a token is missing. Lower is a better match: the name itself,
 * then the name as a prefix, then a phrase inside it, then the metadata. */
static int result_score(const char *title, const char *extra, const char *query) {
    char hay[512];
    snprintf(hay, sizeof hay, "%s %s", title ? title : "",
             extra ? extra : "");
    if (!tokens_match(hay, query)) return -1;

    if (!strcasecmp(title, query)) return 0;
    if (!strncasecmp(title, query, strlen(query))) return 1;
    if (strcasestr(title, query)) return 2;
    return 3;
}

static int result_cmp(const void *va, const void *vb) {
    const menu_hit *a = va, *b = vb;
    if (a->score != b->score) return a->score - b->score;
    if (a->kind != b->kind) return a->kind - b->kind;
    return a->index - b->index;
}

static void build_results(void) {
    g_n_matches = 0;
    int stored = 0;
    const char *q = g_query;

    if (!q[0]) {
        int n = finn_apps_count();
        for (int i = 0; i < n; i++) {
            if (stored >= MENU_RESULT_MAX) break;
            g_results[stored].kind = FINNMENU_RESULT_APP;
            g_results[stored].index = i;
            g_results[stored].score = 0;
            stored++;
            g_n_matches++;
        }
        return;
    }

    int n = finn_apps_count();
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a) continue;
        char extra[256];
        snprintf(extra, sizeof extra, "%s %s", a->id, a->note);
        int score = result_score(a->name, extra, q);
        if (score < 0) continue;
        g_n_matches++;
        if (stored < MENU_RESULT_MAX) {
            g_results[stored].kind = FINNMENU_RESULT_APP;
            g_results[stored].index = i;
            g_results[stored].score = score;
            stored++;
        }
    }

    int pages = finnsettingsapp_page_count();
    for (int i = 0; i < pages; i++) {
        finn_settings_page_info info;
        if (finnsettingsapp_page_info(i, &info) != 0) continue;
        char extra[320];
        snprintf(extra, sizeof extra, "%s %s %s", info.id, info.section,
                 info.keywords);
        int score = result_score(info.title, extra, q);
        if (score < 0) continue;
        g_n_matches++;
        if (stored < MENU_RESULT_MAX) {
            g_results[stored].kind = FINNMENU_RESULT_SETTINGS;
            g_results[stored].index = i;
            g_results[stored].score = score;
            stored++;
        }
    }

    for (int i = 0; i < action_count(); i++) {
        char title[64], note[128];
        action_text(i, title, sizeof title, note, sizeof note);
        char extra[256];
        snprintf(extra, sizeof extra, "%s %s", note,
                 i == FINNMENU_ACTION_APPEARANCE
                     ? "appearance dark light theme mode switch" : "");
        int score = result_score(title, extra, q);
        if (score < 0) continue;
        g_n_matches++;
        if (stored < MENU_RESULT_MAX) {
            g_results[stored].kind = FINNMENU_RESULT_ACTION;
            g_results[stored].index = i;
            g_results[stored].score = score;
            stored++;
        }
    }

    qsort(g_results, (size_t)stored, sizeof g_results[0], result_cmp);
}

static int row_count(void) {
    int shown = g_n_matches;
    return shown > MENU_ROW_MAX ? MENU_ROW_MAX : shown;
}

static const finn_app *result_app(const menu_hit *r) {
    if (!r || r->kind != FINNMENU_RESULT_APP) return NULL;
    return finn_apps_at(r->index);
}

/*
 * The result the right panel is about. In search mode it is the keyboard
 * selection, because the panel has to agree with what Enter would open; in
 * the plain list it is the row under the pointer.
 */
static const menu_hit *preview_result(void) {
    if (g_query[0]) {
        if (g_sel >= 0 && g_sel < g_n_rows) return &g_results[g_sel];
        return NULL;
    }
    if (g_hover_idx >= 0 && g_hover_idx < g_n_rows)
        return &g_results[g_hover_idx];
    return NULL;
}

static fsvg *result_icon(const menu_hit *r, int row) {
    if (!r) return NULL;
    if (r->kind == FINNMENU_RESULT_APP) {
        const finn_app *a = result_app(r);
        return a ? icon_for(r->index, a) : NULL;
    }
    if (r->kind == FINNMENU_RESULT_SETTINGS) return &g_ic_app_settings;
    if (r->kind == FINNMENU_RESULT_ACTION) {
        if (r->index == FINNMENU_ACTION_APPEARANCE) return &g_ic_app_settings;
        return &g_ic_power;
    }
    (void)row;
    return NULL;
}

void finnmenu_render(int screen_w, int screen_h, float scale,
                     int dock_x, int dock_y, int dock_w, int dock_h,
                     const finn_state *st) {
    float s = scale;
    if (g_font_scale != s) {
        if (load_fonts(s) != 0) return;
        g_font_scale = s;
    }

    int pw = rs(MENU_W, s), ph = rs(MENU_H, s);

    int lx = 0, ly = 0, lw = 0, lh = 0;
    finndock_launcher(&lx, &ly, &lw, &lh);
    if (lw <= 0 || lh <= 0) {
        lx = dock_x; ly = dock_y; lw = dock_w; lh = dock_h;
    }

    /* Anchor horizontally centered on screen, resting 15px above dock top */
    int px0 = (screen_w - pw) / 2;
    int py0 = ly - rs(ANCHOR_GAP, s) - ph;

    int margin = rs(12, s);
    if (px0 < margin) px0 = margin;
    if (py0 < margin) py0 = margin;
    if (px0 + pw > screen_w - margin) px0 = screen_w - margin - pw;
    if (py0 + ph > screen_h - margin) py0 = screen_h - margin - ph;

    g_lx0 = px0;
    g_ly0 = py0;

    /* Start Menu grows out of its own center per docs/design/motion.md */
    g_ax = px0 + pw / 2;
    g_ay = py0 + ph / 2;

    g_r_panel.x = 0; g_r_panel.y = 0; g_r_panel.w = pw; g_r_panel.h = ph;

    g_dark = st->dark ? 1 : 0;
    int n = finn_apps_count();

    char username[64];
    get_logged_in_user(username, sizeof username);

    build_results();
    g_n_rows = row_count();
    if (g_n_matches <= 0) g_sel = -1;
    else {
        if (g_sel < 0 && g_query[0]) g_sel = 0;
        if (g_sel >= g_n_rows) g_sel = g_n_rows - 1;
        if (g_sel < 0) g_sel = 0;
    }
    if (!g_query[0] && g_sel >= g_n_rows) g_sel = -1;

    uint32_t key = 2166136261u;
    #define MIX(v) do { key ^= (uint32_t)(v); key *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(pw); MIX(ph);
    MIX(g_lx0); MIX(g_ly0);
    MIX(st->dark + 2);
    MIX(g_n_rows); MIX(g_n_matches); MIX(g_sel + 2);
    MIX(g_query[0] != 0);
    for (int k = 0; g_query[k]; k++) MIX(g_query[k]);
    MIX(g_search_focused);
    MIX((int)(g_hover * 64.0f));
    MIX(g_hover_idx + 2);
    MIX((int)(finn_anim_v(&g_a_pin) * 64.0f));
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
    MIX(g_press_idx + 2);
    /* the glass is the desktop under it: when that moves, this redraws */
    MIX(finnblur_generation());
    for (int k = 0; username[k]; k++) MIX(username[k]);
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (a) MIX(a->installed);
    }
    #undef MIX

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

    /* 1. Main frosted glass panel */
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     0.0f, 0.0f, (float)pw, (float)ph,
                     s * MENU_R, FINN_GLASS, MENU_GLASS);
    finn_stroke_rrect(g_layer, pw, ph, 0.0f, 0.0f, (float)pw, (float)ph,
                      s * MENU_R, 1.0f, FINN_HAIR);

    /* 2. Left header: the title, and a real search field where the
     * decorative "View all" pill used to be. */
    float head_x = s * LEFT_PAD;
    float head_y = s * (HEAD_Y + 11.0f * REF);
    const char *title = g_query[0] ? "Results" : "Apps";
    finn_text_draw(&g_f_head, g_layer, pw, ph, head_x, head_y, title, FINN_INK);

    if (g_query[0]) {
        char count[32];
        snprintf(count, sizeof count, "%d", g_n_matches);
        float tw = finn_text_width(&g_f_head, title);
        float cc = finn_text_cap_height(&g_f_micro);
        finn_text_draw(&g_f_micro, g_layer, pw, ph,
                       head_x + tw + s * (8.0f * REF),
                       head_y - cc * 0.25f, count, FINN_MUTE);
    }

    float sx = s * SEARCH_X, sy = s * SEARCH_Y;
    float sw = s * SEARCH_W, sh = s * SEARCH_H;
    g_r_search.x = (int)lroundf(sx);
    g_r_search.y = (int)lroundf(sy);
    g_r_search.w = (int)lroundf(sw);
    g_r_search.h = (int)lroundf(sh);

    finn_fill_rrect(g_layer, pw, ph, sx, sy, sw, sh, s * SEARCH_R,
                    finn_theme_dark() ? 0x14FFFFFFu : 0x0C000000u);
    finn_stroke_rrect(g_layer, pw, ph, sx, sy, sw, sh, s * SEARCH_R,
                      g_search_focused ? 1.5f : 1.0f,
                      g_search_focused ? FINN_ON : FINN_HAIR);

    float tx = sx + s * (10.0f * REF);
    if (g_ic_search.vw > 0) {
        int isz = rs(12.0f * REF, s);
        finn_icon_blit_ink(&g_ic_search, g_layer, pw, ph,
                           (int)lroundf(tx + s * (6.5f * REF)),
                           (int)lroundf(sy + sh * 0.5f),
                           isz, FINN_MUTE, FINN_MUTE, 0);
        tx += s * (19.0f * REF);
    }

    float scan = finn_text_cap_height(&g_f_sub);
    float text_avail = sx + sw - s * (8.0f * REF) - tx;
    const char *shown = g_query[0] ? g_query : "Search";
    char fitted[128];
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, shown, text_avail);
    finn_text_draw(&g_f_sub, g_layer, pw, ph, tx,
                   sy + sh * 0.5f + scan * 0.5f, fitted,
                   g_query[0] ? FINN_INK : FINN_MUTE);

    if (g_search_focused) {
        float qw = finn_text_width(&g_f_sub, g_query);
        float cx = tx + qw + s * (2.0f * REF);
        if (cx < sx + sw - s * (6.0f * REF))
            finn_fill_rrect(g_layer, pw, ph, cx, sy + s * (7.0f * REF),
                            1.0f, sh - s * (14.0f * REF), 0.0f, FINN_ON);
    }

    /* 3. The result list. With no query it is the app catalogue, as before;
     * with one it is apps, settings pages and shell actions in one ranking. */
    float row_y = s * APPLIST_Y;
    float row_step = s * (APP_ROW_H + APP_ROW_GAP);
    int icon_sz = rs(APP_ICON_SZ, s);

    float hover_v = g_hover;
    float press_v = finn_anim_v(&g_a_press);

    for (int i = 0; i < g_n_rows; i++) {
        const menu_hit *r = &g_results[i];
        float ry = row_y + (float)i * row_step;
        g_r_row[i].x = rs(LEFT_PAD, s);
        g_r_row[i].y = (int)lroundf(ry);
        g_r_row[i].w = rs(ROW_RIGHT - LEFT_PAD, s);
        g_r_row[i].h = rs(APP_ROW_H, s);

        /* Hover and press wash */
        if (g_hover_idx == i && hover_v > 0.004f)
            control_wash_r(g_layer, pw, ph, g_r_row[i], s * (4.0f * REF),
                           FINN_MOTION_HOVER_WASH * hover_v);
        if (g_press_idx == i && press_v > 0.004f)
            control_wash_r(g_layer, pw, ph, g_r_row[i], s * (4.0f * REF),
                           FINN_MOTION_PRESS_WASH * press_v);

        /* A result chosen with the keyboard shows the same selection wash
         * the pointer would have put there. */
        if (g_query[0] && g_sel == i && (int)(finn_anim_v(&g_a_open) * 100) > 90)
            finn_fill_rrect(g_layer, pw, ph, (float)g_r_row[i].x,
                            (float)g_r_row[i].y + s, (float)g_r_row[i].w,
                            (float)g_r_row[i].h - s * 2.0f, s * 4.0f,
                            finn_theme_dark() ? 0x1FFFFFFFu : 0x12000000u);

        const char *rtitle = "";
        const char *rsub = "";
        const char *rtag = "";
        uint32_t title_col = FINN_INK;

        if (r->kind == FINNMENU_RESULT_APP) {
            const finn_app *a = result_app(r);
            if (!a) continue;
            rtitle = a->name;
            rsub = a->note[0] ? a->note : a->id;
            rtag = a->installed ? "" : "Not installed";
            if (!a->installed) title_col = FINN_DIM;
        } else if (r->kind == FINNMENU_RESULT_SETTINGS) {
            finn_settings_page_info info;
            if (finnsettingsapp_page_info(r->index, &info) != 0) continue;
            rtitle = info.title;
            rsub = "Settings";
            rtag = info.section;
        } else {
            char at[64], an[128];
            action_text(r->index, at, sizeof at, an, sizeof an);
            rtitle = at;
            rsub = an;
            rtag = "Action";
        }

        fsvg *ic = result_icon(r, i);
        if (ic && ic->vw > 0) {
            finn_icon_blit(ic, g_layer, pw, ph,
                           rs(LEFT_PAD, s), (int)lroundf(ry),
                           icon_sz, 0, 0, 0);
        } else {
            finn_fill_rrect(g_layer, pw, ph, s * LEFT_PAD, ry,
                            (float)icon_sz, (float)icon_sz, s * 5.0f, FINN_ON);
            char initial[2] = { rtitle[0] ? rtitle[0] : '?', 0 };
            float icap = finn_text_cap_height(&g_f_micro);
            float iw = finn_text_width(&g_f_micro, initial);
            finn_text_draw(&g_f_micro, g_layer, pw, ph,
                           s * LEFT_PAD + (icon_sz - iw) * 0.5f,
                           ry + icon_sz * 0.5f + icap * 0.5f,
                           initial, 0xFFFFFFFFu);
        }

        float tag_w = rtag[0] ? finn_text_width(&g_f_micro, rtag) : 0.0f;
        float text_right = s * ROW_RIGHT - s * (8.0f * REF) -
                           (tag_w > 0 ? tag_w + s * (8.0f * REF) : 0.0f);
        float name_max = text_right - s * APP_TEXT_X;
        if (name_max < s * (40.0f * REF)) name_max = s * (40.0f * REF);

        char fitted_name[128];
        finnapp_ellipsis(&g_f_label, fitted_name, sizeof fitted_name,
                         rtitle, name_max);
        float lcap = finn_text_cap_height(&g_f_label);
        float ty = ry + (float)g_r_row[i].h * 0.5f + lcap * 0.15f;
        finn_text_draw(&g_f_label, g_layer, pw, ph,
                       s * APP_TEXT_X, ty, fitted_name, title_col);

        char fitted_sub[192];
        finnapp_ellipsis(&g_f_micro, fitted_sub, sizeof fitted_sub,
                         rsub, name_max);
        if (rsub[0])
            finn_text_draw(&g_f_micro, g_layer, pw, ph,
                           s * APP_TEXT_X, ty + s * (11.0f * REF),
                           fitted_sub, FINN_MUTE);

        if (rtag[0])
            finn_text_draw(&g_f_micro, g_layer, pw, ph,
                           s * ROW_RIGHT - s * (8.0f * REF) - tag_w,
                           ty - lcap * 0.10f, rtag, FINN_MUTE);
    }

    if (g_n_matches == 0) {
        const char *line = "No app, setting or action matches";
        float lc2 = finn_text_cap_height(&g_f_label);
        finn_text_draw(&g_f_label, g_layer, pw, ph,
                       s * LEFT_PAD + (s * (ROW_RIGHT - LEFT_PAD) -
                                       finn_text_width(&g_f_label, line)) * 0.5f,
                       row_y + s * (24.0f * REF) + lc2, line, FINN_DIM);
    }

    /* 4. Left Account Row */
    float ac_x = s * ACCOUNT_X, ac_y = s * ACCOUNT_Y;
    float ac_w = s * ACCOUNT_W, ac_h = s * ACCOUNT_H;
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     ac_x, ac_y, ac_w, ac_h, s * ACCOUNT_R,
                     FINN_GLASS, FINN_GLASS_TILE);

    /* Avatar */
    int av_px = rs(AVATAR_SZ, s);
    float av_top = ac_y + (ac_h - (float)av_px) * 0.5f;
    if (g_ic_avatar.vw > 0) {
        finn_icon_blit(&g_ic_avatar, g_layer, pw, ph,
                       (int)lroundf(ac_x + s * AVATAR_X),
                       (int)lroundf(av_top),
                       av_px, 0, 0, 0);
    } else {
        float av_cx = ac_x + s * AVATAR_X + s * (AVATAR_SZ * 0.5f);
        float av_cy = ac_y + ac_h * 0.5f;
        float av_r = s * (AVATAR_SZ * 0.5f);
        finn_fill_rrect(g_layer, pw, ph, av_cx - av_r, av_cy - av_r,
                        av_r * 2.0f, av_r * 2.0f, av_r, FINN_ON);
    }

    /* User Name (Real session user) */
    float ac_cap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, g_layer, pw, ph,
                   ac_x + s * NAME_X, ac_y + ac_h * 0.5f + ac_cap * 0.45f,
                   username, FINN_INK);

    /* Restart & Power buttons */
    int btn_sz = rs(BTN_ICO_SZ, s);
    int hit_pad = rs(26.0f * REF, s);

    g_r_restart.x = (int)lroundf(ac_x + s * RESTART_X + btn_sz * 0.5f - hit_pad * 0.5f);
    g_r_restart.y = (int)lroundf(ac_y + ac_h * 0.5f - hit_pad * 0.5f);
    g_r_restart.w = hit_pad; g_r_restart.h = hit_pad;

    g_r_shutdown.x = (int)lroundf(ac_x + s * POWER_X + btn_sz * 0.5f - hit_pad * 0.5f);
    g_r_shutdown.y = (int)lroundf(ac_y + ac_h * 0.5f - hit_pad * 0.5f);
    g_r_shutdown.w = hit_pad; g_r_shutdown.h = hit_pad;

    /* Washes for restart/shutdown */
    if (g_hover_idx == FINN_APP_MAX && hover_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_restart, (float)hit_pad * 0.5f,
                       FINN_MOTION_HOVER_WASH * hover_v);
    if (g_press_idx == FINN_APP_MAX && press_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_restart, (float)hit_pad * 0.5f,
                       FINN_MOTION_PRESS_WASH * press_v);

    if (g_hover_idx == FINN_APP_MAX + 1 && hover_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_shutdown, (float)hit_pad * 0.5f,
                       FINN_MOTION_HOVER_WASH * hover_v);
    if (g_press_idx == FINN_APP_MAX + 1 && press_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_shutdown, (float)hit_pad * 0.5f,
                       FINN_MOTION_PRESS_WASH * press_v);

    if (g_ic_refresh.vw > 0) {
        finn_icon_blit_ink(&g_ic_refresh, g_layer, pw, ph,
                           (int)lroundf(ac_x + s * RESTART_X + btn_sz * 0.5f),
                           (int)lroundf(ac_y + ac_h * 0.5f),
                           btn_sz, FINN_INK, FINN_INK, 0);
    }
    if (g_ic_power.vw > 0) {
        finn_icon_blit_ink(&g_ic_power, g_layer, pw, ph,
                           (int)lroundf(ac_x + s * POWER_X + btn_sz * 0.5f),
                           (int)lroundf(ac_y + ac_h * 0.5f),
                           btn_sz, FINN_INK, FINN_INK, 0);
    }

    /* 5. Right panel: the hovered application, at a glance. It is the one
     * raised sheet in the menu, so it takes the tile weight over the panel. */
    float r_x = s * RIGHT_X, r_y = s * RIGHT_Y;
    float r_w = s * RIGHT_W, r_h = s * RIGHT_H;
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     r_x, r_y, r_w, r_h, s * RIGHT_R,
                     FINN_GLASS, FINN_GLASS_TILE);
    finn_stroke_rrect(g_layer, pw, ph, r_x, r_y, r_w, r_h, s * RIGHT_R,
                      1.0f, FINN_HAIR);

    const menu_hit *selr = preview_result();
    float rcx = r_x + r_w * 0.5f;

    if (selr) {
        const char *ptitle = "";
        const char *pnote = "";
        const char *pstatus = "";
        uint32_t pcol = FINN_ON;
        uint32_t pbg = 0x1F006FFFu;

        if (selr->kind == FINNMENU_RESULT_APP) {
            const finn_app *a = result_app(selr);
            if (a) {
                ptitle = a->name;
                pnote = a->note[0] ? a->note : "Application";
                pstatus = a->installed ? "Installed" : "Not installed";
                pcol = a->installed ? FINN_ON : FINN_MUTE;
                pbg = a->installed ? 0x1F006FFFu
                                   : (finn_theme_dark() ? 0x1AFFFFFFu
                                                        : 0x14000000u);
            }
        } else if (selr->kind == FINNMENU_RESULT_SETTINGS) {
            finn_settings_page_info info;
            if (finnsettingsapp_page_info(selr->index, &info) == 0) {
                ptitle = info.title;
                pnote = "Settings";
                pstatus = info.section;
                pcol = FINN_DIM;
                pbg = finn_theme_dark() ? 0x1AFFFFFFu : 0x14000000u;
            }
        } else {
            char at[64], an[128];
            action_text(selr->index, at, sizeof at, an, sizeof an);
            ptitle = at;
            pnote = an;
            pstatus = "Action";
            pcol = FINN_DIM;
            pbg = finn_theme_dark() ? 0x1AFFFFFFu : 0x14000000u;
        }

        fsvg *ic = result_icon(selr, 0);
        float big = s * 64.0f;
        float by = r_y + r_h * 0.30f;
        if (ic && ic->vw > 0) {
            finn_icon_blit(ic, g_layer, pw, ph,
                           (int)lroundf(rcx - big * 0.5f),
                           (int)lroundf(by - big * 0.5f),
                           (int)lroundf(big), 0, 0, 0);
        } else {
            finn_fill_rrect(g_layer, pw, ph, rcx - big * 0.5f, by - big * 0.5f,
                            big, big, s * 14.0f, FINN_ON);
            char initial[2] = { ptitle[0] ? ptitle[0] : '?', 0 };
            float icap = finn_text_cap_height(&g_f_head);
            float iw = finn_text_width(&g_f_head, initial);
            finn_text_draw(&g_f_head, g_layer, pw, ph,
                           rcx - iw * 0.5f, by + icap * 0.5f,
                           initial, 0xFFFFFFFFu);
        }

        char fitted[128];
        finnapp_ellipsis(&g_f_head, fitted, sizeof fitted, ptitle,
                         r_w - s * (40.0f * REF));
        float nw = finn_text_width(&g_f_head, fitted);
        finn_text_draw(&g_f_head, g_layer, pw, ph, rcx - nw * 0.5f,
                       by + big * 0.5f + s * 30.0f, fitted, FINN_INK);

        char fitted_note[256];
        finnapp_ellipsis(&g_f_sub, fitted_note, sizeof fitted_note, pnote,
                         r_w - s * (40.0f * REF));
        float nw2 = finn_text_width(&g_f_sub, fitted_note);
        finn_text_draw(&g_f_sub, g_layer, pw, ph, rcx - nw2 * 0.5f,
                       by + big * 0.5f + s * 48.0f, fitted_note, FINN_DIM);

        float sw = finn_text_width(&g_f_sub, pstatus);
        float pill_w = sw + s * 22.0f, pill_h = s * 24.0f;
        float pill_x = rcx - pill_w * 0.5f;
        float pill_y = by + big * 0.5f + s * 66.0f;
        finn_fill_rrect(g_layer, pw, ph, pill_x, pill_y, pill_w, pill_h,
                        pill_h * 0.5f, pbg);
        finn_text_draw(&g_f_sub, g_layer, pw, ph,
                       pill_x + (pill_w - sw) * 0.5f,
                       pill_y + pill_h * 0.5f + finn_text_cap_height(&g_f_sub) * 0.45f,
                       pstatus, pcol);

        /*
         * A pin is a taskbar preference, so it belongs with the app, not in
         * a settings page far from the thing being pinned. Only an installed
         * app gets the control: a catalogue entry that is not on the machine
         * has no tile to pin.
         */
        const finn_app *pin_app = result_app(selr);
        if (pin_app && pin_app->installed) {
            int pinned = menu_is_pinned(pin_app->id);
            const char *label = pinned ? "Unpin from taskbar" : "Pin to taskbar";
            float lw = finn_text_width(&g_f_sub, label);
            float bw = lw + s * 30.0f, bh = s * 30.0f;
            float bx = rcx - bw * 0.5f;
            float byy = pill_y + pill_h + s * 14.0f;
            uint32_t bg = pinned
                        ? (finn_theme_dark() ? 0x1FFFFFFFu : 0x14000000u)
                        : FINN_ON;
            uint32_t fg = pinned ? FINN_INK : 0xFFFFFFFFu;
            float wash = fmaxf(finn_anim_v(&g_a_pin) * FINN_MOTION_HOVER_WASH,
                               g_press == FINNMENU_PIN
                                   ? finn_anim_v(&g_a_press) * FINN_MOTION_PRESS_WASH
                                   : 0.0f);
            finn_fill_rrect(g_layer, pw, ph, bx, byy, bw, bh, bh * 0.5f, bg);
            if (wash > 0.004f) {
                uint32_t a8 = (uint32_t)(wash * 255.0f + 0.5f);
                finn_fill_rrect(g_layer, pw, ph, bx, byy, bw, bh, bh * 0.5f,
                                (a8 << 24) | (pinned ? (FINN_INK & 0x00FFFFFFu)
                                                     : 0x00000000u));
            }
            finn_text_draw(&g_f_sub, g_layer, pw, ph,
                           bx + (bw - lw) * 0.5f,
                           byy + bh * 0.5f + finn_text_cap_height(&g_f_sub) * 0.45f,
                           label, fg);
            g_r_pin.x = (int)lroundf(bx);
            g_r_pin.y = (int)lroundf(byy);
            g_r_pin.w = (int)lroundf(bw);
            g_r_pin.h = (int)lroundf(bh);
        } else {
            g_r_pin.x = g_r_pin.y = g_r_pin.w = g_r_pin.h = 0;
        }
    } else {
        g_r_pin.x = g_r_pin.y = g_r_pin.w = g_r_pin.h = 0;
        float cap = finn_text_cap_height(&g_f_sub);
        const char *l1 = g_query[0] ? "No matches" : "Choose an application";
        const char *l2 = g_query[0] ? "Try an app, a setting or an action."
                                    : "Search apps, settings and actions.";
        float w1 = finn_text_width(&g_f_sub, l1);
        float w2 = finn_text_width(&g_f_micro, l2);
        finn_text_draw(&g_f_sub, g_layer, pw, ph, rcx - w1 * 0.5f,
                       r_y + r_h * 0.46f + cap, l1, FINN_DIM);
        finn_text_draw(&g_f_micro, g_layer, pw, ph, rcx - w2 * 0.5f,
                       r_y + r_h * 0.46f + cap + s * 22.0f, l2, FINN_MUTE);
    }
}

void finnmenu_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnmenu_visible() || !g_layer) return;
    finn_shadow_rrect(dst, dw, dh, (float)(g_lx0 + dx), (float)(g_ly0 + dy),
                      (float)g_lw, (float)g_lh, g_font_scale * MENU_R,
                      g_font_scale * REF * FINN_SHADOW_BLUR, 0.0f,
                      g_font_scale * REF * FINN_SHADOW_OFFSET_Y,
                      FINN_SHADOW_ALPHA);
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnmenu_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    float a = v * FINN_MOTION_FADE;
    if (a > 1.0f) a = 1.0f;
    float k = FINN_MOTION_OPEN_SCALE + (1.0f - FINN_MOTION_OPEN_SCALE) * v;

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
    float k = g_lw > 0 ? w / (float)g_lw : 1.0f;
    finn_shadow_rrect(dst, dw, dh, x, y, w, h,
                      g_font_scale * MENU_R * k,
                      g_font_scale * REF * FINN_SHADOW_BLUR * k, 0.0f,
                      g_font_scale * REF * FINN_SHADOW_OFFSET_Y * k,
                      FINN_SHADOW_ALPHA * alpha);
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

static int in_rect(const menu_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int finnmenu_hit(int px, int py, finnmenu_result *out) {
    if (out) { out->kind = FINNMENU_RESULT_NONE; out->index = -1; out->row = -1; }
    if (!g_open || !g_layer || !finnmenu_visible()) return FINNMENU_NONE;
    if (finn_anim_v(&g_a_open) < 0.90f) return FINNMENU_NONE;

    int x = px - g_lx0, y = py - g_ly0;
    if (x < 0 || y < 0 || x >= g_lw || y >= g_lh) return FINNMENU_NONE;

    if (in_rect(&g_r_search, x, y)) return FINNMENU_SEARCH;

    for (int i = 0; i < g_n_rows; i++) {
        if (!in_rect(&g_r_row[i], x, y)) continue;
        if (out) {
            out->kind = g_results[i].kind;
            out->index = g_results[i].index;
            out->row = i;
        }
        return FINNMENU_RESULT;
    }
    if (in_rect(&g_r_restart, x, y))   return FINNMENU_RESTART;
    if (in_rect(&g_r_shutdown, x, y))  return FINNMENU_SHUTDOWN;

    /* The pin button belongs to the previewed app, so the hit carries that
     * app: the compositor toggles the same id the panel is showing. */
    const menu_hit *selr = preview_result();
    if (selr && g_r_pin.w > 0 && in_rect(&g_r_pin, x, y)) {
        if (out) {
            out->kind = selr->kind;
            out->index = selr->index;
            out->row = (int)(selr - g_results);
        }
        return FINNMENU_PIN;
    }

    return in_rect(&g_r_panel, x, y) ? FINNMENU_PANEL : FINNMENU_NONE;
}

void finnmenu_focus_search(void) {
    if (!g_open) return;
    g_search_focused = 1;
    g_built = 0;
}

void finnmenu_blur_search(void) {
    if (!g_search_focused) return;
    g_search_focused = 0;
    g_built = 0;
}

void finnmenu_hover_at(int px, int py) {
    finnmenu_result r;
    int what = finnmenu_hit(px, py, &r);

    int next = -1;
    if (what == FINNMENU_RESULT) next = r.row;
    else if (what == FINNMENU_RESTART)  next = FINN_APP_MAX;
    else if (what == FINNMENU_SHUTDOWN) next = FINN_APP_MAX + 1;
    else if (what == FINNMENU_PIN || what == FINNMENU_PANEL)
        next = g_hover_idx;   /* the preview stays while the pointer is inside */

    /* The pin button is the one control inside the preview; it takes the
     * same wash as a row, and moving onto it must not take the preview (and
     * with it the button) out from under the pointer. */
    int on_pin = (what == FINNMENU_PIN);
    if (on_pin != g_hover_pin) {
        g_hover_pin = on_pin;
        finn_anim_to(&g_a_pin, on_pin ? 1.0f : 0.0f,
                     on_pin ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT,
                     0.0f, FINN_EASE_OUT);
    }

    /* In search mode the preview follows the pointer. In the plain app list
     * it already did; keeping the pointer selection out of g_sel means the
     * keyboard and the mouse do not fight over the same row. */
    if (what == FINNMENU_RESULT && g_query[0] && next != g_sel) {
        g_sel = next;
        g_built = 0;
    }

    if (next == g_hover_idx) return;
    g_hover_idx = next;
    finn_anim_to(&g_a_hover, next >= 0 ? 1.0f : 0.0f,
                 next >= 0 ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT,
                 0.0f, FINN_EASE_OUT);
}

void finnmenu_press(int what) {
    g_press = what;
    if (what == FINNMENU_NONE) {
        g_press_idx = -1;
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    } else {
        if (what == FINNMENU_RESULT) g_press_idx = g_hover_idx;
        else if (what == FINNMENU_RESTART)  g_press_idx = FINN_APP_MAX;
        else if (what == FINNMENU_SHUTDOWN) g_press_idx = FINN_APP_MAX + 1;
        else g_press_idx = -1;
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
    }
}

/* ---------------- keyboard ---------------- */

static void set_query(const char *text) {
    snprintf(g_query, sizeof g_query, "%s", text ? text : "");
    g_sel = g_query[0] ? 0 : -1;
    g_search_focused = 1;
    g_built = 0;
}

static void move_selection(int delta) {
    build_results();
    int rows = row_count();
    if (rows <= 0) { g_sel = -1; g_built = 0; return; }

    int next = g_sel + delta;
    if (g_sel < 0) next = delta > 0 ? 0 : rows - 1;
    if (next < 0) next = 0;
    if (next >= rows) next = rows - 1;
    if (next != g_sel) {
        g_sel = next;
        g_built = 0;
    }
}

int finnmenu_key(const finn_key_event *e, finnmenu_result *activate) {
    if (activate) { activate->kind = FINNMENU_RESULT_NONE;
                    activate->index = -1; activate->row = -1; }
    if (!g_open || !e || !e->pressed) return 0;

    if (e->code == KEY_ESC) {
        if (g_query[0]) set_query("");
        else finnmenu_set_open(0);
        return 1;
    }

    if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
        build_results();
        int rows = row_count();
        if (rows <= 0) return 1;
        int row = g_sel;
        if (row < 0) row = g_hover_idx;
        if (row >= 0 && row < rows && activate) {
            activate->kind = g_results[row].kind;
            activate->index = g_results[row].index;
            activate->row = row;
        }
        return 1;
    }

    if (e->code == KEY_UP)   { move_selection(-1); return 1; }
    if (e->code == KEY_DOWN) { move_selection(1);  return 1; }

    if (e->code == KEY_BACKSPACE) {
        size_t len = strlen(g_query);
        if (len) g_query[len - 1] = '\0';
        if (!g_query[0]) g_sel = -1;
        else g_sel = 0;
        g_built = 0;
        return 1;
    }

    /* Text is search. There is no other text field in the launcher, so a
     * letter typed with the menu open belongs in the field. */
    if (!(e->mods & (FINN_MOD_CTRL | FINN_MOD_ALT | FINN_MOD_SUPER)) &&
        e->keysym >= 32 && e->keysym <= 126 &&
        strlen(g_query) + 1 < sizeof g_query) {
        size_t len = strlen(g_query);
        g_query[len] = (char)e->keysym;
        g_query[len + 1] = '\0';
        g_search_focused = 1;
        g_sel = 0;
        g_built = 0;
        return 1;
    }

    return 0;
}
