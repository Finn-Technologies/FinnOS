// Implementation of finn-comp display server using wlroots.
// Dispatches seat input, orders scene trees across shell layers and toplevels,
// and enforces security boundaries by isolating socket permissions to group finn.

#include "comp/server.hpp"
#include "comp/wlroots.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <grp.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <systemd/sd-daemon.h>
#include <xkbcommon/xkbcommon.h>

namespace finn {
namespace {

int64_t monotonic_ms() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

double ease_out_cubic(double t) {
    t = std::max(0.0, std::min(1.0, t));
    const double left = 1.0 - t;
    return 1.0 - left * left * left;
}

bool reduced_motion() {
    const char* value = std::getenv("FINN_REDUCED_MOTION");
    return value != nullptr && *value != '\0' && *value != '0';
}

// Checks environment overrides followed by user settings to ensure dark theme
// is respected on startup before any layer-shell surfaces or windows map.
bool prefers_dark() {
    if (const char* value = std::getenv("FINN_DARK"); value != nullptr) {
        const std::string text(value);
        if (text == "1" || text == "true" || text == "dark") return true;
        if (text == "0" || text == "false" || text == "light") return false;
    }
    const char* home = std::getenv("HOME");
    const std::string user_path =
        home != nullptr && *home != '\0'
            ? std::string(home) + "/.config/finn/settings"
            : "/home/finn/.config/finn/settings";
    for (const std::string& path :
         {user_path, std::string("/home/finn/.config/finn/settings"),
          std::string("/var/lib/finn/settings")}) {
        std::ifstream settings(path);
        std::string line;
        while (std::getline(settings, line)) {
            if (line == "dark=1") return true;
            if (line == "dark=0") return false;
        }
    }
    return false;
}

void detach(wl_listener& listener) {
    if (listener.link.next == nullptr) return;
    wl_list_remove(&listener.link);
    listener.link.next = nullptr;
    listener.link.prev = nullptr;
}

void remove_listener(wl_listener& listener) { detach(listener); }

}  // namespace

struct Server::Output {
    Server* server = nullptr;
    wlr_output* output = nullptr;
    wl_listener frame{};
    wl_listener request_state{};
    wl_listener destroy{};

    ~Output() {
        detach(frame);
        detach(request_state);
        detach(destroy);
    }
};

struct Server::Toplevel {
    Server* server = nullptr;
    wlr_xdg_toplevel* xdg_toplevel = nullptr;
    wlr_scene_tree* scene_tree = nullptr;
    wl_listener map{};
    wl_listener unmap{};
    wl_listener commit{};
    wl_listener destroy{};
    wl_listener request_move{};
    wl_listener request_resize{};
    wl_listener request_maximize{};
    bool opened_once = false;
    bool opening = false;
    int64_t open_start_ms = 0;
    int open_from_y = 0;
    int open_to_y = 0;
    bool maximized = false;
    int restore_x = 0;
    int restore_y = 0;
    int restore_w = 0;
    int restore_h = 0;

    ~Toplevel() {
        detach(map);
        detach(unmap);
        detach(commit);
        detach(destroy);
        detach(request_move);
        detach(request_resize);
        detach(request_maximize);
    }
};

struct Server::Keyboard {
    Server* server = nullptr;
    wlr_keyboard* keyboard = nullptr;
    wlr_input_device* device = nullptr;
    wl_listener modifiers{};
    wl_listener key{};
    wl_listener destroy{};

    ~Keyboard() {
        detach(modifiers);
        detach(key);
        detach(destroy);
    }
};

struct Server::Layer {
    Server* server = nullptr;
    wlr_layer_surface_v1* layer = nullptr;
    wlr_scene_layer_surface_v1* scene = nullptr;
    wl_listener map{};
    wl_listener commit{};
    wl_listener destroy{};
    bool configured = false;

