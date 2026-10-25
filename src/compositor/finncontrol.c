/* finncontrol — the FinnOS Control Center (Figma 9:63 / 9:82). */
#include "finncontrol.h"
#include "finndock.h"
#include "finnblur.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------- tokens ----------------
 * The panel and its tiles are glass, not paint: each is the blurred wallpaper
 * brought toward bone by a shared pass weight. FINN_GLASS_PANEL is 0.65; the
 * second pass is 0.5714286, producing the 0.85 effective tile density in
 * ui/_tokens.fui. finn_frost_rrect applies both passes over the live backdrop.
 */

#define CC_KNOB_INK  0xFF1A1D24u

/* ---------------- metrics, authored for a 1080p canvas ----------------
 *
 * Measurements from Figma Page 1 / Desktop - Control Center / Home at
 * 1280x832, converted to the compositor's 1080p reference with 1.298077
 * (1080 / 832). The 405x500 frame's lower blank region is intentional space
 * for widgets; the status footer is anchored to its bottom.
 *
 *   panel 405x500 at (721,260), radius 8
 *   Wi-Fi tile 188x40 at (10,10); Bluetooth tile 188x40 at (207,10)
 *   Brightness slider 335x35 at (10,60); appearance tile 40x35 at (355,60)
 *   Volume slider 385x35 at (10,105)
 *   Footer 385x40 at (10,450)
 */
#define REF          1.298077f

#define CC_W        (405.0f * REF)
#define CC_H        (500.0f * REF)
#define CC_PAD      (10.0f * REF)
#define CC_R_PANEL  (8.0f * REF)
#define CC_R_TILE   (4.14f * REF)

#define TILE_W      (188.0f * REF)
#define TILE_H      (40.0f * REF)
#define TILE2_X     (207.0f * REF)

#define APPEAR_X    (355.0f * REF)
#define APPEAR_Y    (60.0f * REF)
#define APPEAR_W    (40.0f * REF)
#define APPEAR_H    (35.0f * REF)

#define SLIDER_W    (385.0f * REF)
#define SLIDER_H    (35.0f * REF)
#define SLIDER_BRIGHT_W (335.0f * REF)
#define SLIDER_BRIGHT_Y (60.0f * REF)
#define SLIDER_VOL_Y    (105.0f * REF)
#define SLIDER_ICON_X   (8.0f * REF)
#define SLIDER_GLYPH    (24.0f * REF)

#define STATUS_C        (24.0f * REF)
#define STATUS_INSET    (8.0f * REF)
#define STATUS_GLYPH    (16.0f * REF)
#define STATUS_TEXT_X   (42.0f * REF)

#define FOOTER_X        (10.0f * REF)
#define FOOTER_Y        (450.0f * REF)
#define FOOTER_W        (385.0f * REF)
#define FOOTER_H        (40.0f * REF)
#define FOOTER_R        (4.14f * REF)
#define FOOTER_BAT_X    (9.0f * REF)
#define FOOTER_BAT_SZ   (22.0f * REF)
#define FOOTER_BAT_TXT_X (41.0f * REF)
#define FOOTER_EDIT_X   (328.0f * REF)
#define FOOTER_SET_X    (356.0f * REF)
#define FOOTER_ICO      (18.0f * REF)

#define ANCHOR_GAP      (15.0f * REF)
#define PANEL_W         CC_W
#define PANEL_H         CC_H

/* ---------------- assets + fonts ---------------- */
static fsvg g_ic_wifi, g_ic_bt, g_ic_sun, g_ic_vol, g_ic_bat;
static fsvg g_ic_moon, g_ic_settings;
static int  g_ready = 0;

static finn_font g_f_footer, g_f_label, g_f_label_sm, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];

/* ---------------- layer ---------------- */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx0 = 0, g_ly0 = 0;
static int g_open = 0;
static float g_scale = 1.0f;

/* the tray corner the panel grows out of, in scanout pixels */
static int g_ax = 0, g_ay = 0;

static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_cc_rev = 0;

