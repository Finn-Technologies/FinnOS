/* finnmenu — the FinnOS Start Menu (ui/StartMenu.fui / Desktop - Home). */
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
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

#define REF          1.298077f

#define MENU_W       (800.0f * REF)
#define MENU_H       (500.0f * REF)
#define MENU_R       (8.0f * REF)

#define LEFT_PAD     (20.0f * REF)
#define HEAD_Y       (25.0f * REF)
#define VIEWALL_X    (307.0f * REF)
#define VIEWALL_Y    (18.0f * REF)
#define VIEWALL_W    (88.0f * REF)
#define VIEWALL_H    (26.0f * REF)

#define APPLIST_Y    (56.0f * REF)
#define APP_ROW_H    (26.0f * REF)
#define APP_ROW_GAP  (10.0f * REF)
#define APP_ICON_SZ  (26.0f * REF)
#define APP_LABEL_GAP (14.0f * REF)
#define APP_TEXT_X   (LEFT_PAD + APP_ICON_SZ + APP_LABEL_GAP)

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

/* ---------------- fonts ---------------- */
static finn_font g_f_head, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];
static char  g_icon_dir[512];
static char  g_registry[512];

/* ---------------- icons ---------------- */
static fsvg g_ic_avatar, g_ic_refresh, g_ic_power;
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
static menu_rect g_r_panel, g_r_restart, g_r_shutdown;
static menu_rect g_r_app[FINN_APP_MAX];
static int       g_n_apps = 0;

/* ---------------- motion ---------------- */
static finn_anim g_a_open;
static finn_anim g_a_hover, g_a_press;
static float     g_hover = 0.0f;
static int       g_hover_idx = -1;
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
    snprintf(p, sizeof p, "%s/settings-app.svg", g_icon_dir);    fsvg_load(p, &g_ic_app_settings);
    snprintf(p, sizeof p, "%s/chevron-right.svg", g_icon_dir);   fsvg_load(p, &g_ic_chevron_right);

    return 0;
}

