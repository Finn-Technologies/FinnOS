// Pure C++20 adaptive form-factor engine shared by finn-comp, finn-shell,
// libfinnui, and all first-party / FinnSDK applications.
//
// Unifies KDE Plasma-style Desktop, Tablet (hybrid), Mobile (Plasma Mobile-style),
// and TV (Plasma Bigscreen-style 10-foot) layouts from one single token authority
// (ui/_tokens.fui -> src/ui/generated_tokens.hpp).

#ifndef FINN_UI_FINN_FORM_FACTOR_HPP
#define FINN_UI_FINN_FORM_FACTOR_HPP

#include "ui/generated_tokens.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace finn::ui {

enum class AdaptiveFormFactor {
    Desktop = 0,
    Tablet = 1,
    Mobile = 2,
    TV = 3,
};

enum class InputModality {
    Pointer,
    Touch,
    RemoteDPad,
};

enum class WindowPolicy {
    FloatingAndTiling,   // Desktop: Plasma-style floating, snap, overview, virtual desktops
    HybridSplitAndFloat, // Tablet: 50/50 & 60/40 split panes + touch-friendly floating windows
    FullscreenCardStack, // Mobile: single active fullscreen surface + horizontal card switcher
    TenFootFullscreen,   // TV: fullscreen focus-driven surfaces + overlay side drawer
};

struct AdaptiveMetrics {
    AdaptiveFormFactor form_factor = AdaptiveFormFactor::Desktop;
    InputModality primary_input = InputModality::Pointer;
    WindowPolicy window_policy = WindowPolicy::FloatingAndTiling;

    int screen_width_dp = 1280;
    int screen_height_dp = 832;

    // Typography and visual scale
    double type_scale = 1.0;
    int min_body_px = 14;
    int title_px = 16;
    int display_clock_px = 64;

    // Spacing & hit targets (4dp grid)
    int page_margin = spacing::sp_4;
    int card_gap = spacing::sp_3;
    int min_hit_target = 32;
    int button_height = 32;
    int input_height = 32;
    int nav_row_height = 36;
    int toggle_width = 30;
    int toggle_height = 18;

    // Shell chrome geometry
    int top_bar_height = 0;        // 0 on Desktop (bottom 4-pill dock), 32 on Tablet, 32 on Mobile, 64 on TV
    int bottom_dock_height = 44;   // 44 on Desktop (Figma 4:58), 56 on Tablet, 24 gesture bar on Mobile, 0 on TV
    int bottom_dock_margin = 15;   // 15px from bottom edge on Desktop (832 - 773 - 44 = 15)
    int sidebar_width = 250;       // Figma 21:131
    int nav_rail_width = 68;       // Tablet portrait rail
    int corner_radius_window = static_cast<int>(radii::r_lg);
    int corner_radius_card = static_cast<int>(radii::r_sm);
    int corner_radius_sheet = static_cast<int>(radii::r_xl);
    int focus_ring_px = 2;         // 2px desktop/tablet, 3px luminous TV focus ring

    bool edge_to_edge_windows = false;
    bool show_window_controls = true;
    bool use_bottom_sheets = false;
    bool dpad_focus_navigation = false;
    int virtual_desktop_count = 4;
};

inline std::string_view form_factor_id(AdaptiveFormFactor ff) noexcept {
    switch (ff) {
        case AdaptiveFormFactor::Desktop: return "desktop";
        case AdaptiveFormFactor::Tablet:  return "tablet";
        case AdaptiveFormFactor::Mobile:  return "mobile";
        case AdaptiveFormFactor::TV:      return "tv";
    }
    return "desktop";
}

inline std::string_view form_factor_display_name(AdaptiveFormFactor ff) noexcept {
    switch (ff) {
        case AdaptiveFormFactor::Desktop: return "Desktop";
        case AdaptiveFormFactor::Tablet:  return "Tablet";
        case AdaptiveFormFactor::Mobile:  return "Mobile";
        case AdaptiveFormFactor::TV:      return "TV";
    }
    return "Desktop";
}

