/*
 * finn-comp — FinnOS compositor / rendering engine
 *
 * Direct DRM/KMS renderer. No X11, no Wayland, no GTK/Qt, and no toolkit
 * hiding the pipeline: it draws into a scanout buffer itself and page-flips
 * it, on the GPU through GBM/EGL where one is available and by hand where it
 * is not.
 *
 * Layout:
 *   finnvg       vector rasteriser for the icon set
 *   finntext     Fixel Display text
 *   finnstate    the system state the chrome reflects
 *   finndock     the desktop shell chrome, as a cached layer
 *   finncontrol  the Control Center, as a cached layer
 *   finnclock    the clock page, as a cached layer
 *   finnmenu     the Start Menu, as a cached layer
 *   finnpointer  evdev pointer + cursor sprite
 *   finngpu      GBM/EGL/GLES2 compositing, when the machine has it
 *   finn-comp    DRM/KMS, wallpaper, composition and page flips
 *
 * Frames are only composed when something actually changes: the wallpaper
 * is decoded once, the dock is rasterised only when state changes, and the
 * cursor is composited over a cached base. An untouched desktop redraws
 * nothing at all — on either backend, since both are driven by the same
 * damage signal and paced by the same page flip.
 *
 *   build:  make
 *   run:    finn-comp [wallpaper.webp]
 *   modes:  FINN_MODE=1920x1080 finn-comp
 *   paths:  FINN_GPU=0 finn-comp      (force the software path)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <webp/decode.h>

#include "finndock.h"
#include "finncontrol.h"
#include "finnclock.h"
#include "finnmenu.h"
#include "finnnotify.h"
#include "finndraw.h"
#include "finnblur.h"
#include "finnanim.h"
#include "finnstate.h"
#include "finnaudio.h"
#include "finnpointer.h"
#include "finngpu.h"
#include "finnapps.h"
#include "finnproto.h"
#include "finnsock.h"

/*
 * One visual weight for the chrome.
 *
 * The reference dock is drawn slightly smaller and its panels slightly larger
 * than a straight reading of the measurements gives, so a tray glyph and a
 * panel disc land at the same optical size instead of the dock looking heavy
 * against the panels it anchors. `scale` stays the honest density of the
 * screen; these are composition, and live here because they are a relationship
 * between two surfaces rather than a property of either.
 */
#define DOCK_SCALE   0.950f
#define PANEL_SCALE  1.055f

/*
 * The scrim behind the Start Menu. The desktop does not go away when the
 * menu opens, it stops being the thing you are looking at — so the menu is
 * the only thing in the room that is lit.
 */
#define MENU_SCRIM   0.30f

#define ICON_DIR "/usr/share/finn/icons"
#define FONT_DIR "/usr/share/fonts/OTF"

/* Cap the scanout size: the target is a 1080p-class i3, and 4K dumb
 * buffers would eat the whole memory budget for no benefit. */
#define MAX_W 2560
#define MAX_H 1600

/*
 * Animation pacing. Nothing here runs on a timer: a track that is still
 * moving only shortens the poll, and the page flip paces the actual frames.
 * When the last track settles the compositor is back to blocking on input
 * with zero redraws — the property the whole shell is built around.
 */
#define FRAME_MS 8

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

/* ---------------- wallpaper ---------------- */
static int load_wallpaper(uint32_t *out, int w, int h, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n ? n : 1);
    if (!data || fread(data, 1, n, f) != (size_t)n) { fclose(f); free(data); return -1; }
    fclose(f);

    int sw = 0, sh = 0;
    uint8_t *rgba = WebPDecodeRGBA(data, n, &sw, &sh);
    free(data);
    if (!rgba) return -1;
    fprintf(stderr, "finn-comp: wallpaper %dx%d -> %dx%d (cover)\n", sw, sh, w, h);

    float s = fminf((float)sw / w, (float)sh / h);
    float offx = (sw - w * s) * 0.5f, offy = (sh - h * s) * 0.5f;

    for (int y = 0; y < h; y++) {
        float fy = offy + (y + 0.5f) * s;
        int y0 = (int)fy, y1 = y0 + 1; if (y1 >= sh) y1 = sh - 1;
        float ty = fy - y0;
        for (int x = 0; x < w; x++) {
            float fx = offx + (x + 0.5f) * s;
            int x0 = (int)fx, x1 = x0 + 1; if (x1 >= sw) x1 = sw - 1;
            float tx = fx - x0;
            float r = 0, g = 0, b = 0;
            for (int k = 0; k < 2; k++)
                for (int j = 0; j < 2; j++) {
                    int xi = k ? x1 : x0, yi = j ? y1 : y0;
                    float wg = (k ? tx : 1 - tx) * (j ? ty : 1 - ty);
                    const uint8_t *p = rgba + ((size_t)yi * sw + xi) * 4;
                    r += p[0] * wg; g += p[1] * wg; b += p[2] * wg;
                }
            out[(size_t)y * w + x] = 0xFF000000u |
                ((uint32_t)(r + 0.5f) << 16) | ((uint32_t)(g + 0.5f) << 8) | (uint32_t)(b + 0.5f);
        }
    }
    free(rgba);
    return 0;
}

/* ---------------- DRM ---------------- */
struct fb { uint32_t id, handle; uint32_t *map; uint32_t size; };

static int drm_create_dumb(int fd, int w, int h, struct fb *f) {
    struct drm_mode_create_dumb c = {0};
    c.width = w; c.height = h; c.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0) return -1;
    f->handle = c.handle; f->size = c.size;
    if (drmModeAddFB(fd, w, h, 24, 32, c.pitch, c.handle, &f->id) < 0) return -1;
    struct drm_mode_map_dumb m = {0};
    m.handle = c.handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) return -1;
    f->map = mmap(NULL, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    return (f->map == MAP_FAILED) ? -1 : 0;
}

static drmModeModeInfo pick_mode(drmModeConnector *c) {
    const char *want = getenv("FINN_MODE");
    if (want) {
        int ww = 0, hh = 0;
        if (sscanf(want, "%dx%d", &ww, &hh) == 2) {
            for (int i = 0; i < c->count_modes; i++)
                if (c->modes[i].hdisplay == ww && c->modes[i].vdisplay == hh) {
                    fprintf(stderr, "finn-comp: FINN_MODE %dx%d\n", ww, hh);
                    return c->modes[i];
                }
            fprintf(stderr, "finn-comp: FINN_MODE %s unavailable, choosing automatically\n", want);
        }
    }

    /* Largest mode up to the scanout cap; falls back to the preferred one. */
    drmModeModeInfo best = c->modes[0];
    int best_area = best.hdisplay * best.vdisplay;
    for (int i = 0; i < c->count_modes; i++) {
        drmModeModeInfo m = c->modes[i];
        if (m.hdisplay > MAX_W || m.vdisplay > MAX_H) continue;
        int area = m.hdisplay * m.vdisplay;
        if (area > best_area) { best = m; best_area = area; }
    }
    return best;
}

static volatile int g_flip_done = 1;

