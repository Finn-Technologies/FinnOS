/* finndock — the FinnOS desktop shell chrome (ui/Desktop.fui). */
#define _GNU_SOURCE
#include "finndock.h"
#include "finndraw.h"
#include "finnblur.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnproto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "finnvg.h"

/*
 * Metrics authored for the Figma design system (1280x832 canvas, scaled by
 * the canonical finn_ui_scale() — 1.923x on the 2560x1600 VM):
 *
 * 4 floating pills at rest:
 *   1. Taskbar (Apps):    content-sized, h=42, r=8. The exported Figma frame
 *                         measures 115 wide with three 26px items, 10px gaps
 *                         and 8.5px outer insets; each tile is vertically centered.
 *   2. Navigation:        w=116, h=42, r=8 (search x=10, home x=47, windows x=84)
 *   3. Time & Weather:    w=139, h=42, r=8 (clock 16px at x=10, real weather if present).
 *                         Without a weather source the pill is the clock's own
 *                         width, so the half that would have held it is not air.
 *   4. Status / Tray:     w=116, h=42, r=8 (volume x=10, wifi x=47, battery x=84)
 *
 * Gaps between pills: 15 design px.
 * Bottom margin:      15 design px.
 */
#define DOCK_H        42.0f
#define DOCK_R        8.0f
#define DOCK_GAP      15.0f
#define DOCK_BOTTOM   15.0f

#define PILL2_W       116.0f
#define PILL3_W       139.0f
#define PILL4_W       116.0f

#define TILE_W        26.0f
#define TILE_STEP     36.0f
#define TILE_OUTER_INSET 8.5f
#define TILE_TOP_INSET 8.0f
#define TILE_RADIUS   5.909f

#define DOCK_GAP_SUNK 7.5f

#define GLASS_BAR     0.94f

static float g_dock_u = 2.0f;

/* ---------------- assets ---------------- */
static fsvg g_ic_search, g_ic_home, g_ic_windows;
static fsvg g_ic_sun_weather;
static fsvg g_ic_vol, g_ic_wifi, g_ic_bat;
static int  g_ready = 0;
static char g_icon_dir[512];

/* One icon per taskbar tile, loaded from the tile's own icon file. */
static fsvg g_tile_ic[FINNDOCK_APPS_MAX];
static char g_tile_icon_name[FINNDOCK_APPS_MAX][48];

/* The taskbar list the compositor last gave us, and a hash of it so a render
 * can tell whether anything it draws has changed. */
static finndock_app g_tasks[FINNDOCK_APPS_MAX];
static int          g_n_tasks = 0;
static uint32_t     g_task_hash = 0;

/* typography */
static finn_font g_font;       /* 16px clock */
static finn_font g_f_temp;     /* 12px weather temp */
static finn_font g_f_cond;     /* 10px weather cond */
static float     g_font_scale = -1.0f;
static char      g_font_dir[512];

/* ---------------- icons ---------------- */

static const uint32_t *icon_battery(int px, const finn_state *st) {
    int pct = st->battery_pct < 0 ? 100 : st->battery_pct;
    if (pct > 100) pct = 100;
    if (g_ic_bat.n > 1) {
        g_ic_bat.shapes[1].clip_x = 3.0f + 14.0f * (float)pct / 100.0f;
    }
    uint32_t rev = 1000u + (uint32_t)pct * 2u + (st->charging ? 1u : 0u);
    return finn_icon_ref(&g_ic_bat, px, FINN_INK, FINN_MUTE, rev);
}

static const uint32_t *icon_wifi(int px, const finn_state *st) {
    int sig = st->wifi;
    if (g_ic_wifi.n > 0) {
        fshape *sh = &g_ic_wifi.shapes[0];
        for (int i = 0; i < sh->npolys; i++) {
            fpoly *q = &sh->polys[i];
            q->vis = 1;
            if (sig < 0) {
                q->col = FINN_MUTE;
            } else if (i >= 3) {
                q->col = FINN_INK;
            } else if (sig == 1 && i == 2) {
                q->col = FINN_INK;
            } else if (sig == 2 && (i == 1 || i == 2)) {
                q->col = FINN_INK;
            } else if (sig >= 3) {
                q->col = FINN_INK;
            } else {
                q->col = FINN_MUTE;
            }
        }
    }
    return finn_icon_ref(&g_ic_wifi, px, FINN_INK, FINN_MUTE,
                         2000u + (uint32_t)(sig < 0 ? 50 : sig));
}

static const uint32_t *icon_volume(int px, const finn_state *st) {
    int lvl = st->volume;
    int waves = (lvl < 0) ? 1 : (lvl < 5 ? 0 : (lvl < 55 ? 1 : 2));

    if (g_ic_vol.n >= 3) {
        g_ic_vol.shapes[0].fill = FINN_INK;
        g_ic_vol.shapes[1].visible = (waves >= 1);
        if (waves >= 2) {
            g_ic_vol.shapes[2].visible = 1;
            g_ic_vol.shapes[2].stroke = 0xFF000000u;
        } else if (waves == 1) {
            g_ic_vol.shapes[2].visible = 1;
            g_ic_vol.shapes[2].stroke = 0xFF969696u;
        } else {
            g_ic_vol.shapes[2].visible = 0;
        }
    }

    return finn_icon_ref(&g_ic_vol, px, FINN_INK, FINN_MUTE,
                         3000u + (uint32_t)waves);
}

