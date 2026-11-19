// Implements DockWindow: 4 centered Figma glass pills with exact geometry,
// 1:1 active highlights, full-bleed app icons, and masked input regions.

#include "shell/dock.hpp"

#include <QAbstractButton>
#include <QDateTime>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "shell/shell_layout.hpp"
#include "system/desktop_services.hpp"

namespace finn::shell {
namespace {

void paintPillBackground(QPainter& p, const QRectF& rect, qreal radius,
                         const finn::ui::FinnTokens& t) {
    p.setPen(QPen(t.hair, 1.0));
    p.setBrush(shell_dock_fill(t));
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
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
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(app_id_);

        hover_anim_ = std::make_unique<ShellSpringDriver>(
            this, finn::ui::springs::kSnappy, 0.0, [this](double val) {
                hover_progress_ = std::clamp(val, 0.0, 1.2);
                update();
            });
        bounce_anim_ = std::make_unique<ShellSpringDriver>(
            this, finn::ui::springs::kBouncy, 0.0, [this](double val) {
                bounce_offset_ = std::clamp(val, -8.0, 2.0);
                update();
            });
        press_anim_ = std::make_unique<ShellSpringDriver>(
            this, finn::ui::springs::kSnappy, 0.0, [this](double val) {
                press_progress_ = std::clamp(val, 0.0, 1.0);
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
        if (finn::ui::reduced_motion()) return;
        bounce_anim_->impulse(-140.0, 0.0);
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        hover_anim_->retarget(1.0);
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        pressed_ = false;
        hover_anim_->retarget(0.0);
        press_anim_->retarget(0.0);
        QAbstractButton::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            triggerBounce();
            if (on_activate_) on_activate_();
            event->accept();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            press_anim_->retarget(1.0);
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
            press_anim_->retarget(0.0);
            update();
        }
        QAbstractButton::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // macOS-style spring hover lift + subtle magnification + elastic press compression.
        const qreal lift_y = -4.5 * hover_progress_ + bounce_offset_;
        const qreal scale = (1.0 + 0.12 * hover_progress_) * (1.0 - 0.10 * press_progress_);

        p.save();
        p.translate(16.0, 4.0 + lift_y + 16.0);
        p.scale(scale, scale);
        p.translate(-16.0, -16.0);

        const QRectF r(0.0, 0.0, 32.0, 32.0);
        QPainterPath clip;
        clip.addRoundedRect(r, finn::ui::radii::r_md, finn::ui::radii::r_md);
        p.setClipPath(clip);

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
            icon_name_, QSize(32, 32), QColor(), dpr);
        if (!pm.isNull()) {
            p.drawPixmap(QRect(0, 0, 32, 32), pm);
        }

        if (hovered_ || pressed_) {
            p.fillRect(r, pressed_ ? shell_pressed_fill(t) : shell_hover_fill(t));
        }
        p.restore();

        if (hasFocus()) {
            draw_focus_ring(p, QRectF(0.0, 3.0, 32.0, 34.0),
                            finn::ui::radii::r_md, t);
        }
    }

private:
    QString app_id_;
    QString icon_name_;
    std::function<void()> on_activate_;
    std::unique_ptr<ShellSpringDriver> hover_anim_;
    std::unique_ptr<ShellSpringDriver> bounce_anim_;
    std::unique_ptr<ShellSpringDriver> press_anim_;
    qreal hover_progress_ = 0.0;
    qreal bounce_offset_ = 0.0;
    qreal press_progress_ = 0.0;
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
        paintPillBackground(p, QRectF(rect()), finn::ui::radii::r_lg, t);

