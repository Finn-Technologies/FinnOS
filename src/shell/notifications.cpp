// Implements NotificationServer, NotificationsDBusAdaptor, and masked overlay.

#include "shell/notifications.hpp"

#include <QAbstractButton>
#include <QDBusConnection>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

#include "shell/shell_layout.hpp"

namespace finn::shell {
namespace {

QString resolveNotificationIcon(QString raw) {
    if (raw.isEmpty()) return QStringLiteral("info");
    if (raw.startsWith(QStringLiteral("finn-"))) raw = raw.mid(5);
    if (raw == QStringLiteral("settings")) return QStringLiteral("settings-app");
    return raw;
}

bool isColoredAppIcon(const QString& name) {
    static const QStringList kColoredApps = {
        QStringLiteral("files"),   QStringLiteral("terminal"),
        QStringLiteral("notes"),   QStringLiteral("store"),
        QStringLiteral("settings-app"), QStringLiteral("viewer"),
        QStringLiteral("monitor"), QStringLiteral("installer"),
        QStringLiteral("browser")};
    return kColoredApps.contains(name);
}

void setLabelStyle(QLabel* lbl, int px, const QColor& color) {
    if (!lbl) return;
    QFont font(finn::ui::primary_font_family());
    font.setPixelSize(px);
    font.setWeight(QFont::Normal);
    lbl->setFont(font);
    QPalette pal = lbl->palette();
    pal.setColor(QPalette::WindowText, color);
    lbl->setPalette(pal);
}

class NotificationIconTile final : public QWidget {
public:
    NotificationIconTile(QString icon_name, QWidget* parent)
        : QWidget(parent), icon_name_(resolveNotificationIcon(std::move(icon_name))) {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(40, 40);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        if (isColoredAppIcon(icon_name_)) {
            QPainterPath clip;
            clip.addRoundedRect(QRectF(0.0, 0.0, 40.0, 40.0), 10.0, 10.0);
            p.setClipPath(clip);
            const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
                icon_name_, QSize(40, 40), QColor(), dpr);
            if (!pm.isNull()) {
                p.drawPixmap(QRect(0, 0, 40, 40), pm);
            }
            return;
        }

        p.setPen(Qt::NoPen);
        p.setBrush(shell_hover_fill(t));
        p.drawRoundedRect(QRectF(0.0, 0.0, 40.0, 40.0), 10.0, 10.0);

        const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
            icon_name_, QSize(22, 22), t.ink, dpr);
        if (!pm.isNull()) {
            p.drawPixmap(QPointF(9.0, 9.0), pm);
        }
    }

private:
    QString icon_name_;
};

class NotificationDismissButton final : public QAbstractButton {
public:
    explicit NotificationDismissButton(QWidget* parent) : QAbstractButton(parent) {
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(QStringLiteral("Dismiss notification"));
        setToolTip(QStringLiteral("Dismiss"));
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            click();
            event->accept();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        QColor fill = shell_hover_fill(t);
        if (isDown()) {
            fill = shell_pressed_fill(t);
        } else if (hovered_) {
            fill = with_alpha(t.ink, t.dark ? 48 : 32);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(0.0, 0.0, 28.0, 28.0), finn::ui::radii::r_md,
                          finn::ui::radii::r_md);
        if (hasFocus()) {
            draw_focus_ring(p, QRectF(0.0, 0.0, 28.0, 28.0),
                            finn::ui::radii::r_md, t);
        }

        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
            QStringLiteral("x"), QSize(12, 12), t.ink, dpr);
        if (!pm.isNull()) {
            p.drawPixmap(QPointF(8.0, 8.0), pm);
        }
    }

private:
    bool hovered_ = false;
};