static void flip_handler(int fd, unsigned seq, unsigned sec, unsigned usec, void *data) {
    (void)fd; (void)seq; (void)sec; (void)usec; (void)data;
    g_flip_done = 1;
}

static void wait_flip(int fd) {
    while (!g_flip_done && !g_stop) {
        drmEventContext ev = {0};
        ev.version = DRM_EVENT_CONTEXT_VERSION;
        ev.page_flip_handler = flip_handler;
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 100) <= 0) break;
        if (drmHandleEvent(fd, &ev) != 0) break;
    }
}

/* ---------------- control plane provider ----------------
 *
 * finn-comp owns the scanout buffer and the pointer, so it is the provider
 * for display and input verbs. It speaks the line protocol from finnproto.h
 * over /run/finn/comp.sock; finnd sits in front of it as the control plane.
 */

typedef struct {
    finn_pointer *ptr;
    int           w, h;
    uint32_t     *base;      /* wallpaper + dock, no cursor */
    float         scale;
    int           click_pending;   /* button bit for an injected click */
    int           click_release;   /* frames until that click releases   */

    /* The accelerated path keeps no CPU composite: the frame exists in
     * textures, and only until it is presented. A capture therefore waits for
     * the next frame and reads that one back, which is also the only way it can
     * be the frame that is really on the screen. */
    int capture_fd;      /* client waiting on a frame, or -1 */
    int capture_step;
    int gpu;

    /* Set when a verb changed something the frame shows but no other input
     * did — a notification landing while its page is open, say. Without it
     * the loop would correctly decide nothing had moved and leave the screen
     * one event behind. */
    int dirty;
} ctl_ctx;

static void compose_base(uint32_t *base, const uint32_t *wall, int w, int h,
                         float scale, const finn_state *st);

/* Non-destructive: scanning must not mutate the line, because a single
 * command carries several key=value tokens. */
static int kv_find(const char *args, const char *key, const char **val) {
    size_t kl = strlen(key);
    const char *p = args;
    while ((p = strstr(p, key)) != NULL) {
        int at_token_start = (p == args) || p[-1] == ' ' || p[-1] == '\t';
        if (at_token_start && p[kl] == '=') { *val = p + kl + 1; return 1; }
        p += kl;
    }
    return 0;
}

static int kv_int(const char *args, const char *key, int *out) {
    const char *v;
    if (!kv_find(args, key, &v)) return 0;
    *out = atoi(v);
    return 1;
}

static int kv_str(const char *args, const char *key, char *out, int cap) {
    const char *v;
    if (!kv_find(args, key, &v)) return 0;
    int n = 0;
    while (v[n] && v[n] != ' ' && v[n] != '\t' && n < cap - 1) { out[n] = v[n]; n++; }
    out[n] = 0;
    return 1;
}

static void write_state_int(const char *name, int v) {
    finn_state_set(name, v);
}