        const auto buttons = findChildren<QAbstractButton*>();
        for (const QAbstractButton* abtn : buttons) {
            const auto* btn = dynamic_cast<const DockAppButton*>(abtn);
            if (!btn || !btn->isRunning()) continue;
            const QColor dot = btn->isActivated()
                                   ? t.accent
                                   : with_alpha(t.ink, 150);
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
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(QStringLiteral("Search, Home, and Open Windows"));

        highlight_x_anim_ = std::make_unique<ShellSpringDriver>(
            this, finn::ui::springs::kSnappy, 43.0, [this](double val) {
                highlight_x_ = val;
                update();
            });
        highlight_opacity_anim_ = std::make_unique<ShellSpringDriver>(
            this, finn::ui::springs::kSnappy, 0.0, [this](double val) {
                highlight_opacity_ = std::clamp(val, 0.0, 1.0);
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

        if (next_slot >= 0) {
            const qreal target_x = kSlotX[next_slot];
            if (active_slot_ >= 0 && active_slot_ != next_slot) {
                highlight_x_anim_->retarget(target_x);
                highlight_opacity_anim_->set(1.0);
            } else {
                highlight_x_anim_->set(target_x);
                highlight_opacity_anim_->retarget(1.0);
            }
        } else {
            highlight_opacity_anim_->retarget(0.0);
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
            updateSlotTooltip(hover_slot_);
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

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Left) {
            focused_slot_ = (focused_slot_ + 2) % 3;
            updateSlotTooltip(focused_slot_);
            update();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            focused_slot_ = (focused_slot_ + 1) % 3;
            updateSlotTooltip(focused_slot_);
            update();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            activateSlot(focused_slot_);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const int slot = slotAtX(event->position().x());
            focused_slot_ = slot;
            activateSlot(slot);
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        paintPillBackground(p, QRectF(0.0, 0.0, 130.0, 44.0),
                            finn::ui::radii::r_lg, t);

        constexpr qreal slot_x[3] = {0.0, 43.0, 86.0};
        if (hover_slot_ >= 0 && hover_slot_ < 3 && hover_slot_ != active_slot_) {
            const QRectF box(slot_x[hover_slot_], 0.0, 44.0, 44.0);
            p.setPen(Qt::NoPen);
            p.setBrush(shell_hover_fill(t));
            p.drawRoundedRect(box, finn::ui::radii::r_lg, finn::ui::radii::r_lg);
        }

        if (highlight_opacity_ > 0.01) {
            QColor hl = with_alpha(t.tile_on, t.dark ? 220 : 236);
            hl.setAlphaF(hl.alphaF() * std::clamp(highlight_opacity_, 0.0, 1.0));
            p.setPen(Qt::NoPen);
            p.setBrush(hl);
            p.drawRoundedRect(QRectF(highlight_x_, 0.0, 44.0, 44.0),
                              finn::ui::radii::r_lg, finn::ui::radii::r_lg);
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
            p.setBrush(windows_active_ ? t.accent : with_alpha(t.ink, 150));
            p.drawRoundedRect(QRectF(105.0, 40.0, 6.0, 2.0), 1.0, 1.0);
        }

        if (hasFocus()) {
            const QRectF focus_box(slot_x[std::clamp(focused_slot_, 0, 2)], 1.0,
                                   44.0, 42.0);
            draw_focus_ring(p, focus_box, finn::ui::radii::r_lg, t);
        }
    }

private:
    static int slotAtX(qreal x) {
        if (x < 43.0) return 0;
        if (x < 86.0) return 1;
        return 2;
    }

    void updateSlotTooltip(int slot) {
        switch (slot) {
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
    }

    void activateSlot(int slot) {
        switch (slot) {
            case 0:
                if (on_search_) on_search_();
                break;
            case 1:
                if (on_home_) on_home_();
                break;
            case 2:
                if (on_windows_) on_windows_();
                break;
            default:
                break;
        }
    }

    std::function<void()> on_search_;
    std::function<void()> on_home_;
    std::function<void()> on_windows_;
    std::unique_ptr<ShellSpringDriver> highlight_x_anim_;
    std::unique_ptr<ShellSpringDriver> highlight_opacity_anim_;
    qreal highlight_x_ = 43.0;
    qreal highlight_opacity_ = 0.0;
    int active_slot_ = -1;
    int focused_slot_ = 1;
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
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(QStringLiteral("Control Center and System Status"));
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

    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) &&
            on_click_) {
            on_click_();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
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
        paintPillBackground(p, QRectF(0.0, 0.0, 130.0, 44.0),
                            finn::ui::radii::r_lg, t);

        if (active_) {
            p.setPen(Qt::NoPen);
            p.setBrush(with_alpha(t.tile_on, t.dark ? 216 : 228));
            p.drawRoundedRect(QRectF(3.0, 3.0, 124.0, 38.0), 9.0, 9.0);
        } else if (hovered_) {
            p.setPen(Qt::NoPen);
            p.setBrush(shell_hover_fill(t));
            p.drawRoundedRect(QRectF(3.0, 3.0, 124.0, 38.0), 9.0, 9.0);
        }

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        const QColor dim_ink = with_alpha(t.ink, 115);

        paintDockGlyph(p, QRectF(8.0, 8.0, 28.0, 28.0),
                       QStringLiteral("volume-02"),
                       audio_dimmed_ ? dim_ink : t.ink, dpr);
        // Figma 2:285 specifies wifi-02 unconditionally in the hardware pill.
        paintDockGlyph(p, QRectF(51.0, 8.0, 28.0, 28.0),
                       QStringLiteral("wifi-02"),
                       wifi_dimmed_ ? dim_ink : t.ink, dpr);
        paintDockGlyph(p, QRectF(94.0, 8.0, 28.0, 28.0),
                       QStringLiteral("battery"), t.ink, dpr);

        if (hasFocus()) {
            draw_focus_ring(p, QRectF(0.0, 0.0, 130.0, 44.0),
                            finn::ui::radii::r_lg, t);
        }
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
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(QStringLiteral("Date, Time, and Notifications"));
        refreshTime();
    }

    void setCompact(bool compact) {
        if (compact_ == compact) return;
        compact_ = compact;
        date_str_.clear();
        time_str_.clear();
        refreshTime();
    }

    void refreshTime() {
        const QDateTime now = QDateTime::currentDateTime();
        const QString next_date =
            compact_ ? QString() : now.toString(QStringLiteral("ddd d MMM"));
        const QString next_time =
            compact_ ? now.toString(QStringLiteral("HH:mm"))
                     : now.toString(QStringLiteral("  HH:mm"));
        if (next_date == date_str_ && next_time == time_str_) return;

        date_str_ = next_date;
        time_str_ = next_time;

        QFont font(finn::ui::primary_font_family());
        font.setPixelSize(16);
        font.setWeight(QFont::Normal);
        const QFontMetrics fm(font);
        const int needed = fm.horizontalAdvance(date_str_ + time_str_) + 18;
        setFixedWidth(std::max(compact_ ? 74 : 154, needed));
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

    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) &&
            on_click_) {
            on_click_();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
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
        paintPillBackground(p, full, finn::ui::radii::r_lg, t);

        if (hovered_) {
            p.setPen(Qt::NoPen);
            p.setBrush(shell_hover_fill(t));
            p.drawRoundedRect(full.adjusted(3.0, 3.0, -3.0, -3.0), 9.0, 9.0);
        }

        QFont font(finn::ui::primary_font_family());
        font.setPixelSize(16);
        font.setWeight(QFont::Normal);
        p.setFont(font);

        const QFontMetricsF fm(font);
        const qreal date_w = fm.horizontalAdvance(date_str_);
        const qreal time_w = fm.horizontalAdvance(time_str_);
        const qreal start_x =
            (static_cast<qreal>(width()) - (date_w + time_w)) * 0.5;

        p.setPen(t.dim);
        if (!date_str_.isEmpty()) {
            p.drawText(QRectF(start_x, 0.0, date_w + 1.0, 44.0),
                       Qt::AlignVCenter | Qt::AlignLeft, date_str_);
        }
        p.setPen(t.ink);
        p.drawText(QRectF(start_x + date_w, 0.0, time_w + 1.0, 44.0),
                   Qt::AlignVCenter | Qt::AlignLeft, time_str_);

        if (hasFocus()) {
            draw_focus_ring(p, full, finn::ui::radii::r_lg, t);
        }
    }

private:
    std::function<void()> on_click_;
    QString date_str_;
    QString time_str_;
    bool hovered_ = false;
    bool compact_ = false;
};

class PhoneStatusBarWindow final : public finn::ui::FinnLayerWindow {
public:
    explicit PhoneStatusBarWindow(std::function<void()> on_click)
        : FinnLayerWindow(Layer::Top, QStringLiteral("finn-phone-statusbar")),
          on_click_(std::move(on_click)) {
        setAnchors(AnchorTop | AnchorLeft | AnchorRight);
        setKeyboardMode(KeyboardMode::None);
        setExclusiveZone(36);
        setDesiredSize(QSize(0, 36));
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(QStringLiteral("Phone Status Bar"));
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && on_click_) {
            on_click_();
            return;
        }
        FinnLayerWindow::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.fillRect(rect(), shell_dock_fill(t));
        p.setPen(QPen(t.hair, 1.0));
        p.drawLine(0, height() - 1, width(), height() - 1);

