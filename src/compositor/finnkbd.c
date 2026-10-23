/* finnkbd — the FinnOS keyboard. */
#define _GNU_SOURCE
#include "finnkbd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <linux/input.h>

#define INPUT_DIR "/dev/input"

/* Enough words for the whole EV_KEY bitmap EVIOCGKEY returns. */
#define KEY_LONGS ((KEY_MAX / (8 * (int)sizeof(long))) + 2)

_Static_assert(FINN_KBD_KEY_BYTES * 8 > KEY_MAX, "finnkbd: key bitmap too small");

/* Bit test over an ioctl capability word-array (codes run past bit 255). */
static int word_bit(const unsigned long *w, int n) {
    const int per = (int)(8 * sizeof(long));
    return (int)((w[n / per] >> (n % per)) & 1UL);
}

static int kbit(const uint8_t *map, int code) {
    if (code < 0 || code >= FINN_KBD_KEY_BYTES * 8) return 0;
    return (map[code >> 3] >> (code & 7)) & 1;
}

static void kset(uint8_t *map, int code, int on) {
    if (code < 0 || code >= FINN_KBD_KEY_BYTES * 8) return;
    if (on) map[code >> 3] |= (uint8_t)(1u << (code & 7));
    else    map[code >> 3] &= (uint8_t)~(1u << (code & 7));
}

/*
 * The US layout, one row per key: what the key types, and what it types with
 * Shift. The control characters are here rather than omitted because a text
 * field needs Enter, Tab and Backspace to arrive as characters; keys that
 * produce none carry 0.
 */
typedef struct {
    int         code;
    const char *name;
    uint32_t    base;
    uint32_t    shifted;
} kbd_key;

