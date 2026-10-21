#ifndef FINNWINDOW_H
#define FINNWINDOW_H

#include <stdint.h>
#include <stddef.h>
#include "finnstate.h"

/*
 * finnwindow — the FinnOS window manager surface (Figma 12:2).
 *
 * Renders floating frosted glass windows (500x300 r=15 in 1280x832 reference
 * units, 0.70 frost) with the top-right Window Options pill (72x24 r=8, 0.50
 * frost) carrying expand, minimize, and close controls.
 *
 * Works identically on the software path (finnwindow_blit_scaled) and the
 * GPU quad path (finnwindow_layer + finnwindow_transform).
 */

#define FINNWINDOW_MAX 8

enum {
    FINNWINDOW_HIT_NONE = 0,
    FINNWINDOW_HIT_BODY,
    FINNWINDOW_HIT_TITLEBAR,
    FINNWINDOW_HIT_EXPAND,
    FINNWINDOW_HIT_MINIMIZE,
    FINNWINDOW_HIT_CLOSE
};

int  finnwindow_init(const char *icon_dir, const char *font_dir, const char *registry);
void finnwindow_free(void);

/* Window lifecycle & control plane operations */
int  finnwindow_open(const char *arg);     /* returns window id (>0), or -1 */
int  finnwindow_close(int id);             /* id<=0 closes focused window   */
int  finnwindow_focus(int id);
int  finnwindow_minimize(int id);          /* toggles minimize on id (<=0 focused) */
int  finnwindow_maximize(int id);          /* toggles maximize on id (<=0 focused) */
int  finnwindow_move(int id, int x, int y);
int  finnwindow_list(char *buf, size_t cap);

/*
 * The band the dock reserves at the bottom edge, in scanout pixels. A
 * maximized window stops at its top, so the room it gets is the room the
 * dock is not using — one dock height and no floating margin.
 */
void finnwindow_set_dock_clearance(int clearance);

/*
 * 1 when a visible window is maximized, or when any visible window reaches
 * `y_top` — the dock's resting top edge. The dock asks this to decide
 * whether to sink; the answer is computed from the resting edge, not from
 * the dock's animated position, so it cannot chase itself.
 */
int  finnwindow_overlaps_band(int y_top);

/* Taskbar button in the dock: restores minimized windows or opens an empty
 * window when none exist. */
void finnwindow_taskbar_toggle(void);

/* Queries */
int  finnwindow_count(void);               /* total open windows (including minimized) */
int  finnwindow_visible_count(void);       /* non-minimized open windows */
int  finnwindow_visible(void);
int  finnwindow_running(void);
int  finnwindow_tick(float dt);

/* Pointer interaction */
int  finnwindow_hit(int px, int py, int *out_win_id);
void finnwindow_hover_at(int px, int py);
void finnwindow_press(int hit_part, int win_id, int px, int py);
int  finnwindow_drag(int px, int py);      /* 1 if dragging a window */
void finnwindow_release(void);

/* Rendering & compositing */
void finnwindow_render(int screen_w, int screen_h, float scale,
                       int dock_x, int dock_y, int dock_w, int dock_h,
                       const finn_state *st);
void finnwindow_blit(uint32_t *dst, int dw, int dh, int dx, int dy);
void finnwindow_blit_scaled(uint32_t *dst, int dw, int dh);
void finnwindow_transform(float *x, float *y, float *w, float *h, float *alpha);
const uint32_t *finnwindow_layer(int *x, int *y, int *w, int *h, unsigned *rev);

/*
 * The hovered or pressed Window Options button lives on its own layer. The
 * window body is expensive — a maximized one is the size of the screen — and
 * the wash changes on every frame of a hover, so keeping it out of the body
 * is what makes hovering a control cost a thousand pixels instead of four
 * million. The overlay is composited immediately after the body, under the
 * dock and every panel, and carries the same open/close transform.
 */
void finnwindow_overlay_blit_scaled(uint32_t *dst, int dw, int dh);
void finnwindow_overlay_transform(float *x, float *y, float *w, float *h, float *alpha);
const uint32_t *finnwindow_overlay_layer(int *x, int *y, int *w, int *h, unsigned *rev);

/* 0 while the shell is whole, rising to 1 as the window layer disintegrates.
 * The compositor uses it for the closing/minimizing dissolve. */
float finnwindow_dust(void);

#endif
