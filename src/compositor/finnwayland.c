#define _GNU_SOURCE
#include "finnwayland.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include "xdg-shell-server-protocol.h"
#include <xkbcommon/xkbcommon.h>

#define FINN_WAYLAND_MAX_DIMENSION 32768
#define FINN_WAYLAND_MAX_BUFFER_BYTES (256u * 1024u * 1024u)
#define FINN_WAYLAND_MAX_EVDEV_KEY 0x2ffu

struct FinnFrameCallback {
    struct wl_list link;
    struct wl_resource *resource;
};

struct FinnPointerResource {
    struct wl_list link;
    struct wl_resource *resource;
};

struct FinnKeyboardResource {
    struct wl_list link;
    struct wl_resource *resource;
};

struct FinnWaylandSurface {
    struct wl_list link;
    struct FinnWayland *server;
    struct wl_resource *resource;
    struct wl_resource *xdg_surface_resource;
    struct wl_resource *toplevel_resource;
    struct wl_resource *pending_buffer;
    struct wl_listener pending_buffer_destroy;
    bool pending_attach;
    bool committed_once;
    bool initial_configure_sent;
    bool role_assigned;
    bool role_created;
    bool destroying;
    uint32_t last_configure_serial;
    uint32_t acked_configure_serial;
    int32_t width;
    int32_t height;
    int32_t stride;
    FinnWaylandFormat format;
    uint32_t *pixels;
    char *title;
    struct wl_list frame_callbacks;
};

struct FinnWayland {
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_global *compositor_global;
    struct wl_global *seat_global;
    struct wl_global *output_global;
    struct wl_global *xdg_global;
    struct wl_global *data_device_manager_global;
    struct wl_list surfaces;
    struct wl_list pointers;
    struct wl_list keyboards;
    struct FinnWaylandSurface *pointer_focus;
    struct FinnWaylandSurface *pointer_grab;
    uint32_t pointer_buttons;
    struct FinnWaylandSurface *keyboard_focus;
    double pointer_x;
    double pointer_y;
    int32_t output_width;
    int32_t output_height;
    int32_t output_refresh_millihz;
    FinnWaylandCallbacks callbacks;
    void *userdata;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    char *keymap_text;
    size_t keymap_size;
    bool key_down[FINN_WAYLAND_MAX_EVDEV_KEY + 1];
    char *socket_path;
    dev_t socket_dev;
    ino_t socket_ino;
    bool socket_created;
};

static void surface_resource_destroy(struct wl_resource *resource);
static void xdg_surface_resource_destroy(struct wl_resource *resource);
static void toplevel_resource_destroy(struct wl_resource *resource);
static const struct xdg_surface_interface xdg_surface_implementation;
static const struct xdg_toplevel_interface toplevel_implementation;

