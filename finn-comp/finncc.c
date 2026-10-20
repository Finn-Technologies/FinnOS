/* finncc — the FinnOS Control Center. */
#include "finncc.h"
#include "finndock.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------- tokens ----------------
 * The panel and its tiles are glass, not paint: each is the blurred wallpaper
 * brought toward bone by a fixed weight. The two weights are the ones the
 * reference measures — 0.70 for the panel, 0.50 for a tile laid over it.
 * finn_frost_rrect mixes toward the tint over whatever the layer already
 * carries, so the tile's second pass compounds to the lighter inset the
 * reference shows, and the pair re-grades together when Appearance flips.
 */
#define CC_GLASS       0.70f
#define CC_TILE_GLASS  0.50f

/*
 * The glyph on a knob-white circle is a fixed dark ink. The circle is the
 * reference's white control in both appearances, so it cannot use FINN_INK:
 * that flips to near-white in dark mode and would disappear into the circle.
 * The status circles are the other way round — they use the light FINN_KNOB
 * glyph over an accent or grey disc — so one token cannot serve both.
 */
#define CC_KNOB_INK  0xFF1A1D24u

/* ---------------- metrics, authored for a 1080p canvas ----------------
 *
 * Every number here is the attachment's own, divided by 0.770 — the density
 * its 1280x832 frame implies against this canvas. Measured off the reference:
 *   panel 260x260 r16, pad 8, tiles 116x36 r7.5, tile gap 12,
 *   status disc 24 inset 5, two slider rows 244x30 r15 eight apart,
 *   slider thumb 37 wide (the row's own left cap), bars filled to 100%.
 * Rounding to whole authored units keeps the panel exactly square to within
 * a pixel of the reference.
 */
/*
 * 260 reference pixels, plus the 4% the longest real label needs.
 *
 * The reference's own labels are short ("Wi-Fi"), so its tile fits a title at
 * the reference's size. "Wired Connection" is 156px at 18 and the reference's
 * insets leave 149.7 in a 260 panel, which is what had been forcing the whole
 * panel a size down — one shared size is the point, so the panel gives the 4%
 * instead and every other number here keeps the reference's proportion.
 */
#define CC_W        351.0f  /* panel width  (reference 260 + 4%) */
#define CC_PAD       10.4f  /* panel padding (reference 8) */
#define CC_GAP       10.4f  /* row gap      (reference 8) */
#define CC_R_PANEL   20.8f  /* reference 16 */
#define CC_R_TILE     9.7f  /* reference 7.5 */
#define TILE_H       46.7f  /* the two-up status tile rows (reference 36) */
#define ROW_H        38.9f  /* a slider row: a pill, so its radius is ROW_H/2 */
#define THUMB_W      48.0f  /* the slider's white left cap (reference 37) */
#define TILE_GAP     15.6f  /* between the two tiles of a row (reference 12) */
#define STATUS_C     31.2f  /* the status tiles' filled disc (reference 24) */
#define ANCHOR_GAP   19.5f  /* panel to dock: reference 15 */
#define HALF_W      ((CC_W - CC_PAD * 2.0f - TILE_GAP) * 0.5f)   /* 150.6 */

/* the tile's inner rhythm: disc inset, glyph size, and the gap to the text.
 * The disc sits 6 reference pixels in and the label only 5 off its edge, so
 * the pair reads as one unit rather than as two things in a box.
 *
 * Glyph sizes are ink, not viewBox (finn_icon_frame): the assets fill their
 * boxes by anything from a half to all of it, so equal boxes gave the disc a
 * glyph noticeably smaller than the slider's. These are the reference's own
 * ink sizes — 14 reference pixels on a disc, 16 in a knob. */
#define STATUS_INSET     7.8f
#define STATUS_GLYPH    18.0f   /* ink 14 */
#define SLIDER_GLYPH     21.0f  /* ink 16 */
#define STATUS_TEXT_GAP  6.5f
#define STATUS_TEXT_PAD  4.0f

#define PANEL_W CC_W
/*
 * The attachment is square — 260x260 with its content stacked at the top and
 * the rest left open, because that open half is where macOS puts widgets and
 * this shell has none yet. Keeping the panel square is what makes the shape
 * read as the reference rather than as a box that hugged its contents, so the
 * height is a constant and the empty strip below the controls is honest.
 */
