// Wayland client and surface management engine for libfinnui.
// Implements event loops, shared-memory double buffering, xdg-shell toplevels,
// layer-shell surfaces, and keyboard/pointer input dispatching.

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

// Re-evaluates appearance settings periodically during event loop idle ticks so
// theme toggles take immediate effect without restarting running client apps.
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

std::string lowercase_hyphenated(std::string_view str) {
    std::string result;
    bool last_was_dash = false;
    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            last_was_dash = false;
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!result.empty() && !last_was_dash) {
                result.push_back('-');
                last_was_dash = true;
            }
        }
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result.empty() ? "finn.app" : result;
}

}  // namespace

class Client {
public:
    struct OutputEntry {
        Client* client = nullptr;
        wl_output* output = nullptr;
        std::uint32_t name = 0;
        std::int32_t scale = 1;
        std::int32_t mode_width = 0;
        std::int32_t mode_height = 0;
    };

    enum class Mode { xdg, layer };

    Client() = default;
    ~Client() { destroy(); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool init_xdg(int width, int height, std::string title, std::string app_id = {});
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
    static void output_geometry(void* data, wl_output* output, int32_t x, int32_t y,
                                int32_t physical_width, int32_t physical_height,
                                int32_t subpixel, const char* make, const char* model,
                                int32_t transform);
    static void output_mode(void* data, wl_output* output, uint32_t flags,
                            int32_t width, int32_t height, int32_t refresh);
    static void output_done(void* data, wl_output* output);
    static void output_scale(void* data, wl_output* output, int32_t factor);
    static void output_name(void* data, wl_output* output, const char* name);
    static void output_description(void* data, wl_output* output, const char* description);
    static void touch_down(void* data, wl_touch* touch, std::uint32_t serial,
                           std::uint32_t time, wl_surface* surface,
                           int32_t id, wl_fixed_t x, wl_fixed_t y);
    static void touch_up(void* data, wl_touch* touch, std::uint32_t serial,
                         std::uint32_t time, int32_t id);
    static void touch_motion(void* data, wl_touch* touch, std::uint32_t time,
                             int32_t id, wl_fixed_t x, wl_fixed_t y);
    static void touch_frame(void* data, wl_touch* touch);
    static void touch_cancel(void* data, wl_touch* touch);
    static void touch_shape(void* data, wl_touch* touch, int32_t id,
                            wl_fixed_t major, wl_fixed_t minor);
    static void touch_orientation(void* data, wl_touch* touch, int32_t id,
                                  wl_fixed_t orientation);

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
    void update_scale();
    void handle_pointer_motion(double x, double y);
    void handle_pointer_press(std::uint32_t button, std::uint32_t serial);
    void handle_pointer_release();

    Mode mode_ = Mode::xdg;
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_touch* touch_ = nullptr;
    int32_t touch_id_ = -1;
    wl_keyboard* keyboard_ = nullptr;
    xdg_wm_base* wm_base_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_callback* frame_callback_ = nullptr;
    int tick_ms_ = 0;

    std::vector<std::unique_ptr<OutputEntry>> outputs_;
    std::int32_t scale_ = 1;
    std::vector<std::unique_ptr<ShmBuffer>> buffers_;
    Input input_;
    Theme theme_;
    Canvas canvas_;
    std::string title_;
    std::string app_id_;
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
    wl_output_listener output_listener_{};
    wl_seat_listener seat_listener_{};
    wl_pointer_listener pointer_listener_{};
    wl_touch_listener touch_listener_{};
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
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        auto entry = std::make_unique<OutputEntry>();
        entry->client = client;
        entry->name = name;
        entry->output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 3u)));
        wl_output_add_listener(entry->output, &client->output_listener_, entry.get());
        client->outputs_.push_back(std::move(entry));
    }
}