static int
create_listening_socket(FinnWayland *server, const char *path)
{
    struct sockaddr_un address;
    struct stat st;
    struct group *finn_group;
    mode_t old_mask;
    int fd;
    size_t path_len;

    if (!path || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    path_len = strlen(path);
    if (path_len == 0 || path_len >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (lstat(path, &st) == 0) {
        errno = EADDRINUSE;
        return -1;
    }
    if (errno != ENOENT)
        return -1;
    finn_group = getgrnam("finn");
    if (!finn_group) {
        errno = ENOENT;
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, path_len + 1);

    /* bind(2) creates the filesystem entry using the process umask. Tighten
     * it during creation, then set the exact requested mode below. */
    old_mask = umask(0117);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        int saved_errno = errno;
        umask(old_mask);
        close(fd);
        errno = saved_errno;
        return -1;
    }
    umask(old_mask);
    if (lstat(path, &st) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISSOCK(st.st_mode)) {
        close(fd);
        errno = EIO;
        return -1;
    }
    server->socket_dev = st.st_dev;
    server->socket_ino = st.st_ino;
    server->socket_created = true;

    if (chown(path, (uid_t)-1, finn_group->gr_gid) < 0 ||
        chmod(path, 0660) < 0 || listen(fd, 32) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    if (wl_display_add_socket_fd(server->display, fd) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static void
unlink_owned_socket(FinnWayland *server)
{
    struct stat st;
    if (!server->socket_created || !server->socket_path)
        return;
    if (lstat(server->socket_path, &st) == 0 && S_ISSOCK(st.st_mode) &&
        st.st_dev == server->socket_dev && st.st_ino == server->socket_ino)
        (void)unlink(server->socket_path);
    server->socket_created = false;
}

static void
pending_buffer_destroyed(struct wl_listener *listener, void *data)
{
    FinnWaylandSurface *surface = wl_container_of(listener, surface,
                                                   pending_buffer_destroy);
    (void)data;
    surface->pending_buffer = NULL;
    surface->pending_attach = true;
    wl_list_remove(&surface->pending_buffer_destroy.link);
    wl_list_init(&surface->pending_buffer_destroy.link);
}

static void
clear_pending_buffer_listener(FinnWaylandSurface *surface)
{
    if (!wl_list_empty(&surface->pending_buffer_destroy.link)) {
        wl_list_remove(&surface->pending_buffer_destroy.link);
        wl_list_init(&surface->pending_buffer_destroy.link);
    }
}

static void
notify_role_destroyed(FinnWaylandSurface *surface)
{
    if (surface->role_created) {
        surface->role_created = false;
        if (surface->server->callbacks.surface_destroyed)
            surface->server->callbacks.surface_destroyed(
                surface->server->userdata, surface);
    }
}

static void
frame_resource_destroy(struct wl_resource *resource)
{
    struct FinnFrameCallback *frame = wl_resource_get_user_data(resource);
    if (!frame)
        return;
    wl_list_remove(&frame->link);
    free(frame);
}

static void
surface_resource_destroy(struct wl_resource *resource)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    struct FinnFrameCallback *frame, *tmp;
    if (!surface)
        return;

    surface->destroying = true;
    if (surface->server->pointer_grab == surface) {
        surface->server->pointer_grab = NULL;
        surface->server->pointer_buttons = 0;
    }
    if (surface->server->pointer_focus == surface)
        finn_wayland_pointer_focus(surface->server, NULL, 0, 0, 0);
    if (surface->server->keyboard_focus == surface)
        finn_wayland_keyboard_focus(surface->server, NULL);

    if (surface->toplevel_resource)
        wl_resource_destroy(surface->toplevel_resource);
    if (surface->xdg_surface_resource)
        wl_resource_destroy(surface->xdg_surface_resource);
    clear_pending_buffer_listener(surface);
    surface->pending_buffer = NULL;
    wl_list_for_each_safe(frame, tmp, &surface->frame_callbacks, link)
        wl_resource_destroy(frame->resource);
    notify_role_destroyed(surface);
    wl_list_remove(&surface->link);
    free(surface->pixels);
    free(surface->title);
    free(surface);
}

static void
region_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void
region_add_request(struct wl_client *client, struct wl_resource *resource,
                   int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void
region_subtract_request(struct wl_client *client, struct wl_resource *resource,
                        int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static const struct wl_region_interface region_implementation = {
    .destroy = region_destroy_request,
    .add = region_add_request,
    .subtract = region_subtract_request,
};

static void
compositor_create_region(struct wl_client *client, struct wl_resource *resource,
                         uint32_t id)
{
    struct wl_resource *region;
    (void)resource;
    region = wl_resource_create(client, &wl_region_interface, 1, id);
    if (!region) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(region, &region_implementation, NULL, NULL);
}

static void
surface_attach_request(struct wl_client *client, struct wl_resource *resource,
                       struct wl_resource *buffer, int32_t x, int32_t y)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    (void)client;
    if (x != 0 || y != 0) {
        wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                               "buffer attach offsets must be zero");
        return;
    }
    clear_pending_buffer_listener(surface);
    surface->pending_buffer = buffer;
    surface->pending_attach = true;
    if (buffer) {
        surface->pending_buffer_destroy.notify = pending_buffer_destroyed;
        wl_resource_add_destroy_listener(buffer,
                                         &surface->pending_buffer_destroy);
    }
}

static void
surface_damage_request(struct wl_client *client, struct wl_resource *resource,
                       int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void
surface_frame_request(struct wl_client *client, struct wl_resource *resource,
                      uint32_t callback_id)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    struct FinnFrameCallback *frame = calloc(1, sizeof(*frame));
    if (!frame) {
        wl_client_post_no_memory(client);
        return;
    }
    frame->resource = wl_resource_create(client, &wl_callback_interface,
                                         1, callback_id);
    if (!frame->resource) {
        free(frame);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(surface->frame_callbacks.prev, &frame->link);
    wl_resource_set_implementation(frame->resource, NULL, frame,
                                    frame_resource_destroy);
}

static void
surface_set_region_request(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *region)
{
    (void)client;
    (void)resource;
    (void)region;
}

static void
surface_commit_request(struct wl_client *client, struct wl_resource *resource);

static void
surface_set_buffer_transform_request(struct wl_client *client,
                                     struct wl_resource *resource,
                                     int32_t transform)
{
    (void)client;
    if (transform != WL_OUTPUT_TRANSFORM_NORMAL)
        wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM,
                               "buffer transforms are not supported");
}

static void
surface_set_buffer_scale_request(struct wl_client *client,
                                 struct wl_resource *resource, int32_t scale)
{
    (void)client;
    if (scale != 1)
        wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE,
                               "only buffer scale 1 is supported");
}

static void
surface_damage_buffer_request(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t x, int32_t y,
                              int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void
surface_offset_request(struct wl_client *client, struct wl_resource *resource,
                       int32_t x, int32_t y)
{
    (void)client;
    if (x != 0 || y != 0)
        wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                               "surface buffer offsets are not supported");
}

static const struct wl_surface_interface surface_implementation = {
    .destroy = region_destroy_request,
    .attach = surface_attach_request,
    .damage = surface_damage_request,
    .frame = surface_frame_request,
    .set_opaque_region = surface_set_region_request,
    .set_input_region = surface_set_region_request,
    .commit = surface_commit_request,
    .set_buffer_transform = surface_set_buffer_transform_request,
    .set_buffer_scale = surface_set_buffer_scale_request,
    .damage_buffer = surface_damage_buffer_request,
    .offset = surface_offset_request,
};

static void
compositor_create_surface(struct wl_client *client,
                          struct wl_resource *resource, uint32_t id)
{
    FinnWayland *server = wl_resource_get_user_data(resource);
    FinnWaylandSurface *surface = calloc(1, sizeof(*surface));
    (void)resource;
    if (!surface) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->server = server;
    surface->title = strdup("");
    if (!surface->title) {
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }
    surface->resource = wl_resource_create(client, &wl_surface_interface,
                                           wl_resource_get_version(resource), id);
    if (!surface->resource) {
        free(surface->title);
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_init(&surface->link);
    wl_list_init(&surface->frame_callbacks);
    wl_list_init(&surface->pending_buffer_destroy.link);
    wl_list_insert(server->surfaces.prev, &surface->link);
    wl_resource_set_implementation(surface->resource, &surface_implementation,
                                   surface, surface_resource_destroy);
}

static const struct wl_compositor_interface compositor_implementation = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

static void
bind_compositor(struct wl_client *client, void *data, uint32_t version,
                uint32_t id)
{
    FinnWayland *server = data;
    struct wl_resource *resource = wl_resource_create(
        client, &wl_compositor_interface, version < 6 ? version : 6, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &compositor_implementation,
                                   server, NULL);
}

static void
output_release_request(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_output_interface output_implementation = {
    .release = output_release_request,
};

static void
bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    FinnWayland *server = data;
    uint32_t resource_version = version < 4 ? version : 4;
    struct wl_resource *resource = wl_resource_create(
        client, &wl_output_interface, resource_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &output_implementation,
                                   server, NULL);
    wl_output_send_geometry(resource, 0, 0, 0, 0,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            "FinnOS", "FinnOS display",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT |
                        WL_OUTPUT_MODE_PREFERRED,
                        server->output_width, server->output_height,
                        server->output_refresh_millihz);
    wl_output_send_scale(resource, 1);
    if (resource_version >= WL_OUTPUT_NAME_SINCE_VERSION) {
        wl_output_send_name(resource, "finn-output-0");
        wl_output_send_description(resource, "FinnOS primary display");
    }
    if (resource_version >= WL_OUTPUT_DONE_SINCE_VERSION)
        wl_output_send_done(resource);
}

static void
pointer_set_cursor_request(struct wl_client *client,
                           struct wl_resource *resource, uint32_t serial,
                           struct wl_resource *surface,
                           int32_t hotspot_x, int32_t hotspot_y)
{
    (void)client;
    (void)resource;
    (void)serial;
    (void)surface;
    (void)hotspot_x;
    (void)hotspot_y;
}

static void
pointer_resource_destroy(struct wl_resource *resource)
{
    struct FinnPointerResource *pointer = wl_resource_get_user_data(resource);
    if (!pointer)
        return;
    wl_list_remove(&pointer->link);
    free(pointer);
}

static void
pointer_release_request(struct wl_client *client,
                        struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_pointer_interface pointer_implementation = {
    .set_cursor = pointer_set_cursor_request,
    .release = pointer_release_request,
};

static void
send_pointer_frame(struct wl_resource *resource)
{
    if (wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION)
        wl_pointer_send_frame(resource);
}

static void
send_pointer_enter(struct wl_resource *resource,
                   FinnWaylandSurface *surface, double x, double y)
{
    uint32_t serial = wl_display_next_serial(surface->server->display);
    wl_pointer_send_enter(resource, serial, surface->resource,
                          wl_fixed_from_double(x), wl_fixed_from_double(y));
    send_pointer_frame(resource);
}

static void
seat_get_pointer(struct wl_client *client, struct wl_resource *seat_resource,
                  uint32_t id)
{
    FinnWayland *server = wl_resource_get_user_data(seat_resource);
    struct FinnPointerResource *pointer = calloc(1, sizeof(*pointer));
    uint32_t version = wl_resource_get_version(seat_resource);
    if (!pointer) {
        wl_client_post_no_memory(client);
        return;
    }
    pointer->resource = wl_resource_create(client, &wl_pointer_interface,
                                           version < 8 ? version : 8, id);
    if (!pointer->resource) {
        free(pointer);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(server->pointers.prev, &pointer->link);
    wl_resource_set_implementation(pointer->resource, &pointer_implementation,
                                    pointer, pointer_resource_destroy);
    if (server->pointer_focus &&
        wl_resource_get_client(server->pointer_focus->resource) == client)
        send_pointer_enter(pointer->resource, server->pointer_focus,
                           server->pointer_x, server->pointer_y);
}

static int
keymap_fd_with_contents(const FinnWayland *server)
{
    char template[] = "/tmp/finn-wayland-keymap-XXXXXX";
    size_t written = 0;
    int fd = mkstemp(template);
    if (fd < 0)
        return -1;
    (void)unlink(template);
    if (fchmod(fd, 0600) < 0)
        goto fail;
    while (written < server->keymap_size) {
        ssize_t count = write(fd, server->keymap_text + written,
                              server->keymap_size - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            goto fail;
        written += (size_t)count;
    }
    if (lseek(fd, 0, SEEK_SET) < 0)
        goto fail;
    return fd;
fail:
    close(fd);
    return -1;
}

static void
keyboard_resource_destroy(struct wl_resource *resource)
{
    struct FinnKeyboardResource *keyboard = wl_resource_get_user_data(resource);
    if (!keyboard)
        return;
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

static void
keyboard_release_request(struct wl_client *client,
                         struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_implementation = {
    .release = keyboard_release_request,
};

static void
send_keyboard_enter(struct wl_resource *resource,
                    FinnWaylandSurface *surface)
{
    FinnWayland *server = surface->server;
    struct wl_array pressed;
    uint32_t serial = wl_display_next_serial(server->display);
    unsigned key;
    wl_array_init(&pressed);
    for (key = 0; key <= FINN_WAYLAND_MAX_EVDEV_KEY; key++) {
        if (server->key_down[key]) {
            uint32_t *entry = wl_array_add(&pressed, sizeof(*entry));
            if (!entry)
                break;
            *entry = key; /* wl_keyboard carries Linux evdev key codes. */
        }
    }
    wl_keyboard_send_enter(resource, serial, surface->resource, &pressed);
    wl_array_release(&pressed);
    wl_keyboard_send_modifiers(resource, serial,
        xkb_state_serialize_mods(server->xkb_state, XKB_STATE_MODS_DEPRESSED),
        xkb_state_serialize_mods(server->xkb_state, XKB_STATE_MODS_LATCHED),
        xkb_state_serialize_mods(server->xkb_state, XKB_STATE_MODS_LOCKED),
        xkb_state_serialize_layout(server->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE));
}

static void
seat_get_keyboard(struct wl_client *client,
                  struct wl_resource *seat_resource, uint32_t id)
{
    FinnWayland *server = wl_resource_get_user_data(seat_resource);
    struct FinnKeyboardResource *keyboard = calloc(1, sizeof(*keyboard));
    uint32_t version = wl_resource_get_version(seat_resource);
    int fd;
    if (!keyboard) {
        wl_client_post_no_memory(client);
        return;
    }
    keyboard->resource = wl_resource_create(client, &wl_keyboard_interface,
                                            version < 8 ? version : 8, id);
    if (!keyboard->resource) {
        free(keyboard);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(server->keyboards.prev, &keyboard->link);
    wl_resource_set_implementation(keyboard->resource,
                                   &keyboard_implementation, keyboard,
                                   keyboard_resource_destroy);
    fd = keymap_fd_with_contents(server);
    if (fd < 0) {
        wl_resource_destroy(keyboard->resource);
        wl_client_post_no_memory(client);
        return;
    }
    wl_keyboard_send_keymap(keyboard->resource,
                            WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                            fd, (uint32_t)server->keymap_size);
    close(fd);
    if (server->keyboard_focus &&
        wl_resource_get_client(server->keyboard_focus->resource) == client)
        send_keyboard_enter(keyboard->resource, server->keyboard_focus);
}

static void
seat_get_touch(struct wl_client *client, struct wl_resource *resource,
               uint32_t id)
{
    (void)client;
    (void)id;
    wl_resource_post_error(resource, WL_SEAT_ERROR_MISSING_CAPABILITY,
                           "touch input is not supported");
}

static void
seat_release_request(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_seat_interface seat_implementation = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = seat_release_request,
};

static void
bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    FinnWayland *server = data;
    uint32_t resource_version = version < 7 ? version : 7;
    struct wl_resource *resource = wl_resource_create(
        client, &wl_seat_interface, resource_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &seat_implementation,
                                   server, NULL);
    wl_seat_send_capabilities(resource,
                              WL_SEAT_CAPABILITY_POINTER |
                              WL_SEAT_CAPABILITY_KEYBOARD);
    if (resource_version >= WL_SEAT_NAME_SINCE_VERSION)
        wl_seat_send_name(resource, "finn-seat-0");
}

/* GTK/GDK requires the core clipboard data-device global before it accepts a
 * Wayland display. Clipboard transfers and drag-and-drop are not wired to the
 * shell yet; these resources safely accept their setup and lifecycle requests
 * so clients that do not use clipboard/DND can still create toplevels. */
static void
data_source_offer_request(struct wl_client *client,
                          struct wl_resource *resource, const char *mime_type)
{
    (void)client;
    (void)resource;
    (void)mime_type;
}

static void
data_source_destroy_request(struct wl_client *client,
                            struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void
data_source_set_actions_request(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t actions)
{
    (void)client;
    (void)resource;
    (void)actions;
}

static const struct wl_data_source_interface data_source_implementation = {
    .offer = data_source_offer_request,
    .destroy = data_source_destroy_request,
    .set_actions = data_source_set_actions_request,
};

static void
data_device_start_drag_request(struct wl_client *client,
                                struct wl_resource *resource,
                                struct wl_resource *source,
                                struct wl_resource *origin,
                                struct wl_resource *icon, uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)source;
    (void)origin;
    (void)icon;
    (void)serial;
}

static void
data_device_set_selection_request(struct wl_client *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *source,
                                    uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)source;
    (void)serial;
}

static void
data_device_release_request(struct wl_client *client,
                            struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_data_device_interface data_device_implementation = {
    .start_drag = data_device_start_drag_request,
    .set_selection = data_device_set_selection_request,
    .release = data_device_release_request,
};

static void
data_device_manager_create_source(struct wl_client *client,
                                   struct wl_resource *resource, uint32_t id)
{
    struct wl_resource *source = wl_resource_create(
        client, &wl_data_source_interface,
        wl_resource_get_version(resource), id);
    if (!source) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(source, &data_source_implementation,
                                   NULL, NULL);
}

static void
data_device_manager_get_device(struct wl_client *client,
                                  struct wl_resource *resource, uint32_t id,
                                  struct wl_resource *seat_resource)
{
    FinnWayland *server = wl_resource_get_user_data(resource);
    struct wl_resource *device;
    uint32_t version;
    if (!seat_resource || wl_resource_get_client(seat_resource) != client ||
        wl_resource_get_user_data(seat_resource) != server) {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "data device requested for an unknown seat");
        return;
    }
    version = wl_resource_get_version(resource);
    if (version > 3) version = 3;
    device = wl_resource_create(client, &wl_data_device_interface, version, id);
    if (!device) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(device, &data_device_implementation,
                                   server, NULL);
}

static void
data_device_manager_release_request(struct wl_client *client,
                                      struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_data_device_manager_interface
    data_device_manager_implementation = {
        .create_data_source = data_device_manager_create_source,
        .get_data_device = data_device_manager_get_device,
        .release = data_device_manager_release_request,
    };

static void
bind_data_device_manager(struct wl_client *client, void *data,
                         uint32_t version, uint32_t id)
{
    FinnWayland *server = data;
    struct wl_resource *resource = wl_resource_create(
        client, &wl_data_device_manager_interface,
        version < 3 ? version : 3, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &data_device_manager_implementation,
                                   server, NULL);
}

static void
xdg_positioner_destroy(struct wl_client *client,
                       struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

#define IGNORE_POSITIONER_REQUEST(name, args, ignored) \
    static void name args { (void)client; (void)resource; ignored; }

IGNORE_POSITIONER_REQUEST(positioner_size,
    (struct wl_client *client, struct wl_resource *resource,
     int32_t width, int32_t height), (void)width; (void)height)
IGNORE_POSITIONER_REQUEST(positioner_anchor_rect,
    (struct wl_client *client, struct wl_resource *resource,
     int32_t x, int32_t y, int32_t width, int32_t height),
    (void)x; (void)y; (void)width; (void)height)
IGNORE_POSITIONER_REQUEST(positioner_anchor,
    (struct wl_client *client, struct wl_resource *resource, uint32_t anchor),
    (void)anchor)
IGNORE_POSITIONER_REQUEST(positioner_gravity,
    (struct wl_client *client, struct wl_resource *resource, uint32_t gravity),
    (void)gravity)
IGNORE_POSITIONER_REQUEST(positioner_constraint_adjustment,
    (struct wl_client *client, struct wl_resource *resource,
     uint32_t adjustment), (void)adjustment)
IGNORE_POSITIONER_REQUEST(positioner_offset,
    (struct wl_client *client, struct wl_resource *resource,
     int32_t x, int32_t y), (void)x; (void)y)
IGNORE_POSITIONER_REQUEST(positioner_reactive,
    (struct wl_client *client, struct wl_resource *resource), (void)0)
IGNORE_POSITIONER_REQUEST(positioner_parent_size,
    (struct wl_client *client, struct wl_resource *resource,
     int32_t width, int32_t height), (void)width; (void)height)
IGNORE_POSITIONER_REQUEST(positioner_parent_configure,
    (struct wl_client *client, struct wl_resource *resource, uint32_t serial),
    (void)serial)

static const struct xdg_positioner_interface positioner_implementation = {
    .destroy = xdg_positioner_destroy,
    .set_size = positioner_size,
    .set_anchor_rect = positioner_anchor_rect,
    .set_anchor = positioner_anchor,
    .set_gravity = positioner_gravity,
    .set_constraint_adjustment = positioner_constraint_adjustment,
    .set_offset = positioner_offset,
    .set_reactive = positioner_reactive,
    .set_parent_size = positioner_parent_size,
    .set_parent_configure = positioner_parent_configure,
};

static void
xdg_wm_base_create_positioner(struct wl_client *client,
                             struct wl_resource *resource, uint32_t id)
{
    struct wl_resource *positioner = wl_resource_create(
        client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
    if (!positioner) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(positioner, &positioner_implementation,
                                   NULL, NULL);
}

static void
xdg_wm_base_pong(struct wl_client *client, struct wl_resource *resource,
                 uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)serial;
}

static void
xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void
xdg_wm_base_get_xdg_surface(struct wl_client *client,
                            struct wl_resource *wm_base_resource,
                            uint32_t id, struct wl_resource *surface_resource)
{
    FinnWayland *server = wl_resource_get_user_data(wm_base_resource);
    FinnWaylandSurface *surface = wl_resource_get_user_data(surface_resource);
    struct wl_resource *xdg_surface;
    if (!surface || surface->server != server || surface->committed_once ||
        surface->role_assigned ||
        wl_resource_get_client(surface_resource) != client ||
        surface->xdg_surface_resource) {
        wl_resource_post_error(wm_base_resource, XDG_WM_BASE_ERROR_ROLE,
                               "surface cannot be assigned an xdg role");
        return;
    }
    xdg_surface = wl_resource_create(client, &xdg_surface_interface,
                                     wl_resource_get_version(wm_base_resource), id);
    if (!xdg_surface) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->xdg_surface_resource = xdg_surface;
    wl_resource_set_implementation(xdg_surface, &xdg_surface_implementation,
                                   surface, xdg_surface_resource_destroy);
}

static const struct xdg_wm_base_interface xdg_wm_base_implementation = {
    .destroy = xdg_wm_base_destroy,
    .create_positioner = xdg_wm_base_create_positioner,
    .get_xdg_surface = xdg_wm_base_get_xdg_surface,
    .pong = xdg_wm_base_pong,
};

static void
bind_xdg_wm_base(struct wl_client *client, void *data, uint32_t version,
                 uint32_t id)
{
    FinnWayland *server = data;
    uint32_t resource_version = version < 6 ? version : 6;
    struct wl_resource *resource = wl_resource_create(
        client, &xdg_wm_base_interface, resource_version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &xdg_wm_base_implementation,
                                   server, NULL);
}

static void
xdg_surface_destroy_request(struct wl_client *client,
                            struct wl_resource *resource)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    (void)client;
    if (surface->toplevel_resource) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                               "xdg_toplevel must be destroyed first");
        return;
    }
    wl_resource_destroy(resource);
}

static void
xdg_surface_get_toplevel(struct wl_client *client,
                         struct wl_resource *resource, uint32_t id)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    struct wl_resource *toplevel;
    if (surface->toplevel_resource || surface->role_assigned) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "surface already has an xdg role");
        return;
    }
    toplevel = wl_resource_create(client, &xdg_toplevel_interface,
                                  wl_resource_get_version(resource), id);
    if (!toplevel) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->toplevel_resource = toplevel;
    surface->role_assigned = true;
    surface->role_created = true;
    wl_resource_set_implementation(toplevel, &toplevel_implementation, surface,
                                   toplevel_resource_destroy);
    if (surface->server->callbacks.surface_created)
        surface->server->callbacks.surface_created(surface->server->userdata,
                                                   surface);
}

static void
xdg_surface_get_popup(struct wl_client *client, struct wl_resource *resource,
                      uint32_t id, struct wl_resource *parent,
                      struct wl_resource *positioner)
{
    (void)client;
    (void)id;
    (void)parent;
    (void)positioner;
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                           "xdg_popup roles are not supported");
}

static void
xdg_surface_set_window_geometry(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x, int32_t y,
                                int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void
xdg_surface_ack_configure(struct wl_client *client,
                          struct wl_resource *resource, uint32_t serial)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    (void)client;
    if (!surface->initial_configure_sent ||
        serial != surface->last_configure_serial) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_INVALID_SERIAL,
                               "ack_configure references an unknown serial");
        return;
    }
    surface->acked_configure_serial = serial;
}

