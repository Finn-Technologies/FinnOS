#include "shell/core/shell_corona.hpp"

#include "common/paths.hpp"
#include "session/client.hpp"
#include "shell/oobe_capability_probe.hpp"
#include "ui/finn/theme.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QRegion>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

#ifdef FINN_HAVE_QT_DBUS
#include <QDBusConnection>
#endif

namespace finn::shell {
namespace {

QString resolve_catalogue_path() {
    if (QFileInfo::exists(QString::fromLatin1(finn::kCataloguePath))) {
        return QString::fromLatin1(finn::kCataloguePath);
    }
    const QStringList local = {
        QDir::currentPath() + QStringLiteral("/assets/apps.registry"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../assets/apps.registry"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../assets/apps.registry"),
    };
    for (const QString& p : local) {
        if (QFileInfo::exists(p)) return p;
    }
    return QString::fromLatin1(finn::kCataloguePath);
}

QString category_for_app(const finn::App& app) {
    if (!app.category.empty()) {
        const QString cat = QString::fromStdString(app.category).trimmed();
        if (cat.compare(QStringLiteral("Developer"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("Development");
        }
        return cat;
    }
    const QString id = QString::fromStdString(app.id).toLower();
    if (id == QLatin1String("settings") || id == QLatin1String("monitor") ||
        id == QLatin1String("files") || id == QLatin1String("archive")) {
        return QStringLiteral("System");
    }
    if (id == QLatin1String("terminal")) return QStringLiteral("Development");
    if (id == QLatin1String("notes") || id == QLatin1String("calculator")) return QStringLiteral("Productivity");
    if (id == QLatin1String("viewer") || id == QLatin1String("capture")) return QStringLiteral("Media");
    if (id == QLatin1String("store") || id == QLatin1String("flourish")) return QStringLiteral("Internet");
    return QStringLiteral("Other");
}

}  // namespace

// ---- AppCatalogModel ----

AppCatalogModel::AppCatalogModel(QObject* parent) : QAbstractListModel(parent) {
    reload();
}

int AppCatalogModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(apps_.size());
}

QVariant AppCatalogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= apps_.size()) {
        return {};
    }
    const auto& app = apps_[static_cast<std::size_t>(index.row())];
    const QString id = QString::fromStdString(app.id);
    switch (role) {
        case IdRole:        return id;
        case NameRole:      return QString::fromStdString(app.name);
        case IconRole:      return QString::fromStdString(app.icon);
        case CategoryRole:  return category_for_app(app);
        case NoteRole:      return QString::fromStdString(app.note);
        case SourceRole:    return QString::fromLatin1(finn::app_source_name(app.source));
        case InstalledRole: return app.installed;
        case PinnedRole:    return pinned_ids_.contains(id);
        case RunningRole:   return running_ids_.contains(id);
        default:            return {};
    }
}

QHash<int, QByteArray> AppCatalogModel::roleNames() const {
    return {
        {IdRole, "appId"},
        {NameRole, "name"},
        {IconRole, "icon"},
        {CategoryRole, "category"},
        {NoteRole, "note"},
        {SourceRole, "source"},
        {InstalledRole, "installed"},
        {PinnedRole, "pinned"},
        {RunningRole, "running"},
    };
}

void AppCatalogModel::reload() {
    beginResetModel();
    const QString cat = resolve_catalogue_path();
    auto reg = finn::AppRegistry::load_all(cat.toStdString(), finn::kInstalledPath);
    apps_ = reg.apps();
    // On uninstalled dev hosts where /var/lib/finn/installed is absent, treat
    // built-in first-party catalogue entries as installed so the shell renders them.
    if (!QFileInfo::exists(QString::fromLatin1(finn::kInstalledPath))) {
        for (auto& a : apps_) {
            if (a.source == finn::AppSource::kNative) a.installed = true;
        }
    }
    const auto settings = finn::sys::load_merged_settings();
    pinned_ids_.clear();
    for (const auto& pin : settings.dock_pins) {
        pinned_ids_.append(QString::fromStdString(pin));
    }
    if (pinned_ids_.isEmpty()) {
        pinned_ids_ = {QStringLiteral("files"), QStringLiteral("terminal"),
                       QStringLiteral("notes"), QStringLiteral("store"),
                       QStringLiteral("viewer"), QStringLiteral("settings")};
    }
    endResetModel();
}

void AppCatalogModel::setPinnedIds(const QStringList& pins) {
    pinned_ids_ = pins;
    if (!apps_.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(apps_.size()) - 1), {PinnedRole});
    }
}

void AppCatalogModel::setRunningAppIds(const QStringList& running) {
    running_ids_ = running;
    if (!apps_.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(apps_.size()) - 1), {RunningRole});
    }
}

QVariantMap AppCatalogModel::toMap(const finn::App& app) const {
    const QString id = QString::fromStdString(app.id);
    QVariantMap m;
    m.insert(QStringLiteral("appId"), id);
    m.insert(QStringLiteral("name"), QString::fromStdString(app.name));
    m.insert(QStringLiteral("icon"), QString::fromStdString(app.icon));
    m.insert(QStringLiteral("category"), category_for_app(app));
    m.insert(QStringLiteral("note"), QString::fromStdString(app.note));
    m.insert(QStringLiteral("installed"), app.installed);
    m.insert(QStringLiteral("running"), running_ids_.contains(id));
    return m;
}

