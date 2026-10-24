/* finnsetup — the FinnOS first-boot setup surface. */
#define _GNU_SOURCE
#include "finnsetup.h"
#include "finndraw.h"
#include "finnblur.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnvg.h"
#include "finnsettings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

/*
 * Metrics & Design Language:
 *
 *   - Scaled from 1280x832 reference via REF (1.298077 for 1080p).
 *   - Centered frosted glass card (matching Start Menu: white 65%, blur, r=8).
 *   - Buttons and fields styled like Settings app (r=4.14, white 85% fills).
 *   - Wi-Fi badge blue accent #006EFF (Figma node 14:382: 0, 110, 255).
 *   - Clean wallpaper backdrop without dimming.
 */
#define REF                 1.298077f

#define SETUP_CARD_W        (480.0f * REF)
#define SETUP_CARD_H        (356.0f * REF)
#define SETUP_CARD_R        (8.0f * REF)

#define SETUP_PAD           (36.0f * REF)
#define SETUP_FIELD_R       (4.14f * REF)
#define SETUP_FIELD_H       (36.0f * REF)
#define SETUP_BTN_H         (40.0f * REF)

/* Exact blue accent from Figma node 14:382 (Wi-Fi badge child: r:0.0013, g:0.434, b:1.0) */
#define SETUP_BLUE          0xFF006EFFu

static const char *const g_langs[] = {
    "English (UK)",
    "English (US)",
    "Suomi",
    "Deutsch",
    "Español",
    "Français"
};

static const char *const g_lang_codes[] = {
    "en-GB", "en-US", "fi", "de", "es", "fr"
};
#define N_LANGS ((int)(sizeof g_langs / sizeof g_langs[0]))
static int g_lang_idx = 0;

/* Keyboard focus: language, appearance, start. */
static const int g_focus_acts[] = {
    FINNSETUP_LANGUAGE, FINNSETUP_ACCESSIBILITY, FINNSETUP_START
};
#define N_FOCUS ((int)(sizeof g_focus_acts / sizeof g_focus_acts[0]))
static int g_focus = 0;

/* Assets */
static fsvg g_ic_mark;
static fsvg g_ic_chev;
static int  g_ready = 0;

/* Fonts */
static finn_font g_f_title, g_f_label, g_f_detail, g_f_micro;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];

/* Layer & Geometry */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0;
static int g_lx0 = 0, g_ly0 = 0;
static int g_open = 0;
static int g_completed = 0;
static float g_scale = 1.0f;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

typedef struct { int x, y, w, h; } setup_rect;
static setup_rect g_r_lang, g_r_acc, g_r_start;

/* Motion */
static finn_anim g_a_open;
static finn_anim g_a_hover, g_a_press;
static finn_anim g_a_appearance;
static float     g_hover = 0.0f;
static int       g_hover_act = FINNSETUP_NONE;
static int       g_press_act = FINNSETUP_NONE;
static int       g_last_px = -1, g_last_py = -1;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_detail);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_title, p, font_px(s, 17.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, font_px(s, 13.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_detail, p, font_px(s, 11.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, font_px(s, 9.5f * REF)) != 0) return -1;
    return 0;
}

static int lang_index(const char *name) {
    for (int i = 0; i < N_LANGS; i++)
        if (!strcmp(name, g_langs[i]) || !strcmp(name, g_lang_codes[i])) return i;
    return -1;
}

static void read_marker_lang(void) {
    char code[32];
    if (finn_settings_get("lang", code, sizeof code) == 0) {
        int i = lang_index(code);
        if (i >= 0) { g_lang_idx = i; return; }
    }
    FILE *f = fopen(FINN_SETUP_DONE_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (strncmp(line, "lang=", 5)) continue;
        int i = lang_index(line + 5);
        if (i >= 0) g_lang_idx = i;
        break;
    }
    fclose(f);
}

static int write_marker(void) {
    const char *dir = "/var/lib/finn";
    mkdir(dir, 0755);

    char tmp[256];
    int len = snprintf(tmp, sizeof tmp, "%s/.setup-done.XXXXXX", dir);
    if (len < 0 || (size_t)len >= sizeof tmp) return -1;

    int fd = mkstemp(tmp);
    if (fd < 0) return -1;
    fchmod(fd, 0644);

    char line[128];
    len = snprintf(line, sizeof line, "lang=%s\n", g_lang_codes[g_lang_idx]);
    int ok = 1;
    for (int off = 0; ok && off < len; ) {
        ssize_t w = write(fd, line + off, (size_t)(len - off));
        if (w < 0) {
            if (errno == EINTR) continue;
            ok = 0;
        } else if (w == 0) {
            ok = 0;
        } else {
            off += (int)w;
        }
    }
    if (ok && fsync(fd) != 0) ok = 0;
    if (close(fd) != 0) ok = 0;

    if (!ok || rename(tmp, FINN_SETUP_DONE_PATH) != 0) {
        unlink(tmp);
        return -1;
    }

    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
    return 0;
}