void finnmenu_free(void) {
    fsvg_free(&g_ic_avatar);
    fsvg_free(&g_ic_refresh);
    fsvg_free(&g_ic_power);
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
        g_built = 0;
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    } else {
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
        g_press = FINNMENU_NONE;
        g_press_idx = -1;
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
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
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finnmenu_running(void) {
    return finn_anim_running(&g_a_open) ||
           finn_anim_running(&g_a_hover) || finn_anim_running(&g_a_press);
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

    int n = finn_apps_count();
    if (n > FINN_APP_MAX) n = FINN_APP_MAX;
    g_n_apps = (n > 0) ? n : 1;

    char username[64];
    get_logged_in_user(username, sizeof username);

    uint32_t key = 2166136261u;
    #define MIX(v) do { key ^= (uint32_t)(v); key *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(pw); MIX(ph);
    MIX(g_lx0); MIX(g_ly0);
    MIX(st->dark + 2);
    MIX(g_n_apps);
    MIX((int)(g_hover * 64.0f));
    MIX(g_hover_idx + 2);
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
    MIX(g_press_idx + 2);
    for (int k = 0; username[k]; k++) MIX(username[k]);
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

    /* 1. Main frosted glass panel (r=8, 65% white) */
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     0.0f, 0.0f, (float)pw, (float)ph,
                     s * MENU_R, FINN_GLASS, FINN_GLASS_PANEL);

    /* 2. Left Header: "Apps" + "View all" pill with chevron-right icon */
    float head_x = s * LEFT_PAD;
    float head_y = s * (HEAD_Y + 11.0f * REF);
    finn_text_draw(&g_f_head, g_layer, pw, ph, head_x, head_y, "Apps", FINN_INK);

    float va_x = s * VIEWALL_X, va_y = s * VIEWALL_Y;
    float va_w = s * VIEWALL_W, va_h = s * VIEWALL_H;
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     va_x, va_y, va_w, va_h, va_h * 0.5f,
                     FINN_GLASS, FINN_GLASS_TILE);

    float va_cap = finn_text_cap_height(&g_f_sub);
    finn_text_draw(&g_f_sub, g_layer, pw, ph,
                   va_x + s * (10.0f * REF), va_y + va_h * 0.5f + va_cap * 0.45f,
                   "View all", FINN_INK);

    int chev_sz = rs(14.0f * REF, s);
    if (g_ic_chevron_right.vw > 0) {
        finn_icon_blit_ink(&g_ic_chevron_right, g_layer, pw, ph,
                           (int)lroundf(va_x + s * (64.0f * REF) + chev_sz * 0.5f),
                           (int)lroundf(va_y + va_h * 0.5f),
                           chev_sz, FINN_DIM, FINN_DIM, 0);
    }

    /* 3. Left App List — Settings is the single app */
    float row_y = s * APPLIST_Y;
    float row_step = s * (APP_ROW_H + APP_ROW_GAP);
    int icon_sz = rs(APP_ICON_SZ, s);

    struct { const char *name; fsvg *ic; } def_apps[] = {
        { "Settings", &g_ic_app_settings }
    };
    int n_display = 1;

    float hover_v = g_hover;
    float press_v = finn_anim_v(&g_a_press);

    for (int i = 0; i < n_display && i < FINN_APP_MAX; i++) {
        float ry = row_y + (float)i * row_step;
        g_r_app[i].x = rs(LEFT_PAD, s);
        g_r_app[i].y = (int)lroundf(ry);
        g_r_app[i].w = rs(VIEWALL_X + VIEWALL_W - LEFT_PAD, s);
        g_r_app[i].h = rs(APP_ROW_H, s);

        /* Hover and press wash */
        if (g_hover_idx == i && hover_v > 0.004f)
            control_wash_r(g_layer, pw, ph, g_r_app[i], s * (4.0f * REF),
                           FINN_MOTION_HOVER_WASH * hover_v);
        if (g_press_idx == i && press_v > 0.004f)
            control_wash_r(g_layer, pw, ph, g_r_app[i], s * (4.0f * REF),
                           FINN_MOTION_PRESS_WASH * press_v);

        fsvg *ic = def_apps[0].ic;
        const char *name = def_apps[0].name;

        if (i < n) {
            const finn_app *a = finn_apps_at(i);
            if (a) {
                name = a->name;
                fsvg *reg_ic = icon_for(i, a);
                if (reg_ic && reg_ic->vw > 0) ic = reg_ic;
            }
        }

        if (ic && ic->vw > 0) {
            finn_icon_blit(ic, g_layer, pw, ph,
                           rs(LEFT_PAD, s), (int)lroundf(ry),
                           icon_sz, 0, 0, 0);
        }

        float lcap = finn_text_cap_height(&g_f_label);
        finn_text_draw(&g_f_label, g_layer, pw, ph,
                       s * APP_TEXT_X, ry + (float)g_r_app[i].h * 0.5f + lcap * 0.45f,
                       name, FINN_INK);
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

    /* 5. Right White Panel (385x480 r=4.14, 85% white) */
    float r_x = s * RIGHT_X, r_y = s * RIGHT_Y;
    float r_w = s * RIGHT_W, r_h = s * RIGHT_H;
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     r_x, r_y, r_w, r_h, s * RIGHT_R,
                     FINN_GLASS, FINN_GLASS_TILE);
}

void finnmenu_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnmenu_visible() || !g_layer) return;
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

void finnmenu_hover_at(int px, int py) {
    int idx = -1;
    int what = finnmenu_hit(px, py, &idx);

    int next = -1;
    if (what == FINNMENU_APP) next = idx;
    else if (what == FINNMENU_RESTART)  next = FINN_APP_MAX;
    else if (what == FINNMENU_SHUTDOWN) next = FINN_APP_MAX + 1;

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
        if (what == FINNMENU_APP) g_press_idx = g_hover_idx;
        else if (what == FINNMENU_RESTART)  g_press_idx = FINN_APP_MAX;
        else if (what == FINNMENU_SHUTDOWN) g_press_idx = FINN_APP_MAX + 1;
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
    }
}