/* The wall clock, resolved to the minute the pill actually shows. */
static int clock_minute_now(char *out, size_t n) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t sec = ts.tv_sec;
    struct tm lt;
    localtime_r(&sec, &lt);
    if (out && n) snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
    return lt.tm_hour * 60 + lt.tm_min;
}

/* Optional real weather source from finnstate directory */
static int read_weather(char *temp, size_t temp_sz, char *cond, size_t cond_sz) {
    FILE *f = fopen(FINN_STATE_DIR "/weather_temp", "r");
    if (!f) return 0;
    if (!fgets(temp, (int)temp_sz, f)) { fclose(f); return 0; }
    fclose(f);
    char *nl = strchr(temp, '\n'); if (nl) *nl = 0;
    if (!temp[0]) return 0;

    cond[0] = 0;
    f = fopen(FINN_STATE_DIR "/weather_cond", "r");
    if (f) {
        if (fgets(cond, (int)cond_sz, f)) {
            nl = strchr(cond, '\n'); if (nl) *nl = 0;
        }
        fclose(f);
    }
    return 1;
}

/* ---------------- motion & state ---------------- */

static finn_anim g_a_press;
static int      g_pressed = FINNDOCK_HIT_NONE;
static int      g_pressed_task = -1;

static float    g_well = 0.0f;
static float    g_well_clock = 0.0f;
static float    g_well_launcher = 0.0f;

void finndock_set_well(float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    g_well = amount;
}

void finndock_set_clock_well(float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    g_well_clock = amount;
}

void finndock_set_launcher_well(float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    g_well_launcher = amount;
}

static finn_anim g_a_sink;
static float     g_sink = 0.0f;
static int       g_sunk = 0;

void finndock_set_sunk(int sunk) {
    sunk = sunk ? 1 : 0;
    if (sunk == g_sunk) return;
    g_sunk = sunk;
    finn_anim_to(&g_a_sink, (float)sunk,
                 sunk ? FINN_MOTION_CLOSE : FINN_MOTION_OPEN,
                 0.0f, FINN_EASE_IN_OUT);
}

void finndock_press(int which) {
    g_pressed = which;
    if (which != FINNDOCK_HIT_TASK) g_pressed_task = -1;
    if (which == FINNDOCK_HIT_NONE)
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
}

void finndock_press_task(int index) {
    g_pressed_task = index;
    finndock_press(FINNDOCK_HIT_TASK);
}

static finn_anim g_a_task[FINNDOCK_APPS_MAX];
static float     g_task_hover[FINNDOCK_APPS_MAX];
static int       g_task_on[FINNDOCK_APPS_MAX];

static finn_anim g_a_hover_s;
static float     g_hover_s = 0.0f;
static int       g_hover_search = 0;

static finn_anim g_a_hover_l;
static float     g_hover_l = 0.0f;
static int       g_hover_launcher = 0;

static finn_anim g_a_hover_w;
static float     g_hover_w = 0.0f;
static int       g_hover_windows = 0;

static finn_anim g_a_hover_c;
static float     g_hover_c = 0.0f;
static int       g_hover_clock = 0;

static finn_anim g_a_hover_t;
static float     g_hover_t = 0.0f;
static int       g_hover_tray = 0;

static int g_p1_x = 0, g_p1_w = 0;
static int g_p2_x = 0, g_p2_w = 0;
static int g_p3_x = 0, g_p3_w = 0;
static int g_p4_x = 0, g_p4_w = 0;
static int g_p_y = 0, g_p_h = 0;

/* The taskbar tiles in screen coordinates, filled by the last render. */
static int g_tile_x[FINNDOCK_APPS_MAX];
static int g_tile_w[FINNDOCK_APPS_MAX];