#define PANEL_H 351.0f

/* motion, authored the same way */
#define OPEN_DUR     0.28f
#define CLOSE_DUR    0.18f
#define OPEN_SCALE   0.90f   /* the panel starts at 90% and grows to 1 */
#define PRESS_WASH   0.090f  /* the same wash the dock uses, so the two agree */
#define PRESS_IN_DUR 0.08f
#define PRESS_OUT_DUR 0.16f
#define FILL_DUR     0.20f

/* ---------------- assets + fonts ---------------- */
static fsvg g_ic_wifi, g_ic_bt, g_ic_eth, g_ic_sun, g_ic_moon, g_ic_vol, g_ic_bat;
static int  g_ready = 0;

/* One weight, four sizes. The panel sets no bold: hierarchy comes from size
 * and from ink against dim, which is how the reference's own labels separate
 * from their detail lines without going heavy. */
static finn_font g_f_label, g_f_label_sm, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static char  g_font_dir[512];

/* ---------------- layer ---------------- */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx0 = 0, g_ly0 = 0;
static int g_open = 0;
static float g_scale = 1.0f;

/* the tray corner the panel grows out of, in scanout pixels */
static int g_ax = 0, g_ay = 0;

/* the layer's identity: everything above that changes a pixel goes in here,
 * so the panel can keep its raster while only its transform animates */
static uint32_t g_build_key = 0;
static int      g_built = 0;
static unsigned g_cc_rev = 0;   /* moves only when the raster is redrawn */

typedef struct { int x, y, w, h; } cc_rect;
static cc_rect g_r_panel, g_r_wifi, g_r_bt, g_r_app,
               g_r_bright, g_r_vol, g_r_power;

/* ---------------- motion ---------------- */
static finn_anim g_a_open;                 /* 0 closed .. 1 open  */
static finn_anim g_a_press;                /* 0 up .. 1 held down */
static finn_anim g_a_vol, g_a_bright;      /* the level the bars draw */
static int      g_press = FINNCC_NONE;
/* 1 while the connection slot is showing the wired link instead of a radio */
static int      g_conn_wired = 0;
static int      g_fill_primed = 0;

static int rs(float v, float s) { return (int)lroundf(v * s); }

static void text_r(uint32_t *L, int lw, int lh, const finn_font *f,
                   float right, float base, const char *s, uint32_t col) {
    finn_text_draw(f, L, lw, lh, right - finn_text_width(f, s), base, s, col);
}

static void fill_circle(uint32_t *L, int lw, int lh, float cx, float cy, float r, uint32_t c) {
    finn_fill_rrect(L, lw, lh, cx - r, cy - r, r * 2.0f, r * 2.0f, r, c);
}

/*
 * A pressed control answers with a wash, not a shrink: the tile keeps its
 * exact box and darkens under the finger. Nothing in this shell changes size
 * when it is clicked — a control that shrinks reads as flinching away.
 */
