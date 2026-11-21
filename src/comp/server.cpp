#include "comp/internal.hpp"
#include "comp/backdrop_blur.hpp"
#include "common/paths.hpp"

#include <linux/input-event-codes.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace finn {

void setup_xdg_decoration(Server* server, wlr_xdg_toplevel_decoration_v1* dec);
void setup_xwayland_window(Server::ManagedWindow* w, wlr_xwayland_surface* xsurface);

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

bool parse_bool_str(const std::string& v) {
    return v == "1" || v == "true" || v == "TRUE" || v == "yes";
}

void write_xdisplay_file(const char* disp) {
    std::error_code ec;
    fs::create_directories(fs::path(kXwaylandDisplayFile).parent_path(), ec);
    std::ofstream out(kXwaylandDisplayFile, std::ios::trunc);
    if (out && disp && *disp) {
        out << disp << '\n';
    }
}

// finn-shell watches /run/finn/state for this file. The compositor does not
// know what a lock screen is; it only asks the shell to show one.
void request_lock() {
    std::error_code ec;
    fs::create_directories(fs::path(kLockRequestPath).parent_path(), ec);
    std::ofstream out(kLockRequestPath, std::ios::trunc);
    if (out) out << "lock\n";
}

const char* cursor_for_edges(uint32_t edges) {
    if (edges == (WLR_EDGE_TOP | WLR_EDGE_LEFT) ||
        edges == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) {
        return "nwse-resize";
    }
    if (edges == (WLR_EDGE_TOP | WLR_EDGE_RIGHT) ||
        edges == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) {
        return "nesw-resize";
    }
    if ((edges & WLR_EDGE_LEFT) || (edges & WLR_EDGE_RIGHT)) return "ew-resize";
    if ((edges & WLR_EDGE_TOP) || (edges & WLR_EDGE_BOTTOM)) return "ns-resize";
    return "left_ptr";
}

}  // namespace

Server::Server() = default;
Server::~Server() { shutdown(); }

uint64_t Server::monotonic_msec() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u +
           static_cast<uint64_t>(ts.tv_nsec / 1000000u);
}

Server::ManagedWindow* Server::find_xdg_toplevel(const wlr_xdg_toplevel* top) const {
    for (const auto& w : windows_) {
        if (!w->is_xwayland && w->xdg_toplevel == top) return w.get();
    }
    return nullptr;
}

wlr_output* Server::primary_output() const {
    return outputs_.empty() ? nullptr : outputs_.front()->output;
}

std::vector<std::string> Server::settings_paths() {
    std::vector<std::string> paths;
    const char* override_path = std::getenv("FINN_SETTINGS_PATH");
    const char* home = std::getenv("HOME");
    if ((override_path && *override_path) ||
        (home && *home && std::strcmp(home, "/root") != 0)) {
        paths.push_back(user_settings_path());
    } else {
        // finn-comp runs as root (HOME=/root); read the desktop user's settings.
        paths.push_back(std::string("/home/finn/") + kUserSettingsRel);
    }
    paths.emplace_back(kSettingsPath);
    return paths;
}

std::optional<std::string> Server::read_setting_value(std::string_view key) {
    const std::string prefix = std::string(key) + "=";
    for (const std::string& path : settings_paths()) {
        std::ifstream in(path);
        if (!in) continue;
        std::string line;
        std::optional<std::string> found;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.rfind(prefix, 0) == 0) {
                found = trim(line.substr(prefix.size()));
            }
        }
        if (found.has_value()) return found;
    }
    return std::nullopt;
}

bool Server::read_dark_setting() {
    const auto val = read_setting_value("dark");
    return val.has_value() && parse_bool_str(*val);
}

bool Server::read_reduced_motion_setting() {
    const auto val = read_setting_value("reduced_motion");
    return val.has_value() && parse_bool_str(*val);
}

int Server::read_auto_lock_minutes() {
    const auto val = read_setting_value("auto_lock_minutes");
    if (!val.has_value()) return 0;  // 0 means auto-lock is off; never invent a timeout.
    return std::clamp(std::atoi(val->c_str()), 0, 120);
}

Server::OutputConfig Server::read_output_config() {
    OutputConfig cfg;
    if (const char* mode_env = std::getenv("FINN_OUTPUT_MODE")) {
        if (std::sscanf(mode_env, "%dx%d", &cfg.width, &cfg.height) == 2 &&
            cfg.width > 0 && cfg.height > 0) {
            cfg.explicit_mode = true;
        }
    }
    if (!cfg.explicit_mode) {
        if (const char* w = std::getenv("FINN_OUTPUT_WIDTH")) cfg.width = std::atoi(w);
        if (const char* h = std::getenv("FINN_OUTPUT_HEIGHT")) cfg.height = std::atoi(h);
        if (cfg.width > 0 && cfg.height > 0) cfg.explicit_mode = true;
    }
    if (const char* s = std::getenv("FINN_OUTPUT_SCALE")) {
        cfg.scale = std::strtof(s, nullptr);
        if (cfg.scale > 0.0f) cfg.explicit_scale = true;
    }

    for (const std::string& path : settings_paths()) {
        std::ifstream in(path);
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!cfg.explicit_mode && line.rfind("resolution=", 0) == 0) {
                int w = 0, h = 0;
                if (std::sscanf(line.c_str() + 11, "%dx%d", &w, &h) == 2 &&
                    w > 0 && h > 0) {
                    cfg.width = w;
                    cfg.height = h;
                    cfg.explicit_mode = true;
                }
            } else if (!cfg.explicit_mode && line.rfind("width=", 0) == 0) {
                cfg.width = std::atoi(line.c_str() + 6);
                if (cfg.width > 0 && cfg.height > 0) cfg.explicit_mode = true;
            } else if (!cfg.explicit_mode && line.rfind("height=", 0) == 0) {
                cfg.height = std::atoi(line.c_str() + 7);
                if (cfg.width > 0 && cfg.height > 0) cfg.explicit_mode = true;
            } else if (!cfg.explicit_scale && line.rfind("scale=", 0) == 0) {
                cfg.scale = std::strtof(line.c_str() + 6, nullptr);
                if (cfg.scale > 0.0f) cfg.explicit_scale = true;
            }
        }
        if (cfg.explicit_mode && cfg.explicit_scale) break;
    }

    if (cfg.width <= 0 || cfg.height <= 0) {
        cfg.width = 1440;
        cfg.height = 900;
    }
    if (cfg.explicit_scale) {
        cfg.scale = std::clamp(cfg.scale, 0.5f, 4.0f);
    }
    return cfg;
}

void Server::apply_output_config(wlr_output* output, const OutputConfig& cfg) {
    if (!output) return;
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    wlr_output_mode* target_mode = nullptr;
    if (cfg.explicit_mode) {
        wlr_output_mode* mode = nullptr;
        wl_list_for_each(mode, &output->modes, link) {
            if (mode->width == cfg.width && mode->height == cfg.height) {
                target_mode = mode;
                break;
            }
        }
    }
    if (!target_mode && !wl_list_empty(&output->modes)) {
        // When no explicit resolution is pinned, follow the DRM preferred mode
        // so both physical monitors and VMware/VirtualBox dynamic window
        // resizes take effect natively.
        target_mode = wlr_output_preferred_mode(output);
    }

    int effective_w = cfg.width;
    if (target_mode) {
        wlr_output_state_set_mode(&state, target_mode);
        effective_w = target_mode->width;
    } else {
        wlr_output_state_set_custom_mode(&state, cfg.width, cfg.height, 60000);
    }

    const float desired_scale = cfg.explicit_scale
                                    ? cfg.scale
                                    : ((effective_w >= 2400) ? 2.0f : 1.0f);
    wlr_output_state_set_scale(&state, std::clamp(desired_scale, 0.5f, 4.0f));
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);
}

void Server::update_background() {
    dark_theme_ = read_dark_setting();
    if (!background_rect_) return;
    if (dark_theme_) {
        const float col[4] = {0.105f, 0.118f, 0.149f, 1.0f};
        wlr_scene_rect_set_color(background_rect_, col);
    } else {
        const float col[4] = {0.918f, 0.925f, 0.941f, 1.0f};
        wlr_scene_rect_set_color(background_rect_, col);
    }
    if (wlr_output* out = primary_output()) {
        int w = 0, h = 0;
        wlr_output_effective_resolution(out, &w, &h);
        wlr_scene_rect_set_size(background_rect_, std::max(1, w), std::max(1, h));
    }
    sync_all_ssd_themes();
}

void Server::sync_all_ssd_themes() {
    for (auto& w : windows_) {
        w->apply_ssd_theme(dark_theme_, focused_window_ == w.get());
    }
}

int Server::read_cursor_size_setting() {
    const auto val = read_setting_value("cursor_size");
    if (val.has_value()) {
        const int sz = std::atoi(val->c_str());
        if (sz >= 16 && sz <= 96) return sz;
    }
    return 24;
}

std::string Server::read_kb_layout_setting() {
    if (const char* env_layout = std::getenv("XKB_DEFAULT_LAYOUT");
        env_layout && *env_layout) {
        return env_layout;
    }
    const auto val = read_setting_value("kb_layout");
    return val.value_or("");
}

void Server::apply_keyboard_layout(wlr_keyboard* kb, const std::string& layout) const {
    if (!kb) return;
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) return;
    const auto variant = read_setting_value("kb_variant");
    const auto options = read_setting_value("kb_options");
    xkb_rule_names rules{};
    if (!layout.empty()) rules.layout = layout.c_str();
    if (variant.has_value() && !variant->empty()) rules.variant = variant->c_str();
    if (options.has_value() && !options->empty()) rules.options = options->c_str();

    xkb_keymap* map = xkb_keymap_new_from_names(
        ctx, (!layout.empty() || rules.variant || rules.options) ? &rules : nullptr,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!map) {
        map = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (map) {
        wlr_keyboard_set_keymap(kb, map);
        xkb_keymap_unref(map);
    }
    xkb_context_unref(ctx);
}

