// Motion timing curves and spring-like animation primitives for libfinnui.
// Defines standardized transition durations and cubic easing curves that respect
// the user's reduced-motion preference across all animated UI elements.

#ifndef FINN_UI_ANIMATION_HPP
#define FINN_UI_ANIMATION_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "../../common/paths.hpp"

/*
 * The motion language.
 *
 * These are the base durations and curves the UI uses. A control settles
 * quickly, a surface enters with the standard duration, and the dock has one
 * longer entrance. Entrances use ease-out so the last pixels settle; state
 * changes use an interruptible approach or spring (see finn/spring.hpp), so a
 * pointer or gesture that reverses halfway through does not jump.
 *
 * Reduced motion is read from FINN_REDUCED_MOTION or the FinnOS settings file
 * (FINN_SETTINGS_PATH, $HOME/.config/finn/settings, /var/lib/finn/settings)
 * with a short cache TTL so live Settings changes take effect without restart.
 */

namespace finn::ui {

inline constexpr double kMotionFast = 0.14;
inline constexpr double kMotionStandard = 0.22;
inline constexpr double kMotionSlow = 0.32;
inline constexpr double kMotionReducedCrossfade = 0.08;

namespace detail {

inline std::string trim_ws(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

inline bool parse_settings_bool(std::string_view s) {
    const std::string t = trim_ws(s);
    return t == "1" || t == "true" || t == "TRUE" || t == "yes";
}

inline std::string resolve_settings_file_path() {
    if (const char* override_path = std::getenv("FINN_SETTINGS_PATH");
        override_path && *override_path) {
        return override_path;
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        const std::string user_path = std::string(home) + "/" + finn::kUserSettingsRel;
        std::ifstream test_user(user_path);
        if (test_user.good()) return user_path;
    }
    return finn::kSettingsPath;
}

inline bool read_reduced_motion_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool saw = false;
    bool value = false;
    while (std::getline(in, line)) {
        const std::string t = trim_ws(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.rfind("reduced_motion=", 0) == 0) {
            value = parse_settings_bool(t.substr(15));
            saw = true;
        }
    }
    return saw && value;
}

struct ReducedMotionCache {
    std::mutex mu;
    std::optional<bool> override_value;
    std::string cached_path;
    bool cached_file_value = false;
    bool has_cached = false;
    std::chrono::steady_clock::time_point last_checked{};
};

inline ReducedMotionCache& reduced_motion_cache() {
    static ReducedMotionCache cache;
    return cache;
}

}  // namespace detail

/* Read reduced_motion directly from the merged settings file without caching. */
inline bool read_reduced_motion_setting() {
    return detail::read_reduced_motion_file(detail::resolve_settings_file_path());
}

/* Override reduced motion in-process (e.g. from FinnThemeManager or tests).
 * Pass std::nullopt to clear the override and resume reading env/settings. */
inline void set_reduced_motion_override(std::optional<bool> enabled) {
    auto& cache = detail::reduced_motion_cache();
    std::lock_guard<std::mutex> lock(cache.mu);
    cache.override_value = enabled;
}

inline void clear_reduced_motion_override() {
    set_reduced_motion_override(std::nullopt);
}

/* Force immediate re-read of the settings file on next reduced_motion() call. */
inline void refresh_reduced_motion() {
    auto& cache = detail::reduced_motion_cache();
    std::lock_guard<std::mutex> lock(cache.mu);
    cache.has_cached = false;
}

inline bool reduced_motion() {
    if (const char* value = std::getenv("FINN_REDUCED_MOTION");
        value != nullptr && *value != '\0' && *value != '0') {
        return true;
    }

    auto& cache = detail::reduced_motion_cache();
    std::lock_guard<std::mutex> lock(cache.mu);
    if (cache.override_value.has_value()) {
        return *cache.override_value;
    }

    const std::string path = detail::resolve_settings_file_path();
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kTtl = std::chrono::milliseconds(250);
    if (!cache.has_cached || cache.cached_path != path ||
        (now - cache.last_checked) >= kTtl) {
        cache.cached_path = path;
        cache.cached_file_value = detail::read_reduced_motion_file(path);
        cache.last_checked = now;
        cache.has_cached = true;
    }
    return cache.cached_file_value;
}

inline double clamp_unit(double value) {
    return std::max(0.0, std::min(1.0, value));
}

/* Unit cubic Bezier curve P0=(0,0), P1=(x1,y1), P2=(x2,y2), P3=(1,1). */
struct CubicBezier {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 1.0;
    double y2 = 1.0;