    ~Layer() {
        detach(map);
        detach(commit);
        detach(destroy);
    }
};

Server::Server() = default;

Server::~Server() { shutdown(); }

int Server::run() {
    if (!init()) {
        shutdown();
        return 1;
    }
    wl_display_run(display_);
    shutdown();
    return 0;
}

bool Server::init() {
    wlr_log_init(WLR_INFO, nullptr);

    display_ = wl_display_create();
    if (display_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: wl_display_create failed");
        return false;
    }

    backend_ = wlr_backend_autocreate(wl_display_get_event_loop(display_),
                                      nullptr);
    if (backend_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no backend");
        return false;
    }

    renderer_ = wlr_renderer_autocreate(backend_);
    if (renderer_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no renderer");
        return false;
    }
    wlr_renderer_init_wl_display(renderer_, display_);

    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    if (allocator_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no allocator");
        return false;
    }

    wlr_compositor_create(display_, 5, renderer_);
    wlr_subcompositor_create(display_);
    wlr_data_device_manager_create(display_);
    wlr_screencopy_manager_v1_create(display_);

    output_layout_ = wlr_output_layout_create(display_);
    wlr_xdg_output_manager_v1_create(display_, output_layout_);
    scene_ = wlr_scene_create();
    scene_layout_ = wlr_scene_attach_output_layout(scene_, output_layout_);

    /* The scene is ordered once, here: background, background layer, bottom
     * layer, ordinary windows, top layer, overlay. A layer surface therefore
     * stays above app windows without the shell or the compositor knowing an
     * app id. */
    const bool dark = prefers_dark();
    float background_color[4] = {0.93f, 0.95f, 0.98f, 1.0f};
    if (dark) {
        background_color[0] = 0.086f;
        background_color[1] = 0.094f;
        background_color[2] = 0.114f;
    }
    background_ = wlr_scene_rect_create(&scene_->tree, 1, 1, background_color);
    layer_trees_[0] = wlr_scene_tree_create(&scene_->tree);
    layer_trees_[1] = wlr_scene_tree_create(&scene_->tree);
    toplevel_tree_ = wlr_scene_tree_create(&scene_->tree);
    layer_trees_[2] = wlr_scene_tree_create(&scene_->tree);
    layer_trees_[3] = wlr_scene_tree_create(&scene_->tree);

    new_output_.notify = on_new_output;
    wl_signal_add(&backend_->events.new_output, &new_output_);

    xdg_shell_ = wlr_xdg_shell_create(display_, 3);
    if (xdg_shell_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no xdg-shell");
        return false;
    }
    new_toplevel_.notify = on_new_toplevel;
    wl_signal_add(&xdg_shell_->events.new_toplevel, &new_toplevel_);

    layer_shell_ = wlr_layer_shell_v1_create(display_, 4);
    if (layer_shell_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no layer-shell");
        return false;
    }
    new_layer_.notify = on_new_layer;
    wl_signal_add(&layer_shell_->events.new_surface, &new_layer_);

    cursor_ = wlr_cursor_create();
    if (cursor_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no cursor");
        return false;
    }
    wlr_cursor_attach_output_layout(cursor_, output_layout_);
    cursor_manager_ = wlr_xcursor_manager_create(nullptr, 24);
    if (cursor_manager_ != nullptr) wlr_xcursor_manager_load(cursor_manager_, 1);

    cursor_motion_.notify = on_cursor_motion;
    wl_signal_add(&cursor_->events.motion, &cursor_motion_);
    cursor_motion_absolute_.notify = on_cursor_motion_absolute;
    wl_signal_add(&cursor_->events.motion_absolute, &cursor_motion_absolute_);
    cursor_button_.notify = on_cursor_button;
    wl_signal_add(&cursor_->events.button, &cursor_button_);
    cursor_axis_.notify = on_cursor_axis;
    wl_signal_add(&cursor_->events.axis, &cursor_axis_);
    cursor_frame_.notify = on_cursor_frame;
    wl_signal_add(&cursor_->events.frame, &cursor_frame_);
    touch_down_.notify = on_touch_down;
    wl_signal_add(&cursor_->events.touch_down, &touch_down_);
    touch_up_.notify = on_touch_up;
    wl_signal_add(&cursor_->events.touch_up, &touch_up_);
    touch_motion_.notify = on_touch_motion;
    wl_signal_add(&cursor_->events.touch_motion, &touch_motion_);
    touch_cancel_.notify = on_touch_cancel;
    wl_signal_add(&cursor_->events.touch_cancel, &touch_cancel_);
    touch_frame_.notify = on_touch_frame;
    wl_signal_add(&cursor_->events.touch_frame, &touch_frame_);

    seat_ = wlr_seat_create(display_, "seat0");
    if (seat_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no seat");
        return false;
    }
    request_cursor_.notify = on_request_cursor;
    wl_signal_add(&seat_->events.request_set_cursor, &request_cursor_);
    pointer_focus_change_.notify = on_pointer_focus_change;
    wl_signal_add(&seat_->pointer_state.events.focus_change,
                  &pointer_focus_change_);
    request_set_selection_.notify = on_request_set_selection;
    wl_signal_add(&seat_->events.request_set_selection,
                  &request_set_selection_);

    new_input_.notify = on_new_input;
    wl_signal_add(&backend_->events.new_input, &new_input_);

    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || *runtime == '\0') {
        wlr_log(WLR_ERROR,
                "finn-comp: XDG_RUNTIME_DIR is not set; the display socket "
                "has nowhere to live");
        return false;
    }
    const std::string socket_path = std::string(runtime) + "/wayland-0";
    struct stat socket_status{};
    if (lstat(socket_path.c_str(), &socket_status) == 0) {
        if (!S_ISSOCK(socket_status.st_mode)) {
            wlr_log(WLR_ERROR, "finn-comp: %s exists and is not a socket",
                    socket_path.c_str());
            return false;
        }
        if (unlink(socket_path.c_str()) != 0) {
            wlr_log(WLR_ERROR, "finn-comp: cannot remove a stale socket: %s",
                    std::strerror(errno));
            return false;
        }
    }
    if (wl_display_add_socket(display_, "wayland-0") != 0) {
        wlr_log(WLR_ERROR, "finn-comp: cannot bind wayland-0: %s",
                std::strerror(errno));
        return false;
    }
    if (chmod(socket_path.c_str(), 0660) != 0) {
        wlr_log(WLR_ERROR, "finn-comp: cannot chmod %s: %s", socket_path.c_str(),
                std::strerror(errno));
    }
    if (group* finn_group = getgrnam("finn"); finn_group != nullptr) {
        if (chown(socket_path.c_str(), static_cast<uid_t>(-1),
                  finn_group->gr_gid) != 0) {
            wlr_log(WLR_ERROR, "finn-comp: cannot chown %s: %s",
                    socket_path.c_str(), std::strerror(errno));
        }
    }

    if (!wlr_backend_start(backend_)) {
        wlr_log(WLR_ERROR, "finn-comp: backend failed to start");
        return false;
    }

    wlr_log(WLR_INFO, "finn-comp: listening on WAYLAND_DISPLAY=wayland-0");
    wl_event_loop* loop = wl_display_get_event_loop(display_);
    watchdog_ = wl_event_loop_add_timer(
        loop,
        [](void* data) -> int {
            Server* server = static_cast<Server*>(data);
            sd_notify(0, "WATCHDOG=1");
            wl_event_source_timer_update(server->watchdog_, 5000);
            return 0;
        },
        this);
    if (watchdog_ != nullptr) wl_event_source_timer_update(watchdog_, 5000);
    theme_source_ = wl_event_loop_add_timer(
        loop,
        [](void* data) -> int {
            Server* server = static_cast<Server*>(data);
            server->update_background_theme();
            wl_event_source_timer_update(server->theme_source_, 1000);
            return 0;
        },
        this);
    if (theme_source_ != nullptr) wl_event_source_timer_update(theme_source_, 1000);
    anim_source_ = wl_event_loop_add_timer(loop, on_animation, this);
    sd_notify(0, "READY=1");
    return true;
}

