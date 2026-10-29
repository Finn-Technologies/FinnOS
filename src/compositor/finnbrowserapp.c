/*
 * finnbrowserapp — the Finn Browser view inside finn-comp.
 *
 * A browser is two pieces: the chrome (tab strip, back/forward/reload,
 * address bar, find bar, and status banner) is drawn directly here by the
 * compositor using the shell's own native primitives and design tokens. Only
 * the web page canvas is supplied out of process by Chromium over
 * FINN_BROWSER_SOCK; no foreign toolkit window (GTK/Qt/X11) is ever created.
 *
 * This design enforces FinnOS invariants:
 *   - Never fake a capability: disabled buttons (Back without history,
 *     Forward without forward history, Reload/Find without engine) are
 *     drawn visibly disabled and refuse presses.
 *   - The control plane is the single door: launching the engine helper goes
 *     through finnd ("app.launch browser") rather than fork-exec in finn-comp.
 *   - Plain wire: line-oriented text messages and raw 0xFFRRGGBB pixel frames
 *     over a private UNIX stream socket (mode 0660 root:finn, SO_PEERCRED checked).
 */
#define _GNU_SOURCE
#include "finnbrowserapp.h"
#include "finnbrowser.h"
#include "finnappui.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnvg.h"
#include "finnsock.h"
#include "finnproto.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <linux/input.h>

/* Linux evdev key fallbacks for host builds on systems with minimal shims */
#ifndef KEY_ESC
#define KEY_ESC 1
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 14
#endif
#ifndef KEY_TAB
#define KEY_TAB 15
#endif
#ifndef KEY_ENTER
#define KEY_ENTER 28
#endif
#ifndef KEY_F
#define KEY_F 33
#endif
#ifndef KEY_L
#define KEY_L 38
#endif
#ifndef KEY_T
#define KEY_T 20
#endif
#ifndef KEY_W
#define KEY_W 17
#endif
#ifndef KEY_U
#define KEY_U 22
#endif
#ifndef KEY_R
#define KEY_R 19
#endif
#ifndef KEY_F5
#define KEY_F5 63
#endif
#ifndef KEY_SPACE
#define KEY_SPACE 57
#endif
#ifndef KEY_PAGEUP
#define KEY_PAGEUP 104
#endif
#ifndef KEY_PAGEDOWN
#define KEY_PAGEDOWN 109
#endif
#ifndef KEY_END
#define KEY_END 107
#endif
#ifndef KEY_HOME
#define KEY_HOME 102
#endif
#ifndef KEY_LEFT
#define KEY_LEFT 105
#endif
#ifndef KEY_UP
#define KEY_UP 103
#endif
#ifndef KEY_RIGHT
#define KEY_RIGHT 106
#endif
#ifndef KEY_DOWN
#define KEY_DOWN 108
#endif
#ifndef KEY_DELETE
#define KEY_DELETE 111
#endif
#ifndef KEY_KPENTER
#define KEY_KPENTER 96
#endif

#ifndef FINN_BROWSER_MAX_TABS
#define FINN_BROWSER_MAX_TABS FINN_BROWSER_TAB_MAX
#endif

/* ---------------- data structures ---------------- */

typedef struct {
    int id;
    int active;
    int loading;
    int can_back;
    int can_forward;
    char url[FINN_BROWSER_URL_MAX];
    char title[FINN_BROWSER_TITLE_MAX];
    uint32_t *pixels;
    int pixel_w;
    int pixel_h;
    uint32_t frame_seq;
    int find_active;
    int find_total;
} BrowserTab;

typedef struct {
    int listen_fd;
    int client_fd;
    int engine_spawned;
    int engine_ready;
    int engine_unavailable;
    char unavailable_reason[256];
    char status_msg[256];
    int window_open;
    int launch_requested;

    BrowserTab tabs[FINN_BROWSER_TAB_MAX];
    int tab_count;
    int active_tab_id;

    /* Viewport metrics (in scanout pixels) */
    int vp_w;
    int vp_h;
    float last_scale;

    /* Interactive UI state */
    int address_focused;
    char address_buf[FINN_BROWSER_URL_MAX];
    int address_len;

    int find_open;
    int find_focused;
    char find_buf[FINN_BROWSER_FIND_MAX];
    int find_len;

    int viewport_focused;
    int pointer_in_viewport;
    int pointer_buttons;

    /* Socket RX line buffer */
    char rx_buf[FINN_BROWSER_LINE_MAX * 2];
    size_t rx_buf_len;

    /* Binary frame rx staging */
    uint32_t *rx_staging;
    int rx_frame_tab;
    int rx_frame_w;
    int rx_frame_h;
    size_t rx_frame_rem;
    size_t rx_frame_off;

    uint32_t revision;
    int dirty;
} FinnBrowserApp;

/* ---------------- static state ---------------- */

static FinnBrowserApp g_browser_app;
static finn_font g_f_title, g_f_label, g_f_sub;
static fsvg g_ic_search, g_ic_refresh, g_ic_chevron, g_ic_x;
static float g_font_scale = -1.0f;
static char g_font_dir[512] = "";
static char g_icon_dir[512] = "";
static int g_ready = 0;

/* ---------------- helper functions ---------------- */

static BrowserTab *find_tab(FinnBrowserApp *app, int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < app->tab_count; i++) {
        if (app->tabs[i].id == id)
            return &app->tabs[i];
    }
    return NULL;
}

static BrowserTab *add_tab(FinnBrowserApp *app, int id) {
    if (app->tab_count >= FINN_BROWSER_TAB_MAX) return NULL;
    BrowserTab *t = find_tab(app, id);
    if (t) return t;
    t = &app->tabs[app->tab_count++];
    memset(t, 0, sizeof(*t));
    t->id = id;
    if (app->tab_count == 1) {
        app->active_tab_id = id;
    }
    return t;
}

static void remove_tab(FinnBrowserApp *app, int id) {
    int idx = -1;
    for (int i = 0; i < app->tab_count; i++) {
        if (app->tabs[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;

    if (app->tabs[idx].pixels) {
        free(app->tabs[idx].pixels);
        app->tabs[idx].pixels = NULL;
    }

    for (int i = idx; i < app->tab_count - 1; i++) {
        app->tabs[i] = app->tabs[i + 1];
    }
    app->tab_count--;
    memset(&app->tabs[app->tab_count], 0, sizeof(BrowserTab));

    if (app->active_tab_id == id) {
        if (idx < app->tab_count) {
            app->active_tab_id = app->tabs[idx].id;
        } else if (app->tab_count > 0) {
            app->active_tab_id = app->tabs[app->tab_count - 1].id;
        } else {
            app->active_tab_id = 0;
        }
        BrowserTab *act = find_tab(app, app->active_tab_id);
        if (act && !app->address_focused) {
            snprintf(app->address_buf, sizeof(app->address_buf), "%s", act->url);
            app->address_len = (int)strlen(app->address_buf);
        }
    }
}

static BrowserTab *get_active_tab(FinnBrowserApp *app) {
    BrowserTab *t = find_tab(app, app->active_tab_id);
    if (!t && app->tab_count > 0) {
        app->active_tab_id = app->tabs[0].id;
        t = &app->tabs[0];
    }
    return t;
}

static const BrowserTab *get_active_tab_const(const FinnBrowserApp *app) {
    for (int i = 0; i < app->tab_count; i++) {
        if (app->tabs[i].id == app->active_tab_id)
            return &app->tabs[i];
    }
    if (app->tab_count > 0)
        return &app->tabs[0];
    return NULL;
}

/* Format and dispatch a command string non-blockingly over client_fd. */
static int send_cmd(int fd, const char *fmt, ...) {
    if (fd < 0) return -1;
    char buf[FINN_BROWSER_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return -1;

    size_t off = 0;
    while (off < (size_t)n) {
        ssize_t w = write(fd, buf + off, (size_t)n - off);
        if (w > 0) {
            off += (size_t)w;
        } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* Drop excess line if output buffer is saturated rather than stalling */
            break;
        } else {
            return -1;
        }
    }
    return off == (size_t)n ? 0 : -1;
}

static void handle_disconnect(FinnBrowserApp *app) {
    if (app->client_fd >= 0) {
        close(app->client_fd);
        app->client_fd = -1;
    }
    if (app->rx_staging) {
        free(app->rx_staging);
        app->rx_staging = NULL;
    }
    app->rx_frame_rem = 0;
    app->rx_frame_off = 0;
    app->rx_buf_len = 0;
    app->engine_ready = 0;
    app->dirty = 1;
    app->revision++;
}

/* ---------------- vector drawing primitives ---------------- */

/* Anti-aliased line segment with subpixel round caps */
static void draw_segment(uint32_t *layer, int lw, int lh,
                         float x1, float y1, float x2, float y2,
                         float thick, uint32_t color) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float l2 = dx * dx + dy * dy;
    float r = thick * 0.5f;

    int min_x = (int)floorf((x1 < x2 ? x1 : x2) - r - 1.0f);
    int max_x = (int)ceilf((x1 > x2 ? x1 : x2) + r + 1.0f);
    int min_y = (int)floorf((y1 < y2 ? y1 : y2) - r - 1.0f);
    int max_y = (int)ceilf((y1 > y2 ? y1 : y2) + r + 1.0f);

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= lw) max_x = lw - 1;
    if (max_y >= lh) max_y = lh - 1;

    for (int py = min_y; py <= max_y; py++) {
        float fpy = (float)py + 0.5f;
        uint32_t *row = layer + py * lw;
        for (int px = min_x; px <= max_x; px++) {
            float fpx = (float)px + 0.5f;
            float t = 0.0f;
            if (l2 > 0.0001f) {
                t = ((fpx - x1) * dx + (fpy - y1) * dy) / l2;
                if (t < 0.0f) t = 0.0f;
                else if (t > 1.0f) t = 1.0f;
            }
            float qx = x1 + t * dx;
            float qy = y1 + t * dy;
            float dist = sqrtf((fpx - qx) * (fpx - qx) + (fpy - qy) * (fpy - qy));
            float cov = r + 0.5f - dist;
            if (cov > 1.0f) cov = 1.0f;
            if (cov > 0.0f) {
                row[px] = finn_over(row[px], color, cov);
            }
        }
    }
}

static void draw_cross(uint32_t *layer, int lw, int lh,
                       float cx, float cy, float sz, float thick, uint32_t color) {
    float r = sz * 0.5f;
    draw_segment(layer, lw, lh, cx - r, cy - r, cx + r, cy + r, thick, color);
    draw_segment(layer, lw, lh, cx - r, cy + r, cx + r, cy - r, thick, color);
}

static void draw_plus(uint32_t *layer, int lw, int lh,
                      float cx, float cy, float sz, float thick, uint32_t color) {
    float r = sz * 0.5f;
    draw_segment(layer, lw, lh, cx - r, cy, cx + r, cy, thick, color);
    draw_segment(layer, lw, lh, cx, cy - r, cx, cy + r, thick, color);
}

static void draw_chevron_left(uint32_t *layer, int lw, int lh,
                              float cx, float cy, float sz, float thick, uint32_t color) {
    float r = sz * 0.5f;
    draw_segment(layer, lw, lh, cx + r * 0.5f, cy - r, cx - r * 0.5f, cy, thick, color);
    draw_segment(layer, lw, lh, cx - r * 0.5f, cy, cx + r * 0.5f, cy + r, thick, color);
}

static void draw_chevron_right(uint32_t *layer, int lw, int lh,
                               float cx, float cy, float sz, float thick, uint32_t color) {
    float r = sz * 0.5f;
    draw_segment(layer, lw, lh, cx - r * 0.5f, cy - r, cx + r * 0.5f, cy, thick, color);
    draw_segment(layer, lw, lh, cx + r * 0.5f, cy, cx - r * 0.5f, cy + r, thick, color);
}

static void draw_reload(uint32_t *layer, int lw, int lh,
                        float cx, float cy, float sz, float thick, uint32_t color) {
    float rad = sz * 0.42f;
    float r_thick = thick * 0.5f;

    int min_x = (int)floorf(cx - rad - r_thick - 2.0f);
    int max_x = (int)ceilf(cx + rad + r_thick + 2.0f);
    int min_y = (int)floorf(cy - rad - r_thick - 2.0f);
    int max_y = (int)ceilf(cy + rad + r_thick + 2.0f);

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= lw) max_x = lw - 1;
    if (max_y >= lh) max_y = lh - 1;

    /* Draw ~270 degree circular arc */
    for (int py = min_y; py <= max_y; py++) {
        float fpy = (float)py + 0.5f - cy;
        uint32_t *row = layer + py * lw;
        for (int px = min_x; px <= max_x; px++) {
            float fpx = (float)px + 0.5f - cx;
            float dist = sqrtf(fpx * fpx + fpy * fpy);
            float diff = fabsf(dist - rad);
            float cov = r_thick + 0.5f - diff;
            if (cov <= 0.0f) continue;
            float angle = atan2f(fpy, fpx);
            /* Gap in top-left quadrant */
            if (angle > -2.8f && angle < -1.4f) continue;
            if (cov > 1.0f) cov = 1.0f;
            row[px] = finn_over(row[px], color, cov);
        }
    }

    /* Arrowhead at top-right end of arc */
    float tip_x = cx + rad * cosf(-1.4f);
    float tip_y = cy + rad * sinf(-1.4f);
    draw_segment(layer, lw, lh, tip_x, tip_y, tip_x + sz * 0.35f, tip_y - sz * 0.1f, thick, color);
    draw_segment(layer, lw, lh, tip_x, tip_y, tip_x - sz * 0.1f, tip_y - sz * 0.35f, thick, color);
}

