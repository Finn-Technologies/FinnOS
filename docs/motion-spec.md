# FinnOS Motion Specification (Phase 3)

Defines the analytic spring solver, motion tokens, per-surface transition rules, reduced-motion contract, frame-time budget, and integration points for FinnOS (`libfinnui`, `finn-comp`, `finn-shell`, and first-party apps).

> **Implementation status:** The pure-C++20 motion primitives live in [`src/ui/finn/animation.hpp`](../src/ui/finn/animation.hpp) and [`src/ui/finn/spring.hpp`](../src/ui/finn/spring.hpp) and are unit-tested on host (`make test-unit`). `libfinnui` widgets (`FinnCard`, `FinnButton`, `FinnIconButton`, `FinnToggle`, `FinnSlider`, `FinnNavItem`, `FinnToast`, `FinnPageStack`, `FinnDialog`, `FinnSidebar` overscroll) and `FinnWindow` (collapsible sidebar + window entrance) are wired via `FinnSpringDriver` in [`src/ui/finn/components.cpp`](../src/ui/finn/components.cpp) and [`src/ui/finn/window.cpp`](../src/ui/finn/window.cpp) (not compiled on macOS host because Qt6 is not installed here). `src/comp` and `src/shell` surfaces still run their existing `QVariantAnimation` / `wl_event_source_timer` timelines; their exact integration points are listed in Section 7.

---

## 1. Motion Tokens

### 1.1 Durations (`src/ui/finn/animation.hpp`)

| Token | Value | Use |
|---|---|---|
| `kMotionFast` | `0.14 s` (140 ms) | Hover tints, press down, toggles, sliders, menu close |
| `kMotionStandard` | `0.22 s` (220 ms) | Popover / menu open, page stack switches, window open |
| `kMotionSlow` | `0.32 s` (320 ms) | Dock entrance, large sheet / lock-screen fades |
| `kMotionReducedCrossfade` | `0.08 s` (80 ms) | Reduced-motion opacity crossfade (capped `<= 100 ms`) |

### 1.2 Cubic-Bezier Curves (`CubicBezier` in `src/ui/finn/animation.hpp`)

| Token | Control Points `(x1, y1, x2, y2)` | Equivalence / Use |
|---|---|---|
| `kCurveOut` | `(0.333333, 1.0, 0.666667, 1.0)` | Exact `ease_out_cubic(t) = 1 - (1 - t)^3`; linear-to-settle deceleration |
| `kCurveStandardOut` | `(0.22, 1.0, 0.36, 1.0)` | Crisp standard entrance where initial punch is desired |
| `kCurveInOut` | `(0.65, 0.0, 0.35, 1.0)` | Symmetric transitions (`ease_in_out_cubic`), boot/OOBE crossfades |
| `kCurveOutBack` | `(0.34, 1.36, 0.64, 1.0)` | Gentle popover overshoot (layout never depends on overshoot) |
| `kCurveSharp` | `(0.40, 0.0, 0.20, 1.0)` | Rapid exits and dismissals |

Additional curve primitives:
- `stagger(progress, index, count, step = 0.06)` — per-item normalized timing for grid entrances (`60 ms` offset at 1 s unit span).
- `approach(value, target, dt, duration)` — frame-rate-independent exponential smoothing (`tau = max(0.016, duration * 0.45)`).
- `elastic_release_curve(t)` — `1 - exp(-10t) * cos(2.5 * pi * t)` for normalized `[0, 1]` press-release micro-rebound (~1.8% overshoot, exact `0` at `t=0`, `1` at `t=1`).
- `rubber_band(offset, dimension, coefficient = 0.55)` — iOS-style resistance `copysign((|x| * d * c) / (d + c * |x|), offset)`, strictly monotonic and bounded in `(-dimension, +dimension)`.

### 1.3 Spring Presets (`finn::ui::springs` in `src/ui/finn/spring.hpp`)

All springs use the exact closed-form analytic damped harmonic oscillator (`SpringSolver::solve_analytic`) over `(stiffness k, damping c, mass m)` with natural frequency $\omega_0 = \sqrt{k / m}$ and damping ratio $\zeta = \frac{c}{2\sqrt{k m}}$:

| Preset | `stiffness` | `damping` | `mass` | $\omega_0$ (rad/s) | $\zeta$ | Regime | Role |
|---|---|---|---|---|---|---|---|
| `springs::kSnappy` | `484.0` | `44.0` | `1.0` | `22.0` | `1.000` | Critically damped (0 overshoot) | Toggles, sliders, press-down, snap sheets, quick popovers |
| `springs::kSmooth` | `324.0` | `36.0` | `1.0` | `18.0` | `1.000` | Critically damped (0 overshoot) | Window open/minimize/snap, page transitions, dock highlight |
| `springs::kBouncy` | `360.0` | `24.0` | `1.0` | `18.97` | `0.632` | Underdamped (~7.6% overshoot) | Dock icon bounce, elastic button release (`PressFeedback`) |
| `springs::kGentle` | `196.0` | `28.0` | `1.0` | `14.0` | `1.000` | Critically damped (0 overshoot) | Large overlays, dock entrance, lock-screen fade/settle |
| `springs::kOverscroll` | `280.0` | `32.0` | `1.0` | `16.73` | `0.956` | Slightly underdamped (taut return) | Scroll overscroll snap-back after `rubber_band` release |

---

## 2. Physics & Gesture Primitives (`src/ui/finn/spring.hpp`)

1. **Analytic Spring Solver (`SpringSolver::solve_analytic` & `SpringValue`):**
   Closed-form evaluation for underdamped ($\zeta < 1$), critically damped ($|\zeta - 1| \le 10^{-6}$), and overdamped ($\zeta > 1$) oscillators. Frame-rate independent and unconditionally stable for arbitrary `dt`.
2. **Interruptible Retargeting (`SpringAnimator`, `SpringAnimator2D`):**
   `retarget(new_target)` updates the equilibrium target while preserving the instantaneous position and velocity ($C^1$ continuity), eliminating jumps when a user reverses a pointer, popover, or window drag mid-flight.
3. **Rubber-Band Overscroll (`rubber_band`, `rubber_band_inverse`, `rubber_band_clamp`):**
   Hyperbolic compression past scroll/drag boundaries bounded strictly within `(-dimension, +dimension)` with invertible mapping (`rubber_band_inverse`) so re-grabbing a bouncing scroll view picks up seamlessly.
4. **Elastic Press Feedback (`press_down_scale`, `elastic_release_curve`, `PressFeedback`):**
   Press down tracks `springs::kSnappy` toward `0.96` without overshoot; release hands velocity to `springs::kBouncy` back to `1.0`.
5. **Gesture Progress Tracking & Fling Handoff (`GestureProgressTracker`, `project_fling`, `snap_target_from_fling`):**
   Tracks pointer/touch delta across `[min_val, max_val]`, applies optional `rubber_band_clamp` outside bounds, EMA-filters velocity, projects resting position using continuous exponential deceleration (`deceleration_rate = 0.998` per ms, $\lambda = -1000 \ln r$), and hands both position and velocity directly to a `SpringAnimator`.

---

## 3. Per-Surface Motion Specification