// Offsets newly mapped windows slightly downwards and animates them to rest,
// giving visual feedback of window instantiation unless reduced motion is set.
void Server::begin_open_animation(Toplevel* toplevel) {
    if (toplevel == nullptr || toplevel->opened_once || reduced_motion()) {
        if (toplevel != nullptr) toplevel->opened_once = true;
        return;
    }
    toplevel->opened_once = true;
    toplevel->opening = true;
    toplevel->open_start_ms = monotonic_ms();
    toplevel->open_to_y = toplevel->scene_tree->node.y;
    toplevel->open_from_y = toplevel->open_to_y + 14;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->scene_tree->node.x,
                                toplevel->open_from_y);
    if (anim_source_ != nullptr) wl_event_source_timer_update(anim_source_, 16);
}

void Server::animate_windows() {
    const int64_t now = monotonic_ms();
    bool active = false;
    for (auto& entry : toplevels_) {
        Toplevel* toplevel = entry.get();
        if (!toplevel->opening) continue;
        const double k = std::min(
            1.0, static_cast<double>(now - toplevel->open_start_ms) / 220.0);
        const double eased = ease_out_cubic(k);
        const int y = toplevel->open_from_y +
                      static_cast<int>((toplevel->open_to_y - toplevel->open_from_y) *
                                       eased + 0.5);
        wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                    toplevel->scene_tree->node.x, y);
        if (k >= 1.0) {
            toplevel->opening = false;
            wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                        toplevel->scene_tree->node.x,
                                        toplevel->open_to_y);
        } else {
            active = true;
        }
    }
    if (anim_source_ != nullptr) {
        wl_event_source_timer_update(anim_source_, active ? 16 : 0);
    }
}

int Server::on_animation(void* data) {
    static_cast<Server*>(data)->animate_windows();
    return 0;
}

void Server::update_background_theme() {
    if (background_ == nullptr) return;
    const bool dark = prefers_dark();
    float color[4] = {0.93f, 0.95f, 0.98f, 1.0f};
    if (dark) {
        color[0] = 0.086f;
        color[1] = 0.094f;
        color[2] = 0.114f;
    }
    wlr_scene_rect_set_color(background_, color);
}

void Server::shutdown() {
    if (display_ == nullptr) return;

    wl_display_destroy_clients(display_);

    toplevels_.clear();
    keyboards_.clear();
    layers_.clear();
    outputs_.clear();

    remove_listener(new_output_);
    remove_listener(new_input_);
    remove_listener(new_toplevel_);
    remove_listener(new_layer_);
    remove_listener(cursor_motion_);
    remove_listener(cursor_motion_absolute_);
    remove_listener(cursor_button_);
    remove_listener(cursor_axis_);
    remove_listener(cursor_frame_);
    remove_listener(touch_down_);
    remove_listener(touch_up_);
    remove_listener(touch_motion_);
    remove_listener(touch_cancel_);
    remove_listener(touch_frame_);
    remove_listener(request_cursor_);
    remove_listener(pointer_focus_change_);
    remove_listener(request_set_selection_);

    if (scene_ != nullptr) wlr_scene_node_destroy(&scene_->tree.node);
    if (watchdog_ != nullptr) {
        wl_event_source_remove(watchdog_);
        watchdog_ = nullptr;
    }
    if (theme_source_ != nullptr) {
        wl_event_source_remove(theme_source_);
        theme_source_ = nullptr;
    }
    if (anim_source_ != nullptr) {
        wl_event_source_remove(anim_source_);
        anim_source_ = nullptr;
    }
    if (cursor_manager_ != nullptr) wlr_xcursor_manager_destroy(cursor_manager_);
    if (cursor_ != nullptr) wlr_cursor_destroy(cursor_);
    if (allocator_ != nullptr) wlr_allocator_destroy(allocator_);
    if (renderer_ != nullptr) wlr_renderer_destroy(renderer_);
    if (backend_ != nullptr) wlr_backend_destroy(backend_);

    wl_display_destroy(display_);

    display_ = nullptr;
    backend_ = nullptr;
    renderer_ = nullptr;
    allocator_ = nullptr;
    scene_ = nullptr;
    scene_layout_ = nullptr;
    output_layout_ = nullptr;
    xdg_shell_ = nullptr;
    layer_shell_ = nullptr;
    seat_ = nullptr;
    cursor_ = nullptr;
    cursor_manager_ = nullptr;
    focused_ = nullptr;
}

// One output, one integer scale. A panel at least 2400 px wide is a HiDPI
// panel: at 2x its logical size is near the 1280-wide frames in the design.
// FINN_OUTPUT_SCALE overrides the guess for panels that report badly.
static float choose_output_scale(const wlr_output_mode* mode) {
    if (const char* forced = std::getenv("FINN_OUTPUT_SCALE")) {
        const int value = std::atoi(forced);
        if (value >= 1 && value <= 4) return static_cast<float>(value);
    }
    return (mode != nullptr && mode->width >= 2400) ? 2.0f : 1.0f;
}

