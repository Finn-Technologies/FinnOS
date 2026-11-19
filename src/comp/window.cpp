#include "comp/internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace finn {
namespace {

void set_rect_color(wlr_scene_rect* rect, float r, float g, float b, float a) {
    if (!rect) return;
    const float c[4] = {r, g, b, a};
    wlr_scene_rect_set_color(rect, c);
}

void set_rect_bounds(wlr_scene_rect* rect, int x, int y, int w, int h, bool visible) {
    if (!rect) return;
    wlr_scene_node_set_enabled(&rect->node, visible);
    if (visible && w > 0 && h > 0) {
        wlr_scene_node_set_position(&rect->node, x, y);
        wlr_scene_rect_set_size(rect, w, h);
    }
}

bool point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

}  // namespace

Server::ManagedWindow::~ManagedWindow() {
    stop_animation();
    remove_listener(map_l);
    remove_listener(unmap_l);
    remove_listener(commit_l);
    remove_listener(destroy_l);
    remove_listener(req_move_l);
    remove_listener(req_resize_l);
    remove_listener(req_maximize_l);
    remove_listener(req_minimize_l);
    remove_listener(req_fullscreen_l);
    remove_listener(set_title_l);
    remove_listener(set_app_id_l);
    remove_listener(dec_request_mode_l);
    remove_listener(dec_destroy_l);
    remove_listener(xw_associate_l);
    remove_listener(xw_dissociate_l);
    remove_listener(xw_req_configure_l);
    remove_listener(xw_req_activate_l);
    remove_listener(xw_req_close_l);
    remove_listener(ft_req_activate_l);
    remove_listener(ft_req_maximize_l);
    remove_listener(ft_req_minimize_l);
    remove_listener(ft_req_fullscreen_l);
    remove_listener(ft_req_close_l);
    if (foreign_handle) {
        wlr_foreign_toplevel_handle_v1_destroy(foreign_handle);
        foreign_handle = nullptr;
    }
    if (scene_tree) {
        wlr_scene_node_destroy(&scene_tree->node);
        scene_tree = nullptr;
        surface_tree = nullptr;
    }
}

wlr_surface* Server::ManagedWindow::main_surface() const {
    if (is_xwayland) return xwayland_surface ? xwayland_surface->surface : nullptr;
    return xdg_toplevel && xdg_toplevel->base ? xdg_toplevel->base->surface : nullptr;
}

std::string Server::ManagedWindow::title() const {
    if (is_xwayland) {
        return (xwayland_surface && xwayland_surface->title) ? xwayland_surface->title
                                                             : "Application";
    }
    return (xdg_toplevel && xdg_toplevel->title) ? xdg_toplevel->title : "Application";
}

std::string Server::ManagedWindow::app_id() const {
    if (is_xwayland) {
        const char* cls = finn_xwayland_surface_class(xwayland_surface);
        if (cls && *cls) return cls;
        return (xwayland_surface && xwayland_surface->instance)
                   ? xwayland_surface->instance
                   : "x11";
    }
    return (xdg_toplevel && xdg_toplevel->app_id) ? xdg_toplevel->app_id : "";
}

wlr_box Server::ManagedWindow::total_box() const {
    return wlr_box{x, y, width, height + title_offset()};
}

void Server::ManagedWindow::create_ssd_nodes() {
    if (!scene_tree || ssd_titlebar) return;
    const float zero[4] = {0, 0, 0, 0};
    ssd_titlebar = wlr_scene_rect_create(scene_tree, 1, 1, zero);
    ssd_divider = wlr_scene_rect_create(scene_tree, 1, 1, zero);
    ssd_border_top = wlr_scene_rect_create(scene_tree, 1, 1, zero);
    ssd_border_bottom = wlr_scene_rect_create(scene_tree, 1, 1, zero);
    ssd_border_left = wlr_scene_rect_create(scene_tree, 1, 1, zero);
    ssd_border_right = wlr_scene_rect_create(scene_tree, 1, 1, zero);
    ssd_btn_min = wlr_scene_rect_create(scene_tree, kSsdButtonW, kSsdButtonH, zero);
    ssd_btn_max = wlr_scene_rect_create(scene_tree, kSsdButtonW, kSsdButtonH, zero);
    ssd_btn_close = wlr_scene_rect_create(scene_tree, kSsdButtonW, kSsdButtonH, zero);
    ssd_glyph_min = wlr_scene_rect_create(scene_tree, 10, 2, zero);
    ssd_glyph_max = wlr_scene_rect_create(scene_tree, 9, 9, zero);
    ssd_glyph_close_a = wlr_scene_rect_create(scene_tree, 9, 2, zero);
    ssd_glyph_close_b = wlr_scene_rect_create(scene_tree, 2, 9, zero);
}

