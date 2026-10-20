#ifndef FINNPOINTER_H
#define FINNPOINTER_H

#include <stdint.h>

/*
 * finnpointer — the FinnOS pointer.
 *
 * There is no Wayland and no X11, so nothing else is going to move a cursor
 * for us: the compositor reads evdev directly and composites the pointer
 * sprite into the scanout buffer itself.
 *
 * Two kinds of pointing device exist in the wild and both must work:
 *   relative — an ordinary mouse or touchpad (EV_REL, REL_X/REL_Y)
 *   absolute — a tablet, a touchscreen, and notably the Parallels
 *              "smart mouse" virtual tablet (EV_ABS, ABS_X/ABS_Y)
 * An absolute device reports its position over its own axis range, which is
 * normalised onto the scanout, exactly as a graphics tablet does.
 *
 * All matching devices are opened at once: which one the host drives is not
 * something we get to choose, so we listen to every one of them.
 */

#define FINN_PTR_MAX 4

typedef struct {
    int  fd;
    int  is_abs;            /* 1 when the device drives ABS_X/ABS_Y       */
    int  minx, maxx;        /* axis range, absolute devices only          */
    int  miny, maxy;
    int  ax, ay;            /* latest raw absolute sample                 */
    int  pending;           /* axes seen since the last EV_SYN            */
    char path[64];
} finn_ptr_dev;

typedef struct {
    finn_ptr_dev dev[FINN_PTR_MAX];
    int          ndev;

    int       x, y;         /* cursor hotspot, in scanout pixels */
    int       buttons;      /* FINN_BTN_* bitmask                */
    int       dirty;
    int       screen_w, screen_h;

    uint32_t *sprite;       /* cursor, 0xAARRGGBB */
    int       sprite_px;
    int       hot_x, hot_y;
} finn_pointer;

/* Button bits as the control plane reports and injects them. */
#define FINN_BTN_LEFT    0x01
#define FINN_BTN_RIGHT   0x02
#define FINN_BTN_MIDDLE  0x04

/* Opens every pointer-capable device and loads the cursor art. */
int  finn_pointer_open(finn_pointer *p, int screen_w, int screen_h,
                       const char *cursor_svg);
void finn_pointer_close(finn_pointer *p);

/* Copies the evdev fds to poll into out[]. Returns how many were written. */
int  finn_pointer_fds(const finn_pointer *p, int *out, int max);

/* Drains pending events from every device. Returns 1 if the pointer moved
 * or a button changed, 0 otherwise. */
int  finn_pointer_poll(finn_pointer *p);

/* Composites the cursor into an XRGB8888 buffer at the current position. */
void finn_pointer_draw(const finn_pointer *p, uint32_t *buf, int w, int h);

#endif
