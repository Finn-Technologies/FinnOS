#ifndef FINNSETUP_H
#define FINNSETUP_H

#include <stdint.h>
#include "finnstate.h"

/*
 * finnsetup — the FinnOS first-boot setup surface.
 *
 * Redesigned in the Figma design language:
 *   - Wallpaper background (no dimming, no radial glow).
 *   - A centered frosted glass card (matching Start Menu: white 65%, blur, r=8).
 *   - 4-shape FinnOS mark, title, and subtitle using the system type scale.
 *   - Language selector field and Appearance choice toggle styled like the
 *     Settings app (r=4.14, white 85% fills, blue #006EFF accent).
 *   - Primary "Get started" button with #006EFF accent and r=4.14.
 *   - Design honesty: language control is a display-name choice only and
 *     states so explicitly.
 *   - Full keyboard navigation (Tab/Shift-Tab, Arrows, Enter, Space).
 *   - Works identically on software path and GPU path.
 *
 * The marker is the completion gate: /var/lib/finn/setup-done exists only
 * once setup has been completed and written atomically.
 * Two development escapes (must be exactly "1"):
 *   FINN_SETUP_SKIP=1   do not open setup at boot
 *   FINN_SETUP_FORCE=1  show setup even where marker exists
 */

#define FINN_SETUP_DONE_PATH "/var/lib/finn/setup-done"

enum {
    FINNSETUP_NONE = 0,
    FINNSETUP_PANEL,
    FINNSETUP_LANGUAGE,
    FINNSETUP_ACCESSIBILITY,
    FINNSETUP_START
};

int  finnsetup_init(const char *icon_dir, const char *font_dir);
void finnsetup_free(void);

/* Logical state */
void finnsetup_set_open(int open);
int  finnsetup_is_open(void);
int  finnsetup_is_completed(void);

/*
 * Writes the marker atomically and records the language. Returns 0 when the
 * machine now remembers that setup is done, and -1 when it does not.
 */
int  finnsetup_complete(void);

void finnsetup_cycle_language(void);
const char *finnsetup_language(void);

/* en-GB, en-US, fi, de, es, fr — the stored choice, not a locale. */
const char *finnsetup_language_code(void);

/* Keyboard focus over language, appearance, start. */
void finnsetup_focus_next(int dir);   /* +1 forward, -1 back, wrapping */
int  finnsetup_activate(void);         /* act on focus; 0 when there is none */
int  finnsetup_focus_index(void);      /* 0..2, for tests and debug */

/* Visual & animation state */
int  finnsetup_visible(void);
int  finnsetup_running(void);
int  finnsetup_tick(float dt);

/* Pointer interaction */
int  finnsetup_hit(int px, int py);
int  finnsetup_hit_appearance_option(int px, int py);
void finnsetup_hover_at(int px, int py);
void finnsetup_press(int act);

/* Rendering & compositing */
void finnsetup_render(int screen_w, int screen_h, float scale,
                      const finn_state *st);
void finnsetup_blit(uint32_t *dst, int dw, int dh, int dx, int dy);
void finnsetup_blit_scaled(uint32_t *dst, int dw, int dh);
void finnsetup_transform(float *x, float *y, float *w, float *h, float *alpha);
const uint32_t *finnsetup_layer(int *x, int *y, int *w, int *h, unsigned *rev);

#endif
