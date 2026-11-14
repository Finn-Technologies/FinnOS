// Implements DockWindow: 4 centered Figma glass pills with exact geometry,
// 1:1 active highlights, full-bleed app icons, and masked input regions.

#include "shell/dock.hpp"

#include <QAbstractButton>
#include <QDateTime>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QTimer>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>
#include <utility>

#include "system/desktop_services.hpp"

namespace finn::shell {
namespace {

QColor dockGlassFill(bool dark) {
    return dark ? QColor(0x28, 0x2C, 0x37, 0xCC) : QColor(255, 255, 255, 0xB2);
}

QColor dockHighlightFill(bool dark) {
    return dark ? QColor(0x3A, 0x40, 0x50) : QColor(255, 255, 255);
}

void paintPillBackground(QPainter& p, const QRectF& rect, qreal radius, bool dark) {
    p.setPen(Qt::NoPen);
    p.setBrush(dockGlassFill(dark));
    p.drawRoundedRect(rect, radius, radius);
}

void paintDockGlyph(QPainter& p, const QRectF& rect, const QString& icon_name,
                    const QColor& tint, qreal dpr) {
    const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
        icon_name, rect.size().toSize(), tint, dpr);
    if (!pm.isNull()) {
        p.drawPixmap(rect.topLeft(), pm);
    }
}

class DockAppButton final : public QAbstractButton {
public:
    DockAppButton(QString app_id, QString icon_name, bool running, bool activated,
                  std::function<void()> on_activate, QWidget* parent)
        : QAbstractButton(parent),
          app_id_(std::move(app_id)),
          icon_name_(std::move(icon_name)),
          on_activate_(std::move(on_activate)),
          running_(running),
          activated_(activated) {
        setFixedSize(32, 38);
        setCursor(Qt::PointingHandCursor);

        hover_anim_ = new QVariantAnimation(this);
        hover_anim_->setDuration(140);
        hover_anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(hover_anim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& val) {
                    hover_progress_ = val.toReal();
                    update();
                });

        bounce_anim_ = new QVariantAnimation(this);
        bounce_anim_->setDuration(260);
        bounce_anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(bounce_anim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& val) {
                    bounce_offset_ = val.toReal();
                    update();
                });
    }

    const QString& appId() const { return app_id_; }
    bool isRunning() const { return running_; }
    bool isActivated() const { return activated_; }

    void setState(const QString& icon_name, bool running, bool activated) {
        if (icon_name_ == icon_name && running_ == running &&
            activated_ == activated) {
            return;
        }
        icon_name_ = icon_name;
        running_ = running;
        activated_ = activated;
        update();
    }

    void triggerBounce() {
        if (finn::ui::FinnThemeManager::instance().reducedMotion()) return;
        bounce_anim_->stop();
        bounce_anim_->setStartValue(0.0);
        bounce_anim_->setKeyValueAt(0.42, -4.0);
        bounce_anim_->setEndValue(0.0);
        bounce_anim_->start();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
            hover_anim_->stop();
            hover_progress_ = 1.0;
            update();
        } else {
            hover_anim_->stop();
            hover_anim_->setStartValue(hover_progress_);
            hover_anim_->setEndValue(1.0);
            hover_anim_->start();
        }
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        pressed_ = false;
        if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
            hover_anim_->stop();
            hover_progress_ = 0.0;
            update();
        } else {
            hover_anim_->stop();
            hover_anim_->setStartValue(hover_progress_);
            hover_anim_->setEndValue(0.0);
            hover_anim_->start();
        }
        QAbstractButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            triggerBounce();
            update();
            if (on_activate_) on_activate_();
            event->accept();
            return;
        }
        QAbstractButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (pressed_) {
            pressed_ = false;
            update();
        }
        QAbstractButton::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const qreal lift_y = -3.5 * hover_progress_ + bounce_offset_;
        const qreal scale = pressed_ ? 0.92 : 1.0;

        p.save();
        p.translate(16.0, 4.0 + lift_y + 16.0);
        p.scale(scale, scale);
        p.translate(-16.0, -16.0);

        const QRectF r(0.0, 0.0, 32.0, 32.0);
        QPainterPath clip;
        clip.addRoundedRect(r, 8.0, 8.0);
        p.setClipPath(clip);

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
            icon_name_, QSize(32, 32), QColor(), dpr);
        if (!pm.isNull()) {
            p.drawPixmap(QRect(0, 0, 32, 32), pm);
        }

        if (hovered_ || pressed_) {
            const bool dark = finn::ui::FinnThemeManager::instance().isDark();
            const QColor veil = pressed_
                                    ? (dark ? QColor(255, 255, 255, 42)
                                            : QColor(0, 0, 0, 28))
                                    : (dark ? QColor(255, 255, 255, 24)
                                            : QColor(255, 255, 255, 48));
            p.fillRect(r, veil);
        }
        p.restore();
    }

