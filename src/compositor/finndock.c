/* finndock — the FinnOS desktop shell chrome (ui/Desktop.fui). */
#define _GNU_SOURCE
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "finnvg.h"

/*
 * Metrics authored for the Figma design system (1280x832 canvas, scaled by
 * REF = 1080 / 832 = 1.298077):
 *
 * 4 floating pills at rest:
 *   1. Taskbar (Apps):    w=115, h=42, r=8 (chips 26x26 r5.91 at x=9, 45, 81)
 *   2. Navigation:        w=116, h=42, r=8 (search x=10, home x=47, windows x=84)
 *   3. Time & Weather:    w=139, h=42, r=8 (clock 16px, sun 24px, 23°C/Sunny)
 *   4. Status:            w=116, h=42, r=8 (volume x=10, wifi x=47, battery x=84)
 *
 * Gaps between pills: 15 design px (19.5 canvas px).
 * Bottom margin:      15 design px (19.5 canvas px).
 */
#define REF           1.298077f

#define DOCK_H        (42.0f * REF)
#define DOCK_R        (8.0f * REF)
#define DOCK_GAP      (15.0f * REF)
#define DOCK_BOTTOM   (15.0f * REF)

#define PILL1_W       (115.0f * REF)
#define PILL2_W       (116.0f * REF)
#define PILL3_W       (139.0f * REF)
#define PILL4_W       (116.0f * REF)

#define TOTAL_W       (PILL1_W + DOCK_GAP + PILL2_W + DOCK_GAP + PILL3_W + DOCK_GAP + PILL4_W)

#define DOCK_GAP_SUNK (7.5f * REF)

#define GLASS_TINT    FINN_GLASS_PANEL
#define GLASS_BAR     0.94f