static void draw_magnifier(uint32_t *layer, int lw, int lh,
                           float cx, float cy, float sz, float thick, uint32_t color) {
    float rad = sz * 0.35f;
    float center_x = cx - sz * 0.12f;
    float center_y = cy - sz * 0.12f;
    float r_thick = thick * 0.5f;

    int min_x = (int)floorf(center_x - rad - r_thick - 1.0f);
    int max_x = (int)ceilf(center_x + rad + r_thick + 1.0f);
    int min_y = (int)floorf(center_y - rad - r_thick - 1.0f);
    int max_y = (int)ceilf(center_y + rad + r_thick + 1.0f);

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= lw) max_x = lw - 1;
    if (max_y >= lh) max_y = lh - 1;

    for (int py = min_y; py <= max_y; py++) {
        float fpy = (float)py + 0.5f - center_y;
        uint32_t *row = layer + py * lw;
        for (int px = min_x; px <= max_x; px++) {
            float fpx = (float)px + 0.5f - center_x;
            float dist = sqrtf(fpx * fpx + fpy * fpy);
            float diff = fabsf(dist - rad);
            float cov = r_thick + 0.5f - diff;
            if (cov > 1.0f) cov = 1.0f;
            if (cov > 0.0f) {
                row[px] = finn_over(row[px], color, cov);
            }
        }
    }

    /* Magnifier handle pointing down-right */
    float h_start_x = center_x + rad * 0.707f;
    float h_start_y = center_y + rad * 0.707f;
    float h_end_x = cx + sz * 0.45f;
    float h_end_y = cy + sz * 0.45f;
    draw_segment(layer, lw, lh, h_start_x, h_start_y, h_end_x, h_end_y, thick * 1.25f, color);
}

/* ---------------- hash & caching ---------------- */

static inline uint64_t fnv1a_step(uint64_t h, uint64_t val) {
    h ^= val;
    h *= 1099511628211ULL;
    return h;
}

uint64_t finnbrowser_content_key(const FinnBrowserApp *app) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a_step(h, app->revision);
    h = fnv1a_step(h, (uint64_t)app->vp_w);
    h = fnv1a_step(h, (uint64_t)app->vp_h);
    h = fnv1a_step(h, (uint64_t)app->active_tab_id);
    h = fnv1a_step(h, (uint64_t)app->address_focused);
    h = fnv1a_step(h, (uint64_t)app->find_open);
    h = fnv1a_step(h, (uint64_t)app->find_focused);
    const BrowserTab *t = get_active_tab_const(app);
    if (t) {
        h = fnv1a_step(h, (uint64_t)t->frame_seq);
        h = fnv1a_step(h, (uint64_t)t->loading);
        h = fnv1a_step(h, (uint64_t)t->can_back);
        h = fnv1a_step(h, (uint64_t)t->can_forward);
    }
    h = fnv1a_step(h, (uint64_t)app->engine_ready);
    h = fnv1a_step(h, (uint64_t)app->engine_unavailable);
    return h;
}

/* ---------------- socket lifecycle & line framing ---------------- */

static void finnbrowser_init(FinnBrowserApp *app) {
    memset(app, 0, sizeof(*app));
    app->listen_fd = -1;
    app->client_fd = -1;
    app->revision = 1;
    app->dirty = 1;

    (void)mkdir("/run/finn", 0755);
    (void)unlink(FINN_BROWSER_SOCK);

    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) return;

    int fl = fcntl(sfd, F_GETFL, 0);
    if (fl >= 0) (void)fcntl(sfd, F_SETFL, fl | O_NONBLOCK);
    int fdfl = fcntl(sfd, F_GETFD, 0);
    if (fdfl >= 0) (void)fcntl(sfd, F_SETFD, fdfl | FD_CLOEXEC);

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, FINN_BROWSER_SOCK, sizeof(sun.sun_path) - 1);

    if (bind(sfd, (struct sockaddr *)&sun, sizeof(sun)) == 0) {
        (void)chmod(FINN_BROWSER_SOCK, 0660);
        struct group *grp = getgrnam("finn");
        if (grp) {
            (void)chown(FINN_BROWSER_SOCK, (uid_t)-1, grp->gr_gid);
        } else {
            gid_t dgid = finnapp_desktop_gid();
            if ((int)dgid >= 0) (void)chown(FINN_BROWSER_SOCK, (uid_t)-1, dgid);
        }
        if (listen(sfd, 4) == 0) {
            app->listen_fd = sfd;
        } else {
            close(sfd);
        }
    } else {
        close(sfd);
    }
}

