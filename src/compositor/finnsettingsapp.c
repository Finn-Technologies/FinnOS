/*
 * finnsettingsapp — the Settings application view inside finn-comp.
 *
 * The app is a window, so its body is already frosted glass over the desktop;
 * this file is the furniture on it. The arrangement is the one a person
 * already knows from macOS System Settings — a translucent sidebar with a
 * search field and grouped rows, and a content column of inset cards — but
 * the material, radii and type are FinnOS's, so it belongs to the shell it
 * lives in rather than looking like a port.
 *
 * Both columns are the same sheet. The sidebar is the body glass set back by
 * a wash of ink; a card is the body glass brought forward by a second frost.
 * Painting the sidebar white instead made the window read as two windows: a
 * flat white page on the left with glass leaking in from the right.
 *
 * Two rules decide most of what follows:
 *
 *   - Everything is a token. A row that hardcodes white in the light
 *     appearance goes white-on-white in the dark one, and that is exactly
 *     what this screen used to do.
 *   - Nothing is faked. A machine without a radio says so and its switch is
 *     inert; there is no refresh rate to report, so none is shown; the power
 *     page says the sleep policy does not exist yet.
 */
#define _GNU_SOURCE
#include "finnsettingsapp.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnvg.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <linux/input.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <ctype.h>

/*
 * This app works in the frame's own units: a 250 sidebar, a 30px search
 * field, 12px labels. ui_scale() maps one design unit onto the compositor's
 * pixels, so the frame fills the window it was drawn for and keeps that size
 * when the window is maximised.
 */

/* Round to scaled integer */
static inline int rs(float v, float s) { return (int)lroundf(v * s); }

static inline int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

/*
 * One material, three depths. The body glass is the window's own (0.62 in
 * finnwindow.c); the sidebar is that sheet set back by a wash of ink, and a
 * card is the same sheet brought forward by a second frost. The three are
 * deliberately close — a card that turns opaque is a web form, not a panel,
 * and a sidebar that turns white is a different window.
 */
#define CARD_GLASS   0.72f

/*
 * Sidebar metrics, reference units — the Figma frame's numbers: a 250 sheet,
 * the title at 15,44, a 220x30 search field at 15,70, and 30px rows on a 35px
 * pitch starting at 138, with each group's caption 18 above its first row.
 */
#define SB_W        250.0f
#define SB_PAD       16.0f
#define SB_TITLE_Y   60.0f
#define SB_SEARCH_Y  88.0f
#define SB_SEARCH_H  30.0f
#define SB_FIRST_Y  148.0f
#define SB_ROW_H     30.0f
#define SB_ROW_STEP  33.0f
#define SB_SECTION_GAP 6.0f

/* Content metrics: the same 16px title on the sidebar's baseline, then cards. */
#define CARD_R        8.0f
#define CARD_PAD     18.0f
#define HEADER_TOP   60.0f
#define HEADER_SUB   88.0f
#define CARD_TOP    104.0f
#define CARD_GAP     12.0f

/*
 * The window keeps its size and the furniture inside it comes down a little.
 * A whole tenth off would read as a different app at a different resolution;
 * three percent reads as the same app, drawn by someone who left the room to
 * breathe. Render and hit-testing both take the scale from here, or a control
 * would move out from under the pointer that found it.
 */

/* Card internals, shared by every page so no page invents its own rhythm. */
#define ROW_H        34.0f
#define HERO_H       62.0f
#define SW_W         42.0f
#define SW_H         25.0f
#define TILE_W      112.0f
#define TILE_H       66.0f

/*
 * The frame is 900x500; the window the compositor opens for it is 860x480
 * shell units, which is 1116x623 of the compositor's pixels at scale 1. The
 * app therefore draws at the compositor's scale times that ratio: 900 design
 * units across the window's resting width, and the same physical size when
 * the window is maximised. Deriving the scale from the window's *current*
 * width instead made every surface zoom when the window filled the screen —
 * a maximised window should have more room, not larger text.
 */
#define WIN_REST_REF    1.24f
#define CONTENT_MAX    780.0f
static float ui_scale(float scale) {
    float s = scale * WIN_REST_REF;
    return s > 0.0f ? s : 1.0f;
}

/* ---------------- fonts & assets ---------------- */

static finn_font g_f_hero;      /* 22 reference: page title */
static finn_font g_f_title;     /* 15 reference: card row title */
static finn_font g_f_label;     /* 12 reference: body */
static finn_font g_f_sub;       /* 10 reference: detail */
static finn_font g_f_micro;     /* 8  reference: section caps */
static float     g_font_scale = -1.0f;
static char      g_font_dir[512] = "";
static char      g_icon_dir[512] = "";

static fsvg g_ic_wifi;
static fsvg g_ic_bt;
static fsvg g_ic_sun;
static fsvg g_ic_vol;
static fsvg g_ic_bat;
static fsvg g_ic_about;
static fsvg g_ic_search;
static fsvg g_ic_power;
static fsvg g_ic_refresh;
static fsvg g_ic_chevron;
static fsvg g_ic_display;
static int  g_ready = 0;

/* ---------------- pages metadata ---------------- */

typedef struct {
    const char *id;
    const char *title;
    const char *section;
    fsvg       *icon;
    const char *keywords;
} settings_page_desc;

static const settings_page_desc g_pages[] = {
    { "wifi",      "Wi-Fi",     "NETWORK", &g_ic_wifi,  "wifi wi-fi wireless network internet wlan" },
    { "bluetooth", "Bluetooth", "NETWORK", &g_ic_bt,    "bluetooth bt wireless accessory accessories device" },
    { "display",   "Display",   "DEVICE",  &g_ic_display, "display screen monitor resolution scale brightness appearance dark light theme" },
    { "sound",     "Sound",     "DEVICE",  &g_ic_vol,   "sound audio volume speaker output mixer mute" },
    { "power",     "Power",     "DEVICE",  &g_ic_bat,   "power battery charge mains energy ac charging" },
    { "about",     "About",     "SYSTEM",  &g_ic_about, "about system os kernel version uptime hardware host memory ram" }
};
#define PAGE_COUNT ((int)(sizeof(g_pages)/sizeof(g_pages[0])))

/* ---------------- interactive state ---------------- */

static int   g_active_page = 0;
static char  g_search_text[64] = "";
static int   g_search_focused = 0;
static float g_caret_timer = 0.0f;
static int   g_caret_visible = 1;

static int   g_dragging_slider = 0; /* 0 = none, 1 = volume, 2 = brightness */
static int   g_cached_screen_w = 1920;
static int   g_cached_screen_h = 1080;
static int   g_cached_wx = 0;
static int   g_cached_wy = 0;
static int   g_cached_ww = 900;
static int   g_cached_wh = 500;

/* Saved volume for mute toggle */
static int   g_prev_volume = 70;

/* ---------------- font loader ---------------- */

