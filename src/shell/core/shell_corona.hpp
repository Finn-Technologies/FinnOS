// ShellCorona: KDE Plasma-inspired unified System UI controller for finn-shell.
// Coordinates form-factor adaptation (Desktop, Tablet, Mobile, TV), app registry
// & Spotlight search, honest hardware/system status, receding Figma notifications,
// running window tracking, and real lock-screen authentication.

#ifndef FINN_SHELL_CORE_SHELL_CORONA_HPP
#define FINN_SHELL_CORE_SHELL_CORONA_HPP

#include "session/app_registry.hpp"
#include "shell/search.hpp"
#include "system/sys_files.hpp"
#include "ui/finn/form_factor.hpp"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <vector>

class QQuickWindow;

namespace finn::shell {

struct NotificationItem {
    std::uint32_t id = 0;
    QString app_name;
    QString icon_name;
    QString summary;
    QString body;
    QString time_label;
    int timeout_ms = 5000;
};

struct TrackedWindowItem {
    QString id;
    QString title;
    QString app_id;
    bool activated = false;
    bool minimized = false;
};

class AppCatalogModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        CategoryRole,
        NoteRole,
        SourceRole,
        InstalledRole,
        PinnedRole,
        RunningRole,
    };

    explicit AppCatalogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    void setPinnedIds(const QStringList& pins);
    void setRunningAppIds(const QStringList& running);

    const std::vector<finn::App>& apps() const { return apps_; }

    Q_INVOKABLE QVariantList pinnedApps() const;
    Q_INVOKABLE QVariantList favoriteApps(int limit = 10) const;
    Q_INVOKABLE QVariantList categoryFolders() const;
    Q_INVOKABLE QVariantList appsInCategory(const QString& category) const;
    Q_INVOKABLE QVariantList recentApps(int limit = 8) const;
    Q_INVOKABLE QVariantList searchQuery(const QString& query) const;

private:
    QVariantMap toMap(const finn::App& app) const;

    std::vector<finn::App> apps_;
    QStringList pinned_ids_;
    QStringList running_ids_;
};

class NotificationListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        IconRole,
        SummaryRole,
        BodyRole,
        TimeRole,
    };

    explicit NotificationListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(items_.size()); }

    Q_INVOKABLE quint32 push(const QString& appName, quint32 replacesId,
                             const QString& iconName, const QString& summary,
                             const QString& body, int timeoutMs = 5000);
    Q_INVOKABLE void dismiss(quint32 id);
    Q_INVOKABLE void clearAll();

signals:
    void countChanged();

private:
    std::vector<NotificationItem> items_;
    std::uint32_t next_id_ = 1;
};