        QFont font(finn::ui::primary_font_family());
        font.setPixelSize(14);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.setPen(t.ink);
        const QString time_str =
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
        p.drawText(QRect(16, 0, 80, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   time_str);

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        const auto& svc = finn::sys::FinnDesktopServices::instance();
        const QColor dim_ink = with_alpha(t.ink, 115);
        paintDockGlyph(p, QRectF(width() - 78.0, 9.0, 18.0, 18.0),
                       QStringLiteral("wifi-02"),
                       (svc.network().wifi_hardware &&
                        !svc.network().wifi_enabled)
                           ? dim_ink
                           : t.ink,
                       dpr);
        paintDockGlyph(p, QRectF(width() - 52.0, 9.0, 18.0, 18.0),
                       QStringLiteral("volume-02"),
                       (svc.audio().available && svc.audio().muted) ? dim_ink
                                                                    : t.ink,
                       dpr);
        paintDockGlyph(p, QRectF(width() - 28.0, 9.0, 18.0, 18.0),
                       QStringLiteral("battery"), t.ink, dpr);
    }

private:
    std::function<void()> on_click_;
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
    setAccessibleName(QStringLiteral("Dock"));

    root_layout_ = new QHBoxLayout(this);
    root_layout_->setContentsMargins(16, 0, 16, 0);
    root_layout_->setSpacing(15);
    root_layout_->addStretch(1);