static int load_fonts(float s) {
    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char regular[600];
    snprintf(regular, sizeof regular, "%s/FixelDisplay-Regular.otf", g_font_dir);

    /* The frame's type, one step up: 17 for the titles, 13 for rows and the
     * search field, 10.5 for group captions. The frame is drawn at 1280 wide
     * and this screen renders it at 0.89, so the frame's own 16/12 comes out
     * a size too quiet to read at the distance the window actually sits. */
    int ok = 1;
    ok &= (finn_font_load(&g_f_hero,  regular, font_px(s, 22.0f)) == 0);
    ok &= (finn_font_load(&g_f_title, regular, font_px(s, 15.0f)) == 0);
    ok &= (finn_font_load(&g_f_label, regular, font_px(s, 13.0f)) == 0);
    ok &= (finn_font_load(&g_f_sub,   regular, font_px(s, 12.0f)) == 0);
    ok &= (finn_font_load(&g_f_micro, regular, font_px(s, 11.5f)) == 0);

    if (!ok) {
        fprintf(stderr, "finnsettingsapp: cannot load fonts from %s\n", g_font_dir);
        return -1;
    }
    return 0;
}

int finnsettingsapp_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");

    char p[600];
    int ok = 1;
    snprintf(p, sizeof p, "%s/wifi-02.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_wifi) == 0);
    snprintf(p, sizeof p, "%s/bluetooth.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_bt) == 0);
    snprintf(p, sizeof p, "%s/sun-01.svg", g_icon_dir);         ok &= (fsvg_load(p, &g_ic_sun) == 0);
    snprintf(p, sizeof p, "%s/volume-02.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_vol) == 0);
    snprintf(p, sizeof p, "%s/battery.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_bat) == 0);
    snprintf(p, sizeof p, "%s/boot-mark.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_about) == 0);
    snprintf(p, sizeof p, "%s/search-01.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_search) == 0);
    snprintf(p, sizeof p, "%s/power.svg", g_icon_dir);          ok &= (fsvg_load(p, &g_ic_power) == 0);
    snprintf(p, sizeof p, "%s/refresh.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_refresh) == 0);
    snprintf(p, sizeof p, "%s/chevron-right.svg", g_icon_dir);  ok &= (fsvg_load(p, &g_ic_chevron) == 0);
    snprintf(p, sizeof p, "%s/display.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_display) == 0);

    g_ready = ok;
    return ok ? 0 : -1;
}

void finnsettingsapp_free(void) {
    fsvg_free(&g_ic_wifi);
    fsvg_free(&g_ic_bt);
    fsvg_free(&g_ic_sun);
    fsvg_free(&g_ic_vol);
    fsvg_free(&g_ic_bat);
    fsvg_free(&g_ic_about);
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_power);
    fsvg_free(&g_ic_refresh);
    fsvg_free(&g_ic_chevron);
    fsvg_free(&g_ic_display);

    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    g_font_scale = -1.0f;
    g_ready = 0;
}

/* ---------------- timer & key tracking ---------------- */

int finnsettingsapp_tick(float dt) {
    int dirty = 0;
    if (g_search_focused) {
        g_caret_timer += dt;
        if (g_caret_timer >= 0.5f) {
            g_caret_timer = 0.0f;
            g_caret_visible = !g_caret_visible;
            dirty = 1;
        }
    }
    return dirty;
}

uint32_t finnsettingsapp_build_key(float scale, const finn_state *st) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(scale * 1000.0f));
    MIX(g_active_page);
    MIX(g_search_focused);
    MIX(g_caret_visible);
    MIX(st->dark);
    MIX(st->wifi);
    MIX(st->bluetooth);
    MIX(st->volume);
    MIX(st->brightness);
    MIX(st->battery_pct);
    MIX(st->charging);
    MIX(st->wired);
    for (const char *p = g_search_text; *p; p++) MIX(*p);
    #undef MIX
    return h ? h : 1u;
}

/* ---------------- search filtering ---------------- */

static int page_matches_search(int idx) {
    if (!g_search_text[0]) return 1;
    if (strcasestr(g_pages[idx].title, g_search_text) != NULL) return 1;
    if (strcasestr(g_pages[idx].keywords, g_search_text) != NULL) return 1;
    return 0;
}

/* ---------------- drawing helpers ---------------- */

/* A card: the window glass brought forward by a second frost, and one
 * hairline so it keeps an edge against a white wallpaper. Every card in the
 * app goes through here, so no page can invent a different paper. */
static void card_bg(uint32_t *L, int lw, int lh, int lx0, int ly0,
                    float x, float y, float w, float h, float r,
                    float alpha, int accent) {
    finn_frost_rrect(L, lw, lh, lx0, ly0, x, y, w, h, r, FINN_GLASS, alpha);
    /* A card is raised above the window, so a dark card takes a little light
     * rather than only more tint. */
    if (finn_theme_dark())
        finn_fill_rrect(L, lw, lh, x, y, w, h, r, 0x0AFFFFFFu);
    finn_stroke_rrect(L, lw, lh, x, y, w, h, r, accent ? 2.0f : 1.0f,
                      accent ? FINN_ON : FINN_HAIR);
}

/* A circular icon chip with the accent behind it. */
static void icon_disc(uint32_t *L, int lw, int lh, float cx, float cy,
                      float d, float s, fsvg *ic, int lit) {
    uint32_t bg = lit ? FINN_ON : (finn_theme_dark() ? 0xFF3A3F49u : 0xFFE7E7EAu);
    uint32_t fg = lit ? 0xFFFFFFFFu : FINN_DIM;
    finn_fill_rrect(L, lw, lh, cx - d * 0.5f, cy - d * 0.5f, d, d, d * 0.5f, bg);
    if (ic && ic->vw > 0)
        finn_icon_blit_ink(ic, L, lw, lh, (int)lroundf(cx), (int)lroundf(cy),
                           rs(d * 0.56f / s, s), fg, fg, 0);
}

/* A macOS-style switch: a pill track and a knob, inert when unavailable. */
static void switch_ctrl(uint32_t *L, int lw, int lh, float x, float y,
                        float w, float h, int on, int enabled) {
    float r = h * 0.5f;
    uint32_t track = !enabled ? (finn_theme_dark() ? 0xFF343841u : 0x1F000000u)
                   : (on ? FINN_ON : (finn_theme_dark() ? 0xFF4A4F59u : 0xFFD6D6DAu));
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, track);

    float knob = h - 4.0f;
    float kx = on ? (x + w - 2.0f - knob) : (x + 2.0f);
    uint32_t kc = !enabled ? (finn_theme_dark() ? 0x66FFFFFFu : 0x66FFFFFFu)
                 : (on ? 0xFFFFFFFFu : (finn_theme_dark() ? 0xFFB9BCC4u : 0xFFFFFFFFu));
    finn_fill_rrect(L, lw, lh, kx, y + 2.0f, knob, knob, knob * 0.5f, kc);
}

/* A capsule action button. */
static void pill_button(uint32_t *L, int lw, int lh, float x, float y,
                        float w, float h, const char *label,
                        const finn_font *f, int filled, int enabled) {
    float r = h * 0.5f;
    uint32_t bg = !enabled ? (finn_theme_dark() ? 0x1AFFFFFFu : 0x14000000u)
                : (filled ? FINN_ON : (finn_theme_dark() ? 0x24FFFFFFu : 0x18000000u));
    uint32_t fg = !enabled ? FINN_MUTE : (filled ? 0xFFFFFFFFu : FINN_INK);
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, bg);
    if (!filled && enabled)
        finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 1.0f,
                          finn_theme_dark() ? 0x26FFFFFFu : 0x1A000000u);
    float tw = finn_text_width(f, label);
    finn_text_draw(f, L, lw, lh, x + (w - tw) * 0.5f,
                   y + h * 0.5f + finn_text_cap_height(f) * 0.5f, label, fg);
}

/* A slider with a knob, drawn for the settings cards. */
static void slider_ctrl(uint32_t *L, int lw, int lh, float x, float y,
                        float w, float h, int pct, fsvg *icon, int enabled) {
    float r = h * 0.5f;
    uint32_t track = finn_theme_dark() ? 0x33FFFFFFu : 0x33000000u;
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, track);

    if (!enabled) {
        if (icon && icon->vw > 0)
            finn_icon_blit_ink(icon, L, lw, lh, (int)lroundf(x + h * 0.5f),
                               (int)lroundf(y + h * 0.5f),
                               (int)lroundf(h * 0.48f),
                               FINN_MUTE, FINN_MUTE, 0);
        return;
    }

    float fw = w * (float)pct / 100.0f;
    if (fw < h) fw = h;
    if (fw > w) fw = w;
    /*
     * The accent, not the shell's white fill: this slider sits on a card that
     * is itself near-white, where a white bar reads as a hole in the track.
     * The Control Center keeps the white fill because it is glass over the
     * desktop; paper needs ink.
     *
     * Rounded at its start, square at the level: the track continues past it.
     */
    finn_fill_bar(L, lw, lh, x, y, fw, h, r, FINN_ON);

    if (icon && icon->vw > 0) {
        int covered = fw > h * 1.4f;
        uint32_t ink = covered ? 0xFFFFFFFFu : FINN_INK;
        finn_icon_blit_ink(icon, L, lw, lh, (int)lroundf(x + h * 0.5f),
                           (int)lroundf(y + h * 0.5f),
                           (int)lroundf(h * 0.52f), ink, ink, 0);
    }
}

