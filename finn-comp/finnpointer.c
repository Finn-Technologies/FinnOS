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
#include <linux/input.h>

#include "finnvg.h"

#define COL_CURSOR_FILL  0xFFFFFFFFu   /* white body            */
#define COL_CURSOR_EDGE  0xFF1A1D24u   /* dark outline          */
#define CURSOR_LOGICAL   22.0f         /* cursor art at 1x      */

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

static int probe_dev(const char *path, finn_ptr_dev *d) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;

    unsigned long ev[1] = {0}, rel[2] = {0}, abs[2] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0) { close(fd); return -1; }
    ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);

    int is_rel = bit_set(ev, EV_REL) && bit_set(rel, REL_X) && bit_set(rel, REL_Y);
    int is_abs = bit_set(ev, EV_ABS) && bit_set(abs, ABS_X) && bit_set(abs, ABS_Y);
    if (!is_rel && !is_abs) { close(fd); return -1; }

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

    int grab = 1;                          /* this seat has exactly one reader */
    ioctl(fd, EVIOCGRAB, &grab);
    return 0;
}

/* Deterministic order: event0, event1, ... rather than readdir's hash order. */
static int open_pointers(finn_pointer *p) {
    for (int n = 0; n < 64 && p->ndev < FINN_PTR_MAX; n++) {
        char path[64];
        snprintf(path, sizeof path, "/dev/input/event%d", n);
        finn_ptr_dev d;
        if (probe_dev(path, &d) != 0) continue;

        char name[128] = {0};
        if (ioctl(d.fd, EVIOCGNAME(sizeof name - 1), name) > 0)
            fprintf(stderr, "finnpointer: %s \"%s\" %s%s\n", path, name,
                    d.is_abs ? "absolute" : "relative",
                    d.is_abs ? " (tablet/touch)" : "");
        p->dev[p->ndev++] = d;
    }
    return p->ndev ? 0 : -1;
}

int finn_pointer_fds(const finn_pointer *p, int *out, int max) {
    int n = 0;
    for (int i = 0; i < p->ndev && n < max; i++)
        if (p->dev[i].fd >= 0) out[n++] = p->dev[i].fd;
    return n;
}

int finn_pointer_open(finn_pointer *p, int screen_w, int screen_h, const char *cursor_svg) {
    memset(p, 0, sizeof *p);
    for (int i = 0; i < FINN_PTR_MAX; i++) p->dev[i].fd = -1;

    open_pointers(p);
    p->screen_w = screen_w;
    p->screen_h = screen_h;
    p->x = screen_w / 2;
    p->y = screen_h / 2;

    fsvg svg;
    if (fsvg_load(cursor_svg, &svg) != 0) {
        fprintf(stderr, "finnpointer: cannot load %s\n", cursor_svg);
        return p->ndev ? 0 : -1;
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

void finn_pointer_close(finn_pointer *p) {
    for (int i = 0; i < p->ndev; i++) {
        if (p->dev[i].fd >= 0) {
            int grab = 0;
            ioctl(p->dev[i].fd, EVIOCGRAB, &grab);
            close(p->dev[i].fd);
            p->dev[i].fd = -1;
        }
    }
    p->ndev = 0;
    free(p->sprite);
    p->sprite = NULL;
}

int finn_pointer_poll(finn_pointer *p) {
    if (!p->ndev) return 0;

    static int logged_abs = 0, logged_rel = 0;
    struct input_event ev;
    int moved = 0;

    for (int i = 0; i < p->ndev; i++) {
        finn_ptr_dev *d = &p->dev[i];
        if (d->fd < 0) continue;

        for (;;) {
            ssize_t n = read(d->fd, &ev, sizeof ev);
            if (n != (ssize_t)sizeof ev) {
                /* ENODEV means the device was unplugged underneath us. */
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close(d->fd);
                    d->fd = -1;
                }
                break;
            }

            if (ev.type == EV_REL) {
                if (ev.code == REL_X)      { p->x += ev.value; moved = 1; }
                else if (ev.code == REL_Y) { p->y += ev.value; moved = 1; }
                else if (ev.code == REL_WHEEL) { /* routed to the shell later */ }
                if (moved && !logged_rel) {
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
                    if (ev.value) p->buttons |= bit;
                    else          p->buttons &= ~bit;
                    moved = 1;
                }
            }

            /* An absolute device reports X and Y within one SYN frame; the
             * frame boundary is where the sample is coherent, so map there. */
            if (ev.type == EV_SYN && d->pending) {
                long nx = (long)(d->ax - d->minx) * (p->screen_w - 1) / (d->maxx - d->minx);
                long ny = (long)(d->ay - d->miny) * (p->screen_h - 1) / (d->maxy - d->miny);
                if (nx != p->x || ny != p->y) {
                    p->x = (int)nx;
                    p->y = (int)ny;
                    moved = 1;
                    if (!logged_abs) {
                        logged_abs = 1;
                        fprintf(stderr, "finnpointer: motion from %s (absolute)\n", d->path);
                    }
                }
                d->pending = 0;
            }
        }
    }

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