| Surface | Target Spring / Curve / Duration | Interruptible? | Gesture-driven? | Reduced-motion fallback (`<= 100 ms`, no bounce) | Current implementation status |
|---|---|---|---|---|---|
| **App launch / close** | `springs::kSmooth` (scale `0.96->1.0`, +14 px slide) on map; `kCurveSharp` / `kMotionFast` (140 ms) opacity fade on close | Yes (`SpringAnimator2D`) | No (future: gesture minimize) | Instant map or `<= 80 ms` opacity crossfade (`ReducedCrossfade`) | Currently `ManagedWindow::start_animation` (150 ms cubic position slide on map; instant unmap) |
| **App switching (`Alt+F1`, Overview, Dock pill)** | `springs::kSnappy` for `OpenWindowsPopover` cards & `DockWindow` highlight pill | Yes (`SpringAnimator`) | No (future: swipe switch) | Instant focus / highlight jump | Currently `QVariantAnimation` (160 ms `OutCubic`) + instant focus |
| **Window open / minimize / snap** | `springs::kSmooth` (`x, y` position; minimize/restore 14 px vertical settle; half-snap geometry transition) | Yes (`SpringAnimator2D`) | Future (drag-to-snap edge) | Instant geometry/visibility snap (no slide) | Currently `start_animation` (140–150 ms `wl_event_loop` timer); `snap_half` is instant |
| **In-app transitions (`FinnPageStack`, sidebar, OOBE step)** | `springs::kSmooth` (8 px vertical cross-slide + outgoing 45% fade; `FinnWindow` sidebar width) | Yes (retarget page index / sidebar width) | Future (back-swipe) | Instant page/sidebar snap or `<= 80 ms` opacity crossfade | Wired in `StackTransitionOverlay` & `FinnWindow::setSidebarCollapsed` (`FinnSpringDriver` + `springs::kSmooth`); `browser_window` sidebar still uses `QPropertyAnimation` |
| **Overscroll scrolling** | `rubber_band(offset, viewport_dim, 0.55)` during drag + `springs::kOverscroll` on release (`project_fling`) | Yes (`GestureProgressTracker` + `rubber_band_inverse`) | Yes (wheel / touch / trackpad) | Hard clamp at scroll bounds (`rubber_band_for_motion(..., true) == 0`); no bounce | `install_overscroll_physics(QScrollArea*)` wired on `FinnSidebar` (`OverscrollFilter` + `rubber_band` + `springs::kOverscroll`); other `QScrollArea`s remain standard Qt |
| **Toggles, sliders, pressable buttons** | `FinnToggle` / `FinnSlider`: `springs::kSnappy`; buttons/tiles: `PressFeedback` (`0.95–0.985` down `kSnappy` / `kBouncy` release) | Yes (`FinnSpringDriver`) | Yes (`FinnSlider` 1:1 drag + spring set) | Instant state snap, zero bounce/scale overshoot | Wired in `FinnToggle`, `FinnSlider`, `FinnButton`, `FinnIconButton`, `FinnCard`, `FinnNavItem` via `FinnSpringDriver` |
| **Notifications & toasts** | `NotificationCard` / `FinnToast`: `springs::kSnappy` (slide in 8–12 px + opacity) | Yes (dismiss mid-entrance) | Future (swipe-to-dismiss) | Instant show/hide or `<= 80 ms` opacity crossfade | `FinnToast` wired (`springs::kSnappy` + `ReducedCrossfade` 80 ms); `NotificationOverlay` in `src/shell` currently snaps immediately |
| **Lock / unlock (`LockScreenWindow`)** | `springs::kGentle` (`ReducedCrossfade` / 320 ms scrim fade); failed-auth shake: `springs::kSnappy` damped lateral impulse | Yes | Future (drag-up unlock) | Instant lock/unlock cut or `<= 80 ms` fade; **no shake animation** on auth failure | Currently instant show/hide; `shakeInput()` uses 280 ms `QPropertyAnimation` (skipped when reduced) |
| **Boot & OOBE (`finnos` Plymouth, `FinnRibbonAnimation`)** | Boot: `finnos` Plymouth progress bar driven strictly by real Plymouth reports. OOBE step 0 / completion only: `FinnRibbonAnimation` (`Mode::HiIntro`, `Mode::SlinkyWave`) | Boot: N/A; OOBE fade: Yes | No | Boot: static mark + bar. OOBE: static `"hi."` mark + `<= 80 ms` UI fade (`reducedMotion`) | Implemented (`base/plymouth`, `src/ui/finn/components.cpp`) |
| **System dialogs (`FinnDialog::confirm`, `prompt`)** | `springs::kSnappy` (window opacity settle / `ReducedCrossfade`) | No (modal) | No | Instant appear/dismiss or `<= 80 ms` opacity crossfade | `FinnDialog::showEvent` wired (`springs::kSnappy` + `ReducedCrossfade` 80 ms) |

---

## 4. Reduced Motion Contract

- **Preference sources** (checked in order by `finn::ui::reduced_motion()` in `src/ui/finn/animation.hpp`):
  1. `FINN_REDUCED_MOTION` environment variable (non-empty and not `"0"`).
  2. In-process override (`set_reduced_motion_override()`, used by tests/theme manager).
  3. Merged settings file: `FINN_SETTINGS_PATH` (if set), else `$HOME/.config/finn/settings` (`kUserSettingsRel`), else `/var/lib/finn/settings` (`kSettingsPath`), reading `reduced_motion=1|true|yes`.
- **Live reload:** Settings -> Accessibility (`apps/settings/main.cpp`) writes `reduced_motion` via `FinnThemeManager::setReducedMotion()`, which updates `/var/lib/finn/settings` + `~/.config/finn/settings`. `finn-comp` polls every 1 s (`Server::read_reduced_motion_setting()`), `FinnThemeManager` watches via `QFileSystemWatcher`, and `finn::ui::reduced_motion()` refreshes on a 250 ms TTL (or immediately via `refresh_reduced_motion()`).
- **Behavioral guarantee:** When reduced motion is enabled, every `SpringAnimator`, `SpringValue`, `PressFeedback`, `Animation`, and `GestureProgressTracker` snaps immediately or uses `ReducedCrossfade` (`<= 100 ms` opacity-only). **Never** spring bounce, rubber-band overscroll, scale overshoot, or auth-error shake.

---

## 5. 60 / 120 fps Frame Budget & Measurement Method