void Server::reload_cursor_theme() {
    if (xcursor_mgr_) {
        wlr_xcursor_manager_destroy(xcursor_mgr_);
        xcursor_mgr_ = nullptr;
    }
    const char* theme_name = dark_theme_ ? "finn-dark" : "finn-light";
    xcursor_mgr_ = wlr_xcursor_manager_create(
        theme_name, static_cast<uint32_t>(cursor_size_));
    if (!xcursor_mgr_) {
        xcursor_mgr_ = wlr_xcursor_manager_create(
            nullptr, static_cast<uint32_t>(cursor_size_));
    }
    if (xcursor_mgr_) {
        wlr_xcursor_manager_load(xcursor_mgr_, 1.0f);
    }
    if (xcursor_mgr_ && cursor_) {
        if (active_cursor_name_.empty()) active_cursor_name_ = "left_ptr";
        wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, active_cursor_name_.c_str());
    }
}

void Server::update_display_settings() {
    reduced_motion_ = read_reduced_motion_setting();
    const bool new_dark = read_dark_setting();
    const bool dark_changed = (new_dark != dark_theme_);
    if (dark_changed) {
        dark_theme_ = new_dark;
        update_background();
    }
    const int new_cursor = read_cursor_size_setting();
    const bool size_changed = (new_cursor != cursor_size_);
    if (dark_changed || size_changed || !xcursor_mgr_) {
        cursor_size_ = new_cursor;
        reload_cursor_theme();
    }
    const std::string new_layout = read_kb_layout_setting();
    if (new_layout != active_kb_layout_) {
        active_kb_layout_ = new_layout;
        for (auto& kd : keyboards_) {
            apply_keyboard_layout(kd->keyboard, active_kb_layout_);
        }
    }
    const OutputConfig cfg = read_output_config();
    for (auto& o : outputs_) {
        wlr_output_mode* desired_mode = nullptr;
        if (cfg.explicit_mode) {
            wlr_output_mode* m = nullptr;
            wl_list_for_each(m, &o->output->modes, link) {
                if (m->width == cfg.width && m->height == cfg.height) {
                    desired_mode = m;
                    break;
                }
            }
        }
        if (!desired_mode && !wl_list_empty(&o->output->modes)) {
            desired_mode = wlr_output_preferred_mode(o->output);
        }

        const int target_w = desired_mode ? desired_mode->width : cfg.width;
        const float desired_scale =
            cfg.explicit_scale ? cfg.scale : ((target_w >= 2400) ? 2.0f : 1.0f);
        const bool scale_changed = std::abs(o->output->scale - desired_scale) > 0.05f;
        const bool mode_changed =
            desired_mode &&
            (!o->output->current_mode ||
             o->output->current_mode->width != desired_mode->width ||
             o->output->current_mode->height != desired_mode->height);

        if (scale_changed || mode_changed) {
            apply_output_config(o->output, cfg);
            arrange_layers();
            update_background();
        }
    }
    if (session_locked_) {
        update_lock_backdrop();
    }
}

bool Server::is_input_locked() const {
    if (session_locked_) return true;
    for (const auto& layer : layers_) {
        if (!layer->mapped || !layer->layer_surface) continue;
        if (layer->current_layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY &&
            layer->layer_surface->current.keyboard_interactive ==
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
            return true;
        }
    }
    return false;
}

void Server::focus_lock_surface(wlr_surface* surface) {
    if (!surface || !seat_) return;
    wlr_keyboard* kb = wlr_seat_get_keyboard(seat_);
    if (kb) {
        wlr_seat_keyboard_notify_enter(seat_, surface, kb->keycodes, kb->num_keycodes,
                                       &kb->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(seat_, surface, nullptr, 0, nullptr);
    }
}

void Server::update_lock_backdrop() {
    if (!lock_tree_) return;
    if (!session_locked_) {
        if (lock_bg_rect_) {
            wlr_scene_node_destroy(&lock_bg_rect_->node);
            lock_bg_rect_ = nullptr;
        }
        return;
    }
    int w = 1440, h = 900;
    if (wlr_output* out = primary_output()) {
        wlr_output_effective_resolution(out, &w, &h);
    }
    if (!lock_bg_rect_) {
        const float col[4] = {0.043f, 0.055f, 0.078f, 1.0f};
        lock_bg_rect_ = wlr_scene_rect_create(lock_tree_, std::max(1, w), std::max(1, h), col);
        wlr_scene_node_lower_to_bottom(&lock_bg_rect_->node);
    } else {
        wlr_scene_rect_set_size(lock_bg_rect_, std::max(1, w), std::max(1, h));
    }
}

void Server::focus_window(ManagedWindow* win) {
    if (session_locked_) return;
    if (win && is_input_locked()) return;
    if (win && (!win->mapped || win->minimized)) return;

    if (focused_window_ && focused_window_ != win) {
        focused_window_->set_activated(false);
        focused_window_->sync_foreign_state();
    }
    focused_window_ = win;

    if (!win) {
        if (seat_) wlr_seat_keyboard_clear_focus(seat_);
        process_cursor_motion(monotonic_msec());
        return;
    }

    // Move to front in scene-graph & windows_ list
    windows_.splice(windows_.begin(), windows_,
                    std::find_if(windows_.begin(), windows_.end(),
                                 [win](const auto& p) { return p.get() == win; }));
    wlr_scene_node_raise_to_top(&win->scene_tree->node);

    win->set_activated(true);
    win->sync_foreign_state();

    if (win->is_xwayland && win->xwayland_surface) {
        wlr_xwayland_surface_restack(win->xwayland_surface, nullptr, XCB_STACK_MODE_ABOVE);
    }

    wlr_surface* surf = win->main_surface();
    if (surf && seat_) {
        wlr_keyboard* kb = wlr_seat_get_keyboard(seat_);
        if (kb) {
            wlr_seat_keyboard_notify_enter(seat_, surf, kb->keycodes, kb->num_keycodes,
                                           &kb->modifiers);
        } else {
            wlr_seat_keyboard_notify_enter(seat_, surf, nullptr, 0, nullptr);
        }
    }
    process_cursor_motion(monotonic_msec());
}

void Server::focus_top_window() {
    for (auto& w : windows_) {
        if (w->mapped && !w->minimized && !w->override_redirect) {
            focus_window(w.get());
            return;
        }
    }
    focus_window(nullptr);
}

void Server::cycle_focus(bool forward) {
    std::vector<ManagedWindow*> eligible;
    for (auto& w : windows_) {
        if (w->mapped && !w->override_redirect) eligible.push_back(w.get());
    }
    if (eligible.empty()) return;

    std::size_t idx = 0;
    for (std::size_t i = 0; i < eligible.size(); ++i) {
        if (eligible[i] == focused_window_) {
            idx = forward ? ((i + 1) % eligible.size())
                          : ((i + eligible.size() - 1) % eligible.size());
            break;
        }
    }
    ManagedWindow* target = eligible[idx];
    if (target->minimized) {
        target->set_minimized(false);
    } else if (target != focused_window_) {
        // Subtle macOS Alt-Tab spring lift when raising a background window.
        target->start_spring_animation(target->x, target->y + 10, 0.90, 1.0,
                                       finn::ui::springs::kSnappy, false);
    }
    focus_window(target);
}

void Server::toggle_overview() {
    if (overview_active_) {
        exit_overview(focused_window_);
        return;
    }
    std::vector<ManagedWindow*> visible;
    for (auto& w : windows_) {
        if (w->mapped && !w->minimized && !w->override_redirect && !w->fullscreen) {
            visible.push_back(w.get());
        }
    }
    if (visible.empty()) return;

    overview_active_ = true;
    const wlr_box area = usable_area_for();
    const int n = static_cast<int>(visible.size());
    const int cols = (n <= 1) ? 1 : (n <= 4 ? 2 : 3);
    const int rows = (n + cols - 1) / cols;
    const int pad = 24;
    const int cell_w = std::max(260, (area.width - pad * (cols + 1)) / cols);
    const int cell_h = std::max(200, (area.height - pad * (rows + 1)) / rows);

    for (int i = 0; i < n; ++i) {
        ManagedWindow* w = visible[static_cast<std::size_t>(i)];
        w->overview_saved_geometry = wlr_box{w->x, w->y, w->width, w->height};
        w->overview_saved_maximized = w->maximized;
        const double old_x = w->anim_timer ? w->spring_x.value() : static_cast<double>(w->x);
        const double old_y = w->anim_timer ? w->spring_y.value() : static_cast<double>(w->y);

        const int col = i % cols;
        const int row = i / cols;
        const int nx = area.x + pad + col * (cell_w + pad);
        const int ny = area.y + pad + row * (cell_h + pad);
        w->set_position(nx, ny);
        w->configure_size(cell_w, std::max(160, cell_h - w->title_offset()));
        w->start_spring_animation(old_x, old_y, w->cur_opacity, 1.0,
                                  finn::ui::springs::kSmooth, false);
    }
    process_cursor_motion(monotonic_msec());
}

void Server::exit_overview(ManagedWindow* focus_target) {
    if (!overview_active_) return;
    overview_active_ = false;

    for (auto& w : windows_) {
        if (!w->mapped || w->minimized || w->override_redirect || w->fullscreen) continue;
        const double old_x = w->anim_timer ? w->spring_x.value() : static_cast<double>(w->x);
        const double old_y = w->anim_timer ? w->spring_y.value() : static_cast<double>(w->y);
        const wlr_box& saved = w->overview_saved_geometry;
        w->set_position(saved.x, saved.y);
        w->configure_size(std::max(160, saved.width), std::max(120, saved.height));
        w->start_spring_animation(old_x, old_y, w->cur_opacity, 1.0,
                                  finn::ui::springs::kSmooth, false);
    }
    if (focus_target && focus_target->mapped && !focus_target->minimized) {
        focus_window(focus_target);
    }
    process_cursor_motion(monotonic_msec());
}

void Server::tile_all_windows() {
    std::vector<ManagedWindow*> visible;
    for (auto& w : windows_) {
        if (w->mapped && !w->minimized && !w->override_redirect && !w->fullscreen) {
            visible.push_back(w.get());
        }
    }
    if (visible.empty()) return;
    if (overview_active_) overview_active_ = false;

    const wlr_box area = usable_area_for();
    const int n = static_cast<int>(visible.size());
    const int gap = 10;
    const int cols = (n <= 1) ? 1 : (n <= 2 ? 2 : (n <= 4 ? 2 : 3));
    const int rows = (n + cols - 1) / cols;
    const int cell_w = std::max(260, (area.width - gap * (cols + 1)) / cols);
    const int cell_h = std::max(200, (area.height - gap * (rows + 1)) / rows);

    for (int i = 0; i < n; ++i) {
        ManagedWindow* w = visible[static_cast<std::size_t>(i)];
        const double old_x = w->anim_timer ? w->spring_x.value() : static_cast<double>(w->x);
        const double old_y = w->anim_timer ? w->spring_y.value() : static_cast<double>(w->y);
        w->maximized = false;
        const int col = i % cols;
        const int row = i / cols;
        const int nx = area.x + gap + col * (cell_w + gap);
        const int ny = area.y + gap + row * (cell_h + gap);
        w->set_position(nx, ny);
        w->configure_size(cell_w, std::max(160, cell_h - w->title_offset()));
        w->start_spring_animation(old_x, old_y, w->cur_opacity, 1.0,
                                  finn::ui::springs::kSmooth, false);
        w->sync_foreign_state();
    }
    process_cursor_motion(monotonic_msec());
}

void Server::minimize_all_toggle() {
    bool any_visible = false;
    for (auto& w : windows_) {
        if (w->mapped && !w->minimized && !w->override_redirect) {
            any_visible = true;
            break;
        }
    }
    all_minimized_state_ = any_visible;
    for (auto& w : windows_) {
        if (!w->mapped || w->override_redirect) continue;
        w->set_minimized(all_minimized_state_);
    }
    if (!all_minimized_state_) focus_top_window();
}

void Server::begin_move(ManagedWindow* win) {
    if (!win || !win->mapped || win->override_redirect) return;
    if (!seat_ || seat_->pointer_state.button_count == 0) return;
    win->stop_animation();
    if (win->maximized) win->set_maximized(false);
    grabbed_window_ = win;
    cursor_mode_ = CursorMode::kMove;
    grab_x_ = cursor_->x - win->x;
    grab_y_ = cursor_->y - win->y;
    active_cursor_name_ = "grabbing";
    wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, "grabbing");
}

