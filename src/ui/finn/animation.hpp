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

/* Gentle overshoot for popovers: fast arrival, small settle past target.
 * Capped so layout never depends on the overshoot. */
inline double ease_out_back(double t) {
    t = clamp_unit(t);
    constexpr double c1 = 1.30158;
    constexpr double c3 = c1 + 1.0;
    const double u = t - 1.0;
    return 1.0 + c3 * u * u * u + c1 * u * u;
}

/* Staggered grid entrance: item `index` of `count` starts after
 * `index * step` of the `progress` range. Returns 0..1 local progress.
 * Pure: same layout, only timing differs. */
inline double stagger(double progress, int index, int count, double step = 0.06) {
    if (count <= 1) return clamp_unit(progress);
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    const double start = index * step;
    const double span = 1.0 - start;
    if (span <= 0.0) return progress >= 1.0 ? 1.0 : 0.0;
    return clamp_unit((progress - start) / span);
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

    double step(double dt) {
        if (!running()) {
            value = target;
            return value;
        }
        elapsed += std::max(0.0, dt);
        const double t = clamp_unit(elapsed / duration);
        value = from + (target - from) * ease_out_cubic(t);
        if (elapsed >= duration) {
            value = target;
            duration = 0.0;
        }
        return value;
    }

    double update(double dt) { return step(dt); }
};

/* Damped harmonic oscillator physics spring for fluid UI motion. */
struct SpringValue {
    double value = 0.0;
    double velocity = 0.0;
    double target = 0.0;
    double stiffness = 320.0;
    double damping = 26.0;
    double mass = 1.0;

    void set(double v) {
        value = target = v;
        velocity = 0.0;
    }

    void to(double v) {
        if (reduced_motion()) {
            set(v);
            return;
        }
        target = v;
    }

    bool settled() const {
        return std::abs(target - value) < 1e-4 && std::abs(velocity) < 1e-3;
    }

    double step(double dt) {
        if (reduced_motion() || dt <= 0.0) {
            if (reduced_motion()) set(target);
            return value;
        }
        const double clamped_dt = std::min(dt, 0.033);
        const double force = -stiffness * (value - target);
        const double damper = -damping * velocity;
        const double accel = (force + damper) / std::max(0.1, mass);
        velocity += accel * clamped_dt;
        value += velocity * clamped_dt;
        if (settled()) {
            set(target);
        }
        return value;
    }
};

}  // namespace finn::ui

#endif