void finndock_hover_at(int px, int py) {
    int hit = finndock_hit(px, py);
    int ti = (hit == FINNDOCK_HIT_TASK) ? finndock_task_at(px, py) : -1;

    for (int i = 0; i < FINNDOCK_APPS_MAX; i++) {
        int on = (i == ti);
        /* A tile that is not in the list has nothing to hover. */
        if (on && i >= g_n_tasks) on = 0;
        if (on == g_task_on[i]) continue;
        g_task_on[i] = on;
        finn_anim_to(&g_a_task[i], on ? 1.0f : 0.0f,
                     on ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT,
                     0.0f, FINN_EASE_OUT);
    }

    int ons = (hit == FINNDOCK_HIT_SEARCH);
    if (ons != g_hover_search) {
        g_hover_search = ons;
        finn_anim_to(&g_a_hover_s, ons ? 1.0f : 0.0f,
                     ons ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int onl = (hit == FINNDOCK_HIT_LAUNCHER);
    if (onl != g_hover_launcher) {
        g_hover_launcher = onl;
        finn_anim_to(&g_a_hover_l, onl ? 1.0f : 0.0f,
                     onl ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int onw = (hit == FINNDOCK_HIT_WINDOWS);
    if (onw != g_hover_windows) {
        g_hover_windows = onw;
        finn_anim_to(&g_a_hover_w, onw ? 1.0f : 0.0f,
                     onw ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int onc = (hit == FINNDOCK_HIT_CLOCK);
    if (onc != g_hover_clock) {
        g_hover_clock = onc;
        finn_anim_to(&g_a_hover_c, onc ? 1.0f : 0.0f,
                     onc ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int ont = (hit == FINNDOCK_HIT_TRAY);
    if (ont != g_hover_tray) {
        g_hover_tray = ont;
        finn_anim_to(&g_a_hover_t, ont ? 1.0f : 0.0f,
                     ont ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }
}

int finndock_tick(float dt) {
    int moved = finn_anim_tick(&g_a_press, dt);
    for (int i = 0; i < FINNDOCK_APPS_MAX; i++) {
        moved |= finn_anim_tick(&g_a_task[i], dt);
        float v = finn_anim_v(&g_a_task[i]);
        if (v != g_task_hover[i]) { g_task_hover[i] = v; moved = 1; }
    }
    moved |= finn_anim_tick(&g_a_hover_s, dt);
    moved |= finn_anim_tick(&g_a_hover_l, dt);
    moved |= finn_anim_tick(&g_a_hover_w, dt);
    moved |= finn_anim_tick(&g_a_hover_c, dt);
    moved |= finn_anim_tick(&g_a_hover_t, dt);
    moved |= finn_anim_tick(&g_a_sink, dt);

    float hs = finn_anim_v(&g_a_hover_s); if (hs != g_hover_s) { g_hover_s = hs; moved = 1; }
    float hl = finn_anim_v(&g_a_hover_l); if (hl != g_hover_l) { g_hover_l = hl; moved = 1; }
    float hw = finn_anim_v(&g_a_hover_w); if (hw != g_hover_w) { g_hover_w = hw; moved = 1; }
    float hc = finn_anim_v(&g_a_hover_c); if (hc != g_hover_c) { g_hover_c = hc; moved = 1; }
    float ht = finn_anim_v(&g_a_hover_t); if (ht != g_hover_t) { g_hover_t = ht; moved = 1; }
    float sv = finn_anim_v(&g_a_sink);    if (sv != g_sink)    { g_sink = sv;    moved = 1; }
    return moved;
}

int finndock_running(void) {
    if (finn_anim_running(&g_a_press) ||
        finn_anim_running(&g_a_hover_s) || finn_anim_running(&g_a_hover_l) ||
        finn_anim_running(&g_a_hover_w) || finn_anim_running(&g_a_hover_c) ||
        finn_anim_running(&g_a_hover_t) || finn_anim_running(&g_a_sink))
        return 1;
    for (int i = 0; i < FINNDOCK_APPS_MAX; i++)
        if (finn_anim_running(&g_a_task[i])) return 1;
    return 0;
}

/* ---------------- layer & rendering ---------------- */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx = 0, g_ly = 0;
static int g_visible = 0;

static uint32_t g_dock_key = 0;
static unsigned g_dock_rev = 0;
static int      g_dock_built = 0;
static int      g_clock_min = -1;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static void load_fonts(float u) {
    finn_font_free(&g_font);
    finn_font_free(&g_f_temp);
    finn_font_free(&g_f_cond);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    int px_clock = rs(16.0f, u);
    int px_temp  = rs(12.0f, u);
    int px_cond  = rs(10.0f, u);
    finn_font_load(&g_font, p, px_clock < 8 ? 8 : px_clock);
    finn_font_load(&g_f_temp, p, px_temp < 6 ? 6 : px_temp);
    finn_font_load(&g_f_cond, p, px_cond < 5 ? 5 : px_cond);
}

int finndock_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    g_font_scale = -1.0f;

    char p[512];
    int ok = 1;

    snprintf(p, sizeof p, "%s/search-01.svg", icon_dir);        ok &= fsvg_load(p, &g_ic_search) == 0;
    snprintf(p, sizeof p, "%s/home-09.svg", icon_dir);          ok &= fsvg_load(p, &g_ic_home) == 0;
    snprintf(p, sizeof p, "%s/view-open-windows.svg", icon_dir); ok &= fsvg_load(p, &g_ic_windows) == 0;

    snprintf(p, sizeof p, "%s/sun-03.svg", icon_dir);           ok &= fsvg_load(p, &g_ic_sun_weather) == 0;

    snprintf(p, sizeof p, "%s/volume-02.svg", icon_dir);        ok &= fsvg_load(p, &g_ic_vol) == 0;
    snprintf(p, sizeof p, "%s/wifi-02.svg", icon_dir);          ok &= fsvg_load(p, &g_ic_wifi) == 0;
    snprintf(p, sizeof p, "%s/battery.svg", icon_dir);          ok &= fsvg_load(p, &g_ic_bat) == 0;

    if (!ok) fprintf(stderr, "finndock: some icons could not be loaded from %s\n", icon_dir);
    g_ready = 1;
    return 0;
}

void finndock_free(void) {
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_home);
    fsvg_free(&g_ic_windows);
    fsvg_free(&g_ic_sun_weather);
    fsvg_free(&g_ic_vol);
    fsvg_free(&g_ic_wifi);
    fsvg_free(&g_ic_bat);
    for (int i = 0; i < FINNDOCK_APPS_MAX; i++) {
        fsvg_free(&g_tile_ic[i]);
        g_tile_icon_name[i][0] = 0;
    }
    g_n_tasks = 0;
    g_task_hash = 0;

    finn_font_free(&g_font);
    finn_font_free(&g_f_temp);
    finn_font_free(&g_f_cond);

    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_ready = 0;
    g_font_scale = -1.0f;
    g_clock_min = -1;
}

static void draw_wash(uint32_t *layer, int lw, int lh,
                      float x, float y, float w, float h, float r,
                      float hover_v, int is_pressed, float well_v) {
    float press_v = is_pressed ? finn_anim_v(&g_a_press) : 0.0f;
    float wash = fmaxf(hover_v * FINN_MOTION_HOVER_WASH, press_v * FINN_MOTION_PRESS_WASH);
    wash *= (1.0f - well_v);
    if (wash < 0.003f) return;
    uint32_t a8 = (uint32_t)(wash * 255.0f + 0.5f);
    uint32_t col = (a8 << 24) | (FINN_INK & 0x00FFFFFFu);
    finn_fill_rrect(layer, lw, lh, x, y, w, h, r, col);
}

static uint32_t task_hash(void) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX(g_n_tasks);
    for (int i = 0; i < g_n_tasks; i++) {
        const finndock_app *a = &g_tasks[i];
        for (int k = 0; a->id[k]; k++) MIX(a->id[k]);
        for (int k = 0; a->name[k]; k++) MIX(a->name[k]);
        for (int k = 0; a->icon[k]; k++) MIX(a->icon[k]);
        MIX(a->windows + 2); MIX(a->focused + 2);
        MIX(a->minimized + 2); MIX(a->pinned + 2);
    }
    #undef MIX
    return h ? h : 1u;
}

/* Load the tile icons the new list asks for. A tile whose icon name did not
 * change keeps the raster it already has; one that left the list releases
 * it. */
static void task_icons_sync(void) {
    for (int i = 0; i < FINNDOCK_APPS_MAX; i++) {
        const char *want = (i < g_n_tasks) ? g_tasks[i].icon : "";
        if (!strcmp(want, g_tile_icon_name[i])) continue;

        fsvg_free(&g_tile_ic[i]);
        g_tile_icon_name[i][0] = 0;
        if (!want[0] || !g_icon_dir[0]) continue;

        char path[600];
        snprintf(path, sizeof path, "%s/%s", g_icon_dir, want);
        if (fsvg_load(path, &g_tile_ic[i]) == 0)
            snprintf(g_tile_icon_name[i], sizeof g_tile_icon_name[i], "%s", want);
    }
}

void finndock_set_apps(const finndock_app *apps, int n) {
    if (!apps || n < 0) n = 0;
    if (n > FINNDOCK_APPS_MAX) n = FINNDOCK_APPS_MAX;

    int same = (n == g_n_tasks);
    for (int i = 0; same && i < n; i++) {
        if (strcmp(apps[i].id, g_tasks[i].id) ||
            strcmp(apps[i].name, g_tasks[i].name) ||
            strcmp(apps[i].icon, g_tasks[i].icon) ||
            apps[i].windows != g_tasks[i].windows ||
            apps[i].focused != g_tasks[i].focused ||
            apps[i].minimized != g_tasks[i].minimized ||
            apps[i].pinned != g_tasks[i].pinned)
            same = 0;
    }
    if (same) return;

    memset(g_tasks, 0, sizeof g_tasks);
    for (int i = 0; i < n; i++) g_tasks[i] = apps[i];
    g_n_tasks = n;
    task_icons_sync();
    g_task_hash = task_hash();
}

void finndock_render(int screen_w, int screen_h, float scale, const finn_state *st) {
    if (!g_ready) { g_visible = 0; return; }

    /* One canonical density, the same factor every other surface uses. */
    float u = scale;
    g_dock_u = u;

    if (g_font_scale != u) { load_fonts(u); g_font_scale = u; }

    float sink = g_sink;

    float dh = DOCK_H * u;
    float dr = DOCK_R * u;
    float gap = (DOCK_GAP * (1.0f - sink) + DOCK_GAP_SUNK * sink) * u;
    float bottom_m = (DOCK_BOTTOM * (1.0f - sink)) * u;

    /*
     * The clock pill is authored 139 wide because it holds time and a second
     * column. That column is the weather when a weather source exists. A
     * machine with none would otherwise leave a gap where the pill's other
     * half used to be, so the column shows the date instead: the same width,
     * the same two-line rhythm, and no invented temperature.
     */
    char clock_s[8];
    int minute = clock_minute_now(clock_s, sizeof clock_s);
    char wtemp[16] = {0}, wcond[32] = {0};
    int have_weather = read_weather(wtemp, sizeof wtemp, wcond, sizeof wcond);

    char date1_s[8] = {0}, date2_s[24] = {0};
    if (!have_weather) {
        time_t now = time(NULL);
        struct tm tm;
        if (localtime_r(&now, &tm)) {
            strftime(date1_s, sizeof date1_s, "%a", &tm);
            strftime(date2_s, sizeof date2_s, "%d %b", &tm);
        }
    }

    float p1_w = g_n_tasks > 0
               ? (TILE_OUTER_INSET * 2.0f + TILE_W +
                  (float)(g_n_tasks - 1) * TILE_STEP) * u
               : 0.0f;
    float p2_w = PILL2_W * u;
    float p3_w = PILL3_W * u;
    float p4_w = PILL4_W * u;

    float p2_x = ((float)screen_w - p2_w) * 0.5f;
    float p1_x = g_n_tasks > 0 ? p2_x - gap - p1_w : p2_x;
    float p3_x = p2_x + p2_w + gap;
    float p4_x = p3_x + p3_w + gap;
    float y0 = (float)screen_h - bottom_m - dh;

    float min_x = p1_x;
    float max_x = p4_x + p4_w;
    float total = max_x - min_x;

    /* Sinking bar */
    float bar_w = total + ((float)screen_w - total) * sink;
    float bar_x = min_x * (1.0f - sink);
    float bar_r = dr * (1.0f - sink);

    int slack = 4;
    int lx = (int)floorf(min_x < bar_x ? min_x : bar_x) - slack;
    int ly = (int)floorf(y0) - slack;
    float right_edge = (max_x > (bar_x + bar_w)) ? max_x : (bar_x + bar_w);
    int lw = (int)ceilf(right_edge) - lx + slack * 2;
    int lh = (int)ceilf(dh) + slack * 2;

    /* Pill screen coordinates */
    g_p1_x = (int)lroundf(p1_x);
    g_p1_w = (int)lroundf(p1_w);

    for (int i = 0; i < FINNDOCK_APPS_MAX; i++) {
        if (i < g_n_tasks) {
            g_tile_x[i] = (int)lroundf(p1_x +
                (TILE_OUTER_INSET + (float)i * TILE_STEP) * u);
            g_tile_w[i] = (int)lroundf(TILE_W * u);
        } else {
            g_tile_x[i] = 0;
            g_tile_w[i] = 0;
        }
    }

    g_p2_x = (int)lroundf(p2_x);
    g_p2_w = (int)lroundf(p2_w);

    g_p3_x = (int)lroundf(p3_x);
    g_p3_w = (int)lroundf(p3_w);

    g_p4_x = (int)lroundf(p4_x);
    g_p4_w = (int)lroundf(p4_w);

    g_p_y = (int)lroundf(y0);
    g_p_h = (int)lroundf(dh);

    g_clock_min = minute;

    uint32_t key = 2166136261u;
    #define MIX(v) do { key ^= (uint32_t)(v); key *= 16777619u; } while (0)
    MIX(screen_w); MIX(screen_h); MIX((int)(u * 1000.0f)); MIX(lw); MIX(lh);
    MIX(st->wifi + 2); MIX(st->battery_pct + 2); MIX(st->charging + 2);
    MIX(st->volume + 2); MIX(st->dark + 2);
    MIX(g_pressed);
    MIX(minute); MIX(have_weather);
    MIX((int)(sink * 512.0f));
    MIX((int)(g_well * 64.0f));
    MIX((int)(g_well_clock * 64.0f));
    MIX((int)(g_well_launcher * 64.0f));
    MIX(g_task_hash);
    MIX(g_pressed_task + 2);
    for (int i = 0; i < FINNDOCK_APPS_MAX; i++)
        MIX((int)(g_task_hover[i] * 64.0f));
    MIX((int)(g_hover_s * 64.0f));
    MIX((int)(g_hover_l * 64.0f));
    MIX((int)(g_hover_w * 64.0f));
    MIX((int)(g_hover_c * 64.0f));
    MIX((int)(g_hover_t * 64.0f));
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
    /* the glass is the desktop under it: when that moves, this redraws */
    MIX(finnblur_generation());
    #undef MIX

    if (g_dock_built && key == g_dock_key && g_layer) { g_visible = 1; return; }
    g_dock_key = key;
    g_dock_built = 1;
    g_dock_rev++;

    if (lw != g_lw || lh != g_lh) {
        free(g_layer);
        g_layer = malloc((size_t)lw * lh * 4);
        g_lw = lw; g_lh = lh;
    }
    if (!g_layer) { g_visible = 0; return; }
    memset(g_layer, 0, (size_t)lw * lh * 4);
    g_lx = lx; g_ly = ly;

    float by = y0 - ly;
    float frost = 1.0f - sink;

    if (sink > 0.004f) {
        finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                         bar_x - lx, by, bar_w, dh, bar_r,
                         FINN_GLASS, GLASS_BAR * sink);
        finn_stroke_rrect(g_layer, lw, lh, bar_x - lx, by, bar_w, dh,
                          bar_r, 1.0f, FINN_HAIR);
    }

    /* ---------------- PILL 1: Taskbar ---------------- */
    if (g_n_tasks > 0) {
        float p1_bx = g_p1_x - lx;
        if (frost > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             p1_bx, by, p1_w, dh, dr,
                             FINN_GLASS, FINN_GLASS_PANEL * frost);
        }

        for (int i = 0; i < g_n_tasks; i++) {
            const finndock_app *t = &g_tasks[i];
            float tx = (float)(g_tile_x[i] - lx);
            float ty = by + TILE_TOP_INSET * u;
            float tw = TILE_W * u;
            float pressed = (g_pressed == FINNDOCK_HIT_TASK && g_pressed_task == i) ? 1.0f : 0.0f;

            /* The focused app sits on a 34x34 white well (Figma 186:3892),
             * four design px larger than the tile on every side. An app that
             * is open but not focused gets the same well at half strength, and
             * a folded-away app barely at all: the shape says "open" and the
             * weight says how far away it is. A pinned app that is not open
             * has no well — the glass is its resting state. */
            if (t->windows > 0) {
                float plate = t->focused ? 1.0f : (t->minimized ? 0.20f : 0.42f);
                uint32_t a8 = (uint32_t)(plate * 255.0f + 0.5f);
                finn_fill_rrect(g_layer, lw, lh, tx - 4.0f * u, by + 4.0f * u,
                                34.0f * u, 34.0f * u, 4.0f * u,
                                (a8 << 24) | 0x00FFFFFFu);
            }

            draw_wash(g_layer, lw, lh, tx, ty, tw, tw, TILE_RADIUS * u,
                      g_task_hover[i], pressed > 0.5f, 0.0f);

            float icon = 26.0f * u;
            float ix = tx + (tw - icon) * 0.5f;
            float iy = ty + (tw - icon) * 0.5f;
            if (g_tile_ic[i].vw > 0) {
                finn_icon_blit(&g_tile_ic[i], g_layer, lw, lh,
                               (int)lroundf(ix), (int)lroundf(iy),
                               (int)lroundf(icon), 0, 0, 0);
            } else if (g_f_temp.face) {
                /* A task whose icon file is missing still gets a recognisable
                 * tile: the first letter of its name, in the shell's own type.
                 * The alternative is an empty square that looks broken. */
                char initial[2] = { t->name[0] ? t->name[0] : '?', 0 };
                float cap = finn_text_cap_height(&g_f_temp);
                float iw = finn_text_width(&g_f_temp, initial);
                finn_text_draw(&g_f_temp, g_layer, lw, lh,
                               tx + (tw - iw) * 0.5f,
                               ty + tw * 0.5f + cap * 0.5f,
                               initial, FINN_INK);
            }

            /* A running app carries the 4x1.5 blue mark the design gives the
             * focused tile (Figma 186:3893); a folded-away one carries it in
             * mute ink so the indicator says "open" without pretending it is
             * on screen. */
            if (t->windows > 0) {
                float ind_w = 4.0f * u;
                float ind_h = 1.5f * u;
                float ind_x = tx + (tw - ind_w) * 0.5f;
                float ind_y = by + dh - 2.0f * u - ind_h * 0.5f;
                finn_fill_rrect(g_layer, lw, lh, ind_x, ind_y, ind_w, ind_h,
                                ind_h * 0.5f,
                                t->minimized ? FINN_MUTE : FINN_ON);
            }
        }
    }

    /* ---------------- PILL 2: Navigation ---------------- */
    {
        float p2_bx = g_p2_x - lx;
        if (frost > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             p2_bx, by, p2_w, dh, dr,
                             FINN_GLASS, FINN_GLASS_PANEL * frost);
        }

        /* Active Home well when Start Menu is open (Figma 218:190): a plain
         * white 42x42 square, the height of the pill, not a rounded chip. */
        if (g_well_launcher > 0.004f) {
            float bg_w = 42.0f * u, bg_h = dh;
            float bg_x = p2_bx + 37.0f * u;
            float bg_y = by;
            uint32_t la = (uint32_t)(g_well_launcher * 255.0f + 0.5f);
            finn_fill_rrect(g_layer, lw, lh, bg_x, bg_y, bg_w, bg_h, 0.0f,
                            (la << 24) | 0x00FFFFFFu);
        }

        /* Hover / press washes for search, launcher, windows */
        draw_wash(g_layer, lw, lh, p2_bx + 4.0f * u, by + 4.0f * u, 34.0f * u, 34.0f * u, 4.0f * u,
                  g_hover_s, g_pressed == FINNDOCK_HIT_SEARCH, 0.0f);
        draw_wash(g_layer, lw, lh, p2_bx + 41.0f * u, by + 4.0f * u, 34.0f * u, 34.0f * u, 4.0f * u,
                  g_hover_l, g_pressed == FINNDOCK_HIT_LAUNCHER, g_well_launcher);
        draw_wash(g_layer, lw, lh, p2_bx + 78.0f * u, by + 4.0f * u, 34.0f * u, 34.0f * u, 4.0f * u,
                  g_hover_w, g_pressed == FINNDOCK_HIT_WINDOWS, 0.0f);

        int ico_sz = rs(22.0f, u);
        float icy = by + 10.0f * u;

        if (g_ic_search.vw > 0) {
            finn_icon_blit(&g_ic_search, g_layer, lw, lh,
                           (int)lroundf(p2_bx + 10.0f * u), (int)lroundf(icy),
                           ico_sz, FINN_INK, FINN_DIM, 0);
        }
        if (g_ic_home.vw > 0) {
            finn_icon_blit(&g_ic_home, g_layer, lw, lh,
                           (int)lroundf(p2_bx + 47.0f * u), (int)lroundf(icy),
                           ico_sz, FINN_INK, FINN_DIM, 0);
        }
        if (g_ic_windows.vw > 0) {
            finn_icon_blit(&g_ic_windows, g_layer, lw, lh,
                           (int)lroundf(p2_bx + 84.0f * u), (int)lroundf(icy),
                           ico_sz, FINN_INK, FINN_DIM, 0);
        }
    }

    /* ---------------- PILL 3: Time & Weather ---------------- */
    {
        float p3_bx = g_p3_x - lx;
        if (frost > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             p3_bx, by, p3_w, dh, dr,
                             FINN_GLASS, FINN_GLASS_PANEL * frost);
        }

        /* Active well when Notification Center / Clock page is open */
        if (g_well_clock > 0.004f) {
            float bg_w = p3_w - 8.0f * u, bg_h = 34.0f * u;
            float bg_x = p3_bx + 4.0f * u;
            float bg_y = by + 4.0f * u;
            uint32_t ca = (uint32_t)(g_well_clock * 255.0f + 0.5f);
            finn_fill_rrect(g_layer, lw, lh, bg_x, bg_y, bg_w, bg_h, 4.0f * u,
                            (ca << 24) | 0x00FFFFFFu);
        }

        /* Hover / press wash */
        draw_wash(g_layer, lw, lh, p3_bx + 4.0f * u, by + 4.0f * u, p3_w - 8.0f * u, 34.0f * u, 4.0f * u,
                  g_hover_c, g_pressed == FINNDOCK_HIT_CLOCK, g_well_clock);

        /* Clock text (16px, baseline y=27 in Figma 3:150) */
        if (g_font.face) {
            float base_y = by + 27.0f * u;
            finn_text_draw(&g_font, g_layer, lw, lh,
                           p3_bx + 10.0f * u, base_y, clock_s, FINN_INK);
        }

        /* Weather text & icon when a real source exists; otherwise the same
         * column carries the date, which is real and keeps the pill's shape. */
        if (have_weather) {
            const char *temp_s = wtemp;
            const char *cond_s = wcond;
            if (g_ic_sun_weather.vw > 0) {
                int sun_sz = rs(24.0f, u);
                float sun_y = by + 9.0f * u;
                finn_icon_blit(&g_ic_sun_weather, g_layer, lw, lh,
                               (int)lroundf(p3_bx + 60.0f * u), (int)lroundf(sun_y),
                               sun_sz, 0, 0, 0);
            }
            if (g_f_temp.face) {
                float tx = p3_bx + 93.0f * u;
                finn_text_draw(&g_f_temp, g_layer, lw, lh, tx, by + 19.0f * u, temp_s, FINN_INK);
            }
            if (g_f_cond.face && cond_s[0]) {
                float tx = p3_bx + 93.0f * u;
                finn_text_draw(&g_f_cond, g_layer, lw, lh, tx, by + 34.0f * u, cond_s, FINN_DIM);
            }
        } else if (date1_s[0]) {
            if (g_f_temp.face)
                finn_text_draw(&g_f_temp, g_layer, lw, lh,
                               p3_bx + 60.0f * u, by + 19.0f * u,
                               date1_s, FINN_INK);
            if (g_f_cond.face && date2_s[0])
                finn_text_draw(&g_f_cond, g_layer, lw, lh,
                               p3_bx + 60.0f * u, by + 34.0f * u,
                               date2_s, FINN_DIM);
        }
    }

    /* ---------------- PILL 4: Status / Tray ---------------- */
    {
        float p4_bx = g_p4_x - lx;
        if (frost > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             p4_bx, by, p4_w, dh, dr,
                             FINN_GLASS, FINN_GLASS_PANEL * frost);
        }

        /* White active well chip behind icons when Control Center is open (Figma 9:64) */
        if (g_well > 0.004f) {
            float bg_w = 108.0f * u, bg_h = 34.0f * u;
            float bg_x = p4_bx + 4.0f * u;
            float bg_y = by + 4.0f * u;
            uint32_t wa = (uint32_t)(g_well * 255.0f + 0.5f);
            finn_fill_rrect(g_layer, lw, lh, bg_x, bg_y, bg_w, bg_h, 4.0f * u,
                            (wa << 24) | 0x00FFFFFFu);
        }

        /* Hover / press wash */
        draw_wash(g_layer, lw, lh, p4_bx + 4.0f * u, by + 4.0f * u, 108.0f * u, 34.0f * u, 4.0f * u,
                  g_hover_t, g_pressed == FINNDOCK_HIT_TRAY, g_well);

        int ico_sz = rs(22.0f, u);
        float icy = by + 10.0f * u;

        const uint32_t *ic_v = icon_volume(ico_sz, st);
        if (ic_v) finn_blit(g_layer, lw, lh, ic_v, ico_sz, ico_sz,
                            (int)lroundf(p4_bx + 10.0f * u), (int)lroundf(icy));

        const uint32_t *ic_w = icon_wifi(ico_sz, st);
        if (ic_w) finn_blit(g_layer, lw, lh, ic_w, ico_sz, ico_sz,
                            (int)lroundf(p4_bx + 47.0f * u), (int)lroundf(icy));

        const uint32_t *ic_b = icon_battery(ico_sz, st);
        if (ic_b) finn_blit(g_layer, lw, lh, ic_b, ico_sz, ico_sz,
                            (int)lroundf(p4_bx + 84.0f * u), (int)lroundf(icy));
    }

    g_visible = 1;
}

void finndock_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!g_visible || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx + dx, g_ly + dy);
}