void Server::ManagedWindow::set_ssd_enabled(bool enabled) {
    ssd_enabled = enabled;
    if (enabled && !ssd_titlebar) create_ssd_nodes();
    update_ssd_layout();
    apply_ssd_theme(server->dark_theme(), server->focused_window() == this);
}

void Server::ManagedWindow::update_ssd_layout() {
    if (scene_tree && surface_tree) {
        wlr_scene_node_set_position(&surface_tree->node, 0, title_offset());
    }
    if (!ssd_titlebar) return;

    const bool vis = ssd_enabled && !fullscreen && mapped && !minimized;
    const int w = std::max(width, 120);
    const int h = std::max(height, 40);
    const int tot_h = h + kSsdTitleHeight;

    set_rect_bounds(ssd_titlebar, 0, 0, w, kSsdTitleHeight, vis);
    set_rect_bounds(ssd_divider, 0, kSsdTitleHeight - 1, w, 1, vis);
    set_rect_bounds(ssd_border_top, -1, -1, w + 2, 1, vis);
    set_rect_bounds(ssd_border_bottom, -1, tot_h, w + 2, 1, vis);
    set_rect_bounds(ssd_border_left, -1, 0, 1, tot_h, vis);
    set_rect_bounds(ssd_border_right, w, 0, 1, tot_h, vis);

    const int close_x = w - kSsdControlsRightPad - kSsdButtonW;
    const int max_x = close_x - kSsdButtonGap - kSsdButtonW;
    const int min_x = max_x - kSsdButtonGap - kSsdButtonW;
    const int btn_y = (kSsdTitleHeight - kSsdButtonH) / 2;

    set_rect_bounds(ssd_btn_min, min_x, btn_y, kSsdButtonW, kSsdButtonH, vis);
    set_rect_bounds(ssd_btn_max, max_x, btn_y, kSsdButtonW, kSsdButtonH, vis);
    set_rect_bounds(ssd_btn_close, close_x, btn_y, kSsdButtonW, kSsdButtonH, vis);

    set_rect_bounds(ssd_glyph_min, min_x + 7, btn_y + 9, 10, 2, vis);
    set_rect_bounds(ssd_glyph_max, max_x + 8, btn_y + 6, 8, 8, vis);
    set_rect_bounds(ssd_glyph_close_a, close_x + 8, btn_y + 9, 8, 2, vis);
    set_rect_bounds(ssd_glyph_close_b, close_x + 11, btn_y + 6, 2, 8, vis);
}