typedef struct { int x, y, w, h; } cc_rect;
static cc_rect g_r_panel, g_r_wifi, g_r_bt, g_r_appearance, g_r_settings,
               g_r_bright, g_r_vol, g_r_footer;

/* ---------------- motion ---------------- */
static finn_anim g_a_open;                 /* 0 closed .. 1 open  */
static finn_anim g_a_press;                /* 0 up .. 1 held down */
static finn_anim g_a_hover;                /* 0 off .. 1 under the pointer */
static finn_anim g_a_vol, g_a_bright;      /* the level the bars draw */
static float     g_hover = 0.0f;
static int       g_hover_act = FINNCONTROL_NONE;
static int       g_press = FINNCONTROL_NONE;
static int       g_fill_primed = 0;

static int in_rect(const cc_rect *r, int x, int y);
static int rs(float v, float s) { return (int)lroundf(v * s); }

static void fill_circle(uint32_t *L, int lw, int lh, float cx, float cy, float r, uint32_t c) {
    finn_fill_rrect(L, lw, lh, cx - r, cy - r, r * 2.0f, r * 2.0f, r, c);
}

static void control_wash_r(uint32_t *L, int lw, int lh, cc_rect r, float rad,
                           float amount) {
    if (amount <= 0.004f) return;
    unsigned a = (unsigned)(amount * 255.0f + 0.5f);
    if (a > 255) a = 255;
    if (a == 0) return;
    finn_fill_rrect(L, lw, lh, (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                    rad, (FINN_INK & 0x00FFFFFFu) | (a << 24));
}

static void control_wash(uint32_t *L, int lw, int lh, float s, cc_rect r,
                         float amount) {
    control_wash_r(L, lw, lh, r, s * CC_R_TILE, amount);
}

static void press_wash(uint32_t *L, int lw, int lh, float s, cc_rect r,
                       float press) {
    control_wash(L, lw, lh, s, r, FINN_MOTION_PRESS_WASH * press);
}

/* ---------------- tiles ---------------- */

static const finn_font *fit(const char *s, float avail,
                            const finn_font *a, const finn_font *b,
                            const finn_font *c, const finn_font *d) {
    if (finn_text_width(a, s) <= avail) return a;
    if (finn_text_width(b, s) <= avail) return b;
    if (finn_text_width(c, s) <= avail) return c;
    return d;
}

static const finn_font *fit_all(const char *const *names, int n, float avail) {
    const finn_font *ladder[4] = { &g_f_label, &g_f_label_sm, &g_f_sub, &g_f_micro };
    for (int i = 0; i < 4; i++) {
        int ok = 1;
        for (int j = 0; j < n; j++)
            if (finn_text_width(ladder[i], names[j]) > avail) { ok = 0; break; }
        if (ok) return ladder[i];
    }
    return &g_f_micro;
}

static float tile_text_avail(const cc_rect *r, float s) {
    return (float)r->w - s * STATUS_TEXT_X;
}

static void tile_edge(uint32_t *L, int lw, int lh, float s, cc_rect r) {
    /* The hairline is what separates a tile from the panel when both are
     * white in the light appearance. */
    finn_stroke_rrect(L, lw, lh, (float)r.x, (float)r.y, (float)r.w,
                      (float)r.h, s * CC_R_TILE, 1.0f, FINN_HAIR);
}

static void status_tile(uint32_t *L, int lw, int lh, float s, cc_rect r,
                        fsvg *icon, uint32_t vkey, const char *name,
                        const char *detail, int on, const finn_font *lf_set) {
    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                     (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                     s * CC_R_TILE, FINN_GLASS, FINN_GLASS_TILE);
    tile_edge(L, lw, lh, s, r);

    float cr = s * (STATUS_C * 0.5f);
    float ccx = (float)r.x + s * (STATUS_INSET + STATUS_C * 0.5f);
    float ccy = (float)r.y + (float)r.h * 0.5f;
    fill_circle(L, lw, lh, ccx, ccy, cr, on ? FINN_ON : 0xFF969696u);

    finn_icon_blit_ink(icon, L, lw, lh, (int)lroundf(ccx), (int)lroundf(ccy),
                       rs(STATUS_GLYPH, s), 0xFFFFFFFFu, 0xFFFFFFFFu, vkey);

    float tx = (float)r.x + s * STATUS_TEXT_X;
    float avail = (float)r.w - s * STATUS_TEXT_X - s * 6.0f;
    const finn_font *lf = lf_set ? lf_set
                         : fit(name,   avail, &g_f_label, &g_f_label_sm,
                                &g_f_sub, &g_f_micro);
    const finn_font *df = fit(detail, avail, &g_f_sub, &g_f_micro,
                              &g_f_micro, &g_f_micro);

    finn_text_draw(lf, L, lw, lh, tx,
                   (float)(r.y + rs(18.0f * REF, s)), name, FINN_INK);
    finn_text_draw(df, L, lw, lh, tx,
                   (float)(r.y + rs(31.0f * REF, s)), detail, FINN_DIM);
}

static void slider_row(uint32_t *L, int lw, int lh, float s, cc_rect r,
                       fsvg *icon, int fill, int available) {
    float x = (float)r.x, y = (float)r.y, w = (float)r.w, h = (float)r.h;
    float rad = h * 0.5f;

    /* Background track: black 25% */
    finn_fill_rrect(L, lw, lh, x, y, w, h, rad, FINN_TRACK);

    /*
     * Filled bar: the bone fill, rounded where it starts and square where
     * the level currently is. The track carries on to the right of it, so a
     * round cap there would close the bar instead of marking a point on it.
     */
    float fw = 0.0f;
    if (available && fill > 0) {
        fw = w * ((float)fill / 100.0f);
        if (fw > w) fw = w;
        if (fw < h) fw = h;
        finn_fill_bar(L, lw, lh, x, y, fw, h, rad, FINN_FILL);
    }

    /* Icon on the left */
    float ic_cx = x + s * SLIDER_ICON_X + s * (SLIDER_GLYPH * 0.5f);
    float ic_cy = y + h * 0.5f;

    if (available) {
        finn_icon_blit_ink(icon, L, lw, lh,
                           (int)lroundf(ic_cx), (int)lroundf(ic_cy),
                           rs(SLIDER_GLYPH, s), FINN_INK, FINN_INK, 0);
    } else {
        /* When hardware absent (e.g. brightness on VM/desktop with no backlight):
         * Look inert honestly: clean muted text on the track, no white fill, no fake 'n/a' overlap */
        uint32_t inert_col = finn_theme_dark() ? 0x80FFFFFFu : 0xB3FFFFFFu;
        finn_icon_blit_ink(icon, L, lw, lh,
                           (int)lroundf(ic_cx), (int)lroundf(ic_cy),
                           rs(SLIDER_GLYPH, s), inert_col, inert_col, 0);

        float cap = finn_text_cap_height(&g_f_label);
        float tx = x + s * STATUS_TEXT_X;
        finn_text_draw(&g_f_label, L, lw, lh, tx, y + h * 0.5f + cap * 0.45f,
                       "Not available", inert_col);
    }
}

/*
 * The appearance tile.
 *
 * It is a control, not a readout: the whole tile toggles the theme and the
 * switch says which half is current. The disc carries the sun or the moon,
 * so the answer is legible before the label is read.
 */
/*
 * The appearance control sits beside the brightness bar, as a plain icon: a
 * sun in the light appearance and a moon in the dark one. It is a button, so
 * it takes the same hover and press washes as the other tiles — the icon
 * itself is the whole control, and nothing on it pretends to be a readout.
 */
static void appearance_tile(uint32_t *L, int lw, int lh, float s, cc_rect r,
                            int dark) {
    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                     (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                     s * CC_R_TILE, FINN_GLASS, FINN_GLASS_TILE);
    tile_edge(L, lw, lh, s, r);

    fsvg *ic = dark ? &g_ic_moon : &g_ic_sun;
    if (ic->vw > 0)
        finn_icon_blit_ink(ic, L, lw, lh,
                           r.x + r.w / 2, r.y + r.h / 2,
                           rs(18.0f * REF, s), FINN_INK, FINN_INK, 0);
}

static void footer_row(uint32_t *L, int lw, int lh, float s,
                       const finn_state *st, float hover, float press) {
    float fx = (float)g_r_footer.x;
    float fy = (float)g_r_footer.y;
    float fw = (float)g_r_footer.w;
    float fh = (float)g_r_footer.h;
    float cy = fy + fh * 0.5f;

    /* Inset frosted tile */
    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0, fx, fy, fw, fh,
                     s * FOOTER_R, FINN_GLASS, FINN_GLASS_TILE);
    tile_edge(L, lw, lh, s, g_r_footer);

    /*
     * A machine with no battery has no level to report, so its glyph is an
     * empty outline rather than a full one: the footer already says "Mains",
     * and a battery drawn at 100% beside that would be a reading of a thing
     * the machine does not have.
     */
    int have_bat = st->battery_pct >= 0;
    int pct = have_bat ? st->battery_pct : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    if (g_ic_bat.n > 1) {
        g_ic_bat.shapes[1].visible = 1;
        g_ic_bat.shapes[1].clip_x = 3.0f + 14.0f * (float)pct / 100.0f;
    }

    float bat_cx = fx + s * FOOTER_BAT_X + s * (FOOTER_BAT_SZ * 0.5f);
    finn_icon_blit_ink(&g_ic_bat, L, lw, lh,
                       (int)lroundf(bat_cx), (int)lroundf(cy),
                       rs(FOOTER_BAT_SZ, s),
                       have_bat ? FINN_INK : FINN_MUTE,
                       have_bat ? FINN_INK : FINN_MUTE,
                       4000u + (uint32_t)pct * 2u + (st->charging ? 1u : 0u));

    char bat_s[32];
    if (have_bat) snprintf(bat_s, sizeof bat_s, "%d%%", pct);
    else          snprintf(bat_s, sizeof bat_s, "Mains");

    float cap = finn_text_cap_height(&g_f_footer);
    float tx = fx + s * FOOTER_BAT_TXT_X;
    finn_text_draw(&g_f_footer, L, lw, lh, tx, cy + cap * 0.45f, bat_s, FINN_INK);

    /* Settings button: opens Settings app */
    float hit_r = (float)g_r_settings.h * 0.5f;
    if (g_hover_act == FINNCONTROL_SETTINGS)
        control_wash_r(L, lw, lh, g_r_settings, hit_r, FINN_MOTION_HOVER_WASH * hover);
    if (g_press == FINNCONTROL_SETTINGS)
        control_wash_r(L, lw, lh, g_r_settings, hit_r, FINN_MOTION_PRESS_WASH * press);

    finn_icon_blit_ink(&g_ic_settings, L, lw, lh,
                       g_r_settings.x + g_r_settings.w / 2,
                       g_r_settings.y + g_r_settings.h / 2,
                       rs(FOOTER_ICO, s), FINN_INK, FINN_INK, 0);
}