void Server::add_output(wlr_output* output) {
    wlr_output_init_render(output, allocator_, renderer_);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_mode* mode = wlr_output_preferred_mode(output);
    if (mode != nullptr) wlr_output_state_set_mode(&state, mode);
    const float scale = choose_output_scale(mode);
    wlr_output_state_set_scale(&state, scale);
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);
    // The cursor theme must exist at the output's scale or the arrow is not
    // drawn at all.
    if (cursor_manager_ != nullptr) wlr_xcursor_manager_load(cursor_manager_, scale);

    auto entry = std::make_unique<Output>();
    entry->server = this;
    entry->output = output;

    entry->frame.notify = on_output_frame;
    wl_signal_add(&output->events.frame, &entry->frame);
    entry->request_state.notify = on_output_request_state;
    wl_signal_add(&output->events.request_state, &entry->request_state);
    entry->destroy.notify = on_output_destroy;
    wl_signal_add(&output->events.destroy, &entry->destroy);

    wlr_output_layout_output* layout_output =
        wlr_output_layout_add_auto(output_layout_, output);
    wlr_scene_output* scene_output =
        wlr_scene_output_create(scene_, output);
    wlr_scene_output_layout_add_output(scene_layout_, layout_output,
                                       scene_output);
    // Start the pointer in the middle of the screen with an arrow, so it is
    // visible before the first motion event.
    if (cursor_ != nullptr && outputs_.empty()) {
        wlr_box box{};
        wlr_output_layout_get_box(output_layout_, output, &box);
        wlr_cursor_warp(cursor_, nullptr, box.x + box.width / 2.0,
                        box.y + box.height / 2.0);
        if (cursor_manager_ != nullptr) {
            wlr_cursor_set_xcursor(cursor_, cursor_manager_, "default");
        }
    }

    if (usable_w_ == 0 || usable_h_ == 0) {
        usable_x_ = 0;
        usable_y_ = 0;
        // Logical size: at scale 2 the output has twice as many pixels as
        // the layout has units.
        wlr_box logical{};
        wlr_output_layout_get_box(output_layout_, output, &logical);
        usable_w_ = logical.width;
        usable_h_ = logical.height;
    }

    const bool dark = prefers_dark();
    float color[4] = {0.93f, 0.95f, 0.98f, 1.0f};
    if (dark) {
        color[0] = 0.086f;
        color[1] = 0.094f;
        color[2] = 0.114f;
    }
    if (background_ != nullptr) {
        int logical_w = 0;
        int logical_h = 0;
        wlr_output_effective_resolution(output, &logical_w, &logical_h);
        wlr_scene_rect_set_size(background_, logical_w, logical_h);
        wlr_scene_rect_set_color(background_, color);
        wlr_scene_node_set_position(&background_->node, 0, 0);
        wlr_scene_node_lower_to_bottom(&background_->node);
    }

    for (auto& layer : layers_) {
        if (layer->layer->output == nullptr) {
            layer->layer->output = output;
            configure_layer(layer.get());
        }
    }

    outputs_.push_back(std::move(entry));
}

void Server::remove_output(Output* output) {
    outputs_.erase(std::remove_if(outputs_.begin(), outputs_.end(),
                                  [output](const auto& entry) {
                                      return entry.get() == output;
                                  }),
                   outputs_.end());
}

void Server::add_input(wlr_input_device* device) {
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            add_keyboard(wlr_keyboard_from_input_device(device), device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            wlr_cursor_attach_input_device(cursor_, device);
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            // The cursor maps touch coordinates onto the output layout and
            // re-emits them as touch_* signals.
            wlr_cursor_attach_input_device(cursor_, device);
            ++touch_devices_;
            break;
        default:
            break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (touch_devices_ > 0) caps |= WL_SEAT_CAPABILITY_TOUCH;
    if (!keyboards_.empty()) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat_, caps);
}

void Server::add_keyboard(wlr_keyboard* keyboard, wlr_input_device* device) {
    auto entry = std::make_unique<Keyboard>();
    entry->server = this;
    entry->keyboard = keyboard;
    entry->device = device;

    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no xkb context for a keyboard");
        return;
    }
    xkb_keymap* keymap = xkb_keymap_new_from_names(
        context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no keymap for a keyboard");
        xkb_context_unref(context);
        return;
    }
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(keyboard, 25, 600);

    entry->modifiers.notify = on_keyboard_modifiers;
    wl_signal_add(&keyboard->events.modifiers, &entry->modifiers);
    entry->key.notify = on_keyboard_key;
    wl_signal_add(&keyboard->events.key, &entry->key);
    entry->destroy.notify = on_keyboard_destroy;
    wl_signal_add(&device->events.destroy, &entry->destroy);

    wlr_seat_set_keyboard(seat_, keyboard);
    keyboards_.push_back(std::move(entry));
}

void Server::remove_keyboard(Keyboard* keyboard) {
    keyboards_.erase(std::remove_if(keyboards_.begin(), keyboards_.end(),
                                    [keyboard](const auto& entry) {
                                        return entry.get() == keyboard;
                                    }),
                     keyboards_.end());
}

void Server::add_toplevel(wlr_xdg_toplevel* xdg_toplevel) {
    auto entry = std::make_unique<Toplevel>();
    entry->server = this;
    entry->xdg_toplevel = xdg_toplevel;
    entry->scene_tree = wlr_scene_xdg_surface_create(toplevel_tree_,
                                                     xdg_toplevel->base);
    entry->scene_tree->node.data = entry.get();
    xdg_toplevel->base->data = entry->scene_tree;

    const int offset = 40 + 32 * static_cast<int>(toplevels_.size() % 8);
    wlr_scene_node_set_position(&entry->scene_tree->node, offset, offset);

    entry->map.notify = on_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &entry->map);
    entry->unmap.notify = on_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &entry->unmap);
    entry->commit.notify = on_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &entry->commit);
    entry->destroy.notify = on_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &entry->destroy);
    entry->request_move.notify = on_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &entry->request_move);
    entry->request_resize.notify = on_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &entry->request_resize);
    entry->request_maximize.notify = on_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize,
                  &entry->request_maximize);

    toplevels_.push_back(std::move(entry));
}

