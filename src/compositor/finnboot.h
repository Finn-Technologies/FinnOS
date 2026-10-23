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
 * The design matches Figma frame 'Boot' 1:2 exactly:
 *   - Background: solid black #000000
 *   - Mark: 50x50 boot mark (boot-mark.svg: rounded square, circle,
 *     4-point star, rotated rounded square in solid white), centered on screen
 *     at (615, 391) on the 1280x832 reference.
 *   - Progress track: 150px wide, centered at (565, 541), 5px stroke weight,
 *     round caps, stroke color #6C6C6C at 40% (RGB 43, 43, 43 over black).
 *   - Lit segment: 15px lit segment in Figma reference (10% progress),
 *     5px stroke weight, round caps, solid white #FFFFFF.
 *
 * Real wake-up progression advances the lit segment from initial progress
 * to 100%. The mark fades and scales in and out per docs/design/motion.md.
 * The handoff to the desktop is a smooth crossfade.
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

/* 1 while the line or mark animations are still moving. */
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

/*
 * Motion in/out & handoff to desktop.
 * finnboot_start_exit triggers the mark scaling and fading out as well as
 * the desktop handoff dissolve. finnboot_handoff_alpha returns the remaining
 * boot screen alpha (1.0 -> 0.0) during the handoff crossfade.
 */
void  finnboot_start_exit(void);
int   finnboot_exiting(void);
float finnboot_handoff_alpha(void);
float finnboot_mark_alpha(void);
float finnboot_mark_scale(void);

#endif