static void focus_apply(void) {
    int act = g_focus_acts[g_focus];
    if (act != g_hover_act) {
        g_hover_act = act;
        finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
    }
    g_built = 0;
}

static void focus_from_act(int act) {
    for (int i = 0; i < N_FOCUS; i++)
        if (g_focus_acts[i] == act) { g_focus = i; return; }
}

int finnsetup_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");

    char p[600];
    snprintf(p, sizeof p, "%s/boot-mark.svg", icon_dir);
    int ok = (fsvg_load(p, &g_ic_mark) == 0);
    if (!ok) fprintf(stderr, "finnsetup: cannot load mark from %s\n", p);

    snprintf(p, sizeof p, "%s/chevron-right.svg", icon_dir);
    if (fsvg_load(p, &g_ic_chev) != 0) {
        fprintf(stderr, "finnsetup: cannot load chevron from %s\n", p);
        ok = 0;
    }

    memset(&g_a_appearance, 0, sizeof g_a_appearance);
    finn_state init_st;
    finn_state_read(&init_st);
    finn_anim_jump(&g_a_appearance, init_st.dark ? 1.0f : 0.0f);

    g_lang_idx = 0;
    g_focus = 0;

    const char *force = getenv("FINN_SETUP_FORCE");
    const char *skip  = getenv("FINN_SETUP_SKIP");
    int marked = access(FINN_SETUP_DONE_PATH, F_OK) == 0;

    if (force && !strcmp(force, "1")) {
        g_completed = 0;
        g_open = 1;
        finn_anim_jump(&g_a_open, 1.0f);
        if (marked) read_marker_lang();
    } else if (skip && !strcmp(skip, "1") && !marked) {
        g_completed = 0;
        g_open = 0;
        finn_anim_jump(&g_a_open, 0.0f);
    } else if (marked) {
        g_completed = 1;
        g_open = 0;
        finn_anim_jump(&g_a_open, 0.0f);
        read_marker_lang();
    } else {
        g_completed = 0;
        g_open = 1;
        finn_anim_jump(&g_a_open, 1.0f);
    }

    if (g_open) focus_apply();

    g_ready = ok;
    return ok ? 0 : -1;
}

void finnsetup_free(void) {
    fsvg_free(&g_ic_mark);
    fsvg_free(&g_ic_chev);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_detail);
    finn_font_free(&g_f_micro);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_built = 0;
    g_font_scale = -1.0f;
    g_last_px = g_last_py = -1;
    g_ready = 0;
}

void finnsetup_set_open(int open) {
    open = open ? 1 : 0;
    if (open == g_open) return;
    g_open = open;
    g_built = 0;
    if (open) {
        g_focus = 0;
        finn_state st;
        finn_state_read(&st);
        finn_anim_jump(&g_a_appearance, st.dark ? 1.0f : 0.0f);
        g_completed = 0;
        focus_apply();
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT);
    } else {
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
    }
}

int finnsetup_is_open(void) { return g_open; }
int finnsetup_is_completed(void) { return g_completed; }

int finnsetup_complete(void) {
    if (finn_settings_set("lang", g_lang_codes[g_lang_idx]) != 0) return -1;
    if (write_marker() != 0) return -1;

    g_completed = 1;
    finnsetup_set_open(0);
    return 0;
}

void finnsetup_cycle_language(void) {
    g_lang_idx = (g_lang_idx + 1) % N_LANGS;
    g_built = 0;
}

const char *finnsetup_language(void) {
    return g_langs[g_lang_idx];
}

const char *finnsetup_language_code(void) {
    return g_lang_codes[g_lang_idx];
}

void finnsetup_focus_next(int dir) {
    g_focus = (g_focus + (dir < 0 ? N_FOCUS - 1 : 1)) % N_FOCUS;
    focus_apply();
}

int finnsetup_focus_index(void) {
    return g_focus;
}

