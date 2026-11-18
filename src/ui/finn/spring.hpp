// Analytic damped-harmonic-oscillator spring solver, interruptible retargeting
// animators, iOS-style rubber-band overscroll, elastic press feedback,
// gesture progress tracking with fling projection, and frame-time recording.
// Pure C++20 (no Qt dependency) so host unit tests compile standalone.

#ifndef FINN_UI_SPRING_HPP
#define FINN_UI_SPRING_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "animation.hpp"

namespace finn::ui {

struct SpringParams {
    double stiffness = 324.0;
    double damping = 36.0;
    double mass = 1.0;

    double natural_frequency() const {
        const double m = std::max(1e-4, mass);
        const double k = std::max(1e-4, stiffness);
        return std::sqrt(k / m);
    }

    double damping_ratio() const {
        const double m = std::max(1e-4, mass);
        const double k = std::max(1e-4, stiffness);
        const double c = std::max(0.0, damping);
        return c / (2.0 * std::sqrt(k * m));
    }

    bool is_critically_damped(double tol = 1e-4) const {
        return std::abs(damping_ratio() - 1.0) <= tol;
    }

    bool is_underdamped(double tol = 1e-4) const {
        return damping_ratio() < 1.0 - tol;
    }

    bool is_overdamped(double tol = 1e-4) const {
        return damping_ratio() > 1.0 + tol;
    }

    static SpringParams critical(double stiffness_val, double mass_val = 1.0) {
        const double m = std::max(1e-4, mass_val);
        const double k = std::max(1e-4, stiffness_val);
        return SpringParams{k, 2.0 * std::sqrt(k * m), m};
    }

    static SpringParams from_response(double response_s, double zeta,
                                      double mass_val = 1.0) {
        constexpr double kTwoPi = 6.28318530717958647692;
        const double m = std::max(1e-4, mass_val);
        const double r = std::max(1e-3, response_s);
        const double w0 = kTwoPi / r;
        const double k = m * w0 * w0;
        const double c = 2.0 * std::max(0.0, zeta) * std::sqrt(k * m);
        return SpringParams{k, c, m};
    }
};

namespace springs {

// omega0 = 22 rad/s, zeta = 1.0 (critically damped, zero overshoot)
inline constexpr SpringParams kSnappy{484.0, 44.0, 1.0};
// omega0 = 18 rad/s, zeta = 1.0 (critically damped, zero overshoot)
inline constexpr SpringParams kSmooth{324.0, 36.0, 1.0};
// omega0 ~ 18.97 rad/s, zeta ~ 0.632 (controlled playful bounce)
inline constexpr SpringParams kBouncy{360.0, 24.0, 1.0};
// omega0 = 14 rad/s, zeta = 1.0 (calm entrance for larger surfaces)
inline constexpr SpringParams kGentle{196.0, 28.0, 1.0};
// omega0 ~ 16.73 rad/s, zeta ~ 0.956 (taut overscroll snap-back)
inline constexpr SpringParams kOverscroll{280.0, 32.0, 1.0};

}  // namespace springs

struct SpringState {
    double value = 0.0;
    double velocity = 0.0;
};

struct SpringSolver {
    static SpringState solve_analytic(double displacement0, double velocity0,
                                      double target, const SpringParams& params,
                                      double dt) {
        if (dt <= 0.0) {
            return SpringState{target + displacement0, velocity0};
        }

        const double m = std::max(1e-4, params.mass);
        const double k = std::max(1e-4, params.stiffness);
        const double c = std::max(0.0, params.damping);
        const double omega0 = std::sqrt(k / m);
        const double zeta = c / (2.0 * std::sqrt(k * m));
        const double x0 = displacement0;
        const double v0 = velocity0;

        double x = 0.0;
        double v = 0.0;

        if (std::abs(zeta - 1.0) <= 1e-6) {
            // Critically damped: x(t) = e^(-w0*t) * (x0 + (v0 + w0*x0)*t)
            const double decay = std::exp(-omega0 * dt);
            const double c2 = v0 + omega0 * x0;
            x = decay * (x0 + c2 * dt);
            v = decay * (v0 - omega0 * c2 * dt);
        } else if (zeta < 1.0) {
            // Underdamped oscillatory envelope
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
            // Overdamped dual-exponential decay
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

        return SpringState{target + x, v};
    }
};

/* Interruptible analytic spring animator. Retargeting mid-flight keeps both
 * current position and current velocity so direction reversals are C1-smooth. */
class SpringAnimator {
public:
    SpringAnimator() = default;
    explicit SpringAnimator(const SpringParams& params, double initial = 0.0)
        : value_(initial), target_(initial), params_(params) {}