static void press_wash(uint32_t *L, int lw, int lh, int s, cc_rect r,
                       float press) {
    if (press <= 0.004f) return;
    unsigned a = (unsigned)(PRESS_WASH * press * 255.0f + 0.5f);
    if (a > 255) a = 255;
    if (a == 0) return;
    finn_fill_rrect(L, lw, lh, (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                    s * CC_R_TILE, (FINN_INK & 0x00FFFFFFu) | (a << 24));
}

/* ---------------- tiles ---------------- */

/*
 * The largest face in the ladder whose width still leaves the tile's right
 * padding intact. Nothing in this panel is clipped and nothing overruns its
 * box: a name too long for its tile steps down a size instead, which is the
 * only reason the same weight is loaded four times.
 */
static const finn_font *fit(const char *s, float avail,
                            const finn_font *a, const finn_font *b,
                            const finn_font *c, const finn_font *d) {
    if (finn_text_width(a, s) <= avail) return a;
    if (finn_text_width(b, s) <= avail) return b;
    if (finn_text_width(c, s) <= avail) return c;
    return d;
}

/* One size for every label in the panel. The tiles are repeated units, so a
 * tile that shrank on its own would read as a different kind of tile; the
 * longest name in the set instead sets the size for all of them. */
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

/* the width a label has once the disc, its inset and the tile's padding are
 * taken out — the budget every string in a tile has to live inside */
static float tile_text_avail(const cc_rect *r, float s) {
    return (float)r->w -
           s * (STATUS_INSET + STATUS_C + STATUS_TEXT_GAP + STATUS_TEXT_PAD);
}

/*
 * A status tile is the reference's unit: a lighter frosted tile, a filled
 * circle carrying the glyph, then a label with a grey detail under it. Size
 * and ink carry the hierarchy; the label is the same weight as the detail.
 * The circle is the state — accent when the control is on, grey when it is
 * off or the hardware is absent — while the label and detail keep their
 * colours so "No adapter" reads as information rather than as damage.
 */
static void status_tile(uint32_t *L, int lw, int lh, float s, cc_rect r,
                        fsvg *icon, uint32_t vkey, const char *name,
                        const char *detail, int on, const finn_font *lf_set) {
    finn_frost_rrect(L, lw, lh, g_lx0, g_ly0,
                     (float)r.x, (float)r.y, (float)r.w, (float)r.h,
                     s * CC_R_TILE, FINN_GLASS, CC_TILE_GLASS);

    float cr = s * (STATUS_C * 0.5f);
    float ccx = (float)r.x + s * (STATUS_INSET + STATUS_C * 0.5f);
    float ccy = (float)r.y + (float)r.h * 0.5f;
    fill_circle(L, lw, lh, ccx, ccy, cr, on ? FINN_ON : FINN_MUTE);

    finn_icon_blit_ink(icon, L, lw, lh, (int)lroundf(ccx), (int)lroundf(ccy),
                       rs(STATUS_GLYPH, s), FINN_KNOB, FINN_KNOB, vkey);

    float tx = (float)r.x + s * (STATUS_INSET + STATUS_C + STATUS_TEXT_GAP);
    float avail = tile_text_avail(&r, s);
    const finn_font *lf = lf_set ? lf_set
                         : fit(name,   avail, &g_f_label, &g_f_label_sm,
                               &g_f_sub, &g_f_micro);
    const finn_font *df = fit(detail, avail, &g_f_sub, &g_f_micro,
                              &g_f_micro, &g_f_micro);

    finn_text_draw(lf, L, lw, lh, tx,
                   (float)(r.y + rs(21, s)), name, FINN_INK);
    finn_text_draw(df, L, lw, lh, tx,
                   (float)(r.y + rs(35, s)), detail, FINN_DIM);
}

/*
 * A slider row is one pill, not a disc with a track beside it. Its left
 * THUMB_W is white — the row's own left cap, which is why the value fill in
 * the reference emerges from under it with a square edge instead of a curve.
 * The value is drawn from the row's left edge so the fill's round cap stays
 * hidden under the thumb; nothing is drawn at all when there is no level to
 * draw, so an absent control is a bare track with "n/a", never a fake 100%.
 */
static void slider_row(uint32_t *L, int lw, int lh, float s, cc_rect r,
                       fsvg *icon, int fill, int available) {
    float x = (float)r.x, y = (float)r.y, w = (float)r.w, h = (float)r.h;
    float rad = h * 0.5f;
    float tw = s * THUMB_W;

    /* the unfilled part is the fill colour held back, not a second grey: a
     * fifth of it over the glass reads as "this much is not in use" and
     * inherits the appearance without a track token of its own. */
    finn_fill_rrect(L, lw, lh, x, y, w, h, rad,
                    (FINN_FILL & 0x00FFFFFFu) | 0x38000000u);

    if (available && fill > 0)
        finn_fill_rrect(L, lw, lh, x, y,
                        tw + (w - tw) * (float)fill / 100.0f, h, rad, FINN_FILL);

    finn_fill_rrect(L, lw, lh, x, y, tw, h, rad, FINN_KNOB);
    /* The thumb is the row's own left cap, so its right edge is a straight
     * cut where the value emerges from under it. A full pill would bulge a
     * second round cap into the fill and the row would read as a ball on a
     * stick instead of a well with a level behind it. */
    finn_fill_rrect(L, lw, lh, x + tw - rad, y, rad, h, 0.0f, FINN_KNOB);

    uint32_t gc = available ? CC_KNOB_INK : FINN_MUTE;
    finn_icon_blit_ink(icon, L, lw, lh,
                       (int)lroundf(x + tw * 0.5f),
                       (int)lroundf(y + h * 0.5f),
                       rs(SLIDER_GLYPH, s), gc, gc, 0);

    if (!available) {
        float cap = finn_text_cap_height(&g_f_sub);
        text_r(L, lw, lh, &g_f_sub, x + w - s * 14.0f,
               y + h * 0.5f + cap * 0.5f, "n/a", FINN_MUTE);
    }
}

/*
 * Power is a gauge, not a toggle, but it sits in the tile grid with the same
 * disc and the same two lines: the disc is accent while the machine is on
 * mains and grey on battery, and the charge level is the battery glyph's own
 * clipped bar, so the percentage is drawn twice from the same reading.
 */
static void power_tile(uint32_t *L, int lw, int lh, float s, cc_rect r,
                       const finn_state *st, const finn_font *lf) {
    int have = st->battery_pct >= 0;
    int pct = have ? st->battery_pct : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    g_ic_bat.shapes[1].visible = have ? 1 : 0;
    g_ic_bat.shapes[1].clip_x = 4.0f + 14.0f * (float)pct / 100.0f;
    if (g_ic_bat.n > 2) g_ic_bat.shapes[2].visible = 0;

    char v[48];
    if (have)
        snprintf(v, sizeof v, "%d%%  %s", pct,
                 st->charging ? "Charging" : "On battery");
    else
        snprintf(v, sizeof v, "Mains");

    status_tile(L, lw, lh, s, r, &g_ic_bat,
                4000u + (uint32_t)pct * 2u + (st->charging ? 1u : 0u),
                "Power", v, st->charging && have, lf);
}

/* ---------------- fonts ---------------- */

static int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

static int load_fonts(float s) {
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_label_sm);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_label, p, font_px(s, 12.0f)) != 0) {
        fprintf(stderr, "finncc: cannot load %s\n", p);
        return -1;
    }
    /* the same weight one step down, only reached by a name that cannot fit */
    if (finn_font_load(&g_f_label_sm, p, font_px(s, 11.0f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, font_px(s, 10.0f)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, font_px(s, 8.5f)) != 0) return -1;
    return 0;
}

