// Implements FinnUI custom Qt6 components aligned to FinnOS tokens and motion curves.

#include "ui/finn/components.hpp"

#include <QDateTime>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "ui/finn/animation.hpp"
#include "ui/finn/spring.hpp"
#include "ui/finn/theme.hpp"

namespace finn::ui {
namespace {

QColor role_background(FinnSurface::Role role, const FinnTokens& t) {
    switch (role) {
        case FinnSurface::Role::Canvas:
            return t.surface_canvas;
        case FinnSurface::Role::Bone:
            return t.bone;
        case FinnSurface::Role::Tile:
            return t.tile;
        case FinnSurface::Role::TileOn:
            return t.tile_on;
        case FinnSurface::Role::Transparent:
            return Qt::transparent;
    }
    return t.tile;
}

QColor lerp_color(const QColor& a, const QColor& b, qreal progress) {
    const qreal t = std::clamp<qreal>(progress, 0.0, 1.0);
    if (a.alpha() == 0 && b.alpha() == 0) return Qt::transparent;
    if (a.alpha() == 0) {
        QColor c = b;
        c.setAlpha(qRound(b.alpha() * t));
        return c;
    }
    if (b.alpha() == 0) {
        QColor c = a;
        c.setAlpha(qRound(a.alpha() * (1.0 - t)));
        return c;
    }
    return QColor(
        qRound(a.red() + (b.red() - a.red()) * t),
        qRound(a.green() + (b.green() - a.green()) * t),
        qRound(a.blue() + (b.blue() - a.blue()) * t),
        qRound(a.alpha() + (b.alpha() - a.alpha()) * t));
}

bool is_reduced_motion_active() {
    return FinnThemeManager::instance().reducedMotion() || reduced_motion();
}

void apply_press_scale(QPainter& p, const QWidget* w, qreal scale) {
    if (!w || is_reduced_motion_active() || std::abs(scale - 1.0) <= 1e-4) return;
    const QPointF c = QRectF(w->rect()).center();
    p.translate(c);
    p.scale(scale, scale);
    p.translate(-c);
}

class KeyboardFocusTracker : public QObject {
  public:
    explicit KeyboardFocusTracker(QObject* parent = nullptr) : QObject(parent) {}
    bool kbd_focus = false;

  protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::FocusIn) {
            auto* fe = static_cast<QFocusEvent*>(ev);
            const Qt::FocusReason r = fe->reason();
            kbd_focus = (r == Qt::TabFocusReason ||
                         r == Qt::BacktabFocusReason ||
                         r == Qt::ShortcutFocusReason);
        } else if (ev->type() == QEvent::MouseButtonPress) {
            kbd_focus = false;
        }
        return QObject::eventFilter(obj, ev);
    }
};

bool is_keyboard_focus_active() {
    static KeyboardFocusTracker* tracker = nullptr;
    if (!tracker && qApp) {
        tracker = new KeyboardFocusTracker(qApp);
        qApp->installEventFilter(tracker);
    }
    return tracker ? tracker->kbd_focus : false;
}

void draw_focus_ring(QPainter& p, const QWidget* w, qreal radius,
                     const FinnTokens& t) {
    if (!w || !w->hasFocus() || !w->isEnabled() || !is_keyboard_focus_active()) {
        return;
    }
    p.save();
    p.setBrush(Qt::NoBrush);
    QColor ring = t.accent;
    ring.setAlpha(215);
    p.setPen(QPen(ring, 2.0));
    const QRectF fr = QRectF(w->rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    p.drawRoundedRect(fr, std::max(1.0, radius - 1.0),
                      std::max(1.0, radius - 1.0));
    p.restore();
}

QColor token_fill_alpha(int alpha) {
    QColor c = QColor::fromRgba(colors::fill_light);
    c.setAlpha(std::clamp(alpha, 0, 255));
    return c;
}

QColor token_ink_alpha(int alpha) {
    QColor c = QColor::fromRgba(colors::ink_light);
    c.setAlpha(std::clamp(alpha, 0, 255));
    return c;
}

// Paints a theme-reactive SVG glyph directly in paintEvent so Light/Dark
// toggles never leave a cached QLabel pixmap behind.
class DynamicIconWidget final : public QWidget {
   public:
    DynamicIconWidget(QString icon_name, int box_size, int icon_size,
                      bool container_when_plain, bool use_mute_tint,
                      QWidget* parent = nullptr)
        : QWidget(parent),
          icon_name_(std::move(icon_name)),
          box_size_(box_size),
          icon_size_(icon_size),
          container_when_plain_(container_when_plain),
          use_mute_tint_(use_mute_tint) {
        setFixedSize(box_size_, box_size_);
        setVisible(!icon_name_.isEmpty());
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setIconName(const QString& name) {
        icon_name_ = name;
        setVisible(!icon_name_.isEmpty());
        update();
    }

    void setBadgeColor(const QColor& color) {
        badge_color_ = color;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override {
        if (icon_name_.isEmpty()) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool has_badge = badge_color_.isValid();
        if (has_badge || container_when_plain_) {
            const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
            p.setBrush(has_badge ? badge_color_ : t.surface_input);
            p.setPen(QPen(t.hair, 1.0));
            p.drawRoundedRect(r, kRadiusMd, kRadiusMd);
        }

        const QColor tint =
            has_badge ? QColor::fromRgba(colors::fill_light)
                      : (use_mute_tint_ ? t.mute : t.ink);
        const qreal ox = (width() - icon_size_) / 2.0;
        const qreal oy = (height() - icon_size_) / 2.0;
        FinnThemeManager::instance().paintIcon(
            &p, QRectF(ox, oy, icon_size_, icon_size_), icon_name_, tint);
    }

   private:
    QString icon_name_;
    int box_size_;
    int icon_size_;
    bool container_when_plain_;
    bool use_mute_tint_;
    QColor badge_color_;
};

}  // namespace

FinnFormFactor resolve_form_factor(int width_dp) {
    if (const char* env_ff = std::getenv("FINN_FORM_FACTOR"); env_ff && *env_ff) {
        const QString val = QString::fromUtf8(env_ff).trimmed().toLower();
        if (val == QStringLiteral("phone")) return FinnFormFactor::Phone;
        if (val == QStringLiteral("tablet")) return FinnFormFactor::Tablet;
        if (val == QStringLiteral("desktop")) return FinnFormFactor::Desktop;
    }
    int w = width_dp;
    if (w <= 0) {
        if (const char* sim_w = std::getenv("FINN_SIM_WIDTH"); sim_w && *sim_w) {
            const int parsed = std::atoi(sim_w);
            if (parsed > 0) w = parsed;
        }
    }
    if (w > 0) {
        if (w <= breakpoints::bp_phone_max) return FinnFormFactor::Phone;
        if (w <= breakpoints::bp_tablet_max) return FinnFormFactor::Tablet;
    }
    return FinnFormFactor::Desktop;
}

int touch_target_height(int desktop_height, FinnFormFactor ff) {
    switch (ff) {
        case FinnFormFactor::Phone:
            return std::max(desktop_height, 48);
        case FinnFormFactor::Tablet:
            return std::max(desktop_height, 44);
        case FinnFormFactor::Desktop:
            return desktop_height;
    }
    return desktop_height;
}

void FinnSpringDriver::init(QWidget* owner, const SpringParams& params,
                            double initial,
                            std::function<void(double)> step_cb) {
    animator = SpringAnimator(params, initial);
    on_step = std::move(step_cb);
    if (!timer && owner) {
        timer = new QTimer(owner);
        timer->setTimerType(Qt::PreciseTimer);
        timer->setInterval(8);
        QObject::connect(timer, &QTimer::timeout, owner, [this]() {
            const double dt = clock.isValid()
                                  ? std::clamp(static_cast<double>(clock.restart()) * 1e-3,
                                               0.001, 0.05)
                                  : (1.0 / 60.0);
            if (using_crossfade) {
                const double v = crossfade.step(dt);
                if (on_step) on_step(v);
                if (!crossfade.running() && timer) {
                    using_crossfade = false;
                    timer->stop();
                }
                return;
            }
            const bool rm = is_reduced_motion_active();
            animator.set_reduced_motion_override(rm);
            const double v = animator.step(dt);
            if (on_step) on_step(v);
            if (animator.settled() && timer) timer->stop();
        });
    }
}

void FinnSpringDriver::retarget(double target) {
    using_crossfade = false;
    const bool rm = is_reduced_motion_active();
    animator.set_reduced_motion_override(rm);
    animator.retarget(target);
    if (animator.settled()) {
        if (timer) timer->stop();
        if (on_step) on_step(animator.value());
        return;
    }
    if (timer && !timer->isActive()) {
        clock.restart();
        timer->start();
    }
}

void FinnSpringDriver::retarget(double target, const SpringParams& params) {
    animator.set_params(params);
    retarget(target);
}

void FinnSpringDriver::retargetOrCrossfade(double from_on_reduced,
                                           double target,
                                           const SpringParams& params,
                                           double reduced_seconds) {
    if (is_reduced_motion_active()) {
        using_crossfade = true;
        animator.set(target, 0.0);
        crossfade.start(from_on_reduced, target, reduced_seconds);
        if (on_step) on_step(crossfade.value);
        if (crossfade.running() && timer && !timer->isActive()) {
            clock.restart();
            timer->start();
        }
        return;
    }
    retarget(target, params);
}

void FinnSpringDriver::setImmediate(double value) {
    using_crossfade = false;
    if (timer) timer->stop();
    animator.set(value, 0.0);
    if (on_step) on_step(value);
}

// ---- FinnSurface ----

FinnSurface::FinnSurface(Role role, qreal radius, bool border, QWidget* parent)
    : QWidget(parent), role_(role), radius_(radius), bordered_(border) {
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnSurface::setRole(Role role) {
    role_ = role;
    update();
}

void FinnSurface::setRadius(qreal radius) {
    radius_ = radius;
    update();
}

void FinnSurface::setBordered(bool bordered) {
    bordered_ = bordered;
    update();
}

void FinnSurface::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor fill = role_background(role_, t);
    p.setBrush(fill == Qt::transparent ? Qt::NoBrush : QBrush(fill));
    p.setPen(bordered_ ? QPen(t.hair, 1.0) : Qt::NoPen);
    p.drawRoundedRect(r, radius_, radius_);
}

// ---- FinnCard ----

FinnCard::FinnCard(qreal radius, bool clickable, QWidget* parent)
    : QWidget(parent), radius_(radius), clickable_(clickable) {
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(clickable_ ? Qt::TabFocus : Qt::NoFocus);
    setAccessibleName(QStringLiteral("Card"));
    if (clickable_) setCursor(Qt::PointingHandCursor);
    hover_spring_.init(this, springs::kSnappy, 0.0, [this](double v) {
        hover_progress_ = v;
        update();
    });
    press_spring_.init(this, springs::kSnappy, 1.0, [this](double v) {
        press_scale_ = v;
        update();
    });
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnCard::setClickable(bool clickable) {
    clickable_ = clickable;
    setFocusPolicy(clickable_ ? Qt::TabFocus : Qt::NoFocus);
    setCursor(clickable_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void FinnCard::setSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    update();
}

void FinnCard::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    apply_press_scale(p, this, press_scale_);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QColor bg = t.tile;
    QColor border = t.hair;
    if (selected_) {
        bg = t.tile_on;
        border = t.accent;
    } else if (clickable_ && pressed_) {
        bg = t.dark ? t.tile.lighter(115) : t.surface_canvas;
    } else if (clickable_ && hover_progress_ > 0.001) {
        const QColor hover_col = t.dark ? t.tile.lighter(108) : t.surface_input;
        bg = lerp_color(t.tile, hover_col, hover_progress_);
    }

    p.setBrush(bg);
    p.setPen(QPen(border, selected_ ? 1.4 : 1.0));
    p.drawRoundedRect(r, radius_, radius_);

    if (!selected_) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(token_fill_alpha(t.dark ? 10 : 140), 1.0));
        p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0),
                          std::max(1.0, radius_ - 1.0),
                          std::max(1.0, radius_ - 1.0));
    }
    if (clickable_) draw_focus_ring(p, this, radius_, t);
}

void FinnCard::enterEvent(QEnterEvent*) {
    hovered_ = true;
    hover_spring_.retarget(1.0);
}

void FinnCard::leaveEvent(QEvent*) {
    hovered_ = false;
    pressed_ = false;
    hover_spring_.retarget(0.0);
    press_spring_.retarget(1.0, springs::kBouncy);
}

