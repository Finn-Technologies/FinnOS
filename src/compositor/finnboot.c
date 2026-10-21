/* finnboot — the boot screen: the mark, and the progress of the wake-up. */
#define _GNU_SOURCE
#include "finnboot.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Metrics, taken off the boot screen in the Figma file and scaled to the
 * 1080p canvas the rest of the shell is authored on (the reference is
 * 1280 wide, so its numbers are x 1.298).
 *
 *   the mark        50 x 50, centred on the screen
 *   the line        150 wide, 5 thick, 100 below the mark
 *   the track       #6C6C6C at 40%
 *   the fill        white, growing from the left
 */
#define BOOT_MARK      65.0f    /* 50 */
#define BOOT_LINE_W   195.0f    /* 150 */
#define BOOT_LINE_H     6.5f    /* 5 */
#define BOOT_LINE_GAP 130.0f    /* 100: below the mark, not below the centre */

/* The colour of the track: a grey at forty percent, so the line reads as
 * a groove the progress fills rather than as a bar. */
#define BOOT_TRACK  0x666C6C66u

/* ---------------- assets ---------------- */
static fsvg g_ic_mark;
static int  g_ready = 0;

/* ---------------- the layer ---------------- */
static uint32_t *g_layer = NULL;
static int g_lw = 0, g_lh = 0, g_lx = 0, g_ly = 0;
static unsigned g_rev = 0;

/* where the mark and the line are, in scanout pixels */
static int g_mark_x = 0, g_mark_y = 0, g_mark_px = 0;
static int g_line_x = 0, g_line_y = 0, g_line_w = 0, g_line_h = 0;

/* ---------------- the progress ---------------- */
static finn_anim g_a_progress;
static float     g_target = 0.0f;

static int rs(float v, float s) { return (int)lroundf(v * s); }

int finnboot_init(const char *icon_dir) {
    char p[512];
    snprintf(p, sizeof p, "%s/start_button.svg", icon_dir);
    if (fsvg_load(p, &g_ic_mark) != 0) {
        fprintf(stderr, "finnboot: cannot load the mark from %s\n", p);
        g_ready = 0;
        return -1;
    }
    g_ready = 1;
    return 0;
}

void finnboot_free(void) {
    fsvg_free(&g_ic_mark);
    free(g_layer);
    g_layer = NULL;
    g_lw = g_lh = 0;
    g_ready = 0;
}

void finnboot_set_progress(float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    if (p <= g_target) return;
    g_target = p;
    finn_anim_to(&g_a_progress, p, 0.22f, 0.0f, FINN_EASE_OUT);
}

float finnboot_progress(void) { return finn_anim_v(&g_a_progress); }

int finnboot_tick(float dt) {
    int ch = finn_anim_tick(&g_a_progress, dt);
    if (g_target >= 0.999f && finn_anim_v(&g_a_progress) >= 0.999f) {
        /* the line is full and has stopped moving: the wake-up is over */
        finn_anim_jump(&g_a_progress, 1.0f);
    }
    return ch;
}

int finnboot_running(void) { return finn_anim_running(&g_a_progress); }

void finnboot_render(int screen_w, int screen_h, float scale) {
    if (!g_ready) return;

    float s = scale;

    int mark = rs(BOOT_MARK, s);
    int line_w = rs(BOOT_LINE_W, s);
    int line_h = rs(BOOT_LINE_H, s);
    int gap = rs(BOOT_LINE_GAP, s);

    g_mark_px = mark;
    g_mark_x = (screen_w - mark) / 2;
    g_mark_y = (screen_h - mark) / 2;
    g_line_w = line_w;
    g_line_h = line_h;
    g_line_x = (screen_w - line_w) / 2;
    g_line_y = g_mark_y + mark + gap;

    /* The layer holds the mark and the line and nothing else: the black
     * around them is the frame's own fill, which the compositor draws
     * before the layer. A full-screen layer would cost sixteen megabytes
     * to carry a few hundred pixels of ink. */
    int lw = line_w > mark ? line_w : mark;
    int lh = (g_line_y + line_h) - g_mark_y;
    int lx = g_mark_x - (lw - mark) / 2;
    int ly = g_mark_y;

    if (lw != g_lw || lh != g_lh) {
        free(g_layer);
        g_layer = malloc((size_t)lw * lh * 4);
        g_lw = lw; g_lh = lh;
    }
    if (!g_layer) return;
    memset(g_layer, 0, (size_t)lw * lh * 4);
    g_lx = lx; g_ly = ly;
    g_rev++;

    /* ---- the mark ----
     *
     * Drawn by its ink rather than by its box, like every other glyph in
     * the shell: the assets fill their boxes by different amounts, and a
     * mark laid out by viewBox would sit smaller than the line beneath it
     * for reasons nobody could see.
     */
    {
        int ipx, iox, ioy;
        finn_icon_frame(&g_ic_mark, mark,
                        g_mark_x + mark / 2, g_mark_y + mark / 2,
                        &ipx, &iox, &ioy);
        const uint32_t *t = finn_icon_ref(&g_ic_mark, ipx, 0xFFFFFFFFu, 0xFFFFFFFFu, 0);
        if (t) finn_blit(g_layer, g_lw, g_lh, t, ipx, ipx, iox - lx, ioy - ly);
    }

    /* ---- the line ----
     *
     * The track is a groove: a grey at forty percent, drawn with round
     * caps, and the fill is white and grows from the left over it. The
     * fill's cap is round as well, so the end of the progress is a round
     * end and not a cut.
     */
    {
        float rad = (float)line_h * 0.5f;
        int tx = g_line_x - lx, ty = g_line_y - ly;

        finn_fill_rrect(g_layer, g_lw, g_lh,
                        (float)tx, (float)ty, (float)line_w, (float)line_h,
                        rad, BOOT_TRACK);

        float p = finnboot_progress();
        if (p > 0.001f) {
            float fw = (float)line_w * p;
            if (fw < rad * 2.0f) fw = rad * 2.0f;   /* a cap needs a cap */
            if (fw > (float)line_w) fw = (float)line_w;
            finn_fill_rrect(g_layer, g_lw, g_lh,
                            (float)tx, (float)ty, fw, (float)line_h,
                            rad, 0xFFFFFFFFu);
        }
    }
}

void finnboot_blit(uint32_t *dst, int dw, int dh, int dx, int dy) {
    if (!g_layer) return;
    finn_blit(dst, dw, dh, g_layer, g_lw, g_lh, g_lx + dx, g_ly + dy);
}

const uint32_t *finnboot_layer(int *x, int *y, int *w, int *h, unsigned *rev) {
    if (x) *x = g_lx;
    if (y) *y = g_ly;
    if (w) *w = g_lw;
    if (h) *h = g_lh;
    if (rev) *rev = g_rev;
    return g_layer;
}
