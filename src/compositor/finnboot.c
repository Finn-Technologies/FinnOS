/* finnboot — the boot screen: the mark, and the progress of the wake-up. */
#define _GNU_SOURCE
#include "finnboot.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

/*
 * Metrics matching Figma frame 'Boot' 1:2 exactly (1280x832 reference canvas):
 *
 *   Background: pure black #000000
 *   The mark:   50 x 50, centered on the screen at (615, 391)
 *   The line:   150 wide track at (565, 541), 5 stroke weight, round caps
 *   The track:  #6C6C6C at 40% opacity (solid 43, 43, 43 over black)
 *   The lit:    15px lit segment in Figma reference (10% progress), solid white
 *   The gap:    100px from bottom of the mark (391 + 50 = 441) to line center (541)
 */
#define BOOT_MARK_SZ   50.0f
#define BOOT_TRACK_W  150.0f
#define BOOT_LINE_H     5.0f
#define BOOT_GAP      100.0f

/* Track color in ARGB straight alpha: 40% opacity of #6C6C6C */
#define BOOT_TRACK_COLOR 0x666C6C6Cu
#define BOOT_LIT_COLOR   0xFFFFFFFFu

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

/* ---------------- the progress & motion ---------------- */
static finn_anim g_a_progress;
static finn_anim g_a_mark_in;
static finn_anim g_a_mark_out;
static finn_anim g_a_handoff;
static float     g_target = 0.10f;  /* Starts at Figma 1:2 lit segment (10% = 15px) */
static int       g_exiting = 0;

static int rs(float v, float s) { return (int)lroundf(v * s); }

int finnboot_init(const char *icon_dir) {
    char p[512];
    snprintf(p, sizeof p, "%s/boot-mark.svg", icon_dir);
    if (fsvg_load(p, &g_ic_mark) != 0) {
        fprintf(stderr, "finnboot: cannot load the mark from %s\n", p);
        g_ready = 0;
        return -1;
    }

    /* Start progress primed at 10% (15px lit segment on 150px track as in Figma 1:2) */
    memset(&g_a_progress, 0, sizeof g_a_progress);
    g_target = 0.10f;
    finn_anim_jump(&g_a_progress, 0.10f);

    /* Mark starts fully visible so static snapshots/captures match Figma 1:2 */
    memset(&g_a_mark_in, 0, sizeof g_a_mark_in);
    finn_anim_jump(&g_a_mark_in, 1.0f);

    memset(&g_a_mark_out, 0, sizeof g_a_mark_out);
    finn_anim_jump(&g_a_mark_out, 0.0f);

    memset(&g_a_handoff, 0, sizeof g_a_handoff);
    finn_anim_jump(&g_a_handoff, 0.0f);
    g_exiting = 0;

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
    finn_anim_to(&g_a_progress, p, FINN_MOTION_FILL, 0.0f, FINN_EASE_OUT);
}

float finnboot_progress(void) { return finn_anim_v(&g_a_progress); }

void finnboot_start_exit(void) {
    if (g_exiting) return;
    g_exiting = 1;
    /* Scale/fade out consistent with docs/design/motion.md (closes over 0.18s, FINN_EASE_IN_OUT) */
    finn_anim_to(&g_a_mark_out, 1.0f, FINN_MOTION_CLOSE, 0.0f, FINN_EASE_IN_OUT);
    /* Desktop handoff dissolve over FINN_MOTION_THEME (0.36s, FINN_EASE_IN_OUT) */
    finn_anim_to(&g_a_handoff, 1.0f, FINN_MOTION_THEME, 0.0f, FINN_EASE_IN_OUT);
}

int finnboot_exiting(void) { return g_exiting; }

float finnboot_handoff_alpha(void) {
    if (!g_exiting) return 1.0f;
    float h = finn_anim_v(&g_a_handoff);
    float a = 1.0f - h;
    return a < 0.0f ? 0.0f : a;
}