/* ---------------- fonts ---------------- */

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_footer);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_label_sm);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_footer, p, font_px(s, 12.0f * REF)) != 0) {
        fprintf(stderr, "finncontrol: cannot load %s\n", p);
        return -1;
    }
    if (finn_font_load(&g_f_label, p, font_px(s, 12.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_label_sm, p, font_px(s, 11.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, font_px(s, 10.0f * REF)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, font_px(s, 8.5f * REF)) != 0) return -1;
    return 0;
}

int finncontrol_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);

    char p[512];
    int ok = 1;
    snprintf(p, sizeof p, "%s/wifi-02.svg", icon_dir);      ok &= fsvg_load(p, &g_ic_wifi) == 0;
    snprintf(p, sizeof p, "%s/bluetooth.svg", icon_dir);    ok &= fsvg_load(p, &g_ic_bt) == 0;
    snprintf(p, sizeof p, "%s/sun-01.svg", icon_dir);       ok &= fsvg_load(p, &g_ic_sun) == 0;
    snprintf(p, sizeof p, "%s/moon.svg", icon_dir);         ok &= fsvg_load(p, &g_ic_moon) == 0;
    snprintf(p, sizeof p, "%s/volume-02.svg", icon_dir);    ok &= fsvg_load(p, &g_ic_vol) == 0;
    snprintf(p, sizeof p, "%s/battery.svg", icon_dir);      ok &= fsvg_load(p, &g_ic_bat) == 0;
    snprintf(p, sizeof p, "%s/settings-02.svg", icon_dir);  ok &= fsvg_load(p, &g_ic_settings) == 0;

    if (!ok) fprintf(stderr, "finncontrol: icon set incomplete in %s\n", icon_dir);
    g_ready = ok;
    return ok ? 0 : -1;
}