    static constexpr double sample(double a1, double a2, double u) {
        const double inv = 1.0 - u;
        return 3.0 * inv * inv * u * a1 + 3.0 * inv * u * u * a2 + u * u * u;
    }

    static constexpr double slope(double a1, double a2, double u) {
        const double inv = 1.0 - u;
        return 3.0 * inv * inv * a1 + 6.0 * inv * u * (a2 - a1) +
               3.0 * u * u * (1.0 - a2);
    }

    double evaluate(double t) const {
        if (t <= 0.0) return 0.0;
        if (t >= 1.0) return 1.0;
        double u = t;
        for (int i = 0; i < 6; ++i) {
            const double err = sample(x1, x2, u) - t;
            if (std::abs(err) < 1e-7) return sample(y1, y2, u);
            const double d = slope(x1, x2, u);
            if (std::abs(d) < 1e-6) break;
            u = std::clamp(u - err / d, 0.0, 1.0);
        }
        double lo = 0.0;
        double hi = 1.0;
        u = t;
        for (int i = 0; i < 24; ++i) {
            const double x = sample(x1, x2, u);
            if (std::abs(x - t) < 1e-7) break;
            if (x < t) lo = u;
            else hi = u;
            u = 0.5 * (lo + hi);
        }
        return sample(y1, y2, u);
    }
};

inline constexpr CubicBezier kCurveOut{0.333333, 1.0, 0.666667, 1.0};
inline constexpr CubicBezier kCurveStandardOut{0.22, 1.0, 0.36, 1.0};
inline constexpr CubicBezier kCurveInOut{0.65, 0.0, 0.35, 1.0};
inline constexpr CubicBezier kCurveOutBack{0.34, 1.36, 0.64, 1.0};
inline constexpr CubicBezier kCurveSharp{0.40, 0.0, 0.20, 1.0};

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

/* Analytic damped harmonic oscillator spring for fluid UI motion. */
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
        if (reduced_motion()) {
            set(target);
            return value;
        }
        if (dt <= 0.0 || settled()) {
            if (settled()) set(target);
            return value;
        }
        const double m = std::max(1e-4, mass);
        const double k = std::max(1e-4, stiffness);
        const double c = std::max(0.0, damping);
        const double omega0 = std::sqrt(k / m);
        const double zeta = c / (2.0 * std::sqrt(k * m));
        const double x0 = value - target;
        const double v0 = velocity;

        double x = 0.0;
        double v = 0.0;
        if (std::abs(zeta - 1.0) <= 1e-6) {
            const double decay = std::exp(-omega0 * dt);
            const double c2 = v0 + omega0 * x0;
            x = decay * (x0 + c2 * dt);
            v = decay * (v0 - omega0 * c2 * dt);
        } else if (zeta < 1.0) {
            const double wd = omega0 * std::sqrt(1.0 - zeta * zeta);
            const double decay = std::exp(-zeta * omega0 * dt);
            const double a = x0;
            const double b = (v0 + zeta * omega0 * x0) / wd;
            const double cos_t = std::cos(wd * dt);
            const double sin_t = std::sin(wd * dt);
            x = decay * (a * cos_t + b * sin_t);
            v = decay * (v0 * cos_t -
                         ((zeta * omega0 * v0 + omega0 * omega0 * x0) / wd) * sin_t);
        } else {
            const double s = omega0 * std::sqrt(zeta * zeta - 1.0);
            const double r1 = -omega0 * zeta + s;
            const double r2 = -omega0 * zeta - s;
            const double c1 = (v0 - r2 * x0) / (r1 - r2);
            const double c2 = x0 - c1;
            const double e1 = std::exp(r1 * dt);
            const double e2 = std::exp(r2 * dt);
            x = c1 * e1 + c2 * e2;
            v = c1 * r1 * e1 + c2 * r2 * e2;
        }

        value = target + x;
        velocity = v;
        if (settled()) {
            set(target);
        }
        return value;
    }
};

}  // namespace finn::ui

#endif