void Server::begin_resize(ManagedWindow* win, uint32_t edges) {
    if (!win || !win->mapped || win->override_redirect) return;
    if (!seat_ || seat_->pointer_state.button_count == 0) return;
    win->stop_animation();
    if (win->maximized) win->set_maximized(false);
    grabbed_window_ = win;
    cursor_mode_ = CursorMode::kResize;
    grab_x_ = cursor_->x;
    grab_y_ = cursor_->y;
    grab_geobox_ = wlr_box{win->x, win->y, win->width, win->height};
    resize_edges_ = edges;
    active_cursor_name_ = cursor_for_edges(edges);
    wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, active_cursor_name_.c_str());
}

void Server::update_cursor_shape_or_edge(double lx, double ly) {
    if (cursor_mode_ != CursorMode::kPassthrough || !scene_ || !cursor_) return;

    double sx = 0, sy = 0;
    wlr_scene_node* node = wlr_scene_node_at(&scene_->tree.node, lx, ly, &sx, &sy);
    for (wlr_scene_tree* t = node ? node->parent : nullptr; t != nullptr;
         t = t->node.parent) {
        if (t == layer_trees_[2] || t == layer_trees_[3] || t == lock_tree_) {
            return;
        }
    }

    for (auto& w : windows_) {
        if (!w->mapped || w->minimized) continue;
        uint32_t edges = 0;
        const auto hz = w->hit_test(lx, ly, &edges);
        if (hz == ManagedWindow::HitZone::kNone) continue;

        const bool want_hover_close = (hz == ManagedWindow::HitZone::kBtnClose);
        if (w->hover_close != want_hover_close) {
            w->hover_close = want_hover_close;
            w->apply_ssd_theme(dark_theme_, focused_window_ == w.get());
        }
        if (hz == ManagedWindow::HitZone::kResize) {
            const char* cname = cursor_for_edges(edges);
            if (active_cursor_name_ != cname) {
                active_cursor_name_ = cname;
                wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, cname);
            }
            return;
        }
        if (hz != ManagedWindow::HitZone::kClient) {
            if (active_cursor_name_ != "left_ptr") {
                active_cursor_name_ = "left_ptr";
                wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, "left_ptr");
            }
            return;
        }
        break;
    }

    if (seat_ && !seat_->pointer_state.focused_surface &&
        active_cursor_name_ != "left_ptr") {
        active_cursor_name_ = "left_ptr";
        wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, "left_ptr");
    }
}

wlr_surface* Server::surface_at(double lx, double ly, double* sx, double* sy,
                                bool* in_top_or_overlay_layer,
                                bool* in_lock_layer) const {
    if (in_top_or_overlay_layer) *in_top_or_overlay_layer = false;
    if (in_lock_layer) *in_lock_layer = false;
    if (!scene_) return nullptr;

    double local_sx = 0.0, local_sy = 0.0;
    wlr_scene_node* node =
        wlr_scene_node_at(&scene_->tree.node, lx, ly, &local_sx, &local_sy);
    if (!node) return nullptr;

    bool hit_lock = false;
    bool hit_top_overlay = false;
    for (wlr_scene_tree* t = node->parent; t != nullptr; t = t->node.parent) {
        if (t == lock_tree_) {
            hit_lock = true;
            hit_top_overlay = true;
            break;
        }
        if (t == layer_trees_[2] || t == layer_trees_[3]) {
            hit_top_overlay = true;
            break;
        }
    }

    if (session_locked_ && !hit_lock) {
        return nullptr;
    }

    wlr_surface* surface = nullptr;
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        wlr_scene_buffer* scene_buf = wlr_scene_buffer_from_node(node);
        wlr_scene_surface* scene_surf = wlr_scene_surface_try_from_buffer(scene_buf);
        if (scene_surf) surface = scene_surf->surface;
    }
    if (sx) *sx = local_sx;
    if (sy) *sy = local_sy;
    if (in_top_or_overlay_layer) *in_top_or_overlay_layer = hit_top_overlay;
    if (in_lock_layer) *in_lock_layer = hit_lock;
    return surface;
}

void Server::process_cursor_motion(uint32_t time_msec) {
    if (!scene_ || !cursor_ || !seat_) return;
    last_activity_msec_ = monotonic_msec();
    if (idle_notifier_) wlr_idle_notifier_v1_notify_activity(idle_notifier_, seat_);

    if (cursor_mode_ != CursorMode::kPassthrough &&
        (seat_->pointer_state.button_count == 0 || session_locked_)) {
        cursor_mode_ = CursorMode::kPassthrough;
        grabbed_window_ = nullptr;
        active_cursor_name_ = "left_ptr";
        wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, "left_ptr");
    }

    if (!session_locked_ && cursor_mode_ == CursorMode::kMove && grabbed_window_) {
        grabbed_window_->set_position(static_cast<int>(cursor_->x - grab_x_),
                                      static_cast<int>(cursor_->y - grab_y_));
        return;
    }
    if (!session_locked_ && cursor_mode_ == CursorMode::kResize && grabbed_window_) {
        const int dx = static_cast<int>(cursor_->x - grab_x_);
        const int dy = static_cast<int>(cursor_->y - grab_y_);
        int nx = grab_geobox_.x;
        int ny = grab_geobox_.y;
        int nw = grab_geobox_.width;
        int nh = grab_geobox_.height;

        if (resize_edges_ & WLR_EDGE_LEFT) {
            nw = std::max(240, grab_geobox_.width - dx);
            nx = grab_geobox_.x + (grab_geobox_.width - nw);
        } else if (resize_edges_ & WLR_EDGE_RIGHT) {
            nw = std::max(240, grab_geobox_.width + dx);
        }
        if (resize_edges_ & WLR_EDGE_TOP) {
            nh = std::max(140, grab_geobox_.height - dy);
            ny = grab_geobox_.y + (grab_geobox_.height - nh);
        } else if (resize_edges_ & WLR_EDGE_BOTTOM) {
            nh = std::max(140, grab_geobox_.height + dy);
        }
        grabbed_window_->set_position(nx, ny);
        grabbed_window_->configure_size(nw, nh);
        return;
    }

    double sx = 0, sy = 0;
    wlr_surface* surface = surface_at(cursor_->x, cursor_->y, &sx, &sy, nullptr, nullptr);

    if (surface) {
        wlr_seat_pointer_notify_enter(seat_, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat_);
    }
    if (!session_locked_) {
        update_cursor_shape_or_edge(cursor_->x, cursor_->y);
    }
}

void Server::on_new_output(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_output_);
    auto* wlr_out = static_cast<wlr_output*>(data);
    wlr_output_init_render(wlr_out, server->allocator_, server->renderer_);

    const OutputConfig cfg = read_output_config();
    server->apply_output_config(wlr_out, cfg);

    auto dev = std::make_unique<OutputDevice>();
    auto* d = dev.get();
    d->server = server;
    d->output = wlr_out;

    wlr_output_layout_output* l_out =
        wlr_output_layout_add_auto(server->output_layout_, wlr_out);
    d->scene_output = wlr_scene_output_create(server->scene_, wlr_out);
    wlr_scene_output_layout_add_output(server->scene_layout_, l_out, d->scene_output);

    server->outputs_.push_back(std::move(dev));

    attach_listener(wlr_out->events.frame, d->frame_l,
                    [](wl_listener* l, void*) {
                        OutputDevice* od = wl_container_of(l, od, frame_l);
                        od->server->commit_output_with_backdrop_blur(od);
                        timespec now{};
                        clock_gettime(CLOCK_MONOTONIC, &now);
                        wlr_scene_output_send_frame_done(od->scene_output, &now);
                    });

    attach_listener(wlr_out->events.request_state, d->request_state_l,
                    [](wl_listener* l, void* edata) {
                        OutputDevice* od = wl_container_of(l, od, request_state_l);
                        const auto* ev =
                            static_cast<const wlr_output_event_request_state*>(edata);
                        wlr_output_commit_state(od->output, ev->state);
                        od->server->arrange_layers();
                        od->server->update_background();
                    });

    attach_listener(wlr_out->events.destroy, d->destroy_l,
                    [](wl_listener* l, void*) {
                        OutputDevice* od = wl_container_of(l, od, destroy_l);
                        Server* s = od->server;
                        s->outputs_.remove_if(
                            [od](const auto& p) { return p.get() == od; });
                    });

    server->arrange_layers();
    server->update_background();
    if (server->cursor_ && server->xcursor_mgr_) {
        wlr_cursor_set_xcursor(server->cursor_, server->xcursor_mgr_,
                               server->active_cursor_name_.c_str());
    }
}