void Client::registry_remove(void* data, wl_registry* registry, std::uint32_t name) {
    (void)registry;
    Client* client = static_cast<Client*>(data);
    for (auto it = client->outputs_.begin(); it != client->outputs_.end(); ++it) {
        if ((*it)->name == name) {
            if ((*it)->output != nullptr) {
                wl_output_destroy((*it)->output);
            }
            client->outputs_.erase(it);
            client->update_scale();
            break;
        }
    }
}

void Client::output_geometry(void* data, wl_output* output, int32_t x, int32_t y,
                             int32_t physical_width, int32_t physical_height,
                             int32_t subpixel, const char* make, const char* model,
                             int32_t transform) {
    (void)data;
    (void)output;
    (void)x;
    (void)y;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;
    (void)make;
    (void)model;
    (void)transform;
}

void Client::output_mode(void* data, wl_output* output, uint32_t flags,
                         int32_t width, int32_t height, int32_t refresh) {
    (void)output;
    (void)refresh;
    auto* entry = static_cast<OutputEntry*>(data);
    if (entry != nullptr && (flags & WL_OUTPUT_MODE_CURRENT) != 0) {
        entry->mode_width = width;
        entry->mode_height = height;
    }
}

void Client::output_done(void* data, wl_output* output) {
    (void)data;
    (void)output;
}

void Client::output_scale(void* data, wl_output* output, int32_t factor) {
    (void)output;
    auto* entry = static_cast<OutputEntry*>(data);
    if (entry != nullptr) {
        entry->scale = factor;
        if (entry->client != nullptr) {
            entry->client->update_scale();
        }
    }
}

void Client::output_name(void* data, wl_output* output, const char* name) {
    (void)data;
    (void)output;
    (void)name;
}

void Client::output_description(void* data, wl_output* output, const char* description) {
    (void)data;
    (void)output;
    (void)description;
}

// Recomputes maximum scale factor across all bound outputs and recreates buffer pools
// when the display scale changes at runtime so content matches screen DPI.
void Client::update_scale() {
    std::int32_t max_scale = 1;
    for (const auto& out : outputs_) {
        if (out->scale > max_scale) {
            max_scale = out->scale;
        }
    }
    if (max_scale < 1) max_scale = 1;
    if (scale_ != max_scale) {
        scale_ = max_scale;
        if (surface_ != nullptr) {
            wl_surface_set_buffer_scale(surface_, scale_);
        }
        if (configured_) {
            destroy_buffers();
            create_buffers();
            request_frame();
        }
    }
}

void Client::connect_display() {
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        std::fprintf(stderr, "libfinnui: cannot connect to WAYLAND_DISPLAY\n");
        failed_ = true;
        return;
    }

    output_listener_ = {};
    output_listener_.geometry = output_geometry;
    output_listener_.mode = output_mode;
    output_listener_.done = output_done;
    output_listener_.scale = output_scale;
    output_listener_.name = output_name;
    output_listener_.description = output_description;

    touch_listener_ = {};
    touch_listener_.down = touch_down;
    touch_listener_.up = touch_up;
    touch_listener_.motion = touch_motion;
    touch_listener_.frame = touch_frame;
    touch_listener_.cancel = touch_cancel;
    touch_listener_.shape = touch_shape;
    touch_listener_.orientation = touch_orientation;

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
    } else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && client->pointer_ != nullptr) {
        wl_pointer_destroy(client->pointer_);
        client->pointer_ = nullptr;
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
    } else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && client->keyboard_ != nullptr) {
        wl_keyboard_destroy(client->keyboard_);
        client->keyboard_ = nullptr;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0 && client->touch_ == nullptr) {
        client->touch_ = wl_seat_get_touch(seat);
        wl_touch_add_listener(client->touch_, &client->touch_listener_, client);
    } else if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) == 0 && client->touch_ != nullptr) {
        wl_touch_destroy(client->touch_);
        client->touch_ = nullptr;
        client->touch_id_ = -1;
    }
}