int finndock_height(float scale) {
    (void)scale;
    return rs(DOCK_H, g_dock_u > 0.0f ? g_dock_u : 2.0f);
}

void finndock_app_icon_for(const char *app_id, int *x, int *y) {
    if (app_id && *app_id) {
        for (int i = 0; i < g_n_tasks; i++) {
            if (!strcmp(g_tasks[i].id, app_id)) {
                if (x) *x = g_tile_x[i] + g_tile_w[i] / 2;
                if (y) *y = g_p_y + g_p_h / 2;
                return;
            }
        }
    }
    finndock_app_icon(x, y);
}

void finndock_app_icon(int *x, int *y) {
    if (x) *x = g_p1_x + g_p1_w / 2;
    if (y) *y = g_p_y + g_p_h / 2;
}

int finndock_visible(void) { return g_visible; }

int finndock_rest_top(int screen_h, float scale) {
    (void)scale;
    float u = g_dock_u > 0.0f ? g_dock_u : 2.0f;
    return screen_h - rs(DOCK_BOTTOM + DOCK_H, u);
}

void finndock_geometry(int *x, int *y, int *w, int *h) {
    if (x) *x = g_p1_x;
    if (y) *y = g_p_y;
    if (w) *w = (g_p4_x + g_p4_w) - g_p1_x;
    if (h) *h = g_p_h;
}

