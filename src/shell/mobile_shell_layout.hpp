// Responsive phone / tablet shell geometry and motion for finn-shell.
//
// The geometry in this header is pure C++20 on purpose: every number is
// derived from a token in ui/_tokens.fui (as generated into
// src/ui/generated_tokens.hpp) or from a measured Figma frame in
// docs/oobe-mobile-design.md.  Widget composition and painting live in
// mobile_shell_layout.cpp; keeping the maths here makes it testable headlessly.

#ifndef FINN_SHELL_MOBILE_SHELL_LAYOUT_HPP
#define FINN_SHELL_MOBILE_SHELL_LAYOUT_HPP

#include "ui/finn/spring.hpp"
#include "ui/generated_tokens.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class QWidget;  // the Qt implementation owns the concrete widget

namespace finn::shell::mobile {

// These assertions make a token drift a build error rather than a silent
// layout change.  The generator is the only place the numbers may change.
static_assert(finn::ui::breakpoints::bp_phone_max == 599);
static_assert(finn::ui::breakpoints::bp_tablet_min == 600);
static_assert(finn::ui::breakpoints::bp_tablet_max == 1024);
static_assert(finn::ui::breakpoints::bp_desktop_min == 1025);

enum class LayoutClass { Phone, Tablet, Desktop };

// The same breakpoint rule as finn::shell::resolve_form_factor(), expressed
// against the generated tokens directly so the pure header needs no Qt.
constexpr LayoutClass resolve_layout_class(int width_dp) noexcept {
    namespace bp = finn::ui::breakpoints;
    if (width_dp > 0 && width_dp <= bp::bp_phone_max) return LayoutClass::Phone;
    if (width_dp > 0 && width_dp <= bp::bp_tablet_max) return LayoutClass::Tablet;
    return LayoutClass::Desktop;
}

constexpr const char* layout_name(LayoutClass layout) noexcept {
    switch (layout) {
        case LayoutClass::Phone: return "phone";
        case LayoutClass::Tablet: return "tablet";
        case LayoutClass::Desktop: return "desktop";
    }
    return "unknown";
}

// Token timings transcribed from the `motion:` block of ui/_tokens.fui.
// These are not C++ animation constants: they are the shell's single copy of
// the design durations, repeated here only because generated_tokens.hpp does
// not emit the motion block.  Their names map one-to-one to the token names.
enum class MotionToken {
    SurfaceOpen,
    SurfaceClose,
    ScrimIn,
    ScrimOut,
    HoverIn,
    HoverOut,
    PressIn,
    PressOut,
    Fill,
    Theme,
};

constexpr int motion_duration_ms(MotionToken token) noexcept {
    switch (token) {
        case MotionToken::SurfaceOpen: return 280;   // surface_open 280ms out_back
        case MotionToken::SurfaceClose: return 180;  // surface_close 180ms in_out
        case MotionToken::ScrimIn: return 200;       // scrim_in 200ms out
        case MotionToken::ScrimOut: return 140;      // scrim_out 140ms in_out
        case MotionToken::HoverIn: return 110;       // hover_in 110ms out
        case MotionToken::HoverOut: return 200;      // hover_out 200ms out
        case MotionToken::PressIn: return 80;        // press_in 80ms out
        case MotionToken::PressOut: return 180;      // press_out 180ms out
        case MotionToken::Fill: return 200;          // fill 200ms out
        case MotionToken::Theme: return 360;         // theme 360ms in_out
    }
    return 0;
}

constexpr const char* motion_curve(MotionToken token) noexcept {
    switch (token) {
        case MotionToken::SurfaceOpen: return "out_back";
        case MotionToken::SurfaceClose: return "in_out";
        case MotionToken::ScrimIn: return "out";
        case MotionToken::ScrimOut: return "in_out";
        case MotionToken::HoverIn: return "out";
        case MotionToken::HoverOut: return "out";
        case MotionToken::PressIn: return "out";
        case MotionToken::PressOut: return "out";
        case MotionToken::Fill: return "out";
        case MotionToken::Theme: return "in_out";
    }
    return "linear";
}

// Translate a token's duration + easing into the existing analytic spring
// solver (finn::ui::SpringParams::from_response).  The damping ratio is chosen
// from the curve family, not from taste:
//   out_back -> slightly underdamped, one small overshoot (~8% for zeta .62);
//   in_out   -> critically damped, no overshoot;
//   out      -> near-critical, a fast start with a gentle settle.
// The response period is the token duration in seconds, so a 280 ms open and
// a 140 ms scrim-out settle on their token budgets rather than a shared 220 ms
// default.
inline finn::ui::SpringParams motion_spring(MotionToken token) noexcept {
    const double seconds = static_cast<double>(motion_duration_ms(token)) / 1000.0;
    switch (token) {
        case MotionToken::SurfaceOpen:
            return finn::ui::SpringParams::from_response(seconds, 0.62);
        case MotionToken::ScrimIn:
            return finn::ui::SpringParams::from_response(seconds, 0.90);
        case MotionToken::SurfaceClose:
        case MotionToken::ScrimOut:
        case MotionToken::Theme:
            return finn::ui::SpringParams::from_response(seconds, 1.0);
        case MotionToken::HoverIn:
        case MotionToken::HoverOut:
        case MotionToken::PressIn:
        case MotionToken::PressOut:
        case MotionToken::Fill:
        default:
            return finn::ui::SpringParams::from_response(seconds, 1.0);
    }
}

struct RectDp {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct LayoutMetrics {
    LayoutClass layout = LayoutClass::Desktop;
    int width_dp = 0;
    int height_dp = 0;

