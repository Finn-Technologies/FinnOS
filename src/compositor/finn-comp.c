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
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <dirent.h>
#include <pwd.h>
#include <linux/input.h>
#include <linux/vt.h>
#include <wayland-server-protocol.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <webp/decode.h>

#include "finndock.h"
#include "finncontrol.h"
#include "finnclock.h"
#include "finnmenu.h"
#include "finnwindow.h"
#include "finnsettingsapp.h"
#include "finnfilesapp.h"
#include "finnsetup.h"
#include "finnboot.h"
#include "finnnotify.h"
#include "finndraw.h"
#include "finnblur.h"
/* TEMP timing */
#include "finnanim.h"
#include "finnmotion.h"
#include "finnstate.h"
#include "finnaudio.h"
#include "finnpointer.h"
#include "finnkbd.h"
#include "finnconn.h"
#include "finngpu.h"
#include "finnapps.h"
#include "finntaskbar.h"
#include "finnproto.h"
#include "finnsock.h"
#include "finnwayland.h"

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
#define DOCK_SCALE   0.97f
#define PANEL_SCALE  0.96f

/*
 * The scrim behind the Start Menu. The desktop does not go away when the
 * menu opens, it stops being the thing you are looking at — so the menu is
 * the only thing in the room that is lit. Its strength and its timing are
 * the shell's (finnmotion.h), and the menu owns the track: the room dims
 * before the door opens and brightens before it shuts.
 */

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
            /* Figma Desktop frame (1:12) has a 5% solid black overlay over the wallpaper fill */
            r *= 0.95f;
            g *= 0.95f;
            b *= 0.95f;
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

/*
 * The display we own, kept where the recovery path can reach it. A page flip
 * is the only proof the screen followed us; when it stops landing, a full
 * modeset needs the CRTC, the connector and the mode that were chosen at
 * start-up.
 */
static int      g_drm_fd = -1;
static uint32_t g_crtc_id = 0;
static uint32_t g_conn_id = 0;
static drmModeModeInfo g_mode;

/* How long a flip may take to land, and how many in a row may fail before
 * the screen is declared lost. The bound is deliberately small: a compositor
 * that cannot change the screen is not running, and letting systemd restart
 * it is better than pretending it is alive. */
#define FLIP_TIMEOUT_MS 100
#define FLIP_FAIL_LIMIT 5
static int g_flip_fail = 0;
static uint32_t g_last_fb = 0;

/* Returns 0 when the queued flip landed, -1 when it timed out or DRM
 * failed. Errno is set so the caller can say why. */
static int wait_flip(int fd) {
    while (!g_flip_done && !g_stop) {
        drmEventContext ev = {0};
        ev.version = DRM_EVENT_CONTEXT_VERSION;
        ev.page_flip_handler = flip_handler;
        struct pollfd pfd = { fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, FLIP_TIMEOUT_MS);
        if (pr == 0) { errno = ETIMEDOUT; return -1; }
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (drmHandleEvent(fd, &ev) != 0) return -1;
    }
    return g_flip_done ? 0 : -1;
}

/*
 * Put a frame on the screen and prove it got there.
 *
 * A failed flip used to be logged and forgotten: the process stayed alive
 * with the last image frozen, input went nowhere and systemd saw a healthy
 * service. Here a failure tries the full modeset that re-programs the CRTC,
 * and a run of failures is fatal on purpose. Only a frame that landed may
 * release the buffer it replaced — freeing a buffer the display is still
 * showing is how a recoverable hiccup becomes a frozen screen.
 *
 * Returns 0 when the frame is on the screen, -1 when it is not.
 */
static int display_flip(uint32_t fbid) {
    if (g_drm_fd < 0 || !fbid) { errno = EINVAL; g_flip_fail++; return -1; }

    g_flip_done = 0;
    if (drmModePageFlip(g_drm_fd, g_crtc_id, fbid, DRM_MODE_PAGE_FLIP_EVENT, NULL) == 0) {
        if (wait_flip(g_drm_fd) == 0) {
            g_flip_fail = 0;
            g_last_fb = fbid;
            return 0;
        }
        fprintf(stderr, "finn-comp: page flip did not land: %s\n",
                strerror(errno ? errno : ETIMEDOUT));
    } else {
        g_flip_done = 1;
        fprintf(stderr, "finn-comp: page flip failed: %s\n", strerror(errno));
    }

    if (drmModeSetCrtc(g_drm_fd, g_crtc_id, fbid, 0, 0,
                       &g_conn_id, 1, &g_mode) == 0) {
        fprintf(stderr, "finn-comp: display recovered with a full modeset\n");
        g_flip_fail = 0;
        g_last_fb = fbid;
        return 0;
    }
    fprintf(stderr, "finn-comp: modeset recovery failed: %s\n", strerror(errno));

    g_flip_fail++;
    return -1;
}

static int display_lost(void) { return g_flip_fail >= FLIP_FAIL_LIMIT; }

/* ---------------- service readiness and watchdog ----------------
 *
 * systemd's notify protocol is one datagram to NOTIFY_SOCKET; pulling in
 * libsystemd for it would be a dependency for twenty lines. The watchdog is
 * the point: a compositor that is alive but no longer painting must be
 * restarted, and a heartbeat is the only thing that proves it is still
 * turning.
 */
static int g_notify_fd = -1;

static void notify_open(void) {
    const char *path = getenv("NOTIFY_SOCKET");
    if (!path || !*path) return;

    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    if (path[0] == '@') {
        /* an abstract socket: the leading NUL is part of the address */
        if (strlen(path) >= sizeof a.sun_path - 1) return;
        a.sun_path[0] = 0;
        memcpy(a.sun_path + 1, path + 1, strlen(path) - 1);
    } else {
        if (strlen(path) >= sizeof a.sun_path) return;
        memcpy(a.sun_path, path, strlen(path) + 1);
    }

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        fprintf(stderr, "finn-comp: cannot reach systemd at %s: %s\n",
                path, strerror(errno));
        close(fd);
        return;
    }
    g_notify_fd = fd;
}

static void notify_say(const char *state) {
    if (g_notify_fd < 0) return;
    (void)send(g_notify_fd, state, strlen(state), MSG_NOSIGNAL);
}

static void notify_close(void) {
    if (g_notify_fd >= 0) { close(g_notify_fd); g_notify_fd = -1; }
}

/* ---------------- the virtual console ----------------
 *
 * The compositor owns tty1, and the rescue console is tty2. Ctrl+Alt+F1..F6
 * is therefore the way out of a session that is drawing but not listening,
 * and it is also the way back: the reader below still sees the key events
 * while another VT is active, so the same chord returns to the session.
 *
 * DRM master is dropped the moment the console stops being ours and taken
 * back when it becomes ours again. Flips are not attempted in between — a
 * flip without master would count as a display failure and get the compositor
 * restarted for doing what it was told.
 */
static int g_vt_fd = -1;
static int g_vt = 1;
static int g_vt_active = 1;

static void vt_open(void) {
    g_vt_fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (g_vt_fd < 0) {
        fprintf(stderr, "finn-comp: no /dev/tty0, VT switching disabled\n");
        return;
    }
    struct vt_stat vts;
    if (ioctl(g_vt_fd, VT_GETSTATE, &vts) == 0) g_vt = vts.v_active;
}

static void vt_switch(int n) {
    if (g_vt_fd < 0 || n < 1 || n > 63) return;
    if (ioctl(g_vt_fd, VT_ACTIVATE, n) != 0)
        fprintf(stderr, "finn-comp: cannot activate VT %d: %s\n", n, strerror(errno));
}

/* 1 when this process's VT is the one on the screen. No tty0 means there is
 * no console to switch away from, so the answer is always yes. */
static int vt_is_active(void) {
    if (g_vt_fd < 0) return 1;
    struct vt_stat vts;
    if (ioctl(g_vt_fd, VT_GETSTATE, &vts) != 0) return 1;
    return vts.v_active == g_vt;
}

/*
 * Follow the console. Returns 1 when the session has just come back, which
 * the caller treats as damage: the screen was someone else's and has to be
 * painted again, and the buffers it left behind may no longer be what the
 * CRTC is showing.
 */
static int vt_sync(int drm_fd, finn_pointer *ptr, finn_kbd *kbd) {
    int active = vt_is_active();
    if (active == g_vt_active) return 0;

    if (active) {
        if (drm_fd >= 0 && drmSetMaster(drm_fd) != 0)
            fprintf(stderr, "finn-comp: could not take DRM master back: %s\n",
                    strerror(errno));
        g_flip_fail = 0;
        if (drm_fd >= 0 && g_last_fb)
            (void)drmModeSetCrtc(drm_fd, g_crtc_id, g_last_fb, 0, 0,
                                 &g_conn_id, 1, &g_mode);
        finn_pointer_set_grab(ptr, 1);
        finn_kbd_set_grab(kbd, 1);
        g_vt_active = 1;
        fprintf(stderr, "finn-comp: console returned\n");
        return 1;
    }

    finn_pointer_set_grab(ptr, 0);
    finn_kbd_set_grab(kbd, 0);
    if (drm_fd >= 0) (void)drmDropMaster(drm_fd);
    g_vt_active = 0;
    fprintf(stderr, "finn-comp: console released\n");
    return 0;
}

/* ---------------- control plane provider ----------------
 *
 * finn-comp owns the scanout buffer and the pointer, so it is the provider
 * for display and input verbs. It speaks the line protocol from finnproto.h
 * over /run/finn/comp.sock; finnd sits in front of it as the control plane.
 *
 * The socket is root-only (0600) and every accepted peer is checked with
 * SO_PEERCRED: the file mode is not a substitute for asking, and a session
 * service that had the descriptor passed to it would otherwise inherit the
 * authority without the audit trail that finnd keeps.
 */