void FinnCard::mousePressEvent(QMouseEvent* event) {
    if (clickable_ && event->button() == Qt::LeftButton) {
        pressed_ = true;
        press_spring_.retarget(0.985, springs::kSnappy);
        update();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void FinnCard::mouseReleaseEvent(QMouseEvent* event) {
    if (clickable_ && event->button() == Qt::LeftButton && pressed_) {
        pressed_ = false;
        press_spring_.retarget(1.0, springs::kBouncy);
        update();
        if (rect().contains(event->pos())) {
            emit clicked();
        }
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void FinnCard::keyPressEvent(QKeyEvent* event) {
    if (clickable_ && (event->key() == Qt::Key_Space ||
                       event->key() == Qt::Key_Return ||
                       event->key() == Qt::Key_Enter)) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---- FinnGlassPanel ----

FinnGlassPanel::FinnGlassPanel(qreal radius, QWidget* parent)
    : QWidget(parent), radius_(radius) {
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnGlassPanel::setRadius(qreal radius) {
    radius_ = radius;
    update();
}

void FinnGlassPanel::setOpacityAlpha(int alpha) {
    alpha_ = std::clamp(alpha, 0, 255);
    update();
}

void FinnGlassPanel::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QColor bg = t.glass;
    if (alpha_ >= 0) bg.setAlpha(alpha_);
    p.setBrush(bg);
    p.setPen(QPen(t.hair, 1.0));
    p.drawRoundedRect(r, radius_, radius_);

    // A hairline of light on the top edge is enough; a bright inner rim made
    // every glass surface look like a glossy card.
    QColor spec = Qt::white;
    spec.setAlpha(t.dark ? 12 : 60);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(spec, 1.0));
    p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0),
                       std::max(1.0, radius_ - 1.0),
                       std::max(1.0, radius_ - 1.0));
}

// ---- FinnLabel ----

FinnLabel::FinnLabel(const QString& text, Role role, Tone tone, QWidget* parent)
    : QLabel(text, parent), role_(role), tone_(tone) {
    applyTypographyAndColor();
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, &FinnLabel::applyTypographyAndColor);
}

void FinnLabel::setRole(Role role) {
    role_ = role;
    applyTypographyAndColor();
}

void FinnLabel::setTone(Tone tone) {
    tone_ = tone;
    applyTypographyAndColor();
}

void FinnLabel::applyTypographyAndColor() {
    switch (role_) {
        // Figma 20:64: 16px page header, 14px values, 11-12px meta. Hierarchy
        // comes from size and ink, not from a heavier cut of the face.
        case Role::TitleLarge:
            setFont(finn_font_px(18, QFont::Medium));
            break;
        case Role::Title:
            setFont(finn_font_px(16, QFont::Medium));
            break;
        case Role::Headline:
            setFont(finn_font_px(14, QFont::Medium));
            break;
        case Role::Body:
            setFont(finn_font_px(14, QFont::Normal));
            break;
        case Role::Caption:
            setFont(finn_font_px(12, QFont::Normal));
            break;
        case Role::Mono:
            setFont(finn_mono_font(9.0, QFont::Normal));
            break;
    }

    const auto& t = FinnThemeManager::instance().tokens();
    QColor color = t.ink;
    switch (tone_) {
        case Tone::Ink:
            color = t.ink;
            break;
        case Tone::Dim:
            color = t.dim;
            break;
        case Tone::Mute:
            color = t.mute;
            break;
        case Tone::Accent:
            color = t.on;
            break;
        case Tone::Success:
            color = t.success;
            break;
        case Tone::Warning:
            color = t.warning;
            break;
        case Tone::Danger:
            color = t.danger;
            break;
        case Tone::OnAccent:
            color = Qt::white;
            break;
    }

    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, color);
    pal.setColor(QPalette::Text, color);
    setPalette(pal);
}

// ---- FinnButton ----

FinnButton::FinnButton(const QString& text, Variant variant,
                       const QString& icon_name, QWidget* parent)
    : QPushButton(text, parent), variant_(variant), icon_name_(icon_name) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(text.isEmpty() ? icon_name : text);
    setFont(finn_font(10.0, QFont::Medium));
    setMinimumHeight(touch_target_height(32, resolve_form_factor()));
    hover_spring_.init(this, springs::kSnappy, 0.0, [this](double v) {
        hover_progress_ = v;
        update();
    });
    press_spring_.init(this, springs::kSnappy, 1.0, [this](double v) {
        press_scale_ = v;
        update();
    });
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnButton::setVariant(Variant variant) {
    variant_ = variant;
    update();
}

void FinnButton::setIconName(const QString& icon_name) {
    icon_name_ = icon_name;
    updateGeometry();
    update();
}

QSize FinnButton::sizeHint() const {
    const QFontMetrics fm(font());
    int w = 24;
    if (!icon_name_.isEmpty()) w += 18 + (text().isEmpty() ? 0 : 6);
    if (!text().isEmpty()) w += fm.horizontalAdvance(text());
    return QSize(std::max(w, 60),
                 touch_target_height(32, resolve_form_factor()));
}

void FinnButton::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    apply_press_scale(p, this, press_scale_);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool en = isEnabled();
    const bool down = isDown();

    QColor bg = t.surface_input;
    QColor border = t.hair;
    QColor fg = en ? t.ink : t.mute;

    if (!en) {
        bg = t.surface_canvas;
    } else if (variant_ == Variant::Primary) {
        bg = down ? t.accent.darker(115)
                  : lerp_color(t.accent, t.accent.lighter(110), hover_progress_);
        border = Qt::transparent;
        fg = Qt::white;
    } else if (variant_ == Variant::Danger) {
        bg = down ? t.danger_hover
                  : lerp_color(t.danger, t.danger_hover, hover_progress_);
        border = Qt::transparent;
        fg = Qt::white;
    } else if (variant_ == Variant::Ghost) {
        bg = down ? t.surface_pressed
                  : lerp_color(Qt::transparent, t.surface_hover, hover_progress_);
        border = Qt::transparent;
    } else {
        if (isChecked()) {
            bg = t.tile_on;
            border = t.accent;
            fg = t.on;
        } else if (down) {
            bg = t.dark ? t.surface_input.lighter(128)
                        : lerp_color(t.surface_input, t.ink, 0.08);
        } else if (hover_progress_ > 0.001) {
            const QColor hover_col =
                t.dark ? t.surface_input.lighter(115)
                       : lerp_color(t.surface_input, t.ink, 0.04);
            bg = lerp_color(t.surface_input, hover_col, hover_progress_);
        }
    }

    if (bg.alpha() > 0) {
        if ((variant_ == Variant::Primary || variant_ == Variant::Danger) && en) {
            QLinearGradient grad(r.topLeft(), r.bottomLeft());
            grad.setColorAt(0.0, bg.lighter(108));
            grad.setColorAt(1.0, bg);
            p.setBrush(grad);
        } else {
            p.setBrush(bg);
        }
    } else {
        p.setBrush(Qt::NoBrush);
    }
    p.setPen(border == Qt::transparent ? Qt::NoPen : QPen(border, 1.0));
    p.drawRoundedRect(r, kRadiusMd, kRadiusMd);

    if (en && (variant_ == Variant::Primary || variant_ == Variant::Danger)) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(token_fill_alpha(50), 1.0));
        p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0),
                          std::max(1.0, kRadiusMd - 1.0),
                          std::max(1.0, kRadiusMd - 1.0));
    }

    const bool has_icon = !icon_name_.isEmpty();
    const bool has_text = !text().isEmpty();
    const QFontMetrics fm(font());
    const int icon_sz = 16;
    const int text_w = has_text ? fm.horizontalAdvance(text()) : 0;
    const int gap = (has_icon && has_text) ? 6 : 0;
    const int total_w = (has_icon ? icon_sz : 0) + gap + text_w;
    qreal cur_x = (width() - total_w) / 2.0;

    if (has_icon) {
        const QRectF ir(cur_x, (height() - icon_sz) / 2.0, icon_sz, icon_sz);
        FinnThemeManager::instance().paintIcon(&p, ir, icon_name_, fg);
        cur_x += icon_sz + gap;
    }
    if (has_text) {
        p.setFont(font());
        p.setPen(fg);
        p.drawText(QRectF(cur_x, 0, text_w + 4, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, text());
    }
    draw_focus_ring(p, this, kRadiusMd, t);
}

void FinnButton::enterEvent(QEnterEvent*) {
    hovered_ = true;
    hover_spring_.retarget(1.0);
}

void FinnButton::leaveEvent(QEvent*) {
    hovered_ = false;
    hover_spring_.retarget(0.0);
    press_spring_.retarget(1.0, springs::kBouncy);
}

void FinnButton::mousePressEvent(QMouseEvent* event) {
    if (isEnabled() && event->button() == Qt::LeftButton) {
        press_spring_.retarget(0.96, springs::kSnappy);
    }
    QPushButton::mousePressEvent(event);
}

void FinnButton::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        press_spring_.retarget(1.0, springs::kBouncy);
    }
    QPushButton::mouseReleaseEvent(event);
}

void FinnButton::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        animateClick();
        event->accept();
        return;
    }
    QPushButton::keyPressEvent(event);
}

// ---- FinnIconButton ----

FinnIconButton::FinnIconButton(const QString& icon_name, int box_size,
                               int icon_size, QWidget* parent)
    : QAbstractButton(parent),
      icon_name_(icon_name),
      box_size_(box_size),
      icon_size_(icon_size) {
    setFixedSize(box_size, box_size);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::TabFocus);
    if (!icon_name_.isEmpty()) setAccessibleName(icon_name_);
    hover_spring_.init(this, springs::kSnappy, 0.0, [this](double v) {
        hover_progress_ = v;
        update();
    });
    press_spring_.init(this, springs::kSnappy, 1.0, [this](double v) {
        press_scale_ = v;
        update();
    });
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnIconButton::setIconName(const QString& icon_name) {
    icon_name_ = icon_name;
    if (accessibleName().isEmpty() && !icon_name_.isEmpty()) {
        setAccessibleName(icon_name_);
    }
    update();
}

QSize FinnIconButton::sizeHint() const {
    const int h = touch_target_height(box_size_, resolve_form_factor());
    return QSize(std::max(box_size_, h), std::max(box_size_, h));
}

void FinnIconButton::setActive(bool active) {
    active_ = active;
    update();
}

void FinnIconButton::setBadgeDot(bool show_dot) {
    badge_dot_ = show_dot;
    update();
}

void FinnIconButton::setIconTint(const QColor& tint) {
    custom_tint_ = tint;
    update();
}

void FinnIconButton::setCornerRadius(qreal radius) {
    radius_ = radius;
    update();
}

void FinnIconButton::setCustomHoverColor(const QColor& bg, const QColor& icon_tint) {
    hover_bg_ = bg;
    hover_tint_ = icon_tint;
    update();
}

void FinnIconButton::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    apply_press_scale(p, this, press_scale_);

    const QColor base_bg = (active_ || isChecked()) ? t.tile_on : QColor(Qt::transparent);
    const QColor target_hover_bg = hover_bg_.isValid() ? hover_bg_ : t.surface_hover;
    QColor bg = base_bg;
    if (isDown()) {
        bg = (hovered_ && hover_bg_.isValid()) ? hover_bg_.darker(110) : t.surface_pressed;
    } else if (hover_progress_ > 0.001) {
        bg = lerp_color(base_bg, target_hover_bg, hover_progress_);
    }

    if (bg.alpha() > 0) {
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                           radius_, radius_);
    }

    QColor tint = custom_tint_;
    if (!isEnabled()) {
        tint = t.mute;
    } else if (hover_tint_.isValid() && hover_progress_ > 0.5) {
        tint = hover_tint_;
    }
    // An active button gets a tile_on background; a monochrome glyph keeps the
    // theme ink on it and a coloured app icon keeps its own colours.

    const QRectF ir((width() - icon_size_) / 2.0, (height() - icon_size_) / 2.0,
                    icon_size_, icon_size_);
    FinnThemeManager::instance().paintIcon(&p, ir, icon_name_, tint);

    if (badge_dot_) {
        p.setBrush(t.accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(width() * 0.5, height() - 3.5), 2.2, 2.2);
    }
    draw_focus_ring(p, this, radius_, t);
}

void FinnIconButton::enterEvent(QEnterEvent*) {
    hovered_ = true;
    hover_spring_.retarget(1.0);
}

void FinnIconButton::leaveEvent(QEvent*) {
    hovered_ = false;
    hover_spring_.retarget(0.0);
    press_spring_.retarget(1.0, springs::kBouncy);
}

void FinnIconButton::mousePressEvent(QMouseEvent* event) {
    if (isEnabled() && event->button() == Qt::LeftButton) {
        press_spring_.retarget(0.95, springs::kSnappy);
    }
    QAbstractButton::mousePressEvent(event);
}

void FinnIconButton::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        press_spring_.retarget(1.0, springs::kBouncy);
    }
    QAbstractButton::mouseReleaseEvent(event);
}

void FinnIconButton::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        animateClick();
        event->accept();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

// ---- FinnToggle ----

FinnToggle::FinnToggle(bool checked, QWidget* parent)
    : QAbstractButton(parent), progress_(checked ? 1.0 : 0.0) {
    setCheckable(true);
    setChecked(checked);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(QStringLiteral("Toggle"));
    const QSize hint = sizeHint();
    if (resolve_form_factor() == FinnFormFactor::Desktop) {
        setFixedSize(hint);
    } else {
        setMinimumSize(hint);
    }
    setCursor(Qt::PointingHandCursor);

    toggle_spring_.init(this, springs::kSnappy, progress_, [this](double v) {
        progress_ = std::clamp(v, 0.0, 1.0);
        update();
    });
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

QSize FinnToggle::sizeHint() const {
    const int h = touch_target_height(22, resolve_form_factor());
    return QSize(std::max(38, h), h);
}

void FinnToggle::nextCheckState() {
    QAbstractButton::nextCheckState();
    const qreal target = isChecked() ? 1.0 : 0.0;
    toggle_spring_.retarget(target);
}

void FinnToggle::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        animateClick();
        event->accept();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

void FinnToggle::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF pill_r(std::max(0.0, (width() - 38.0) * 0.5),
                        std::max(0.0, (height() - 22.0) * 0.5), 38.0, 22.0);
    const QRectF track_r = pill_r.adjusted(1.0, 1.0, -1.0, -1.0);
    QColor off_col = t.track;
    QColor on_col = isEnabled() ? t.accent : t.mute;
    const QColor track_col(
        qRound(off_col.red() + (on_col.red() - off_col.red()) * progress_),
        qRound(off_col.green() + (on_col.green() - off_col.green()) * progress_),
        qRound(off_col.blue() + (on_col.blue() - off_col.blue()) * progress_),
        qRound(off_col.alpha() + (on_col.alpha() - off_col.alpha()) * progress_));

    p.setBrush(track_col);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(track_r, track_r.height() / 2.0, track_r.height() / 2.0);

    const qreal knob_d = track_r.height() - 4.0;
    const qreal min_x = track_r.left() + 2.0;
    const qreal max_x = track_r.right() - knob_d - 2.0;
    const qreal knob_x = min_x + (max_x - min_x) * progress_;

    p.setBrush(t.knob);
    p.drawEllipse(QRectF(knob_x, track_r.top() + 2.0, knob_d, knob_d));
    draw_focus_ring(p, this, track_r.height() / 2.0, t);
}

// ---- FinnSlider ----

