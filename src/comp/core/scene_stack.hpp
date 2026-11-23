// Strict 8-layer scene stack order for finn-comp (KWin / Plasma Wayland model).
// Every surface enters its designated layer; no surface ever bypasses the stack.

#ifndef FINN_COMP_CORE_SCENE_STACK_HPP
#define FINN_COMP_CORE_SCENE_STACK_HPP

#include <cstdint>

namespace finn::comp {

enum class SceneLayer : std::uint8_t {
    Background      = 0,  // Compositor solid backdrop / fallback color
    LayerBackground = 1,  // wlr-layer-shell background (wallpaper)
    LayerBottom     = 2,  // wlr-layer-shell bottom widgets
    Toplevels       = 3,  // Normal xdg_toplevel & Xwayland windows
    LayerTop        = 4,  // wlr-layer-shell top (Dock, StatusBar)
    Fullscreen      = 5,  // Active fullscreen toplevels (above dock/bars)
    LayerOverlay    = 6,  // wlr-layer-shell overlay (Popovers, Spotlight, Notifications)
    SessionLock     = 7,  // ext-session-lock-v1 (opaque lock surface, blocks all below)
};

// Monotonic Z-base per layer (leaves 1000 intra-layer stacking slots each).
constexpr int layer_z_base(SceneLayer layer) noexcept {
    return static_cast<int>(layer) * 1000;
}

constexpr const char* layer_name(SceneLayer layer) noexcept {
    switch (layer) {
        case SceneLayer::Background:      return "background";
        case SceneLayer::LayerBackground: return "layer-background";
        case SceneLayer::LayerBottom:     return "layer-bottom";
        case SceneLayer::Toplevels:       return "toplevels";
        case SceneLayer::LayerTop:        return "layer-top";
        case SceneLayer::Fullscreen:      return "fullscreen";
        case SceneLayer::LayerOverlay:    return "layer-overlay";
        case SceneLayer::SessionLock:     return "session-lock";
    }
    return "unknown";
}

// When session-locked, only SessionLock surfaces may receive pointer/keyboard input.
constexpr bool layer_accepts_input(SceneLayer layer, bool session_locked) noexcept {
    if (session_locked) {
        return layer == SceneLayer::SessionLock;
    }
    return layer != SceneLayer::Background;
}

}  // namespace finn::comp

#endif  // FINN_COMP_CORE_SCENE_STACK_HPP
