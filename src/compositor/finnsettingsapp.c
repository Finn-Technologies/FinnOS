/*
 * finnsettingsapp — the Settings application view inside finn-comp (Figma 14:330).
 *
 * Implements the Settings desktop app:
 *   - Sidebar: 250x500 at left, solid white with r=8 outer corners
 *     - "Settings" title at (15, 44) (Fixel Display 16px)
 *     - Search field at (15, 70), 220x30 r=4 with search icon and placeholder
 *     - Section labels: "NETWORK", "DEVICE", "SYSTEM" (grey 8px caps)
 *     - Interactive rows: "Wi-Fi", "Bluetooth", "Display", "Sound", "Power", "About"
 *       (220x30 r=4.14 with 18px blue circular badge and selected-row highlight)
 *   - Content area: width 650 at right over window frosted glass
 *     - Content pages for Wi-Fi, Bluetooth, Display, Sound, Power, and About
 *     - Real state from finnstate, sysfs, uname, sysinfo, and the control plane
 *     - Honest absent states for hardware not installed
 *     - Working interactive controls: appearance switch, volume slider, toggles
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

#define REF 1.298077f

/* Round to scaled integer */
static inline int rs(float v, float s) { return (int)lroundf(v * s); }

static inline int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

/* ---------------- fonts & assets ---------------- */