void Server::remove_toplevel(Toplevel* toplevel) {
    if (focused_ == toplevel) focused_ = nullptr;
    if (last_focused_ == toplevel) last_focused_ = nullptr;
    if (grabbed_ == toplevel) reset_cursor_mode();
    toplevels_.erase(std::remove_if(toplevels_.begin(), toplevels_.end(),
                                    [toplevel](const auto& entry) {
                                        return entry.get() == toplevel;
                                    }),
                     toplevels_.end());
}

void Server::add_layer(wlr_layer_surface_v1* layer_surface) {
    if (layer_surface == nullptr) return;
    if (layer_surface->output == nullptr) {
        if (outputs_.empty()) {
            wlr_log(WLR_ERROR, "finn-comp: layer surface before an output");
            return;
        }
        layer_surface->output = outputs_.front()->output;
    }

    auto entry = std::make_unique<Layer>();
    entry->server = this;
    entry->layer = layer_surface;
    std::uint32_t layer_number =
        static_cast<std::uint32_t>(layer_surface->current.layer);
    if (layer_number > 3) layer_number = 3;
    wlr_scene_tree* tree = layer_trees_[layer_number];
    if (tree == nullptr) tree = &scene_->tree;
    entry->scene = wlr_scene_layer_surface_v1_create(tree, layer_surface);
    if (entry->scene == nullptr) return;

    entry->map.notify = on_layer_map;
    wl_signal_add(&layer_surface->surface->events.map, &entry->map);
    entry->commit.notify = on_layer_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &entry->commit);
    entry->destroy.notify = on_layer_destroy;
    wl_signal_add(&layer_surface->events.destroy, &entry->destroy);

    layers_.push_back(std::move(entry));
    wlr_log(WLR_INFO, "finn-comp: layer surface '%s'", layer_surface->namespace_);
}

void Server::remove_layer(Layer* layer) {
    if (focused_layer_ == layer) {
        focused_layer_ = nullptr;
        wlr_seat_keyboard_notify_clear_focus(seat_);
        if (last_focused_ != nullptr) focus(last_focused_);
    }
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                                 [layer](const auto& entry) {
                                     return entry.get() == layer;
                                 }),
                  layers_.end());
}

void Server::configure_layer(Layer* layer) {
    if (layer == nullptr || layer->layer->output == nullptr) return;
    wlr_box full{};
    wlr_output_layout_get_box(output_layout_, layer->layer->output, &full);
    if (full.width <= 0 || full.height <= 0) {
        wlr_output_effective_resolution(layer->layer->output, &full.width,
                                        &full.height);
    }
    wlr_box usable = full;
    wlr_scene_layer_surface_v1_configure(layer->scene, &full, &usable);
    if (usable.width > 0 && usable.height > 0) {
        usable_x_ = usable.x;
        usable_y_ = usable.y;
        usable_w_ = usable.width;
        usable_h_ = usable.height;
    }
}

void Server::focus_layer(Layer* layer) {
    if (layer == nullptr || seat_ == nullptr) return;
    if (!layer->layer->surface->mapped) return;
    if (layer->layer->current.keyboard_interactive == 0) return;
    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
    if (keyboard == nullptr) return;
    wlr_seat_keyboard_notify_enter(seat_, layer->layer->surface,
                                   keyboard->keycodes, keyboard->num_keycodes,
                                   &keyboard->modifiers);
    if (focused_ != nullptr) {
        wlr_xdg_toplevel_set_activated(focused_->xdg_toplevel, false);
        focused_ = nullptr;
    }
    focused_layer_ = layer;
}

// Preserves unmaximized window geometry before constraining to usable screen area,
// allowing clean restoration when unmaximize is requested by the client.
void Server::maximize(Toplevel* toplevel) {
    if (toplevel == nullptr || toplevel->maximized) return;
    int x = usable_x_;
    int y = usable_y_;
    int w = usable_w_;
    int h = usable_h_;
    if (w <= 0 || h <= 0) {
        if (outputs_.empty()) return;
        x = 0;
        y = 0;
        wlr_output_effective_resolution(outputs_.front()->output, &w, &h);
    }
    toplevel->restore_x = toplevel->scene_tree->node.x;
    toplevel->restore_y = toplevel->scene_tree->node.y;
    toplevel->restore_w = toplevel->xdg_toplevel->base->geometry.width;
    toplevel->restore_h = toplevel->xdg_toplevel->base->geometry.height;
    toplevel->maximized = true;
    toplevel->opening = false;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, w, h);
    wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
    focus(toplevel);
}

void Server::unmaximize(Toplevel* toplevel) {
    if (toplevel == nullptr || !toplevel->maximized) return;
    toplevel->maximized = false;
    toplevel->opening = false;
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->restore_x, toplevel->restore_y);
    if (toplevel->restore_w > 0 && toplevel->restore_h > 0) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, toplevel->restore_w,
                                  toplevel->restore_h);
    }
    wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
    focus(toplevel);
}