int finncc_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);

    char p[512];
    int ok = 1;
    snprintf(p, sizeof p, "%s/wifi.svg", icon_dir);      ok &= fsvg_load(p, &g_ic_wifi) == 0;
    snprintf(p, sizeof p, "%s/bluetooth.svg", icon_dir); ok &= fsvg_load(p, &g_ic_bt) == 0;
    snprintf(p, sizeof p, "%s/ethernet.svg", icon_dir);  ok &= fsvg_load(p, &g_ic_eth) == 0;
    snprintf(p, sizeof p, "%s/sun.svg", icon_dir);       ok &= fsvg_load(p, &g_ic_sun) == 0;
    snprintf(p, sizeof p, "%s/moon.svg", icon_dir);      ok &= fsvg_load(p, &g_ic_moon) == 0;
    snprintf(p, sizeof p, "%s/volume.svg", icon_dir);    ok &= fsvg_load(p, &g_ic_vol) == 0;
    snprintf(p, sizeof p, "%s/battery.svg", icon_dir);   ok &= fsvg_load(p, &g_ic_bat) == 0;

    if (!ok) fprintf(stderr, "finncc: icon set incomplete in %s\n", icon_dir);
    g_ready = ok;
    return ok ? 0 : -1;
}

void finncc_free(void) {
    fsvg_free(&g_ic_wifi); fsvg_free(&g_ic_bt);  fsvg_free(&g_ic_eth);
    fsvg_free(&g_ic_sun);  fsvg_free(&g_ic_moon); fsvg_free(&g_ic_vol);
    fsvg_free(&g_ic_bat);
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

void finncc_set_open(int open) {
    open = open ? 1 : 0;
    if (open == g_open) return;
    g_open = open;
    fprintf(stderr, "finncc: control center %s\n", open ? "open" : "closed");

    if (open) {
        /* a small overshoot on the way out reads as the panel being flung
         * open rather than fading in */
        finn_anim_to(&g_a_open, 1.0f, OPEN_DUR, 0.0f, FINN_EASE_OUT_BACK);
    } else {
        finn_anim_to(&g_a_open, 0.0f, CLOSE_DUR, 0.0f, FINN_EASE_IN_OUT);
        g_press = FINNCC_NONE;
        finn_anim_to(&g_a_press, 0.0f, PRESS_OUT_DUR, 0.0f, FINN_EASE_OUT);
    }
}

int finncc_is_open(void) { return g_open; }
int finncc_visible(void) { return finn_anim_v(&g_a_open) > 0.004f; }
float finncc_openness(void) { return finn_anim_v(&g_a_open); }

int finncc_settled(void) {
    return !finn_anim_running(&g_a_open) && finn_anim_v(&g_a_open) >= 0.999f;
}

void finncc_press(int act) {
    g_press = act;
    if (act == FINNCC_NONE)
        finn_anim_to(&g_a_press, 0.0f, PRESS_OUT_DUR, 0.0f, FINN_EASE_OUT);
    else
        finn_anim_to(&g_a_press, 1.0f, PRESS_IN_DUR, 0.0f, FINN_EASE_OUT);
}

int finncc_tick(float dt) {
    int ch = finn_anim_tick(&g_a_open, dt);
    ch |= finn_anim_tick(&g_a_press, dt);
    ch |= finn_anim_tick(&g_a_vol, dt);
    ch |= finn_anim_tick(&g_a_bright, dt);
    return ch;
}

int finncc_running(void) {
    return finn_anim_running(&g_a_open) || finn_anim_running(&g_a_press) ||
           finn_anim_running(&g_a_vol)  || finn_anim_running(&g_a_bright);
}

/* Follow the hardware, but only re-aim when the target actually moves, so
 * calling this on every rebuild does not restart a sweep that is under way. */
static void follow(finn_anim *a, int value) {
    if (value < 0) return;
    finn_anim_to(a, (float)value, FILL_DUR, 0.0f, FINN_EASE_OUT);
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
    MIX((int)(finn_anim_v(&g_a_vol) * 8.0f));
    MIX((int)(finn_anim_v(&g_a_bright) * 8.0f));
    #undef MIX
    return h ? h : 1u;
}

void finncc_render(int screen_w, int screen_h, float scale,
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

    /*
     * The reference sets the panel's left edge on the tray pill's left edge
     * (680 vs 681: flush) and its bottom one gap above the dock's top. The
     * pill's rect has to come from the dock itself, because the pill's width
     * is content-driven (the clock is inside it) and the dock's bounding box
     * cannot say where the pill starts.
     */
    int tx = 0, ty = 0, tw = 0, th = 0;
    finndock_tray(&tx, &ty, &tw, &th);
    if (tw <= 0 || th <= 0) {          /* dock has not been laid out yet */
        tx = dock_x; ty = dock_y; tw = dock_w; th = dock_h;
    }

    int px0 = tx;
    int py0 = ty - rs(ANCHOR_GAP, s) - ph;

    int margin = rs(12, s);
    if (px0 < margin) px0 = margin;
    if (py0 < margin) py0 = margin;
    if (px0 + pw > screen_w - margin) px0 = screen_w - margin - pw;
    if (py0 + ph > screen_h - margin) py0 = screen_h - margin - ph;

    /* no shadow: the reference's edges are a hard one-pixel step, so the
     * layer is exactly the panel and there is no margin to spare */
    g_lx0 = px0;
    g_ly0 = py0;

    /* the panel grows out of the tray pill's bottom-left corner */
    g_ax = tx;
    g_ay = ty + th;

    /* rows — all rects are layer-local */
    int x0 = rs(CC_PAD, s);
    int y  = rs(CC_PAD, s);
    int half = rs(HALF_W, s), gap = rs(CC_GAP, s);
    int tht = rs(TILE_H, s), rowh = rs(ROW_H, s);
    int full = pw - 2 * x0;

    g_r_panel.x = 0; g_r_panel.y = 0; g_r_panel.w = pw; g_r_panel.h = ph;

    g_r_wifi.x = x0;              g_r_wifi.y = y; g_r_wifi.w = half; g_r_wifi.h = tht;
    g_r_bt.x   = x0 + half + gap; g_r_bt.y   = y; g_r_bt.w   = half; g_r_bt.h   = tht;
    y += tht + gap;

    g_r_bright.x = x0; g_r_bright.y = y; g_r_bright.w = full; g_r_bright.h = rowh;
    y += rowh + gap;

    g_r_vol.x = x0; g_r_vol.y = y; g_r_vol.w = full; g_r_vol.h = rowh;
    y += rowh + gap;

    g_r_app.x   = x0;              g_r_app.y   = y; g_r_app.w   = half; g_r_app.h   = tht;
    g_r_power.x = x0 + half + gap; g_r_power.y = y; g_r_power.w = half; g_r_power.h = tht;

    /* the bars settle to the hardware's value the first time the panel is
     * built; after that they chase it */
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
    if (g_built && key == g_build_key) return;   /* raster already matches */
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

    /* panel: frosted glass over the wallpaper it actually covers. The layer
     * origin is the settled panel origin, so the blur is sampled from the
     * right place rather than from the layer's top-left. */
    finn_frost_rrect(g_layer, lw, lh, g_lx0, g_ly0,
                     0.0f, 0.0f, (float)pw, (float)ph,
                     s * CC_R_PANEL, FINN_GLASS, CC_GLASS);

    float press = finn_anim_v(&g_a_press);

    /*
     * One connection slot, not two. The reference has a single "how am I
     * online" tile, so when the machine has no radio the slot shows the wire
     * it actually has instead of standing empty next to a second Wired tile.
     * Both interfaces would otherwise compete for the same sentence, and the
     * honest one is the one carrying traffic.
     */
    int wifi_ok  = st->wifi >= 0;
    int wired_ok = st->wired >= 0;
    g_conn_wired = (!wifi_ok && wired_ok);

    /* One size for every label in the panel, set by the longest name. */
    const char *conn_name = g_conn_wired ? "Wired Connection" : "Wi-Fi";
    const char *labels[4] = { conn_name, "Bluetooth", "Appearance", "Power" };
    const finn_font *lf = fit_all(labels, 4, tile_text_avail(&g_r_wifi, s));

    if (g_conn_wired)
        status_tile(g_layer, lw, lh, s, g_r_wifi, &g_ic_eth, 0,
                    conn_name,
                    st->wired ? "Connected to LAN" : "No link",
                    st->wired > 0, lf);
    else
        status_tile(g_layer, lw, lh, s, g_r_wifi, &g_ic_wifi, 0, conn_name,
                    wifi_ok ? (st->wifi > 0 ? "Connected" : "Off") : "No adapter",
                    wifi_ok && st->wifi > 0, lf);

    /* a wire is not a switch: the tile is inert while it shows one, so it
     * gives no press feedback for an action it cannot take */
    if (!g_conn_wired && g_press == FINNCC_WIFI)
        press_wash(g_layer, lw, lh, s, g_r_wifi, press);

    int bt_ok = st->bluetooth >= 0;
    status_tile(g_layer, lw, lh, s, g_r_bt, &g_ic_bt, 0, "Bluetooth",
                bt_ok ? (st->bluetooth ? "On" : "Off") : "No adapter",
                bt_ok && st->bluetooth > 0, lf);
    if (g_press == FINNCC_BLUETOOTH) press_wash(g_layer, lw, lh, s, g_r_bt, press);

    status_tile(g_layer, lw, lh, s, g_r_app,
                st->dark ? &g_ic_moon : &g_ic_sun, 0,
                "Appearance", st->dark ? "Dark" : "Light", st->dark, lf);
    if (g_press == FINNCC_APPEARANCE) press_wash(g_layer, lw, lh, s, g_r_app, press);

    /*
     * The sliders do not squeeze when pressed. A toggle is a button and can
     * give under the finger, but the whole point of a slider is that the
     * surface stays still while the value travels across it — shrinking the
     * row mid-drag reads as the control flinching away from the pointer.
     */
    slider_row(g_layer, lw, lh, s, g_r_bright,
               &g_ic_sun, fill_bright, st->brightness >= 0);
    slider_row(g_layer, lw, lh, s, g_r_vol,
               &g_ic_vol, fill_vol, st->volume >= 0);

    power_tile(g_layer, lw, lh, s, g_r_power, st, lf);
}

void finncc_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!finncc_visible() || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx0 + dx, g_ly0 + dy);
}

