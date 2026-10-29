#include "comp/server.hpp"
#include "comp/wlroots.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#include <xkbcommon/xkbcommon.h>

namespace finn {
namespace {

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

    ~Toplevel() {
        detach(map);
        detach(unmap);
        detach(commit);
        detach(destroy);
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

    output_layout_ = wlr_output_layout_create(display_);
    scene_ = wlr_scene_create();
    scene_layout_ = wlr_scene_attach_output_layout(scene_, output_layout_);

    new_output_.notify = on_new_output;
    wl_signal_add(&backend_->events.new_output, &new_output_);

    xdg_shell_ = wlr_xdg_shell_create(display_, 3);
    if (xdg_shell_ == nullptr) {
        wlr_log(WLR_ERROR, "finn-comp: no xdg-shell");
        return false;
    }
    new_toplevel_.notify = on_new_toplevel;
    wl_signal_add(&xdg_shell_->events.new_toplevel, &new_toplevel_);

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
    if (wl_display_add_socket(display_, "wayland-0") != 0) {
        wlr_log(WLR_ERROR, "finn-comp: cannot bind wayland-0: %s",
                std::strerror(errno));
        return false;
    }

    if (!wlr_backend_start(backend_)) {
        wlr_log(WLR_ERROR, "finn-comp: backend failed to start");
        return false;
    }

    wlr_log(WLR_INFO, "finn-comp: listening on WAYLAND_DISPLAY=wayland-0");
    return true;
}

void Server::shutdown() {
    if (display_ == nullptr) return;

    wl_display_destroy_clients(display_);

    toplevels_.clear();
    keyboards_.clear();
    outputs_.clear();

    remove_listener(new_output_);
    remove_listener(new_input_);
    remove_listener(new_toplevel_);
    remove_listener(cursor_motion_);
    remove_listener(cursor_motion_absolute_);
    remove_listener(cursor_button_);
    remove_listener(cursor_axis_);
    remove_listener(cursor_frame_);
    remove_listener(request_cursor_);
    remove_listener(pointer_focus_change_);
    remove_listener(request_set_selection_);

    if (scene_ != nullptr) wlr_scene_node_destroy(&scene_->tree.node);
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
    seat_ = nullptr;
    cursor_ = nullptr;
    cursor_manager_ = nullptr;
    focused_ = nullptr;
}

void Server::add_output(wlr_output* output) {
    wlr_output_init_render(output, allocator_, renderer_);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_mode* mode = wlr_output_preferred_mode(output);
    if (mode != nullptr) wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

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
        default:
            break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
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
    entry->scene_tree = wlr_scene_xdg_surface_create(&scene_->tree,
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

    toplevels_.push_back(std::move(entry));
}

void Server::remove_toplevel(Toplevel* toplevel) {
    if (focused_ == toplevel) focused_ = nullptr;
    toplevels_.erase(std::remove_if(toplevels_.begin(), toplevels_.end(),
                                    [toplevel](const auto& entry) {
                                        return entry.get() == toplevel;
                                    }),
                     toplevels_.end());
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
}

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
    wlr_surface* surface = nullptr;
    Toplevel* toplevel =
        toplevel_at(cursor_->x, cursor_->y, &surface, &sx, &sy);
    if (toplevel == nullptr && cursor_manager_ != nullptr) {
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

void Server::on_output_frame(wl_listener* listener, void* data) {
    (void)data;
    Output* output = nullptr;
    output = wl_container_of(listener, output, frame);
    wlr_scene_output* scene_output =
        wlr_scene_get_scene_output(output->server->scene_, output->output);
    if (scene_output == nullptr) return;

    wlr_scene_output_commit(scene_output, nullptr);

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
    toplevel->server->focus(toplevel);
}

void Server::on_toplevel_unmap(wl_listener* listener, void* data) {
    (void)data;
    Toplevel* toplevel = nullptr;
    toplevel = wl_container_of(listener, toplevel, unmap);
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
    server->pointer_motion(event->time_msec);
}

void Server::on_cursor_motion_absolute(wl_listener* listener, void* data) {
    Server* server = nullptr;
    server = wl_container_of(listener, server, cursor_motion_absolute_);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(server->cursor_, &event->pointer->base, event->x,
                             event->y);
    server->pointer_motion(event->time_msec);
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
    auto* event = static_cast<wlr_seat_pointer_focus_change_event*>(data);
    if (event->new_surface == nullptr && server->cursor_manager_ != nullptr) {
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