void Server::ManagedWindow::apply_ssd_theme(bool dark, bool focused) {
    if (!ssd_titlebar || !ssd_enabled) return;
    if (dark) {
        const float bg = focused ? 0.165f : 0.140f;
        set_rect_color(ssd_titlebar, bg, bg + 0.01f, bg + 0.03f, 0.96f);
        set_rect_color(ssd_divider, 1, 1, 1, 0.10f);
        set_rect_color(ssd_border_top, 1, 1, 1, focused ? 0.20f : 0.10f);
        set_rect_color(ssd_border_bottom, 1, 1, 1, focused ? 0.20f : 0.10f);
        set_rect_color(ssd_border_left, 1, 1, 1, focused ? 0.20f : 0.10f);
        set_rect_color(ssd_border_right, 1, 1, 1, focused ? 0.20f : 0.10f);
        set_rect_color(ssd_btn_min, 0.22f, 0.24f, 0.29f, 1.0f);
        set_rect_color(ssd_btn_max, 0.22f, 0.24f, 0.29f, 1.0f);
        set_rect_color(ssd_btn_close, hover_close ? 1.0f : 0.22f,
                       hover_close ? 0.01f : 0.24f,
                       hover_close ? 0.01f : 0.29f, 1.0f);
        set_rect_color(ssd_glyph_min, 0.94f, 0.93f, 0.90f, 1.0f);
        set_rect_color(ssd_glyph_max, 0.94f, 0.93f, 0.90f, 1.0f);
        set_rect_color(ssd_glyph_close_a, 1.0f, 1.0f, 1.0f, 1.0f);
        set_rect_color(ssd_glyph_close_b, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        set_rect_color(ssd_titlebar, focused ? 0.98f : 0.94f,
                       focused ? 0.98f : 0.94f, focused ? 0.99f : 0.95f, 0.97f);
        set_rect_color(ssd_divider, 0, 0, 0, 0.08f);
        set_rect_color(ssd_border_top, 0, 0, 0, focused ? 0.16f : 0.09f);
        set_rect_color(ssd_border_bottom, 0, 0, 0, focused ? 0.16f : 0.09f);
        set_rect_color(ssd_border_left, 0, 0, 0, focused ? 0.16f : 0.09f);
        set_rect_color(ssd_border_right, 0, 0, 0, focused ? 0.16f : 0.09f);
        set_rect_color(ssd_btn_min, 1.0f, 1.0f, 1.0f, 1.0f);
        set_rect_color(ssd_btn_max, 1.0f, 1.0f, 1.0f, 1.0f);
        set_rect_color(ssd_btn_close, 1.0f, hover_close ? 0.01f : 1.0f,
                       hover_close ? 0.01f : 1.0f, 1.0f);
        set_rect_color(ssd_glyph_min, 0.08f, 0.11f, 0.20f, 1.0f);
        set_rect_color(ssd_glyph_max, 0.08f, 0.11f, 0.20f, 1.0f);
        const float cg = hover_close ? 1.0f : 0.08f;
        set_rect_color(ssd_glyph_close_a, cg, cg, hover_close ? 1.0f : 0.20f, 1.0f);
        set_rect_color(ssd_glyph_close_b, cg, cg, hover_close ? 1.0f : 0.20f, 1.0f);
    }
}

void Server::ManagedWindow::update_size_from_surface() {
    if (is_xwayland) {
        if (!xwayland_surface) return;
        width = std::max<int>(xwayland_surface->width, 1);
        height = std::max<int>(xwayland_surface->height, 1);
    } else if (xdg_toplevel && xdg_toplevel->base) {
        const wlr_box& geom = xdg_toplevel->base->current.geometry;
        if (geom.width > 0 && geom.height > 0) {
            width = geom.width;
            height = geom.height;
        } else if (wlr_surface* s = main_surface()) {
            if (s->current.width > 0 && s->current.height > 0) {
                width = s->current.width;
                height = s->current.height;
            }
        }
    }
    update_ssd_layout();
}

void Server::ManagedWindow::configure_size(int client_w, int client_h) {
    client_w = std::max(client_w, 160);
    client_h = std::max(client_h, 80);
    if (is_xwayland) {
        if (xwayland_surface) {
            wlr_xwayland_surface_configure(
                xwayland_surface, static_cast<int16_t>(x),
                static_cast<int16_t>(y + title_offset()),
                static_cast<uint16_t>(client_w), static_cast<uint16_t>(client_h));
        }
    } else if (xdg_toplevel) {
        wlr_xdg_toplevel_set_size(xdg_toplevel, client_w, client_h);
    }
    width = client_w;
    height = client_h;
    update_ssd_layout();
}

namespace {

void apply_tree_opacity(wlr_scene_tree* tree, float alpha) {
    if (!tree) return;
    wlr_scene_node* node = nullptr;
    wl_list_for_each(node, &tree->children, link) {
        if (node->type == WLR_SCENE_NODE_BUFFER) {
            wlr_scene_buffer_set_opacity(wlr_scene_buffer_from_node(node), alpha);
        } else if (node->type == WLR_SCENE_NODE_TREE) {
            apply_tree_opacity(wlr_scene_tree_from_node(node), alpha);
        }
    }
}

}  // namespace

void Server::ManagedWindow::apply_opacity(double opacity) {
    cur_opacity = std::clamp(opacity, 0.0, 1.0);
    if (!scene_tree) return;
    apply_tree_opacity(scene_tree, static_cast<float>(cur_opacity));
}

void Server::ManagedWindow::stop_animation() {
    if (anim_timer) {
        wl_event_source_remove(anim_timer);
        anim_timer = nullptr;
    }
    anim_hide_on_settle = false;
}

void Server::ManagedWindow::start_spring_animation(
    double from_x, double from_y, double from_opacity, double to_opacity,
    const finn::ui::SpringParams& params, bool hide_on_settle,
    double target_x_override, double target_y_override) {
    if (!scene_tree) return;
    const double target_x = (target_x_override < 1e8) ? target_x_override : static_cast<double>(x);
    const double target_y = (target_y_override < 1e8) ? target_y_override : static_cast<double>(y);

    if (!server || !server->display() || server->reduced_motion()) {
        stop_animation();
        wlr_scene_node_set_position(&scene_tree->node,
                                    static_cast<int>(std::lround(target_x)),
                                    static_cast<int>(std::lround(target_y)));
        apply_opacity(to_opacity);
        if (hide_on_settle) {
            wlr_scene_node_set_enabled(&scene_tree->node, false);
        }
        return;
    }

    // Preserve velocity if retargeting mid-flight for 100% interruptible macOS motion.
    const bool mid_flight = (anim_timer != nullptr);
    const double vx = mid_flight ? spring_x.velocity() : 0.0;
    const double vy = mid_flight ? spring_y.velocity() : 0.0;
    const double vo = mid_flight ? spring_opacity.velocity() : 0.0;

    spring_x.set_thresholds(0.25, 0.5);
    spring_y.set_thresholds(0.25, 0.5);
    spring_opacity.set_thresholds(0.005, 0.01);

    spring_x.set(mid_flight ? spring_x.value() : from_x, vx);
    spring_y.set(mid_flight ? spring_y.value() : from_y, vy);
    spring_opacity.set(mid_flight ? spring_opacity.value() : from_opacity, vo);

    spring_x.retarget(target_x, params);
    spring_y.retarget(target_y, params);
    spring_opacity.retarget(std::clamp(to_opacity, 0.0, 1.0), params);

    anim_hide_on_settle = hide_on_settle;
    anim_last_ms = Server::monotonic_msec();

    wlr_scene_node_set_position(&scene_tree->node,
                                static_cast<int>(std::lround(spring_x.value())),
                                static_cast<int>(std::lround(spring_y.value())));
    apply_opacity(spring_opacity.value());

    if (!anim_timer) {
        wl_event_loop* loop = wl_display_get_event_loop(server->display());
        if (!loop) {
            wlr_scene_node_set_position(&scene_tree->node, x, y);
            apply_opacity(to_opacity);
            if (hide_on_settle) wlr_scene_node_set_enabled(&scene_tree->node, false);
            return;
        }
        anim_timer = wl_event_loop_add_timer(
            loop,
            [](void* data) -> int {
                auto* win = static_cast<Server::ManagedWindow*>(data);
                if (!win->scene_tree || !win->mapped) {
                    if (win->scene_tree) {
                        wlr_scene_node_set_position(&win->scene_tree->node, win->x, win->y);
                        win->apply_opacity(1.0);
                    }
                    win->stop_animation();
                    return 0;
                }
                const uint64_t now = Server::monotonic_msec();
                const double dt = std::clamp(
                    static_cast<double>(now > win->anim_last_ms ? (now - win->anim_last_ms) : 8) *
                        0.001,
                    0.001, 0.05);
                win->anim_last_ms = now;

                const double cx = win->spring_x.step(dt);
                const double cy = win->spring_y.step(dt);
                const double co = win->spring_opacity.step(dt);

                wlr_scene_node_set_position(&win->scene_tree->node,
                                            static_cast<int>(std::lround(cx)),
                                            static_cast<int>(std::lround(cy)));
                win->apply_opacity(co);

                if (win->spring_x.settled() && win->spring_y.settled() &&
                    win->spring_opacity.settled()) {
                    const bool hide_after = win->anim_hide_on_settle;
                    wlr_scene_node_set_position(
                        &win->scene_tree->node,
                        static_cast<int>(std::lround(win->spring_x.target())),
                        static_cast<int>(std::lround(win->spring_y.target())));
                    win->apply_opacity(win->spring_opacity.target());
                    win->stop_animation();
                    if (hide_after && win->scene_tree) {
                        wlr_scene_node_set_enabled(&win->scene_tree->node, false);
                        wlr_scene_node_set_position(&win->scene_tree->node, win->x, win->y);
                        win->apply_opacity(1.0);
                    }
                    return 0;
                }
                wl_event_source_timer_update(win->anim_timer, 8);
                return 0;
            },
            this);
    }
    if (anim_timer) {
        wl_event_source_timer_update(anim_timer, 8);
    }
}

void Server::ManagedWindow::start_animation(double from_x, double from_y,
                                            uint32_t duration_ms) {
    if (duration_ms == 0) {
        stop_animation();
        if (scene_tree) {
            wlr_scene_node_set_position(&scene_tree->node, x, y);
            apply_opacity(1.0);
        }
        return;
    }
    start_spring_animation(from_x, from_y, 0.0, 1.0, finn::ui::springs::kSmooth, false);
}

void Server::ManagedWindow::set_position(int nx, int ny) {
    if (server && (server->cursor_mode() == Server::CursorMode::kMove ||
                   server->cursor_mode() == Server::CursorMode::kResize)) {
        stop_animation();
    }
    x = nx;
    y = ny;
    if (scene_tree && !anim_timer) wlr_scene_node_set_position(&scene_tree->node, x, y);
    if (is_xwayland && xwayland_surface && mapped) {
        wlr_xwayland_surface_configure(
            xwayland_surface, static_cast<int16_t>(x),
            static_cast<int16_t>(y + title_offset()),
            static_cast<uint16_t>(std::max(width, 1)),
            static_cast<uint16_t>(std::max(height, 1)));
    }
}

void Server::ManagedWindow::set_activated(bool active) {
    if (is_xwayland) {
        if (xwayland_surface && !override_redirect) {
            wlr_xwayland_surface_activate(xwayland_surface, active);
        }
    } else if (xdg_toplevel && xdg_toplevel->base && xdg_toplevel->base->initialized) {
        wlr_xdg_toplevel_set_activated(xdg_toplevel, active);
    }
    if (foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_activated(foreign_handle, active);
    }
    apply_ssd_theme(server->dark_theme(), active);
}

void Server::ManagedWindow::set_maximized(bool on) {
    if (override_redirect || maximized == on) return;
    if (fullscreen) set_fullscreen(false);
    const double old_x = anim_timer ? spring_x.value() : static_cast<double>(x);
    const double old_y = anim_timer ? spring_y.value() : static_cast<double>(y);
    if (on) {
        saved_geometry = wlr_box{x, y, width, height};
        const wlr_box area = server->usable_area_for();
        maximized = true;
        set_position(area.x, area.y);
        configure_size(area.width, std::max(120, area.height - title_offset()));
    } else {
        maximized = false;
        set_position(saved_geometry.x, saved_geometry.y);
        configure_size(saved_geometry.width, saved_geometry.height);
    }
    if (mapped && !minimized) {
        start_spring_animation(old_x, old_y, cur_opacity, 1.0,
                               finn::ui::springs::kSmooth, false);
    }
    if (is_xwayland && xwayland_surface) {
        wlr_xwayland_surface_set_maximized(xwayland_surface, maximized, maximized);
    } else if (xdg_toplevel) {
        wlr_xdg_toplevel_set_maximized(xdg_toplevel, maximized);
    }
    sync_foreign_state();
    server->process_cursor_motion(Server::monotonic_msec());
}

void Server::ManagedWindow::set_fullscreen(bool on) {
    if (override_redirect) return;
    if (fullscreen != on) {
        const double old_x = anim_timer ? spring_x.value() : static_cast<double>(x);
        const double old_y = anim_timer ? spring_y.value() : static_cast<double>(y);
        if (on) {
            if (!maximized) saved_geometry = wlr_box{x, y, width, height};
            saved_maximized = maximized;
            fullscreen = true;
            if (scene_tree && server && server->fullscreen_tree()) {
                wlr_scene_node_reparent(&scene_tree->node, server->fullscreen_tree());
            }
            int out_w = 1440, out_h = 900;
            if (wlr_output* out = server ? server->primary_output() : nullptr) {
                wlr_output_effective_resolution(out, &out_w, &out_h);
            }
            set_position(0, 0);
            configure_size(out_w, out_h);
        } else {
            fullscreen = false;
            if (scene_tree && server && server->toplevel_tree()) {
                wlr_scene_node_reparent(&scene_tree->node, server->toplevel_tree());
            }
            if (saved_maximized) {
                const wlr_box area = server->usable_area_for();
                maximized = true;
                set_position(area.x, area.y);
                configure_size(area.width, std::max(120, area.height - title_offset()));
            } else {
                maximized = false;
                set_position(saved_geometry.x, saved_geometry.y);
                configure_size(saved_geometry.width, saved_geometry.height);
            }
        }
        update_ssd_layout();
        if (mapped && !minimized) {
            start_spring_animation(old_x, old_y, cur_opacity, 1.0,
                                   finn::ui::springs::kSmooth, false);
        }
    }
    if (is_xwayland && xwayland_surface) {
        wlr_xwayland_surface_set_fullscreen(xwayland_surface, fullscreen);
    } else if (xdg_toplevel) {
        wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, fullscreen);
    }
    sync_foreign_state();
    if (server) server->process_cursor_motion(Server::monotonic_msec());
}