/*
 * The open/close transform, resolved once and used by both backends: the
 * software path scales the raster as it blits it, and the accelerated path
 * hands the same rectangle to the GPU as a quad. There is one description of
 * where the panel is, which is the point.
 */
void finncc_transform(float *x, float *y, float *w, float *h, float *alpha) {
    float v = finn_anim_v(&g_a_open);
    float a = v * 1.15f;
    if (a > 1.0f) a = 1.0f;
    float k = OPEN_SCALE + (1.0f - OPEN_SCALE) * v;

    float ax = (float)g_ax, ay = (float)g_ay;
    if (x) *x = ax + ((float)g_lx0 - ax) * k;
    if (y) *y = ay + ((float)g_ly0 - ay) * k;
    if (w) *w = (float)g_lw * k;
    if (h) *h = (float)g_lh * k;
    if (alpha) *alpha = a;
}

void finncc_blit_scaled(uint32_t *dst, int dw, int dh) {
    if (!g_layer) return;
    float x, y, w, h, alpha;
    finncc_transform(&x, &y, &w, &h, &alpha);
    if (alpha <= 0.004f) return;
    finn_blit_scaled(dst, dw, dh, g_layer, g_lw, g_lh, x, y, w, h, alpha);
}

const uint32_t *finncc_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx0;
    if (y) *y = g_ly0;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_cc_rev;
    return finncc_visible() ? g_layer : NULL;
}