/* Write `src` (0xAARRGGBB, w x h) to the client as a binary PPM. */
static int ppm_write(int fd, const uint32_t *src, int w, int h, int step) {
    if (step < 1) step = 1;

    int ow = (w + step - 1) / step, oh = (h + step - 1) / step;
    char ph[64];
    int pn = snprintf(ph, sizeof ph, "P6\n%d %d\n255\n", ow, oh);
    size_t px = (size_t)ow * oh * 3;

    char hdr[64];
    int hn = snprintf(hdr, sizeof hdr, "OK BINARY %zu\n", px + (size_t)pn);
    finn_write_all(fd, hdr, (size_t)hn);
    finn_write_all(fd, ph, (size_t)pn);

    uint8_t *row = malloc((size_t)ow * 3);
    if (!row) return -1;
    for (int y = 0; y < oh; y++) {
        const uint32_t *s = src + (size_t)(y * step) * w;
        for (int x = 0; x < ow; x++) {
            uint32_t c = s[x * step];
            row[x * 3 + 0] = (uint8_t)((c >> 16) & 0xFF);
            row[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (uint8_t)(c & 0xFF);
        }
        finn_write_all(fd, row, (size_t)ow * 3);
    }
    free(row);
    return 0;
}

/* The software path's frame is a buffer the compositor owns, so the cursor is
 * added on the way out. The accelerated path's frame already has it. */
static void send_ppm(int fd, ctl_ctx *cx, int step) {
    uint32_t *frame = malloc((size_t)cx->w * cx->h * 4);
    if (!frame) { finn_write_str(fd, "ERR out of memory\n"); return; }
    memcpy(frame, cx->base, (size_t)cx->w * cx->h * 4);
    finn_pointer_draw(cx->ptr, frame, cx->w, cx->h);
    ppm_write(fd, frame, cx->w, cx->h, step);
    free(frame);
}

static void ctl_dispatch(char *line, int fd, ctl_ctx *cx) {
    char *rest = strchr(line, ' ');
    if (rest) { *rest++ = 0; } else { rest = line + strlen(line); }
    const char *verb = line;
    if (!*verb) { finn_write_str(fd, "ERR empty\n"); return; }

    if (!strcmp(verb, "ping")) {
        finn_write_str(fd, "OK " FINN_PROTO_MAGIC "\n");
        return;
    }
    if (!strcmp(verb, "version")) {
        finn_write_str(fd, "OK comp=0.1 proto=" FINN_PROTO_MAGIC "\n");
        return;
    }
    if (!strcmp(verb, "screen.info")) {
        char b[128];
        snprintf(b, sizeof b, "OK w=%d h=%d scale=%.3f\n", cx->w, cx->h, cx->scale);
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "screen.shell")) {
        int dx, dy, dw, dh;
        finndock_geometry(&dx, &dy, &dw, &dh);
        char b[320];
        snprintf(b, sizeof b,
                 "OK cc=%d openness=%.3f cc_anim=%d clock=%d clock_openness=%.3f "
                 "menu=%d menu_openness=%.3f "
                 "dock_anim=%d dock=%d,%d,%d,%d pointer=%d,%d buttons=%d\n",
                 finncontrol_is_open(), finncontrol_openness(), finncontrol_running(),
                 finnclock_is_open(), finnclock_openness(),
                 finnmenu_is_open(), finnmenu_openness(),
                 finndock_running(),
                 dx, dy, dw, dh, cx->ptr->x, cx->ptr->y, cx->ptr->buttons);
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "screen.capture")) {
        int step = 1;
        kv_int(rest, "step", &step);
        if (step < 1) step = 1;
        if (!cx->gpu) { send_ppm(fd, cx, step); return; }
        /* The frame lives in textures until it is presented, so the capture
         * waits for one to be drawn and reads that one back: the frame that is
         * actually on the screen rather than a reconstruction that might not
         * match it. The reply goes out with the frame. */
        if (cx->capture_fd >= 0) { finn_write_str(fd, "ERR capture pending\n"); return; }
        cx->capture_fd = fd;
        cx->capture_step = step;
        return;
    }
    if (!strcmp(verb, "screen.pointer")) {
        char b[128];
        snprintf(b, sizeof b, "OK x=%d y=%d buttons=%d\n",
                 cx->ptr->x, cx->ptr->y, cx->ptr->buttons);
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "state.get")) {
        finn_state st;
        finn_state_read(&st);
        char b[224];
        snprintf(b, sizeof b,
                 "OK battery=%d charging=%d wifi=%d volume=%d brightness=%d "
                 "bluetooth=%d wired=%d dark=%d\n",
                 st.battery_pct, st.charging, st.wifi, st.volume,
                 st.brightness, st.bluetooth, st.wired, st.dark);
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "state.set")) {
        int iv;
        if (kv_int(rest, "battery", &iv))  write_state_int("battery", iv);
        if (kv_int(rest, "charging", &iv)) write_state_int("charging", iv);
        if (kv_int(rest, "wifi", &iv))     write_state_int("wifi", iv);
        if (kv_int(rest, "volume", &iv))   write_state_int("volume", iv);
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "state.clear")) {
        finn_state_clear();
        finn_write_str(fd, "OK\n");
        return;
    }

    /* ---- notifications: the shell's own queue, open to any service ----
     * `notify.post <title>|<body>` rather than key=value, because a title is
     * prose and the protocol splits on spaces: the pipe is the one character a
     * title will not contain. */
    if (!strcmp(verb, "notify.post")) {
        char *bar = strchr(rest, '|');
        if (!*rest || !bar) { finn_write_str(fd, "ERR notify.post <title>|<body>\n"); return; }
        *bar = 0;
        const char *body = bar + 1;
        if (!*body) { finn_write_str(fd, "ERR empty body\n"); return; }
        finn_notify_post(rest, body);
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "notify.clear")) {
        finn_notify_clear();
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "notify.list")) {
        char b[512];
        int n = finn_notify_count(), used = 0;
        used += snprintf(b + used, sizeof b - (size_t)used, "OK count=%d", n);
        for (int i = 0; i < n && used < (int)sizeof b - 96; i++) {
            const finn_note *nt = finn_notify_at(i);
            struct tm tm;
            localtime_r(&nt->at, &tm);
            char when[16];
            strftime(when, sizeof when, "%H:%M", &tm);
            used += snprintf(b + used, sizeof b - (size_t)used, " [%s|%s|%s]",
                             nt->title, nt->body, when);
        }
        snprintf(b + used, sizeof b - (size_t)used, "\n");
        finn_write_str(fd, b);
        return;
    }

    /* ---- real actuators: exactly what the Control Center drives ----
     * These do not write an override, they change the machine. That is the
     * difference between "show me 40%" and "be 40%". */
    if (!strcmp(verb, "sound.get") || !strcmp(verb, "display.get")) {
        finn_state st;
        finn_state_read(&st);
        char b[128];
        if (!strcmp(verb, "sound.get")) {
            const char *el = finn_audio_element();
            snprintf(b, sizeof b, "OK volume=%d element=%s\n", st.volume, el ? el : "none");
        } else {
            snprintf(b, sizeof b, "OK brightness=%d\n", st.brightness);
        }
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "sound.set") || !strcmp(verb, "display.set")) {
        int v = -1;
        int sound = !strcmp(verb, "sound.set");
        int ok = sound ? kv_int(rest, "volume", &v) : kv_int(rest, "brightness", &v);
        if (!ok) { finn_write_str(fd, "ERR missing value\n"); return; }
        int rc = sound ? finn_state_set_volume(v) : finn_state_set_brightness(v);
        if (rc != 0) { finn_write_str(fd, "ERR hardware unavailable\n"); return; }
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "radio.set")) {
        int v, any = 0;
        if (kv_int(rest, "wifi", &v))      { finn_state_set_wifi(v);      any = 1; }
        if (kv_int(rest, "bluetooth", &v)) { finn_state_set_bluetooth(v); any = 1; }
        finn_write_str(fd, any ? "OK\n" : "ERR name wifi= or bluetooth=\n");
        return;
    }
    if (!strcmp(verb, "appearance.set")) {
        int v;
        if (!kv_int(rest, "dark", &v)) {
            finn_write_str(fd, "ERR appearance.set needs dark=0|1\n");
            return;
        }
        finn_state_set_dark(v);
        finn_write_str(fd, "OK\n");
        return;
    }

    /* ---- pointer injection ---- */
    if (!strcmp(verb, "pointer.move")) {
        int x, y;
        if (!kv_int(rest, "x", &x) || !kv_int(rest, "y", &y)) {
            finn_write_str(fd, "ERR pointer.move needs x= y=\n");
            return;
        }
        cx->ptr->x = x; cx->ptr->y = y; cx->ptr->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "pointer.by")) {
        int dx = 0, dy = 0;
        kv_int(rest, "dx", &dx);
        kv_int(rest, "dy", &dy);
        cx->ptr->x += dx; cx->ptr->y += dy; cx->ptr->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "pointer.click") || !strcmp(verb, "pointer.down") ||
        !strcmp(verb, "pointer.up")) {
        char btn[32] = "left";
        kv_str(rest, "button", btn, sizeof btn);
        int bit = !strcmp(btn, "right") ? FINN_BTN_RIGHT
                : (!strcmp(btn, "middle") ? FINN_BTN_MIDDLE : FINN_BTN_LEFT);

        if (!strcmp(verb, "pointer.down")) {
            cx->ptr->buttons |= bit;
        } else if (!strcmp(verb, "pointer.up")) {
            cx->ptr->buttons &= ~bit;
        } else {
            /* A click has to be an edge the shell can see, so the press is
             * injected now and released after the shell has handled it. */
            cx->ptr->buttons |= bit;
            cx->click_pending = bit;
            cx->click_release = 1;
        }
        cx->ptr->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "pointer.scroll")) {
        /* SCROLL is routed to the shell once there is scrollable content. */
        finn_write_str(fd, "OK\n");
        return;
    }

    finn_write_str(fd, "ERR unknown verb\n");
}

/* ---------------- shell surfaces ----------------
 *
 * The dock and the Control Center are chrome rather than windows: both are
 * composited straight into the cached base layer, so a frame stays one blit
 * plus the cursor no matter how many surfaces are showing.
 */
static double finn_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* Build the chrome layers for this frame. Both backends want these first —
 * the software one blits them into a buffer, the accelerated one uploads them
 * as textures — so the shell is rasterised exactly once either way. */
static void build_chrome(int w, int h, float scale, const finn_state *st) {
    finn_theme_set(st->dark);

    /*
     * The tray's well belongs to the Control Center, not to the dock: it is
     * the mark that ties the pill to the panel above it, so it is driven by
     * the panel's own openness and fades out with it.
     */
    finndock_set_well(finncontrol_openness());
    finndock_set_clock_well(finnclock_openness());
    finndock_render(w, h, scale * DOCK_SCALE, st);
    if (finncontrol_visible()) {
        int dx, dy, dw, dh;
        finndock_geometry(&dx, &dy, &dw, &dh);
        finncontrol_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, st);
    }
    /* The clock page shares the tray's well with nothing: the well marks the
     * pill that opens the Control Center, and this page opens from the clock
     * itself, which is why it is a second surface rather than a tab. */
    if (finnclock_visible()) {
        int dx, dy, dw, dh;
        finndock_geometry(&dx, &dy, &dw, &dh);
        finnclock_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, st);
    }
    /* The Start Menu is an overlay above the shell chrome: it is opened from
     * the dock, but it covers the desktop rather than sitting beside the
     * dock, so it is drawn after everything the dock owns. */
    if (finnmenu_visible()) {
        int dx, dy, dw, dh;
        finndock_geometry(&dx, &dy, &dw, &dh);
        finnmenu_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, st);
    }
}