void Server::on_new_input(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_input_);
    auto* device = static_cast<wlr_input_device*>(data);

    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        wlr_keyboard* kb = wlr_keyboard_from_input_device(device);
        server->active_kb_layout_ = read_kb_layout_setting();
        server->apply_keyboard_layout(kb, server->active_kb_layout_);
        wlr_keyboard_set_repeat_info(kb, 28, 350);

        auto kdev = std::make_unique<KeyboardDevice>();
        auto* kd = kdev.get();
        kd->server = server;
        kd->keyboard = kb;
        server->keyboards_.push_back(std::move(kdev));
        wlr_seat_set_keyboard(server->seat_, kb);

        attach_listener(kb->events.modifiers, kd->modifiers_l,
                        [](wl_listener* l, void*) {
                            KeyboardDevice* k = wl_container_of(l, k, modifiers_l);
                            wlr_seat_set_keyboard(k->server->seat_, k->keyboard);
                            wlr_seat_keyboard_notify_modifiers(k->server->seat_,
                                                               &k->keyboard->modifiers);
                        });

        attach_listener(kb->events.key, kd->key_l,
                        [](wl_listener* l, void* edata) {
                            KeyboardDevice* k = wl_container_of(l, k, key_l);
                            const auto* ev = static_cast<const wlr_keyboard_key_event*>(edata);
                            Server* s = k->server;
                            wlr_idle_notifier_v1_notify_activity(s->idle_notifier_, s->seat_);
                            s->last_activity_msec_ = monotonic_msec();

                            const uint32_t mods = wlr_keyboard_get_modifiers(k->keyboard);
                            const xkb_keycode_t keycode = ev->keycode + 8;
                            const xkb_keysym_t* syms = nullptr;
                            const int nsyms = xkb_state_key_get_syms(
                                k->keyboard->xkb_state, keycode, &syms);

                            const bool input_locked = s->is_input_locked();
                            bool handled = false;
                            if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                                for (int i = 0; i < nsyms; ++i) {
                                    const xkb_keysym_t sym = syms[i];
                                    if ((mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) ==
                                            (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT) &&
                                        sym >= XKB_KEY_XF86Switch_VT_1 &&
                                        sym <= XKB_KEY_XF86Switch_VT_12) {
                                        if (s->wlr_session_) {
                                            wlr_session_change_vt(
                                                s->wlr_session_,
                                                sym - XKB_KEY_XF86Switch_VT_1 + 1);
                                        }
                                        handled = true;
                                        break;
                                    }
                                    if (input_locked) {
                                        continue;
                                    }
                                    if (s->overview_active() && sym == XKB_KEY_Escape) {
                                        s->exit_overview(s->focused_window_);
                                        handled = true;
                                        break;
                                    }
                                    if (sym == XKB_KEY_F3) {
                                        s->toggle_overview();
                                        handled = true;
                                        break;
                                    }
                                    if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_Tab) {
                                        s->cycle_focus(!(mods & WLR_MODIFIER_SHIFT));
                                        handled = true;
                                        break;
                                    }
                                    if ((mods & WLR_MODIFIER_LOGO) && sym == XKB_KEY_Tab) {
                                        s->toggle_overview();
                                        handled = true;
                                        break;
                                    }
                                    if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_F4) {
                                        if (s->focused_window_) s->focused_window_->close();
                                        handled = true;
                                        break;
                                    }
                                    if (mods & WLR_MODIFIER_LOGO) {
                                        if (sym == XKB_KEY_q || sym == XKB_KEY_Q) {
                                            if (s->focused_window_) s->focused_window_->close();
                                            handled = true;
                                            break;
                                        }
                                        if (sym == XKB_KEY_l || sym == XKB_KEY_L) {
                                            request_lock();
                                            handled = true;
                                            break;
                                        }
                                        if (sym == XKB_KEY_Left && s->focused_window_) {
                                            s->focused_window_->snap_half(true);
                                            handled = true;
                                            break;
                                        }
                                        if (sym == XKB_KEY_Right && s->focused_window_) {
                                            s->focused_window_->snap_half(false);
                                            handled = true;
                                            break;
                                        }
                                        if ((sym == XKB_KEY_Up || sym == XKB_KEY_m) &&
                                            s->focused_window_) {
                                            s->focused_window_->set_maximized(
                                                !s->focused_window_->maximized);
                                            handled = true;
                                            break;
                                        }
                                        if (sym == XKB_KEY_Down && s->focused_window_) {
                                            if (s->focused_window_->maximized)
                                                s->focused_window_->set_maximized(false);
                                            else
                                                s->focused_window_->set_minimized(true);
                                            handled = true;
                                            break;
                                        }
                                        if (sym == XKB_KEY_d || sym == XKB_KEY_D) {
                                            s->minimize_all_toggle();
                                            handled = true;
                                            break;
                                        }
                                        if (sym == XKB_KEY_t || sym == XKB_KEY_T) {
                                            s->tile_all_windows();
                                            handled = true;
                                            break;
                                        }
                                    }
                                    if (const char* allow = std::getenv("FINN_ALLOW_QUIT");
                                        allow && *allow == '1' &&
                                        (mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_Escape) {
                                        wl_display_terminate(s->display_);
                                        handled = true;
                                        break;
                                    }
                                }
                            }
                            if (!handled) {
                                wlr_seat_set_keyboard(s->seat_, k->keyboard);
                                wlr_seat_keyboard_notify_key(s->seat_, ev->time_msec,
                                                             ev->keycode, ev->state);
                            }
                        });

        attach_listener(device->events.destroy, kd->destroy_l,
                        [](wl_listener* l, void*) {
                            KeyboardDevice* k = wl_container_of(l, k, destroy_l);
                            Server* s = k->server;
                            s->keyboards_.remove_if(
                                [k](const auto& p) { return p.get() == k; });
                        });
    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(server->cursor_, device);
        auto pdev = std::make_unique<PointerDevice>();
        auto* pd = pdev.get();
        pd->server = server;
        pd->pointer = wlr_pointer_from_input_device(device);
        server->pointers_.push_back(std::move(pdev));
        attach_listener(device->events.destroy, pd->destroy_l,
                        [](wl_listener* l, void*) {
                            PointerDevice* p = wl_container_of(l, p, destroy_l);
                            Server* s = p->server;
                            s->pointers_.remove_if(
                                [p](const auto& x) { return x.get() == p; });
                        });
    } else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
        wlr_cursor_attach_input_device(server->cursor_, device);
        auto tdev = std::make_unique<TouchDevice>();
        auto* td = tdev.get();
        td->server = server;
        td->touch = wlr_touch_from_input_device(device);
        server->touches_.push_back(std::move(tdev));
        attach_listener(device->events.destroy, td->destroy_l,
                        [](wl_listener* l, void*) {
                            TouchDevice* t = wl_container_of(l, t, destroy_l);
                            Server* s = t->server;
                            s->touches_.remove_if(
                                [t](const auto& x) { return x.get() == t; });
                        });
    } else if (device->type == WLR_INPUT_DEVICE_TABLET) {
        wlr_cursor_attach_input_device(server->cursor_, device);
        auto tabdev = std::make_unique<TabletDevice>();
        auto* td = tabdev.get();
        td->server = server;
        td->tablet = wlr_tablet_from_input_device(device);
        server->tablets_.push_back(std::move(tabdev));
        attach_listener(device->events.destroy, td->destroy_l,
                        [](wl_listener* l, void*) {
                            TabletDevice* t = wl_container_of(l, t, destroy_l);
                            Server* s = t->server;
                            s->tablets_.remove_if(
                                [t](const auto& x) { return x.get() == t; });
                        });
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!server->keyboards_.empty()) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (!server->touches_.empty()) caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server->seat_, caps);
}

