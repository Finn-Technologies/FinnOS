// Pure C++20 ASan/UBSan unit tests for finn-comp core:
// SceneStack, WorkspaceModel (Desktop/Tablet/Mobile/TV policies, virtual
// desktops, snapping, Overview, spring physics), and InputRouter (shortcuts,
// edge gestures, auto-lock, and session-lock security gating).

#include "comp/core/input_router.hpp"
#include "comp/core/scene_stack.hpp"
#include "comp/core/workspace_model.hpp"

#include <cmath>
#include <cstdio>

namespace {

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,    \
                         __LINE__, #cond);                                 \
            return false;                                                  \
        }                                                                  \
    } while (false)

bool test_scene_stack_order_and_lock() {
    using finn::comp::SceneLayer;
    CHECK(finn::comp::layer_z_base(SceneLayer::Background) <
          finn::comp::layer_z_base(SceneLayer::LayerBackground));
    CHECK(finn::comp::layer_z_base(SceneLayer::LayerBackground) <
          finn::comp::layer_z_base(SceneLayer::LayerBottom));
    CHECK(finn::comp::layer_z_base(SceneLayer::LayerBottom) <
          finn::comp::layer_z_base(SceneLayer::Toplevels));
    CHECK(finn::comp::layer_z_base(SceneLayer::Toplevels) <
          finn::comp::layer_z_base(SceneLayer::LayerTop));
    CHECK(finn::comp::layer_z_base(SceneLayer::LayerTop) <
          finn::comp::layer_z_base(SceneLayer::Fullscreen));
    CHECK(finn::comp::layer_z_base(SceneLayer::Fullscreen) <
          finn::comp::layer_z_base(SceneLayer::LayerOverlay));
    CHECK(finn::comp::layer_z_base(SceneLayer::LayerOverlay) <
          finn::comp::layer_z_base(SceneLayer::SessionLock));

    CHECK(!finn::comp::layer_accepts_input(SceneLayer::Toplevels, true));
    CHECK(!finn::comp::layer_accepts_input(SceneLayer::LayerTop, true));
    CHECK(finn::comp::layer_accepts_input(SceneLayer::SessionLock, true));
    CHECK(finn::comp::layer_accepts_input(SceneLayer::Toplevels, false));
    return true;
}

bool test_workspace_desktop_and_virtual_desktops() {
    finn::comp::WorkspaceModel ws;
    CHECK(ws.form_factor() == finn::ui::AdaptiveFormFactor::Desktop);
    CHECK(ws.desktop_count() == 4);
    CHECK(ws.current_desktop() == 0);

    const auto w1 = ws.add_window("First", 700, 480);
    const auto w2 = ws.add_window("Second", 700, 480);
    CHECK(ws.focused_window_id() == w2);

    ws.cycle_focus(true);
    CHECK(ws.focused_window_id() == w1);

    ws.snap_window(w1, finn::comp::SnapEdge::Left);
    CHECK(ws.find_window(w1)->snap == finn::comp::SnapEdge::Left);
    CHECK(ws.find_window(w1)->target_geometry.x == 0);
    CHECK(ws.find_window(w1)->target_geometry.width < 700);

    ws.toggle_overview();
    CHECK(ws.overview_active());
    CHECK(ws.find_window(w1)->overview_geometry.width > 100);
    CHECK(ws.find_window(w2)->overview_geometry.x != ws.find_window(w1)->overview_geometry.x);
    ws.exit_overview(w2);
    CHECK(!ws.overview_active());
    CHECK(ws.focused_window_id() == w2);

    // Step springs to settle
    for (int i = 0; i < 200; ++i) ws.step_animations(0.008);
    CHECK(std::fabs(ws.find_window(w2)->spring_opacity.value() - 1.0) < 0.01);

    ws.switch_desktop(1);
    CHECK(ws.current_desktop() == 1);
    CHECK(ws.focused_window_id() == 0);
    ws.switch_desktop(0);
    CHECK(ws.focused_window_id() == w2);
    return true;
}

bool test_workspace_adaptive_form_factors() {
    finn::comp::WorkspaceModel ws;

    // Tablet hybrid: 1st window fills usable area, 2nd triggers 50/50 split!
    ws.set_outputs({{1, "tablet-0", {0, 0, 900, 1200}, {0, 32, 900, 1100}, 2.0, 120000, false, true}});
    CHECK(ws.form_factor() == finn::ui::AdaptiveFormFactor::Tablet);
    const auto t1 = ws.add_window("Pane A", 500, 500);
    CHECK(ws.find_window(t1)->maximized);
    const auto t2 = ws.add_window("Pane B", 500, 500);
    CHECK(ws.find_window(t1)->snap == finn::comp::SnapEdge::Left);
    CHECK(ws.find_window(t2)->snap == finn::comp::SnapEdge::Right);

    // Mobile card stack: edge-to-edge fullscreen, 1 desktop
    ws.set_outputs({{1, "phone-0", {0, 0, 390, 844}, {0, 34, 390, 786}, 3.0, 120000, false, true}});
    CHECK(ws.form_factor() == finn::ui::AdaptiveFormFactor::Mobile);
    CHECK(ws.desktop_count() == 1);
    CHECK(ws.find_window(t2)->maximized);

    // TV 10-foot Bigscreen mode
    ws.set_outputs({{1, "hdmi-tv", {0, 0, 1920, 1080}, {0, 72, 1920, 1008}, 1.0, 60000, true, true}});
    CHECK(ws.form_factor() == finn::ui::AdaptiveFormFactor::TV);
    CHECK(ws.form_factor_metrics().dpad_focus_navigation);
    CHECK(ws.form_factor_metrics().type_scale == 1.5);
    return true;
}

