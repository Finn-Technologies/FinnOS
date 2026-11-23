// C++20 Software Center & App Catalog backend for finn-store.
// Uses finn::AppRegistry and real binary probes — never fakes installs or feeds.

#ifndef FINN_APPS_STORE_CONTROLLER_HPP
#define FINN_APPS_STORE_CONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QVariantList>

#include "session/app_registry.hpp"

namespace finn::apps {

class StoreController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList catalogApps READ catalogApps NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList installedApps READ installedApps NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList runtimeBackends READ runtimeBackends NOTIFY catalogChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY filterChanged)
    Q_PROPERTY(bool flatpakAvailable READ flatpakAvailable CONSTANT)
    Q_PROPERTY(int installedCount READ installedCount NOTIFY catalogChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY catalogChanged)

public:
    explicit StoreController(QObject* parent = nullptr);

    QVariantList catalogApps() const;
    QVariantList installedApps() const;
    QVariantList runtimeBackends() const { return backends_; }
    QString searchQuery() const { return search_; }
    void setSearchQuery(const QString& q);
    bool flatpakAvailable() const { return flatpak_available_; }
    int installedCount() const;
    int totalCount() const { return static_cast<int>(apps_.size()); }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void launchApp(const QString& appId);

signals:
    void catalogChanged();
    void filterChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    QVariantMap toMap(const finn::App& app) const;
    bool matchesFilter(const finn::App& app) const;

    std::vector<finn::App> apps_;
    QVariantList backends_;
    QString search_;
    bool flatpak_available_ = false;
};

}  // namespace finn::apps

#endif  // FINN_APPS_STORE_CONTROLLER_HPP
