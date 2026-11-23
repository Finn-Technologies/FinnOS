// C++20 Software Center backend implementation for finn-store.

#include "store_controller.hpp"
#include "common/paths.hpp"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

namespace finn::apps {

StoreController::StoreController(QObject* parent) : QObject(parent) {
    flatpak_available_ = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
    reload();
}

void StoreController::reload() {
    QString cat_path = QString::fromLatin1(finn::kCataloguePath);
    if (!QFileInfo::exists(cat_path) && QFileInfo::exists(QStringLiteral("assets/apps.registry"))) {
        cat_path = QStringLiteral("assets/apps.registry");
    }
    const auto reg = finn::AppRegistry::load_all(cat_path.toStdString(), finn::kInstalledPath);
    apps_ = reg.apps();
    bool any_installed = false;
    for (const auto& a : apps_) {
        if (a.installed) { any_installed = true; break; }
    }
    if (!any_installed) {
        for (auto& a : apps_) {
            if (a.source == finn::AppSource::kNative) a.installed = true;
        }
    }

    backends_.clear();
    auto probe = [this](const QString& name, const QString& bin, const QString& desc) {
        const bool avail = !QStandardPaths::findExecutable(bin).isEmpty();
        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("available"), avail);
        m.insert(QStringLiteral("status"), avail ? QStringLiteral("Ready") : QStringLiteral("Not installed"));
        m.insert(QStringLiteral("description"), desc);
        backends_.append(m);
    };
    probe(QStringLiteral("Native FinnSDK"), QStringLiteral("finn-settings"),
          QStringLiteral("C++20 / Qt 6 FinnUI adaptive system and third-party apps"));
    probe(QStringLiteral("Flatpak"), QStringLiteral("flatpak"),
          QStringLiteral("Sandboxed Linux desktop packages"));
    probe(QStringLiteral("Wine"), QStringLiteral("wine"),
          QStringLiteral("Windows executable and MSI runner"));
    probe(QStringLiteral("Finn Android"), QStringLiteral("finn-android"),
          QStringLiteral("Android APK runtime"));
    probe(QStringLiteral("Finn Darwin"), QStringLiteral("finn-darwin"),
          QStringLiteral("macOS Mach-O compatibility runtime"));

    emit catalogChanged();
}

void StoreController::setSearchQuery(const QString& q) {
    if (q == search_) return;
    search_ = q;
    emit filterChanged();
    emit catalogChanged();
}

bool StoreController::matchesFilter(const finn::App& app) const {
    const QString needle = search_.trimmed().toLower();
    if (needle.isEmpty()) return true;
    return QString::fromStdString(app.name).toLower().contains(needle) ||
           QString::fromStdString(app.id).toLower().contains(needle) ||
           QString::fromStdString(app.note).toLower().contains(needle);
}

QVariantMap StoreController::toMap(const finn::App& app) const {
    QVariantMap m;
    m.insert(QStringLiteral("appId"), QString::fromStdString(app.id));
    m.insert(QStringLiteral("name"), QString::fromStdString(app.name));
    m.insert(QStringLiteral("icon"), QString::fromStdString(app.icon));
    m.insert(QStringLiteral("note"), QString::fromStdString(app.note));
    m.insert(QStringLiteral("source"), QString::fromLatin1(finn::app_source_name(app.source)));
    m.insert(QStringLiteral("installed"), app.installed);
    m.insert(QStringLiteral("exec"), QString::fromStdString(app.command));
    return m;
}

QVariantList StoreController::catalogApps() const {
    QVariantList out;
    for (const auto& a : apps_) {
        if (matchesFilter(a)) out.append(toMap(a));
    }
    return out;
}

QVariantList StoreController::installedApps() const {
    QVariantList out;
    for (const auto& a : apps_) {
        if (a.installed && matchesFilter(a)) out.append(toMap(a));
    }
    return out;
}

int StoreController::installedCount() const {
    int count = 0;
    for (const auto& a : apps_) {
        if (a.installed) ++count;
    }
    return count;
}

void StoreController::launchApp(const QString& appId) {
    for (const auto& a : apps_) {
        if (QString::fromStdString(a.id) == appId) {
            const QString bin = QString::fromStdString(a.command).section(QLatin1Char(' '), 0, 0);
            if (!bin.isEmpty() && QProcess::startDetached(bin, {})) {
                emit toastRequested(QStringLiteral("Launched %1").arg(QString::fromStdString(a.name)),
                                    QStringLiteral("success"));
                return;
            }
            emit toastRequested(
                QStringLiteral("%1 executable is not on PATH in this environment").arg(QString::fromStdString(a.name)),
                QStringLiteral("warning"));
            return;
        }
    }
}

}  // namespace finn::apps
