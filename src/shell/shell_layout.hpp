// Shared responsive form-factor helpers, tokenized color accessors, focus ring
// drawing, and spring animation driver for finn-shell.

#ifndef FINN_SHELL_LAYOUT_HPP
#define FINN_SHELL_LAYOUT_HPP

#include <QColor>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

#include "ui/finn/spring.hpp"
#include "ui/finn/tokens.hpp"
#include "ui/generated_tokens.hpp"

namespace finn::shell {

enum class FormFactor {
    Phone,
    Tablet,
    Desktop,
};

inline FormFactor resolve_form_factor(int width_dp) {
    const QByteArray env = qgetenv("FINN_FORM_FACTOR").trimmed().toLower();
    if (env == "phone") return FormFactor::Phone;
    if (env == "tablet") return FormFactor::Tablet;
    if (env == "desktop") return FormFactor::Desktop;
    if (width_dp > 0 && width_dp <= finn::ui::breakpoints::bp_phone_max) {
        return FormFactor::Phone;
    }
    if (width_dp > 0 && width_dp <= finn::ui::breakpoints::bp_tablet_max) {
        return FormFactor::Tablet;
    }
    return FormFactor::Desktop;
}

inline QSize primary_screen_size() {
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize avail = screen->availableGeometry().size();
        if (avail.isValid() && !avail.isEmpty()) return avail;
        const QSize full = screen->geometry().size();
        if (full.isValid() && !full.isEmpty()) return full;
    }
    return QSize(1280, 720);
}

inline FormFactor current_form_factor() {
    return resolve_form_factor(primary_screen_size().width());
}

inline QColor token_color(std::uint32_t rgba) {
    return QColor::fromRgba(rgba);
}

inline QColor with_alpha(QColor color, int alpha) {
    color.setAlpha(std::clamp(alpha, 0, 255));
    return color;
}

inline QColor shell_glass_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(t.glass, t.dark ? 228 : 220);
}

inline QColor shell_dock_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(t.glass, t.dark ? 210 : 196);
}

inline QColor shell_subcard_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(t.tile, t.dark ? 220 : 204);
}

inline QColor shell_footer_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(
        token_color(t.dark ? finn::ui::colors::surface_canvas_dark
                           : finn::ui::colors::surface_canvas_light),
        t.dark ? 236 : 242);
}

inline QColor shell_input_fill(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::surface_input_dark
                              : finn::ui::colors::surface_input_light);
}

inline QColor shell_hover_fill(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::surface_hover_dark
                              : finn::ui::colors::surface_hover_light);
}

inline QColor shell_pressed_fill(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::surface_pressed_dark
                              : finn::ui::colors::surface_pressed_light);
}

inline QColor shell_danger(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::danger_dark
                              : finn::ui::colors::danger_light);
}

inline QColor shell_danger_hover(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::danger_hover_dark
                              : finn::ui::colors::danger_hover_light);
}

inline QColor shell_success(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::success_dark
                              : finn::ui::colors::success_light);
}

inline QColor shell_warning(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::warning_dark
                              : finn::ui::colors::warning_light);
}

inline QColor shell_on_accent(const finn::ui::FinnTokens& t) {
    return token_color(t.dark ? finn::ui::colors::fill_dark
                              : finn::ui::colors::fill_light);
}

inline void draw_focus_ring(QPainter& p,
                            const QRectF& rect,
                            qreal radius,
                            const finn::ui::FinnTokens& t) {
    p.save();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(t.accent, 2.0));
    const QRectF r = rect.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal clamped_r = std::max<qreal>(0.0, radius - 1.0);
    p.drawRoundedRect(r, clamped_r, clamped_r);
    p.restore();
}

// Drives a single scalar SpringAnimator on a 120 Hz QTimer (adaptive elapsed dt)
// while preserving instantaneous settling under reduced-motion mode.
class ShellSpringDriver final {
public:
    ShellSpringDriver(QObject* parent,
                      const finn::ui::SpringParams& params,
                      double initial,
                      std::function<void(double)> on_value,
                      std::function<void()> on_settled = {})
        : spring_(params, initial),
          timer_(parent),
          on_value_(std::move(on_value)),
          on_settled_(std::move(on_settled)) {
        timer_.setInterval(8);
        QObject::connect(&timer_, &QTimer::timeout, parent, [this]() { tick(); });
    }

    void set(double v, double vel = 0.0) {
        timer_.stop();
        spring_.set(v, vel);
        if (on_value_) on_value_(spring_.value());
    }

    void retarget(double target,
                  const finn::ui::SpringParams* override_params = nullptr) {
        if (override_params) {
            spring_.retarget(target, *override_params);
        } else {
            spring_.retarget(target);
        }
        if (finn::ui::reduced_motion() || spring_.settled()) {
            timer_.stop();
            spring_.set(target, 0.0);
            if (on_value_) on_value_(spring_.value());
            if (on_settled_) on_settled_();
            return;
        }
        if (!timer_.isActive()) {
            elapsed_.restart();
            timer_.start();
        }
    }

    void impulse(double dv, double target) {
        spring_.retarget(target);
        spring_.add_velocity(dv);
        if (finn::ui::reduced_motion() || spring_.settled()) {
            timer_.stop();
            spring_.set(target, 0.0);
            if (on_value_) on_value_(spring_.value());
            if (on_settled_) on_settled_();
            return;
        }
        if (!timer_.isActive()) {
            elapsed_.restart();
            timer_.start();
        }
    }

    void set_thresholds(double value_eps, double velocity_eps) {
        spring_.set_thresholds(value_eps, velocity_eps);
    }

    void stop() { timer_.stop(); }

    double value() const { return spring_.value(); }
    double target() const { return spring_.target(); }
    bool running() const { return timer_.isActive(); }

private:
    void tick() {
        const qint64 ns = elapsed_.nsecsElapsed();
        elapsed_.restart();
        const double dt = std::clamp(static_cast<double>(ns) * 1e-9, 0.001, 0.05);
        const double val = spring_.step(dt);
        if (on_value_) on_value_(val);
        if (spring_.settled()) {
            timer_.stop();
            if (on_settled_) on_settled_();
        }
    }

    finn::ui::SpringAnimator spring_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    std::function<void(double)> on_value_;
    std::function<void()> on_settled_;
};

}  // namespace finn::shell

#endif