private:
    QString app_id_;
    QString icon_name_;
    std::function<void()> on_activate_;
    QVariantAnimation* hover_anim_ = nullptr;
    QVariantAnimation* bounce_anim_ = nullptr;
    qreal hover_progress_ = 0.0;
    qreal bounce_offset_ = 0.0;
    bool running_ = false;
    bool activated_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
};

class AppsPill final : public QWidget {
public:
    explicit AppsPill(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedHeight(44);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        paintPillBackground(p, QRectF(rect()), 12.0, t.dark);

        const auto buttons = findChildren<QAbstractButton*>();
        for (const QAbstractButton* abtn : buttons) {
            const auto* btn = dynamic_cast<const DockAppButton*>(abtn);
            if (!btn || !btn->isRunning()) continue;
            const QColor dot = btn->isActivated()
                                   ? t.accent
                                   : QColor(t.ink.red(), t.ink.green(), t.ink.blue(), 150);
            p.setPen(Qt::NoPen);
            p.setBrush(dot);
            p.drawRoundedRect(QRectF(btn->x() + 13.0, 40.0, 6.0, 2.0), 1.0, 1.0);
        }
    }
};

QString normalize_icon_for_app_id(const std::string& id,
                                    const std::vector<finn::App>* apps) {
    if (apps) {
        for (const auto& a : *apps) {
            if (a.id == id && !a.icon.empty()) {
                return QString::fromStdString(a.icon);
            }
        }
    }
    if (id == "settings") return QStringLiteral("settings-app");
    return QString::fromStdString(id);
}

}  // namespace

class HomeButtonsPill final : public QWidget {
public:
    HomeButtonsPill(std::function<void()> on_search,
                    std::function<void()> on_home,
                    std::function<void()> on_windows,
                    QWidget* parent)
        : QWidget(parent),
          on_search_(std::move(on_search)),
          on_home_(std::move(on_home)),
          on_windows_(std::move(on_windows)) {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(130, 44);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        highlight_x_anim_ = new QVariantAnimation(this);
        highlight_x_anim_->setDuration(160);
        highlight_x_anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(highlight_x_anim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& val) {
                    highlight_x_ = val.toReal();
                    update();
                });