static const kbd_key KEYS[] = {
    { KEY_ESC,          "esc",          0x1B, 0x1B },
    { KEY_GRAVE,        "grave",        '`',  '~'  },
    { KEY_1,            "1",            '1',  '!'  },
    { KEY_2,            "2",            '2',  '@'  },
    { KEY_3,            "3",            '3',  '#'  },
    { KEY_4,            "4",            '4',  '$'  },
    { KEY_5,            "5",            '5',  '%'  },
    { KEY_6,            "6",            '6',  '^'  },
    { KEY_7,            "7",            '7',  '&'  },
    { KEY_8,            "8",            '8',  '*'  },
    { KEY_9,            "9",            '9',  '('  },
    { KEY_0,            "0",            '0',  ')'  },
    { KEY_MINUS,        "minus",        '-',  '_'  },
    { KEY_EQUAL,        "equal",        '=',  '+'  },
    { KEY_BACKSPACE,    "backspace",    0x08, 0x08 },

    { KEY_TAB,          "tab",          0x09, 0x09 },
    { KEY_Q,            "q",            'q',  'Q'  },
    { KEY_W,            "w",            'w',  'W'  },
    { KEY_E,            "e",            'e',  'E'  },
    { KEY_R,            "r",            'r',  'R'  },
    { KEY_T,            "t",            't',  'T'  },
    { KEY_Y,            "y",            'y',  'Y'  },
    { KEY_U,            "u",            'u',  'U'  },
    { KEY_I,            "i",            'i',  'I'  },
    { KEY_O,            "o",            'o',  'O'  },
    { KEY_P,            "p",            'p',  'P'  },
    { KEY_LEFTBRACE,    "leftbrace",    '[',  '{'  },
    { KEY_RIGHTBRACE,   "rightbrace",   ']',  '}'  },
    { KEY_BACKSLASH,    "backslash",    '\\', '|'  },

    { KEY_CAPSLOCK,     "capslock",     0,    0    },
    { KEY_A,            "a",            'a',  'A'  },
    { KEY_S,            "s",            's',  'S'  },
    { KEY_D,            "d",            'd',  'D'  },
    { KEY_F,            "f",            'f',  'F'  },
    { KEY_G,            "g",            'g',  'G'  },
    { KEY_H,            "h",            'h',  'H'  },
    { KEY_J,            "j",            'j',  'J'  },
    { KEY_K,            "k",            'k',  'K'  },
    { KEY_L,            "l",            'l',  'L'  },
    { KEY_SEMICOLON,    "semicolon",    ';',  ':'  },
    { KEY_APOSTROPHE,   "apostrophe",   '\'', '"'  },
    { KEY_ENTER,        "enter",        0x0D, 0x0D },

    { KEY_LEFTSHIFT,    "leftshift",    0,    0    },
    { KEY_Z,            "z",            'z',  'Z'  },
    { KEY_X,            "x",            'x',  'X'  },
    { KEY_C,            "c",            'c',  'C'  },
    { KEY_V,            "v",            'v',  'V'  },
    { KEY_B,            "b",            'b',  'B'  },
    { KEY_N,            "n",            'n',  'N'  },
    { KEY_M,            "m",            'm',  'M'  },
    { KEY_COMMA,        "comma",        ',',  '<'  },
    { KEY_DOT,          "dot",          '.',  '>'  },
    { KEY_SLASH,        "slash",        '/',  '?'  },
    { KEY_RIGHTSHIFT,   "rightshift",   0,    0    },

    { KEY_LEFTCTRL,     "leftctrl",     0,    0    },
    { KEY_LEFTMETA,     "leftmeta",     0,    0    },
    { KEY_LEFTALT,      "leftalt",      0,    0    },
    { KEY_SPACE,        "space",        ' ',  ' '  },
    { KEY_RIGHTALT,     "rightalt",     0,    0    },
    { KEY_RIGHTMETA,    "rightmeta",    0,    0    },
    { KEY_RIGHTCTRL,    "rightctrl",    0,    0    },

    { KEY_DELETE,       "delete",       0x7F, 0x7F },
    { KEY_INSERT,       "insert",       0,    0    },
    { KEY_HOME,         "home",         0,    0    },
    { KEY_PAGEUP,       "pageup",       0,    0    },
    { KEY_END,          "end",          0,    0    },
    { KEY_PAGEDOWN,     "pagedown",     0,    0    },
    { KEY_RIGHT,        "right",        0,    0    },
    { KEY_LEFT,         "left",         0,    0    },
    { KEY_DOWN,         "down",         0,    0    },
    { KEY_UP,           "up",           0,    0    },
    { KEY_NUMLOCK,      "numlock",      0,    0    },
    { KEY_SCROLLLOCK,   "scrolllock",   0,    0    },
    { KEY_SYSRQ,        "sysrq",        0,    0    },
    { KEY_PAUSE,        "pause",        0,    0    },
    { KEY_MENU,         "menu",         0,    0    },
    { KEY_COMPOSE,      "compose",      0,    0    },
    { KEY_PRINT,        "print",        0,    0    },

    /* The keypad types its digits with Num Lock on, which is the state this
     * module assumes; it does not read or drive the lock LEDs. */
    { KEY_KP0,          "kp0",          '0',  '0'  },
    { KEY_KP1,          "kp1",          '1',  '1'  },
    { KEY_KP2,          "kp2",          '2',  '2'  },
    { KEY_KP3,          "kp3",          '3',  '3'  },
    { KEY_KP4,          "kp4",          '4',  '4'  },
    { KEY_KP5,          "kp5",          '5',  '5'  },
    { KEY_KP6,          "kp6",          '6',  '6'  },
    { KEY_KP7,          "kp7",          '7',  '7'  },
    { KEY_KP8,          "kp8",          '8',  '8'  },
    { KEY_KP9,          "kp9",          '9',  '9'  },
    { KEY_KPDOT,        "kpdot",        '.',  '.'  },
    { KEY_KPSLASH,      "kpslash",      '/',  '/'  },
    { KEY_KPASTERISK,   "kpasterisk",   '*',  '*'  },
    { KEY_KPMINUS,      "kpminus",      '-',  '-'  },
    { KEY_KPPLUS,       "kpplus",       '+',  '+'  },
    { KEY_KPEQUAL,      "kpequal",      '=',  '='  },
    { KEY_KPENTER,      "kpenter",      0x0D, 0x0D },

    { KEY_F1,           "f1",           0,    0    },
    { KEY_F2,           "f2",           0,    0    },
    { KEY_F3,           "f3",           0,    0    },
    { KEY_F4,           "f4",           0,    0    },
    { KEY_F5,           "f5",           0,    0    },
    { KEY_F6,           "f6",           0,    0    },
    { KEY_F7,           "f7",           0,    0    },
    { KEY_F8,           "f8",           0,    0    },
    { KEY_F9,           "f9",           0,    0    },
    { KEY_F10,          "f10",          0,    0    },
    { KEY_F11,          "f11",          0,    0    },
    { KEY_F12,          "f12",          0,    0    },
    { KEY_F13,          "f13",          0,    0    },
    { KEY_F14,          "f14",          0,    0    },
    { KEY_F15,          "f15",          0,    0    },
    { KEY_F16,          "f16",          0,    0    },
    { KEY_F17,          "f17",          0,    0    },
    { KEY_F18,          "f18",          0,    0    },
    { KEY_F19,          "f19",          0,    0    },
    { KEY_F20,          "f20",          0,    0    },
    { KEY_F21,          "f21",          0,    0    },
    { KEY_F22,          "f22",          0,    0    },
    { KEY_F23,          "f23",          0,    0    },
    { KEY_F24,          "f24",          0,    0    },

    { KEY_POWER,        "power",        0,    0    },
    { KEY_SLEEP,        "sleep",        0,    0    },
    { KEY_WAKEUP,       "wakeup",       0,    0    },
    { KEY_MUTE,         "mute",         0,    0    },
    { KEY_VOLUMEDOWN,   "volumedown",   0,    0    },
    { KEY_VOLUMEUP,     "volumeup",     0,    0    },
    { KEY_NEXTSONG,     "nextsong",     0,    0    },
    { KEY_PLAYPAUSE,    "playpause",    0,    0    },
    { KEY_PREVIOUSSONG, "previoussong", 0,    0    },
    { KEY_STOPCD,       "stopcd",       0,    0    },
};