/* ---------------- input ---------------- */

static int in_rect(const cc_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* The value's span is the row from the thumb's right edge to the row's right
 * edge; the fraction is the pointer's position across that span, so a click
 * on the thumb reads as zero rather than as a value of a twelfth. */
static float slider_frac(const cc_rect *r, int x, float s) {
    float bx = (float)r->x + s * THUMB_W;
    float bw = (float)r->w - s * THUMB_W;
    if (bw <= 0.0f) return 0.0f;
    float f = ((float)x - bx) / bw;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

int finncc_hit(int px, int py, float *out01) {
    /* the panel only takes the pointer once it is essentially open: during
     * the pop it is drawn smaller than the box it is hit-tested against */
    if (!g_open || !g_layer || finncc_visible() == 0) return FINNCC_NONE;
    if (finn_anim_v(&g_a_open) < 0.90f) return FINNCC_NONE;

    int x = px - g_lx0, y = py - g_ly0;
    if (x < 0 || y < 0 || x >= g_lw || y >= g_lh) return FINNCC_NONE;
    if (out01) *out01 = -1.0f;

    if (!g_conn_wired && in_rect(&g_r_wifi, x, y)) return FINNCC_WIFI;
    if (in_rect(&g_r_bt, x, y))     return FINNCC_BLUETOOTH;
    if (in_rect(&g_r_app, x, y))    return FINNCC_APPEARANCE;
    if (in_rect(&g_r_bright, x, y)) {
        if (out01) *out01 = slider_frac(&g_r_bright, x, g_scale);
        return FINNCC_BRIGHTNESS;
    }
    if (in_rect(&g_r_vol, x, y)) {
        if (out01) *out01 = slider_frac(&g_r_vol, x, g_scale);
        return FINNCC_VOLUME;
    }
    return in_rect(&g_r_panel, x, y) ? FINNCC_PANEL : FINNCC_NONE;
}

float finncc_drag(int action, int px) {
    if (action == FINNCC_BRIGHTNESS) return slider_frac(&g_r_bright, px - g_lx0, g_scale);
    if (action == FINNCC_VOLUME)     return slider_frac(&g_r_vol, px - g_lx0, g_scale);
    return -1.0f;
}