FinnSlider::FinnSlider(int min, int max, int value, QWidget* parent)
    : QWidget(parent), min_(min), max_(max), value_(std::clamp(value, min, max)) {
    setFixedHeight(touch_target_height(22, resolve_form_factor()));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(QStringLiteral("Slider"));
    visual_ratio_ = currentRatio();
    ratio_spring_.init(this, springs::kSnappy, visual_ratio_, [this](double v) {
        visual_ratio_ = std::clamp(v, 0.0, 1.0);
        update();
    });
    knob_spring_.init(this, springs::kSnappy, knob_radius_, [this](double v) {
        knob_radius_ = v;
        update();
    });
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

qreal FinnSlider::currentRatio() const {
    if (max_ <= min_) return 0.0;
    return std::clamp<qreal>(qreal(value_ - min_) / (max_ - min_), 0.0, 1.0);
}

int FinnSlider::value() const { return value_; }

void FinnSlider::setRange(int min, int max) {
    min_ = min;
    max_ = std::max(min + 1, max);
    value_ = std::clamp(value_, min_, max_);
    ratio_spring_.setImmediate(currentRatio());
    update();
}

void FinnSlider::setValue(int v) {
    const int clamped = std::clamp(v, min_, max_);
    if (clamped == value_) return;
    value_ = clamped;
    if (dragging_) {
        ratio_spring_.setImmediate(currentRatio());
    } else {
        ratio_spring_.retarget(currentRatio());
    }
    update();
}

QSize FinnSlider::sizeHint() const {
    const FinnFormFactor ff = resolve_form_factor();
    return pill_ ? QSize(432, touch_target_height(36, ff))
                 : QSize(180, touch_target_height(22, ff));
}

void FinnSlider::setPillStyle(bool pill, const QString& icon_name, int icon_x,
                              int icon_size) {
    pill_ = pill;
    pill_icon_ = icon_name;
    pill_icon_x_ = icon_x;
    pill_icon_size_ = std::max(8, icon_size);
    const FinnFormFactor ff = resolve_form_factor();
    if (pill_) {
        setFixedHeight(touch_target_height(36, ff));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
        setFixedHeight(touch_target_height(22, ff));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    updateGeometry();
    update();
}

void FinnSlider::updateFromX(int x, bool immediate) {
    qreal ratio = 0.0;
    if (pill_) {
        ratio = std::clamp<qreal>(qreal(x) / std::max(1, width()), 0.0, 1.0);
    } else {
        const qreal margin = 9.0;
        const qreal usable = std::max<qreal>(1.0, width() - margin * 2.0);
        ratio = std::clamp<qreal>((x - margin) / usable, 0.0, 1.0);
    }
    const int next = qRound(min_ + ratio * (max_ - min_));
    const qreal snapped_ratio =
        (max_ > min_) ? std::clamp<qreal>(qreal(next - min_) / (max_ - min_), 0.0, 1.0)
                      : 0.0;
    if (immediate) {
        ratio_spring_.setImmediate(snapped_ratio);
    } else {
        ratio_spring_.retarget(snapped_ratio);
    }
    if (next != value_) {
        value_ = next;
        update();
        emit valueChanged(value_);
    }
}

void FinnSlider::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (pill_) {
        const QRectF bounds(0.0, 0.0, width(), height());
        const qreal r = height() / 2.0;
        QPainterPath clip_path;
        clip_path.addRoundedRect(bounds, r, r);

        const QColor track_bg = token_ink_alpha(
            isEnabled() ? (t.dark ? 0x99 : 0x80) : (t.dark ? 0x66 : 0x4D));
        p.setPen(Qt::NoPen);
        p.setBrush(track_bg);
        p.drawPath(clip_path);

        const qreal ratio = visual_ratio_;
        // Minimum fill matches height (36px) so the left icon stays on the light pill.
        const qreal min_fill_w = std::min<qreal>(width(), height());
        const qreal fill_w = std::max(min_fill_w, ratio * width());

        // Keep pill fill opaque so translucent white never muddies into flat grey over dark track.
        const QColor fill_color =
            t.dark ? QColor::fromRgba(colors::ink_dark)
                   : QColor::fromRgba(colors::knob_light);

        p.save();
        p.setClipPath(clip_path);
        p.setBrush(fill_color);
        p.drawRect(QRectF(0.0, 0.0, fill_w, height()));
        p.restore();

        if (!pill_icon_.isEmpty()) {
            const qreal iy = (height() - pill_icon_size_) / 2.0;
            const QColor icon_tint = token_ink_alpha(isEnabled() ? 255 : 120);
            FinnThemeManager::instance().paintIcon(
                &p, QRectF(pill_icon_x_, iy, pill_icon_size_, pill_icon_size_),
                pill_icon_, icon_tint);
        }
        draw_focus_ring(p, this, r, t);
        return;
    }

    const qreal margin = 9.0;
    const qreal cy = height() / 2.0;
    const qreal track_w = std::max<qreal>(10.0, width() - margin * 2.0);
    const QRectF track(margin, cy - 2.5, track_w, 5.0);

    p.setPen(Qt::NoPen);
    p.setBrush(t.track);
    p.drawRoundedRect(track, 2.5, 2.5);

    const qreal ratio = visual_ratio_;
    const qreal handle_x = margin + ratio * track_w;

    p.setBrush(isEnabled() ? t.accent : t.mute);
    p.drawRoundedRect(QRectF(track.left(), track.top(),
                             std::max<qreal>(5.0, handle_x - track.left()), 5.0),
                       2.5, 2.5);

    const qreal r = knob_radius_;
    p.setBrush(t.knob);
    p.setPen(QPen(t.hair, 1.0));
    p.drawEllipse(QPointF(handle_x, cy), r, r);
    draw_focus_ring(p, this, kRadiusMd, t);
}

void FinnSlider::mousePressEvent(QMouseEvent* event) {
    if (!isEnabled() || event->button() != Qt::LeftButton) return;
    dragging_ = true;
    knob_spring_.retarget(8.5, springs::kSnappy);
    updateFromX(qRound(event->position().x()), false);
}

void FinnSlider::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) updateFromX(qRound(event->position().x()), true);
}

void FinnSlider::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = false;
    knob_spring_.retarget(hovered_ ? 8.5 : 7.0, springs::kBouncy);
    update();
}

void FinnSlider::enterEvent(QEnterEvent*) {
    hovered_ = true;
    knob_spring_.retarget(8.5, springs::kSnappy);
    update();
}

void FinnSlider::leaveEvent(QEvent*) {
    hovered_ = false;
    if (!dragging_) knob_spring_.retarget(7.0, springs::kSnappy);
    update();
}

void FinnSlider::keyPressEvent(QKeyEvent* event) {
    if (!isEnabled()) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int step = std::max(1, (max_ - min_) / 20);
    int next = value_;
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down) {
        next = std::clamp(value_ - step, min_, max_);
    } else if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Up) {
        next = std::clamp(value_ + step, min_, max_);
    } else if (event->key() == Qt::Key_Home) {
        next = min_;
    } else if (event->key() == Qt::Key_End) {
        next = max_;
    } else {
        QWidget::keyPressEvent(event);
        return;
    }
    if (next != value_) {
        setValue(next);
        emit valueChanged(value_);
    }
    event->accept();
}

// ---- FinnSearchField ----

FinnSearchField::FinnSearchField(const QString& placeholder, QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(36);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(34, 2, 6, 2);
    lay->setSpacing(4);

    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(placeholder);
    edit_->setFrame(false);
    edit_->setFont(finn_font(10.5, QFont::Normal));
    edit_->installEventFilter(this);

    clear_btn_ = new FinnIconButton(QStringLiteral("x"), 24, 12, this);
    clear_btn_->setVisible(false);

    lay->addWidget(edit_, 1);
    lay->addWidget(clear_btn_);

    syncPalette();

    connect(edit_, &QLineEdit::textChanged, this, [this](const QString& s) {
        clear_btn_->setVisible(!s.isEmpty());
        emit textChanged(s);
    });
    connect(edit_, &QLineEdit::returnPressed, this,
            [this]() { emit submitted(edit_->text()); });
    connect(clear_btn_, &QAbstractButton::clicked, edit_, &QLineEdit::clear);
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, &FinnSearchField::syncPalette);
}

void FinnSearchField::syncPalette() {
    const auto& t = FinnThemeManager::instance().tokens();
    QPalette p = edit_->palette();
    p.setColor(QPalette::Base, Qt::transparent);
    p.setColor(QPalette::Text, t.ink);
    p.setColor(QPalette::PlaceholderText, t.mute);
    edit_->setPalette(p);
    update();
}

QString FinnSearchField::text() const { return edit_->text(); }
void FinnSearchField::setText(const QString& text) { edit_->setText(text); }
void FinnSearchField::setPlaceholderText(const QString& text) {
    edit_->setPlaceholderText(text);
}
void FinnSearchField::setPillMode(bool pill) {
    pill_ = pill;
    update();
}
void FinnSearchField::focusInput() { edit_->setFocus(Qt::ShortcutFocusReason); }

bool FinnSearchField::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == edit_) {
        if (ev->type() == QEvent::FocusIn) {
            focused_ = true;
            update();
        } else if (ev->type() == QEvent::FocusOut) {
            focused_ = false;
            update();
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void FinnSearchField::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
    p.setBrush(t.surface_input);
    p.setPen(QPen(focused_ ? t.accent : t.hair, focused_ ? 1.5 : 1.0));
    const qreal rad = pill_ ? (r.height() / 2.0) : kRadiusMd;
    p.drawRoundedRect(r, rad, rad);

    FinnThemeManager::instance().paintIcon(
        &p, QRectF(11, (height() - 15) / 2.0, 15, 15),
        QStringLiteral("search-01"), focused_ ? t.accent : t.dim);
}

// ---- FinnTextField ----

FinnTextField::FinnTextField(const QString& placeholder, QWidget* parent)
    : QLineEdit(parent) {
    setPlaceholderText(placeholder);
    setMinimumHeight(34);
    setFont(finn_font(10.5, QFont::Normal));
}

void FinnTextField::setPasswordMode(bool enabled) {
    setEchoMode(enabled ? QLineEdit::Password : QLineEdit::Normal);
    if (enabled) {
        setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoAutoUppercase |
                            Qt::ImhNoPredictiveText);
    } else {
        setInputMethodHints(Qt::ImhNone);
    }
}

// ---- FinnProgressBar ----

FinnProgressBar::FinnProgressBar(int height_px, QWidget* parent)
    : QWidget(parent), height_px_(height_px) {
    setFixedHeight(std::max(4, height_px_));
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnProgressBar::setValue(double ratio) {
    value_ = std::clamp(ratio, 0.0, 1.0);
    update();
}

void FinnProgressBar::setUseLightFill(bool white_fill) {
    white_fill_ = white_fill;
    update();
}

void FinnProgressBar::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF track(0, 0, width(), height());
    const qreal rad = height() / 2.0;
    p.setPen(Qt::NoPen);
    p.setBrush(t.track);
    p.drawRoundedRect(track, rad, rad);

    if (value_ > 0.001) {
        QRectF fill = track;
        fill.setWidth(std::max<qreal>(height(), track.width() * value_));
        p.setBrush(white_fill_ ? QColor::fromRgba(colors::knob_light) : t.accent);
        p.drawRoundedRect(fill, rad, rad);
    }
}

// ---- FinnProgressRing ----

FinnProgressRing::FinnProgressRing(int size_px, QWidget* parent)
    : QWidget(parent), size_px_(size_px) {
    setFixedSize(size_px, size_px);
    spin_timer_ = new QTimer(this);
    spin_timer_->setInterval(16);
    connect(spin_timer_, &QTimer::timeout, this, [this]() {
        if (is_reduced_motion_active()) {
            spin_timer_->stop();
            update();
            return;
        }
        angle_deg_ = (angle_deg_ + 9) % 360;
        update();
    });
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, [this]() {
                if (is_reduced_motion_active()) spin_timer_->stop();
                update();
            });
}

void FinnProgressRing::setSpinning(bool spinning) {
    if (spinning && ratio_ < 0.0 && !is_reduced_motion_active()) {
        if (!spin_timer_->isActive()) spin_timer_->start();
    } else {
        spin_timer_->stop();
    }
    update();
}

void FinnProgressRing::setValue(double ratio) {
    ratio_ = ratio;
    if (ratio_ >= 0.0) spin_timer_->stop();
    update();
}

