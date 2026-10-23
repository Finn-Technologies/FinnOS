#ifndef FINNKBD_H
#define FINNKBD_H

#include <stdint.h>

/*
 * finnkbd — the FinnOS keyboard.
 *
 * There is no Wayland, no libinput and nothing else underneath, so the
 * compositor reads evdev itself. This module owns the devices that type: it
 * tracks which keys are held, derives the modifiers from those keys rather
 * than from the event's mods field, and turns a key code into the character
 * it produces on a US layout.
 *
 * It reports events; it does not deliver them to anything. There is no
 * client surface protocol yet, so what a key does is the compositor's
 * decision.
 *
 * Devices come and go. /dev/input is watched, so a keyboard plugged in after
 * the session started is picked up without a restart, and one unplugged
 * releases everything it was holding: a stuck Ctrl is worse than a lost key.
 */

/*
 * The size of the poll fd array the compositor needs, not a cap on how many
 * devices may be open. The device list itself is allocated and grows.
 */
#define FINN_KBD_MAX 8

#define FINN_MOD_SHIFT 0x01
#define FINN_MOD_CTRL  0x02
#define FINN_MOD_ALT   0x04
#define FINN_MOD_SUPER 0x08
#define FINN_MOD_CAPS  0x10

typedef struct {
    int      code;      /* the evdev KEY_* code                        */
    int      pressed;   /* 1 while the key is held, 0 on release       */
    int      repeat;    /* 1 for an autorepeat, pressed stays 1        */
    unsigned mods;      /* FINN_MOD_* at the time of the event         */
    uint32_t keysym;    /* Unicode code point, 0 when the key has none */
} finn_key_event;

/* A held-key bitmap as wide as KEY_MAX+1, the range EVIOCGKEY covers. */
#define FINN_KBD_KEY_BYTES 96

typedef struct {
    int     fd;
    uint8_t keys[FINN_KBD_KEY_BYTES];   /* what this device holds */
    int     drop;                       /* discarding until the next SYN_REPORT */
    char    path[64];
} finn_kbd_dev;

/* Events produced by finn_kbd_inject and by a resynchronisation wait here
 * until the next poll; hardware events are read straight from the fd. */
#define FINN_KBD_QUEUE 64

typedef struct {
    finn_kbd_dev *dev;      /* grown with realloc as devices appear */
    int           ndev;
    int           cap;

    int       watch_fd;     /* inotify on /dev/input, -1 when unavailable */
    int       watch_wd;

    uint8_t   injected[FINN_KBD_KEY_BYTES];  /* held by a key.* verb */
    int       caps;                          /* the lock, toggled on press */
    int       grab;                          /* 1 while our VT owns the seat */

    finn_key_event queue[FINN_KBD_QUEUE];
    int       qhead, qtail;
    int       qlost;                         /* events dropped when full */
} finn_kbd;

/* Opens every keyboard-capable device and starts watching for more. Returns
 * 0 when the reader is running; a machine with no keyboard yet is not an
 * error, the watch picks one up when it is plugged in. */
int  finn_kbd_open(finn_kbd *k);
void finn_kbd_close(finn_kbd *k);

/* Releases or re-takes EVIOCGRAB across VT switches so the rescue console on
 * tty2 can read the keyboard while the session is away, and resyncs held keys
 * when the session comes back. */
void finn_kbd_set_grab(finn_kbd *k, int grab);

/* Copies the evdev and hotplug fds to poll into out[], including the
 * hotplug fd so a keyboard plugged in wakes the loop. */
int  finn_kbd_fds(const finn_kbd *k, int *out, int max);

/* Drains devices into out[]; returns how many events were written, 0 when
 * nothing happened. Events that do not fit stay for the next call. */
int  finn_kbd_poll(finn_kbd *k, finn_key_event *out, int max);

/* 1 while code is held, on any device or by an injection. */
int  finn_kbd_down(const finn_kbd *k, int code);

/* The live modifier state: FINN_MOD_* bits, including FINN_MOD_CAPS. */
unsigned finn_kbd_mods(const finn_kbd *k);

/* Synthesises a press or release, for the key.* verbs. The event is
 * delivered by the next poll and changes the modifier state exactly as a
 * real key would. Returns 0, or -1 for a code no keyboard has. */
int  finn_kbd_inject(finn_kbd *k, int code, int pressed);

/* "enter", "esc", "tab", "space", "left", "a", "f1", "super", ... Returns
 * the KEY_* code, or -1 for a name that is not a key. */
int  finn_kbd_code_for_name(const char *name);

/* The stable lowercase name for a code, or NULL for a code with none. */
const char *finn_kbd_name_for_code(int code);

#endif