class NotificationCard final : public QWidget {
public:
    NotificationCard(const NotificationEntry& n, int card_width,
                     std::function<void()> on_dismiss, QWidget* parent)
        : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        // Transparent rim around the glass card for its elevation shadow.
        setFixedWidth(card_width + 2 * kNotificationShadowPad);
        setMinimumHeight(76 + 2 * kNotificationShadowPad);
        setAccessibleName(n.app_name.isEmpty()
                              ? n.summary
                              : QStringLiteral("%1: %2").arg(n.app_name, n.summary));
        enter_anim_ = std::make_unique<ShellSpringDriver>(
            this, finn::ui::springs::kSnappy,
            finn::ui::reduced_motion() ? 1.0 : 0.0,
            [this](double v) {
                enter_progress_ = std::clamp(v, 0.0, 1.0);
                update();
            },
            [this]() {
                if (dismissing_ && on_dismiss_) {
                    auto cb = std::move(on_dismiss_);
                    cb();
                }
            });
        enter_anim_->retarget(1.0);

        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(12 + kNotificationShadowPad,
                                12 + kNotificationShadowPad,
                                12 + kNotificationShadowPad,
                                12 + kNotificationShadowPad);
        row->setSpacing(12);

        icon_tile_ = new NotificationIconTile(n.app_icon, this);
        row->addWidget(icon_tile_, 0, Qt::AlignVCenter);

        auto* col = new QVBoxLayout();
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
        const QString header =
            n.app_name.isEmpty()
                ? n.summary
                : QStringLiteral("%1 — %2").arg(n.app_name, n.summary);
        title_lbl_ = new QLabel(header, this);
        title_lbl_->setWordWrap(true);
        col->addWidget(title_lbl_);

        if (!n.body.isEmpty()) {
            body_lbl_ = new QLabel(n.body, this);
            body_lbl_->setWordWrap(true);
            col->addWidget(body_lbl_);
        }
        row->addLayout(col, 1);

        auto* right_col = new QVBoxLayout();
        right_col->setContentsMargins(0, 0, 0, 0);
        right_col->setSpacing(6);
        right_col->setAlignment(Qt::AlignRight | Qt::AlignTop);

        time_lbl_ = new QLabel(QStringLiteral("now"), this);
        right_col->addWidget(time_lbl_, 0, Qt::AlignRight);

        close_btn_ = new NotificationDismissButton(this);
        on_dismiss_ = std::move(on_dismiss);
        connect(close_btn_, &QAbstractButton::clicked, this, [this]() {
            if (dismissing_) return;
            dismissing_ = true;
            if (finn::ui::reduced_motion()) {
                if (on_dismiss_) on_dismiss_();
                return;
            }
            enter_anim_->retarget(0.0, &finn::ui::springs::kSnappy);
        });
        right_col->addWidget(close_btn_, 0, Qt::AlignRight);
        row->addLayout(right_col);

        applyTheme();
        adjustSize();
    }

    void applyTheme() {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        setLabelStyle(title_lbl_, 15, t.ink);
        setLabelStyle(body_lbl_, 13, t.dim);
        setLabelStyle(time_lbl_, 12, t.dim);
        if (icon_tile_) icon_tile_->update();
        if (close_btn_) close_btn_->update();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(enter_progress_);
        // macOS notification banner glide from top-right.
        p.translate((1.0 - enter_progress_) * 26.0, (1.0 - enter_progress_) * -4.0);

        const QRectF outer = QRectF(rect());
        const QRectF r = outer.adjusted(
            kNotificationShadowPad + 0.5, kNotificationShadowPad + 0.5,
            -kNotificationShadowPad - 0.5, -kNotificationShadowPad - 0.5);
        paintLayerShadow(p, r, finn::ui::radii::r_xl, kNotificationShadowBlur,
                         finn::ui::elevation::elev_notification);
        p.setBrush(shell_glass_fill(t));
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(r, finn::ui::radii::r_xl, finn::ui::radii::r_xl);
    }

private:
    NotificationIconTile* icon_tile_ = nullptr;
    QLabel* title_lbl_ = nullptr;
    QLabel* body_lbl_ = nullptr;
    QLabel* time_lbl_ = nullptr;
    NotificationDismissButton* close_btn_ = nullptr;
    std::unique_ptr<ShellSpringDriver> enter_anim_;
    std::function<void()> on_dismiss_;
    double enter_progress_ = 1.0;
    bool dismissing_ = false;
};

}  // namespace

