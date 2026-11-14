// Public interface for finn-comp, the FinnOS Wayland + Xwayland compositor.
// Implements wlroots 0.20 output/input/scene orchestration with zero app-id
// awareness: windows are managed purely via Wayland/X11 protocols.

#ifndef FINN_COMP_SERVER_HPP
#define FINN_COMP_SERVER_HPP

#include "comp/wlroots.hpp"

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

namespace finn {

class Server {
public:
    enum class CursorMode {
        kPassthrough,
        kMove,
        kResize,
    };

    struct OutputConfig {
        int width = 0;
        int height = 0;
        float scale = 0.0f;
    };

    struct ManagedWindow;
    struct PopupSurface;
    struct LayerSurface;
    struct LockSurface;
    struct KeyboardDevice;
    struct PointerDevice;
    struct OutputDevice;

    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    int run();

    // Output and theme helpers
    void arrange_layers();
    void update_background();
    void update_display_settings();
    void focus_window(ManagedWindow* win);
    void focus_top_window();
    void cycle_focus(bool forward);
    void minimize_all_toggle();
    ManagedWindow* find_xdg_toplevel(const wlr_xdg_toplevel* top) const;

    // Pointer / interactive window ops
    void process_cursor_motion(uint32_t time_msec);
    void update_cursor_shape_or_edge(double lx, double ly);
    void begin_move(ManagedWindow* win);
    void begin_resize(ManagedWindow* win, uint32_t edges);

    void create_popup(wlr_xdg_popup* popup, wlr_scene_tree* parent_tree);
    wlr_scene_tree* parent_tree_for_xdg(wlr_surface* parent) const;
    wlr_box usable_area_for(wlr_output* output = nullptr) const;
    wlr_output* primary_output() const;

    wl_display* display() const { return display_; }
    wlr_backend* backend() const { return backend_; }
    wlr_renderer* renderer() const { return renderer_; }
    wlr_allocator* allocator() const { return allocator_; }
    wlr_seat* seat() const { return seat_; }
    wlr_scene* scene() const { return scene_; }
    wlr_output_layout* output_layout() const { return output_layout_; }
    wlr_foreign_toplevel_manager_v1* foreign_toplevel_mgr() const {
        return foreign_toplevel_mgr_;
    }
    wlr_xwayland* xwayland() const { return xwayland_; }
    wlr_scene_tree* toplevel_tree() const { return toplevel_tree_; }
    wlr_scene_tree* top_layer_tree() const { return layer_trees_[2]; }
    bool dark_theme() const { return dark_theme_; }
    bool reduced_motion() const { return reduced_motion_; }
    static uint64_t monotonic_msec();

    ManagedWindow* focused_window() const { return focused_window_; }
    CursorMode cursor_mode() const { return cursor_mode_; }

private:
    static OutputConfig read_output_config();
    static bool read_dark_setting();
    static bool read_reduced_motion_setting();
    void apply_output_config(wlr_output* output, const OutputConfig& cfg);
    void sync_all_ssd_themes();
    void reload_cursor_theme();

    void shutdown();

    wl_display* display_ = nullptr;
    wlr_backend* backend_ = nullptr;
    wlr_session* wlr_session_ = nullptr;
    wlr_renderer* renderer_ = nullptr;
    wlr_allocator* allocator_ = nullptr;

    wlr_scene* scene_ = nullptr;
    wlr_scene_output_layout* scene_layout_ = nullptr;
    wlr_output_layout* output_layout_ = nullptr;
    wlr_output_manager_v1* output_manager_ = nullptr;

    wlr_scene_rect* background_rect_ = nullptr;
    // Strict scene stack order: Background, Bottom, Toplevels (separate), Top, Overlay
    std::array<wlr_scene_tree*, 4> layer_trees_{};
    wlr_scene_tree* toplevel_tree_ = nullptr;
    wlr_scene_tree* lock_tree_ = nullptr;