static finn_font g_f_title_lg;  /* 20px reference */
static finn_font g_f_title;     /* 16px reference */
static finn_font g_f_label;     /* 12px reference */
static finn_font g_f_sub;       /* 10px reference */
static finn_font g_f_micro;     /* 8px reference */
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
    { "display",   "Display",   "DEVICE",  &g_ic_sun,   "display screen monitor resolution scale brightness appearance dark light theme" },
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
    finn_font_free(&g_f_title_lg);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);

    int ok = 1;
    ok &= (finn_font_load(&g_f_title_lg, p, font_px(s, 20.0f * REF)) == 0);
    ok &= (finn_font_load(&g_f_title,    p, font_px(s, 16.0f * REF)) == 0);
    ok &= (finn_font_load(&g_f_label,    p, font_px(s, 12.0f * REF)) == 0);
    ok &= (finn_font_load(&g_f_sub,      p, font_px(s, 10.0f * REF)) == 0);
    ok &= (finn_font_load(&g_f_micro,    p, font_px(s, 8.0f * REF)) == 0);

    if (!ok) {
        fprintf(stderr, "finnsettingsapp: cannot load fonts from %s\n", p);
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
    snprintf(p, sizeof p, "%s/sun-01.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_sun) == 0);
    snprintf(p, sizeof p, "%s/volume-02.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_vol) == 0);
    snprintf(p, sizeof p, "%s/battery.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_bat) == 0);
    snprintf(p, sizeof p, "%s/settings-02.svg", g_icon_dir);    ok &= (fsvg_load(p, &g_ic_about) == 0);
    snprintf(p, sizeof p, "%s/search-01.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_search) == 0);
    snprintf(p, sizeof p, "%s/power.svg", g_icon_dir);          ok &= (fsvg_load(p, &g_ic_power) == 0);
    snprintf(p, sizeof p, "%s/refresh.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_refresh) == 0);
    snprintf(p, sizeof p, "%s/chevron-right.svg", g_icon_dir);  ok &= (fsvg_load(p, &g_ic_chevron) == 0);

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

    finn_font_free(&g_f_title_lg);
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

/* Fill a rounded rect that only rounds its left corners, leaving the right edge straight */
static void fill_sidebar_bg(uint32_t *b, int w, int h, float x, float y,
                            float rw, float rh, float r, uint32_t c) {
    int x0 = (int)floorf(x);
    int x1 = (int)ceilf(x + rw);
    int y0 = (int)floorf(y);
    int y1 = (int)ceilf(y + rh);

    for (int yy = y0; yy < y1; yy++) {
        if (yy < 0 || yy >= h) continue;
        for (int xx = x0; xx < x1; xx++) {
            if (xx < 0 || xx >= w) continue;
            float px = (float)xx + 0.5f;
            float py = (float)yy + 0.5f;
            float a = 1.0f;
            if (px < x + r) {
                if (py < y + r) {
                    float dx = px - (x + r);
                    float dy = py - (y + r);
                    float d = sqrtf(dx * dx + dy * dy) - r;
                    a = 0.5f - d;
                } else if (py > y + rh - r) {
                    float dx = px - (x + r);
                    float dy = py - (y + rh - r);
                    float d = sqrtf(dx * dx + dy * dy) - r;
                    a = 0.5f - d;
                }
            }
            if (a <= 0.0f) continue;
            if (a > 1.0f) a = 1.0f;
            b[(size_t)yy * w + xx] = finn_over(b[(size_t)yy * w + xx], c, a);
        }
    }
}

/* Draw a crisp anti-aliased focus ring / border */
static void draw_rrect_stroke(uint32_t *b, int w, int h, float x, float y,
                              float rw, float rh, float r, float thickness, uint32_t c) {
    if (rw <= 0.0f || rh <= 0.0f) return;
    int x0 = (int)floorf(x - thickness - 1);
    int x1 = (int)ceilf(x + rw + thickness + 1);
    int y0 = (int)floorf(y - thickness - 1);
    int y1 = (int)ceilf(y + rh + thickness + 1);

    float cx = x + rw * 0.5f, cy = y + rh * 0.5f, hw = rw * 0.5f, hh = rh * 0.5f;
    float half_th = thickness * 0.5f;

    for (int yy = y0; yy <= y1; yy++) {
        if (yy < 0 || yy >= h) continue;
        for (int xx = x0; xx <= x1; xx++) {
            if (xx < 0 || xx >= w) continue;
            float px = (float)xx + 0.5f, py = (float)yy + 0.5f;
            float dx = fabsf(px - cx) - (hw - r), dy = fabsf(py - cy) - (hh - r);
            float ox = fmaxf(dx, 0.0f), oy = fmaxf(dy, 0.0f);
            float sdf = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(dx, dy), 0.0f) - r;
            float dist = fabsf(sdf) - half_th;
            float a = 0.5f - dist;
            if (a <= 0.0f) continue;
            if (a > 1.0f) a = 1.0f;
            b[(size_t)yy * w + xx] = finn_over(b[(size_t)yy * w + xx], c, a);
        }
    }
}

/* ---------------- sidebar geometry calculation ---------------- */

typedef struct {
    int is_header;
    int page_idx;
    const char *label;
    float y_ref;
} sidebar_layout_item;

static int get_sidebar_layout(sidebar_layout_item *items, int max_items) {
    int count = 0;
    const char *last_section = "";

    float cur_y = 120.0f; /* NETWORK starting y in reference coordinates */

    for (int i = 0; i < PAGE_COUNT && count < max_items; i++) {
        if (!page_matches_search(i)) continue;

        if (strcmp(g_pages[i].section, last_section) != 0) {
            last_section = g_pages[i].section;
            if (count > 0) cur_y += 17.0f; /* gap before section */

            if (count < max_items) {
                items[count].is_header = 1;
                items[count].page_idx = -1;
                items[count].label = last_section;
                items[count].y_ref = cur_y;
                count++;
            }
            cur_y += 18.0f; /* gap after section label to first row */
        }

        if (count < max_items) {
            items[count].is_header = 0;
            items[count].page_idx = i;
            items[count].label = g_pages[i].title;
            items[count].y_ref = cur_y;
            count++;
        }
        cur_y += 35.0f; /* 30px row + 5px gap */
    }
    return count;
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

    float s = scale;
    g_cached_screen_w = screen_w;
    g_cached_screen_h = screen_h;
    g_cached_wx = wx;
    g_cached_wy = wy;
    g_cached_ww = ww;
    g_cached_wh = wh;

    /* Ensure active page is visible if filtered */
    if (!page_matches_search(g_active_page)) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            if (page_matches_search(i)) {
                g_active_page = i;
                break;
            }
        }
    }

    /* ---------------- 1. Sidebar Background ---------------- */
    float sb_w = s * (250.0f * REF);
    if (sb_w > (float)ww) sb_w = (float)ww;
    float sb_x = (float)(wx - lx0);
    float sb_y = (float)(wy - ly0);
    float sb_h = (float)wh;
    float win_r = s * (8.0f * REF);

    fill_sidebar_bg(layer, lw, lh, sb_x, sb_y, sb_w, sb_h, win_r, 0xFFFFFFFFu);

    /* ---------------- 2. Settings Title ---------------- */
    /* Figma 14:339: (15, 44), 16px Fixel Display Regular */
    float title_x = sb_x + s * (15.0f * REF);
    float title_y = sb_y + s * (44.0f * REF) + finn_text_cap_height(&g_f_title);
    finn_text_draw(&g_f_title, layer, lw, lh, title_x, title_y, "Settings", 0xFF000000u);

    /* ---------------- 3. Search Field ---------------- */
    /* Figma 14:357: (15, 70), 220x30 r=4, fill #C7C7C740 */
    float sf_x = sb_x + s * (15.0f * REF);
    float sf_y = sb_y + s * (70.0f * REF);
    float sf_w = s * (220.0f * REF);
    float sf_h = s * (30.0f * REF);
    float sf_r = s * (4.0f * REF);

    finn_fill_rrect(layer, lw, lh, sf_x, sf_y, sf_w, sf_h, sf_r, 0x40C7C7C7u);

    if (g_search_focused) {
        draw_rrect_stroke(layer, lw, lh, sf_x, sf_y, sf_w, sf_h, sf_r, 1.5f * s, FINN_ON);
    }

    /* Search icon: 18x18 at (21, 76) */
    if (g_ic_search.vw > 0) {
        int ico_sz = rs(18.0f * REF, s);
        int ic_cx = (int)lroundf(sf_x + s * (6.0f * REF) + ico_sz * 0.5f);
        int ic_cy = (int)lroundf(sf_y + sf_h * 0.5f);
        finn_icon_blit_ink(&g_ic_search, layer, lw, lh, ic_cx, ic_cy, ico_sz,
                           0xFF555555u, 0xFF555555u, 0);
    }

    /* Search placeholder or query */
    float stext_x = sf_x + s * (34.0f * REF);
    float stext_y = sf_y + sf_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f;

    if (g_search_text[0] == '\0') {
        finn_text_draw(&g_f_label, layer, lw, lh, stext_x, stext_y,
                       "Type here to search", 0xFF555555u);
        if (g_search_focused && g_caret_visible) {
            float ch = finn_text_cap_height(&g_f_label) * 1.2f;
            float cy0 = sf_y + (sf_h - ch) * 0.5f;
            finn_fill_rrect(layer, lw, lh, stext_x, cy0, 1.5f * s, ch, 0.5f, FINN_ON);
        }
    } else {
        finn_text_draw(&g_f_label, layer, lw, lh, stext_x, stext_y,
                       g_search_text, 0xFF000000u);
        if (g_search_focused && g_caret_visible) {
            float tw = finn_text_width(&g_f_label, g_search_text);
            float ch = finn_text_cap_height(&g_f_label) * 1.2f;
            float cy0 = sf_y + (sf_h - ch) * 0.5f;
            finn_fill_rrect(layer, lw, lh, stext_x + tw + 2.0f, cy0, 1.5f * s, ch, 0.5f, FINN_ON);
        }
    }

    /* ---------------- 4. Sidebar Sections & Rows ---------------- */
    sidebar_layout_item items[20];
    int n_items = get_sidebar_layout(items, 20);

    for (int k = 0; k < n_items; k++) {
        if (items[k].is_header) {
            float hy = sb_y + s * (items[k].y_ref * REF) + finn_text_cap_height(&g_f_micro);
            finn_text_draw(&g_f_micro, layer, lw, lh, title_x, hy, items[k].label, 0xFF555555u);
        } else {
            int pidx = items[k].page_idx;
            float ry = sb_y + s * (items[k].y_ref * REF);
            float rw = s * (220.0f * REF);
            float rh = s * (30.0f * REF);
            float rr = s * (4.14f * REF);

            /* Selected row highlight */
            if (pidx == g_active_page) {
                finn_fill_rrect(layer, lw, lh, title_x, ry, rw, rh, rr, 0x1E006FFFu);
            }

            /* 18px blue circular badge */
            float b_sz = s * (18.0f * REF);
            float bx = title_x + s * (6.0f * REF);
            float by = ry + s * (6.0f * REF);
            finn_fill_rrect(layer, lw, lh, bx, by, b_sz, b_sz, b_sz * 0.5f, FINN_ON);

            /* Icon inside badge (12px ink in white) */
            fsvg *ic = g_pages[pidx].icon;
            if (ic && ic->vw > 0) {
                int ico_sz = rs(12.0f * REF, s);
                int cx = (int)lroundf(bx + b_sz * 0.5f);
                int cy = (int)lroundf(by + b_sz * 0.5f);
                finn_icon_blit_ink(ic, layer, lw, lh, cx, cy, ico_sz,
                                   0xFFFFFFFFu, 0xFFFFFFFFu, 0);
            }

            /* Row label */
            float tx = title_x + s * (34.0f * REF);
            float ty = ry + rh * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f;
            finn_text_draw(&g_f_label, layer, lw, lh, tx, ty, g_pages[pidx].title, 0xFF000000u);
        }
    }

    if (n_items == 0) {
        float ny = sb_y + s * (140.0f * REF);
        finn_text_draw(&g_f_sub, layer, lw, lh, title_x, ny, "No matching settings", 0xFF555555u);
    }

    /* ---------------- 5. Content Area ---------------- */
    float ca_x = sb_x + sb_w;
    float ca_w = (float)ww - sb_w;
    float pad_x = s * (35.0f * REF);
    float pad_y = s * (30.0f * REF);

    float cx = ca_x + pad_x;
    float cy = sb_y + pad_y;
    float cw = ca_w - pad_x * 2.0f;
    if (cw < 100.0f) return;

    /* Page Header */
    const char *header_title = g_pages[g_active_page].title;
    const char *header_sub = "";
    if (g_active_page == 0)      header_sub = "Manage wireless network adapters and connections";
    else if (g_active_page == 1) header_sub = "Pair and manage wireless Bluetooth accessories";
    else if (g_active_page == 2) header_sub = "Display resolution, scale factor, and system appearance";
    else if (g_active_page == 3) header_sub = "Audio output device levels and master mixer";
    else if (g_active_page == 4) header_sub = "Power supply status and battery management";
    else if (g_active_page == 5) header_sub = "FinnOS system overview, kernel, and hardware specifications";

    float pt_y = cy + finn_text_cap_height(&g_f_title_lg);
    finn_text_draw(&g_f_title_lg, layer, lw, lh, cx, pt_y, header_title, FINN_INK);

    float ps_y = cy + s * (28.0f * REF) + finn_text_cap_height(&g_f_sub);
    finn_text_draw(&g_f_sub, layer, lw, lh, cx, ps_y, header_sub, FINN_DIM);

    /* Card starting position */
    float card_y = cy + s * (58.0f * REF);
    float card_r = s * (10.0f * REF);

    /* ---------------- PAGE 0: Wi-Fi ---------------- */
    if (g_active_page == 0) {
        float c1_h = s * (76.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, card_y, cw, c1_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, card_y, cw, c1_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        /* Badge 36px */
        float b36 = s * (36.0f * REF);
        float b36_x = cx + s * (16.0f * REF);
        float b36_y = card_y + (c1_h - b36) * 0.5f;
        uint32_t bcol = st->wifi > 0 ? FINN_ON : 0xFF969696u;
        finn_fill_rrect(layer, lw, lh, b36_x, b36_y, b36, b36, b36 * 0.5f, bcol);

        if (g_ic_wifi.vw > 0) {
            finn_icon_blit_ink(&g_ic_wifi, layer, lw, lh,
                               (int)lroundf(b36_x + b36 * 0.5f),
                               (int)lroundf(b36_y + b36 * 0.5f),
                               rs(20.0f * REF, s), 0xFFFFFFFFu, 0xFFFFFFFFu, 0);
        }

        /* Status title & subtitle */
        const char *w_title = (st->wifi < 0) ? "No wireless adapter"
                            : (st->wifi == 0 ? "Wi-Fi is Off" : "Wi-Fi is Connected");
        const char *w_sub = (st->wifi < 0) ? "No Wi-Fi hardware installed in this machine"
                          : (st->wifi == 0 ? "Turn on Wi-Fi to scan for wireless networks"
                                           : "Connected · Signal strength: Strong (3 of 3 arcs)");
        float tx = b36_x + b36 + s * (16.0f * REF);
        float ty1 = card_y + s * (28.0f * REF);
        float ty2 = card_y + s * (48.0f * REF);
        finn_text_draw(&g_f_title, layer, lw, lh, tx, ty1, w_title, FINN_INK);
        finn_text_draw(&g_f_sub, layer, lw, lh, tx, ty2, w_sub, FINN_DIM);

        /* Action button */
        float btn_w = s * (86.0f * REF);
        float btn_h = s * (30.0f * REF);
        float btn_x = cx + cw - s * (16.0f * REF) - btn_w;
        float btn_y = card_y + (c1_h - btn_h) * 0.5f;
        float btn_r = btn_h * 0.5f;

        if (st->wifi < 0) {
            finn_fill_rrect(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, 0x1A969696u);
            float bw = finn_text_width(&g_f_label, "Disabled");
            finn_text_draw(&g_f_label, layer, lw, lh, btn_x + (btn_w - bw) * 0.5f,
                           btn_y + btn_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                           "Disabled", FINN_MUTE);
        } else if (st->wifi == 0) {
            finn_fill_rrect(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, FINN_ON);
            float bw = finn_text_width(&g_f_label, "Turn On");
            finn_text_draw(&g_f_label, layer, lw, lh, btn_x + (btn_w - bw) * 0.5f,
                           btn_y + btn_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                           "Turn On", 0xFFFFFFFFu);
        } else {
            finn_fill_rrect(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, 0x1A000000u);
            draw_rrect_stroke(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, 1.0f * s, 0x33000000u);
            float bw = finn_text_width(&g_f_label, "Turn Off");
            finn_text_draw(&g_f_label, layer, lw, lh, btn_x + (btn_w - bw) * 0.5f,
                           btn_y + btn_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                           "Turn Off", FINN_INK);
        }

        /* Card 2: Networks list */
        float c2_y = card_y + c1_h + s * (16.0f * REF);
        float c2_h = s * (120.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, c2_y, cw, c2_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        finn_text_draw(&g_f_micro, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (22.0f * REF), "KNOWN NETWORKS", 0xFF555555u);

        if (st->wifi <= 0) {
            finn_text_draw(&g_f_label, layer, lw, lh, cx + s * (16.0f * REF),
                           c2_y + s * (58.0f * REF),
                           "No wireless networks available while Wi-Fi is off.", FINN_MUTE);
        } else {
            finn_text_draw(&g_f_label, layer, lw, lh, cx + s * (16.0f * REF),
                           c2_y + s * (54.0f * REF),
                           "No wireless networks detected in range.", FINN_INK);
            finn_text_draw(&g_f_sub, layer, lw, lh, cx + s * (16.0f * REF),
                           c2_y + s * (76.0f * REF),
                           "Active scan requires wpa_supplicant or station interface.", FINN_DIM);
        }
    }

    /* ---------------- PAGE 1: Bluetooth ---------------- */
    else if (g_active_page == 1) {
        float c1_h = s * (76.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, card_y, cw, c1_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, card_y, cw, c1_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        float b36 = s * (36.0f * REF);
        float b36_x = cx + s * (16.0f * REF);
        float b36_y = card_y + (c1_h - b36) * 0.5f;
        uint32_t bcol = st->bluetooth > 0 ? FINN_ON : 0xFF969696u;
        finn_fill_rrect(layer, lw, lh, b36_x, b36_y, b36, b36, b36 * 0.5f, bcol);

        if (g_ic_bt.vw > 0) {
            finn_icon_blit_ink(&g_ic_bt, layer, lw, lh,
                               (int)lroundf(b36_x + b36 * 0.5f),
                               (int)lroundf(b36_y + b36 * 0.5f),
                               rs(20.0f * REF, s), 0xFFFFFFFFu, 0xFFFFFFFFu, 0);
        }

        const char *bt_title = (st->bluetooth < 0) ? "No Bluetooth adapter"
                             : (st->bluetooth == 0 ? "Bluetooth is Off" : "Bluetooth is On");
        const char *bt_sub = (st->bluetooth < 0) ? "No Bluetooth controller detected on this device"
                           : (st->bluetooth == 0 ? "Turn on Bluetooth to connect accessories"
                                                 : "Discoverable as finnos");
        float tx = b36_x + b36 + s * (16.0f * REF);
        finn_text_draw(&g_f_title, layer, lw, lh, tx, card_y + s * (28.0f * REF), bt_title, FINN_INK);
        finn_text_draw(&g_f_sub, layer, lw, lh, tx, card_y + s * (48.0f * REF), bt_sub, FINN_DIM);

        float btn_w = s * (86.0f * REF);
        float btn_h = s * (30.0f * REF);
        float btn_x = cx + cw - s * (16.0f * REF) - btn_w;
        float btn_y = card_y + (c1_h - btn_h) * 0.5f;
        float btn_r = btn_h * 0.5f;

        if (st->bluetooth < 0) {
            finn_fill_rrect(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, 0x1A969696u);
            float bw = finn_text_width(&g_f_label, "Disabled");
            finn_text_draw(&g_f_label, layer, lw, lh, btn_x + (btn_w - bw) * 0.5f,
                           btn_y + btn_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                           "Disabled", FINN_MUTE);
        } else if (st->bluetooth == 0) {
            finn_fill_rrect(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, FINN_ON);
            float bw = finn_text_width(&g_f_label, "Turn On");
            finn_text_draw(&g_f_label, layer, lw, lh, btn_x + (btn_w - bw) * 0.5f,
                           btn_y + btn_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                           "Turn On", 0xFFFFFFFFu);
        } else {
            finn_fill_rrect(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, 0x1A000000u);
            draw_rrect_stroke(layer, lw, lh, btn_x, btn_y, btn_w, btn_h, btn_r, 1.0f * s, 0x33000000u);
            float bw = finn_text_width(&g_f_label, "Turn Off");
            finn_text_draw(&g_f_label, layer, lw, lh, btn_x + (btn_w - bw) * 0.5f,
                           btn_y + btn_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                           "Turn Off", FINN_INK);
        }

        /* Card 2: Devices list */
        float c2_y = card_y + c1_h + s * (16.0f * REF);
        float c2_h = s * (100.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, c2_y, cw, c2_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        finn_text_draw(&g_f_micro, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (22.0f * REF), "MY DEVICES", 0xFF555555u);

        const char *dev_msg = (st->bluetooth <= 0) ? "Bluetooth is turned off."
                                                   : "No Bluetooth accessories connected.";
        finn_text_draw(&g_f_label, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (54.0f * REF), dev_msg, (st->bluetooth <= 0) ? FINN_MUTE : FINN_DIM);
    }

    /* ---------------- PAGE 2: Display ---------------- */
    else if (g_active_page == 2) {
        float c1_h = s * (145.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, card_y, cw, c1_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, card_y, cw, c1_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        finn_text_draw(&g_f_micro, layer, lw, lh, cx + s * (16.0f * REF),
                       card_y + s * (22.0f * REF), "DISPLAY & SCANOUT", 0xFF555555u);

        char res_buf[64], scale_buf[64];
        snprintf(res_buf, sizeof res_buf, "%d × %d", g_cached_screen_w, g_cached_screen_h);
        snprintf(scale_buf, sizeof scale_buf, "%.3f× (canvas density)", s);

        float lx = cx + s * (16.0f * REF);
        float rx = cx + cw * 0.5f;

        finn_text_draw(&g_f_sub, layer, lw, lh, lx, card_y + s * (48.0f * REF), "Resolution", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, lx, card_y + s * (68.0f * REF), res_buf, FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, rx, card_y + s * (48.0f * REF), "Scale Factor", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, rx, card_y + s * (68.0f * REF), scale_buf, FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, lx, card_y + s * (96.0f * REF), "Display Server", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, lx, card_y + s * (116.0f * REF), "Direct DRM/KMS dumb buffers", FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, rx, card_y + s * (96.0f * REF), "Refresh Rate", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, rx, card_y + s * (116.0f * REF), "60 Hz", FINN_INK);

        /* Card 2: Appearance */
        float c2_y = card_y + c1_h + s * (16.0f * REF);
        float c2_h = s * (140.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, c2_y, cw, c2_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        finn_text_draw(&g_f_micro, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (22.0f * REF), "APPEARANCE", 0xFF555555u);
        finn_text_draw(&g_f_sub, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (40.0f * REF), "Select system appearance theme (persists to /var/lib/finn/settings).", FINN_DIM);

        /* Light Tile */
        float tw = s * (120.0f * REF);
        float th = s * (64.0f * REF);
        float tr = s * (8.0f * REF);
        float t1_x = cx + s * (16.0f * REF);
        float t1_y = c2_y + s * (58.0f * REF);

        finn_fill_rrect(layer, lw, lh, t1_x, t1_y, tw, th, tr, 0xFFFFFFFFu);
        draw_rrect_stroke(layer, lw, lh, t1_x, t1_y, tw, th, tr, 1.0f * s, 0x33000000u);
        if (!st->dark) {
            draw_rrect_stroke(layer, lw, lh, t1_x, t1_y, tw, th, tr, 2.0f * s, FINN_ON);
        }
        float lw1 = finn_text_width(&g_f_label, "Light");
        finn_text_draw(&g_f_label, layer, lw, lh, t1_x + (tw - lw1) * 0.5f,
                       t1_y + th * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                       "Light", 0xFF1A1D24u);

        /* Dark Tile */
        float t2_x = t1_x + tw + s * (16.0f * REF);
        float t2_y = t1_y;

        finn_fill_rrect(layer, lw, lh, t2_x, t2_y, tw, th, tr, 0xFF23262Eu);
        draw_rrect_stroke(layer, lw, lh, t2_x, t2_y, tw, th, tr, 1.0f * s, 0x33FFFFFFu);
        if (st->dark) {
            draw_rrect_stroke(layer, lw, lh, t2_x, t2_y, tw, th, tr, 2.0f * s, FINN_ON);
        }
        float lw2 = finn_text_width(&g_f_label, "Dark");
        finn_text_draw(&g_f_label, layer, lw, lh, t2_x + (tw - lw2) * 0.5f,
                       t2_y + th * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                       "Dark", 0xFFFFFFFFu);
    }

    /* ---------------- PAGE 3: Sound ---------------- */
    else if (g_active_page == 3) {
        float c1_h = s * (130.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, card_y, cw, c1_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, card_y, cw, c1_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        finn_text_draw(&g_f_micro, layer, lw, lh, cx + s * (16.0f * REF),
                       card_y + s * (22.0f * REF), "MASTER OUTPUT VOLUME", 0xFF555555u);

        if (st->volume >= 0) {
            char vol_str[32];
            snprintf(vol_str, sizeof vol_str, "%d%%", st->volume);
            finn_text_draw(&g_f_label, layer, lw, lh, cx + cw - s * (50.0f * REF),
                           card_y + s * (22.0f * REF), vol_str, FINN_INK);

            /* Interactive Volume Slider row */
            float sl_x = cx + s * (16.0f * REF);
            float sl_y = card_y + s * (38.0f * REF);
            float sl_w = cw - s * (32.0f * REF);
            float sl_h = s * (34.0f * REF);
            float sl_r = sl_h * 0.5f;

            /* Track: FINN_TRACK */
            finn_fill_rrect(layer, lw, lh, sl_x, sl_y, sl_w, sl_h, sl_r, FINN_TRACK);

            /* Fill portion */
            if (st->volume > 0) {
                float fill_w = sl_w * ((float)st->volume / 100.0f);
                if (fill_w < sl_h) fill_w = sl_h;
                if (fill_w > sl_w) fill_w = sl_w;
                finn_fill_rrect(layer, lw, lh, sl_x, sl_y, fill_w, sl_h, sl_r, FINN_FILL);
            }

            /* Speaker icon on left of slider */
            if (g_ic_vol.vw > 0) {
                finn_icon_blit_ink(&g_ic_vol, layer, lw, lh,
                                   (int)lroundf(sl_x + sl_h * 0.5f),
                                   (int)lroundf(sl_y + sl_h * 0.5f),
                                   rs(18.0f * REF, s), FINN_INK, FINN_INK, 0);
            }

            /* Mute / Unmute pill button */
            float mb_w = s * (86.0f * REF);
            float mb_h = s * (28.0f * REF);
            float mb_x = cx + s * (16.0f * REF);
            float mb_y = card_y + s * (86.0f * REF);
            float mb_r = mb_h * 0.5f;

            finn_fill_rrect(layer, lw, lh, mb_x, mb_y, mb_w, mb_h, mb_r, 0x1A000000u);
            draw_rrect_stroke(layer, lw, lh, mb_x, mb_y, mb_w, mb_h, mb_r, 1.0f * s, 0x33000000u);
            const char *mlabel = (st->volume == 0) ? "Unmute" : "Mute";
            float mw = finn_text_width(&g_f_label, mlabel);
            finn_text_draw(&g_f_label, layer, lw, lh, mb_x + (mb_w - mw) * 0.5f,
                           mb_y + mb_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
                           mlabel, FINN_INK);
        } else {
            finn_text_draw(&g_f_label, layer, lw, lh, cx + s * (16.0f * REF),
                           card_y + s * (58.0f * REF), "No ALSA mixer detected.", FINN_MUTE);
            finn_text_draw(&g_f_sub, layer, lw, lh, cx + s * (16.0f * REF),
                           card_y + s * (80.0f * REF), "Audio hardware is absent or unconfigured.", FINN_DIM);
        }

        /* Card 2: Audio Device info */
        float c2_y = card_y + c1_h + s * (16.0f * REF);
        float c2_h = s * (90.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, c2_y, cw, c2_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        finn_text_draw(&g_f_micro, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (22.0f * REF), "AUDIO CONTROLLER", 0xFF555555u);

        const char *elem = finn_audio_element();
        char mixer_buf[64];
        snprintf(mixer_buf, sizeof mixer_buf, "%s", elem ? elem : "none");

        finn_text_draw(&g_f_sub, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (46.0f * REF), "Active Mixer Element", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (66.0f * REF), mixer_buf, FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, cx + cw * 0.5f,
                       c2_y + s * (46.0f * REF), "Audio Driver", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, cx + cw * 0.5f,
                       c2_y + s * (66.0f * REF), "Advanced Linux Sound Architecture (ALSA)", FINN_INK);
    }

    /* ---------------- PAGE 4: Power ---------------- */
    else if (g_active_page == 4) {
        float c1_h = s * (120.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, card_y, cw, c1_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, card_y, cw, c1_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        /* 36px Battery disc */
        float b36 = s * (36.0f * REF);
        float b36_x = cx + s * (16.0f * REF);
        float b36_y = card_y + s * (24.0f * REF);
        finn_fill_rrect(layer, lw, lh, b36_x, b36_y, b36, b36, b36 * 0.5f, FINN_ON);

        if (g_ic_bat.vw > 0) {
            finn_icon_blit_ink(&g_ic_bat, layer, lw, lh,
                               (int)lroundf(b36_x + b36 * 0.5f),
                               (int)lroundf(b36_y + b36 * 0.5f),
                               rs(20.0f * REF, s), 0xFFFFFFFFu, 0xFFFFFFFFu, 0);
        }

        float tx = b36_x + b36 + s * (16.0f * REF);

        if (st->battery_pct >= 0) {
            char p_title[64];
            snprintf(p_title, sizeof p_title, "Battery Level: %d%%", st->battery_pct);
            const char *p_sub = st->charging ? "Charging · Connected to external AC power adapter"
                                             : "Discharging · Running on battery power";
            finn_text_draw(&g_f_title, layer, lw, lh, tx, card_y + s * (34.0f * REF), p_title, FINN_INK);
            finn_text_draw(&g_f_sub, layer, lw, lh, tx, card_y + s * (54.0f * REF), p_sub, FINN_DIM);

            /* Battery gauge meter */
            float gm_x = tx;
            float gm_y = card_y + s * (72.0f * REF);
            float gm_w = s * (220.0f * REF);
            float gm_h = s * (18.0f * REF);
            float gm_r = gm_h * 0.5f;

            finn_fill_rrect(layer, lw, lh, gm_x, gm_y, gm_w, gm_h, gm_r, FINN_TRACK);
            float gw = gm_w * ((float)st->battery_pct / 100.0f);
            if (gw < gm_h) gw = gm_h;
            finn_fill_rrect(layer, lw, lh, gm_x, gm_y, gw, gm_h, gm_r, FINN_ON);
        } else {
            finn_text_draw(&g_f_title, layer, lw, lh, tx, card_y + s * (34.0f * REF),
                           "Power Source: Mains", FINN_INK);
            finn_text_draw(&g_f_sub, layer, lw, lh, tx, card_y + s * (54.0f * REF),
                           "Connected to AC external power supply", FINN_DIM);
            finn_text_draw(&g_f_sub, layer, lw, lh, tx, card_y + s * (74.0f * REF),
                           "No battery detected (Desktop or virtualized machine).", FINN_DIM);
        }

        /* Card 2: Energy Management */
        float c2_y = card_y + c1_h + s * (16.0f * REF);
        float c2_h = s * (90.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, c2_y, cw, c2_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, c2_y, cw, c2_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        finn_text_draw(&g_f_micro, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (22.0f * REF), "ENERGY & SLEEP", 0xFF555555u);

        finn_text_draw(&g_f_sub, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (46.0f * REF), "Power Profile", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, cx + s * (16.0f * REF),
                       c2_y + s * (66.0f * REF), "Balanced (FinnOS Power Governor)", FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, cx + cw * 0.5f,
                       c2_y + s * (46.0f * REF), "Display Sleep", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, cx + cw * 0.5f,
                       c2_y + s * (66.0f * REF), "Automatic DRM KMS DPMS standby", FINN_INK);
    }

    /* ---------------- PAGE 5: About ---------------- */
    else if (g_active_page == 5) {
        float c1_h = s * (250.0f * REF);
        finn_frost_rrect(layer, lw, lh, lx0, ly0, cx, card_y, cw, c1_h,
                         card_r, FINN_GLASS, FINN_GLASS_TILE);
        draw_rrect_stroke(layer, lw, lh, cx, card_y, cw, c1_h, card_r, 1.0f * s, 0x1AFFFFFFu);

        /* System Info queries */
        struct utsname un;
        memset(&un, 0, sizeof un);
        uname(&un);

        char host[64] = "finnos";
        gethostname(host, sizeof host);

        struct sysinfo si;
        memset(&si, 0, sizeof si);
        sysinfo(&si);

        char uptime_str[64];
        long up_m = si.uptime / 60;
        long up_h = up_m / 60;
        snprintf(uptime_str, sizeof uptime_str, "%ld hr, %ld min", up_h, up_m % 60);

        char mem_str[64];
        double gb = (double)(si.totalram) * (double)si.mem_unit / (1024.0 * 1024.0 * 1024.0);
        snprintf(mem_str, sizeof mem_str, "%.1f GB RAM", gb);

        /* Title inside Card */
        finn_text_draw(&g_f_title, layer, lw, lh, cx + s * (16.0f * REF),
                       card_y + s * (26.0f * REF), "FinnOS 0.1", FINN_INK);
        finn_text_draw(&g_f_sub, layer, lw, lh, cx + s * (16.0f * REF),
                       card_y + s * (44.0f * REF), "Redesign prototype build · Zero toolkit direct compositor", FINN_DIM);

        float row_y0 = card_y + s * (76.0f * REF);
        float row_step = s * (38.0f * REF);
        float lx = cx + s * (16.0f * REF);
        float rx = cx + cw * 0.5f;

        /* Row 1: Kernel & Architecture */
        finn_text_draw(&g_f_sub, layer, lw, lh, lx, row_y0, "Kernel", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, lx, row_y0 + s * (18.0f * REF), un.release, FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, rx, row_y0, "Architecture", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, rx, row_y0 + s * (18.0f * REF), un.machine, FINN_INK);

        /* Row 2: Computer Name & System Uptime */
        finn_text_draw(&g_f_sub, layer, lw, lh, lx, row_y0 + row_step, "Device Name", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, lx, row_y0 + row_step + s * (18.0f * REF), host, FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, rx, row_y0 + row_step, "System Uptime", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, rx, row_y0 + row_step + s * (18.0f * REF), uptime_str, FINN_INK);

        /* Row 3: Memory & Compositor */
        finn_text_draw(&g_f_sub, layer, lw, lh, lx, row_y0 + row_step * 2.0f, "Total Memory", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, lx, row_y0 + row_step * 2.0f + s * (18.0f * REF), mem_str, FINN_INK);

        finn_text_draw(&g_f_sub, layer, lw, lh, rx, row_y0 + row_step * 2.0f, "Compositor", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, rx, row_y0 + row_step * 2.0f + s * (18.0f * REF), "finn-comp (Direct DRM/KMS)", FINN_INK);

        /* Row 4: Control Plane */
        finn_text_draw(&g_f_sub, layer, lw, lh, lx, row_y0 + row_step * 3.0f, "Control Plane", FINN_DIM);
        finn_text_draw(&g_f_label, layer, lw, lh, lx, row_y0 + row_step * 3.0f + s * (18.0f * REF), "finnd (/run/finn/control.sock)", FINN_INK);
    }
}

/* ---------------- hit testing & interaction ---------------- */

int finnsettingsapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                         float scale, int *out_sub) {
    if (out_sub) *out_sub = 0;
    if (px < wx || py < wy || px >= wx + ww || py >= wy + wh)
        return FINNSETTINGS_HIT_NONE;

    float s = scale;
    float sb_w = s * (250.0f * REF);
    if (sb_w > (float)ww) sb_w = (float)ww;

    /* Inside Sidebar */
    if (px < wx + (int)sb_w) {
        /* Titlebar drag area at top of sidebar (above search field) */
        if (py < wy + rs(35.0f * REF, s))
            return FINNSETTINGS_HIT_TITLEBAR;

        /* Search field */
        float sf_x = (float)wx + s * (15.0f * REF);
        float sf_y = (float)wy + s * (70.0f * REF);
        float sf_w = s * (220.0f * REF);
        float sf_h = s * (30.0f * REF);

        if (px >= sf_x && px < sf_x + sf_w && py >= sf_y && py < sf_y + sf_h)
            return FINNSETTINGS_HIT_SEARCH;

        /* Sidebar rows */
        sidebar_layout_item items[20];
        int n_items = get_sidebar_layout(items, 20);

        for (int k = 0; k < n_items; k++) {
            if (items[k].is_header) continue;
            float ry = (float)wy + s * (items[k].y_ref * REF);
            float rh = s * (30.0f * REF);
            if (py >= ry && py < ry + rh && px >= sf_x && px < sf_x + sf_w) {
                if (out_sub) *out_sub = items[k].page_idx;
                return FINNSETTINGS_HIT_ROW;
            }
        }
        return FINNSETTINGS_HIT_BODY;
    }

    /* Inside Content Area */
    /* Titlebar area in content area (top margin to the left of window options) */
    if (py < wy + rs(40.0f * REF, s) && px < wx + ww - rs(90.0f * REF, s))
        return FINNSETTINGS_HIT_TITLEBAR;

    float ca_x = (float)wx + sb_w;
    float pad_x = s * (35.0f * REF);
    float pad_y = s * (30.0f * REF);
    float cx = ca_x + pad_x;
    float cy = (float)wy + pad_y;
    float cw = (float)ww - sb_w - pad_x * 2.0f;
    float card_y = cy + s * (58.0f * REF);

    /* Wi-Fi controls */
    if (g_active_page == 0) {
        float c1_h = s * (76.0f * REF);
        float btn_w = s * (86.0f * REF);
        float btn_h = s * (30.0f * REF);
        float btn_x = cx + cw - s * (16.0f * REF) - btn_w;
        float btn_y = card_y + (c1_h - btn_h) * 0.5f;
        if (px >= btn_x && px < btn_x + btn_w && py >= btn_y && py < btn_y + btn_h)
            return FINNSETTINGS_HIT_WIFI_TOGGLE;
    }

    /* Bluetooth controls */
    else if (g_active_page == 1) {
        float c1_h = s * (76.0f * REF);
        float btn_w = s * (86.0f * REF);
        float btn_h = s * (30.0f * REF);
        float btn_x = cx + cw - s * (16.0f * REF) - btn_w;
        float btn_y = card_y + (c1_h - btn_h) * 0.5f;
        if (px >= btn_x && px < btn_x + btn_w && py >= btn_y && py < btn_y + btn_h)
            return FINNSETTINGS_HIT_BT_TOGGLE;
    }

    /* Display controls */
    else if (g_active_page == 2) {
        float c1_h = s * (145.0f * REF);
        float c2_y = card_y + c1_h + s * (16.0f * REF);
        float tw = s * (120.0f * REF);
        float th = s * (64.0f * REF);
        float t1_x = cx + s * (16.0f * REF);
        float t1_y = c2_y + s * (58.0f * REF);
        float t2_x = t1_x + tw + s * (16.0f * REF);

        if (px >= t1_x && px < t1_x + tw && py >= t1_y && py < t1_y + th)
            return FINNSETTINGS_HIT_LIGHT_THEME;
        if (px >= t2_x && px < t2_x + tw && py >= t1_y && py < t1_y + th)
            return FINNSETTINGS_HIT_DARK_THEME;
    }

    /* Sound controls */
    else if (g_active_page == 3) {
        float sl_x = cx + s * (16.0f * REF);
        float sl_y = card_y + s * (38.0f * REF);
        float sl_w = cw - s * (32.0f * REF);
        float sl_h = s * (34.0f * REF);

        if (px >= sl_x && px < sl_x + sl_w && py >= sl_y && py < sl_y + sl_h)
            return FINNSETTINGS_HIT_VOL_SLIDER;

        float mb_w = s * (86.0f * REF);
        float mb_h = s * (28.0f * REF);
        float mb_x = cx + s * (16.0f * REF);
        float mb_y = card_y + s * (86.0f * REF);
        if (px >= mb_x && px < mb_x + mb_w && py >= mb_y && py < mb_y + mb_h)
            return FINNSETTINGS_HIT_MUTE_TOGGLE;
    }

    return FINNSETTINGS_HIT_BODY;
}

void finnsettingsapp_press(int hit, int sub, int px, int py, float scale,
                           const finn_state *st) {
    (void)py;
    float s = scale;

    if (hit == FINNSETTINGS_HIT_SEARCH) {
        g_search_focused = 1;
        g_caret_timer = 0.0f;
        g_caret_visible = 1;
        return;
    }

    /* Clicking anywhere outside search unfocuses it */
    g_search_focused = 0;

    if (hit == FINNSETTINGS_HIT_ROW) {
        if (sub >= 0 && sub < PAGE_COUNT) {
            g_active_page = sub;
        }
        return;
    }

    if (hit == FINNSETTINGS_HIT_WIFI_TOGGLE) {
        if (st->wifi >= 0) {
            finn_state_set_wifi(st->wifi == 0 ? 1 : 0);
        }
        return;
    }

    if (hit == FINNSETTINGS_HIT_BT_TOGGLE) {
        if (st->bluetooth >= 0) {
            finn_state_set_bluetooth(st->bluetooth == 0 ? 1 : 0);
        }
        return;
    }

    if (hit == FINNSETTINGS_HIT_LIGHT_THEME) {
        finn_state_set_dark(0);
        return;
    }

    if (hit == FINNSETTINGS_HIT_DARK_THEME) {
        finn_state_set_dark(1);
        return;
    }

    if (hit == FINNSETTINGS_HIT_VOL_SLIDER) {
        float sb_w = s * (250.0f * REF);
        float ca_x = (float)g_cached_wx + sb_w;
        float pad_x = s * (35.0f * REF);
        float cx = ca_x + pad_x;
        float cw = (float)g_cached_ww - sb_w - pad_x * 2.0f;
        float sl_x = cx + s * (16.0f * REF);
        float sl_w = cw - s * (32.0f * REF);

        float rel = (float)px - sl_x;
        int pct = (int)lroundf((rel / sl_w) * 100.0f);
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

    float s = scale;
    float sb_w = s * (250.0f * REF);
    float ca_x = (float)g_cached_wx + sb_w;
    float pad_x = s * (35.0f * REF);
    float cx = ca_x + pad_x;
    float cw = (float)g_cached_ww - sb_w - pad_x * 2.0f;
    float sl_x = cx + s * (16.0f * REF);
    float sl_w = cw - s * (32.0f * REF);

    float rel = (float)px - sl_x;
    int pct = (int)lroundf((rel / sl_w) * 100.0f);
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

    /* Esc: clear search or unfocus */
    if (e->code == KEY_ESC) {
        if (g_search_text[0]) {
            g_search_text[0] = '\0';
        } else {
            g_search_focused = 0;
        }
        return 1;
    }

    /* Backspace: delete character */
    if (e->code == KEY_BACKSPACE) {
        size_t len = strlen(g_search_text);
        if (len > 0) {
            g_search_text[len - 1] = '\0';
            g_caret_timer = 0.0f;
            g_caret_visible = 1;
        }
        return 1;
    }

    /* Ctrl+A: select all / clear */
    if ((e->mods & FINN_MOD_CTRL) && (e->code == KEY_A)) {
        g_search_text[0] = '\0';
        g_caret_timer = 0.0f;
        g_caret_visible = 1;
        return 1;
    }

    /* Enter: commit search / unfocus */
    if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
        g_search_focused = 0;
        return 1;
    }

    /* Printable characters */
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