void Server::ManagedWindow::set_minimized(bool on) {
    if (override_redirect || minimized == on) return;
    minimized = on;
    const wlr_box area = server ? server->usable_area_for() : wlr_box{0, 0, 1440, 900};
    const double dock_y = static_cast<double>(area.y + area.height - 60);

    if (minimized) {
        if (is_xwayland && xwayland_surface) {
            wlr_xwayland_surface_set_minimized(xwayland_surface, true);
        }
        sync_foreign_state();
        if (server && server->focused_window() == this) {
            server->focus_top_window();
        }
        if (scene_tree && mapped) {
            // Smooth macOS minimize-to-dock spring glide + fade out, then disable node.
            const double from_x = anim_timer ? spring_x.value() : static_cast<double>(x);
            const double from_y = anim_timer ? spring_y.value() : static_cast<double>(y);
            start_spring_animation(from_x, from_y, cur_opacity, 0.0,
                                   finn::ui::springs::kSnappy, true,
                                   static_cast<double>(x), dock_y);
        } else if (scene_tree) {
            wlr_scene_node_set_enabled(&scene_tree->node, false);
        }
    } else {
        if (scene_tree) {
            wlr_scene_node_set_enabled(&scene_tree->node, mapped);
        }
        if (is_xwayland && xwayland_surface) {
            wlr_xwayland_surface_set_minimized(xwayland_surface, false);
        }
        sync_foreign_state();
        if (scene_tree && mapped) {
            const double from_x = anim_timer ? spring_x.value() : static_cast<double>(x);
            const double from_y = anim_timer ? spring_y.value() : std::min(dock_y, static_cast<double>(y + 90));
            const double from_op = anim_timer ? cur_opacity : 0.0;
            start_spring_animation(from_x, from_y, from_op, 1.0,
                                   finn::ui::springs::kSmooth, false);
        }
        if (server) server->focus_window(this);
    }
    if (server) server->process_cursor_motion(Server::monotonic_msec());
}