class ShellCorona final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool waylandSession READ waylandSession CONSTANT)
    Q_PROPERTY(QString activePanel READ activePanel NOTIFY panelChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockStateChanged)
    Q_PROPERTY(bool verifyingUnlock READ verifyingUnlock NOTIFY lockStateChanged)
    Q_PROPERTY(QString unlockError READ unlockError NOTIFY lockStateChanged)
    Q_PROPERTY(QString currentUser READ currentUser NOTIFY userChanged)
    Q_PROPERTY(QString currentDisplayName READ currentDisplayName NOTIFY userChanged)
    Q_PROPERTY(QVariantList userAccounts READ userAccounts NOTIFY userChanged)

    // Clock & Date
    Q_PROPERTY(QString dockDateText READ dockDateText NOTIFY clockChanged)
    Q_PROPERTY(QString dockTimeText READ dockTimeText NOTIFY clockChanged)
    Q_PROPERTY(QString lockDateText READ lockDateText NOTIFY clockChanged)
    Q_PROPERTY(QString lockTimeText READ lockTimeText NOTIFY clockChanged)

    // Honest hardware & system status (never fabricated when absent)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY statusChanged)
    Q_PROPERTY(QString wifiSsid READ wifiSsid NOTIFY statusChanged)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY statusChanged)
    Q_PROPERTY(QString bluetoothSubtitle READ bluetoothSubtitle NOTIFY statusChanged)
    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY statusChanged)
    Q_PROPERTY(double volumeLevel READ volumeLevel NOTIFY statusChanged)
    Q_PROPERTY(bool audioMuted READ audioMuted NOTIFY statusChanged)
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY statusChanged)
    Q_PROPERTY(double brightnessLevel READ brightnessLevel NOTIFY statusChanged)
    Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY statusChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY statusChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY statusChanged)
    Q_PROPERTY(QString batteryText READ batteryText NOTIFY statusChanged)
    Q_PROPERTY(bool mediaAvailable READ mediaAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying NOTIFY statusChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY statusChanged)
    Q_PROPERTY(QString mediaArtist READ mediaArtist NOTIFY statusChanged)
    Q_PROPERTY(double mediaProgress READ mediaProgress NOTIFY statusChanged)
    Q_PROPERTY(QString mediaTimeText READ mediaTimeText NOTIFY statusChanged)

    // Open windows (foreign-toplevel) & live Dock taskbar items (pinned + unpinned open apps)
    Q_PROPERTY(QVariantList openWindows READ openWindows NOTIFY windowsChanged)
    Q_PROPERTY(QVariantList dockItems READ dockItems NOTIFY windowsChanged)
    Q_PROPERTY(bool oobeActive READ oobeActive NOTIFY oobeActiveChanged)

public:
    explicit ShellCorona(QObject* parent = nullptr);
    ~ShellCorona() override;

    bool oobeActive() const { return oobe_active_; }

    AppCatalogModel* appModel() const { return app_model_; }
    NotificationListModel* notificationModel() const { return notif_model_; }

    bool waylandSession() const;
    void attachWindow(QQuickWindow* window);
    void updateInputRegion();

    QString activePanel() const { return active_panel_; }
    bool locked() const { return locked_; }
    bool verifyingUnlock() const { return verifying_unlock_; }
    QString unlockError() const { return unlock_error_; }
    QString currentUser() const { return selected_user_; }
    QString currentDisplayName() const { return selected_display_name_; }
    QVariantList userAccounts() const { return user_accounts_; }

    QString dockDateText() const { return dock_date_text_; }
    QString dockTimeText() const { return dock_time_text_; }
    QString lockDateText() const { return lock_date_text_; }
    QString lockTimeText() const { return lock_time_text_; }

    bool wifiAvailable() const { return wifi_available_; }
    bool wifiEnabled() const { return wifi_enabled_; }
    QString wifiSsid() const { return wifi_ssid_; }
    bool bluetoothAvailable() const { return bt_available_; }
    bool bluetoothEnabled() const { return bt_enabled_; }
    QString bluetoothSubtitle() const { return bt_subtitle_; }
    bool audioAvailable() const { return audio_available_; }
    double volumeLevel() const { return volume_level_; }
    bool audioMuted() const { return audio_muted_; }
    bool brightnessAvailable() const { return brightness_available_; }
    double brightnessLevel() const { return brightness_level_; }
    bool batteryAvailable() const { return battery_available_; }
    int batteryPercent() const { return battery_percent_; }
    bool batteryCharging() const { return battery_charging_; }
    QString batteryText() const { return battery_text_; }
    bool mediaAvailable() const { return media_available_; }
    bool mediaPlaying() const { return media_playing_; }
    QString mediaTitle() const { return media_title_; }
    QString mediaArtist() const { return media_artist_; }
    double mediaProgress() const { return media_progress_; }
    QString mediaTimeText() const { return media_time_text_; }

    QVariantList openWindows() const;
    QVariantList dockItems() const;

    // Shell actions callable from SystemUI QML across Desktop/Tablet/Mobile/TV
    Q_INVOKABLE void togglePanel(const QString& panelName);
    Q_INVOKABLE void openPanel(const QString& panelName);
    Q_INVOKABLE void closePanels();
    Q_INVOKABLE void setHoverBlurRect(double x, double y, double w, double h, double radius);
    Q_INVOKABLE void clearHoverBlurRect();

    Q_INVOKABLE void launchApp(const QString& appId);
    Q_INVOKABLE void handleDockAppClick(const QString& appId, const QString& windowId);
    Q_INVOKABLE void openTarget(const QString& target);
    Q_INVOKABLE void openSettings(const QString& pageId = {});
    Q_INVOKABLE void activateWindow(const QString& windowId);
    Q_INVOKABLE void closeWindow(const QString& windowId);

    Q_INVOKABLE void toggleWifi();
    Q_INVOKABLE void toggleBluetooth();
    Q_INVOKABLE void setVolume(double level01);
    Q_INVOKABLE void setBrightness(double level01);
    Q_INVOKABLE void mediaPrev();
    Q_INVOKABLE void mediaPlayPause();
    Q_INVOKABLE void mediaNext();

    Q_INVOKABLE void lockNow();
    Q_INVOKABLE void selectUser(const QString& username);
    Q_INVOKABLE void attemptUnlock(const QString& password);
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void powerOff();

    void startSystemIntegration();