void FinnProgressRing::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
    p.setPen(QPen(t.track, 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(r);

    p.setPen(QPen(t.accent, 2.2, Qt::SolidLine, Qt::RoundCap));
    if (ratio_ >= 0.0) {
        const int span = qRound(-360.0 * std::clamp(ratio_, 0.0, 1.0) * 16.0);
        p.drawArc(r, 90 * 16, span);
    } else if (is_reduced_motion_active()) {
        p.drawArc(r, 90 * 16, -110 * 16);
    } else {
        p.drawArc(r, (90 - angle_deg_) * 16, -110 * 16);
    }
}

// ---- FinnNavItem ----

FinnNavItem::FinnNavItem(const QString& icon_name, const QString& label,
                         QWidget* parent)
    : QAbstractButton(parent), icon_name_(icon_name) {
    setText(label);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(label);
    setFixedHeight(touch_target_height(34, resolve_form_factor()));
    setFont(finn_font(10.0, QFont::Medium));
    hover_spring_.init(this, springs::kSnappy, 0.0, [this](double v) {
        hover_progress_ = v;
        update();
    });
    press_spring_.init(this, springs::kSnappy, 1.0, [this](double v) {
        press_scale_ = v;
        update();
    });
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnNavItem::setBadgeText(const QString& badge) {
    badge_ = badge;
    update();
}

void FinnNavItem::setBadgeColor(const QColor& color) {
    badge_color_ = color;
    badge_dark_color_ = QColor();
    update();
}

void FinnNavItem::setBadgeColor(const QColor& light, const QColor& dark) {
    badge_color_ = light;
    badge_dark_color_ = dark;
    update();
}

void FinnNavItem::setIconName(const QString& icon_name) {
    icon_name_ = icon_name;
    update();
}

void FinnNavItem::setSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    update();
}

QSize FinnNavItem::sizeHint() const {
    return QSize(226, touch_target_height(36, resolve_form_factor()));
}

void FinnNavItem::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    apply_press_scale(p, this, press_scale_);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = badge_color_.isValid() ? 5.0 : kRadiusMd;
    if (selected_) {
        p.setBrush(badge_color_.isValid() ? t.nav_sel : t.tile_on);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, radius, radius);
    } else if (hover_progress_ > 0.001) {
        p.setBrush(lerp_color(Qt::transparent, t.surface_hover, hover_progress_));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, radius, radius);
    }

    const QColor fg = (selected_ && !badge_color_.isValid()) ? t.on : t.ink;
    int text_left = 12;
    if (!icon_name_.isEmpty()) {
        if (badge_color_.isValid()) {
            // Figma Settings 26x26 circular icon badge with 17x17 white icon.
            const QRectF circle(5, (height() - 26) / 2.0, 26, 26);
            QColor resolved_badge = (t.dark && badge_dark_color_.isValid())
                                        ? badge_dark_color_
                                        : badge_color_;
            if (t.dark && !badge_dark_color_.isValid() &&
                badge_color_.lightness() < 60) {
                resolved_badge = t.surface_input;
            }
            p.setBrush(resolved_badge);
            p.setPen(t.dark ? QPen(t.hair, 1.0) : Qt::NoPen);
            p.drawEllipse(circle);
            FinnThemeManager::instance().paintIcon(
                &p, QRectF(circle.x() + 4.5, circle.y() + 4.5, 17, 17),
                icon_name_, QColor::fromRgba(colors::fill_light));
            text_left = 41;
        } else {
            FinnThemeManager::instance().paintIcon(
                &p, QRectF(10, (height() - 16) / 2.0, 16, 16), icon_name_, fg);
            text_left = 34;
        }
    }

    p.setFont(finn_font_px(13, QFont::Normal));
    p.setPen(fg);
    const int badge_reserve = badge_.isEmpty() ? 8 : 42;
    p.drawText(QRect(text_left, 0, width() - text_left - badge_reserve, height()),
               Qt::AlignVCenter | Qt::AlignLeft, text());

    if (!badge_.isEmpty()) {
        p.setFont(finn_mono_font(8.5, QFont::Normal));
        p.setPen(selected_ ? t.on : t.mute);
        p.drawText(QRect(width() - 40, 0, 30, height()),
                   Qt::AlignVCenter | Qt::AlignRight, badge_);
    }
    draw_focus_ring(p, this, radius, t);
}

void FinnNavItem::enterEvent(QEnterEvent*) {
    hovered_ = true;
    hover_spring_.retarget(1.0);
}

void FinnNavItem::leaveEvent(QEvent*) {
    hovered_ = false;
    hover_spring_.retarget(0.0);
    press_spring_.retarget(1.0, springs::kBouncy);
}

void FinnNavItem::mousePressEvent(QMouseEvent* event) {
    if (isEnabled() && event->button() == Qt::LeftButton) {
        press_spring_.retarget(0.985, springs::kSnappy);
    }
    QAbstractButton::mousePressEvent(event);
}

void FinnNavItem::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        press_spring_.retarget(1.0, springs::kBouncy);
    }
    QAbstractButton::mouseReleaseEvent(event);
}

void FinnNavItem::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        animateClick();
        event->accept();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

// ---- FinnSidebar ----

FinnSidebar::FinnSidebar(int width_px, QWidget* parent)
    : QWidget(parent),
      desktop_width_(width_px),
      form_factor_(resolve_form_factor()) {
    setFormFactor(form_factor_);
    root_layout_ = new QVBoxLayout(this);
    root_layout_->setContentsMargins(12, 24, 12, 12);
    root_layout_->setSpacing(10);

    header_label_ = new FinnLabel(QString(), FinnLabel::Role::Title,
                                  FinnLabel::Tone::Ink, this);
    header_label_->setVisible(false);
    auto sync_header_font = [this]() {
        QFont f = finn_font(12.0, QFont::Normal);
        f.setPixelSize(16);
        header_label_->setFont(f);
    };
    sync_header_font();
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            header_label_, sync_header_font);
    root_layout_->addWidget(header_label_);

    search_field_ = new FinnSearchField(QStringLiteral("Search"), this);
    search_field_->setFixedHeight(30);
    search_field_->setPillMode(true);
    search_field_->setVisible(false);
    connect(search_field_, &FinnSearchField::textChanged,
            this, &FinnSidebar::filterItems);
    root_layout_->addWidget(search_field_);

    pinned_layout_ = new QVBoxLayout();
    pinned_layout_->setContentsMargins(0, 0, 0, 0);
    pinned_layout_->setSpacing(4);
    root_layout_->addLayout(pinned_layout_);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto* container = new QWidget(scroll);
    container->setAutoFillBackground(false);
    items_layout_ = new QVBoxLayout(container);
    items_layout_->setContentsMargins(0, 0, 0, 0);
    items_layout_->setSpacing(5);

    scroll->setWidget(container);
    install_overscroll_physics(scroll);
    root_layout_->addWidget(scroll, 1);
}

void FinnSidebar::setFormFactor(FinnFormFactor form_factor) {
    form_factor_ = form_factor;
    int target_w = desktop_width_;
    if (form_factor_ == FinnFormFactor::Tablet) {
        target_w = std::min(desktop_width_, 210);
    } else if (form_factor_ == FinnFormFactor::Phone) {
        target_w = std::min(desktop_width_, 260);
    }
    setFixedWidth(target_w);
    const int item_h = touch_target_height(34, form_factor_);
    for (const auto& pair : pinned_items_) {
        if (pair.second) pair.second->setFixedHeight(item_h);
    }
    for (const auto& pair : items_) {
        if (pair.second) pair.second->setFixedHeight(item_h);
    }
    updateGeometry();
}

void FinnSidebar::setHeaderTitle(const QString& title) {
    header_label_->setText(title);
    QFont f = finn_font(12.0, QFont::Normal);
    f.setPixelSize(16);
    header_label_->setFont(f);
    header_label_->setVisible(!title.isEmpty());
}

void FinnSidebar::setSearchEnabled(bool enabled, const QString& placeholder) {
    search_field_->setPlaceholderText(placeholder);
    search_field_->setVisible(enabled);
}

void FinnSidebar::addSectionHeader(const QString& title) {
    auto* lbl = new FinnLabel(title.toUpper(), FinnLabel::Role::Caption,
                              FinnLabel::Tone::Mute, this);
    lbl->setContentsMargins(10, 10, 6, 4);
    items_layout_->addWidget(lbl);
}

void FinnSidebar::addSectionSpacer(int height_px) {
    items_layout_->addSpacing(height_px);
}

FinnNavItem* FinnSidebar::createAndWireItem(QVBoxLayout* target_layout,
                                            const QString& id,
                                            const QString& icon_name,
                                            const QString& label,
                                            const QColor& badge_color,
                                            const QString& badge, bool pinned) {
    auto* item = new FinnNavItem(icon_name, label, this);
    if (!badge.isEmpty()) item->setBadgeText(badge);
    if (badge_color.isValid()) item->setBadgeColor(badge_color);
    target_layout->addWidget(item);
    (pinned ? pinned_items_ : items_).push_back({id, item});

    connect(item, &QAbstractButton::clicked, this, [this, id]() {
        setCurrentId(id);
        emit selected(id);
    });
    if (!pinned && items_.size() == 1 && current_id_.isEmpty()) setCurrentId(id);
    return item;
}

FinnNavItem* FinnSidebar::addPinnedItem(const QString& id,
                                        const QString& icon_name,
                                        const QString& label,
                                        const QColor& badge_color,
                                        const QString& badge) {
    pinned_layout_->setContentsMargins(0, 2, 0, 4);
    return createAndWireItem(pinned_layout_, id, icon_name, label, badge_color,
                             badge, true);
}

FinnNavItem* FinnSidebar::addItem(const QString& id, const QString& icon_name,
                                  const QString& label, const QString& badge) {
    return addItem(id, icon_name, label, QColor(), badge);
}

FinnNavItem* FinnSidebar::addItem(const QString& id, const QString& icon_name,
                                  const QString& label, const QColor& badge_color,
                                  const QString& badge) {
    return createAndWireItem(items_layout_, id, icon_name, label, badge_color,
                             badge, false);
}

FinnNavItem* FinnSidebar::addItem(const QString& id, const QString& icon_name,
                                  const QString& label,
                                  const QColor& light_badge,
                                  const QColor& dark_badge,
                                  const QString& badge) {
    auto* item = createAndWireItem(items_layout_, id, icon_name, label,
                                   light_badge, badge, false);
    if (item && dark_badge.isValid()) {
        item->setBadgeColor(light_badge, dark_badge);
    }
    return item;
}

void FinnSidebar::filterItems(const QString& query) {
    const QString q = query.trimmed().toLower();
    for (const auto& pair : items_) {
        const bool match = q.isEmpty() || pair.second->text().toLower().contains(q);
        pair.second->setVisible(match);
    }
}

void FinnSidebar::addStretch() { items_layout_->addStretch(1); }

void FinnSidebar::addFooterWidget(QWidget* widget) {
    if (root_layout_) {
        root_layout_->addWidget(widget);
    }
}

void FinnSidebar::setCurrentId(const QString& id) {
    current_id_ = id;
    for (const auto& pair : pinned_items_) {
        pair.second->setSelected(pair.first == id);
    }
    for (const auto& pair : items_) {
        pair.second->setSelected(pair.first == id);
    }
}

// ---- FinnStatCard ----

FinnStatCard::FinnStatCard(const QString& caption, const QString& value,
                           QWidget* parent)
    : FinnStatCard(caption, value, {}, parent) {}

FinnStatCard::FinnStatCard(const QString& caption, const QString& primary_value,
                           const QString& secondary_suffix, QWidget* parent)
    : QWidget(parent),
      caption_(caption),
      value_(primary_value),
      suffix_(secondary_suffix) {
    setMinimumSize(210, 70);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(70);
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnStatCard::setCaption(const QString& caption) {
    caption_ = caption;
    update();
}

void FinnStatCard::setValue(const QString& value) {
    value_ = value;
    suffix_.clear();
    update();
}

void FinnStatCard::setValueParts(const QString& primary, const QString& suffix) {
    value_ = primary;
    suffix_ = suffix;
    update();
}

void FinnStatCard::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(t.tile);
    p.setPen(QPen(t.hair, 1.0));
    p.drawRoundedRect(r, kRadiusMd, kRadiusMd);

    const QColor muted_half = t.dim;

    QFont cap_font = finn_font(9.5, QFont::Normal);
    cap_font.setPixelSize(12);
    p.setFont(cap_font);
    p.setPen(muted_half);
    p.drawText(QRect(10, 10, width() - 20, 16),
               Qt::AlignLeft | Qt::AlignVCenter, caption_);

    QFont val_font = finn_font(10.5, QFont::Normal);
    val_font.setPixelSize(14);
    p.setFont(val_font);
    const QFontMetrics fm(val_font);
    const int avail_w = std::max(10, width() - 20);
    const QRect val_rect(10, height() - 26, avail_w, 18);

    if (suffix_.isEmpty()) {
        p.setPen(t.ink);
        p.drawText(val_rect, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(value_, Qt::ElideRight, avail_w));
    } else {
        const QString primary = fm.elidedText(value_, Qt::ElideRight, avail_w);
        const int primary_w = fm.horizontalAdvance(primary);
        p.setPen(t.ink);
        p.drawText(val_rect, Qt::AlignLeft | Qt::AlignVCenter, primary);

        const int gap = suffix_.startsWith(QLatin1Char(' '))
                            ? 0
                            : std::max(5, fm.horizontalAdvance(QLatin1Char(' ')));
        const int rem_w = avail_w - primary_w - gap;
        if (rem_w > 6) {
            const QString suf = fm.elidedText(suffix_, Qt::ElideRight, rem_w);
            p.setPen(muted_half);
            p.drawText(QRect(10 + primary_w + gap, val_rect.y(), rem_w,
                             val_rect.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, suf);
        }
    }
}

// ---- FinnHeroCard ----

FinnHeroCard::FinnHeroCard(const QString& title, const QString& subtitle,
                           const QString& icon_name, QWidget* parent)
    : QWidget(parent), title_(title), subtitle_(subtitle), icon_name_(icon_name) {
    setMinimumSize(210, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnHeroCard::setTitle(const QString& title) {
    title_ = title;
    update();
}

void FinnHeroCard::setInlineSuffix(const QString& suffix) {
    inline_suffix_ = suffix;
    update();
}

void FinnHeroCard::setSubtitle(const QString& subtitle) {
    subtitle_ = subtitle;
    update();
}

void FinnHeroCard::setIconName(const QString& icon_name) {
    icon_name_ = icon_name;
    update();
}

void FinnHeroCard::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    // The identity card is the one tinted surface on the screen. Keep the tint
    // solid: a white-to-green gradient made the chrome look like a landing page.
    p.setBrush(t.hero_tint);
    p.setPen(QPen(t.hair, 1.0));
    p.drawRoundedRect(r, kRadiusMd, kRadiusMd);

    QFont f = finn_font(12.0, QFont::Normal);
    f.setPixelSize(16);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int title_w = fm.horizontalAdvance(title_);
    const int suffix_w =
        inline_suffix_.isEmpty() ? 0 : fm.horizontalAdvance(inline_suffix_);
    const int text_w = title_w + suffix_w;
    const int icon_sz = 22;
    const int gap = (!icon_name_.isEmpty() && text_w > 0) ? 10 : 0;
    const int total_w = (icon_name_.isEmpty() ? 0 : icon_sz) + gap + text_w;
    const int start_x = std::max(12, (width() - total_w) / 2);
    const int cy = subtitle_.isEmpty() ? (height() / 2) : (height() / 2 - 12);

    if (!icon_name_.isEmpty()) {
        FinnThemeManager::instance().paintIcon(
            &p, QRectF(start_x, cy - icon_sz / 2.0, icon_sz, icon_sz),
            icon_name_, t.ink);
    }

    const int text_x = start_x + (icon_name_.isEmpty() ? 0 : icon_sz + gap);
    const int avail_text_w = std::max(10, width() - text_x - 10);
    const QString primary = fm.elidedText(title_, Qt::ElideRight, avail_text_w);
    const int drawn_title_w = fm.horizontalAdvance(primary);

    p.setPen(t.ink);
    p.drawText(QRect(text_x, cy - 12, avail_text_w, 24),
               Qt::AlignLeft | Qt::AlignVCenter, primary);

    if (!inline_suffix_.isEmpty()) {
        const int rem_w = avail_text_w - drawn_title_w;
        if (rem_w > 4) {
            p.setPen(t.dim);
            p.drawText(QRect(text_x + drawn_title_w, cy - 12, rem_w, 24),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(inline_suffix_, Qt::ElideRight, rem_w));
        }
    }

    if (!subtitle_.isEmpty()) {
        p.setFont(finn_font(9.0, QFont::Normal));
        p.setPen(t.dim);
        p.drawText(QRect(14, cy + 18, width() - 28, 18),
                   Qt::AlignHCenter | Qt::AlignVCenter, subtitle_);
    }
}

// ---- FinnNavRail ----

namespace {
class RailButton final : public QAbstractButton {
public:
    RailButton(const QString& icon_name, const QString& label, QWidget* parent)
        : QAbstractButton(parent), icon_name_(icon_name) {
        setText(label);
        setCheckable(true);
        setFixedSize(56, 52);
        setCursor(Qt::PointingHandCursor);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
        if (isChecked()) {
            p.setBrush(t.tile_on);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r, kRadiusMd, kRadiusMd);
            p.setBrush(t.accent);
            p.drawRoundedRect(QRectF(2, 13, 3, height() - 26), 1.5, 1.5);
        } else if (underMouse()) {
            p.setBrush(t.surface_hover);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r, kRadiusMd, kRadiusMd);
        }
        const QColor fg = isChecked() ? t.on : t.dim;
        FinnThemeManager::instance().paintIcon(
            &p, QRectF((width() - 20) / 2.0, 8, 20, 20), icon_name_, fg);
        p.setFont(finn_font(8.2, isChecked() ? QFont::DemiBold : QFont::Normal));
        p.setPen(fg);
        p.drawText(QRect(2, 31, width() - 4, 16),
                   Qt::AlignHCenter | Qt::AlignVCenter, text());
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
private:
    QString icon_name_;
};
}  // namespace

FinnNavRail::FinnNavRail(int width_px, QWidget* parent) : QWidget(parent) {
    setFixedWidth(width_px);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 12, 6, 12);
    root->setSpacing(6);

    top_layout_ = new QVBoxLayout();
    top_layout_->setSpacing(4);
    bottom_layout_ = new QVBoxLayout();
    bottom_layout_->setSpacing(4);

    root->addLayout(top_layout_);
    root->addStretch(1);
    root->addLayout(bottom_layout_);
}

