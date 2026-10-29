#ifndef FINN_COMP_SERVER_HPP
#define FINN_COMP_SERVER_HPP

#include <wayland-server-core.h>

#include <memory>
#include <vector>

struct wl_display;
struct wlr_allocator;
struct wlr_backend;
struct wlr_cursor;
struct wlr_input_device;
struct wlr_keyboard;
struct wlr_output;
struct wlr_output_layout;
struct wlr_renderer;
struct wlr_scene;
struct wlr_scene_output_layout;
struct wlr_scene_rect;
struct wlr_scene_tree;
struct wlr_seat;
struct wlr_surface;
struct wlr_xcursor_manager;
struct wlr_xdg_shell;
struct wlr_xdg_toplevel;
struct wlr_layer_shell_v1;
struct wlr_layer_surface_v1;
struct wlr_scene_layer_surface_v1;

namespace finn {

/*
 * finn-comp, the display server. It owns the backend, the outputs, the seat
 * and the toplevels. It does not know what an app is called and it does not
 * draw the shell; those are separate programs.
 */
class Server {
public:
    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    int run();

private:
    struct Output;
    struct Toplevel;
    struct Keyboard;
    struct Layer;

    enum class CursorMode { passthrough, move, resize };

    bool init();
    void shutdown();

    void add_output(wlr_output* output);
    void add_input(wlr_input_device* device);
    void add_keyboard(wlr_keyboard* keyboard, wlr_input_device* device);
    void add_toplevel(wlr_xdg_toplevel* xdg_toplevel);
    void add_layer(wlr_layer_surface_v1* layer_surface);
    void remove_output(Output* output);
    void remove_toplevel(Toplevel* toplevel);
    void remove_keyboard(Keyboard* keyboard);
    void remove_layer(Layer* layer);
    void configure_layer(Layer* layer);
    void focus(Toplevel* toplevel);
    void focus_layer(Layer* layer);
    void maximize(Toplevel* toplevel);
    bool handle_keybinding(uint32_t keysym);
    void begin_interactive(Toplevel* toplevel, CursorMode mode, uint32_t edges);
    void process_cursor_motion(uint32_t time_msec);
    void process_cursor_move();
    void process_cursor_resize();
    void reset_cursor_mode();
    void pointer_motion(uint32_t time_msec);
    void update_background_theme();
    wlr_surface* surface_at(double lx, double ly, double* sx, double* sy);
    Toplevel* toplevel_at(double lx, double ly, wlr_surface** surface,
                          double* sx, double* sy);

    static void on_new_output(wl_listener* listener, void* data);
    static void on_new_input(wl_listener* listener, void* data);
    static void on_new_toplevel(wl_listener* listener, void* data);
    static void on_new_layer(wl_listener* listener, void* data);

    static void on_output_frame(wl_listener* listener, void* data);
    static void on_output_request_state(wl_listener* listener, void* data);
    static void on_output_destroy(wl_listener* listener, void* data);

    static void on_toplevel_map(wl_listener* listener, void* data);
    static void on_toplevel_unmap(wl_listener* listener, void* data);
    static void on_toplevel_commit(wl_listener* listener, void* data);
    static void on_toplevel_destroy(wl_listener* listener, void* data);
    static void on_toplevel_request_move(wl_listener* listener, void* data);
    static void on_toplevel_request_resize(wl_listener* listener, void* data);
    static void on_toplevel_request_maximize(wl_listener* listener, void* data);

    static void on_layer_map(wl_listener* listener, void* data);
    static void on_layer_commit(wl_listener* listener, void* data);
    static void on_layer_destroy(wl_listener* listener, void* data);

    static void on_keyboard_modifiers(wl_listener* listener, void* data);
    static void on_keyboard_key(wl_listener* listener, void* data);
    static void on_keyboard_destroy(wl_listener* listener, void* data);

    static void on_cursor_motion(wl_listener* listener, void* data);
    static void on_cursor_motion_absolute(wl_listener* listener, void* data);
    static void on_cursor_button(wl_listener* listener, void* data);
    static void on_cursor_axis(wl_listener* listener, void* data);
    static void on_cursor_frame(wl_listener* listener, void* data);

    static void on_request_cursor(wl_listener* listener, void* data);
    static void on_pointer_focus_change(wl_listener* listener, void* data);
    static void on_request_set_selection(wl_listener* listener, void* data);

    wl_display* display_ = nullptr;
    wlr_backend* backend_ = nullptr;
    wlr_renderer* renderer_ = nullptr;
    wlr_allocator* allocator_ = nullptr;
    wlr_scene* scene_ = nullptr;
    wlr_scene_output_layout* scene_layout_ = nullptr;
    wlr_scene_rect* background_ = nullptr;
    wlr_scene_tree* toplevel_tree_ = nullptr;
    wlr_scene_tree* layer_trees_[4] = {};
    wlr_output_layout* output_layout_ = nullptr;
    wlr_xdg_shell* xdg_shell_ = nullptr;
    wlr_layer_shell_v1* layer_shell_ = nullptr;
    wlr_seat* seat_ = nullptr;
    wlr_cursor* cursor_ = nullptr;
    wlr_xcursor_manager* cursor_manager_ = nullptr;
    wl_event_source* watchdog_ = nullptr;
    wl_event_source* theme_source_ = nullptr;

    wl_listener new_output_{};
    wl_listener new_input_{};
    wl_listener new_toplevel_{};
    wl_listener new_layer_{};
    wl_listener cursor_motion_{};
    wl_listener cursor_motion_absolute_{};
    wl_listener cursor_button_{};
    wl_listener cursor_axis_{};
    wl_listener cursor_frame_{};
    wl_listener request_cursor_{};
    wl_listener pointer_focus_change_{};
    wl_listener request_set_selection_{};

    std::vector<std::unique_ptr<Output>> outputs_;
    std::vector<std::unique_ptr<Toplevel>> toplevels_;
    std::vector<std::unique_ptr<Keyboard>> keyboards_;
    std::vector<std::unique_ptr<Layer>> layers_;
    Toplevel* focused_ = nullptr;
    Toplevel* last_focused_ = nullptr;
    Layer* focused_layer_ = nullptr;
    CursorMode cursor_mode_ = CursorMode::passthrough;
    Toplevel* grabbed_ = nullptr;
    double grab_x_ = 0;
    double grab_y_ = 0;
    int grab_gx_ = 0;
    int grab_gy_ = 0;
    int grab_gw_ = 0;
    int grab_gh_ = 0;
    uint32_t resize_edges_ = 0;
    int usable_x_ = 0;
    int usable_y_ = 0;
    int usable_w_ = 0;
    int usable_h_ = 0;
};

}  // namespace finn

#endif