QVariantList AppCatalogModel::pinnedApps() const {
    QVariantList out;
    for (const QString& pin : pinned_ids_) {
        for (const auto& a : apps_) {
            if (QString::fromStdString(a.id) == pin) {
                out.append(toMap(a));
                break;
            }
        }
    }
    // Ensure 6 pinned slots match Figma 4:58 dock layout (254x44, 6x 32x32 tiles)
    if (out.size() < 6) {
        const QStringList fallback_order = {
            QStringLiteral("files"), QStringLiteral("flourish"),
            QStringLiteral("terminal"), QStringLiteral("notes"),
            QStringLiteral("store"), QStringLiteral("settings")
        };
        for (const QString& fid : fallback_order) {
            bool already = false;
            for (const QVariant& v : out) {
                if (v.toMap().value(QStringLiteral("appId")).toString() == fid) {
                    already = true;
                    break;
                }
            }
            if (already) continue;
            for (const auto& a : apps_) {
                if (QString::fromStdString(a.id) == fid) {
                    out.append(toMap(a));
                    break;
                }
            }
            if (out.size() >= 6) break;
        }
    }
    return out;
}

QVariantList AppCatalogModel::favoriteApps(int limit) const {
    QVariantList out;
    for (const auto& a : apps_) {
        if (!a.installed) continue;
        out.append(toMap(a));
        if (out.size() >= limit) break;
    }
    return out;
}

QVariantList AppCatalogModel::categoryFolders() const {
    static const QStringList kOrder = {
        QStringLiteral("Productivity"), QStringLiteral("System"),
        QStringLiteral("Development"), QStringLiteral("Media"),
        QStringLiteral("Internet"), QStringLiteral("Other"),
    };
    QVariantList out;
    for (const QString& cat : kOrder) {
        QVariantList icons;
        int count = 0;
        for (const auto& a : apps_) {
            if (!a.installed) continue;
            if (category_for_app(a) == cat) {
                ++count;
                if (icons.size() < 7) icons.append(QString::fromStdString(a.icon));
            }
        }
        QVariantMap folder;
        folder.insert(QStringLiteral("name"), cat);
        folder.insert(QStringLiteral("count"), count);
        folder.insert(QStringLiteral("icons"), icons);
        out.append(folder);
    }
    return out;
}

QVariantList AppCatalogModel::appsInCategory(const QString& category) const {
    QVariantList out;
    for (const auto& a : apps_) {
        if (!a.installed) continue;
        if (category.isEmpty() || category_for_app(a) == category) {
            out.append(toMap(a));
        }
    }
    return out;
}

QVariantList AppCatalogModel::recentApps(int limit) const {
    QVariantList out;
    const auto recents = finn::sys::load_recent_apps();
    for (const auto& rid : recents) {
        for (const auto& a : apps_) {
            if (a.id == rid) {
                out.append(toMap(a));
                break;
            }
        }
        if (out.size() >= limit) break;
    }
    QVariantList installed_list;
    for (const auto& a : apps_) {
        if (a.installed) installed_list.append(toMap(a));
    }
    int idx = 0;
    while (out.size() < limit && !installed_list.isEmpty()) {
        out.append(installed_list[idx % installed_list.size()]);
        ++idx;
    }
    return out;
}

QVariantList AppCatalogModel::searchQuery(const QString& query) const {
    QVariantList out;
    const QString q = query.trimmed();
    if (q.isEmpty()) return out;

    // Inline calculator evaluation
    const std::string math = finn::sys::evaluate_math_expression(q.toStdString());
    if (!math.empty()) {
        QVariantMap calc;
        calc.insert(QStringLiteral("kind"), QStringLiteral("math"));
        calc.insert(QStringLiteral("title"), QString::fromStdString(math));
        calc.insert(QStringLiteral("subtitle"), QStringLiteral("= %1").arg(q));
        calc.insert(QStringLiteral("icon"), QStringLiteral("search-01"));
        calc.insert(QStringLiteral("action"), QString());
        out.append(calc);
    }

    // Search registered apps using the shared pure-C++ matcher
    std::vector<finn::SessionApp> session_apps;
    session_apps.reserve(apps_.size());
    for (const auto& a : apps_) {
        finn::SessionApp sa;
        sa.id = a.id;
        sa.name = a.name;
        sa.icon = a.icon;
        sa.installed = a.installed;
        sa.note = a.note;
        session_apps.push_back(std::move(sa));
    }
    const auto matches = finn::search_apps(session_apps, q.toStdString());
    for (const auto& sa : matches) {
        QVariantMap row;
        row.insert(QStringLiteral("kind"), QStringLiteral("app"));
        row.insert(QStringLiteral("appId"), QString::fromStdString(sa.id));
        row.insert(QStringLiteral("title"), QString::fromStdString(sa.name));
        row.insert(QStringLiteral("subtitle"), QString::fromStdString(sa.note));
        row.insert(QStringLiteral("icon"), QString::fromStdString(sa.icon));
        out.append(row);
        if (out.size() >= 8) break;
    }

    // Quick theme toggle actions
    const QString ql = q.toLower();
    if (ql.contains(QLatin1String("dark")) || ql.contains(QLatin1String("light")) ||
        ql.contains(QLatin1String("theme"))) {
        QVariantMap act;
        act.insert(QStringLiteral("kind"), QStringLiteral("toggle_theme"));
        act.insert(QStringLiteral("title"), QStringLiteral("Toggle Dark / Light Appearance"));
        act.insert(QStringLiteral("subtitle"), QStringLiteral("System Appearance"));
        act.insert(QStringLiteral("icon"), QStringLiteral("swatch-book"));
        out.append(act);
    }
    return out;
}

// ---- NotificationListModel ----

NotificationListModel::NotificationListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int NotificationListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(items_.size());
}

QVariant NotificationListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= items_.size()) {
        return {};
    }
    const auto& n = items_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole:      return static_cast<quint32>(n.id);
        case AppNameRole: return n.app_name;
        case IconRole:    return n.icon_name;
        case SummaryRole: return n.summary;
        case BodyRole:    return n.body;
        case TimeRole:    return n.time_label;
        default:          return {};
    }
}