typedef struct {
    finn_pointer *ptr;
    finn_kbd     *kbd;
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

typedef struct {
    FinnWaylandSurface *surface;
    int window_id;
    int sent_width, sent_height;
    int sent_active, sent_maximized;
} finn_wayland_window;

typedef struct {
    FinnWayland *server;
    ctl_ctx *control;
    finn_wayland_window windows[FINNWINDOW_MAX];
    int default_width, default_height;
    int pointer_window;
    unsigned pointer_buttons;
    int keyboard_window;
    FinnWaylandSurface *pointer_surface;
} finn_wayland_ctx;

static uint32_t wayland_time_msec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

static finn_wayland_window *wayland_window_for_surface(
        finn_wayland_ctx *ctx, FinnWaylandSurface *surface) {
    if (!ctx || !surface) return NULL;
    for (int i = 0; i < FINNWINDOW_MAX; i++)
        if (ctx->windows[i].surface == surface) return &ctx->windows[i];
    return NULL;
}

static finn_wayland_window *wayland_window_for_id(finn_wayland_ctx *ctx,
                                                   int id) {
    if (!ctx || id <= 0) return NULL;
    for (int i = 0; i < FINNWINDOW_MAX; i++)
        if (ctx->windows[i].surface && ctx->windows[i].window_id == id)
            return &ctx->windows[i];
    return NULL;
}

static FinnWaylandSurface *wayland_surface_for_id(finn_wayland_ctx *ctx,
                                                   int id) {
    finn_wayland_window *window = wayland_window_for_id(ctx, id);
    return window ? window->surface : NULL;
}

static void wayland_surface_created(void *userdata,
                                    FinnWaylandSurface *surface) {
    finn_wayland_ctx *ctx = userdata;
    finn_wayland_window *slot = NULL;
    const char *title = finn_wayland_surface_title(surface);
    for (int i = 0; i < FINNWINDOW_MAX; i++)
        if (!ctx->windows[i].surface) { slot = &ctx->windows[i]; break; }
    if (!slot) {
        fprintf(stderr, "finn-comp: Wayland surface limit reached\n");
        finn_wayland_toplevel_close(surface);
        return;
    }
    int id = finnwindow_external_open(NULL, title, ctx->default_width,
                                      ctx->default_height);
    if (id <= 0) {
        fprintf(stderr, "finn-comp: could not create Wayland window (%d)\n", id);
        finn_wayland_toplevel_close(surface);
        return;
    }
    memset(slot, 0, sizeof *slot);
    slot->surface = surface;
    slot->window_id = id;
    slot->sent_width = -1;
    slot->sent_height = -1;
    slot->sent_active = -1;
    slot->sent_maximized = -1;
    ctx->control->dirty = 1;
}

static void wayland_surface_destroyed(void *userdata,
                                     FinnWaylandSurface *surface) {
    finn_wayland_ctx *ctx = userdata;
    finn_wayland_window *window = wayland_window_for_surface(ctx, surface);
    if (!window) return;
    int id = window->window_id;
    if (ctx->pointer_window == id) {
        ctx->pointer_window = 0;
        ctx->pointer_buttons = 0;
        ctx->pointer_surface = NULL;
    }
    if (ctx->keyboard_window == id) ctx->keyboard_window = 0;
    memset(window, 0, sizeof *window);
    (void)finnwindow_external_destroy(id);
    ctx->control->dirty = 1;
}

static void wayland_title_changed(void *userdata,
                                  FinnWaylandSurface *surface,
                                  const char *title) {
    finn_wayland_ctx *ctx = userdata;
    finn_wayland_window *window = wayland_window_for_surface(ctx, surface);
    if (window)
        (void)finnwindow_external_title(window->window_id, title);
    if (ctx->control) ctx->control->dirty = 1;
}

static void wayland_surface_committed(void *userdata,
                                      FinnWaylandSurface *surface) {
    finn_wayland_ctx *ctx = userdata;
    finn_wayland_window *window = wayland_window_for_surface(ctx, surface);
    int32_t width = 0, height = 0, stride = 0;
    FinnWaylandFormat format = FINN_WAYLAND_ARGB8888;
    const uint32_t *pixels = finn_wayland_surface_pixels(
        surface, &width, &height, &stride, &format);
    if (window && !pixels && width == 0 && height == 0) {
        (void)finnwindow_external_commit(window->window_id, NULL, 0, 0, 0, 0);
    } else if (window && pixels && width > 0 && height > 0) {
        if (finnwindow_external_commit(window->window_id, pixels, width, height,
                                       stride,
                                       format == FINN_WAYLAND_XRGB8888) == 0) {
            window->sent_width = width;
            window->sent_height = height;
        } else {
            fprintf(stderr, "finn-comp: rejected Wayland SHM surface %dx%d\n",
                    width, height);
        }
    }
    if (ctx->control) ctx->control->dirty = 1;
}

static void wayland_window_close_request(int id, void *userdata) {
    finn_wayland_ctx *ctx = userdata;
    FinnWaylandSurface *surface = wayland_surface_for_id(ctx, id);
    if (surface) finn_wayland_toplevel_close(surface);
}

static void wayland_frame_done_all(finn_wayland_ctx *ctx,
                                   uint32_t time_msec) {
    if (!ctx) return;
    for (int i = 0; i < FINNWINDOW_MAX; i++)
        if (ctx->windows[i].surface)
            finn_wayland_frame_done(ctx->windows[i].surface, time_msec);
}

static int wayland_dispatch_now(finn_wayland_ctx *ctx) {
    if (!ctx || !ctx->server) return 0;
    if (finn_wayland_dispatch(ctx->server, 0) == 0) return 0;
    fprintf(stderr, "finn-comp: Wayland dispatch failed: %s\n", strerror(errno));
    finn_wayland_destroy(ctx->server);
    ctx->server = NULL;
    return -1;
}

static void wayland_sync_focus(finn_wayland_ctx *ctx) {
    if (!ctx || !ctx->server) return;
    int id = finnwindow_external_focused();
    FinnWaylandSurface *surface = wayland_surface_for_id(ctx, id);
    if (id != ctx->keyboard_window) {
        finn_wayland_keyboard_focus(ctx->server, surface);
        ctx->keyboard_window = id;
    }
    for (int i = 0; i < FINNWINDOW_MAX; i++) {
        finn_wayland_window *window = &ctx->windows[i];
        if (!window->surface) continue;
        int width = 0, height = 0;
        if (finnwindow_external_get_size(window->window_id, &width, &height) != 0)
            continue;
        int active = window->window_id == id;
        int x, y, ww, wh;
        const char *state = NULL;
        int maximized = finnwindow_get(window->window_id, NULL, &x, &y,
                                        &ww, &wh, &state) == 0 &&
                        state && !strcmp(state, "maximized");
        if (width == window->sent_width && height == window->sent_height &&
            active == window->sent_active &&
            maximized == window->sent_maximized)
            continue;
        uint32_t states = (active ? FINN_WAYLAND_CONFIGURE_ACTIVATED : 0) |
                          (maximized ? FINN_WAYLAND_CONFIGURE_MAXIMIZED : 0);
        if (finn_wayland_configure(window->surface, width, height, states,
                                   NULL) == 0) {
            window->sent_width = width;
            window->sent_height = height;
            window->sent_active = active;
            window->sent_maximized = maximized;
        }
    }
}

static void wayland_sync_pointer(finn_wayland_ctx *ctx, int px, int py,
                                 uint32_t time_msec) {
    if (!ctx || !ctx->server) return;
    if (ctx->pointer_window) {
        int sx, sy;
        finn_wayland_window *window = wayland_window_for_id(
            ctx, ctx->pointer_window);
        if (!window || !finnwindow_external_coordinates(ctx->pointer_window,
                                                        px, py, &sx, &sy)) {
            ctx->pointer_window = 0;
            ctx->pointer_buttons = 0;
            ctx->pointer_surface = NULL;
        } else {
            finn_wayland_pointer_motion(ctx->server, sx, sy, time_msec);
            return;
        }
    }
    int blocked = finnmenu_is_open() || finncontrol_is_open() ||
                  finnclock_is_open() || finnsetup_is_open();
    int dock = finndock_hit(px, py);
    int id = 0, sx = 0, sy = 0;
    if (!blocked && dock == FINNDOCK_HIT_NONE &&
        finnwindow_external_hit(px, py, &id, &sx, &sy)) {
        FinnWaylandSurface *surface = wayland_surface_for_id(ctx, id);
        if (surface) {
            if (ctx->pointer_surface != surface) {
                finn_wayland_pointer_focus(ctx->server, surface, sx, sy,
                                           time_msec);
                ctx->pointer_surface = surface;
            } else {
                finn_wayland_pointer_motion(ctx->server, sx, sy, time_msec);
            }
            return;
        }
    }
    if (ctx->pointer_surface) {
        finn_wayland_pointer_focus(ctx->server, NULL, 0, 0, time_msec);
        ctx->pointer_surface = NULL;
    }
}

static void wayland_send_button(finn_wayland_ctx *ctx, uint32_t button,
                                uint32_t state, unsigned mask,
                                int px, int py, uint32_t time_msec) {
    if (!ctx || !ctx->server) return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (!ctx->pointer_window) {
            int sx = 0, sy = 0, hit_id = 0;
            if (!finnwindow_external_hit(px, py, &hit_id, &sx, &sy)) return;
            int id = hit_id;
            ctx->pointer_window = id;
            ctx->pointer_surface = wayland_surface_for_id(ctx, id);
            if (!ctx->pointer_surface) {
                ctx->pointer_window = 0;
                return;
            }
            finn_wayland_pointer_focus(ctx->server, ctx->pointer_surface,
                                       sx, sy, time_msec);
            finn_wayland_keyboard_focus(ctx->server, ctx->pointer_surface);
            ctx->keyboard_window = id;
        } else {
            int sx, sy;
            if (finnwindow_external_coordinates(ctx->pointer_window, px, py,
                                                &sx, &sy))
                finn_wayland_pointer_motion(ctx->server, sx, sy, time_msec);
        }
        finn_wayland_pointer_button(ctx->server, button,
                                    WL_POINTER_BUTTON_STATE_PRESSED,
                                    time_msec);
        ctx->pointer_buttons |= mask;
    } else if (ctx->pointer_buttons & mask) {
        finn_wayland_pointer_button(ctx->server, button,
                                    WL_POINTER_BUTTON_STATE_RELEASED,
                                    time_msec);
        ctx->pointer_buttons &= ~mask;
        if (!ctx->pointer_buttons) ctx->pointer_window = 0;
    }
}

static void compose_base(uint32_t *base, const uint32_t *wall, int w, int h,
                         float scale, const finn_state *st, float theme_t);

/* ---------------- taskbar ----------------
 *
 * The dock draws tiles; it does not know what a window is or where pins are
 * kept. This is the join: the pin list from the settings store, the open
 * windows from the window manager, and the catalogue for the icon and the
 * label. Pinned entries come first in pin order; an app that is running but
 * not pinned follows in the order its first window appeared. A running app
 * is always shown, even if its launcher is missing — the window is proof it
 * is here.
 */

static int pin_has(char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int n,
                   const char *id) {
    for (int i = 0; i < n; i++)
        if (!strcmp(ids[i], id)) return 1;
    return 0;
}

static int task_find(const finndock_app *apps, int n, const char *id) {
    for (int i = 0; i < n; i++)
        if (!strcmp(apps[i].id, id)) return i;
    return -1;
}

static int app_launchable(const finn_app *a) {
    return a && a->command[0] && access(a->command, X_OK) == 0;
}

/*
 * The pin list is a file, and the taskbar is rebuilt whenever anything on
 * screen moves. Read it when the pins change, not once per frame; -1 means
 * "not read yet". A store that cannot be read leaves the last answer in
 * place — dropping every tile because of one failed read would hide apps
 * that are still running.
 */
static char g_pins[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX];
static int  g_n_pins = -1;

static void taskbar_pins_reload(void) {
    int n = finntaskbar_pins(g_pins, FINNTASKBAR_MAX);
    g_n_pins = n < 0 ? 0 : n;
}

/* The list the dock is currently drawing. The pointer acts on the tile it
 * sees, so the compositor has to keep the same list the dock got. */
static finndock_app g_task_apps[FINNDOCK_APPS_MAX];
static int          g_n_task_apps = 0;