void Server::on_new_xdg_toplevel(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_xdg_toplevel_);
    auto* xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    // setup_xdg_toplevel appends ManagedWindow inside this scope manually
    auto node = std::make_unique<ManagedWindow>(server);
    auto* w = node.get();
    server->windows_.push_back(std::move(node));

    w->is_xwayland = false;
    w->xdg_toplevel = xdg_toplevel;
    w->scene_tree = wlr_scene_tree_create(server->toplevel_tree_);
    w->surface_tree = wlr_scene_xdg_surface_create(w->scene_tree, xdg_toplevel->base);
    w->scene_tree->node.data = w;
    xdg_toplevel->base->data = w->surface_tree;

    const wlr_box area = server->usable_area_for();
    w->x = area.x + 36;
    w->y = area.y + 32;
    w->set_position(w->x, w->y);
    wlr_scene_node_set_enabled(&w->scene_tree->node, false);

    attach_listener(xdg_toplevel->base->surface->events.map, w->map_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, map_l);
                        w->mapped = true;
                        w->update_size_from_surface();
                        const wlr_box area = w->server->usable_area_for();
                        int mapped_others = 0;
                        for (const auto& other : w->server->windows_) {
                            if (other.get() != w && other->mapped && !other->minimized) {
                                ++mapped_others;
                            }
                        }
                        const int offset_x = (mapped_others % 5) * 26;
                        const int offset_y = (mapped_others % 5) * 22;
                        w->x = area.x + std::max(16, (area.width - w->width) / 2) + offset_x;
                        w->y = area.y +
                               std::max(16, (area.height - w->height - w->title_offset()) / 2) +
                               offset_y;
                        if (w->x + w->width > area.x + area.width) {
                            w->x = std::max(area.x + 12, area.x + area.width - w->width - 16);
                        }
                        if (w->y + w->height + w->title_offset() > area.y + area.height) {
                            w->y = std::max(area.y + 12,
                                            area.y + area.height - w->height -
                                                w->title_offset() - 16);
                        }
                        w->set_position(w->x, w->y);
                        wlr_scene_node_set_enabled(&w->scene_tree->node, !w->minimized);
                        w->update_ssd_layout();
                        w->ensure_foreign_handle();
                        w->sync_foreign_state();
                        if (!w->minimized) {
                            w->start_spring_animation(w->x, w->y + 24, 0.0, 1.0,
                                                      finn::ui::springs::kSmooth, false);
                        }
                        w->server->focus_window(w);
                        w->server->process_cursor_motion(monotonic_msec());
                    });

    attach_listener(xdg_toplevel->base->surface->events.unmap, w->unmap_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, unmap_l);
                        w->stop_animation();
                        w->mapped = false;
                        wlr_scene_node_set_enabled(&w->scene_tree->node, false);
                        if (w->server->focused_window() == w) w->server->focus_top_window();
                        w->server->process_cursor_motion(monotonic_msec());
                    });

    attach_listener(xdg_toplevel->base->surface->events.commit, w->commit_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, commit_l);
                        if (w->xdg_toplevel->base->initial_commit) {
                            // The xdg surface is not initialized yet; a
                            // configure here would trip a wlroots assertion.
                            if (w->xdg_ready()) {
                                wlr_xdg_toplevel_set_size(w->xdg_toplevel, 0, 0);
                            }
                            return;
                        }
                        // The client has completed its first commit, so the
                        // surface is initialized: replay any size/maximize/
                        // fullscreen state requested before that point.
                        w->apply_pending_xdg_state();
                        if (w->mapped) w->update_size_from_surface();
                    });

    attach_listener(xdg_toplevel->events.request_move, w->req_move_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, req_move_l);
                        w->server->begin_move(w);
                    });

    attach_listener(xdg_toplevel->events.request_resize, w->req_resize_l,
                    [](wl_listener* l, void* edata) {
                        ManagedWindow* w = wl_container_of(l, w, req_resize_l);
                        const auto* ev = static_cast<const wlr_xdg_toplevel_resize_event*>(edata);
                        w->server->begin_resize(w, ev->edges);
                    });

    attach_listener(xdg_toplevel->events.request_maximize, w->req_maximize_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, req_maximize_l);
                        w->set_maximized(w->xdg_toplevel->requested.maximized);
                    });

    attach_listener(xdg_toplevel->events.request_minimize, w->req_minimize_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, req_minimize_l);
                        w->set_minimized(true);
                    });

    attach_listener(xdg_toplevel->events.request_fullscreen, w->req_fullscreen_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, req_fullscreen_l);
                        w->set_fullscreen(w->xdg_toplevel->requested.fullscreen);
                    });

    attach_listener(xdg_toplevel->events.set_title, w->set_title_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, set_title_l);
                        w->sync_foreign_state();
                    });

    attach_listener(xdg_toplevel->events.set_app_id, w->set_app_id_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, set_app_id_l);
                        w->sync_foreign_state();
                    });

    attach_listener(xdg_toplevel->events.destroy, w->destroy_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, destroy_l);
                        Server* s = w->server;
                        if (s->grabbed_window_ == w) {
                            s->grabbed_window_ = nullptr;
                            s->cursor_mode_ = CursorMode::kPassthrough;
                        }
                        if (s->focused_window_ == w) s->focused_window_ = nullptr;
                        s->windows_.remove_if(
                            [w](const auto& p) { return p.get() == w; });
                        if (!s->focused_window_) s->focus_top_window();
                    });
}

void Server::on_new_xdg_popup(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_xdg_popup_);
    auto* popup = static_cast<wlr_xdg_popup*>(data);
    if (!popup->parent) return;  // Layer-shell popups arrive via layer->events.new_popup
    wlr_scene_tree* ptree = server->parent_tree_for_xdg(popup->parent);
    if (!ptree) ptree = server->layer_trees_[2];
    server->create_popup(popup, ptree);
}

void Server::on_new_xdg_decoration(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_xdg_decoration_);
    setup_xdg_decoration(server, static_cast<wlr_xdg_toplevel_decoration_v1*>(data));
}

void Server::on_xwayland_ready(wl_listener* listener, void*) {
    Server* server = wl_container_of(listener, server, xwayland_ready_);
    if (!server->xwayland_) return;
    wlr_xwayland_set_seat(server->xwayland_, server->seat_);
    write_xdisplay_file(server->xwayland_->display_name);
    wlr_log(WLR_INFO, "finn-comp: Xwayland ready on DISPLAY=%s",
            server->xwayland_->display_name ? server->xwayland_->display_name : "");
}

void Server::on_xwayland_new_surface(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, xwayland_new_surface_);
    auto* xsurface = static_cast<wlr_xwayland_surface*>(data);
    auto node = std::make_unique<ManagedWindow>(server);
    ManagedWindow* w = node.get();
    server->windows_.push_back(std::move(node));
    setup_xwayland_window(w, xsurface);

    attach_listener(xsurface->events.destroy, w->destroy_l,
                    [](wl_listener* l, void*) {
                        ManagedWindow* w = wl_container_of(l, w, destroy_l);
                        Server* s = w->server;
                        if (s->grabbed_window_ == w) {
                            s->grabbed_window_ = nullptr;
                            s->cursor_mode_ = CursorMode::kPassthrough;
                        }
                        if (s->focused_window_ == w) s->focused_window_ = nullptr;
                        if (w->scene_tree) {
                            wlr_scene_node_destroy(&w->scene_tree->node);
                            w->scene_tree = nullptr;
                            w->surface_tree = nullptr;
                        }
                        s->windows_.remove_if(
                            [w](const auto& p) { return p.get() == w; });
                        if (!s->focused_window_) s->focus_top_window();
                    });
}

void Server::on_request_cursor(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, request_cursor_);
    const auto* ev = static_cast<const wlr_seat_pointer_request_set_cursor_event*>(data);
    if (server->seat_->pointer_state.focused_client == ev->seat_client &&
        server->cursor_mode_ == CursorMode::kPassthrough) {
        wlr_cursor_set_surface(server->cursor_, ev->surface, ev->hotspot_x, ev->hotspot_y);
    }
}

void Server::on_request_set_shape(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, request_set_shape_);
    const auto* ev =
        static_cast<const wlr_cursor_shape_manager_v1_request_set_shape_event*>(data);
    if (server->seat_->pointer_state.focused_client == ev->seat_client &&
        server->cursor_mode_ == CursorMode::kPassthrough) {
        const char* cname = wlr_cursor_shape_v1_name(ev->shape);
        if (cname && *cname) {
            server->active_cursor_name_ = cname;
            wlr_cursor_set_xcursor(server->cursor_, server->xcursor_mgr_, cname);
        }
    }
}

void Server::on_request_set_selection(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, request_set_selection_);
    const auto* ev = static_cast<const wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat_, ev->source, ev->serial);
}

void Server::on_request_set_primary_selection(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, request_set_primary_selection_);
    const auto* ev =
        static_cast<const wlr_seat_request_set_primary_selection_event*>(data);
    wlr_seat_set_primary_selection(server->seat_, ev->source, ev->serial);
}

void Server::on_request_activate(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, request_activate_);
    if (server->is_input_locked()) return;
    const auto* ev =
        static_cast<const wlr_xdg_activation_v1_request_activate_event*>(data);
    if (!ev->surface) return;
    for (auto& w : server->windows_) {
        if (w->main_surface() == ev->surface) {
            if (w->minimized) w->set_minimized(false);
            server->focus_window(w.get());
            break;
        }
    }
}

void Server::on_cursor_motion(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_motion_);
    const auto* ev = static_cast<const wlr_pointer_motion_event*>(data);
    wlr_cursor_move(server->cursor_, &ev->pointer->base, ev->delta_x, ev->delta_y);
    server->process_cursor_motion(ev->time_msec);
}

void Server::on_cursor_motion_absolute(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_motion_absolute_);
    const auto* ev = static_cast<const wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(server->cursor_, &ev->pointer->base, ev->x, ev->y);
    server->process_cursor_motion(ev->time_msec);
}