void FinnNavRail::addItem(const QString& id, const QString& icon_name,
                          const QString& label, bool bottom) {
    auto* btn = new RailButton(icon_name, label, this);
    (bottom ? bottom_layout_ : top_layout_)->addWidget(btn, 0, Qt::AlignHCenter);
    entries_.push_back({id, icon_name, label, btn});
    connect(btn, &QAbstractButton::clicked, this, [this, id]() {
        setCurrentId(id);
        emit selected(id);
    });
    if (entries_.size() == 1) setCurrentId(id);
}

void FinnNavRail::setCurrentId(const QString& id) {
    current_id_ = id;
    for (const auto& e : entries_) {
        e.btn->setChecked(e.id == id);
    }
}

// ---- FinnSettingRow ----

FinnSettingRow::FinnSettingRow(const QString& title, const QString& subtitle,
                               const QString& icon_name, QWidget* trailing,
                               QWidget* parent)
    : FinnCard(kRadiusMd, false, parent) {
    row_layout_ = new QHBoxLayout(this);
    row_layout_->setContentsMargins(14, 12, 14, 12);
    row_layout_->setSpacing(12);

    icon_box_ = new DynamicIconWidget(icon_name, 30, 16, true, false, this);
    row_layout_->addWidget(icon_box_, 0, Qt::AlignVCenter);
    setAccessibleName(title);
    if (!subtitle.isEmpty()) setAccessibleDescription(subtitle);

    auto* text_col = new QVBoxLayout();
    text_col->setContentsMargins(0, 0, 0, 0);
    text_col->setSpacing(2);
    auto* title_lbl = new FinnLabel(title, FinnLabel::Role::Body,
                                    FinnLabel::Tone::Ink, this);
    title_lbl->setFont(finn_font(10.5, QFont::Medium));
    text_col->addWidget(title_lbl);

    subtitle_label_ = new FinnLabel(subtitle, FinnLabel::Role::Caption,
                                    FinnLabel::Tone::Dim, this);
    subtitle_label_->setWordWrap(true);
    subtitle_label_->setVisible(!subtitle.isEmpty());
    text_col->addWidget(subtitle_label_);

    row_layout_->addLayout(text_col, 1);

    if (trailing) setTrailingWidget(trailing);
}

void FinnSettingRow::setTrailingWidget(QWidget* trailing) {
    if (trailing_) {
        row_layout_->removeWidget(trailing_);
        trailing_->deleteLater();
    }
    trailing_ = trailing;
    if (trailing_) {
        trailing_->setParent(this);
        row_layout_->addWidget(trailing_, 0, Qt::AlignVCenter);
    }
}

void FinnSettingRow::setSubtitle(const QString& subtitle) {
    subtitle_label_->setText(subtitle);
    subtitle_label_->setVisible(!subtitle.isEmpty());
    setAccessibleDescription(subtitle);
}

void FinnSettingRow::setIconName(const QString& icon_name) {
    if (auto* box = static_cast<DynamicIconWidget*>(icon_box_)) {
        box->setIconName(icon_name);
    }
}

void FinnSettingRow::setIconBadgeColor(const QColor& color) {
    if (auto* box = static_cast<DynamicIconWidget*>(icon_box_)) {
        box->setBadgeColor(color);
    }
}

// ---- FinnBadge ----

FinnBadge::FinnBadge(const QString& text, Tone tone, QWidget* parent)
    : QWidget(parent), text_(text), tone_(tone) {
    setFont(finn_font(8.8, QFont::DemiBold));
    setFixedHeight(22);
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnBadge::setText(const QString& text) {
    text_ = text;
    updateGeometry();
    update();
}

void FinnBadge::setTone(Tone tone) {
    tone_ = tone;
    update();
}

QSize FinnBadge::sizeHint() const {
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text_) + 16, 22);
}

void FinnBadge::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor bg = t.surface_hover;
    QColor fg = t.dim;
    switch (tone_) {
        case Tone::Neutral:
            break;
        case Tone::Accent:
            bg = t.tile_on;
            fg = t.on;
            break;
        case Tone::Success:
            bg = t.success;
            bg.setAlpha(t.dark ? 45 : 30);
            fg = t.success;
            break;
        case Tone::Warning:
            bg = t.warning;
            bg.setAlpha(t.dark ? 45 : 30);
            fg = t.warning;
            break;
        case Tone::Danger:
            bg = t.danger;
            bg.setAlpha(t.dark ? 45 : 30);
            fg = t.danger;
            break;
    }

    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 11.0, 11.0);
    p.setFont(font());
    p.setPen(fg);
    p.drawText(rect(), Qt::AlignCenter, text_);
}

// ---- FinnDivider ----

FinnDivider::FinnDivider(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent), orientation_(orientation) {
    if (orientation_ == Qt::Horizontal) {
        setFixedHeight(1);
    } else {
        setFixedWidth(1);
    }
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnDivider::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.fillRect(rect(), t.hair);
}

// ---- FinnTabBar ----

FinnTabBar::FinnTabBar(QWidget* parent) : QWidget(parent) {
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);
}

void FinnTabBar::setTabs(const QStringList& labels) {
    for (auto* b : buttons_) {
        layout_->removeWidget(b);
        b->deleteLater();
    }
    buttons_.clear();
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = new FinnButton(labels[i], FinnButton::Variant::Secondary, {}, this);
        b->setCheckable(true);
        b->setFixedHeight(28);
        connect(b, &QAbstractButton::clicked, this, [this, i]() {
            setCurrentIndex(i);
            emit tabChanged(i);
        });
        layout_->addWidget(b);
        buttons_.push_back(b);
    }
    if (!buttons_.isEmpty()) setCurrentIndex(0);
}

void FinnTabBar::setCurrentIndex(int index) {
    if (index < 0 || index >= buttons_.size()) return;
    current_ = index;
    for (int i = 0; i < buttons_.size(); ++i) {
        buttons_[i]->setChecked(i == index);
    }
}

// ---- FinnBreadcrumbs ----

FinnBreadcrumbs::FinnBreadcrumbs(QWidget* parent) : QWidget(parent) {
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(2);
}

void FinnBreadcrumbs::setSegments(const QVector<QPair<QString, QString>>& segments) {
    while (QLayoutItem* item = layout_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    for (int i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            auto* sep = new FinnLabel(QStringLiteral("/"), FinnLabel::Role::Caption,
                                      FinnLabel::Tone::Mute, this);
            sep->setContentsMargins(2, 0, 2, 0);
            layout_->addWidget(sep);
        }
        const QString target = segments[i].second;
        auto* btn = new FinnButton(segments[i].first, FinnButton::Variant::Ghost,
                                   {}, this);
        btn->setFixedHeight(26);
        btn->setMinimumWidth(24);
        connect(btn, &QAbstractButton::clicked, this,
                [this, target]() { emit segmentActivated(target); });
        layout_->addWidget(btn);
    }
    layout_->addStretch(1);
}

// ---- FinnEmptyState ----

FinnEmptyState::FinnEmptyState(const QString& icon_name, const QString& title,
                               const QString& description,
                               const QString& action_label, QWidget* parent)
    : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setAlignment(Qt::AlignCenter);
    root->setSpacing(10);

    if (!icon_name.isEmpty()) {
        icon_box_ = new DynamicIconWidget(icon_name, 44, 36, false, true, this);
        root->addWidget(icon_box_, 0, Qt::AlignCenter);
    }

    auto* t_lbl = new FinnLabel(title, FinnLabel::Role::Headline,
                                FinnLabel::Tone::Ink, this);
    t_lbl->setAlignment(Qt::AlignCenter);
    root->addWidget(t_lbl);

    if (!description.isEmpty()) {
        auto* d_lbl = new FinnLabel(description, FinnLabel::Role::Body,
                                    FinnLabel::Tone::Dim, this);
        d_lbl->setWordWrap(true);
        d_lbl->setAlignment(Qt::AlignCenter);
        d_lbl->setMaximumWidth(360);
        root->addWidget(d_lbl);
    }

    if (!action_label.isEmpty()) {
        action_btn_ =
            new FinnButton(action_label, FinnButton::Variant::Primary, {}, this);
        connect(action_btn_, &QAbstractButton::clicked, this,
                &FinnEmptyState::actionClicked);
        root->addWidget(action_btn_, 0, Qt::AlignCenter);
    }
}

// ---- FinnToast ----

FinnToast::FinnToast(QWidget* parent) : FinnGlassPanel(kRadiusLg, parent) {
    setVisible(false);
    setOpacityAlpha(240);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(18, 9, 14, 9);
    label_ = new FinnLabel({}, FinnLabel::Role::Body, FinnLabel::Tone::Ink, this);
    label_->setFont(finn_font(10.0, QFont::Medium));
    lay->addWidget(label_);

    entrance_spring_.init(this, springs::kSnappy, 1.0, [this](double v) {
        entrance_progress_ = std::clamp(v, 0.0, 1.15);
        const qreal alpha_u = std::clamp<qreal>(v, 0.0, 1.0);
        setOpacityAlpha(qRound(240.0 * alpha_u));
        if (reduced_fade_) {
            move(target_pos_);
        } else {
            move(target_pos_.x(),
                 target_pos_.y() + qRound((1.0 - v) * 8.0));
        }
        update();
    });

    hide_timer_ = new QTimer(this);
    hide_timer_->setSingleShot(true);
    connect(hide_timer_, &QTimer::timeout, this, &QWidget::hide);
}

void FinnToast::repositionInParent() {
    if (!parentWidget()) return;
    adjustSize();
    const int x = (parentWidget()->width() - width()) / 2;
    const int y = parentWidget()->height() - height() - 18;
    target_pos_ = QPoint(std::max(12, x), std::max(56, y));
    if (reduced_fade_) {
        move(target_pos_);
    } else {
        move(target_pos_.x(),
             target_pos_.y() + qRound((1.0 - entrance_progress_) * 8.0));
    }
}

void FinnToast::showMessage(const QString& message, Severity severity,
                            int duration_ms) {
    severity_ = severity;
    label_->setText(message);
    adjustSize();
    if (parentWidget()) {
        const int x = (parentWidget()->width() - width()) / 2;
        const int y = parentWidget()->height() - height() - 18;
        target_pos_ = QPoint(std::max(12, x), std::max(56, y));
    }
    reduced_fade_ = is_reduced_motion_active();
    entrance_progress_ = 0.0;
    setOpacityAlpha(0);
    move(target_pos_.x(), reduced_fade_ ? target_pos_.y() : (target_pos_.y() + 8));
    entrance_spring_.animator.set(0.0, 0.0);
    entrance_spring_.retargetOrCrossfade(0.0, 1.0, springs::kSnappy);
    show();
    raise();
    update();
    hide_timer_->start(std::max(800, duration_ms));
}