#define NKEYS ((int)(sizeof KEYS / sizeof KEYS[0]))

/* Names a caller is likely to try that are not the table's own. */
static const struct { const char *alias; int code; } ALIASES[] = {
    { "escape",   KEY_ESC },
    { "return",   KEY_ENTER },
    { "del",      KEY_DELETE },
    { "pgup",     KEY_PAGEUP },
    { "pgdn",     KEY_PAGEDOWN },
    { "ctrl",     KEY_LEFTCTRL },
    { "control",  KEY_LEFTCTRL },
    { "shift",    KEY_LEFTSHIFT },
    { "alt",      KEY_LEFTALT },
    { "meta",     KEY_LEFTMETA },
    { "super",    KEY_LEFTMETA },
    { "win",      KEY_LEFTMETA },
    { "spacebar", KEY_SPACE },
};

#define NALIASES ((int)(sizeof ALIASES / sizeof ALIASES[0]))

static const kbd_key *find_key(int code) {
    for (int i = 0; i < NKEYS; i++)
        if (KEYS[i].code == code) return &KEYS[i];
    return NULL;
}

/* Names are matched without regard to case, so "Enter" is the same key. */
static int name_eq(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* The character a key types with these modifiers held. Caps acts as Shift on
 * letters only, which is what the lock does. */
static uint32_t us_keysym(int code, unsigned mods) {
    const kbd_key *key = find_key(code);
    if (!key || !key->base) return 0;

    if (key->base >= 'a' && key->base <= 'z') {
        int upper = ((mods & FINN_MOD_SHIFT) != 0) ^ ((mods & FINN_MOD_CAPS) != 0);
        return upper ? key->base - 'a' + 'A' : key->base;
    }
    return (mods & FINN_MOD_SHIFT) ? key->shifted : key->base;
}

/* Each side of a modifier is a key of its own: releasing the left Shift
 * while the right one is still held must not clear the bit. */
static unsigned mods_of(const uint8_t *keys, unsigned mods) {
    if (kbit(keys, KEY_LEFTSHIFT) || kbit(keys, KEY_RIGHTSHIFT)) mods |= FINN_MOD_SHIFT;
    if (kbit(keys, KEY_LEFTCTRL)  || kbit(keys, KEY_RIGHTCTRL))  mods |= FINN_MOD_CTRL;
    if (kbit(keys, KEY_LEFTALT)   || kbit(keys, KEY_RIGHTALT))   mods |= FINN_MOD_ALT;
    if (kbit(keys, KEY_LEFTMETA)  || kbit(keys, KEY_RIGHTMETA))  mods |= FINN_MOD_SUPER;
    return mods;
}

unsigned finn_kbd_mods(const finn_kbd *k) {
    unsigned mods = k->caps ? FINN_MOD_CAPS : 0;
    for (int i = 0; i < k->ndev; i++) mods = mods_of(k->dev[i].keys, mods);
    return mods_of(k->injected, mods);
}

static void event_fill(const finn_kbd *k, finn_key_event *e,
                       int code, int pressed, int repeat) {
    e->code = code;
    e->pressed = pressed;
    e->repeat = repeat;
    e->mods = finn_kbd_mods(k);
    e->keysym = us_keysym(code, e->mods);
}

static void queue_push(finn_kbd *k, const finn_key_event *e) {
    int next = (k->qtail + 1) % FINN_KBD_QUEUE;
    if (next == k->qhead) {
        if (!k->qlost)
            fprintf(stderr, "finnkbd: event queue full, events will be lost\n");
        k->qlost++;
        return;
    }
    k->queue[k->qtail] = *e;
    k->qtail = next;
}

/* Reads a device's real held-key state, the form EVIOCGKEY reports. */
static void kbd_read_keys(finn_kbd_dev *d) {
    unsigned long keys[KEY_LONGS];
    memset(keys, 0, sizeof keys);
    memset(d->keys, 0, sizeof d->keys);
    if (ioctl(d->fd, EVIOCGKEY(sizeof keys), keys) < 0) return;
    for (int code = 1; code <= KEY_MAX; code++)
        if (word_bit(keys, code)) kset(d->keys, code, 1);
}

/*
 * A keyboard reports typing keys; a mouse reports none below the BTN_* range.
 * Requiring one here is what keeps the pointer (which refuses those devices)
 * and this module from grabbing the same fd on a keyboard with a pointing
 * stick.
 */
static int claims_device(int fd) {
    unsigned long keys[KEY_LONGS];
    memset(keys, 0, sizeof keys);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0) return 0;
    for (int code = 1; code < BTN_MISC; code++)
        if (word_bit(keys, code)) return 1;
    return 0;
}