    void set(double value, double velocity = 0.0) {
        value_ = value;
        target_ = value;
        velocity_ = velocity;
    }

    void retarget(double new_target) {
        if (is_reduced_motion()) {
            set(new_target, 0.0);
            return;
        }
        target_ = new_target;
    }

    void retarget(double new_target, const SpringParams& new_params) {
        params_ = new_params;
        retarget(new_target);
    }

    void to(double new_target) { retarget(new_target); }

    void set_params(const SpringParams& params) { params_ = params; }
    const SpringParams& params() const { return params_; }

    void set_velocity(double v) {
        velocity_ = is_reduced_motion() ? 0.0 : v;
    }

    void add_velocity(double dv) {
        if (!is_reduced_motion()) velocity_ += dv;
    }

    void set_thresholds(double value_eps, double velocity_eps) {
        value_eps_ = std::max(1e-9, value_eps);
        velocity_eps_ = std::max(1e-9, velocity_eps);
    }

    void set_reduced_motion_override(std::optional<bool> enabled) {
        reduced_override_ = enabled;
        if (is_reduced_motion()) {
            set(target_, 0.0);
        }
    }

    bool settled() const {
        return std::abs(target_ - value_) < value_eps_ &&
               std::abs(velocity_) < velocity_eps_;
    }

    bool running() const { return !settled(); }

    double value() const { return value_; }
    double velocity() const { return velocity_; }
    double target() const { return target_; }

    double step(double dt) {
        if (is_reduced_motion()) {
            set(target_, 0.0);
            return value_;
        }
        if (dt <= 0.0 || settled()) {
            if (settled()) set(target_, 0.0);
            return value_;
        }
        const SpringState next = SpringSolver::solve_analytic(
            value_ - target_, velocity_, target_, params_, dt);
        value_ = next.value;
        velocity_ = next.velocity;
        if (settled()) {
            set(target_, 0.0);
        }
        return value_;
    }

private:
    bool is_reduced_motion() const {
        return reduced_override_.has_value() ? *reduced_override_
                                             : reduced_motion();
    }

    double value_ = 0.0;
    double velocity_ = 0.0;
    double target_ = 0.0;
    SpringParams params_{springs::kSmooth};
    double value_eps_ = 1e-4;
    double velocity_eps_ = 1e-3;
    std::optional<bool> reduced_override_;
};

struct SpringAnimator2D {
    SpringAnimator x;
    SpringAnimator y;

    explicit SpringAnimator2D(const SpringParams& params = springs::kSmooth)
        : x(params), y(params) {}

    void set(double vx, double vy, double vel_x = 0.0, double vel_y = 0.0) {
        x.set(vx, vel_x);
        y.set(vy, vel_y);
    }

    void retarget(double tx, double ty) {
        x.retarget(tx);
        y.retarget(ty);
    }

    void retarget(double tx, double ty, const SpringParams& params) {
        x.retarget(tx, params);
        y.retarget(ty, params);
    }

    void set_velocity(double vx, double vy) {
        x.set_velocity(vx);
        y.set_velocity(vy);
    }

    void set_reduced_motion_override(std::optional<bool> enabled) {
        x.set_reduced_motion_override(enabled);
        y.set_reduced_motion_override(enabled);
    }

    void step(double dt) {
        x.step(dt);
        y.step(dt);
    }

    bool settled() const { return x.settled() && y.settled(); }
};

/* Crossfade helper capped at 100 ms for reduced-motion opacity transitions. */
struct ReducedCrossfade {
    double value = 1.0;
    double from = 1.0;
    double target = 1.0;
    double elapsed = 0.0;
    double duration = 0.0;

    void set(double alpha) {
        value = from = target = clamp_unit(alpha);
        elapsed = duration = 0.0;
    }

    void start(double from_alpha, double to_alpha,
               double seconds = kMotionReducedCrossfade) {
        from = clamp_unit(from_alpha);
        target = clamp_unit(to_alpha);
        duration = std::clamp(seconds, 0.0, 0.10);
        elapsed = 0.0;
        value = (duration <= 0.0) ? target : from;
    }