QHash<int, QByteArray> NotificationListModel::roleNames() const {
    return {
        {IdRole, "notifId"},
        {AppNameRole, "appName"},
        {IconRole, "icon"},
        {SummaryRole, "summary"},
        {BodyRole, "body"},
        {TimeRole, "timeLabel"},
    };
}

quint32 NotificationListModel::push(const QString& appName, quint32 replacesId,
                                    const QString& iconName, const QString& summary,
                                    const QString& body, int timeoutMs) {
    const std::uint32_t id = replacesId != 0 ? replacesId : next_id_++;
    NotificationItem item;
    item.id = id;
    item.app_name = appName.isEmpty() ? QStringLiteral("FinnOS") : appName;
    item.icon_name = iconName.isEmpty() ? QStringLiteral("info") : iconName;
    item.summary = summary;
    item.body = body;
    item.time_label = QStringLiteral("now");
    item.timeout_ms = timeoutMs;

    auto it = std::find_if(items_.begin(), items_.end(),
                           [id](const NotificationItem& n) { return n.id == id; });
    if (it != items_.end()) {
        *it = item;
        const int row = static_cast<int>(std::distance(items_.begin(), it));
        emit dataChanged(index(row), index(row));
    } else {
        beginInsertRows(QModelIndex(), 0, 0);
        items_.insert(items_.begin(), item);
        endInsertRows();
        while (items_.size() > 6) {
            const int last = static_cast<int>(items_.size()) - 1;
            beginRemoveRows(QModelIndex(), last, last);
            items_.pop_back();
            endRemoveRows();
        }
        emit countChanged();
    }

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, id]() { dismiss(id); });
    }
    return id;
}

void NotificationListModel::dismiss(quint32 id) {
    auto it = std::find_if(items_.begin(), items_.end(),
                           [id](const NotificationItem& n) { return n.id == id; });
    if (it == items_.end()) return;
    const int row = static_cast<int>(std::distance(items_.begin(), it));
    beginRemoveRows(QModelIndex(), row, row);
    items_.erase(it);
    endRemoveRows();
    emit countChanged();
}

void NotificationListModel::clearAll() {
    if (items_.empty()) return;
    beginResetModel();
    items_.clear();
    endResetModel();
    emit countChanged();
}

// ---- ShellCorona ----

ShellCorona::ShellCorona(QObject* parent)
    : QObject(parent),
      app_model_(new AppCatalogModel(this)),
      notif_model_(new NotificationListModel(this)) {
    loadUsers();
    updateClock();
    refreshHardwareStatus();

    clock_timer_.setInterval(1000);
    connect(&clock_timer_, &QTimer::timeout, this, &ShellCorona::updateClock);
    clock_timer_.start();

    status_timer_.setInterval(5000);
    connect(&status_timer_, &QTimer::timeout, this, &ShellCorona::refreshHardwareStatus);
    status_timer_.start();

    windows_timer_.setInterval(250);
    connect(&windows_timer_, &QTimer::timeout, this, &ShellCorona::pollOpenWindows);
    windows_timer_.start();
    pollOpenWindows();

    connect(this, &ShellCorona::panelChanged, this, &ShellCorona::updateInputRegion);
    connect(this, &ShellCorona::lockStateChanged, this, &ShellCorona::updateInputRegion);
    connect(this, &ShellCorona::windowsChanged, this, &ShellCorona::updateInputRegion);
    connect(notif_model_, &NotificationListModel::countChanged, this,
            &ShellCorona::updateInputRegion);
}

bool ShellCorona::waylandSession() const {
    return QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                    Qt::CaseInsensitive);
}

void ShellCorona::attachWindow(QQuickWindow* window) {
    shell_window_ = window;
    if (!shell_window_) return;
    connect(shell_window_, &QWindow::widthChanged, this,
            [this](int) { updateInputRegion(); });
    connect(shell_window_, &QWindow::heightChanged, this,
            [this](int) { updateInputRegion(); });
    updateInputRegion();
}