bool test_input_router_and_lock_security() {
    finn::comp::InputRouter router;
    using namespace finn::comp;

    // Unlocked shortcuts
    CHECK(router.match_key(ModSuper, keys::kKey_l, false) == ShortcutAction::RequestLock);
    CHECK(router.match_key(ModAlt, keys::kKey_space, false) == ShortcutAction::ToggleSearch);
    CHECK(router.match_key(ModSuper, keys::kKey_Tab, false) == ShortcutAction::ToggleOverview);
    CHECK(router.match_key(ModAlt, keys::kKey_Tab, false) == ShortcutAction::CycleFocusForward);
    CHECK(router.match_key(ModAlt | ModShift, keys::kKey_Tab, false) == ShortcutAction::CycleFocusBackward);
    CHECK(router.match_key(ModCtrl | ModAlt, keys::kKey_Right, false) == ShortcutAction::SwitchDesktopNext);

    // Locked: ALL WM shortcuts MUST be blocked except rescue VT2
    CHECK(router.match_key(ModSuper, keys::kKey_q, true) == ShortcutAction::None);
    CHECK(router.match_key(ModAlt, keys::kKey_F4, true) == ShortcutAction::None);
    CHECK(router.match_key(ModAlt, keys::kKey_Tab, true) == ShortcutAction::None);
    CHECK(router.match_key(ModSuper, keys::kKey_d, true) == ShortcutAction::None);
    CHECK(router.match_key(ModCtrl | ModAlt, keys::kKey_F2, true) == ShortcutAction::SwitchVT2);

    // Edge gestures
    CHECK(router.recognize_edge_swipe(195, 840, 195, 700, 390, 844, false) ==
          ShortcutAction::EdgeSwipeHome);
    CHECK(router.recognize_edge_swipe(320, 10, 320, 120, 390, 844, false) ==
          ShortcutAction::EdgeSwipeQuickSettings);
    CHECK(router.recognize_edge_swipe(6, 400, 120, 400, 390, 844, false) ==
          ShortcutAction::EdgeSwipeBack);
    CHECK(router.recognize_edge_swipe(195, 840, 195, 700, 390, 844, true) ==
          ShortcutAction::None);

    // Multi-finger trackpad gestures (unlocked & locked gating)
    CHECK(router.recognize_trackpad_swipe(3, 0, -90, false) == ShortcutAction::ToggleOverview);
    CHECK(router.recognize_trackpad_swipe(3, 0, 90, false) == ShortcutAction::ToggleShowDesktop);
    CHECK(router.recognize_trackpad_swipe(3, -90, 0, false) == ShortcutAction::SwitchDesktopNext);
    CHECK(router.recognize_trackpad_swipe(3, 90, 0, false) == ShortcutAction::SwitchDesktopPrev);
    CHECK(router.recognize_trackpad_swipe(4, 0, -90, false) == ShortcutAction::ToggleLauncher);
    CHECK(router.recognize_trackpad_swipe(4, -90, 0, false) == ShortcutAction::CycleFocusForward);
    CHECK(router.recognize_trackpad_swipe(4, 90, 0, false) == ShortcutAction::CycleFocusBackward);
    CHECK(router.recognize_trackpad_swipe(3, 0, -90, true) == ShortcutAction::None);
    CHECK(router.recognize_trackpad_pinch(3, 0.65, false) == ShortcutAction::ToggleOverview);
    CHECK(router.recognize_trackpad_pinch(3, 1.35, false) == ShortcutAction::ToggleShowDesktop);
    CHECK(router.recognize_trackpad_pinch(3, 0.65, true) == ShortcutAction::None);

    // Auto-lock timer
    CHECK(!InputRouter::should_auto_lock(600000, 0, 0, false));       // 0 = never
    CHECK(!InputRouter::should_auto_lock(299000, 0, 5, false));       // < 5 min
    CHECK(InputRouter::should_auto_lock(300000, 0, 5, false));        // == 5 min
    CHECK(!InputRouter::should_auto_lock(999999, 0, 5, true));        // already locked
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_scene_stack_order_and_lock();
    ok &= test_workspace_desktop_and_virtual_desktops();
    ok &= test_workspace_adaptive_form_factors();
    ok &= test_input_router_and_lock_security();
    if (!ok) return 1;
    std::puts("test_compositor: ok");
    return 0;
}
