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
 * Devices come and go. A machine may boot with no pointer at all and gain
 * one when a mouse is plugged in, so the device list is grown at runtime and
 * /dev/input is watched: a plug or an unplug is noticed even while the
 * compositor is blocked in poll.
 */

/*
 * The size of the poll fd array the compositor needs, not a cap on how many
 * devices may be open. The device list itself is allocated and grows.
 */
#define FINN_PTR_MAX 16

typedef struct {
    int  fd;
    int  is_abs;            /* 1 when the device drives ABS_X/ABS_Y       */
    int  minx, maxx;        /* axis range, absolute devices only          */
    int  miny, maxy;
    int  ax, ay;            /* latest raw absolute sample                 */
    int  pending;           /* axes seen since the last EV_SYN            */
    int  drop;              /* discarding until the next SYN_REPORT       */
    int  buttons;           /* buttons this device holds, so an unplug
                             * releases exactly what it was holding       */
    char path[64];
} finn_ptr_dev;

typedef struct {
    finn_ptr_dev *dev;      /* grown with realloc as devices appear       */
    int           ndev;
    int           cap;

    int       watch_fd;     /* inotify on /dev/input, -1 when unavailable */
    int       watch_wd;

    int       x, y;         /* cursor hotspot, in scanout pixels */
    int       buttons;      /* FINN_BTN_* bitmask                */
    int       wheel;        /* REL_WHEEL clicks not yet taken    */
    int       dirty;
    int       grab;         /* 1 while our VT owns the seat      */
    int       screen_w, screen_h;

    uint32_t *sprite;       /* cursor, 0xAARRGGBB */
    int       sprite_px;
    int       hot_x, hot_y;
} finn_pointer;

/* Button bits as the control plane reports and injects them. */
#define FINN_BTN_LEFT    0x01
#define FINN_BTN_RIGHT   0x02
#define FINN_BTN_MIDDLE  0x04

/* Opens every pointer-capable device and loads the cursor art. A machine
 * with no pointer yet is not an error: the watch picks one up later. */
int  finn_pointer_open(finn_pointer *p, int screen_w, int screen_h,
                       const char *cursor_svg);
void finn_pointer_close(finn_pointer *p);

/* Releases or re-takes EVIOCGRAB across VT switches so the pointer is not
 * held while another console owns the seat. */
void finn_pointer_set_grab(finn_pointer *p, int grab);

/* Copies the evdev and hotplug fds to poll into out[]. Returns how many were
 * written; the caller must rebuild its poll set each round, because a device
 * can appear or go away between rounds. */
int  finn_pointer_fds(const finn_pointer *p, int *out, int max);

/* Drains pending events from every device and picks up any that were plugged
 * in or removed. Returns 1 if the pointer moved or a button changed. */
int  finn_pointer_poll(finn_pointer *p);

/* Wheel clicks are accumulated between frames so a caller that consumes
 * scroll in batches never misses a notch that arrived between polls. */
int  finn_pointer_take_wheel(finn_pointer *p);

/* Composites the cursor into an XRGB8888 buffer at the current position. */
void finn_pointer_draw(const finn_pointer *p, uint32_t *buf, int w, int h);

#endif