void finncontrol_free(void) {
    fsvg_free(&g_ic_wifi); fsvg_free(&g_ic_bt);
    fsvg_free(&g_ic_sun);  fsvg_free(&g_ic_vol);
    fsvg_free(&g_ic_bat);  fsvg_free(&g_ic_moon); fsvg_free(&g_ic_settings);
    finn_font_free(&g_f_footer);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_label_sm);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_built = 0;
    g_font_scale = -1.0f;
    g_ready = 0;
}

/* ---------------- open / close ---------------- */

void finncontrol_set_open(int open) {
    open = open ? 1 : 0;
    if (open == g_open) return;
    g_open = open;
    fprintf(stderr, "finncontrol: control center %s\n", open ? "open" : "closed");

    if (open) {
        finn_anim_to(&g_a_open, 1.0f, FINN_MOTION_OPEN, 0.0f, FINN_EASE_OUT_BACK);
    } else {
        finn_anim_to(&g_a_open, 0.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
        g_press = FINNCONTROL_NONE;
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    }
}

int finncontrol_is_open(void) { return g_open; }
int finncontrol_visible(void) { return finn_anim_v(&g_a_open) > 0.004f; }
float finncontrol_openness(void) { return finn_anim_v(&g_a_open); }

int finncontrol_settled(void) {
    return !finn_anim_running(&g_a_open) && finn_anim_v(&g_a_open) >= 0.999f;
}

void finncontrol_press(int act) {
    g_press = act;
    if (act == FINNCONTROL_NONE)
        finn_anim_to(&g_a_press, 0.0f, FINN_MOTION_PRESS_OUT, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_press, 1.0f, FINN_MOTION_PRESS_IN, 0.0f, FINN_EASE_OUT);
}

void finncontrol_hover_at(int px, int py) {
    int act = FINNCONTROL_NONE;

    if (g_open && g_layer && finn_anim_v(&g_a_open) >= 0.90f) {
        int x = px - g_lx0, y = py - g_ly0;
        if (x >= 0 && y >= 0 && x < g_lw && y < g_lh) {
            /* Wi-Fi and Bluetooth only take hover when hardware adapter exists */
            if (in_rect(&g_r_wifi, x, y) && g_r_wifi.w > 0) {
                act = FINNCONTROL_WIFI;
            } else if (in_rect(&g_r_bt, x, y) && g_r_bt.w > 0) {
                act = FINNCONTROL_BLUETOOTH;
            } else if (in_rect(&g_r_appearance, x, y)) {
                act = FINNCONTROL_APPEARANCE;
            } else if (in_rect(&g_r_settings, x, y)) {
                act = FINNCONTROL_SETTINGS;
            }
        }
    }

    if (act == g_hover_act) return;
    g_hover_act = act;
    if (act != FINNCONTROL_NONE)
        finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_hover, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
}

int finncontrol_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_press, dt);
    ch |= finn_anim_tick(&g_a_hover, dt);
    ch |= finn_anim_tick(&g_a_vol, dt);
    ch |= finn_anim_tick(&g_a_bright, dt);
    float hv = finn_anim_v(&g_a_hover);
    if (hv != g_hover) { g_hover = hv; ch = 1; }
    return ch;
}

