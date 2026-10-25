#ifndef FINNTASKBAR_H
#define FINNTASKBAR_H

#include <stddef.h>

/*
 * finntaskbar — the apps the user has pinned to the taskbar.
 *
 * A pin is a preference, not a package: it says "keep this app's tile on the
 * taskbar", and the tile still shows the truth about the machine. An app that
 * is not installed (or whose launcher is not executable) is not drawn as a
 * live tile, however it got pinned; the pin is kept so it comes back when the
 * app does.
 *
 * The list lives in the one settings store under `taskbar.pins`, as a
 * comma-separated list of app ids in pin order. An absent key means "the
 * default pins"; an empty value means "the user pinned nothing", which is a
 * different answer and has to stay one. Every write is validated: an id that
 * could not be an app id is refused rather than stored and ignored later.
 */

#define FINNTASKBAR_MAX 12
#define FINNTASKBAR_ID_MAX 32

/* Open the default pins. These are first-party apps the catalogue ships with;
 * a machine that installs none of them simply draws no pinned tiles. */
int finntaskbar_defaults(char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX],
                         int max);

/* Pin order, or the defaults when the key is absent. Returns the number of
 * usable ids, or -1 when the store could not be read at all. */
int finntaskbar_pins(char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int max);

/* 1 when the id is pinned, 0 when it is not, -1 when the store is unreadable.
 * A first run (no key) has the defaults pinned. */
int finntaskbar_is_pinned(const char *app_id);

/* Add or remove one id, preserving the order of the rest. `now_pinned` gets
 * the new state (1 pinned, 0 unpinned). Returns 0 on success including when
 * the pin was already in the requested state, -1 on a refused id or a failed
 * write. */
int finntaskbar_toggle(const char *app_id, int *now_pinned);

/* The testable forms, on an explicit settings path. */
int finntaskbar_pins_at(const char *path,
                        char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int max);
int finntaskbar_is_pinned_at(const char *path, const char *app_id);
int finntaskbar_toggle_at(const char *path, const char *app_id, int *now_pinned);

#endif
