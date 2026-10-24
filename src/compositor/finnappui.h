#ifndef FINNAPPUI_H
#define FINNAPPUI_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <math.h>

#include "finntext.h"

/*
 * finnappui — the small pieces every in-compositor app view needs.
 *
 * Files and the Store are windows, not programs: there is no client surface
 * protocol for them to draw through, so their views live in this process like
 * Settings does. They share a design scale, a text ellipsis and the identity
 * of the desktop user, and none of those should be copied into each app.
 *
 * Everything is in the reference units the rest of the shell uses. A module
 * multiplies by its own scale and never invents a pixel size.
 */

#define FINN_APP_REF 1.298077f

static inline int finnapp_rs(float v, float s) {
    return (int)lroundf(v * s);
}

static inline int finnapp_font_px(float s, float base) {
    int px = finnapp_rs(base, s);
    return px < 6 ? 6 : px;
}

/*
 * Fit `src` into `maxw` pixels, with an ellipsis when it does not fit. The
 * destination always stays valid UTF-8: the cut is made at a character
 * boundary by measuring candidates rather than by slicing bytes.
 */
void finnapp_ellipsis(const finn_font *f, char *dst, size_t cap,
                      const char *src, float maxw);

/* "1.4 MB", "412 B" — decimal units, the ones a person reads. */
void finnapp_format_size(unsigned long long bytes, char *out, size_t cap);

/* "24 Oct 2026 18:42" in the machine's own timezone. */
void finnapp_format_time(long long mtime, char *out, size_t cap);

/* The home directory of the desktop user, from uid 1000 (the session's
 * user), falling back to $HOME and then "/". */
const char *finnapp_home(char *buf, size_t cap);

/* The session user's uid/gid, or -1 when the account does not exist. */
uid_t finnapp_desktop_uid(void);
gid_t finnapp_desktop_gid(void);

/*
 * Read the filesystem as the desktop user, not as the compositor's root.
 *
 * The compositor runs as root because it owns the DRM device; that does not
 * mean a file browser in it should be able to read /etc/shadow. Dropping the
 * fsuid for the duration of a directory read makes the kernel apply the
 * user's permissions. The calls are per-thread, and this code is called from
 * the compositor's single input/render thread.
 */
void finnapp_fs_enter(void);
void finnapp_fs_leave(void);

#endif
