/* finnsetup — the FinnOS first-boot & device setup surface (Figma 2:14 & 7:10). */
#define _GNU_SOURCE
#include "finnsetup.h"
#include "finndraw.h"
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
#include <webp/decode.h>

/*
 * Figma 2:14 (Setup - Light) & 7:10 (Setup - Dark) tokens:
 *   Light base: #FFFFFF + LAYER_BLUR(500) hero glow, pill: #636363@0.10, Start: #000000
 *   Dark  base: #000000 + LAYER_BLUR(500) hero glow, pill: #B6B6B6@0.10, Start: #FFFFFF
 *
 * Reference frame (1280x832, * 1.298077 for 1080p):
 *   Hero card: x=30, y=30, w=580, h=772, r=30
 *   Right column at x=670:
 *     Branding: y=288 (mark 30x30 + "FinnOS" 18px at x+50)
 *     Welcome heading: y=338 ("Welcome!\nLet's start configuring your device." 18px)
 *     Language pill: y=418, w=149, h=32, r=50
 *     Accessibility circle: x=670, y=480, w=32, h=32, r=50
 *     Start pill: x=717, y=480, w=80, h=32, r=50
 */
#define SETUP_PILL_LIGHT  0x1A636363u
#define SETUP_PILL_DARK   0x1AB6B6B6u

#define K_REF             1.298077f  /* 1080 / 832 */

static const char *const g_langs[] = {
    "English (UK)",
    "English (US)",
    "Suomi",
    "Deutsch",
    "Español",
    "Français"
};
/* The stable identifier for a language, for the store and the marker. The
 * display names above may be translated later; these may not change. */
static const char *const g_lang_codes[] = {
    "en-GB", "en-US", "fi", "de", "es", "fr"
};
#define N_LANGS ((int)(sizeof g_langs / sizeof g_langs[0]))
static int g_lang_idx = 0;

/* Keyboard focus: language, accessibility, start. */
static const int g_focus_acts[] = {
    FINNSETUP_LANGUAGE, FINNSETUP_ACCESSIBILITY, FINNSETUP_START
};
#define N_FOCUS ((int)(sizeof g_focus_acts / sizeof g_focus_acts[0]))
static int g_focus = 0;

static fsvg g_ic_start, g_ic_lang, g_ic_acc, g_ic_chev_down, g_ic_chev_right;
static int  g_ready = 0;

static finn_font g_f_brand, g_f_head, g_f_btn;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];

/* Decoded raw hero image (setup.webp) */
static uint32_t *g_hero_src = NULL;
static int       g_hero_sw = 0, g_hero_sh = 0;
/* Scaled & rounded hero card cache */
static uint32_t *g_hero_card = NULL;
static int       g_hero_cw = 0, g_hero_ch = 0, g_hero_cr = 0;

static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0;
static int g_open = 0;
/* Not completed until a marker says so: before finnsetup_init has looked,
 * "done" would be a claim nobody has made. */
static int g_completed = 0;
static float g_scale = 1.0f;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_rev = 0;

typedef struct { int x, y, w, h; } setup_rect;
static setup_rect g_r_lang, g_r_acc, g_r_start;

static finn_anim g_a_open;
static finn_anim g_a_hover, g_a_press;
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
    finn_font_free(&g_f_brand);
    finn_font_free(&g_f_head);
    finn_font_free(&g_f_btn);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_brand, p, font_px(s, 18.0f * K_REF)) != 0) {
        fprintf(stderr, "finnsetup: cannot load %s\n", p);
        return -1;
    }
    if (finn_font_load(&g_f_head, p, font_px(s, 18.0f * K_REF)) != 0) return -1;
    if (finn_font_load(&g_f_btn, p, font_px(s, 13.0f * K_REF)) != 0) return -1;
    return 0;
}

