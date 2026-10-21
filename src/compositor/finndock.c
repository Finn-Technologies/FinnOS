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
 * Metrics authored for a 1080p canvas, taken straight off the reference dock
 * (which is 1280 wide, so these are its numbers x 1.298):
 *
 *      bubble 50      bubble gap 15     pill 160 x 50     clock cap 10
 *      bubble ink 24  tray ink 17       pill pad 12       tray gap 18
 *      clock gap 21   bottom 26
 *
 * Everything here is an *ink* size, not a viewBox size: the assets fill their
 * 24 unit boxes by anywhere from a third to all of it, so a dock laid out by
 * viewBox puts icons of visibly different weight in one row. finn_icon_frame
 * turns these numbers into rasters of the right drawn size.
 *
 * The one place the reference is not followed is the dock's position: it sits
 * centred on the screen there by accident of the capture (both photos have it
 * ~55px right of centre), and centring is what the launcher should do.
 */
#define REF_H         1080.0f
#define BTN           65.0f     /* 50 */
#define BTN_R         19.5f     /* r15 */
#define BTN_ICON      31.0f     /* ink 24 */
#define PILL_PAD      21.0f     /* 16: the well's rim and its inner padding */
#define WELL_PAD_X    12.0f     /* 9: clear of the glyphs on both sides */
#define TRAY_ICON     22.0f     /* ink 17 */
/* a filled gauge reads heavier than an outline at the same ink, so the battery
 * is set one step down from the outline glyphs beside it — the reference does
 * the same (14 wide against their 17) */
#define TRAY_BAT      19.0f     /* ink 14 */
#define TRAY_GAP      23.4f     /* 18 */
#define DOCK_GAP      19.5f     /* 15 */
#define DOCK_BOTTOM   34.0f     /* 26 */
/*
 * The compact dock. When a window needs the room, the dock sinks to the
 * bottom edge and the three bubbles become one surface. The gaps and the
 * pill's padding come in with it, so the controls sit a little closer
 * together instead of carrying the rest state's air for no reason. The
 * glyphs keep their size: this is a change of spacing, not of scale.
 */
#define DOCK_GAP_SUNK   7.5f
#define PILL_PAD_SUNK  18.0f
#define TRAY_GAP_SUNK  17.0f
#define CLOCK_GAP_SUNK 21.0f
/*
 * The reference draws both bubbles at full weight: the launcher and the app
 * grid are the same glyph there, and one of them being greyed out reads as a
 * disabled control rather than as an empty one.
 *
 * The first bubble is the reserved slot for pinned and running apps, and this
 * shell has no way to pin or run anything yet — so it is drawn empty: the
 * glyph is in the mute ink rather than the ink, and it takes no press wash.
 * A slot that lights up under the finger and then does nothing is a promise
 * the shell cannot keep.
 */

/* the tray clock: cap 10 in the reference, which is 18px of Fixel Display */
#define CLOCK_GAP     27.0f     /* 21 */
#define CLOCK_PX      18.0f
#define PILL_INSET     9.0f     /* 7 */

/*
 * The chrome is frosted glass, not a flat fill. These are the two tints the
 * reference gives: the surface is the blurred wallpaper brought most of the
 * way to bone, and the tray's status well is a second, lighter pass over it.
 * The arithmetic that turns two passes into the well's final strength lives
 * at the render site.
 */
#define GLASS_TINT    0.70f
#define GLASS_WELL    0.53f
/*
 * The compact dock is a bar across the bottom of the screen, and a bar that
 * sat at the floating dock's weight would disappear against a maximized
 * window — both are the same glass over the same wallpaper. It is laid down
 * heavier: still the palette's glass, so it is light in the light appearance
 * and deep in the dark one, but it reads as the plane the content stops at
 * rather than as another window.
 */
#define GLASS_BAR     0.94f

/*
 * Motion. Nothing in the dock moves and nothing shrinks: a control answers by
 * taking a wash of ink, which is the same patch the Control Center lays down
 * behind the tray when it opens — only darker, because a hover is a hint that
 * a press is a confirmation, and the open panel is the thing being promised.
 *
 * The numbers are the shell's (finnmotion.h): the dock and the panels answer
 * the pointer with the same wash, at the same strength, in the same time.
 */

/* ---------------- assets ---------------- */
static fsvg g_ic_start, g_ic_vol, g_ic_wifi, g_ic_bat, g_ic_bolt;
static int  g_ready = 0;

/* the clock face; loaded for whatever density the first frame arrives at */
static finn_font g_font;
static float     g_font_scale = -1.0f;
static char      g_font_dir[512];