/* ---------------- sidebar geometry ---------------- */

typedef struct {
    int is_header;
    int page_idx;
    const char *label;
    float y_ref;      /* top of the row, reference units */
} sidebar_layout_item;

static int get_sidebar_layout(sidebar_layout_item *items, int max_items) {
    int count = 0;
    const char *last_section = "";
    float cur_y = SB_FIRST_Y;

    for (int i = 0; i < PAGE_COUNT && count < max_items; i++) {
        if (!page_matches_search(i)) continue;

        if (strcmp(g_pages[i].section, last_section) != 0) {
            last_section = g_pages[i].section;
            /* Gap after the previous group's last row, then the caption's
             * own 18 before its first row. */
            if (count > 0) cur_y += SB_SECTION_GAP + 10.0f;

            /* The caption sits 10 above its first row, so it belongs to the
             * group below it rather than the row above. */
            if (count < max_items) {
                items[count].is_header = 1;
                items[count].page_idx = -1;
                items[count].label = last_section;
                items[count].y_ref = cur_y - 10.0f;
                count++;
            }
        }

        if (count < max_items) {
            items[count].is_header = 0;
            items[count].page_idx = i;
            items[count].label = g_pages[i].title;
            items[count].y_ref = cur_y;
            count++;
        }
        cur_y += SB_ROW_STEP;
    }
    return count;
}

/* Where the content column starts and how wide it is, in scaled pixels.
 * Render and hit-testing both go through this, so a card cannot move without
 * its control moving with it. */
static void content_box(int wx, int ww, float s, float *cx, float *cw) {
    float sb_w = s * SB_W;
    if (sb_w > (float)ww) sb_w = (float)ww;
    float pad = s * 32.0f;
    float avail = (float)ww - sb_w - pad * 2.0f;
    if (avail < 80.0f) avail = 80.0f;

    /* A maximised window gets a wider column, but only up to a point: a
     * paragraph of settings rows as wide as the screen is unreadable. Past
     * that the column stops growing and stays where the sidebar put it, so
     * the eye still reads it as one page rather than a floating island. */
    float w = s * CONTENT_MAX;
    if (avail < w) w = avail;
    *cx = (float)wx + sb_w + pad;
    *cw = w;
}

/* ---------------- rendering ---------------- */

/*
 * The sidebar is the Figma frame, drawn: 250 reference units of sheet, the
 * 16px title at 15,44, a 30px search field at 15,70, then group captions and
 * 30px rows on a 35px pitch, with an 18px circular accent chip in front of
 * every row. Nothing here is an interpretation of the design; the numbers are
 * the frame's numbers.
 */