static void load_hero_webp(void) {
    const char *paths[] = {
        "/usr/share/finn/wallpaper/setup.webp",
        "assets/wallpaper/setup.webp",
        "/usr/share/finn/wallpaper/default.webp",
        "assets/wallpaper/default.webp",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 32 * 1024 * 1024) { fclose(f); continue; }
        uint8_t *buf = malloc((size_t)sz);
        if (!buf) { fclose(f); continue; }
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf); fclose(f); continue;
        }
        fclose(f);

        int w = 0, h = 0;
        uint8_t *bgra = WebPDecodeBGRA(buf, (size_t)sz, &w, &h);
        free(buf);
        if (bgra && w > 0 && h > 0) {
            g_hero_src = malloc((size_t)w * h * 4);
            if (g_hero_src) {
                memcpy(g_hero_src, bgra, (size_t)w * h * 4);
                g_hero_sw = w;
                g_hero_sh = h;
            }
            WebPFree(bgra);
            return;
        }
    }
}

static void ensure_hero_card(int cw, int ch, int cr) {
    if (g_hero_card && g_hero_cw == cw && g_hero_ch == ch && g_hero_cr == cr)
        return;
    free(g_hero_card);
    g_hero_card = malloc((size_t)cw * ch * 4);
    g_hero_cw = cw;
    g_hero_ch = ch;
    g_hero_cr = cr;
    if (!g_hero_card) return;

    float rad = (float)cr;
    float hw = (float)cw * 0.5f, hh = (float)ch * 0.5f;
    float bx = hw - rad, by = hh - rad;

    /* Cover-crop sample from g_hero_src */
    float scale_x = g_hero_src ? (float)g_hero_sw / (float)cw : 1.0f;
    float scale_y = g_hero_src ? (float)g_hero_sh / (float)ch : 1.0f;
    float sc = scale_x < scale_y ? scale_x : scale_y;
    float off_x = g_hero_src ? ((float)g_hero_sw - (float)cw * sc) * 0.5f : 0.0f;
    float off_y = g_hero_src ? ((float)g_hero_sh - (float)ch * sc) * 0.5f : 0.0f;

    for (int y = 0; y < ch; y++) {
        float py = (float)y + 0.5f - hh;
        float dy = fabsf(py) - by;
        for (int x = 0; x < cw; x++) {
            float px = (float)x + 0.5f - hw;
            float dx = fabsf(px) - bx;

            float cov = 1.0f;
            if (dx > -1.0f || dy > -1.0f) {
                float ox = dx > 0.0f ? dx : 0.0f;
                float oy = dy > 0.0f ? dy : 0.0f;
                float outside = sqrtf(ox * ox + oy * oy);
                float inside  = fminf(fmaxf(dx, dy), 0.0f);
                float d = outside + inside - rad;
                cov = 0.5f - d;
                if (cov <= 0.0f) {
                    g_hero_card[y * cw + x] = 0;
                    continue;
                }
                if (cov > 1.0f) cov = 1.0f;
            }

            uint32_t col = 0xFF2B4C7Eu;
            if (g_hero_src) {
                int sx = (int)(off_x + (float)x * sc);
                int sy = (int)(off_y + (float)y * sc);
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= g_hero_sw) sx = g_hero_sw - 1;
                if (sy >= g_hero_sh) sy = g_hero_sh - 1;
                col = g_hero_src[sy * g_hero_sw + sx] | 0xFF000000u;
            }
            unsigned a = (unsigned)(cov * 255.0f + 0.5f);
            g_hero_card[y * cw + x] = (col & 0x00FFFFFFu) | (a << 24);
        }
    }
}

/* A marker from an older build holds the display name; one from this build
 * holds the stable code. Both name the same choice, so accept either. */
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

/*
 * Writes the completion marker the way the settings store writes: a temporary
 * file beside it, flushed and renamed. The marker is the thing a later boot
 * trusts, so it either exists complete or not at all.
 */
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

    /* The rename is already done; this only makes it survive a power cut. */
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
    return 0;
}