static void compose_base(uint32_t *base, const uint32_t *wall, int w, int h,
                         float scale, const finn_state *st) {
    build_chrome(w, h, scale, st);
    memcpy(base, wall, (size_t)w * h * 4);
    finndock_blit(base, w, h, 0, 0);

    /* the panel is in the base layer too, so a pointer move over it costs a
     * blit and nothing more. While it is mid-open it is transformed instead:
     * the raster is unchanged, only where and how large it lands. */
    if (finncontrol_visible()) {
        if (finncontrol_settled()) finncontrol_blit(base, w, h, 0, 0);
        else                  finncontrol_blit_scaled(base, w, h);
    }
    if (finnclock_visible()) {
        if (finnclock_settled()) finnclock_blit(base, w, h, 0, 0);
        else                     finnclock_blit_scaled(base, w, h);
    }
    if (finnmenu_visible()) {
        /* the scrim goes down before the menu goes up, so the menu is the
         * only thing in the room that is lit */
        float dim = MENU_SCRIM * finnmenu_openness();
        if (dim > 0.004f) finn_frame_dim(base, (size_t)w * h, dim);
        if (finnmenu_settled()) finnmenu_blit(base, w, h, 0, 0);
        else                    finnmenu_blit_scaled(base, w, h);
    }
}

/* ---------------- accelerated path ----------------
 *
 * The same four surfaces the software path composites, kept as textures and
 * pasted as quads. What this buys is not just speed: the wallpaper stops being
 * copied 16 MB at a time, the panel's transform becomes a vertex transform,
 * and the appearance dissolve happens in the texture unit instead of in a
 * per-pixel software blend.
 *
 * The two chrome layers are uploaded only when their revision moves, which is
 * why finndock and finncontrol both carry one. Everything else is static.
 */
typedef struct {
    finngpu *gpu;

    unsigned tex_wall;
    unsigned tex_cursor;

    unsigned tex_dock, tex_dock_prev, dock_rev;
    unsigned tex_cc,   tex_cc_prev,   cc_rev;
    unsigned tex_clock, tex_clock_prev, clock_rev;
    unsigned tex_menu,  tex_menu_prev,  menu_rev;

    /* One black pixel, stretched over the screen: the scrim behind the Start
     * Menu. A texture is cheaper than a second framebuffer and does not need
     * a shader of its own. */
    unsigned tex_scrim;

    /* The chrome the current theme is dissolving away from. The wallpaper is
     * not part of it: the picture is the one thing an appearance change does
     * not touch, so there is nothing to dissolve it against. */
    int has_prev;

    /* the offscreen composite the dissolve crossfades against */
    unsigned fbo, fbo_tex;
    int      fbo_w, fbo_h;
} gpu_ctx;

/* Upload a layer if its raster has moved on. A NULL layer means the surface is
 * not showing, which is not the same thing as an empty one. */
static unsigned gpu_upload(gpu_ctx *g, const uint32_t *px,
                           int w, int h, unsigned rev, unsigned *tex, unsigned *seen) {
    if (!px) return 0;
    if (*tex && rev == *seen) return *tex;
    if (*tex) finngpu_tex_update(g->gpu, *tex, px, w, h);
    else      *tex = finngpu_tex(g->gpu, px, w, h);
    *seen = rev;
    return *tex;
}

/* Draw one accelerated frame; returns the DRM framebuffer to flip to. When
 * `shot` is given the frame is read back into it on the way out, before the
 * swap — the one moment it exists here. */
