/* finnpointer — the FinnOS pointer. */
#define _GNU_SOURCE
#include "finnpointer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <linux/input.h>

#include "finnvg.h"

#define COL_CURSOR_FILL  0xFFFFFFFFu   /* white body            */
#define COL_CURSOR_EDGE  0xFF1A1D24u   /* dark outline          */
#define CURSOR_LOGICAL   22.0f         /* cursor art at 1x      */

#define INPUT_DIR "/dev/input"

/* Enough words for the whole EV_KEY bitmap EVIOCGKEY returns (KEY_MAX+1). */
#define KEY_LONGS ((KEY_MAX / (8 * (int)sizeof(long))) + 2)

static inline uint32_t over(uint32_t dst, uint32_t src, float a) {
    if (a <= 0.0f) return dst;
    float sa = a * ((src >> 24) & 0xFF) / 255.0f;
    if (sa <= 0.0f) return dst;
    if (sa >= 1.0f) return (src & 0x00FFFFFFu) | 0xFF000000u;
    uint32_t d0 = dst & 0xFF, d1 = (dst >> 8) & 0xFF, d2 = (dst >> 16) & 0xFF;
    uint32_t s0 = src & 0xFF, s1 = (src >> 8) & 0xFF, s2 = (src >> 16) & 0xFF;
    uint32_t r0 = (uint32_t)(s0 * sa + d0 * (1 - sa));
    uint32_t r1 = (uint32_t)(s1 * sa + d1 * (1 - sa));
    uint32_t r2 = (uint32_t)(s2 * sa + d2 * (1 - sa));
    return 0xFF000000u | (r2 << 16) | (r1 << 8) | r0;
}

/*
 * The cursor art uses fill=black for the body and stroke=#969696 for the
 * outline, so the same palette convention as the dock icons recolours it:
 * black -> white body, #969696 -> dark edge.
 */
static uint32_t cursor_tint(uint32_t c, void *ud) {
    (void)ud;
    uint32_t rgb = c & 0x00FFFFFFu;
    uint32_t col = (rgb == 0x00969696u) ? COL_CURSOR_EDGE : COL_CURSOR_FILL;
    return col | (c & 0xFF000000u);
}

/* Bit test over an ioctl capability word-array (codes run past bit 255). */
static int bit_set(const unsigned long *w, int n) {
    const int per = (int)(8 * sizeof(long));
    return (int)((w[n / per] >> (n % per)) & 1UL);
}

/*
 * True when the device reports typing keys — anything below the BTN_* mouse
 * range. A keyboard with a built-in pointing stick reports both axes and
 * keys, and only one client can grab an evdev device, so the pointer leaves
 * those to finnkbd: a trackpoint reachable through another device is
 * recoverable, a keyboard grabbed away from the compositor is not.
 */
static int has_typing_keys(int fd) {
    unsigned long keys[KEY_LONGS];
    memset(keys, 0, sizeof keys);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0) return 0;
    for (int code = 1; code < BTN_MISC; code++)
        if (bit_set(keys, code)) return 1;
    return 0;
}

static int probe_dev(const char *path, finn_ptr_dev *d, int grab) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;

    unsigned long ev[1] = {0}, rel[2] = {0}, abs[2] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0) { close(fd); return -1; }
    ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);

    int is_rel = bit_set(ev, EV_REL) && bit_set(rel, REL_X) && bit_set(rel, REL_Y);
    int is_abs = bit_set(ev, EV_ABS) && bit_set(abs, ABS_X) && bit_set(abs, ABS_Y);
    if ((!is_rel && !is_abs) || has_typing_keys(fd)) { close(fd); return -1; }

    memset(d, 0, sizeof *d);
    d->fd = fd;
    d->is_abs = is_abs;
    snprintf(d->path, sizeof d->path, "%s", path);

    if (is_abs) {
        struct input_absinfo ix = {0}, iy = {0};
        if (ioctl(fd, EVIOCGABS(ABS_X), &ix) == 0) {
            d->minx = ix.minimum; d->maxx = ix.maximum; d->ax = ix.value;
        }
        if (ioctl(fd, EVIOCGABS(ABS_Y), &iy) == 0) {
            d->miny = iy.minimum; d->maxy = iy.maximum; d->ay = iy.value;
        }
        if (d->maxx <= d->minx) d->maxx = d->minx + 1;
        if (d->maxy <= d->miny) d->maxy = d->miny + 1;
    }

    /* This seat has one reader while our VT is active. EVIOCGRAB takes the
     * integer value directly; a refused grab is reported and we keep reading. */
    if (grab && ioctl(fd, EVIOCGRAB, (void *)(intptr_t)1) < 0)
        fprintf(stderr, "finnpointer: %s: cannot grab: %s\n", path, strerror(errno));
    return 0;
}