void ShellCorona::updateInputRegion() {
    if (!shell_window_ || !waylandSession()) return;
    const int w = std::max(1, shell_window_->width());
    const int h = std::max(1, shell_window_->height());

    // Compute exact Dock pill rects for both Wayland input mask & real-time compositor blur
    const QVariantList items = dockItems();
    const int n_items = std::max(6, static_cast<int>(items.size()));
    bool has_sep = false;
    for (const QVariant& iv : items) {
        if (iv.toMap().value(QStringLiteral("separatorBefore")).toBool()) {
            has_sep = true;
            break;
        }
    }
    const int apps_pill_w = std::max(60, 12 + n_items * 32 + std::max(0, n_items - 1) * 10 + (has_sep ? 11 : 0));
    constexpr int kHomeW = 130;
    constexpr int kHwW = 130;
    constexpr int kTimeW = 154;
    constexpr int kGap = 15;
    constexpr int kDockH = 44;
    const int dock_total_w = apps_pill_w + kGap + kHomeW + kGap + kHwW + kGap + kTimeW;
    const int dock_x = std::max(0, (w - dock_total_w) / 2);
    const int dock_y = std::max(0, h - 15 - kDockH);

    // Publish exact frosted-glass rects so finn-comp blurs windows + wallpaper behind them in real time
    {
        QByteArray out;
        auto addRect = [&out](int rx, int ry, int rw, int rh, int rr) {
            out += QByteArray::number(rx) + '\t' + QByteArray::number(ry) + '\t' +
                   QByteArray::number(rw) + '\t' + QByteArray::number(rh) + '\t' +
                   QByteArray::number(rr) + '\n';
        };
        if (!locked_) {
            addRect(dock_x, dock_y, apps_pill_w, kDockH, 12);
            addRect(dock_x + apps_pill_w + kGap, dock_y, kHomeW, kDockH, 12);
            addRect(dock_x + apps_pill_w + kGap + kHomeW + kGap, dock_y, kHwW, kDockH, 12);
            addRect(dock_x + apps_pill_w + kGap + kHomeW + kGap + kHwW + kGap, dock_y, kTimeW, kDockH, 12);
            if (active_panel_ == QLatin1String("home")) {
                addRect((w - 580) / 2, std::max(12, dock_y - 436 - 14), 580, 436, 16);
            } else if (active_panel_ == QLatin1String("search")) {
                addRect((w - 540) / 2, std::max(12, dock_y - 360 - 14), 540, 360, 16);
            } else if (active_panel_ == QLatin1String("control")) {
                addRect((w - 340) / 2 + 120, std::max(12, dock_y - 380 - 14), 340, 380, 16);
            } else if (active_panel_ == QLatin1String("windows")) {
                addRect((w - 560) / 2, std::max(12, dock_y - 320 - 14), 560, 320, 16);
            }
        }
        const QString dir = QString::fromLatin1(finn::kCompositorStateDir);
        const QString tmp = dir + QStringLiteral("/blur-rects.tsv.tmp");
        const QString dst = dir + QStringLiteral("/blur-rects.tsv");
        QFile f(tmp);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(out);
            f.close();
            QFile::remove(dst);
            QFile::rename(tmp, dst);
        }
    }

    if (locked_ || !active_panel_.isEmpty()) {
        shell_window_->setMask(QRegion());
        return;
    }
    QRegion reg;
    reg |= QRect(std::max(0, dock_x - 8), std::max(0, dock_y - 8),
                 dock_total_w + 16, kDockH + 16);
    if (notif_model_ && notif_model_->count() > 0) {
        reg |= QRect(std::max(0, w - 364), 12, 352, 160);
    }
    shell_window_->setMask(reg);
}

ShellCorona::~ShellCorona() {
    if (auth_proc_ && auth_proc_->state() != QProcess::NotRunning) {
        auth_proc_->kill();
        auth_proc_->waitForFinished(500);
    }
}

void ShellCorona::startSystemIntegration() {
    setupLockWatchers();
    setupCatalogWatchers();
    maybeLaunchFirstBootOobe();
    QTimer::singleShot(4000, this, &ShellCorona::pollOtaStatus);
}

void ShellCorona::setupCatalogWatchers() {
    catalog_reload_timer_.setSingleShot(true);
    catalog_reload_timer_.setInterval(200);
    connect(&catalog_reload_timer_, &QTimer::timeout, this, [this]() {
        app_model_->reload();
        emit windowsChanged();
    });

    auto addWatchPath = [this](const QString& p) {
        if (QFileInfo::exists(p) && !catalog_watcher_.files().contains(p) &&
            !catalog_watcher_.directories().contains(p)) {
            catalog_watcher_.addPath(p);
        }
    };

    const QString home = QDir::homePath();
    const QStringList dirs = {
        QStringLiteral("/var/lib/finn"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        home + QStringLiteral("/.local/share/finn"),
        home + QStringLiteral("/.local/share/finn/apps"),
        home + QStringLiteral("/.local/share/finn/macos"),
        home + QStringLiteral("/.local/share/finn/macos/Applications"),
        home + QStringLiteral("/.local/share/finn/ohos"),
        home + QStringLiteral("/.local/share/finn/android"),
        home + QStringLiteral("/.local/share/finn/appimages"),
        home + QStringLiteral("/.local/share/finn/wine"),
        home + QStringLiteral("/.local/share/applications"),
        home + QStringLiteral("/.local/share/applications/wine"),
        home + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
        home + QStringLiteral("/Applications"),
    };
    for (const QString& d : dirs) {
        QDir().mkpath(d.startsWith(home + QStringLiteral("/.local/share/finn")) ? d : QString());
        addWatchPath(d);
    }
    const QStringList files = {
        QString::fromLatin1(finn::kInstalledPath),
        home + QLatin1Char('/') + QString::fromLatin1(finn::kUserWindowsConfRel),
        home + QLatin1Char('/') + QString::fromLatin1(finn::kUserOhosRegistryRel),
        home + QLatin1Char('/') + QString::fromLatin1(finn::kUserAndroidRegistryRel),
        home + QLatin1Char('/') + QString::fromLatin1(finn::kWinePrefixRel) + QStringLiteral("/system.reg"),
        home + QLatin1Char('/') + QString::fromLatin1(finn::kWinePrefixRel) + QStringLiteral("/user.reg"),
    };
    for (const QString& f : files) addWatchPath(f);

    connect(&catalog_watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this, addWatchPath](const QString& dir) {
                addWatchPath(dir);
                catalog_reload_timer_.start();
            });
    connect(&catalog_watcher_, &QFileSystemWatcher::fileChanged, this,
            [this, addWatchPath](const QString& file) {
                addWatchPath(file);
                catalog_reload_timer_.start();
            });
}

