#ifndef FINN_COMP_INTERNAL_HPP
#define FINN_COMP_INTERNAL_HPP

#include "comp/server.hpp"

#include <wayland-util.h>

#include <string>

namespace finn {

inline constexpr int kSsdTitleHeight = 32;
inline constexpr int kSsdBorderPx = 1;
inline constexpr int kResizeEdgeMargin = 6;
inline constexpr int kSsdButtonW = 24;
inline constexpr int kSsdButtonH = 20;
inline constexpr int kSsdButtonGap = 2;
inline constexpr int kSsdControlsRightPad = 6;

inline void remove_listener(wl_listener& listener) {
    if (listener.link.prev != nullptr && listener.link.next != nullptr) {
        wl_list_remove(&listener.link);
        wl_list_init(&listener.link);
    }
}

inline void attach_listener(wl_signal& signal, wl_listener& listener,
                            wl_notify_func_t notify) {
    listener.notify = notify;
    wl_list_init(&listener.link);
    wl_signal_add(&signal, &listener);
}

struct Server::ManagedWindow {
    enum class HitZone {
        kNone,
        kClient,
        kMoveBar,
        kBtnMin,
        kBtnMax,
        kBtnClose,
        kResize,
    };

    explicit ManagedWindow(Server* s) : server(s) {}
    ~ManagedWindow();

    Server* server = nullptr;
    wlr_scene_tree* scene_tree = nullptr;
    wlr_scene_tree* surface_tree = nullptr;

    wlr_xdg_toplevel* xdg_toplevel = nullptr;
    wlr_xdg_toplevel_decoration_v1* xdg_decoration = nullptr;
    wlr_xwayland_surface* xwayland_surface = nullptr;
    wlr_foreign_toplevel_handle_v1* foreign_handle = nullptr;

    bool is_xwayland = false;
    bool override_redirect = false;
    bool mapped = false;
    bool minimized = false;
    bool maximized = false;
    bool ssd_enabled = false;
    bool hover_close = false;

    int x = 80;
    int y = 60;
    int width = 880;
    int height = 600;
    wlr_box saved_geometry{80, 60, 880, 600};

    wl_event_source* anim_timer = nullptr;
    uint64_t anim_start_ms = 0;
    uint32_t anim_duration_ms = 150;
    double anim_from_x = 0.0;
    double anim_from_y = 0.0;

    void start_animation(double from_x, double from_y, uint32_t duration_ms = 150);
    void stop_animation();

    // Scene-graph SSD frame + Figma-style top-right 3-button controls
    wlr_scene_rect* ssd_titlebar = nullptr;
    wlr_scene_rect* ssd_divider = nullptr;
    wlr_scene_rect* ssd_border_top = nullptr;
    wlr_scene_rect* ssd_border_bottom = nullptr;
    wlr_scene_rect* ssd_border_left = nullptr;
    wlr_scene_rect* ssd_border_right = nullptr;
    wlr_scene_rect* ssd_btn_min = nullptr;
    wlr_scene_rect* ssd_btn_max = nullptr;
    wlr_scene_rect* ssd_btn_close = nullptr;
    wlr_scene_rect* ssd_glyph_min = nullptr;
    wlr_scene_rect* ssd_glyph_max = nullptr;
    wlr_scene_rect* ssd_glyph_close_a = nullptr;
    wlr_scene_rect* ssd_glyph_close_b = nullptr;

    // XDG / Xwayland listeners
    wl_listener map_l{};
    wl_listener unmap_l{};
    wl_listener commit_l{};
    wl_listener destroy_l{};
    wl_listener req_move_l{};
    wl_listener req_resize_l{};
    wl_listener req_maximize_l{};
    wl_listener req_minimize_l{};
    wl_listener req_fullscreen_l{};
    wl_listener set_title_l{};
    wl_listener set_app_id_l{};
    wl_listener dec_request_mode_l{};
    wl_listener dec_destroy_l{};