int finncontrol_running(void) {
    return finn_anim_running(&g_a_open) || finn_anim_running(&g_a_press) ||
           finn_anim_running(&g_a_hover) ||
           finn_anim_running(&g_a_vol)  || finn_anim_running(&g_a_bright);
}

static void follow(finn_anim *a, int value) {
    if (value < 0) return;
    finn_anim_to(a, (float)value, FINN_MOTION_FILL, 0.0f, FINN_EASE_OUT);
}

/* ---------------- render ---------------- */

static uint32_t build_key(float s, int pw, int ph,
                          int dx, int dy, int dw, int dh,
                          int tx, int ty, int tw, int th,
                          const finn_state *st) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(s * 1000.0f)); MIX(pw); MIX(ph);
    MIX(g_lx0); MIX(g_ly0);
    MIX(dx); MIX(dy); MIX(dw); MIX(dh);
    MIX(tx); MIX(ty); MIX(tw); MIX(th);
    MIX(st->battery_pct + 2); MIX(st->charging + 2);
    MIX(st->volume + 2);      MIX(st->brightness + 2);
    MIX(st->wifi + 2);        MIX(st->bluetooth + 2); MIX(st->wired + 2);
    MIX(st->dark + 2);
    MIX(g_press);
    MIX((int)(finn_anim_v(&g_a_press) * 512.0f));
    MIX((int)(finn_anim_v(&g_a_hover) * 64.0f));
    MIX(g_hover_act);
    MIX((int)(finn_anim_v(&g_a_vol) * 8.0f));
    MIX((int)(finn_anim_v(&g_a_bright) * 8.0f));
    /* the glass is the desktop under it: when that moves, this redraws */
    MIX(finnblur_generation());
    #undef MIX
    return h ? h : 1u;
}