int finnsetup_activate(void) {
    if (!g_open) return FINNSETUP_NONE;

    int act = g_focus_acts[g_focus];
    if (act == FINNSETUP_LANGUAGE) {
        finnsetup_cycle_language();
        return FINNSETUP_LANGUAGE;
    }
    if (act == FINNSETUP_ACCESSIBILITY) {
        finn_state st;
        finn_state_read(&st);
        finn_state_set_dark(!st.dark);
        g_built = 0;
        return FINNSETUP_ACCESSIBILITY;
    }

    finnsetup_complete();
    return FINNSETUP_START;
}

int finnsetup_visible(void) {
    return finn_anim_v(&g_a_open) > 0.004f;
}

int finnsetup_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_hover, dt);
    ch |= finn_anim_tick(&g_a_press, dt);
    ch |= finn_anim_tick(&g_a_appearance, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finnsetup_running(void) {
    return finn_anim_running(&g_a_open) ||
           finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_press) ||
           finn_anim_running(&g_a_appearance);
}

static int in_rect(const setup_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int finnsetup_hit(int px, int py) {
    if (!g_open || !finnsetup_visible()) return FINNSETUP_NONE;
    int x = px - g_lx0, y = py - g_ly0;
    if (x < 0 || y < 0 || x >= g_lw || y >= g_lh) return FINNSETUP_NONE;

    if (in_rect(&g_r_start, x, y)) return FINNSETUP_START;
    if (in_rect(&g_r_acc, x, y))   return FINNSETUP_ACCESSIBILITY;
    if (in_rect(&g_r_lang, x, y))  return FINNSETUP_LANGUAGE;
    return FINNSETUP_PANEL;
}

int finnsetup_hit_appearance_option(int px, int py) {
    if (!g_open || !finnsetup_visible()) return -1;
    int x = px - g_lx0, y = py - g_ly0;
    if (!in_rect(&g_r_acc, x, y)) return -1;
    return (x - g_r_acc.x >= g_r_acc.w / 2) ? 1 : 0;
}

void finnsetup_hover_at(int px, int py) {
    if (px == g_last_px && py == g_last_py) return;
    g_last_px = px;
    g_last_py = py;

    int act = finnsetup_hit(px, py);
    if (act == FINNSETUP_PANEL) act = FINNSETUP_NONE;
    if (act != FINNSETUP_NONE) focus_from_act(act);
    if (act == g_hover_act) return;
    g_hover_act = act;
    g_built = 0;
    if (act != FINNSETUP_NONE)
        finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_hover, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

void finnsetup_press(int act) {
    g_press_act = act;
    g_built = 0;
    if (act == FINNSETUP_NONE)
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
}

/* ---------------- render ---------------- */

static void blit_chevron_down(uint32_t *dst, int dw, int dh, int cx, int cy, int sz, uint32_t tint) {
    if (g_ic_chev.vw <= 0.0f) return;
    int ipx, iox, ioy;
    finn_icon_frame(&g_ic_chev, sz, cx, cy, &ipx, &iox, &ioy);
    const uint32_t *src = finn_icon_ref(&g_ic_chev, ipx, tint, tint, 0);
    if (!src || ipx <= 0) return;

    /* Rotate 90 degrees clockwise: (x, y) -> (ipx - 1 - y, x) */
    uint32_t stack_buf[128 * 128];
    uint32_t *rot = (ipx <= 128) ? stack_buf : malloc((size_t)ipx * ipx * 4);
    if (!rot) return;
    for (int y = 0; y < ipx; y++) {
        for (int x = 0; x < ipx; x++) {
            rot[x * ipx + (ipx - 1 - y)] = src[y * ipx + x];
        }
    }
    finn_blit(dst, dw, dh, rot, ipx, ipx, iox, ioy);
    if (rot != stack_buf) free(rot);
}

static uint32_t lerp_color(uint32_t c0, uint32_t c1, float t) {
    if (t <= 0.0f) return c0;
    if (t >= 1.0f) return c1;
    unsigned a0 = (c0 >> 24) & 0xFF, r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
    unsigned a1 = (c1 >> 24) & 0xFF, r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    unsigned a = (unsigned)((float)a0 + t * (float)((int)a1 - (int)a0) + 0.5f);
    unsigned r = (unsigned)((float)r0 + t * (float)((int)r1 - (int)r0) + 0.5f);
    unsigned g = (unsigned)((float)g0 + t * (float)((int)g1 - (int)g0) + 0.5f);
    unsigned b = (unsigned)((float)b0 + t * (float)((int)b1 - (int)b0) + 0.5f);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t build_key(int sw, int sh, float s, const finn_state *st) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX(sw); MIX(sh); MIX((int)(s * 1000.0f));
    MIX(st->dark + 2);
    MIX((int)(finn_anim_v(&g_a_appearance) * 1024.0f));
    MIX(g_lang_idx);
    MIX(g_focus);
    MIX(g_hover_act);
    MIX((int)(g_hover * 64.0f));
    MIX(g_press_act);
    MIX((int)(finn_anim_v(&g_a_press) * 256.0f));
    MIX(finnblur_generation());
    #undef MIX
    return h ? h : 1u;
}

void finnsetup_render(int screen_w, int screen_h, float scale,
                      const finn_state *st) {
    if (!g_ready || (!g_open && !finnsetup_visible())) return;

    g_scale = scale;
    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }

    float s = scale;
    int pw = rs(SETUP_CARD_W, s);
    int ph = rs(SETUP_CARD_H, s);

    /* Center the card on screen */
    g_lx0 = (screen_w - pw) / 2;
    g_ly0 = (screen_h - ph) / 2;

    uint32_t key = build_key(screen_w, screen_h, scale, st);
    if (g_built && key == g_build_key && pw == g_lw && ph == g_lh)
        return;
    g_build_key = key;
    g_built = 1;
    g_rev++;

    if (pw != g_lw || ph != g_lh) {
        free(g_layer);
        g_layer = malloc((size_t)pw * ph * 4);
        g_lw = pw;
        g_lh = ph;
    }
    if (!g_layer) return;
    memset(g_layer, 0, (size_t)pw * ph * 4);

    /* 1. Main centered frosted glass card (r=8, 65% white over blurred wallpaper) */
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     0.0f, 0.0f, (float)pw, (float)ph,
                     s * SETUP_CARD_R, FINN_GLASS, FINN_GLASS_PANEL);

    int pad = rs(SETUP_PAD, s);
    int content_w = pw - pad * 2;
    float field_r = s * SETUP_FIELD_R;
    float press_v = finn_anim_v(&g_a_press);

    /*
     * 2. Branding & Title (top)
     * Mark: 32px ink size, centered horizontally.
     */
    int mark_sz = rs(32.0f * REF, s);
    int mark_cx = pw / 2;
    int mark_cy = rs(36.0f * REF, s);
    finn_icon_blit_ink(&g_ic_mark, g_layer, pw, ph,
                       mark_cx, mark_cy, mark_sz,
                       FINN_INK, FINN_INK, 0);

    /* Title: "Welcome to FinnOS" */
    const char *title_str = "Welcome to FinnOS";
    float title_tw = finn_text_width(&g_f_title, title_str);
    float title_x = ((float)pw - title_tw) * 0.5f;
    float title_y = (float)rs(80.0f * REF, s);
    finn_text_draw(&g_f_title, g_layer, pw, ph, title_x, title_y, title_str, FINN_INK);

    /* Subtitle: "Set up your device to get started." */
    const char *sub_str = "Set up your device to get started.";
    float sub_tw = finn_text_width(&g_f_detail, sub_str);
    float sub_x = ((float)pw - sub_tw) * 0.5f;
    float sub_y = (float)rs(100.0f * REF, s);
    finn_text_draw(&g_f_detail, g_layer, pw, ph, sub_x, sub_y, sub_str, FINN_DIM);

    /*
     * 3. Language Selector Section
     * Section micro-label: "LANGUAGE"
     */
    float lang_lbl_y = (float)rs(126.0f * REF, s);
    finn_text_draw(&g_f_micro, g_layer, pw, ph, (float)pad, lang_lbl_y, "LANGUAGE", FINN_DIM);

    /* Language field: r=4.14, 85% white fill */
    g_r_lang.x = pad;
    g_r_lang.y = rs(138.0f * REF, s);
    g_r_lang.w = content_w;
    g_r_lang.h = rs(SETUP_FIELD_H, s);

    /* Inset frosted tile pass at 0.85 */
    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     (float)g_r_lang.x, (float)g_r_lang.y,
                     (float)g_r_lang.w, (float)g_r_lang.h,
                     field_r, FINN_GLASS, FINN_GLASS_TILE);

    /* Hover / Press wash & Keyboard focus ring */
    int is_lang_focus = (g_focus_acts[g_focus] == FINNSETUP_LANGUAGE);
    if (g_hover_act == FINNSETUP_LANGUAGE || g_press_act == FINNSETUP_LANGUAGE || is_lang_focus) {
        float hw = (g_hover_act == FINNSETUP_LANGUAGE ? FINN_MOTION_HOVER_WASH * g_hover : 0.0f) +
                   (g_press_act == FINNSETUP_LANGUAGE ? FINN_MOTION_PRESS_WASH * press_v : 0.0f);
        if (is_lang_focus && hw < FINN_MOTION_HOVER_WASH) hw = FINN_MOTION_HOVER_WASH;
        unsigned al = (unsigned)(hw * 255.0f + 0.5f);
        if (al > 255) al = 255;
        finn_fill_rrect(g_layer, pw, ph,
                        (float)g_r_lang.x, (float)g_r_lang.y,
                        (float)g_r_lang.w, (float)g_r_lang.h,
                        field_r, (FINN_INK & 0x00FFFFFFu) | (al << 24));
    }

    int lang_cy = g_r_lang.y + g_r_lang.h / 2;
    float lcap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, g_layer, pw, ph,
                   (float)(g_r_lang.x + rs(14.0f * REF, s)),
                   (float)lang_cy + lcap * 0.45f,
                   g_langs[g_lang_idx], FINN_INK);

    /* Downward chevron rotated from chevron-right.svg */
    int chev_sz = rs(14.0f * REF, s);
    int ch_cx = g_r_lang.x + g_r_lang.w - rs(18.0f * REF, s);
    blit_chevron_down(g_layer, pw, ph, ch_cx, lang_cy, chev_sz, FINN_DIM);

    /* Honesty caption */
    float cap_y = (float)rs(186.0f * REF, s);
    finn_text_draw(&g_f_micro, g_layer, pw, ph,
                   (float)pad, cap_y,
                   "Display name only. The shell remains in English.",
                   FINN_MUTE);

    /*
     * 4. Appearance Selection Section
     * Section micro-label: "APPEARANCE"
     */
    float acc_lbl_y = (float)rs(208.0f * REF, s);
    finn_text_draw(&g_f_micro, g_layer, pw, ph, (float)pad, acc_lbl_y, "APPEARANCE", FINN_DIM);

    /* Appearance toggle pill field */
    g_r_acc.x = pad;
    g_r_acc.y = rs(220.0f * REF, s);
    g_r_acc.w = content_w;
    g_r_acc.h = rs(SETUP_FIELD_H, s);

    finn_frost_rrect(g_layer, pw, ph, g_lx0, g_ly0,
                     (float)g_r_acc.x, (float)g_r_acc.y,
                     (float)g_r_acc.w, (float)g_r_acc.h,
                     field_r, FINN_GLASS, FINN_GLASS_TILE);

    int is_acc_focus = (g_focus_acts[g_focus] == FINNSETUP_ACCESSIBILITY);
    if (g_hover_act == FINNSETUP_ACCESSIBILITY || g_press_act == FINNSETUP_ACCESSIBILITY || is_acc_focus) {
        float hw = (g_hover_act == FINNSETUP_ACCESSIBILITY ? FINN_MOTION_HOVER_WASH * g_hover : 0.0f) +
                   (g_press_act == FINNSETUP_ACCESSIBILITY ? FINN_MOTION_PRESS_WASH * press_v : 0.0f);
        if (is_acc_focus && hw < FINN_MOTION_HOVER_WASH) hw = FINN_MOTION_HOVER_WASH;
        unsigned al = (unsigned)(hw * 255.0f + 0.5f);
        if (al > 255) al = 255;
        finn_fill_rrect(g_layer, pw, ph,
                        (float)g_r_acc.x, (float)g_r_acc.y,
                        (float)g_r_acc.w, (float)g_r_acc.h,
                        field_r, (FINN_INK & 0x00FFFFFFu) | (al << 24));
    }

    /* Drive the sliding appearance animation track */
    float target_t = st->dark ? 1.0f : 0.0f;
    if (g_a_appearance.to != target_t) {
        finn_anim_to(&g_a_appearance, target_t, FINN_MOTION_THEME, 0.0f, FINN_EASE_IN_OUT);
    }
    float slide_t = finn_anim_v(&g_a_appearance);

    /* Symmetric container inset (equal padding on all 4 sides and between slots) */
    int seg_inset = rs(3.0f * REF, s);
    float pill_w = ((float)g_r_acc.w - (float)(seg_inset * 3)) * 0.5f;
    float pill_h = (float)g_r_acc.h - (float)(seg_inset * 2);
    float pill_r = field_r > 2.0f ? field_r - 1.0f : field_r;
    float slide_dist = pill_w + (float)seg_inset;

    /* Animated sliding active pill with symmetric insets */
    float pill_x = (float)(g_r_acc.x + seg_inset) + slide_t * slide_dist;
    float pill_y = (float)(g_r_acc.y + seg_inset);
    finn_fill_rrect(g_layer, pw, ph, pill_x, pill_y, pill_w, pill_h, pill_r, SETUP_BLUE);

    /* Segment option labels with smooth color transition */
    float cx_light = (float)(g_r_acc.x + seg_inset) + pill_w * 0.5f;
    float cx_dark  = (float)(g_r_acc.x + seg_inset + seg_inset) + pill_w * 1.5f;
    float opt_y    = (float)(g_r_acc.y + seg_inset) + pill_h * 0.5f + lcap * 0.45f;

    float l_tw = finn_text_width(&g_f_label, "Light");
    float d_tw = finn_text_width(&g_f_label, "Dark");

    uint32_t col_light = lerp_color(0xFFFFFFFFu, FINN_DIM, slide_t);
    uint32_t col_dark  = lerp_color(FINN_DIM, 0xFFFFFFFFu, slide_t);

    finn_text_draw(&g_f_label, g_layer, pw, ph,
                   cx_light - l_tw * 0.5f, opt_y, "Light", col_light);
    finn_text_draw(&g_f_label, g_layer, pw, ph,
                   cx_dark - d_tw * 0.5f, opt_y, "Dark", col_dark);

    /*
     * 5. "Get started" Primary Action Button
     * Solid #006EFF blue accent with r=4.14
     */
    g_r_start.x = pad;
    g_r_start.y = rs(282.0f * REF, s);
    g_r_start.w = content_w;
    g_r_start.h = rs(SETUP_BTN_H, s);

    finn_fill_rrect(g_layer, pw, ph,
                    (float)g_r_start.x, (float)g_r_start.y,
                    (float)g_r_start.w, (float)g_r_start.h,
                    field_r, SETUP_BLUE);

    int is_start_focus = (g_focus_acts[g_focus] == FINNSETUP_START);
    if (g_hover_act == FINNSETUP_START || g_press_act == FINNSETUP_START || is_start_focus) {
        float hw = (g_hover_act == FINNSETUP_START ? FINN_MOTION_HOVER_WASH * g_hover : 0.0f) +
                   (g_press_act == FINNSETUP_START ? FINN_MOTION_PRESS_WASH * press_v : 0.0f);
        if (is_start_focus && hw < FINN_MOTION_HOVER_WASH) hw = FINN_MOTION_HOVER_WASH;
        unsigned al = (unsigned)(hw * 255.0f + 0.5f);
        if (al > 255) al = 255;
        finn_fill_rrect(g_layer, pw, ph,
                        (float)g_r_start.x, (float)g_r_start.y,
                        (float)g_r_start.w, (float)g_r_start.h,
                        field_r, (0xFFFFFFFFu & 0x00FFFFFFu) | (al << 24));
    }

    const char *start_str = "Get started";
    float st_tw = finn_text_width(&g_f_label, start_str);
    finn_text_draw(&g_f_label, g_layer, pw, ph,
                   (float)g_r_start.x + ((float)g_r_start.w - st_tw) * 0.5f,
                   (float)g_r_start.y + (float)g_r_start.h * 0.5f + lcap * 0.45f,
                   start_str, 0xFFFFFFFFu);
}

void finnsetup_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnsetup_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finnsetup_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    float a = v * FINN_MOTION_FADE;
    if (a > 1.0f) a = 1.0f;
    float k = FINN_MOTION_OPEN_SCALE + (1.0f - FINN_MOTION_OPEN_SCALE) * v;

    float cx = (float)g_lx0 + (float)g_lw * 0.5f;
    float cy = (float)g_ly0 + (float)g_lh * 0.5f;

    if (x) *x = cx - (float)g_lw * k * 0.5f;
    if (y) *y = cy - (float)g_lh * k * 0.5f;
    if (w) *w = (float)g_lw * k;
    if (h) *h = (float)g_lh * k;
    if (alpha) *alpha = a;
}

void finnsetup_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_layer) return;
    float x, y, w, h, alpha;
    finnsetup_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_scaled(dst, dw, dh, g_layer, g_lw, g_lh, x, y, w, h, alpha);
}

const uint32_t *finnsetup_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx0;
    if (y) *y = g_ly0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_rev;
    return finnsetup_visible() ? g_layer : NULL;
}