/* ---------------- assets ---------------- */
static fsvg g_ic_files, g_ic_arc, g_ic_settings_app;
static fsvg g_ic_search, g_ic_home, g_ic_windows;
static fsvg g_ic_sun_weather;
static fsvg g_ic_vol, g_ic_wifi, g_ic_bat, g_ic_bolt;
static int  g_ready = 0;

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
        g_ic_bat.shapes[1].clip_x = 4.0f + 14.0f * (float)pct / 100.0f;
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
            if (i == 3) {
                q->col = (sig < 0) ? FINN_MUTE : FINN_INK;
            } else {
                int first_lit = 3 - sig;
                q->col = (sig >= 0 && i >= first_lit) ? FINN_INK : FINN_MUTE;
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

/* ---------------- motion & state ---------------- */

static finn_anim g_a_press;
static int      g_pressed = FINNDOCK_HIT_NONE;

static float    g_well = 0.0f;
static float    g_well_clock = 0.0f;
static float    g_well_apps  = 0.0f;
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

void finndock_set_apps_well(float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    g_well_apps = amount;
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
    if (which == FINNDOCK_HIT_NONE)
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
}

static finn_anim g_a_hover;
static float     g_hover = 0.0f;
static int       g_hover_on = 0;

static finn_anim g_a_hover_c;
static float     g_hover_c = 0.0f;
static int       g_hover_clock = 0;

static finn_anim g_a_hover_l;
static float     g_hover_l = 0.0f;
static int       g_hover_launcher = 0;

static finn_anim g_a_hover_a;
static float     g_hover_a = 0.0f;
static int       g_hover_apps = 0;

static int g_p1_x = 0, g_p1_w = 0;
static int g_p2_x = 0, g_p2_w = 0;
static int g_p3_x = 0, g_p3_w = 0;
static int g_p4_x = 0, g_p4_w = 0;
static int g_p_y = 0, g_p_h = 0;

void finndock_hover_at(int px, int py) {
    int hit = finndock_hit(px, py);

    int on = (hit == FINNDOCK_HIT_TRAY);
    if (on != g_hover_on) {
        g_hover_on = on;
        finn_anim_to(&g_a_hover, on ? 1.0f : 0.0f,
                     on ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int onc = (hit == FINNDOCK_HIT_CLOCK);
    if (onc != g_hover_clock) {
        g_hover_clock = onc;
        finn_anim_to(&g_a_hover_c, onc ? 1.0f : 0.0f,
                     onc ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int ona = (hit == FINNDOCK_HIT_APPS);
    if (ona != g_hover_apps) {
        g_hover_apps = ona;
        finn_anim_to(&g_a_hover_a, ona ? 1.0f : 0.0f,
                     ona ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int onl = (hit == FINNDOCK_HIT_LAUNCHER || hit == FINNDOCK_HIT_SEARCH || hit == FINNDOCK_HIT_WINDOWS);
    if (onl != g_hover_launcher) {
        g_hover_launcher = onl;
        finn_anim_to(&g_a_hover_l, onl ? 1.0f : 0.0f,
                     onl ? FINN_MOTION_HOVER_IN : FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }
}

int finndock_tick(float dt) {
    int moved = finn_anim_tick(&g_a_press, dt);
    moved |= finn_anim_tick(&g_a_hover, dt);
    moved |= finn_anim_tick(&g_a_hover_c, dt);
    moved |= finn_anim_tick(&g_a_hover_l, dt);
    moved |= finn_anim_tick(&g_a_hover_a, dt);
    moved |= finn_anim_tick(&g_a_sink, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; moved = 1; }
    float hc = finn_anim_v(&g_a_hover_c);
    if (hc != g_hover_c) { g_hover_c = hc; moved = 1; }
    float hl = finn_anim_v(&g_a_hover_l);
    if (hl != g_hover_l) { g_hover_l = hl; moved = 1; }
    float ha = finn_anim_v(&g_a_hover_a);
    if (ha != g_hover_a) { g_hover_a = ha; moved = 1; }
    float sv = finn_anim_v(&g_a_sink);
    if (sv != g_sink) { g_sink = sv; moved = 1; }
    return moved;
}

int finndock_running(void) {
    return finn_anim_running(&g_a_press) || finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_hover_c) || finn_anim_running(&g_a_hover_l) ||
           finn_anim_running(&g_a_hover_a) || finn_anim_running(&g_a_sink);
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

static void load_fonts(float s) {
    finn_font_free(&g_font);
    finn_font_free(&g_f_temp);
    finn_font_free(&g_f_cond);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    int px_clock = rs(16.0f * REF, s);
    int px_temp  = rs(12.0f * REF, s);
    int px_cond  = rs(10.0f * REF, s);
    finn_font_load(&g_font, p, px_clock < 8 ? 8 : px_clock);
    finn_font_load(&g_f_temp, p, px_temp < 6 ? 6 : px_temp);
    finn_font_load(&g_f_cond, p, px_cond < 5 ? 5 : px_cond);
}

int finndock_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);
    g_font_scale = -1.0f;

    char p[512];
    int ok = 1;
    snprintf(p, sizeof p, "%s/app_files.svg", icon_dir);        ok &= fsvg_load(p, &g_ic_files) == 0;
    snprintf(p, sizeof p, "%s/app_arc.svg", icon_dir);          ok &= fsvg_load(p, &g_ic_arc) == 0;
    snprintf(p, sizeof p, "%s/app_settings.svg", icon_dir);     ok &= fsvg_load(p, &g_ic_settings_app) == 0;

    snprintf(p, sizeof p, "%s/search.svg", icon_dir);           ok &= fsvg_load(p, &g_ic_search) == 0;
    snprintf(p, sizeof p, "%s/home.svg", icon_dir);             ok &= fsvg_load(p, &g_ic_home) == 0;
    snprintf(p, sizeof p, "%s/windows.svg", icon_dir);          ok &= fsvg_load(p, &g_ic_windows) == 0;

    snprintf(p, sizeof p, "%s/sun_weather.svg", icon_dir);      ok &= fsvg_load(p, &g_ic_sun_weather) == 0;

    snprintf(p, sizeof p, "%s/volume.svg", icon_dir);           ok &= fsvg_load(p, &g_ic_vol) == 0;
    snprintf(p, sizeof p, "%s/wifi.svg", icon_dir);             ok &= fsvg_load(p, &g_ic_wifi) == 0;
    snprintf(p, sizeof p, "%s/battery.svg", icon_dir);          ok &= fsvg_load(p, &g_ic_bat) == 0;
    snprintf(p, sizeof p, "%s/battery_bolt.svg", icon_dir);     ok &= fsvg_load(p, &g_ic_bolt) == 0;

    if (!ok) fprintf(stderr, "finndock: some icons could not be loaded from %s\n", icon_dir);
    g_ready = 1;
    return 0;
}

void finndock_free(void) {
    fsvg_free(&g_ic_files);
    fsvg_free(&g_ic_arc);
    fsvg_free(&g_ic_settings_app);
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_home);
    fsvg_free(&g_ic_windows);
    fsvg_free(&g_ic_sun_weather);
    fsvg_free(&g_ic_vol);
    fsvg_free(&g_ic_wifi);
    fsvg_free(&g_ic_bat);
    fsvg_free(&g_ic_bolt);

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

void finndock_render(int screen_w, int screen_h, float scale, const finn_state *st) {
    if (!g_ready) { g_visible = 0; return; }

    float f = scale;
    if (g_font_scale != f) { load_fonts(f); g_font_scale = f; }

    float sink = g_sink;
    float u = f * REF;

    float dh = 42.0f * u;
    float dr = 8.0f * u;
    float gap = (15.0f * (1.0f - sink) + 7.5f * sink) * u;
    float bottom_m = (15.0f * (1.0f - sink)) * u;

    float p1_w = 115.0f * u;
    float p2_w = 116.0f * u;
    float p3_w = 139.0f * u;
    float p4_w = 116.0f * u;

    float p2_x = ((float)screen_w - p2_w) * 0.5f;
    float p1_x = p2_x - gap - p1_w;
    float p3_x = p2_x + p2_w + gap;
    float p4_x = p3_x + p3_w + gap;
    float y0 = screen_h - bottom_m - dh;

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

    g_p2_x = (int)lroundf(p2_x);
    g_p2_w = (int)lroundf(p2_w);

    g_p3_x = (int)lroundf(p3_x);
    g_p3_w = (int)lroundf(p3_w);

    g_p4_x = (int)lroundf(p4_x);
    g_p4_w = (int)lroundf(p4_w);

    g_p_y = (int)lroundf(y0);
    g_p_h = (int)lroundf(dh);

    char clock_s[8];
    int minute = clock_minute_now(clock_s, sizeof clock_s);
    g_clock_min = minute;

    uint32_t key = 2166136261u;
    #define MIX(v) do { key ^= (uint32_t)(v); key *= 16777619u; } while (0)
    MIX(screen_w); MIX(screen_h); MIX((int)(f * 1000.0f)); MIX(lw); MIX(lh);
    MIX(st->wifi + 2); MIX(st->battery_pct + 2); MIX(st->charging + 2);
    MIX(st->volume + 2); MIX(st->dark + 2);
    MIX(g_pressed);
    MIX(minute);
    MIX((int)(sink * 512.0f));
    MIX((int)(g_well * 64.0f));
    MIX((int)(g_well_clock * 64.0f));
    MIX((int)(g_well_apps * 64.0f));
    MIX((int)(g_well_launcher * 64.0f));
    MIX((int)(g_hover * 64.0f));
    MIX((int)(g_hover_c * 64.0f));
    MIX((int)(g_hover_l * 64.0f));
    MIX((int)(g_hover_a * 64.0f));
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
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
    }

    /* ---------------- PILL 1: Apps / Taskbar ---------------- */
    {
        float p1_bx = g_p1_x - lx;
        if (frost > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             p1_bx, by, p1_w, dh, dr,
                             FINN_GLASS, FINN_GLASS_PANEL * frost);
        }

        /* Running app highlight on slot 2 (Settings) if window is active */
        if (g_well_apps > 0.004f) {
            float bg_w = 34.0f * u, bg_h = 34.0f * u;
            float bg_x = p1_bx + 77.0f * u;
            float bg_y = by + 4.0f * u;
            finn_fill_rrect(g_layer, lw, lh, bg_x, bg_y, bg_w, bg_h, 4.0f * u, 0xFFFFFFFFu);

            /* Blue underline indicator */
            float ind_w = 6.0f * u, ind_h = 2.0f * u;
            float ind_x = p1_bx + (81.0f + 13.0f - 3.0f) * u;
            float ind_y = by + dh - ind_h - 1.0f * u;
            finn_fill_rrect(g_layer, lw, lh, ind_x, ind_y, ind_w, ind_h, 1.0f * u, FINN_ON);
        }

        /* 3 App chips: Files (x=9), Arc (x=45), Settings (x=81) */
        int chip_sz = rs(26.0f * REF, f);
        float cy = by + 8.0f * u;

        if (g_ic_files.vw > 0) {
            finn_icon_blit(&g_ic_files, g_layer, lw, lh,
                           (int)lroundf(p1_bx + 9.0f * u), (int)lroundf(cy),
                           chip_sz, 0, 0, 0);
        }
        if (g_ic_arc.vw > 0) {
            finn_icon_blit(&g_ic_arc, g_layer, lw, lh,
                           (int)lroundf(p1_bx + 45.0f * u), (int)lroundf(cy),
                           chip_sz, 0, 0, 0);
        }
        if (g_ic_settings_app.vw > 0) {
            finn_icon_blit(&g_ic_settings_app, g_layer, lw, lh,
                           (int)lroundf(p1_bx + 81.0f * u), (int)lroundf(cy),
                           chip_sz, 0, 0, 0);
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

        /* Active Home well when Start Menu is open (Figma 4:217) */
        if (g_well_launcher > 0.004f) {
            float bg_w = 42.0f * u, bg_h = dh;
            float bg_x = p2_bx + 37.0f * u;
            float bg_y = by;
            finn_fill_rrect(g_layer, lw, lh, bg_x, bg_y, bg_w, bg_h, dr, 0xFFFFFFFFu);
        }

        int ico_sz = rs(22.0f * REF, f);
        float icy = by + (dh - 22.0f * u) * 0.5f;

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

        if (g_well_clock > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             p3_bx + 4.0f * u, by + 4.0f * u, p3_w - 8.0f * u, dh - 8.0f * u,
                             4.0f * u, FINN_GLASS, 0.53f * g_well_clock);
        }

        /* Clock text (16px) */
        if (g_font.face) {
            float base_y = by + dh * 0.5f + rs(6.0f * REF, f);
            finn_text_draw(&g_font, g_layer, lw, lh,
                           p3_bx + 10.0f * u, base_y, clock_s, FINN_INK);
        }

        /* Sun icon (24px) */
        if (g_ic_sun_weather.vw > 0) {
            int sun_sz = rs(24.0f * REF, f);
            float sun_y = by + (dh - 24.0f * u) * 0.5f;
            finn_icon_blit(&g_ic_sun_weather, g_layer, lw, lh,
                           (int)lroundf(p3_bx + 60.0f * u), (int)lroundf(sun_y),
                           sun_sz, 0, 0, 0);
        }

        /* Weather text (23°C / Sunny) */
        if (g_f_temp.face && g_f_cond.face) {
            float tx = p3_bx + 93.0f * u;
            finn_text_draw(&g_f_temp, g_layer, lw, lh, tx, by + dh * 0.42f, "23°C", 0xFF2B2B2Bu);
            finn_text_draw(&g_f_cond, g_layer, lw, lh, tx, by + dh * 0.82f, "Sunny", 0xFF606060u);
        }
    }

    /* ---------------- PILL 4: Status ---------------- */
    {
        float p4_bx = g_p4_x - lx;
        if (frost > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             p4_bx, by, p4_w, dh, dr,
                             FINN_GLASS, FINN_GLASS_PANEL * frost);
        }

        /* White active well chip behind icons when Control Center is open */
        if (g_well > 0.004f) {
            float bg_w = 108.0f * u, bg_h = 34.0f * u;
            float bg_x = p4_bx + 4.0f * u;
            float bg_y = by + 4.0f * u;
            finn_fill_rrect(g_layer, lw, lh, bg_x, bg_y, bg_w, bg_h, 4.0f * u, 0xFFFFFFFFu);
        }

        int ico_sz = rs(22.0f * REF, f);
        float icy = by + (dh - 22.0f * u) * 0.5f;

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
    return rs(DOCK_H, scale);
}

void finndock_app_icon(int *x, int *y) {
    if (x) *x = g_p1_x + g_p1_w / 2;
    if (y) *y = g_p_y + g_p_h / 2;
}

int finndock_visible(void) { return g_visible; }

int finndock_rest_top(int screen_h, float scale) {
    return screen_h - rs(DOCK_BOTTOM + DOCK_H, scale);
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

int finndock_hit(int px, int py) {
    if (!g_visible || g_p_h <= 0) return FINNDOCK_HIT_NONE;
    if (py < g_p_y || py >= g_p_y + g_p_h) return FINNDOCK_HIT_NONE;

    if (px >= g_p1_x && px < g_p1_x + g_p1_w) return FINNDOCK_HIT_APPS;
    if (px >= g_p2_x && px < g_p2_x + g_p2_w) {
        float rel = (float)(px - g_p2_x) / (float)g_p2_w;
        if (rel < 0.33f) return FINNDOCK_HIT_SEARCH;
        if (rel < 0.67f) return FINNDOCK_HIT_LAUNCHER;
        return FINNDOCK_HIT_WINDOWS;
    }
    if (px >= g_p3_x && px < g_p3_x + g_p3_w) return FINNDOCK_HIT_CLOCK;
    if (px >= g_p4_x && px < g_p4_x + g_p4_w) return FINNDOCK_HIT_TRAY;

    return FINNDOCK_HIT_NONE;
}