        highlight_opacity_anim_ = new QVariantAnimation(this);
        highlight_opacity_anim_->setDuration(160);
        highlight_opacity_anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(highlight_opacity_anim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& val) {
                    highlight_opacity_ = val.toReal();
                    update();
                });
    }

    void setActivePopovers(bool search, bool home, bool windows_open) {
        if (search_active_ == search && home_active_ == home &&
            windows_active_ == windows_open) {
            return;
        }
        search_active_ = search;
        home_active_ = home;
        windows_active_ = windows_open;

        constexpr qreal kSlotX[3] = {0.0, 43.0, 86.0};
        const int next_slot =
            search ? 0 : (home ? 1 : (windows_open ? 2 : -1));
        const bool reduced = finn::ui::FinnThemeManager::instance().reducedMotion();

        if (next_slot >= 0) {
            const qreal target_x = kSlotX[next_slot];
            if (reduced) {
                highlight_x_anim_->stop();
                highlight_opacity_anim_->stop();
                highlight_x_ = target_x;
                highlight_opacity_ = 1.0;
            } else if (active_slot_ >= 0 && active_slot_ != next_slot) {
                highlight_x_anim_->stop();
                highlight_x_anim_->setStartValue(highlight_x_);
                highlight_x_anim_->setEndValue(target_x);
                highlight_x_anim_->start();

                highlight_opacity_anim_->stop();
                highlight_opacity_ = 1.0;
            } else {
                highlight_x_ = target_x;
                highlight_opacity_anim_->stop();
                highlight_opacity_anim_->setDuration(160);
                highlight_opacity_anim_->setStartValue(highlight_opacity_);
                highlight_opacity_anim_->setEndValue(1.0);
                highlight_opacity_anim_->start();
            }
        } else {
            if (reduced) {
                highlight_x_anim_->stop();
                highlight_opacity_anim_->stop();
                highlight_opacity_ = 0.0;
            } else {
                highlight_opacity_anim_->stop();
                highlight_opacity_anim_->setDuration(140);
                highlight_opacity_anim_->setStartValue(highlight_opacity_);
                highlight_opacity_anim_->setEndValue(0.0);
                highlight_opacity_anim_->start();
            }
        }
        active_slot_ = next_slot;
        update();
    }

    void setHasRunningWindows(bool running) {
        if (has_running_windows_ == running) return;
        has_running_windows_ = running;
        update();
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        const int next = slotAtX(event->position().x());
        if (next != hover_slot_) {
            hover_slot_ = next;
            switch (hover_slot_) {
                case 0:
                    setToolTip(QStringLiteral("Search (Alt+Space)"));
                    break;
                case 1:
                    setToolTip(QStringLiteral("Home (Super)"));
                    break;
                case 2:
                    setToolTip(QStringLiteral("Open Windows"));
                    break;
                default:
                    setToolTip({});
                    break;
            }
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (hover_slot_ != -1) {
            hover_slot_ = -1;
            update();
        }
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            switch (slotAtX(event->position().x())) {
                case 0:
                    if (on_search_) on_search_();
                    return;
                case 1:
                    if (on_home_) on_home_();
                    return;
                case 2:
                    if (on_windows_) on_windows_();
                    return;
                default:
                    break;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        paintPillBackground(p, QRectF(0.0, 0.0, 130.0, 44.0), 12.0, t.dark);

        constexpr qreal slot_x[3] = {0.0, 43.0, 86.0};
        if (hover_slot_ >= 0 && hover_slot_ < 3 && hover_slot_ != active_slot_) {
            const QRectF box(slot_x[hover_slot_], 0.0, 44.0, 44.0);
            p.setPen(Qt::NoPen);
            p.setBrush(t.dark ? QColor(255, 255, 255, 26)
                              : QColor(255, 255, 255, 82));
            p.drawRoundedRect(box, 12.0, 12.0);
        }

        if (highlight_opacity_ > 0.01) {
            QColor hl = dockHighlightFill(t.dark);
            hl.setAlphaF(hl.alphaF() * std::clamp(highlight_opacity_, 0.0, 1.0));
            p.setPen(Qt::NoPen);
            p.setBrush(hl);
            p.drawRoundedRect(QRectF(highlight_x_, 0.0, 44.0, 44.0), 12.0, 12.0);
        }

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        paintDockGlyph(p, QRectF(8.0, 8.0, 28.0, 28.0),
                       QStringLiteral("search-01"), t.ink, dpr);
        paintDockGlyph(p, QRectF(51.0, 8.0, 28.0, 28.0),
                       QStringLiteral("home-09"), t.ink, dpr);
        paintDockGlyph(p, QRectF(94.0, 8.0, 28.0, 28.0),
                       QStringLiteral("view-open-windows"), t.ink, dpr);

        if (has_running_windows_) {
            p.setPen(Qt::NoPen);
            p.setBrush(windows_active_
                           ? t.accent
                           : QColor(t.ink.red(), t.ink.green(), t.ink.blue(), 150));
            p.drawRoundedRect(QRectF(105.0, 40.0, 6.0, 2.0), 1.0, 1.0);
        }
    }

private:
    static int slotAtX(qreal x) {
        if (x < 43.0) return 0;
        if (x < 86.0) return 1;
        return 2;
    }

    std::function<void()> on_search_;
    std::function<void()> on_home_;
    std::function<void()> on_windows_;
    QVariantAnimation* highlight_x_anim_ = nullptr;
    QVariantAnimation* highlight_opacity_anim_ = nullptr;
    qreal highlight_x_ = 43.0;
    qreal highlight_opacity_ = 0.0;
    int active_slot_ = -1;
    bool search_active_ = false;
    bool home_active_ = false;
    bool windows_active_ = false;
    bool has_running_windows_ = false;
    int hover_slot_ = -1;
};

class HardwarePill final : public QWidget {
public:
    explicit HardwarePill(std::function<void()> on_click, QWidget* parent)
        : QWidget(parent), on_click_(std::move(on_click)) {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(130, 44);
        setCursor(Qt::PointingHandCursor);
    }

    void setActive(bool active) {
        if (active_ == active) return;
        active_ = active;
        update();
    }

    void refreshServices() {
        const auto& svc = finn::sys::FinnDesktopServices::instance();
        const auto& aud = svc.audio();
        const auto& net = svc.network();
        const auto& bat = svc.battery();

        audio_dimmed_ = aud.available && aud.muted;
        wifi_dimmed_ = net.wifi_hardware && !net.wifi_enabled;

        QStringList tips;
        if (aud.available) {
            tips << (aud.muted ? QStringLiteral("Audio muted")
                               : QStringLiteral("Volume %1%").arg(aud.volume_percent));
        }
        if (net.wifi_hardware) {
            if (!net.active_wifi_ssid.isEmpty()) {
                tips << QStringLiteral("Wi-Fi: %1").arg(net.active_wifi_ssid);
            } else {
                tips << (net.wifi_enabled ? QStringLiteral("Wi-Fi on")
                                          : QStringLiteral("Wi-Fi off"));
            }
        } else if (net.wired_connected) {
            tips << QStringLiteral("Wired network");
        } else {
            tips << QStringLiteral("No Wi-Fi hardware");
        }
        tips << (bat.available
                     ? QStringLiteral("Battery %1%").arg(bat.capacity_percent)
                     : QStringLiteral("AC Power"));
        setToolTip(tips.join(QStringLiteral(" · ")));
        update();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && on_click_) {
            on_click_();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        paintPillBackground(p, QRectF(0.0, 0.0, 130.0, 44.0), 12.0, t.dark);

        if (active_) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.dark ? QColor(0x3A, 0x40, 0x50, 0xCC)
                              : QColor(255, 255, 255, 0xB2));
            p.drawRoundedRect(QRectF(3.0, 3.0, 124.0, 38.0), 9.0, 9.0);
        } else if (hovered_) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.dark ? QColor(255, 255, 255, 22)
                              : QColor(255, 255, 255, 72));
            p.drawRoundedRect(QRectF(3.0, 3.0, 124.0, 38.0), 9.0, 9.0);
        }

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        const QColor dim_ink(t.ink.red(), t.ink.green(), t.ink.blue(), 115);

        paintDockGlyph(p, QRectF(8.0, 8.0, 28.0, 28.0),
                       QStringLiteral("volume-02"),
                       audio_dimmed_ ? dim_ink : t.ink, dpr);
        // Figma 2:285 specifies wifi-02 unconditionally in the hardware pill.
        paintDockGlyph(p, QRectF(51.0, 8.0, 28.0, 28.0),
                       QStringLiteral("wifi-02"),
                       wifi_dimmed_ ? dim_ink : t.ink, dpr);
        paintDockGlyph(p, QRectF(94.0, 8.0, 28.0, 28.0),
                       QStringLiteral("battery"), t.ink, dpr);
    }

