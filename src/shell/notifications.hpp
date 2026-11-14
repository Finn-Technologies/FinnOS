// Freedesktop org.freedesktop.Notifications D-Bus daemon + top-right overlay
// toast layer with strict masked input regions and live notification history.

#ifndef FINN_SHELL_NOTIFICATIONS_HPP
#define FINN_SHELL_NOTIFICATIONS_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QDBusAbstractAdaptor>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class QVBoxLayout;

namespace finn::shell {

struct NotificationEntry {
    quint32 id = 0;
    QString app_name;
    QString app_icon;
    QString summary;
    QString body;
    QDateTime received_at;
    int timeout_ms = 4500;
};

class NotificationOverlay;

class NotificationServer final : public QObject {
    Q_OBJECT

public:
    explicit NotificationServer(QObject* parent = nullptr);
    ~NotificationServer() override = default;

    bool startDBusService();
    const QVector<NotificationEntry>& history() const { return history_; }

    quint32 pushNotification(const QString& app_name, quint32 replaces_id,
                             const QString& app_icon, const QString& summary,
                             const QString& body, int expire_timeout_ms);
    void dismissPopup(quint32 id);
    void removeFromHistory(quint32 id);
    void clearHistory();

signals:
    void historyChanged();
    void notificationClosedSignal(quint32 id, quint32 reason);

private:
    quint32 next_id_ = 1;
    QVector<NotificationEntry> history_;
    QVector<NotificationEntry> active_popups_;
    NotificationOverlay* overlay_ = nullptr;
};

class NotificationsDBusAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    explicit NotificationsDBusAdaptor(NotificationServer* parent);

public slots:
    QStringList GetCapabilities();
    quint32 Notify(const QString& app_name, quint32 replaces_id,
                   const QString& app_icon, const QString& summary,
                   const QString& body, const QStringList& actions,
                   const QVariantMap& hints, int expire_timeout);
    void CloseNotification(quint32 id);
    QString GetServerInformation(QString& vendor, QString& version,
                                 QString& spec_version);

signals:
    void NotificationClosed(quint32 id, quint32 reason);
    void ActionInvoked(quint32 id, const QString& action_key);

private:
    NotificationServer* server_;
};

}  // namespace finn::shell

#endif