static int probe_dev(const char *path, finn_kbd_dev *d, int grab) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    if (!claims_device(fd)) { close(fd); return -1; }

    /* This seat has one reader while our VT is active. EVIOCGRAB takes the
     * integer value directly (not a pointer); a refused grab is reported and
     * the device is still read so typing still works. */
    if (grab && ioctl(fd, EVIOCGRAB, (void *)(intptr_t)1) < 0)
        fprintf(stderr, "finnkbd: %s: cannot grab: %s\n", path, strerror(errno));

    memset(d, 0, sizeof *d);
    d->fd = fd;
    snprintf(d->path, sizeof d->path, "%s", path);

    /* A key already held when we open the device must not be assumed up. */
    kbd_read_keys(d);

    char name[128] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) > 0)
        fprintf(stderr, "finnkbd: %s \"%s\"\n", path, name);
    return 0;
}

static void dev_close(finn_kbd_dev *d) {
    if (d->fd < 0) return;
    ioctl(d->fd, EVIOCGRAB, (void *)(intptr_t)0);
    close(d->fd);
    d->fd = -1;
}

/* A node can be gone while the fd still exists, and a replug can reuse the
 * number while this fd is dead; both look the same from the path alone, so
 * the device is asked as well. */
static int dev_alive(const finn_kbd_dev *d) {
    struct input_id id;
    return access(d->path, F_OK) == 0 && ioctl(d->fd, EVIOCGID, &id) == 0;
}

