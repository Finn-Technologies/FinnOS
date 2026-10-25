#ifndef FINNDOCK_H
#define FINNDOCK_H

#include <stdint.h>
#include "finnstate.h"

/*
 * finndock — the FinnOS desktop shell chrome (ui/Desktop.fui).
 *
 * Draws the dock: an app-dock bubble, the launcher bubble, and the tray
 * pill. Everything is composed into an offscreen layer so the compositor
 * can re-composite a frame by blitting one cached tile, rather than
 * re-rasterising the SVG iconography on every pointer move.
 *
 * Colours are 0xAARRGGBB, matching the DRM XRGB8888 scanout buffer.
 */

enum {
    FINNDOCK_HIT_NONE = 0,
    FINNDOCK_HIT_TASK,        /* a taskbar tile; ask finndock_task_at which */
    FINNDOCK_HIT_LAUNCHER,
    FINNDOCK_HIT_SEARCH,
    FINNDOCK_HIT_WINDOWS,
    FINNDOCK_HIT_TRAY,
    FINNDOCK_HIT_CLOCK        /* the clock end of the pill: its own page */
};

/*
 * One taskbar tile: an app that is pinned, running, or both. The dock draws
 * what the compositor tells it; it does not read the registry or the window
 * manager itself. `id` is the app id, or "@<window id>" for a window opened
 * with a title and no app, which is its own tile and cannot be pinned.
 */
#define FINNDOCK_APPS_MAX 12
typedef struct {
    char id[32];
    char name[48];
    char icon[32];      /* an SVG in the dock's icon directory, or empty */
    int  windows;       /* open windows grouped under this app */
    int  focused;       /* one of them is the focused window */
    int  minimized;     /* it has windows and every one is minimized */
    int  pinned;
} finndock_app;

int  finndock_init(const char *icon_dir, const char *font_dir);
void finndock_free(void);

/* Rebuild the dock layer for the given screen and density. */
void finndock_render(int screen_w, int screen_h, float scale, const finn_state *st);

/*
 * The dock has two states. At rest it floats clear of the bottom edge as
 * three bubbles. When a window is maximized, or reaches the band the dock
 * rests in, the dock sinks flush to the bottom edge and the bubbles become
 * one surface with the controls drawn closer together — the window gets the
 * room the gaps were using.
 *
 * The state is asked for, not animated here: a caller says which state it
 * wants every frame, and the dock takes the one changed answer as a track.
 */
void finndock_set_sunk(int sunk);

/* The top edge of the dock at rest, in scanout pixels. This is the line a
 * window has to reach to be in the dock's space; it does not move with the
 * dock's own animation, so the answer cannot oscillate. */
int  finndock_rest_top(int screen_h, float scale);

/* Blit the cached layer into a scanout buffer at the given origin. */
void finndock_blit(uint32_t *dst, int dw, int dh, int dx, int dy);

int  finndock_height(float scale);
/* Centre of the taskbar tile for an app in scanout pixels (for the minimize
 * zoom). Falls back to the pill centre when the app has no tile. */
void finndock_app_icon_for(const char *app_id, int *x, int *y);
void finndock_app_icon(int *x, int *y);
int  finndock_visible(void);

/* Replace the taskbar's app list. Called by the compositor from the window
 * manager and the pin store; the dock only draws the answer. */
void finndock_set_apps(const finndock_app *apps, int n);

/* Bounding box of the last render, in scanout pixels. */
void finndock_geometry(int *x, int *y, int *w, int *h);

/* the tray pill's screen rect — the Control Center anchors to its left edge */
void finndock_tray(int *x, int *y, int *w, int *h);

/* the clock's target inside the pill — the clock page anchors to its right
 * edge, and clicking it opens that page rather than the Control Center */
void finndock_clock(int *x, int *y, int *w, int *h);

/* the launcher button's screen rect — the Start Menu anchors to its left
 * edge, and clicking it opens the Start Menu */
void finndock_launcher(int *x, int *y, int *w, int *h);

/* 1 when the wall clock has moved into a new minute since the last render,
 * so the compositor can force the frame the clock needs. Cheap enough to call
 * every loop iteration, and returns 1 at most once per minute change. */
int  finndock_clock_tick(void);

/* The layer itself, for a backend that pastes it on the GPU rather than
 * memcpy'ing it. `rev` changes only when the pixels do, so an upload can be
 * skipped rather than repeated. */
const uint32_t *finndock_layer(int *x, int *y, int *w, int *h, unsigned *rev);

/* Which dock control is at this scanout position, if any. */
int  finndock_hit(int px, int py);

/* Which taskbar tile is at this position: an index into the list given to
 * finndock_set_apps, or -1. Padding and the gaps between tiles are -1. */
int  finndock_task_at(int px, int py);

/*
 * ---------------- motion ----------------
 *
 * The dock answers the pointer with colour and nothing else. A control under
 * the pointer takes a wash of ink and returns to the glass when it leaves;
 * a control under the button takes a slightly deeper one. Nothing changes
 * size, nothing changes position — the surface never moves under the pointer,
 * which is also why the shell stays silent while the pointer crosses it.
 */
void finndock_press(int which);   /* button went down on this control, or NONE */
/* The same, for a taskbar tile, so the wash lands on the tile that was
 * pressed rather than on the whole pill. */
void finndock_press_task(int index);

/* The status block under the pointer (px, py in scanout pixels) takes the
 * hover wash; everything else in the dock takes nothing. Safe to call every
 * iteration: only a change of answer starts anything. */
void finndock_hover_at(int px, int py);

/* The tray's brighter "well" is the Control Center's anchor: the compositor
 * feeds it the panel's openness so the mark belongs to the open panel rather
 * than to the dock. 0 = flat pill, 1 = fully lifted. */
void finndock_set_well(float amount);

/* The clock's well, driven by the clock page's openness. Two controls, two
 * anchors: the pill lifts at whichever end was asked for. */
void finndock_set_clock_well(float amount);

/* The launcher button's well, driven by Start Menu openness. */
void finndock_set_launcher_well(float amount);
int  finndock_tick(float dt);     /* 1 while the squeeze moved this frame */
int  finndock_running(void);      /* 1 while another frame is still due */

#endif