/* ---------------- icon providers ----------------
 *
 * Each hands back a raster from the icon cache rather than blitting it,
 * because the caller may still need to move it — the hover lift draws the
 * same raster through the scaled path. A revision travels with every request
 * so a changed battery level or wifi signal can never be served a tile that
 * was rasterised for the previous one.
 */

static const uint32_t *icon_start(int px, uint32_t ink) {
    /* The reserved slot is drawn in the mute ink: it holds nothing yet, and
     * a slot that looks exactly like the button beside it is a button the
     * shell cannot honour. */
    return finn_icon_tile(&g_ic_start, px, ink, ink, 0);
}

/*
 * battery.svg is: [0] shell (stroked), [1] charge (rect spanning x=4..18).
 * The charge is clipped horizontally so the bar tracks the real percentage,
 * and the shell takes the palette's grey while the charge takes its ink — the
 * reference draws the container a step lighter than the level inside it.
 *
 * battery_bolt.svg shares the battery's 24-unit box, so blitting it at the
 * battery's own origin lands the bolt exactly inside the shell with no second
 * set of coordinates to keep in sync. It is blitted as opaque glass rather
 * than as a cut-out: the charge bar underneath is solid, and the only colour
 * that can sit on it and still read as a gap is the material of the tray.
 */
static const uint32_t *icon_battery(int px, const finn_state *st) {
    int pct = st->battery_pct < 0 ? 100 : st->battery_pct;
    if (pct > 100) pct = 100;
    g_ic_bat.shapes[1].clip_x = 4.0f + 14.0f * (float)pct / 100.0f;

    uint32_t rev = 1000u + (uint32_t)pct * 2u + (st->charging ? 1u : 0u);
    /* Ink for the charge bar, mute for the outline: the reference draws the
     * shell a step lighter than the level inside it, so the battery reads as
     * a gauge rather than as one solid stamp. */
    return finn_icon_ref(&g_ic_bat, px, FINN_INK, FINN_MUTE, rev);
}

/*
 * wifi.svg is a single path with four subpaths:
 *   0 = outer arc, 1 = middle arc, 2 = inner arc, 3 = dot.
 * Signal lights the arcs from the inside out; no interface greys all of them.
 * These subpaths carry an explicit output colour rather than a tint source,
 * so the signal level is part of the raster's identity.
 */
static const uint32_t *icon_wifi(int px, const finn_state *st) {
    int sig = st->wifi;
    fshape *sh = &g_ic_wifi.shapes[0];
    for (int i = 0; i < sh->npolys; i++) {
        fpoly *q = &sh->polys[i];
        q->vis = 1;
        if (i == 3) {                        /* the dot */
            q->col = (sig < 0) ? FINN_MUTE : FINN_INK;
        } else {
            int first_lit = 3 - sig;         /* outer -> inner ordering */
            q->col = (sig >= 0 && i >= first_lit) ? FINN_INK : FINN_MUTE;
        }
    }
    return finn_icon_ref(&g_ic_wifi, px, FINN_INK, FINN_MUTE,
                         2000u + (uint32_t)(sig < 0 ? 50 : sig));
}

/*
 * volume.svg is: [0] speaker (filled), [1] inner arc, [2] outer arc.
 * These are tint sources, so the level rides in the palette: black is the
 * active wave and #969696 is the headroom that is still available.
 */
static const uint32_t *icon_volume(int px, const finn_state *st) {
    int lvl = st->volume;
    int waves = (lvl < 0) ? 1 : (lvl < 5 ? 0 : (lvl < 55 ? 1 : 2));

    g_ic_vol.shapes[0].fill = FINN_INK;
    g_ic_vol.shapes[1].visible = (waves >= 1);
    if (waves >= 2) {
        /* full level: the outer arc goes solid rather than staying dim */
        g_ic_vol.shapes[2].visible = 1;
        g_ic_vol.shapes[2].stroke = 0xFF000000u;
    } else if (waves == 1) {
        g_ic_vol.shapes[2].visible = 1;   /* shown as available headroom */
        g_ic_vol.shapes[2].stroke = 0xFF969696u;
    } else {
        g_ic_vol.shapes[2].visible = 0;
    }

    return finn_icon_ref(&g_ic_vol, px, FINN_INK, FINN_MUTE,
                         3000u + (uint32_t)waves);
}

/* ---------------- clock ----------------
 *
 * The pill carries the wall clock at minute resolution. Nothing here is a
 * timer: the minute is read on demand — it is folded into the dock's build
 * key so a new minute is a new layer, and finndock_clock_tick() is the
 * one-bit damage signal for the frame that change needs. The compositor stays
 * demand-driven, with no free-running timer ticking just to advance digits.
 */

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