void Server::ManagedWindow::snap_half(bool left) {
    if (override_redirect) return;
    const double old_x = anim_timer ? spring_x.value() : static_cast<double>(x);
    const double old_y = anim_timer ? spring_y.value() : static_cast<double>(y);
    if (!maximized) saved_geometry = wlr_box{x, y, width, height};
    maximized = false;
    const wlr_box area = server->usable_area_for();
    const int gap = 8;
    const int half_w = std::max(240, (area.width - gap * 3) / 2);
    const int nx = left ? (area.x + gap) : (area.x + gap * 2 + half_w);
    const int ny = area.y + gap;
    const int ch = std::max(160, area.height - gap * 2 - title_offset());
    set_position(nx, ny);
    configure_size(half_w, ch);
    if (mapped && !minimized) {
        start_spring_animation(old_x, old_y, cur_opacity, 1.0,
                               finn::ui::springs::kSmooth, false);
    }
    if (!is_xwayland && xdg_toplevel) wlr_xdg_toplevel_set_maximized(xdg_toplevel, false);
    sync_foreign_state();
}

void Server::ManagedWindow::close() {
    if (is_xwayland) {
        if (xwayland_surface) wlr_xwayland_surface_close(xwayland_surface);
    } else if (xdg_toplevel) {
        wlr_xdg_toplevel_send_close(xdg_toplevel);
    }
}

