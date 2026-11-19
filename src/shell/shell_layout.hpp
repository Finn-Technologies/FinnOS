// Shared responsive form-factor helpers, tokenized color accessors, focus ring
// drawing, and spring animation driver for finn-shell.

#ifndef FINN_SHELL_LAYOUT_HPP
#define FINN_SHELL_LAYOUT_HPP

#include <QColor>
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
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include "shell/blur_kernel.hpp"
#include "shell/mobile_shell_layout.hpp"
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
    // The pure, token-backed model is the single breakpoint authority; this
    // Qt enum is only its shell-side spelling.
    switch (mobile::resolve_layout_class(width_dp)) {
        case mobile::LayoutClass::Phone: return FormFactor::Phone;
        case mobile::LayoutClass::Tablet: return FormFactor::Tablet;
        case mobile::LayoutClass::Desktop: return FormFactor::Desktop;
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

// Token-derived phone/tablet geometry (page margins, touch rows, gesture pill,
// sheet radius) for the current primary screen. Exposed so shell widgets and
// the host-side verification can read the same numbers the model asserts.
inline mobile::LayoutMetrics current_layout_metrics() {
    const QSize s = primary_screen_size();
    return mobile::compute_layout(s.width(), s.height());
}

inline QColor token_color(std::uint32_t rgba) {
    return QColor::fromRgba(rgba);
}

inline QColor with_alpha(QColor color, int alpha) {
    color.setAlpha(std::clamp(alpha, 0, 255));
    return color;
}

// Material weights from ui/_tokens.fui:102. They are not emitted into
// generated_tokens.hpp, so the shell transcribes the names and values here
// rather than inventing new per-widget alphas. The compositor's real
// backdrop blur and the elevation shadows are unchanged by this choice.
inline constexpr int kGlassPanelAlpha = static_cast<int>(255 * 0.65 + 0.5);
inline constexpr int kGlassTileAlpha = static_cast<int>(255 * 0.85 + 0.5);
inline constexpr int kGlassWellAlpha = static_cast<int>(255 * 0.53 + 0.5);

inline QColor shell_glass_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(t.glass, kGlassPanelAlpha);
}

inline QColor shell_dock_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(t.glass, kGlassPanelAlpha);
}

inline QColor shell_subcard_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(t.glass, kGlassPanelAlpha);
}

inline QColor shell_footer_fill(const finn::ui::FinnTokens& t) {
    return with_alpha(t.glass, kGlassWellAlpha);
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
//
// The kernel is O(w * h) per pass with sliding-window sums. The previous
// version scanned up to 2r+1 neighbours per pixel and used QImage::pixel() in
// the vertical pass, which made a first-time popover shadow (472x328 px at
// blur 35, radius 11) cost ~21M neighbour reads. See docs/performance.md.
inline void box_blur_argb(QImage& img, int radius) {
    if (radius <= 0 || img.format() != QImage::Format_ARGB32) return;
    const int w = img.width();
    const int h = img.height();
    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    std::vector<QRgb> buf(pixels);
    std::vector<QRgb> tmp(pixels);
    // QImage rows may be padded; copy through contiguous buffers so the
    // strided vertical pass is well-defined for any bytesPerLine.
    for (int y = 0; y < h; ++y) {
        std::memcpy(buf.data() + static_cast<std::size_t>(y) * w,
                    img.constScanLine(y), static_cast<std::size_t>(w) * sizeof(QRgb));
    }
    blur_kernel::box_blur_argb_u32(
        reinterpret_cast<std::uint32_t*>(buf.data()),
        reinterpret_cast<std::uint32_t*>(tmp.data()), w, h, radius);
    for (int y = 0; y < h; ++y) {
        std::memcpy(img.scanLine(y), buf.data() + static_cast<std::size_t>(y) * w,
                    static_cast<std::size_t>(w) * sizeof(QRgb));
    }
}

struct ShadowCacheKey {
    int w = 0;
    int h = 0;
    qreal corner = 0;
    int blur = 0;
    bool operator==(const ShadowCacheKey& o) const {
        return w == o.w && h == o.h && blur == o.blur &&
               qFuzzyCompare(corner + 1.0, o.corner + 1.0);
    }
};

inline uint qHash(const ShadowCacheKey& key, uint seed = 0) {
    const auto mix = [seed](uint h, int v) {
        return h ^ static_cast<uint>(std::hash<int>{}(v) + 0x9e3779b9u +
                                     (h << 6) + (h >> 2));
    };
    uint h = mix(seed, key.w);
    h = mix(h, key.h);
    h = mix(h, key.blur);
    h = mix(h, qRound(key.corner * 100.0));
    return h;
}

inline QPixmap layer_shadow_pixmap(int w, int h, qreal corner, int blur) {
    static QHash<ShadowCacheKey, QPixmap> cache;
    const int pad = layer_shadow_pad(blur);
    const int iw = w + 2 * pad;
    const int ih = h + 2 * pad;
    const ShadowCacheKey key{w, h, corner, blur};
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