/* Keyboard focus and pointer hover are one state: whichever is most recent
 * decides, and both drive the same wash. */
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
    int ok = 1;
    snprintf(p, sizeof p, "%s/start_button.svg", icon_dir);  ok &= fsvg_load(p, &g_ic_start) == 0;
    snprintf(p, sizeof p, "%s/language.svg", icon_dir);      ok &= fsvg_load(p, &g_ic_lang) == 0;
    snprintf(p, sizeof p, "%s/accessibility.svg", icon_dir); ok &= fsvg_load(p, &g_ic_acc) == 0;
    snprintf(p, sizeof p, "%s/chevron_down.svg", icon_dir);  ok &= fsvg_load(p, &g_ic_chev_down) == 0;
    snprintf(p, sizeof p, "%s/chevron_right.svg", icon_dir); ok &= fsvg_load(p, &g_ic_chev_right) == 0;

    if (!ok) fprintf(stderr, "finnsetup: icon set incomplete in %s\n", icon_dir);
    load_hero_webp();

    /* Init starts from the default choice and the first control; only the
     * marker or the user moves either. */
    g_lang_idx = 0;
    g_focus = 0;

    /*
     * The marker is the only proof that setup ran. Missing means open, which
     * is what a fresh install is. Both escapes are opt-in and exact, because
     * a mistyped or inherited variable must not decide whether a machine is
     * ever configured.
     */
    const char *force = getenv("FINN_SETUP_FORCE");
    const char *skip  = getenv("FINN_SETUP_SKIP");
    int marked = access(FINN_SETUP_DONE_PATH, F_OK) == 0;

    if (force && !strcmp(force, "1")) {
        g_completed = 0;
        g_open = 1;
        finn_anim_jump(&g_a_open, 1.0f);
        if (marked) read_marker_lang();
    } else if (skip && !strcmp(skip, "1") && !marked) {
        /* Development escape: the screen stays shut and the marker stays
         * absent, so setup.get still says this machine was never completed. */
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

    /* Keyboard users see the initial control focused immediately on launch. */
    if (g_open) focus_apply();

    g_ready = ok;
    return ok ? 0 : -1;
}

void finnsetup_free(void) {
    fsvg_free(&g_ic_start);
    fsvg_free(&g_ic_lang);
    fsvg_free(&g_ic_acc);
    fsvg_free(&g_ic_chev_down);
    fsvg_free(&g_ic_chev_right);
    finn_font_free(&g_f_brand);
    finn_font_free(&g_f_head);
    finn_font_free(&g_f_btn);
    free(g_hero_src);  g_hero_src = NULL;
    free(g_hero_card); g_hero_card = NULL;
    free(g_layer);     g_layer = NULL;
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
        g_completed = 0;
        focus_apply();
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT);
    } else {
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
    }
}

int finnsetup_is_open(void) { return g_open; }
int finnsetup_is_completed(void) { return g_completed; }

/*
 * Setup counts as complete only once the machine can prove it after a reboot:
 * the language in the store, then the marker. The marker is written last
 * because it is the completion gate — a settings write that failed must not
 * leave a marker that says setup finished.
 */
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

    /* START reports even when the write failed: the compositor replies ERR
     * with what setup.get says rather than being told a smaller story here. */
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
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finnsetup_running(void) {
    return finn_anim_running(&g_a_open) ||
           finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_press);
}