/* ---------------- motion ---------------- */

enum { SLOT_APPS = 0, SLOT_LAUNCHER, SLOT_TRAY, SLOT_COUNT };

static finn_anim g_a_press;               /* 0 = up, 1 = held down */
static int      g_pressed = FINNDOCK_HIT_NONE;

/* How much of the tray's Control Center anchor to show, 0..1. The compositor
 * drives this from the panel's own openness, so the well rises with the panel
 * instead of snapping on at the moment the press lands. */
static float    g_well = 0.0f;

/* The clock's own mark, for the same reason: its page opens from that end of
 * the pill, so the clock lifts into a well while the page is up. */
static float    g_well_clock = 0.0f;
static float    g_well_apps  = 0.0f;

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

/*
 * Sinking, which is the dock answering a window rather than the pointer.
 *
 * The dock leaves its floating position for the bottom edge, and its three
 * bubbles close up into one surface. The pace is the shell's: leaving is
 * FINN_MOTION_CLOSE, returning is FINN_MOTION_OPEN, so the dock retreats
 * the way every other surface does and comes back the way one arrives.
 */
static finn_anim g_a_sink;
static float     g_sink = 0.0f;     /* drawn position, 0 = floating, 1 = flush */
static int       g_sunk = 0;        /* what the caller last asked for         */

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

/*
 * Hover, which in this shell is a colour and not a movement.
 *
 * The pill holds two controls, so it answers in two places: the status block
 * (which opens the Control Center) and the clock (which opens the clock page).
 * The pads either side and the gap between the two are surface, not controls,
 * and take no wash — a hover that appeared under them would be the dock
 * claiming a target it does not have. The pointer position comes in every
 * iteration; only a change of answer starts a track, so a pointer resting on
 * a control costs nothing.
 */
static finn_anim g_a_hover;
static float     g_hover = 0.0f;
static int       g_hover_on = 0;

static finn_anim g_a_hover_c;
static float     g_hover_c = 0.0f;
static int       g_hover_clock = 0;

/* the launcher's own answer: the bubble that opens the Start Menu is a
 * control like any other, and the pointer arriving at it says so */
static finn_anim g_a_hover_l;
static float     g_hover_l = 0.0f;
static int       g_hover_launcher = 0;

static finn_anim g_a_hover_a;
static float     g_hover_a = 0.0f;
static int       g_hover_apps = 0;

/* the tray's status block in scanout pixels, as of the last render — the one
 * region of the dock that answers a hover */
static int g_segx = 0, g_segy = 0, g_segw = 0, g_segh = 0;
/* the clock's own target, which is not the tray's: the two open different
 * pages, so the pill cannot be one hit rectangle any more */
static int g_clockx = 0, g_clocky = 0, g_clockw = 0, g_clockh = 0;