inline bool parse_form_factor_id(std::string_view raw, AdaptiveFormFactor* out) noexcept {
    std::string lower;
    lower.reserve(raw.size());
    for (unsigned char c : raw) {
        if (!std::isspace(c)) lower.push_back(static_cast<char>(std::tolower(c)));
    }
    if (lower == "desktop") { *out = AdaptiveFormFactor::Desktop; return true; }
    if (lower == "tablet")  { *out = AdaptiveFormFactor::Tablet;  return true; }
    if (lower == "mobile" || lower == "phone") { *out = AdaptiveFormFactor::Mobile; return true; }
    if (lower == "tv" || lower == "bigscreen") { *out = AdaptiveFormFactor::TV;     return true; }
    return false;
}

// Single authoritative form-factor resolver:
// 1. Explicit runtime override (env FINN_FORM_FACTOR or explicit argument)
// 2. Persisted user setting (if not "auto")
// 3. Output dimensions + optional TV hint against ui/_tokens.fui breakpoints
inline AdaptiveFormFactor resolve_adaptive_form_factor(
    int width_dp,
    int height_dp = 0,
    std::string_view setting_override = {},
    bool tv_output_hint = false) noexcept {
    AdaptiveFormFactor parsed = AdaptiveFormFactor::Desktop;
    if (const char* env = std::getenv("FINN_FORM_FACTOR"); env && *env) {
        if (parse_form_factor_id(env, &parsed)) return parsed;
    }
    if (!setting_override.empty() && setting_override != "auto") {
        if (parse_form_factor_id(setting_override, &parsed)) return parsed;
    }
    if (tv_output_hint) {
        return AdaptiveFormFactor::TV;
    }
    if (width_dp > 0 && width_dp <= breakpoints::bp_phone_max) {
        return AdaptiveFormFactor::Mobile;
    }
    if (width_dp > 0 && width_dp <= breakpoints::bp_tablet_max) {
        return AdaptiveFormFactor::Tablet;
    }
    // Portrait narrow tall display with small physical width also maps to Mobile
    if (width_dp > 0 && height_dp > width_dp && width_dp < 640) {
        return AdaptiveFormFactor::Mobile;
    }
    return AdaptiveFormFactor::Desktop;
}