    // Pill 1: Apps (Figma 2:285 — 254x44 for 6 pins, r=12)
    apps_pill_ = new AppsPill(this);
    apps_pill_layout_ = new QHBoxLayout(apps_pill_);
    apps_pill_layout_->setContentsMargins(6, 2, 6, 4);
    apps_pill_layout_->setSpacing(10);
    root_layout_->addWidget(apps_pill_);

    // Pill 2: Home Buttons (Figma 2:285 — 130x44, r=12)
    home_pill_ = new HomeButtonsPill(
        [this]() { emit searchClicked(); },
        [this]() { emit homeClicked(); },
        [this]() { emit windowsClicked(); },
        this);
    root_layout_->addWidget(home_pill_);

    // Pill 3: Hardware Options (Figma 2:285 — 130x44, r=12)
    status_pill_ = new HardwarePill([this]() { emit statusClicked(); }, this);
    root_layout_->addWidget(status_pill_);

    // Pill 4: Time (Figma 2:285 — 154x44, r=12)
    time_pill_ = new TimePill([this]() { emit statusClicked(); }, this);
    root_layout_->addWidget(time_pill_);

    root_layout_->addStretch(1);

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

    applyResponsiveLayout();
    rebuildAppsPill();
    updateStatusPill();
}

DockWindow::~DockWindow() {
    delete phone_status_bar_;
    phone_status_bar_ = nullptr;
}

void DockWindow::applyResponsiveLayout() {
    const FormFactor ff = current_form_factor();
    const int sw = primary_screen_size().width();

    if (ff == FormFactor::Phone) {
        if (!phone_status_bar_) {
            phone_status_bar_ =
                new PhoneStatusBarWindow([this]() { emit statusClicked(); });
        }
        phone_status_bar_->show();

        setExclusiveZone(54);
        setLayerMargins(QMargins(0, 0, 0, 10));
        if (root_layout_) {
            root_layout_->setContentsMargins(8, 0, 8, 0);
            root_layout_->setSpacing(8);
        }
        if (status_pill_) status_pill_->setVisible(false);
        if (time_pill_) time_pill_->setVisible(false);
    } else {
        if (phone_status_bar_) {
            phone_status_bar_->hide();
        }
        const bool compact_tablet =
            (ff == FormFactor::Tablet) && (sw < 840);
        setExclusiveZone(ff == FormFactor::Tablet ? 52 : 59);
        setLayerMargins(QMargins(0, 0, 0, ff == FormFactor::Tablet ? 8 : 15));
        if (root_layout_) {
            root_layout_->setContentsMargins(ff == FormFactor::Tablet ? 10 : 16,
                                             0,
                                             ff == FormFactor::Tablet ? 10 : 16,
                                             0);
            root_layout_->setSpacing(ff == FormFactor::Tablet ? 10 : 15);
        }
        if (status_pill_) status_pill_->setVisible(true);
        if (time_pill_) {
            time_pill_->setVisible(true);
            time_pill_->setCompact(compact_tablet);
        }
    }
}

void DockWindow::updateThemeVisuals() {
    rebuildAppsPill();
    if (home_pill_) home_pill_->update();
    if (status_pill_) status_pill_->update();
    if (time_pill_) time_pill_->update();
    if (phone_status_bar_) phone_status_bar_->update();
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

    const FormFactor ff = current_form_factor();
    const int max_visible =
        (ff == FormFactor::Phone) ? 4 : ((ff == FormFactor::Tablet) ? 6 : 12);
    if (shown.size() > max_visible) {
        shown = shown.mid(0, max_visible);
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
    if (phone_status_bar_) phone_status_bar_->update();
    QTimer::singleShot(0, this, &DockWindow::updateInputMask);
}

void DockWindow::updateInputMask() {
    QVector<QRect> regions;
    if (apps_pill_ && apps_pill_->isVisible()) {
        regions.push_back(apps_pill_->geometry());
    }
    if (home_pill_ && home_pill_->isVisible()) {
        regions.push_back(home_pill_->geometry());
    }
    if (status_pill_ && status_pill_->isVisible()) {
        regions.push_back(status_pill_->geometry());
    }
    if (time_pill_ && time_pill_->isVisible()) {
        regions.push_back(time_pill_->geometry());
    }
    setInputRegions(regions);
}

void DockWindow::resizeEvent(QResizeEvent* event) {
    FinnLayerWindow::resizeEvent(event);
    applyResponsiveLayout();
    rebuildAppsPill();
    updateInputMask();
}

void DockWindow::showEvent(QShowEvent* event) {
    FinnLayerWindow::showEvent(event);
    QTimer::singleShot(0, this, &DockWindow::updateInputMask);
}

}  // namespace finn::shell