void Client::create_surface() {
    surface_ = wl_compositor_create_surface(compositor_);
    if (surface_ == nullptr) {
        failed_ = true;
        return;
    }
    wl_surface_set_buffer_scale(surface_, scale_);

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
        xdg_toplevel_set_app_id(toplevel_, !app_id_.empty() ? app_id_.c_str() : "finn.app");
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

bool Client::init_xdg(int width, int height, std::string title, std::string app_id) {
    mode_ = Mode::xdg;
    requested_width_ = width > 0 ? width : 900;
    requested_height_ = height > 0 ? height : 600;
    title_ = std::move(title);
    if (!app_id.empty()) {
        app_id_ = std::move(app_id);
    } else {
        app_id_ = lowercase_hyphenated(title_);
    }
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

void Client::handle_pointer_motion(double x, double y) {
    input_.x = x;
    input_.y = y;
    request_frame();
}

void Client::handle_pointer_press(std::uint32_t button, std::uint32_t serial) {
    input_.pointer_down = true;
    input_.pointer_pressed = true;
    input_.button = button;

    if (mode_ == Mode::xdg && framed_ && button == BTN_LEFT && toplevel_ != nullptr) {
        const Rect pill{static_cast<double>(width_ - 83 - 8), 8.0, 83.0, 24.0};
        const Rect min_btn{pill.x + 2, pill.y + 2, 25, 20};
        const Rect max_btn{pill.x + 29, pill.y + 2, 25, 20};
        const Rect close_btn{pill.x + 56, pill.y + 2, 25, 20};

        if (close_btn.contains(input_.x, input_.y)) {
            request_close();
            return;
        }
        if (max_btn.contains(input_.x, input_.y)) {
            maximized_ = !maximized_;
            if (maximized_) {
                xdg_toplevel_set_maximized(toplevel_);
            } else {
                xdg_toplevel_unset_maximized(toplevel_);
            }
            request_frame();
            return;
        }
        if (min_btn.contains(input_.x, input_.y)) {
            // Compositor does not implement request_minimize in v1.
            // Button is disabled per AGENTS.md / contract.md: refuse presses.
            return;
        }
        if (pill.contains(input_.x, input_.y)) {
            return;
        }
        for (const auto& r : canvas_.interactive_rects()) {
            if (r.contains(input_.x, input_.y)) {
                request_frame();
                return;
            }
        }
        if (input_.y < 44) {
            const double now = now_seconds();
            if (now - last_title_press_ < 0.35) {
                maximized_ = !maximized_;
                if (maximized_) {
                    xdg_toplevel_set_maximized(toplevel_);
                } else {
                    xdg_toplevel_unset_maximized(toplevel_);
                }
            } else if (seat_ != nullptr) {
                xdg_toplevel_move(toplevel_, seat_, serial);
            }
            last_title_press_ = now;
        }
    }
    request_frame();
}

void Client::handle_pointer_release() {
    input_.pointer_down = false;
    input_.pointer_released = true;
    request_frame();
}

void Client::pointer_enter(void* data, wl_pointer* pointer, std::uint32_t serial,
                           wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    (void)serial;
    (void)surface;
    static_cast<Client*>(data)->handle_pointer_motion(wl_fixed_to_double(sx),
                                                      wl_fixed_to_double(sy));
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
    static_cast<Client*>(data)->handle_pointer_motion(wl_fixed_to_double(sx),
                                                      wl_fixed_to_double(sy));
}

void Client::pointer_button(void* data, wl_pointer* pointer, std::uint32_t serial,
                            std::uint32_t time, std::uint32_t button,
                            std::uint32_t state) {
    (void)pointer;
    (void)time;
    Client* client = static_cast<Client*>(data);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        client->handle_pointer_press(button, serial);
    } else {
        client->handle_pointer_release();
    }
}

void Client::touch_down(void* data, wl_touch* touch, std::uint32_t serial,
                        std::uint32_t time, wl_surface* surface,
                        int32_t id, wl_fixed_t x, wl_fixed_t y) {
    (void)touch;
    (void)time;
    (void)surface;
    Client* client = static_cast<Client*>(data);
    // Maps the primary touch contact to mouse button press semantics.
    if (client->touch_id_ < 0) {
        client->touch_id_ = id;
        client->handle_pointer_motion(wl_fixed_to_double(x), wl_fixed_to_double(y));
        client->handle_pointer_press(BTN_LEFT, serial);
    }
}

void Client::touch_up(void* data, wl_touch* touch, std::uint32_t serial,
                      std::uint32_t time, int32_t id) {
    (void)touch;
    (void)serial;
    (void)time;
    Client* client = static_cast<Client*>(data);
    if (client->touch_id_ == id) {
        client->touch_id_ = -1;
        client->handle_pointer_release();
    }
}

void Client::touch_motion(void* data, wl_touch* touch, std::uint32_t time,
                          int32_t id, wl_fixed_t x, wl_fixed_t y) {
    (void)touch;
    (void)time;
    Client* client = static_cast<Client*>(data);
    if (client->touch_id_ == id) {
        client->handle_pointer_motion(wl_fixed_to_double(x), wl_fixed_to_double(y));
    }
}

void Client::touch_frame(void* data, wl_touch* touch) {
    (void)touch;
    static_cast<Client*>(data)->request_frame();
}

void Client::touch_cancel(void* data, wl_touch* touch) {
    (void)touch;
    Client* client = static_cast<Client*>(data);
    // Compositor cancelled touch gesture; reset touch state without firing clicks.
    if (client->touch_id_ >= 0) {
        client->touch_id_ = -1;
        client->input_.pointer_down = false;
        client->input_.pointer_pressed = false;
        client->input_.pointer_released = false;
        client->input_.x = -1;
        client->input_.y = -1;
        client->request_frame();
    }
}

void Client::touch_shape(void* data, wl_touch* touch, int32_t id,
                         wl_fixed_t major, wl_fixed_t minor) {
    (void)data;
    (void)touch;
    (void)id;
    (void)major;
    (void)minor;
}

void Client::touch_orientation(void* data, wl_touch* touch, int32_t id,
                               wl_fixed_t orientation) {
    (void)data;
    (void)touch;
    (void)id;
    (void)orientation;
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

// Allocates anonymous memory files via memfd_create for zero-copy Wayland shm
// buffer sharing between client render loops and the compositor.
bool Client::create_buffers() {
    destroy_buffers();
    if (width_ <= 0 || height_ <= 0 || shm_ == nullptr) return false;
    const int buf_width = width_ * scale_;
    const int buf_height = height_ * scale_;
    if (buf_width > 16384 || buf_height > 16384) {
        std::fprintf(stderr, "libfinnui: window is too large: %dx%d\n", buf_width, buf_height);
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(buf_width) * 4;
    const std::size_t bytes = stride * static_cast<std::size_t>(buf_height);
    for (int i = 0; i < 2; ++i) {
        const int fd = memfd_create("finn-ui", MFD_CLOEXEC);
        if (fd < 0) {
            std::fprintf(stderr, "libfinnui: memfd_create failed: %s\n", std::strerror(errno));
            return false;
        }
        if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
            std::fprintf(stderr, "libfinnui: ftruncate failed: %s\n", std::strerror(errno));
            close(fd);
            return false;
        }
        void* map = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            std::fprintf(stderr, "libfinnui: mmap failed: %s\n", std::strerror(errno));
            close(fd);
            return false;
        }
        wl_shm_pool* pool = wl_shm_create_pool(shm_, fd, static_cast<std::int32_t>(bytes));
        if (pool == nullptr) {
            std::fprintf(stderr, "libfinnui: wl_shm_create_pool failed\n");
            munmap(map, bytes);
            close(fd);
            return false;
        }
        wl_buffer* buffer = wl_shm_pool_create_buffer(
            pool, 0, buf_width, buf_height, static_cast<std::int32_t>(stride),
            WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        if (buffer == nullptr) {
            std::fprintf(stderr, "libfinnui: create_buffer failed\n");
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
    const int buf_width = width_ * scale_;
    const int buf_height = height_ * scale_;
    target->busy = true;
    in_draw_ = true;
    if (theme_counter_++ % 5 == 0) theme_ = pick_theme();

    int screen_mode_w = 0;
    int screen_mode_h = 0;
    for (const auto& out : outputs_) {
        if (out->mode_width > screen_mode_w) {
            screen_mode_w = out->mode_width;
            screen_mode_h = out->mode_height;
        }
    }
    const double screen_w = screen_mode_w > 0
        ? (screen_mode_w / static_cast<double>(scale_))
        : static_cast<double>(width_);
    const double screen_h = screen_mode_h > 0
        ? (screen_mode_h / static_cast<double>(scale_))
        : static_cast<double>(height_);
    double surface_x = 0;
    double surface_y = 0;
    if (mode_ == Mode::layer) {
        if (width_ < screen_w) {
            surface_x = (screen_w - width_) / 2.0;
        }
        if (height_ < screen_h) {
            surface_y = screen_h - height_;
        }
    }
    canvas_.set_screen_geometry(screen_w, screen_h, surface_x, surface_y);

    canvas_.begin(target->pixels, buf_width, buf_height, static_cast<double>(scale_), input_, theme_);
    draw_frame();
    if (canvas_.animating()) needs_frame_ = true;
    canvas_.end();
    in_draw_ = false;
    wl_surface_set_buffer_scale(surface_, scale_);
    wl_surface_attach(surface_, target->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, buf_width, buf_height);
    wl_surface_commit(surface_);
    reset_input();
}

// Synthesizes client-side window chrome per Figma design specification:
// borderless rounded rectangle, floating top-right window controls pill,
// and top-left reserved application title without a titlebar dividing strip.
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
    const Color tint = theme_.bone.with_alpha(theme_.dark ? 214 : 178);
    canvas_.glass(window, 8, tint, 15);
    canvas_.stroke(window, theme_.hair, 8, 1);

    if (draw_) draw_(canvas_);

    // Floating window controls pill (Figma node 240:352 / SPEC.md Section 5)
    // 83x24 positioned top-right at margin top 8, margin right 8
    const Rect pill{static_cast<double>(width_ - 83 - 8), 8.0, 83.0, 24.0};

    // Soft drop shadow (Figma drop shadow: radius 20, rgba(0, 0, 0, 0.10), offset y 4)
    canvas_.fill(Rect{pill.x, pill.y + 2, pill.w, pill.h}, Color::rgba(0x14000000), 6);
    canvas_.fill(Rect{pill.x, pill.y + 4, pill.w, pill.h}, Color::rgba(0x0A000000), 8);
    // Container background (Figma: solid white, radius 4)
    canvas_.fill(pill, Color::rgb(0xFFFFFF), 4);

    // Three 25x20 buttons with padding 2 and item spacing 2
    const Rect min_btn{pill.x + 2, pill.y + 2, 25, 20};
    const Rect max_btn{pill.x + 29, pill.y + 2, 25, 20};
    const Rect close_btn{pill.x + 56, pill.y + 2, 25, 20};

    // Button 1: Minimize (DISABLED: compositor does not implement request_minimize)
    canvas_.fill(min_btn, Color::rgb(0xFFFFFF), 2);
    // Glyph: minus-sign (node 240:185), width 9.33, disabled color theme_.mute
    canvas_.line(min_btn.x + 7.83, min_btn.y + 10.0, min_btn.x + 17.17, min_btn.y + 10.0,
                 theme_.mute, 1.17);

    // Button 2: Maximize
    const bool max_hover = max_btn.contains(input_.x, input_.y);
    canvas_.fill(max_btn, Color::rgb(0xFFFFFF), 2);
    if (max_hover) {
        canvas_.fill(max_btn, Color::rgba(0x08000000), 2);
    }
    // Glyph: expand (node 240:181/209), two opposing corner brackets 4.08x4.08, stroke #141B34
    const Color max_glyph = Color::rgb(0x141B34);
    canvas_.line(max_btn.x + 8.42, max_btn.y + 5.92, max_btn.x + 12.50, max_btn.y + 5.92,
                 max_glyph, 1.17);
    canvas_.line(max_btn.x + 8.42, max_btn.y + 5.92, max_btn.x + 8.42, max_btn.y + 10.00,
                 max_glyph, 1.17);
    canvas_.line(max_btn.x + 12.50, max_btn.y + 14.08, max_btn.x + 16.58, max_btn.y + 14.08,
                 max_glyph, 1.17);
    canvas_.line(max_btn.x + 16.58, max_btn.y + 10.00, max_btn.x + 16.58, max_btn.y + 14.08,
                 max_glyph, 1.17);

    // Button 3: Close
    const bool close_hover = close_btn.contains(input_.x, input_.y);
    const Color close_bg = close_hover ? Color::rgb(0xFF0202) : Color::rgb(0xFFFFFF);
    const Color close_glyph = close_hover ? Color::rgb(0xFFFFFF) : Color::rgb(0x141B34);
    canvas_.fill(close_btn, close_bg, 2);
    // Glyph: close-button (node 240:179/224), 7x7 X
    canvas_.line(close_btn.x + 9.0, close_btn.y + 6.5, close_btn.x + 16.0, close_btn.y + 13.5,
                 close_glyph, 1.17);
    canvas_.line(close_btn.x + 16.0, close_btn.y + 6.5, close_btn.x + 9.0, close_btn.y + 13.5,
                 close_glyph, 1.17);

    // Top left area reserved for App's name (SPEC.md Section 6: x: 22, y: 18, size 16)
    canvas_.text(22, 29, title_, 16, theme_.ink);
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
    if (toplevel_ != nullptr) {
        xdg_toplevel_set_title(toplevel_, title_.c_str());
        if (app_id_.empty() && !title_.empty()) {
            app_id_ = lowercase_hyphenated(title_);
            xdg_toplevel_set_app_id(toplevel_, app_id_.c_str());
        }
    }
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
    for (auto& out : outputs_) {
        if (out->output != nullptr) wl_output_destroy(out->output);
    }
    outputs_.clear();
    if (frame_callback_ != nullptr) {
        wl_callback_destroy(frame_callback_);
        frame_callback_ = nullptr;
    }
    if (toplevel_ != nullptr) xdg_toplevel_destroy(toplevel_);
    if (xdg_surface_ != nullptr) xdg_surface_destroy(xdg_surface_);
    if (layer_surface_ != nullptr) zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_ != nullptr) wl_surface_destroy(surface_);
    if (pointer_ != nullptr) wl_pointer_destroy(pointer_);
    if (touch_ != nullptr) wl_touch_destroy(touch_);
    touch_id_ = -1;
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

    Impl(App& app_ref, std::string title, int width, int height, std::string app_id) : app(app_ref) {
        client.set_draw([this](Canvas& canvas) { app.draw(canvas); });
        client.set_close([this] { app.on_close(); });
        if (!client.init_xdg(width, height, std::move(title), std::move(app_id))) {
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
    : impl_(std::make_unique<Impl>(*this, std::move(title), width, height, "")) {}

App::App(std::string title, int width, int height, std::string app_id)
    : impl_(std::make_unique<Impl>(*this, std::move(title), width, height, std::move(app_id))) {}

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
