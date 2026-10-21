#ifndef FINNSETUP_H
#define FINNSETUP_H

#include <stdint.h>
#include "finnstate.h"

/*
 * finnsetup — the FinnOS first-boot & device setup surface (Figma 2:14 & 7:10).
 *
 * Full-screen onboarding surface with the left hero card (setup.webp rounded
 * r=30) and the right configuration column (FinnOS branding, Welcome heading,
 * Language selector pill, Accessibility appearance button, and Start button).
 *
 * Works identically on the software path (finnsetup_blit_scaled) and the
 * GPU quad path (finnsetup_layer + finnsetup_transform).
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
void finnsetup_complete(void);
void finnsetup_cycle_language(void);
const char *finnsetup_language(void);

/* Visual & animation state */
int  finnsetup_visible(void);
int  finnsetup_running(void);
int  finnsetup_tick(float dt);

/* Pointer interaction */
int  finnsetup_hit(int px, int py);
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