inline AdaptiveMetrics compute_adaptive_metrics(
    AdaptiveFormFactor ff,
    int width_dp = 1280,
    int height_dp = 832) noexcept {
    AdaptiveMetrics m;
    m.form_factor = ff;
    m.screen_width_dp = width_dp > 0 ? width_dp : 1280;
    m.screen_height_dp = height_dp > 0 ? height_dp : 832;

    switch (ff) {
        case AdaptiveFormFactor::Desktop:
            m.primary_input = InputModality::Pointer;
            m.window_policy = WindowPolicy::FloatingAndTiling;
            m.type_scale = 1.0;
            m.min_body_px = 14;
            m.title_px = 16;
            m.display_clock_px = 64;
            m.page_margin = spacing::sp_4;
            m.card_gap = spacing::sp_3;
            m.min_hit_target = 32;
            m.button_height = 32;
            m.input_height = 30;
            m.nav_row_height = 36;
            m.toggle_width = 30;
            m.toggle_height = 18;
            m.top_bar_height = 0;
            m.bottom_dock_height = 44;
            m.bottom_dock_margin = 15;
            m.sidebar_width = 250;
            m.nav_rail_width = 68;
            m.corner_radius_window = static_cast<int>(radii::r_lg);
            m.corner_radius_card = static_cast<int>(radii::r_sm);
            m.corner_radius_sheet = static_cast<int>(radii::r_xl);
            m.focus_ring_px = 2;
            m.edge_to_edge_windows = false;
            m.show_window_controls = true;
            m.use_bottom_sheets = false;
            m.dpad_focus_navigation = false;
            m.virtual_desktop_count = 4;
            break;

        case AdaptiveFormFactor::Tablet:
            m.primary_input = InputModality::Touch;
            m.window_policy = WindowPolicy::HybridSplitAndFloat;
            m.type_scale = 1.05;
            m.min_body_px = 15;
            m.title_px = 18;
            m.display_clock_px = 68;
            m.page_margin = spacing::sp_5;
            m.card_gap = spacing::sp_3;
            m.min_hit_target = 44;
            m.button_height = 40;
            m.input_height = 40;
            m.nav_row_height = 44;
            m.toggle_width = 44;
            m.toggle_height = 24;
            m.top_bar_height = 32;
            m.bottom_dock_height = 56;
            m.bottom_dock_margin = 12;
            m.sidebar_width = (m.screen_width_dp >= 840) ? 240 : 68;
            m.nav_rail_width = 68;
            m.corner_radius_window = static_cast<int>(radii::r_lg);
            m.corner_radius_card = static_cast<int>(radii::r_md);
            m.corner_radius_sheet = static_cast<int>(radii::r_xl);
            m.focus_ring_px = 2;
            m.edge_to_edge_windows = false;
            m.show_window_controls = true;
            m.use_bottom_sheets = true;
            m.dpad_focus_navigation = false;
            m.virtual_desktop_count = 2;
            break;

        case AdaptiveFormFactor::Mobile:
            m.primary_input = InputModality::Touch;
            m.window_policy = WindowPolicy::FullscreenCardStack;
            m.type_scale = 1.0;
            m.min_body_px = 15;
            m.title_px = 18;
            m.display_clock_px = 64;
            m.page_margin = spacing::sp_4;
            m.card_gap = spacing::sp_3;
            m.min_hit_target = 44;
            m.button_height = 44;
            m.input_height = 44;
            m.nav_row_height = 48;
            m.toggle_width = 48;
            m.toggle_height = 28;
            m.top_bar_height = 34;
            m.bottom_dock_height = 24;
            m.bottom_dock_margin = 0;
            m.sidebar_width = 0;
            m.nav_rail_width = 0;
            m.corner_radius_window = 0;
            m.corner_radius_card = static_cast<int>(radii::r_md);
            m.corner_radius_sheet = static_cast<int>(radii::r_xl);
            m.focus_ring_px = 2;
            m.edge_to_edge_windows = true;
            m.show_window_controls = false;
            m.use_bottom_sheets = true;
            m.dpad_focus_navigation = false;
            m.virtual_desktop_count = 1;
            break;

        case AdaptiveFormFactor::TV:
            m.primary_input = InputModality::RemoteDPad;
            m.window_policy = WindowPolicy::TenFootFullscreen;
            m.type_scale = tv::type_scale;   // 1.5x from ui/_tokens.fui
            m.min_body_px = std::max(20, static_cast<int>(14.0 * tv::type_scale));
            m.title_px = std::max(tv::min_sp, 32);
            m.display_clock_px = 76;
            m.page_margin = spacing::sp_12;  // 48dp overscan safe-area margin
            m.card_gap = spacing::sp_5;
            m.min_hit_target = 56;
            m.button_height = 52;
            m.input_height = 52;
            m.nav_row_height = 56;
            m.toggle_width = 52;
            m.toggle_height = 30;
            m.top_bar_height = 72;
            m.bottom_dock_height = 0;
            m.bottom_dock_margin = 0;
            m.sidebar_width = 320;
            m.nav_rail_width = 96;
            m.corner_radius_window = 0;
            m.corner_radius_card = static_cast<int>(radii::r_lg);
            m.corner_radius_sheet = static_cast<int>(radii::r_xl);
            m.focus_ring_px = 3;
            m.edge_to_edge_windows = true;
            m.show_window_controls = false;
            m.use_bottom_sheets = false;
            m.dpad_focus_navigation = true;
            m.virtual_desktop_count = 1;
            break;
    }

    return m;
}

}  // namespace finn::ui

#endif  // FINN_UI_FINN_FORM_FACTOR_HPP