void Server::ManagedWindow::ensure_foreign_handle() {
    if (override_redirect || foreign_handle || !server->foreign_toplevel_mgr()) return;
    foreign_handle = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_mgr());
    if (!foreign_handle) return;
    foreign_handle->data = this;
    sync_foreign_state();
    if (wlr_output* out = server->primary_output()) {
        wlr_foreign_toplevel_handle_v1_output_enter(foreign_handle, out);
    }

    attach_listener(foreign_handle->events.request_activate, ft_req_activate_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, ft_req_activate_l);
                        if (w->minimized) w->set_minimized(false);
                        w->server->focus_window(w);
                    });
    attach_listener(foreign_handle->events.request_maximize, ft_req_maximize_l,
                    [](wl_listener* l, void* data) {
                        ManagedWindow* w = wl_container_of(l, w, ft_req_maximize_l);
                        const auto* ev =
                            static_cast<const wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
                        w->set_maximized(ev->maximized);
                    });
    attach_listener(foreign_handle->events.request_minimize, ft_req_minimize_l,
                    [](wl_listener* l, void* data) {
                        ManagedWindow* w = wl_container_of(l, w, ft_req_minimize_l);
                        const auto* ev =
                            static_cast<const wlr_foreign_toplevel_handle_v1_minimized_event*>(data);
                        w->set_minimized(ev->minimized);
                    });
    attach_listener(foreign_handle->events.request_fullscreen, ft_req_fullscreen_l,
                    [](wl_listener* l, void* data) {
                        ManagedWindow* w = wl_container_of(l, w, ft_req_fullscreen_l);
                        const auto* ev =
                            static_cast<const wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data);
                        w->set_fullscreen(ev->fullscreen);
                    });
    attach_listener(foreign_handle->events.request_close, ft_req_close_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, ft_req_close_l);
                        w->close();
                    });
}

void Server::ManagedWindow::sync_foreign_state() {
    if (!foreign_handle) return;
    wlr_foreign_toplevel_handle_v1_set_title(foreign_handle, title().c_str());
    const std::string aid = app_id();
    if (!aid.empty()) wlr_foreign_toplevel_handle_v1_set_app_id(foreign_handle, aid.c_str());
    wlr_foreign_toplevel_handle_v1_set_maximized(foreign_handle, maximized);
    wlr_foreign_toplevel_handle_v1_set_minimized(foreign_handle, minimized);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(foreign_handle, fullscreen);
    wlr_foreign_toplevel_handle_v1_set_activated(foreign_handle,
                                                 server->focused_window() == this);
}