static void taskbar_sync(void) {
    if (finn_apps_count() <= 0) {
        const char *reg = getenv("FINN_APPS");
        finn_apps_load(reg && reg[0] ? reg : FINN_APPS_CATALOGUE, NULL);
    }
    if (g_n_pins < 0) taskbar_pins_reload();

    finnwindow_brief briefs[FINNWINDOW_MAX];
    int nb = finnwindow_briefs(briefs, FINNWINDOW_MAX);

    finndock_app apps[FINNDOCK_APPS_MAX];
    int n = 0;
    memset(apps, 0, sizeof apps);

    for (int i = 0; i < g_n_pins && n < FINNDOCK_APPS_MAX; i++) {
        if (task_find(apps, n, g_pins[i]) >= 0) continue;
        const finn_app *a = finn_apps_find(g_pins[i]);
        /* A pin outlives the package: it is kept in the store and comes back
         * when the app does, but a pin for something the machine does not
         * have is not drawn as a live tile. */
        if (!app_launchable(a) || !a->installed) continue;
        finndock_app *t = &apps[n++];
        snprintf(t->id, sizeof t->id, "%s", a->id);
        snprintf(t->name, sizeof t->name, "%s", a->name);
        snprintf(t->icon, sizeof t->icon, "%s", a->icon);
        t->pinned = 1;
    }

    for (int i = 0; i < nb && n < FINNDOCK_APPS_MAX; i++) {
        const finnwindow_brief *b = &briefs[i];
        char id[32];
        if (b->app_id[0])
            snprintf(id, sizeof id, "%s", b->app_id);
        else
            snprintf(id, sizeof id, "@%d", b->id);

        int at = task_find(apps, n, id);
        if (at < 0) {
            finndock_app *t = &apps[n];
            at = n++;
            memset(t, 0, sizeof *t);
            snprintf(t->id, sizeof t->id, "%s", id);
            if (b->app_id[0]) {
                const finn_app *a = finn_apps_find(b->app_id);
                snprintf(t->name, sizeof t->name, "%s",
                         a ? a->name : b->title);
                snprintf(t->icon, sizeof t->icon, "%s", a ? a->icon : "");
                t->pinned = pin_has(g_pins, g_n_pins, b->app_id);
            } else {
                snprintf(t->name, sizeof t->name, "%s",
                         b->title[0] ? b->title : "Window");
            }
        }

        finndock_app *t = &apps[at];
        t->windows++;
        if (b->focused) t->focused = 1;
        if (!b->minimized) t->minimized = 0;
        else if (t->windows == 1) t->minimized = 1;
    }

    finndock_set_apps(apps, n);
    memcpy(g_task_apps, apps, sizeof apps);
    g_n_task_apps = n;
}

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
                 "dock_anim=%d dock=%d,%d,%d,%d pointer=%d,%d buttons=%d mods=%u\n",
                 finncontrol_is_open(), finncontrol_openness(), finncontrol_running(),
                 finnclock_is_open(), finnclock_openness(),
                 finnmenu_is_open(), finnmenu_openness(),
                 finndock_running(),
                 dx, dy, dw, dh, cx->ptr->x, cx->ptr->y, cx->ptr->buttons,
                 cx->kbd ? finn_kbd_mods(cx->kbd) : 0u);
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
        int iv, n = 0;
        if (kv_int(rest, "battery", &iv))  { write_state_int("battery", iv);  n++; }
        if (kv_int(rest, "charging", &iv)) { write_state_int("charging", iv); n++; }
        if (kv_int(rest, "wifi", &iv))     { write_state_int("wifi", iv);     n++; }
        if (kv_int(rest, "volume", &iv))   { write_state_int("volume", iv);   n++; }
        if (!n) { finn_write_str(fd, "ERR no recognised keys\n"); return; }
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
        int v, any = 0, failed = 0;
        if (kv_int(rest, "wifi", &v))      { any = 1; if (finn_state_set_wifi(v) != 0) failed = 1; }
        if (kv_int(rest, "bluetooth", &v)) { any = 1; if (finn_state_set_bluetooth(v) != 0) failed = 1; }
        if (!any)         { finn_write_str(fd, "ERR name wifi= or bluetooth=\n"); return; }
        /* The radio's answer is the answer: a machine with no adapter, or a
         * rfkill write the kernel refused, is not a success. */
        if (failed)       { finn_write_str(fd, "ERR hardware unavailable\n"); return; }
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "appearance.set")) {
        int v;
        if (!kv_int(rest, "dark", &v)) {
            finn_write_str(fd, "ERR appearance.set needs dark=0|1\n");
            return;
        }
        if (finn_state_set_dark(v) != 0) {
            finn_write_str(fd, "ERR could not save the appearance\n");
            return;
        }
        cx->dirty = 1;
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
        /* The wheel is queued, not delivered here: the shell loop takes it
         * on the next frame and gives it to the window under the pointer,
         * exactly as a hardware notch would be. */
        int clicks = 1;
        if (!kv_int(rest, "clicks", &clicks)) kv_int(rest, "dy", &clicks);
        if (clicks == 0) {
            finn_write_str(fd, "ERR pointer.scroll needs a nonzero clicks=\n");
            return;
        }
        cx->ptr->wheel += clicks;
        finn_write_str(fd, "OK\n");
        return;
    }

    /* ---- windows (Figma 12:2) ---- */
    if (!strcmp(verb, "window.list")) {
        char b[1024];
        if (finnwindow_list(b, sizeof b) == 0) finn_write_str(fd, b);
        else finn_write_str(fd, "ERR window.list failed\n");
        return;
    }
    if (!strcmp(verb, "window.get")) {
        int id = 0, wid = 0, x = 0, y = 0, w = 0, h = 0;
        if (!kv_int(rest, "id", &id) && *rest) id = atoi(rest);
        const char *state = "";
        if (finnwindow_get(id, &wid, &x, &y, &w, &h, &state) != 0) {
            finn_write_str(fd, "ERR no such window\n");
            return;
        }
        char b[160];
        snprintf(b, sizeof b, "OK id=%d state=%s x=%d y=%d w=%d h=%d\n",
                 wid, state, x, y, w, h);
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "window.open")) {
        int id = finnwindow_open(rest);
        if (id == FINNWINDOW_ERR_LIMIT) {
            char b[96];
            snprintf(b, sizeof b, "ERR window limit reached (%d)\n", FINNWINDOW_MAX);
            finn_write_str(fd, b);
            return;
        }
        if (id < 0) { finn_write_str(fd, "ERR cannot open window\n"); return; }
        cx->dirty = 1;
        char b[64];
        snprintf(b, sizeof b, "OK id=%d\n", id);
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "window.close") || !strcmp(verb, "window.focus") ||
        !strcmp(verb, "window.minimize") || !strcmp(verb, "window.maximize") ||
        !strcmp(verb, "window.restore")) {
        int id = 0;
        if (!kv_int(rest, "id", &id) && *rest) id = atoi(rest);
        int rc = 0;
        if (!strcmp(verb, "window.close"))         rc = finnwindow_close(id);
        else if (!strcmp(verb, "window.focus"))    rc = finnwindow_focus(id);
        else if (!strcmp(verb, "window.minimize")) rc = finnwindow_minimize(id);
        else if (!strcmp(verb, "window.maximize")) rc = finnwindow_maximize(id);
        else                                       rc = finnwindow_restore(id);
        if (rc != 0) { finn_write_str(fd, "ERR no such window\n"); return; }
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "window.resize")) {
        int id = 0, w = 0, h = 0;
        if (!kv_int(rest, "id", &id)) id = atoi(rest);
        if (!kv_int(rest, "w", &w) || !kv_int(rest, "h", &h)) {
            finn_write_str(fd, "ERR window.resize needs id= w= h=\n");
            return;
        }
        if (finnwindow_resize(id, w, h) != 0) {
            finn_write_str(fd, "ERR no such window\n");
            return;
        }
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "window.snap")) {
        int id = 0;
        char edge[24] = "";
        if (!kv_int(rest, "id", &id)) id = atoi(rest);
        if (!kv_str(rest, "edge", edge, sizeof edge)) {
            finn_write_str(fd, "ERR window.snap needs id= edge=left|right|top|top-left|top-right|bottom-left|bottom-right|restore\n");
            return;
        }
        int e = -1;
        if (!strcmp(edge, "left"))              e = FINNWINDOW_SNAP_LEFT;
        else if (!strcmp(edge, "right"))        e = FINNWINDOW_SNAP_RIGHT;
        else if (!strcmp(edge, "top"))          e = FINNWINDOW_SNAP_TOP;
        else if (!strcmp(edge, "top-left"))     e = FINNWINDOW_SNAP_TOPLEFT;
        else if (!strcmp(edge, "top-right"))    e = FINNWINDOW_SNAP_TOPRIGHT;
        else if (!strcmp(edge, "bottom-left"))  e = FINNWINDOW_SNAP_BOTTOMLEFT;
        else if (!strcmp(edge, "bottom-right")) e = FINNWINDOW_SNAP_BOTTOMRIGHT;
        else if (!strcmp(edge, "restore"))      e = FINNWINDOW_SNAP_RESTORE;
        if (e < 0) {
            finn_write_str(fd, "ERR unknown edge\n");
            return;
        }
        if (finnwindow_snap(id, e) != 0) {
            finn_write_str(fd, "ERR no such window\n");
            return;
        }
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "window.move")) {
        int id = 0, x = 0, y = 0;
        if (!kv_int(rest, "id", &id)) id = atoi(rest);
        if (!kv_int(rest, "x", &x) || !kv_int(rest, "y", &y)) {
            finn_write_str(fd, "ERR window.move needs id= x= y=\n");
            return;
        }
        if (finnwindow_move(id, x, y) != 0) {
            finn_write_str(fd, "ERR no such window\n");
            return;
        }
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }

    /* ---- settings app (Figma 14:330 / 14:289) ---- */
    if (!strcmp(verb, "settings.page")) {
        char page[32] = "";
        if (!kv_str(rest, "name", page, sizeof page)) {
            while (*rest == ' ') rest++;
            snprintf(page, sizeof page, "%s", rest);
        }
        if (!page[0]) {
            finn_write_str(fd, "ERR settings.page needs name=<page>\n");
            return;
        }
        if (finnsettingsapp_set_page(page) != 0) {
            finn_write_str(fd, "ERR no such page\n");
            return;
        }
        finnwindow_open("settings");
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "settings.get")) {
        char b[64];
        snprintf(b, sizeof b, "OK page=%s\n", finnsettingsapp_get_page());
        finn_write_str(fd, b);
        return;
    }

    /* ---- Finn Files ---- */
    if (!strcmp(verb, "files.open")) {
        const char *p = rest;
        while (*p == ' ') p++;
        if (!strncmp(p, "path=", 5)) p += 5;
        if (!*p) {
            finn_write_str(fd, "ERR files.open needs a path\n");
            return;
        }
        if (finnfilesapp_open_path(p) != 0) {
            char b[320];
            snprintf(b, sizeof b, "ERR %s\n",
                     finnfilesapp_error()[0] ? finnfilesapp_error() : "could not open that folder");
            finn_write_str(fd, b);
            return;
        }
        if (finnwindow_open("files") == FINNWINDOW_ERR_LIMIT) {
            finn_write_str(fd, "ERR window limit reached\n");
            return;
        }
        cx->dirty = 1;
        char b[PATH_MAX + 32];
        snprintf(b, sizeof b, "OK path=%s\n", finnfilesapp_path());
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "files.get")) {
        char b[PATH_MAX + 160];
        snprintf(b, sizeof b, "OK path=%s selected=%s\n",
                 finnfilesapp_path()[0] ? finnfilesapp_path() : "-",
                 finnfilesapp_selection()[0] ? finnfilesapp_selection() : "-");
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "files.select")) {
        const char *p = rest;
        while (*p == ' ') p++;
        if (!strncmp(p, "name=", 5)) p += 5;
        if (!*p) {
            finn_write_str(fd, "ERR files.select needs a name\n");
            return;
        }
        if (finnfilesapp_select(p) != 0) {
            finn_write_str(fd, "ERR no such item in this folder\n");
            return;
        }
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }

    /* ---- first-boot setup (Figma 2:14 & 7:10) ---- */
    if (!strcmp(verb, "setup.get")) {
        char b[160];
        snprintf(b, sizeof b, "OK active=%d done=%d lang=%s\n",
                 finnsetup_is_open() ? 1 : 0,
                 finnsetup_is_completed() ? 1 : 0,
                 finnsetup_language());
        finn_write_str(fd, b);
        return;
    }
    if (!strcmp(verb, "setup.show")) {
        finnsetup_set_open(1);
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }
    if (!strcmp(verb, "setup.complete")) {
        /* Setup reports the marker write, not the intention. A failure keeps
         * the screen open and says why; a machine that forgets its setup on
         * the next boot has not completed it. */
        if (finnsetup_complete() != 0) {
            finn_write_str(fd, "ERR could not save the setup\n");
            return;
        }
        cx->dirty = 1;
        finn_write_str(fd, "OK\n");
        return;
    }

    /* ---- keyboard injection ----
     *
     * The reader is real (finnkbd); these verbs are how a test or the control
     * plane types into the same event path a key takes. There is no client
     * text field yet, so an injected key can drive the shell and setup but
     * cannot deliver a character to an application — that needs the surface
     * protocol and is not pretended here.
     */
    if (!strcmp(verb, "key.press") || !strcmp(verb, "key.release") ||
        !strcmp(verb, "key.tap")) {
        char name[32] = "";
        if (!kv_str(rest, "code", name, sizeof name) || !name[0]) {
            finn_write_str(fd, "ERR key needs code=<name>\n");
            return;
        }
        int code = finn_kbd_code_for_name(name);
        if (code < 0) {
            char b[96];
            snprintf(b, sizeof b, "ERR unknown key '%s'\n", name);
            finn_write_str(fd, b);
            return;
        }
        if (!cx->kbd) { finn_write_str(fd, "ERR no keyboard reader\n"); return; }

        int pressed = strcmp(verb, "key.release") != 0;
        if (finn_kbd_inject(cx->kbd, code, pressed) != 0) {
            finn_write_str(fd, "ERR cannot inject the key\n");
            return;
        }
        if (!strcmp(verb, "key.tap"))
            finn_kbd_inject(cx->kbd, code, 0);
        cx->dirty = 1;
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

/*
 * Frame timing, for looking at the shell without a profiler. It is off
 * unless /tmp/finn-timing exists — the file is the switch the old FRAME line
 * used — and checking it costs one access() a second rather than one a
 * frame. The summary is per second, because an average hides exactly the
 * single stall that makes a drag feel heavy; the max and the over-budget
 * count do not.
 *
 * A sample is build plus the wait for the flip to land: the frame the user
 * actually waited for, not only the CPU cost. The FRAME line still splits
 * the two, so a summary that sits at 16.7ms can be read as "on cadence"
 * rather than "the renderer is using all of it".
 */
#define TIMING_SAMPLES 240
#define TIMING_BUDGET_MS 16.7f

static float  g_timing_ms[TIMING_SAMPLES];
static int    g_timing_n = 0;
static int    g_timing_over = 0;
static int    g_timing_on = 0;
static double g_timing_last_check = 0.0;
static double g_timing_last_summary = 0.0;

static int timing_on(double now) {
    if (now - g_timing_last_check >= 1.0) {
        g_timing_last_check = now;
        int was = g_timing_on;
        g_timing_on = access("/tmp/finn-timing", F_OK) == 0;
        if (g_timing_on && !was) {
            /* A summary must describe the window that was just measured,
             * not samples left over from an earlier switch-on. */
            g_timing_n = 0;
            g_timing_over = 0;
            g_timing_last_summary = now;
        }
    }
    return g_timing_on;
}

static int timing_cmp(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void timing_summary(double now) {
    int n = g_timing_n;
    if (!n) { g_timing_last_summary = now; return; }

    float sorted[TIMING_SAMPLES];
    memcpy(sorted, g_timing_ms, (size_t)n * sizeof *sorted);
    qsort(sorted, (size_t)n, sizeof *sorted, timing_cmp);

    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += g_timing_ms[i];

    int i50 = (n * 50) / 100; if (i50 >= n) i50 = n - 1;
    int i95 = (n * 95) / 100; if (i95 >= n) i95 = n - 1;

    fprintf(stderr,
            "FRAME-SUMMARY n=%d mean=%.2f p50=%.2f p95=%.2f max=%.2f "
            "over=%d/%d budget=%.2fms\n",
            n, sum / (float)n, sorted[i50], sorted[i95], sorted[n - 1],
            g_timing_over, n, TIMING_BUDGET_MS);

    g_timing_n = 0;
    g_timing_over = 0;
    g_timing_last_summary = now;
}

static void timing_sample(double now, double build_ms, double flip_ms) {
    float ms = (float)(build_ms + flip_ms);
    if (g_timing_n < TIMING_SAMPLES) g_timing_ms[g_timing_n++] = ms;
    if (ms > TIMING_BUDGET_MS) g_timing_over++;
    if (g_timing_n >= TIMING_SAMPLES || now - g_timing_last_summary >= 1.0)
        timing_summary(now);
}

/*
 * The desktop under the chrome: the wallpaper with every open window on it,
 * in the places they are actually drawn. This is what a frosted surface
 * samples, so the glass over a window shows that window's colour rather than
 * the picture it hides.
 *
 * It lives here, in the compositor, because the compositor is the only part
 * of the system that knows where the windows currently are: the window
 * manager caches one layer and the transform is applied at composition time.
 */
static uint32_t *g_backdrop = NULL;
static int g_backdrop_w = 0, g_backdrop_h = 0;

/*
 * The backdrop only has to be rebuilt when what it is a picture of moves: the
 * window's layer revision, or where the transform puts it. Dock hovers, menu
 * fades and a cursor crossing a panel are chrome-only frames — rebuilding
 * four million pixel reads for them is what made the shell feel heavy.
 */
static unsigned g_scene_key = 0;
static int g_scene_valid = 0;
static int g_bd_had_win = 0;
static int g_bd_rect[4] = {0, 0, 0, 0};

/* The key build_chrome settled on this frame, and whether the backdrop it
 * built against is current. compose_base reads these instead of asking the
 * window manager a second time. */
static unsigned g_frame_scene = 0;
static int g_frame_scene_ok = 0;

/* Where the window layer lands this frame, clipped to the screen. Returns 0
 * when no window is showing; r is x, y, w, h. */
static int window_footprint(int w, int h, int r[4]) {
    int wx = 0, wy = 0, ww = 0, wh = 0;
    unsigned rev = 0;
    const uint32_t *wl = finnwindow_layer(&wx, &wy, &ww, &wh, &rev);
    if (!wl || ww <= 0 || wh <= 0) return 0;

    float x = (float)wx, y = (float)wy, rw = (float)ww, rh = (float)wh, a = 1.0f;
    finnwindow_transform(&x, &y, &rw, &rh, &a);
    if (a <= 0.004f || rw < 1.0f || rh < 1.0f) return 0;

    /* a cell of slack: a scaled blit can land a pixel outside the rect */
    int x0 = (int)floorf(x) - 2, y0 = (int)floorf(y) - 2;
    int x1 = (int)ceilf(x + rw) + 2, y1 = (int)ceilf(y + rh) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x1 <= x0 || y1 <= y0) return 0;
    r[0] = x0; r[1] = y0; r[2] = x1 - x0; r[3] = y1 - y0;
    return 1;
}

static uint32_t scene_key(int w, int h) {
    int wx = 0, wy = 0, ww = 0, wh = 0;
    unsigned rev = 0;
    const uint32_t *wl = finnwindow_layer(&wx, &wy, &ww, &wh, &rev);
    float x = (float)wx, y = (float)wy, rw = (float)ww, rh = (float)wh, a = 1.0f;
    if (wl) finnwindow_transform(&x, &y, &rw, &rh, &a);

    uint32_t k = 2166136261u;
#define MIX(v) do { k ^= (uint32_t)(v); k *= 16777619u; } while (0)
    MIX(w); MIX(h); MIX(wl ? 1u : 0u); MIX(rev);
    MIX((uint32_t)lroundf(x * 4.0f)); MIX((uint32_t)lroundf(y * 4.0f));
    MIX((uint32_t)lroundf(rw * 4.0f)); MIX((uint32_t)lroundf(rh * 4.0f));
    MIX((uint32_t)lroundf(a * 4000.0f));
#undef MIX
    return k ? k : 1u;
}

/*
 * Paint the desktop under the chrome: the wallpaper, with the window in the
 * place the transform currently puts it, over the rect (r) it occupies. Only
 * that rect is touched, so the two full-screen copies this used to make per
 * frame are gone.
 */
static void backdrop_paint(const uint32_t *wall, int w, int h, const int r[4]) {
    if (!wall || !g_backdrop) return;
    int rx = r[0], ry = r[1], rw = r[2], rh = r[3];
    for (int y = ry; y < ry + rh; y++)
        memcpy(g_backdrop + (size_t)y * w + rx,
               wall + (size_t)y * w + rx, (size_t)rw * 4);

    int wx = 0, wy = 0, ww = 0, wh = 0;
    unsigned rev = 0;
    const uint32_t *wl = finnwindow_layer(&wx, &wy, &ww, &wh, &rev);
    if (wl && ww > 0 && wh > 0) {
        float x = (float)wx, y = (float)wy, rw2 = (float)ww, rh2 = (float)wh, a = 1.0f;
        finnwindow_transform(&x, &y, &rw2, &rh2, &a);
        if (a > 0.004f)
            finn_blit_scaled(g_backdrop, w, h, wl, ww, wh, x, y, rw2, rh2, a);
    }
}

/*
 * Bring the blurred backdrop up to date, doing only the work the change
 * warrants: nothing when the scene is the same as last frame, the window's
 * own rect when the window moved or redrew, and everything on a new geometry.
 */
static void backdrop_update(const uint32_t *wall, int w, int h) {
    if (!wall || w <= 0 || h <= 0) return;

    int r[4];
    int has = window_footprint(w, h, r);

    if (g_backdrop_w != w || g_backdrop_h != h || !g_backdrop) {
        free(g_backdrop);
        g_backdrop = malloc((size_t)w * h * 4);
        g_backdrop_w = g_backdrop ? w : 0;
        g_backdrop_h = g_backdrop ? h : 0;
        g_scene_valid = 0;
    }
    if (!g_backdrop) return;

    /* the old footprint has to go back to the wallpaper, and the new one has
     * to be painted: one union rect covers both */
    if (!has && !g_bd_had_win) {
        /* nothing under the chrome but the wallpaper, and that is what the
         * live backdrop already holds */
        finnblur_reset();
        return;
    }
    int u[4];
    if (has) memcpy(u, r, sizeof u);
    else     memcpy(u, g_bd_rect, sizeof u);
    if (has && g_bd_had_win) {
        int x0 = u[0], y0 = u[1], x1 = u[0] + u[2], y1 = u[1] + u[3];
        if (g_bd_rect[0] < x0) x0 = g_bd_rect[0];
        if (g_bd_rect[1] < y0) y0 = g_bd_rect[1];
        if (g_bd_rect[0] + g_bd_rect[2] > x1) x1 = g_bd_rect[0] + g_bd_rect[2];
        if (g_bd_rect[1] + g_bd_rect[3] > y1) y1 = g_bd_rect[1] + g_bd_rect[3];
        u[0] = x0; u[1] = y0; u[2] = x1 - x0; u[3] = y1 - y0;
    }

    backdrop_paint(wall, w, h, u);
    finnblur_live_rect(g_backdrop, w, h, u[0], u[1], u[2], u[3]);

    if (has) memcpy(g_bd_rect, r, sizeof g_bd_rect);
    g_bd_had_win = has;
}

/* Build the chrome layers for this frame. Both backends want these first —
 * the software one blits them into a buffer, the accelerated one uploads them
 * as textures — so the shell is rasterised exactly once either way. */
static void build_chrome(int w, int h, float scale, const finn_state *st,
                         const uint32_t *wall) {
    finn_theme_set(st->dark);

    /*
     * The dock and the windows share one piece of geometry: the band the
     * dock occupies at the bottom. A maximized window ends where that band
     * begins, and a window that reaches the dock's resting edge asks the
     * dock to sink into the bottom corner and give the room back.
     *
     * The edge the question is asked against is the dock's *resting* top,
     * not its current one. The dock moving down must not move the line that
     * decided it should move, or the answer would flip back on the next
     * frame.
     */
    float dock_scale = scale * DOCK_SCALE;
    finnwindow_set_dock_clearance(finndock_height(dock_scale));
    finndock_set_sunk(finnwindow_overlaps_band(finndock_rest_top(h, dock_scale)));

    /*
     * The tray's well belongs to the Control Center, not to the dock: it is
     * the mark that ties the pill to the panel above it, so it is driven by
     * the panel's own openness and fades out with it.
     */
    finndock_set_well(finncontrol_openness());
    finndock_set_clock_well(finnclock_openness());
    finndock_set_launcher_well(finnmenu_openness());
    taskbar_sync();

    /*
     * The frosted surfaces blur what is behind them, and what is behind them
     * is the desktop, not only the wallpaper: a menu over a window frosts that
     * window. The backdrop is therefore a picture of the wallpaper plus the
     * windows, kept up to date by the compositor, which is the only part of
     * the system that knows where the windows are.
     *
     * Windows draw against it and bake themselves in as they go, so window two
     * frosts window one. Everything above them — dock, Control Center, menu —
     * frosts the desktop under its own rect.
     */
    int hide_chrome = finnsetup_is_open() || finnsetup_visible();
    int win_showing = !hide_chrome &&
                      (finnwindow_visible_count() > 0 || finnwindow_visible());

    /*
     * The backdrop has to be refreshed when the windows move, and only then:
     * a frame whose only change is the dock's hover state leaves it alone.
     * Rebuilding it for the chrome's own redraws was four million pixel reads
     * a frame, which is what made hovering the shell feel heavy.
     */
    unsigned key0 = scene_key(w, h);
    int scene_changed = !g_scene_valid || g_scene_key != key0;
    if (hide_chrome) {
        finnblur_reset();
        g_scene_valid = 0;
    } else if (scene_changed) {
        /* A window frosts what is *under* it, so the windows themselves have
         * to draw against the desktop before they are part of it. Without
         * this reset the glass would sample its own picture back out of the
         * backdrop and read as a pale ghost of itself. */
        finnblur_reset();
    }

    int dx, dy, dw, dh;
    finndock_geometry(&dx, &dy, &dw, &dh);

    if (win_showing)
        finnwindow_render(w, h, dock_scale, dx, dy, dw, dh, st);

    /*
     * The window can rebuild its own body in place — a keystroke, a list
     * that scrolled — after the pre-render key was taken. Ask again: the
     * desktop under the chrome is new this frame even though it was old
     * when the frame started, and a composition that reused the previous
     * desktop would show the old body through the new chrome.
     */
    unsigned key1 = scene_key(w, h);
    int backdrop_dirty = !hide_chrome && (scene_changed || key1 != g_scene_key);
    if (backdrop_dirty) {
        backdrop_update(wall, w, h);
        g_scene_key = key1;
        g_scene_valid = 1;
    }

    /* What the desktop under the chrome is, and whether it is current. The
     * software composition uses this to decide between rebuilding it and
     * putting the saved pixels back. */
    g_frame_scene = key1;
    g_frame_scene_ok = !hide_chrome && g_scene_valid;

    finndock_render(w, h, dock_scale, st);

    if (finncontrol_visible())
        finncontrol_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, st);

    if (finnclock_visible())
        finnclock_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, st);

    if (finnmenu_visible())
        finnmenu_render(w, h, scale * PANEL_SCALE, dx, dy, dw, dh, st);

    if (finnsetup_is_open() || finnsetup_visible())
        finnsetup_render(w, h, scale, st);
}