static void render_sidebar(uint32_t *L, int lw, int lh,
                           float sb_x, float sb_y, float sb_w, float sb_h,
                           float win_r, float s, const finn_state *st) {
    (void)st;

    /*
     * The frame's sidebar is opaque white over a 70% window. The shell builds
     * the same relationship from glass: a denser frost on the body, so the
     * column still carries the wallpaper's light but never its shape. At 0.92
     * the sheet is solid enough for a 12px label to sit on without a scrim.
     */
    finn_frost_rrect(L, lw, lh, 0, 0, sb_x, sb_y, sb_w, sb_h, win_r,
                     FINN_GLASS, 0.92f);
    /* The sheet is round only where the window is round. The frost above
     * rounds all four corners; this squares off the two at the seam. */
    finn_frost_rrect(L, lw, lh, 0, 0, sb_x + sb_w - win_r, sb_y, win_r, sb_h,
                     0.0f, FINN_GLASS, 0.92f);

    /* the seam between the two columns */
    finn_fill_rrect(L, lw, lh, sb_x + sb_w - 1.0f, sb_y, 1.0f, sb_h, 0.0f,
                    FINN_HAIR);

    float pad = s * SB_PAD;
    float title_x = sb_x + pad;

    float t_cap = finn_text_cap_height(&g_f_hero);
    finn_text_draw(&g_f_hero, L, lw, lh, title_x, sb_y + s * SB_TITLE_Y + t_cap,
                   "Settings", FINN_INK);

    /* Search field: the frame's 220x30 field at 15,70, radius 4. */
    float sf_x = title_x;
    float sf_y = sb_y + s * SB_SEARCH_Y;
    float sf_w = sb_w - pad * 2.0f;
    float sf_h = s * SB_SEARCH_H;
    float sf_r = s * 4.0f;
    finn_fill_rrect(L, lw, lh, sf_x, sf_y, sf_w, sf_h, sf_r,
                    finn_theme_dark() ? 0x33FFFFFFu : 0x40C7C7C7u);
    if (g_search_focused)
        finn_stroke_rrect(L, lw, lh, sf_x, sf_y, sf_w, sf_h, sf_r, 1.5f, FINN_ON);

    if (g_ic_search.vw > 0)
        finn_icon_blit_ink(&g_ic_search, L, lw, lh,
                           (int)lroundf(sf_x + s * 15.0f),
                           (int)lroundf(sf_y + sf_h * 0.5f),
                           rs(16.0f, s), FINN_DIM, FINN_MUTE, 0);

    float stext_x = sf_x + s * 36.0f;
    float stext_y = sf_y + sf_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f;
    if (g_search_text[0] == '\0') {
        finn_text_draw(&g_f_label, L, lw, lh, stext_x, stext_y,
                       "Type here to search", FINN_DIM);
    } else {
        finn_text_draw(&g_f_label, L, lw, lh, stext_x, stext_y,
                       g_search_text, FINN_INK);
    }
    if (g_search_focused && g_caret_visible) {
        float tw = finn_text_width(&g_f_label, g_search_text);
        float ch = finn_text_cap_height(&g_f_label) * 1.15f;
        finn_fill_rrect(L, lw, lh, stext_x + tw + 2.0f,
                        sf_y + (sf_h - ch) * 0.5f, 1.5f, ch, 0.75f, FINN_ON);
    }

    /* Group captions and rows, on the frame's 35px pitch. */
    sidebar_layout_item items[24];
    int n_items = get_sidebar_layout(items, 24);

    for (int k = 0; k < n_items; k++) {
        float y = sb_y + s * items[k].y_ref;
        if (items[k].is_header) {
            finn_text_draw(&g_f_micro, L, lw, lh, title_x, y + finn_text_cap_height(&g_f_micro),
                           items[k].label, FINN_MUTE);
            continue;
        }

        int pidx = items[k].page_idx;
        float rw = sf_w;
        float rh = s * SB_ROW_H;
        float rr = s * 4.0f;
        int active = (pidx == g_active_page);

        if (active) {
            finn_fill_rrect(L, lw, lh, title_x, y, rw, rh, rr,
                            finn_theme_dark() ? 0x38FFFFFFu : 0x26006FFFu);
            finn_stroke_rrect(L, lw, lh, title_x, y, rw, rh, rr, 1.0f,
                              finn_theme_dark() ? 0x2EFFFFFFu : 0x24006FFFu);
        }

        /* The frame's chip: 18px, a circle, the accent under a white glyph. */
        float chip = s * 20.0f;
        float chip_x = title_x + s * 6.0f;
        float chip_y = y + (rh - chip) * 0.5f;
        finn_fill_rrect(L, lw, lh, chip_x, chip_y, chip, chip, chip * 0.5f,
                        active ? FINN_ON
                               : (finn_theme_dark() ? 0xFF4A4F59u : 0xFF006FFFu));

        fsvg *ic = g_pages[pidx].icon;
        if (ic && ic->vw > 0)
            finn_icon_blit_ink(ic, L, lw, lh,
                               (int)lroundf(chip_x + chip * 0.5f),
                               (int)lroundf(chip_y + chip * 0.5f),
                               rs(13.0f, s), 0xFFFFFFFFu, 0xFFFFFFFFu, 0);

        float tx = chip_x + chip + s * 10.0f;
        float ty = y + rh * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f;
        finn_text_draw(&g_f_label, L, lw, lh, tx, ty, g_pages[pidx].title, FINN_INK);
    }

    if (n_items == 0) {
        finn_text_draw(&g_f_label, L, lw, lh, title_x,
                       sb_y + s * (SB_FIRST_Y + 10.0f), "No matching settings", FINN_MUTE);
    }
}

/* ---------------- cards ----------------
 *
 * A page is a column of cards. Every card is the same paper, every caption
 * sits on the same baseline, and every row is the same 34 units tall with its
 * hairline inset by the card's padding. The rhythm is the whole point: a page
 * that invents one number looks like a different app.
 */

/* A caption row at the top of a card: 10px, tracked by the face, quiet. */
static void card_head(uint32_t *L, int lw, int lh, float x, float y,
                      const char *label, float s) {
    finn_text_draw(&g_f_micro, L, lw, lh, x + s * CARD_PAD,
                   y + s * 21.0f, label, FINN_MUTE);
}

/* One label/value line, value flush right. */
static void card_row(uint32_t *L, int lw, int lh, float x, float y, float w,
                     const char *label, const char *value, float s) {
    float cap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, L, lw, lh, x + s * CARD_PAD, y + s * 20.0f + cap,
                   label, FINN_INK);
    if (value && value[0]) {
        float vw = finn_text_width(&g_f_label, value);
        finn_text_draw(&g_f_label, L, lw, lh, x + w - s * CARD_PAD - vw,
                       y + s * 20.0f + cap, value, FINN_DIM);
    }
}

/* The hairline between two rows, inset so it stops short of the card edge. */
static void card_div(uint32_t *L, int lw, int lh, float x, float y, float w, float s) {
    finn_fill_rrect(L, lw, lh, x + s * CARD_PAD, y, w - s * CARD_PAD * 2.0f,
                    1.0f, 0.0f, FINN_HAIR);
}

/*
 * A quiet well inside a card: the sentence a page has instead of a list when
 * there is nothing to list. It is a recess, not another card, so it takes a
 * wash of ink rather than a frost and never a hairline of its own.
 */
static void card_well(uint32_t *L, int lw, int lh, float x, float y, float w,
                      float h, const char *line, const char *sub, float s) {
    float wn = w - s * CARD_PAD * 2.0f;
    float wx = x + s * CARD_PAD;
    finn_fill_rrect(L, lw, lh, wx, y, wn, h, s * 6.0f,
                    finn_theme_dark() ? 0x1AFFFFFFu : 0x0D000000u);
    float cap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, L, lw, lh, wx + s * 14.0f, y + s * 24.0f + cap,
                   line, FINN_DIM);
    if (sub && sub[0])
        finn_text_draw(&g_f_sub, L, lw, lh, wx + s * 14.0f, y + s * 46.0f + cap,
                       sub, FINN_MUTE);
}

/* The status card every connectivity page opens with: chip, state, switch. */
static void card_status(uint32_t *L, int lw, int lh, int lx0, int ly0,
                        float x, float y, float w, float s, fsvg *icon,
                        const char *title, const char *sub, int on, int enabled) {
    float h = s * HERO_H;
    float r = s * CARD_R;
    card_bg(L, lw, lh, lx0, ly0, x, y, w, h, r, CARD_GLASS, 0);

    float disc = s * 34.0f;
    icon_disc(L, lw, lh, x + s * CARD_PAD + disc * 0.5f, y + h * 0.5f,
              disc, s, icon, on);

    float tx = x + s * CARD_PAD + disc + s * 12.0f;
    finn_text_draw(&g_f_title, L, lw, lh, tx, y + s * 26.0f, title, FINN_INK);
    finn_text_draw(&g_f_sub, L, lw, lh, tx, y + s * 44.0f, sub, FINN_DIM);

    switch_ctrl(L, lw, lh, x + w - s * CARD_PAD - s * SW_W,
                y + (h - s * SW_H) * 0.5f, s * SW_W, s * SW_H, on, enabled);
}

/* ---------------- pages ---------------- */