void finndock_hover_at(int px, int py) {
    int hit = finndock_hit(px, py);

    int on = (hit == FINNDOCK_HIT_TRAY &&
              px >= g_segx && px < g_segx + g_segw &&
              py >= g_segy && py < g_segy + g_segh);
    if (on != g_hover_on) {
        g_hover_on = on;
        if (on)
            finn_anim_to(&g_a_hover, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
        else
            finn_anim_to(&g_a_hover, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int onc = (hit == FINNDOCK_HIT_CLOCK);
    if (onc != g_hover_clock) {
        g_hover_clock = onc;
        if (onc)
            finn_anim_to(&g_a_hover_c, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
        else
            finn_anim_to(&g_a_hover_c, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int ona = (hit == FINNDOCK_HIT_APPS);
    if (ona != g_hover_apps) {
        g_hover_apps = ona;
        if (ona)
            finn_anim_to(&g_a_hover_a, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
        else
            finn_anim_to(&g_a_hover_a, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
    }

    int onl = (hit == FINNDOCK_HIT_LAUNCHER);
    if (onl != g_hover_launcher) {
        g_hover_launcher = onl;
        if (onl)
            finn_anim_to(&g_a_hover_l, 1.0f, FINN_MOTION_HOVER_IN, 0.0f, FINN_EASE_OUT);
        else
            finn_anim_to(&g_a_hover_l, 0.0f, FINN_MOTION_HOVER_OUT, 0.0f, FINN_EASE_OUT);
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

/* The press, resolved per control. There is no geometry in it: a control
 * never changes size or position under the pointer, it only darkens. */
typedef struct { float wash; float hover; } dock_bump;

static dock_bump bump_at(int slot, float f) {
    (void)f;
    dock_bump b;
    b.wash = (g_pressed == slot + FINNDOCK_HIT_APPS)
           ? finn_anim_v(&g_a_press) : 0.0f;
    b.hover = 0.0f;
    if (slot == SLOT_APPS)     b.hover = g_hover_a;
    if (slot == SLOT_LAUNCHER) b.hover = g_hover_l;
    return b;
}

/* A translucent ink: darkens in light, lightens in dark, and stays inside the
 * palette either way. */
static uint32_t ink_wash(float a) {
    unsigned v = (unsigned)(a * 255.0f + 0.5f);
    if (v > 255) v = 255;
    return (FINN_INK & 0x00FFFFFFu) | (v << 24);
}

/* ---------------- dock layer ---------------- */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx = 0, g_ly = 0;
static int g_visible = 0;

/*
 * The layer's identity, and how many times it has actually been drawn. A
 * compositor that keeps the raster on the GPU re-uploads it when the revision
 * moves and not otherwise; without the early-out below, an animating Control
 * Center would rebuild this layer sixty times a second for no change at all.
 */
static uint32_t g_dock_key = 0;
static unsigned g_dock_rev = 0;
static int      g_dock_built = 0;

/* the minute the last render folded into the build key — what the tick
 * compares against so the clock can ask for exactly one frame per minute */
static int g_clock_min = -1;

/* resting geometry of the last render, in scanout pixels, for hit-testing and
 * for anchoring the Control Center above the tray. It is deliberately the
 * resting box, not the layer: the layer carries slack for its own edges, and
 * an anchor that moved with the pointer would drag the panel around. */
static int g_x0 = 0, g_y0 = 0, g_btn = 0, g_gap = 0, g_pillx = 0, g_pillw = 0;

static void load_clock_font(float s) {
    finn_font_free(&g_font);
    /* One weight. The shell's hierarchy comes from size and from ink against
     * dim, never from a heavier cut of the face — the clock is the largest
     * text in the dock, and it is large enough on its own. */
    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    int px = (int)lroundf(CLOCK_PX * s);
    if (finn_font_load(&g_font, p, px) != 0)
        fprintf(stderr, "finndock: cannot load %s\n", p);
}

int finndock_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);
    g_font_scale = -1.0f;   /* load the face for whatever density arrives first */

    char p[512];
    int ok = 1;
    snprintf(p, sizeof p, "%s/start_button.svg", icon_dir); ok &= fsvg_load(p, &g_ic_start) == 0;
    snprintf(p, sizeof p, "%s/volume.svg", icon_dir);       ok &= fsvg_load(p, &g_ic_vol) == 0;
    snprintf(p, sizeof p, "%s/wifi.svg", icon_dir);         ok &= fsvg_load(p, &g_ic_wifi) == 0;
    snprintf(p, sizeof p, "%s/battery.svg", icon_dir);      ok &= fsvg_load(p, &g_ic_bat) == 0;
    snprintf(p, sizeof p, "%s/battery_bolt.svg", icon_dir); ok &= fsvg_load(p, &g_ic_bolt) == 0;
    if (!ok) fprintf(stderr, "finndock: icon set incomplete in %s\n", icon_dir);
    g_ready = ok;
    return ok ? 0 : -1;
}

void finndock_free(void) {
    fsvg_free(&g_ic_start);
    fsvg_free(&g_ic_vol);
    fsvg_free(&g_ic_wifi);
    fsvg_free(&g_ic_bat);
    fsvg_free(&g_ic_bolt);
    finn_font_free(&g_font);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_ready = 0;
    g_font_scale = -1.0f;
    g_clock_min = -1;
}

static int rs(float v, float s) { return (int)lroundf(v * s); }

/*
 * Draw one rounded-square control at its resting box, through its bump.
 *
 * `ipx`/`ix`/`iy` are the glyph's raster and where its top-left goes, which
 * comes from finn_icon_frame: the glyph is placed by the size of its ink, so
 * it is centred as drawn rather than as boxed.
 */
static void bubble(uint32_t *L, int lw, int lh, int ox, int oy,
                   float x, float y, float w, float h,
                   float rad, dock_bump b, float frost_alpha,
                   const uint32_t *icon, int ipx,
                   int ix, int iy, float icon_alpha) {
    /* frost, not fill: the glass samples the wallpaper under the control as
     * it is drawn, so the bubble carries the same light as the panel. In the
     * compact dock the surface is one rect drawn behind the row, so this
     * bubble's own frost fades out as the merged one fades in. */
    if (frost_alpha > 0.004f)
        finn_frost_rrect(L, lw, lh, ox, oy, x, y, w, h, rad,
                         FINN_GLASS, GLASS_TINT * frost_alpha);

    /* The pointer's answer, and the press that follows it: the same patch,
     * the same strengths, as every other surface in the shell. */
    float wash = FINN_MOTION_HOVER_WASH * b.hover + FINN_MOTION_PRESS_WASH * b.wash;
    if (wash > 0.004f)
        finn_fill_rrect(L, lw, lh, x, y, w, h, rad, ink_wash(wash));
    if (!icon) return;

    if (icon_alpha >= 1.0f)
        finn_blit(L, lw, lh, icon, ipx, ipx, ix, iy);
    else
        finn_blit_scaled(L, lw, lh, icon, ipx, ipx, (float)ix, (float)iy,
                         (float)ipx, (float)ipx, icon_alpha);
}

void finndock_render(int screen_w, int screen_h, float scale, const finn_state *st) {
    if (!g_ready) { g_visible = 0; return; }

    float f = scale;
    if (g_font_scale != f) { load_clock_font(f); g_font_scale = f; }

    /*
     * The compact state is drawn from its animated value, so the controls
     * close up as the dock sinks rather than after it has arrived. The layer
     * is rebuilt on every step of that track: its width is part of the key.
     */
    float sink = g_sink;
    float btn = f * BTN;
    float gap = f * (DOCK_GAP + (DOCK_GAP_SUNK - DOCK_GAP) * sink);
    float pad = f * (PILL_PAD + (PILL_PAD_SUNK - PILL_PAD) * sink);
    float ico = f * TRAY_ICON;
    float tgap = f * (TRAY_GAP + (TRAY_GAP_SUNK - TRAY_GAP) * sink);
    float cgap = f * (CLOCK_GAP + (CLOCK_GAP_SUNK - CLOCK_GAP) * sink);
    float rad = f * BTN_R;

    /*
     * The clock comes first because it drives the layout: its width is part
     * of the pill's width and the pill's width is part of the dock's.
     */
    char clock_s[8];
    int minute = clock_minute_now(clock_s, sizeof clock_s);
    g_clock_min = minute;
    float clock_w = g_font.face ? finn_text_width(&g_font, clock_s) : 0.0f;

    /*
     * The tray carries only what the machine actually has. A desktop with no
     * battery gets no battery glyph; a machine with no wireless hardware gets
     * no wifi glyph. This is the adaptive-UI rule applied at the smallest
     * scale: the same dock, driven by real capability rather than a fixed
     * set of pictures.
     */
    int n_tray = 1 + (st->wifi >= 0 ? 1 : 0) + (st->battery_pct >= 0 ? 1 : 0);
    float icons_w = ico * n_tray + tgap * (n_tray - 1);
    float pill_w = pad * 2 + icons_w +
                   (clock_w > 0.0f ? cgap + clock_w : 0.0f);
    float total = btn * 2 + gap * 2 + pill_w;
    float x0 = (screen_w - total) * 0.5f;
    float y0 = screen_h - f * DOCK_BOTTOM * (1.0f - sink) - btn;

    /*
     * The compact dock is one bar across the bottom of the screen. It grows
     * from the row's own width to the full screen as it sinks, so the last
     * thing the eye sees is the three bubbles becoming the one surface the
     * window is leaving its room to.
     */
    float bar_w = total + ((float)screen_w - total) * sink;
    float bar_x = ((float)screen_w - bar_w) * 0.5f;
    float bar_r = rad * (1.0f - sink);

    /* a hair of slack at the layer's edge, so the glass never clips its own
     * antialiasing against the wallpaper */
    int slack = 2;
    int lx = (int)floorf(bar_x) - slack;
    int ly = (int)floorf(y0) - slack;
    int lw = (int)ceilf(bar_w) + slack * 2;
    int lh = (int)ceilf(btn) + slack * 2;

    g_x0 = (int)lroundf(x0);
    g_y0 = (int)lroundf(y0);
    g_btn = rs(BTN, f);
    g_gap = (int)lroundf(gap);
    g_pillx = (int)lroundf(x0 + btn * 2 + gap * 2);
    g_pillw = (int)lroundf(pill_w);

    /* Everything that can put a different pixel in the layer, in one number.
     * The layer's own size is in here too, so a key match guarantees the
     * buffer behind it is already the right shape. The minute is in here so
     * the clock redraws itself exactly when the time it shows moves. */
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

    /* layer-local coordinates */
    float bx = x0 - lx, by = y0 - ly;

    /*
     * The compact dock is one surface. It is drawn behind the row and grows
     * to the full screen as it fades in, while the three bubbles' own frost
     * fades out. The bar is laid down heavier than a bubble (GLASS_BAR), so
     * the handover reads as the gaps closing and the base plane settling
     * under the same glass language rather than as a second material.
     */
    float frost = 1.0f - sink;
    if (sink > 0.004f)
        finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                         bar_x - lx, by, bar_w, btn, bar_r,
                         FINN_GLASS, GLASS_BAR * sink);

    /* Taskbar / running apps bubble (Figma 7:41 / 12:9 "Taskbar") */
    {
        dock_bump ab = bump_at(SLOT_APPS, f);
        if (frost > 0.004f)
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly, bx, by, btn, btn, rad,
                             FINN_GLASS, GLASS_TINT * frost);

        float inset = f * PILL_INSET;
        float well_x = bx + inset;
        float well_y = by + inset;
        float well_w = btn - inset * 2.0f;
        float well_h = btn - inset * 2.0f;
        float well_r = rad - inset;
        if (well_r < 0.0f) well_r = 0.0f;

        if (g_well_apps > 0.004f) {
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly,
                             well_x, well_y, well_w, well_h, well_r,
                             FINN_GLASS, GLASS_WELL * g_well_apps);
        }

        float wash = FINN_MOTION_HOVER_WASH * ab.hover + FINN_MOTION_PRESS_WASH * ab.wash;
        if (wash > 0.004f) {
            if (g_well_apps > 0.004f)
                finn_fill_rrect(g_layer, lw, lh, well_x, well_y, well_w, well_h,
                                well_r, ink_wash(wash));
            else
                finn_fill_rrect(g_layer, lw, lh, bx, by, btn, btn,
                                rad, ink_wash(wash));
        }

        int ipx, iox, ioy;
        finn_icon_frame(&g_ic_start, rs(BTN_ICON, f),
                        (int)lroundf(bx + btn * 0.5f),
                        (int)lroundf(by + btn * 0.5f), &ipx, &iox, &ioy);
        const uint32_t *ic = icon_start(ipx, FINN_INK);
        if (ic) {
            float alpha = (g_well_apps > 0.004f) ? 1.0f : 0.35f;
            if (alpha >= 1.0f)
                finn_blit(g_layer, lw, lh, ic, ipx, ipx, iox, ioy);
            else
                finn_blit_scaled(g_layer, lw, lh, ic, ipx, ipx,
                                 (float)iox, (float)ioy, (float)ipx, (float)ipx, alpha);
        }

        /* Running app indicator pill at the bottom of the Taskbar slot */
        if (g_well_apps > 0.004f) {
            int active_vis = (g_well_apps >= 0.8f);
            float ind_w = active_vis ? (f * 16.0f) : (f * 7.0f);
            float ind_h = f * 3.2f;
            float ind_x = bx + (btn - ind_w) * 0.5f;
            float ind_y = by + btn - inset * 0.5f - ind_h * 0.5f;
            uint32_t ind_col = active_vis ? FINN_ON : FINN_MUTE;
            finn_fill_rrect(g_layer, lw, lh, ind_x, ind_y, ind_w, ind_h,
                            ind_h * 0.5f, ind_col);
        }
    }

    /* launcher */
    {
        float lxx = bx + btn + gap;
        int ipx, iox, ioy;
        finn_icon_frame(&g_ic_start, rs(BTN_ICON, f),
                        (int)lroundf(lxx + btn * 0.5f),
                        (int)lroundf(by + btn * 0.5f), &ipx, &iox, &ioy);
        bubble(g_layer, lw, lh, g_lx, g_ly, lxx, by, btn, btn, rad,
               bump_at(SLOT_LAUNCHER, f), frost,
               icon_start(ipx, FINN_INK), ipx, iox, ioy, 1.0f);
    }

    /*
     * tray pill — same corner radius as the bubbles, same frosted material.
     * It is wider than tall, so its contents ride one uniform scale about the
     * pill's centre rather than being re-laid-out, which keeps the glyph
     * spacing intact mid-lift.
     */
    {
        float px0 = bx + btn * 2 + gap * 2;

        dock_bump tb = bump_at(SLOT_TRAY, f);
        float tcy = by + btn * 0.5f;

        if (frost > 0.004f)
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly, px0, by, pill_w, btn,
                             rad, FINN_GLASS, GLASS_TINT * frost);

        /*
         * The inset patch the status icons sit in — the one surface in the
         * dock that carries two different meanings, which is why it is drawn
         * once from the resolved numbers below.
         */
        float inset = f * PILL_INSET;
        float well_pad = f * WELL_PAD_X;
        /* The well is the icons block plus an even margin: the glyphs keep the
         * same clearance on the left as on the right, which a rim-plus-pad
         * split could not do while the icons also sit off-centre in the pill. */
        float seg_l = px0 + pad - well_pad;
        float seg_t = by + inset;
        float seg_w = icons_w + well_pad * 2.0f;
        float seg_h = btn - inset * 2.0f;
        float seg_r = rad - inset; if (seg_r < 0.0f) seg_r = 0.0f;

        /* the block's screen rect, which is what the hover hit-tests against */
        g_segx = (int)floorf(seg_l) + lx;
        g_segy = (int)floorf(seg_t) + ly;
        g_segw = (int)lroundf(seg_w);
        g_segh = (int)lroundf(seg_h);

        /*
         * Open: the Control Center's anchor. It is not permanent decoration —
         * it is the mark that ties the tray to the panel above it, so it rises
         * and falls with the panel's own openness (finndock_set_well) rather
         * than being painted into every frame of the dock's life.
         *
         * It is a second frost pass at GLASS_WELL over the GLASS_TINT pill, so
         * the glass weight compounds to
         *     0.70 + (1 - 0.70)·0.53 ≈ 0.86
         * — that is the whole reason it is a second pass rather than one
         * 0.86 fill: it is the same glass, laid down twice.
         */
        if (g_well > 0.004f)
            finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly, seg_l, seg_t,
                             seg_w, seg_h, seg_r,
                             FINN_GLASS, GLASS_WELL * g_well);

        /*
         * Shut: the same patch, but a wash instead of a lift. Hovering the
         * tray says "this opens something" and pressing it says "you got it";
         * both are darker than the open panel's anchor, because neither of
         * them *is* the open panel. They fade out as the well takes over.
         */
        float wash_a = (FINN_MOTION_HOVER_WASH * g_hover + FINN_MOTION_PRESS_WASH * tb.wash)
                     * (1.0f - g_well);
        if (wash_a > 0.004f)
            finn_fill_rrect(g_layer, lw, lh, seg_l, seg_t, seg_w, seg_h, seg_r,
                            ink_wash(wash_a));

        float ix = px0 + pad;

        /*
         * One tray glyph, centred on the pill's mid-line and sized by its ink.
         *
         * Each icon owns a box `ico` wide and is drawn to fill it. Layout by
         * viewBox would put a battery (half of its 24 units) next to a wifi
         * (two thirds) and a volume (two thirds tall, two fifths wide) at three
         * different optical sizes; ink is the only unit that makes a row of
         * them look like one set.
         */
        #define TRAY_GLYPH(svgp, tile, cx_)                                      \
            do {                                                                 \
                int gpx, gox, goy;                                               \
                finn_icon_frame((svgp), rs(TRAY_ICON, f),                        \
                                (int)lroundf(cx_), (int)lroundf(tcy),            \
                                &gpx, &gox, &goy);                               \
                const uint32_t *_t = (tile);                                     \
                if (_t)                                                          \
                    finn_blit(g_layer, lw, lh, _t, gpx, gpx, gox, goy);           \
            } while (0)

        TRAY_GLYPH(&g_ic_vol, icon_volume(gpx, st), ix + ico * 0.5f);
        ix += ico + tgap;
        if (st->wifi >= 0) {
            TRAY_GLYPH(&g_ic_wifi, icon_wifi(gpx, st), ix + ico * 0.5f);
            ix += ico + tgap;
        }
        if (st->battery_pct >= 0) {
            int gpx, gox, goy;
            finn_icon_frame(&g_ic_bat, rs(TRAY_BAT, f),
                            (int)lroundf(ix + ico * 0.5f), (int)lroundf(tcy),
                            &gpx, &gox, &goy);
            const uint32_t *bat = icon_battery(gpx, st);
            if (bat) finn_blit(g_layer, lw, lh, bat, gpx, gpx, gox, goy);
            if (st->charging) {
                /* the bolt shares the battery's 24-unit box, so it rides the
                 * battery's own frame: same raster size, same origin */
                const uint32_t *bolt = finn_icon_ref(&g_ic_bolt, gpx,
                                                     FINN_GLASS, FINN_GLASS, 60u);
                if (bolt) finn_blit(g_layer, lw, lh, bolt, gpx, gpx, gox, goy);
            }
        }
        #undef TRAY_GLYPH

        /* the clock, right-aligned after the battery. Caps are centred on the
         * pill's mid-line, which is what finn_text_cap_height is for. */
        if (clock_w > 0.0f) {
            float cap = finn_text_cap_height(&g_font);
            float clx0 = px0 + pill_w - pad - clock_w;
            float base0 = by + btn * 0.5f + cap * 0.5f;

            /* The clock gets the tray's two treatments, because it is the
             * tray's other control: a well while its page is open and a wash
             * while the pointer is on it. Both are the same glass and the same
             * ink as the status block uses, so the pill reads as one object
             * with two halves rather than two widgets that happen to touch. */
            float kl = clx0 - well_pad;
            float kw = clock_w + well_pad * 2.0f;
            if (g_well_clock > 0.004f)
                finn_frost_rrect(g_layer, lw, lh, g_lx, g_ly, kl, seg_t,
                                 kw, seg_h, seg_r,
                                 FINN_GLASS, GLASS_WELL * g_well_clock);

            float cwash = FINN_MOTION_HOVER_WASH * g_hover_c * (1.0f - g_well_clock);
            if (cwash > 0.004f)
                finn_fill_rrect(g_layer, lw, lh, kl, seg_t, kw, seg_h, seg_r,
                                ink_wash(cwash));

            finn_text_draw(&g_font, g_layer, lw, lh, clx0, base0, clock_s, FINN_INK);

            /* The clock's target reaches halfway into the gap before it and
             * runs to the pill's edge: a clock is a control here, and a
             * control should be easier to hit than the ink it is drawn from. */
            float half_gap = cgap * 0.5f;
            g_clockx = (int)lroundf(px0 + pill_w - pad - clock_w - half_gap) + lx;
            g_clocky = (int)lroundf(by) + ly;
            g_clockw = (int)lroundf(clock_w + pad + half_gap);
            g_clockh = g_btn;
        }
    }

    g_visible = 1;
}

void finndock_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!g_visible || !g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx + dx, g_ly + dy);
}

int finndock_height(float scale) { return rs(BTN, scale); }
int finndock_visible(void) { return g_visible; }

int finndock_rest_top(int screen_h, float scale) {
    return screen_h - rs(DOCK_BOTTOM, scale) - rs(BTN, scale);
}

void finndock_geometry(int *x, int *y, int *w, int *h) {
    if (x) *x = g_x0;
    if (y) *y = g_y0;
    if (w) *w = g_btn * 2 + g_gap * 2 + g_pillw;
    if (h) *h = g_btn;
}

void finndock_tray(int *x, int *y, int *w, int *h) {
    if (x) *x = g_pillx;
    if (y) *y = g_y0;
    if (w) *w = g_pillw;
    if (h) *h = g_btn;
}

void finndock_clock(int *x, int *y, int *w, int *h) {
    if (x) *x = g_clockx;
    if (y) *y = g_clocky;
    if (w) *w = g_clockw;
    if (h) *h = g_clockh;
}

/*
 * The launcher button is the second bubble: the first is a reserved slot for
 * pinned and running apps, and it is dimmed because it has nothing in it
 * yet. The Start Menu grows out of the launcher's corner, so the button's
 * own rect has to come from here — the dock's bounding box cannot say where
 * one bubble ends and the next begins.
 */
void finndock_launcher(int *x, int *y, int *w, int *h) {
    if (x) *x = g_x0 + g_btn + g_gap;
    if (y) *y = g_y0;
    if (w) *w = g_btn;
    if (h) *h = g_btn;
}

/*
 * 1 when the wall clock has moved into a new minute since the last render, so
 * the compositor can force the frame the clock needs — and 0 the rest of the
 * time, which is what lets this sit in the per-iteration damage test.
 */
int finndock_clock_tick(void) {
    int m = clock_minute_now(NULL, 0);
    if (m == g_clock_min) return 0;
    g_clock_min = m;
    return 1;
}

/* The layer itself, for a backend that would rather paste it on the GPU than
 * memcpy it. `rev` moves only when the pixels do, so an upload can be skipped
 * rather than repeated. */
const uint32_t *finndock_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx;
    if (y) *y = g_ly;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_dock_rev;
    return g_visible ? g_layer : NULL;
}

int finndock_hit(int px, int py) {
    if (!g_visible || g_btn <= 0) return FINNDOCK_HIT_NONE;
    if (py < g_y0 || py >= g_y0 + g_btn) return FINNDOCK_HIT_NONE;

    if (px >= g_x0 && px < g_x0 + g_btn)                    return FINNDOCK_HIT_APPS;
    if (px >= g_x0 + g_btn + g_gap && px < g_x0 + 2 * g_btn + g_gap)
                                                            return FINNDOCK_HIT_LAUNCHER;
    if (g_clockw > 0 &&
        px >= g_clockx && px < g_clockx + g_clockw)         return FINNDOCK_HIT_CLOCK;
    if (px >= g_pillx && px < g_pillx + g_pillw)            return FINNDOCK_HIT_TRAY;
    return FINNDOCK_HIT_NONE;
}