static void finnbrowser_step(FinnBrowserApp *app) {
    /* Accept any new engine connection on the provider socket */
    if (app->listen_fd >= 0) {
        struct sockaddr_un un;
        socklen_t un_len = sizeof(un);
        int cfd = accept(app->listen_fd, (struct sockaddr *)&un, &un_len);
        if (cfd >= 0) {
#ifdef SO_PEERCRED
            struct ucred cred;
            socklen_t cred_len = sizeof(cred);
            if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) == 0) {
                uid_t desk_uid = finnapp_desktop_uid();
                if (cred.uid != 0 && cred.uid != desk_uid) {
                    close(cfd);
                    cfd = -1;
                }
            }
#endif
            if (cfd >= 0) {
                if (app->client_fd >= 0) {
                    const char *busy = "ERR busy\n";
                    (void)write(cfd, busy, strlen(busy));
                    close(cfd);
                } else {
                    int cfl = fcntl(cfd, F_GETFL, 0);
                    if (cfl >= 0) (void)fcntl(cfd, F_SETFL, cfl | O_NONBLOCK);
                    int cfdfl = fcntl(cfd, F_GETFD, 0);
                    if (cfdfl >= 0) (void)fcntl(cfd, F_SETFD, cfdfl | FD_CLOEXEC);

                    app->client_fd = cfd;
                    app->rx_buf_len = 0;
                    app->rx_frame_rem = 0;
                    app->rx_frame_off = 0;
                    if (app->rx_staging) {
                        free(app->rx_staging);
                        app->rx_staging = NULL;
                    }
                    if (app->vp_w > 0 && app->vp_h > 0) {
                        int sm = (int)(app->last_scale * 1000.0f + 0.5f);
                        if (sm < FINN_BROWSER_SCALE_MIN_MILLI) sm = 1000;
                        if (sm > FINN_BROWSER_SCALE_MAX_MILLI) sm = 4000;
                        (void)send_cmd(app->client_fd, "VIEWPORT %d %d %d\n", app->vp_w, app->vp_h, sm);
                    }
                }
            }
        }
    }

    if (app->client_fd < 0) return;

    /* Read non-blockingly until pending socket data is drained */
    for (;;) {
        /* If waiting for raw frame pixels, read directly into rx_staging */
        if (app->rx_frame_rem > 0 && app->rx_staging) {
            ssize_t n = read(app->client_fd, (char *)app->rx_staging + app->rx_frame_off, app->rx_frame_rem);
            if (n > 0) {
                app->rx_frame_off += (size_t)n;
                app->rx_frame_rem -= (size_t)n;
                if (app->rx_frame_rem == 0) {
                    BrowserTab *tab = find_tab(app, app->rx_frame_tab);
                    if (!tab) tab = add_tab(app, app->rx_frame_tab);
                    if (tab) {
                        if (tab->pixels) free(tab->pixels);
                        tab->pixels = app->rx_staging;
                        tab->pixel_w = app->rx_frame_w;
                        tab->pixel_h = app->rx_frame_h;
                        tab->frame_seq++;
                    } else {
                        free(app->rx_staging);
                    }
                    app->rx_staging = NULL;
                    app->dirty = 1;
                    app->revision++;
                }
                continue;
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            } else {
                handle_disconnect(app);
                break;
            }
        }

        /* Read control lines into rx_buf */
        size_t space = sizeof(app->rx_buf) - app->rx_buf_len;
        if (space == 0) {
            /* Line overflow without newline: reset buffer to recover */
            app->rx_buf_len = 0;
            space = sizeof(app->rx_buf);
        }

        ssize_t n = read(app->client_fd, app->rx_buf + app->rx_buf_len, space);
        if (n > 0) {
            app->rx_buf_len += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            handle_disconnect(app);
            break;
        }

        /* Parse complete '\n'-terminated lines in rx_buf */
        while (app->rx_frame_rem == 0 && app->rx_buf_len > 0) {
            char *nl = memchr(app->rx_buf, '\n', app->rx_buf_len);
            if (!nl) break;

            *nl = '\0';
            char line[FINN_BROWSER_LINE_MAX];
            snprintf(line, sizeof(line), "%s", app->rx_buf);
            size_t consumed = (size_t)(nl - app->rx_buf) + 1;
            size_t rem_buf = app->rx_buf_len - consumed;

            /* Strip trailing carriage return if CRLF */
            size_t line_len = strlen(line);
            if (line_len > 0 && line[line_len - 1] == '\r') {
                line[--line_len] = '\0';
            }

            if (strncmp(line, "HELLO ", 6) == 0) {
                if (app->vp_w > 0 && app->vp_h > 0) {
                    int sm = (int)(app->last_scale * 1000.0f + 0.5f);
                    if (sm < FINN_BROWSER_SCALE_MIN_MILLI) sm = 1000;
                    if (sm > FINN_BROWSER_SCALE_MAX_MILLI) sm = 4000;
                    (void)send_cmd(app->client_fd, "VIEWPORT %d %d %d\n", app->vp_w, app->vp_h, sm);
                }
            } else if (strcmp(line, "READY") == 0) {
                app->engine_ready = 1;
                app->engine_unavailable = 0;
                app->status_msg[0] = '\0';
                app->dirty = 1;
                app->revision++;
            } else if (strncmp(line, "UNAVAILABLE", 11) == 0) {
                app->engine_unavailable = 1;
                app->engine_ready = 0;
                const char *r = line + 11;
                while (*r == ' ') r++;
                finn_browser_text_clean(app->unavailable_reason, sizeof(app->unavailable_reason), r);
                app->dirty = 1;
                app->revision++;
            } else if (strncmp(line, "STATUS ", 7) == 0 || strncmp(line, "ERROR ", 6) == 0) {
                const char *r = strchr(line, ' ');
                if (r) {
                    while (*r == ' ') r++;
                    if (strncmp(line, "ERROR ", 6) == 0) {
                        while (*r >= '0' && *r <= '9') r++;
                        while (*r == ' ') r++;
                    }
                    finn_browser_text_clean(app->status_msg, sizeof(app->status_msg), r);
                    app->dirty = 1;
                    app->revision++;
                }
            } else if (strncmp(line, "TAB ", 4) == 0) {
                int id = 0, active = 0, loading = 0, can_back = 0, can_forward = 0;
                char *p = line + 4;
                while (*p == ' ') p++;
                id = atoi(p);
                char *p1 = strchr(p, ' ');
                if (p1) {
                    while (*p1 == ' ') p1++;
                    active = atoi(p1);
                    char *p2 = strchr(p1, ' ');
                    if (p2) {
                        while (*p2 == ' ') p2++;
                        loading = atoi(p2);
                        char *p3 = strchr(p2, ' ');
                        if (p3) {
                            while (*p3 == ' ') p3++;
                            can_back = atoi(p3);
                            char *p4 = strchr(p3, ' ');
                            if (p4) {
                                while (*p4 == ' ') p4++;
                                can_forward = atoi(p4);
                                char *p5 = strchr(p4, ' ');
                                if (p5) {
                                    while (*p5 == ' ') p5++;
                                    char *tab_char = strchr(p5, '\t');
                                    const char *url_str = p5;
                                    const char *title_str = "";
                                    if (tab_char) {
                                        *tab_char = '\0';
                                        title_str = tab_char + 1;
                                    }
                                    BrowserTab *tab = find_tab(app, id);
                                    if (!tab) tab = add_tab(app, id);
                                    if (tab) {
                                        tab->active = active;
                                        tab->loading = loading;
                                        tab->can_back = can_back;
                                        tab->can_forward = can_forward;
                                        finn_browser_text_clean(tab->url, sizeof(tab->url), url_str);
                                        finn_browser_text_clean(tab->title, sizeof(tab->title), title_str);
                                        if (active) {
                                            app->active_tab_id = id;
                                            if (!app->address_focused) {
                                                snprintf(app->address_buf, sizeof(app->address_buf), "%s", tab->url);
                                                app->address_len = (int)strlen(app->address_buf);
                                            }
                                        }
                                        app->dirty = 1;
                                        app->revision++;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    BrowserTab *tab = find_tab(app, id);
                    if (!tab) (void)add_tab(app, id);
                    app->dirty = 1;
                    app->revision++;
                }
            } else if (strncmp(line, "TABCLOSED ", 10) == 0 || strncmp(line, "CLOSED ", 7) == 0) {
                int id = atoi(line + (*line == 'T' ? 10 : 7));
                remove_tab(app, id);
                app->dirty = 1;
                app->revision++;
            } else if (strncmp(line, "ACTIVE ", 7) == 0) {
                int id = atoi(line + 7);
                app->active_tab_id = id;
                BrowserTab *tab = find_tab(app, id);
                if (tab && !app->address_focused) {
                    snprintf(app->address_buf, sizeof(app->address_buf), "%s", tab->url);
                    app->address_len = (int)strlen(app->address_buf);
                }
                app->dirty = 1;
                app->revision++;
            } else if (strncmp(line, "URL ", 4) == 0) {
                int id = 0;
                char *p = line + 4;
                while (*p == ' ') p++;
                id = atoi(p);
                char *url_p = strchr(p, ' ');
                if (url_p) {
                    while (*url_p == ' ') url_p++;
                    BrowserTab *tab = find_tab(app, id);
                    if (!tab) tab = add_tab(app, id);
                    if (tab) {
                        finn_browser_text_clean(tab->url, sizeof(tab->url), url_p);
                        if (tab->id == app->active_tab_id && !app->address_focused) {
                            snprintf(app->address_buf, sizeof(app->address_buf), "%s", tab->url);
                            app->address_len = (int)strlen(app->address_buf);
                        }
                        app->dirty = 1;
                        app->revision++;
                    }
                }
            } else if (strncmp(line, "TITLE ", 6) == 0) {
                int id = 0;
                char *p = line + 6;
                while (*p == ' ') p++;
                id = atoi(p);
                char *title_p = strchr(p, ' ');
                if (title_p) {
                    while (*title_p == ' ') title_p++;
                    BrowserTab *tab = find_tab(app, id);
                    if (!tab) tab = add_tab(app, id);
                    if (tab) {
                        finn_browser_text_clean(tab->title, sizeof(tab->title), title_p);
                        app->dirty = 1;
                        app->revision++;
                    }
                }
            } else if (strncmp(line, "LOADING ", 8) == 0) {
                int id = 0, loading = 0;
                if (sscanf(line + 8, "%d %d", &id, &loading) == 2) {
                    BrowserTab *tab = find_tab(app, id);
                    if (tab) {
                        tab->loading = loading;
                        app->dirty = 1;
                        app->revision++;
                    }
                }
            } else if (strncmp(line, "HISTORY ", 8) == 0) {
                int id = 0, can_back = 0, can_forward = 0;
                if (sscanf(line + 8, "%d %d %d", &id, &can_back, &can_forward) == 3) {
                    BrowserTab *tab = find_tab(app, id);
                    if (tab) {
                        tab->can_back = can_back;
                        tab->can_forward = can_forward;
                        app->dirty = 1;
                        app->revision++;
                    }
                }
            } else if (strncmp(line, "FOUND ", 6) == 0) {
                int id = 0, a1 = 0, a2 = 0;
                if (sscanf(line + 6, "%d %d %d", &id, &a1, &a2) == 3) {
                    BrowserTab *tab = find_tab(app, id);
                    if (tab) {
                        tab->find_active = a1;
                        tab->find_total = a2;
                        app->dirty = 1;
                        app->revision++;
                    }
                }
            } else if (strncmp(line, "FRAME ", 6) == 0) {
                int tab_id = 0, fw = 0, fh = 0;
                if (finn_browser_frame_parse(line, &tab_id, &fw, &fh) == 0) {
                    size_t bytes = finn_browser_frame_bytes(fw, fh);
                    if (bytes > 0) {
                        app->rx_staging = malloc(bytes);
                        if (app->rx_staging) {
                            app->rx_frame_tab = tab_id;
                            app->rx_frame_w = fw;
                            app->rx_frame_h = fh;
                            app->rx_frame_rem = bytes;
                            app->rx_frame_off = 0;

                            /* Consume any binary bytes already trailing the newline */
                            size_t to_copy = rem_buf < bytes ? rem_buf : bytes;
                            if (to_copy > 0) {
                                memcpy(app->rx_staging, nl + 1, to_copy);
                                app->rx_frame_off += to_copy;
                                app->rx_frame_rem -= to_copy;
                                consumed += to_copy;
                                rem_buf -= to_copy;
                            }

                            if (app->rx_frame_rem == 0) {
                                BrowserTab *tab = find_tab(app, app->rx_frame_tab);
                                if (!tab) tab = add_tab(app, app->rx_frame_tab);
                                if (tab) {
                                    if (tab->pixels) free(tab->pixels);
                                    tab->pixels = app->rx_staging;
                                    tab->pixel_w = app->rx_frame_w;
                                    tab->pixel_h = app->rx_frame_h;
                                    tab->frame_seq++;
                                } else {
                                    free(app->rx_staging);
                                }
                                app->rx_staging = NULL;
                                app->dirty = 1;
                                app->revision++;
                            }
                        }
                    }
                }
            }

            /* Shift unparsed bytes forward in rx_buf */
            memmove(app->rx_buf, app->rx_buf + consumed, rem_buf);
            app->rx_buf_len = rem_buf;

            if (app->rx_frame_rem > 0) {
                /* Pausing line scanner while absorbing binary payload */
                break;
            }
        }
    }
}

static void finnbrowser_open(FinnBrowserApp *app) {
    app->address_focused = 0;
    app->find_focused = 0;
    app->status_msg[0] = '\0';
    if (app->client_fd < 0 && !app->engine_spawned) {
        char reply[128];
        (void)finn_call(FINN_CONTROL_SOCK, "app.launch browser", reply, sizeof(reply));
        app->engine_spawned = 1;
        app->launch_requested = 1;
    }
}

/* ---------------- fonts & drawing ---------------- */

static int load_fonts(float scale) {
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);

    char p[600];
    snprintf(p, sizeof(p), "%s/FixelDisplay-Medium.otf", g_font_dir);
    if (finn_font_load(&g_f_title, p, finnapp_font_px(scale, 14.0f)) != 0) {
        snprintf(p, sizeof(p), "%s/FixelDisplay-Regular.otf", g_font_dir);
        if (finn_font_load(&g_f_title, p, finnapp_font_px(scale, 14.0f)) != 0) return -1;
    }
    snprintf(p, sizeof(p), "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_label, p, finnapp_font_px(scale, 12.0f)) != 0) return -1;
    if (finn_font_load(&g_f_sub,   p, finnapp_font_px(scale, 10.0f)) != 0) return -1;
    return 0;
}

static void finnbrowser_draw(FinnBrowserApp *app, uint32_t *layer, int lw, int lh,
                             int lx0, int ly0, int wx, int wy, int ww, int wh,
                             float scale, const finn_state *st, int focused) {
    (void)st;
    (void)focused;
    if (!g_ready) return;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }
    app->last_scale = scale;

    float s = scale;
    float ox = (float)(wx - lx0);
    float oy = (float)(wy - ly0);
#define LX(v) ((v) + ox)
#define LY(v) ((v) + oy)

    float win_w = (float)ww / s;
    int dark = finn_theme_dark();
    uint32_t ink = FINN_INK;
    uint32_t mute = FINN_MUTE;
    uint32_t card_col = dark ? 0xFF2A2D33u : 0xFFFFFFFFu;
    uint32_t input_bg = dark ? 0x2AFFFFFFu : 0xFFFFFFFFu;
    uint32_t tab_inactive_bg = dark ? 0x14FFFFFFu : 0x0A000000u;
    uint32_t btn_enabled_bg = dark ? 0x1AFFFFFFu : 0x12000000u;
    uint32_t btn_disabled_bg = 0x00000000u;
    uint32_t btn_disabled_ink = dark ? 0x33FFFFFFu : 0x33000000u;
    uint32_t accent = FINN_ON;

    float chrome_h = app->find_open ? 124.0f : 88.0f;
    BrowserTab *active_tab = get_active_tab(app);

    /* Toolbar shelf and dividing hairlines */
    finn_fill_rrect(layer, lw, lh, LX(0.0f), LY(s * 44.0f), (float)ww, s * (chrome_h - 44.0f), 0.0f,
                    dark ? 0x4D1E2128u : 0x66FFFFFFu);
    finn_fill_rrect(layer, lw, lh, LX(0.0f), LY(s * 44.0f), (float)ww, 1.0f, 0.0f, FINN_HAIR);
    finn_fill_rrect(layer, lw, lh, LX(0.0f), LY(s * chrome_h), (float)ww, 1.0f, 0.0f, FINN_HAIR);

    /* ---------------- Row 1: Tab Strip (y = 0..44) ---------------- */
    float avail_w = win_w - 14.0f - 116.0f - 34.0f;
    int ntabs = app->tab_count;
    float tab_w = 160.0f;
    if (ntabs > 0) {
        tab_w = avail_w / (float)ntabs;
        if (tab_w > 180.0f) tab_w = 180.0f;
        if (tab_w < 90.0f) tab_w = 90.0f;
    }

    for (int i = 0; i < ntabs; i++) {
        BrowserTab *tab = &app->tabs[i];
        int is_active = (tab->id == app->active_tab_id);
        float tx = 14.0f + i * (tab_w + 4.0f);
        float ty = 7.0f;
        float th = 30.0f;
        float tr = 8.0f;

        finn_fill_rrect(layer, lw, lh, LX(s * tx), LY(s * ty), s * tab_w, s * th, s * tr,
                        is_active ? card_col : tab_inactive_bg);
        if (is_active) {
            finn_stroke_rrect(layer, lw, lh, LX(s * tx), LY(s * ty), s * tab_w, s * th, s * tr, 1.0f, FINN_HAIR);
        }

        const char *t_title = tab->title[0] ? tab->title : (tab->loading ? "Loading…" : (tab->url[0] ? tab->url : "New Tab"));
        float text_max_w = (ntabs > 1) ? (tab_w - 38.0f) : (tab_w - 16.0f);
        char ellip_title[128];
        finnapp_ellipsis(&g_f_label, ellip_title, sizeof(ellip_title), t_title, s * text_max_w);
        float cap_h = finn_text_cap_height(&g_f_label);
        float text_base = ty + th * 0.5f + cap_h * 0.5f;
        finn_text_draw(&g_f_label, layer, lw, lh, LX(s * (tx + 10.0f)), LY(s * text_base),
                       ellip_title, is_active ? ink : mute);

        if (ntabs > 1) {
            float cx = tx + tab_w - 20.0f;
            float cy = ty + th * 0.5f;
            draw_cross(layer, lw, lh, LX(s * cx), LY(s * cy), s * 8.0f, 1.5f, mute);
        }
    }

    /* New Tab '+' button */
    float plus_x = 14.0f + ntabs * (tab_w + 4.0f);
    float plus_y = 8.0f;
    float plus_sz = 28.0f;
    int can_new = (app->engine_ready && ntabs < FINN_BROWSER_TAB_MAX);
    finn_fill_rrect(layer, lw, lh, LX(s * plus_x), LY(s * plus_y), s * plus_sz, s * plus_sz, s * 8.0f,
                    can_new ? btn_enabled_bg : btn_disabled_bg);
    draw_plus(layer, lw, lh, LX(s * (plus_x + plus_sz * 0.5f)), LY(s * (plus_y + plus_sz * 0.5f)),
              s * 10.0f, 1.5f, can_new ? ink : btn_disabled_ink);

    /* ---------------- Row 2: Navigation & Address Bar (y = 44..88) ---------------- */
    float bx = 14.0f, by = 50.0f, bsz = 32.0f, br = 8.0f;
    int can_back = (active_tab && active_tab->can_back && app->engine_ready);
    finn_fill_rrect(layer, lw, lh, LX(s * bx), LY(s * by), s * bsz, s * bsz, s * br,
                    can_back ? btn_enabled_bg : btn_disabled_bg);
    draw_chevron_left(layer, lw, lh, LX(s * (bx + bsz * 0.5f)), LY(s * (by + bsz * 0.5f)),
                      s * 10.0f, 1.8f, can_back ? ink : btn_disabled_ink);

    float fx = 52.0f;
    int can_fwd = (active_tab && active_tab->can_forward && app->engine_ready);
    finn_fill_rrect(layer, lw, lh, LX(s * fx), LY(s * by), s * bsz, s * bsz, s * br,
                    can_fwd ? btn_enabled_bg : btn_disabled_bg);
    draw_chevron_right(layer, lw, lh, LX(s * (fx + bsz * 0.5f)), LY(s * (by + bsz * 0.5f)),
                       s * 10.0f, 1.8f, can_fwd ? ink : btn_disabled_ink);

    float rx = 90.0f;
    int can_rel = (active_tab && app->engine_ready);
    finn_fill_rrect(layer, lw, lh, LX(s * rx), LY(s * by), s * bsz, s * bsz, s * br,
                    can_rel ? btn_enabled_bg : btn_disabled_bg);
    if (active_tab && active_tab->loading) {
        draw_cross(layer, lw, lh, LX(s * (rx + bsz * 0.5f)), LY(s * (by + bsz * 0.5f)),
                   s * 10.0f, 1.8f, can_rel ? ink : btn_disabled_ink);
    } else {
        draw_reload(layer, lw, lh, LX(s * (rx + bsz * 0.5f)), LY(s * (by + bsz * 0.5f)),
                    s * 12.0f, 1.8f, can_rel ? ink : btn_disabled_ink);
    }

    /* Address Bar */
    float ax = 132.0f, ay = 50.0f, aw = win_w - 54.0f - 132.0f, ah = 32.0f, ar = 8.0f;
    finn_fill_rrect(layer, lw, lh, LX(s * ax), LY(s * ay), s * aw, s * ah, s * ar, input_bg);
    if (app->address_focused) {
        finn_stroke_rrect(layer, lw, lh, LX(s * ax), LY(s * ay), s * aw, s * ah, s * ar, 2.0f, accent);
    } else {
        finn_stroke_rrect(layer, lw, lh, LX(s * ax), LY(s * ay), s * aw, s * ah, s * ar, 1.0f, FINN_HAIR);
    }

    float cap_l = finn_text_cap_height(&g_f_label);
    float aty = ay + ah * 0.5f + cap_l * 0.5f;
    float atx = ax + 12.0f;
    if (app->address_buf[0] != '\0') {
        char ellip_addr[FINN_BROWSER_URL_MAX];
        finnapp_ellipsis(&g_f_label, ellip_addr, sizeof(ellip_addr), app->address_buf, s * (aw - 24.0f));
        finn_text_draw(&g_f_label, layer, lw, lh, LX(s * atx), LY(s * aty), ellip_addr, ink);
        if (app->address_focused) {
            float tw = finn_text_width(&g_f_label, ellip_addr);
            if (tw > s * (aw - 26.0f)) tw = s * (aw - 26.0f);
            finn_fill_rrect(layer, lw, lh, LX(s * atx + tw + 2.0f), LY(s * (ay + 7.0f)), 1.5f, s * 18.0f, 0.5f, accent);
        }
    } else {
        finn_text_draw(&g_f_label, layer, lw, lh, LX(s * atx), LY(s * aty), "Enter https:// address…", mute);
        if (app->address_focused) {
            finn_fill_rrect(layer, lw, lh, LX(s * atx), LY(s * (ay + 7.0f)), 1.5f, s * 18.0f, 0.5f, accent);
        }
    }

    /* Accent progress bar on loading */
    if (active_tab && active_tab->loading) {
        finn_fill_rrect(layer, lw, lh, LX(s * (ax + 4.0f)), LY(s * (ay + ah - 3.0f)),
                        s * (aw - 8.0f) * 0.65f, s * 2.0f, s * 1.0f, accent);
    }

    /* Find Toggle Button */
    float find_x = win_w - 46.0f;
    int can_find = (app->engine_ready && active_tab != NULL);
    uint32_t f_bg = app->find_open ? accent : (can_find ? btn_enabled_bg : btn_disabled_bg);
    uint32_t f_fg = app->find_open ? 0xFFFFFFFFu : (can_find ? ink : btn_disabled_ink);
    finn_fill_rrect(layer, lw, lh, LX(s * find_x), LY(s * by), s * bsz, s * bsz, s * br, f_bg);
    draw_magnifier(layer, lw, lh, LX(s * (find_x + bsz * 0.5f)), LY(s * (by + bsz * 0.5f)), s * 12.0f, 1.8f, f_fg);

    /* ---------------- Row 3: Find Bar (y = 88..124) ---------------- */
    if (app->find_open) {
        float fy = 88.0f, fh = 36.0f;
        finn_fill_rrect(layer, lw, lh, LX(0.0f), LY(s * fy), (float)ww, s * fh, 0.0f,
                        dark ? 0x24FFFFFFu : 0x0A000000u);
        finn_fill_rrect(layer, lw, lh, LX(0.0f), LY(s * fy), (float)ww, 1.0f, 0.0f, FINN_HAIR);

        float fi_x = 14.0f, fi_y = 92.0f, fi_w = 240.0f, fi_h = 28.0f, fi_r = 6.0f;
        finn_fill_rrect(layer, lw, lh, LX(s * fi_x), LY(s * fi_y), s * fi_w, s * fi_h, s * fi_r, input_bg);
        if (app->find_focused) {
            finn_stroke_rrect(layer, lw, lh, LX(s * fi_x), LY(s * fi_y), s * fi_w, s * fi_h, s * fi_r, 1.5f, accent);
        } else {
            finn_stroke_rrect(layer, lw, lh, LX(s * fi_x), LY(s * fi_y), s * fi_w, s * fi_h, s * fi_r, 1.0f, FINN_HAIR);
        }

        float fty = fi_y + fi_h * 0.5f + cap_l * 0.5f;
        float ftx = fi_x + 10.0f;
        if (app->find_buf[0] != '\0') {
            char ellip_find[FINN_BROWSER_FIND_MAX];
            finnapp_ellipsis(&g_f_label, ellip_find, sizeof(ellip_find), app->find_buf, s * (fi_w - 20.0f));
            finn_text_draw(&g_f_label, layer, lw, lh, LX(s * ftx), LY(s * fty), ellip_find, ink);
            if (app->find_focused) {
                float tw = finn_text_width(&g_f_label, ellip_find);
                finn_fill_rrect(layer, lw, lh, LX(s * ftx + tw + 2.0f), LY(s * (fi_y + 5.0f)), 1.5f, s * 18.0f, 0.5f, accent);
            }
        } else {
            finn_text_draw(&g_f_label, layer, lw, lh, LX(s * ftx), LY(s * fty), "Find in page…", mute);
            if (app->find_focused) {
                finn_fill_rrect(layer, lw, lh, LX(s * ftx), LY(s * (fi_y + 5.0f)), 1.5f, s * 18.0f, 0.5f, accent);
            }
        }

        float mc_x = 264.0f;
        if (app->find_buf[0] != '\0') {
            char mcount[64];
            if (active_tab && active_tab->find_total > 0) {
                snprintf(mcount, sizeof(mcount), "%d / %d", active_tab->find_active, active_tab->find_total);
            } else {
                snprintf(mcount, sizeof(mcount), "0 matches");
            }
            finn_text_draw(&g_f_sub, layer, lw, lh, LX(s * mc_x), LY(s * fty), mcount, mute);
        }

        float fbtn_sz = 26.0f, fbtn_r = 6.0f, fbtn_y = 93.0f;
        float prev_x = 345.0f;
        float next_x = 377.0f;
        float close_x = 415.0f;
        int can_nav = (app->find_buf[0] != '\0' && app->engine_ready);

        finn_fill_rrect(layer, lw, lh, LX(s * prev_x), LY(s * fbtn_y), s * fbtn_sz, s * fbtn_sz, s * fbtn_r,
                        can_nav ? btn_enabled_bg : btn_disabled_bg);
        draw_chevron_left(layer, lw, lh, LX(s * (prev_x + fbtn_sz * 0.5f)), LY(s * (fbtn_y + fbtn_sz * 0.5f)),
                          s * 8.0f, 1.5f, can_nav ? ink : btn_disabled_ink);

        finn_fill_rrect(layer, lw, lh, LX(s * next_x), LY(s * fbtn_y), s * fbtn_sz, s * fbtn_sz, s * fbtn_r,
                        can_nav ? btn_enabled_bg : btn_disabled_bg);
        draw_chevron_right(layer, lw, lh, LX(s * (next_x + fbtn_sz * 0.5f)), LY(s * (fbtn_y + fbtn_sz * 0.5f)),
                           s * 8.0f, 1.5f, can_nav ? ink : btn_disabled_ink);

        finn_fill_rrect(layer, lw, lh, LX(s * close_x), LY(s * fbtn_y), s * fbtn_sz, s * fbtn_sz, s * fbtn_r,
                        btn_enabled_bg);
        draw_cross(layer, lw, lh, LX(s * (close_x + fbtn_sz * 0.5f)), LY(s * (fbtn_y + fbtn_sz * 0.5f)),
                   s * 8.0f, 1.5f, ink);
    }

    /* ---------------- Viewport Area (y = chrome_h..win_h) ---------------- */
    int vp_y = (int)lroundf(chrome_h * scale);
    int vp_w = ww;
    int vp_h = wh - vp_y;
    if (vp_h < 0) vp_h = 0;

    if (vp_w != app->vp_w || vp_h != app->vp_h) {
        app->vp_w = vp_w;
        app->vp_h = vp_h;
        if (app->client_fd >= 0) {
            int sm = (int)(scale * 1000.0f + 0.5f);
            if (sm < FINN_BROWSER_SCALE_MIN_MILLI) sm = 1000;
            if (sm > FINN_BROWSER_SCALE_MAX_MILLI) sm = 4000;
            (void)send_cmd(app->client_fd, "VIEWPORT %d %d %d\n", vp_w, vp_h, sm);
        }
    }

    int corner_r = (int)lroundf(14.0f * scale);
    if (active_tab && active_tab->pixels && active_tab->pixel_w > 0 && active_tab->pixel_h > 0) {
        /* Blank any uncovered border canvas before blitting freshly resized page */
        if (active_tab->pixel_w < vp_w || active_tab->pixel_h < vp_h) {
            for (int r = 0; r < vp_h; r++) {
                int ty = (int)LY(vp_y + r);
                if (ty >= 0 && ty < lh) {
                    uint32_t *drow = layer + (size_t)ty * lw;
                    for (int c = 0; c < vp_w && (int)LX(c) < lw; c++) {
                        drow[(int)LX(c)] = 0xFFFFFFFFu;
                    }
                }
            }
        }
        finn_blit_opaque_rounded_bottom(layer, lw, lh,
                                        active_tab->pixels, active_tab->pixel_w, active_tab->pixel_h,
                                        (int)LX(0), (int)LY(vp_y), corner_r);
    } else {
        /* Clean native empty state: paper canvas with window's bottom round corners */
        uint32_t paper_col = dark ? 0xFF1E2128u : 0xFFFFFFFFu;
        finn_fill_rrect(layer, lw, lh, LX(0.0f), LY((float)vp_y), (float)vp_w, (float)vp_h, 0.0f, paper_col);
        finn_fill_rrect(layer, lw, lh, LX(0.0f), LY((float)(wh - corner_r)), (float)vp_w, (float)corner_r,
                        (float)corner_r, paper_col);

        float mid_x = (float)ww * 0.5f;
        float mid_y = (float)vp_y + (float)vp_h * 0.5f;

        if (app->engine_unavailable) {
            const char *title = "Browser engine unavailable";
            const char *sub = app->unavailable_reason[0] ? app->unavailable_reason
                                                         : "Chromium engine is not installed";
            float tw = finn_text_width(&g_f_title, title);
            float ch = finn_text_cap_height(&g_f_title);
            finn_text_draw(&g_f_title, layer, lw, lh, LX(mid_x - tw * 0.5f), LY(mid_y - ch), title, ink);
            float sw = finn_text_width(&g_f_sub, sub);
            float sch = finn_text_cap_height(&g_f_sub);
            finn_text_draw(&g_f_sub, layer, lw, lh, LX(mid_x - sw * 0.5f), LY(mid_y + sch * 1.5f), sub, mute);
        } else if (!app->engine_ready) {
            const char *msg = "Starting page engine…";
            float tw = finn_text_width(&g_f_label, msg);
            finn_text_draw(&g_f_label, layer, lw, lh, LX(mid_x - tw * 0.5f), LY(mid_y), msg, mute);
        } else {
            const char *msg = (active_tab && active_tab->loading) ? "Loading page…"
                                                                  : "Enter a web address above";
            float tw = finn_text_width(&g_f_label, msg);
            finn_text_draw(&g_f_label, layer, lw, lh, LX(mid_x - tw * 0.5f), LY(mid_y), msg, mute);
        }
    }

    /* Status banner pill floating beneath toolbar when a status is active */
    if (app->status_msg[0] != '\0') {
        float pill_h = 24.0f;
        float pill_y = chrome_h + 8.0f;
        float sw = finn_text_width(&g_f_sub, app->status_msg);
        float pill_w = sw / s + 24.0f;
        if (pill_w > win_w - 28.0f) pill_w = win_w - 28.0f;
        float pill_x = (win_w - pill_w) * 0.5f;

        finn_fill_rrect(layer, lw, lh, LX(s * pill_x), LY(s * pill_y), s * pill_w, s * pill_h, s * 12.0f,
                        dark ? 0xEB2A2D33u : 0xF5FFFFFFu);
        finn_stroke_rrect(layer, lw, lh, LX(s * pill_x), LY(s * pill_y), s * pill_w, s * pill_h, s * 12.0f, 1.0f, FINN_HAIR);
        float scap_h = finn_text_cap_height(&g_f_sub);
        finn_text_draw(&g_f_sub, layer, lw, lh, LX(s * (pill_x + 12.0f)),
                       LY(s * (pill_y + pill_h * 0.5f + scap_h * 0.5f)), app->status_msg, ink);
    }

#undef LX
#undef LY
}

/* ---------------- hit testing & event handling ---------------- */

int finnbrowserapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                       float scale, int *out_sub) {
    FinnBrowserApp *app = &g_browser_app;
    float s = scale > 0.01f ? scale : 1.0f;
    float rx = (float)(px - wx) / s;
    float ry = (float)(py - wy) / s;
    float win_w = (float)ww / s;
    float win_h = (float)wh / s;

    if (rx < 0.0f || rx >= win_w || ry < 0.0f || ry >= win_h) {
        return FINNBROWSER_HIT_NONE;
    }

    float chrome_h = app->find_open ? 124.0f : 88.0f;
    if (ry >= chrome_h) {
        if (out_sub) *out_sub = 0;
        return FINNBROWSER_HIT_VIEWPORT;
    }

    /* Tab strip: y = 0..44 */
    if (ry < 44.0f) {
        float avail_w = win_w - 14.0f - 116.0f - 34.0f;
        int ntabs = app->tab_count;
        float tab_w = 160.0f;
        if (ntabs > 0) {
            tab_w = avail_w / (float)ntabs;
            if (tab_w > 180.0f) tab_w = 180.0f;
            if (tab_w < 90.0f) tab_w = 90.0f;
        }

        for (int i = 0; i < ntabs; i++) {
            float tx = 14.0f + i * (tab_w + 4.0f);
            if (ry >= 7.0f && ry <= 37.0f && rx >= tx && rx <= tx + tab_w) {
                if (ntabs > 1 && rx >= tx + tab_w - 24.0f && rx <= tx + tab_w - 4.0f) {
                    if (out_sub) *out_sub = i;
                    return FINNBROWSER_HIT_TAB_CLOSE;
                }
                if (out_sub) *out_sub = i;
                return FINNBROWSER_HIT_TAB;
            }
        }

        float plus_x = 14.0f + ntabs * (tab_w + 4.0f);
        if (rx >= plus_x && rx <= plus_x + 28.0f && ry >= 8.0f && ry <= 36.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_TAB_NEW;
        }
        return FINNBROWSER_HIT_NONE;
    }

    /* Navigation row: y = 44..88 */
    if (ry >= 44.0f && ry < 88.0f) {
        if (rx >= 14.0f && rx <= 46.0f && ry >= 50.0f && ry <= 82.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_BACK;
        }
        if (rx >= 52.0f && rx <= 84.0f && ry >= 50.0f && ry <= 82.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_FORWARD;
        }
        if (rx >= 90.0f && rx <= 122.0f && ry >= 50.0f && ry <= 82.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_RELOAD;
        }
        if (rx >= win_w - 46.0f && rx <= win_w - 14.0f && ry >= 50.0f && ry <= 82.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_FIND_TOGGLE;
        }
        if (rx >= 132.0f && rx <= win_w - 54.0f && ry >= 50.0f && ry <= 82.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_ADDRESS;
        }
        return FINNBROWSER_HIT_NONE;
    }

    /* Find row: y = 88..124 */
    if (app->find_open && ry >= 88.0f && ry < 124.0f) {
        if (rx >= 14.0f && rx <= 254.0f && ry >= 92.0f && ry <= 120.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_FIND_INPUT;
        }
        if (rx >= 345.0f && rx <= 371.0f && ry >= 93.0f && ry <= 119.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_FIND_PREV;
        }
        if (rx >= 377.0f && rx <= 403.0f && ry >= 93.0f && ry <= 119.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_FIND_NEXT;
        }
        if (rx >= 415.0f && rx <= 441.0f && ry >= 93.0f && ry <= 119.0f) {
            if (out_sub) *out_sub = 0;
            return FINNBROWSER_HIT_FIND_CLOSE;
        }
    }

    return FINNBROWSER_HIT_NONE;
}

static int finnbrowser_press(FinnBrowserApp *app, int hit, int sub,
                             int wx, int wy, int ww, int wh, int px, int py, float scale) {
    (void)ww;
    (void)wh;
    BrowserTab *act = get_active_tab(app);

    switch (hit) {
    case FINNBROWSER_HIT_TAB: {
        if (sub >= 0 && sub < app->tab_count) {
            app->active_tab_id = app->tabs[sub].id;
            if (!app->address_focused) {
                snprintf(app->address_buf, sizeof(app->address_buf), "%s", app->tabs[sub].url);
                app->address_len = (int)strlen(app->address_buf);
            }
            (void)send_cmd(app->client_fd, "ACTIVATE %d\n", app->tabs[sub].id);
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_TAB_CLOSE: {
        if (app->tab_count > 1 && sub >= 0 && sub < app->tab_count) {
            int tid = app->tabs[sub].id;
            (void)send_cmd(app->client_fd, "CLOSE %d\n", tid);
            remove_tab(app, tid);
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_TAB_NEW: {
        if (app->engine_ready && app->tab_count < FINN_BROWSER_TAB_MAX) {
            (void)send_cmd(app->client_fd, "NEW https://example.com/\n");
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_BACK: {
        if (act && act->can_back && app->engine_ready) {
            (void)send_cmd(app->client_fd, "BACK %d\n", act->id);
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_FORWARD: {
        if (act && act->can_forward && app->engine_ready) {
            (void)send_cmd(app->client_fd, "FORWARD %d\n", act->id);
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_RELOAD: {
        if (act && app->engine_ready) {
            if (act->loading) {
                (void)send_cmd(app->client_fd, "STOP %d\n", act->id);
            } else {
                (void)send_cmd(app->client_fd, "RELOAD %d\n", act->id);
            }
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_ADDRESS: {
        app->address_focused = 1;
        app->find_focused = 0;
        app->viewport_focused = 0;
        app->status_msg[0] = '\0';
        app->dirty = 1;
        app->revision++;
        return 1;
    }
    case FINNBROWSER_HIT_FIND_TOGGLE: {
        if (!app->engine_ready || !act) return 0;
        app->find_open = !app->find_open;
        if (app->find_open) {
            app->find_focused = 1;
            app->address_focused = 0;
            app->viewport_focused = 0;
        } else {
            app->find_focused = 0;
            (void)send_cmd(app->client_fd, "FINDEND %d\n", act->id);
        }
        app->dirty = 1;
        app->revision++;
        return 1;
    }
    case FINNBROWSER_HIT_FIND_INPUT: {
        app->find_focused = 1;
        app->address_focused = 0;
        app->viewport_focused = 0;
        app->dirty = 1;
        app->revision++;
        return 1;
    }
    case FINNBROWSER_HIT_FIND_PREV: {
        if (act && app->engine_ready && app->find_buf[0] != '\0') {
            (void)send_cmd(app->client_fd, "FIND %d 0 0 %s\n", act->id, app->find_buf);
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_FIND_NEXT: {
        if (act && app->engine_ready && app->find_buf[0] != '\0') {
            (void)send_cmd(app->client_fd, "FIND %d 1 0 %s\n", act->id, app->find_buf);
            return 1;
        }
        return 0;
    }
    case FINNBROWSER_HIT_FIND_CLOSE: {
        app->find_open = 0;
        app->find_focused = 0;
        if (act) (void)send_cmd(app->client_fd, "FINDEND %d\n", act->id);
        app->dirty = 1;
        app->revision++;
        return 1;
    }
    case FINNBROWSER_HIT_VIEWPORT: {
        app->viewport_focused = 1;
        app->address_focused = 0;
        app->find_focused = 0;
        app->pointer_in_viewport = 1;
        app->pointer_buttons |= 1;

        float chrome_h = app->find_open ? 124.0f : 88.0f;
        float s = scale > 0.01f ? scale : 1.0f;
        int rx = px - wx;
        int ry = py - wy;
        int vpy = (int)lroundf(chrome_h * s);
        int cx = rx;
        int cy = ry - vpy;

        (void)send_cmd(app->client_fd, "MOUSE down %d %d left 1 1\n", cx, cy);
        app->dirty = 1;
        app->revision++;
        return 1;
    }
    default:
        break;
    }
    return 0;
}

static int finnbrowser_motion(FinnBrowserApp *app, int wx, int wy, int ww, int wh,
                              int px, int py, float scale) {
    float chrome_h = app->find_open ? 124.0f : 88.0f;
    float s = scale > 0.01f ? scale : 1.0f;
    int rx = px - wx;
    int ry = py - wy;
    int vpy = (int)lroundf(chrome_h * s);

    if (rx >= 0 && rx < ww && ry >= vpy && ry < wh) {
        app->pointer_in_viewport = 1;
        int cx = rx;
        int cy = ry - vpy;
        const char *btn = (app->pointer_buttons & 1) ? "left" : "none";
        (void)send_cmd(app->client_fd, "MOUSE move %d %d %s %d 0\n", cx, cy, btn, app->pointer_buttons);
        return 1;
    } else {
        app->pointer_in_viewport = 0;
    }
    return 0;
}

static void finnbrowser_release(FinnBrowserApp *app, int wx, int wy, int ww, int wh,
                                int px, int py, float scale) {
    float chrome_h = app->find_open ? 124.0f : 88.0f;
    float s = scale > 0.01f ? scale : 1.0f;
    int rx = px - wx;
    int ry = py - wy;
    int vpy = (int)lroundf(chrome_h * s);

    app->pointer_buttons = 0;
    if (app->pointer_in_viewport || (rx >= 0 && rx < ww && ry >= vpy && ry < wh)) {
        int cx = rx;
        int cy = ry - vpy;
        (void)send_cmd(app->client_fd, "MOUSE up %d %d left 0 1\n", cx, cy);
    }
}

static int finnbrowser_scroll(FinnBrowserApp *app, int clicks, int wx, int wy, int ww, int wh,
                              int px, int py, float scale) {
    float chrome_h = app->find_open ? 124.0f : 88.0f;
    float s = scale > 0.01f ? scale : 1.0f;
    int rx = px - wx;
    int ry = py - wy;
    int vpy = (int)lroundf(chrome_h * s);

    if (rx >= 0 && rx < ww && ry >= vpy && ry < wh) {
        int cx = rx;
        int cy = ry - vpy;
        int pdy = clicks * 60;
        (void)send_cmd(app->client_fd, "WHEEL %d %d 0 %d\n", cx, cy, pdy);
        return 1;
    }
    return 0;
}

struct SpecialKey {
    int evdev_code;
    int vk;
    const char *key;
    const char *code;
};

static const struct SpecialKey kSpecialKeys[] = {
    { KEY_BACKSPACE, 8,   "Backspace", "Backspace" },
    { KEY_TAB,       9,   "Tab",       "Tab" },
    { KEY_ENTER,     13,  "Enter",     "Enter" },
    { KEY_KPENTER,   13,  "Enter",     "NumpadEnter" },
    { KEY_ESC,       27,  "Escape",    "Escape" },
    { KEY_SPACE,     32,  " ",         "Space" },
    { KEY_PAGEUP,    33,  "PageUp",    "PageUp" },
    { KEY_PAGEDOWN,  34,  "PageDown",  "PageDown" },
    { KEY_END,       35,  "End",       "End" },
    { KEY_HOME,      36,  "Home",      "Home" },
    { KEY_LEFT,      37,  "ArrowLeft", "ArrowLeft" },
    { KEY_UP,        38,  "ArrowUp",   "ArrowUp" },
    { KEY_RIGHT,     39,  "ArrowRight","ArrowRight" },
    { KEY_DOWN,      40,  "ArrowDown", "ArrowDown" },
    { KEY_DELETE,    46,  "Delete",    "Delete" },
};

static int finnbrowser_key(FinnBrowserApp *app, const finn_key_event *e) {
    if (!e->pressed) return 0;
    BrowserTab *act = get_active_tab(app);

    /* Global accelerator shortcuts */
    if (e->mods & FINN_MOD_CTRL) {
        if (e->code == KEY_F) {
            app->find_open = 1;
            app->find_focused = 1;
            app->address_focused = 0;
            app->viewport_focused = 0;
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        if (e->code == KEY_L) {
            app->address_focused = 1;
            app->find_focused = 0;
            app->viewport_focused = 0;
            app->status_msg[0] = '\0';
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        if (e->code == KEY_T) {
            if (app->engine_ready && app->tab_count < FINN_BROWSER_TAB_MAX) {
                (void)send_cmd(app->client_fd, "NEW https://example.com/\n");
                app->dirty = 1;
                app->revision++;
                return 1;
            }
            return 0;
        }
        if (e->code == KEY_W) {
            if (app->tab_count > 1 && act) {
                (void)send_cmd(app->client_fd, "CLOSE %d\n", act->id);
                remove_tab(app, act->id);
                app->dirty = 1;
                app->revision++;
                return 1;
            }
            return 0;
        }
        if (e->code == KEY_R) {
            if (act && app->engine_ready) {
                (void)send_cmd(app->client_fd, "RELOAD %d\n", act->id);
                app->dirty = 1;
                app->revision++;
                return 1;
            }
            return 0;
        }
    }
    if (e->code == KEY_F5 && act && app->engine_ready) {
        (void)send_cmd(app->client_fd, "RELOAD %d\n", act->id);
        app->dirty = 1;
        app->revision++;
        return 1;
    }

    /* Address Bar key events */
    if (app->address_focused) {
        if (e->code == KEY_ESC) {
            app->address_focused = 0;
            app->status_msg[0] = '\0';
            if (act) {
                snprintf(app->address_buf, sizeof(app->address_buf), "%s", act->url);
                app->address_len = (int)strlen(app->address_buf);
            }
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        if ((e->mods & FINN_MOD_CTRL) && (e->code == KEY_BACKSPACE || e->code == KEY_U)) {
            app->address_buf[0] = '\0';
            app->address_len = 0;
            app->status_msg[0] = '\0';
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        if (e->code == KEY_BACKSPACE) {
            if (app->address_len > 0) {
                app->address_buf[--app->address_len] = '\0';
                while (app->address_len > 0 &&
                       ((unsigned char)app->address_buf[app->address_len] & 0xC0) == 0x80) {
                    app->address_buf[--app->address_len] = '\0';
                }
            }
            app->status_msg[0] = '\0';
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
            char norm[FINN_BROWSER_URL_MAX];
            int res = finn_browser_address(app->address_buf, norm, sizeof(norm));
            if (res == FINN_BROWSER_ADDR_URL) {
                snprintf(app->address_buf, sizeof(app->address_buf), "%s", norm);
                app->address_len = (int)strlen(app->address_buf);
                app->address_focused = 0;
                app->viewport_focused = 1;
                app->status_msg[0] = '\0';
                if (act && app->engine_ready) {
                    (void)send_cmd(app->client_fd, "NAVIGATE %d %s\n", act->id, norm);
                }
            } else if (res == FINN_BROWSER_ADDR_NOT_URL) {
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Enter a domain or full URL (no search engine configured)");
            } else {
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Only http://, https://, and about:blank addresses are supported");
            }
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        if (e->keysym >= 0x20 && e->keysym <= 0x7E) {
            if ((size_t)app->address_len + 1 < sizeof(app->address_buf)) {
                app->address_buf[app->address_len++] = (char)e->keysym;
                app->address_buf[app->address_len] = '\0';
                app->status_msg[0] = '\0';
                app->dirty = 1;
                app->revision++;
                return 1;
            }
        }
        return 0;
    }

    /* Find Bar key events */
    if (app->find_focused) {
        if (e->code == KEY_ESC) {
            app->find_open = 0;
            app->find_focused = 0;
            if (act) (void)send_cmd(app->client_fd, "FINDEND %d\n", act->id);
            app->dirty = 1;
            app->revision++;
            return 1;
        }
        if (e->code == KEY_BACKSPACE) {
            if (app->find_len > 0) {
                app->find_buf[--app->find_len] = '\0';
                while (app->find_len > 0 &&
                       ((unsigned char)app->find_buf[app->find_len] & 0xC0) == 0x80) {
                    app->find_buf[--app->find_len] = '\0';
                }
                if (act && app->engine_ready) {
                    if (app->find_buf[0] != '\0') {
                        (void)send_cmd(app->client_fd, "FIND %d 1 1 %s\n", act->id, app->find_buf);
                    } else {
                        (void)send_cmd(app->client_fd, "FINDEND %d\n", act->id);
                    }
                }
                app->dirty = 1;
                app->revision++;
                return 1;
            }
            return 0;
        }
        if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
            if (act && app->engine_ready && app->find_buf[0] != '\0') {
                int forward = (e->mods & FINN_MOD_SHIFT) ? 0 : 1;
                (void)send_cmd(app->client_fd, "FIND %d %d 0 %s\n", act->id, forward, app->find_buf);
                return 1;
            }
            return 0;
        }
        if (e->keysym >= 0x20 && e->keysym <= 0x7E) {
            if ((size_t)app->find_len + 1 < sizeof(app->find_buf)) {
                app->find_buf[app->find_len++] = (char)e->keysym;
                app->find_buf[app->find_len] = '\0';
                if (act && app->engine_ready) {
                    (void)send_cmd(app->client_fd, "FIND %d 1 1 %s\n", act->id, app->find_buf);
                }
                app->dirty = 1;
                app->revision++;
                return 1;
            }
        }
        return 0;
    }

    /* Viewport web page key delivery */
    if (app->viewport_focused && app->engine_ready && act) {
        int cdp_mods = 0;
        if (e->mods & FINN_MOD_ALT) cdp_mods |= 1;
        if (e->mods & FINN_MOD_CTRL) cdp_mods |= 2;
        if (e->mods & FINN_MOD_SUPER) cdp_mods |= 4;
        if (e->mods & FINN_MOD_SHIFT) cdp_mods |= 8;

        /* Printable characters without control modifiers */
        if (!(e->mods & (FINN_MOD_CTRL | FINN_MOD_ALT)) && e->keysym >= 32 && e->keysym <= 126) {
            int vk = (int)e->keysym;
            char key[2] = { (char)e->keysym, '\0' };
            char code[16] = "Key";
            if (e->keysym >= 'a' && e->keysym <= 'z') {
                vk = e->keysym - 'a' + 65;
                snprintf(code, sizeof(code), "Key%c", (char)toupper((unsigned char)e->keysym));
            } else if (e->keysym >= 'A' && e->keysym <= 'Z') {
                vk = (int)e->keysym;
                snprintf(code, sizeof(code), "Key%c", (char)e->keysym);
            } else if (e->keysym >= '0' && e->keysym <= '9') {
                snprintf(code, sizeof(code), "Digit%c", (char)e->keysym);
            } else if (e->keysym == ' ') {
                snprintf(code, sizeof(code), "Space");
            } else {
                snprintf(code, sizeof(code), "%c", (char)e->keysym);
            }

            (void)send_cmd(app->client_fd, "KEY down %d %d %s %s\n", vk, cdp_mods, key, code);
            (void)send_cmd(app->client_fd, "TEXT %s\n", key);
            (void)send_cmd(app->client_fd, "KEY up %d %d %s %s\n", vk, cdp_mods, key, code);
            return 1;
        }

        /* Special navigation and editing keys */
        for (size_t i = 0; i < sizeof(kSpecialKeys) / sizeof(kSpecialKeys[0]); i++) {
            if (kSpecialKeys[i].evdev_code == e->code) {
                (void)send_cmd(app->client_fd, "KEY down %d %d %s %s\n",
                               kSpecialKeys[i].vk, cdp_mods, kSpecialKeys[i].key, kSpecialKeys[i].code);
                (void)send_cmd(app->client_fd, "KEY up %d %d %s %s\n",
                               kSpecialKeys[i].vk, cdp_mods, kSpecialKeys[i].key, kSpecialKeys[i].code);
                return 1;
            }
        }
    }

    return 0;
}

/* ---------------- public compositor API ---------------- */

int finnbrowserapp_init(const char *icon_dir, const char *font_dir) {
    if (icon_dir) snprintf(g_icon_dir, sizeof(g_icon_dir), "%s", icon_dir);
    if (font_dir) snprintf(g_font_dir, sizeof(g_font_dir), "%s", font_dir);

    finnbrowser_init(&g_browser_app);

    char p[600];
    snprintf(p, sizeof(p), "%s/search-01.svg", g_icon_dir);
    (void)fsvg_load(p, &g_ic_search);
    snprintf(p, sizeof(p), "%s/refresh.svg", g_icon_dir);
    (void)fsvg_load(p, &g_ic_refresh);
    snprintf(p, sizeof(p), "%s/chevron-right.svg", g_icon_dir);
    (void)fsvg_load(p, &g_ic_chevron);
    snprintf(p, sizeof(p), "%s/x.svg", g_icon_dir);
    (void)fsvg_load(p, &g_ic_x);

    g_ready = 1;
    return 0;
}

void finnbrowserapp_free(void) {
    if (g_browser_app.client_fd >= 0) {
        close(g_browser_app.client_fd);
        g_browser_app.client_fd = -1;
    }
    if (g_browser_app.listen_fd >= 0) {
        close(g_browser_app.listen_fd);
        g_browser_app.listen_fd = -1;
    }
    (void)unlink(FINN_BROWSER_SOCK);

    for (int i = 0; i < g_browser_app.tab_count; i++) {
        if (g_browser_app.tabs[i].pixels) {
            free(g_browser_app.tabs[i].pixels);
            g_browser_app.tabs[i].pixels = NULL;
        }
    }
    g_browser_app.tab_count = 0;

    if (g_browser_app.rx_staging) {
        free(g_browser_app.rx_staging);
        g_browser_app.rx_staging = NULL;
    }

    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_refresh);
    fsvg_free(&g_ic_chevron);
    fsvg_free(&g_ic_x);

    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    g_font_scale = -1.0f;
    g_ready = 0;
}

int finnbrowserapp_listen_fd(void) {
    return g_browser_app.listen_fd;
}

int finnbrowserapp_client_fd(void) {
    return g_browser_app.client_fd;
}

int finnbrowserapp_poll(void) {
    g_browser_app.dirty = 0;
    finnbrowser_step(&g_browser_app);
    return g_browser_app.dirty;
}

void finnbrowserapp_window_opened(void) {
    g_browser_app.window_open = 1;
    finnbrowser_open(&g_browser_app);
}

void finnbrowserapp_window_closed(void) {
    g_browser_app.window_open = 0;
    if (g_browser_app.client_fd < 0) {
        g_browser_app.engine_spawned = 0;
    }
}

int finnbrowserapp_take_launch_request(void) {
    if (g_browser_app.launch_requested) {
        g_browser_app.launch_requested = 0;
        return 1;
    }
    return 0;
}

uint32_t finnbrowserapp_build_key(float scale) {
    uint64_t h = finnbrowser_content_key(&g_browser_app);
    uint32_t k = (uint32_t)(h ^ (h >> 32));
    uint32_t s_bits = 0;
    memcpy(&s_bits, &scale, sizeof(s_bits));
    k ^= s_bits;
    return k;
}

void finnbrowserapp_render(uint32_t *layer, int lw, int lh,
                           int lx0, int ly0, int wx, int wy, int ww, int wh,
                           float scale, const finn_state *st, int focused) {
    finnbrowser_draw(&g_browser_app, layer, lw, lh, lx0, ly0, wx, wy, ww, wh, scale, st, focused);
}

int finnbrowserapp_press(int hit, int sub, int wx, int wy, int ww, int wh,
                          int px, int py, float scale) {
    return finnbrowser_press(&g_browser_app, hit, sub, wx, wy, ww, wh, px, py, scale);
}

int finnbrowserapp_move(int wx, int wy, int ww, int wh, int px, int py,
                         float scale) {
    return finnbrowser_motion(&g_browser_app, wx, wy, ww, wh, px, py, scale);
}

void finnbrowserapp_release(int wx, int wy, int ww, int wh, int px, int py,
                            float scale) {
    finnbrowser_release(&g_browser_app, wx, wy, ww, wh, px, py, scale);
}

int finnbrowserapp_key(const finn_key_event *e) {
    return finnbrowser_key(&g_browser_app, e);
}

int finnbrowserapp_scroll(int clicks, int wx, int wy, int ww, int wh,
                           int px, int py, float scale) {
    return finnbrowser_scroll(&g_browser_app, clicks, wx, wy, ww, wh, px, py, scale);
}

int finnbrowserapp_open_url(const char *url) {
    char norm[FINN_BROWSER_URL_MAX];
    if (finn_browser_address(url, norm, sizeof(norm)) != FINN_BROWSER_ADDR_URL) {
        return -1;
    }
    BrowserTab *act = get_active_tab(&g_browser_app);
    if (act) {
        snprintf(act->url, sizeof(act->url), "%s", norm);
        snprintf(g_browser_app.address_buf, sizeof(g_browser_app.address_buf), "%s", norm);
        g_browser_app.address_len = (int)strlen(norm);
        if (g_browser_app.client_fd >= 0 && g_browser_app.engine_ready) {
            (void)send_cmd(g_browser_app.client_fd, "NAVIGATE %d %s\n", act->id, norm);
        }
        g_browser_app.dirty = 1;
        g_browser_app.revision++;
        return 0;
    } else if (g_browser_app.tab_count < FINN_BROWSER_TAB_MAX) {
        if (g_browser_app.client_fd >= 0 && g_browser_app.engine_ready) {
            (void)send_cmd(g_browser_app.client_fd, "NEW %s\n", norm);
        }
        g_browser_app.dirty = 1;
        g_browser_app.revision++;
        return 0;
    }
    return -1;
}

const char *finnbrowserapp_url(void) {
    BrowserTab *act = get_active_tab(&g_browser_app);
    return act ? act->url : "";
}

const char *finnbrowserapp_title(void) {
    BrowserTab *act = get_active_tab(&g_browser_app);
    return (act && act->title[0]) ? act->title : "Finn Browser";
}

const char *finnbrowserapp_status(void) {
    return g_browser_app.status_msg;
}
