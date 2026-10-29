#ifndef FINNWAYLAND_H
#define FINNWAYLAND_H

#include <stdbool.h>
#include <stdint.h>

struct wl_display;
typedef struct FinnWayland FinnWayland;
typedef struct FinnWaylandSurface FinnWaylandSurface;

typedef enum {
    FINN_WAYLAND_ARGB8888 = 0,
    FINN_WAYLAND_XRGB8888 = 1
} FinnWaylandFormat;

enum {
    FINN_WAYLAND_CONFIGURE_ACTIVATED = 1u << 0,
    FINN_WAYLAND_CONFIGURE_MAXIMIZED = 1u << 1
};

typedef struct {
    void (*surface_created)(void *userdata, FinnWaylandSurface *surface);
    void (*surface_destroyed)(void *userdata, FinnWaylandSurface *surface);
    void (*title_changed)(void *userdata, FinnWaylandSurface *surface,
                          const char *title);
    void (*surface_committed)(void *userdata, FinnWaylandSurface *surface);
    /* A client assigned (or re-assigned) its application id. The window
     * already exists by then — get_toplevel fires surface_created first —
     * so the compositor updates the grouping rather than reopening. */
    void (*app_id_changed)(void *userdata, FinnWaylandSurface *surface,
                           const char *app_id);
    /* A popup (menu, combo, tooltip, dialog) arrived for a toplevel. The
     * compositor draws it as a chromeless child of the parent's window;
     * popups never take a window slot of their own. Dismissal (outside
     * press, Escape, grab loss) is the compositor's job; the close call
     * below reports it back to the client. */
    void (*popup_created)(void *userdata, FinnWaylandSurface *popup,
                          FinnWaylandSurface *parent);
    void (*popup_destroyed)(void *userdata, FinnWaylandSurface *popup);
    void (*decoration_changed)(void *userdata, FinnWaylandSurface *surface);
} FinnWaylandCallbacks;

typedef struct {
    /* Must be an absolute path. The socket is created mode 0660, group finn. */
    const char *socket_path;
    int32_t output_width;
    int32_t output_height;
    int32_t output_refresh_millihz; /* zero selects 60000 */
    FinnWaylandCallbacks callbacks;
    void *userdata;
} FinnWaylandConfig;

/* Creates the display, core globals, xdg-shell global, and explicitly named
 * listening socket. Returns NULL with errno set on failure. The finn group
 * must exist; an existing path is never removed or replaced.
 *
 * This is a small toplevel + popup + subsurface server: popups are realized
 * as chromeless transients owned by their parent toplevel, and subsurfaces
 * composite into their parent's pixels. linux-dmabuf, touch, clipboard
 * transfers, drag-and-drop, non-unit scale, and transformed buffers are not
 * implemented. Window move/resize/state requests and input/opaque regions
 * are accepted as no-ops; the compositor controls geometry through
 * finn_wayland_configure(). Only ARGB8888 and XRGB8888 wl_shm buffers are
 * copied. The built-in wl_shm pool implementation still enforces pool bounds.
 * All API calls and callbacks belong on the compositor/event-loop thread;
 * surface handles are borrowed and expire in surface_destroyed. */
FinnWayland *finn_wayland_create(const FinnWaylandConfig *config);
void finn_wayland_destroy(FinnWayland *server);

/* Integrate the Wayland event-loop fd into the compositor's poll loop. The
 * dispatch call is non-blocking for timeout_ms == 0 and flushes client events.
 * Call all server APIs on the same compositor/event-loop thread. */
struct wl_display *finn_wayland_display(FinnWayland *server);
int finn_wayland_fd(FinnWayland *server);
int finn_wayland_dispatch(FinnWayland *server, int timeout_ms);

/* A configure may be sent only after the previous configure was acknowledged.
 * A zero width or height asks the client to choose that dimension. */
int finn_wayland_configure(FinnWaylandSurface *surface, int32_t width,
                           int32_t height, uint32_t states,
                           uint32_t *serial_out);

/* Pixel storage is owned by the server and remains valid until the next
 * successful buffer commit or surface destruction. Rows are tightly packed
 * native-endian 0xAARRGGBB words; XRGB buffers are normalized to opaque alpha. */
const uint32_t *finn_wayland_surface_pixels(const FinnWaylandSurface *surface,
                                            int32_t *width, int32_t *height,
                                            int32_t *stride_bytes,
                                            FinnWaylandFormat *format);
const char *finn_wayland_surface_title(const FinnWaylandSurface *surface);
const char *finn_wayland_surface_app_id(const FinnWaylandSurface *surface);
/* Returns non-zero when the surface negotiated server-side decorations through
 * either zxdg_decoration_manager_v1 or org_kde_kwin_server_decoration_manager,
 * so the compositor draws window chrome rather than assuming client borders. */
int finn_wayland_surface_server_decorated(const FinnWaylandSurface *surface);
/* A popup's parent-relative offset, resolved from the xdg_positioner at
 * creation. The compositor places the transient relative to its parent
 * window's client area. */
void finn_wayland_popup_geometry(const FinnWaylandSurface *surface,
                                 int32_t *x, int32_t *y);
int finn_wayland_surface_is_popup(const FinnWaylandSurface *surface);
/* Dismiss a popup as if the client destroyed it: sends no protocol event
 * beyond the surface going away, which is exactly what an outside press or
 * Escape means to a grabbed menu. */
void finn_wayland_popup_dismiss(FinnWaylandSurface *surface);

/* Seat events use Wayland's millisecond timestamps, Linux evdev button/key
 * codes, and WL_POINTER_BUTTON_STATE_* / WL_KEYBOARD_KEY_STATE_* values. A
 * NULL focus clears focus. Pointer coordinates are surface-local. */
void finn_wayland_pointer_focus(FinnWayland *server,
                                FinnWaylandSurface *surface,
                                double x, double y, uint32_t time_msec);
void finn_wayland_pointer_motion(FinnWayland *server, double x, double y,
                                 uint32_t time_msec);
void finn_wayland_pointer_button(FinnWayland *server, uint32_t button,
                                 uint32_t state, uint32_t time_msec);
void finn_wayland_pointer_axis(FinnWayland *server, uint32_t axis,
                               double value, int32_t discrete,
                               uint32_t time_msec);
void finn_wayland_keyboard_focus(FinnWayland *server,
                                 FinnWaylandSurface *surface);
int finn_wayland_keyboard_key(FinnWayland *server, uint32_t evdev_key,
                             uint32_t state, uint32_t time_msec);

/* Completes every wl_surface.frame callback currently queued on this surface. */
void finn_wayland_frame_done(FinnWaylandSurface *surface, uint32_t time_msec);
/* Sends xdg_toplevel.close for a shell-initiated window close. */
void finn_wayland_toplevel_close(FinnWaylandSurface *surface);

#endif