void ShellCorona::maybeLaunchFirstBootOobe() {
    QString cmdline;
    {
        QFile cf(QStringLiteral("/proc/cmdline"));
        if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cmdline = QString::fromUtf8(cf.readAll());
        }
    }
    const bool live_marker =
        QFileInfo::exists(QStringLiteral("/run/archiso")) ||
        QFileInfo::exists(QString::fromLatin1(finn::kRecoverySessionMarkerPath));
    const bool installed_marker =
        QFileInfo::exists(QString::fromLatin1(finn::kInstalledPath));
    const bool sys_complete =
        QFileInfo::exists(QString::fromLatin1(finn::kSystemOobeCompletePath));
    const QString user_complete_path =
        QDir::homePath() + QLatin1Char('/') + QString::fromLatin1(finn::kOobeCompleteRel);
    const bool user_complete = QFileInfo::exists(user_complete_path);

    if (!finn::should_run_oobe_on_boot(cmdline.toStdString(),
                                       live_marker,
                                       installed_marker,
                                       sys_complete,
                                       user_complete,
                                       std::getenv("FINN_OOBE_FORCE"))) {
        return;
    }

    // Write measured preflight capability snapshot before spawning finn-oobe.
    const QString preflight_path =
        QDir::homePath() + QLatin1Char('/') +
        QString::fromLatin1(finn::shell::oobe::probe::kOobeCapabilitiesRel);
    QDir().mkpath(QFileInfo(preflight_path).absolutePath());
    finn::shell::oobe::probe::write_preflight(
        preflight_path, finn::shell::oobe::probe::capabilities());

    QString oobe_exe = QStandardPaths::findExecutable(QStringLiteral("finn-oobe"));
    if (oobe_exe.isEmpty() && QFileInfo::exists(QString::fromLatin1(finn::kOobeBinPath))) {
        oobe_exe = QString::fromLatin1(finn::kOobeBinPath);
    }
    if (oobe_exe.isEmpty()) {
        const QString sibling =
            QCoreApplication::applicationDirPath() + QStringLiteral("/../../apps/oobe/finn-oobe");
        if (QFileInfo::exists(sibling)) oobe_exe = sibling;
    }
    if (oobe_exe.isEmpty()) return;

    oobe_active_ = true;
    emit oobeActiveChanged();

    oobe_proc_ = new QProcess(this);
    connect(oobe_proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                oobe_active_ = false;
                emit oobeActiveChanged();
                loadUsers();
                finn::ui::FinnThemeManager::instance().reload();
                app_model_->reload();
                emit windowsChanged();
            });
    oobe_proc_->start(oobe_exe, {QStringLiteral("--capabilities"), preflight_path});
}

void ShellCorona::updateClock() {
    const QDateTime now = QDateTime::currentDateTime();
    const QString dd = now.toString(QStringLiteral("ddd d MMM"));
    const QString dt = now.toString(QStringLiteral("HH:mm"));
    const QString ld = now.toString(QStringLiteral("dddd, d MMMM"));
    if (dd != dock_date_text_ || dt != dock_time_text_ || ld != lock_date_text_) {
        dock_date_text_ = dd;
        dock_time_text_ = dt;
        lock_date_text_ = ld;
        lock_time_text_ = dt;
        emit clockChanged();
    }
}

void ShellCorona::refreshHardwareStatus() {
    // Strictly honest hardware probes — missing devices stay available=false!
    const bool prev_bl_avail = brightness_available_;
    const double prev_bl = brightness_level_;
    const bool prev_bat_avail = battery_available_;
    const int prev_bat_pct = battery_percent_;
    const bool prev_bat_chg = battery_charging_;
    const QString prev_bat_txt = battery_text_;

    const auto bl = finn::sys::read_backlight();
    brightness_available_ = bl.available;
    brightness_level_ = bl.available ? (bl.percent / 100.0) : 0.0;

    const auto bat = finn::sys::read_battery();
    battery_available_ = bat.available;
    battery_percent_ = bat.available ? bat.capacity_percent : 0;
    battery_charging_ = bat.charging;
    battery_text_ = QString::fromStdString(bat.status_text);

    bool dirty = (prev_bl_avail != brightness_available_) ||
                 !qFuzzyCompare(prev_bl + 1.0, brightness_level_ + 1.0) ||
                 (prev_bat_avail != battery_available_) ||
                 (prev_bat_pct != battery_percent_) ||
                 (prev_bat_chg != battery_charging_) ||
                 (prev_bat_txt != battery_text_);

    // Probe real Linux tools asynchronously only when installed (never fake)
    const bool has_nmcli = !QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty();
    if (!has_nmcli) {
        wifi_available_ = false;
        wifi_enabled_ = false;
        wifi_ssid_ = QStringLiteral("Unavailable");
    } else {
        auto* p = new QProcess(this);
        connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, p](int code, QProcess::ExitStatus st) {
                    const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                    p->deleteLater();
                    if (st != QProcess::NormalExit || code != 0) return;
                    wifi_available_ = true;
                    wifi_enabled_ = out.contains(QLatin1String("enabled"), Qt::CaseInsensitive) ||
                                    out.contains(QLatin1String("connected"), Qt::CaseInsensitive);
                    wifi_ssid_ = wifi_enabled_ ? QStringLiteral("Connected") : QStringLiteral("Off");
                    emit statusChanged();
                });
        p->start(QStringLiteral("nmcli"), {QStringLiteral("radio"), QStringLiteral("wifi")});
    }

    const bool has_wpctl = !QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty();
    if (!has_wpctl) {
        audio_available_ = false;
        volume_level_ = 0.0;
        audio_muted_ = false;
    } else {
        auto* p = new QProcess(this);
        connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, p](int code, QProcess::ExitStatus st) {
                    const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                    p->deleteLater();
                    if (st != QProcess::NormalExit || code != 0) return;
                    // Format: "Volume: 0.72" or "Volume: 0.72 [MUTED]"
                    const QStringList parts = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (parts.size() >= 2) {
                        bool ok = false;
                        const double v = parts[1].toDouble(&ok);
                        if (ok) {
                            audio_available_ = true;
                            volume_level_ = std::clamp(v, 0.0, 1.0);
                            audio_muted_ = out.contains(QLatin1String("MUTED"));
                            emit statusChanged();
                        }
                    }
                });
        p->start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
    }

    const bool has_bt = !QStandardPaths::findExecutable(QStringLiteral("bluetoothctl")).isEmpty();
    if (!has_bt) {
        if (bt_available_ || bt_enabled_ || bt_subtitle_ != QLatin1String("Unavailable")) {
            bt_available_ = false;
            bt_enabled_ = false;
            bt_subtitle_ = QStringLiteral("Unavailable");
            dirty = true;
        }
    }

    if (dirty) emit statusChanged();
}