    // Xwayland-specific extra lifecycle listeners
    wl_listener xw_associate_l{};
    wl_listener xw_dissociate_l{};
    wl_listener xw_req_configure_l{};
    wl_listener xw_req_activate_l{};
    wl_listener xw_req_close_l{};

    // Foreign-toplevel protocol requests from dock/shell
    wl_listener ft_req_activate_l{};
    wl_listener ft_req_maximize_l{};
    wl_listener ft_req_minimize_l{};
    wl_listener ft_req_fullscreen_l{};
    wl_listener ft_req_close_l{};

    wlr_surface* main_surface() const;
    std::string title() const;
    std::string app_id() const;
    int title_offset() const { return ssd_enabled ? kSsdTitleHeight : 0; }
    wlr_box total_box() const;

    void create_ssd_nodes();
    void set_ssd_enabled(bool enabled);
    void update_ssd_layout();
    void apply_ssd_theme(bool dark, bool focused);
    void update_size_from_surface();
    void configure_size(int client_w, int client_h);
    void set_position(int nx, int ny);
    void set_activated(bool active);
    void set_maximized(bool on);
    void set_minimized(bool on);
    void snap_half(bool left);
    void close();
    void ensure_foreign_handle();
    void sync_foreign_state();
    HitZone hit_test(double lx, double ly, uint32_t* resize_edges = nullptr) const;
};

struct Server::PopupSurface {
    Server* server = nullptr;
    wlr_xdg_popup* popup = nullptr;
    wlr_scene_tree* scene_tree = nullptr;
    wlr_scene_tree* parent_tree = nullptr;

    wl_listener commit_l{};
    wl_listener destroy_l{};

    ~PopupSurface() {
        remove_listener(commit_l);
        remove_listener(destroy_l);
    }
};

struct Server::LayerSurface {
    Server* server = nullptr;
    wlr_layer_surface_v1* layer_surface = nullptr;
    wlr_scene_layer_surface_v1* scene_layer = nullptr;
    uint32_t current_layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    bool mapped = false;

    wl_listener map_l{};
    wl_listener unmap_l{};
    wl_listener commit_l{};
    wl_listener destroy_l{};
    wl_listener new_popup_l{};

    ~LayerSurface() {
        remove_listener(map_l);
        remove_listener(unmap_l);
        remove_listener(commit_l);
        remove_listener(destroy_l);
        remove_listener(new_popup_l);
    }
};

struct Server::LockSurface {
    Server* server = nullptr;
    wlr_session_lock_surface_v1* lock_surface = nullptr;
    wlr_scene_tree* scene_tree = nullptr;
    wl_listener destroy_l{};
    ~LockSurface() { remove_listener(destroy_l); }
};

struct Server::OutputDevice {
    Server* server = nullptr;
    wlr_output* output = nullptr;
    wlr_scene_output* scene_output = nullptr;

    wl_listener frame_l{};
    wl_listener request_state_l{};
    wl_listener destroy_l{};

    ~OutputDevice() {
        remove_listener(frame_l);
        remove_listener(request_state_l);
        remove_listener(destroy_l);
    }
};

struct Server::KeyboardDevice {
    Server* server = nullptr;
    wlr_keyboard* keyboard = nullptr;
    wl_listener modifiers_l{};
    wl_listener key_l{};
    wl_listener destroy_l{};

    ~KeyboardDevice() {
        remove_listener(modifiers_l);
        remove_listener(key_l);
        remove_listener(destroy_l);
    }
};

struct Server::PointerDevice {
    Server* server = nullptr;
    wlr_pointer* pointer = nullptr;
    wl_listener destroy_l{};
    ~PointerDevice() { remove_listener(destroy_l); }
};

void setup_xdg_decoration(Server* server, wlr_xdg_toplevel_decoration_v1* dec);
void setup_xwayland_window(Server::ManagedWindow* w, wlr_xwayland_surface* xsurface);

}  // namespace finn

#endif