private:
    std::function<void()> on_click_;
    bool active_ = false;
    bool hovered_ = false;
    bool audio_dimmed_ = false;
    bool wifi_dimmed_ = false;
};

class TimePill final : public QWidget {
public:
    explicit TimePill(std::function<void()> on_click, QWidget* parent)
        : QWidget(parent), on_click_(std::move(on_click)) {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(154, 44);
        setCursor(Qt::PointingHandCursor);
        refreshTime();
    }

    void refreshTime() {
        const QDateTime now = QDateTime::currentDateTime();
        const QString next_date = now.toString(QStringLiteral("ddd d MMM"));
        const QString next_time = now.toString(QStringLiteral("  HH:mm"));
        if (next_date == date_str_ && next_time == time_str_) return;

        date_str_ = next_date;
        time_str_ = next_time;

        QFont font(finn::ui::primary_font_family());
        font.setPixelSize(16);
        font.setWeight(QFont::Normal);
        const QFontMetrics fm(font);
        const int needed = fm.horizontalAdvance(date_str_ + time_str_) + 18;
        setFixedWidth(std::max(154, needed));
        update();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && on_click_) {
            on_click_();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF full(0.0, 0.0, static_cast<qreal>(width()), 44.0);
        paintPillBackground(p, full, 12.0, t.dark);

        if (hovered_) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.dark ? QColor(255, 255, 255, 20)
                              : QColor(255, 255, 255, 64));
            p.drawRoundedRect(full.adjusted(3.0, 3.0, -3.0, -3.0), 9.0, 9.0);
        }

        QFont font(finn::ui::primary_font_family());
        font.setPixelSize(16);
        font.setWeight(QFont::Normal);
        p.setFont(font);

        const QFontMetricsF fm(font);
        const qreal date_w = fm.horizontalAdvance(date_str_);
        const qreal time_w = fm.horizontalAdvance(time_str_);
        const qreal start_x = (static_cast<qreal>(width()) - (date_w + time_w)) * 0.5;

        const QColor date_color =
            t.dark ? QColor(255, 255, 255, 0xB2) : QColor(0, 0, 0, 0xB2);
        const QColor time_color =
            t.dark ? QColor(255, 255, 255) : QColor(0, 0, 0);

        p.setPen(date_color);
        p.drawText(QRectF(start_x, 0.0, date_w + 1.0, 44.0),
                   Qt::AlignVCenter | Qt::AlignLeft, date_str_);
        p.setPen(time_color);
        p.drawText(QRectF(start_x + date_w, 0.0, time_w + 1.0, 44.0),
                   Qt::AlignVCenter | Qt::AlignLeft, time_str_);
    }