/*
 * The chrome is drawn on top of the desktop, and most frames change nothing
 * but the chrome: a dock wash, the minute on the clock, a panel hover. Those
 * frames used to copy the wallpaper over the whole base and blit the window
 * again just to erase the old chrome. Keep the desktop pixels under each
 * chrome surface instead; putting them back erases exactly the chrome that
 * was drawn, and the new chrome goes over the same desk.
 *
 * The menu is deliberately absent. Its shadow reaches far outside its layer
 * (a hundred-pixel blur at reference scale), so a layer-sized block would
 * not cover what the menu actually painted.
 */
enum { CH_OVERLAY, CH_DOCK, CH_CONTROL, CH_CLOCK, CH_COUNT };

static uint32_t *g_ch_px[CH_COUNT];
static size_t    g_ch_cap[CH_COUNT];
static int       g_ch_rect[CH_COUNT][4];

/* What the current base was composed against. */
static int      g_base_valid = 0;
static unsigned g_base_scene = 0;
static int      g_base_hide = 0;
static int      g_base_menu = 0;

/* The screen rect one chrome surface occupies this frame, with the margin a
 * scaled blit writes past its rect. Returns 0 when the surface is not
 * showing, which is not the same as a surface of zero size. */
static int chrome_slot_rect(int slot, int w, int h, int r[4]) {
    const uint32_t *layer = NULL;
    float x = 0, y = 0, rw = 0, rh = 0, a = 0;

    if (slot == CH_OVERLAY) {
        int lx, ly, lw, lh; unsigned rev;
        layer = finnwindow_overlay_layer(&lx, &ly, &lw, &lh, &rev);
        if (layer) finnwindow_overlay_transform(&x, &y, &rw, &rh, &a);
    } else if (slot == CH_DOCK) {
        int lx, ly, lw, lh; unsigned rev;
        layer = finndock_layer(&lx, &ly, &lw, &lh, &rev);
        x = (float)lx; y = (float)ly; rw = (float)lw; rh = (float)lh; a = 1.0f;
    } else if (slot == CH_CONTROL) {
        int lx, ly, lw, lh; unsigned rev;
        layer = finncontrol_layer(&lx, &ly, &lw, &lh, &rev);
        if (layer) finncontrol_transform(&x, &y, &rw, &rh, &a);
    } else {
        int lx, ly, lw, lh; unsigned rev;
        layer = finnclock_layer(&lx, &ly, &lw, &lh, &rev);
        if (layer) finnclock_transform(&x, &y, &rw, &rh, &a);
    }

    if (!layer || a <= 0.004f || rw < 1.0f || rh < 1.0f) return 0;

    int x0 = (int)floorf(x) - 2, y0 = (int)floorf(y) - 2;
    int x1 = (int)ceilf(x + rw) + 2, y1 = (int)ceilf(y + rh) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x1 <= x0 || y1 <= y0) return 0;
    r[0] = x0; r[1] = y0; r[2] = x1 - x0; r[3] = y1 - y0;
    return 1;
}

/* Save the desktop under every chrome surface. Returns 0 if a block could
 * not be allocated, in which case the next frame rebuilds from the wallpaper
 * rather than trusting a block that was not saved. */
static int chrome_capture(uint32_t *base, int w, int h) {
    int ok = 1;
    for (int i = 0; i < CH_COUNT; i++) {
        int r[4];
        if (!chrome_slot_rect(i, w, h, r)) {
            g_ch_rect[i][2] = g_ch_rect[i][3] = 0;
            continue;
        }
        size_t need = (size_t)r[2] * (size_t)r[3];
        if (need > g_ch_cap[i]) {
            uint32_t *np = realloc(g_ch_px[i], need * 4);
            if (!np) { ok = 0; continue; }   /* the old block is still there */
            g_ch_px[i] = np;
            g_ch_cap[i] = need;
        }
        for (int y = 0; y < r[3]; y++)
            memcpy(g_ch_px[i] + (size_t)y * r[2],
                   base + (size_t)(r[1] + y) * w + r[0],
                   (size_t)r[2] * 4);
        memcpy(g_ch_rect[i], r, sizeof r);
    }
    return ok;
}

/* Put the saved desktop back where chrome was drawn last frame. */
static void chrome_restore(uint32_t *base, int w) {
    for (int i = 0; i < CH_COUNT; i++) {
        int rw = g_ch_rect[i][2], rh = g_ch_rect[i][3];
        if (!g_ch_px[i] || rw <= 0 || rh <= 0) continue;
        for (int y = 0; y < rh; y++)
            memcpy(base + (size_t)(g_ch_rect[i][1] + y) * w + g_ch_rect[i][0],
                   g_ch_px[i] + (size_t)y * rw, (size_t)rw * 4);
    }
}

static void compose_base(uint32_t *base, const uint32_t *wall, int w, int h,
                         float scale, const finn_state *st, float theme_t) {
    build_chrome(w, h, scale, st, wall);

    int hide_chrome = finnsetup_is_open() || finnsetup_visible();
    int menu_vis = finnmenu_visible();

    /*
     * A full compose is required when the desktop itself may have moved
     * (the scene key), when setup owns the screen, when the menu is up (its
     * shadow is not in a block), and while the appearance is dissolving —
     * the whole frame is the new theme then, not a patch of it.
     */
    int full = !g_base_valid || hide_chrome || g_base_hide != hide_chrome ||
               !g_frame_scene_ok || g_base_scene != g_frame_scene ||
               g_base_menu != menu_vis || menu_vis || theme_t < 0.999f;

    int captured = 1;
    if (full) {
        memcpy(base, wall, (size_t)w * h * 4);
        if (!hide_chrome && finnwindow_visible())
            finnwindow_blit_scaled(base, w, h);
    } else {
        chrome_restore(base, w);
    }

    if (!hide_chrome) {
        /* The window is down and the chrome is not: this is the desk the
         * next chrome-only frame restores. */
        captured = chrome_capture(base, w, h);

        if (finnwindow_visible())
            finnwindow_overlay_blit_scaled(base, w, h);

        finndock_blit(base, w, h, 0, 0);

        /* the panel is in the base layer too, so a pointer move over it costs a
         * blit and nothing more. While it is mid-open it is transformed instead:
         * the raster is unchanged, only where and how large it lands. */
        if (finncontrol_visible()) {
            if (finncontrol_settled()) finncontrol_blit(base, w, h, 0, 0);
            else                       finncontrol_blit_scaled(base, w, h);
        }
        if (finnclock_visible()) {
            if (finnclock_settled()) finnclock_blit(base, w, h, 0, 0);
            else                     finnclock_blit_scaled(base, w, h);
        }
        if (finnmenu_visible()) {
            /* the scrim goes down before the menu goes up, so the menu is the
             * only thing in the room that is lit */
            float dim = finnmenu_scrim();
            if (dim > 0.004f) finn_frame_dim(base, (size_t)w * h, dim);
            if (finnmenu_settled()) finnmenu_blit(base, w, h, 0, 0);
            else                    finnmenu_blit_scaled(base, w, h);
        }
    }
    if (finnsetup_visible())
        finnsetup_blit_scaled(base, w, h);

    g_base_scene = g_frame_scene;
    g_base_hide = hide_chrome;
    g_base_menu = menu_vis;
    g_base_valid = captured && g_frame_scene_ok && !hide_chrome;
}