static void dev_close(finn_ptr_dev *d) {
    if (d->fd < 0) return;
    ioctl(d->fd, EVIOCGRAB, (void *)(intptr_t)0);
    close(d->fd);
    d->fd = -1;
}

/* A node can be gone while the fd still exists, and a replug can reuse the
 * number while this fd is dead; both look the same from the path alone, so
 * the device is asked as well. */
static int dev_alive(const finn_ptr_dev *d) {
    struct input_id id;
    return access(d->path, F_OK) == 0 && ioctl(d->fd, EVIOCGID, &id) == 0;
}

static int dev_add(finn_pointer *p, const finn_ptr_dev *d) {
    if (p->ndev == p->cap) {
        int cap = p->cap ? p->cap * 2 : 4;
        finn_ptr_dev *grown = realloc(p->dev, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        p->dev = grown;
        p->cap = cap;
    }
    p->dev[p->ndev++] = *d;
    return 0;
}

/* The device set changed: the button mask is what the devices still here are
 * holding, and a button nobody holds must not stay down. */
static int merge_buttons(finn_pointer *p) {
    int mask = 0;
    for (int i = 0; i < p->ndev; i++) mask |= p->dev[i].buttons;
    if (mask == p->buttons) return 0;
    p->buttons = mask;
    p->dirty = 1;
    return 1;
}

static void dev_remove(finn_pointer *p, int i) {
    dev_close(&p->dev[i]);
    memmove(&p->dev[i], &p->dev[i + 1],
            (size_t)(p->ndev - i - 1) * sizeof *p->dev);
    p->ndev--;
    merge_buttons(p);
}

/* Returns 1 when a device was added, 0 when it was already ours, -1 when it
 * is not a pointer (or cannot be opened yet). */
static int open_one(finn_pointer *p, const char *path) {
    for (int i = 0; i < p->ndev; i++)
        if (!strcmp(p->dev[i].path, path)) return 0;

    finn_ptr_dev d;
    if (probe_dev(path, &d, p->grab) != 0) return -1;

    char name[128] = {0};
    if (ioctl(d.fd, EVIOCGNAME(sizeof name - 1), name) > 0)
        fprintf(stderr, "finnpointer: %s \"%s\" %s%s\n", path, name,
                d.is_abs ? "absolute" : "relative",
                d.is_abs ? " (tablet/touch)" : "");

    if (dev_add(p, &d) != 0) {
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
static int rescan(finn_pointer *p) {
    int changed = 0;

    for (int i = p->ndev - 1; i >= 0; i--) {
        if (dev_alive(&p->dev[i])) continue;
        dev_remove(p, i);
        changed = 1;
    }

    for (int n = 0; n < 64; n++) {
        char path[64];
        snprintf(path, sizeof path, "%s/event%d", INPUT_DIR, n);
        if (open_one(p, path) > 0) changed = 1;
    }
    return changed;
}

static void watch_open(finn_pointer *p) {
    p->watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (p->watch_fd < 0) {
        fprintf(stderr, "finnpointer: no inotify, hotplug will not be seen\n");
        return;
    }
    p->watch_wd = inotify_add_watch(p->watch_fd, INPUT_DIR,
                                    IN_CREATE | IN_DELETE | IN_ATTRIB);
    if (p->watch_wd < 0) {
        fprintf(stderr, "finnpointer: cannot watch %s: %s\n",
                INPUT_DIR, strerror(errno));
        close(p->watch_fd);
        p->watch_fd = -1;
    }
}

/* Returns 1 when something changed in the directory. The events themselves
 * are of no interest: a rescan is cheap and is always right. */
static int watch_drain(finn_pointer *p) {
    if (p->watch_fd < 0) return 0;
    char buf[4096];
    int changed = 0;
    for (;;) {
        ssize_t n = read(p->watch_fd, buf, sizeof buf);
        if (n <= 0) break;
        changed = 1;
    }
    return changed;
}

/* Maps an absolute sample onto the scanout. Called at the frame boundary,
 * where X and Y belong to the same sample. */
static int map_abs(finn_pointer *p, finn_ptr_dev *d) {
    long nx = (long)(d->ax - d->minx) * (p->screen_w - 1) / (d->maxx - d->minx);
    long ny = (long)(d->ay - d->miny) * (p->screen_h - 1) / (d->maxy - d->miny);
    if (nx == p->x && ny == p->y) return 0;
    p->x = (int)nx;
    p->y = (int)ny;
    return 1;
}

static void buttons_from_keys(finn_ptr_dev *d, const unsigned long *keys) {
    int mask = 0;
    if (bit_set(keys, BTN_LEFT) || bit_set(keys, BTN_TOUCH)) mask |= FINN_BTN_LEFT;
    if (bit_set(keys, BTN_RIGHT)) mask |= FINN_BTN_RIGHT;
    if (bit_set(keys, BTN_MIDDLE)) mask |= FINN_BTN_MIDDLE;
    d->buttons = mask;
}

/*
 * The kernel dropped events because this reader fell behind, so what the
 * stream told us the device was holding is no longer true. Ask the device
 * instead of guessing: the held buttons and the current absolute position.
 */
static int ptr_resync(finn_pointer *p, finn_ptr_dev *d) {
    int changed = 0;

    /* The frame the drop interrupted is incomplete; its sample is discarded
     * rather than mapped as if it were whole. */
    d->pending = 0;

    unsigned long keys[KEY_LONGS];
    memset(keys, 0, sizeof keys);
    if (ioctl(d->fd, EVIOCGKEY(sizeof keys), keys) >= 0)
        buttons_from_keys(d, keys);

    if (d->is_abs) {
        struct input_absinfo ix = {0}, iy = {0};
        if (ioctl(d->fd, EVIOCGABS(ABS_X), &ix) == 0) {
            d->minx = ix.minimum; d->maxx = ix.maximum; d->ax = ix.value;
        }
        if (ioctl(d->fd, EVIOCGABS(ABS_Y), &iy) == 0) {
            d->miny = iy.minimum; d->maxy = iy.maximum; d->ay = iy.value;
        }
        if (d->maxx <= d->minx) d->maxx = d->minx + 1;
        if (d->maxy <= d->miny) d->maxy = d->miny + 1;
        if (map_abs(p, d)) changed = 1;
    }

    if (merge_buttons(p)) changed = 1;
    return changed;
}

int finn_pointer_fds(const finn_pointer *p, int *out, int max) {
    int n = 0;
    /* The watch comes first: it must never be the fd that does not fit, or a
     * full poll set would stop seeing devices come and go. */
    if (p->watch_fd >= 0 && n < max) out[n++] = p->watch_fd;
    for (int i = 0; i < p->ndev && n < max; i++)
        if (p->dev[i].fd >= 0) out[n++] = p->dev[i].fd;
    return n;
}

int finn_pointer_open(finn_pointer *p, int screen_w, int screen_h, const char *cursor_svg) {
    memset(p, 0, sizeof *p);
    p->watch_fd = -1;
    p->watch_wd = -1;
    p->grab = 1;

    p->screen_w = screen_w;
    p->screen_h = screen_h;
    p->x = screen_w / 2;
    p->y = screen_h / 2;

    watch_open(p);
    rescan(p);

    fsvg svg;
    if (fsvg_load(cursor_svg, &svg) != 0) {
        fprintf(stderr, "finnpointer: cannot load %s\n", cursor_svg);
        if (p->ndev) return 0;
        finn_pointer_close(p);
        return -1;
    }
    int px = (int)(CURSOR_LOGICAL * (screen_h >= 2000 ? 2.0f : (screen_h >= 1400 ? 1.5f : 1.0f)));
    p->sprite = calloc((size_t)px * px, 4);
    if (p->sprite) {
        fsvg_raster(&svg, p->sprite, px, px, cursor_tint, NULL);
        p->sprite_px = px;
        /* hotspot is the arrow tip at (5,2) of the 24x24 art */
        p->hot_x = (int)(5.0f / 24.0f * px);
        p->hot_y = (int)(2.0f / 24.0f * px);
    }
    fsvg_free(&svg);
    return 0;
}

void finn_pointer_set_grab(finn_pointer *p, int grab) {
    grab = grab ? 1 : 0;
    if (p->grab == grab) return;
    p->grab = grab;
    for (int i = 0; i < p->ndev; i++) {
        if (p->dev[i].fd < 0) continue;
        ioctl(p->dev[i].fd, EVIOCGRAB, (void *)(intptr_t)grab);
        if (grab) ptr_resync(p, &p->dev[i]);
    }
}

void finn_pointer_close(finn_pointer *p) {
    for (int i = 0; i < p->ndev; i++) dev_close(&p->dev[i]);
    free(p->dev);
    p->dev = NULL;
    p->ndev = 0;
    p->cap = 0;

    if (p->watch_fd >= 0) close(p->watch_fd);
    p->watch_fd = -1;
    p->watch_wd = -1;

    free(p->sprite);
    p->sprite = NULL;
}

int finn_pointer_take_wheel(finn_pointer *p) {
    int wheel = p->wheel;
    p->wheel = 0;
    return wheel;
}

int finn_pointer_poll(finn_pointer *p) {
    static int logged_abs = 0, logged_rel = 0;
    struct input_event ev;
    int moved = 0, lost = 0;

    if (watch_drain(p) && rescan(p)) moved = 1;

    for (int i = 0; i < p->ndev; i++) {
        finn_ptr_dev *d = &p->dev[i];
        if (d->fd < 0) continue;

        for (;;) {
            ssize_t n = read(d->fd, &ev, sizeof ev);
            if (n != (ssize_t)sizeof ev) {
                /* ENODEV means the device was unplugged underneath us. */
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    dev_remove(p, i);
                    i--;
                    moved = 1;
                    lost = 1;
                }
                break;
            }

            if (ev.type == EV_SYN) {
                if (ev.code == SYN_DROPPED) {
                    /* Re-read the device, then ignore the stale events the
                     * kernel queued before the drop until the frame closes. */
                    d->drop = 1;
                    d->pending = 0;
                    if (ptr_resync(p, d)) moved = 1;
                } else if (ev.code == SYN_REPORT) {
                    d->drop = 0;
                    if (d->pending) {
                        /* An absolute device reports X and Y within one SYN
                         * frame; the frame boundary is where the sample is
                         * coherent, so map there. */
                        if (map_abs(p, d)) {
                            moved = 1;
                            if (!logged_abs) {
                                logged_abs = 1;
                                fprintf(stderr, "finnpointer: motion from %s (absolute)\n", d->path);
                            }
                        }
                        d->pending = 0;
                    }
                }
                continue;
            }
            if (d->drop) continue;

            if (ev.type == EV_REL) {
                int rel_moved = 0;
                if (ev.code == REL_X)      { p->x += ev.value; moved = 1; rel_moved = 1; }
                else if (ev.code == REL_Y) { p->y += ev.value; moved = 1; rel_moved = 1; }
                else if (ev.code == REL_WHEEL) { p->wheel += ev.value; }
                if (rel_moved && !logged_rel) {
                    logged_rel = 1;
                    fprintf(stderr, "finnpointer: motion from %s (relative)\n", d->path);
                }
            } else if (ev.type == EV_ABS) {
                if (ev.code == ABS_X)      { d->ax = ev.value; d->pending |= 1; }
                else if (ev.code == ABS_Y) { d->ay = ev.value; d->pending |= 2; }
            } else if (ev.type == EV_KEY) {
                int bit = 0;
                if (ev.code == BTN_LEFT || ev.code == BTN_TOUCH) bit = FINN_BTN_LEFT;
                else if (ev.code == BTN_RIGHT)  bit = FINN_BTN_RIGHT;
                else if (ev.code == BTN_MIDDLE) bit = FINN_BTN_MIDDLE;
                if (bit) {
                    if (ev.value) { p->buttons |= bit; d->buttons |= bit; }
                    else          { p->buttons &= ~bit; d->buttons &= ~bit; }
                    moved = 1;
                }
            }
        }
    }

    /* A node that was re-plugged under the same number is picked up here,
     * once, rather than inside the read loop where it would be re-added to
     * the same iteration. */
    if (lost && rescan(p)) moved = 1;

    if (moved) {
        if (p->x < 0) p->x = 0;
        if (p->y < 0) p->y = 0;
        if (p->x > p->screen_w - 1) p->x = p->screen_w - 1;
        if (p->y > p->screen_h - 1) p->y = p->screen_h - 1;
        p->dirty = 1;
    }
    return moved;
}

void finn_pointer_draw(const finn_pointer *p, uint32_t *buf, int w, int h) {
    if (!p->sprite || p->sprite_px <= 0) return;
    int ox = p->x - p->hot_x;
    int oy = p->y - p->hot_y;
    int n = p->sprite_px;
    for (int y = 0; y < n; y++) {
        int ty = oy + y;
        if (ty < 0 || ty >= h) continue;
        for (int x = 0; x < n; x++) {
            int tx = ox + x;
            if (tx < 0 || tx >= w) continue;
            uint32_t c = p->sprite[(size_t)y * n + x];
            if (!(c >> 24)) continue;
            buf[(size_t)ty * w + tx] = over(buf[(size_t)ty * w + tx], c, 1.0f);
        }
    }
}