void ShellCorona::loadUsers() {
    user_accounts_.clear();
    const struct passwd* cur = ::getpwuid(::getuid());
    if (cur && cur->pw_name) {
        selected_user_ = QString::fromLocal8Bit(cur->pw_name);
        QString gecos = cur->pw_gecos ? QString::fromLocal8Bit(cur->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed()
                                      : QString();
        if (gecos.isEmpty()) {
            gecos = selected_user_;
            if (!gecos.isEmpty()) gecos[0] = gecos[0].toUpper();
        }
        selected_display_name_ = gecos;
        QVariantMap u;
        u.insert(QStringLiteral("username"), selected_user_);
        u.insert(QStringLiteral("displayName"), selected_display_name_);
        u.insert(QStringLiteral("uid"), static_cast<int>(cur->pw_uid));
        user_accounts_.append(u);
    } else {
        selected_user_ = QStringLiteral("finn");
        selected_display_name_ = QStringLiteral("Finn");
    }
    emit userChanged();
}

void ShellCorona::setupLockWatchers() {
    const QString state_dir = QString::fromLatin1(finn::kCompositorStateDir);
    if (QDir(state_dir).exists()) {
        lock_watcher_.addPath(state_dir);
        consumeLockRequest();
    }
    connect(&lock_watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { consumeLockRequest(); });

#ifdef FINN_HAVE_QT_DBUS
    if (finn::sys::load_merged_settings().lock_on_sleep) {
        QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForSuspend"),
            this, SLOT(lockNow()));
    }
#endif
}

void ShellCorona::consumeLockRequest() {
    const QString req = QString::fromLatin1(finn::kLockRequestPath);
    if (!QFile::exists(req)) return;
    QFile::remove(req);
    lockNow();
}

void ShellCorona::pollOtaStatus() {
    QString exe = QStandardPaths::findExecutable(QStringLiteral("finn-update"));
    if (exe.isEmpty() && QFileInfo::exists(QString::fromLatin1(finn::kFinnUpdateBin))) {
        exe = QString::fromLatin1(finn::kFinnUpdateBin);
    }
    if (exe.isEmpty()) return;

    auto* proc = new QProcess(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, proc](int code, QProcess::ExitStatus st) {
                const QByteArray raw = proc->readAllStandardOutput();
                proc->deleteLater();
                if (st != QProcess::NormalExit || code != 0) return;
                const auto doc = QJsonDocument::fromJson(raw);
                if (!doc.isObject()) return;
                const auto obj = doc.object();
                const QString pending = obj.value(QStringLiteral("pending_slot")).toString().trimmed();
                if (!pending.isEmpty()) {
                    notif_model_->push(
                        QStringLiteral("System Update"), 0, QStringLiteral("settings-app"),
                        QStringLiteral("Restart Required"),
                        QStringLiteral("FinnOS update is staged and ready on reboot."), 7000);
                }
            });
    proc->start(exe, {QStringLiteral("status"), QStringLiteral("--json")});
}

QVariantList ShellCorona::openWindows() const {
    QVariantList out;
    for (const auto& w : open_windows_) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), w.id);
        m.insert(QStringLiteral("title"), w.title);
        m.insert(QStringLiteral("appId"), w.app_id);
        m.insert(QStringLiteral("activated"), w.activated);
        m.insert(QStringLiteral("minimized"), w.minimized);
        out.append(m);
    }
    return out;
}

void ShellCorona::pollOpenWindows() {
    // Consume compositor-triggered shell actions (e.g. 4-finger trackpad swipe up -> home)
    const QString action_path =
        QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/shell-action");
    if (QFile::exists(action_path)) {
        QFile af(action_path);
        QString act;
        if (af.open(QIODevice::ReadOnly | QIODevice::Text)) {
            act = QString::fromUtf8(af.readAll()).trimmed();
            af.close();
        }
        QFile::remove(action_path);
        if (!act.isEmpty() && !locked_) {
            togglePanel(act);
        }
    }

    const QString path =
        QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/windows.tsv");
    QFile f(path);
    std::vector<TrackedWindowItem> next;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const QStringList cols = line.split(QLatin1Char('\t'));
            if (cols.size() < 10) continue;
            TrackedWindowItem item;
            item.id = cols[0];
            item.app_id = cols[1];
            item.title = cols[2];
            item.activated = (cols[7] == QLatin1String("1"));
            item.minimized = (cols[8] == QLatin1String("1"));
            next.push_back(std::move(item));
        }
    }
    bool same = (next.size() == open_windows_.size());
    for (std::size_t i = 0; same && i < next.size(); ++i) {
        const auto& a = next[i];
        const auto& b = open_windows_[i];
        if (a.id != b.id || a.app_id != b.app_id || a.title != b.title ||
            a.activated != b.activated || a.minimized != b.minimized) {
            same = false;
        }
    }
    if (!same) {
        open_windows_ = std::move(next);
        emit windowsChanged();
    }
}

void ShellCorona::sendWindowCommand(const QString& verb, const QString& windowId) {
    if (windowId.isEmpty()) return;
    const QString cmd_path =
        QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/window-cmd");
    QFile f(cmd_path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write((verb + QLatin1Char(' ') + windowId + QLatin1Char('\n')).toUtf8());
        f.close();
    }
}