void Server::handle_pointer_button(uint32_t time_msec, uint32_t button,
                                   wl_pointer_button_state state) {
    if (!scene_ || !cursor_ || !seat_) return;
    last_activity_msec_ = monotonic_msec();
    if (idle_notifier_) {
        wlr_idle_notifier_v1_notify_activity(idle_notifier_, seat_);
    }

    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (cursor_mode_ != CursorMode::kPassthrough) {
            cursor_mode_ = CursorMode::kPassthrough;
            grabbed_window_ = nullptr;
            active_cursor_name_ = "left_ptr";
            wlr_cursor_set_xcursor(cursor_, xcursor_mgr_, "left_ptr");
            process_cursor_motion(time_msec);
            return;
        }
        wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
        return;
    }

    double sx = 0, sy = 0;
    bool hit_top_or_overlay_layer = false;
    bool hit_lock_layer = false;
    wlr_surface* hit_surface = surface_at(cursor_->x, cursor_->y, &sx, &sy,
                                          &hit_top_or_overlay_layer, &hit_lock_layer);

    if (session_locked_) {
        if (hit_lock_layer && hit_surface) {
            wlr_seat_pointer_notify_enter(seat_, hit_surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
            wlr_surface* root_surf = wlr_surface_get_root_surface(hit_surface);
            focus_lock_surface(root_surf ? root_surf : hit_surface);
            wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
        }
        return;
    }

    if (hit_top_or_overlay_layer) {
        if (hit_surface) {
            wlr_seat_pointer_notify_enter(seat_, hit_surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
            wlr_surface* root_surf = wlr_surface_get_root_surface(hit_surface);
            if (wlr_layer_surface_v1* lsurf =
                    root_surf ? wlr_layer_surface_v1_try_from_wlr_surface(root_surf)
                              : nullptr) {
                if (lsurf->current.keyboard_interactive !=
                    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
                    focus_lock_surface(lsurf->surface);
                }
            }
        }
        wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
        return;
    }

    wlr_keyboard* kb = wlr_seat_get_keyboard(seat_);
    const uint32_t mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;
    bool hit_any_window = false;

    for (auto& w : windows_) {
        if (!w->mapped || w->minimized) continue;
        uint32_t edges = 0;
        const auto hz = w->hit_test(cursor_->x, cursor_->y, &edges);
        if (hz == ManagedWindow::HitZone::kNone) continue;

        hit_any_window = true;
        if (overview_active_) {
            exit_overview(w.get());
            return;
        }
        focus_window(w.get());

        if (hz == ManagedWindow::HitZone::kClient) {
            break;
        }

        if ((mods & WLR_MODIFIER_ALT) && button == BTN_LEFT) {
            wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
            begin_move(w.get());
            return;
        }
        if ((mods & WLR_MODIFIER_ALT) && button == BTN_RIGHT) {
            wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
            begin_resize(w.get(), WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
            return;
        }
        if (button == BTN_LEFT) {
            if (hz == ManagedWindow::HitZone::kBtnClose) {
                w->close();
                return;
            }
            if (hz == ManagedWindow::HitZone::kBtnMax) {
                w->set_maximized(!w->maximized);
                return;
            }
            if (hz == ManagedWindow::HitZone::kBtnMin) {
                w->set_minimized(true);
                return;
            }
            if (hz == ManagedWindow::HitZone::kMoveBar) {
                wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
                begin_move(w.get());
                return;
            }
            if (hz == ManagedWindow::HitZone::kResize) {
                wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
                begin_resize(w.get(), edges);
                return;
            }
        }
        break;
    }

    if (!hit_any_window) {
        wlr_scene_node* node = wlr_scene_node_at(
            &scene_->tree.node, cursor_->x, cursor_->y, &sx, &sy);
        if (node && node->type == WLR_SCENE_NODE_BUFFER) {
            wlr_scene_node* cur = node;
            while (cur && !cur->data) cur = cur->parent ? &cur->parent->node : nullptr;
            if (cur && cur->data) {
                auto* candidate = static_cast<ManagedWindow*>(cur->data);
                for (auto& w : windows_) {
                    if (w.get() == candidate) {
                        hit_any_window = true;
                        focus_window(candidate);
                        break;
                    }
                }
            }
        }
    }

    if (!hit_any_window) {
        focus_window(nullptr);
    }

    hit_surface = surface_at(cursor_->x, cursor_->y, &sx, &sy, nullptr, nullptr);
    if (hit_surface) {
        wlr_seat_pointer_notify_enter(seat_, hit_surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat_);
    }

    if (hit_any_window && (mods & WLR_MODIFIER_ALT) && button == BTN_LEFT &&
        focused_window_) {
        wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
        begin_move(focused_window_);
        return;
    }
    if (hit_any_window && (mods & WLR_MODIFIER_ALT) && button == BTN_RIGHT &&
        focused_window_) {
        wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
        begin_resize(focused_window_, WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        return;
    }

    wlr_seat_pointer_notify_button(seat_, time_msec, button, state);
}

void Server::on_cursor_button(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_button_);
    const auto* ev = static_cast<const wlr_pointer_button_event*>(data);
    server->handle_pointer_button(ev->time_msec, ev->button, ev->state);
}

void Server::on_cursor_axis(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_axis_);
    const auto* ev = static_cast<const wlr_pointer_axis_event*>(data);
    server->last_activity_msec_ = monotonic_msec();
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier_, server->seat_);
    wlr_seat_pointer_notify_axis(server->seat_, ev->time_msec, ev->orientation,
                                 ev->delta, ev->delta_discrete, ev->source,
                                 ev->relative_direction);
}

void Server::on_cursor_frame(wl_listener* listener, void*) {
    Server* server = wl_container_of(listener, server, cursor_frame_);
    wlr_seat_pointer_notify_frame(server->seat_);
}

void Server::on_cursor_touch_down(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_touch_down_);
    const auto* ev = static_cast<const wlr_touch_down_event*>(data);
    if (!server->cursor_ || !server->seat_) return;

    server->last_activity_msec_ = monotonic_msec();
    if (server->idle_notifier_) {
        wlr_idle_notifier_v1_notify_activity(server->idle_notifier_, server->seat_);
    }

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(server->cursor_, &ev->touch->base,
                                         ev->x, ev->y, &lx, &ly);

    double sx = 0.0, sy = 0.0;
    bool hit_top_overlay = false;
    bool hit_lock = false;
    wlr_surface* hit_surface = server->surface_at(
        lx, ly, &sx, &sy, &hit_top_overlay, &hit_lock);

    if (!server->session_locked_ && !hit_top_overlay) {
        for (auto& w : server->windows_) {
            if (!w->mapped || w->minimized) continue;
            if (w->hit_test(lx, ly) != ManagedWindow::HitZone::kNone) {
                server->focus_window(w.get());
                hit_surface = server->surface_at(
                    lx, ly, &sx, &sy, &hit_top_overlay, &hit_lock);
                break;
            }
        }
    } else if (hit_top_overlay && hit_surface) {
        wlr_surface* root_surf = wlr_surface_get_root_surface(hit_surface);
        if (server->session_locked_ && hit_lock) {
            server->focus_lock_surface(root_surf ? root_surf : hit_surface);
        } else if (wlr_layer_surface_v1* lsurf =
                       root_surf ? wlr_layer_surface_v1_try_from_wlr_surface(root_surf)
                                 : nullptr) {
            if (lsurf->current.keyboard_interactive !=
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
                server->focus_lock_surface(lsurf->surface);
            }
        }
    }

    int out_w = 1440, out_h = 900;
    if (wlr_output* out = server->primary_output()) {
        wlr_output_effective_resolution(out, &out_w, &out_h);
    }
    constexpr double kEdgeMargin = 24.0;
    const bool near_edge =
        (ly >= out_h - kEdgeMargin || lx <= kEdgeMargin || lx >= out_w - kEdgeMargin);

    ActiveTouchPoint pt;
    pt.touch_id = ev->touch_id;
    pt.start_lx = lx;
    pt.start_ly = ly;
    pt.cur_lx = lx;
    pt.cur_ly = ly;
    pt.edge_candidate =
        (ev->touch_id == 0 && !server->is_input_locked() && !hit_top_overlay && near_edge);

    if (hit_surface) {
        wlr_seat_touch_notify_down(server->seat_, hit_surface, ev->time_msec,
                                   ev->touch_id, sx, sy);
        pt.surf_origin_lx = lx - sx;
        pt.surf_origin_ly = ly - sy;
        pt.has_surface = true;
    }

    server->active_touches_.erase(
        std::remove_if(server->active_touches_.begin(), server->active_touches_.end(),
                       [&](const ActiveTouchPoint& p) { return p.touch_id == ev->touch_id; }),
        server->active_touches_.end());
    server->active_touches_.push_back(pt);
}

void Server::on_cursor_touch_motion(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_touch_motion_);
    const auto* ev = static_cast<const wlr_touch_motion_event*>(data);
    if (!server->cursor_ || !server->seat_) return;

    server->last_activity_msec_ = monotonic_msec();
    if (server->idle_notifier_) {
        wlr_idle_notifier_v1_notify_activity(server->idle_notifier_, server->seat_);
    }

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(server->cursor_, &ev->touch->base,
                                         ev->x, ev->y, &lx, &ly);

    ActiveTouchPoint* active = nullptr;
    for (auto& pt : server->active_touches_) {
        if (pt.touch_id == ev->touch_id) {
            active = &pt;
            break;
        }
    }
    if (!active) return;

    active->cur_lx = lx;
    active->cur_ly = ly;

    if (active->edge_candidate && !active->gesture_triggered && !server->is_input_locked()) {
        int out_w = 1440, out_h = 900;
        if (wlr_output* out = server->primary_output()) {
            wlr_output_effective_resolution(out, &out_w, &out_h);
        }
        const double dx = lx - active->start_lx;
        const double dy = ly - active->start_ly;
        constexpr double kEdgeMargin = 24.0;
        enum class EdgeAction { kNone, kToggleDesktop, kCycleNext, kCyclePrev };
        EdgeAction action = EdgeAction::kNone;

        if (active->start_ly >= out_h - kEdgeMargin && dy <= -56.0) {
            action = EdgeAction::kToggleDesktop;
        } else if (active->start_lx <= kEdgeMargin && dx >= 64.0) {
            action = EdgeAction::kCycleNext;
        } else if (active->start_lx >= out_w - kEdgeMargin && dx <= -64.0) {
            action = EdgeAction::kCyclePrev;
        }

        if (action != EdgeAction::kNone) {
            active->gesture_triggered = true;
            if (wlr_touch_point* wpt = wlr_seat_touch_get_point(server->seat_, ev->touch_id);
                wpt && wpt->client) {
                wlr_seat_touch_notify_cancel(server->seat_, wpt->client);
            }
            if (action == EdgeAction::kToggleDesktop) server->minimize_all_toggle();
            else server->cycle_focus(action == EdgeAction::kCycleNext);
            return;
        }
    }

    if (!active->gesture_triggered && active->has_surface &&
        wlr_seat_touch_get_point(server->seat_, ev->touch_id)) {
        wlr_seat_touch_notify_motion(server->seat_, ev->time_msec, ev->touch_id,
                                     lx - active->surf_origin_lx,
                                     ly - active->surf_origin_ly);
    }
}

void Server::on_cursor_touch_up(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_touch_up_);
    const auto* ev = static_cast<const wlr_touch_up_event*>(data);
    if (!server->seat_) return;

    server->active_touches_.erase(
        std::remove_if(server->active_touches_.begin(), server->active_touches_.end(),
                       [&](const ActiveTouchPoint& p) { return p.touch_id == ev->touch_id; }),
        server->active_touches_.end());

    if (wlr_seat_touch_get_point(server->seat_, ev->touch_id)) {
        wlr_seat_touch_notify_up(server->seat_, ev->time_msec, ev->touch_id);
    }
}

void Server::on_cursor_touch_frame(wl_listener* listener, void*) {
    Server* server = wl_container_of(listener, server, cursor_touch_frame_);
    if (server->seat_) wlr_seat_touch_notify_frame(server->seat_);
}

void Server::on_cursor_touch_cancel(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_touch_cancel_);
    const auto* ev = static_cast<const wlr_touch_cancel_event*>(data);
    if (!server->seat_) return;

    server->active_touches_.erase(
        std::remove_if(server->active_touches_.begin(), server->active_touches_.end(),
                       [&](const ActiveTouchPoint& p) { return p.touch_id == ev->touch_id; }),
        server->active_touches_.end());

    if (wlr_touch_point* pt = wlr_seat_touch_get_point(server->seat_, ev->touch_id);
        pt && pt->client) {
        wlr_seat_touch_notify_cancel(server->seat_, pt->client);
    }
}

