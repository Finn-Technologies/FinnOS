#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "finn/ui.hpp"

#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace finn::ui {
namespace {

Theme pick_theme() {
    const char* dark = std::getenv("FINN_DARK");
    if (dark != nullptr) {
        const std::string value(dark);
        if (value == "1" || value == "true" || value == "dark") return Theme::dark_theme();
        if (value == "0" || value == "false" || value == "light") return Theme::light();
    }
    const char* home = std::getenv("HOME");
    const std::string user_path =
        home != nullptr && *home != '\0'
            ? std::string(home) + "/.config/finn/settings"
            : "/home/finn/.config/finn/settings";
    for (const std::string& path : {user_path, std::string("/var/lib/finn/settings")}) {
        std::ifstream settings(path);
        std::string line;
        while (std::getline(settings, line)) {
            if (line == "dark=1") return Theme::dark_theme();
            if (line == "dark=0") return Theme::light();
        }
    }
    return Theme::light();
}

double now_seconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

Key map_key(xkb_keysym_t sym) {
    switch (sym) {
        case XKB_KEY_Escape: return Key::escape;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: return Key::enter;
        case XKB_KEY_Tab: return Key::tab;
        case XKB_KEY_BackSpace: return Key::backspace;
        case XKB_KEY_Delete: return Key::del;
        case XKB_KEY_Left: return Key::left;
        case XKB_KEY_Right: return Key::right;
        case XKB_KEY_Up: return Key::up;
        case XKB_KEY_Down: return Key::down;
        case XKB_KEY_Home: return Key::home;
        case XKB_KEY_End: return Key::end;
        default: return Key::none;
    }
}

struct ShmBuffer {
    wl_buffer* buffer = nullptr;
    std::uint32_t* pixels = nullptr;
    std::size_t bytes = 0;
    bool busy = false;
    void* owner = nullptr;
};

}  // namespace

class Client {
public:
    enum class Mode { xdg, layer };