void Server::begin_interactive(Toplevel* toplevel, CursorMode mode,
                               uint32_t edges) {
    if (toplevel == nullptr) return;
    grabbed_ = toplevel;
    cursor_mode_ = mode;
    if (mode == CursorMode::move) {
        grab_x_ = cursor_->x - toplevel->scene_tree->node.x;
        grab_y_ = cursor_->y - toplevel->scene_tree->node.y;
        return;
    }
    const wlr_box& geometry = toplevel->xdg_toplevel->base->geometry;
    const double border_x = toplevel->scene_tree->node.x + geometry.x +
                            ((edges & WLR_EDGE_RIGHT) != 0 ? geometry.width : 0);
    const double border_y = toplevel->scene_tree->node.y + geometry.y +
                            ((edges & WLR_EDGE_BOTTOM) != 0 ? geometry.height : 0);
    grab_x_ = cursor_->x - border_x;
    grab_y_ = cursor_->y - border_y;
    grab_gx_ = geometry.x + static_cast<int>(toplevel->scene_tree->node.x);
    grab_gy_ = geometry.y + static_cast<int>(toplevel->scene_tree->node.y);
    grab_gw_ = geometry.width;
    grab_gh_ = geometry.height;
    resize_edges_ = edges;
}

void Server::process_cursor_move() {
    if (grabbed_ == nullptr) return;
    wlr_scene_node_set_position(&grabbed_->scene_tree->node,
                                static_cast<int>(cursor_->x - grab_x_),
                                static_cast<int>(cursor_->y - grab_y_));
}

void Server::process_cursor_resize() {
    if (grabbed_ == nullptr) return;
    const double border_x = cursor_->x - grab_x_;
    const double border_y = cursor_->y - grab_y_;
    int left = grab_gx_;
    int right = grab_gx_ + grab_gw_;
    int top = grab_gy_;
    int bottom = grab_gy_ + grab_gh_;

    if ((resize_edges_ & WLR_EDGE_TOP) != 0) {
        top = static_cast<int>(border_y);
        if (top >= bottom) top = bottom - 1;
    } else if ((resize_edges_ & WLR_EDGE_BOTTOM) != 0) {
        bottom = static_cast<int>(border_y);
        if (bottom <= top) bottom = top + 1;
    }
    if ((resize_edges_ & WLR_EDGE_LEFT) != 0) {
        left = static_cast<int>(border_x);
        if (left >= right) left = right - 1;
    } else if ((resize_edges_ & WLR_EDGE_RIGHT) != 0) {
        right = static_cast<int>(border_x);
        if (right <= left) right = left + 1;
    }

    const wlr_box& geometry = grabbed_->xdg_toplevel->base->geometry;
    wlr_scene_node_set_position(&grabbed_->scene_tree->node, left - geometry.x,
                                top - geometry.y);
    wlr_xdg_toplevel_set_size(grabbed_->xdg_toplevel, right - left, bottom - top);
}

void Server::process_cursor_motion(uint32_t time_msec) {
    if (cursor_mode_ == CursorMode::move) {
        process_cursor_move();
        return;
    }
    if (cursor_mode_ == CursorMode::resize) {
        process_cursor_resize();
        return;
    }
    pointer_motion(time_msec);
}

void Server::reset_cursor_mode() {
    cursor_mode_ = CursorMode::passthrough;
    grabbed_ = nullptr;
}


void Server::focus(Toplevel* toplevel) {
    if (toplevel == nullptr) return;
    wlr_surface* surface = toplevel->xdg_toplevel->base->surface;
    wlr_surface* previous = seat_->keyboard_state.focused_surface;
    if (previous == surface) return;

    if (previous != nullptr) {
        wlr_xdg_toplevel* previous_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(previous);
        if (previous_toplevel != nullptr) {
            wlr_xdg_toplevel_set_activated(previous_toplevel, false);
        }
    }

    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
    if (keyboard != nullptr) {
        wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes,
                                       keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
    focused_ = toplevel;
    last_focused_ = toplevel;
    focused_layer_ = nullptr;
}

// Handles compositor-level shortcuts (F1 window cycling, Escape shutdown)
// before passing keystrokes down to focused client surfaces.
bool Server::handle_keybinding(uint32_t keysym) {
    switch (keysym) {
        case XKB_KEY_Escape:
            wl_display_terminate(display_);
            return true;
        case XKB_KEY_F1: {
            if (toplevels_.empty()) return true;
            const auto current = std::find_if(
                toplevels_.begin(), toplevels_.end(),
                [this](const auto& entry) { return entry.get() == focused_; });
            std::size_t index = 0;
            if (current != toplevels_.end()) {
                index = static_cast<std::size_t>(
                            std::distance(toplevels_.begin(), current)) +
                        1;
                index %= toplevels_.size();
            }
            focus(toplevels_[index].get());
            return true;
        }
        default:
            return false;
    }
}

wlr_surface* Server::surface_at(double lx, double ly, double* sx, double* sy) {
    wlr_scene_node* node = wlr_scene_node_at(&scene_->tree.node, lx, ly, sx, sy);
    if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) return nullptr;
    wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    wlr_scene_surface* scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (scene_surface == nullptr) return nullptr;
    return scene_surface->surface;
}

Server::Toplevel* Server::toplevel_at(double lx, double ly,
                                      wlr_surface** surface, double* sx,
                                      double* sy) {
    wlr_scene_node* node =
        wlr_scene_node_at(&scene_->tree.node, lx, ly, sx, sy);
    if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) return nullptr;

    wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    wlr_scene_surface* scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (scene_surface == nullptr) return nullptr;

    *surface = scene_surface->surface;
    wlr_scene_tree* tree = node->parent;
    while (tree != nullptr && tree->node.data == nullptr) {
        tree = tree->node.parent;
    }
    if (tree == nullptr) return nullptr;
    return static_cast<Toplevel*>(tree->node.data);
}

void Server::pointer_motion(uint32_t time_msec) {
    double sx = 0;
    double sy = 0;
    wlr_surface* surface = surface_at(cursor_->x, cursor_->y, &sx, &sy);
    if (surface == nullptr && cursor_manager_ != nullptr) {
        wlr_cursor_set_xcursor(cursor_, cursor_manager_, "default");
    }
    if (surface != nullptr) {
        wlr_seat_pointer_notify_enter(seat_, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat_);
    }
}

