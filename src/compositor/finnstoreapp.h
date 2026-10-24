#ifndef FINNSTOREAPP_H
#define FINNSTOREAPP_H

#include <stdint.h>
#include <stddef.h>
#include "finnstate.h"
#include "finnkbd.h"

/*
 * finnstoreapp — the Finn Store view inside finn-comp.
 *
 * The catalogue is the OS's, the install list is finnpkg's, and this view is
 * a window onto both: it never invents an app, never claims one is installed
 * that the package manager has not marked, and never offers an install for a
 * catalogue entry whose launcher is not on the machine.
 *
 * Installing and removing go through the control plane (`app.install` /
 * `app.remove`), which is the only writer of the machine's package state.
 * The view then re-reads the registry so what is drawn afterwards is the
 * package manager's answer, not a guess made here.
 */

enum {
    FINNSTORE_HIT_NONE = 0,
    FINNSTORE_HIT_BODY,
    FINNSTORE_HIT_FILTER,
    FINNSTORE_HIT_ROW,          /* out_sub = catalogue index */
    FINNSTORE_HIT_ROW_ACTION,   /* narrow layout: the row's own button */
    FINNSTORE_HIT_ACTION        /* wide layout: the detail pane button */
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

uint32_t finnstoreapp_build_key(float scale);

/* The id of the selected app, or "" when the catalogue is empty. */
const char *finnstoreapp_selection(void);
/* The last action's answer, or "" if nothing has been attempted. */
const char *finnstoreapp_status(void);

#endif