### 5.1 Frame Budgets

| Refresh Target | Frame Interval Budget | Solver Allocation Target |
|---|---|---|
| **60 Hz** | `16.67 ms` (`16,667 µs`) | `< 50 µs` total for all active scene/UI springs |
| **120 Hz** | `8.33 ms` (`8,333 µs`) | `< 50 µs` total for all active scene/UI springs |

### 5.2 Headless Frame-Time Recorder Specification

`finn::ui::FrameTimeRecorder` (`src/ui/finn/spring.hpp`) defines the measurement interface for compositor and UI frame loops:
- Captures per-frame durations (`record_ms(double)` / `record_us(double)`) across a fixed animation window (e.g., 120–600 frames).
- Reports `count()`, `min_ms()`, `mean_ms()`, `percentile_ms(0.50)`, `percentile_ms(0.95)`, `percentile_ms(0.99)`, `max_ms()`, `dropped_60hz()` (`> 16.6667 ms`), and `dropped_120hz()` (`> 8.3333 ms`).
- **Headless compositor / shell measurement protocol (to run on target hardware / VM):**
  1. Instrument `wl_output` `frame` / commit timestamps in `finn-comp` (and `QWidget::paintEvent` / `QTimer` tick deltas in `finn-shell`) using `FrameTimeRecorder`.
  2. Exercise open/close, mid-flight retarget, popover open/close, and scroll fling scenarios for at least 300 frames each.
  3. Pass criteria (once measured on target device): `p95_ms <= budget`, `dropped_60hz == 0` (60 Hz target) or `dropped_120hz == 0` (120 Hz target).

### 5.3 Host Pure-C++ Solver Microbenchmark & Acceptance Status

- **Measured on macOS host (`clang++ -std=c++20`, `tests/unit/test_spring.cpp`):**
  100,000 analytic `SpringAnimator::step()` iterations with periodic mid-flight `retarget()` calls measured **`16.5 ns/step`** (`-O2`) and **`10.6 ns/step`** (`-O2 -fsanitize=address,undefined`), i.e. **`~0.017 µs/step`** (~`0.0002%` of an 8.33 ms 120 Hz budget per spring).
- **Acceptance bar status:** **UNMEASURED on target hardware.** Host CPU solver timing proves the analytic spring math is negligible, but full-frame 60/120 fps acceptance requires measuring Wayland surface commits, Qt rasterization, and DRM vblank pacing on the target device (not available on this macOS host).

---

## 6. Host Unit Test Command

Without touching shared `tests/unit/Makefile`, the motion unit tests compile and run standalone on host with:

```sh
clang++ -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Isrc/ui tests/unit/test_animation.cpp -o /tmp/test_animation && /tmp/test_animation && clang++ -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Isrc/ui tests/unit/test_spring.cpp -o /tmp/test_spring && /tmp/test_spring
```

---

## 7. Current Integration Points

`SpringAnimator`, `SpringAnimator2D`, `ReducedCrossfade`, `rubber_band`, and `PressFeedback` (`src/ui/finn/spring.hpp`) are now wired across the Qt 6 codebase:

| Area | Location | Current status |
|---|---|---|
| **Compositor windows & workspaces** | `src/comp/core/workspace_model.cpp`, `src/comp/qml/WaylandCompositorScene.qml` | **Wired** — 120 Hz (60 Hz on software backend) analytic spring transitions for snap, maximize, minimize, virtual desktops, and Overview |
| **QML `FinnUI 1.0` controls** | `src/ui/qml/finn_qml_types.cpp` (`FinnSpringValue`), `src/ui/qml/controls/*.qml` | **Wired** — `FinnSpringValue` drives smooth spring transitions in QML apps and `finn-shell` (`DesktopShell.qml`, `DesktopDock.qml`, popovers, `NotificationStack.qml`) |
| **C++ widget toolkit** | `src/ui/finn/components.cpp` (`FinnSpringDriver`, `StackTransitionOverlay`, `OverscrollFilter`, `FinnDialog`), `src/ui/finn/window.cpp` (`FinnWindow`) | **Wired** — `springs::kSnappy` / `kSmooth` / `kBouncy` / `kOverscroll` with live `ReducedCrossfade` fallbacks |
| **Theme reduced motion bridge** | `src/ui/finn/theme.cpp` (`FinnThemeManager`), `src/ui/qml/finn_qml_types.cpp` (`FinnThemeBridge`), `src/qtplugin/finn_platform_theme.cpp` | **Wired** — live `reduced_motion` setting updates without restarting apps |