class NotificationOverlay final : public finn::ui::FinnLayerWindow {
public:
    explicit NotificationOverlay(NotificationServer* server)
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-notifications")),
          server_(server) {
        setKeyboardMode(KeyboardMode::None);
        setExclusiveZone(-1);
        setAccessibleName(QStringLiteral("Notifications"));
        applyResponsiveGeometry(10);

        layout_ = new QVBoxLayout(this);
        layout_->setContentsMargins(kNotificationShadowPad,
                                    kNotificationShadowPad,
                                    kNotificationShadowPad,
                                    kNotificationShadowPad);
        layout_->setSpacing(8);

        connect(&finn::ui::FinnThemeManager::instance(),
                &finn::ui::FinnThemeManager::themeChanged, this,
                &NotificationOverlay::refreshTheme);
    }

    void syncPopups(const QVector<NotificationEntry>& popups) {
        while (QLayoutItem* item = layout_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        cards_.clear();

        if (popups.isEmpty()) {
            setInputRegions({});
            hide();
            return;
        }

        const int card_w = popupWidth();
        int total_h = 0;
        for (int i = 0; i < popups.size(); ++i) {
            const auto& n = popups[i];
            auto* card = new NotificationCard(
                n, card_w, [this, id = n.id]() { server_->dismissPopup(id); },
                this);
            cards_.push_back(card);
            layout_->addWidget(card);
            const int card_h = std::max(
                76, card->sizeHint().height() - 2 * kNotificationShadowPad);
            total_h += card_h;
            if (i + 1 < popups.size()) total_h += 8;
        }

        const int target_h = std::max(76, total_h);
        applyResponsiveGeometry(target_h);
        show();
        layout_->activate();
        QTimer::singleShot(0, this, &NotificationOverlay::updateInputMask);
    }

private:
    int popupWidth() const {
        const FormFactor ff = current_form_factor();
        const int sw = primary_screen_size().width();
        if (ff == FormFactor::Phone) {
            return std::max(260, std::min(400, sw - 24));
        }
        return std::min(340, std::max(260, sw - 28));
    }

    void applyResponsiveGeometry(int target_h) {
        const FormFactor ff = current_form_factor();
        const int card_w = popupWidth();
        if (ff == FormFactor::Phone) {
            setAnchors(AnchorTop);
            setLayerMargins(QMargins(
                std::max(0, 12 - kNotificationShadowPad),
                std::max(0, 44 - kNotificationShadowPad),
                std::max(0, 12 - kNotificationShadowPad), 0));
        } else {
            setAnchors(AnchorTop | AnchorRight);
            setLayerMargins(QMargins(
                0, std::max(0, 14 - kNotificationShadowPad),
                std::max(0, 14 - kNotificationShadowPad), 0));
        }
        setDesiredSize(QSize(card_w + 2 * kNotificationShadowPad,
                             target_h + 2 * kNotificationShadowPad));
        resize(card_w + 2 * kNotificationShadowPad,
               target_h + 2 * kNotificationShadowPad);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        FinnLayerWindow::resizeEvent(event);
        updateInputMask();
    }

    void showEvent(QShowEvent* event) override {
        FinnLayerWindow::showEvent(event);
        QTimer::singleShot(0, this, &NotificationOverlay::updateInputMask);
    }

private:
    void refreshTheme() {
        for (NotificationCard* card : cards_) {
            if (card) card->applyTheme();
        }
        update();
    }

    void updateInputMask() {
        if (!isVisible()) {
            setInputRegions({});
            return;
        }
        QVector<QRect> regions;
        for (int i = 0; i < layout_->count(); ++i) {
            if (QWidget* w = layout_->itemAt(i)->widget()) {
                if (!w->geometry().isEmpty()) {
                    regions.push_back(w->geometry());
                }
            }
        }
        setInputRegions(regions);
    }

    NotificationServer* server_;
    QVBoxLayout* layout_ = nullptr;
    QVector<NotificationCard*> cards_;
};

NotificationServer::NotificationServer(QObject* parent) : QObject(parent) {
    new NotificationsDBusAdaptor(this);
    overlay_ = new NotificationOverlay(this);
}

bool NotificationServer::startDBusService() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications"))) {
        return false;
    }
    return bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                              this);
}

