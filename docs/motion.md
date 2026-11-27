# Motion

How surfaces move in FinnOS, driven by the analytic spring solver in `src/ui/finn/spring.hpp` (`<FinnSDK/Spring.hpp>`) and exposed to QML via `FinnSpringValue` (`FinnUI 1.0`).

## Spring presets (`finn::ui::springs`)

All spring motion uses a closed-form damped harmonic oscillator (`SpringAnimator`, `SpringAnimator2D`) — never Euler integration — so motion is frame-rate independent across 60 Hz and 120 Hz outputs and retargets cleanly mid-flight with preserved velocity:

| Preset | Character | Typical use |
|---|---|---|
| `kSnappy` | Fast, critically damped | Button press/release (`PressFeedback`), toggle thumbs, focus transitions |
| `kSmooth` | Balanced, gentle settle | Popover open/close, window resize/maximize, workspace switches |
| `kBouncy` | Slight overshoot | Dock/notification entrance, interactive physics gestures |
| `kGentle` | Longer settle | Sheet and overview transitions |
| `kOverscroll` | Rubber-band return (`rubber_band`) | Scroll edge resistance and gesture release |

See [docs/motion-spec.md](motion-spec.md) for the exact stiffness, damping, and settling mathematics.

## Per-surface motion

| Surface | Behaviour | Implementation |
|---|---|---|
| Compositor windows & workspaces | 120 Hz (60 Hz on software backend) analytic spring transitions for snap, maximize, minimize, virtual desktop switch, and Overview grid | `src/comp/core/workspace_model.cpp`, `src/comp/qml/WaylandCompositorScene.qml` |
| Desktop dock & popovers | Frosted 4-pill dock with spring hover/active indicators; Home Menu, Spotlight, Control Center, Notification Center, and Overview popovers | `src/shell/ui/desktop/*.qml` |
| Notifications | Receding 3-card stack with smooth entrance/dismissal | `src/shell/ui/common/NotificationStack.qml` |
| Native QML apps | `FinnSpringValue`, `FinnButton`, `FinnToggle`, `FinnNavItem`, and `FinnApplicationWindow` transitions | `src/ui/qml/finn_qml_types.cpp`, `src/ui/qml/controls/*.qml` |
| First-time setup (`finn-oobe`) | Ribbon "hi." intro over the 72 × 72 px Finn mark, slide transitions, interactive 3D pendant lamp cord physics, calm minimal completion screen | `apps/oobe/main.cpp`, `apps/oobe/lamp_widget.hpp`, `src/ui/finn/components.cpp` |
| Recovery Mode (`recovery-mode`) | Matches OOBE's slide transitions (`OobeSlideTransitionOverlay`), progressive list blur, and calm Finn-mark completion screen | `apps/recovery-mode/main.cpp` |

## Reduced motion

Reduced motion is a live setting (`reduced_motion=1` in `~/.config/finn/settings` or `FINN_REDUCED_MOTION=1` in the environment), watched without restarting apps:
- `libfinnplatformtheme` exposes `QPlatformTheme::motionPreference()`.
- `SpringAnimator` and `ReducedCrossfade` snap or crossfade cleanly when reduced motion is active.
- `FinnProgressRing` renders a static ring instead of spinning continuously.