void finndock_tray(int *x, int *y, int *w, int *h) {
    if (x) *x = g_p4_x;
    if (y) *y = g_p_y;
    if (w) *w = g_p4_w;
    if (h) *h = g_p_h;
}

void finndock_clock(int *x, int *y, int *w, int *h) {
    if (x) *x = g_p3_x;
    if (y) *y = g_p_y;
    if (w) *w = g_p3_w;
    if (h) *h = g_p_h;
}

void finndock_launcher(int *x, int *y, int *w, int *h) {
    if (x) *x = g_p2_x;
    if (y) *y = g_p_y;
    if (w) *w = g_p2_w;
    if (h) *h = g_p_h;
}

int finndock_clock_tick(void) {
    int m = clock_minute_now(NULL, 0);
    if (m == g_clock_min) return 0;
    g_clock_min = m;
    return 1;
}

const uint32_t *finndock_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx;
    if (y) *y = g_ly;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_dock_rev;
    return g_visible ? g_layer : NULL;
}

int finndock_task_at(int px, int py) {
    if (!g_visible || g_p_h <= 0) return -1;
    if (py < g_p_y || py >= g_p_y + g_p_h) return -1;
    for (int i = 0; i < g_n_tasks; i++) {
        if (g_tile_w[i] > 0 &&
            px >= g_tile_x[i] && px < g_tile_x[i] + g_tile_w[i])
            return i;
    }
    return -1;
}

int finndock_hit(int px, int py) {
    if (!g_visible || g_p_h <= 0) return FINNDOCK_HIT_NONE;
    if (py < g_p_y || py >= g_p_y + g_p_h) return FINNDOCK_HIT_NONE;

    if (g_n_tasks > 0 && px >= g_p1_x && px < g_p1_x + g_p1_w)
        return finndock_task_at(px, py) >= 0 ? FINNDOCK_HIT_TASK : FINNDOCK_HIT_NONE;
    if (px >= g_p2_x && px < g_p2_x + g_p2_w) {
        float rel = (float)(px - g_p2_x) / (float)g_p2_w;
        if (rel < 0.333f) return FINNDOCK_HIT_SEARCH;
        if (rel < 0.667f) return FINNDOCK_HIT_LAUNCHER;
        return FINNDOCK_HIT_WINDOWS;
    }
    if (px >= g_p3_x && px < g_p3_x + g_p3_w) return FINNDOCK_HIT_CLOCK;
    if (px >= g_p4_x && px < g_p4_x + g_p4_w) return FINNDOCK_HIT_TRAY;

    return FINNDOCK_HIT_NONE;
}