void Server::on_cursor_tablet_proximity(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_tablet_proximity_);
    const auto* ev = static_cast<const wlr_tablet_tool_proximity_event*>(data);
    if (!server->cursor_) return;
    if (ev->state == WLR_TABLET_TOOL_PROXIMITY_IN) {
        wlr_cursor_warp_absolute(server->cursor_, &ev->tablet->base, ev->x, ev->y);
        server->process_cursor_motion(ev->time_msec);
    }
}

void Server::on_cursor_tablet_axis(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_tablet_axis_);
    const auto* ev = static_cast<const wlr_tablet_tool_axis_event*>(data);
    if (!server->cursor_) return;
    if ((ev->updated_axes & WLR_TABLET_TOOL_AXIS_X) ||
        (ev->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
        wlr_cursor_warp_absolute(server->cursor_, &ev->tablet->base, ev->x, ev->y);
        server->process_cursor_motion(ev->time_msec);
    }
}

void Server::on_cursor_tablet_tip(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_tablet_tip_);
    const auto* ev = static_cast<const wlr_tablet_tool_tip_event*>(data);
    if (!server->cursor_) return;
    wlr_cursor_warp_absolute(server->cursor_, &ev->tablet->base, ev->x, ev->y);
    server->process_cursor_motion(ev->time_msec);
    const auto state = (ev->state == WLR_TABLET_TOOL_TIP_DOWN)
                           ? WL_POINTER_BUTTON_STATE_PRESSED
                           : WL_POINTER_BUTTON_STATE_RELEASED;
    server->handle_pointer_button(ev->time_msec, BTN_LEFT, state);
}

void Server::on_cursor_tablet_button(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, cursor_tablet_button_);
    const auto* ev = static_cast<const wlr_tablet_tool_button_event*>(data);
    const auto state = (ev->state == WLR_BUTTON_PRESSED)
                           ? WL_POINTER_BUTTON_STATE_PRESSED
                           : WL_POINTER_BUTTON_STATE_RELEASED;
    const uint32_t btn = (ev->button == BTN_STYLUS) ? BTN_RIGHT : ev->button;
    server->handle_pointer_button(ev->time_msec, btn, state);
}

void Server::on_new_session_lock(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_session_lock_);
    auto* lock = static_cast<wlr_session_lock_v1*>(data);

    if (server->active_lock_ || server->session_locked_) {
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    server->active_lock_ = lock;
    server->session_locked_ = true;
    server->cursor_mode_ = CursorMode::kPassthrough;
    server->grabbed_window_ = nullptr;
    if (server->seat_) {
        wlr_seat_pointer_clear_focus(server->seat_);
        wlr_seat_keyboard_clear_focus(server->seat_);
    }
    server->update_lock_backdrop();

    attach_listener(
        lock->events.new_surface, server->active_lock_new_surface_,
        [](wl_listener* l, void* sdata) {
            Server* s = wl_container_of(l, s, active_lock_new_surface_);
            auto* lsurf = static_cast<wlr_session_lock_surface_v1*>(sdata);
            auto node = std::make_unique<LockSurface>();
            LockSurface* ls = node.get();
            ls->server = s;
            ls->lock_surface = lsurf;
            ls->scene_tree =
                wlr_scene_subsurface_tree_create(s->lock_tree_, lsurf->surface);
            s->lock_surfaces_.push_back(std::move(node));

            int out_w = 1440, out_h = 900;
            wlr_output* target = lsurf->output ? lsurf->output : s->primary_output();
            if (target) wlr_output_effective_resolution(target, &out_w, &out_h);
            wlr_session_lock_surface_v1_configure(
                lsurf, static_cast<uint32_t>(std::max(1, out_w)),
                static_cast<uint32_t>(std::max(1, out_h)));

            attach_listener(
                lsurf->surface->events.map, ls->map_l,
                [](wl_listener* ml, void*) {
                    LockSurface* item = wl_container_of(ml, item, map_l);
                    item->server->focus_lock_surface(item->lock_surface->surface);
                });

            attach_listener(
                lsurf->events.destroy, ls->destroy_l,
                [](wl_listener* dl, void*) {
                    LockSurface* item = wl_container_of(dl, item, destroy_l);
                    Server* srv = item->server;
                    if (item->scene_tree) {
                        wlr_scene_node_destroy(&item->scene_tree->node);
                        item->scene_tree = nullptr;
                    }
                    srv->lock_surfaces_.remove_if(
                        [item](const auto& p) { return p.get() == item; });
                    if (srv->session_locked_) {
                        if (!srv->lock_surfaces_.empty()) {
                            srv->focus_lock_surface(
                                srv->lock_surfaces_.front()->lock_surface->surface);
                        } else if (srv->seat_) {
                            wlr_seat_keyboard_clear_focus(srv->seat_);
                        }
                    }
                });
        });

    attach_listener(
        lock->events.unlock, server->active_lock_unlock_,
        [](wl_listener* l, void*) {
            Server* s = wl_container_of(l, s, active_lock_unlock_);
            s->session_locked_ = false;
            remove_listener(s->active_lock_new_surface_);
            remove_listener(s->active_lock_unlock_);
            remove_listener(s->active_lock_destroy_);
            s->active_lock_ = nullptr;
            for (auto& ls : s->lock_surfaces_) {
                if (ls->scene_tree) {
                    wlr_scene_node_destroy(&ls->scene_tree->node);
                    ls->scene_tree = nullptr;
                }
            }
            s->lock_surfaces_.clear();
            s->update_lock_backdrop();
            s->focus_top_window();
        });

    attach_listener(
        lock->events.destroy, server->active_lock_destroy_,
        [](wl_listener* l, void*) {
            Server* s = wl_container_of(l, s, active_lock_destroy_);
            remove_listener(s->active_lock_new_surface_);
            remove_listener(s->active_lock_unlock_);
            remove_listener(s->active_lock_destroy_);
            s->active_lock_ = nullptr;
        });

    wlr_session_lock_v1_send_locked(lock);
}

void Server::on_output_mgr_apply(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, output_mgr_apply_);
    server->on_output_manager_configure(static_cast<wlr_output_configuration_v1*>(data),
                                        false);
}

void Server::on_output_mgr_test(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, output_mgr_test_);
    server->on_output_manager_configure(static_cast<wlr_output_configuration_v1*>(data),
                                        true);
}

void Server::on_output_manager_configure(wlr_output_configuration_v1* cfg,
                                         bool test_only) {
    wlr_output_configuration_head_v1* head = nullptr;
    bool ok = true;
    wl_list_for_each(head, &cfg->heads, link) {
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, head->state.enabled);
        if (head->state.enabled) {
            if (head->state.mode) {
                wlr_output_state_set_mode(&state, head->state.mode);
            } else if (head->state.custom_mode.width > 0) {
                wlr_output_state_set_custom_mode(&state, head->state.custom_mode.width,
                                                 head->state.custom_mode.height,
                                                 head->state.custom_mode.refresh);
            }
            wlr_output_state_set_scale(&state, head->state.scale);
        }
        if (test_only) {
            ok &= wlr_output_test_state(head->state.output, &state);
        } else {
            ok &= wlr_output_commit_state(head->state.output, &state);
        }
        wlr_output_state_finish(&state);
    }
    if (ok) wlr_output_configuration_v1_send_succeeded(cfg);
    else wlr_output_configuration_v1_send_failed(cfg);
    wlr_output_configuration_v1_destroy(cfg);
    if (!test_only) {
        arrange_layers();
        update_background();
    }
}