    bool running() const { return duration > 0.0 && elapsed < duration; }

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
};

/* iOS-style rubber-band overscroll resistance:
 * f(x, d) = (x * d * c) / (d + c * x), signed for negative offsets.
 * Strictly monotonic and bounded in (-dimension, +dimension). */
inline double rubber_band(double offset, double dimension,
                          double coefficient = 0.55) {
    if (dimension <= 0.0 || coefficient <= 0.0 || offset == 0.0) return 0.0;
    const double abs_x = std::abs(offset);
    const double mag =
        (abs_x * dimension * coefficient) / (dimension + coefficient * abs_x);
    return std::copysign(mag, offset);
}

inline double rubber_band_inverse(double visual_offset, double dimension,
                                  double coefficient = 0.55) {
    if (dimension <= 0.0 || coefficient <= 0.0 || visual_offset == 0.0) {
        return 0.0;
    }
    const double abs_y = std::min(std::abs(visual_offset), dimension * 0.999999);
    const double mag = (abs_y * dimension) / (coefficient * (dimension - abs_y));
    return std::copysign(mag, visual_offset);
}

inline double rubber_band_clamp(double value, double min_val, double max_val,
                                double dimension, double coefficient = 0.55) {
    if (min_val > max_val) std::swap(min_val, max_val);
    if (value < min_val) {
        return min_val + rubber_band(value - min_val, dimension, coefficient);
    }
    if (value > max_val) {
        return max_val + rubber_band(value - max_val, dimension, coefficient);
    }
    return value;
}

inline double rubber_band_for_motion(double offset, double dimension,
                                     bool reduced, double coefficient = 0.55) {
    return reduced ? 0.0 : rubber_band(offset, dimension, coefficient);
}

/* Elastic press feedback curves (normalized progress t in [0, 1]). */
inline double press_down_scale(double t, double min_scale = 0.96) {
    const double u = ease_out_cubic(clamp_unit(t));
    return 1.0 + (min_scale - 1.0) * u;
}

inline double elastic_release_curve(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    constexpr double kPi = 3.14159265358979323846;
    return 1.0 - std::exp(-10.0 * t) * std::cos(2.5 * kPi * t);
}

inline double elastic_press_scale(double release_t, double min_scale = 0.96) {
    return min_scale + (1.0 - min_scale) * elastic_release_curve(release_t);
}

class PressFeedback {
public:
    explicit PressFeedback(double min_scale = 0.96)
        : min_scale_(min_scale), animator_(springs::kSnappy, 1.0) {}

    void set_pressed(bool pressed) {
        if (pressed_ == pressed) return;
        pressed_ = pressed;
        if (pressed_) {
            animator_.retarget(min_scale_, springs::kSnappy);
        } else {
            animator_.retarget(1.0, springs::kBouncy);
        }
    }

    void set_reduced_motion_override(std::optional<bool> enabled) {
        animator_.set_reduced_motion_override(enabled);
    }

    double step(double dt) { return animator_.step(dt); }
    double scale() const { return animator_.value(); }
    double velocity() const { return animator_.velocity(); }
    bool settled() const { return animator_.settled(); }
    bool pressed() const { return pressed_; }

private:
    double min_scale_ = 0.96;
    bool pressed_ = false;
    SpringAnimator animator_;
};

/* Fling projection using continuous exponential velocity decay matching
 * UIScrollView deceleration rate (0.998 normal, 0.990 fast per millisecond). */
inline double project_fling(double position, double velocity,
                            double deceleration_rate = 0.998) {
    const double r = std::clamp(deceleration_rate, 0.8, 0.99999);
    const double lambda = -1000.0 * std::log(r);
    return position + velocity / lambda;
}

inline double snap_target_from_fling(double position, double velocity,
                                     double start, double end,
                                     double threshold = 0.5,
                                     double deceleration_rate = 0.998) {
    const double span = end - start;
    if (std::abs(span) < 1e-9) return end;
    const double projected = project_fling(position, velocity, deceleration_rate);
    const double normalized = (projected - start) / span;
    return (normalized >= threshold) ? end : start;
}

/* Tracks a pointer/touch drag across [min_value, max_value], optionally applies
 * rubber-band resistance beyond bounds, filters velocity, and hands both
 * position and velocity directly to a SpringAnimator on release. */
class GestureProgressTracker {
public:
    GestureProgressTracker() = default;
    GestureProgressTracker(double min_val, double max_val,
                           double rubber_band_dim = 0.0,
                           double coefficient = 0.55,
                           double deceleration_rate = 0.998)
        : min_val_(min_val),
          max_val_(max_val),
          rubber_band_dim_(std::max(0.0, rubber_band_dim)),
          coefficient_(coefficient),
          deceleration_rate_(deceleration_rate) {
        if (min_val_ > max_val_) std::swap(min_val_, max_val_);
    }

    void set_reduced_motion_override(std::optional<bool> enabled) {
        reduced_override_ = enabled;
    }

    void begin(double initial_position) {
        dragging_ = true;
        raw_pos_ = initial_position;
        velocity_ = 0.0;
        has_velocity_ = false;
        update_display_pos();
    }

