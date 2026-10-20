#ifndef FINNCC_H
#define FINNCC_H

#include <stdint.h>
#include "finnstate.h"

/*
 * finncc — the FinnOS Control Center.
 *
 * A system surface, not a window: it floats above the wallpaper like the
 * dock does, anchored to the tray pill that opens it. The arrangement follows
 * the reference: a connectivity pair, the brightness and volume bars whose
 * glyph circles touch, then the remaining real controls — Appearance, Wired
 * and Power — in the same frosted tile language.
 *
 * Every control is bound to finnstate, so a tile is only lit when the
 * machine can actually do what the tile claims: a machine with no wireless
 * adapter shows "No adapter" over a grey circle, rather than a switch that
 * lies.
 */

enum {
    FINNCC_NONE = 0,
    FINNCC_PANEL,        /* inside the panel, but not on a control */
    FINNCC_WIFI,
    FINNCC_BLUETOOTH,
    FINNCC_APPEARANCE,
    FINNCC_BRIGHTNESS,
    FINNCC_VOLUME
};

int  finncc_init(const char *icon_dir, const char *font_dir);
void finncc_free(void);

/* The logical state: whether the panel accepts input. */
void finncc_set_open(int open);
int  finncc_is_open(void);

/* The visual state, which lags the logical one by the length of a close. */
int  finncc_visible(void);
int  finncc_settled(void);
float finncc_openness(void);     /* 0..1, the animated open value */

/* A control went down, or FINNCC_NONE for a release. */
void finncc_press(int act);

int  finncc_tick(float dt);      /* 1 while any track moved this frame */
int  finncc_running(void);       /* 1 while another frame is still due */

/* Rebuild the panel, anchored to the tray pill's left edge and above the
 * dock. The dock geometry is passed for the fallback and the build key; the
 * pill rect itself comes from finndock_tray(). */
void finncc_render(int screen_w, int screen_h, float scale,
                   int dock_x, int dock_y, int dock_w, int dock_h,
                   const finn_state *st);
void finncc_blit(uint32_t *dst, int dw, int dh, int dx, int dy);

/* Blit through the open/close transform: the panel grows out of the tray
 * corner it is anchored to rather than fading in place. */
void finncc_blit_scaled(uint32_t *dst, int dw, int dh);

/* The same transform, resolved for a backend that draws the layer as a quad
 * instead of resampling it. */
void finncc_transform(float *x, float *y, float *w, float *h, float *alpha);

/* The layer and its revision, for a backend that keeps it on the GPU. */
const uint32_t *finncc_layer(int *x, int *y, int *w, int *h, unsigned *rev);

/* Which control is at this scanout position. Sliders report the fraction
 * they would take at that x through out01. */
int  finncc_hit(int px, int py, float *out01);

/* Fraction a slider would take at this x, for press-and-drag. */
float finncc_drag(int action, int px);

#endif
