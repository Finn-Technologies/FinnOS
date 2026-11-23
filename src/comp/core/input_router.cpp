#include "comp/core/input_router.hpp"

#include <cmath>

namespace finn::comp {

ShortcutAction InputRouter::match_key(std::uint32_t mods, std::uint32_t keysym,
                                      bool session_locked) const noexcept {
    // Rescue console Ctrl+Alt+F2 is always permitted even when locked.
    if ((mods & (ModCtrl | ModAlt)) == (ModCtrl | ModAlt) && keysym == keys::kKey_F2) {
        return ShortcutAction::SwitchVT2;
    }

    // Strict security invariant: gate ALL WM shortcuts while session-locked.
    if (session_locked) {
        return ShortcutAction::None;
    }

    const bool has_super = (mods & ModSuper) != 0;
    const bool has_alt   = (mods & ModAlt) != 0;
    const bool has_ctrl  = (mods & ModCtrl) != 0;
    const bool has_shift = (mods & ModShift) != 0;

    if (has_super && !has_alt && !has_ctrl) {
        if (keysym == keys::kKey_l || keysym == keys::kKey_L) return ShortcutAction::RequestLock;
        if (keysym == keys::kKey_q) return ShortcutAction::CloseFocused;
        if (keysym == keys::kKey_d) return ShortcutAction::ToggleShowDesktop;
        if (keysym == keys::kKey_w || keysym == keys::kKey_Tab) return ShortcutAction::ToggleOverview;
        if (keysym == keys::kKey_Left) return ShortcutAction::SnapLeft;
        if (keysym == keys::kKey_Right) return ShortcutAction::SnapRight;
        if (keysym == keys::kKey_Up) return ShortcutAction::ToggleMaximize;
    }

    if (has_alt && !has_ctrl && !has_super) {
        if (keysym == keys::kKey_space) return ShortcutAction::ToggleSearch;
        if (keysym == keys::kKey_F4) return ShortcutAction::CloseFocused;
        if (keysym == keys::kKey_F1) return ShortcutAction::CycleFocusForward;
        if (keysym == keys::kKey_Tab) {
            return has_shift ? ShortcutAction::CycleFocusBackward
                             : ShortcutAction::CycleFocusForward;
        }
    }

    if (has_ctrl && has_alt && !has_super) {
        if (keysym == keys::kKey_Left) return ShortcutAction::SwitchDesktopPrev;
        if (keysym == keys::kKey_Right) return ShortcutAction::SwitchDesktopNext;
    }

    return ShortcutAction::None;
}

ShortcutAction InputRouter::recognize_edge_swipe(double start_x, double start_y,
                                                 double end_x, double end_y,
                                                 int screen_w, int screen_h,
                                                 bool session_locked) const noexcept {
    if (session_locked || screen_w <= 0 || screen_h <= 0) {
        return ShortcutAction::None;
    }
    const double dx = end_x - start_x;
    const double dy = end_y - start_y;
    constexpr double kEdgeZone = 28.0;
    constexpr double kMinSwipe = 56.0;

    // Bottom edge swipe up -> Home / Task Switcher
    if (start_y >= screen_h - kEdgeZone && dy <= -kMinSwipe && std::abs(dy) > std::abs(dx)) {
        return ShortcutAction::EdgeSwipeHome;
    }
    // Top-right edge swipe down -> Control Center / Quick Settings
    if (start_y <= kEdgeZone && start_x >= screen_w * 0.5 &&
        dy >= kMinSwipe && std::abs(dy) > std::abs(dx)) {
        return ShortcutAction::EdgeSwipeQuickSettings;
    }
    // Left edge swipe right -> Back
    if (start_x <= kEdgeZone && dx >= kMinSwipe && std::abs(dx) > std::abs(dy)) {
        return ShortcutAction::EdgeSwipeBack;
    }
    return ShortcutAction::None;
}

ShortcutAction InputRouter::recognize_trackpad_swipe(int fingers,
                                                     double dx, double dy,
                                                     bool session_locked) const noexcept {
    if (session_locked || fingers < 3) return ShortcutAction::None;
    constexpr double kMinDelta = 42.0;
    const double ax = std::abs(dx);
    const double ay = std::abs(dy);
    if (ax < kMinDelta && ay < kMinDelta) return ShortcutAction::None;

    if (fingers == 3) {
        if (ay >= ax) {
            return (dy < 0.0) ? ShortcutAction::ToggleOverview
                              : ShortcutAction::ToggleShowDesktop;
        }
        return (dx < 0.0) ? ShortcutAction::SwitchDesktopNext
                          : ShortcutAction::SwitchDesktopPrev;
    }
    if (fingers >= 4) {
        if (ay >= ax) {
            return (dy < 0.0) ? ShortcutAction::ToggleLauncher
                              : ShortcutAction::ToggleShowDesktop;
        }
        return (dx < 0.0) ? ShortcutAction::CycleFocusForward
                          : ShortcutAction::CycleFocusBackward;
    }
    return ShortcutAction::None;
}

ShortcutAction InputRouter::recognize_trackpad_pinch(int fingers,
                                                     double scale,
                                                     bool session_locked) const noexcept {
    if (session_locked || fingers < 3) return ShortcutAction::None;
    if (scale <= 0.78) return ShortcutAction::ToggleOverview;
    if (scale >= 1.25) return ShortcutAction::ToggleShowDesktop;
    return ShortcutAction::None;
}

bool InputRouter::should_auto_lock(std::uint64_t now_msec,
                                   std::uint64_t last_activity_msec,
                                   int auto_lock_minutes,
                                   bool already_locked) noexcept {
    if (already_locked || auto_lock_minutes <= 0 || now_msec <= last_activity_msec) {
        return false;
    }
    const std::uint64_t threshold_ms =
        static_cast<std::uint64_t>(auto_lock_minutes) * 60ULL * 1000ULL;
    return (now_msec - last_activity_msec) >= threshold_ms;
}

}  // namespace finn::comp