static const struct xdg_surface_interface xdg_surface_implementation = {
    .destroy = xdg_surface_destroy_request,
    .get_toplevel = xdg_surface_get_toplevel,
    .get_popup = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_set_window_geometry,
    .ack_configure = xdg_surface_ack_configure,
};

static void
xdg_surface_resource_destroy(struct wl_resource *resource)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;
    surface->xdg_surface_resource = NULL;
    if (!surface->destroying) {
        surface->initial_configure_sent = false;
        surface->last_configure_serial = 0;
        surface->acked_configure_serial = 0;
    }
}

static void
toplevel_destroy_request(struct wl_client *client,
                         struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void
toplevel_resource_destroy(struct wl_resource *resource)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;
    surface->toplevel_resource = NULL;
    notify_role_destroyed(surface);
}

static void
toplevel_set_parent(struct wl_client *client, struct wl_resource *resource,
                     struct wl_resource *parent)
{
    (void)client;
    (void)resource;
    (void)parent;
}

static void
toplevel_set_title(struct wl_client *client, struct wl_resource *resource,
                    const char *title)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    char *copy = strdup(title ? title : "");
    if (!copy) {
        wl_client_post_no_memory(client);
        return;
    }
    free(surface->title);
    surface->title = copy;
    if (surface->server->callbacks.title_changed)
        surface->server->callbacks.title_changed(surface->server->userdata,
                                                 surface, surface->title);
}

