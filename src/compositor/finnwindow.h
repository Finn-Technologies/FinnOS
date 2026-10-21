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

#endif