private:
    std::function<void()> on_click_;
    QString date_str_;
    QString time_str_;
    bool hovered_ = false;
};

DockWindow::DockWindow(const std::vector<finn::App>* apps,
                       ForeignWindowsManager* windows,
                       std::function<void(const std::string&)> launch_app)
    : FinnLayerWindow(Layer::Top, QStringLiteral("finn-shell-dock")),
      apps_(apps),
      windows_(windows),
      launch_app_(std::move(launch_app)) {
    setAnchors(AnchorBottom | AnchorLeft | AnchorRight);
    setKeyboardMode(KeyboardMode::None);
    setExclusiveZone(59);
    setLayerMargins(QMargins(0, 0, 0, 15));
    setDesiredSize(QSize(0, 44));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 0, 16, 0);
    root->setSpacing(15);
    root->addStretch(1);

    // Pill 1: Apps (Figma 2:285 — 254x44 for 6 pins, r=12)
    apps_pill_ = new AppsPill(this);
    apps_pill_layout_ = new QHBoxLayout(apps_pill_);
    apps_pill_layout_->setContentsMargins(6, 2, 6, 4);
    apps_pill_layout_->setSpacing(10);
    root->addWidget(apps_pill_);

    // Pill 2: Home Buttons (Figma 2:285 — 130x44, r=12)
    home_pill_ = new HomeButtonsPill(
        [this]() { emit searchClicked(); },
        [this]() { emit homeClicked(); },
        [this]() { emit windowsClicked(); },
        this);
    root->addWidget(home_pill_);

    // Pill 3: Hardware Options (Figma 2:285 — 130x44, r=12)
    status_pill_ = new HardwarePill([this]() { emit statusClicked(); }, this);
    root->addWidget(status_pill_);

    // Pill 4: Time (Figma 2:285 — 154x44, r=12)
    time_pill_ = new TimePill([this]() { emit statusClicked(); }, this);
    root->addWidget(time_pill_);

    root->addStretch(1);

    auto* clock_timer = new QTimer(this);
    clock_timer->setInterval(1000);
    connect(clock_timer, &QTimer::timeout, this, &DockWindow::updateStatusPill);
    clock_timer->start();

    connect(&finn::sys::FinnDesktopServices::instance(),
            &finn::sys::FinnDesktopServices::stateUpdated, this,
            &DockWindow::updateStatusPill);
    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this,
            &DockWindow::updateThemeVisuals);
    if (windows_) {
        connect(windows_, &ForeignWindowsManager::windowsChanged, this,
                &DockWindow::rebuildAppsPill);
    }

    rebuildAppsPill();
    updateStatusPill();
}

void DockWindow::updateThemeVisuals() {
    rebuildAppsPill();
    if (home_pill_) home_pill_->update();
    if (status_pill_) status_pill_->update();
    if (time_pill_) time_pill_->update();
}