void FinnToast::paintEvent(QPaintEvent* event) {
    FinnGlassPanel::paintEvent(event);
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(entrance_progress_);
    QColor dot = t.accent;
    switch (severity_) {
        case Severity::Info:
            dot = t.accent;
            break;
        case Severity::Success:
            dot = t.success;
            break;
        case Severity::Warning:
            dot = t.warning;
            break;
        case Severity::Error:
            dot = t.danger;
            break;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(dot);
    p.drawEllipse(QPointF(10.5, height() / 2.0), 3.5, 3.5);
}

// ---- Stack Transition Animator & FinnPageStack ----

namespace {

class OverscrollFilter final : public QObject {
public:
    explicit OverscrollFilter(QScrollArea* scroll)
        : QObject(scroll), scroll_(scroll) {
        if (!scroll_ || !scroll_->viewport()) return;
        scroll_->viewport()->installEventFilter(this);
        snap_back_.init(scroll_->viewport(), springs::kOverscroll, 0.0,
                        [this](double v) {
                            overscroll_y_ = v;
                            applyVisualOffset();
                        });
        idle_timer_ = new QTimer(this);
        idle_timer_->setSingleShot(true);
        idle_timer_->setInterval(80);
        QObject::connect(idle_timer_, &QTimer::timeout, this, [this]() {
            raw_offset_ = 0.0;
            snap_back_.retarget(0.0);
        });
        if (QScrollBar* vbar = scroll_->verticalScrollBar()) {
            QObject::connect(vbar, &QScrollBar::valueChanged, this, [this](int) {
                if (std::abs(overscroll_y_) > 0.01) {
                    raw_offset_ = 0.0;
                    overscroll_y_ = 0.0;
                    snap_back_.setImmediate(0.0);
                }
            });
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (!scroll_ || obj != scroll_->viewport()) {
            return QObject::eventFilter(obj, ev);
        }
        if (ev->type() == QEvent::Wheel) {
            if (is_reduced_motion_active()) {
                if (std::abs(overscroll_y_) > 0.01) {
                    raw_offset_ = 0.0;
                    overscroll_y_ = 0.0;
                    snap_back_.setImmediate(0.0);
                }
                return false;
            }
            auto* we = static_cast<QWheelEvent*>(ev);
            QScrollBar* vbar = scroll_->verticalScrollBar();
            if (!vbar || vbar->maximum() <= vbar->minimum()) {
                return false;
            }
            double dy = we->pixelDelta().y();
            if (dy == 0.0) {
                dy = static_cast<double>(we->angleDelta().y()) * 0.35;
            }
            if (dy == 0.0) return false;

            const bool at_top = (vbar->value() <= vbar->minimum() && dy > 0.0);
            const bool at_bottom = (vbar->value() >= vbar->maximum() && dy < 0.0);
            if (at_top || at_bottom || std::abs(overscroll_y_) > 0.5) {
                const double dim = std::max(
                    120.0, static_cast<double>(scroll_->viewport()->height()));
                if (snap_back_.active()) {
                    raw_offset_ = rubber_band_inverse(overscroll_y_, dim);
                    if (snap_back_.timer) snap_back_.timer->stop();
                }
                raw_offset_ += dy;
                if ((vbar->value() <= vbar->minimum() && raw_offset_ < 0.0) ||
                    (vbar->value() >= vbar->maximum() && raw_offset_ > 0.0)) {
                    raw_offset_ = 0.0;
                }
                overscroll_y_ = rubber_band(raw_offset_, dim);
                snap_back_.animator.set(overscroll_y_, 0.0);
                applyVisualOffset();
                idle_timer_->start();
                if (std::abs(raw_offset_) > 0.0) {
                    we->accept();
                    return true;
                }
            }
        } else if (ev->type() == QEvent::Resize) {
            applyVisualOffset();
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    void applyVisualOffset() {
        if (!scroll_ || !scroll_->widget() || !scroll_->verticalScrollBar()) return;
        const int base_y = -scroll_->verticalScrollBar()->value();
        scroll_->widget()->move(scroll_->widget()->x(),
                                base_y + qRound(overscroll_y_));
    }

    QScrollArea* scroll_ = nullptr;
    QTimer* idle_timer_ = nullptr;
    FinnSpringDriver snap_back_;
    double raw_offset_ = 0.0;
    double overscroll_y_ = 0.0;
};

class StackTransitionOverlay final : public QWidget {
public:
    explicit StackTransitionOverlay(QStackedWidget* stack)
        : QWidget(stack), stack_(stack) {
        setObjectName(QStringLiteral("_finn_stack_overlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        hide();

        last_page_ = stack_->currentWidget();
        stack_->installEventFilter(this);

        spring_.init(this, springs::kSmooth, 1.0, [this](double v) {
            if (is_reduced_motion_active()) {
                finishImmediately();
                return;
            }
            progress_ = std::clamp(v, 0.0, 1.0);
            if (active_page_) {
                active_page_->move(0, qRound((1.0 - progress_) * 8.0));
            }
            update();
            if (spring_.animator.settled()) {
                finishImmediately();
            }
        });

        connect(stack_, &QStackedWidget::currentChanged, this,
                [this](int next_idx) { onPageChanged(next_idx); });
    }

    void setEnabledTransitions(bool enabled) {
        enabled_ = enabled;
        if (!enabled_) finishImmediately();
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == stack_ && ev->type() == QEvent::Resize && isVisible()) {
            setGeometry(stack_->rect());
        }
        return QWidget::eventFilter(obj, ev);
    }

    void paintEvent(QPaintEvent*) override {
        if (outgoing_pixmap_.isNull() || progress_ >= 1.0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setOpacity((1.0 - progress_) * 0.45);
        p.drawPixmap(QPoint(0, -qRound(progress_ * 4.0)), outgoing_pixmap_);
    }

private:
    void finishImmediately() {
        if (spring_.timer) spring_.timer->stop();
        spring_.animator.set(1.0, 0.0);
        progress_ = 1.0;
        if (active_page_) {
            active_page_->move(0, 0);
        }
        outgoing_pixmap_ = QPixmap();
        hide();
    }

    void onPageChanged(int next_idx) {
        QWidget* next_page = stack_->widget(next_idx);
        if (active_page_ && active_page_ != next_page) {
            active_page_->move(0, 0);
        }
        QWidget* prev_page = last_page_;
        last_page_ = next_page;

        const bool should_animate =
            enabled_ && !is_reduced_motion_active() &&
            stack_->isVisible() && next_page != nullptr &&
            prev_page != nullptr && prev_page != next_page &&
            stack_->width() > 16 && stack_->height() > 16;

        if (!should_animate) {
            finishImmediately();
            return;
        }

        const qreal dpr = std::max<qreal>(1.0, stack_->devicePixelRatioF());
        outgoing_pixmap_ = QPixmap(stack_->size() * dpr);
        outgoing_pixmap_.setDevicePixelRatio(dpr);
        outgoing_pixmap_.fill(Qt::transparent);
        if (prev_page->size().isValid()) {
            QPainter pm_painter(&outgoing_pixmap_);
            prev_page->render(&pm_painter, QPoint(), QRegion(),
                              QWidget::DrawChildren);
        }

        active_page_ = next_page;
        progress_ = 0.0;
        active_page_->move(0, 8);
        setGeometry(stack_->rect());
        show();
        raise();

        // Preserve any residual velocity when switching pages rapidly mid-flight.
        const double carry_vel = spring_.active() ? spring_.animator.velocity() : 0.0;
        spring_.animator.set(0.0, carry_vel);
        spring_.retarget(1.0, springs::kSmooth);
    }

    QStackedWidget* stack_ = nullptr;
    FinnSpringDriver spring_;
    QPointer<QWidget> active_page_;
    QPointer<QWidget> last_page_;
    QPixmap outgoing_pixmap_;
    qreal progress_ = 1.0;
    bool enabled_ = true;
};

}  // namespace

void install_overscroll_physics(QScrollArea* scroll) {
    if (!scroll || !scroll->viewport()) return;
    if (scroll->findChild<QObject*>(QStringLiteral("_finn_overscroll_filter"),
                                    Qt::FindDirectChildrenOnly)) {
        return;
    }
    auto* filter = new OverscrollFilter(scroll);
    filter->setObjectName(QStringLiteral("_finn_overscroll_filter"));
}

void install_stack_animator(QStackedWidget* stack) {
    if (!stack) return;
    if (stack->findChild<QWidget*>(QStringLiteral("_finn_stack_overlay"),
                                   Qt::FindDirectChildrenOnly)) {
        return;
    }
    new StackTransitionOverlay(stack);
}

void animate_stack_switch(QStackedWidget* stack, int next_index) {
    if (!stack) return;
    install_stack_animator(stack);
    stack->setCurrentIndex(next_index);
}

FinnPageStack::FinnPageStack(QWidget* parent) : QStackedWidget(parent) {
    install_stack_animator(this);
}

void FinnPageStack::setAnimatedTransitions(bool enabled) {
    animated_ = enabled;
    if (auto* ov = findChild<QWidget*>(QStringLiteral("_finn_stack_overlay"),
                                       Qt::FindDirectChildrenOnly)) {
        static_cast<StackTransitionOverlay*>(ov)->setEnabledTransitions(enabled);
    }
}

// ---- FinnDialog ----

FinnDialog::FinnDialog(const QString& title, const QString& message,
                       const QString& confirm_label,
                       const QString& cancel_label, bool destructive,
                       QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(360);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(12);

    root->addWidget(new FinnLabel(title, FinnLabel::Role::Headline,
                                  FinnLabel::Tone::Ink, this));
    auto* msg = new FinnLabel(message, FinnLabel::Role::Body,
                              FinnLabel::Tone::Dim, this);
    msg->setWordWrap(true);
    root->addWidget(msg);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    if (!cancel_label.isEmpty()) {
        auto* cancel_btn =
            new FinnButton(cancel_label, FinnButton::Variant::Secondary, {}, this);
        connect(cancel_btn, &QAbstractButton::clicked, this, &QDialog::reject);
        row->addWidget(cancel_btn);
    }
    auto* ok_btn = new FinnButton(
        confirm_label,
        destructive ? FinnButton::Variant::Danger : FinnButton::Variant::Primary,
        {}, this);
    connect(ok_btn, &QAbstractButton::clicked, this, &QDialog::accept);
    row->addWidget(ok_btn);
    root->addLayout(row);

    opacity_spring_.init(this, springs::kSnappy, 1.0, [this](double v) {
        setWindowOpacity(std::clamp(v, 0.0, 1.0));
    });
}

void FinnDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (shown_once_) return;
    shown_once_ = true;
    opacity_spring_.animator.set(0.85, 0.0);
    opacity_spring_.retargetOrCrossfade(0.5, 1.0, springs::kSnappy);
}

bool FinnDialog::confirm(QWidget* parent, const QString& title,
                         const QString& message, const QString& confirm_label,
                         bool destructive) {
    FinnDialog dlg(title, message, confirm_label, QStringLiteral("Cancel"),
                   destructive, parent);
    return dlg.exec() == QDialog::Accepted;
}

QString FinnDialog::prompt(QWidget* parent, const QString& title,
                           const QString& label, const QString& initial_value,
                           bool password_mode) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setModal(true);
    dlg.setMinimumWidth(360);

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(10);
    root->addWidget(new FinnLabel(title, FinnLabel::Role::Headline,
                                  FinnLabel::Tone::Ink, &dlg));
    if (!label.isEmpty()) {
        root->addWidget(new FinnLabel(label, FinnLabel::Role::Body,
                                      FinnLabel::Tone::Dim, &dlg));
    }
    auto* edit = new FinnTextField({}, &dlg);
    edit->setAccessibleName(title);
    if (password_mode) edit->setPasswordMode(true);
    edit->setText(initial_value);
    edit->selectAll();
    root->addWidget(edit);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    auto* cancel_btn = new FinnButton(QStringLiteral("Cancel"),
                                      FinnButton::Variant::Secondary, {}, &dlg);
    auto* ok_btn = new FinnButton(QStringLiteral("OK"),
                                  FinnButton::Variant::Primary, {}, &dlg);
    connect(cancel_btn, &QAbstractButton::clicked, &dlg, &QDialog::reject);
    connect(ok_btn, &QAbstractButton::clicked, &dlg, &QDialog::accept);
    connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
    row->addWidget(cancel_btn);
    row->addWidget(ok_btn);
    root->addLayout(row);

    return (dlg.exec() == QDialog::Accepted) ? edit->text() : QString();
}

// ---- FinnRibbonAnimation ----

namespace {

// Normalized (41x31) centerline stroke coordinates for the cursive "hi." from
// Figma node 36:37 (Bumbbled Regular), traced in natural handwriting order:
//   - Stroke 0 (52 pts): lead-in tail -> tall 'h' ascender loop -> stem down ->
//                        'h' arch -> downstroke -> connector up into 'i' stem ->
//                        'i' downstroke and exit curl.
//   - Stroke 1 (9 pts):  the 'i' dot.
//   - Stroke 2 (9 pts):  the trailing period '.' dot.
struct NormPt {
    double x;
    double y;
};

constexpr NormPt kHiMainStroke[] = {
    {-8.0, 20.5},  {-3.5, 20.8},  {0.8, 20.6},   {3.5, 19.2},   {6.8, 15.8},
    {9.8, 11.4},   {12.2, 6.8},   {13.6, 3.4},   {13.9, 1.4},   {12.8, 0.7},
    {11.1, 1.3},   {9.3, 3.6},    {7.4, 7.5},    {5.6, 12.6},   {4.1, 18.2},
    {2.9, 23.6},   {2.2, 27.4},   {2.4, 29.0},   {3.4, 28.2},   {5.2, 25.4},
    {7.8, 21.6},   {10.6, 17.8},  {13.4, 15.0},  {15.8, 13.8},  {17.6, 14.3},
    {18.4, 16.2},  {18.1, 19.4},  {17.1, 23.2},  {16.3, 26.6},  {16.5, 28.6},
    {17.8, 29.3},  {19.8, 28.4},  {22.2, 25.8},  {24.6, 22.0},  {26.8, 17.6},
    {28.3, 14.4},  {28.8, 13.5},  {28.4, 15.2},  {27.3, 18.8},  {26.1, 23.0},
    {25.4, 26.5},  {25.9, 28.4},  {27.4, 29.0},  {29.8, 28.0},  {32.4, 25.6},
    {34.8, 22.6},  {36.6, 20.4},
};

constexpr NormPt kHiIDot[] = {
    {30.3, 6.9}, {31.6, 7.5}, {32.1, 8.6}, {31.5, 9.7}, {30.3, 10.2},
    {29.0, 9.7}, {28.4, 8.6}, {29.0, 7.5}, {30.3, 6.9},
};

constexpr NormPt kHiPeriodDot[] = {
    {37.9, 25.9}, {39.2, 26.5}, {39.7, 27.8}, {39.1, 29.1}, {37.9, 29.7},
    {36.7, 29.1}, {36.1, 27.8}, {36.7, 26.5}, {37.9, 25.9},
};

QPointF sample_polyline(const NormPt* pts, int count, double u,
                        double ox, double oy, double sx, double sy) {
    if (count <= 1) return QPointF(ox + pts[0].x * sx, oy + pts[0].y * sy);
    const double pos = std::clamp(u, 0.0, 1.0) * static_cast<double>(count - 1);
    const int idx = std::min(count - 2, static_cast<int>(std::floor(pos)));
    const double frac = pos - static_cast<double>(idx);
    const double x = pts[idx].x + (pts[idx + 1].x - pts[idx].x) * frac;
    const double y = pts[idx].y + (pts[idx + 1].y - pts[idx].y) * frac;
    return QPointF(ox + x * sx, oy + y * sy);
}

void draw_feathered_aura(QPainter& p, const QRect& rect, double cx, double cy,
                         bool dark, double opacity_scale) {
    if (opacity_scale <= 0.01) return;
    const double w = rect.width();
    const double h = rect.height();
    const double glow_r = std::min(w, h) * 0.44;
    QRadialGradient aura(QPointF(cx, cy), glow_r);
    if (dark) {
        aura.setColorAt(0.0, QColor(147, 51, 234, qRound(38 * opacity_scale)));
        aura.setColorAt(0.45, QColor(249, 115, 22, qRound(20 * opacity_scale)));
        aura.setColorAt(0.78, QColor(0, 123, 255, qRound(8 * opacity_scale)));
        aura.setColorAt(1.0, Qt::transparent);
    } else {
        aura.setColorAt(0.0, QColor(228, 194, 255, qRound(72 * opacity_scale)));
        aura.setColorAt(0.45, QColor(255, 206, 188, qRound(46 * opacity_scale)));
        aura.setColorAt(0.78, QColor(210, 228, 255, qRound(16 * opacity_scale)));
        aura.setColorAt(1.0, Qt::transparent);
    }
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(aura);
    p.drawRect(rect);
    p.restore();
}

}  // namespace

FinnRibbonAnimation::FinnRibbonAnimation(Mode mode, QWidget* parent)
    : QWidget(parent), mode_(mode) {
    setMinimumSize(280, 140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_TranslucentBackground, true);

    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &FinnRibbonAnimation::tick);
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

FinnRibbonAnimation::FinnRibbonAnimation(QWidget* parent)
    : FinnRibbonAnimation(Mode::HiIntro, parent) {}

void FinnRibbonAnimation::setMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    update();
}

void FinnRibbonAnimation::restart() {
    elapsed_ = 0.0;
    paused_ = false;
    intro_emitted_ = false;
    last_ms_ = QDateTime::currentMSecsSinceEpoch();
    if (isVisible() && !FinnThemeManager::instance().reducedMotion()) {
        timer_->start();
    }
    update();
}

void FinnRibbonAnimation::setPhaseTime(double seconds) {
    if (seconds >= 0.0) {
        elapsed_ = seconds;
        paused_ = true;
        timer_->stop();
        emit frameAdvanced(elapsed_);
    } else {
        paused_ = false;
        last_ms_ = QDateTime::currentMSecsSinceEpoch();
        if (isVisible() && !FinnThemeManager::instance().reducedMotion()) {
            timer_->start();
        }
    }
    update();
}

void FinnRibbonAnimation::setAmbientGlow(bool enabled) {
    ambient_glow_ = enabled;
    update();
}

double FinnRibbonAnimation::uiRevealProgress() const {
    if (FinnThemeManager::instance().reducedMotion()) return 1.0;
    if (mode_ == Mode::HiIntro) {
        return ease_out_cubic(clamp_unit((elapsed_ - 8.45) / 0.50));
    }
    return ease_out_cubic(clamp_unit((elapsed_ - 1.95) / 0.45));
}

void FinnRibbonAnimation::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    last_ms_ = QDateTime::currentMSecsSinceEpoch();
    if (!paused_ && !FinnThemeManager::instance().reducedMotion()) {
        timer_->start();
    }
}

void FinnRibbonAnimation::hideEvent(QHideEvent* event) {
    timer_->stop();
    QWidget::hideEvent(event);
}

void FinnRibbonAnimation::tick() {
    if (FinnThemeManager::instance().reducedMotion()) {
        timer_->stop();
        elapsed_ = 3.0;
        if (!intro_emitted_) {
            intro_emitted_ = true;
            emit introCompleted();
        }
        emit frameAdvanced(elapsed_);
        update();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt =
        (last_ms_ > 0) ? std::clamp((now - last_ms_) / 1000.0, 0.001, 0.05) : 0.016;
    last_ms_ = now;
    elapsed_ += dt;
    const double done_t = (mode_ == Mode::HiIntro) ? 8.52 : 2.25;
    if (!intro_emitted_ && elapsed_ >= done_t) {
        intro_emitted_ = true;
        emit introCompleted();
    }
    emit frameAdvanced(elapsed_);
    update();
}

void FinnRibbonAnimation::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double w = width();
    const double h = height();
    if (w < 16.0 || h < 16.0) return;

    const double t = FinnThemeManager::instance().reducedMotion() ? 3.0 : elapsed_;
    if (mode_ == Mode::HiIntro) {
        paintHiIntro(p, w, h, t);
    } else {
        paintSlinkyWave(p, w, h, t);
    }
}

void FinnRibbonAnimation::paintHiIntro(QPainter& p, double w, double h, double t) {
    const auto& tok = FinnThemeManager::instance().tokens();
    constexpr double kPi = 3.14159265358979323846;

    // Center of the "hi." mark in Figma 36:3 (x:621, y:400, 41x31 inside 1280x832 -> cy = 49.9%)
    const double cx = w * 0.5;
    const double cy = h * 0.499;

    // Timeline for Mode::HiIntro (black boot Finn logo -> white bg -> logo fade out ->
    // expanding torus rings -> clockwise eccentric circles -> left shrink -> spring -> "hi."):
    //   0.00..0.55s : Black background with white Finn logo at center
    //   0.55..1.15s : Background fades black -> white; Finn logo fades white -> black
    //   1.15..1.65s : Finn logo gently scales down & fades out
    //   1.25..2.85s : Rings bloom outward from center and fan into 8 eccentric circles
    //   2.85..4.70s : 8 eccentric nested circles orbit clockwise, then merge into 1 circle
    //   4.65..5.35s : Left side shrinks down to (cx, cy + r_max) while spring emerges
    //   5.05..8.15s : Full-width upright oval spring pans right-to-left -> centered "hi."
    //   8.12..8.50s : Cross-fades into crisp Figma 36:37 "hi." vector mark
    //   8.45..8.95s : uiRevealProgress() reveals FinnOS branding and Next button
    const double p_bg_white = ease_in_out_cubic(clamp_unit((t - 0.55) / 0.60));
    const double p_logo_out = ease_in_out_cubic(clamp_unit((t - 1.15) / 0.50));
    const double p_bloom = ease_in_out_cubic(clamp_unit((t - 1.25) / 1.10));
    const double p_torus = ease_in_out_cubic(clamp_unit((t - 2.10) / 0.75));
    const double p_converge = ease_in_out_cubic(clamp_unit((t - 4.30) / 0.40));
    const double raw_split = clamp_unit((t - 4.65) / 0.70);
    const double p_split = raw_split * raw_split;

    constexpr double kCoilSplit = 0.76;
    const double p_stroke = clamp_unit((t - 5.05) / 3.10);
    const double head_s =
        p_stroke * (0.82 + 0.18 * (3.0 * p_stroke - 2.0 * p_stroke * p_stroke));
    const double hi_prog = clamp_unit((head_s - kCoilSplit) / (1.0 - kCoilSplit));

    const double p_cam = ease_in_out_cubic(clamp_unit((t - 5.30) / 2.20));
    const double p_tail = ease_in_out_cubic(clamp_unit((t - 6.40) / 1.45));
    const double p_svg = ease_in_out_cubic(clamp_unit((t - 8.12) / 0.38));

    // Black boot background fading smoothly to transparent (revealing white/theme canvas)
    if (p_bg_white < 0.999) {
        p.save();
        p.setOpacity(1.0 - p_bg_white);
        p.fillRect(rect(), Qt::black);
        p.restore();
    }

    const QColor ink = tok.ink;
    const QColor base_ink = tok.ink;
    const QColor coral(0xFF, 0x57, 0x22);
    const QColor amber(0xFF, 0x95, 0x00);
    const QColor violet(0x8B, 0x5C, 0xF6);
    const QColor blue = tok.accent.isValid()
                            ? tok.accent
                            : QColor::fromRgba(colors::accent_light);

    // Target bounding box for the 41x31 "hi." mark at the center of the screen
    const double hi_w = 41.0;
    const double hi_h = 31.0;
    const double hi_ox = cx - hi_w * 0.5;
    const double hi_oy = cy - hi_h * 0.5;

    if (p_svg < 0.999) {
        constexpr int kRings = 8;
        constexpr int kSamples = 110;
        const double r_max = std::min(w, h) * 0.165;
        const double dr = r_max * 0.058;
        const double phi = kPi + t * 3.8;

        const double coil_world_start = cx;
        const double coil_world_span = w * 1.85;
        const double coil_world_end = coil_world_start + coil_world_span;
        const double hi_world_ox = coil_world_end - kHiMainStroke[0].x;
        const double cam_x = (hi_world_ox - hi_ox) * p_cam;

        QConicalGradient torus_grad(QPointF(cx - cam_x, cy), -phi * 180.0 / kPi);
        torus_grad.setColorAt(0.00, coral);
        torus_grad.setColorAt(0.11, amber);
        torus_grad.setColorAt(0.25, violet);
        torus_grad.setColorAt(0.39, blue);
        torus_grad.setColorAt(0.56, base_ink);
        torus_grad.setColorAt(0.84, base_ink);
        torus_grad.setColorAt(0.94, violet);
        torus_grad.setColorAt(1.00, coral);

        // Phase 0: Finn logo softly scales down & fades out while luminous rings
        // bloom outward from center into the 8 clockwise-orbiting torus rings.
        const double logo_size = std::min(w, h) * 0.088;
        {
            const double scale = 1.0 - 0.16 * p_logo_out;
            const double cur_size = logo_size * scale;
            const QRectF logo_rect(cx - cur_size * 0.5, cy - cur_size * 0.5, cur_size, cur_size);
            const double svg_alpha = 1.0 - p_logo_out;
            if (svg_alpha > 0.01) {
                auto& tm = FinnThemeManager::instance();
                if (p_bg_white < 0.999) {
                    p.save();
                    p.setOpacity(svg_alpha * (1.0 - p_bg_white));
                    tm.paintIcon(&p, logo_rect, QStringLiteral("finn-logo-light"),
                                 QColor::fromRgba(colors::knob_light));
                    p.restore();
                }
                if (p_bg_white > 0.001) {
                    p.save();
                    p.setOpacity(svg_alpha * p_bg_white);
                    tm.paintIcon(&p, logo_rect,
                                 tok.dark ? QStringLiteral("finn-logo-light")
                                          : QStringLiteral("finn-logo-dark"),
                                 ink);
                    p.restore();
                }
            }
        }

        if (p_bloom > 0.001 && p_split < 0.998) {
            const double theta_start = 2.0 * kPi * p_split;
            const double theta_span = 2.0 * kPi * (1.0 - p_split);
            const double arc_opacity = (1.0 - p_svg * 0.92) * clamp_unit(p_bloom * 1.6);
            const double r_seed = logo_size * 0.42;

            for (int i = 0; i < kRings; ++i) {
                const double ring_alpha =
                    (i == kRings - 1) ? arc_opacity : arc_opacity * (1.0 - p_converge);
                if (ring_alpha <= 0.01) continue;

                // Each ring blooms outward from r_seed at (cx, cy) and fans into its
                // eccentric orbiting torus radius/center.
                const double ri_target = r_max - (kRings - 1 - i) * dr * (1.0 - p_converge);
                const double ri = r_seed + (ri_target - r_seed) * p_bloom;
                const double ecc = (kRings - 1 - i) * dr * 0.92 * (1.0 - p_converge) * p_torus;
                const double t_cx = cx + ecc * std::cos(phi);
                const double t_cy = cy + ecc * std::sin(phi);

                QPainterPath path;
                for (int s = 0; s <= kSamples; ++s) {
                    const double u = static_cast<double>(s) / kSamples;
                    const double theta = theta_start + u * theta_span;
                    const double delta = theta + t * 2.2 * (1.0 - p_converge);
                    const double pt_x = (t_cx + ri * std::sin(delta)) - cam_x;
                    const double pt_y = t_cy + ri * std::cos(delta);
                    if (s == 0) path.moveTo(pt_x, pt_y);
                    else path.lineTo(pt_x, pt_y);
                }

                QPen ring_pen(QBrush(torus_grad), 1.65 * (i == kRings - 1 ? 1.0 : 0.72),
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                p.save();
                p.setOpacity(ring_alpha);
                p.setPen(ring_pen);
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
                p.restore();
            }
        }

        // Phase 2: From the vanished bottom point (cx, cy + r_max), continue seamlessly
        // into the closer upright oval spring + lively up/down wave -> crisp "hi."
        const int main_count = static_cast<int>(sizeof(kHiMainStroke) / sizeof(kHiMainStroke[0]));
        const double tail_s = std::min(head_s, kCoilSplit * p_tail);

        if (head_s > tail_s + 0.002) {
            constexpr double kLoops = 12.5;
            const double pitch = coil_world_span / kLoops;
            const double loop_rx = pitch * 0.40;
            const double loop_ry = r_max * 0.90;
            const double hi_start_y = hi_oy + kHiMainStroke[0].y;

            auto sample_coil = [&](double u) -> QPointF {
                const double exit_amp = 1.0 - ease_in_out_cubic(clamp_unit((u - 0.88) / 0.12));
                const double rx = loop_rx * exit_amp;
                const double ry = loop_ry * exit_amp;
                const double wave = std::sin(2.0 * kPi * 2.0 * u + t * 2.1) *
                                    (r_max * 0.16) * clamp_unit(u / 0.08) * (1.0 - u);
                // First spring loop is full height right from u = 0, anchored at (cx, cy + r_max)
                const double start_base_y = cy + r_max - loop_ry;
                const double base_x = coil_world_start + coil_world_span * u - cam_x;
                const double base_y =
                    start_base_y + (hi_start_y - start_base_y) * ease_in_out_cubic(u) + wave;
                const double angle = 2.0 * kPi * kLoops * u;
                return QPointF(base_x + rx * std::sin(angle),
                               base_y + ry * std::cos(angle));
            };

            auto sample_ribbon = [&](double s) -> QPointF {
                if (s <= kCoilSplit) {
                    return sample_coil(clamp_unit(s / kCoilSplit));
                }
                const double hi_u = clamp_unit((s - kCoilSplit) / (1.0 - kCoilSplit));
                return sample_polyline(
                    kHiMainStroke, main_count, hi_u, hi_world_ox - cam_x, hi_oy, 1.0, 1.0);
            };

            const QPointF nib_pt = sample_ribbon(head_s);
            const QPointF tail_pt = sample_ribbon(tail_s);
            const double grad_x0 = std::min(tail_pt.x(), nib_pt.x() - 220.0);
            const double grad_x1 = std::max(nib_pt.x() + 18.0, grad_x0 + 40.0);

            QLinearGradient ribbon_grad(QPointF(grad_x0, cy), QPointF(grad_x1, cy));
            ribbon_grad.setColorAt(0.00, ink);
            ribbon_grad.setColorAt(0.45, ink);
            ribbon_grad.setColorAt(0.68, violet);
            ribbon_grad.setColorAt(0.82, coral);
            ribbon_grad.setColorAt(0.92, amber);
            ribbon_grad.setColorAt(1.00, blue);

            QPainterPath ribbon_path;
            bool started = false;
            if (tail_s < kCoilSplit) {
                const double coil_end_s = std::min(head_s, kCoilSplit);
                constexpr int kCoilSamples = 420;
                for (int i = 0; i <= kCoilSamples; ++i) {
                    const double s =
                        tail_s + (coil_end_s - tail_s) * (static_cast<double>(i) / kCoilSamples);
                    const QPointF pt = sample_coil(clamp_unit(s / kCoilSplit));
                    if (!started) {
                        ribbon_path.moveTo(pt);
                        started = true;
                    } else {
                        ribbon_path.lineTo(pt);
                    }
                }
            }
            if (head_s > kCoilSplit) {
                // Sample "hi." at high density with midpoint quadratic smoothing so the
                // handwriting curves are crisp and undistorted
                const double hi_end_u = clamp_unit((head_s - kCoilSplit) / (1.0 - kCoilSplit));
                const int max_idx = std::max(
                    1, static_cast<int>(std::floor(hi_end_u * (main_count - 1))));
                const double ox = hi_world_ox - cam_x;
                auto hi_pt_at = [&](int idx) {
                    return QPointF(ox + kHiMainStroke[idx].x, hi_oy + kHiMainStroke[idx].y);
                };
                if (!started) {
                    ribbon_path.moveTo(hi_pt_at(0));
                    started = true;
                }
                for (int idx = 1; idx < max_idx; ++idx) {
                    const QPointF p0 = hi_pt_at(idx);
                    const QPointF p1 = hi_pt_at(idx + 1);
                    ribbon_path.quadTo(p0, (p0 + p1) * 0.5);
                }
                ribbon_path.lineTo(sample_polyline(
                    kHiMainStroke, main_count, hi_end_u, ox, hi_oy, 1.0, 1.0));
            }

            const double stroke_w = 2.0 + 1.5 * hi_prog;
            p.save();
            p.setOpacity(1.0 - p_svg * 0.92);
            p.setPen(QPen(QBrush(ribbon_grad), stroke_w,
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(ribbon_path);
            p.restore();
        }

        // Draw the 'i' dot and '.' period dot as the handwriting finishes
        const double dot1_prog = clamp_unit((hi_prog - 0.72) / 0.16);
        const double dot2_prog = clamp_unit((hi_prog - 0.84) / 0.16);
        if (dot1_prog > 0.01) {
            const int d1_count = static_cast<int>(sizeof(kHiIDot) / sizeof(kHiIDot[0]));
            QPainterPath d1;
            for (int s = 0; s <= 16; ++s) {
                const double u = (static_cast<double>(s) / 16.0) * dot1_prog;
                const QPointF pt = sample_polyline(
                    kHiIDot, d1_count, u, hi_world_ox - cam_x, hi_oy, 1.0, 1.0);
                if (s == 0) d1.moveTo(pt);
                else d1.lineTo(pt);
            }
            p.save();
            p.setOpacity((1.0 - p_svg * 0.92) * dot1_prog);
            p.setPen(QPen(coral, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(d1);
            p.restore();
        }
        if (dot2_prog > 0.01) {
            const int d2_count = static_cast<int>(sizeof(kHiPeriodDot) / sizeof(kHiPeriodDot[0]));
            QPainterPath d2;
            for (int s = 0; s <= 16; ++s) {
                const double u = (static_cast<double>(s) / 16.0) * dot2_prog;
                const QPointF pt = sample_polyline(
                    kHiPeriodDot, d2_count, u, hi_world_ox - cam_x, hi_oy, 1.0, 1.0);
                if (s == 0) d2.moveTo(pt);
                else d2.lineTo(pt);
            }
            p.save();
            p.setOpacity((1.0 - p_svg * 0.92) * dot2_prog);
            p.setPen(QPen(amber, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(d2);
            p.restore();
        }
    }

    // Crisp Figma 36:37 "hi." vector cross-fades in as the handwriting stroke completes
    if (p_svg > 0.01) {
        p.save();
        p.setOpacity(p_svg);
        FinnThemeManager::instance().paintIcon(
            &p, QRectF(hi_ox, hi_oy, hi_w, hi_h), QStringLiteral("oobe-hi"), ink);
        p.restore();
    }
}

void FinnRibbonAnimation::paintSlinkyWave(QPainter& p, double w, double h, double t) {
    const auto& tok = FinnThemeManager::instance().tokens();
    const double cx = w * 0.5;
    const double cy = h * 0.52;

    if (ambient_glow_) {
        draw_feathered_aura(p, rect(), cx, h * 0.45, tok.dark, 1.0);
    }

    constexpr int kRings = 6;
    constexpr int kSamples = 96;
    constexpr double kPi = 3.14159265358979323846;

    const double p_cyl = ease_in_out_cubic(clamp_unit(t / 0.42));
    const double p_torus = ease_in_out_cubic(clamp_unit((t - 0.34) / 0.36));
    const double torus_spin = clamp_unit((t - 0.38) / 1.08);
    const double p_unfurl = clamp_unit((t - 1.40) / 0.58);

    const double r_max = std::min(w * 0.34, h * 0.42);
    const double dr = r_max * 0.068;
    const double phi = kPi + torus_spin * 1.38 * kPi;

    const double slinky_margin = 2.0;
    const double slinky_w = std::max(60.0, w - slinky_margin * 2.0);
    const double pitch = slinky_w / static_cast<double>(kRings);
    const double slinky_ay = std::min(h * 0.295, pitch * 0.88);

    const QColor base_ink = tok.ink;
    const QColor coral(0xFF, 0x57, 0x22);
    const QColor amber(0xFF, 0x95, 0x00);
    const QColor violet(0x8B, 0x5C, 0xF6);
    const QColor blue = tok.accent.isValid()
                            ? tok.accent
                            : QColor::fromRgba(colors::accent_light);

    QConicalGradient torus_grad(QPointF(cx, cy), -phi * 180.0 / kPi);
    torus_grad.setColorAt(0.00, coral);
    torus_grad.setColorAt(0.10, amber);
    torus_grad.setColorAt(0.24, violet);
    torus_grad.setColorAt(0.38, blue);
    torus_grad.setColorAt(0.56, base_ink);
    torus_grad.setColorAt(0.84, base_ink);
    torus_grad.setColorAt(0.94, violet);
    torus_grad.setColorAt(1.00, coral);

    const double wave_center = std::fmod(std::max(0.0, t - 1.55) * 0.36, 1.36) - 0.18;
    QLinearGradient wave_grad(QPointF(0.0, cy), QPointF(w, cy));
    auto add_clamped = [&](double pos, const QColor& col) {
        wave_grad.setColorAt(std::clamp(pos, 0.0, 1.0), col);
    };
    wave_grad.setColorAt(0.0, base_ink);
    wave_grad.setColorAt(1.0, base_ink);
    if (wave_center > -0.15 && wave_center < 1.15) {
        add_clamped(wave_center - 0.24, base_ink);
        add_clamped(wave_center - 0.13, violet);
        add_clamped(wave_center - 0.04, coral);
        add_clamped(wave_center + 0.04, amber);
        add_clamped(wave_center + 0.14, blue);
        add_clamped(wave_center + 0.25, base_ink);
    }

    const bool use_wave_brush = (p_unfurl > 0.55);
    const QBrush primary_brush = use_wave_brush ? QBrush(wave_grad) : QBrush(torus_grad);
    const qreal stroke_w = std::clamp(std::min(w, h) * 0.0068, 1.45, 2.25);

    auto sample_point = [&](int i, double u, double echo_offset) -> QPointF {
        const double delta = (u * 2.0 - 1.0) * kPi;
        const double norm_i = (static_cast<double>(i) - 2.5);

        const double arc_x = (u - 0.5) * w * 1.02;
        const double arc_y = cy + norm_i * (h * 0.055) + std::cos(delta * 0.5) * (h * 0.035);

        const double cyl_rx = std::min(w * 0.40, r_max * 1.32);
        const double cyl_ry = std::min(h * 0.14, r_max * 0.42);
        const double cyl_x = cx + cyl_rx * std::sin(delta);
        const double cyl_y =
            cy + norm_i * (h * 0.046) - cyl_ry * std::cos(delta);

        const double stage1_x = arc_x + (cyl_x - arc_x) * p_cyl;
        const double stage1_y = arc_y + (cyl_y - arc_y) * p_cyl;

        const double ri = r_max - (5 - i) * dr + echo_offset * 0.6;
        const double ecc = (5 - i) * dr * 0.88;
        const double t_cx = cx + ecc * std::cos(phi);
        const double t_cy = cy + ecc * std::sin(phi);
        const double torus_x = t_cx + ri * std::sin(delta);
        const double torus_y = t_cy - ri * std::cos(delta);

        const double ring_x = stage1_x + (torus_x - stage1_x) * p_torus;
        const double ring_y = stage1_y + (torus_y - stage1_y) * p_torus;

        if (p_unfurl <= 1e-4) {
            return QPointF(ring_x, ring_y);
        }

        const double u_unfurl =
            ease_in_out_cubic(stagger(p_unfurl, i, kRings, 0.055));
        const double loop_cx = slinky_margin + (static_cast<double>(i) + 0.5) * pitch;
        const double breath =
            (p_unfurl >= 0.99) ? (1.8 * std::sin(t * 2.1 + i * 0.55)) : 0.0;
        const double slinky_x =
            loop_cx + (pitch / (2.0 * kPi)) * delta -
            0.335 * pitch * std::sin(delta) + echo_offset;
        const double slinky_y =
            cy + breath - (slinky_ay + echo_offset * 0.35) * std::cos(delta);

        const double left_gather =
            -std::sin(u_unfurl * kPi) * (w * 0.22) * (1.0 - 0.12 * i);

        const double out_x =
            ring_x + (slinky_x - ring_x) * u_unfurl + left_gather;
        const double out_y = ring_y + (slinky_y - ring_y) * u_unfurl;
        return QPointF(out_x, out_y);
    };

    const double echo_alpha =
        (p_unfurl < 0.98)
            ? (0.32 * p_cyl * (1.0 - 0.72 * p_unfurl))
            : 0.16;
    if (echo_alpha > 0.02) {
        QColor echo_col = tok.dark ? QColor(167, 139, 250) : QColor(139, 92, 246);
        echo_col.setAlphaF(echo_alpha);
        QPen echo_pen(echo_col, stroke_w * 0.78, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin);
        p.setPen(echo_pen);
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < kRings; ++i) {
            QPainterPath path;
            for (int s = 0; s <= kSamples; ++s) {
                const double u = static_cast<double>(s) / kSamples;
                const QPointF pt = sample_point(i, u, 3.2);
                if (s == 0) path.moveTo(pt);
                else path.lineTo(pt);
            }
            p.drawPath(path);
        }
    }

    QPen main_pen(primary_brush, stroke_w, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin);
    p.setPen(main_pen);
    p.setBrush(Qt::NoBrush);

    for (int i = 0; i < kRings; ++i) {
        QPainterPath path;
        for (int s = 0; s <= kSamples; ++s) {
            const double u = static_cast<double>(s) / kSamples;
            const QPointF pt = sample_point(i, u, 0.0);
            if (s == 0) path.moveTo(pt);
            else path.lineTo(pt);
        }
        p.drawPath(path);
    }
}

}  // namespace finn::ui
