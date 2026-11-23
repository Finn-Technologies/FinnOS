// KWin-inspired pure C++20 global shortcut, touch gesture, TV D-pad, and
// session-lock input router for finn-comp.

#ifndef FINN_COMP_CORE_INPUT_ROUTER_HPP
#define FINN_COMP_CORE_INPUT_ROUTER_HPP

#include <cstdint>

namespace finn::comp {

enum ModBits : std::uint32_t {
    ModNone  = 0,
    ModShift = 1u << 0,
    ModCtrl  = 1u << 1,
    ModAlt   = 1u << 2,
    ModSuper = 1u << 3,
};

enum class ShortcutAction {
    None,
    RequestLock,          // Super+L (or idle timeout)
    ToggleLauncher,       // Super tap
    ToggleSearch,         // Alt+Space
    ToggleOverview,       // Super+Tab or Super+W
    CycleFocusForward,    // Alt+Tab or Alt+F1
    CycleFocusBackward,   // Alt+Shift+Tab
    SnapLeft,             // Super+Left
    SnapRight,            // Super+Right
    ToggleMaximize,       // Super+Up
    ToggleShowDesktop,    // Super+D
    CloseFocused,         // Alt+F4 or Super+Q
    SwitchDesktopPrev,    // Ctrl+Alt+Left
    SwitchDesktopNext,    // Ctrl+Alt+Right
    SwitchVT2,            // Ctrl+Alt+F2
    EdgeSwipeHome,        // Bottom-edge swipe up (Mobile/Tablet)
    EdgeSwipeQuickSettings, // Top-right swipe down (Mobile/Tablet)
    EdgeSwipeBack,        // Left-edge swipe right (Mobile)
};

// XKB keysym constants kept local so pure host unit tests need no libxkbcommon headers.
namespace keys {
inline constexpr std::uint32_t kKey_l      = 0x006c;
inline constexpr std::uint32_t kKey_L      = 0x004c;
inline constexpr std::uint32_t kKey_q      = 0x0071;
inline constexpr std::uint32_t kKey_d      = 0x0064;
inline constexpr std::uint32_t kKey_w      = 0x0077;
inline constexpr std::uint32_t kKey_space  = 0x0020;
inline constexpr std::uint32_t kKey_Tab    = 0xff09;
inline constexpr std::uint32_t kKey_Left   = 0xff51;
inline constexpr std::uint32_t kKey_Up     = 0xff52;
inline constexpr std::uint32_t kKey_Right  = 0xff53;
inline constexpr std::uint32_t kKey_F1     = 0xffbe;
inline constexpr std::uint32_t kKey_F2     = 0xffbf;
inline constexpr std::uint32_t kKey_F4     = 0xffc1;
}  // namespace keys

class InputRouter {
public:
    ShortcutAction match_key(std::uint32_t mods, std::uint32_t keysym,
                             bool session_locked) const noexcept;

    ShortcutAction recognize_edge_swipe(double start_x, double start_y,
                                        double end_x, double end_y,
                                        int screen_w, int screen_h,
                                        bool session_locked) const noexcept;

    ShortcutAction recognize_trackpad_swipe(int fingers,
                                            double dx, double dy,
                                            bool session_locked) const noexcept;

    ShortcutAction recognize_trackpad_pinch(int fingers,
                                            double scale,
                                            bool session_locked) const noexcept;

    // Returns true if idle duration exceeded auto_lock_minutes (>0).
    static bool should_auto_lock(std::uint64_t now_msec,
                                 std::uint64_t last_activity_msec,
                                 int auto_lock_minutes,
                                 bool already_locked) noexcept;
};

}  // namespace finn::comp

#endif  // FINN_COMP_CORE_INPUT_ROUTER_HPP