static void page_wifi(uint32_t *L, int lw, int lh, int lx0, int ly0,
                      float cx, float card_y, float cw, float s,
                      const finn_state *st) {
    float r = s * CARD_R;
    int ok = st->wifi >= 0;

    const char *title = !ok ? "No wireless adapter"
                     : (st->wifi == 0 ? "Wi-Fi is off" : "Wi-Fi is on");
    const char *sub = !ok ? "This machine has no Wi-Fi hardware"
                    : (st->wifi == 0 ? "Turn it on to join a network"
                                     : "No network joined yet");
    card_status(L, lw, lh, lx0, ly0, cx, card_y, cw, s, &g_ic_wifi,
                title, sub, ok && st->wifi > 0, ok);

    /* Known networks: the list, or the reason there is no list. */
    float y = card_y + s * HERO_H + s * CARD_GAP;
    float h = s * 110.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, y, cw, h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, y, "KNOWN NETWORKS", s);

    if (!ok)
        card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
                  "No wireless hardware to scan with.",
                  "The compositor found no station interface.", s);
    else if (st->wifi <= 0)
        card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
                  "Wi-Fi is turned off.",
                  "Networks are listed once the radio is on.", s);
    else
        card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
                  "No networks in range.",
                  "Association is not implemented in this build.", s);

    /* The interface, reported rather than guessed. */
    y += h + s * CARD_GAP;
    card_bg(L, lw, lh, lx0, ly0, cx, y, cw, s * 110.0f, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, y, "INTERFACE", s);
    card_row(L, lw, lh, cx, y + s * ROW_H, cw, "Interface",
             ok ? "wlan0" : "None", s);
    card_div(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, "State",
             !ok ? "Unavailable" : (st->wifi > 0 ? "Ready" : "Off"), s);
}

static void page_bluetooth(uint32_t *L, int lw, int lh, int lx0, int ly0,
                           float cx, float card_y, float cw, float s,
                           const finn_state *st) {
    float r = s * CARD_R;
    int ok = st->bluetooth >= 0;

    const char *title = !ok ? "No Bluetooth adapter"
                     : (st->bluetooth == 0 ? "Bluetooth is off" : "Bluetooth is on");
    const char *sub = !ok ? "This machine has no Bluetooth controller"
                    : (st->bluetooth == 0 ? "Turn it on to pair an accessory"
                                          : "Discoverable as finnos");
    card_status(L, lw, lh, lx0, ly0, cx, card_y, cw, s, &g_ic_bt,
                title, sub, ok && st->bluetooth > 0, ok);

    float y = card_y + s * HERO_H + s * CARD_GAP;
    float h = s * 110.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, y, cw, h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, y, "MY DEVICES", s);
    card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
              (!ok || st->bluetooth <= 0) ? "No accessories connected."
                                          : "No accessories paired yet.",
              "Pairing is not implemented in this build.", s);

    y += h + s * CARD_GAP;
    card_bg(L, lw, lh, lx0, ly0, cx, y, cw, s * 110.0f, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, y, "CONTROLLER", s);
    card_row(L, lw, lh, cx, y + s * ROW_H, cw, "Adapter",
             ok ? "hci0" : "None", s);
    card_div(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, "State",
             !ok ? "Unavailable" : (st->bluetooth > 0 ? "Ready" : "Off"), s);
}

/* One of the two appearance previews: a miniature desktop, not a screenshot. */
static void appearance_tile(uint32_t *L, int lw, int lh, float x, float y,
                            float w, float h, float s, int dark, int selected) {
    float r = s * 8.0f;

    uint32_t sky  = dark ? 0xFF1A1D25u : 0xFFE4F0FEu;
    uint32_t deep = dark ? 0xFF101319u : 0xFFA9CDF2u;
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, sky);
    finn_fill_rrect(L, lw, lh, x, y + h * 0.58f, w, h * 0.42f, 0.0f, deep);

    float win_w = w * 0.58f, win_h = h * 0.48f;
    float wx = x + (w - win_w) * 0.5f;
    float wy = y + h * 0.16f;
    finn_fill_rrect(L, lw, lh, wx, wy, win_w, win_h, s * 4.0f,
                    dark ? 0xCC2B3039u : 0xD9FFFFFFu);
    finn_fill_rrect(L, lw, lh, wx, wy, win_w * 0.3f, win_h, s * 4.0f,
                    dark ? 0xE0222630u : 0xE6F1F3F7u);
    finn_fill_rrect(L, lw, lh, wx + win_w - s * 9.0f, wy + s * 3.5f,
                    s * 6.0f, s * 2.6f, s * 1.3f,
                    dark ? 0x66FFFFFFu : 0x40000000u);

    float dock_w = w * 0.42f, dock_h = s * 6.0f;
    finn_fill_rrect(L, lw, lh, x + (w - dock_w) * 0.5f, y + h - dock_h - s * 6.0f,
                    dock_w, dock_h, dock_h * 0.5f,
                    dark ? 0xC03C414Cu : 0xC0FFFFFFu);

    finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 1.0f,
                      dark ? 0x30FFFFFFu : 0x24000000u);

    if (selected) {
        finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 2.0f, FINN_ON);
        float check = s * 16.0f;
        float cxp = x + w - check - s * 6.0f;
        float cyp = y + s * 6.0f;
        finn_fill_rrect(L, lw, lh, cxp, cyp, check, check, check * 0.5f, FINN_ON);
        float cx0 = cxp + check * 0.30f, cy0 = cyp + check * 0.52f;
        float cx1 = cxp + check * 0.45f, cy1 = cyp + check * 0.68f;
        float cx2 = cxp + check * 0.72f, cy2 = cyp + check * 0.36f;
        for (float t = 0.0f; t <= 1.0f; t += 0.08f) {
            float ax = cx0 + (cx1 - cx0) * t, ay = cy0 + (cy1 - cy0) * t;
            float bx = cx1 + (cx2 - cx1) * t, by = cy1 + (cy2 - cy1) * t;
            finn_fill_rrect(L, lw, lh, ax - s * 0.9f, ay - s * 0.9f,
                            s * 1.8f, s * 1.8f, s * 0.9f, 0xFFFFFFFFu);
            finn_fill_rrect(L, lw, lh, bx - s * 0.9f, by - s * 0.9f,
                            s * 1.8f, s * 1.8f, s * 0.9f, 0xFFFFFFFFu);
        }
    }

    float cap = finn_text_cap_height(&g_f_sub);
    float label_w = finn_text_width(&g_f_sub, dark ? "Dark" : "Light");
    finn_text_draw(&g_f_sub, L, lw, lh, x + (w - label_w) * 0.5f,
                   y + h + s * 14.0f + cap, dark ? "Dark" : "Light", FINN_DIM);
}