void DockWindow::rebuildAppsPill() {
    QStringList pins = finn::ui::FinnThemeManager::instance().dockPins();
    if (pins.isEmpty()) {
        pins = {QStringLiteral("files"), QStringLiteral("terminal"),
                QStringLiteral("notes"), QStringLiteral("store"),
                QStringLiteral("settings"), QStringLiteral("monitor")};
    }

    // Track unpinned running windows as well so third-party Linux/Windows apps appear cleanly.
    QStringList shown = pins;
    if (windows_) {
        for (const auto& w : windows_->windows()) {
            QString aid = w.app_id;
            if (aid.isEmpty()) continue;
            if (aid.startsWith(QStringLiteral("finn-"))) aid = aid.mid(5);
            if (!shown.contains(aid, Qt::CaseInsensitive)) {
                shown.push_back(aid);
            }
        }
    }

    if (shown == shown_app_ids_ && apps_pill_layout_->count() == shown.size()) {
        for (int i = 0; i < shown.size(); ++i) {
            const QString& raw_id = shown[i];
            const std::string id = raw_id.toStdString();
            const QString icon_name = normalize_icon_for_app_id(id, apps_);
            const auto running =
                windows_ ? windows_->windowsForApp(raw_id)
                         : QVector<ForeignWindowInfo>{};
            bool is_activated = false;
            for (const auto& rw : running) {
                if (rw.activated) {
                    is_activated = true;
                    break;
                }
            }
            if (QLayoutItem* item = apps_pill_layout_->itemAt(i)) {
                if (auto* btn = dynamic_cast<DockAppButton*>(item->widget())) {
                    btn->setState(icon_name, !running.isEmpty(), is_activated);
                }
            }
        }
        apps_pill_->update();
        if (home_pill_) {
            home_pill_->setHasRunningWindows(windows_ && !windows_->windows().isEmpty());
        }
        return;
    }

    shown_app_ids_ = shown;
    while (QLayoutItem* item = apps_pill_layout_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (const QString& raw_id : shown) {
        const std::string id = raw_id.toStdString();
        const QString icon_name = normalize_icon_for_app_id(id, apps_);
        const auto running =
            windows_ ? windows_->windowsForApp(raw_id)
                     : QVector<ForeignWindowInfo>{};
        bool is_activated = false;
        for (const auto& rw : running) {
            if (rw.activated) {
                is_activated = true;
                break;
            }
        }

        auto on_activate = [this, raw_id, id]() {
            const bool had_popover = any_popover_open_;
            emit dockAppActivated();
            if (windows_) {
                const auto wins = windows_->windowsForApp(raw_id);
                if (!wins.isEmpty()) {
                    const auto& w = wins.first();
                    if (w.minimized || !w.activated || had_popover) {
                        windows_->activateWindow(w.id);
                    } else {
                        windows_->setMinimized(w.id, true);
                    }
                    return;
                }
            }
            if (launch_app_) launch_app_(id);
        };

        auto* btn = new DockAppButton(raw_id, icon_name, !running.isEmpty(),
                                      is_activated, std::move(on_activate),
                                      apps_pill_);
        btn->setToolTip(raw_id);
        apps_pill_layout_->addWidget(btn);
    }

    const int n = static_cast<int>(shown.size());
    const int pill_w = 12 + n * 32 + std::max(0, n - 1) * 10;
    apps_pill_->setFixedWidth(pill_w);
    apps_pill_->update();

    if (home_pill_) {
        home_pill_->setHasRunningWindows(windows_ && !windows_->windows().isEmpty());
    }

    QTimer::singleShot(0, this, &DockWindow::updateInputMask);
}

void DockWindow::setActivePopovers(bool home, bool windows_open, bool search,
                                   bool control) {
    any_popover_open_ = home || windows_open || search || control;
    if (home_pill_) home_pill_->setActivePopovers(search, home, windows_open);
    if (status_pill_) status_pill_->setActive(control);
}

void DockWindow::updateStatusPill() {
    if (time_pill_) time_pill_->refreshTime();
    if (status_pill_) status_pill_->refreshServices();
    QTimer::singleShot(0, this, &DockWindow::updateInputMask);
}

void DockWindow::updateInputMask() {
    QVector<QRect> regions;
    if (apps_pill_) regions.push_back(apps_pill_->geometry());
    if (home_pill_) regions.push_back(home_pill_->geometry());
    if (status_pill_) regions.push_back(status_pill_->geometry());
    if (time_pill_) regions.push_back(time_pill_->geometry());
    setInputRegions(regions);
}

void DockWindow::resizeEvent(QResizeEvent* event) {
    FinnLayerWindow::resizeEvent(event);
    updateInputMask();
}

void DockWindow::showEvent(QShowEvent* event) {
    FinnLayerWindow::showEvent(event);
    QTimer::singleShot(0, this, &DockWindow::updateInputMask);
}

}  // namespace finn::shell
