#ifndef FINNBROWSERAPP_H
#define FINNBROWSERAPP_H

#include <stdint.h>

#include "finnstate.h"
#include "finnkbd.h"

/*
 * finnbrowserapp — the Finn Browser view inside finn-comp.
 *
 * The chrome — tab strip, back/forward/reload, address bar, find bar and
 * status state — is drawn here with the shell's own primitives, in the same
 * frosted glass and type scale as Settings, Finn Files and Finn Store. Only
 * the page rectangle is supplied by the out-of-process engine over
 * FINN_BROWSER_SOCK; no foreign toolkit window is ever created.
 *
 * A control that cannot act (Back with no back history, Forward with no
 * forward history, Reload or Find when the engine is unavailable) is drawn
 * disabled and refuses presses, so the view never pretends a button works
 * when it cannot.
 */

enum {
    FINNBROWSER_HIT_NONE = 0,
    FINNBROWSER_HIT_TAB,            /* out_sub = tab index                */
    FINNBROWSER_HIT_TAB_CLOSE,      /* out_sub = tab index                */
    FINNBROWSER_HIT_TAB_NEW,
    FINNBROWSER_HIT_BACK,
    FINNBROWSER_HIT_FORWARD,
    FINNBROWSER_HIT_RELOAD,
    FINNBROWSER_HIT_ADDRESS,
    FINNBROWSER_HIT_FIND_TOGGLE,
    FINNBROWSER_HIT_FIND_INPUT,
    FINNBROWSER_HIT_FIND_PREV,
    FINNBROWSER_HIT_FIND_NEXT,
    FINNBROWSER_HIT_FIND_CLOSE,
    FINNBROWSER_HIT_VIEWPORT
};

int  finnbrowserapp_init(const char *icon_dir, const char *font_dir);
void finnbrowserapp_free(void);

/* Provider socket descriptors for the compositor's poll loop. */
int  finnbrowserapp_listen_fd(void);
int  finnbrowserapp_client_fd(void);
/* Drain pending engine lines and frames; returns 1 when the view changed. */
int  finnbrowserapp_poll(void);

/* Window lifecycle hooks called by finnwindow when the browser window opens
 * or closes. finnbrowserapp_take_launch_request() returns 1 once when the
 * window opened without a connected engine so finn-comp can ask finnd to
 * launch `browser`. */
void finnbrowserapp_window_opened(void);
void finnbrowserapp_window_closed(void);
int  finnbrowserapp_take_launch_request(void);

/* Every state or frame change bumps the revision; finnwindow folds this into
 * its body and app content keys so a new frame or tab state repaints. */
uint32_t finnbrowserapp_build_key(float scale);

void finnbrowserapp_render(uint32_t *layer, int lw, int lh,
                           int lx0, int ly0, int wx, int wy, int ww, int wh,
                           float scale, const finn_state *st, int focused);

int  finnbrowserapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                        float scale, int *out_sub);
int  finnbrowserapp_press(int hit, int sub, int wx, int wy, int ww, int wh,
                          int px, int py, float scale);
int  finnbrowserapp_move(int wx, int wy, int ww, int wh, int px, int py,
                         float scale);
void finnbrowserapp_release(int wx, int wy, int ww, int wh, int px, int py,
                            float scale);
int  finnbrowserapp_key(const finn_key_event *e);
int  finnbrowserapp_scroll(int clicks, int wx, int wy, int ww, int wh,
                           int px, int py, float scale);

int         finnbrowserapp_open_url(const char *url);
const char *finnbrowserapp_url(void);
const char *finnbrowserapp_title(void);
const char *finnbrowserapp_status(void);

#endif