Server::ManagedWindow::HitZone Server::ManagedWindow::hit_test(
    double lx, double ly, uint32_t* resize_edges) const {
    if (!mapped || minimized) return HitZone::kNone;
    const int px = static_cast<int>(lx);
    const int py = static_cast<int>(ly);
    const wlr_box b = total_box();

    if (px < b.x - kResizeEdgeMargin || px >= b.x + b.width + kResizeEdgeMargin ||
        py < b.y - kResizeEdgeMargin || py >= b.y + b.height + kResizeEdgeMargin) {
        return HitZone::kNone;
    }

    if (ssd_enabled && !fullscreen && !override_redirect) {
        const int rx = px - b.x;
        const int ry = py - b.y;
        const int close_x = b.width - kSsdControlsRightPad - kSsdButtonW;
        const int max_x = close_x - kSsdButtonGap - kSsdButtonW;
        const int min_x = max_x - kSsdButtonGap - kSsdButtonW;
        const int btn_y = (kSsdTitleHeight - kSsdButtonH) / 2;

        if (point_in(rx, ry, close_x, btn_y, kSsdButtonW, kSsdButtonH)) return HitZone::kBtnClose;
        if (point_in(rx, ry, max_x, btn_y, kSsdButtonW, kSsdButtonH)) return HitZone::kBtnMax;
        if (point_in(rx, ry, min_x, btn_y, kSsdButtonW, kSsdButtonH)) return HitZone::kBtnMin;

        uint32_t edges = WLR_EDGE_NONE;
        if (rx < kResizeEdgeMargin) edges |= WLR_EDGE_LEFT;
        else if (rx >= b.width - kResizeEdgeMargin) edges |= WLR_EDGE_RIGHT;
        if (ry < kResizeEdgeMargin) edges |= WLR_EDGE_TOP;
        else if (ry >= b.height - kResizeEdgeMargin) edges |= WLR_EDGE_BOTTOM;

        if (edges != WLR_EDGE_NONE) {
            if (resize_edges) *resize_edges = edges;
            return HitZone::kResize;
        }
        if (ry >= 0 && ry < kSsdTitleHeight && rx >= 0 && rx < b.width) {
            return HitZone::kMoveBar;
        }
    }

    if (point_in(px, py, b.x, b.y, b.width, b.height)) return HitZone::kClient;
    return HitZone::kNone;
}

