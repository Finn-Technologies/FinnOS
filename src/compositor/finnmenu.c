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
#include "finnproto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <linux/input.h>
#include <sys/types.h>

/* Authored on the 1280x832 design canvas; the shell scale is applied once,
 * at render time, so these are plain design units.
 *
 * The Home and All Apps frames place every element on the same grid: a
 * 375-wide left column at x20, rows 36 high with a 5 gap, and a 385-wide
 * right panel at x405. Keeping the numbers here rather than deriving them
 * from each other is what lets this file be read against the frame. */
#define REF          1.0f

#define MENU_W       (800.0f * REF)
#define MENU_H       (500.0f * REF)
#define MENU_R       (8.0f * REF)

#define LEFT_X       (20.0f * REF)
#define LEFT_W       (375.0f * REF)
#define RIGHT_X      (405.0f * REF)
#define RIGHT_Y      (10.0f * REF)
#define RIGHT_W      (385.0f * REF)
#define RIGHT_H      (480.0f * REF)
#define RIGHT_R      (4.14f * REF)

#define ROW_H        (36.0f * REF)
#define ROW_GAP      (5.0f * REF)
#define ROW_R        (8.0f * REF)
#define ROW_PAD      (5.0f * REF)
#define ROW_ICON     (26.0f * REF)
#define ROW_ICON_R   (5.909f * REF)
#define ROW_ICON_GAP (10.0f * REF)
#define ROW_TEXT_X   (LEFT_X + ROW_PAD + ROW_ICON + ROW_ICON_GAP)
#define ROW_RIGHT    (395.0f * REF)

#define HEAD_Y       (25.0f * REF)
#define HEAD_CAP     (11.0f * REF)
#define ALL_HEAD_X   (56.0f * REF)
#define BACK_X       (20.0f * REF)
#define BACK_Y       (18.0f * REF)
#define BACK_SZ      (26.0f * REF)

#define VIEWALL_X    (307.0f * REF)
#define VIEWALL_Y    (18.0f * REF)
#define VIEWALL_W    (88.0f * REF)
#define VIEWALL_H    (26.0f * REF)

#define PIN_Y        (56.0f * REF)
#define RECENT_GAP   (20.0f * REF)
#define RECENT_LEAD  (31.0f * REF)
#define RECENT_MAX_VISIBLE 5
#define PIN_MAX_VISIBLE    5

#define SEARCH_Y     (56.0f * REF)
#define SEARCH_H     (36.0f * REF)
#define SEARCH_R     (8.0f * REF)
#define RESULTS_Y    (102.0f * REF)
/* Seven rows end at 425 and leave the note clear of the account row at 450;
 * an eighth would put its note underneath that row. */
#define SEARCH_MAX_VISIBLE 7

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

#define WIDGET_X1    (10.0f * REF)
#define WIDGET_X2    (197.0f * REF)
#define WIDGET_Y     (10.0f * REF)
#define WIDGET_W     (177.5f * REF)
#define WIDGET_H     (170.0f * REF)
#define WIDGET_R     (2.0f * REF)

#define ANCHOR_GAP   (15.0f * REF)

/* The Home frame is a 65% white fill over the same 15-unit backdrop blur as
 * the Window surface; keep its measured material weight rather than adding a
 * menu-specific opacity step. The right panel and the account row are the
 * 85% raised tile the design uses for anything sitting on that glass. */
#define MENU_GLASS   0.65f

/* #C7C7C7 at 25%, the search field the design puts on the glass. */
#define SEARCH_LIGHT 0x40C7C7C7u

/* ---------------- fonts ---------------- */
static finn_font g_f_head, g_f_label, g_f_sub, g_f_micro, g_f_time;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];
static char  g_icon_dir[512];
static char  g_registry[512];

/* ---------------- icons ---------------- */
static fsvg g_ic_avatar, g_ic_refresh, g_ic_power, g_ic_search;
static fsvg g_ic_app_settings, g_ic_chevron_right, g_ic_chevron_left;
static fsvg g_ic_paint;

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

/*
 * The menu has three views, and only one of them is the frame that was
 * drawn: Home is the summary, All Apps the catalogue, and Search is entered
 * by typing or by the dock's search button. Keeping the mode explicit stops
 * a cleared query from silently turning into a different screen.
 */
enum { MENU_MODE_HOME = 0, MENU_MODE_ALL, MENU_MODE_SEARCH };
static int g_mode = MENU_MODE_HOME;

typedef struct { int x, y, w, h; } menu_rect;
static menu_rect g_r_panel, g_r_restart, g_r_shutdown, g_r_search, g_r_pin;
static menu_rect g_r_viewall, g_r_back;
static menu_rect g_r_row[16];
static int       g_n_rows = 0;

/* ---------------- pins ---------------- */

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

/* ---------------- recent activity ---------------- */

/*
 * Recent activity is a report, so it reads the file finnd writes after a
 * launch and never invents an entry. The menu closes when a launch starts,
 * so a snapshot taken at open is enough; reading the file on every hover
 * would spend a syscall on a pointer move.
 */
static char g_recent_id[FINN_RECENT_MAX][FINNTASKBAR_ID_MAX];
static long g_recent_when[FINN_RECENT_MAX];
static int  g_n_recent = 0;

static void recent_load(void) {
    g_n_recent = 0;

    FILE *f = fopen(FINN_RECENT_FILE, "r");
    if (!f) return;
    char line[128];
    while (g_n_recent < FINN_RECENT_MAX && fgets(line, sizeof line, f)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char *end = NULL;
        long when = strtol(line, &end, 10);
        if (!end || *end || when <= 0) continue;
        char *id = tab + 1;
        char *nl = strchr(id, '\n');
        if (nl) *nl = '\0';
        if (!id[0]) continue;
        /* A catalogue that no longer has the app cannot draw its row. */
        if (!finn_apps_find(id)) continue;
        snprintf(g_recent_id[g_n_recent], sizeof g_recent_id[0], "%s", id);
        g_recent_when[g_n_recent] = when;
        g_n_recent++;
    }
    fclose(f);
}

static int app_index_of(const char *id) {
    if (!id || !id[0]) return -1;
    int n = finn_apps_count();
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (a && !strcmp(a->id, id)) return i;
    }
    return -1;
}

static void format_when(long when, char *out, size_t cap) {
    time_t now = time(NULL);
    time_t t = (time_t)when;
    struct tm a, b;
    if (!localtime_r(&t, &a) || !localtime_r(&now, &b)) {
        snprintf(out, cap, "—");
        return;
    }
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday)
        strftime(out, cap, "%H:%M", &a);
    else
        strftime(out, cap, "%-d %b, %H:%M", &a);
}

/* ---------------- unified search ---------------- */

#define MENU_RESULT_MAX 32
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

/*
 * One visible row. A recent row carries the launch time, because its label
 * is "Name | Opened HH:MM"; the same app in the pinned section has no time.
 * The row is the only thing the renderer, the hit test and the keyboard need
 * to agree on, so they share this list instead of three parallel ones.
 */
typedef struct {
    int  kind;        /* FINNMENU_RESULT_* */
    int  index;       /* registry index / settings page / action */
    int  recent;      /* 1 when `when` is shown */
    long when;
} menu_row;