static uint32_t gpu_frame(gpu_ctx *g, int w, int h, float scale,
                          const finn_state *st, const finn_pointer *ptr,
                          float theme_t, uint32_t *shot) {
    build_chrome(w, h, scale, st);

    int dx = 0, dy = 0, dw = 0, dh = 0;
    unsigned drev = 0;
    const uint32_t *dl = finndock_layer(&dx, &dy, &dw, &dh, &drev);
    unsigned dtex = gpu_upload(g, dl, dw, dh, drev, &g->tex_dock, &g->dock_rev);

    int cx = 0, cy = 0, cw = 0, ch = 0;
    unsigned crev = 0;
    const uint32_t *cl = finncontrol_layer(&cx, &cy, &cw, &ch, &crev);
    unsigned ctex = gpu_upload(g, cl, cw, ch, crev, &g->tex_cc, &g->cc_rev);

    float tx = 0, ty = 0, tw = 0, th = 0, ta = 0;
    if (cl) finncontrol_transform(&tx, &ty, &tw, &th, &ta);

    int kx = 0, ky = 0, kw = 0, kh = 0;
    unsigned krev = 0;
    const uint32_t *kl = finnclock_layer(&kx, &ky, &kw, &kh, &krev);
    unsigned ktex = gpu_upload(g, kl, kw, kh, krev, &g->tex_clock, &g->clock_rev);

    float qx = 0, qy = 0, qw = 0, qh = 0, qa = 0;
    if (kl) finnclock_transform(&qx, &qy, &qw, &qh, &qa);

    int mx = 0, my = 0, mw = 0, mh = 0;
    unsigned mrev = 0;
    const uint32_t *ml = finnmenu_layer(&mx, &my, &mw, &mh, &mrev);
    unsigned mtex = gpu_upload(g, ml, mw, mh, mrev, &g->tex_menu, &g->menu_rev);

    float sx = 0, sy = 0, sw = 0, sh = 0, sa = 0;
    if (ml) finnmenu_transform(&sx, &sy, &sw, &sh, &sa);

    /*
     * The dissolve. The picture takes no part in it: an appearance change
     * re-grades what the shell draws on top of the wallpaper and leaves the
     * wallpaper itself alone, so there is nothing there to crossfade. What
     * changes is the chrome, and the two versions of it overlap, so drawing
     * the new dock and panel straight over the old ones at the fade's value
     * would attenuate the old chrome a second time underneath them and let it
     * show through the new. The new composite goes offscreen as one image
     * first, and the two whole images are then crossfaded. It costs a render
     * target for the 0.45s it runs.
     */
    int fading = theme_t < 0.999f && g->has_prev;
    unsigned wall = g->tex_wall;

    if (fading) {
        if (!g->fbo || g->fbo_w != w || g->fbo_h != h) {
            if (g->fbo) finngpu_fbo_free(g->gpu, g->fbo, g->fbo_tex);
            g->fbo = g->fbo_tex = 0;
            if (finngpu_fbo(g->gpu, &g->fbo, &g->fbo_tex, w, h) != 0)
                g->fbo = g->fbo_tex = 0;
            else { g->fbo_w = w; g->fbo_h = h; }
        }
    } else if (g->fbo) {
        finngpu_fbo_free(g->gpu, g->fbo, g->fbo_tex);
        g->fbo = g->fbo_tex = 0;
    }

    finngpu_begin(g->gpu);

    if (fading && g->fbo) {
        finngpu_target(g->gpu, g->fbo);
        finngpu_paste(g->gpu, wall, 0, 0, (float)w, (float)h, 1.0f);
        if (dl) finngpu_paste(g->gpu, dtex, (float)dx, (float)dy, (float)dw, (float)dh, 1.0f);
        if (cl) finngpu_paste(g->gpu, ctex, tx, ty, tw, th, ta);
        if (kl) finngpu_paste(g->gpu, ktex, qx, qy, qw, qh, qa);
        finngpu_target(g->gpu, 0);

        /* the chrome that was on screen, over the picture, then the new one
         * crossfading in over it */
        finngpu_paste(g->gpu, wall, 0, 0, (float)w, (float)h, 1.0f);
        if (dl && g->tex_dock_prev)
            finngpu_paste(g->gpu, g->tex_dock_prev, (float)dx, (float)dy,
                          (float)dw, (float)dh, 1.0f);
        if (cl && g->tex_cc_prev)
            finngpu_paste(g->gpu, g->tex_cc_prev, tx, ty, tw, th, ta);
        if (kl && g->tex_clock_prev)
            finngpu_paste(g->gpu, g->tex_clock_prev, qx, qy, qw, qh, qa);
        finngpu_paste_raw(g->gpu, g->fbo_tex, 0, 0, (float)w, (float)h, theme_t);
    } else {
        finngpu_paste(g->gpu, wall, 0, 0, (float)w, (float)h, 1.0f);
        if (dl)
            finngpu_paste(g->gpu, dtex, (float)dx, (float)dy,
                          (float)dw, (float)dh, 1.0f);
        if (cl)
            finngpu_paste(g->gpu, ctex, tx, ty, tw, th, ta);
        if (kl)
            finngpu_paste(g->gpu, ktex, qx, qy, qw, qh, qa);
    }

    /*
     * The scrim and the menu are drawn last, over everything: the menu is
     * the front door, and nothing in the room is allowed to be in front of
     * it. The appearance switch never happens with the menu open — the
     * Control Center owns that control — so the menu is not part of the
     * dissolve.
     */
    float dim = MENU_SCRIM * finnmenu_openness();
    if (dim > 0.004f && g->tex_scrim)
        finngpu_paste(g->gpu, g->tex_scrim, 0, 0, (float)w, (float)h, dim);
    if (ml)
        finngpu_paste(g->gpu, mtex, sx, sy, sw, sh, sa);

    if (ptr->sprite && ptr->sprite_px > 0 && g->tex_cursor)
        finngpu_paste(g->gpu, g->tex_cursor,
                      (float)(ptr->x - ptr->hot_x), (float)(ptr->y - ptr->hot_y),
                      (float)ptr->sprite_px, (float)ptr->sprite_px, 1.0f);

    if (shot) finngpu_readback(g->gpu, shot);

    uint32_t fb = 0;
    if (!finngpu_end(g->gpu, &fb)) return 0;
    return fb;
}

/* ---------------- shell input ----------------
 *
 * The pointer is ours, so the shell is what turns a click into an action.
 * There is no toolkit in the middle to do hit-testing for us.
 */
static int g_prev_buttons = 0;
static int g_drag = FINNCONTROL_NONE;

static int cc_activate(int act, float v01, const finn_state *st) {
    switch (act) {
    case FINNCONTROL_WIFI:
        finn_state_set_wifi(st->wifi > 0 ? 0 : 1);
        return 1;
    case FINNCONTROL_BLUETOOTH:
        finn_state_set_bluetooth(st->bluetooth > 0 ? 0 : 1);
        return 1;
    case FINNCONTROL_APPEARANCE:
        finn_state_set_dark(!st->dark);
        return 1;
    case FINNCONTROL_BRIGHTNESS:
    case FINNCONTROL_VOLUME:
        if (v01 < 0.0f) return 0;
        if (act == FINNCONTROL_BRIGHTNESS) finn_state_set_brightness((int)(v01 * 100.0f + 0.5f));
        else                          finn_state_set_volume((int)(v01 * 100.0f + 0.5f));
        return 1;
    default:
        return 0;
    }
}

static int cc_set_level(int act, float v01) {
    if (v01 < 0.0f) return 0;
    int pct = (int)(v01 * 100.0f + 0.5f);
    if (act == FINNCONTROL_BRIGHTNESS) { finn_state_set_brightness(pct); return 1; }
    if (act == FINNCONTROL_VOLUME)     { finn_state_set_volume(pct);     return 1; }
    return 0;
}

/*
 * Ask the control plane to do something.
 *
 * The compositor never spawns anything and never powers the machine off by
 * itself: finnd is the one permissioned entry point, and it keeps the audit
 * trail of who asked for what. The shell asks it the same way a script
 * would.
 */
static int ctl_ask(const char *req, char *reply, int cap) {
    return finn_call(FINN_CONTROL_SOCK, req, reply, cap);
}

/* The reason a request failed, in words the shell can show. */
static const char *ctl_why(const char *reply) {
    if (!reply || !*reply) return "No answer from the control plane";
    if (strncmp(reply, "ERR ", 4) == 0) return reply + 4;
    return reply;
}

/*
 * An app was pressed. The menu does not know how to start anything — it only
 * knows what was pressed — so the request goes to the control plane and the
 * answer comes back as a notification.
 */
static void menu_launch(int index) {
    const finn_app *a = finn_apps_at(index);
    if (!a) return;

    if (!a->installed) {
        finn_notify_post(a->name, "Not installed on this machine");
        return;
    }

    char req[160], reply[256];
    snprintf(req, sizeof req, "app.launch %s", a->id);
    if (ctl_ask(req, reply, sizeof reply) == 0 && !strncmp(reply, "OK", 2) == 0)
        finn_notify_post(a->name, "Started");
    else
        finn_notify_post(a->name, ctl_why(reply));
}

/* The power row. The control plane does the work; the shell only says what
 * was asked for. */
static void menu_power(const char *action) {
    char req[64], reply[256];
    snprintf(req, sizeof req, "power %s", action);
    if (ctl_ask(req, reply, sizeof reply) == 0 && !strncmp(reply, "OK", 2) == 0)
        finn_notify_post("FinnOS",
                         strcmp(action, "restart") ? "Restarting" : "Shutting down");
    else
        finn_notify_post("FinnOS", ctl_why(reply));
}