void Server::on_new_output(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, new_output_);
    server->add_output(static_cast<wlr_output*>(data));
}

void Server::on_new_input(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, new_input_);
    server->add_input(static_cast<wlr_input_device*>(data));
}

void Server::on_new_toplevel(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, new_toplevel_);
    server->add_toplevel(static_cast<wlr_xdg_toplevel*>(data));
}

void Server::on_new_layer(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, new_layer_);
    server->add_layer(static_cast<wlr_layer_surface_v1*>(data));
}

void Server::on_output_frame(wl_listener* listener, void* data) {
    (void)data;
    Output* output = nullptr;
    output = wl_container_of(listener, output, frame);
    wlr_scene_output* scene_output =
        wlr_scene_get_scene_output(output->server->scene_, output->output);
    if (scene_output == nullptr) return;

    wlr_scene_output_commit(scene_output, nullptr);
    sd_notify(0, "WATCHDOG=1");

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void Server::on_output_request_state(wl_listener* listener, void* data) {
    Output* output = nullptr;
    output = wl_container_of(listener, output, request_state);
    const auto* event = static_cast<wlr_output_event_request_state*>(data);
    wlr_output_commit_state(output->output, event->state);
}

void Server::on_output_destroy(wl_listener* listener, void* data) {
    (void)data;
    Output* output = nullptr;
    output = wl_container_of(listener, output, destroy);
    output->server->remove_output(output);
}

void Server::on_toplevel_map(wl_listener* listener, void* data) {
    (void)data;
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, map);
    wlr_log(WLR_INFO, "finn-comp: toplevel mapped (%zu open)",
            toplevel->server->toplevels_.size());
    toplevel->server->begin_open_animation(toplevel);
    toplevel->server->focus(toplevel);
}

void Server::on_toplevel_unmap(wl_listener* listener, void* data) {
    (void)data;
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, unmap);
    toplevel->opening = false;
    wlr_log(WLR_INFO, "finn-comp: toplevel unmapped");
    if (toplevel->server->focused_ == toplevel) {
        toplevel->server->focused_ = nullptr;
    }
}

void Server::on_toplevel_commit(wl_listener* listener, void* data) {
    (void)data;
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, commit);
    if (toplevel->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }
}

void Server::on_toplevel_destroy(wl_listener* listener, void* data) {
    (void)data;
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, destroy);
    toplevel->server->remove_toplevel(toplevel);
}

void Server::on_toplevel_request_move(wl_listener* listener, void* data) {
    (void)data;
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, request_move);
    toplevel->server->begin_interactive(toplevel, CursorMode::move, 0);
}

void Server::on_toplevel_request_resize(wl_listener* listener, void* data) {
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, request_resize);
    auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
    toplevel->server->begin_interactive(toplevel, CursorMode::resize,
                                        event->edges);
}

void Server::on_toplevel_request_maximize(wl_listener* listener, void* data) {
    (void)data;
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, request_maximize);
    if (!toplevel->xdg_toplevel->base->initialized) return;
    if (toplevel->xdg_toplevel->requested.maximized) {
        toplevel->server->maximize(toplevel);
    } else {
        toplevel->server->unmaximize(toplevel);
    }
}

void Server::on_layer_map(wl_listener* listener, void* data) {
    (void)data;
    Layer* layer = nullptr;
    layer = wl_container_of(listener, layer, map);
    if (layer->layer->current.keyboard_interactive != 0) {
        layer->server->focus_layer(layer);
    }
}

// Reconfigures layer surfaces dynamically so adjustments to panel dimensions
// immediately recalculate usable space for maximized application windows.
void Server::on_layer_commit(wl_listener* listener, void* data) {
    (void)data;
    Layer* layer = nullptr;
    layer = wl_container_of(listener, layer, commit);
    Server* server = layer->server;
    if (!layer->configured) {
        if (layer->layer->initialized) {
            layer->configured = true;
            server->configure_layer(layer);
        }
        return;
    }
    if (!layer->layer->surface->mapped) return;
    if (layer->layer->current.keyboard_interactive != 0) {
        server->focus_layer(layer);
    } else if (server->focused_layer_ == layer) {
        server->focused_layer_ = nullptr;
        wlr_seat_keyboard_notify_clear_focus(server->seat_);
        if (server->last_focused_ != nullptr) server->focus(server->last_focused_);
    }
}

void Server::on_layer_destroy(wl_listener* listener, void* data) {
    (void)data;
    Layer* layer = nullptr;
    layer = wl_container_of(listener, layer, destroy);
    layer->server->remove_layer(layer);
}

void Server::on_keyboard_modifiers(wl_listener* listener, void* data) {
    (void)data;
    Keyboard* keyboard = nullptr;
    keyboard = wl_container_of(listener, keyboard, modifiers);
    Server* server = keyboard->server;
    wlr_seat_set_keyboard(server->seat_, keyboard->keyboard);
    wlr_seat_keyboard_notify_modifiers(server->seat_,
                                       &keyboard->keyboard->modifiers);
}

void Server::on_keyboard_key(wl_listener* listener, void* data) {
    Keyboard* keyboard = nullptr;
    keyboard = wl_container_of(listener, keyboard, key);
    Server* server = keyboard->server;
    auto* event = static_cast<wlr_keyboard_key_event*>(data);

    const uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t* syms = nullptr;
    const int count =
        xkb_state_key_get_syms(keyboard->keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    const uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);
    if ((modifiers & WLR_MODIFIER_ALT) != 0 &&
        event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < count; ++i) {
            if (server->handle_keybinding(syms[i])) handled = true;
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(server->seat_, keyboard->keyboard);
        wlr_seat_keyboard_notify_key(server->seat_, event->time_msec,
                                     event->keycode, event->state);
    }
}