void finncontrol_render(int screen_w, int screen_h, float scale,
                   int dock_x, int dock_y, int dock_w, int dock_h,
                   const finn_state *st) {
    if (!g_ready) return;

    float s = scale;
    g_scale = s;

    if (g_font_scale != s) {
        if (load_fonts(s) != 0) return;
        g_font_scale = s;
    }

    int pw = rs(PANEL_W, s), ph = rs(PANEL_H, s);

    int tx = 0, ty = 0, tw = 0, th = 0;
    finndock_tray(&tx, &ty, &tw, &th);
    if (tw <= 0 || th <= 0) {
        tx = dock_x; ty = dock_y; tw = dock_w; th = dock_h;
    }

    /* Anchor horizontally centered directly over Pill 4 (Status pill) */
    int px0 = tx + (tw - pw) / 2;
    int py0 = ty - rs(ANCHOR_GAP, s) - ph;

    int margin = rs(12, s);
    if (px0 < margin) px0 = margin;
    if (py0 < margin) py0 = margin;
    if (px0 + pw > screen_w - margin) px0 = screen_w - margin - pw;
    if (py0 + ph > screen_h - margin) py0 = screen_h - margin - ph;

    g_lx0 = px0;
    g_ly0 = py0;

    g_ax = tx + tw / 2;
    g_ay = ty;

    g_r_panel.x = 0; g_r_panel.y = 0; g_r_panel.w = pw; g_r_panel.h = ph;

    g_r_wifi.x = rs(CC_PAD, s);
    g_r_wifi.y = rs(CC_PAD, s);
    g_r_wifi.w = rs(TILE_W, s);
    g_r_wifi.h = rs(TILE_H, s);

    g_r_bt.x   = rs(TILE2_X, s);
    g_r_bt.y   = rs(CC_PAD, s);
    g_r_bt.w   = rs(TILE_W, s);
    g_r_bt.h   = rs(TILE_H, s);

    g_r_appearance.x = rs(APPEAR_X, s);
    g_r_appearance.y = rs(APPEAR_Y, s);
    g_r_appearance.w = rs(APPEAR_W, s);
    g_r_appearance.h = rs(APPEAR_H, s);

    g_r_bright.x = rs(CC_PAD, s);
    g_r_bright.y = rs(SLIDER_BRIGHT_Y, s);
    g_r_bright.w = rs(SLIDER_BRIGHT_W, s);
    g_r_bright.h = rs(SLIDER_H, s);

    g_r_vol.x    = rs(CC_PAD, s);
    g_r_vol.y    = rs(SLIDER_VOL_Y, s);
    g_r_vol.w    = rs(SLIDER_W, s);
    g_r_vol.h    = rs(SLIDER_H, s);

    g_r_footer.x = rs(FOOTER_X, s);
    g_r_footer.y = rs(FOOTER_Y, s);
    g_r_footer.w = rs(FOOTER_W, s);
    g_r_footer.h = rs(FOOTER_H, s);

    int hit = rs(30.0f * REF, s);
    g_r_settings.x = rs(FOOTER_X + FOOTER_SET_X + FOOTER_ICO * 0.5f, s) - hit / 2;
    g_r_settings.y = rs(FOOTER_Y + FOOTER_H * 0.5f, s) - hit / 2;
    g_r_settings.w = hit;
    g_r_settings.h = hit;

    if (!g_fill_primed) {
        finn_anim_jump(&g_a_vol, st->volume >= 0 ? (float)st->volume : 0.0f);
        finn_anim_jump(&g_a_bright, st->brightness >= 0 ? (float)st->brightness : 0.0f);
        g_fill_primed = 1;
    }
    follow(&g_a_vol, st->volume);
    follow(&g_a_bright, st->brightness);
    int fill_vol = (int)(finn_anim_v(&g_a_vol) + 0.5f);
    int fill_bright = (int)(finn_anim_v(&g_a_bright) + 0.5f);

    uint32_t key = build_key(s, pw, ph, dock_x, dock_y, dock_w, dock_h,
                             tx, ty, tw, th, st);
    if (g_built && key == g_build_key) return;
    g_build_key = key;
    g_built = 1;
    g_cc_rev++;

    int lw = pw, lh = ph;
    if (lw != g_lw || lh != g_lh) {
        free(g_layer);
        g_layer = malloc((size_t)lw * lh * 4);
        g_lw = lw; g_lh = lh;
    }
    if (!g_layer) return;
    memset(g_layer, 0, (size_t)lw * lh * 4);

    finn_frost_rrect(g_layer, lw, lh, g_lx0, g_ly0,
                     0.0f, 0.0f, (float)pw, (float)ph,
                     s * CC_R_PANEL, FINN_GLASS, FINN_GLASS_PANEL);
    finn_stroke_rrect(g_layer, lw, lh, 0.0f, 0.0f, (float)pw, (float)ph,
                      s * CC_R_PANEL, 1.0f, FINN_HAIR);

    float press = finn_anim_v(&g_a_press);
    float hover = g_hover;

    int wifi_ok = st->wifi >= 0;
    int bt_ok   = st->bluetooth >= 0;

    const char *labels[2] = { "Wi-Fi", "Bluetooth" };
    const finn_font *lf = fit_all(labels, 2, tile_text_avail(&g_r_wifi, s));

    if (g_ic_wifi.n > 0) {
        fshape *sh = &g_ic_wifi.shapes[0];
        for (int i = 0; i < sh->npolys; i++) {
            sh->polys[i].vis = 1;
            sh->polys[i].col = 0;
        }
    }

    /* Honest Wi-Fi state: never label it "Wired" with a wifi icon */
    const char *wifi_detail = "Not available";
    if (wifi_ok) {
        wifi_detail = (st->wifi > 0) ? "Connected" : "Off";
    }
    status_tile(g_layer, lw, lh, s, g_r_wifi, &g_ic_wifi, 0, "Wi-Fi",
                wifi_detail, wifi_ok && st->wifi > 0, lf);

    if (wifi_ok) {
        if (g_hover_act == FINNCONTROL_WIFI)
            control_wash(g_layer, lw, lh, s, g_r_wifi,
                         FINN_MOTION_HOVER_WASH * hover);
        if (g_press == FINNCONTROL_WIFI)
            press_wash(g_layer, lw, lh, s, g_r_wifi, press);
    }

    /* Honest Bluetooth state */
    const char *bt_detail = "Not available";
    if (bt_ok) {
        bt_detail = (st->bluetooth > 0) ? "On" : "Off";
    }
    status_tile(g_layer, lw, lh, s, g_r_bt, &g_ic_bt, 0, "Bluetooth",
                bt_detail, bt_ok && st->bluetooth > 0, lf);

    if (bt_ok) {
        if (g_hover_act == FINNCONTROL_BLUETOOTH)
            control_wash(g_layer, lw, lh, s, g_r_bt,
                         FINN_MOTION_HOVER_WASH * hover);
        if (g_press == FINNCONTROL_BLUETOOTH)
            press_wash(g_layer, lw, lh, s, g_r_bt, press);
    }

    appearance_tile(g_layer, lw, lh, s, g_r_appearance, st->dark);
    if (g_hover_act == FINNCONTROL_APPEARANCE)
        control_wash(g_layer, lw, lh, s, g_r_appearance,
                     FINN_MOTION_HOVER_WASH * hover);
    if (g_press == FINNCONTROL_APPEARANCE)
        press_wash(g_layer, lw, lh, s, g_r_appearance, press);

    slider_row(g_layer, lw, lh, s, g_r_bright,
               &g_ic_sun, fill_bright, st->brightness >= 0);
    slider_row(g_layer, lw, lh, s, g_r_vol,
               &g_ic_vol, fill_vol, st->volume >= 0);

    footer_row(g_layer, lw, lh, s, st, hover, press);
}