void setup_xdg_decoration(Server* server, wlr_xdg_toplevel_decoration_v1* dec) {
    Server::ManagedWindow* w = server ? server->find_xdg_toplevel(dec->toplevel) : nullptr;
    if (!w) return;
    w->xdg_decoration = dec;

    const auto apply_mode = [w]() {
        if (!w->xdg_decoration) return;
        const auto mode =
            (w->xdg_decoration->requested_mode ==
             WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
                ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        if (w->xdg_toplevel && w->xdg_toplevel->base->initialized) {
            wlr_xdg_toplevel_decoration_v1_set_mode(w->xdg_decoration, mode);
        }
        w->set_ssd_enabled(mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    };
    apply_mode();

    attach_listener(dec->events.request_mode, w->dec_request_mode_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win =
                            wl_container_of(l, win, dec_request_mode_l);
                        if (!win->xdg_decoration) return;
                        const auto mode =
                            (win->xdg_decoration->requested_mode ==
                             WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
                                ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
                        if (win->xdg_toplevel && win->xdg_toplevel->base->initialized) {
                            wlr_xdg_toplevel_decoration_v1_set_mode(win->xdg_decoration, mode);
                        }
                        win->set_ssd_enabled(
                            mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
                    });

    attach_listener(dec->events.destroy, w->dec_destroy_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win = wl_container_of(l, win, dec_destroy_l);
                        remove_listener(win->dec_request_mode_l);
                        remove_listener(win->dec_destroy_l);
                        win->xdg_decoration = nullptr;
                    });
}

void setup_xwayland_window(Server::ManagedWindow* w, wlr_xwayland_surface* xsurface) {
    w->is_xwayland = true;
    w->xwayland_surface = xsurface;
    w->override_redirect = xsurface->override_redirect;
    w->scene_tree = wlr_scene_tree_create(w->server->toplevel_tree());
    w->scene_tree->node.data = w;
    wlr_scene_node_set_enabled(&w->scene_tree->node, false);

    const bool wants_ssd =
        !xsurface->override_redirect &&
        !(xsurface->decorations & WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE);
    w->set_ssd_enabled(wants_ssd);

    attach_listener(
        xsurface->events.associate, w->xw_associate_l,
        [](wl_listener* l, void*) {
            Server::ManagedWindow* win = wl_container_of(l, win, xw_associate_l);
            if (!win->xwayland_surface->surface) return;
            win->surface_tree = wlr_scene_subsurface_tree_create(
                win->scene_tree, win->xwayland_surface->surface);
            win->update_ssd_layout();

            attach_listener(
                win->xwayland_surface->surface->events.map, win->map_l,
                [](wl_listener* ml, void*) {
                    Server::ManagedWindow* mw = wl_container_of(ml, mw, map_l);
                    mw->mapped = true;
                    mw->override_redirect = mw->xwayland_surface->override_redirect;
                    if (mw->override_redirect) {
                        mw->x = mw->xwayland_surface->x;
                        mw->y = mw->xwayland_surface->y;
                        mw->set_ssd_enabled(false);
                    } else {
                        const wlr_box area = mw->server->usable_area_for();
                        if (mw->xwayland_surface->x > 0 || mw->xwayland_surface->y > 0) {
                            mw->x = std::clamp<int>(mw->xwayland_surface->x, area.x,
                                                    area.x + area.width - 120);
                            mw->y = std::clamp<int>(mw->xwayland_surface->y, area.y,
                                                    area.y + area.height - 120);
                        } else {
                            mw->x = area.x + 60;
                            mw->y = area.y + 44;
                        }
                    }
                    mw->update_size_from_surface();
                    mw->set_position(mw->x, mw->y);
                    wlr_scene_node_set_enabled(&mw->scene_tree->node, !mw->minimized);
                    mw->update_ssd_layout();
                    if (!mw->override_redirect) {
                        mw->ensure_foreign_handle();
                        mw->sync_foreign_state();
                        if (!mw->minimized) {
                            mw->start_spring_animation(mw->x, mw->y + 24, 0.0, 1.0,
                                                       finn::ui::springs::kSmooth, false);
                        }
                        mw->server->focus_window(mw);
                    } else if (wlr_xwayland_surface_override_redirect_wants_focus(
                                   mw->xwayland_surface)) {
                        mw->server->focus_window(mw);
                    }
                    mw->server->process_cursor_motion(Server::monotonic_msec());
                });

            attach_listener(
                win->xwayland_surface->surface->events.unmap, win->unmap_l,
                [](wl_listener* ul, void*) {
                    Server::ManagedWindow* mw = wl_container_of(ul, mw, unmap_l);
                    mw->stop_animation();
                    mw->mapped = false;
                    wlr_scene_node_set_enabled(&mw->scene_tree->node, false);
                    if (mw->server->focused_window() == mw) {
                        mw->server->focus_top_window();
                    }
                    mw->server->process_cursor_motion(Server::monotonic_msec());
                });
        });

    attach_listener(
        xsurface->events.dissociate, w->xw_dissociate_l,
        [](wl_listener* l, void*) {
            Server::ManagedWindow* win = wl_container_of(l, win, xw_dissociate_l);
            remove_listener(win->map_l);
            remove_listener(win->unmap_l);
            if (win->surface_tree) {
                wlr_scene_node_destroy(&win->surface_tree->node);
                win->surface_tree = nullptr;
            }
        });

    attach_listener(
        xsurface->events.request_configure, w->xw_req_configure_l,
        [](wl_listener* l, void* data) {
            Server::ManagedWindow* win = wl_container_of(l, win, xw_req_configure_l);
            const auto* ev =
                static_cast<const wlr_xwayland_surface_configure_event*>(data);
            if (!win->mapped || win->override_redirect) {
                wlr_xwayland_surface_configure(win->xwayland_surface, ev->x, ev->y,
                                               ev->width, ev->height);
                win->width = std::max<int>(ev->width, 1);
                win->height = std::max<int>(ev->height, 1);
                if (win->override_redirect) win->set_position(ev->x, ev->y);
                win->update_ssd_layout();
            } else {
                win->configure_size(ev->width, ev->height);
            }
        });

    attach_listener(xsurface->events.request_move, w->req_move_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win = wl_container_of(l, win, req_move_l);
                        win->server->begin_move(win);
                    });

    attach_listener(xsurface->events.request_resize, w->req_resize_l,
                    [](wl_listener* l, void* data) {
                        Server::ManagedWindow* win = wl_container_of(l, win, req_resize_l);
                        const auto* ev = static_cast<const wlr_xwayland_resize_event*>(data);
                        win->server->begin_resize(win, ev->edges);
                    });

    attach_listener(xsurface->events.request_maximize, w->req_maximize_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win =
                            wl_container_of(l, win, req_maximize_l);
                        const bool on = win->xwayland_surface->maximized_horz &&
                                        win->xwayland_surface->maximized_vert;
                        win->set_maximized(on);
                    });

    attach_listener(xsurface->events.request_minimize, w->req_minimize_l,
                    [](wl_listener* l, void* data) {
                        Server::ManagedWindow* win =
                            wl_container_of(l, win, req_minimize_l);
                        const auto* ev =
                            static_cast<const wlr_xwayland_minimize_event*>(data);
                        win->set_minimized(ev->minimize);
                    });

    attach_listener(xsurface->events.request_fullscreen, w->req_fullscreen_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win =
                            wl_container_of(l, win, req_fullscreen_l);
                        win->set_fullscreen(win->xwayland_surface->fullscreen);
                    });

    attach_listener(xsurface->events.request_activate, w->xw_req_activate_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win =
                            wl_container_of(l, win, xw_req_activate_l);
                        if (win->minimized) win->set_minimized(false);
                        win->server->focus_window(win);
                    });

    attach_listener(xsurface->events.request_close, w->xw_req_close_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win =
                            wl_container_of(l, win, xw_req_close_l);
                        win->close();
                    });

    attach_listener(xsurface->events.set_title, w->set_title_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win = wl_container_of(l, win, set_title_l);
                        win->sync_foreign_state();
                    });

    attach_listener(xsurface->events.set_class, w->set_app_id_l,
                    [](wl_listener* l, void*) {
                        Server::ManagedWindow* win = wl_container_of(l, win, set_app_id_l);
                        win->sync_foreign_state();
                    });
}

}  // namespace finn