    void update_delta(double delta, double dt) {
        if (!dragging_) return;
        raw_pos_ += delta;
        const double prev_display = display_pos_;
        update_display_pos();
        if (dt > 1e-5) {
            const double inst_v = (display_pos_ - prev_display) / dt;
            if (!has_velocity_) {
                velocity_ = inst_v;
                has_velocity_ = true;
            } else {
                // Exponential moving average to damp single-event touch jitter.
                velocity_ = 0.6 * inst_v + 0.4 * velocity_;
            }
        }
    }

    void update_position(double raw_position, double dt) {
        update_delta(raw_position - raw_pos_, dt);
    }

    void handoff(SpringAnimator& animator, double target) {
        dragging_ = false;
        const bool reduced = reduced_override_.has_value()
                                 ? *reduced_override_
                                 : reduced_motion();
        if (reduced) {
            animator.set(target, 0.0);
            return;
        }
        animator.set(display_pos_, velocity_);
        animator.retarget(target);
    }

    double handoff_projected(SpringAnimator& animator,
                             double threshold = 0.5) {
        const double target = snap_target_from_fling(
            display_pos_, velocity_, min_val_, max_val_, threshold,
            deceleration_rate_);
        handoff(animator, target);
        return target;
    }

    double progress() const {
        const double span = max_val_ - min_val_;
        if (span <= 1e-9) return 0.0;
        return (display_pos_ - min_val_) / span;
    }

    double position() const { return display_pos_; }
    double raw_position() const { return raw_pos_; }
    double velocity() const { return velocity_; }
    bool dragging() const { return dragging_; }

private:
    void update_display_pos() {
        const bool reduced = reduced_override_.has_value()
                                 ? *reduced_override_
                                 : reduced_motion();
        if (reduced || rubber_band_dim_ <= 0.0) {
            display_pos_ = std::clamp(raw_pos_, min_val_, max_val_);
        } else {
            display_pos_ = rubber_band_clamp(
                raw_pos_, min_val_, max_val_, rubber_band_dim_, coefficient_);
        }
    }

    double min_val_ = 0.0;
    double max_val_ = 1.0;
    double rubber_band_dim_ = 0.0;
    double coefficient_ = 0.55;
    double deceleration_rate_ = 0.998;
    double raw_pos_ = 0.0;
    double display_pos_ = 0.0;
    double velocity_ = 0.0;
    bool has_velocity_ = false;
    bool dragging_ = false;
    std::optional<bool> reduced_override_;
};

/* Headless frame-time recorder for measuring 60 Hz (16.667 ms) and
 * 120 Hz (8.333 ms) budget compliance and solver cost in microseconds. */
class FrameTimeRecorder {
public:
    static constexpr double kBudget60HzMs = 1000.0 / 60.0;
    static constexpr double kBudget120HzMs = 1000.0 / 120.0;

    void reset() { samples_ms_.clear(); }

    void reserve(std::size_t n) { samples_ms_.reserve(n); }

    void record_ms(double ms) {
        if (ms >= 0.0 && std::isfinite(ms)) samples_ms_.push_back(ms);
    }

    void record_us(double us) { record_ms(us * 0.001); }

    std::size_t count() const { return samples_ms_.size(); }

    double min_ms() const {
        if (samples_ms_.empty()) return 0.0;
        return *std::min_element(samples_ms_.begin(), samples_ms_.end());
    }

    double max_ms() const {
        if (samples_ms_.empty()) return 0.0;
        return *std::max_element(samples_ms_.begin(), samples_ms_.end());
    }

    double mean_ms() const {
        if (samples_ms_.empty()) return 0.0;
        double sum = 0.0;
        for (double v : samples_ms_) sum += v;
        return sum / static_cast<double>(samples_ms_.size());
    }

    double percentile_ms(double q) const {
        if (samples_ms_.empty()) return 0.0;
        std::vector<double> sorted = samples_ms_;
        std::sort(sorted.begin(), sorted.end());
        const double clamped_q = clamp_unit(q);
        const std::size_t idx = static_cast<std::size_t>(
            std::floor(clamped_q * static_cast<double>(sorted.size() - 1)));
        return sorted[idx];
    }

    std::size_t dropped_60hz() const {
        std::size_t n = 0;
        for (double v : samples_ms_) {
            if (v > kBudget60HzMs) ++n;
        }
        return n;
    }

    std::size_t dropped_120hz() const {
        std::size_t n = 0;
        for (double v : samples_ms_) {
            if (v > kBudget120HzMs) ++n;
        }
        return n;
    }

private:
    std::vector<double> samples_ms_;
};

}  // namespace finn::ui

#endif
