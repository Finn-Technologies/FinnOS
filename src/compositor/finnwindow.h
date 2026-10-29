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

/*
 * finnwindow_open refuses the request that would be the ninth window. It
 * does not evict one to make room: which window to lose is a decision for
 * the caller, not for the window manager, and a window that disappears
 * while it is being used is a lie about what the machine is doing.
 */
#define FINNWINDOW_ERR_LIMIT (-2)

enum {
    FINNWINDOW_HIT_NONE = 0,
    FINNWINDOW_HIT_BODY,
    FINNWINDOW_HIT_TITLEBAR,
    FINNWINDOW_HIT_EXPAND,
    FINNWINDOW_HIT_MINIMIZE,
    FINNWINDOW_HIT_CLOSE,
    FINNWINDOW_HIT_SETTINGS_CTRL,
    /* A control inside one of the window's own app views. The view keeps the
     * sub-hit; the window manager only needs to know the press belongs to
     * the app rather than to the chrome. */
    FINNWINDOW_HIT_APP_CTRL,
    /* The eight resize handles. A window is resized by the edge under the
     * pointer, not by a separate grip: the chrome already owns its border,
     * and a visible handle would be one more thing drawn on the glass. */
    FINNWINDOW_HIT_RESIZE_E,
    FINNWINDOW_HIT_RESIZE_W,
    FINNWINDOW_HIT_RESIZE_N,
    FINNWINDOW_HIT_RESIZE_S,
    FINNWINDOW_HIT_RESIZE_NE,
    FINNWINDOW_HIT_RESIZE_NW,
    FINNWINDOW_HIT_RESIZE_SE,
    FINNWINDOW_HIT_RESIZE_SW
};

/* The edges a window can be snapped to. `restore` puts it back where it was
 * before the snap, which is what a second press on the same edge does too. */
enum {
    FINNWINDOW_SNAP_NONE = 0,
    FINNWINDOW_SNAP_LEFT,
    FINNWINDOW_SNAP_RIGHT,
    FINNWINDOW_SNAP_TOP,
    FINNWINDOW_SNAP_TOPLEFT,
    FINNWINDOW_SNAP_TOPRIGHT,
    FINNWINDOW_SNAP_BOTTOMLEFT,
    FINNWINDOW_SNAP_BOTTOMRIGHT,
    FINNWINDOW_SNAP_RESTORE
};

/* The smallest a window may be made by the pointer. A window smaller than
 * this cannot show its own titlebar controls and the content has nowhere to
 * go, so the resize stops there instead of letting the frame collapse. */
#define FINNWINDOW_MIN_W 360.0f
#define FINNWINDOW_MIN_H 240.0f

#include "finnkbd.h"

/* Deliver keyboard event to focused window (e.g. search field) */
int  finnwindow_key(const finn_key_event *e);

int  finnwindow_init(const char *icon_dir, const char *font_dir, const char *registry);
void finnwindow_free(void);

/* Window lifecycle & control plane operations */
int  finnwindow_open(const char *arg);     /* id (>0), FINNWINDOW_ERR_LIMIT when
                                            * full, -1 on an invalid request */
int  finnwindow_close(int id);             /* id<=0 closes focused window   */
int  finnwindow_focus(int id);
/* Alt+Tab: move focus to the next/previous open window, wrapping. A
 * minimized window is restored by the focus, exactly as a click would. */
int  finnwindow_cycle_focus(int dir);
int  finnwindow_minimize(int id);          /* toggles minimize on id (<=0 focused) */
int  finnwindow_maximize(int id);          /* toggles maximize on id (<=0 focused) */
int  finnwindow_restore(int id);           /* leave maximize/snap, back to saved */
int  finnwindow_move(int id, int x, int y);
int  finnwindow_resize(int id, int w, int h);
int  finnwindow_snap(int id, int edge);    /* FINNWINDOW_SNAP_*; top maximizes */
int  finnwindow_list(char *buf, size_t cap);
/* Geometry and state of one window (id<=0: the focused one). `state` is the
 * same word window.list uses, and `wid` gets the id it resolved to.
 * Returns 0, or -1 when there is no such window. */
int  finnwindow_get(int id, int *wid, int *x, int *y, int *w, int *h,
                    const char **state);

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

/*
 * What the taskbar needs to know, one entry per open window, in z-order from
 * the bottom to the focused window. The taskbar groups them by `app_id`; a
 * window opened with a title but no app has an empty id and is its own tile.
 */
typedef struct {
    int  id;
    char app_id[32];
    char title[64];
    int  minimized;
    int  focused;
} finnwindow_brief;

#define FINNWINDOW_BRIEF_MAX FINNWINDOW_MAX
int  finnwindow_briefs(finnwindow_brief *out, int max);

/* Native Wayland toplevels reuse the shell's stacking and window chrome.
 * Surface pixels are copied into compositor-owned memory (bounded to 64 Mi
 * pixels across clients) so a client can reuse its SHM buffer immediately. */
int  finnwindow_external_open(const char *app_id, const char *title,
                               int surface_w, int surface_h);
int  finnwindow_external_title(int id, const char *title);
/* The client named its application after the window already existed
 * (set_app_id arrives after get_toplevel). Regroups the taskbar tile
 * rather than reopening. */
int  finnwindow_external_app_id(int id, const char *app_id);
int  finnwindow_external_set_ssd(int id, int ssd);
int  finnwindow_external_commit(int id, const uint32_t *pixels,
                                int width, int height, int stride, int opaque);
int  finnwindow_external_destroy(int id);
/* A popup is a chromeless child of its parent's window: no titlebar, no
 * options pill, no resize handles, no slot in the 8-window budget and no
 * row in window.list. It draws above its parent at a parent-relative
 * offset, dismisses on an outside press or Escape, and never takes focus
 * away as a window of its own. */
int  finnwindow_popup_open(int parent_id, int x, int y,
                           int surface_w, int surface_h);
int  finnwindow_popup_move(int id, int x, int y);
int  finnwindow_popup_commit(int id, const uint32_t *pixels,
                             int width, int height, int stride, int opaque);
int  finnwindow_popup_destroy(int id);
int  finnwindow_popup_hit(int px, int py, int *id, int *sx, int *sy);
typedef void (*finnwindow_external_close_handler)(int id, void *userdata);
void finnwindow_set_external_close_handler(finnwindow_external_close_handler handler,
                                           void *userdata);
int  finnwindow_external_hit(int px, int py, int *id, int *sx, int *sy);
int  finnwindow_external_coordinates(int id, int px, int py, int *sx, int *sy);
int  finnwindow_external_contains(int id);
int  finnwindow_external_focused(void);
int  finnwindow_external_get_size(int id, int *width, int *height);

/*
 * Activate the taskbar group for `app_id`: focus its most recent window, or
 * minimize the group when it is already the focused window. `window_id` is
 * the fallback for a window that has no app id (pass 0 for an app group).
 * Returns 1 when a window was there to act on, 0 when the caller should
 * launch the app instead.
 */
int  finnwindow_taskbar_activate(const char *app_id, int window_id);

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
int  finnwindow_drag(int px, int py);      /* 1 if dragging or resizing */
/* Release the pointer. The position is the release point, so a drag that
 * ends against an edge can snap and a press that did not move can still be
 * a click. */
void finnwindow_release(int px, int py);
/* Wheel clicks over a window, delivered to the app view under the pointer.
 * Returns 1 when something scrolled. */
int  finnwindow_scroll(int px, int py, int clicks);

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