    Client() = default;
    ~Client() { destroy(); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool init_xdg(int width, int height, std::string title);
    bool init_layer(std::string name, int width, int height, std::uint32_t anchor,
                    Layer layer, KeyboardMode keyboard);
    int run();

    void request_frame();
    void set_title(std::string title);
    void set_framed(bool framed) { framed_ = framed; request_frame(); }
    void set_tick_ms(int ms);
    void set_input_region(const std::vector<Rect>& rects);
    void set_keyboard_mode(KeyboardMode mode);

    int width() const { return width_; }
    int height() const { return height_; }
    Rect content_rect() const {
        if (!framed_) return Rect{0, 0, static_cast<double>(width_), static_cast<double>(height_)};
        return Rect{0, 44, static_cast<double>(width_),
                    std::max(0.0, static_cast<double>(height_) - 44)};
    }
    void set_draw(std::function<void(Canvas&)> draw) { draw_ = std::move(draw); }
    void set_tick(std::function<void()> tick) { tick_ = std::move(tick); }
    void set_close(std::function<void()> close) { close_ = std::move(close); }

private:
    static void registry_global(void* data, wl_registry* registry, std::uint32_t name,
                                const char* interface, std::uint32_t version);
    static void registry_remove(void* data, wl_registry* registry, std::uint32_t name);
    static void seat_capabilities(void* data, wl_seat* seat, std::uint32_t capabilities);
    static void seat_name(void* data, wl_seat* seat, const char* name);
    static void pointer_enter(void* data, wl_pointer* pointer, std::uint32_t serial,
                              wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_leave(void* data, wl_pointer* pointer, std::uint32_t serial,
                              wl_surface* surface);
    static void pointer_motion(void* data, wl_pointer* pointer, std::uint32_t time,
                               wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_button(void* data, wl_pointer* pointer, std::uint32_t serial,
                               std::uint32_t time, std::uint32_t button, std::uint32_t state);
    static void pointer_axis(void* data, wl_pointer* pointer, std::uint32_t time,
                             std::uint32_t axis, wl_fixed_t value);
    static void pointer_frame(void* data, wl_pointer* pointer);
    static void pointer_axis_source(void* data, wl_pointer* pointer, std::uint32_t source);
    static void pointer_axis_stop(void* data, wl_pointer* pointer, std::uint32_t time,
                                  std::uint32_t axis);
    static void pointer_axis_discrete(void* data, wl_pointer* pointer, std::uint32_t axis,
                                      std::int32_t discrete);
    static void keyboard_keymap(void* data, wl_keyboard* keyboard, std::uint32_t format,
                                int fd, std::uint32_t size);
    static void keyboard_enter(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                               wl_surface* surface, wl_array* keys);
    static void keyboard_leave(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                               wl_surface* surface);
    static void keyboard_key(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                             std::uint32_t time, std::uint32_t key, std::uint32_t state);
    static void keyboard_modifiers(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                                   std::uint32_t depressed, std::uint32_t latched,
                                   std::uint32_t locked, std::uint32_t group);
    static void keyboard_repeat(void* data, wl_keyboard* keyboard, std::int32_t rate,
                                std::int32_t delay);
    static void wm_ping(void* data, xdg_wm_base* wm_base, std::uint32_t serial);
    static void xdg_surface_configure(void* data, xdg_surface* surface, std::uint32_t serial);
    static void toplevel_configure(void* data, xdg_toplevel* toplevel, std::int32_t width,
                                   std::int32_t height, wl_array* states);
    static void toplevel_configure_bounds(void* data, xdg_toplevel* toplevel,
                                          std::int32_t width, std::int32_t height);
    static void toplevel_close(void* data, xdg_toplevel* toplevel);
    static void layer_configure(void* data, zwlr_layer_surface_v1* surface,
                                std::uint32_t serial, std::uint32_t width,
                                std::uint32_t height);
    static void layer_closed(void* data, zwlr_layer_surface_v1* surface);
    static void buffer_release(void* data, wl_buffer* buffer);
    static void frame_done(void* data, wl_callback* callback, std::uint32_t time);

    void connect_display();
    void create_surface();
    bool create_buffers();
    void destroy_buffers();
    void render_frame();
    void draw_frame();
    void reset_input();
    void apply_input_region();
    void request_close();
    void destroy();
    void focus_from_pointer();

    Mode mode_ = Mode::xdg;
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    xdg_wm_base* wm_base_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_callback* frame_callback_ = nullptr;
    int tick_ms_ = 0;

    std::vector<std::unique_ptr<ShmBuffer>> buffers_;
    Input input_;
    Theme theme_;
    Canvas canvas_;
    std::string title_;
    std::function<void(Canvas&)> draw_;
    std::function<void()> tick_;
    std::function<void()> close_;

    int width_ = 0;
    int height_ = 0;
    int requested_width_ = 900;
    int requested_height_ = 600;
    int pending_width_ = 0;
    int pending_height_ = 0;
    bool configured_ = false;
    bool running_ = true;
    bool failed_ = false;
    bool framed_ = true;
    bool needs_frame_ = false;
    bool in_draw_ = false;
    bool close_requested_ = false;
    bool maximized_ = false;
    int theme_counter_ = 0;
    double last_title_press_ = 0;

    std::uint32_t layer_anchor_ = kAnchorAll;
    Layer layer_ = kLayerOverlay;
    KeyboardMode keyboard_mode_ = KeyboardMode::on_demand;
    std::vector<Rect> input_region_;
    bool input_region_set_ = false;

    wl_registry_listener registry_listener_{};
    wl_seat_listener seat_listener_{};
    wl_pointer_listener pointer_listener_{};
    wl_keyboard_listener keyboard_listener_{};
    wl_buffer_listener buffer_listener_{};
    xdg_wm_base_listener wm_base_listener_{};
    xdg_surface_listener xdg_surface_listener_{};
    xdg_toplevel_listener toplevel_listener_{};
    zwlr_layer_surface_v1_listener layer_listener_{};
    wl_callback_listener frame_listener_{};

    xkb_context* xkb_context_ = nullptr;
    xkb_keymap* xkb_keymap_ = nullptr;
    xkb_state* xkb_state_ = nullptr;
};

void Client::registry_global(void* data, wl_registry* registry, std::uint32_t name,
                             const char* interface, std::uint32_t version) {
    Client* client = static_cast<Client*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        client->compositor_ = static_cast<wl_compositor*>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        client->shm_ = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        client->wm_base_ = static_cast<xdg_wm_base*>(wl_registry_bind(
            registry, name, &xdg_wm_base_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        client->seat_ = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        client->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(
            registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    }
}

void Client::registry_remove(void* data, wl_registry* registry, std::uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

void Client::connect_display() {
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        std::fprintf(stderr, "libfinnui: cannot connect to WAYLAND_DISPLAY\n");
        failed_ = true;
        return;
    }
    registry_ = wl_display_get_registry(display_);
    registry_listener_.global = registry_global;
    registry_listener_.global_remove = registry_remove;
    wl_registry_add_listener(registry_, &registry_listener_, this);
    if (wl_display_roundtrip(display_) < 0) {
        std::fprintf(stderr, "libfinnui: registry roundtrip failed\n");
        failed_ = true;
        return;
    }
    if (compositor_ == nullptr || shm_ == nullptr) {
        std::fprintf(stderr, "libfinnui: wl_compositor or wl_shm missing\n");
        failed_ = true;
        return;
    }

    xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (seat_ != nullptr) {
        seat_listener_.capabilities = seat_capabilities;
        seat_listener_.name = seat_name;
        wl_seat_add_listener(seat_, &seat_listener_, this);
    }
}

void Client::seat_name(void* data, wl_seat* seat, const char* name) {
    (void)data;
    (void)seat;
    (void)name;
}

void Client::seat_capabilities(void* data, wl_seat* seat, std::uint32_t capabilities) {
    Client* client = static_cast<Client*>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && client->pointer_ == nullptr) {
        client->pointer_ = wl_seat_get_pointer(seat);
        client->pointer_listener_ = {};
        client->pointer_listener_.enter = pointer_enter;
        client->pointer_listener_.leave = pointer_leave;
        client->pointer_listener_.motion = pointer_motion;
        client->pointer_listener_.button = pointer_button;
        client->pointer_listener_.axis = pointer_axis;
        client->pointer_listener_.frame = pointer_frame;
        client->pointer_listener_.axis_source = pointer_axis_source;
        client->pointer_listener_.axis_stop = pointer_axis_stop;
        client->pointer_listener_.axis_discrete = pointer_axis_discrete;
        wl_pointer_add_listener(client->pointer_, &client->pointer_listener_, client);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 &&
        client->keyboard_ == nullptr) {
        client->keyboard_ = wl_seat_get_keyboard(seat);
        client->keyboard_listener_ = {};
        client->keyboard_listener_.keymap = keyboard_keymap;
        client->keyboard_listener_.enter = keyboard_enter;
        client->keyboard_listener_.leave = keyboard_leave;
        client->keyboard_listener_.key = keyboard_key;
        client->keyboard_listener_.modifiers = keyboard_modifiers;
        client->keyboard_listener_.repeat_info = keyboard_repeat;
        wl_keyboard_add_listener(client->keyboard_, &client->keyboard_listener_, client);
    }
}

void Client::create_surface() {
    surface_ = wl_compositor_create_surface(compositor_);
    if (surface_ == nullptr) {
        failed_ = true;
        return;
    }

    if (mode_ == Mode::xdg) {
        if (wm_base_ == nullptr) {
            std::fprintf(stderr, "libfinnui: xdg_wm_base missing\n");
            failed_ = true;
            return;
        }
        wm_base_listener_.ping = wm_ping;
        xdg_wm_base_add_listener(wm_base_, &wm_base_listener_, this);

        xdg_surface_ = xdg_wm_base_get_xdg_surface(wm_base_, surface_);
        xdg_surface_listener_.configure = xdg_surface_configure;
        xdg_surface_add_listener(xdg_surface_, &xdg_surface_listener_, this);

        toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
        toplevel_listener_.configure = toplevel_configure;
        toplevel_listener_.configure_bounds = toplevel_configure_bounds;
        toplevel_listener_.close = toplevel_close;
        xdg_toplevel_add_listener(toplevel_, &toplevel_listener_, this);
        xdg_toplevel_set_title(toplevel_, title_.c_str());
        xdg_toplevel_set_app_id(toplevel_, "finn.app");
    } else {
        if (layer_shell_ == nullptr) {
            std::fprintf(stderr, "libfinnui: zwlr_layer_shell_v1 missing\n");
            failed_ = true;
            return;
        }
        layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
            layer_shell_, surface_, nullptr, static_cast<std::uint32_t>(layer_),
            title_.c_str());
        layer_listener_.configure = layer_configure;
        layer_listener_.closed = layer_closed;
        zwlr_layer_surface_v1_add_listener(layer_surface_, &layer_listener_, this);
        zwlr_layer_surface_v1_set_anchor(layer_surface_, layer_anchor_);
        zwlr_layer_surface_v1_set_size(layer_surface_,
                                       static_cast<std::uint32_t>(requested_width_),
                                       static_cast<std::uint32_t>(requested_height_));
        zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, 0);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            layer_surface_, static_cast<std::uint32_t>(keyboard_mode_));
    }

    wl_surface_commit(surface_);
}

bool Client::init_xdg(int width, int height, std::string title) {
    mode_ = Mode::xdg;
    requested_width_ = width > 0 ? width : 900;
    requested_height_ = height > 0 ? height : 600;
    title_ = std::move(title);
    theme_ = pick_theme();
    connect_display();
    if (failed_) return false;
    create_surface();
    return !failed_;
}

bool Client::init_layer(std::string name, int width, int height,
                        std::uint32_t anchor, Layer layer, KeyboardMode keyboard) {
    mode_ = Mode::layer;
    requested_width_ = width;
    requested_height_ = height;
    title_ = std::move(name);
    layer_anchor_ = anchor;
    layer_ = layer;
    keyboard_mode_ = keyboard;
    theme_ = pick_theme();
    connect_display();
    if (failed_) return false;
    create_surface();
    return !failed_;
}

void Client::wm_ping(void* data, xdg_wm_base* wm_base, std::uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

void Client::toplevel_configure(void* data, xdg_toplevel* toplevel, std::int32_t width,
                                std::int32_t height, wl_array* states) {
    (void)toplevel;
    Client* client = static_cast<Client*>(data);
    client->pending_width_ = width;
    client->pending_height_ = height;
    client->maximized_ = false;
    const auto* values = static_cast<const std::uint32_t*>(states->data);
    const std::size_t count = states->size / sizeof(std::uint32_t);
    for (std::size_t i = 0; i < count; ++i) {
        if (values[i] == XDG_TOPLEVEL_STATE_MAXIMIZED) client->maximized_ = true;
    }
}

void Client::toplevel_configure_bounds(void* data, xdg_toplevel* toplevel,
                                       std::int32_t width, std::int32_t height) {
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}

void Client::toplevel_close(void* data, xdg_toplevel* toplevel) {
    (void)toplevel;
    static_cast<Client*>(data)->request_close();
}

void Client::xdg_surface_configure(void* data, xdg_surface* surface,
                                   std::uint32_t serial) {
    Client* client = static_cast<Client*>(data);
    xdg_surface_ack_configure(surface, serial);
    const bool first = !client->configured_;
    int width = client->pending_width_;
    int height = client->pending_height_;
    if (width <= 0) width = first ? client->requested_width_ : client->width_;
    if (height <= 0) height = first ? client->requested_height_ : client->height_;
    if (width <= 0 || height <= 0) return;
    if (width != client->width_ || height != client->height_) {
        client->width_ = width;
        client->height_ = height;
        client->destroy_buffers();
    }
    client->configured_ = true;
    client->create_buffers();
    client->render_frame();
}

void Client::layer_configure(void* data, zwlr_layer_surface_v1* surface,
                             std::uint32_t serial, std::uint32_t width,
                             std::uint32_t height) {
    Client* client = static_cast<Client*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) client->width_ = static_cast<int>(width);
    if (height > 0) client->height_ = static_cast<int>(height);
    if (client->width_ <= 0 || client->height_ <= 0) return;
    client->configured_ = true;
    client->create_buffers();
    client->apply_input_region();
    client->render_frame();
}

void Client::layer_closed(void* data, zwlr_layer_surface_v1* surface) {
    (void)surface;
    static_cast<Client*>(data)->request_close();
}

void Client::focus_from_pointer() {
    // Pointer focus is the compositor's decision; nothing to request.
}

void Client::pointer_enter(void* data, wl_pointer* pointer, std::uint32_t serial,
                           wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    (void)serial;
    (void)surface;
    Client* client = static_cast<Client*>(data);
    client->input_.x = wl_fixed_to_double(sx);
    client->input_.y = wl_fixed_to_double(sy);
    client->request_frame();
}

void Client::pointer_leave(void* data, wl_pointer* pointer, std::uint32_t serial,
                           wl_surface* surface) {
    (void)pointer;
    (void)serial;
    (void)surface;
    Client* client = static_cast<Client*>(data);
    client->input_.x = -1;
    client->input_.y = -1;
    client->request_frame();
}

void Client::pointer_motion(void* data, wl_pointer* pointer, std::uint32_t time,
                            wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    (void)time;
    Client* client = static_cast<Client*>(data);
    client->input_.x = wl_fixed_to_double(sx);
    client->input_.y = wl_fixed_to_double(sy);
    client->request_frame();
}

void Client::pointer_button(void* data, wl_pointer* pointer, std::uint32_t serial,
                            std::uint32_t time, std::uint32_t button,
                            std::uint32_t state) {
    (void)pointer;
    (void)time;
    Client* client = static_cast<Client*>(data);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        client->input_.pointer_down = true;
        client->input_.pointer_pressed = true;
        client->input_.button = button;

        if (client->mode_ == Mode::xdg && client->framed_ && button == BTN_LEFT &&
            client->input_.y < 44 && client->toplevel_ != nullptr) {
            const double w = static_cast<double>(client->width_);
            if (client->input_.x > w - 44) {
                client->request_close();
                return;
            }
            const double now = now_seconds();
            if (now - client->last_title_press_ < 0.35) {
                client->maximized_ = !client->maximized_;
                if (client->maximized_) {
                    xdg_toplevel_set_maximized(client->toplevel_);
                } else {
                    xdg_toplevel_unset_maximized(client->toplevel_);
                }
            } else if (client->seat_ != nullptr) {
                xdg_toplevel_move(client->toplevel_, client->seat_, serial);
            }
            client->last_title_press_ = now;
        }
    } else {
        client->input_.pointer_down = false;
        client->input_.pointer_released = true;
    }
    client->request_frame();
}