int Server::run() {
    wlr_log_init(WLR_INFO, nullptr);

    display_ = wl_display_create();
    if (!display_) return 1;

    wl_event_loop* loop = wl_display_get_event_loop(display_);
    backend_ = wlr_backend_autocreate(loop, &wlr_session_);
    if (!backend_) {
        wlr_log(WLR_ERROR, "finn-comp: failed to create wlroots backend");
        return 1;
    }

    renderer_ = wlr_renderer_autocreate(backend_);
    if (!renderer_) {
        // A virtual GPU can expose DRM without a usable EGL or Vulkan device
        // (VMware SVGA with 3D disabled is one). Software rendering is still a
        // real renderer, so fall back to it instead of leaving the display dead.
        wlr_log(WLR_INFO,
                "finn-comp: no GPU renderer available; falling back to software (pixman)");
        setenv("WLR_RENDERER", "pixman", 1);
        renderer_ = wlr_renderer_autocreate(backend_);
    }
    if (!renderer_) return 1;
    wlr_renderer_init_wl_display(renderer_, display_);

    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    if (!allocator_) return 1;

    compositor_ = wlr_compositor_create(display_, 6, renderer_);
    wlr_subcompositor_create(display_);
    wlr_data_device_manager_create(display_);
    wlr_primary_selection_v1_device_manager_create(display_);
    wlr_data_control_manager_v1_create(display_);
    wlr_screencopy_manager_v1_create(display_);
    wlr_viewporter_create(display_);
    wlr_fractional_scale_manager_v1_create(display_, 1);

    output_layout_ = wlr_output_layout_create(display_);
    wlr_xdg_output_manager_v1_create(display_, output_layout_);
    output_manager_ = wlr_output_manager_v1_create(display_);
    attach_listener(output_manager_->events.apply, output_mgr_apply_, on_output_mgr_apply);
    attach_listener(output_manager_->events.test, output_mgr_test_, on_output_mgr_test);

    scene_ = wlr_scene_create();
    scene_layout_ = wlr_scene_attach_output_layout(scene_, output_layout_);

    // Exact stacking invariant:
    // Background rect -> layer 0 -> layer 1 -> toplevels -> layer 2 (top) -> fullscreen -> layer 3 (overlay) -> lock
    const float bg_init[4] = {0.918f, 0.925f, 0.941f, 1.0f};
    background_rect_ = wlr_scene_rect_create(&scene_->tree, 1440, 900, bg_init);
    // Glass layer surfaces receive a real CPU Gaussian backdrop blur
    // (35/15 px from ui/_tokens.fui) in commit_output_with_backdrop_blur().
    comp::backdrop_blur::report_once();
    layer_trees_[0] = wlr_scene_tree_create(&scene_->tree);
    layer_trees_[1] = wlr_scene_tree_create(&scene_->tree);
    toplevel_tree_ = wlr_scene_tree_create(&scene_->tree);
    layer_trees_[2] = wlr_scene_tree_create(&scene_->tree);
    fullscreen_tree_ = wlr_scene_tree_create(&scene_->tree);
    layer_trees_[3] = wlr_scene_tree_create(&scene_->tree);
    lock_tree_ = wlr_scene_tree_create(&scene_->tree);

    attach_listener(backend_->events.new_output, new_output_, on_new_output);
    attach_listener(backend_->events.new_input, new_input_, on_new_input);

    xdg_shell_ = wlr_xdg_shell_create(display_, 5);
    attach_listener(xdg_shell_->events.new_toplevel, new_xdg_toplevel_,
                    on_new_xdg_toplevel);
    attach_listener(xdg_shell_->events.new_popup, new_xdg_popup_, on_new_xdg_popup);

    xdg_decoration_mgr_ = wlr_xdg_decoration_manager_v1_create(display_);
    attach_listener(xdg_decoration_mgr_->events.new_toplevel_decoration,
                    new_xdg_decoration_, on_new_xdg_decoration);

    layer_shell_ = wlr_layer_shell_v1_create(display_, 4);
    attach_listener(layer_shell_->events.new_surface, new_layer_surface_,
                    on_new_layer_surface);

    foreign_toplevel_mgr_ = wlr_foreign_toplevel_manager_v1_create(display_);
    activation_ = wlr_xdg_activation_v1_create(display_);
    attach_listener(activation_->events.request_activate, request_activate_,
                    on_request_activate);

    idle_notifier_ = wlr_idle_notifier_v1_create(display_);
    idle_inhibit_mgr_ = wlr_idle_inhibit_v1_create(display_);
    session_lock_mgr_ = wlr_session_lock_manager_v1_create(display_);
    attach_listener(session_lock_mgr_->events.new_lock, new_session_lock_,
                    on_new_session_lock);

    cursor_ = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor_, output_layout_);
    dark_theme_ = read_dark_setting();
    cursor_size_ = read_cursor_size_setting();
    reduced_motion_ = read_reduced_motion_setting();
    reload_cursor_theme();

    cursor_shape_mgr_ = wlr_cursor_shape_manager_v1_create(display_, 1);
    attach_listener(cursor_shape_mgr_->events.request_set_shape, request_set_shape_,
                    on_request_set_shape);

    seat_ = wlr_seat_create(display_, "seat0");
    attach_listener(seat_->events.request_set_cursor, request_cursor_, on_request_cursor);
    attach_listener(seat_->events.request_set_selection, request_set_selection_,
                    on_request_set_selection);
    attach_listener(seat_->events.request_set_primary_selection,
                    request_set_primary_selection_, on_request_set_primary_selection);

    attach_listener(cursor_->events.motion, cursor_motion_, on_cursor_motion);
    attach_listener(cursor_->events.motion_absolute, cursor_motion_absolute_,
                    on_cursor_motion_absolute);
    attach_listener(cursor_->events.button, cursor_button_, on_cursor_button);
    attach_listener(cursor_->events.axis, cursor_axis_, on_cursor_axis);
    attach_listener(cursor_->events.frame, cursor_frame_, on_cursor_frame);
    attach_listener(cursor_->events.touch_down, cursor_touch_down_, on_cursor_touch_down);
    attach_listener(cursor_->events.touch_up, cursor_touch_up_, on_cursor_touch_up);
    attach_listener(cursor_->events.touch_motion, cursor_touch_motion_,
                    on_cursor_touch_motion);
    attach_listener(cursor_->events.touch_frame, cursor_touch_frame_, on_cursor_touch_frame);
    attach_listener(cursor_->events.touch_cancel, cursor_touch_cancel_,
                    on_cursor_touch_cancel);
    attach_listener(cursor_->events.tablet_tool_axis, cursor_tablet_axis_,
                    on_cursor_tablet_axis);
    attach_listener(cursor_->events.tablet_tool_proximity, cursor_tablet_proximity_,
                    on_cursor_tablet_proximity);
    attach_listener(cursor_->events.tablet_tool_tip, cursor_tablet_tip_,
                    on_cursor_tablet_tip);
    attach_listener(cursor_->events.tablet_tool_button, cursor_tablet_button_,
                    on_cursor_tablet_button);

    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || !*runtime_dir) {
        wlr_log(WLR_ERROR, "finn-comp: XDG_RUNTIME_DIR is required");
        return 1;
    }
    const char* sock_override = std::getenv("FINN_WAYLAND_SOCKET");
    const char* sock_name = (sock_override && *sock_override) ? sock_override : "wayland-0";
    if (wl_display_add_socket(display_, sock_name) != 0) {
        wlr_log(WLR_ERROR, "finn-comp: cannot bind %s/%s", runtime_dir, sock_name);
        return 1;
    }
    const std::string sock_full = std::string(runtime_dir) + "/" + sock_name;
    ::chmod(sock_full.c_str(), 0660);

    // Optional Xwayland server for X11 Linux apps and Wine Windows apps
    xwayland_ = wlr_xwayland_create(display_, compositor_, true);
    if (xwayland_) {
        attach_listener(xwayland_->events.ready, xwayland_ready_, on_xwayland_ready);
        attach_listener(xwayland_->events.new_surface, xwayland_new_surface_,
                        on_xwayland_new_surface);
        write_xdisplay_file(xwayland_->display_name);
        wlr_log(WLR_INFO, "finn-comp: Xwayland enabled (%s)",
                xwayland_->display_name ? xwayland_->display_name : "lazy");
    } else {
        wlr_log(WLR_INFO, "finn-comp: Xwayland unavailable; running Wayland-only");
    }

    if (!wlr_backend_start(backend_)) {
        wlr_log(WLR_ERROR, "finn-comp: failed to start backend");
        return 1;
    }

    dark_theme_ = read_dark_setting();
    update_background();
    reload_cursor_theme();

    // Feed systemd watchdog every 5s and check theme/display overrides every 1s
    watchdog_timer_ = wl_event_loop_add_timer(
        loop,
        [](void* data) -> int {
            auto* s = static_cast<Server*>(data);
            sd_notify(0, "WATCHDOG=1");
            wl_event_source_timer_update(s->watchdog_timer_, 5000);
            return 0;
        },
        this);
    wl_event_source_timer_update(watchdog_timer_, 5000);

    theme_timer_ = wl_event_loop_add_timer(
        loop,
        [](void* data) -> int {
            auto* s = static_cast<Server*>(data);
            s->update_display_settings();
            // Auto-lock uses the same timer. The compositor only asks; the
            // shell decides whether to show its password-verified overlay.
            const int minutes = Server::read_auto_lock_minutes();
            const uint64_t now = monotonic_msec();
            if (minutes > 0 && s->last_activity_msec_ != 0 &&
                now - s->last_activity_msec_ >=
                    static_cast<uint64_t>(minutes) * 60000ULL) {
                request_lock();
                // Back off a full interval so a shell that cannot lock is not
                // asked every second; real input resets this in the handlers.
                s->last_activity_msec_ = now;
            }
            wl_event_source_timer_update(s->theme_timer_, 1000);
            return 0;
        },
        this);
    wl_event_source_timer_update(theme_timer_, 1000);

    sd_notify(0, "READY=1");
    last_activity_msec_ = monotonic_msec();
    wlr_log(WLR_INFO, "finn-comp: running on %s", sock_full.c_str());
    wl_display_run(display_);
    return 0;
}

void Server::shutdown() {
    if (!display_) return;
    if (watchdog_timer_) {
        wl_event_source_remove(watchdog_timer_);
        watchdog_timer_ = nullptr;
    }
    if (theme_timer_) {
        wl_event_source_remove(theme_timer_);
        theme_timer_ = nullptr;
    }

    if (xwayland_) {
        remove_listener(xwayland_ready_);
        remove_listener(xwayland_new_surface_);
        wlr_xwayland_destroy(xwayland_);
        xwayland_ = nullptr;
        std::error_code ec;
        fs::remove(kXwaylandDisplayFile, ec);
    }

    wl_display_destroy_clients(display_);
    popups_.clear();
    layers_.clear();
    lock_surfaces_.clear();
    windows_.clear();
    outputs_.clear();
    keyboards_.clear();
    pointers_.clear();
    touches_.clear();
    tablets_.clear();

    remove_listener(new_output_);
    remove_listener(new_input_);
    remove_listener(new_xdg_toplevel_);
    remove_listener(new_xdg_popup_);
    remove_listener(new_xdg_decoration_);
    remove_listener(new_layer_surface_);
    remove_listener(request_cursor_);
    remove_listener(request_set_shape_);
    remove_listener(request_set_selection_);
    remove_listener(request_set_primary_selection_);
    remove_listener(request_activate_);
    remove_listener(cursor_motion_);
    remove_listener(cursor_motion_absolute_);
    remove_listener(cursor_button_);
    remove_listener(cursor_axis_);
    remove_listener(cursor_frame_);
    remove_listener(cursor_touch_down_);
    remove_listener(cursor_touch_up_);
    remove_listener(cursor_touch_motion_);
    remove_listener(cursor_touch_frame_);
    remove_listener(cursor_touch_cancel_);
    remove_listener(cursor_tablet_axis_);
    remove_listener(cursor_tablet_proximity_);
    remove_listener(cursor_tablet_tip_);
    remove_listener(cursor_tablet_button_);
    remove_listener(new_session_lock_);
    remove_listener(active_lock_new_surface_);
    remove_listener(active_lock_unlock_);
    remove_listener(active_lock_destroy_);
    remove_listener(output_mgr_apply_);
    remove_listener(output_mgr_test_);

    if (xcursor_mgr_) {
        wlr_xcursor_manager_destroy(xcursor_mgr_);
        xcursor_mgr_ = nullptr;
    }
    if (cursor_) {
        wlr_cursor_destroy(cursor_);
        cursor_ = nullptr;
    }
    wl_display_destroy(display_);
    display_ = nullptr;
}

}  // namespace finn