void finncontrol_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finncontrol_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

void finncontrol_transform(float *x, float *y, float *w, float *h, float *alpha) {
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

void finncontrol_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_layer) return;
    float x, y, w, h, alpha;
    finncontrol_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_scaled(dst, dw, dh, g_layer, g_lw, g_lh, x, y, w, h, alpha);
}

const uint32_t *finncontrol_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx0;
    if (y) *y = g_ly0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_cc_rev;
    return finncontrol_visible() ? g_layer : NULL;
}

/* ---------------- input ---------------- */

static int in_rect(const cc_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static float slider_frac(const cc_rect *r, int x, float s) {
    (void)s;
    float bx = (float)r->x;
    float bw = (float)r->w;
    if (bw <= 0.0f) return 0.0f;
    float f = ((float)x - bx) / bw;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

int finncontrol_hit(int px, int py, float *out01) {
    if (!g_open || !g_layer || finncontrol_visible() == 0) return FINNCONTROL_NONE;
    if (finn_anim_v(&g_a_open) < 0.90f) return FINNCONTROL_NONE;

    int x = px - g_lx0, y = py - g_ly0;
    if (x < 0 || y < 0 || x >= g_lw || y >= g_lh) return FINNCONTROL_NONE;
    if (out01) *out01 = -1.0f;

    if (in_rect(&g_r_wifi, x, y))     return FINNCONTROL_WIFI;
    if (in_rect(&g_r_bt, x, y))       return FINNCONTROL_BLUETOOTH;
    if (in_rect(&g_r_appearance, x, y)) return FINNCONTROL_APPEARANCE;
    if (in_rect(&g_r_settings, x, y)) return FINNCONTROL_SETTINGS;
    if (in_rect(&g_r_bright, x, y)) {
        if (out01) *out01 = slider_frac(&g_r_bright, x, g_scale);
        return FINNCONTROL_BRIGHTNESS;
    }
    if (in_rect(&g_r_vol, x, y)) {
        if (out01) *out01 = slider_frac(&g_r_vol, x, g_scale);
        return FINNCONTROL_VOLUME;
    }
    return in_rect(&g_r_panel, x, y) ? FINNCONTROL_PANEL : FINNCONTROL_NONE;
}

float finncontrol_drag(int action, int px) {
    if (action == FINNCONTROL_BRIGHTNESS) return slider_frac(&g_r_bright, px - g_lx0, g_scale);
    if (action == FINNCONTROL_VOLUME)     return slider_frac(&g_r_vol, px - g_lx0, g_scale);
    return -1.0f;
}