void Client::pointer_axis(void* data, wl_pointer* pointer, std::uint32_t time,
                          std::uint32_t axis, wl_fixed_t value) {
    (void)pointer;
    (void)time;
    Client* client = static_cast<Client*>(data);
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        client->input_.scroll_x += wl_fixed_to_double(value);
    } else {
        client->input_.scroll_y += wl_fixed_to_double(value);
    }
    client->request_frame();
}

void Client::pointer_axis_discrete(void* data, wl_pointer* pointer, std::uint32_t axis,
                                   std::int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

void Client::pointer_axis_source(void* data, wl_pointer* pointer, std::uint32_t source) {
    (void)data;
    (void)pointer;
    (void)source;
}

void Client::pointer_axis_stop(void* data, wl_pointer* pointer, std::uint32_t time,
                               std::uint32_t axis) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

void Client::pointer_frame(void* data, wl_pointer* pointer) {
    (void)pointer;
    static_cast<Client*>(data)->request_frame();
}

void Client::keyboard_keymap(void* data, wl_keyboard* keyboard, std::uint32_t format,
                             int fd, std::uint32_t size) {
    (void)keyboard;
    Client* client = static_cast<Client*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || client->xkb_context_ == nullptr) {
        close(fd);
        return;
    }
    char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    if (map == MAP_FAILED) return;
    xkb_keymap* keymap = xkb_keymap_new_from_string(
        client->xkb_context_, map, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (keymap == nullptr) return;
    if (client->xkb_state_ != nullptr) xkb_state_unref(client->xkb_state_);
    if (client->xkb_keymap_ != nullptr) xkb_keymap_unref(client->xkb_keymap_);
    client->xkb_keymap_ = keymap;
    client->xkb_state_ = xkb_state_new(keymap);
}

void Client::keyboard_enter(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                            wl_surface* surface, wl_array* keys) {
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
    static_cast<Client*>(data)->input_.focused = true;
}

void Client::keyboard_leave(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                            wl_surface* surface) {
    (void)keyboard;
    (void)serial;
    (void)surface;
    static_cast<Client*>(data)->input_.focused = false;
}

void Client::keyboard_key(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                          std::uint32_t time, std::uint32_t key, std::uint32_t state) {
    (void)keyboard;
    (void)serial;
    (void)time;
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    Client* client = static_cast<Client*>(data);
    if (client->xkb_state_ == nullptr) return;

    const xkb_keycode_t code = key + 8;
    KeyEvent event;
    event.sym = xkb_state_key_get_one_sym(client->xkb_state_, code);
    event.key = map_key(event.sym);
    event.ctrl = xkb_state_mod_name_is_active(client->xkb_state_, XKB_MOD_NAME_CTRL,
                                              XKB_STATE_MODS_EFFECTIVE) > 0;
    event.alt = xkb_state_mod_name_is_active(client->xkb_state_, XKB_MOD_NAME_ALT,
                                             XKB_STATE_MODS_EFFECTIVE) > 0;
    event.shift = xkb_state_mod_name_is_active(client->xkb_state_, XKB_MOD_NAME_SHIFT,
                                               XKB_STATE_MODS_EFFECTIVE) > 0;
    char text[32] = {};
    const int count = xkb_state_key_get_utf8(client->xkb_state_, code, text, sizeof text);
    if (count > 0) {
        event.text.assign(text, static_cast<std::size_t>(count));
        const unsigned char first = static_cast<unsigned char>(event.text.front());
        if (!event.ctrl && !event.alt && first >= 0x20 && first != 0x7f) {
            client->input_.text += event.text;
        }
    }
    client->input_.keys.push_back(std::move(event));
    client->request_frame();
}

void Client::keyboard_modifiers(void* data, wl_keyboard* keyboard, std::uint32_t serial,
                                std::uint32_t depressed, std::uint32_t latched,
                                std::uint32_t locked, std::uint32_t group) {
    (void)keyboard;
    (void)serial;
    Client* client = static_cast<Client*>(data);
    if (client->xkb_state_ != nullptr) {
        xkb_state_update_mask(client->xkb_state_, depressed, latched, locked, 0, 0, group);
    }
}

void Client::keyboard_repeat(void* data, wl_keyboard* keyboard, std::int32_t rate,
                             std::int32_t delay) {
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

bool Client::create_buffers() {
    destroy_buffers();
    if (width_ <= 0 || height_ <= 0 || shm_ == nullptr) return false;
    if (width_ > 16384 || height_ > 16384) {
        std::fprintf(stderr, "libfinnui: window is too large: %dx%d\n", width_, height_);
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(width_) * 4;
    const std::size_t bytes = stride * static_cast<std::size_t>(height_);
    for (int i = 0; i < 2; ++i) {
        const int fd = memfd_create("finn-ui", MFD_CLOEXEC);
        if (fd < 0) return false;
        if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
            close(fd);
            return false;
        }
        void* map = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            return false;
        }
        wl_shm_pool* pool = wl_shm_create_pool(shm_, fd, static_cast<std::int32_t>(bytes));
        wl_buffer* buffer = wl_shm_pool_create_buffer(
            pool, 0, width_, height_, static_cast<std::int32_t>(stride),
            WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        if (buffer == nullptr) {
            munmap(map, bytes);
            return false;
        }
        auto entry = std::make_unique<ShmBuffer>();
        entry->buffer = buffer;
        entry->pixels = static_cast<std::uint32_t*>(map);
        entry->bytes = bytes;
        entry->owner = this;
        buffer_listener_.release = buffer_release;
        wl_buffer_add_listener(buffer, &buffer_listener_, entry.get());
        buffers_.push_back(std::move(entry));
    }
    return true;
}

void Client::destroy_buffers() {
    for (auto& buffer : buffers_) {
        if (buffer->buffer != nullptr) wl_buffer_destroy(buffer->buffer);
        if (buffer->pixels != nullptr) munmap(buffer->pixels, buffer->bytes);
    }
    buffers_.clear();
}

void Client::buffer_release(void* data, wl_buffer* buffer) {
    (void)buffer;
    auto* entry = static_cast<ShmBuffer*>(data);
    entry->busy = false;
    auto* client = static_cast<Client*>(entry->owner);
    if (client != nullptr && client->needs_frame_) client->request_frame();
}

void Client::frame_done(void* data, wl_callback* callback, std::uint32_t time) {
    (void)time;
    Client* client = static_cast<Client*>(data);
    wl_callback_destroy(callback);
    client->frame_callback_ = nullptr;
    if (!client->running_) return;
    const bool again = client->needs_frame_;
    client->needs_frame_ = false;
    client->render_frame();
    if (client->needs_frame_ || again) client->request_frame();
}

void Client::request_frame() {
    if (!configured_ || !running_ || surface_ == nullptr) {
        needs_frame_ = true;
        return;
    }
    if (in_draw_ || frame_callback_ != nullptr) {
        needs_frame_ = true;
        return;
    }
    frame_callback_ = wl_surface_frame(surface_);
    if (frame_callback_ == nullptr) return;
    frame_listener_.done = frame_done;
    wl_callback_add_listener(frame_callback_, &frame_listener_, this);
    wl_surface_commit(surface_);
}

void Client::render_frame() {
    if (!configured_ || !running_ || failed_) return;
    if (in_draw_) {
        needs_frame_ = true;
        return;
    }
    ShmBuffer* target = nullptr;
    for (auto& buffer : buffers_) {
        if (!buffer->busy) {
            target = buffer.get();
            break;
        }
    }
    if (target == nullptr) {
        needs_frame_ = true;
        return;
    }
    target->busy = true;
    in_draw_ = true;
    if (theme_counter_++ % 5 == 0) theme_ = pick_theme();
    canvas_.begin(target->pixels, width_, height_, 1.0, input_, theme_);
    draw_frame();
    canvas_.end();
    in_draw_ = false;
    wl_surface_attach(surface_, target->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
    wl_surface_commit(surface_);
    reset_input();
}

void Client::draw_frame() {
    if (mode_ == Mode::layer) {
        canvas_.clear(Color::rgba(0));
        if (draw_) draw_(canvas_);
        return;
    }
    if (!framed_) {
        canvas_.clear(Color::rgba(0));
        if (draw_) draw_(canvas_);
        return;
    }
    canvas_.clear(Color::rgba(0));
    const Rect window{0, 0, static_cast<double>(width_), static_cast<double>(height_)};
    canvas_.fill(window, theme_.bone, 10);
    canvas_.stroke(window, theme_.hair, 10, 1);
    canvas_.line(0, 44, width_, 44, theme_.hair, 1);

    const double close_w = 44;
    const Rect close{width_ - close_w, 0, close_w, 44};
    if (close.contains(input_.x, input_.y)) {
        canvas_.fill(close.inset(8), theme_.tile_on, 14);
    }
    const double cx = width_ - 28;
    canvas_.line(cx - 5, 17, cx + 5, 27, theme_.mute, 1.6);
    canvas_.line(cx + 5, 17, cx - 5, 27, theme_.mute, 1.6);
    canvas_.text(16, 28, title_, 15, theme_.ink);

    if (draw_) draw_(canvas_);
}

void Client::reset_input() {
    input_.pointer_pressed = false;
    input_.pointer_released = false;
    input_.button = 0;
    input_.scroll_x = 0;
    input_.scroll_y = 0;
    input_.keys.clear();
    input_.text.clear();
}

void Client::request_close() {
    if (close_requested_) return;
    close_requested_ = true;
    running_ = false;
    if (close_) close_();
}

void Client::set_title(std::string title) {
    title_ = std::move(title);
    if (toplevel_ != nullptr) xdg_toplevel_set_title(toplevel_, title_.c_str());
    request_frame();
}

void Client::set_tick_ms(int ms) { tick_ms_ = ms; }

void Client::set_input_region(const std::vector<Rect>& rects) {
    bool same = input_region_set_ && rects.size() == input_region_.size();
    if (same) {
        for (std::size_t i = 0; i < rects.size(); ++i) {
            const Rect& a = rects[i];
            const Rect& b = input_region_[i];
            if (a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h) {
                same = false;
                break;
            }
        }
    }
    if (same) return;
    input_region_ = rects;
    input_region_set_ = true;
    apply_input_region();
}

void Client::apply_input_region() {
    if (surface_ == nullptr || !input_region_set_ || compositor_ == nullptr) return;
    wl_region* region = wl_compositor_create_region(compositor_);
    for (const Rect& rect : input_region_) {
        wl_region_add(region, static_cast<std::int32_t>(rect.x),
                      static_cast<std::int32_t>(rect.y),
                      static_cast<std::int32_t>(rect.w),
                      static_cast<std::int32_t>(rect.h));
    }
    wl_surface_set_input_region(surface_, region);
    wl_region_destroy(region);
    wl_surface_commit(surface_);
}

void Client::set_keyboard_mode(KeyboardMode mode) {
    if (layer_surface_ != nullptr && mode == keyboard_mode_) return;
    keyboard_mode_ = mode;
    if (layer_surface_ != nullptr) {
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            layer_surface_, static_cast<std::uint32_t>(mode));
        wl_surface_commit(surface_);
    }
}

int Client::run() {
    if (failed_ || display_ == nullptr) return 1;
    const int fd = wl_display_get_fd(display_);
    while (running_) {
        wl_display_flush(display_);
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        const int timeout = tick_ms_ > 0 ? tick_ms_ : 1000;
        const int ready = poll(&descriptor, 1, timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            failed_ = true;
            break;
        }
        if (ready == 0) {
            const Theme next = pick_theme();
            const bool theme_changed = next.dark != theme_.dark;
            if (theme_changed) theme_ = next;
            if (tick_) tick_();
            if (tick_ || theme_changed) request_frame();
            continue;
        }
        if ((descriptor.revents & POLLIN) != 0) {
            if (wl_display_dispatch(display_) < 0) {
                failed_ = true;
                break;
            }
        } else if ((descriptor.revents & (POLLERR | POLLHUP)) != 0) {
            break;
        }
    }
    destroy();
    return failed_ ? 1 : 0;
}

void Client::destroy() {
    if (display_ == nullptr) return;
    destroy_buffers();
    if (frame_callback_ != nullptr) {
        wl_callback_destroy(frame_callback_);
        frame_callback_ = nullptr;
    }
    if (toplevel_ != nullptr) xdg_toplevel_destroy(toplevel_);
    if (xdg_surface_ != nullptr) xdg_surface_destroy(xdg_surface_);
    if (layer_surface_ != nullptr) zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_ != nullptr) wl_surface_destroy(surface_);
    if (pointer_ != nullptr) wl_pointer_destroy(pointer_);
    if (keyboard_ != nullptr) wl_keyboard_destroy(keyboard_);
    if (seat_ != nullptr) wl_seat_destroy(seat_);
    if (xkb_state_ != nullptr) xkb_state_unref(xkb_state_);
    if (xkb_keymap_ != nullptr) xkb_keymap_unref(xkb_keymap_);
    if (xkb_context_ != nullptr) xkb_context_unref(xkb_context_);
    if (shm_ != nullptr) wl_shm_destroy(shm_);
    if (compositor_ != nullptr) wl_compositor_destroy(compositor_);
    if (wm_base_ != nullptr) xdg_wm_base_destroy(wm_base_);
    if (layer_shell_ != nullptr) zwlr_layer_shell_v1_destroy(layer_shell_);
    if (registry_ != nullptr) wl_registry_destroy(registry_);
    wl_display_disconnect(display_);
    display_ = nullptr;
}

struct App::Impl {
    App& app;
    Client client;

    Impl(App& app_ref, std::string title, int width, int height) : app(app_ref) {
        client.set_draw([this](Canvas& canvas) { app.draw(canvas); });
        client.set_close([this] { app.on_close(); });
        if (!client.init_xdg(width, height, std::move(title))) {
            failed = true;
        }
    }

    int run() {
        if (failed) return 1;
        return client.run();
    }

    bool failed = false;
};

App::App(std::string title, int width, int height)
    : impl_(std::make_unique<Impl>(*this, std::move(title), width, height)) {}

App::~App() = default;

void App::request_frame() { impl_->client.request_frame(); }

Rect App::content_rect() const { return impl_->client.content_rect(); }

void App::set_framed(bool framed) { impl_->client.set_framed(framed); }

void App::set_title(std::string title) { impl_->client.set_title(std::move(title)); }

int App::run() { return impl_->run(); }

struct LayerApp::Impl {
    LayerApp& app;
    Client client;

    Impl(LayerApp& app_ref, std::string name, int width, int height,
         std::uint32_t anchor, Layer layer, KeyboardMode keyboard)
        : app(app_ref) {
        client.set_draw([this](Canvas& canvas) { app.draw(canvas); });
        client.set_close([this] { app.on_close(); });
        client.set_tick([this] { app.on_tick(); });
        if (!client.init_layer(std::move(name), width, height, anchor, layer, keyboard)) {
            failed = true;
        }
    }

    int run() {
        if (failed) return 1;
        return client.run();
    }

    bool failed = false;
};

LayerApp::LayerApp(std::string name, int width, int height, std::uint32_t anchor,
                   Layer layer, KeyboardMode keyboard)
    : impl_(std::make_unique<Impl>(*this, std::move(name), width, height, anchor,
                                   layer, keyboard)) {}

LayerApp::~LayerApp() = default;

void LayerApp::set_tick_ms(int ms) { impl_->client.set_tick_ms(ms); }

void LayerApp::set_input_region(const std::vector<Rect>& rects) {
    impl_->client.set_input_region(rects);
}

void LayerApp::set_keyboard_mode(KeyboardMode mode) {
    impl_->client.set_keyboard_mode(mode);
}

void LayerApp::request_frame() { impl_->client.request_frame(); }

int LayerApp::run() { return impl_->run(); }

}  // namespace finn::ui