/* Returns 1 when the chrome itself changed and the layer must be rebuilt. */
static int shell_input(finn_pointer *p, const finn_state *st) {
    int now = p->buttons & FINN_BTN_LEFT;
    int was = g_prev_buttons & FINN_BTN_LEFT;
    g_prev_buttons = p->buttons;

    if (now && !was) {
        float v01 = -1.0f;

        /*
         * The Start Menu floats above everything else, so it sees the click
         * first. Inside it the click is its own; outside it the click
         * dismisses it and goes no further — the rule the Control Center
         * already follows.
         */
        if (finnmenu_is_open()) {
            int app_index = -1;
            int what = finnmenu_hit(p->x, p->y, &app_index);
            if (what == FINNMENU_APP) {
                finnmenu_press(FINNMENU_APP);
                menu_launch(app_index);
                finnmenu_set_open(0);
                return 1;
            }
            if (what == FINNMENU_RESTART || what == FINNMENU_SHUTDOWN) {
                finnmenu_press(what);
                menu_power(what == FINNMENU_RESTART ? "restart" : "shutdown");
                finnmenu_set_open(0);
                return 1;
            }
            if (what != FINNMENU_NONE) {
                /* the menu's own surface: the click is swallowed by it */
                finnmenu_press(FINNMENU_PANEL);
                return 0;
            }
            finnmenu_set_open(0);
            return 1;
        }

        /* The clock page floats above everything, so it sees the click first:
         * inside it the click is its own, outside it the click dismisses it
         * and goes no further — the rule the Control Center already follows. */
        int phit = finnclock_hit(p->x, p->y);
        if (phit == FINNCLOCK_CLEAR) {
            finn_notify_clear();
            return 1;
        }
        if (phit == FINNCLOCK_PANEL)
            return 0;

        int act = finncontrol_hit(p->x, p->y, &v01);

        g_drag = (act == FINNCONTROL_BRIGHTNESS || act == FINNCONTROL_VOLUME) ? act : FINNCONTROL_NONE;

        /* Only the controls that can act, and can act as a button, give press
         * feedback: a squeeze on a tile with no hardware behind it would be a
         * promise it cannot keep, and a slider must not move under the drag. */
        switch (act) {
        case FINNCONTROL_WIFI: case FINNCONTROL_BLUETOOTH: case FINNCONTROL_APPEARANCE:
            finncontrol_press(act);
            break;
        default:
            break;
        }

        if (act != FINNCONTROL_NONE && act != FINNCONTROL_PANEL)
            return cc_activate(act, v01, st);
        if (act == FINNCONTROL_PANEL)
            return 0;                       /* swallowed by the panel */

        int hit = finndock_hit(p->x, p->y);
        /* The clock is a control but not a status tile: it takes no wash. */
        finndock_press(hit == FINNDOCK_HIT_CLOCK ? FINNDOCK_HIT_NONE : hit);

        /* Three controls, three pages: the launcher opens the Start Menu, the
         * status block opens the Control Center, the clock opens the clock
         * page, and never more than one at a time. */
        if (hit == FINNDOCK_HIT_CLOCK) {
            int open = !finnclock_is_open();
            finnmenu_set_open(0);
            finncontrol_set_open(0);
            finnclock_set_open(open);
            return 1;
        }
        if (hit == FINNDOCK_HIT_TRAY) {
            int open = !finncontrol_is_open();
            finnmenu_set_open(0);
            finnclock_set_open(0);
            finncontrol_set_open(open);
            return 1;
        }
        if (hit == FINNDOCK_HIT_LAUNCHER) {
            int open = !finnmenu_is_open();
            finncontrol_set_open(0);
            finnclock_set_open(0);
            finnmenu_set_open(open);
            return 1;
        }
        if (finnclock_is_open()) { finnclock_set_open(0); return 1; }
        if (finncontrol_is_open())    { finncontrol_set_open(0);    return 1; }
        return 0;
    }

    if (!now && was) {
        g_drag = FINNCONTROL_NONE;
        finndock_press(FINNDOCK_HIT_NONE);
        finncontrol_press(FINNCONTROL_NONE);
        finnmenu_press(FINNMENU_NONE);
        return 0;
    }

    /* press-and-drag keeps setting the slider the press landed on */
    if (now && g_drag != FINNCONTROL_NONE)
        return cc_set_level(g_drag, finncontrol_drag(g_drag, p->x));

    return 0;
}