quint32 NotificationServer::pushNotification(const QString& app_name,
                                             quint32 replaces_id,
                                             const QString& app_icon,
                                             const QString& summary,
                                             const QString& body,
                                             int expire_timeout_ms) {
    const quint32 id = (replaces_id > 0) ? replaces_id : next_id_++;
    NotificationEntry entry;
    entry.id = id;
    entry.app_name = app_name;
    entry.app_icon = app_icon;
    entry.summary = summary;
    entry.body = body;
    entry.received_at = QDateTime::currentDateTime();
    entry.timeout_ms = (expire_timeout_ms > 0) ? expire_timeout_ms : 4500;

    auto erase_id = [id](QVector<NotificationEntry>& list) {
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [id](const auto& e) { return e.id == id; }),
                   list.end());
    };
    erase_id(history_);
    erase_id(active_popups_);

    history_.push_front(entry);
    if (history_.size() > 40) history_.resize(40);
    active_popups_.push_front(entry);
    if (active_popups_.size() > 4) active_popups_.resize(4);

    overlay_->syncPopups(active_popups_);
    emit historyChanged();

    cancelPopupTimer(id);
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id]() {
        active_timers_.remove(id);
        dismissPopup(id);
    });
    active_timers_.insert(id, timer);
    timer->start(entry.timeout_ms);
    return id;
}

void NotificationServer::cancelPopupTimer(quint32 id) {
    if (auto it = active_timers_.find(id); it != active_timers_.end()) {
        if (QTimer* t = it.value()) {
            t->stop();
            t->deleteLater();
        }
        active_timers_.erase(it);
    }
}

void NotificationServer::dismissPopup(quint32 id) {
    cancelPopupTimer(id);
    const auto old_n = active_popups_.size();
    active_popups_.erase(
        std::remove_if(active_popups_.begin(), active_popups_.end(),
                       [id](const auto& e) { return e.id == id; }),
        active_popups_.end());
    if (active_popups_.size() != old_n) {
        overlay_->syncPopups(active_popups_);
        emit notificationClosedSignal(id, 2);
    }
}

void NotificationServer::removeFromHistory(quint32 id) {
    dismissPopup(id);
    history_.erase(std::remove_if(history_.begin(), history_.end(),
                                  [id](const auto& e) { return e.id == id; }),
                   history_.end());
    emit historyChanged();
}

void NotificationServer::clearHistory() {
    for (QTimer* t : active_timers_) {
        if (t) {
            t->stop();
            t->deleteLater();
        }
    }
    active_timers_.clear();
    active_popups_.clear();
    history_.clear();
    overlay_->syncPopups(active_popups_);
    emit historyChanged();
}

NotificationsDBusAdaptor::NotificationsDBusAdaptor(NotificationServer* parent)
    : QDBusAbstractAdaptor(parent), server_(parent) {
    connect(server_, &NotificationServer::notificationClosedSignal, this,
            &NotificationsDBusAdaptor::NotificationClosed);
}

QStringList NotificationsDBusAdaptor::GetCapabilities() {
    return {QStringLiteral("body"), QStringLiteral("icon-static"),
            QStringLiteral("persistence")};
}

quint32 NotificationsDBusAdaptor::Notify(
    const QString& app_name, quint32 replaces_id, const QString& app_icon,
    const QString& summary, const QString& body, const QStringList&,
    const QVariantMap&, int expire_timeout) {
    return server_->pushNotification(app_name, replaces_id, app_icon, summary,
                                     body, expire_timeout);
}

void NotificationsDBusAdaptor::CloseNotification(quint32 id) {
    server_->dismissPopup(id);
}

QString NotificationsDBusAdaptor::GetServerInformation(QString& vendor,
                                                       QString& version,
                                                       QString& spec_version) {
    vendor = QStringLiteral("FinnOS");
    version = QStringLiteral("0.2.0");
    spec_version = QStringLiteral("1.2");
    return QStringLiteral("finn-shell");
}

}  // namespace finn::shell