float finnboot_mark_alpha(void) {
    float vin = finn_anim_v(&g_a_mark_in);
    float vout = finn_anim_v(&g_a_mark_out);
    float a = fminf(1.0f, vin * FINN_MOTION_FADE) * (1.0f - vout);
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

float finnboot_mark_scale(void) {
    float vin = finn_anim_v(&g_a_mark_in);
    float vout = finn_anim_v(&g_a_mark_out);
    float sin = FINN_MOTION_OPEN_SCALE + (1.0f - FINN_MOTION_OPEN_SCALE) * vin;
    float sout = 1.0f - 0.08f * vout;
    return sin * sout;
}

int finnboot_tick(float dt) {
    int ch = finn_anim_tick(&g_a_progress, dt);
    ch |= finn_anim_tick(&g_a_mark_in, dt);
    ch |= finn_anim_tick(&g_a_mark_out, dt);
    ch |= finn_anim_tick(&g_a_handoff, dt);
    if (g_target >= 0.999f && finn_anim_v(&g_a_progress) >= 0.999f) {
        finn_anim_jump(&g_a_progress, 1.0f);
    }
    return ch;
}

int finnboot_running(void) {
    return finn_anim_running(&g_a_progress) ||
           finn_anim_running(&g_a_mark_in) ||
           finn_anim_running(&g_a_mark_out) ||
           finn_anim_running(&g_a_handoff);
}

static void maybe_save_capture(int screen_w, int screen_h) {
    const char *cap = getenv("FINN_BOOT_CAPTURE");
    if (!cap || !cap[0]) cap = "/run/finn/boot-capture.ppm";
    static int saved = 0;
    if (saved) return;
    mkdir("/run/finn", 0755);
    FILE *f = fopen(cap, "wb");
    if (!f) return;
    saved = 1;
    fprintf(f, "P6\n%d %d\n255\n", screen_w, screen_h);
    uint32_t *full = calloc((size_t)screen_w * screen_h, 4);
    if (!full) { fclose(f); return; }
    for (int i = 0; i < screen_w * screen_h; i++) full[i] = 0xFF000000u;
    finnboot_blit(full, screen_w, screen_h, 0, 0);
    for (int y = 0; y < screen_h; y++) {
        uint8_t row[screen_w * 3];
        for (int x = 0; x < screen_w; x++) {
            uint32_t c = full[y * screen_w + x];
            row[x * 3 + 0] = (uint8_t)((c >> 16) & 0xFF);
            row[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (uint8_t)(c & 0xFF);
        }
        fwrite(row, 1, (size_t)screen_w * 3, f);
    }
    free(full);
    fclose(f);
    fprintf(stderr, "finnboot: saved capture to %s\n", cap);
}

void finnboot_render(int screen_w, int screen_h, float scale) {
    (void)scale;
    if (!g_ready) return;

    /*
     * Density scale: Figma 1280x832 reference frame renders 1:1 at s=1.0.
     * On VM 2560x1600 or 2560x1664 (2x), scale is 2.0.
     */
    float s = (screen_w == 1280 && screen_h == 832)
            ? 1.0f
            : ((float)screen_w / 1280.0f);

    int mark = rs(BOOT_MARK_SZ, s);
    int line_w = rs(BOOT_TRACK_W, s);
    int line_h = rs(BOOT_LINE_H, s);
    if (line_h < 1) line_h = 1;
    int gap = rs(BOOT_GAP, s);

    g_mark_px = mark;
    /* Exactly centered on screen: (615, 391) on 1280x832 */
    g_mark_x = (screen_w - mark) / 2;
    g_mark_y = (screen_h - mark) / 2;

    /* Progress track centered at (565, 541) on 1280x832 */
    g_line_w = line_w;
    g_line_h = line_h;
    g_line_x = (screen_w - line_w) / 2;
    g_line_y = g_mark_y + mark + gap;
    int track_cy = g_line_y;

    float rad = (float)line_h * 0.5f;

    /*
     * The layer holds the mark and the line and nothing else: the black
     * around them is the frame's own fill (#000000). Layer width and height
     * include the round caps extending `rad` on each side.
     */
    int cap_ext = (int)ceilf(rad);
    int lw = (line_w > mark ? line_w : mark) + cap_ext * 2;
    int lh = (track_cy + cap_ext) - g_mark_y + 4;
    int lx = (screen_w - lw) / 2;
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

    float m_alpha = finnboot_mark_alpha();
    float m_scale = finnboot_mark_scale();

    /* ---- the mark ----
     * Center at (g_mark_x + mark/2, g_mark_y + mark/2), scaled by m_scale.
     * Solid white (#FFFFFF) with animated opacity m_alpha.
     */
    if (m_alpha > 0.002f) {
        int cur_sz = (int)lroundf((float)mark * m_scale);
        if (cur_sz < 1) cur_sz = 1;
        int cx = g_mark_x + mark / 2;
        int cy = g_mark_y + mark / 2;
        int ipx, iox, ioy;
        finn_icon_frame(&g_ic_mark, cur_sz, cx, cy, &ipx, &iox, &ioy);

        unsigned a = (unsigned)(m_alpha * 255.0f + 0.5f);
        if (a > 255) a = 255;
        uint32_t tint = (a << 24) | 0x00FFFFFFu;

        const uint32_t *t = finn_icon_ref(&g_ic_mark, ipx, tint, tint, 0);
        if (t) finn_blit(g_layer, g_lw, g_lh, t, ipx, ipx, iox - lx, ioy - ly);
    }

    /* ---- the progress track ----
     * 150px track with round caps (strokeWeight 5 in Figma, #6C6C6C at 40%).
     * Vector 1 in Figma node 1:9 centered at y=541.
     */
    if (m_alpha > 0.002f) {
        float tx = (float)g_line_x - (float)lx - rad;
        float ty = (float)track_cy - (float)ly - rad;
        float tw = (float)line_w + rad * 2.0f;
        float th = (float)line_h;

        unsigned tr_a = (unsigned)(0x66 * m_alpha + 0.5f);
        if (tr_a > 255) tr_a = 255;
        uint32_t tr_col = (tr_a << 24) | 0x006C6C6Cu;

        finn_fill_rrect(g_layer, g_lw, g_lh, tx, ty, tw, th, rad, tr_col);

        /*
         * ---- the lit segment ----
         * Lit portion grows from left to right with round caps.
         * In Figma node 1:10 (Vector 2), it is 15px lit segment (10% progress).
         * As boot progresses, it advances smoothly to 100%.
         */
        float p = finnboot_progress();
        if (p > 0.001f) {
            float lit_len = (float)line_w * p;
            float fw = lit_len + rad * 2.0f;
            if (fw > tw) fw = tw;

            unsigned lit_a = (unsigned)(255.0f * m_alpha + 0.5f);
            if (lit_a > 255) lit_a = 255;
            uint32_t lit_col = (lit_a << 24) | 0x00FFFFFFu;

            finn_fill_rrect(g_layer, g_lw, g_lh, tx, ty, fw, th, rad, lit_col);
        }
    }

    maybe_save_capture(screen_w, screen_h);
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
