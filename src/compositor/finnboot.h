#ifndef FINNBOOT_H
#define FINNBOOT_H

#include <stdint.h>

/*
 * finnboot — the boot screen.
 *
 * The mark, and a progress line that fills as the machine finishes waking
 * up. It is the only screen the user sees before the desktop exists, and
 * it exists because the machine is genuinely not ready: the wallpaper is
 * not decoded, the face is not loaded, the icons are not parsed. The line
 * reports that work; it does not perform a theatre of booting.
 *
 * The design is the mark from the boot screen in the Figma file: four
 * shapes in a 2x2 grid — a rounded square, a circle, a four-point
 * sparkle and a turned square — in white on black, with a 150-unit line
 * below it. The mark is the same drawing as the dock's launcher icon
 * (icons/boot-mark.svg), so there is one mark in the system and not
 * two.
 *
 * Everything here is authored for the 1080p canvas and scaled, like the
 * rest of the shell.
 */

int  finnboot_init(const char *icon_dir);
void finnboot_free(void);

/*
 * The progress the machine has actually reached, 0..1. The compositor
 * raises it as each step of the wake-up finishes; the line eases toward
 * it, so a step that takes a moment reads as movement rather than as a
 * jump.
 */
void finnboot_set_progress(float p);
float finnboot_progress(void);

/* 1 while the line is still moving. */
int  finnboot_tick(float dt);
int  finnboot_running(void);

/*
 * The boot screen, laid out for a screen of this size. The layer is the
 * mark and the line; the black around them is the frame's own fill, which
 * the compositor draws before the layer.
 */
void finnboot_render(int screen_w, int screen_h, float scale);

void finnboot_blit(uint32_t *dst, int dw, int dh, int dx, int dy);
const uint32_t *finnboot_layer(int *x, int *y, int *w, int *h, unsigned *rev);

#endif