QVariantList ShellCorona::dockItems() const {
    auto matchWindowForApp = [this](const QString& app_id, const QString& app_name) -> const TrackedWindowItem* {
        const QString id_l = app_id.toLower();
        const QString name_l = app_name.toLower();
        for (const auto& w : open_windows_) {
            const QString wa = w.app_id.toLower();
            const QString wt = w.title.toLower();
            if ((!wa.isEmpty() && (wa == id_l || wa == QLatin1String("finn-") + id_l || wa.contains(id_l))) ||
                (!wt.isEmpty() && (wt == name_l || wt.startsWith(name_l) || wt.contains(id_l)))) {
                return &w;
            }
        }
        return nullptr;
    };

    QVariantList out;
    QStringList matched_window_ids;
    const QVariantList pinned = app_model_->pinnedApps();
    for (const QVariant& v : pinned) {
        QVariantMap m = v.toMap();
        const QString app_id = m.value(QStringLiteral("appId")).toString();
        const QString app_name = m.value(QStringLiteral("name")).toString();
        m.insert(QStringLiteral("pinned"), true);
        m.insert(QStringLiteral("separatorBefore"), false);
        if (const auto* w = matchWindowForApp(app_id, app_name)) {
            matched_window_ids.append(w->id);
            m.insert(QStringLiteral("running"), true);
            m.insert(QStringLiteral("active"), w->activated && !w->minimized);
            m.insert(QStringLiteral("minimized"), w->minimized);
            m.insert(QStringLiteral("windowId"), w->id);
        } else {
            m.insert(QStringLiteral("running"), false);
            m.insert(QStringLiteral("active"), false);
            m.insert(QStringLiteral("minimized"), false);
            m.insert(QStringLiteral("windowId"), QString());
        }
        out.append(m);
    }

    // Append any open windows whose app is NOT pinned on the dock (e.g. Finn Monitor, 3rd-party apps)
    const QVariantList all_apps = app_model_->appsInCategory(QString());
    bool added_unpinned = false;
    for (const auto& w : open_windows_) {
        if (matched_window_ids.contains(w.id)) continue;
        QString resolved_id = w.app_id;
        QString resolved_name = w.title.isEmpty() ? w.app_id : w.title;
        QString resolved_icon = QStringLiteral("native-apps");
        const QString wa = w.app_id.toLower();
        const QString wt = w.title.toLower();
        for (const QVariant& av : all_apps) {
            const QVariantMap am = av.toMap();
            const QString aid = am.value(QStringLiteral("appId")).toString().toLower();
            const QString aname = am.value(QStringLiteral("name")).toString().toLower();
            if ((!wa.isEmpty() && (wa == aid || wa == QLatin1String("finn-") + aid || wa.contains(aid))) ||
                (!wt.isEmpty() && (wt == aname || wt.startsWith(aname) || wt.contains(aid)))) {
                resolved_id = am.value(QStringLiteral("appId")).toString();
                resolved_name = am.value(QStringLiteral("name")).toString();
                resolved_icon = am.value(QStringLiteral("icon")).toString();
                break;
            }
        }
        QVariantMap item;
        item.insert(QStringLiteral("appId"), resolved_id);
        item.insert(QStringLiteral("name"), resolved_name);
        item.insert(QStringLiteral("icon"), resolved_icon);
        item.insert(QStringLiteral("pinned"), false);
        item.insert(QStringLiteral("separatorBefore"), !added_unpinned);
        item.insert(QStringLiteral("running"), true);
        item.insert(QStringLiteral("active"), w.activated && !w.minimized);
        item.insert(QStringLiteral("minimized"), w.minimized);
        item.insert(QStringLiteral("windowId"), w.id);
        added_unpinned = true;
        out.append(item);
    }
    return out;
}

void ShellCorona::handleDockAppClick(const QString& appId, const QString& windowId) {
    if (!windowId.isEmpty()) {
        closePanels();
        sendWindowCommand(QStringLiteral("toggle"), windowId);
        return;
    }
    launchApp(appId);
}

void ShellCorona::togglePanel(const QString& panelName) {
    const QString cleaned = panelName.trimmed().toLower();
    if (active_panel_ == cleaned) {
        active_panel_.clear();
    } else {
        active_panel_ = cleaned;
    }
    emit panelChanged();
}

void ShellCorona::openPanel(const QString& panelName) {
    active_panel_ = panelName.trimmed().toLower();
    emit panelChanged();
}

void ShellCorona::closePanels() {
    if (active_panel_.isEmpty()) return;
    active_panel_.clear();
    emit panelChanged();
}

void ShellCorona::launchApp(const QString& appId) {
    closePanels();
    finn::SessionClient client;
    std::string err;
    if (client.launch(appId.toStdString(), &err)) {
        finn::sys::record_recent_app(appId.toStdString());
        return;
    }
    // Development host fallback: try sibling executable next to finn-shell
    const QString sibling = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/../../apps/") + appId +
                            QStringLiteral("/finn-") + appId;
    if (QFileInfo::exists(sibling)) {
        QProcess::startDetached(sibling, {});
        finn::sys::record_recent_app(appId.toStdString());
        return;
    }
    notif_model_->push(QStringLiteral("FinnOS"), 0, QStringLiteral("info"),
                       QStringLiteral("Could not launch %1").arg(appId),
                       QString::fromStdString(err.empty() ? "Session broker unavailable" : err),
                       4500);
}

void ShellCorona::openTarget(const QString& target) {
    closePanels();
    finn::SessionClient client;
    std::string err;
    if (!client.open_target(target.toStdString(), &err)) {
        notif_model_->push(QStringLiteral("FinnOS"), 0, QStringLiteral("info"),
                           QStringLiteral("Could not open target"),
                           QString::fromStdString(err), 4500);
    }
}