static int dev_add(finn_kbd *k, const finn_kbd_dev *d) {
    if (k->ndev == k->cap) {
        int cap = k->cap ? k->cap * 2 : 4;
        finn_kbd_dev *grown = realloc(k->dev, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        k->dev = grown;
        k->cap = cap;
    }
    k->dev[k->ndev++] = *d;
    return 0;
}

/*
 * A device is going away. Let go of everything it was holding and tell the
 * compositor, so a modifier does not stay down with the keyboard that held
 * it: a stuck Ctrl is worse than a lost key.
 */
static void dev_release(finn_kbd *k, int i) {
    finn_kbd_dev *d = &k->dev[i];

    for (int code = 1; code <= KEY_MAX; code++) {
        if (!kbit(d->keys, code)) continue;
        kset(d->keys, code, 0);
        if (code == KEY_CAPSLOCK) continue;   /* a lock, not a held key */
        finn_key_event e;
        event_fill(k, &e, code, 0, 0);
        queue_push(k, &e);
    }
    dev_close(d);
}

static void dev_remove(finn_kbd *k, int i) {
    dev_release(k, i);
    memmove(&k->dev[i], &k->dev[i + 1],
            (size_t)(k->ndev - i - 1) * sizeof *k->dev);
    k->ndev--;
}

/* Returns 1 when a device was added, 0 when it was already ours, -1 when it
 * is not a keyboard (or cannot be opened yet). */
static int open_one(finn_kbd *k, const char *path) {
    for (int i = 0; i < k->ndev; i++)
        if (!strcmp(k->dev[i].path, path)) return 0;

    finn_kbd_dev d;
    if (probe_dev(path, &d, k->grab) != 0) return -1;

    if (dev_add(k, &d) != 0) {
        dev_close(&d);
        return -1;
    }
    return 1;
}

/*
 * Bring the open set in line with what is in /dev/input: let go of devices
 * whose node has gone, and pick up any that appeared. A node that udev has
 * not yet made readable is retried on the IN_ATTRIB that follows the chown.
 */
static int rescan(finn_kbd *k) {
    int changed = 0;

    for (int i = k->ndev - 1; i >= 0; i--) {
        if (dev_alive(&k->dev[i])) continue;
        dev_remove(k, i);
        changed = 1;
    }

    for (int n = 0; n < 64; n++) {
        char path[64];
        snprintf(path, sizeof path, "%s/event%d", INPUT_DIR, n);
        if (open_one(k, path) > 0) changed = 1;
    }
    return changed;
}

static void watch_open(finn_kbd *k) {
    k->watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (k->watch_fd < 0) {
        fprintf(stderr, "finnkbd: no inotify, hotplug will not be seen\n");
        return;
    }
    k->watch_wd = inotify_add_watch(k->watch_fd, INPUT_DIR,
                                    IN_CREATE | IN_DELETE | IN_ATTRIB);
    if (k->watch_wd < 0) {
        fprintf(stderr, "finnkbd: cannot watch %s: %s\n",
                INPUT_DIR, strerror(errno));
        close(k->watch_fd);
        k->watch_fd = -1;
    }
}

/* Returns 1 when something changed in the directory. The events themselves
 * are of no interest: a rescan is cheap and is always right. */
static int watch_drain(finn_kbd *k) {
    if (k->watch_fd < 0) return 0;
    char buf[4096];
    int changed = 0;
    for (;;) {
        ssize_t n = read(k->watch_fd, buf, sizeof buf);
        if (n <= 0) break;
        changed = 1;
    }
    return changed;
}

/*
 * The kernel dropped events because this reader fell behind, so the held
 * keys we derived from the stream are not the keys the device holds. Ask
 * the device, and report what changed: a key that went up during the gap has
 * to be released, or it stays down forever.
 */
static void kbd_resync(finn_kbd *k, finn_kbd_dev *d) {
    unsigned long keys[KEY_LONGS];
    uint8_t now[FINN_KBD_KEY_BYTES];

    memset(keys, 0, sizeof keys);
    memset(now, 0, sizeof now);
    if (ioctl(d->fd, EVIOCGKEY(sizeof keys), keys) >= 0) {
        for (int code = 1; code <= KEY_MAX; code++)
            if (word_bit(keys, code)) kset(now, code, 1);
    }
    /* If the device will not say, every key is released rather than left
     * down: the same rule as an unplug. */

    for (int code = 1; code <= KEY_MAX; code++) {
        int was = kbit(d->keys, code), now_held = kbit(now, code);
        if (was == now_held) continue;
        kset(d->keys, code, now_held);
        if (code == KEY_CAPSLOCK) continue;   /* a lock, not a held key */
        finn_key_event e;
        event_fill(k, &e, code, now_held, 0);
        queue_push(k, &e);
    }
}

int finn_kbd_open(finn_kbd *k) {
    memset(k, 0, sizeof *k);
    k->watch_fd = -1;
    k->watch_wd = -1;
    k->grab = 1;

    watch_open(k);
    rescan(k);
    if (!k->ndev)
        fprintf(stderr, "finnkbd: no keyboard yet, watching %s\n", INPUT_DIR);
    return 0;
}

void finn_kbd_set_grab(finn_kbd *k, int grab) {
    grab = grab ? 1 : 0;
    if (k->grab == grab) return;
    k->grab = grab;
    for (int i = 0; i < k->ndev; i++) {
        if (k->dev[i].fd < 0) continue;
        ioctl(k->dev[i].fd, EVIOCGRAB, (void *)(intptr_t)grab);
        if (grab) kbd_resync(k, &k->dev[i]);
    }
}

void finn_kbd_close(finn_kbd *k) {
    for (int i = 0; i < k->ndev; i++) dev_close(&k->dev[i]);
    free(k->dev);
    k->dev = NULL;
    k->ndev = 0;
    k->cap = 0;

    if (k->watch_fd >= 0) close(k->watch_fd);
    k->watch_fd = -1;
    k->watch_wd = -1;
}

int finn_kbd_fds(const finn_kbd *k, int *out, int max) {
    int n = 0;
    /* The watch comes first: it must never be the fd that does not fit, or a
     * full poll set would stop seeing keyboards come and go. */
    if (k->watch_fd >= 0 && n < max) out[n++] = k->watch_fd;
    for (int i = 0; i < k->ndev && n < max; i++)
        if (k->dev[i].fd >= 0) out[n++] = k->dev[i].fd;
    return n;
}

/* Synthetic and resync events are drained ahead of the device fds so a
 * release generated by an unplug or a key.* verb is seen before newer keys. */
static int queue_pop(finn_kbd *k, finn_key_event *out, int n, int max) {
    while (n < max && k->qhead != k->qtail) {
        out[n++] = k->queue[k->qhead];
        k->qhead = (k->qhead + 1) % FINN_KBD_QUEUE;
    }
    return n;
}

int finn_kbd_poll(finn_kbd *k, finn_key_event *out, int max) {
    struct input_event ev;
    finn_key_event event;
    int n = 0, lost = 0;

    if (max <= 0) return 0;

    if (watch_drain(k)) rescan(k);

    n = queue_pop(k, out, n, max);
    if (n >= max) return n;

    for (int i = 0; i < k->ndev; i++) {
        finn_kbd_dev *d = &k->dev[i];
        if (d->fd < 0) continue;

        for (;;) {
            /* Do not read past the caller's buffer: what does not fit stays
             * in the kernel queue, and the fd stays readable so the next
             * poll call picks it up in order. */
            if (n >= max) {
                if (lost) rescan(k);
                return n;
            }

            ssize_t got = read(d->fd, &ev, sizeof ev);
            if (got != (ssize_t)sizeof ev) {
                /* ENODEV means the device was unplugged underneath us. */
                if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    dev_remove(k, i);
                    i--;
                    lost = 1;
                }
                break;
            }

            if (ev.type == EV_SYN) {
                if (ev.code == SYN_DROPPED) {
                    /* Re-read the device, then ignore the stale events the
                     * kernel queued before the drop until the frame closes. */
                    d->drop = 1;
                    kbd_resync(k, d);
                } else if (ev.code == SYN_REPORT) {
                    d->drop = 0;
                }
                continue;
            }
            if (d->drop) continue;
            if (ev.type != EV_KEY) continue;
            if (ev.code <= 0 || ev.code > KEY_MAX) continue;

            int repeat = ev.value == 2;
            int pressed = ev.value != 0;
            if (repeat) pressed = 1;

            if (!repeat) {
                kset(d->keys, ev.code, pressed);
                /* The lock toggles when the key goes down, and an autorepeat
                 * of it must not toggle it again. */
                if (ev.code == KEY_CAPSLOCK && pressed) k->caps = !k->caps;
            }

            event_fill(k, &event, ev.code, pressed, repeat);
            out[n++] = event;
        }
    }

    /* A node that was re-plugged under the same number is picked up here,
     * once, rather than inside the read loop where it would be re-added to
     * the same iteration. */
    if (lost) rescan(k);

    /* Releases and resynchronised keys are queued while the devices are read;
     * they belong to this call too, or they would wait for the next key. */
    return queue_pop(k, out, n, max);
}

int finn_kbd_down(const finn_kbd *k, int code) {
    if (kbit(k->injected, code)) return 1;
    for (int i = 0; i < k->ndev; i++)
        if (kbit(k->dev[i].keys, code)) return 1;
    return 0;
}

int finn_kbd_inject(finn_kbd *k, int code, int pressed) {
    if (code <= 0 || code > KEY_MAX) return -1;

    kset(k->injected, code, pressed ? 1 : 0);
    if (code == KEY_CAPSLOCK && pressed) k->caps = !k->caps;

    finn_key_event e;
    event_fill(k, &e, code, pressed ? 1 : 0, 0);
    queue_push(k, &e);
    return 0;
}

int finn_kbd_code_for_name(const char *name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < NKEYS; i++)
        if (name_eq(KEYS[i].name, name)) return KEYS[i].code;
    for (int i = 0; i < NALIASES; i++)
        if (name_eq(ALIASES[i].alias, name)) return ALIASES[i].code;
    return -1;
}

const char *finn_kbd_name_for_code(int code) {
    const kbd_key *key = find_key(code);
    return key ? key->name : NULL;
}
