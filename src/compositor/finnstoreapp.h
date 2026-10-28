#ifndef FINNSTOREAPP_H
#define FINNSTOREAPP_H

#include <stdint.h>
#include <stddef.h>
#include "finnstate.h"
#include "finnkbd.h"

/*
 * finnstoreapp — the Finn Store view inside finn-comp.
 *
 * The Store is five pages under the Settings chrome: Home, Native Apps,
 * Linux Apps, Windows Apps and Library. Each page is one source, and the
 * source is named in the navigation rather than hidden behind a filter, so
 * a package never appears without the place it came from.
 *
 * The rules the view keeps:
 *
 *   - The native catalogue is the OS registry. An app the registry does not
 *     know is not offered, and an entry whose launcher is missing is shown
 *     as unavailable with the reason, never as a button that would fail.
 *
 *   - Installed is the package manager's answer. Native state comes from
 *     finnpkg's install list, Flatpak state from flatpak.list, and Windows
 *     state from wine.list. Nothing is called installed because an install
 *     job exited.
 *
 *   - Every package change and every launch goes through finnd. This view
 *     never edits package state or starts a process itself.
 *
 * Linux Apps uses flatpak.list/search/install/remove/run. Windows Apps
 * searches winget through finnd and installs through the audited job path;
 * the installer itself is never run by the compositor. Library combines the
 * three installed sets and launches each through the verb that owns it.
 *
 * Network and package work runs off the compositor thread. The Store draws
 * what the backends said and says why when they said nothing.
 */

enum {
    FINNSTORE_HIT_NONE = 0,
    FINNSTORE_HIT_BODY,
    FINNSTORE_HIT_FILTER,
    FINNSTORE_HIT_FILTER_CLEAR,
    FINNSTORE_HIT_NAV,          /* out_sub = page */
    FINNSTORE_HIT_CARD,         /* out_sub = item; opens detail on Home/Native */
    FINNSTORE_HIT_CARD_ACTION,  /* out_sub = item; primary action */
    FINNSTORE_HIT_CARD_REMOVE,  /* out_sub = item; second press confirms */
    FINNSTORE_HIT_SEE_ALL,      /* out_sub = page */
    FINNSTORE_HIT_REFRESH,      /* re-read the page's source */
    FINNSTORE_HIT_BACK,
    FINNSTORE_HIT_DETAIL_ACTION,
    FINNSTORE_HIT_DETAIL_REMOVE
};

int  finnstoreapp_init(const char *icon_dir, const char *font_dir,
                       const char *registry);
void finnstoreapp_free(void);

void finnstoreapp_render(uint32_t *layer, int lw, int lh,
                         int lx0, int ly0, int wx, int wy, int ww, int wh,
                         float scale, const finn_state *st, int focused);

int  finnstoreapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                      float scale, int *out_sub);
/* Returns 1 when the press changed something the window has to redraw. */
int  finnstoreapp_press(int hit, int sub, int px, int py, float scale);
int  finnstoreapp_key(const finn_key_event *e);
int  finnstoreapp_scroll(int clicks, float scale);
int  finnstoreapp_tick(float dt);
int  finnstoreapp_running(void);

uint32_t finnstoreapp_build_key(float scale);

/* The id of the selected app, or "" when the page is empty. */
const char *finnstoreapp_selection(void);
/* The last action's answer, or "" if nothing has been attempted. */
const char *finnstoreapp_status(void);

#endif
