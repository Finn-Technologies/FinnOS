// C++20 Qt6 StoreController for the rewritten macOS App Store-inspired Finn Store.
// Integrates clearly separated Native FinnOS, Linux (Flatpak / Flathub API v2),
// Android (Aurora Play FDFE Protobuf -> finn-android), and Windows (winget-pkgs
// -> finn-win) catalogs, live async downloads/installs/updates, and --install
// local package sideloading.

#ifndef FINN_APPS_STORE_CONTROLLER_HPP
#define FINN_APPS_STORE_CONTROLLER_HPP

#include "providers/aurora_play_provider.hpp"
#include "providers/store_types.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace finn::apps {

class StoreController : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString activeTab READ activeTab WRITE setActiveTab NOTIFY filterChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY filterChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY catalogChanged)
    Q_PROPERTY(int installedCount READ installedCount NOTIFY catalogChanged)
    Q_PROPERTY(int updatesCount READ updatesCount NOTIFY catalogChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyMessage READ busyMessage NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap runtimeStatus READ runtimeStatus NOTIFY runtimeChanged)
    Q_PROPERTY(QVariantList heroItems READ heroItems NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList discoverShelves READ discoverShelves NOTIFY catalogChanged)
    Q_PROPERTY(QVariantMap selectedApp READ selectedApp NOTIFY selectedAppChanged)
    Q_PROPERTY(bool detailOpen READ detailOpen NOTIFY selectedAppChanged)
    Q_PROPERTY(QVariantMap localPackage READ localPackage NOTIFY localPackageChanged)
    Q_PROPERTY(bool localPackageOpen READ localPackageOpen NOTIFY localPackageChanged)
    Q_PROPERTY(QString flathubCollection READ flathubCollection WRITE setFlathubCollection NOTIFY flathubBrowseChanged)
    Q_PROPERTY(bool flathubHasMore READ flathubHasMore NOTIFY flathubBrowseChanged)
    Q_PROPERTY(int flathubTotalHits READ flathubTotalHits NOTIFY flathubBrowseChanged)
    Q_PROPERTY(QString androidCollection READ androidCollection WRITE setAndroidCollection NOTIFY flathubBrowseChanged)
    Q_PROPERTY(QString wingetCollection READ wingetCollection WRITE setWingetCollection NOTIFY flathubBrowseChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        SubtitleRole,
        DescriptionRole,
        DeveloperRole,
        VersionRole,
        CategoryRole,
        EcosystemIdRole,
        EcosystemLabelRole,
        IconRole,
        RatingRole,
        SizeLabelRole,
        VerifiedRole,
        VerifiedSourceRole,
        InstalledRole,
        UpdateAvailableRole,
        EditorialTagRole,
        AccentHexRole,
        InstallRefRole,
        ProgressRole,
        TaskStateRole,
    };

    explicit StoreController(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeTab() const { return active_tab_; }
    void setActiveTab(const QString& tab);

    QString searchQuery() const { return search_query_; }
    void setSearchQuery(const QString& q);

    int totalCount() const { return static_cast<int>(filtered_.size()); }
    int installedCount() const { return installed_count_; }
    int updatesCount() const { return updates_count_; }
    bool busy() const { return busy_; }
    QString busyMessage() const { return busy_message_; }
    QVariantMap runtimeStatus() const { return runtime_status_; }
    QVariantList heroItems() const { return hero_items_; }
    QVariantList discoverShelves() const { return discover_shelves_; }
    QVariantMap selectedApp() const { return selected_app_; }
    bool detailOpen() const { return !selected_app_id_.isEmpty(); }
    QVariantMap localPackage() const { return local_package_; }
    bool localPackageOpen() const { return local_package_open_; }
    QString flathubCollection() const { return flathub_collection_; }
    void setFlathubCollection(const QString& col);
    bool flathubHasMore() const { return flathub_page_ < flathub_total_pages_; }
    int flathubTotalHits() const { return flathub_total_hits_; }
    QString androidCollection() const { return android_collection_; }
    void setAndroidCollection(const QString& col);
    QString wingetCollection() const { return winget_collection_; }
    void setWingetCollection(const QString& col);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMoreFlathub();
    Q_INVOKABLE void searchOnline(const QString& query);
    Q_INVOKABLE void openAppDetail(const QString& appId);
    Q_INVOKABLE void closeAppDetail();
    Q_INVOKABLE void installApp(const QString& appId);
    Q_INVOKABLE void launchApp(const QString& appId);
    Q_INVOKABLE void uninstallApp(const QString& appId);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void browseLocalPackage();
    Q_INVOKABLE void inspectLocalPackage(const QString& path);
    Q_INVOKABLE void closeLocalPackage();
    Q_INVOKABLE void confirmInstallLocalPackage();

signals:
    void filterChanged();
    void catalogChanged();
    void busyChanged();
    void runtimeChanged();
    void selectedAppChanged();
    void localPackageChanged();
    void flathubBrowseChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    struct ActiveTask {
        double progress = -1.0;  // <0 means idle, 0..1 active
        QString state;           // "Downloading...", "Installing...", "Updating..."
    };

    void probeRuntimes();
    void rebuildCatalog();
    void rebuildFiltered();
    void rebuildDiscoverShelves();
    void refreshSelectedDetail();
    QVariantMap itemToMap(const finn::store::StoreAppItem& item) const;
    const finn::store::StoreAppItem* findItem(const QString& id) const;

    void setTaskState(const QString& appId, double progress, const QString& state);
    void clearTaskState(const QString& appId);
    void runProcessInstall(const QString& appId, const QString& program,
                           const QStringList& args, const QString& successMsg);

    // Live network providers
    void fetchFlathubCollection(const QString& collection, int page, bool append);
    void queryFlathub(const QString& query);
    void fetchFlathubDetails(const QString& appId, const QString& flatpakId);
    void fetchAndroidCollection(const QString& collection);
    void queryAndroidFDroid(const QString& query);
    void ensureAuroraAuth(std::function<void(bool)> callback);
    void queryAuroraPlay(const QString& query);
    void installAuroraAndroidApp(const finn::store::StoreAppItem& item);
    void fetchWingetCollection(const QString& collection);
    void queryWinget(const QString& query);
    void installWingetApp(const finn::store::StoreAppItem& item);

    QNetworkAccessManager* nam_ = nullptr;
    QString active_tab_ = QStringLiteral("discover");
    QString search_query_;
    bool busy_ = false;
    QString busy_message_;

    QVariantMap runtime_status_;
    finn::store::AuroraAuthSession aurora_session_;
    bool aurora_auth_in_flight_ = false;
    std::vector<std::function<void(bool)>> aurora_waiters_;

    std::vector<finn::store::StoreAppItem> curated_;
    std::vector<finn::store::StoreAppItem> remote_flathub_;
    std::vector<finn::store::StoreAppItem> remote_android_;
    std::vector<finn::store::StoreAppItem> remote_winget_;
    std::vector<finn::store::StoreAppItem> all_items_;
    std::vector<finn::store::StoreAppItem> filtered_;
    QHash<QString, ActiveTask> tasks_;
    QStringList updatable_flatpaks_;

    int installed_count_ = 0;
    int updates_count_ = 0;
    QVariantList hero_items_;
    QVariantList discover_shelves_;

    QString selected_app_id_;
    QVariantMap selected_app_;

    finn::store::LocalPackagePreview local_preview_;
    QVariantMap local_package_;
    bool local_package_open_ = false;

    QString flathub_collection_ = QStringLiteral("popular");
    int flathub_page_ = 0;
    int flathub_total_pages_ = 1;
    int flathub_total_hits_ = 0;
    QString android_collection_ = QStringLiteral("popular");
    QString winget_collection_ = QStringLiteral("popular");
    QTimer* search_debounce_ = nullptr;
};

}  // namespace finn::apps

#endif  // FINN_APPS_STORE_CONTROLLER_HPP
