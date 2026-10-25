#ifndef FINNMENU_H
#define FINNMENU_H

#include <stdint.h>
#include "finnstate.h"
#include "finnkbd.h"

/*
 * finnmenu — the FinnOS Start Menu.
 *
 * The surface that opens from the dock's launcher button: the system's
 * catalogue of apps, laid out as a grid, in the same glass and the same type
 * as the Control Center and the clock page. It is a surface and not a window,
 * so it lives in the compositor's frame loop with the rest of the shell.
 *
 * What is on it is what the machine knows. The grid comes from the
 * application registry (finnapps), and an app the package manager has not
 * installed is drawn as what it is — present in the catalogue, absent from the
 * machine — rather than as a live tile that does nothing when it is pressed.
 *
 * The menu never launches anything itself: it reports what was pressed and
 * the compositor asks the control plane to act on it. The control plane is
 * the one permissioned entry point, and it keeps the audit trail.
 */

enum {
    FINNMENU_NONE = 0,
    FINNMENU_PANEL,        /* inside the menu, but not on a control */
    FINNMENU_RESULT,       /* a row in the unified result list */
    FINNMENU_SEARCH,       /* the search field */
    FINNMENU_PIN,          /* pin/unpin the previewed app on the taskbar */
    FINNMENU_RESTART,
    FINNMENU_SHUTDOWN
};

/*
 * What a result row points at. The app index is the registry index; the
 * settings index is the page index; the action index is one of the
 * FINNMENU_ACTION_* values below. The menu does not act on any of them: it
 * hands the result back and the compositor asks the control plane or opens
 * the window, because there is one door for both.
 */
enum {
    FINNMENU_RESULT_NONE = 0,
    FINNMENU_RESULT_APP,
    FINNMENU_RESULT_SETTINGS,
    FINNMENU_RESULT_ACTION
};

enum {
    FINNMENU_ACTION_RESTART = 0,
    FINNMENU_ACTION_SHUTDOWN,
    FINNMENU_ACTION_APPEARANCE
};

typedef struct {
    int kind;
    int index;
    int row;       /* the row that was hit, for the hover wash */
} finnmenu_result;

int  finnmenu_init(const char *icon_dir, const char *font_dir,
                   const char *registry);
void finnmenu_free(void);

/* The logical state: whether the menu accepts input. */
void finnmenu_set_open(int open);
int  finnmenu_is_open(void);

/* The visual state, which lags the logical one by the length of a close. */
int  finnmenu_visible(void);
int  finnmenu_settled(void);
float finnmenu_openness(void);

/* How dark the room goes behind the menu, 0..1 of FINN_MOTION_SCRIM. The
 * compositor dims the frame by this much, so the menu is the only lit thing
 * in the room. The track is the menu's own: the room dims before the menu
 * opens and brightens before it shuts. */
float finnmenu_scrim(void);

int  finnmenu_tick(float dt);       /* 1 while any track moved this frame */
int  finnmenu_running(void);

/*
 * Rebuild the menu, anchored to the launcher button in the dock. The dock
 * geometry is passed for the fallback and for the build key; the button's own
 * rect comes from finndock_launcher().
 */
void finnmenu_render(int screen_w, int screen_h, float scale,
                     int dock_x, int dock_y, int dock_w, int dock_h,
                     const finn_state *st);

void finnmenu_blit(uint32_t *dst, int dw, int dh, int dx, int dy);
void finnmenu_blit_scaled(uint32_t *dst, int dw, int dh);
void finnmenu_transform(float *x, float *y, float *w, float *h, float *alpha);
const uint32_t *finnmenu_layer(int *x, int *y, int *w, int *h, unsigned *rev);

/* Which part of the menu is at this scanout position. A result row fills in
 * out, so the caller knows whether it is an app, a settings page or an
 * action without asking the menu to launch it. */
int  finnmenu_hit(int px, int py, finnmenu_result *out);

/*
 * The pointer moved. A control under it takes a wash of ink and a result row
 * becomes the preview; nothing else moves.
 */
void finnmenu_hover_at(int px, int py);

/* A control went down, or FINNMENU_NONE for a release. */
void finnmenu_press(int what);

/*
 * The pin list changed under the menu (the taskbar wrote it). The menu draws
 * a pin button in the preview, so it has to forget the state it cached when
 * it opened; the next frame rereads it.
 */
void finnmenu_pins_changed(void);

/*
 * Focus the search field. The dock's search button is a request to search,
 * not a second way to open the app list.
 */
void finnmenu_focus_search(void);
void finnmenu_blur_search(void);

/*
 * Keyboard delivery while the menu is open. Returns 1 when the key was used.
 * On Enter over a result, activate is filled in for the compositor to act on.
 */
int finnmenu_key(const finn_key_event *e, finnmenu_result *activate);

#endif
