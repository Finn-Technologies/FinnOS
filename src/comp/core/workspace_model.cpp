#include "comp/core/workspace_model.hpp"

#include <algorithm>
#include <cmath>

namespace finn::comp {

WorkspaceModel::WorkspaceModel() {
    OutputInfo def;
    def.id = 1;
    def.name = "default-0";
    def.geometry = {0, 0, 1280, 832};
    def.usable_area = {0, 0, 1280, 773};
    def.scale = 1.0;
    def.refresh_mhz = 60000;
    def.primary = true;
    outputs_.push_back(def);
    recompute_form_factor();
}

void WorkspaceModel::set_outputs(const std::vector<OutputInfo>& outputs) {
    if (outputs.empty()) return;
    outputs_ = outputs;
    set_exclusive_margins(excl_top_, excl_bottom_, excl_left_, excl_right_);
    recompute_form_factor();
}

OutputInfo WorkspaceModel::primary_output() const {
    for (const auto& o : outputs_) {
        if (o.primary) return o;
    }
    return outputs_.empty() ? OutputInfo{} : outputs_.front();
}

void WorkspaceModel::set_form_factor_override(std::string_view override_id) {
    form_factor_override_ = std::string(override_id);
    recompute_form_factor();
}

finn::ui::AdaptiveMetrics WorkspaceModel::form_factor_metrics() const {
    const auto out = primary_output();
    return finn::ui::compute_adaptive_metrics(form_factor_, out.geometry.width,
                                              out.geometry.height);
}

void WorkspaceModel::set_exclusive_margins(int top, int bottom, int left, int right) {
    excl_top_ = std::max(0, top);
    excl_bottom_ = std::max(0, bottom);
    excl_left_ = std::max(0, left);
    excl_right_ = std::max(0, right);
    for (auto& o : outputs_) {
        o.usable_area.x = o.geometry.x + excl_left_;
        o.usable_area.y = o.geometry.y + excl_top_;
        o.usable_area.width = std::max(160, o.geometry.width - excl_left_ - excl_right_);
        o.usable_area.height = std::max(160, o.geometry.height - excl_top_ - excl_bottom_);
    }
}

void WorkspaceModel::recompute_form_factor() {
    const auto out = primary_output();
    const auto next = finn::ui::resolve_adaptive_form_factor(
        out.geometry.width, out.geometry.height, form_factor_override_, out.tv_hint);
    form_factor_ = next;
    const auto m = finn::ui::compute_adaptive_metrics(form_factor_, out.geometry.width,
                                                      out.geometry.height);
    desktop_count_ = std::max(1, m.virtual_desktop_count);
    current_desktop_ = std::clamp(current_desktop_, 0, desktop_count_ - 1);

    // Update default shell exclusive zones for this form factor
    set_exclusive_margins(m.top_bar_height,
                          m.bottom_dock_height + m.bottom_dock_margin, 0, 0);

    for (auto& w : windows_) {
        if (w.desktop_index >= desktop_count_) {
            w.desktop_index = desktop_count_ - 1;
        }
        if (w.fullscreen) {
            w.target_geometry = out.geometry;
            retarget_window_springs(w, false);
        } else {
            apply_form_factor_policy_to_window(w, false);
        }
    }
    if (overview_active_) compute_overview_layout();
}

void WorkspaceModel::switch_desktop(int index) {
    const int clamped = std::clamp(index, 0, desktop_count_ - 1);
    if (clamped == current_desktop_) return;
    current_desktop_ = clamped;
    overview_active_ = false;
    // Focus top MRU window on the newly active desktop
    std::uint64_t next_focus = 0;
    for (std::uint64_t wid : mru_order_) {
        const auto* w = find_window(wid);
        if (w && w->mapped && !w->minimized &&
            (w->desktop_index == current_desktop_ || w->desktop_index < 0)) {
            next_focus = wid;
            break;
        }
    }
    focus_window(next_focus);
}

void WorkspaceModel::switch_desktop_relative(int delta) {
    if (desktop_count_ <= 1) return;
    const int next = (current_desktop_ + delta % desktop_count_ + desktop_count_) % desktop_count_;
    switch_desktop(next);
}

RectI WorkspaceModel::cascade_geometry(int w, int h) const {
    const RectI ua = primary_output().usable_area;
    const int cw = std::clamp(w, 280, std::max(280, ua.width - 24));
    const int ch = std::clamp(h, 200, std::max(200, ua.height - 24));
    const int base_x = ua.x + std::max(12, (ua.width - cw) / 2);
    const int base_y = ua.y + std::max(12, (ua.height - ch) / 2);
    const int offset = (cascade_step_ % 5) * 24 - 24;
    const int x = std::clamp(base_x + offset, ua.x + 12,
                             std::max(ua.x + 12, ua.x + ua.width - cw - 12));
    const int y = std::clamp(base_y + offset, ua.y + 12,
                             std::max(ua.y + 12, ua.y + ua.height - ch - 12));
    return {x, y, cw, ch};
}

void WorkspaceModel::apply_form_factor_policy_to_window(WindowNode& win, bool is_new) {
    const RectI ua = primary_output().usable_area;
    const auto m = form_factor_metrics();
    if (m.window_policy == finn::ui::WindowPolicy::FullscreenCardStack ||
        m.window_policy == finn::ui::WindowPolicy::TenFootFullscreen) {
        win.maximized = true;
        win.target_geometry = ua;
    } else if (m.window_policy == finn::ui::WindowPolicy::HybridSplitAndFloat) {
        if (is_new) {
            // Tablet hybrid: first window fills usable area, second pairs side-by-side 50/50
            std::size_t visible_count = 0;
            for (const auto& other : windows_) {
                if (other.id != win.id && other.mapped && !other.minimized &&
                    other.desktop_index == current_desktop_) {
                    ++visible_count;
                }
            }
            if (visible_count == 0) {
                win.maximized = true;
                win.target_geometry = ua;
            } else {
                const int gap = 8;
                const int half = std::max(320, (ua.width - gap) / 2);
                win.maximized = false;
                win.snap = SnapEdge::Right;
                win.target_geometry = {ua.x + ua.width - half, ua.y, half, ua.height};
                // Snap previous top window to Left half
                for (auto& other : windows_) {
                    if (other.id != win.id && other.mapped && !other.minimized &&
                        other.desktop_index == current_desktop_) {
                        other.maximized = false;
                        other.snap = SnapEdge::Left;
                        other.target_geometry = {ua.x, ua.y, half, ua.height};
                        retarget_window_springs(other, true);
                        break;
                    }
                }
            }
        }
    } else if (win.maximized) {
        win.target_geometry = ua;
    }
    retarget_window_springs(win, !is_new);
}

void WorkspaceModel::retarget_window_springs(WindowNode& win, bool animate) {
    const RectI& g = overview_active_ ? win.overview_geometry : win.target_geometry;
    const double target_op = win.minimized ? 0.0 : 1.0;
    const double target_sc = win.minimized ? 0.72 : 1.0;
    if (!animate || reduced_motion_) {
        win.spring_x.set(g.x, 0.0);
        win.spring_y.set(g.y, 0.0);
        win.spring_w.set(g.width, 0.0);
        win.spring_h.set(g.height, 0.0);
        win.spring_opacity.set(target_op, 0.0);
        win.spring_scale.set(target_sc, 0.0);
        return;
    }
    win.spring_x.retarget(g.x);
    win.spring_y.retarget(g.y);
    win.spring_w.retarget(g.width);
    win.spring_h.retarget(g.height);
    win.spring_opacity.retarget(target_op);
    win.spring_scale.retarget(target_sc);
}

std::uint64_t WorkspaceModel::add_window(const std::string& title,
                                         int preferred_w,
                                         int preferred_h,
                                         bool server_decorated,
                                         bool xwayland) {
    WindowNode win;
    win.id = next_id_++;
    win.title = title;
    win.desktop_index = current_desktop_;
    win.server_decorated = server_decorated;
    win.xwayland = xwayland;
    win.target_geometry = cascade_geometry(preferred_w, preferred_h);
    win.saved_geometry = win.target_geometry;
    ++cascade_step_;

    windows_.push_back(win);
    mru_order_.insert(mru_order_.begin(), win.id);

    auto& stored = windows_.back();
    apply_form_factor_policy_to_window(stored, true);

    // Clean centered scale entrance: 0.94 -> 1.0 (subtle, crisp, zero jump)
    if (reduced_motion_) {
        retarget_window_springs(stored, false);
    } else {
        stored.spring_x.set(stored.target_geometry.x, 0.0);
        stored.spring_y.set(stored.target_geometry.y, 0.0);
        stored.spring_w.set(stored.target_geometry.width, 0.0);
        stored.spring_h.set(stored.target_geometry.height, 0.0);
        stored.spring_opacity.set(0.0, 0.0);
        stored.spring_scale.set(0.94, 0.0);
        retarget_window_springs(stored, true);
    }

    overview_active_ = false;
    show_desktop_active_ = false;
    focus_window(stored.id);
    return stored.id;
}

bool WorkspaceModel::remove_window(std::uint64_t id) {
    const auto it = std::find_if(windows_.begin(), windows_.end(),
                                 [id](const WindowNode& w) { return w.id == id; });
    if (it == windows_.end()) return false;
    windows_.erase(it);
    mru_order_.erase(std::remove(mru_order_.begin(), mru_order_.end(), id),
                     mru_order_.end());
    if (focused_id_ == id) {
        focused_id_ = 0;
        for (std::uint64_t wid : mru_order_) {
            if (auto* next = find_window(wid);
                next && next->mapped && !next->minimized &&
                (next->desktop_index == current_desktop_ || next->desktop_index < 0)) {
                focus_window(wid);
                break;
            }
        }
    }
    if (overview_active_) compute_overview_layout();
    return true;
}

WindowNode* WorkspaceModel::find_window(std::uint64_t id) {
    for (auto& w : windows_) {
        if (w.id == id) return &w;
    }
    return nullptr;
}

const WindowNode* WorkspaceModel::find_window(std::uint64_t id) const {
    for (const auto& w : windows_) {
        if (w.id == id) return &w;
    }
    return nullptr;
}

void WorkspaceModel::focus_window(std::uint64_t id) {
    focused_id_ = id;
    for (auto& w : windows_) {
        w.focused = (w.id == id);
    }
    if (id != 0) {
        mru_order_.erase(std::remove(mru_order_.begin(), mru_order_.end(), id),
                         mru_order_.end());
        mru_order_.insert(mru_order_.begin(), id);
        if (auto* w = find_window(id); w && w->minimized) {
            w->minimized = false;
            retarget_window_springs(*w, true);
        }
    }
}

void WorkspaceModel::cycle_focus(bool forward) {
    std::vector<std::uint64_t> candidates;
    for (std::uint64_t wid : mru_order_) {
        const auto* w = find_window(wid);
        if (w && w->mapped &&
            (w->desktop_index == current_desktop_ || w->desktop_index < 0)) {
            candidates.push_back(wid);
        }
    }
    if (candidates.size() < 2) {
        if (candidates.size() == 1) focus_window(candidates.front());
        return;
    }
    if (forward) {
        focus_window(candidates[1]);
    } else {
        focus_window(candidates.back());
    }
}

void WorkspaceModel::move_window(std::uint64_t id, int x, int y, bool animate) {
    auto* w = find_window(id);
    if (!w) return;
    w->maximized = false;
    w->snap = SnapEdge::None;
    w->target_geometry.x = x;
    w->target_geometry.y = y;
    w->saved_geometry = w->target_geometry;
    retarget_window_springs(*w, animate);
}

void WorkspaceModel::resize_window(std::uint64_t id, int width, int height, bool animate) {
    auto* w = find_window(id);
    if (!w) return;
    w->maximized = false;
    w->target_geometry.width = std::max(240, width);
    w->target_geometry.height = std::max(160, height);
    w->saved_geometry = w->target_geometry;
    retarget_window_springs(*w, animate);
}

void WorkspaceModel::set_maximized(std::uint64_t id, bool maximized) {
    auto* w = find_window(id);
    if (!w || w->maximized == maximized) return;
    if (maximized) {
        if (w->snap == SnapEdge::None) w->saved_geometry = w->target_geometry;
        w->maximized = true;
        w->snap = SnapEdge::None;
        w->target_geometry = primary_output().usable_area;
    } else {
        w->maximized = false;
        w->target_geometry = w->saved_geometry;
    }
    retarget_window_springs(*w, true);
}

void WorkspaceModel::toggle_maximize(std::uint64_t id) {
    if (const auto* w = find_window(id)) {
        set_maximized(id, !w->maximized);
    }
}

void WorkspaceModel::set_fullscreen(std::uint64_t id, bool fullscreen) {
    auto* w = find_window(id);
    if (!w) return;
    if (fullscreen) {
        if (!w->fullscreen && !w->maximized && w->snap == SnapEdge::None) {
            w->saved_geometry = w->target_geometry;
        }
        w->fullscreen = true;
        w->target_geometry = primary_output().geometry;
        retarget_window_springs(*w, false);
    } else if (w->fullscreen) {
        w->fullscreen = false;
        w->target_geometry = w->maximized ? primary_output().usable_area : w->saved_geometry;
        retarget_window_springs(*w, true);
    }
}

void WorkspaceModel::set_minimized(std::uint64_t id, bool minimized) {
    auto* w = find_window(id);
    if (!w || w->minimized == minimized) return;
    w->minimized = minimized;
    retarget_window_springs(*w, true);
    if (minimized && focused_id_ == id) {
        focused_id_ = 0;
        w->focused = false;
        for (std::uint64_t wid : mru_order_) {
            if (wid != id) {
                if (auto* next = find_window(wid);
                    next && next->mapped && !next->minimized &&
                    next->desktop_index == current_desktop_) {
                    focus_window(wid);
                    break;
                }
            }
        }
    }
}

void WorkspaceModel::snap_window(std::uint64_t id, SnapEdge edge) {
    auto* w = find_window(id);
    if (!w) return;
    const RectI ua = primary_output().usable_area;
    if (w->snap == SnapEdge::None && !w->maximized) {
        w->saved_geometry = w->target_geometry;
    }
    w->maximized = false;
    w->snap = edge;
    const int gap = 8;
    const int half_w = std::max(240, (ua.width - gap) / 2);
    const int half_h = std::max(160, (ua.height - gap) / 2);
    switch (edge) {
        case SnapEdge::Left:
            w->target_geometry = {ua.x, ua.y, half_w, ua.height};
            break;
        case SnapEdge::Right:
            w->target_geometry = {ua.x + ua.width - half_w, ua.y, half_w, ua.height};
            break;
        case SnapEdge::TopLeft:
            w->target_geometry = {ua.x, ua.y, half_w, half_h};
            break;
        case SnapEdge::TopRight:
            w->target_geometry = {ua.x + ua.width - half_w, ua.y, half_w, half_h};
            break;
        case SnapEdge::BottomLeft:
            w->target_geometry = {ua.x, ua.y + ua.height - half_h, half_w, half_h};
            break;
        case SnapEdge::BottomRight:
            w->target_geometry = {ua.x + ua.width - half_w, ua.y + ua.height - half_h,
                                  half_w, half_h};
            break;
        case SnapEdge::None:
            w->target_geometry = w->saved_geometry;
            break;
    }
    retarget_window_springs(*w, true);
}

void WorkspaceModel::toggle_show_desktop() {
    show_desktop_active_ = !show_desktop_active_;
    for (auto& w : windows_) {
        if (w.desktop_index == current_desktop_ || w.desktop_index < 0) {
            w.minimized = show_desktop_active_;
            retarget_window_springs(w, true);
        }
    }
}

void WorkspaceModel::compute_overview_layout() {
    std::vector<WindowNode*> visible;
    for (auto& w : windows_) {
        if (w.mapped && (w.desktop_index == current_desktop_ || w.desktop_index < 0)) {
            visible.push_back(&w);
        }
    }
    if (visible.empty()) return;

    const RectI ua = primary_output().usable_area;
    const int n = static_cast<int>(visible.size());
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))));
    const int rows = std::max(1, (n + cols - 1) / cols);
    const int pad = 32;
    const int gap = 20;
    const int cell_w = std::max(160, (ua.width - pad * 2 - gap * (cols - 1)) / cols);
    const int cell_h = std::max(120, (ua.height - pad * 2 - gap * (rows - 1)) / rows);

    for (int i = 0; i < n; ++i) {
        const int r = i / cols;
        const int c = i % cols;
        auto* w = visible[static_cast<std::size_t>(i)];
        w->overview_geometry = {
            ua.x + pad + c * (cell_w + gap),
            ua.y + pad + r * (cell_h + gap),
            cell_w,
            cell_h,
        };
        retarget_window_springs(*w, true);
    }
}

void WorkspaceModel::toggle_overview() {
    if (overview_active_) {
        exit_overview(focused_id_);
    } else {
        overview_active_ = true;
        compute_overview_layout();
    }
}

void WorkspaceModel::exit_overview(std::uint64_t focus_target) {
    overview_active_ = false;
    for (auto& w : windows_) {
        retarget_window_springs(w, true);
    }
    if (focus_target != 0) focus_window(focus_target);
}

bool WorkspaceModel::step_animations(double dt_seconds) {
    bool any_active = false;
    const double dt = std::clamp(dt_seconds, 0.001, 0.05);
    for (auto& w : windows_) {
        if (!w.spring_x.settled()) { w.spring_x.step(dt); any_active = true; }
        if (!w.spring_y.settled()) { w.spring_y.step(dt); any_active = true; }
        if (!w.spring_w.settled()) { w.spring_w.step(dt); any_active = true; }
        if (!w.spring_h.settled()) { w.spring_h.step(dt); any_active = true; }
        if (!w.spring_opacity.settled()) { w.spring_opacity.step(dt); any_active = true; }
        if (!w.spring_scale.settled()) { w.spring_scale.step(dt); any_active = true; }
    }
    return any_active;
}

}  // namespace finn::comp