signals:
    void panelChanged();
    void lockStateChanged();
    void unlockFailedShake();
    void userChanged();
    void clockChanged();
    void statusChanged();
    void windowsChanged();
    void oobeActiveChanged();

private:
    void updateClock();
    void refreshHardwareStatus();
    void pollOpenWindows();
    void sendWindowCommand(const QString& verb, const QString& windowId);
    void loadUsers();
    void setupLockWatchers();
    void setupCatalogWatchers();
    void maybeLaunchFirstBootOobe();
    void consumeLockRequest();
    void pollOtaStatus();
    void startPasswordlessCheck(const QString& user);
    void startSuAuth(const QString& user, const QString& password);
    void finishUnlockSuccess();
    void finishUnlockFailure(const QString& reason);

    AppCatalogModel* app_model_ = nullptr;
    NotificationListModel* notif_model_ = nullptr;

    QString active_panel_;
    bool locked_ = false;
    bool verifying_unlock_ = false;
    QString unlock_error_;

    QString selected_user_;
    QString selected_display_name_;
    QVariantList user_accounts_;

    QString dock_date_text_;
    QString dock_time_text_;
    QString lock_date_text_;
    QString lock_time_text_;

    bool wifi_available_ = false;
    bool wifi_enabled_ = false;
    QString wifi_ssid_ = QStringLiteral("Unavailable");
    bool bt_available_ = false;
    bool bt_enabled_ = false;
    QString bt_subtitle_ = QStringLiteral("Unavailable");
    bool audio_available_ = false;
    double volume_level_ = 0.0;
    bool audio_muted_ = false;
    bool brightness_available_ = false;
    double brightness_level_ = 0.0;
    bool battery_available_ = false;
    int battery_percent_ = 0;
    bool battery_charging_ = false;
    QString battery_text_ = QStringLiteral("No battery");
    bool media_available_ = false;
    bool media_playing_ = false;
    QString media_title_ = QStringLiteral("No media playing");
    QString media_artist_;
    double media_progress_ = 0.0;
    QString media_time_text_;

    std::vector<TrackedWindowItem> open_windows_;

    QTimer clock_timer_;
    QTimer status_timer_;
    QTimer windows_timer_;
    QTimer catalog_reload_timer_;
    QFileSystemWatcher lock_watcher_;
    QFileSystemWatcher catalog_watcher_;
    QProcess* auth_proc_ = nullptr;
    QProcess* oobe_proc_ = nullptr;
    bool oobe_active_ = false;
    QRectF hover_blur_rect_;
    double hover_blur_radius_ = 0.0;
    QQuickWindow* shell_window_ = nullptr;
};

}  // namespace finn::shell

#endif  // FINN_SHELL_CORE_SHELL_CORONA_HPP