int main(int argc, char **argv) {
    const char *wall_path = (argc > 1) ? argv[1] : "/usr/share/finn/wallpaper/default.webp";

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    if (finndock_init(ICON_DIR, FONT_DIR) != 0)
        fprintf(stderr, "finn-comp: running without shell chrome\n");
    if (finncontrol_init(ICON_DIR, FONT_DIR) != 0)
        fprintf(stderr, "finn-comp: control center unavailable\n");
    if (finnclock_init(FONT_DIR) != 0)
        fprintf(stderr, "finn-comp: clock page unavailable\n");
    {
        const char *reg = getenv("FINN_APPS");
        finnmenu_init(ICON_DIR, FONT_DIR, reg && reg[0] ? reg : FINN_APPS_CATALOGUE);
    }

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { fprintf(stderr, "finn-comp: open card0: %s\n", strerror(errno)); return 1; }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { fprintf(stderr, "finn-comp: no DRM resources\n"); return 1; }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) { conn = c; break; }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) { fprintf(stderr, "finn-comp: no connected display\n"); return 1; }

    drmModeModeInfo mode = pick_mode(conn);
    int w = mode.hdisplay, h = mode.vdisplay;
    float scale = fmaxf(0.85f, fminf((float)h / 1080.0f, 2.0f));
    fprintf(stderr, "finn-comp: %dx%d @%dHz  scale %.2f  (%d modes offered)\n",
            w, h, mode.vrefresh, scale, conn->count_modes);

    drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
    uint32_t crtc_id = enc ? enc->crtc_id : res->crtcs[0];
    if (enc) drmModeFreeEncoder(enc);

    /*
     * Try the accelerated path first. Nothing about it is assumed: GBM and EGL
     * are probed, the config is chosen by matching the scanout format, and a
     * machine that cannot give us a scanout-capable buffer lands on the
     * software path with nothing else changed. The dumb buffers the software
     * path needs are then never allocated, which is most of the memory saving.
     */
    finngpu *gpu = NULL;
    {
        const char *want = getenv("FINN_GPU");
        if (want && want[0] == '0') {
            fprintf(stderr, "finn-comp: FINN_GPU=0, accelerated path disabled\n");
        } else {
            gpu = finngpu_open(fd, w, h);
            if (gpu)
                fprintf(stderr, "finn-comp: compositing on the GPU — %s\n",
                        finngpu_renderer(gpu));
            else
                fprintf(stderr, "finn-comp: falling back to software compositing\n");
        }
    }

    struct fb fbs[2];
    memset(fbs, 0, sizeof(fbs));
    if (!gpu)
        for (int i = 0; i < 2; i++)
            if (drm_create_dumb(fd, w, h, &fbs[i]) < 0) {
                fprintf(stderr, "finn-comp: dumb buffer %d failed: %s\n", i, strerror(errno));
                return 1;
            }

    /* wall = clean wallpaper; base = wallpaper + dock, ready to blit */
    uint32_t *wall = malloc((size_t)w * h * 4);
    uint32_t *base = malloc((size_t)w * h * 4);
    if (!wall || !base) { fprintf(stderr, "finn-comp: out of memory\n"); return 1; }
    if (load_wallpaper(wall, w, h, wall_path) != 0) {
        fprintf(stderr, "finn-comp: wallpaper unavailable, flat fill\n");
        for (int i = 0; i < w * h; i++) wall[i] = 0xFFEFE4F2u;
    }

    /*
     * The wallpaper is static, so it is blurred once, here, at 1/8 scale.
     * From this point every frosted surface samples this instead of the
     * picture itself — see finnblur.h for why that is free per frame.
     */
    finnblur_build(wall, w, h);

    /*
     * The wallpaper is the one surface an appearance change does not touch.
     * The picture is what the room looks like; the shell re-grades on top of
     * it, and leaving the picture alone is what keeps a light-to-dark switch
     * reading as the furniture changing rather than the screen dimming.
     */

    finn_pointer ptr;
    if (finn_pointer_open(&ptr, w, h, ICON_DIR "/cursor.svg") != 0)
        fprintf(stderr, "finn-comp: no pointer device\n");
    if (ptr.ndev == 0) fprintf(stderr, "finn-comp: no pointer device found\n");

    finn_state st, prev;
    finn_state_read(&st);
    prev = st;

    /* The queue is empty at every start, by design: the page should be able to
     * say nothing has happened and be right. The one thing that has certainly
     * happened is this session beginning, so that is what it opens with. */
    finn_notify_post("FinnOS", "Session started");

    compose_base(base, wall, w, h, scale, &st);

    /*
     * Warm both panels before the loop starts. Rasterising a panel is the most
     * expensive thing the shell does, and paying for it on the frame a panel
     * is first opened puts the cost inside the opening animation, where it
     * reads as a stutter at exactly the moment the user is watching. Nothing
     * is shown: the layers are built once and the build key keeps them.
     */
    {
        int dx, dy, dw, dh;
        finndock_geometry(&dx, &dy, &dw, &dh);
        finncontrol_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, &st);
        finnclock_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, &st);
        finnmenu_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, &st);
    }

    /* the control plane needs somewhere to park explicit state overrides */
    mkdir("/run/finn", 0755);
    mkdir(FINN_STATE_DIR, 0755);

    int ctl = finn_listen(FINN_COMP_SOCK, 0660);
    if (ctl < 0) fprintf(stderr, "finn-comp: no control socket: %s\n", strerror(errno));
    else         fprintf(stderr, "finn-comp: control on %s\n", FINN_COMP_SOCK);

    ctl_ctx cx = {
        .ptr = &ptr, .w = w, .h = h, .base = base, .scale = scale,
        .capture_fd = -1, .gpu = gpu != NULL
    };

    /* The static surfaces go up once. Only the chrome is ever re-uploaded,
     * and only when its own revision says the pixels moved. */
    gpu_ctx gx;
    memset(&gx, 0, sizeof gx);
    gx.gpu = gpu;
    if (gpu) {
        gx.tex_wall = finngpu_tex(gpu, wall, w, h);
        if (ptr.sprite && ptr.sprite_px > 0)
            gx.tex_cursor = finngpu_tex(gpu, ptr.sprite, ptr.sprite_px, ptr.sprite_px);
        {
            static const uint32_t black[4] = { 0xFF000000u, 0xFF000000u,
                                               0xFF000000u, 0xFF000000u };
            gx.tex_scrim = finngpu_tex(gpu, black, 2, 2);
        }
    }

    enum { MAX_CLIENTS = 8 };
    int clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i] = -1;

    int cur = 0;
    uint32_t first_fb;
    if (gpu) {
        /* the first frame is produced exactly as every later one is, and then
         * handed to the CRTC: there is nothing yet to flip from */
        first_fb = gpu_frame(&gx, w, h, scale, &st, &ptr, 1.0f, NULL);
        if (!first_fb) {
            fprintf(stderr, "finn-comp: the GPU produced no first frame\n");
            return 1;
        }
    } else {
        memcpy(fbs[cur].map, base, (size_t)w * h * 4);
        finn_pointer_draw(&ptr, fbs[cur].map, w, h);
        first_fb = fbs[cur].id;
    }

    if (drmModeSetCrtc(fd, crtc_id, first_fb, 0, 0, &conn->connector_id, 1, &mode) < 0) {
        fprintf(stderr, "finn-comp: setcrtc failed: %s\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "finn-comp: desktop live %dx%d\n", w, h);
    fflush(stderr);

    /*
     * The appearance switch keeps the frame that was on screen so it can
     * dissolve into the new one. The software path keeps it as a buffer; the
     * accelerated path already holds the two textures and lets the blend unit
     * do the work, so it does not need another 16 MB at all.
     */
    uint32_t *base_prev = gpu ? NULL : malloc((size_t)w * h * 4);
    finn_anim theme_fade;
    memset(&theme_fade, 0, sizeof theme_fade);
    finn_anim_jump(&theme_fade, 1.0f);

    double last_t = finn_now();

    while (!g_stop) {
        struct pollfd pf[FINN_PTR_MAX + 1 + MAX_CLIENTS];
        int nf = 0, ictl = -1, cslot[MAX_CLIENTS];
        int pslot[FINN_PTR_MAX];

        int nptr = finn_pointer_fds(&ptr, pslot, FINN_PTR_MAX);
        for (int i = 0; i < nptr; i++) {
            pf[nf].fd = pslot[i];
            pf[nf].events = POLLIN;
            pslot[i] = nf++;
        }
        if (ctl >= 0)    { pf[nf].fd = ctl;    pf[nf].events = POLLIN; ictl = nf++; }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            cslot[i] = -1;
            if (clients[i] >= 0) { pf[nf].fd = clients[i]; pf[nf].events = POLLIN; cslot[i] = nf++; }
        }

        /* A frame is only due while something is actually moving — or while a
         * capture is waiting on one to read back. Between animations the
         * compositor blocks on input exactly as before. */
        int busy = finndock_running() || finncontrol_running() || finnclock_running()
                   || finnmenu_running() || finn_anim_running(&theme_fade)
                   || cx.capture_fd >= 0;
        int pr = poll(pf, (nfds_t)nf, busy ? FRAME_MS : 400);

        int moved = 0;
        if (pr > 0) {
            for (int i = 0; i < nptr; i++) {
                short rev = pf[pslot[i]].revents;
                if (rev & (POLLIN | POLLERR | POLLHUP)) {
                    moved = finn_pointer_poll(&ptr);
                    break;
                }
            }

            if (ictl >= 0 && (pf[ictl].revents & POLLIN)) {
                int c = finn_accept(ctl);
                if (c >= 0) {
                    int slot = -1;
                    for (int i = 0; i < MAX_CLIENTS; i++)
                        if (clients[i] < 0) { slot = i; break; }
                    if (slot < 0) close(c); else clients[slot] = c;
                }
            }

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (cslot[i] < 0 || !(pf[cslot[i]].revents & POLLIN)) continue;
                char line[FINN_MAX_LINE];
                int n = finn_read_line(clients[i], line, sizeof line);
                if (n <= 0) { close(clients[i]); clients[i] = -1; continue; }
                ctl_dispatch(line, clients[i], &cx);
            }
        }

        double now_t = finn_now();
        float dt = (float)(now_t - last_t);
        last_t = now_t;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        if (dt > 0.10f) dt = 0.10f;      /* never leap after a stall */

        /* The loop wakes on input, not on a timeline, so the gap before a click
         * can be most of a poll timeout. Feeding that gap to a track that
         * starts on this very frame would skip a third of its animation in one
         * step — the panel would appear already half open and then crawl. The
         * tracks advance by frame time instead. */
        float adt = dt > 0.034f ? 0.034f : dt;

        int shell_changed = shell_input(&ptr, &st);
        if (cx.click_release > 0 && --cx.click_release == 0)
            ptr.buttons &= ~cx.click_pending;

        if (ptr.dirty) { ptr.dirty = 0; moved = 1; }

        finn_state_read(&st);
        int schanged = finn_state_differs(&st, &prev);

        /* Hardware transitions the shell can vouch for, announced once each.
         * Nothing is invented here: if the machine never changes, the page
         * stays quiet. */
        if (schanged) {
            if (prev.battery_pct >= 0 && st.battery_pct >= 0 &&
                prev.charging != st.charging) {
                finn_notify_post("Power",
                                 st.charging ? "Charging started" : "Running on battery");
            }
            if (prev.battery_pct > 20 && st.battery_pct <= 20 && !st.charging)
                finn_notify_post("Battery low", "20% or less remaining");
            if (prev.wired != st.wired && st.wired >= 0)
                finn_notify_post("Network",
                                 st.wired ? "Wired link is up" : "Wired link is down");
            if (prev.wifi != st.wifi && st.wifi >= 0 && prev.wifi < 0)
                finn_notify_post("Network", "Wireless adapter available");
        }

        /* appearance is a dissolve of the chrome: bank the rasters that are
         * leaving, then hand the new theme the whole duration of the fade to
         * arrive. The wallpaper is never banked, because it is not changing. */
        if (schanged && prev.dark != st.dark && (base_prev || gpu)) {
            if (gpu) {
                /* Moving a texture is free, and it is what gives the dissolve
                 * something to come from. */
                if (gx.tex_dock) {
                    finngpu_tex_free(gpu, gx.tex_dock_prev);
                    gx.tex_dock_prev = gx.tex_dock;
                    gx.tex_dock = 0;
                    gx.dock_rev = 0;
                    gx.has_prev = 1;
                }
                if (gx.tex_cc) {
                    finngpu_tex_free(gpu, gx.tex_cc_prev);
                    gx.tex_cc_prev = gx.tex_cc;
                    gx.tex_cc = 0;
                    gx.cc_rev = 0;
                    gx.has_prev = 1;
                }
                if (gx.tex_clock) {
                    finngpu_tex_free(gpu, gx.tex_clock_prev);
                    gx.tex_clock_prev = gx.tex_clock;
                    gx.tex_clock = 0;
                    gx.clock_rev = 0;
                    gx.has_prev = 1;
                }
                if (gx.tex_menu) {
                    finngpu_tex_free(gpu, gx.tex_menu_prev);
                    gx.tex_menu_prev = gx.tex_menu;
                    gx.tex_menu = 0;
                    gx.menu_rev = 0;
                    gx.has_prev = 1;
                }
            }
            if (base_prev) memcpy(base_prev, base, (size_t)w * h * 4);
            finn_anim_jump(&theme_fade, 0.0f);
            finn_anim_to(&theme_fade, 1.0f, 0.45f, 0.0f, FINN_EASE_IN_OUT);
        }

        /* the tray's status block is the only thing that answers a hover:
         * the pointer position decides, and a non-answer changes nothing */
        finndock_hover_at(ptr.x, ptr.y);
        finnmenu_hover_at(ptr.x, ptr.y);

        int anim = finndock_tick(adt);
        anim |= finncontrol_tick(adt);
        anim |= finnclock_tick(adt);
        anim |= finnmenu_tick(adt);
        anim |= finn_anim_tick(&theme_fade, adt);

        if (cx.dirty) { cx.dirty = 0; shell_changed = 1; }

        if (!moved && !schanged && !shell_changed && !anim && cx.capture_fd < 0 &&
            !finndock_clock_tick()) continue;

        prev = st;

        if (gpu) {
            /* gpu_frame builds the chrome itself; the composite is never
             * materialised on the CPU at all */
            uint32_t *shot = cx.capture_fd >= 0 ? base : NULL;
            uint32_t fbid = gpu_frame(&gx, w, h, scale, &st, &ptr,
                                      finn_anim_v(&theme_fade), shot);
            if (fbid) {
                g_flip_done = 0;
                if (drmModePageFlip(fd, crtc_id, fbid, DRM_MODE_PAGE_FLIP_EVENT, NULL) == 0)
                    cur = -1;
                else {
                    g_flip_done = 1;
                    fprintf(stderr, "finn-comp: page flip failed: %s\n", strerror(errno));
                }
            }
            /* The flip is the clock. Landing is also what frees the buffer
             * that has just left the screen, so nothing is reclaimed early. */
            wait_flip(fd);
            finngpu_release(gpu);

            if (shot) {
                ppm_write(cx.capture_fd, base, w, h, cx.capture_step);
                cx.capture_fd = -1;
            }
        } else {
            if (schanged || shell_changed || anim)
                compose_base(base, wall, w, h, scale, &st);

            int next = cur ^ 1;
            wait_flip(fd);

            float tf = finn_anim_v(&theme_fade);
            if (base_prev && tf < 0.999f) {
                uint32_t m = (uint32_t)(tf * 256.0f + 0.5f);
                if (m > 256u) m = 256u;
                finn_frame_blend(fbs[next].map, base_prev, base, (size_t)w * h, m);
            } else {
                memcpy(fbs[next].map, base, (size_t)w * h * 4);
            }
            finn_pointer_draw(&ptr, fbs[next].map, w, h);

            g_flip_done = 0;
            if (drmModePageFlip(fd, crtc_id, fbs[next].id, DRM_MODE_PAGE_FLIP_EVENT, NULL) == 0)
                cur = next;
            else
                g_flip_done = 1;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i] >= 0) close(clients[i]);
    if (ctl >= 0) { close(ctl); unlink(FINN_COMP_SOCK); }
    finn_pointer_close(&ptr);
    finncontrol_free();
    finndock_free();
    finnclock_free();
    finnmenu_free();
    finnblur_free();
    fprintf(stderr, "finn-comp: shutting down\n");

    if (gpu) {
        /* let the last flip land before the frame it went to is freed */
        wait_flip(fd);
        finngpu_release(gpu);
    }
    drmModeSetCrtc(fd, crtc_id, 0, 0, 0, NULL, 0, NULL);
    if (gpu) finngpu_close(gpu);
    close(fd);
    free(base);
    free(base_prev);
    free(wall);
    return 0;
}
