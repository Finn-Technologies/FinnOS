// KWin-inspired pure C++20 WorkspaceModel for finn-comp.
// Owns outputs, virtual desktops, managed windows, MRU focus stack, Overview
// grid layout, form-factor-aware window policies (Desktop, Tablet, Mobile, TV),
// and analytic 120Hz spring physics for every window transition.
//
// Contains ZERO app IDs (enforced by scripts/lint.sh).

#ifndef FINN_COMP_CORE_WORKSPACE_MODEL_HPP
#define FINN_COMP_CORE_WORKSPACE_MODEL_HPP

#include "comp/core/scene_stack.hpp"
#include "ui/finn/form_factor.hpp"
#include "ui/finn/spring.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace finn::comp {

struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct OutputInfo {
    std::uint32_t id = 0;
    std::string name;
    RectI geometry{0, 0, 1280, 832};
    RectI usable_area{0, 0, 1280, 773};
    double scale = 1.0;
    int refresh_mhz = 60000;
    bool tv_hint = false;
    bool primary = true;
};

enum class SnapEdge {
    None,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct WindowNode {
    std::uint64_t id = 0;
    std::string title;
    std::string surface_role;
    int desktop_index = 0;  // 0-based virtual desktop (-1 = pinned to all desktops)

    bool mapped = true;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    bool server_decorated = false;
    bool focused = false;
    bool xwayland = false;
    SnapEdge snap = SnapEdge::None;

    RectI target_geometry{120, 80, 800, 500};
    RectI saved_geometry{120, 80, 800, 500};
    RectI overview_geometry{120, 80, 800, 500};

    // Analytic spring state for smooth 60/120Hz compositing
    finn::ui::SpringAnimator spring_x{finn::ui::springs::kSmooth, 120.0};
    finn::ui::SpringAnimator spring_y{finn::ui::springs::kSmooth, 80.0};
    finn::ui::SpringAnimator spring_w{finn::ui::springs::kSmooth, 800.0};
    finn::ui::SpringAnimator spring_h{finn::ui::springs::kSmooth, 500.0};
    finn::ui::SpringAnimator spring_opacity{finn::ui::springs::kSnappy, 1.0};
    finn::ui::SpringAnimator spring_scale{finn::ui::springs::kSmooth, 1.0};

    SceneLayer scene_layer() const noexcept {
        return fullscreen ? SceneLayer::Fullscreen : SceneLayer::Toplevels;
    }

    int z_index(int intra_order) const noexcept {
        return layer_z_base(scene_layer()) + intra_order;
    }
};

class WorkspaceModel {
public:
    WorkspaceModel();

    void set_outputs(const std::vector<OutputInfo>& outputs);
    const std::vector<OutputInfo>& outputs() const { return outputs_; }
    OutputInfo primary_output() const;

    void set_form_factor_override(std::string_view override_id);
    finn::ui::AdaptiveFormFactor form_factor() const { return form_factor_; }
    finn::ui::AdaptiveMetrics form_factor_metrics() const;

    void set_reduced_motion(bool reduced) { reduced_motion_ = reduced; }
    bool reduced_motion() const { return reduced_motion_; }

    // Layer exclusive zones update primary output usable work area
    void set_exclusive_margins(int top, int bottom, int left, int right);

    // Virtual desktops (KWin-style)
    int desktop_count() const { return desktop_count_; }
    int current_desktop() const { return current_desktop_; }
    void switch_desktop(int index);
    void switch_desktop_relative(int delta);

    // Window lifecycle & management
    std::uint64_t add_window(const std::string& title, int preferred_w = 800,
                             int preferred_h = 500, bool server_decorated = false,
                             bool xwayland = false);
    bool remove_window(std::uint64_t id);
    WindowNode* find_window(std::uint64_t id);
    const WindowNode* find_window(std::uint64_t id) const;
    const std::vector<WindowNode>& windows() const { return windows_; }

    std::uint64_t focused_window_id() const { return focused_id_; }
    void focus_window(std::uint64_t id);
    void cycle_focus(bool forward = true);

    void move_window(std::uint64_t id, int x, int y, bool animate = false);
    void resize_window(std::uint64_t id, int w, int h, bool animate = false);
    void set_maximized(std::uint64_t id, bool maximized);
    void toggle_maximize(std::uint64_t id);
    void set_fullscreen(std::uint64_t id, bool fullscreen);
    void set_minimized(std::uint64_t id, bool minimized);
    void snap_window(std::uint64_t id, SnapEdge edge);
    void toggle_show_desktop();

    // KWin Overview / Present Windows
    bool overview_active() const { return overview_active_; }
    void toggle_overview();
    void exit_overview(std::uint64_t focus_target = 0);

    // Advance all window spring animations by dt seconds; returns true if any active
    bool step_animations(double dt_seconds);

private:
    void recompute_form_factor();
    void apply_form_factor_policy_to_window(WindowNode& win, bool is_new);
    void retarget_window_springs(WindowNode& win, bool animate);
    void compute_overview_layout();
    RectI cascade_geometry(int w, int h) const;

    std::vector<OutputInfo> outputs_;
    std::vector<WindowNode> windows_;
    std::vector<std::uint64_t> mru_order_;

    std::string form_factor_override_ = "auto";
    finn::ui::AdaptiveFormFactor form_factor_ = finn::ui::AdaptiveFormFactor::Desktop;
    bool reduced_motion_ = false;
    bool overview_active_ = false;
    bool show_desktop_active_ = false;

    int desktop_count_ = 4;
    int current_desktop_ = 0;
    int cascade_step_ = 0;
    int excl_top_ = 0;
    int excl_bottom_ = 59;  // Desktop 44px dock + 15px margin
    int excl_left_ = 0;
    int excl_right_ = 0;

    std::uint64_t next_id_ = 1;
    std::uint64_t focused_id_ = 0;
};

}  // namespace finn::comp

#endif  // FINN_COMP_CORE_WORKSPACE_MODEL_HPP