static void page_display(uint32_t *L, int lw, int lh, int lx0, int ly0,
                         float cx, float card_y, float cw, float s,
                         const finn_state *st) {
    float r = s * CARD_R;

    /* Output facts. Everything here is read from the running device. */
    float c1_h = s * 144.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, card_y, cw, c1_h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, card_y, "DISPLAY", s);

    char res[64], scale_s[64];
    snprintf(res, sizeof res, "%d × %d", g_cached_screen_w, g_cached_screen_h);
    snprintf(scale_s, sizeof scale_s, "%.2f×", s);

    card_row(L, lw, lh, cx, card_y + s * ROW_H, cw, "Resolution", res, s);
    card_div(L, lw, lh, cx, card_y + s * (ROW_H * 2.0f), cw, s);
    card_row(L, lw, lh, cx, card_y + s * (ROW_H * 2.0f), cw, "UI scale", scale_s, s);
    card_div(L, lw, lh, cx, card_y + s * (ROW_H * 3.0f), cw, s);
    card_row(L, lw, lh, cx, card_y + s * (ROW_H * 3.0f), cw, "Presentation", "Direct DRM/KMS", s);

    /* Appearance: two previews, side by side. */
    float c2_y = card_y + c1_h + s * CARD_GAP;
    float c2_h = s * 158.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, c2_y, "APPEARANCE", s);

    float tw = s * TILE_W, th = s * TILE_H;
    float gap = s * 16.0f;
    float tx = cx + s * CARD_PAD;
    float ty = c2_y + s * 38.0f;
    appearance_tile(L, lw, lh, tx, ty, tw, th, s, 0, !st->dark);
    appearance_tile(L, lw, lh, tx + tw + gap, ty, tw, th, s, 1, st->dark);

    finn_text_draw(&g_f_sub, L, lw, lh, tx, c2_y + c2_h - s * 14.0f,
                   "Saved to /var/lib/finn/settings and applied to every surface.",
                   FINN_MUTE);
}

static void page_sound(uint32_t *L, int lw, int lh, int lx0, int ly0,
                       float cx, float card_y, float cw, float s,
                       const finn_state *st) {
    float r = s * CARD_R;
    float c1_h = s * 132.0f;

    card_bg(L, lw, lh, lx0, ly0, cx, card_y, cw, c1_h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, card_y, "OUTPUT VOLUME", s);

    if (st->volume >= 0) {
        char vol_s[16];
        snprintf(vol_s, sizeof vol_s, "%d%%", st->volume);
        float vw = finn_text_width(&g_f_sub, vol_s);
        finn_text_draw(&g_f_sub, L, lw, lh, cx + cw - s * CARD_PAD - vw,
                       card_y + s * 22.0f, vol_s, FINN_DIM);

        float sl_x = cx + s * CARD_PAD;
        float sl_y = card_y + s * 40.0f;
        float sl_w = cw - s * CARD_PAD * 2.0f;
        float sl_h = s * 26.0f;
        slider_ctrl(L, lw, lh, sl_x, sl_y, sl_w, sl_h, st->volume, &g_ic_vol, 1);

        float mb_w = s * 72.0f, mb_h = s * 28.0f;
        pill_button(L, lw, lh, cx + s * CARD_PAD, card_y + s * 80.0f,
                    mb_w, mb_h, st->volume == 0 ? "Unmute" : "Mute",
                    &g_f_label, 0, 1);
    } else {
        card_well(L, lw, lh, cx, card_y + s * 30.0f, cw, s * 70.0f,
                  "No audio mixer on this machine",
                  "Volume is absent, not zero: there is no ALSA element.", s);
    }

    float c2_y = card_y + c1_h + s * CARD_GAP;
    float c2_h = s * 110.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, c2_y, "AUDIO CONTROLLER", s);

    const char *elem = finn_audio_element();
    card_row(L, lw, lh, cx, c2_y + s * ROW_H, cw, "Mixer element",
             elem ? elem : "None", s);
    card_div(L, lw, lh, cx, c2_y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, c2_y + s * 68.0f, cw, "Driver", "ALSA", s);
}

static void page_power(uint32_t *L, int lw, int lh, int lx0, int ly0,
                       float cx, float card_y, float cw, float s,
                       const finn_state *st) {
    float r = s * CARD_R;
    float hero_h = s * 92.0f;

    card_bg(L, lw, lh, lx0, ly0, cx, card_y, cw, hero_h, r, CARD_GLASS, 0);

    float disc = s * 34.0f;
    icon_disc(L, lw, lh, cx + s * CARD_PAD + disc * 0.5f,
              card_y + s * 46.0f, disc, s, &g_ic_bat, 1);

    float tx = cx + s * CARD_PAD + disc + s * 12.0f;
    float ty = card_y + s * 38.0f, dy = card_y + s * 58.0f;

    if (st->battery_pct >= 0) {
        char t[64];
        snprintf(t, sizeof t, "Battery %d%%", st->battery_pct);
        finn_text_draw(&g_f_title, L, lw, lh, tx, ty, t, FINN_INK);
        finn_text_draw(&g_f_sub, L, lw, lh, tx, dy,
                       st->charging ? "Charging · plugged in"
                                    : "On battery · unplugged", FINN_DIM);

        float gm_x = cx + s * CARD_PAD;
        float gm_y = card_y + s * 74.0f;
        float gm_w = cw - s * CARD_PAD * 2.0f;
        float gm_h = s * 8.0f;
        finn_fill_rrect(L, lw, lh, gm_x, gm_y, gm_w, gm_h, gm_h * 0.5f,
                        finn_theme_dark() ? 0x3DFFFFFFu : 0x24000000u);
        float gw = gm_w * (float)st->battery_pct / 100.0f;
        if (gw < gm_h) gw = gm_h;
        finn_fill_rrect(L, lw, lh, gm_x, gm_y, gw, gm_h, gm_h * 0.5f, FINN_ON);
    } else {
        finn_text_draw(&g_f_title, L, lw, lh, tx, ty, "Power source: mains", FINN_INK);
        finn_text_draw(&g_f_sub, L, lw, lh, tx, dy,
                       "No battery on this machine", FINN_DIM);
    }

    float c2_y = card_y + hero_h + s * CARD_GAP;
    float c2_h = s * 148.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, c2_y, "POWER SUPPLY", s);

    card_row(L, lw, lh, cx, c2_y + s * ROW_H, cw, "Battery",
             st->battery_pct >= 0 ? "Present" : "Not present", s);
    card_div(L, lw, lh, cx, c2_y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, c2_y + s * 68.0f, cw, "External power",
             st->battery_pct < 0 || st->charging ? "Connected" : "Disconnected", s);
    card_div(L, lw, lh, cx, c2_y + s * (ROW_H*3.0f), cw, s);
    card_row(L, lw, lh, cx, c2_y + s * (ROW_H*3.0f), cw, "Sleep policy",
             "Not implemented yet", s);
}