void ShellCorona::openSettings(const QString& pageId) {
    Q_UNUSED(pageId);
    launchApp(QStringLiteral("settings"));
}

void ShellCorona::activateWindow(const QString& windowId) {
    closePanels();
    sendWindowCommand(QStringLiteral("focus"), windowId);
}

void ShellCorona::closeWindow(const QString& windowId) {
    sendWindowCommand(QStringLiteral("close"), windowId);
}

void ShellCorona::toggleWifi() {
    if (!wifi_available_) return;
    const QString next = wifi_enabled_ ? QStringLiteral("off") : QStringLiteral("on");
    QProcess::startDetached(QStringLiteral("nmcli"),
                            {QStringLiteral("radio"), QStringLiteral("wifi"), next});
    QTimer::singleShot(500, this, &ShellCorona::refreshHardwareStatus);
}

void ShellCorona::toggleBluetooth() {
    if (!bt_available_) return;
    const QString next = bt_enabled_ ? QStringLiteral("off") : QStringLiteral("on");
    QProcess::startDetached(QStringLiteral("bluetoothctl"),
                            {QStringLiteral("power"), next});
    QTimer::singleShot(500, this, &ShellCorona::refreshHardwareStatus);
}

void ShellCorona::setVolume(double level01) {
    if (!audio_available_) return;
    volume_level_ = std::clamp(level01, 0.0, 1.0);
    const QString pct = QStringLiteral("%1%").arg(static_cast<int>(std::lround(volume_level_ * 100.0)));
    QProcess::startDetached(QStringLiteral("wpctl"),
                            {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), pct});
    emit statusChanged();
}

void ShellCorona::setBrightness(double level01) {
    if (!brightness_available_) return;
    brightness_level_ = std::clamp(level01, 0.05, 1.0);
    std::string err;
    finn::sys::write_backlight_percent(static_cast<int>(std::lround(brightness_level_ * 100.0)),
                                       "/sys/class/backlight", &err);
    emit statusChanged();
}

void ShellCorona::mediaPrev() {
    if (!media_available_) return;
    QProcess::startDetached(QStringLiteral("playerctl"), {QStringLiteral("previous")});
}

void ShellCorona::mediaPlayPause() {
    if (!media_available_) return;
    QProcess::startDetached(QStringLiteral("playerctl"), {QStringLiteral("play-pause")});
}

void ShellCorona::mediaNext() {
    if (!media_available_) return;
    QProcess::startDetached(QStringLiteral("playerctl"), {QStringLiteral("next")});
}

void ShellCorona::lockNow() {
    closePanels();
    locked_ = true;
    unlock_error_.clear();
    emit lockStateChanged();
}

void ShellCorona::selectUser(const QString& username) {
    for (const QVariant& v : user_accounts_) {
        const auto m = v.toMap();
        if (m.value(QStringLiteral("username")).toString() == username) {
            selected_user_ = username;
            selected_display_name_ = m.value(QStringLiteral("displayName")).toString();
            emit userChanged();
            return;
        }
    }
}

void ShellCorona::attemptUnlock(const QString& password) {
    if (verifying_unlock_) return;
    verifying_unlock_ = true;
    unlock_error_.clear();
    emit lockStateChanged();

    if (password.isEmpty()) {
        startPasswordlessCheck(selected_user_);
    } else {
        startSuAuth(selected_user_, password);
    }
}

void ShellCorona::startPasswordlessCheck(const QString& user) {
    const QString passwd_bin = QStandardPaths::findExecutable(QStringLiteral("passwd"));
    if (passwd_bin.isEmpty()) {
        finishUnlockFailure(QStringLiteral("Password required"));
        return;
    }
    auto* p = new QProcess(this);
    auth_proc_ = p;
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p](int code, QProcess::ExitStatus st) {
                const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                p->deleteLater();
                if (auth_proc_ == p) auth_proc_ = nullptr;
                const QStringList parts = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (st == QProcess::NormalExit && code == 0 && parts.size() >= 2 &&
                    parts[1] == QLatin1String("NP")) {
                    finishUnlockSuccess();
                } else {
                    finishUnlockFailure(QStringLiteral("Enter your password"));
                }
            });
    p->start(passwd_bin, {QStringLiteral("-S"), user});
}

void ShellCorona::startSuAuth(const QString& user, const QString& password) {
    const QString su_bin = QStandardPaths::findExecutable(QStringLiteral("su"));
    if (su_bin.isEmpty()) {
        finishUnlockFailure(QStringLiteral("Authentication service unavailable"));
        return;
    }
    auto* p = new QProcess(this);
    auth_proc_ = p;
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p](int code, QProcess::ExitStatus st) {
                p->deleteLater();
                if (auth_proc_ == p) auth_proc_ = nullptr;
                if (st == QProcess::NormalExit && code == 0) {
                    finishUnlockSuccess();
                } else {
                    finishUnlockFailure(QStringLiteral("Incorrect password"));
                }
            });
    p->start(su_bin, {user, QStringLiteral("-c"), QStringLiteral("true")});
    p->write((password + QLatin1Char('\n')).toUtf8());
    p->closeWriteChannel();
}

void ShellCorona::finishUnlockSuccess() {
    verifying_unlock_ = false;
    locked_ = false;
    unlock_error_.clear();
    emit lockStateChanged();
}

void ShellCorona::finishUnlockFailure(const QString& reason) {
    verifying_unlock_ = false;
    unlock_error_ = reason;
    emit lockStateChanged();
    emit unlockFailedShake();
}

void ShellCorona::reboot() {
    finn::SessionClient client;
    std::string err;
    (void)client.power_action("reboot", &err);
}

void ShellCorona::powerOff() {
    finn::SessionClient client;
    std::string err;
    (void)client.power_action("poweroff", &err);
}

}  // namespace finn::shell