/* ---------------- accelerated path ----------------
 *
 * The same surfaces the software path composites, kept as textures and
 * pasted as quads. What this buys is not just speed: the wallpaper stops being
 * copied 16 MB at a time, the panel's transform becomes a vertex transform,
 * and the appearance dissolve happens in the texture unit instead of in a
 * per-pixel software blend.
 *
 * The chrome layers are uploaded only when their revision moves, which is
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
    unsigned tex_win,   tex_win_prev,   win_rev;
    unsigned tex_win_ovl, tex_win_ovl_prev, win_ovl_rev;
    unsigned tex_setup, tex_setup_prev, setup_rev;

    /* One black pixel, stretched over the screen: the scrim behind the Start
     * Menu. A texture is cheaper than a second framebuffer and does not need
     * a shader of its own. */
    unsigned tex_scrim;

    /* the boot screen's layer: the mark and the line */
    unsigned tex_boot, boot_rev;

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
                          const uint32_t *wall,
                          float theme_t, uint32_t *shot, int *shot_ok) {
    build_chrome(w, h, scale, st, wall);

    int wx = 0, wy = 0, ww = 0, wh = 0;
    unsigned wrev = 0;
    const uint32_t *wl = finnwindow_layer(&wx, &wy, &ww, &wh, &wrev);
    unsigned wtex = gpu_upload(g, wl, ww, wh, wrev, &g->tex_win, &g->win_rev);
    float wtx = 0, wty = 0, wtw = 0, wth = 0, wta = 0;
    if (wl) finnwindow_transform(&wtx, &wty, &wtw, &wth, &wta);
    float wdust = wl ? finnwindow_dust() : 0.0f;

    int vox = 0, voy = 0, vow = 0, voh = 0;
    unsigned vrev = 0;
    const uint32_t *vol = finnwindow_overlay_layer(&vox, &voy, &vow, &voh, &vrev);
    unsigned vtex = gpu_upload(g, vol, vow, voh, vrev,
                               &g->tex_win_ovl, &g->win_ovl_rev);
    float vtx = 0, vty = 0, vtw = 0, vth = 0, vta = 0;
    if (vol) finnwindow_overlay_transform(&vtx, &vty, &vtw, &vth, &vta);

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

    int stx = 0, sty = 0, stw = 0, sth = 0;
    unsigned strev = 0;
    const uint32_t *stl = finnsetup_layer(&stx, &sty, &stw, &sth, &strev);
    unsigned sttex = gpu_upload(g, stl, stw, sth, strev, &g->tex_setup, &g->setup_rev);
    float sux = 0, suy = 0, suw = 0, suh = 0, sua = 0;
    if (stl) finnsetup_transform(&sux, &suy, &suw, &suh, &sua);

    int fading = theme_t < 0.999f && g->has_prev;
    unsigned walltex = g->tex_wall;

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

    int hide_chrome = finnsetup_is_open() || finnsetup_visible();

    if (fading && g->fbo) {
        finngpu_target(g->gpu, g->fbo);
        finngpu_paste(g->gpu, walltex, 0, 0, (float)w, (float)h, 1.0f);
        if (!hide_chrome && wl) finngpu_paste_dissolve(g->gpu, wtex, wtx, wty, wtw, wth, wta, wdust);
        if (!hide_chrome && vol) finngpu_paste_dissolve(g->gpu, vtex, vtx, vty, vtw, vth, vta, wdust);
        if (!hide_chrome && dl) finngpu_paste(g->gpu, dtex, (float)dx, (float)dy, (float)dw, (float)dh, 1.0f);
        if (!hide_chrome && cl) finngpu_paste(g->gpu, ctex, tx, ty, tw, th, ta);
        if (!hide_chrome && kl) finngpu_paste(g->gpu, ktex, qx, qy, qw, qh, qa);
        if (stl) finngpu_paste(g->gpu, sttex, sux, suy, suw, suh, sua);
        finngpu_target(g->gpu, 0);

        finngpu_paste(g->gpu, walltex, 0, 0, (float)w, (float)h, 1.0f);
        if (!hide_chrome && wl && g->tex_win_prev)
            finngpu_paste(g->gpu, g->tex_win_prev, wtx, wty, wtw, wth, wta);
        if (!hide_chrome && vol && g->tex_win_ovl_prev)
            finngpu_paste(g->gpu, g->tex_win_ovl_prev, vtx, vty, vtw, vth, vta);
        if (!hide_chrome && dl && g->tex_dock_prev)
            finngpu_paste(g->gpu, g->tex_dock_prev, (float)dx, (float)dy,
                          (float)dw, (float)dh, 1.0f);
        if (!hide_chrome && cl && g->tex_cc_prev)
            finngpu_paste(g->gpu, g->tex_cc_prev, tx, ty, tw, th, ta);
        if (!hide_chrome && kl && g->tex_clock_prev)
            finngpu_paste(g->gpu, g->tex_clock_prev, qx, qy, qw, qh, qa);
        if (stl && g->tex_setup_prev)
            finngpu_paste(g->gpu, g->tex_setup_prev, sux, suy, suw, suh, sua);
        finngpu_paste_raw(g->gpu, g->fbo_tex, 0, 0, (float)w, (float)h, theme_t);
    } else {
        finngpu_paste(g->gpu, walltex, 0, 0, (float)w, (float)h, 1.0f);
        if (!hide_chrome && wl)
            finngpu_paste_dissolve(g->gpu, wtex, wtx, wty, wtw, wth, wta, wdust);
        if (!hide_chrome && vol)
            finngpu_paste_dissolve(g->gpu, vtex, vtx, vty, vtw, vth, vta, wdust);
        if (!hide_chrome && dl)
            finngpu_paste(g->gpu, dtex, (float)dx, (float)dy,
                          (float)dw, (float)dh, 1.0f);
        if (!hide_chrome && cl)
            finngpu_paste(g->gpu, ctex, tx, ty, tw, th, ta);
        if (!hide_chrome && kl)
            finngpu_paste(g->gpu, ktex, qx, qy, qw, qh, qa);
    }

    if (!hide_chrome) {
        float dim = finnmenu_scrim();
        if (dim > 0.004f && g->tex_scrim)
            finngpu_paste(g->gpu, g->tex_scrim, 0, 0, (float)w, (float)h, dim);
        if (ml)
            finngpu_paste(g->gpu, mtex, sx, sy, sw, sh, sa);
    }
    if (!fading && stl)
        finngpu_paste(g->gpu, sttex, sux, suy, suw, suh, sua);

    float balpha = finnboot_handoff_alpha();
    if (balpha > 0.002f && g->tex_scrim) {
        finngpu_paste(g->gpu, g->tex_scrim, 0, 0, (float)w, (float)h, balpha);
        int bx = 0, by = 0, bw = 0, bh = 0;
        unsigned brev = 0;
        const uint32_t *bl = finnboot_layer(&bx, &by, &bw, &bh, &brev);
        unsigned btex = gpu_upload(g, bl, bw, bh, brev, &g->tex_boot, &g->boot_rev);
        if (bl) finngpu_paste(g->gpu, btex, (float)bx, (float)by, (float)bw, (float)bh, balpha);
    }

    if (ptr->sprite && ptr->sprite_px > 0 && g->tex_cursor)
        finngpu_paste(g->gpu, g->tex_cursor,
                      (float)(ptr->x - ptr->hot_x), (float)(ptr->y - ptr->hot_y),
                      (float)ptr->sprite_px, (float)ptr->sprite_px, 1.0f);

    if (shot_ok) *shot_ok = 0;
    if (shot && finngpu_readback(g->gpu, shot) == 0 && shot_ok) *shot_ok = 1;

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
/* A press is remembered until the release: a control acts only when the
 * release lands on the same target, so a stray down can be slid off. */
static int g_press_part = FINNWINDOW_HIT_NONE;
static int g_press_win = 0;
static int g_cc_press = FINNCONTROL_NONE;
static float g_cc_press_v = -1.0f;

/*
 * A control that can act, and only those.
 *
 * A machine with no wireless adapter cannot be switched to Wi-Fi, and one
 * with no radio cannot be switched to Bluetooth: the tile says "No adapter"
 * and the state must say the same thing. Turning a missing radio on would
 * be inventing hardware, and the state file is what every other part of
 * the system reads.
 */
static int cc_activate(int act, float v01, const finn_state *st) {
    switch (act) {
    case FINNCONTROL_WIFI:
        if (st->wifi < 0) return 0;
        finn_state_set_wifi(st->wifi > 0 ? 0 : 1);
        return 1;
    case FINNCONTROL_BLUETOOTH:
        if (st->bluetooth < 0) return 0;
        finn_state_set_bluetooth(st->bluetooth > 0 ? 0 : 1);
        return 1;
    case FINNCONTROL_APPEARANCE:
        finn_state_set_dark(!st->dark);
        return 1;
    case FINNCONTROL_SETTINGS:
        finncontrol_set_open(0);
        finnwindow_open("app=settings");
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

/* The same rule on the way a slider is dragged: a level the machine does
 * not have is not a level the pointer can set. */
static int cc_set_level(int act, float v01, const finn_state *st) {
    if (v01 < 0.0f) return 0;
    int pct = (int)(v01 * 100.0f + 0.5f);
    if (act == FINNCONTROL_BRIGHTNESS) {
        if (st->brightness < 0) return 0;
        finn_state_set_brightness(pct);
        return 1;
    }
    if (act == FINNCONTROL_VOLUME) {
        if (st->volume < 0) return 0;
        finn_state_set_volume(pct);
        return 1;
    }
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
 * answer comes back as a notification, while its window opens on screen.
 */
static void launch_app(const finn_app *a) {
    if (!a) return;

    if (!a->installed) {
        finn_notify_post(a->name, "Not installed on this machine");
        return;
    }

    int wid = finnwindow_open(a->id);
    if (wid == FINNWINDOW_ERR_LIMIT) {
        finn_notify_post(a->name, "Window limit reached (8)");
        return;
    }
    if (wid < 0) {
        finn_notify_post(a->name, "Could not open a window");
        return;
    }

    char req[160], reply[256];
    snprintf(req, sizeof req, "app.launch %s", a->id);
    if (ctl_ask(req, reply, sizeof reply) == 0 && strncmp(reply, "OK", 2) == 0)
        finn_notify_post(a->name, "Started");
    else
        finn_notify_post(a->name, ctl_why(reply));
}

static void menu_launch(int index) {
    launch_app(finn_apps_at(index));
}

/* A taskbar pin was toggled from the menu or by a right-click on the tile.
 * The pin is a local preference, so the compositor writes it itself; the
 * notification says what changed, because the button that was pressed is
 * about to be covered by the menu when the notification appears. */
static void taskbar_toggle_pin(const char *app_id) {
    if (!app_id || !*app_id) return;
    int now = 0;
    if (finntaskbar_toggle(app_id, &now) != 0) {
        finn_notify_post("Taskbar", "Could not save the pin");
        return;
    }
    taskbar_pins_reload();
    finnmenu_pins_changed();
    const finn_app *a = finn_apps_find(app_id);
    finn_notify_post(a ? a->name : "Taskbar",
                     now ? "Pinned to taskbar" : "Unpinned from taskbar");
}

/* The power row. The control plane does the work; the shell only says what
 * was asked for. */
static void menu_power(const char *action) {
    char req[64], reply[256];
    snprintf(req, sizeof req, "power %s", action);
    if (ctl_ask(req, reply, sizeof reply) == 0 && strncmp(reply, "OK", 2) == 0)
        finn_notify_post("FinnOS",
                         strcmp(action, "restart") == 0 ? "Restarting" : "Shutting down");
    else
        finn_notify_post("FinnOS", ctl_why(reply));
}

/*
 * A unified search result was chosen. The menu only says what it is; the
 * shell does the three things it knows how to do: start an app through the
 * control plane, open Settings at a page, or change the machine.
 */
static void menu_activate(const finnmenu_result *r, const finn_state *st) {
    if (!r) return;

    if (r->kind == FINNMENU_RESULT_APP) {
        menu_launch(r->index);
        return;
    }

    if (r->kind == FINNMENU_RESULT_SETTINGS) {
        finn_settings_page_info info;
        if (finnsettingsapp_page_info(r->index, &info) == 0) {
            finnsettingsapp_set_page(info.id);
            finnwindow_open("settings");
        }
        return;
    }

    if (r->kind != FINNMENU_RESULT_ACTION) return;

    if (r->index == FINNMENU_ACTION_RESTART) menu_power("restart");
    else if (r->index == FINNMENU_ACTION_SHUTDOWN) menu_power("shutdown");
    else if (r->index == FINNMENU_ACTION_APPEARANCE) {
        int want = !(st && st->dark);
        if (finn_state_set_dark(want) == 0)
            finn_notify_post("Appearance", want ? "Dark" : "Light");
        else
            finn_notify_post("Appearance", "Could not save the change");
    }
}

/*
 * Finish setup, and say so when it did not take.
 *
 * The screen stays open on failure: the marker is what the next boot reads,
 * and a machine that forgets its setup is not set up. `finnsetup_activate`
 * reports START either way, so the outcome is read from `is_completed`.
 */
static int setup_finish(void) {
    if (finnsetup_is_completed()) return 1;
    finn_notify_post("Setup", "Could not save the setup");
    return 0;
}

/* 1 while the Meta key is down as a modifier rather than as a press of its
 * own. The menu opens on the release of a Meta press that no other key
 * followed, so Super+arrow can act without the menu opening first. */
static int g_meta_chord = 0;

static void wayland_forward_key(finn_wayland_ctx *ctx,
                                const finn_key_event *event) {
    if (!ctx || !ctx->server || event->repeat || finnmenu_is_open() ||
        finncontrol_is_open() || finnclock_is_open() || finnsetup_is_open())
        return;
    int id = finnwindow_external_focused();
    FinnWaylandSurface *surface = wayland_surface_for_id(ctx, id);
    if (!surface) return;
    if (ctx->keyboard_window != id) {
        finn_wayland_keyboard_focus(ctx->server, surface);
        ctx->keyboard_window = id;
    }
    (void)finn_wayland_keyboard_key(ctx->server, (uint32_t)event->code,
        event->pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                       : WL_KEYBOARD_KEY_STATE_RELEASED,
        wayland_time_msec());
}

/* Returns 1 when the chrome changed. Shell chords are intercepted first;
 * unhandled keys go to the focused external Wayland toplevel. */
static int key_action(const finn_key_event *e, const finn_state *st,
                      finn_wayland_ctx *wayland) {
    /* Meta is a modifier first and the menu key second. Opening the menu on
     * the way down would put it in front of the window the chord is about
     * to move. */
    if (e->code == KEY_LEFTMETA || e->code == KEY_RIGHTMETA) {
        if (e->pressed) { g_meta_chord = 0; return 0; }
        if (g_meta_chord) { g_meta_chord = 0; return 0; }
        int open = !finnmenu_is_open();
        finncontrol_set_open(0);
        finnclock_set_open(0);
        finnmenu_set_open(open);
        return 1;
    }

    if (!e->pressed) {
        wayland_forward_key(wayland, e);
        return 0;
    }

    /* The console chord is checked before anything else: it must work from a
     * setup screen, a menu and a frozen-looking session alike. While another
     * VT is active, no other key belongs to this session. */
    if ((e->mods & FINN_MOD_CTRL) && (e->mods & FINN_MOD_ALT) &&
        e->code >= KEY_F1 && e->code <= KEY_F6) {
        vt_switch(e->code - KEY_F1 + 1);
        return 0;
    }
    if (!g_vt_active) return 0;

    /*
     * Super+arrow is the window-tiling chord. It is checked before the
     * focused window sees the key, because a text field that swallowed the
     * arrow would make the chord depend on what is focused.
     */
    if ((e->mods & FINN_MOD_SUPER) && e->code >= KEY_LEFT && e->code <= KEY_DOWN) {
        g_meta_chord = 1;
        finnmenu_set_open(0);
        finncontrol_set_open(0);
        finnclock_set_open(0);
        int shift = (e->mods & FINN_MOD_SHIFT) != 0;
        switch (e->code) {
        case KEY_LEFT:
            finnwindow_snap(0, shift ? FINNWINDOW_SNAP_TOPLEFT
                                     : FINNWINDOW_SNAP_LEFT);
            break;
        case KEY_RIGHT:
            finnwindow_snap(0, shift ? FINNWINDOW_SNAP_TOPRIGHT
                                     : FINNWINDOW_SNAP_RIGHT);
            break;
        case KEY_UP:
            finnwindow_snap(0, FINNWINDOW_SNAP_TOP);
            break;
        case KEY_DOWN:
            finnwindow_snap(0, FINNWINDOW_SNAP_RESTORE);
            break;
        }
        return 1;
    }

    /* The menu floats above the windows, so it reads the keyboard before
     * they do. Text typed with it open is a search, not a keystroke for the
     * window behind it. */
    if (finnmenu_is_open()) {
        finnmenu_result act;
        if (finnmenu_key(e, &act)) {
            if (act.kind != FINNMENU_RESULT_NONE) {
                menu_activate(&act, st);
                finnmenu_set_open(0);
            }
            return 1;
        }
    }

    if (finnwindow_key(e)) return 1;
    if (e->repeat) return 0;

    if (finnsetup_is_open()) {
        switch (e->code) {
        case KEY_TAB:
            finnsetup_focus_next((e->mods & FINN_MOD_SHIFT) ? -1 : 1);
            return 1;
        case KEY_RIGHT:
        case KEY_DOWN:
            finnsetup_focus_next(1);
            return 1;
        case KEY_LEFT:
        case KEY_UP:
            finnsetup_focus_next(-1);
            return 1;
        case KEY_ENTER:
        case KEY_KPENTER:
        case KEY_SPACE:
            if (finnsetup_activate() == FINNSETUP_START) return setup_finish();
            return 1;
        default:
            return 0;
        }
    }

    if (e->code == KEY_ESC) {
        int was_open = finnmenu_is_open() || finncontrol_is_open() || finnclock_is_open();
        finnmenu_set_open(0);
        finncontrol_set_open(0);
        finnclock_set_open(0);
        return was_open;
    }

    if ((e->mods & FINN_MOD_ALT) && e->code == KEY_TAB) {
        finnwindow_cycle_focus((e->mods & FINN_MOD_SHIFT) ? -1 : 1);
        return 1;
    }

    wayland_forward_key(wayland, e);
    return 0;
}

/* Returns 1 when the chrome itself changed and the layer must be rebuilt. */
static int shell_input(finn_pointer *p, const finn_state *st,
                       finn_wayland_ctx *wayland) {
    int now = p->buttons & FINN_BTN_LEFT;
    int was = g_prev_buttons & FINN_BTN_LEFT;
    int now_r = p->buttons & FINN_BTN_RIGHT;
    int was_r = g_prev_buttons & FINN_BTN_RIGHT;
    uint32_t event_time = wayland_time_msec();
    g_prev_buttons = p->buttons;

    if (!now_r && was_r && wayland &&
        (wayland->pointer_buttons & FINN_BTN_RIGHT))
        wayland_send_button(wayland, BTN_RIGHT,
                            WL_POINTER_BUTTON_STATE_RELEASED,
                            FINN_BTN_RIGHT, p->x, p->y, event_time);

    /*
     * A right-click on a taskbar tile pins or unpins it. This is the shortcut
     * for the same preference the menu's preview shows; neither is a machine
     * change, so neither goes through the control plane.
     */
    if (now_r && !was_r) {
        if (!finnsetup_is_open() && !finnmenu_is_open() &&
            !finncontrol_is_open() && !finnclock_is_open() &&
            finndock_hit(p->x, p->y) == FINNDOCK_HIT_TASK) {
            int ti = finndock_task_at(p->x, p->y);
            if (ti >= 0 && ti < g_n_task_apps && g_task_apps[ti].id[0] != '@') {
                taskbar_toggle_pin(g_task_apps[ti].id);
                return 1;
            }
        }
        if (!finnsetup_is_open() && !finnmenu_is_open() &&
            !finncontrol_is_open() && !finnclock_is_open() &&
            finndock_hit(p->x, p->y) == FINNDOCK_HIT_NONE) {
            int id = 0;
            if (finnwindow_external_hit(p->x, p->y, &id, NULL, NULL)) {
                finnwindow_press(FINNWINDOW_HIT_BODY, id, p->x, p->y);
                wayland_send_button(wayland, BTN_RIGHT,
                                    WL_POINTER_BUTTON_STATE_PRESSED,
                                    FINN_BTN_RIGHT, p->x, p->y, event_time);
                return 1;
            }
        }
        return 0;
    }

    if (now && !was) {
        float v01 = -1.0f;

        /* Setup screen (Figma 2:14 & 7:10) sits over the whole desktop when active */
        if (finnsetup_is_open()) {
            int sact = finnsetup_hit(p->x, p->y);
            finnsetup_press(sact);
            if (sact == FINNSETUP_LANGUAGE) {
                finnsetup_cycle_language();
                return 1;
            }
            if (sact == FINNSETUP_ACCESSIBILITY) {
                int opt = finnsetup_hit_appearance_option(p->x, p->y);
                if (opt >= 0) {
                    if (opt != st->dark) finn_state_set_dark(opt);
                } else {
                    finn_state_set_dark(!st->dark);
                }
                return 1;
            }
            if (sact == FINNSETUP_START) {
                finnsetup_complete();
                return setup_finish();
            }
            return 0;
        }

        /*
         * The Start Menu floats above everything else, so it sees the click
         * first. Inside it the click is its own; outside it the click
         * dismisses it and goes no further — the rule the Control Center
         * already follows.
         */
        if (finnmenu_is_open()) {
            finnmenu_result r;
            int what = finnmenu_hit(p->x, p->y, &r);
            if (what == FINNMENU_RESULT) {
                finnmenu_press(FINNMENU_RESULT);
                menu_activate(&r, st);
                finnmenu_set_open(0);
                return 1;
            }
            if (what == FINNMENU_PIN) {
                const finn_app *a = finn_apps_at(r.index);
                if (a && r.kind == FINNMENU_RESULT_APP) {
                    finnmenu_press(FINNMENU_PIN);
                    taskbar_toggle_pin(a->id);
                }
                return 1;      /* the menu stays open to show the new state */
            }
            if (what == FINNMENU_SEARCH) {
                finnmenu_focus_search();
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
                finnmenu_blur_search();
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
        g_cc_press = FINNCONTROL_NONE;

        switch (act) {
        case FINNCONTROL_WIFI:
        case FINNCONTROL_BLUETOOTH:
        case FINNCONTROL_APPEARANCE:
        case FINNCONTROL_SETTINGS:
            finncontrol_press(act);
            break;
        default:
            break;
        }

        /* A slider follows the finger from the moment it lands; a toggle acts
         * on release, where the finger actually is (docs/review.md P1.6). */
        if (act == FINNCONTROL_BRIGHTNESS || act == FINNCONTROL_VOLUME)
            return cc_set_level(act, v01, st);
        if (act != FINNCONTROL_NONE && act != FINNCONTROL_PANEL) {
            g_cc_press = act;
            g_cc_press_v = v01;
            return 1;
        }
        if (act == FINNCONTROL_PANEL)
            return 0;                       /* swallowed by the panel */

        int hit = finndock_hit(p->x, p->y);
        /* The clock is a control but not a status tile: it takes no wash.
         * A taskbar tile takes the wash on itself, not on the whole pill. */
        if (hit == FINNDOCK_HIT_TASK)
            finndock_press_task(finndock_task_at(p->x, p->y));
        else
            finndock_press(hit == FINNDOCK_HIT_CLOCK ? FINNDOCK_HIT_NONE : hit);

        if (hit == FINNDOCK_HIT_TASK) {
            int ti = finndock_task_at(p->x, p->y);
            finncontrol_set_open(0);
            finnclock_set_open(0);
            finnmenu_set_open(0);
            if (ti >= 0 && ti < g_n_task_apps) {
                const finndock_app *t = &g_task_apps[ti];
                if (t->windows > 0) {
                    /* A tile with windows restores or folds its group. A
                     * tile that is only pinned is a launcher. */
                    if (t->id[0] == '@')
                        finnwindow_taskbar_activate(NULL, atoi(t->id + 1));
                    else
                        finnwindow_taskbar_activate(t->id, 0);
                } else {
                    launch_app(finn_apps_find(t->id));
                }
            }
            return 1;
        }
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
        if (hit == FINNDOCK_HIT_SEARCH) {
            finncontrol_set_open(0);
            finnclock_set_open(0);
            finnmenu_set_open(1);
            finnmenu_focus_search();
            return 1;
        }
        if (hit == FINNDOCK_HIT_WINDOWS) {
            finncontrol_set_open(0);
            finnclock_set_open(0);
            finnmenu_set_open(0);
            finnwindow_cycle_focus(1);
            return 1;
        }
        if (finnclock_is_open())   { finnclock_set_open(0);   return 1; }
        if (finncontrol_is_open()) { finncontrol_set_open(0); return 1; }

        /* Windows (Figma 12:2) */
        int wid = 0;
        int whit = finnwindow_hit(p->x, p->y, &wid);
        if (whit != FINNWINDOW_HIT_NONE) {
            finnwindow_press(whit, wid, p->x, p->y);
            int client_id = 0;
            if (whit == FINNWINDOW_HIT_BODY &&
                finnwindow_external_hit(p->x, p->y, &client_id, NULL, NULL))
                wayland_send_button(wayland, BTN_LEFT,
                                    WL_POINTER_BUTTON_STATE_PRESSED,
                                    FINN_BTN_LEFT, p->x, p->y, event_time);
            if (whit >= FINNWINDOW_HIT_EXPAND && whit <= FINNWINDOW_HIT_CLOSE) {
                g_press_part = whit;
                g_press_win = wid;
            }
            return 1;
        }
        return 0;
    }

    if (!now && was) {
        int changed = 0;

        /* A Window Options control fires only if the release lands on the
         * control the press started on. Nothing irreversible happens on the
         * way down. */
        if (g_press_part != FINNWINDOW_HIT_NONE) {
            int rw = 0;
            int rp = finnwindow_hit(p->x, p->y, &rw);
            if (rp == g_press_part && rw == g_press_win) {
                if (rp == FINNWINDOW_HIT_EXPAND)        finnwindow_maximize(rw);
                else if (rp == FINNWINDOW_HIT_MINIMIZE) finnwindow_minimize(rw);
                else if (rp == FINNWINDOW_HIT_CLOSE)    finnwindow_close(rw);
                changed = 1;
            }
            g_press_part = FINNWINDOW_HIT_NONE;
        }

        /* Same rule for a Control Center toggle, which stays where it was
         * pressed and only acts when the finger is still on it. */
        if (g_cc_press != FINNCONTROL_NONE) {
            float v = -1.0f;
            int act = finncontrol_hit(p->x, p->y, &v);
            if (act == g_cc_press) changed |= cc_activate(act, g_cc_press_v, st);
            g_cc_press = FINNCONTROL_NONE;
        }

        g_drag = FINNCONTROL_NONE;
        finndock_press(FINNDOCK_HIT_NONE);
        finncontrol_press(FINNCONTROL_NONE);
        finnmenu_press(FINNMENU_NONE);
        finnsetup_press(FINNSETUP_NONE);
        finnwindow_release(p->x, p->y);
        if (wayland && (wayland->pointer_buttons & FINN_BTN_LEFT))
            wayland_send_button(wayland, BTN_LEFT,
                                WL_POINTER_BUTTON_STATE_RELEASED,
                                FINN_BTN_LEFT, p->x, p->y, event_time);
        return changed;
    }

    if (now && finnwindow_drag(p->x, p->y))
        return 1;

    /* press-and-drag keeps setting the slider the press landed on */
    if (now && g_drag != FINNCONTROL_NONE)
        return cc_set_level(g_drag, finncontrol_drag(g_drag, p->x), st);

    return 0;
}

/* ---------------- the boot screen's frames ----------------
 *
 * The boot screen is black with a mark and a line on it, so its frame is
 * not a layer over anything: the frame is filled black and the layer — the
 * mark and the line, a couple of hundred pixels — is put on top of it. A
 * full-screen layer would cost sixteen megabytes to carry a few hundred
 * pixels of ink.
 */
static uint32_t boot_frame(finngpu *gpu, gpu_ctx *gx, struct fb *fbs, int *cur,
                            int w, int h) {
    if (gpu) {
        int bx = 0, by = 0, bw = 0, bh = 0;
        unsigned brev = 0;
        const uint32_t *bl = finnboot_layer(&bx, &by, &bw, &bh, &brev);
        unsigned btex = gpu_upload(gx, bl, bw, bh, brev, &gx->tex_boot, &gx->boot_rev);

        finngpu_begin(gpu);
        finngpu_paste(gpu, gx->tex_scrim, 0, 0, (float)w, (float)h, 1.0f);
        if (bl) finngpu_paste(gpu, btex, (float)bx, (float)by,
                              (float)bw, (float)bh, 1.0f);
        uint32_t fbid = 0;
        if (!finngpu_end(gpu, &fbid)) return 0;
        return fbid;
    }

    int next = *cur ^ 1;
    uint32_t *dst = fbs[next].map;
    for (int i = 0; i < w * h; i++) dst[i] = 0xFF000000u;
    finnboot_blit(dst, w, h, 0, 0);
    *cur = next;
    return fbs[next].id;
}

/* Put a frame on the screen, and wait for it to land. The flip is the
 * clock: the next frame is due when the last one has arrived. A boot frame
 * that never landed is not followed by releasing the buffer behind it. */
static int boot_flip(uint32_t fbid, finngpu *gpu) {
    int landed = display_flip(fbid);
    if (gpu && landed == 0) finngpu_release(gpu);
    return landed;
}

static int ensure_user_runtime_dir(uid_t uid, gid_t gid,
                                  char *path, size_t path_cap) {
    int n = snprintf(path, path_cap, "/run/user/%u", (unsigned)uid);
    if (n < 0 || (size_t)n >= path_cap) { errno = ENAMETOOLONG; return -1; }
    int created = mkdir(path, 0700) == 0;
    if (!created && errno != EEXIST) return -1;
    if (created && (chown(path, uid, gid) != 0 || chmod(path, 0700) != 0))
        return -1;
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode) || st.st_uid != uid) {
        errno = EPERM;
        return -1;
    }
    if ((st.st_mode & 0777) != 0700 && chmod(path, 0700) != 0) return -1;
    return 0;
}

int main(int argc, char **argv) {
    const char *wall_path = (argc > 1) ? argv[1] : "/usr/share/finn/wallpaper/default.webp";

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    /* The watchdog starts with the unit, not with the desktop: decoding the
     * wallpaper and parsing the icons is part of the service's start-up, and
     * a slow one must not look like a hung one. */
    notify_open();

    /*
     * Find the card that has a display on it, rather than assuming card0.
     * A machine with two GPUs can have its panel on card1, and the unit's
     * device dependency on card0 is a trigger for "DRM is up", not a claim
     * that card0 is the one with the connector.
     */
    int fd = -1;
    drmModeConnector *conn = NULL;
    drmModeModeInfo mode;
    uint32_t crtc_id = 0;

    for (int n = 0; n < 16 && fd < 0; n++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/dri/card%d", n);
        int cand = open(path, O_RDWR | O_CLOEXEC);
        if (cand < 0) continue;

        drmModeRes *res = drmModeGetResources(cand);
        if (!res) { close(cand); continue; }

        drmModeConnector *c = NULL;
        for (int i = 0; i < res->count_connectors; i++) {
            drmModeConnector *try = drmModeGetConnector(cand, res->connectors[i]);
            if (try && try->connection == DRM_MODE_CONNECTED && try->count_modes > 0) {
                c = try;
                break;
            }
            if (try) drmModeFreeConnector(try);
        }

        if (!c) { drmModeFreeResources(res); close(cand); continue; }

        drmModeEncoder *enc = drmModeGetEncoder(cand, c->encoder_id);
        uint32_t crtc = enc ? enc->crtc_id : (res->count_crtcs ? res->crtcs[0] : 0);
        if (enc) drmModeFreeEncoder(enc);

        if (!crtc) {
            drmModeFreeConnector(c);
            drmModeFreeResources(res);
            close(cand);
            continue;
        }

        fd = cand;
        conn = c;
        mode = pick_mode(c);
        crtc_id = crtc;
        drmModeFreeResources(res);
        fprintf(stderr, "finn-comp: display on %s\n", path);
    }

    if (fd < 0) { fprintf(stderr, "finn-comp: no DRM card with a connected display\n"); return 1; }

    /* Keep what a full modeset needs: a flip that stops landing is repaired
     * with the CRTC, connector and mode that were chosen here. */
    g_drm_fd = fd;
    g_crtc_id = crtc_id;
    g_conn_id = conn->connector_id;
    g_mode = mode;

    int w = mode.hdisplay, h = mode.vdisplay;
    float scale = fmaxf(0.85f, fminf((float)h / 1080.0f, 2.0f));
    fprintf(stderr, "finn-comp: %dx%d @%dHz  scale %.2f  (%d modes offered)\n",
            w, h, mode.vrefresh, scale, conn->count_modes);

    vt_open();
    g_vt_active = vt_is_active();

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

    /* ---------------- the boot screen ----------------
     *
     * The machine is not up yet. The wallpaper is not decoded, the face is
     * not loaded, the icons are not parsed, and the display has nothing to
     * show but that. So the first thing the screen ever shows is the mark
     * and a line that fills as the work completes — the design from the
     * Figma file, and an honest report rather than a theatre of booting.
     */
    if (finnboot_init(ICON_DIR) != 0)
        fprintf(stderr, "finn-comp: no boot mark\n");

    gpu_ctx gx;
    memset(&gx, 0, sizeof gx);
    gx.gpu = gpu;
    if (gpu) {
        static const uint32_t black[4] = { 0xFF000000u, 0xFF000000u,
                                           0xFF000000u, 0xFF000000u };
        gx.tex_scrim = finngpu_tex(gpu, black, 2, 2);
    }

    int cur = 0;
    double boot_t = finn_now();

    /* The first frame the display ever shows is the boot screen. */
    finnboot_render(w, h, scale);
    uint32_t boot_fb = boot_frame(gpu, &gx, fbs, &cur, w, h);
    if (!boot_fb) { fprintf(stderr, "finn-comp: no first frame\n"); return 1; }
    if (drmModeSetCrtc(fd, crtc_id, boot_fb, 0, 0, &conn->connector_id, 1, &mode) < 0) {
        fprintf(stderr, "finn-comp: setcrtc failed: %s\n", strerror(errno));
        return 1;
    }
    g_last_fb = boot_fb;

    /*
     * Move the line to `pct`, drawing as it goes. The line eases toward
     * the truth rather than jumping to it, so a step that takes a moment
     * reads as movement instead of as a leap.
     */
    #define BOOT_TO(pct) do {                                              \
        finnboot_set_progress((float)(pct) / 100.0f);                      \
        notify_say("WATCHDOG=1");                                          \
        while (finnboot_running()) {                                      \
            double now_t = finn_now();                                     \
            float dt = (float)(now_t - boot_t);                            \
            boot_t = now_t;                                                \
            if (dt <= 0.0f) dt = 1.0f / 60.0f;                             \
            if (dt > 0.05f) dt = 0.05f;                                    \
            finnboot_tick(dt);                                             \
            finnboot_render(w, h, scale);                                  \
            uint32_t fbid = boot_frame(gpu, &gx, fbs, &cur, w, h);          \
            if (fbid) boot_flip(fbid, gpu);                                \
            notify_say("WATCHDOG=1");                                      \
        }                                                                  \
    } while (0)

    /* the shell's own assets: the face, the icons */
    if (finndock_init(ICON_DIR, FONT_DIR) != 0)
        fprintf(stderr, "finn-comp: running without shell chrome\n");
    if (finncontrol_init(ICON_DIR, FONT_DIR) != 0)
        fprintf(stderr, "finn-comp: control center unavailable\n");
    if (finnclock_init(ICON_DIR, FONT_DIR) != 0)
        fprintf(stderr, "finn-comp: clock page unavailable\n");
    {
        const char *reg = getenv("FINN_APPS");
        const char *cat = reg && reg[0] ? reg : FINN_APPS_CATALOGUE;
        finnmenu_init(ICON_DIR, FONT_DIR, cat);
        if (finnwindow_init(ICON_DIR, FONT_DIR, cat) != 0)
            fprintf(stderr, "finn-comp: window manager unavailable\n");
    }
    if (finnsetup_init(ICON_DIR, FONT_DIR) != 0)
        fprintf(stderr, "finn-comp: setup surface unavailable\n");
    BOOT_TO(40);

    /* wall = clean wallpaper; base = wallpaper + dock, ready to blit */
    uint32_t *wall = malloc((size_t)w * h * 4);
    uint32_t *base = malloc((size_t)w * h * 4);
    if (!wall || !base) { fprintf(stderr, "finn-comp: out of memory\n"); return 1; }
    if (load_wallpaper(wall, w, h, wall_path) != 0) {
        fprintf(stderr, "finn-comp: wallpaper unavailable, flat fill\n");
        for (int i = 0; i < w * h; i++) wall[i] = 0xFFEFE4F2u;
    }
    BOOT_TO(72);

    /*
     * The wallpaper is static, so it is blurred once, here, at 1/8 scale.
     * From this point every frosted surface samples this instead of the
     * picture itself — see finnblur.h for why that is free per frame.
     */
    finnblur_wall(wall, w, h);
    BOOT_TO(82);

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

    finn_kbd kbd;
    finn_kbd_open(&kbd);
    BOOT_TO(90);

    finn_state st, prev;
    finn_state_read(&st);
    prev = st;

    /* The queue is empty at every start, by design: the page should be able to
     * say nothing has happened and be right. The one thing that has certainly
     * happened is this session beginning, so that is what it opens with. */
    finn_notify_post("FinnOS", "Session started");

    compose_base(base, wall, w, h, scale, &st, 1.0f);

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
        finnwindow_render(w, h, scale * DOCK_SCALE, dx, dy, dw, dh, &st);
    }

    /* the control plane needs somewhere to park explicit state overrides */
    mkdir("/run/finn", 0755);
    mkdir(FINN_STATE_DIR, 0755);

    int ctl = finn_listen(FINN_COMP_SOCK, 0600);
    if (ctl < 0) fprintf(stderr, "finn-comp: no control socket: %s\n", strerror(errno));
    else         fprintf(stderr, "finn-comp: control on %s (root only)\n", FINN_COMP_SOCK);

    ctl_ctx cx = {
        .ptr = &ptr, .kbd = &kbd, .w = w, .h = h, .base = base, .scale = scale,
        .capture_fd = -1, .gpu = gpu != NULL
    };

    finn_wayland_ctx wayland;
    memset(&wayland, 0, sizeof wayland);
    wayland.control = &cx;
    wayland.default_width = (int)(w * 0.56f);
    wayland.default_height = (int)(h * 0.56f);
    if (wayland.default_width < 360) wayland.default_width = 360;
    if (wayland.default_height < 240) wayland.default_height = 240;
    if (wayland.default_width > 1280) wayland.default_width = 1280;
    if (wayland.default_height > 800) wayland.default_height = 800;
    struct passwd *finn_user = getpwnam("finn");
    char runtime_dir[PATH_MAX], wayland_socket[PATH_MAX];
    if (!finn_user) {
        fprintf(stderr, "finn-comp: Wayland unavailable: user 'finn' not found\n");
    } else if (ensure_user_runtime_dir(finn_user->pw_uid, finn_user->pw_gid,
                                       runtime_dir, sizeof runtime_dir) != 0) {
        fprintf(stderr, "finn-comp: Wayland runtime directory unavailable: %s\n",
                strerror(errno));
    } else if (snprintf(wayland_socket, sizeof wayland_socket,
                        "%s/wayland-0", runtime_dir) >=
               (int)sizeof wayland_socket) {
        fprintf(stderr, "finn-comp: Wayland socket path is too long\n");
    } else {
        FinnWaylandCallbacks callbacks = {
            .surface_created = wayland_surface_created,
            .surface_destroyed = wayland_surface_destroyed,
            .title_changed = wayland_title_changed,
            .surface_committed = wayland_surface_committed
        };
        FinnWaylandConfig config = {
            .socket_path = wayland_socket,
            .output_width = w,
            .output_height = h,
            .output_refresh_millihz = 60000,
            .callbacks = callbacks,
            .userdata = &wayland
        };
        wayland.server = finn_wayland_create(&config);
        if (wayland.server)
            fprintf(stderr, "finn-comp: Wayland display on %s\n", wayland_socket);
        else
            fprintf(stderr, "finn-comp: Wayland unavailable: %s\n",
                    strerror(errno));
    }
    finnwindow_set_external_close_handler(wayland_window_close_request,
                                          &wayland);

    /* The static surfaces go up once. Only the chrome is ever re-uploaded,
     * and only when its own revision says the pixels moved. */
    if (gpu) {
        gx.tex_wall = finngpu_tex(gpu, wall, w, h);
        if (ptr.sprite && ptr.sprite_px > 0)
            gx.tex_cursor = finngpu_tex(gpu, ptr.sprite, ptr.sprite_px, ptr.sprite_px);
    }
    BOOT_TO(100);
    #undef BOOT_TO

    /*
     * A hold on the boot screen, for looking at it.
     *
     * The wake-up takes about a second, which is not long enough to
     * photograph, to test, or to read. FINN_BOOT_HOLD=<seconds> keeps the
     * mark and the line on the screen for that long before the desktop
     * takes its place. It is off unless it is asked for.
     */
    {
        const char *hold = getenv("FINN_BOOT_HOLD");
        if (hold && hold[0]) {
            double secs = atof(hold);
            if (secs > 0.0 && secs < 60.0) {
                fprintf(stderr, "finn-comp: holding the boot screen for %.1fs\n", secs);
                double until = finn_now() + secs;
                while (finn_now() < until) {
                    usleep(50000);
                    finnboot_tick(0.05f);
                    finnboot_render(w, h, scale);
                    uint32_t fbid = boot_frame(gpu, &gx, fbs, &cur, w, h);
                    if (fbid) boot_flip(fbid, gpu);
                }
            }
        }
    }

    enum { MAX_CLIENTS = 8 };
    int clients[MAX_CLIENTS];
    finn_conn conns[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = -1;
        finn_conn_init(&conns[i]);
    }

    /*
     * The handoff to the desktop is a smooth crossfade (docs/design/motion.md):
     * the mark and track fade out while the desktop dissolves in.
     */
    finnboot_start_exit();
    double h_t = finn_now();
    while (finnboot_running()) {
        double now_t = finn_now();
        float dt = (float)(now_t - h_t);
        h_t = now_t;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        if (dt > 0.05f) dt = 0.05f;

        finnboot_tick(dt);
        finnboot_render(w, h, scale);

        if (gpu) {
            uint32_t fbid = gpu_frame(&gx, w, h, scale, &st, &ptr, wall, 1.0f, NULL, NULL);
            if (fbid && display_flip(fbid) == 0) finngpu_release(gpu);
        } else {
            int next = cur ^ 1;
            uint32_t *dst = fbs[next].map;
            memcpy(dst, base, (size_t)w * h * 4);
            float balpha = finnboot_handoff_alpha();
            if (balpha > 0.002f) {
                unsigned inv = (unsigned)((1.0f - balpha) * 256.0f + 0.5f);
                for (int i = 0; i < w * h; i++) {
                    uint32_t c = dst[i];
                    unsigned r = ((c >> 16 & 0xFF) * inv) >> 8;
                    unsigned g = ((c >> 8 & 0xFF) * inv) >> 8;
                    unsigned b = ((c & 0xFF) * inv) >> 8;
                    dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
                }
                finnboot_blit(dst, w, h, 0, 0);
            }
            finn_pointer_draw(&ptr, dst, w, h);
            cur = next;
            display_flip(fbs[next].id);
        }
        notify_say("WATCHDOG=1");
    }

    /* The desktop's first frame is a flip, not a setcrtc: the display is
     * already showing the boot screen, and the desktop takes its place. */
    uint32_t first_fb;
    if (gpu) {
        first_fb = gpu_frame(&gx, w, h, scale, &st, &ptr, wall, 1.0f, NULL, NULL);
        if (!first_fb) {
            fprintf(stderr, "finn-comp: the GPU produced no first frame\n");
            return 1;
        }
    } else {
        int next = cur ^ 1;
        memcpy(fbs[next].map, base, (size_t)w * h * 4);
        finn_pointer_draw(&ptr, fbs[next].map, w, h);
        first_fb = fbs[next].id;
        cur = next;
    }
    if (display_flip(first_fb) != 0) {
        fprintf(stderr, "finn-comp: the desktop never reached the screen\n");
        return 1;
    }
    if (gpu) finngpu_release(gpu);

    fprintf(stderr, "finn-comp: desktop live %dx%d\n", w, h);
    fflush(stderr);
    notify_say("READY=1");
    notify_say("WATCHDOG=1");

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
    double last_wd = last_t;
    int exit_code = 0;

    while (!g_stop) {
        struct pollfd pf[FINN_PTR_MAX + FINN_KBD_MAX + 3 + MAX_CLIENTS];
        int nf = 0, ictl = -1, cslot[MAX_CLIENTS];
        int pslot[FINN_PTR_MAX], kslot[FINN_KBD_MAX + 1];

        double wd_now = finn_now();
        if (wd_now - last_wd >= 5.0) {
            notify_say("WATCHDOG=1");
            last_wd = wd_now;
        }
        if (vt_sync(fd, &ptr, &kbd)) cx.dirty = 1;

        int nptr = finn_pointer_fds(&ptr, pslot, FINN_PTR_MAX);
        for (int i = 0; i < nptr; i++) {
            pf[nf].fd = pslot[i];
            pf[nf].events = POLLIN;
            pslot[i] = nf++;
        }
        int nkbd = finn_kbd_fds(&kbd, kslot, FINN_KBD_MAX + 1);
        for (int i = 0; i < nkbd; i++) {
            pf[nf].fd = kslot[i];
            pf[nf].events = POLLIN;
            kslot[i] = nf++;
        }
        if (ctl >= 0)    { pf[nf].fd = ctl;    pf[nf].events = POLLIN; ictl = nf++; }
        if (wayland.server) {
            int wayland_fd = finn_wayland_fd(wayland.server);
            if (wayland_fd >= 0) {
                pf[nf].fd = wayland_fd;
                pf[nf].events = POLLIN;
                nf++;
            }
        }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            cslot[i] = -1;
            if (clients[i] >= 0) { pf[nf].fd = clients[i]; pf[nf].events = POLLIN; cslot[i] = nf++; }
        }

        /* A frame is only due while something is actually moving — or while a
         * capture is waiting on one to read back. Between animations the
         * compositor blocks on input exactly as before. */
        int busy = finndock_running() || finncontrol_running() || finnclock_running()
                   || finnmenu_running() || finnwindow_running() || finnsetup_running()
                   || finn_anim_running(&theme_fade)
                   || cx.capture_fd >= 0 || cx.dirty;
        int pr = poll(pf, (nfds_t)nf, busy ? FRAME_MS : 400);

        (void)wayland_dispatch_now(&wayland);

        int moved = 0, key_changed = 0;
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
                    /* The mode is not the check: a descriptor passed to a
                     * less privileged process would still open. Ask the
                     * kernel who is on the other end. */
                    struct ucred uc;
                    socklen_t ul = sizeof uc;
                    memset(&uc, 0, sizeof uc);
                    int ok = getsockopt(c, SOL_SOCKET, SO_PEERCRED, &uc, &ul) == 0 &&
                             uc.uid == 0;
                    if (!ok) {
                        fprintf(stderr, "finn-comp: refused control client uid=%d\n",
                                (int)uc.uid);
                        finn_write_str(c, "ERR permission denied\n");
                        close(c);
                    } else {
                        int slot = -1;
                        for (int i = 0; i < MAX_CLIENTS; i++)
                            if (clients[i] < 0) { slot = i; break; }
                        if (slot < 0) {
                            finn_write_str(c, "ERR busy\n");
                            close(c);
                        } else {
                            finn_conn_init(&conns[slot]);
                            clients[slot] = c;
                        }
                    }
                }
            }

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (cslot[i] < 0) continue;
                short rev = pf[cslot[i]].revents;
                if (!(rev & (POLLIN | POLLERR | POLLHUP))) continue;

                for (;;) {
                    char line[FINN_MAX_LINE];
                    int eof = 0;
                    int n = finn_conn_read(&conns[i], clients[i], line,
                                           (int)sizeof line, &eof);
                    if (n == 1) {
                        ctl_dispatch(line, clients[i], &cx);
                        continue;
                    }
                    if (n < 0 && errno == EMSGSIZE && !eof) {
                        finn_write_str(clients[i], "ERR line too long\n");
                        continue;
                    }
                    if (n < 0 || eof) {
                        if (cx.capture_fd == clients[i]) {
                            cx.capture_fd = -1;
                            cx.capture_step = 1;
                        }
                        close(clients[i]);
                        clients[i] = -1;
                    }
                    break;
                }
            }
        }

        /* Polled unconditionally, so a key injected by a key.* verb this
         * iteration is handled now rather than after the next wake-up. */
        {
            finn_key_event evs[64];
            int ne = finn_kbd_poll(&kbd, evs, 64);
            for (int k = 0; k < ne; k++)
                key_changed |= key_action(&evs[k], &st, &wayland);
        }

        if (vt_sync(fd, &ptr, &kbd)) cx.dirty = 1;
        if (!g_vt_active) {
            /* Another console owns the screen. Keep reading input and the
             * watchdog, but never flip: without DRM master that is a failure
             * the compositor would be restarted for. */
            last_t = finn_now();
            continue;
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

        int shell_changed = shell_input(&ptr, &st, &wayland) | key_changed;
        wayland_sync_focus(&wayland);
        wayland_sync_pointer(&wayland, ptr.x, ptr.y, wayland_time_msec());

        /* A notch of the wheel belongs to the window under the pointer, and
         * only when no floating surface is in front of it. The menu, the
         * panels and setup are modal-ish: scrolling the desktop behind them
         * would move something the person is not looking at. */
        int wheel = finn_pointer_take_wheel(&ptr);
        if (wheel && !finnmenu_is_open() && !finncontrol_is_open() &&
            !finnclock_is_open() && !finnsetup_is_open()) {
            int client_id = 0, sx = 0, sy = 0;
            int over_client = wayland.server &&
                (wayland.pointer_window ||
                 finnwindow_external_hit(ptr.x, ptr.y, &client_id, &sx, &sy));
            if (over_client) {
                if (!wayland.pointer_window)
                    wayland_sync_pointer(&wayland, ptr.x, ptr.y,
                                         wayland_time_msec());
                finn_wayland_pointer_axis(wayland.server,
                    WL_POINTER_AXIS_VERTICAL_SCROLL, -(double)wheel * 15.0,
                    -wheel, wayland_time_msec());
            } else {
                shell_changed |= finnwindow_scroll(ptr.x, ptr.y, wheel);
            }
        }

        if (cx.click_release > 0 && --cx.click_release == 0) {
            ptr.buttons &= ~cx.click_pending;
            shell_input(&ptr, &st, &wayland);
        }
        (void)wayland_dispatch_now(&wayland);

        if (ptr.dirty) { ptr.dirty = 0; moved = 1; }

        finn_state_poll(&st, now_t);
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
                if (gx.tex_win) {
                    finngpu_tex_free(gpu, gx.tex_win_prev);
                    gx.tex_win_prev = gx.tex_win;
                    gx.tex_win = 0;
                    gx.win_rev = 0;
                    gx.has_prev = 1;
                }
                if (gx.tex_win_ovl) {
                    finngpu_tex_free(gpu, gx.tex_win_ovl_prev);
                    gx.tex_win_ovl_prev = gx.tex_win_ovl;
                    gx.tex_win_ovl = 0;
                    gx.win_ovl_rev = 0;
                    gx.has_prev = 1;
                }
                if (gx.tex_setup) {
                    finngpu_tex_free(gpu, gx.tex_setup_prev);
                    gx.tex_setup_prev = gx.tex_setup;
                    gx.tex_setup = 0;
                    gx.setup_rev = 0;
                    gx.has_prev = 1;
                }
            }
            if (base_prev) memcpy(base_prev, base, (size_t)w * h * 4);
            finn_anim_jump(&theme_fade, 0.0f);
            finn_anim_to(&theme_fade, 1.0f, FINN_MOTION_THEME, 0.0f, FINN_EASE_IN_OUT);
        }

        /* the tray's status block is the only thing that answers a hover:
         * the pointer position decides, and a non-answer changes nothing */
        if (!finnsetup_is_open() && !finnsetup_visible()) {
            finndock_hover_at(ptr.x, ptr.y);
            finnmenu_hover_at(ptr.x, ptr.y);
            finncontrol_hover_at(ptr.x, ptr.y);
            finnclock_hover_at(ptr.x, ptr.y);
            finnwindow_hover_at(ptr.x, ptr.y);
        }
        finnsetup_hover_at(ptr.x, ptr.y);

        int anim = finndock_tick(adt);
        anim |= finncontrol_tick(adt);
        anim |= finnclock_tick(adt);
        anim |= finnmenu_tick(adt);
        anim |= finnwindow_tick(adt);
        anim |= finnsetup_tick(adt);
        anim |= finn_anim_tick(&theme_fade, adt);

        /* The minute changing is a frame, even when nothing else is moving.
         * On the software path the base has to be recomposed for it; a tick
         * that only reaches the damage test is what froze the dock clock
         * (docs/review.md P1.9). */
        int clock_tick = finndock_clock_tick();

        if (cx.dirty) { cx.dirty = 0; shell_changed = 1; }

        if (!moved && !schanged && !shell_changed && !anim && !clock_tick &&
            cx.capture_fd < 0) continue;

        prev = st;

        if (gpu) {
            /* gpu_frame builds the chrome itself; the composite is never
             * materialised on the CPU at all */
            uint32_t *shot = cx.capture_fd >= 0 ? base : NULL;
            int shot_ok = 0;
            double _t0 = finn_now();
            uint32_t fbid = gpu_frame(&gx, w, h, scale, &st, &ptr, wall,
                                      finn_anim_v(&theme_fade), shot, &shot_ok);
            double _t1 = finn_now();
            int landed = fbid ? display_flip(fbid) : -1;
            double _t2 = finn_now();
            if (timing_on(_t2)) {
                fprintf(stderr,
                        "FRAME build=%.2f flip=%.2f dock=%u win=%u gen=%u mode=gpu\n",
                        (_t1 - _t0) * 1000.0, (_t2 - _t1) * 1000.0,
                        gx.dock_rev, gx.win_rev, finnblur_generation());
                timing_sample(_t2, (_t1 - _t0) * 1000.0, (_t2 - _t1) * 1000.0);
            }

            /* Only a frame that landed may release the buffer it replaced;
             * releasing one the display is still showing is how a recoverable
             * hiccup becomes a frozen screen. */
            if (landed == 0) {
                finngpu_release(gpu);
                wayland_frame_done_all(&wayland, wayland_time_msec());
                (void)wayland_dispatch_now(&wayland);
                if (shot && shot_ok) {
                    ppm_write(cx.capture_fd, base, w, h, cx.capture_step);
                    cx.capture_fd = -1;
                } else if (shot) {
                    finn_write_str(cx.capture_fd, "ERR capture failed\n");
                    cx.capture_fd = -1;
                }
            } else if (cx.capture_fd >= 0) {
                /* The frame did not reach the screen, so it is not the frame
                 * a capture promised to return. Say so rather than hanging. */
                finn_write_str(cx.capture_fd, "ERR display unavailable\n");
                cx.capture_fd = -1;
            }
        } else {
            double _t0 = finn_now();
            float tf = finn_anim_v(&theme_fade);
            if (schanged || shell_changed || anim || clock_tick)
                compose_base(base, wall, w, h, scale, &st, tf);
            double _t1 = finn_now();

            int next = cur ^ 1;

            if (base_prev && tf < 0.999f) {
                uint32_t m = (uint32_t)(tf * 256.0f + 0.5f);
                if (m > 256u) m = 256u;
                finn_frame_blend(fbs[next].map, base_prev, base, (size_t)w * h, m);
            } else {
                memcpy(fbs[next].map, base, (size_t)w * h * 4);
            }
            finn_pointer_draw(&ptr, fbs[next].map, w, h);

            if (display_flip(fbs[next].id) == 0) {
                cur = next;
                wayland_frame_done_all(&wayland, wayland_time_msec());
                (void)wayland_dispatch_now(&wayland);
            }
            double _t2 = finn_now();
            if (timing_on(_t2)) {
                fprintf(stderr, "FRAME build=%.2f flip=%.2f gen=%u mode=cpu\n",
                        (_t1 - _t0) * 1000.0, (_t2 - _t1) * 1000.0,
                        finnblur_generation());
                timing_sample(_t2, (_t1 - _t0) * 1000.0, (_t2 - _t1) * 1000.0);
            }
        }

        if (display_lost()) {
            fprintf(stderr, "finn-comp: display lost after %d failed flips\n",
                    g_flip_fail);
            exit_code = 1;
            break;
        }
    }

    notify_say("STOPPING=1");

    for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i] >= 0) close(clients[i]);
    if (ctl >= 0) { close(ctl); unlink(FINN_COMP_SOCK); }
    if (wayland.server) {
        finn_wayland_destroy(wayland.server);
        wayland.server = NULL;
    }
    finnwindow_set_external_close_handler(NULL, NULL);
    finn_pointer_close(&ptr);
    finn_kbd_close(&kbd);
    finncontrol_free();
    finndock_free();
    finnclock_free();
    finnmenu_free();
    finnwindow_free();
    finnsetup_free();
    finnboot_free();
    finnblur_free();
    fprintf(stderr, "finn-comp: shutting down\n");

    if (gpu && !display_lost()) {
        /* let the last flip land before the frame it went to is freed */
        wait_flip(fd);
        finngpu_release(gpu);
    }
    drmModeSetCrtc(fd, crtc_id, 0, 0, 0, NULL, 0, NULL);
    if (gpu) finngpu_close(gpu);
    drmModeFreeConnector(conn);
    if (g_vt_fd >= 0) close(g_vt_fd);
    notify_close();
    close(fd);
    free(base);
    free(base_prev);
    free(wall);
    for (int i = 0; i < CH_COUNT; i++) free(g_ch_px[i]);
    return exit_code;
}