static void page_about(uint32_t *L, int lw, int lh, int lx0, int ly0,
                       float cx, float card_y, float cw, float s,
                       const finn_state *st) {
    (void)st;
    float r = s * CARD_R;

    struct utsname un;
    memset(&un, 0, sizeof un);
    uname(&un);

    char host[64] = "finnos";
    gethostname(host, sizeof host);

    struct sysinfo si;
    memset(&si, 0, sizeof si);
    sysinfo(&si);

    char uptime_s[64];
    long up_m = si.uptime / 60;
    snprintf(uptime_s, sizeof uptime_s, "%ld hr, %ld min", up_m / 60, up_m % 60);

    char mem_s[64];
    double gb = (double)(si.totalram) * (double)si.mem_unit / (1024.0 * 1024.0 * 1024.0);
    snprintf(mem_s, sizeof mem_s, "%.1f GB", gb);

    /* The mark, not a spinner: the identity at the top of the page. */
    float c1_h = s * 88.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, card_y, cw, c1_h, r, CARD_GLASS, 0);

    float tile = s * 52.0f;
    float tx0 = cx + s * CARD_PAD;
    float ty0 = card_y + (c1_h - tile) * 0.5f;
    finn_fill_rrect(L, lw, lh, tx0, ty0, tile, tile, s * 12.0f,
                    finn_theme_dark() ? 0xFF2F333Cu : 0xFFFFFFFFu);
    finn_stroke_rrect(L, lw, lh, tx0, ty0, tile, tile, s * 12.0f, 1.0f, FINN_HAIR);
    if (g_ic_about.vw > 0)
        finn_icon_blit_ink(&g_ic_about, L, lw, lh,
                           (int)lroundf(tx0 + tile * 0.5f),
                           (int)lroundf(ty0 + tile * 0.5f),
                           rs(29.0f, s), FINN_INK, FINN_INK, 0);

    float hx = tx0 + tile + s * 14.0f;
    finn_text_draw(&g_f_title, L, lw, lh, hx, card_y + s * 43.0f,
                   "FinnOS 0.1", FINN_INK);
    finn_text_draw(&g_f_sub, L, lw, lh, hx, card_y + s * 63.0f,
                   "Zero-toolkit compositor · direct DRM/KMS", FINN_DIM);

    float c2_y = card_y + c1_h + s * CARD_GAP;
    float c2_h = s * 222.0f;
    card_bg(L, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h, r, CARD_GLASS, 0);
    card_head(L, lw, lh, cx, c2_y, "SYSTEM", s);

    float row = s * ROW_H;
    float ry = c2_y + s * 38.0f;

    card_row(L, lw, lh, cx, ry, cw, "Device name", host, s);
    card_div(L, lw, lh, cx, ry + s * ROW_H, cw, s);
    card_row(L, lw, lh, cx, ry + row, cw, "Kernel", un.release, s);
    card_div(L, lw, lh, cx, ry + row + s * 34.0f, cw, s);
    card_row(L, lw, lh, cx, ry + row * 2.0f, cw, "Architecture", un.machine, s);
    card_div(L, lw, lh, cx, ry + row * 2.0f + s * 34.0f, cw, s);
    card_row(L, lw, lh, cx, ry + row * 3.0f, cw, "Memory", mem_s, s);
    card_div(L, lw, lh, cx, ry + row * 3.0f + s * 34.0f, cw, s);
    card_row(L, lw, lh, cx, ry + row * 4.0f, cw, "Uptime", uptime_s, s);
}


/* ---------------- rendering ---------------- */

void finnsettingsapp_render(uint32_t *layer, int lw, int lh,
                            int lx0, int ly0, int wx, int wy, int ww, int wh,
                            int screen_w, int screen_h,
                            float scale, const finn_state *st, int focused) {
    (void)focused;
    if (!g_ready) return;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }

    float s = ui_scale(scale);
    g_cached_screen_w = screen_w;
    g_cached_screen_h = screen_h;
    g_cached_wx = wx;
    g_cached_wy = wy;
    g_cached_ww = ww;
    g_cached_wh = wh;

    /* Ensure the active page survives a new filter. */
    if (!page_matches_search(g_active_page)) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            if (page_matches_search(i)) {
                g_active_page = i;
                break;
            }
        }
    }

    float sb_w = s * SB_W;
    if (sb_w > (float)ww) sb_w = (float)ww;
    float sb_x = (float)(wx - lx0);
    float sb_y = (float)(wy - ly0);
    float sb_h = (float)wh;
    float win_r = s * 8.0f;

    render_sidebar(layer, lw, lh, sb_x, sb_y, sb_w, sb_h, win_r, s, st);

    /* Content column. */
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    cx -= (float)lx0;
    float cy = sb_y;

    const char *header_sub = "";
    if (g_active_page == 0)      header_sub = "Wireless adapters and connections";
    else if (g_active_page == 1) header_sub = "Pair and manage accessories";
    else if (g_active_page == 2) header_sub = "Resolution, scale and appearance";
    else if (g_active_page == 3) header_sub = "Output level and mixer";
    else if (g_active_page == 4) header_sub = "Supply status and battery";
    else if (g_active_page == 5) header_sub = "Version, kernel and hardware";

    float pt_y = cy + s * HEADER_TOP + finn_text_cap_height(&g_f_hero);
    finn_text_draw(&g_f_hero, layer, lw, lh, cx, pt_y,
                   g_pages[g_active_page].title, FINN_INK);
    finn_text_draw(&g_f_sub, layer, lw, lh, cx,
                   cy + s * HEADER_SUB + finn_text_cap_height(&g_f_sub),
                   header_sub, FINN_DIM);

    float card_y = cy + s * CARD_TOP;

    switch (g_active_page) {
    case 0: page_wifi(layer, lw, lh, lx0, ly0, cx, card_y, cw, s, st); break;
    case 1: page_bluetooth(layer, lw, lh, lx0, ly0, cx, card_y, cw, s, st); break;
    case 2: page_display(layer, lw, lh, lx0, ly0, cx, card_y, cw, s, st); break;
    case 3: page_sound(layer, lw, lh, lx0, ly0, cx, card_y, cw, s, st); break;
    case 4: page_power(layer, lw, lh, lx0, ly0, cx, card_y, cw, s, st); break;
    default: page_about(layer, lw, lh, lx0, ly0, cx, card_y, cw, s, st); break;
    }
}

/* ---------------- hit testing & interaction ---------------- */

/* The switch in the first card of the Wi-Fi and Bluetooth pages. */
static int toggle_rect(int wx, int wy, int ww, float s,
                       float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    float card_y = (float)wy + s * CARD_TOP;
    float sw_w = s * SW_W, sw_h = s * SW_H;
    float sw_x = cx + cw - s * CARD_PAD - sw_w;
    float sw_y = card_y + (s * HERO_H - sw_h) * 0.5f;
    if (bx) *bx = sw_x;
    if (by) *by = sw_y;
    if (bw) *bw = sw_w;
    if (bh) *bh = sw_h;
    return 1;
}

static void appearance_rect(int wx, int wy, int ww, float s, int which,
                            float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    (void)cw;
    float card_y = (float)wy + s * CARD_TOP + s * 144.0f + s * CARD_GAP;
    float tw = s * TILE_W, th = s * TILE_H;
    float tx = cx + s * CARD_PAD + which * (tw + s * 16.0f);
    float ty = card_y + s * 34.0f;
    if (bx) *bx = tx;
    if (by) *by = ty;
    if (bw) *bw = tw;
    if (bh) *bh = th;
}

static void volume_rect(int wx, int wy, int ww, float s,
                        float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    float card_y = (float)wy + s * CARD_TOP;
    if (bx) *bx = cx + s * CARD_PAD;
    if (by) *by = card_y + s * 40.0f;
    if (bw) *bw = cw - s * CARD_PAD * 2.0f;
    if (bh) *bh = s * 26.0f;
}