    wlr_compositor* compositor_ = nullptr;
    wlr_xdg_shell* xdg_shell_ = nullptr;
    wlr_xdg_decoration_manager_v1* xdg_decoration_mgr_ = nullptr;
    wlr_layer_shell_v1* layer_shell_ = nullptr;
    wlr_foreign_toplevel_manager_v1* foreign_toplevel_mgr_ = nullptr;
    wlr_xdg_activation_v1* activation_ = nullptr;
    wlr_cursor_shape_manager_v1* cursor_shape_mgr_ = nullptr;
    wlr_idle_notifier_v1* idle_notifier_ = nullptr;
    wlr_idle_inhibit_manager_v1* idle_inhibit_mgr_ = nullptr;
    wlr_session_lock_manager_v1* session_lock_mgr_ = nullptr;
    wlr_xwayland* xwayland_ = nullptr;

    wlr_seat* seat_ = nullptr;
    wlr_cursor* cursor_ = nullptr;
    wlr_xcursor_manager* xcursor_mgr_ = nullptr;

    CursorMode cursor_mode_ = CursorMode::kPassthrough;
    ManagedWindow* grabbed_window_ = nullptr;
    double grab_x_ = 0.0;
    double grab_y_ = 0.0;
    wlr_box grab_geobox_{};
    uint32_t resize_edges_ = 0;

    ManagedWindow* focused_window_ = nullptr;
    wlr_box usable_area_{0, 0, 1440, 900};
    bool dark_theme_ = false;
    bool reduced_motion_ = false;
    bool all_minimized_state_ = false;
    int cursor_size_ = 24;
    std::string active_cursor_name_ = "left_ptr";

    std::list<std::unique_ptr<OutputDevice>> outputs_;
    std::list<std::unique_ptr<KeyboardDevice>> keyboards_;
    std::list<std::unique_ptr<PointerDevice>> pointers_;
    std::list<std::unique_ptr<ManagedWindow>> windows_;
    std::list<std::unique_ptr<PopupSurface>> popups_;
    std::list<std::unique_ptr<LayerSurface>> layers_;

    wl_event_source* watchdog_timer_ = nullptr;
    wl_event_source* theme_timer_ = nullptr;

    wl_listener new_output_{};
    wl_listener new_input_{};
    wl_listener new_xdg_toplevel_{};
    wl_listener new_xdg_popup_{};
    wl_listener new_xdg_decoration_{};
    wl_listener new_layer_surface_{};
    wl_listener request_cursor_{};
    wl_listener request_set_shape_{};
    wl_listener request_set_selection_{};
    wl_listener request_set_primary_selection_{};
    wl_listener request_activate_{};
    wl_listener cursor_motion_{};
    wl_listener cursor_motion_absolute_{};
    wl_listener cursor_button_{};
    wl_listener cursor_axis_{};
    wl_listener cursor_frame_{};
    wl_listener output_mgr_apply_{};
    wl_listener output_mgr_test_{};
    wl_listener xwayland_ready_{};
    wl_listener xwayland_new_surface_{};

    static void on_new_output(wl_listener* listener, void* data);
    static void on_new_input(wl_listener* listener, void* data);
    static void on_new_xdg_toplevel(wl_listener* listener, void* data);
    static void on_new_xdg_popup(wl_listener* listener, void* data);
    static void on_new_xdg_decoration(wl_listener* listener, void* data);
    static void on_new_layer_surface(wl_listener* listener, void* data);
    static void on_request_cursor(wl_listener* listener, void* data);
    static void on_request_set_shape(wl_listener* listener, void* data);
    static void on_request_set_selection(wl_listener* listener, void* data);
    static void on_request_set_primary_selection(wl_listener* listener, void* data);
    static void on_request_activate(wl_listener* listener, void* data);
    static void on_cursor_motion(wl_listener* listener, void* data);
    static void on_cursor_motion_absolute(wl_listener* listener, void* data);
    static void on_cursor_button(wl_listener* listener, void* data);
    static void on_cursor_axis(wl_listener* listener, void* data);
    static void on_cursor_frame(wl_listener* listener, void* data);
    static void on_output_mgr_apply(wl_listener* listener, void* data);
    static void on_output_mgr_test(wl_listener* listener, void* data);
    static void on_xwayland_ready(wl_listener* listener, void* data);
    static void on_xwayland_new_surface(wl_listener* listener, void* data);

    void on_output_manager_configure(wlr_output_configuration_v1* cfg, bool test_only);
};

}  // namespace finn

#endif
