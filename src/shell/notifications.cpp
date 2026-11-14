// Implements NotificationServer, NotificationsDBusAdaptor, and masked overlay.

#include "shell/notifications.hpp"

#include <QAbstractButton>
#include <QDBusConnection>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
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
#include <utility>

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
        p.setBrush(t.dark ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 22));
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

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        QColor fill = t.dark ? QColor(255, 255, 255, 34) : QColor(0, 0, 0, 20);
        if (isDown()) {
            fill = t.dark ? QColor(255, 255, 255, 64) : QColor(0, 0, 0, 42);
        } else if (hovered_) {
            fill = t.dark ? QColor(255, 255, 255, 48) : QColor(0, 0, 0, 32);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(0.0, 0.0, 28.0, 28.0), 8.0, 8.0);

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
    NotificationCard(const NotificationEntry& n,
                     std::function<void()> on_dismiss,
                     QWidget* parent)
        : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedWidth(340);
        setMinimumHeight(76);

        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(12, 12, 12, 12);
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
        connect(close_btn_, &QAbstractButton::clicked, this,
                [cb = std::move(on_dismiss)]() {
                    if (cb) cb();
                });
        right_col->addWidget(close_btn_, 0, Qt::AlignRight);
        row->addLayout(right_col);

        applyTheme();
        adjustSize();
    }

    void applyTheme() {
        const bool dark = finn::ui::FinnThemeManager::instance().isDark();
        const QColor ink = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
        const QColor dim =
            dark ? QColor(255, 255, 255, 0xB2) : QColor(0, 0, 0, 0xB2);

        setLabelStyle(title_lbl_, 15, ink);
        setLabelStyle(body_lbl_, 13, dim);
        setLabelStyle(time_lbl_, 12, dim);
        if (icon_tile_) icon_tile_->update();
        if (close_btn_) close_btn_->update();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const bool dark = finn::ui::FinnThemeManager::instance().isDark();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(dark ? QColor(0x28, 0x2C, 0x37, 0xD9)
                        : QColor(255, 255, 255, 0xCC));
        p.setPen(QPen(dark ? QColor(255, 255, 255, 28)
                           : QColor(255, 255, 255, 96),
                      1.0));
        p.drawRoundedRect(r, 20.0, 20.0);
    }

private:
    NotificationIconTile* icon_tile_ = nullptr;
    QLabel* title_lbl_ = nullptr;
    QLabel* body_lbl_ = nullptr;
    QLabel* time_lbl_ = nullptr;
    NotificationDismissButton* close_btn_ = nullptr;
};

}  // namespace

class NotificationOverlay final : public finn::ui::FinnLayerWindow {
public:
    explicit NotificationOverlay(NotificationServer* server)
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-notifications")),
          server_(server) {
        setAnchors(AnchorTop | AnchorRight);
        setKeyboardMode(KeyboardMode::None);
        setExclusiveZone(-1);
        setLayerMargins(QMargins(0, 14, 14, 0));
        setDesiredSize(QSize(340, 10));

        layout_ = new QVBoxLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
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

        int total_h = 0;
        for (int i = 0; i < popups.size(); ++i) {
            const auto& n = popups[i];
            auto* card = new NotificationCard(
                n, [this, id = n.id]() { server_->dismissPopup(id); }, this);
            cards_.push_back(card);
            layout_->addWidget(card);
            const int card_h = std::max(76, card->sizeHint().height());
            total_h += card_h;
            if (i + 1 < popups.size()) total_h += 8;
        }

        const int target_h = std::max(76, total_h);
        setDesiredSize(QSize(340, target_h));
        resize(340, target_h);
        show();
        layout_->activate();
        QTimer::singleShot(0, this, &NotificationOverlay::updateInputMask);
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

    QTimer::singleShot(entry.timeout_ms, this,
                       [this, id]() { dismissPopup(id); });
    return id;
}

void NotificationServer::dismissPopup(quint32 id) {
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