static void
toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource,
                    const char *app_id)
{
    (void)client;
    (void)resource;
    (void)app_id;
}

static void
toplevel_show_window_menu(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *seat, uint32_t serial,
                           int32_t x, int32_t y)
{
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
    (void)x;
    (void)y;
}

static void
toplevel_move(struct wl_client *client, struct wl_resource *resource,
              struct wl_resource *seat, uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
}

static void
toplevel_resize(struct wl_client *client, struct wl_resource *resource,
                struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
    (void)edges;
}

static void
toplevel_set_size_limit(struct wl_client *client,
                         struct wl_resource *resource,
                         int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)width;
    (void)height;
}

static void
toplevel_set_maximized(struct wl_client *client,
                        struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static void
toplevel_unset_maximized(struct wl_client *client,
                          struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static void
toplevel_set_fullscreen(struct wl_client *client,
                         struct wl_resource *resource,
                         struct wl_resource *output)
{
    (void)client;
    (void)resource;
    (void)output;
}

static void
toplevel_unset_fullscreen(struct wl_client *client,
                           struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static void
toplevel_set_minimized(struct wl_client *client,
                        struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static const struct xdg_toplevel_interface toplevel_implementation = {
    .destroy = toplevel_destroy_request,
    .set_parent = toplevel_set_parent,
    .set_title = toplevel_set_title,
    .set_app_id = toplevel_set_app_id,
    .show_window_menu = toplevel_show_window_menu,
    .move = toplevel_move,
    .resize = toplevel_resize,
    .set_max_size = toplevel_set_size_limit,
    .set_min_size = toplevel_set_size_limit,
    .set_maximized = toplevel_set_maximized,
    .unset_maximized = toplevel_unset_maximized,
    .set_fullscreen = toplevel_set_fullscreen,
    .unset_fullscreen = toplevel_unset_fullscreen,
    .set_minimized = toplevel_set_minimized,
};

static int
copy_shm_buffer(FinnWaylandSurface *surface, struct wl_resource *buffer)
{
    struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
    uint32_t format;
    int width, height, stride;
    uint64_t row_bytes, source_bytes, dest_bytes;
    const uint8_t *source;
    uint32_t *copy;
    int row;

    if (!shm) {
        wl_resource_post_error(surface->resource, WL_SURFACE_ERROR_INVALID_SIZE,
                               "only wl_shm buffers are supported");
        return -1;
    }
    width = wl_shm_buffer_get_width(shm);
    height = wl_shm_buffer_get_height(shm);
    stride = wl_shm_buffer_get_stride(shm);
    format = wl_shm_buffer_get_format(shm);
    row_bytes = (uint64_t)(width > 0 ? width : 0) * 4;
    source_bytes = (uint64_t)(stride > 0 ? stride : 0) *
                   (uint64_t)(height > 0 ? height : 0);
    dest_bytes = row_bytes * (uint64_t)(height > 0 ? height : 0);
    if (width <= 0 || height <= 0 ||
        width > FINN_WAYLAND_MAX_DIMENSION ||
        height > FINN_WAYLAND_MAX_DIMENSION ||
        stride < 0 || (uint64_t)stride < row_bytes ||
        source_bytes > FINN_WAYLAND_MAX_BUFFER_BYTES ||
        dest_bytes > FINN_WAYLAND_MAX_BUFFER_BYTES ||
        (format != WL_SHM_FORMAT_ARGB8888 &&
         format != WL_SHM_FORMAT_XRGB8888)) {
        wl_resource_post_error(surface->resource, WL_SURFACE_ERROR_INVALID_SIZE,
                               "invalid shm dimensions, stride, size, or format");
        return -1;
    }
    copy = malloc((size_t)dest_bytes);
    if (!copy) {
        wl_client_post_no_memory(wl_resource_get_client(surface->resource));
        return -1;
    }
    wl_shm_buffer_begin_access(shm);
    source = wl_shm_buffer_get_data(shm);
    if (!source) {
        wl_shm_buffer_end_access(shm);
        free(copy);
        wl_resource_post_error(surface->resource, WL_SURFACE_ERROR_INVALID_SIZE,
                               "shared-memory buffer has no data");
        return -1;
    }
    for (row = 0; row < height; row++)
        memcpy((uint8_t *)copy + (size_t)row * (size_t)row_bytes,
               source + (size_t)row * (size_t)stride, (size_t)row_bytes);
    wl_shm_buffer_end_access(shm);
    if (format == WL_SHM_FORMAT_XRGB8888) {
        size_t pixel_count = (size_t)width * (size_t)height;
        size_t pixel;
        for (pixel = 0; pixel < pixel_count; pixel++)
            copy[pixel] |= 0xff000000u;
    }

    free(surface->pixels);
    surface->pixels = copy;
    surface->width = width;
    surface->height = height;
    surface->stride = width * 4;
    surface->format = format == WL_SHM_FORMAT_ARGB8888
                    ? FINN_WAYLAND_ARGB8888 : FINN_WAYLAND_XRGB8888;
    return 0;
}

static void
surface_commit_request(struct wl_client *client, struct wl_resource *resource)
{
    FinnWaylandSurface *surface = wl_resource_get_user_data(resource);
    struct wl_resource *buffer = NULL;
    bool attached;
    (void)client;
    surface->committed_once = true;

    if (surface->xdg_surface_resource && !surface->toplevel_resource) {
        wl_resource_post_error(surface->xdg_surface_resource,
                               XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg role must be created before commit");
        return;
    }
    if (!surface->initial_configure_sent && surface->toplevel_resource) {
        if (surface->pending_attach && surface->pending_buffer) {
            wl_resource_post_error(surface->xdg_surface_resource,
                                   XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                                   "buffer committed before initial configure");
            return;
        }
        surface->initial_configure_sent = true;
        (void)finn_wayland_configure(surface, 0, 0, 0, NULL);
        if (surface->server->callbacks.surface_committed)
            surface->server->callbacks.surface_committed(
                surface->server->userdata, surface);
        return;
    }
    if (surface->toplevel_resource &&
        surface->acked_configure_serial != surface->last_configure_serial) {
        if (surface->pending_attach && surface->pending_buffer)
            wl_resource_post_error(surface->xdg_surface_resource,
                                   XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                                   "buffer committed before configure acknowledgement");
        return;
    }

    attached = surface->pending_attach;
    if (attached) {
        buffer = surface->pending_buffer;
        clear_pending_buffer_listener(surface);
        surface->pending_buffer = NULL;
        surface->pending_attach = false;
        if (!buffer) {
            free(surface->pixels);
            surface->pixels = NULL;
            surface->width = 0;
            surface->height = 0;
            surface->stride = 0;
        } else {
            int copied = copy_shm_buffer(surface, buffer);
            wl_buffer_send_release(buffer);
            if (copied < 0)
                return;
        }
    }
    if (surface->server->callbacks.surface_committed)
        surface->server->callbacks.surface_committed(surface->server->userdata,
                                                     surface);
}

static void
send_configure(FinnWaylandSurface *surface, int32_t width, int32_t height,
               uint32_t states, uint32_t serial)
{
    struct wl_array state_array;
    uint32_t *state;
    wl_array_init(&state_array);
    if (states & FINN_WAYLAND_CONFIGURE_ACTIVATED) {
        state = wl_array_add(&state_array, sizeof(*state));
        if (state)
            *state = XDG_TOPLEVEL_STATE_ACTIVATED;
    }
    if (states & FINN_WAYLAND_CONFIGURE_MAXIMIZED) {
        state = wl_array_add(&state_array, sizeof(*state));
        if (state)
            *state = XDG_TOPLEVEL_STATE_MAXIMIZED;
    }
    xdg_toplevel_send_configure(surface->toplevel_resource, width, height,
                                &state_array);
    xdg_surface_send_configure(surface->xdg_surface_resource, serial);
    wl_array_release(&state_array);
    surface->last_configure_serial = serial;
}

int
finn_wayland_configure(FinnWaylandSurface *surface, int32_t width,
                       int32_t height, uint32_t states, uint32_t *serial_out)
{
    uint32_t serial;
    if (!surface || !surface->toplevel_resource ||
        !surface->initial_configure_sent ||
        width < 0 || height < 0 ||
        width > FINN_WAYLAND_MAX_DIMENSION ||
        height > FINN_WAYLAND_MAX_DIMENSION ||
        (states & ~(FINN_WAYLAND_CONFIGURE_ACTIVATED |
                    FINN_WAYLAND_CONFIGURE_MAXIMIZED))) {
        errno = EINVAL;
        return -1;
    }
    if (surface->last_configure_serial != surface->acked_configure_serial) {
        errno = EBUSY;
        return -1;
    }
    serial = wl_display_next_serial(surface->server->display);
    send_configure(surface, width, height, states, serial);
    if (serial_out)
        *serial_out = serial;
    return 0;
}

FinnWayland *
finn_wayland_create(const FinnWaylandConfig *config)
{
    FinnWayland *server;
    struct xkb_rule_names names = { .layout = "us" };
    char *keymap;
    size_t keymap_length;

    if (!config || !config->socket_path ||
        config->output_width <= 0 || config->output_height <= 0 ||
        config->output_width > FINN_WAYLAND_MAX_DIMENSION ||
        config->output_height > FINN_WAYLAND_MAX_DIMENSION) {
        errno = EINVAL;
        return NULL;
    }
    server = calloc(1, sizeof(*server));
    if (!server)
        return NULL;
    wl_list_init(&server->surfaces);
    wl_list_init(&server->pointers);
    wl_list_init(&server->keyboards);
    server->output_width = config->output_width;
    server->output_height = config->output_height;
    server->output_refresh_millihz = config->output_refresh_millihz > 0
                                   ? config->output_refresh_millihz : 60000;
    server->callbacks = config->callbacks;
    server->userdata = config->userdata;
    server->socket_path = strdup(config->socket_path);
    if (!server->socket_path)
        goto fail;

    server->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!server->xkb_context)
        goto fail;
    server->xkb_keymap = xkb_keymap_new_from_names(server->xkb_context, &names,
                                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!server->xkb_keymap)
        goto fail;
    server->xkb_state = xkb_state_new(server->xkb_keymap);
    if (!server->xkb_state)
        goto fail;
    keymap = xkb_keymap_get_as_string(server->xkb_keymap,
                                      XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymap)
        goto fail;
    keymap_length = strlen(keymap) + 1;
    if (keymap_length > UINT32_MAX) {
        free(keymap);
        errno = EOVERFLOW;
        goto fail;
    }
    server->keymap_text = keymap;
    server->keymap_size = keymap_length;

    server->display = wl_display_create();
    if (!server->display)
        goto fail;
    server->event_loop = wl_display_get_event_loop(server->display);
    if (!server->event_loop || wl_display_init_shm(server->display) < 0)
        goto fail;
    server->compositor_global = wl_global_create(server->display,
        &wl_compositor_interface, 6, server, bind_compositor);
    server->seat_global = wl_global_create(server->display,
        &wl_seat_interface, 7, server, bind_seat);
    server->output_global = wl_global_create(server->display,
        &wl_output_interface, 4, server, bind_output);
    server->xdg_global = wl_global_create(server->display,
        &xdg_wm_base_interface, 6, server, bind_xdg_wm_base);
    server->data_device_manager_global = wl_global_create(server->display,
        &wl_data_device_manager_interface, 3, server,
        bind_data_device_manager);
    if (!server->compositor_global || !server->seat_global ||
        !server->output_global || !server->xdg_global ||
        !server->data_device_manager_global)
        goto fail;
    if (create_listening_socket(server, config->socket_path) < 0)
        goto fail;
    return server;

fail:
    {
        int saved_errno = errno ? errno : ENOMEM;
        if (server->display) {
            wl_display_destroy_clients(server->display);
            wl_display_destroy(server->display);
        }
        unlink_owned_socket(server);
        if (server->xkb_state)
            xkb_state_unref(server->xkb_state);
        if (server->xkb_keymap)
            xkb_keymap_unref(server->xkb_keymap);
        if (server->xkb_context)
            xkb_context_unref(server->xkb_context);
        free(server->keymap_text);
        free(server->socket_path);
        free(server);
        errno = saved_errno;
        return NULL;
    }
}

void
finn_wayland_destroy(FinnWayland *server)
{
    if (!server)
        return;
    if (server->display) {
        wl_display_destroy_clients(server->display);
        wl_display_destroy(server->display);
    }
    unlink_owned_socket(server);
    if (server->xkb_state)
        xkb_state_unref(server->xkb_state);
    if (server->xkb_keymap)
        xkb_keymap_unref(server->xkb_keymap);
    if (server->xkb_context)
        xkb_context_unref(server->xkb_context);
    free(server->keymap_text);
    free(server->socket_path);
    free(server);
}

struct wl_display *
finn_wayland_display(FinnWayland *server)
{
    return server ? server->display : NULL;
}

int
finn_wayland_fd(FinnWayland *server)
{
    if (!server || !server->event_loop) {
        errno = EINVAL;
        return -1;
    }
    return wl_event_loop_get_fd(server->event_loop);
}

int
finn_wayland_dispatch(FinnWayland *server, int timeout_ms)
{
    int result;
    if (!server || !server->event_loop || timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }
    result = wl_event_loop_dispatch(server->event_loop, timeout_ms);
    if (result < 0)
        return result;
    wl_display_flush_clients(server->display);
    return 0;
}

const uint32_t *
finn_wayland_surface_pixels(const FinnWaylandSurface *surface,
                            int32_t *width, int32_t *height,
                            int32_t *stride_bytes, FinnWaylandFormat *format)
{
    if (!surface)
        return NULL;
    if (width)
        *width = surface->width;
    if (height)
        *height = surface->height;
    if (stride_bytes)
        *stride_bytes = surface->stride;
    if (format)
        *format = surface->format;
    return surface->pixels;
}

const char *
finn_wayland_surface_title(const FinnWaylandSurface *surface)
{
    return surface ? surface->title : NULL;
}

static void
send_pointer_leave(FinnWayland *server, FinnWaylandSurface *surface)
{
    struct FinnPointerResource *pointer;
    struct FinnPointerResource *tmp;
    struct wl_client *client;
    uint32_t serial;
    if (!surface)
        return;
    client = wl_resource_get_client(surface->resource);
    serial = wl_display_next_serial(server->display);
    wl_list_for_each_safe(pointer, tmp, &server->pointers, link) {
        if (wl_resource_get_client(pointer->resource) == client) {
            wl_pointer_send_leave(pointer->resource, serial, surface->resource);
            send_pointer_frame(pointer->resource);
        }
    }
}

void
finn_wayland_pointer_focus(FinnWayland *server, FinnWaylandSurface *surface,
                           double x, double y, uint32_t time_msec)
{
    struct FinnPointerResource *pointer;
    struct FinnPointerResource *tmp;
    struct wl_client *client;
    if (!server || (surface && surface->server != server))
        return;
    if (server->pointer_grab && surface != server->pointer_grab) {
        finn_wayland_pointer_motion(server, x, y, time_msec);
        return;
    }
    if (server->pointer_focus == surface) {
        finn_wayland_pointer_motion(server, x, y, time_msec);
        return;
    }
    send_pointer_leave(server, server->pointer_focus);
    server->pointer_focus = surface;
    server->pointer_x = x;
    server->pointer_y = y;
    if (!surface)
        return;
    client = wl_resource_get_client(surface->resource);
    wl_list_for_each_safe(pointer, tmp, &server->pointers, link) {
        if (wl_resource_get_client(pointer->resource) == client)
            send_pointer_enter(pointer->resource, surface, x, y);
    }
}

void
finn_wayland_pointer_motion(FinnWayland *server, double x, double y,
                            uint32_t time_msec)
{
    struct FinnPointerResource *pointer;
    struct FinnPointerResource *tmp;
    struct wl_client *client;
    if (!server || !server->pointer_focus)
        return;
    server->pointer_x = x;
    server->pointer_y = y;
    client = wl_resource_get_client(server->pointer_focus->resource);
    wl_list_for_each_safe(pointer, tmp, &server->pointers, link) {
        if (wl_resource_get_client(pointer->resource) == client) {
            wl_pointer_send_motion(pointer->resource, time_msec,
                                   wl_fixed_from_double(x),
                                   wl_fixed_from_double(y));
            send_pointer_frame(pointer->resource);
        }
    }
}

void
finn_wayland_pointer_button(FinnWayland *server, uint32_t button,
                            uint32_t state, uint32_t time_msec)
{
    struct FinnPointerResource *pointer;
    struct FinnPointerResource *tmp;
    struct wl_client *client;
    FinnWaylandSurface *target;
    uint32_t serial;
    if (!server ||
        (state != WL_POINTER_BUTTON_STATE_PRESSED &&
         state != WL_POINTER_BUTTON_STATE_RELEASED))
        return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (server->pointer_buttons == 0)
            server->pointer_grab = server->pointer_focus;
        if (!server->pointer_grab) return;
        server->pointer_buttons++;
        target = server->pointer_grab;
    } else {
        if (!server->pointer_buttons || !server->pointer_grab) return;
        target = server->pointer_grab;
    }
    client = wl_resource_get_client(target->resource);
    serial = wl_display_next_serial(server->display);
    wl_list_for_each_safe(pointer, tmp, &server->pointers, link) {
        if (wl_resource_get_client(pointer->resource) == client) {
            wl_pointer_send_button(pointer->resource, serial, time_msec,
                                   button, state);
            send_pointer_frame(pointer->resource);
        }
    }
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (server->pointer_buttons > 0) server->pointer_buttons--;
        if (server->pointer_buttons == 0) server->pointer_grab = NULL;
    }
}

void
finn_wayland_pointer_axis(FinnWayland *server, uint32_t axis, double value,
                          int32_t discrete, uint32_t time_msec)
{
    struct FinnPointerResource *pointer;
    struct FinnPointerResource *tmp;
    struct wl_client *client;
    if (!server || !server->pointer_focus) return;
    client = wl_resource_get_client(server->pointer_focus->resource);
    wl_list_for_each_safe(pointer, tmp, &server->pointers, link) {
        if (wl_resource_get_client(pointer->resource) != client) continue;
        if (wl_resource_get_version(pointer->resource) >= 5)
            wl_pointer_send_axis_source(pointer->resource,
                                        WL_POINTER_AXIS_SOURCE_WHEEL);
        wl_pointer_send_axis(pointer->resource, time_msec, axis,
                             wl_fixed_from_double(value));
        if (discrete && wl_resource_get_version(pointer->resource) >= 5)
            wl_pointer_send_axis_discrete(pointer->resource, axis, discrete);
        if (discrete && wl_resource_get_version(pointer->resource) >= 8)
            wl_pointer_send_axis_value120(pointer->resource, axis,
                                          discrete * 120);
        send_pointer_frame(pointer->resource);
    }
}

static void
send_keyboard_leave(FinnWayland *server, FinnWaylandSurface *surface)
{
    struct FinnKeyboardResource *keyboard;
    struct FinnKeyboardResource *tmp;
    struct wl_client *client;
    uint32_t serial;
    if (!surface)
        return;
    client = wl_resource_get_client(surface->resource);
    serial = wl_display_next_serial(server->display);
    wl_list_for_each_safe(keyboard, tmp, &server->keyboards, link) {
        if (wl_resource_get_client(keyboard->resource) == client)
            wl_keyboard_send_leave(keyboard->resource, serial,
                                   surface->resource);
    }
}

void
finn_wayland_keyboard_focus(FinnWayland *server,
                            FinnWaylandSurface *surface)
{
    struct FinnKeyboardResource *keyboard;
    struct FinnKeyboardResource *tmp;
    struct wl_client *client;
    if (!server || (surface && surface->server != server))
        return;
    if (server->keyboard_focus == surface)
        return;
    send_keyboard_leave(server, server->keyboard_focus);
    server->keyboard_focus = surface;
    if (!surface)
        return;
    client = wl_resource_get_client(surface->resource);
    wl_list_for_each_safe(keyboard, tmp, &server->keyboards, link) {
        if (wl_resource_get_client(keyboard->resource) == client)
            send_keyboard_enter(keyboard->resource, surface);
    }
}

int
finn_wayland_keyboard_key(FinnWayland *server, uint32_t evdev_key,
                          uint32_t state, uint32_t time_msec)
{
    struct FinnKeyboardResource *keyboard;
    struct FinnKeyboardResource *tmp;
    struct wl_client *client;
    enum xkb_key_direction direction;
    uint32_t serial;
    if (!server || evdev_key > FINN_WAYLAND_MAX_EVDEV_KEY ||
        (state != WL_KEYBOARD_KEY_STATE_PRESSED &&
         state != WL_KEYBOARD_KEY_STATE_RELEASED)) {
        errno = EINVAL;
        return -1;
    }
    direction = state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN
                                                        : XKB_KEY_UP;
    server->key_down[evdev_key] = direction == XKB_KEY_DOWN;
    xkb_state_update_key(server->xkb_state, evdev_key + 8, direction);
    if (!server->keyboard_focus)
        return 0;
    client = wl_resource_get_client(server->keyboard_focus->resource);
    serial = wl_display_next_serial(server->display);
    wl_list_for_each_safe(keyboard, tmp, &server->keyboards, link) {
        if (wl_resource_get_client(keyboard->resource) == client) {
            /* Wayland key events carry the platform evdev code. Clients apply
             * the XKB keycode offset when looking it up in the keymap. */
            wl_keyboard_send_key(keyboard->resource, serial, time_msec,
                                 evdev_key, state);
            wl_keyboard_send_modifiers(keyboard->resource, serial,
                xkb_state_serialize_mods(server->xkb_state,
                                         XKB_STATE_MODS_DEPRESSED),
                xkb_state_serialize_mods(server->xkb_state,
                                         XKB_STATE_MODS_LATCHED),
                xkb_state_serialize_mods(server->xkb_state,
                                         XKB_STATE_MODS_LOCKED),
                xkb_state_serialize_layout(server->xkb_state,
                                           XKB_STATE_LAYOUT_EFFECTIVE));
        }
    }
    return 0;
}

void
finn_wayland_toplevel_close(FinnWaylandSurface *surface)
{
    if (surface && surface->toplevel_resource && !surface->destroying)
        xdg_toplevel_send_close(surface->toplevel_resource);
}

void
finn_wayland_frame_done(FinnWaylandSurface *surface, uint32_t time_msec)
{
    struct FinnFrameCallback *frame;
    struct FinnFrameCallback *tmp;
    if (!surface)
        return;
    wl_list_for_each_safe(frame, tmp, &surface->frame_callbacks, link) {
        wl_callback_send_done(frame->resource, time_msec);
        wl_resource_destroy(frame->resource);
    }
}
