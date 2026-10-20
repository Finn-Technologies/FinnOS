#ifndef FINNMENU_H
#define FINNMENU_H

#include <stdint.h>
#include "finnstate.h"

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
    FINNMENU_APP,          /* an app tile; the index comes back in app_index */
    FINNMENU_RESTART,
    FINNMENU_SHUTDOWN
};

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

/* Which part of the menu is at this scanout position. */
int  finnmenu_hit(int px, int py, int *app_index);

/*
 * The pointer moved. A control under it takes a wash of ink; nothing else
 * moves, and a control that is not under the pointer takes nothing.
 */
void finnmenu_hover_at(int px, int py);

/* A control went down, or FINNMENU_NONE for a release. */
void finnmenu_press(int what);

#endif