    // 4 dp spacing grid.
    int page_margin = 0;
    int section_gap = 0;
    int list_gap = 0;

    // Touch and pointer geometry.
    int row_height = 0;
    int primary_height = 0;
    int input_height = 0;
    int nav_bar_height = 0;
    int nav_circle = 0;
    int nav_cluster_gap = 0;

    // Radii: Figma rows use cornerRadius 10 / smoothing 0.6 (r_panel 10.4);
    // phone and tablet sheets use r_xl 20.
    double row_radius = 0.0;
    int sheet_top_radius = 0;

    // OOBE column and tablet panes.
    int column_width = 0;
    int nav_rail_width = 0;
    int master_pane_width = 0;
    int content_min_width = 0;

    // Phone system navigation, from docs/design-language.md section 10.
    int gesture_pill_width = 0;
    int gesture_pill_height = 0;
    int gesture_pill_bottom = 0;

    // Figma type sizes (px) for the setup prompt and option rows.
    int prompt_px = 24;
    int row_label_px = 18;
};

inline LayoutMetrics compute_layout(int width_dp, int height_dp) noexcept {
    namespace sp = finn::ui::spacing;
    namespace radii = finn::ui::radii;

    LayoutMetrics m;
    m.width_dp = width_dp;
    m.height_dp = height_dp;
    m.layout = resolve_layout_class(width_dp);

    m.nav_cluster_gap = sp::sp_5;       // Figma 36:70: 20 px between circles
    m.list_gap = sp::sp_3;              // Figma 37:96 has 10 px; 12 px is the
                                        // 4 dp-grid remediation (figma-audit FA-M6)
    m.row_radius = radii::r_panel;      // 10.4 legacy alias of Figma's 10 + 0.6
    m.sheet_top_radius = static_cast<int>(radii::r_xl);  // 20 dp sheets
    m.gesture_pill_width = 134;
    m.gesture_pill_height = 4;
    m.gesture_pill_bottom = sp::sp_2;

    switch (m.layout) {
        case LayoutClass::Phone:
            m.page_margin = sp::sp_4;      // 16 dp
            m.section_gap = sp::sp_4;      // 16 dp
            m.row_height = 48;             // design-language 8.3: 48-64 dp rows
            m.primary_height = 48;         // 48 dp primary action
            m.input_height = 44;           // 44 dp input
            m.nav_bar_height = 56;         // 56 dp bottom tab bar
            m.nav_circle = 48;             // 44 dp min, 48 dp primary
            m.nav_rail_width = 0;
            m.master_pane_width = 0;
            m.content_min_width = 0;
            break;
        case LayoutClass::Tablet:
            m.page_margin = sp::sp_6;      // 24 dp
            m.section_gap = sp::sp_5;      // 20 dp
            m.row_height = 44;             // 44-56 dp rows
            m.primary_height = 40;         // 40 dp tablet buttons
            m.input_height = 40;           // 40 dp inputs
            m.nav_bar_height = 56;
            m.nav_circle = 48;
            m.nav_rail_width = 68;         // design-language 9: 68 dp rail
            m.master_pane_width = 320;     // 300-340 dp master list
            m.content_min_width = 320;     // >= 320 dp detail pane
            break;
        case LayoutClass::Desktop:
        default:
            m.page_margin = sp::sp_8;      // 32 dp
            m.section_gap = sp::sp_5;
            m.row_height = 37;             // measured Figma 37:98 row height
            m.primary_height = 56;         // measured Figma 36:48 circle
            m.input_height = 42;           // measured apps/oobe OobeInputField
            m.nav_bar_height = 0;          // the dock owns desktop navigation
            m.nav_circle = 56;
            m.nav_rail_width = 0;
            m.master_pane_width = 0;
            m.content_min_width = 0;
            break;
    }

    // Figma 37:96: a 350 px-wide option column.  On smaller phones it reflows
    // to the available width minus the page margins (design-language section 10).
    const int available = width_dp > 0 ? width_dp - 2 * m.page_margin : 350;
    m.column_width = std::max(1, std::min(350, available));
    return m;
}

inline RectDp column_rect(const LayoutMetrics& m) {
    const int width = m.column_width;
    const int x = (m.width_dp - width) / 2;
    return RectDp{x, m.page_margin, width,
                  std::max(0, m.height_dp - m.nav_bar_height - 2 * m.page_margin)};
}

inline RectDp nav_cluster_rect(const LayoutMetrics& m) {
    const int width = 2 * m.nav_circle + m.nav_cluster_gap;
    const int x = (m.width_dp - width) / 2;
    const int bottom = m.nav_bar_height > 0 ? m.nav_bar_height : m.page_margin;
    return RectDp{x, m.height_dp - bottom - m.nav_circle, width, m.nav_circle};
}

inline RectDp sheet_rect(const LayoutMetrics& m, int sheet_height) {
    return RectDp{0, std::max(0, m.height_dp - sheet_height), m.width_dp,
                  std::max(0, sheet_height)};
}

// The Qt-facing seam.  Keeping it as an abstract interface lets the geometry
// and motion above stay pure C++ while mobile_shell_layout.cpp composes real
// finn::ui widgets.  The host owns the returned object.
class MobileShellHostApi {
public:
    virtual ~MobileShellHostApi() = default;
    virtual void setStepTitle(const std::string& title) = 0;
    virtual void setStepContent(QWidget* content) = 0;
    virtual void animateStepContent(QWidget* content, bool forward) = 0;
    virtual void setStepProgress(int current, int total) = 0;
    virtual void setNavigationEnabled(bool back, bool next) = 0;
    virtual void setNavigationCallbacks(std::function<void()> on_back,
                                        std::function<void()> on_next) = 0;
    virtual void setRailSteps(const std::vector<std::string>& ids,
                              const std::vector<std::string>& labels,
                              const std::string& current_id) = 0;
    virtual void setRailCallback(std::function<void(const std::string&)> on_select) = 0;
    virtual void showBottomSheet(QWidget* sheet_content, int sheet_height_dp) = 0;
    virtual void closeBottomSheet() = 0;
    virtual LayoutMetrics metrics() const = 0;
};

// Implemented in mobile_shell_layout.cpp (requires Qt6 Widgets).
MobileShellHostApi* create_mobile_shell_host(QWidget* parent = nullptr);

}  // namespace finn::shell::mobile

#endif  // FINN_SHELL_MOBILE_SHELL_LAYOUT_HPP