static menu_row g_rows[16];

/* Layout results the hit test and the renderer both use. */
static int g_pin_shown = 0;
static int g_pins_total = 0;
static int g_pins_more = 0;
static int g_pin_end = 0;
static int g_recent_label_top = 0;
static int g_recent_y = 0;
static int g_recent_fit = 0;

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

/* Hover/press slots above the row range, so one wash animation can serve the
 * rows and the four chrome controls without a second set of tracks. */
#define HOVER_RESTART  (FINN_APP_MAX)
#define HOVER_SHUTDOWN (FINN_APP_MAX + 1)
#define HOVER_VIEWALL  (FINN_APP_MAX + 2)
#define HOVER_BACK     (FINN_APP_MAX + 3)

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
    finn_font_free(&g_f_time);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_head, p, font_px(s, 16.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, font_px(s, 14.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, font_px(s, 12.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, font_px(s, 10.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_time, p, font_px(s, 34.0f * REF)) != 0) return -1;
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

/* A colour at a fraction of its own alpha. The split header is the same ink
 * as the label, so the suffix cannot drift to a different hue. */
static uint32_t ink_alpha(uint32_t ink, unsigned a8) {
    return (ink & 0x00FFFFFFu) | ((a8 & 0xFFu) << 24);
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
    snprintf(p, sizeof p, "%s/chevron-left.svg", g_icon_dir);    fsvg_load(p, &g_ic_chevron_left);
    snprintf(p, sizeof p, "%s/paint-brush.svg", g_icon_dir);     fsvg_load(p, &g_ic_paint);

    return 0;
}

void finnmenu_free(void) {
    fsvg_free(&g_ic_avatar);
    fsvg_free(&g_ic_refresh);
    fsvg_free(&g_ic_power);
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_app_settings);
    fsvg_free(&g_ic_chevron_right);
    fsvg_free(&g_ic_chevron_left);
    fsvg_free(&g_ic_paint);

    for (int i = 0; i < FINN_APP_MAX; i++) {
        if (g_icons[i].loaded) fsvg_free(&g_icons[i].svg);
        g_icons[i].loaded = 0;
        g_icons[i].id[0] = 0;
    }
    finn_font_free(&g_f_head);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
    finn_font_free(&g_f_time);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_built = 0;
    g_font_scale = -1.0f;
}

static void view_reset(void) {
    g_sel = -1;
    g_hover_idx = -1;
    g_press_idx = -1;
    g_hover_pin = 0;
    /* The hit rects belong to the old view; a click in the frame before the
     * next render must not land on a row that is no longer there. */
    memset(g_r_row, 0, sizeof g_r_row);
    g_n_rows = 0;
    g_built = 0;
}

void finnmenu_show_home(void) {
    g_mode = MENU_MODE_HOME;
    g_query[0] = '\0';
    g_search_focused = 0;
    view_reset();
    finn_anim_to(&g_a_pin, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

void finnmenu_show_all(void) {
    g_mode = MENU_MODE_ALL;
    g_query[0] = '\0';
    g_search_focused = 0;
    view_reset();
    finn_anim_to(&g_a_pin, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

void finnmenu_set_open(int open) {
    open = open ? 1 : 0;
    if (open == g_open) return;
    g_open = open;

    if (open) {
        finn_apps_load(g_registry, NULL);
        pins_load();
        recent_load();
        g_mode = MENU_MODE_HOME;
        g_sel = -1;
        g_query[0] = '\0';
        g_search_focused = 0;
        g_built = 0;
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    } else {
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
        g_press = FINNMENU_NONE;
        g_press_idx = -1;
        g_mode = MENU_MODE_HOME;
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

/* ---------------- search ranking ---------------- */

static int action_count(void) { return ACTION_COUNT + 1; }

/*
 * The appearance action's label is the change, not the state: a menu that
 * says "Dark appearance" while the shell is already dark is a report, and a
 * row in a launcher has to be a verb.
 */
static void action_text(int index, char *title, size_t tcap,
                        char *note, size_t ncap, int dark) {
    if (index == FINNMENU_ACTION_RESTART) {
        snprintf(title, tcap, "%s", g_actions[0].title);
        snprintf(note, ncap, "%s", g_actions[0].note);
    } else if (index == FINNMENU_ACTION_SHUTDOWN) {
        snprintf(title, tcap, "%s", g_actions[1].title);
        snprintf(note, ncap, "%s", g_actions[1].note);
    } else {
        snprintf(title, tcap, "Switch to %s appearance",
                 dark ? "light" : "dark");
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

    /* An empty query is not "show everything": that is the All Apps view.
     * Search with nothing typed has nothing to report. */
    if (!q[0]) return;

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
        action_text(i, title, sizeof title, note, sizeof note, 0);
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

/* ---------------- row construction ---------------- */

static int add_row(int kind, int index, int recent, long when) {
    if (g_n_rows >= (int)(sizeof g_rows / sizeof g_rows[0])) return 0;
    menu_row *r = &g_rows[g_n_rows++];
    r->kind = kind;
    r->index = index;
    r->recent = recent;
    r->when = when;
    return 1;
}

/*
 * Home is a summary, not a second catalogue. It shows the first five pins and
 * the last five launches; when there are more pins than fit, it says so and
 * View all still lists every app. The recent rows flow below the pins, so a
 * machine with one pin does not leave the old fixed gap in the middle.
 */
static void home_metrics(void) {
    int resolved = 0;
    for (int i = 0; i < g_n_pins; i++)
        if (app_index_of(g_pins[i]) >= 0) resolved++;

    g_pins_total = resolved;
    g_pin_shown = resolved > PIN_MAX_VISIBLE ? PIN_MAX_VISIBLE : resolved;
    g_pins_more = resolved > PIN_MAX_VISIBLE;

    g_pin_end = PIN_Y + g_pin_shown * (ROW_H + ROW_GAP) - ROW_GAP;
    if (g_pins_more) g_pin_end += 16;

    g_recent_label_top = g_pin_end + RECENT_GAP;
    g_recent_y = g_recent_label_top + RECENT_LEAD;

    int fit = (ACCOUNT_Y - ROW_GAP - g_recent_y + ROW_GAP) / (ROW_H + ROW_GAP);
    if (fit < 0) fit = 0;
    if (fit > RECENT_MAX_VISIBLE) fit = RECENT_MAX_VISIBLE;
    g_recent_fit = fit;
}

static void build_rows(void) {
    g_n_rows = 0;

    if (g_mode == MENU_MODE_SEARCH) {
        build_results();
        for (int i = 0; i < g_n_matches && i < SEARCH_MAX_VISIBLE; i++)
            add_row(g_results[i].kind, g_results[i].index, 0, 0);
    } else if (g_mode == MENU_MODE_ALL) {
        int n = finn_apps_count();
        for (int i = 0; i < n && g_n_rows < 9; i++)
            add_row(FINNMENU_RESULT_APP, i, 0, 0);
    } else {
        home_metrics();

        int shown = 0;
        for (int i = 0; i < g_n_pins && shown < g_pin_shown; i++) {
            int ai = app_index_of(g_pins[i]);
            if (ai < 0) continue;
            add_row(FINNMENU_RESULT_APP, ai, 0, 0);
            shown++;
        }

        int rec = 0;
        for (int i = 0; i < g_n_recent && rec < g_recent_fit; i++) {
            int ai = app_index_of(g_recent_id[i]);
            if (ai < 0) continue;
            add_row(FINNMENU_RESULT_APP, ai, 1, g_recent_when[i]);
            rec++;
        }
    }

    if (g_sel >= g_n_rows) g_sel = g_n_rows - 1;
    if (g_sel < -1) g_sel = -1;
}

static const menu_row *preview_row(void) {
    if (g_mode != MENU_MODE_SEARCH) return NULL;
    if (g_sel >= 0 && g_sel < g_n_rows) return &g_rows[g_sel];
    if (g_hover_idx >= 0 && g_hover_idx < g_n_rows)
        return &g_rows[g_hover_idx];
    return NULL;
}

static fsvg *row_icon(const menu_row *r) {
    if (!r) return NULL;
    if (r->kind == FINNMENU_RESULT_APP) {
        const finn_app *a = finn_apps_at(r->index);
        return a ? icon_for(r->index, a) : NULL;
    }
    if (r->kind == FINNMENU_RESULT_SETTINGS) return &g_ic_app_settings;
    if (r->kind == FINNMENU_RESULT_ACTION) {
        if (r->index == FINNMENU_ACTION_APPEARANCE) return &g_ic_paint;
        if (r->index == FINNMENU_ACTION_RESTART) return &g_ic_refresh;
        return &g_ic_power;
    }
    return NULL;
}

/* The app's icon is already a 26-unit card; an action is a glyph and needs
 * the same card behind it, or the row loses the one shape that ties the two
 * kinds of result together. */
static int icon_is_card(const menu_row *r) {
    return r && (r->kind == FINNMENU_RESULT_APP ||
                 r->kind == FINNMENU_RESULT_SETTINGS);
}

/* ---------------- icons and type ---------------- */

static void draw_row_icon(const menu_row *r, uint32_t *L, int lw, int lh,
                          float x, float y, float size, float s) {
    fsvg *ic = row_icon(r);
    if (icon_is_card(r) && ic && ic->vw > 0) {
        finn_icon_blit(ic, L, lw, lh, (int)lroundf(x), (int)lroundf(y),
                       (int)lroundf(size), 0, 0, 0);
        return;
    }

    if (!icon_is_card(r)) {
        finn_fill_rrect(L, lw, lh, x, y, size, size, s * ROW_ICON_R, FINN_TILE);
        if (ic && ic->vw > 0) {
            finn_icon_blit_ink(ic, L, lw, lh,
                               (int)lroundf(x + size * 0.5f),
                               (int)lroundf(y + size * 0.5f),
                               (int)lroundf(size * 0.62f), FINN_INK, FINN_MUTE, 0);
            return;
        }
    }

    finn_fill_rrect(L, lw, lh, x, y, size, size, s * ROW_ICON_R, FINN_ON);
    const char *title = r && r->kind == FINNMENU_RESULT_APP &&
                        finn_apps_at(r->index)
                            ? finn_apps_at(r->index)->name
                            : "?";
    char initial[2] = { title[0] ? title[0] : '?', 0 };
    float cap = finn_text_cap_height(&g_f_label);
    float iw = finn_text_width(&g_f_label, initial);
    finn_text_draw(&g_f_label, L, lw, lh, x + (size - iw) * 0.5f,
                   y + size * 0.5f + cap * 0.5f, initial, 0xFFFFFFFFu);
}

static void draw_split_header(uint32_t *L, int lw, int lh, float s,
                              float x, const char *suffix) {
    float baseline = s * (HEAD_Y + HEAD_CAP);
    const char *prefix = "Apps ";
    finn_text_draw(&g_f_head, L, lw, lh, x, baseline, prefix, FINN_INK);
    float w = finn_text_width(&g_f_head, prefix);
    finn_text_draw(&g_f_head, L, lw, lh, x + w, baseline, suffix,
                   ink_alpha(FINN_INK, 0xA6));
}

static void draw_chevron(fsvg *ic, uint32_t *L, int lw, int lh,
                         float cx, float cy, float size, uint32_t ink) {
    if (!ic || ic->vw <= 0) return;
    finn_icon_blit_ink(ic, L, lw, lh, (int)lroundf(cx), (int)lroundf(cy),
                       (int)lroundf(size), ink, ink, 0);
}

static void draw_view_all(uint32_t *L, int lw, int lh, float s, float hover,
                          float press) {
    float x = s * VIEWALL_X, y = s * VIEWALL_Y;
    float w = s * VIEWALL_W, h = s * VIEWALL_H;

    g_r_viewall.x = (int)lroundf(x);
    g_r_viewall.y = (int)lroundf(y);
    g_r_viewall.w = (int)lroundf(w);
    g_r_viewall.h = (int)lroundf(h);

    finn_fill_rrect(L, lw, lh, x, y, w, h, h * 0.5f, FINN_TILE);

    menu_rect r = g_r_viewall;
    if (g_hover_idx == HOVER_VIEWALL && hover > 0.004f)
        control_wash_r(L, lw, lh, r, h * 0.5f, FINN_MOTION_HOVER_WASH * hover);
    if (g_press_idx == HOVER_VIEWALL && press > 0.004f)
        control_wash_r(L, lw, lh, r, h * 0.5f, FINN_MOTION_PRESS_WASH * press);

    float cap = finn_text_cap_height(&g_f_sub);
    finn_text_draw(&g_f_sub, L, lw, lh, x + s * 10.0f,
                   y + h * 0.5f + cap * 0.5f, "View all", FINN_INK);
    draw_chevron(&g_ic_chevron_right, L, lw, lh,
                 x + s * 75.0f, y + h * 0.5f, s * 14.0f, FINN_INK);
}

static void draw_back(uint32_t *L, int lw, int lh, float s, float hover,
                      float press) {
    float x = s * BACK_X, y = s * BACK_Y, sz = s * BACK_SZ;

    g_r_back.x = (int)lroundf(x);
    g_r_back.y = (int)lroundf(y);
    g_r_back.w = (int)lroundf(sz);
    g_r_back.h = (int)lroundf(sz);

    menu_rect r = g_r_back;
    if (g_hover_idx == HOVER_BACK && hover > 0.004f)
        control_wash_r(L, lw, lh, r, sz * 0.5f, FINN_MOTION_HOVER_WASH * hover);
    if (g_press_idx == HOVER_BACK && press > 0.004f)
        control_wash_r(L, lw, lh, r, sz * 0.5f, FINN_MOTION_PRESS_WASH * press);

    draw_chevron(&g_ic_chevron_left, L, lw, lh,
                 x + sz * 0.5f, y + sz * 0.5f, s * 18.0f, FINN_INK);
}

static void draw_search_field(uint32_t *L, int lw, int lh, float s) {
    float x = s * LEFT_X, y = s * SEARCH_Y;
    float w = s * LEFT_W, h = s * SEARCH_H;

    g_r_search.x = (int)lroundf(x);
    g_r_search.y = (int)lroundf(y);
    g_r_search.w = (int)lroundf(w);
    g_r_search.h = (int)lroundf(h);

    finn_fill_rrect(L, lw, lh, x, y, w, h, s * SEARCH_R,
                    finn_theme_dark() ? 0x33FFFFFFu : SEARCH_LIGHT);

    float cx = x + s * 22.0f, cy = y + h * 0.5f;
    if (g_ic_search.vw > 0)
        finn_icon_blit_ink(&g_ic_search, L, lw, lh,
                           (int)lroundf(cx), (int)lroundf(cy),
                           (int)lroundf(s * 14.0f), FINN_MUTE, FINN_MUTE, 0);

    float tx = x + s * 40.0f;
    const char *shown = g_query[0] ? g_query : "Search apps, settings and actions";
    float cap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, L, lw, lh, tx, cy + cap * 0.5f, shown,
                   g_query[0] ? FINN_INK : FINN_MUTE);

    if (g_search_focused) {
        float qw = finn_text_width(&g_f_label, g_query);
        float caret = tx + qw + s * 1.5f;
        if (caret < x + w - s * 6.0f)
            finn_fill_rrect(L, lw, lh, caret, y + s * 10.0f, s * 1.0f,
                            h - s * 20.0f, 0.0f, FINN_INK);
    }
}

/* ---------------- widgets ---------------- */

static void draw_widget_card(uint32_t *L, int lw, int lh, float s,
                             float x, float y) {
    float w = s * WIDGET_W, h = s * WIDGET_H;
    float r = s * WIDGET_R;
    /* The right panel is already 85% white, so a card tinted a step darker
     * disappears into it. The card is solid white with the same one-pixel
     * hairline the icons use; that is the edge the panel does not give it. */
    finn_fill_rrect(L, lw, lh, x, y, w, h, r,
                    finn_theme_dark() ? FINN_BONE : 0xFFFFFFFFu);
    finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 1.0f, FINN_HAIR);
}

static void draw_clock_widget(uint32_t *L, int lw, int lh, float s,
                              float x, float y) {
    draw_widget_card(L, lw, lh, s, x, y);
    float pad = s * 12.0f;
    float cap10 = finn_text_cap_height(&g_f_micro);
    float cap34 = finn_text_cap_height(&g_f_time);
    float cap12 = finn_text_cap_height(&g_f_sub);

    finn_text_draw(&g_f_micro, L, lw, lh, x + pad, y + pad + cap10,
                   "Local time", FINN_MUTE);

    time_t now = time(NULL);
    struct tm tm;
    char hm[16] = "--:--", date[64] = "";
    if (localtime_r(&now, &tm)) {
        strftime(hm, sizeof hm, "%H:%M", &tm);
        strftime(date, sizeof date, "%A %-d %B", &tm);
    }

    finn_text_draw(&g_f_time, L, lw, lh, x + pad, y + s * 74.0f, hm, FINN_INK);
    finn_text_draw(&g_f_sub, L, lw, lh, x + pad, y + s * 146.0f, date,
                   FINN_DIM);
    (void)cap34;
    (void)cap12;
}

static void draw_status_row(uint32_t *L, int lw, int lh, float s,
                            float x, float y, float w,
                            const char *label, const char *value,
                            uint32_t value_col) {
    float pad = s * 12.0f;
    float cap = finn_text_cap_height(&g_f_sub);
    finn_text_draw(&g_f_micro, L, lw, lh, x + pad, y + cap * 0.5f,
                   label, FINN_MUTE);
    float vw = finn_text_width(&g_f_sub, value);
    finn_text_draw(&g_f_sub, L, lw, lh, x + w - pad - vw, y + cap * 0.5f,
                   value, value_col);
}

static void draw_status_widget(uint32_t *L, int lw, int lh, float s,
                               float x, float y, const finn_state *st) {
    draw_widget_card(L, lw, lh, s, x, y);
    float w = s * WIDGET_W;
    float pad = s * 12.0f;
    float cap10 = finn_text_cap_height(&g_f_micro);

    finn_text_draw(&g_f_micro, L, lw, lh, x + pad, y + pad + cap10,
                   "System", FINN_MUTE);

    char power[48], wifi[32], bt[32], vol[32];

    if (st->battery_pct >= 0) {
        if (st->charging)
            snprintf(power, sizeof power, "%d%% · charging", st->battery_pct);
        else
            snprintf(power, sizeof power, "%d%%", st->battery_pct);
    } else {
        snprintf(power, sizeof power, "Mains");
    }

    if (st->wifi < 0)       snprintf(wifi, sizeof wifi, "No adapter");
    else if (st->wifi == 0) snprintf(wifi, sizeof wifi, "Off");
    else                    snprintf(wifi, sizeof wifi, "%d/3", st->wifi);

    if (st->bluetooth < 0)  snprintf(bt, sizeof bt, "No adapter");
    else                    snprintf(bt, sizeof bt, "%s", st->bluetooth ? "On" : "Off");

    if (st->volume < 0)     snprintf(vol, sizeof vol, "No mixer");
    else                    snprintf(vol, sizeof vol, "%d%%", st->volume);

    float row = y + s * 54.0f;
    float step = s * 28.0f;
    draw_status_row(L, lw, lh, s, x, row, w, "Power", power, FINN_INK);
    draw_status_row(L, lw, lh, s, x, row + step, w, "Wi-Fi", wifi, FINN_INK);
    draw_status_row(L, lw, lh, s, x, row + step * 2.0f, w, "Bluetooth", bt, FINN_INK);
    draw_status_row(L, lw, lh, s, x, row + step * 3.0f, w, "Volume", vol, FINN_INK);
}

/* ---------------- render ---------------- */

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
    g_r_search.x = g_r_search.y = g_r_search.w = g_r_search.h = 0;
    g_r_viewall.x = g_r_viewall.y = g_r_viewall.w = g_r_viewall.h = 0;
    g_r_back.x = g_r_back.y = g_r_back.w = g_r_back.h = 0;

    int n = finn_apps_count();

    char username[64];
    get_logged_in_user(username, sizeof username);

    build_rows();
    if (g_n_matches <= 0 && g_mode == MENU_MODE_SEARCH) g_sel = -1;
    if (g_sel >= g_n_rows) g_sel = g_n_rows ? g_n_rows - 1 : -1;

    uint32_t key = 2166136261u;
    #define MIX(v) do { key ^= (uint32_t)(v); key *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(pw); MIX(ph);
    MIX(g_lx0); MIX(g_ly0);
    MIX(st->dark + 2);
    MIX(g_mode);
    MIX(g_n_rows); MIX(g_n_matches); MIX(g_sel + 2);
    MIX(g_query[0] != 0);
    for (int k = 0; g_query[k]; k++) MIX(g_query[k]);
    MIX(g_search_focused);
    MIX((int)(g_hover * 64.0f));
    MIX(g_hover_idx + 2);
    MIX(g_hover_pin);
    MIX((int)(finn_anim_v(&g_a_pin) * 64.0f));
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
    MIX(g_press_idx + 2);
    MIX(g_pin_shown); MIX(g_pins_more);
    MIX(g_recent_label_top); MIX(g_recent_y); MIX(g_recent_fit);
    for (int i = 0; i < g_n_rows; i++) {
        MIX(g_rows[i].kind);
        MIX(g_rows[i].index);
        MIX(g_rows[i].recent);
        MIX((int)(g_rows[i].when / 60));
    }
    MIX((int)(time(NULL) / 60));
    MIX(st->battery_pct + 2); MIX(st->charging + 2);
    MIX(st->wifi + 2); MIX(st->bluetooth + 2);
    MIX(st->volume + 2);
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

    /* 1. The frosted surface. The frame has no stroke; the material edge is
     * the shadow the compositor draws under the finished layer. */
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     0.0f, 0.0f, (float)pw, (float)ph,
                     s * MENU_R, FINN_GLASS, MENU_GLASS);

    float hover_v = g_hover;
    float press_v = finn_anim_v(&g_a_press);

    /* 2. Header. Home and All Apps name the view; Search keeps the same
     * grammar so the back button always means "the previous view". */
    if (g_mode == MENU_MODE_HOME) {
        draw_split_header(g_layer, pw, ph, s, s * LEFT_X, "| Pinned");
        draw_view_all(g_layer, pw, ph, s, hover_v, press_v);
    } else if (g_mode == MENU_MODE_ALL) {
        draw_back(g_layer, pw, ph, s, hover_v, press_v);
        draw_split_header(g_layer, pw, ph, s, s * ALL_HEAD_X, "| All apps");
    } else {
        draw_back(g_layer, pw, ph, s, hover_v, press_v);
        draw_split_header(g_layer, pw, ph, s, s * ALL_HEAD_X, "| Search");
        draw_search_field(g_layer, pw, ph, s);
    }

    /* 3. Rows. Home splits them into pinned and recent; All Apps and Search
     * are one list. The row geometry is the same in every view, but Home has
     * two runs with the section label between them, so the y is chosen per
     * row rather than from one base. */
    float row_step = s * (ROW_H + ROW_GAP);
    float row_y = 0.0f;

    if (g_mode == MENU_MODE_HOME) {
        if (g_pins_more) {
            char note[64];
            snprintf(note, sizeof note, "%d of %d pinned · View all",
                     g_pin_shown, g_pins_total);
            float cap = finn_text_cap_height(&g_f_micro);
            finn_text_draw(&g_f_micro, g_layer, pw, ph, s * LEFT_X,
                           (float)(g_pin_end - 16 + 12) * s + cap,
                           note, FINN_MUTE);
        }

        float label_cap = finn_text_cap_height(&g_f_head);
        finn_text_draw(&g_f_head, g_layer, pw, ph, s * LEFT_X,
                       (float)g_recent_label_top * s + label_cap,
                       "Recent activity", FINN_INK);
    } else if (g_mode == MENU_MODE_ALL) {
        row_y = s * PIN_Y;
    } else {
        row_y = s * RESULTS_Y;
    }

    for (int i = 0; i < g_n_rows; i++) {
        float ry;
        if (g_mode == MENU_MODE_HOME) {
            if (i < g_pin_shown)
                ry = s * PIN_Y + (float)i * row_step;
            else
                ry = s * g_recent_y +
                     (float)(i - g_pin_shown) * row_step;
        } else {
            ry = row_y + (float)i * row_step;
        }

        g_r_row[i].x = rs(LEFT_X, s);
        g_r_row[i].y = (int)lroundf(ry);
        g_r_row[i].w = rs(LEFT_W, s);
        g_r_row[i].h = rs(ROW_H, s);

        if (g_hover_idx == i && hover_v > 0.004f)
            control_wash_r(g_layer, pw, ph, g_r_row[i], s * ROW_R,
                           FINN_MOTION_HOVER_WASH * hover_v);
        if (g_press_idx == i && press_v > 0.004f)
            control_wash_r(g_layer, pw, ph, g_r_row[i], s * ROW_R,
                           FINN_MOTION_PRESS_WASH * press_v);
        if (g_sel == i)
            finn_fill_rrect(g_layer, pw, ph, (float)g_r_row[i].x,
                            (float)g_r_row[i].y, (float)g_r_row[i].w,
                            (float)g_r_row[i].h, s * ROW_R,
                            finn_theme_dark() ? 0x1FFFFFFFu : 0x12000000u);

        const menu_row *r = &g_rows[i];
        const finn_app *a = r->kind == FINNMENU_RESULT_APP
                                ? finn_apps_at(r->index) : NULL;
        const char *title = "";
        char titlebuf[128];
        char tag[32] = "";
        uint32_t title_col = FINN_INK;

        if (r->kind == FINNMENU_RESULT_APP) {
            if (!a) continue;
            snprintf(titlebuf, sizeof titlebuf, "%s", a->name);
            title = titlebuf;
            if (!a->installed) {
                title_col = FINN_DIM;
                snprintf(tag, sizeof tag, "Not installed");
            }
        } else if (r->kind == FINNMENU_RESULT_SETTINGS) {
            finn_settings_page_info info;
            if (finnsettingsapp_page_info(r->index, &info) != 0) continue;
            title = info.title;
            snprintf(tag, sizeof tag, "Settings");
        } else {
            char at[64], an[128];
            action_text(r->index, at, sizeof at, an, sizeof an, st->dark);
            snprintf(titlebuf, sizeof titlebuf, "%s", at);
            title = titlebuf;
            snprintf(tag, sizeof tag, "Action");
        }

        float icon_x = s * (LEFT_X + ROW_PAD);
        float icon_y = ry + s * ROW_PAD;
        draw_row_icon(r, g_layer, pw, ph, icon_x, icon_y, s * ROW_ICON, s);

        float tag_w = tag[0] ? finn_text_width(&g_f_micro, tag) : 0.0f;
        float text_right = s * (ROW_RIGHT - ROW_PAD) -
                           (tag_w > 0 ? tag_w + s * 8.0f : 0.0f);
        float text_x = s * ROW_TEXT_X;
        float name_max = text_right - text_x;
        if (name_max < s * 40.0f) name_max = s * 40.0f;

        float cap = finn_text_cap_height(&g_f_label);
        float baseline = ry + (float)g_r_row[i].h * 0.5f + cap * 0.5f;

        if (r->recent) {
            char when[32], suffix[64];
            format_when(r->when, when, sizeof when);
            snprintf(suffix, sizeof suffix, "| Opened %s", when);

            char fitted[128];
            finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, title, name_max);
            finn_text_draw(&g_f_label, g_layer, pw, ph, text_x, baseline,
                           fitted, FINN_INK);

            float used = finn_text_width(&g_f_label, fitted);
            float sw = finn_text_width(&g_f_label, suffix);
            float space = s * 4.0f;
            if (text_x + used + space + sw <= text_right)
                finn_text_draw(&g_f_label, g_layer, pw, ph,
                               text_x + used + space, baseline, suffix,
                               ink_alpha(FINN_INK, 0xA6));
        } else {
            char fitted[128];
            finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, title, name_max);
            finn_text_draw(&g_f_label, g_layer, pw, ph, text_x, baseline,
                           fitted, title_col);
        }

        if (tag[0])
            finn_text_draw(&g_f_micro, g_layer, pw, ph,
                           s * (ROW_RIGHT - ROW_PAD) - tag_w,
                           baseline - cap * 0.5f + finn_text_cap_height(&g_f_micro) * 0.5f,
                           tag, FINN_MUTE);
    }

    /* Honest empty states. A summary with no history says so rather than
     * leaving a blank that could be mistaken for a loading screen. */
    if (g_mode == MENU_MODE_HOME && g_n_rows == g_pin_shown) {
        float cap = finn_text_cap_height(&g_f_sub);
        finn_text_draw(&g_f_sub, g_layer, pw, ph, s * LEFT_X,
                       s * g_recent_y + cap, "No recent activity.",
                       FINN_MUTE);
    }
    if (g_mode == MENU_MODE_SEARCH) {
        if (!g_query[0]) {
            float cap = finn_text_cap_height(&g_f_sub);
            finn_text_draw(&g_f_sub, g_layer, pw, ph, s * LEFT_X,
                           s * RESULTS_Y + s * 18.0f + cap,
                           "Results appear here as you type.", FINN_MUTE);
        } else if (g_n_matches == 0) {
            char line[96];
            snprintf(line, sizeof line,
                     "No app, setting or action matches “%s”.", g_query);
            float cap = finn_text_cap_height(&g_f_sub);
            finn_text_draw(&g_f_sub, g_layer, pw, ph, s * LEFT_X,
                           s * RESULTS_Y + s * 18.0f + cap, line, FINN_DIM);
        } else if (g_n_matches > g_n_rows) {
            char note[64];
            snprintf(note, sizeof note, "%d results · refine your search",
                     g_n_matches);
            float cap = finn_text_cap_height(&g_f_micro);
            finn_text_draw(&g_f_micro, g_layer, pw, ph, s * LEFT_X,
                           s * RESULTS_Y + (float)g_n_rows * row_step +
                               s * 14.0f + cap, note, FINN_MUTE);
        }
    }
    if (g_mode == MENU_MODE_ALL && n > g_n_rows) {
        char note[64];
        snprintf(note, sizeof note, "%d apps · type to search the rest", n);
        float cap = finn_text_cap_height(&g_f_micro);
        finn_text_draw(&g_f_micro, g_layer, pw, ph, s * LEFT_X,
                       s * PIN_Y + (float)g_n_rows * row_step + s * 14.0f + cap,
                       note, FINN_MUTE);
    }

    /* 4. The right panel: two real widgets on Home and All Apps, the chosen
     * result on Search. The frame's grey cards were placeholders; these
     * report the clock and the machine, which is what the space is for. */
    float r_x = s * RIGHT_X, r_y = s * RIGHT_Y;
    float r_w = s * RIGHT_W, r_h = s * RIGHT_H;
    finn_fill_rrect(g_layer, pw, ph, r_x, r_y, r_w, r_h, s * RIGHT_R,
                    FINN_TILE);

    if (g_mode != MENU_MODE_SEARCH) {
        float wx = r_x + s * WIDGET_X1;
        float wy = r_y + s * WIDGET_Y;
        draw_clock_widget(g_layer, pw, ph, s, wx, wy);
        draw_status_widget(g_layer, pw, ph, s, wx + s * (WIDGET_X2 - WIDGET_X1),
                           wy, st);
        g_r_pin.x = g_r_pin.y = g_r_pin.w = g_r_pin.h = 0;
    } else {
        const menu_row *selr = preview_row();
        float rcx = r_x + r_w * 0.5f;

        if (selr && (selr->kind != FINNMENU_RESULT_APP ||
                     finn_apps_at(selr->index) != NULL)) {
            const finn_app *a = selr->kind == FINNMENU_RESULT_APP
                                    ? finn_apps_at(selr->index) : NULL;
            char ptitle[128] = "";
            char pnote[192] = "";
            char pstatus[64] = "";
            uint32_t pcol = FINN_ON;
            uint32_t pbg = 0x1F006FFFu;

            if (selr->kind == FINNMENU_RESULT_APP) {
                snprintf(ptitle, sizeof ptitle, "%s", a->name);
                snprintf(pnote, sizeof pnote, "%s",
                         a->note[0] ? a->note : "Application");
                snprintf(pstatus, sizeof pstatus, "%s",
                         a->installed ? "Installed" : "Not installed");
                pcol = a->installed ? FINN_ON : FINN_MUTE;
                pbg = a->installed ? 0x1F006FFFu
                                   : (finn_theme_dark() ? 0x1AFFFFFFu
                                                        : 0x14000000u);
            } else if (selr->kind == FINNMENU_RESULT_SETTINGS) {
                finn_settings_page_info info;
                if (finnsettingsapp_page_info(selr->index, &info) == 0) {
                    snprintf(ptitle, sizeof ptitle, "%s", info.title);
                    snprintf(pnote, sizeof pnote, "Settings");
                    snprintf(pstatus, sizeof pstatus, "%s", info.section);
                    pcol = FINN_DIM;
                    pbg = finn_theme_dark() ? 0x1AFFFFFFu : 0x14000000u;
                }
            } else {
                char at[64], an[128];
                action_text(selr->index, at, sizeof at, an, sizeof an, st->dark);
                snprintf(ptitle, sizeof ptitle, "%s", at);
                snprintf(pnote, sizeof pnote, "%s", an);
                snprintf(pstatus, sizeof pstatus, "Action");
                pcol = FINN_DIM;
                pbg = finn_theme_dark() ? 0x1AFFFFFFu : 0x14000000u;
            }

            fsvg *ic = row_icon(selr);
            float big = s * 64.0f;
            float by = r_y + s * 130.0f;
            float bx = rcx - big * 0.5f;

            if (icon_is_card(selr) && ic && ic->vw > 0) {
                finn_icon_blit(ic, g_layer, pw, ph,
                               (int)lroundf(bx), (int)lroundf(by - big * 0.5f),
                               (int)lroundf(big), 0, 0, 0);
            } else {
                finn_fill_rrect(g_layer, pw, ph, bx, by - big * 0.5f,
                                big, big, s * 14.0f, FINN_TILE);
                if (ic && ic->vw > 0)
                    finn_icon_blit_ink(ic, g_layer, pw, ph,
                                       (int)lroundf(rcx), (int)lroundf(by),
                                       (int)lroundf(big * 0.56f),
                                       FINN_INK, FINN_MUTE, 0);
            }

            char fitted[128];
            finnapp_ellipsis(&g_f_head, fitted, sizeof fitted, ptitle,
                             r_w - s * 40.0f);
            float nw = finn_text_width(&g_f_head, fitted);
            finn_text_draw(&g_f_head, g_layer, pw, ph, rcx - nw * 0.5f,
                           by + big * 0.5f + s * 42.0f, fitted, FINN_INK);

            char fitted_note[192];
            fitted_note[0] = 0;
            if (pnote[0] && strcmp(pnote, ptitle) != 0) {
                finnapp_ellipsis(&g_f_sub, fitted_note, sizeof fitted_note,
                                 pnote, r_w - s * 40.0f);
                float nw2 = finn_text_width(&g_f_sub, fitted_note);
                finn_text_draw(&g_f_sub, g_layer, pw, ph, rcx - nw2 * 0.5f,
                               by + big * 0.5f + s * 62.0f, fitted_note,
                               FINN_DIM);
            }

            float sw = finn_text_width(&g_f_sub, pstatus);
            float pill_w = sw + s * 22.0f, pill_h = s * 24.0f;
            float pill_x = rcx - pill_w * 0.5f;
            float pill_y = by + big * 0.5f +
                           s * (fitted_note[0] ? 80.0f : 62.0f);
            finn_fill_rrect(g_layer, pw, ph, pill_x, pill_y, pill_w, pill_h,
                            pill_h * 0.5f, pbg);
            finn_text_draw(&g_f_sub, g_layer, pw, ph,
                           pill_x + (pill_w - sw) * 0.5f,
                           pill_y + pill_h * 0.5f +
                               finn_text_cap_height(&g_f_sub) * 0.5f,
                           pstatus, pcol);

            /*
             * A pin is a taskbar preference, so it belongs with the app, not in
             * a settings page far from the thing being pinned. Only an
             * installed app gets the control: a catalogue entry that is not on
             * the machine has no tile to pin.
             */
            if (a && a->installed) {
                int pinned = menu_is_pinned(a->id);
                const char *label = pinned ? "Unpin from taskbar" : "Pin to taskbar";
                float lw = finn_text_width(&g_f_sub, label);
                float bw = lw + s * 30.0f, bh = s * 30.0f;
                float pbx = rcx - bw * 0.5f;
                float pby = pill_y + pill_h + s * 14.0f;
                uint32_t bg = pinned
                            ? (finn_theme_dark() ? 0x1FFFFFFFu : 0x14000000u)
                            : FINN_ON;
                uint32_t fg = pinned ? FINN_INK : 0xFFFFFFFFu;
                float wash = fmaxf(finn_anim_v(&g_a_pin) * FINN_MOTION_HOVER_WASH,
                                   g_press == FINNMENU_PIN
                                       ? finn_anim_v(&g_a_press) * FINN_MOTION_PRESS_WASH
                                       : 0.0f);
                finn_fill_rrect(g_layer, pw, ph, pbx, pby, bw, bh, bh * 0.5f, bg);
                if (wash > 0.004f) {
                    uint32_t a8 = (uint32_t)(wash * 255.0f + 0.5f);
                    finn_fill_rrect(g_layer, pw, ph, pbx, pby, bw, bh,
                                    bh * 0.5f,
                                    (a8 << 24) | (pinned ? (FINN_INK & 0x00FFFFFFu)
                                                          : 0x00000000u));
                }
                finn_text_draw(&g_f_sub, g_layer, pw, ph,
                               pbx + (bw - lw) * 0.5f,
                               pby + bh * 0.5f +
                                   finn_text_cap_height(&g_f_sub) * 0.5f,
                               label, fg);
                g_r_pin.x = (int)lroundf(pbx);
                g_r_pin.y = (int)lroundf(pby);
                g_r_pin.w = (int)lroundf(bw);
                g_r_pin.h = (int)lroundf(bh);
            } else {
                g_r_pin.x = g_r_pin.y = g_r_pin.w = g_r_pin.h = 0;
            }
        } else {
            g_r_pin.x = g_r_pin.y = g_r_pin.w = g_r_pin.h = 0;
            float cap = finn_text_cap_height(&g_f_sub);
            const char *l1 = g_query[0] ? "No matches" : "Start typing";
            const char *l2 = g_query[0] ? "Try an app, a setting or an action."
                                        : "Apps, settings and actions.";
            float w1 = finn_text_width(&g_f_sub, l1);
            float w2 = finn_text_width(&g_f_micro, l2);
            finn_text_draw(&g_f_sub, g_layer, pw, ph, rcx - w1 * 0.5f,
                           r_y + r_h * 0.44f + cap, l1, FINN_DIM);
            finn_text_draw(&g_f_micro, g_layer, pw, ph, rcx - w2 * 0.5f,
                           r_y + r_h * 0.44f + cap + s * 22.0f, l2, FINN_MUTE);
        }
    }

    /* 5. Account row. It is the same 85% tile as the right panel, and it is
     * fixed to the bottom of the left column so a long recent list cannot
     * push the power controls off the surface. */
    float ac_x = s * ACCOUNT_X, ac_y = s * ACCOUNT_Y;
    float ac_w = s * ACCOUNT_W, ac_h = s * ACCOUNT_H;
    finn_fill_rrect(g_layer, pw, ph, ac_x, ac_y, ac_w, ac_h,
                    s * ACCOUNT_R, FINN_TILE);

    int av_px = rs(AVATAR_SZ, s);
    float av_top = ac_y + (ac_h - (float)av_px) * 0.5f;
    if (g_ic_avatar.vw > 0) {
        finn_icon_blit(&g_ic_avatar, g_layer, pw, ph,
                       (int)lroundf(ac_x + s * AVATAR_X),
                       (int)lroundf(av_top), av_px, 0, 0, 0);
    } else {
        float av_cx = ac_x + s * AVATAR_X + s * (AVATAR_SZ * 0.5f);
        float av_cy = ac_y + ac_h * 0.5f;
        float av_r = s * (AVATAR_SZ * 0.5f);
        finn_fill_rrect(g_layer, pw, ph, av_cx - av_r, av_cy - av_r,
                        av_r * 2.0f, av_r * 2.0f, av_r, FINN_ON);
    }

    float ac_cap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, g_layer, pw, ph,
                   ac_x + s * NAME_X, ac_y + ac_h * 0.5f + ac_cap * 0.45f,
                   username, FINN_INK);

    int btn_sz = rs(BTN_ICO_SZ, s);
    int hit_pad = rs(26.0f * REF, s);

    g_r_restart.x = (int)lroundf(ac_x + s * RESTART_X + btn_sz * 0.5f - hit_pad * 0.5f);
    g_r_restart.y = (int)lroundf(ac_y + ac_h * 0.5f - hit_pad * 0.5f);
    g_r_restart.w = hit_pad; g_r_restart.h = hit_pad;

    g_r_shutdown.x = (int)lroundf(ac_x + s * POWER_X + btn_sz * 0.5f - hit_pad * 0.5f);
    g_r_shutdown.y = (int)lroundf(ac_y + ac_h * 0.5f - hit_pad * 0.5f);
    g_r_shutdown.w = hit_pad; g_r_shutdown.h = hit_pad;

    if (g_hover_idx == HOVER_RESTART && hover_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_restart, (float)hit_pad * 0.5f,
                       FINN_MOTION_HOVER_WASH * hover_v);
    if (g_press_idx == HOVER_RESTART && press_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_restart, (float)hit_pad * 0.5f,
                       FINN_MOTION_PRESS_WASH * press_v);

    if (g_hover_idx == HOVER_SHUTDOWN && hover_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_shutdown, (float)hit_pad * 0.5f,
                       FINN_MOTION_HOVER_WASH * hover_v);
    if (g_press_idx == HOVER_SHUTDOWN && press_v > 0.004f)
        control_wash_r(g_layer, pw, ph, g_r_shutdown, (float)hit_pad * 0.5f,
                       FINN_MOTION_PRESS_WASH * press_v);

    if (g_ic_refresh.vw > 0)
        finn_icon_blit_ink(&g_ic_refresh, g_layer, pw, ph,
                           (int)lroundf(ac_x + s * RESTART_X + btn_sz * 0.5f),
                           (int)lroundf(ac_y + ac_h * 0.5f),
                           btn_sz, FINN_INK, FINN_INK, 0);
    if (g_ic_power.vw > 0)
        finn_icon_blit_ink(&g_ic_power, g_layer, pw, ph,
                           (int)lroundf(ac_x + s * POWER_X + btn_sz * 0.5f),
                           (int)lroundf(ac_y + ac_h * 0.5f),
                           btn_sz, FINN_INK, FINN_INK, 0);
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
    return r->w > 0 && r->h > 0 &&
           x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int finnmenu_hit(int px, int py, finnmenu_result *out) {
    if (out) { out->kind = FINNMENU_RESULT_NONE; out->index = -1; out->row = -1; }
    if (!g_open || !g_layer || !finnmenu_visible()) return FINNMENU_NONE;
    if (finn_anim_v(&g_a_open) < 0.90f) return FINNMENU_NONE;

    int x = px - g_lx0, y = py - g_ly0;
    if (x < 0 || y < 0 || x >= g_lw || y >= g_lh) return FINNMENU_NONE;

    if (g_mode == MENU_MODE_SEARCH && in_rect(&g_r_search, x, y))
        return FINNMENU_SEARCH;
    if (g_mode == MENU_MODE_HOME && in_rect(&g_r_viewall, x, y))
        return FINNMENU_VIEW_ALL;
    if (g_mode != MENU_MODE_HOME && in_rect(&g_r_back, x, y))
        return FINNMENU_BACK;

    for (int i = 0; i < g_n_rows; i++) {
        if (!in_rect(&g_r_row[i], x, y)) continue;
        if (out) {
            out->kind = g_rows[i].kind;
            out->index = g_rows[i].index;
            out->row = i;
        }
        return FINNMENU_RESULT;
    }

    if (in_rect(&g_r_restart, x, y))   return FINNMENU_RESTART;
    if (in_rect(&g_r_shutdown, x, y))  return FINNMENU_SHUTDOWN;

    /* The pin button belongs to the previewed app, so the hit carries that
     * app: the compositor toggles the same id the panel is showing. */
    const menu_row *selr = preview_row();
    if (selr && g_r_pin.w > 0 && in_rect(&g_r_pin, x, y)) {
        if (out) {
            out->kind = selr->kind;
            out->index = selr->index;
            out->row = (int)(selr - g_rows);
        }
        return FINNMENU_PIN;
    }

    return in_rect(&g_r_panel, x, y) ? FINNMENU_PANEL : FINNMENU_NONE;
}

void finnmenu_focus_search(void) {
    if (!g_open) return;
    g_mode = MENU_MODE_SEARCH;
    g_search_focused = 1;
    g_sel = -1;
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
    else if (what == FINNMENU_RESTART)  next = HOVER_RESTART;
    else if (what == FINNMENU_SHUTDOWN) next = HOVER_SHUTDOWN;
    else if (what == FINNMENU_VIEW_ALL) next = HOVER_VIEWALL;
    else if (what == FINNMENU_BACK)     next = HOVER_BACK;
    else if (what == FINNMENU_PIN || what == FINNMENU_PANEL ||
             what == FINNMENU_SEARCH)
        next = g_hover_idx;   /* the preview stays while the pointer is inside */

    /* The pointer taking a row means the keyboard is no longer driving the
     * preview; the two must not leave two rows highlighted at once. */
    if (what == FINNMENU_RESULT && g_sel >= 0) {
        g_sel = -1;
        g_built = 0;
    }

    int on_pin = (what == FINNMENU_PIN);
    if (on_pin != g_hover_pin) {
        g_hover_pin = on_pin;
        finn_anim_to(&g_a_pin, on_pin ? 1.0f : 0.0f,
                     on_pin ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT,
                     0.0f, FINN_EASE_OUT);
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
        return;
    }

    if (what == FINNMENU_RESULT)        g_press_idx = g_hover_idx;
    else if (what == FINNMENU_RESTART)  g_press_idx = HOVER_RESTART;
    else if (what == FINNMENU_SHUTDOWN) g_press_idx = HOVER_SHUTDOWN;
    else if (what == FINNMENU_VIEW_ALL) g_press_idx = HOVER_VIEWALL;
    else if (what == FINNMENU_BACK)     g_press_idx = HOVER_BACK;
    else                                g_press_idx = -1;

    if (what == FINNMENU_PANEL || what == FINNMENU_SEARCH) {
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
        return;
    }
    finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
}

/* ---------------- keyboard ---------------- */

static void set_query(const char *text) {
    snprintf(g_query, sizeof g_query, "%s", text ? text : "");
    g_mode = MENU_MODE_SEARCH;
    g_sel = g_query[0] ? 0 : -1;
    g_search_focused = 1;
    g_built = 0;
    build_rows();
}

static void move_selection(int delta) {
    build_rows();
    if (g_n_rows <= 0) { g_sel = -1; g_built = 0; return; }

    int next = g_sel + delta;
    if (g_sel < 0) next = delta > 0 ? 0 : g_n_rows - 1;
    if (next < 0) next = 0;
    if (next >= g_n_rows) next = g_n_rows - 1;
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
        if (g_mode == MENU_MODE_SEARCH) {
            if (g_query[0]) {
                set_query("");
            } else {
                finnmenu_show_home();
            }
            return 1;
        }
        if (g_mode == MENU_MODE_ALL) {
            finnmenu_show_home();
            return 1;
        }
        finnmenu_set_open(0);
        return 1;
    }

    if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
        build_rows();
        if (g_query[0] && g_n_rows <= 0) return 1;
        int row = g_sel;
        if (row < 0) row = g_hover_idx;
        if (row >= 0 && row < g_n_rows && activate) {
            activate->kind = g_rows[row].kind;
            activate->index = g_rows[row].index;
            activate->row = row;
        }
        return 1;
    }

    if (e->code == KEY_UP)   { move_selection(-1); return 1; }
    if (e->code == KEY_DOWN) { move_selection(1);  return 1; }

    if (e->code == KEY_BACKSPACE) {
        if (g_mode != MENU_MODE_SEARCH) return 1;
        size_t len = strlen(g_query);
        if (len) g_query[len - 1] = '\0';
        g_sel = g_query[0] ? 0 : -1;
        g_built = 0;
        build_rows();
        return 1;
    }

    /* Text is search. There is no other text field in the launcher, so a
     * letter typed in any view belongs in a new query, and the menu moves to
     * Search rather than pretending the character went somewhere else. */
    if (!(e->mods & (FINN_MOD_CTRL | FINN_MOD_ALT | FINN_MOD_SUPER)) &&
        e->keysym >= 32 && e->keysym <= 126 &&
        strlen(g_query) + 1 < sizeof g_query) {
        if (g_mode != MENU_MODE_SEARCH) g_query[0] = '\0';
        size_t len = strlen(g_query);
        g_query[len] = (char)e->keysym;
        g_query[len + 1] = '\0';
        g_mode = MENU_MODE_SEARCH;
        g_search_focused = 1;
        g_sel = 0;
        g_built = 0;
        build_rows();
        return 1;
    }

    return 0;
}
