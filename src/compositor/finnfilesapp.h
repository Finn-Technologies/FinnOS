#ifndef FINNFILESAPP_H
#define FINNFILESAPP_H

#include <stdint.h>

#include "finnstate.h"
#include "finnkbd.h"

/*
 * finnfilesapp — the Finn Files view inside finn-comp.
 *
 * A real browser over the machine's filesystem, drawn in the window the
 * compositor owns. It reads as the desktop user, not as the compositor's
 * root: the app calls finnapp_fs_enter() around every directory read.
 *
 * It is read-only in this build. The compositor's unit mounts the home
 * directory read-only, and a file manager that offered a delete it could not
 * perform would be lying. The view says so in its own footer.
 */

enum {
    FINNFILES_HIT_NONE = 0,
    FINNFILES_HIT_ROW,          /* out_sub = visible row index            */
    FINNFILES_HIT_PLACE,        /* out_sub = place index                  */
    FINNFILES_HIT_FILTER,
    FINNFILES_HIT_UP
};

#define FINNFILES_MAX_ENTRIES 512

int  finnfilesapp_init(const char *icon_dir, const char *font_dir);
void finnfilesapp_free(void);

/* Every state change bumps a revision; the window manager folds this into
 * its body key so a navigation or a selection repaints the window. */
uint32_t finnfilesapp_build_key(float scale);

void finnfilesapp_render(uint32_t *layer, int lw, int lh,
                         int lx0, int ly0, int wx, int wy, int ww, int wh,
                         float scale, const finn_state *st, int focused);

int  finnfilesapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                      float scale, int *out_sub);
int  finnfilesapp_press(int hit, int sub, int px, int py, float scale);
int  finnfilesapp_key(const finn_key_event *e);
int  finnfilesapp_scroll(int clicks, int px, int py, float scale);
void finnfilesapp_release(void);

/* Control-plane navigation. A path is absolute and is resolved before it is
 * used, so `..` and symlinks cannot name something the user did not ask
 * for. Returns 0, or -1 with the reason in finnfilesapp_error(). */
int  finnfilesapp_open_path(const char *path);
int  finnfilesapp_select(const char *name);
const char *finnfilesapp_path(void);
const char *finnfilesapp_selection(void);
const char *finnfilesapp_error(void);

#endif