static void mute_rect(int wx, int wy, int ww, float s,
                      float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    (void)cw;
    float card_y = (float)wy + s * CARD_TOP;
    if (bx) *bx = cx + s * CARD_PAD;
    if (by) *by = card_y + s * 80.0f;
    if (bw) *bw = s * 72.0f;
    if (bh) *bh = s * 28.0f;
}

static int in_box(int px, int py, float x, float y, float w, float h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

int finnsettingsapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                         float scale, int *out_sub) {
    if (out_sub) *out_sub = 0;
    if (px < wx || py < wy || px >= wx + ww || py >= wy + wh)
        return FINNSETTINGS_HIT_NONE;

    float s = ui_scale(scale);
    float sb_w = s * SB_W;
    if (sb_w > (float)ww) sb_w = (float)ww;

    /* Inside the sidebar. */
    if (px < wx + (int)sb_w) {
        if (py < wy + rs(24.0f, s))
            return FINNSETTINGS_HIT_TITLEBAR;

        float sf_x = (float)wx + s * SB_PAD;
        float sf_y = (float)wy + s * SB_SEARCH_Y;
        float sf_w = sb_w - s * SB_PAD * 2.0f;
        float sf_h = s * SB_SEARCH_H;

        if (in_box(px, py, sf_x, sf_y, sf_w, sf_h))
            return FINNSETTINGS_HIT_SEARCH;

        sidebar_layout_item items[20];
        int n_items = get_sidebar_layout(items, 20);
        for (int k = 0; k < n_items; k++) {
            if (items[k].is_header) continue;
            float ry = (float)wy + s * items[k].y_ref;
            float rh = s * SB_ROW_H;
            if (in_box(px, py, sf_x, ry, sf_w, rh)) {
                if (out_sub) *out_sub = items[k].page_idx;
                return FINNSETTINGS_HIT_ROW;
            }
        }
        return FINNSETTINGS_HIT_BODY;
    }

    /* The content titlebar drag strip, left of the window options pill. */
    if (py < wy + rs(28.0f, s) && px < wx + ww - rs(90.0f, s))
        return FINNSETTINGS_HIT_TITLEBAR;

    float bx, by, bw, bh;

    if (g_active_page == 0) {
        toggle_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_WIFI_TOGGLE;
    } else if (g_active_page == 1) {
        toggle_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_BT_TOGGLE;
    } else if (g_active_page == 2) {
        appearance_rect(wx, wy, ww, s, 0, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_LIGHT_THEME;
        appearance_rect(wx, wy, ww, s, 1, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_DARK_THEME;
    } else if (g_active_page == 3) {
        volume_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_VOL_SLIDER;
        mute_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_MUTE_TOGGLE;
    }

    return FINNSETTINGS_HIT_BODY;
}

void finnsettingsapp_press(int hit, int sub, int px, int py, float scale,
                           const finn_state *st) {
    (void)py;
    float s = ui_scale(scale);

    if (hit == FINNSETTINGS_HIT_SEARCH) {
        g_search_focused = 1;
        g_caret_timer = 0.0f;
        g_caret_visible = 1;
        return;
    }

    /* A click anywhere else takes the caret away from the search field. */
    g_search_focused = 0;

    if (hit == FINNSETTINGS_HIT_ROW) {
        if (sub >= 0 && sub < PAGE_COUNT) g_active_page = sub;
        return;
    }

    if (hit == FINNSETTINGS_HIT_WIFI_TOGGLE) {
        if (st->wifi >= 0) finn_state_set_wifi(st->wifi == 0 ? 1 : 0);
        return;
    }

    if (hit == FINNSETTINGS_HIT_BT_TOGGLE) {
        if (st->bluetooth >= 0) finn_state_set_bluetooth(st->bluetooth == 0 ? 1 : 0);
        return;
    }

    if (hit == FINNSETTINGS_HIT_LIGHT_THEME) { finn_state_set_dark(0); return; }
    if (hit == FINNSETTINGS_HIT_DARK_THEME)  { finn_state_set_dark(1); return; }

    if (hit == FINNSETTINGS_HIT_VOL_SLIDER) {
        float bx, by, bw, bh;
        volume_rect(g_cached_wx, g_cached_wy, g_cached_ww, s, &bx, &by, &bw, &bh);
        float rel = (float)px - bx;
        int pct = (int)lroundf((rel / bw) * 100.0f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        finn_state_set_volume(pct);
        g_dragging_slider = 1;
        return;
    }

    if (hit == FINNSETTINGS_HIT_MUTE_TOGGLE) {
        if (st->volume > 0) {
            g_prev_volume = st->volume;
            finn_state_set_volume(0);
        } else {
            finn_state_set_volume(g_prev_volume > 0 ? g_prev_volume : 50);
        }
        return;
    }
}

int finnsettingsapp_drag(int px, int py, float scale, const finn_state *st) {
    (void)py; (void)st;
    if (!g_dragging_slider) return 0;

    float s = ui_scale(scale);
    float bx, by, bw, bh;
    volume_rect(g_cached_wx, g_cached_wy, g_cached_ww, s, &bx, &by, &bw, &bh);

    float rel = (float)px - bx;
    int pct = (int)lroundf((rel / bw) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    if (g_dragging_slider == 1) {
        finn_state_set_volume(pct);
        return 1;
    }
    return 0;
}

void finnsettingsapp_release(void) {
    g_dragging_slider = 0;
}

/* ---------------- keyboard input for search field ---------------- */

int finnsettingsapp_key(const finn_key_event *e) {
    if (!g_search_focused) return 0;
    if (!e->pressed) return 1; /* consume key releases while focused */

    if (e->code == KEY_ESC) {
        if (g_search_text[0]) g_search_text[0] = '\0';
        else                  g_search_focused = 0;
        return 1;
    }

    if (e->code == KEY_BACKSPACE) {
        size_t len = strlen(g_search_text);
        if (len > 0) {
            g_search_text[len - 1] = '\0';
            g_caret_timer = 0.0f;
            g_caret_visible = 1;
        }
        return 1;
    }

    if ((e->mods & FINN_MOD_CTRL) && (e->code == KEY_A)) {
        g_search_text[0] = '\0';
        g_caret_timer = 0.0f;
        g_caret_visible = 1;
        return 1;
    }

    if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
        g_search_focused = 0;
        return 1;
    }

    if (e->keysym >= 32 && e->keysym <= 126) {
        size_t len = strlen(g_search_text);
        if (len + 1 < sizeof(g_search_text)) {
            g_search_text[len] = (char)e->keysym;
            g_search_text[len + 1] = '\0';
            g_caret_timer = 0.0f;
            g_caret_visible = 1;
        }
        return 1;
    }

    return 1;
}

/* ---------------- control plane navigation verbs ---------------- */

int finnsettingsapp_set_page(const char *name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (strcasecmp(name, g_pages[i].id) == 0 ||
            strcasecmp(name, g_pages[i].title) == 0) {
            g_active_page = i;
            return 0;
        }
    }
    return -1;
}

const char *finnsettingsapp_get_page(void) {
    if (g_active_page >= 0 && g_active_page < PAGE_COUNT)
        return g_pages[g_active_page].id;
    return "wifi";
}
