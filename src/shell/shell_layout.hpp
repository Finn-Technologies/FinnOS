// Shared responsive form-factor helpers, tokenized color accessors, focus ring
// drawing, and spring animation driver for finn-shell.

#ifndef FINN_SHELL_LAYOUT_HPP
#define FINN_SHELL_LAYOUT_HPP

#include <QByteArray>
#include <QColor>
#include <QDataStream>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <QRectF>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <algorithm>
#include <cmath>
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

// Layer-surface elevation. Figma elevation level 4 is a (0, 4) shadow of
// #000 at 5% alpha; the blur radius comes from the authoritative blur tokens
// in ui/_tokens.fui (35 px for glass popovers, 15 px for notification cards).
// The level is read from finn::ui::elevation so no shadow colour is invented
// per call site.
inline constexpr int kLayerShadowAlpha = 13;  // 5% of 255, the Figma 5% shadow.
// The notification elevation's shadow blur is 25 px in the Figma elevation
// spec (distinct from its 15 px backdrop-blur token).
inline constexpr int kNotificationShadowBlur = 25;

static_assert(finn::ui::elevation::elev_popover ==
              finn::ui::elevation::elev_notification);

constexpr int layer_shadow_pad(int blur) {
    // ceil(0.4 * blur): one standard deviation carries the visible 5% tail;
    // beyond that the blur is below one 8-bit alpha step.
    return blur > 0 ? std::max(6, (blur * 2 + 4) / 5) : 0;
}

inline constexpr int kPopoverShadowPad =
    layer_shadow_pad(finn::ui::blur::blur_glass);
inline constexpr int kNotificationShadowPad =
    layer_shadow_pad(kNotificationShadowBlur);

// Separable box blur, three passes: a standard real-time approximation of a
// Gaussian. Operates on the ARGB image in place, so it is a genuine blur of
// the shadow mask rather than a drawn gradient.
inline void box_blur_argb(QImage& img, int radius) {
    if (radius <= 0) return;
    const int w = img.width();
    const int h = img.height();
    QImage tmp(w, h, QImage::Format_ARGB32);
    tmp.fill(Qt::transparent);

    auto blur_h = [&](const QImage& src, QImage& dst) {
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            QRgb* out = reinterpret_cast<QRgb*>(dst.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int a = 0, r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, x - radius);
                     k <= std::min(w - 1, x + radius); ++k) {
                    const QRgb px = row[k];
                    a += qAlpha(px); r += qRed(px); g += qGreen(px); b += qBlue(px);
                    ++n;
                }
                out[x] = qRgba(r / n, g / n, b / n, a / n);
            }
        }
    };
    auto blur_v = [&](const QImage& src, QImage& dst) {
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                int a = 0, r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, y - radius);
                     k <= std::min(h - 1, y + radius); ++k) {
                    const QRgb px = src.pixel(x, k);
                    a += qAlpha(px); r += qRed(px); g += qGreen(px); b += qBlue(px);
                    ++n;
                }
                dst.setPixel(x, y, qRgba(r / n, g / n, b / n, a / n));
            }
        }
    };
    blur_h(img, tmp);
    blur_v(tmp, img);
    blur_h(img, tmp);
    blur_v(tmp, img);
    blur_h(img, tmp);
    blur_v(tmp, img);
}

inline QPixmap layer_shadow_pixmap(int w, int h, qreal corner, int blur) {
    static QHash<QByteArray, QPixmap> cache;
    const int pad = layer_shadow_pad(blur);
    const int iw = w + 2 * pad;
    const int ih = h + 2 * pad;
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << w << h << corner << blur;
    if (cache.contains(key)) return cache.value(key);

    QImage mask(iw, ih, QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    {
        QPainter mp(&mask);
        mp.setRenderHint(QPainter::Antialiasing, true);
        mp.setPen(Qt::NoPen);
        mp.setBrush(QColor(0, 0, 0, kLayerShadowAlpha));
        mp.drawRoundedRect(QRectF(pad, pad, w, h), corner, corner);
    }
    // Three box passes at a third of the Figma radius approximate the
    // Gaussian falloff of the elevation token.
    box_blur_argb(mask, std::max(1, blur / 3));
    QPixmap pm = QPixmap::fromImage(mask);
    cache.insert(key, pm);
    return pm;
}

// Draw the elevation shadow for a glass surface whose visible rounded rect is
// `content` (in surface coordinates). The surface must reserve
// layer_shadow_pad(blur) transparent pixels around content; callers size and
// place their layer surface accordingly so Figma geometry is preserved.
inline void paintLayerShadow(QPainter& p, const QRectF& content, qreal corner,
                             int blur, int elevation) {
    if (blur <= 0 || content.isEmpty()) return;
    const int pad = layer_shadow_pad(blur);
    const QPixmap pm = layer_shadow_pixmap(
        static_cast<int>(std::ceil(content.width())),
        static_cast<int>(std::ceil(content.height())), corner, blur);
    // Elevation level 4 gives the Figma (0, 4) offset; x offset is zero.
    p.drawPixmap(QPointF(content.x() - pad, content.y() - pad + elevation), pm);
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
