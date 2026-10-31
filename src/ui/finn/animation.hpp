// Motion timing curves and spring-like animation primitives for libfinnui.
// Defines standardized transition durations and cubic easing curves that respect
// the user's reduced-motion preference across all animated UI elements.

#ifndef FINN_UI_ANIMATION_HPP
#define FINN_UI_ANIMATION_HPP

#include <algorithm>
#include <cmath>
#include <cstdlib>

/*
 * The motion language.
 *
 * These are the only durations the UI uses. A control settles quickly, a
 * surface enters with the standard duration, and the dock has one longer
 * entrance. Entrances use ease-out so the last pixels settle; state changes
 * use an interruptible approach, so a pointer that reverses halfway through
 * a hover does not jump.
 *
 * FINN_REDUCED_MOTION=1 makes every animation instant. It is read once, so a
 * process start is the granularity, which is the right granularity for a
 * preference that should not change under a moving pointer.
 */

namespace finn::ui {

inline constexpr double kMotionFast = 0.14;
inline constexpr double kMotionStandard = 0.22;
inline constexpr double kMotionSlow = 0.32;

inline bool reduced_motion() {
    static const bool reduced = [] {
        const char* value = std::getenv("FINN_REDUCED_MOTION");
        return value != nullptr && *value != '\0' && *value != '0';
    }();
    return reduced;
}

inline double clamp_unit(double value) {
    return std::max(0.0, std::min(1.0, value));
}

inline double ease_out_cubic(double t) {
    t = clamp_unit(t);
    const double left = 1.0 - t;
    return 1.0 - left * left * left;
}

inline double ease_in_out_cubic(double t) {
    t = clamp_unit(t);
    if (t < 0.5) return 4.0 * t * t * t;
    const double left = -2.0 * t + 2.0;
    return 1.0 - (left * left * left) / 2.0;
}

/* Move `value` toward `target` with the same feel as a short animation, but
 * interruptible at any frame. `duration` is the time to get most of the way
 * there, not a hard stop. */
inline double approach(double value, double target, double dt, double duration) {
    if (reduced_motion() || duration <= 0.0) return target;
    if (dt <= 0.0) return value;
    const double tau = std::max(0.016, duration * 0.45);
    const double k = 1.0 - std::exp(-dt / tau);
    return value + (target - value) * k;
}

/* A one-shot animation from the current value to a target. */
struct Animation {
    double value = 0.0;
    double from = 0.0;
    double target = 0.0;
    double elapsed = 0.0;
    double duration = kMotionStandard;

    void set(double v) {
        value = from = target = v;
        elapsed = duration = 0.0;
    }

    void to(double v, double seconds = kMotionStandard) {
        if (reduced_motion() || seconds <= 0.0) {
            set(v);
            return;
        }
        if (std::abs(v - value) < 1e-6) {
            set(v);
            return;
        }
        from = value;
        target = v;
        duration = seconds;
        elapsed = 0.0;
    }

    bool running() const {
        return duration > 0.0 && elapsed < duration;
    }

    double update(double dt) {
        if (!running()) return value;
        elapsed = std::min(duration, elapsed + std::max(0.0, dt));
        const double k = elapsed / duration;
        value = from + (target - from) * ease_out_cubic(k);
        if (elapsed >= duration) value = target;
        return value;
    }
};

}  // namespace finn::ui

#endif
