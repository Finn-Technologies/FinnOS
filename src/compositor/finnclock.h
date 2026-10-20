#ifndef FINNCLOCK_H
#define FINNCLOCK_H

#include <stdint.h>
#include "finnstate.h"

/*
 * finnclock — the FinnOS clock page.
 *
 * The tray clock is a control, not a label: it opens a page of its own, in the
 * same language as the Control Center — frosted panel, tiles, one type scale,
 * the same motion — rather than a macOS-style notification drawer.
 *
 * What is on it is what the machine actually knows:
 *   - the time, the date and the week, read from the same wall clock the dock
 *     uses
 *   - the notifications the system has posted this session (finnnotify), or
 *     an honest empty state when nothing has happened
 *
 * Deliberately not a calendar: a month grid is a thing to consult, and this
 * page is a thing to glance at. The date lives in the header.
 *
 * It is anchored to the clock itself: the panel's right edge meets the pill's
 * right edge and it grows out of the clock's corner, so the thing you pressed
 * is the thing the page came from.
 */

enum {
    FINNCLOCK_NONE = 0,
    FINNCLOCK_PANEL,      /* inside the page, but not on a control */
    FINNCLOCK_CLEAR       /* the Clear control, when there is something to clear */
};

int  finnclock_init(const char *font_dir);
void finnclock_free(void);

/* The logical state: whether the page accepts input. */
void finnclock_set_open(int open);
int  finnclock_is_open(void);

/* The visual state, which lags the logical one by the length of a close. */
int  finnclock_visible(void);
int  finnclock_settled(void);
float finnclock_openness(void);

int  finnclock_tick(float dt);       /* 1 while any track moved this frame */
int  finnclock_running(void);

void finnclock_render(int screen_w, int screen_h, float scale,
                      int dock_x, int dock_y, int dock_w, int dock_h,
                      const finn_state *st);
void finnclock_blit(uint32_t *dst, int dw, int dh, int dx, int dy);
void finnclock_blit_scaled(uint32_t *dst, int dw, int dh);
void finnclock_transform(float *x, float *y, float *w, float *h, float *alpha);
const uint32_t *finnclock_layer(int *x, int *y, int *w, int *h, unsigned *rev);

/* Which part of the page is at this scanout position. */
int  finnclock_hit(int px, int py);

#endif