void Server::on_keyboard_destroy(wl_listener* listener, void* data) {
    (void)data;
    Keyboard* keyboard = nullptr;
    keyboard = wl_container_of(listener, keyboard, destroy);
    keyboard->server->remove_keyboard(keyboard);
}

void Server::on_cursor_motion(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, cursor_motion_);
    auto* event = static_cast<wlr_pointer_motion_event*>(data);
    wlr_cursor_move(server->cursor_, &event->pointer->base, event->delta_x,
                    event->delta_y);
    server->process_cursor_motion(event->time_msec);
}

void Server::on_cursor_motion_absolute(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, cursor_motion_absolute_);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(server->cursor_, &event->pointer->base, event->x,
                             event->y);
    server->process_cursor_motion(event->time_msec);
}

void Server::on_cursor_button(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, cursor_button_);
    auto* event = static_cast<wlr_pointer_button_event*>(data);
    wlr_seat_pointer_notify_button(server->seat_, event->time_msec,
                                   event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        double sx = 0;
        double sy = 0;
        wlr_surface* surface = nullptr;
        server->focus(server->toplevel_at(server->cursor_->x, server->cursor_->y,
                                          &surface, &sx, &sy));
    } else {
        server->reset_cursor_mode();
    }
}

void Server::on_cursor_axis(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, cursor_axis_);
    auto* event = static_cast<wlr_pointer_axis_event*>(data);
    wlr_seat_pointer_notify_axis(
        server->seat_, event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source, event->relative_direction);
}

void Server::on_cursor_frame(wl_listener* listener, void* data) {
    (void)data;
    Server* server = nullptr;
    server = wl_container_of(listener, server, cursor_frame_);
    wlr_seat_pointer_notify_frame(server->seat_);
}

// Touch goes straight to the surface under the finger. Unlike the pointer it
// has no hover state, so a touch-down also focuses the toplevel it landed on.
void Server::on_touch_down(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, touch_down_);
    auto* event = static_cast<wlr_touch_down_event*>(data);
    double lx = 0;
    double ly = 0;
    wlr_cursor_absolute_to_layout_coords(server->cursor_, &event->touch->base,
                                         event->x, event->y, &lx, &ly);
    double sx = 0;
    double sy = 0;
    wlr_surface* surface = nullptr;
    Toplevel* toplevel = server->toplevel_at(lx, ly, &surface, &sx, &sy);
    if (toplevel != nullptr) {
        server->focus(toplevel);
    } else {
        surface = server->surface_at(lx, ly, &sx, &sy);
    }
    if (surface == nullptr) return;
    wlr_seat_touch_notify_down(server->seat_, surface, event->time_msec,
                               event->touch_id, sx, sy);
}

void Server::on_touch_up(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, touch_up_);
    auto* event = static_cast<wlr_touch_up_event*>(data);
    wlr_seat_touch_notify_up(server->seat_, event->time_msec, event->touch_id);
}

void Server::on_touch_motion(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, touch_motion_);
    auto* event = static_cast<wlr_touch_motion_event*>(data);
    // A point that never reached a surface has nothing to move.
    if (wlr_seat_touch_get_point(server->seat_, event->touch_id) == nullptr) return;
    double lx = 0;
    double ly = 0;
    wlr_cursor_absolute_to_layout_coords(server->cursor_, &event->touch->base,
                                         event->x, event->y, &lx, &ly);
    double sx = 0;
    double sy = 0;
    wlr_surface* surface = server->surface_at(lx, ly, &sx, &sy);
    if (surface == nullptr) return;
    wlr_seat_touch_notify_motion(server->seat_, event->time_msec,
                                 event->touch_id, sx, sy);
}

void Server::on_touch_cancel(wl_listener* listener, void* data) {
    (void)data;
    Server* server = nullptr;
    server = wl_container_of(listener, server, touch_cancel_);
    // Cancelling removes points from the list, so collect the clients first.
    std::vector<wlr_seat_client*> clients;
    wlr_touch_point* point = nullptr;
    wl_list_for_each(point, &server->seat_->touch_state.touch_points, link) {
        if (point->client != nullptr &&
            std::find(clients.begin(), clients.end(), point->client) == clients.end()) {
            clients.push_back(point->client);
        }
    }
    for (wlr_seat_client* client : clients) {
        wlr_seat_touch_notify_cancel(server->seat_, client);
    }
}

void Server::on_touch_frame(wl_listener* listener, void* data) {
    (void)data;
    Server* server = nullptr;
    server = wl_container_of(listener, server, touch_frame_);
    wlr_seat_touch_notify_frame(server->seat_);
}

void Server::on_request_cursor(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, request_cursor_);
    auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client* focused = server->seat_->pointer_state.focused_client;
    if (focused == event->seat_client) {
        wlr_cursor_set_surface(server->cursor_, event->surface,
                               event->hotspot_x, event->hotspot_y);
    }
}

void Server::on_pointer_focus_change(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, pointer_focus_change_);
    (void)data;
    // Every focus change starts from the default arrow. A client that wants
    // another image asks for it after the enter, which replaces this one.
    // Without this, a client that never calls set_cursor (libfinnui) leaves
    // the pointer invisible.
    if (server->cursor_manager_ != nullptr) {
        wlr_cursor_set_xcursor(server->cursor_, server->cursor_manager_,
                               "default");
    }
}

void Server::on_request_set_selection(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, request_set_selection_);
    auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat_, event->source, event->serial);
}

}  // namespace finn