static int in_rect(const setup_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int finnsetup_hit(int px, int py) {
    if (!g_open || !finnsetup_visible()) return FINNSETUP_NONE;
    if (in_rect(&g_r_start, px, py)) return FINNSETUP_START;
    if (in_rect(&g_r_acc, px, py))   return FINNSETUP_ACCESSIBILITY;
    if (in_rect(&g_r_lang, px, py))  return FINNSETUP_LANGUAGE;
    return FINNSETUP_PANEL;
}

void finnsetup_hover_at(int px, int py) {
    /* If the pointer has not moved, a stationary cursor must not clobber
     * keyboard focus wash when the frame loop polls hover every tick. */
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

static uint32_t build_key(int sw, int sh, float s, const finn_state *st) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX(sw); MIX(sh); MIX((int)(s * 1000.0f));
    MIX(st->dark + 2);
    MIX(g_lang_idx);
    MIX(g_hover_act);
    MIX((int)(g_hover * 64.0f));
    MIX(g_press_act);
    MIX((int)(finn_anim_v(&g_a_press) * 256.0f));
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

    uint32_t key = build_key(screen_w, screen_h, scale, st);
    if (g_built && key == g_build_key && screen_w == g_lw && screen_h == g_lh)
        return;
    g_build_key = key;
    g_built = 1;
    g_rev++;

    if (screen_w != g_lw || screen_h != g_lh) {
        free(g_layer);
        g_layer = malloc((size_t)screen_w * screen_h * 4);
        g_lw = screen_w;
        g_lh = screen_h;
    }
    if (!g_layer) return;

    float s = scale * K_REF; /* 1.0 in 1280x832 reference units -> screen pixels */
    uint32_t pill_col      = st->dark ? SETUP_PILL_DARK : SETUP_PILL_LIGHT;
    uint32_t ink_col       = st->dark ? 0xFFFFFFFFu     : 0xFF000000u;
    uint32_t dim_col       = (ink_col & 0x00FFFFFFu) | 0x80000000u;
    uint32_t start_bg      = st->dark ? 0xFFFFFFFFu     : 0xFF000000u;
    uint32_t start_fg      = st->dark ? 0xFF000000u     : 0xFFFFFFFFu;

    /* Left hero card geometry (Figma 2:14 & 7:10: x=30, y=30, w=580, h=772, r=30 on 1280x832) */
    int hx = rs(30.0f, s);
    int hy = rs(30.0f, s);
    int hh = screen_h - hy * 2;
    int hw = (screen_w * 580) / 1280;
    int hr = rs(30.0f, s);

    /*
     * Background with Figma LAYER_BLUR(500) hero glow (nodes 6:3 & 7:12):
     * blends #3B688B at up to 34% alpha around the hero card over #FFFFFF (Light)
     * or #000000 (Dark).
     */
    float gcx = (float)hx + (float)hw * 0.5f;
    float gcy = (float)hy + (float)hh * 0.5f;
    float inv_rx = 1.0f / ((float)screen_w * 0.52f);
    float inv_ry = 1.0f / ((float)screen_h * 0.85f);
    unsigned base_rgb = st->dark ? 0u : 255u;
    for (int y = 0; y < screen_h; y++) {
        float dy = ((float)y - gcy) * inv_ry;
        float dy2 = dy * dy;
        uint32_t *row = g_layer + (size_t)y * screen_w;
        for (int x = 0; x < screen_w; x++) {
            float dx = ((float)x - gcx) * inv_rx;
            float d2 = dx * dx + dy2;
            float w_glow = 0.0f;
            if (d2 < 1.0f) {
                float t = 1.0f - d2;
                w_glow = 0.34f * t * t;
            }
            unsigned r = (unsigned)((1.0f - w_glow) * (float)base_rgb + w_glow * 59.0f + 0.5f);
            unsigned g = (unsigned)((1.0f - w_glow) * (float)base_rgb + w_glow * 104.0f + 0.5f);
            unsigned b = (unsigned)((1.0f - w_glow) * (float)base_rgb + w_glow * 139.0f + 0.5f);
            row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    if (hw > 0 && hh > 0) {
        ensure_hero_card(hw, hh, hr);
        if (g_hero_card)
            finn_blit(g_layer, screen_w, screen_h, g_hero_card, hw, hh, hx, hy);
    }

    /* Right column origin (x=670 on 1280x832) */
    int rx = (screen_w * 670) / 1280;

    /* 1. Branding at y=288 (30x30 mark + "FinnOS" 18px at x+50) */
    int brand_y = rs(288.0f, s);
    int mark_sz = rs(30.0f, s);
    finn_icon_blit_ink(&g_ic_start, g_layer, screen_w, screen_h,
                       rx + mark_sz / 2, brand_y + mark_sz / 2,
                       mark_sz, ink_col, ink_col, 0);
    float brand_cap = finn_text_cap_height(&g_f_brand);
    finn_text_draw(&g_f_brand, g_layer, screen_w, screen_h,
                   (float)(rx + rs(50.0f, s)),
                   (float)brand_y + (float)mark_sz * 0.5f + brand_cap * 0.5f,
                   "FinnOS", ink_col);

    /* 2. Heading at y=338 ("Welcome!\nLet's start configuring your device.") */
    float head_y1 = (float)rs(357.0f, s);
    float head_y2 = (float)rs(382.0f, s);
    finn_text_draw(&g_f_head, g_layer, screen_w, screen_h,
                   (float)rx, head_y1, "Welcome!", ink_col);
    finn_text_draw(&g_f_head, g_layer, screen_w, screen_h,
                   (float)rx, head_y2, "Let’s start configuring your device.", dim_col);

    float press_v = finn_anim_v(&g_a_press);

    /* 3. Language selector pill at y=418 (149x32 r=50) */
    g_r_lang.x = rx;
    g_r_lang.y = rs(418.0f, s);
    g_r_lang.w = rs(149.0f, s);
    g_r_lang.h = rs(32.0f, s);
    float lrad = (float)g_r_lang.h * 0.5f;

    finn_fill_rrect(g_layer, screen_w, screen_h,
                    (float)g_r_lang.x, (float)g_r_lang.y,
                    (float)g_r_lang.w, (float)g_r_lang.h,
                    lrad, pill_col);

    if (g_hover_act == FINNSETUP_LANGUAGE || g_press_act == FINNSETUP_LANGUAGE) {
        float w = (g_hover_act == FINNSETUP_LANGUAGE ? FINN_MOTION_HOVER_WASH * g_hover : 0.0f) +
                  (g_press_act == FINNSETUP_LANGUAGE ? FINN_MOTION_PRESS_WASH * press_v : 0.0f);
        unsigned al = (unsigned)(w * 255.0f + 0.5f);
        if (al > 255) al = 255;
        finn_fill_rrect(g_layer, screen_w, screen_h,
                        (float)g_r_lang.x, (float)g_r_lang.y,
                        (float)g_r_lang.w, (float)g_r_lang.h,
                        lrad, (ink_col & 0x00FFFFFFu) | (al << 24));
    }

    int lang_cy = g_r_lang.y + g_r_lang.h / 2;
    finn_icon_blit_ink(&g_ic_lang, g_layer, screen_w, screen_h,
                       g_r_lang.x + rs(20.0f, s), lang_cy,
                       rs(16.0f, s), ink_col, ink_col, 0);

    float btn_cap = finn_text_cap_height(&g_f_btn);
    finn_text_draw(&g_f_btn, g_layer, screen_w, screen_h,
                   (float)(g_r_lang.x + rs(36.0f, s)),
                   (float)lang_cy + btn_cap * 0.5f,
                   g_langs[g_lang_idx], ink_col);

    finn_icon_blit_ink(&g_ic_chev_down, g_layer, screen_w, screen_h,
                       g_r_lang.x + g_r_lang.w - rs(18.0f, s), lang_cy,
                       rs(11.0f, s), ink_col, ink_col, 0);

    /* 4. Accessibility circle button at x=670, y=480 (32x32 r=50) */
    g_r_acc.x = rx;
    g_r_acc.y = rs(480.0f, s);
    g_r_acc.w = rs(32.0f, s);
    g_r_acc.h = rs(32.0f, s);
    float arad = (float)g_r_acc.h * 0.5f;

    finn_fill_rrect(g_layer, screen_w, screen_h,
                    (float)g_r_acc.x, (float)g_r_acc.y,
                    (float)g_r_acc.w, (float)g_r_acc.h,
                    arad, pill_col);

    if (g_hover_act == FINNSETUP_ACCESSIBILITY || g_press_act == FINNSETUP_ACCESSIBILITY) {
        float w = (g_hover_act == FINNSETUP_ACCESSIBILITY ? FINN_MOTION_HOVER_WASH * g_hover : 0.0f) +
                  (g_press_act == FINNSETUP_ACCESSIBILITY ? FINN_MOTION_PRESS_WASH * press_v : 0.0f);
        unsigned al = (unsigned)(w * 255.0f + 0.5f);
        if (al > 255) al = 255;
        finn_fill_rrect(g_layer, screen_w, screen_h,
                        (float)g_r_acc.x, (float)g_r_acc.y,
                        (float)g_r_acc.w, (float)g_r_acc.h,
                        arad, (ink_col & 0x00FFFFFFu) | (al << 24));
    }

    finn_icon_blit_ink(&g_ic_acc, g_layer, screen_w, screen_h,
                       g_r_acc.x + g_r_acc.w / 2,
                       g_r_acc.y + g_r_acc.h / 2,
                       rs(16.0f, s), ink_col, ink_col, 0);

    /* 5. Start pill button at x=717, y=480 (80x32 r=50, #000000 in Light / #FFFFFF in Dark) */
    g_r_start.x = rx + rs(47.0f, s);
    g_r_start.y = rs(480.0f, s);
    g_r_start.w = rs(80.0f, s);
    g_r_start.h = rs(32.0f, s);
    float srad = (float)g_r_start.h * 0.5f;

    finn_fill_rrect(g_layer, screen_w, screen_h,
                    (float)g_r_start.x, (float)g_r_start.y,
                    (float)g_r_start.w, (float)g_r_start.h,
                    srad, start_bg);

    if (g_hover_act == FINNSETUP_START || g_press_act == FINNSETUP_START) {
        float w = (g_hover_act == FINNSETUP_START ? FINN_MOTION_HOVER_WASH * g_hover : 0.0f) +
                  (g_press_act == FINNSETUP_START ? FINN_MOTION_PRESS_WASH * press_v : 0.0f);
        unsigned al = (unsigned)(w * 255.0f + 0.5f);
        if (al > 255) al = 255;
        finn_fill_rrect(g_layer, screen_w, screen_h,
                        (float)g_r_start.x, (float)g_r_start.y,
                        (float)g_r_start.w, (float)g_r_start.h,
                        srad, (start_fg & 0x00FFFFFFu) | (al << 24));
    }

    int start_cy = g_r_start.y + g_r_start.h / 2;
    finn_text_draw(&g_f_btn, g_layer, screen_w, screen_h,
                   (float)(g_r_start.x + rs(14.0f, s)),
                   (float)start_cy + btn_cap * 0.5f,
                   "Start", start_fg);

    finn_icon_blit_ink(&g_ic_chev_right, g_layer, screen_w, screen_h,
                       g_r_start.x + g_r_start.w - rs(18.0f, s), start_cy,
                       rs(11.0f, s), start_fg, start_fg, 0);
}

void finnsetup_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finnsetup_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, dx, dy);
}

void finnsetup_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (w) *w = (float)g_lw;
    if (h) *h = (float)g_lh;
    if (alpha) *alpha = v;
}

void finnsetup_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_layer) return;
    float x, y, w, h, alpha;
    finnsetup_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_scaled(dst, dw, dh, g_layer, g_lw, g_lh, x, y, w, h, alpha);
}

const uint32_t *finnsetup_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_rev;
    return finnsetup_visible() ? g_layer : NULL;
}
