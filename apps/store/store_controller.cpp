// Rewritten C++20 Qt6 StoreController for Finn Store.

#include "store_controller.hpp"

#include "common/paths.hpp"
#include "compat/android_runner.hpp"
#include "compat/appimage_runner.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/finnapp_installer.hpp"
#include "compat/flatpak_installer.hpp"
#include "compat/macos_runner.hpp"
#include "compat/ohos_runner.hpp"
#include "compat/wine_runner.hpp"
#include "providers/curated_catalog.hpp"
#include "providers/flathub_provider.hpp"
#include "providers/winget_provider.hpp"
#include "session/app_registry.hpp"
#include "session/client.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace fs = std::filesystem;

namespace finn::apps {
namespace {

using finn::store::StoreAppItem;
using finn::store::StoreEcosystem;

QString qstr(const std::string& s) { return QString::fromStdString(s); }

StoreEcosystem map_app_source(const finn::App& app) {
    switch (app.source) {
        case finn::AppSource::kNative:  return StoreEcosystem::Native;
        case finn::AppSource::kFlatpak: return StoreEcosystem::Flatpak;
        case finn::AppSource::kAndroid: return StoreEcosystem::Android;
        case finn::AppSource::kWindows:
        case finn::AppSource::kWinget:  return StoreEcosystem::Windows;
        case finn::AppSource::kMacos:   return StoreEcosystem::Macos;
        case finn::AppSource::kOhos:    return StoreEcosystem::Ohos;
        case finn::AppSource::kDesktop: {
            const std::string& cmd = app.command;
            if (cmd.find("finn-win") != std::string::npos ||
                cmd.find("wine") != std::string::npos ||
                cmd.find(".exe") != std::string::npos) {
                return StoreEcosystem::Windows;
            }
            if (cmd.find("finn-android") != std::string::npos) {
                return StoreEcosystem::Android;
            }
            return StoreEcosystem::Flatpak;
        }
        default:                        return StoreEcosystem::Native;
    }
}

bool ends_with_ci(const QString& s, const char* suffix) {
    return s.endsWith(QLatin1String(suffix), Qt::CaseInsensitive);
}

QString accent_for_ecosystem(StoreEcosystem eco) {
    switch (eco) {
        case StoreEcosystem::Native:  return QStringLiteral("#3b82f6");
        case StoreEcosystem::Flatpak: return QStringLiteral("#0ea5e9");
        case StoreEcosystem::Android: return QStringLiteral("#10b981");
        case StoreEcosystem::Windows: return QStringLiteral("#6366f1");
        case StoreEcosystem::Macos:   return QStringLiteral("#8b5cf6");
        case StoreEcosystem::Ohos:    return QStringLiteral("#f59e0b");
    }
    return QStringLiteral("#3b82f6");
}

QString format_speed_bps(double bps) {
    if (bps >= 1024.0 * 1024.0) {
        return QStringLiteral("%1 MB/s").arg(bps / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bps >= 1024.0) {
        return QStringLiteral("%1 KB/s").arg(bps / 1024.0, 0, 'f', 0);
    }
    return QStringLiteral("%1 B/s").arg(qRound(bps));
}

QString mime_for_extension(const QString& rawExt) {
    const QString e = rawExt.trimmed().toLower().remove(QLatin1Char('.'));
    if (e == QLatin1String("7z")) return QStringLiteral("application/x-7z-compressed");
    if (e == QLatin1String("zip")) return QStringLiteral("application/zip");
    if (e == QLatin1String("rar")) return QStringLiteral("application/vnd.rar");
    if (e == QLatin1String("tar")) return QStringLiteral("application/x-tar");
    if (e == QLatin1String("gz") || e == QLatin1String("gzip")) return QStringLiteral("application/gzip");
    if (e == QLatin1String("xz")) return QStringLiteral("application/x-xz");
    if (e == QLatin1String("bz2")) return QStringLiteral("application/x-bzip2");
    if (e == QLatin1String("iso")) return QStringLiteral("application/x-cd-image");
    if (e == QLatin1String("pdf")) return QStringLiteral("application/pdf");
    if (e == QLatin1String("epub")) return QStringLiteral("application/epub+zip");
    if (e == QLatin1String("cbz")) return QStringLiteral("application/vnd.comicbook+zip");
    if (e == QLatin1String("cbr")) return QStringLiteral("application/vnd.comicbook-rar");
    if (e == QLatin1String("kdbx")) return QStringLiteral("application/x-keepass2");
    if (e == QLatin1String("txt") || e == QLatin1String("log") || e == QLatin1String("ini") || e == QLatin1String("cfg")) return QStringLiteral("text/plain");
    if (e == QLatin1String("md")) return QStringLiteral("text/markdown");
    if (e == QLatin1String("json")) return QStringLiteral("application/json");
    if (e == QLatin1String("xml")) return QStringLiteral("application/xml");
    if (e == QLatin1String("html") || e == QLatin1String("htm")) return QStringLiteral("text/html");
    if (e == QLatin1String("c") || e == QLatin1String("cpp") || e == QLatin1String("h") || e == QLatin1String("hpp")) return QStringLiteral("text/x-c++src");
    if (e == QLatin1String("py")) return QStringLiteral("text/x-python");
    if (e == QLatin1String("js")) return QStringLiteral("application/javascript");
    if (e == QLatin1String("mp4")) return QStringLiteral("video/mp4");
    if (e == QLatin1String("mkv")) return QStringLiteral("video/x-matroska");
    if (e == QLatin1String("mp3")) return QStringLiteral("audio/mpeg");
    if (e == QLatin1String("flac")) return QStringLiteral("audio/flac");
    if (e == QLatin1String("png")) return QStringLiteral("image/png");
    if (e == QLatin1String("jpg") || e == QLatin1String("jpeg")) return QStringLiteral("image/jpeg");
    return {};
}

QSet<QString> snapshot_wine_exes(const QString& prefix) {
    QSet<QString> out;
    std::error_code ec;
    for (const char* sub : {"drive_c/Program Files", "drive_c/Program Files (x86)"}) {
        const fs::path root = fs::path(prefix.toStdString()) / sub;
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const QString p = QString::fromStdString(entry.path().string());
            if (p.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
                out.insert(p);
            }
        }
    }
    return out;
}

QString find_best_windows_exe(const QString& prefix, const QSet<QString>& before, const QString& appName) {
    const QSet<QString> after = snapshot_wine_exes(prefix);
    QStringList candidates;
    for (const QString& p : after) {
        if (!before.contains(p)) candidates.append(p);
    }
    // If app was already installed (reinstall), search matching folder in after
    const QString needle = appName.toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
    if (candidates.isEmpty()) {
        for (const QString& p : after) {
            QString low = p.toLower();
            low.remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
            if (!needle.isEmpty() && low.contains(needle)) candidates.append(p);
        }
    }
    QString best;
    int best_score = -1000;
    for (const QString& p : candidates) {
        const QString base = QFileInfo(p).fileName().toLower();
        if (base.contains(QLatin1String("unins")) || base.contains(QLatin1String("uninstall")) ||
            base.contains(QLatin1String("setup")) || base.contains(QLatin1String("update")) ||
            base.contains(QLatin1String("crash")) || base.contains(QLatin1String("helper"))) {
            continue;
        }
        int score = 0;
        const auto pe = finn::compat::inspect_pe_file(p.toStdString());
        if (pe.valid && pe.is_gui) score += 50;
        if (base.contains(QLatin1String("fm.exe"))) score += 20;  // e.g. 7zFM.exe
        if (!needle.isEmpty() && base.contains(needle.left(4))) score += 25;
        if (score > best_score) {
            best_score = score;
            best = p;
        }
    }
    return best;
}

void register_windows_app_and_mimes(const QString& appId, const QString& wgid,
                                    const QString& name, const QString& exePath,
                                    const std::vector<std::string>& fileExts) {
    if (exePath.isEmpty()) return;
    const QString home = QDir::homePath();
    const QString conf = home + QStringLiteral("/") + QString::fromLatin1(finn::kUserWindowsConfRel);
    finn::compat::WindowsAppEntry entry;
    entry.id = appId.toStdString();
    entry.name = name.toStdString();
    entry.exe_path = exePath.toStdString();
    entry.icon = "windows";
    finn::compat::save_windows_app(conf.toStdString(), entry, nullptr);

    // Register Freedesktop .desktop entry with MimeType associations for file extensions
    QStringList mimes;
    for (const std::string& ext : fileExts) {
        const QString m = mime_for_extension(qstr(ext));
        if (!m.isEmpty() && !mimes.contains(m)) mimes.append(m);
    }
    QString slug = wgid.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    const QString apps_dir = home + QStringLiteral("/.local/share/applications");
    QDir().mkpath(apps_dir);
    QFile df(apps_dir + QStringLiteral("/finn-win-") + slug + QStringLiteral(".desktop"));
    if (df.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QByteArray content;
        content.append("[Desktop Entry]\nType=Application\n");
        content.append("Name=" + name.toUtf8() + "\n");
        content.append("Exec=/usr/local/bin/finn-win \"" + exePath.toUtf8() + "\" %f\n");
        content.append("Icon=windows\n");
        content.append("Categories=Utility;\n");
        if (!mimes.isEmpty()) {
            content.append("MimeType=" + mimes.join(QLatin1Char(';')).toUtf8() + ";\n");
        }
        df.write(content);
        df.close();
        QProcess::startDetached(QStringLiteral("update-desktop-database"), {apps_dir});
    }
}

}  // namespace

StoreController::StoreController(QObject* parent)
    : QAbstractListModel(parent),
      nam_(new QNetworkAccessManager(this)),
      search_debounce_(new QTimer(this)) {
    search_debounce_->setSingleShot(true);
    search_debounce_->setInterval(350);
    connect(search_debounce_, &QTimer::timeout, this, [this]() {
        const QString q = search_query_.trimmed();
        if (q.size() >= 2) searchOnline(q);
    });

    curated_ = finn::store::build_curated_catalog();
    probeRuntimes();
    rebuildCatalog();
    fetchFlathubCollection(flathub_collection_, 1, false);
    fetchAndroidCollection(android_collection_);
    fetchWingetCollection(winget_collection_);
}

int StoreController::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(filtered_.size());
}

QVariant StoreController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(filtered_.size())) {
        return {};
    }
    const auto& a = filtered_[static_cast<std::size_t>(index.row())];
    const QString id = qstr(a.id);
    const auto task = tasks_.value(id);

    switch (role) {
        case IdRole:             return id;
        case NameRole:           return qstr(a.name);
        case SubtitleRole:       return qstr(a.subtitle);
        case DescriptionRole:    return qstr(a.description);
        case DeveloperRole:      return qstr(a.developer);
        case VersionRole:        return qstr(a.version);
        case CategoryRole:       return qstr(a.category);
        case EcosystemIdRole:    return QString::fromLatin1(finn::store::ecosystem_id(a.ecosystem));
        case EcosystemLabelRole: return QString::fromLatin1(finn::store::ecosystem_label(a.ecosystem));
        case IconRole:           return qstr(a.icon);
        case RatingRole:         return a.rating;
        case SizeLabelRole:      return qstr(a.size_label);
        case VerifiedRole:       return a.verified;
        case VerifiedSourceRole: return qstr(a.verified_source);
        case InstalledRole:      return a.installed;
        case UpdateAvailableRole:return a.update_available;
        case EditorialTagRole:   return qstr(a.editorial_tag);
        case AccentHexRole:      return qstr(a.accent_hex);
        case InstallRefRole:     return qstr(a.install_ref);
        case ProgressRole:       return task.progress;
        case TaskStateRole:      return task.state;
        default:                 return {};
    }
}

QHash<int, QByteArray> StoreController::roleNames() const {
    return {
        {IdRole, "appId"},
        {NameRole, "name"},
        {SubtitleRole, "subtitle"},
        {DescriptionRole, "description"},
        {DeveloperRole, "developer"},
        {VersionRole, "version"},
        {CategoryRole, "category"},
        {EcosystemIdRole, "ecosystemId"},
        {EcosystemLabelRole, "ecosystemLabel"},
        {IconRole, "iconName"},
        {RatingRole, "rating"},
        {SizeLabelRole, "sizeLabel"},
        {VerifiedRole, "verified"},
        {VerifiedSourceRole, "verifiedSource"},
        {InstalledRole, "installed"},
        {UpdateAvailableRole, "updateAvailable"},
        {EditorialTagRole, "editorialTag"},
        {AccentHexRole, "accentHex"},
        {InstallRefRole, "installRef"},
        {ProgressRole, "taskProgress"},
        {TaskStateRole, "taskState"},
    };
}

void StoreController::setActiveTab(const QString& tab) {
    const QString clean = tab.trimmed().toLower();
    if (clean.isEmpty() || clean == active_tab_) return;
    active_tab_ = clean;
    emit filterChanged();
    rebuildFiltered();
}

void StoreController::setSearchQuery(const QString& q) {
    if (q == search_query_) return;
    search_query_ = q;
    emit filterChanged();
    rebuildFiltered();

    const QString needle = search_query_.trimmed().toLower();
    if (needle.size() >= 2) {
        // Only query online cross-OS stores if no Native FinnOS app matches (or on a non-native tab).
        bool has_native_match = false;
        if (active_tab_ == QLatin1String("discover") || active_tab_ == QLatin1String("native")) {
            for (const auto& a : all_items_) {
                if (a.ecosystem != StoreEcosystem::Native) continue;
                if (qstr(a.name).toLower().contains(needle) ||
                    qstr(a.subtitle).toLower().contains(needle) ||
                    qstr(a.category).toLower().contains(needle)) {
                    has_native_match = true;
                    break;
                }
            }
        }
        if (!has_native_match && active_tab_ != QLatin1String("native")) {
            search_debounce_->start();
        } else {
            search_debounce_->stop();
        }
    } else {
        search_debounce_->stop();
    }
}

void StoreController::setFlathubCollection(const QString& col) {
    const QString clean = col.trimmed();
    if (clean.isEmpty() || clean == flathub_collection_) return;
    flathub_collection_ = clean;
    emit flathubBrowseChanged();
    fetchFlathubCollection(flathub_collection_, 1, false);
}

void StoreController::setAndroidCollection(const QString& col) {
    const QString clean = col.trimmed();
    if (clean.isEmpty() || clean == android_collection_) return;
    android_collection_ = clean;
    emit flathubBrowseChanged();
    fetchAndroidCollection(android_collection_);
}

void StoreController::setWingetCollection(const QString& col) {
    const QString clean = col.trimmed();
    if (clean.isEmpty() || clean == winget_collection_) return;
    winget_collection_ = clean;
    emit flathubBrowseChanged();
    fetchWingetCollection(winget_collection_);
}

void StoreController::loadMoreFlathub() {
    if (busy_ || !flathubHasMore()) return;
    fetchFlathubCollection(flathub_collection_, flathub_page_ + 1, true);
}

void StoreController::reload() {
    probeRuntimes();
    rebuildCatalog();
    fetchFlathubCollection(flathub_collection_, 1, false);
    fetchAndroidCollection(android_collection_);
    fetchWingetCollection(winget_collection_);
}

void StoreController::probeRuntimes() {
    const bool has_flatpak = finn::compat::executable_exists("flatpak");
    const auto android = finn::compat::probe_android_runtime();
    const auto wine = finn::compat::probe_wine_runtime();
    const auto macos = finn::compat::probe_macos_runtime();
    const auto ohos = finn::compat::probe_ohos_runtime();

    QVariantMap m;
    m.insert(QStringLiteral("flatpakReady"), has_flatpak);
    m.insert(QStringLiteral("androidReady"), android.ready);
    m.insert(QStringLiteral("androidSummary"), qstr(android.summary));
    m.insert(QStringLiteral("windowsReady"), wine.ready);
    m.insert(QStringLiteral("windowsSummary"), qstr(wine.summary));
    m.insert(QStringLiteral("macosReady"), macos.gui_ready);
    m.insert(QStringLiteral("ohosReady"), ohos.ready);
    m.insert(QStringLiteral("auroraAuthenticated"), aurora_session_.authenticated);
    runtime_status_ = std::move(m);
    emit runtimeChanged();
}

void StoreController::rebuildCatalog() {
    const auto reg = finn::AppRegistry::load_all(finn::kCataloguePath, finn::kInstalledPath);

    std::vector<StoreAppItem> merged;
    std::unordered_map<std::string, std::size_t> by_id;

    // Seed with curated items; for Flathub when live results are loaded, lead with
    // live Flathub hits (which have official icons). For Android & Windows, keep
    // curated picks when browsing "popular" so well-known apps are always present.
    const bool have_live_flathub = !remote_flathub_.empty();
    const bool have_live_android = !remote_android_.empty();
    const bool have_live_winget = !remote_winget_.empty();
    for (const auto& c : curated_) {
        if (have_live_flathub && c.ecosystem == StoreEcosystem::Flatpak) continue;
        if (have_live_android && c.ecosystem == StoreEcosystem::Android &&
            android_collection_ != QLatin1String("popular")) continue;
        if (have_live_winget && c.ecosystem == StoreEcosystem::Windows &&
            winget_collection_ != QLatin1String("popular")) continue;
        by_id[c.id] = merged.size();
        merged.push_back(c);
    }

    auto upsert_remote = [&](const std::vector<StoreAppItem>& src) {
        for (const auto& r : src) {
            const auto it = by_id.find(r.id);
            if (it == by_id.end()) {
                by_id[r.id] = merged.size();
                merged.push_back(r);
            } else if (r.icon.rfind("https://", 0) == 0) {
                merged[it->second].icon = r.icon;
                if (!r.developer.empty()) merged[it->second].developer = r.developer;
                if (!r.subtitle.empty()) merged[it->second].subtitle = r.subtitle;
            }
        }
    };
    upsert_remote(remote_flathub_);
    upsert_remote(remote_android_);
    upsert_remote(remote_winget_);

    // Reconcile with real installed apps from AppRegistry
    for (const auto& inst : reg.apps()) {
        const StoreEcosystem eco = map_app_source(inst);
        bool matched = false;
        for (auto& item : merged) {
            const bool id_match = (item.id == inst.id || item.session_app_id == inst.id ||
                                   ("flatpak:" + item.install_ref) == inst.id ||
                                   item.install_ref == inst.command);
            if (id_match) {
                item.installed = inst.installed;
                item.session_app_id = inst.id;
                if (!inst.icon.empty() && item.icon.find('.') != std::string::npos &&
                    inst.icon[0] == '/') {
                    item.icon = inst.icon;
                }
                matched = true;
                break;
            }
        }
        if (!matched && inst.installed) {
            StoreAppItem item;
            item.id = inst.id;
            item.session_app_id = inst.id;
            item.install_ref = inst.command.empty() ? inst.id : inst.command;
            item.name = inst.name;
            item.subtitle = inst.note.empty() ? std::string(finn::store::ecosystem_label(eco))
                                              : inst.note;
            item.description = item.subtitle;
            item.developer = (eco == StoreEcosystem::Native) ? "FinnOS" : "Installed App";
            item.category = inst.category.empty() ? "Utilities" : inst.category;
            item.ecosystem = eco;
            item.icon = inst.icon.empty() ? "app-grid.svg" : inst.icon;
            item.installed = true;
            item.verified = (eco == StoreEcosystem::Native);
            item.verified_source = (eco == StoreEcosystem::Native) ? "FinnOS Core" : "";
            item.accent_hex = accent_for_ecosystem(eco).toStdString();
            by_id[item.id] = merged.size();
            merged.push_back(std::move(item));
        }
    }

    installed_count_ = 0;
    updates_count_ = 0;
    for (auto& item : merged) {
        if (item.ecosystem == StoreEcosystem::Flatpak &&
            updatable_flatpaks_.contains(qstr(item.install_ref))) {
            item.update_available = true;
        }
        if (item.installed) ++installed_count_;
        if (item.update_available) ++updates_count_;
    }

    all_items_ = std::move(merged);
    rebuildDiscoverShelves();
    rebuildFiltered();
    refreshSelectedDetail();
    emit catalogChanged();
}

void StoreController::rebuildDiscoverShelves() {
    QVariantList heroes;
    QVariantList native_list, flatpak_list, android_list, windows_list;

    for (const auto& a : all_items_) {
        const QVariantMap m = itemToMap(a);
        if (a.featured_hero) heroes.append(m);
        if (a.ecosystem == StoreEcosystem::Flatpak) {
            if (flatpak_list.size() < 6) flatpak_list.append(m);
            continue;
        }
        if (a.ecosystem == StoreEcosystem::Android) {
            if (android_list.size() < 6) android_list.append(m);
            continue;
        }
        if (a.ecosystem == StoreEcosystem::Windows) {
            if (windows_list.size() < 6) windows_list.append(m);
            continue;
        }
        if (!a.curated && !a.installed) continue;
        if (a.ecosystem == StoreEcosystem::Native && native_list.size() < 6) {
            native_list.append(m);
        }
    }

    hero_items_ = std::move(heroes);
    QVariantList shelves;
    auto push_shelf = [&](const QString& title, const QString& subtitle,
                          const QString& tabId, const QVariantList& items) {
        if (items.isEmpty()) return;
        QVariantMap s;
        s.insert(QStringLiteral("title"), title);
        s.insert(QStringLiteral("subtitle"), subtitle);
        s.insert(QStringLiteral("tabId"), tabId);
        s.insert(QStringLiteral("items"), items);
        shelves.append(s);
    };
    push_shelf(QStringLiteral("Native FinnOS Essentials"),
               QStringLiteral("Built for Wayland with FinnUI — fast, clean, zero overhead"),
               QStringLiteral("native"), native_list);
    push_shelf(QStringLiteral("Linux & Flathub Picks"),
               QStringLiteral("Sandboxed desktop apps from Flathub with verified publishers"),
               QStringLiteral("flatpak"), flatpak_list);
    push_shelf(QStringLiteral("Android Apps (Aurora / Play)"),
               QStringLiteral("Anonymous Play Store delivery installed into finn-android"),
               QStringLiteral("android"), android_list);
    push_shelf(QStringLiteral("Windows Apps (winget / finn-win)"),
               QStringLiteral("SHA-256 verified Windows installers via winget-pkgs"),
               QStringLiteral("windows"), windows_list);
    discover_shelves_ = std::move(shelves);
}

void StoreController::rebuildFiltered() {
    const QString needle = search_query_.trimmed().toLower();
    auto matches_query = [&](const StoreAppItem& a) -> bool {
        if (needle.isEmpty()) return true;
        return qstr(a.name).toLower().contains(needle) ||
               qstr(a.subtitle).toLower().contains(needle) ||
               qstr(a.developer).toLower().contains(needle) ||
               qstr(a.category).toLower().contains(needle) ||
               qstr(a.install_ref).toLower().contains(needle);
    };

    std::vector<StoreAppItem> next;
    if (!needle.isEmpty() && active_tab_ == QLatin1String("discover")) {
        // Global search: lead with Native FinnOS matches, followed by Linux,
        // Windows, Android, macOS, and OpenHarmony results (all with accurate tags).
        std::vector<StoreAppItem> native_hits, linux_hits, win_hits, android_hits, other_hits;
        for (const auto& a : all_items_) {
            if (!matches_query(a)) continue;
            switch (a.ecosystem) {
                case StoreEcosystem::Native:  native_hits.push_back(a); break;
                case StoreEcosystem::Flatpak: linux_hits.push_back(a); break;
                case StoreEcosystem::Windows: win_hits.push_back(a); break;
                case StoreEcosystem::Android: android_hits.push_back(a); break;
                default:                      other_hits.push_back(a); break;
            }
        }
        next.reserve(native_hits.size() + linux_hits.size() + win_hits.size() +
                     android_hits.size() + other_hits.size());
        next.insert(next.end(), native_hits.begin(), native_hits.end());
        next.insert(next.end(), linux_hits.begin(), linux_hits.end());
        next.insert(next.end(), win_hits.begin(), win_hits.end());
        next.insert(next.end(), android_hits.begin(), android_hits.end());
        next.insert(next.end(), other_hits.begin(), other_hits.end());
    } else {
        for (const auto& a : all_items_) {
            bool tab_ok = true;
            if (active_tab_ == QLatin1String("native")) {
                tab_ok = (a.ecosystem == StoreEcosystem::Native);
            } else if (active_tab_ == QLatin1String("flatpak")) {
                tab_ok = (a.ecosystem == StoreEcosystem::Flatpak);
            } else if (active_tab_ == QLatin1String("android")) {
                tab_ok = (a.ecosystem == StoreEcosystem::Android);
            } else if (active_tab_ == QLatin1String("windows")) {
                tab_ok = (a.ecosystem == StoreEcosystem::Windows);
            } else if (active_tab_ == QLatin1String("updates")) {
                tab_ok = a.installed || a.update_available;
            } else if (active_tab_ == QLatin1String("installed")) {
                tab_ok = a.installed;
            }
            if (!tab_ok || !matches_query(a)) continue;
            next.push_back(a);
        }
    }

    beginResetModel();
    filtered_ = std::move(next);
    endResetModel();
    emit catalogChanged();
}

QVariantMap StoreController::itemToMap(const StoreAppItem& a) const {
    const QString id = qstr(a.id);
    const auto task = tasks_.value(id);
    QVariantMap m;
    m.insert(QStringLiteral("appId"), id);
    m.insert(QStringLiteral("name"), qstr(a.name));
    m.insert(QStringLiteral("subtitle"), qstr(a.subtitle));
    m.insert(QStringLiteral("description"), qstr(a.description));
    m.insert(QStringLiteral("developer"), qstr(a.developer));
    m.insert(QStringLiteral("version"), qstr(a.version));
    m.insert(QStringLiteral("category"), qstr(a.category));
    m.insert(QStringLiteral("ecosystemId"), QString::fromLatin1(finn::store::ecosystem_id(a.ecosystem)));
    m.insert(QStringLiteral("ecosystemLabel"), QString::fromLatin1(finn::store::ecosystem_label(a.ecosystem)));
    m.insert(QStringLiteral("iconName"), qstr(a.icon));
    m.insert(QStringLiteral("rating"), a.rating);
    m.insert(QStringLiteral("reviewCount"), a.review_count);
    m.insert(QStringLiteral("sizeLabel"), qstr(a.size_label));
    m.insert(QStringLiteral("verified"), a.verified);
    m.insert(QStringLiteral("verifiedSource"), qstr(a.verified_source));
    m.insert(QStringLiteral("installed"), a.installed);
    m.insert(QStringLiteral("updateAvailable"), a.update_available);
    m.insert(QStringLiteral("editorialTag"), qstr(a.editorial_tag));
    m.insert(QStringLiteral("accentHex"), qstr(a.accent_hex));
    m.insert(QStringLiteral("installRef"), qstr(a.install_ref));
    m.insert(QStringLiteral("taskProgress"), task.progress);
    m.insert(QStringLiteral("taskState"), task.state);

    QStringList perms;
    for (const auto& p : a.permissions) perms.append(qstr(p));
    m.insert(QStringLiteral("permissions"), perms);

    QStringList shots;
    for (const auto& s : a.screenshots) shots.append(qstr(s));
    m.insert(QStringLiteral("screenshots"), shots);
    return m;
}

const StoreAppItem* StoreController::findItem(const QString& id) const {
    const std::string key = id.toStdString();
    for (const auto& a : all_items_) {
        if (a.id == key) return &a;
    }
    return nullptr;
}

void StoreController::refreshSelectedDetail() {
    if (selected_app_id_.isEmpty()) return;
    if (const auto* item = findItem(selected_app_id_)) {
        selected_app_ = itemToMap(*item);
        emit selectedAppChanged();
    }
}

void StoreController::openAppDetail(const QString& appId) {
    const auto* item = findItem(appId);
    if (!item) return;
    selected_app_id_ = appId;
    selected_app_ = itemToMap(*item);
    emit selectedAppChanged();

    // Enrich Flathub details lazily with real screenshots & version
    if (item->ecosystem == StoreEcosystem::Flatpak && item->screenshots.empty()) {
        fetchFlathubDetails(appId, qstr(item->install_ref));
    }
}

void StoreController::closeAppDetail() {
    if (selected_app_id_.isEmpty()) return;
    selected_app_id_.clear();
    selected_app_.clear();
    emit selectedAppChanged();
}

void StoreController::setTaskState(const QString& appId, double progress, const QString& state) {
    tasks_[appId] = ActiveTask{progress, state};
    for (int i = 0; i < static_cast<int>(filtered_.size()); ++i) {
        if (qstr(filtered_[static_cast<std::size_t>(i)].id) == appId) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {ProgressRole, TaskStateRole});
            break;
        }
    }
    if (selected_app_id_ == appId) refreshSelectedDetail();
}

void StoreController::clearTaskState(const QString& appId) {
    tasks_.remove(appId);
    for (int i = 0; i < static_cast<int>(filtered_.size()); ++i) {
        if (qstr(filtered_[static_cast<std::size_t>(i)].id) == appId) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {ProgressRole, TaskStateRole});
            break;
        }
    }
    if (selected_app_id_ == appId) refreshSelectedDetail();
}

void StoreController::downloadFileFast(const QString& appId, const QString& url,
                                       const QString& destPath,
                                       double progressStart, double progressSpan,
                                       std::function<void(bool ok, const QString& err)> onDone) {
    const QFileInfo fi(destPath);
    QDir().mkpath(fi.absolutePath());
    QFile::remove(destPath);

    // Prefer 16-connection parallel aria2c for 5x-10x faster downloads + live speed telemetry
    if (finn::compat::executable_exists("aria2c")) {
        setTaskState(appId, progressStart, QStringLiteral("Connecting (16x parallel)..."));
        auto* proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        auto last_err = std::make_shared<QString>();
        connect(proc, &QProcess::readyReadStandardOutput, this,
                [this, proc, appId, progressStart, progressSpan, last_err]() {
                    const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
                    static const QRegularExpression pctRe(QStringLiteral("\\((\\d{1,3})%\\)"));
                    static const QRegularExpression spdRe(QStringLiteral("DL:([0-9.]+[A-Za-z]+)(?:/s)?"));
                    const auto pm = pctRe.match(chunk);
                    const auto sm = spdRe.match(chunk);
                    if (pm.hasMatch() || sm.hasMatch()) {
                        const int pct = pm.hasMatch() ? pm.captured(1).toInt() : 0;
                        const double p = progressStart + progressSpan * (std::clamp(pct, 0, 100) / 100.0);
                        QString st = QStringLiteral("Downloading");
                        if (sm.hasMatch()) st += QStringLiteral(" · %1/s").arg(sm.captured(1));
                        if (pm.hasMatch()) st += QStringLiteral(" · %1%").arg(pct);
                        setTaskState(appId, p, st);
                    }
                    if (chunk.contains(QLatin1String("errorCode=")) ||
                        chunk.contains(QLatin1String("Exception:"))) {
                        *last_err = chunk.trimmed().section(QLatin1Char('\n'), -1);
                    }
                });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc, destPath, onDone, last_err](int code, QProcess::ExitStatus status) {
                    proc->deleteLater();
                    const bool ok = (status == QProcess::NormalExit && code == 0 &&
                                     QFileInfo::exists(destPath) && QFileInfo(destPath).size() > 0);
                    onDone(ok, ok ? QString() : (last_err->isEmpty() ? QStringLiteral("aria2c download failed (code %1)").arg(code) : *last_err));
                });
        proc->start(QStringLiteral("aria2c"), {
            QStringLiteral("-x16"),
            QStringLiteral("-s16"),
            QStringLiteral("-k1M"),
            QStringLiteral("--allow-overwrite=true"),
            QStringLiteral("--auto-file-renaming=false"),
            QStringLiteral("--summary-interval=1"),
            QStringLiteral("--console-log-level=notice"),
            QStringLiteral("--user-agent=FinnStore/1.0"),
            QStringLiteral("-d"), fi.absolutePath(),
            QStringLiteral("-o"), fi.fileName(),
            url,
        });
        if (proc->waitForStarted(3000)) return;
        proc->deleteLater();
    }

    // Fallback: QNetworkAccessManager with live MB/s calculation
    setTaskState(appId, progressStart, QStringLiteral("Downloading..."));
    QNetworkRequest dlreq{QUrl(url)};
    dlreq.setRawHeader("User-Agent", "FinnStore/1.0");
    dlreq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* dl = nam_->get(dlreq);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    connect(dl, &QNetworkReply::downloadProgress, this,
            [this, appId, progressStart, progressSpan, timer](qint64 rec, qint64 tot) {
                const double secs = std::max(0.1, static_cast<double>(timer->elapsed()) / 1000.0);
                const QString spd = format_speed_bps(static_cast<double>(rec) / secs);
                if (tot > 0) {
                    const int pct = qRound(100.0 * static_cast<double>(rec) / static_cast<double>(tot));
                    const double p = progressStart + progressSpan * (static_cast<double>(rec) / static_cast<double>(tot));
                    setTaskState(appId, p, QStringLiteral("Downloading · %1 · %2%").arg(spd).arg(pct));
                } else {
                    setTaskState(appId, progressStart + progressSpan * 0.3,
                                 QStringLiteral("Downloading · %1").arg(spd));
                }
            });
    connect(dl, &QNetworkReply::finished, this, [dl, destPath, onDone]() {
        const QByteArray bytes = dl->readAll();
        const bool ok = (dl->error() == QNetworkReply::NoError && !bytes.isEmpty());
        const QString err = dl->errorString();
        dl->deleteLater();
        if (!ok) {
            onDone(false, err.isEmpty() ? QStringLiteral("Download failed") : err);
            return;
        }
        QFile f(destPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            onDone(false, QStringLiteral("Cannot write downloaded file"));
            return;
        }
        f.write(bytes);
        f.close();
        onDone(true, {});
    });
}

void StoreController::runProcessInstall(const QString& appId, const QString& program,
                                        const QStringList& args, const QString& successMsg,
                                        std::function<void()> onSuccess) {
    setTaskState(appId, 0.15, QStringLiteral("Installing..."));
    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    auto last_output = std::make_shared<QString>();
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, appId, last_output]() {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        if (!out.trimmed().isEmpty()) {
            *last_output = out.trimmed();
        }
        static const QRegularExpression pctRe(QStringLiteral("(\\d{1,3})%"));
        static const QRegularExpression spdRe(QStringLiteral("([0-9.]+\\s*[kMGT]?B/s)"));
        static const QRegularExpression stepRe(QStringLiteral("(Installing\\s+\\d+/\\d+)"));
        const auto pm = pctRe.match(out);
        const auto sm = spdRe.match(out);
        const auto stm = stepRe.match(out);
        if (pm.hasMatch() || sm.hasMatch() || stm.hasMatch()) {
            const int pct = pm.hasMatch() ? pm.captured(1).toInt() : 25;
            const double p = std::clamp(static_cast<double>(pct) / 100.0, 0.08, 0.98);
            QString st = stm.hasMatch() ? stm.captured(1) : QStringLiteral("Installing");
            if (pm.hasMatch()) st += QStringLiteral(" · %1%").arg(pct);
            if (sm.hasMatch()) st += QStringLiteral(" · %1").arg(sm.captured(1).simplified());
            setTaskState(appId, p, st);
        }
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, appId, successMsg, onSuccess, last_output](int code, QProcess::ExitStatus status) {
                const QString tail = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                if (!tail.isEmpty()) *last_output = tail;
                proc->deleteLater();
                clearTaskState(appId);
                if (status == QProcess::NormalExit && code == 0) {
                    if (onSuccess) onSuccess();
                    emit toastRequested(successMsg, QStringLiteral("success"));
                    reload();
                } else {
                    QString msg;
                    for (const QString& line : last_output->split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                        if (line.contains(QLatin1String("error:"), Qt::CaseInsensitive)) {
                            msg = line.trimmed();
                        }
                    }
                    if (msg.isEmpty()) {
                        msg = last_output->isEmpty()
                            ? QStringLiteral("Operation failed (exit code %1)").arg(code)
                            : last_output->section(QLatin1Char('\n'), -1).trimmed();
                    }
                    emit toastRequested(msg, QStringLiteral("error"));
                }
            });
    proc->start(program, args);
    if (!proc->waitForStarted(3000)) {
        proc->deleteLater();
        clearTaskState(appId);
        emit toastRequested(QStringLiteral("Could not start %1").arg(program), QStringLiteral("error"));
    }
}

void StoreController::searchOnline(const QString& query) {
    const QString q = query.trimmed();
    setSearchQuery(q);
    if (q.size() < 2) return;

    busy_ = true;
    busy_message_ = QStringLiteral("Searching catalogs for \"%1\"...").arg(q);
    emit busyChanged();

    if (active_tab_ == QLatin1String("flatpak") || active_tab_ == QLatin1String("discover")) {
        queryFlathub(q);
    }
    if (active_tab_ == QLatin1String("android") || active_tab_ == QLatin1String("discover")) {
        queryAndroidFDroid(q);
        queryAuroraPlay(q);
    }
    if (active_tab_ == QLatin1String("windows") || active_tab_ == QLatin1String("discover")) {
        queryWinget(q);
    }
}

void StoreController::fetchFlathubCollection(const QString& collection, int page, bool append) {
    const bool is_special =
        collection == QLatin1String("popular") ||
        collection == QLatin1String("trending") ||
        collection == QLatin1String("recently-updated") ||
        collection == QLatin1String("recently-added");
    const QString endpoint = is_special
        ? (QString::fromLatin1(finn::store::kFlathubApiBase) + QStringLiteral("/collection/") +
           collection + QStringLiteral("?page=%1&per_page=48").arg(page))
        : (QString::fromLatin1(finn::store::kFlathubApiBase) + QStringLiteral("/collection/category/") +
           collection + QStringLiteral("?page=%1&per_page=48").arg(page));

    busy_ = true;
    busy_message_ = QStringLiteral("Loading Flathub catalog...");
    emit busyChanged();

    QNetworkRequest req{QUrl(endpoint)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, collection, page, append]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        busy_ = false;
        busy_message_.clear();
        emit busyChanged();
        if (!ok || raw.isEmpty() || collection != flathub_collection_) return;

        auto parsed = finn::store::parse_flathub_hits_json(
            std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));

        static const QRegularExpression tpRe(QStringLiteral("\"totalPages\"\\s*:\\s*(\\d+)"));
        static const QRegularExpression thRe(QStringLiteral("\"totalHits\"\\s*:\\s*(\\d+)"));
        const QString text = QString::fromUtf8(raw);
        const auto tpm = tpRe.match(text);
        const auto thm = thRe.match(text);
        flathub_page_ = page;
        flathub_total_pages_ = tpm.hasMatch() ? std::max(1, tpm.captured(1).toInt()) : page;
        flathub_total_hits_ = thm.hasMatch() ? thm.captured(1).toInt()
                                             : static_cast<int>(parsed.size());

        if (append) {
            remote_flathub_.insert(remote_flathub_.end(),
                                   std::make_move_iterator(parsed.begin()),
                                   std::make_move_iterator(parsed.end()));
        } else {
            remote_flathub_ = std::move(parsed);
        }
        rebuildCatalog();
        emit flathubBrowseChanged();
    });
}

void StoreController::queryFlathub(const QString& query) {
    QNetworkRequest req(QUrl(QString::fromLatin1(finn::store::kFlathubApiBase) + QStringLiteral("/search")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QStringLiteral("{\"query\":\"%1\",\"hits_per_page\":20}")
                                .arg(QString(query).replace(QLatin1Char('"'), QStringLiteral("\\\"")))
                                .toUtf8();
    QNetworkReply* reply = nam_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        busy_ = false;
        busy_message_.clear();
        emit busyChanged();
        if (ok && !raw.isEmpty()) {
            remote_flathub_ = finn::store::parse_flathub_hits_json(
                std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));
            rebuildCatalog();
        }
    });
}

void StoreController::fetchFlathubDetails(const QString& appId, const QString& flatpakId) {
    QNetworkRequest req(QUrl(QString::fromLatin1(finn::store::kFlathubApiBase) +
                             QStringLiteral("/appstream/") + flatpakId));
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        if (!ok || raw.isEmpty()) return;
        const std::string key = appId.toStdString();
        for (auto& a : all_items_) {
            if (a.id == key) {
                finn::store::enrich_flathub_appstream_json(
                    std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())), &a);
                break;
            }
        }
        refreshSelectedDetail();
    });
}

void StoreController::fetchAndroidCollection(const QString& collection) {
    QString q = QStringLiteral("browser");
    if (collection == QLatin1String("popular")) q = QStringLiteral("open");
    else if (collection == QLatin1String("internet")) q = QStringLiteral("browser");
    else if (collection == QLatin1String("messaging")) q = QStringLiteral("chat");
    else if (collection == QLatin1String("media")) q = QStringLiteral("music");
    else if (collection == QLatin1String("games")) q = QStringLiteral("game");
    else if (collection == QLatin1String("maps")) q = QStringLiteral("map");
    else if (collection == QLatin1String("office")) q = QStringLiteral("notes");
    else if (collection == QLatin1String("utilities")) q = QStringLiteral("file");
    queryAndroidFDroid(q);
}

void StoreController::queryAndroidFDroid(const QString& query) {
    QUrl url(QStringLiteral("https://search.f-droid.org/api/search_apps"));
    QUrlQuery uq;
    uq.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(uq);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        if (!ok || raw.isEmpty()) return;

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        const QJsonArray arr = doc.object().value(QStringLiteral("apps")).toArray();
        std::vector<StoreAppItem> with_icons, fallback_icons;
        for (const QJsonValue& v : arr) {
            const QJsonObject obj = v.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
            const QString url_str = obj.value(QStringLiteral("url")).toString().trimmed();
            if (name.isEmpty()) continue;
            QString pkg = url_str.section(QLatin1Char('/'), -1);
            if (pkg.isEmpty()) pkg = name.toLower().replace(QLatin1Char(' '), QLatin1Char('.'));
            const QString icon = obj.value(QStringLiteral("icon")).toString().trimmed();
            const bool is_default_icon = icon.isEmpty() || icon.contains(QLatin1String("ic_repo_app_default"));

            StoreAppItem item;
            item.id = "android:" + pkg.toStdString();
            item.install_ref = pkg.toStdString();
            item.name = name.toStdString();
            item.subtitle = obj.value(QStringLiteral("summary")).toString().trimmed().toStdString();
            if (item.subtitle.empty()) item.subtitle = "Open-source Android app";
            item.description = item.subtitle;
            item.developer = "F-Droid / Android";
            item.ecosystem = StoreEcosystem::Android;
            if (android_collection_ == QLatin1String("internet")) item.category = "Internet";
            else if (android_collection_ == QLatin1String("messaging")) item.category = "Messaging";
            else if (android_collection_ == QLatin1String("media")) item.category = "Media";
            else if (android_collection_ == QLatin1String("games")) item.category = "Games";
            else if (android_collection_ == QLatin1String("maps")) item.category = "Navigation";
            else if (android_collection_ == QLatin1String("office")) item.category = "Office";
            else if (android_collection_ == QLatin1String("utilities")) item.category = "Utilities";
            else item.category = "Apps";
            item.icon = is_default_icon ? "android" : icon.toStdString();
            item.verified = true;
            item.verified_source = "F-Droid";
            item.accent_hex = "#10b981";
            item.permissions = {"Android Runtime (finn-android)", "APK Package"};

            if (!is_default_icon) with_icons.push_back(std::move(item));
            else fallback_icons.push_back(std::move(item));
            if (with_icons.size() + fallback_icons.size() >= 48) break;
        }
        std::vector<StoreAppItem> parsed = std::move(with_icons);
        parsed.insert(parsed.end(), std::make_move_iterator(fallback_icons.begin()),
                      std::make_move_iterator(fallback_icons.end()));
        if (!parsed.empty()) {
            remote_android_ = std::move(parsed);
            rebuildCatalog();
        }
    });
}

void StoreController::ensureAuroraAuth(std::function<void(bool)> callback) {
    if (aurora_session_.authenticated) {
        callback(true);
        return;
    }
    aurora_waiters_.push_back(std::move(callback));
    if (aurora_auth_in_flight_) return;
    aurora_auth_in_flight_ = true;

    const auto android = finn::compat::probe_android_runtime();
    const std::string props = finn::store::build_aurora_device_properties_json(android.host_android_abi);
    QNetworkRequest req(QUrl(QString::fromLatin1(finn::store::kDefaultAuroraDispenserUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = nam_->post(req, QByteArray(props.data(), static_cast<qsizetype>(props.size())));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        const QString err_str = reply->errorString();
        reply->deleteLater();
        aurora_auth_in_flight_ = false;
        if (ok && !raw.isEmpty()) {
            aurora_session_ = finn::store::parse_aurora_dispenser_json(
                std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));
        } else {
            aurora_session_.authenticated = false;
            aurora_session_.error = err_str.toStdString();
        }
        probeRuntimes();
        auto waiters = std::move(aurora_waiters_);
        aurora_waiters_.clear();
        for (auto& cb : waiters) cb(aurora_session_.authenticated);
    });
}

void StoreController::queryAuroraPlay(const QString& query) {
    ensureAuroraAuth([this, query](bool ok) {
        if (!ok) return;
        QUrl url(QString::fromLatin1(finn::store::kGooglePlayFdfeBaseUrl) + QStringLiteral("/search"));
        QUrlQuery uq;
        uq.addQueryItem(QStringLiteral("c"), QStringLiteral("3"));
        uq.addQueryItem(QStringLiteral("q"), query);
        url.setQuery(uq);
        QNetworkRequest req(url);
        for (const auto& [k, v] : finn::store::build_play_fdfe_headers(aurora_session_)) {
            req.setRawHeader(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
        }
        QNetworkReply* reply = nam_->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const QByteArray raw = reply->readAll();
            const bool net_ok = (reply->error() == QNetworkReply::NoError);
            reply->deleteLater();
            if (net_ok && !raw.isEmpty()) {
                remote_android_ = finn::store::parse_play_fdfe_items_protobuf(
                    std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));
                rebuildCatalog();
            }
        });
    });
}

void StoreController::fetchWingetCollection(const QString& collection) {
    QString q = QStringLiteral("app");
    if (collection == QLatin1String("popular")) q = QStringLiteral("app");
    else if (collection == QLatin1String("developer")) q = QStringLiteral("code");
    else if (collection == QLatin1String("browsers")) q = QStringLiteral("browser");
    else if (collection == QLatin1String("media")) q = QStringLiteral("player");
    else if (collection == QLatin1String("graphics")) q = QStringLiteral("editor");
    else if (collection == QLatin1String("gaming")) q = QStringLiteral("game");
    else if (collection == QLatin1String("utilities")) q = QStringLiteral("tool");
    else if (collection == QLatin1String("security")) q = QStringLiteral("password");
    queryWinget(q);
}

void StoreController::queryWinget(const QString& query) {
    QUrl url(QString::fromLatin1(finn::store::kWingetRunApiBase) + QStringLiteral("/packages"));
    QUrlQuery uq;
    uq.addQueryItem(QStringLiteral("query"), query);
    uq.addQueryItem(QStringLiteral("take"), QStringLiteral("48"));
    url.setQuery(uq);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        if (ok && !raw.isEmpty()) {
            auto parsed = finn::store::parse_winget_search_json(
                std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));
            std::string col_cat = "Apps";
            if (winget_collection_ == QLatin1String("developer")) col_cat = "Developer";
            else if (winget_collection_ == QLatin1String("browsers")) col_cat = "Browsers";
            else if (winget_collection_ == QLatin1String("media")) col_cat = "Media";
            else if (winget_collection_ == QLatin1String("graphics")) col_cat = "Graphics";
            else if (winget_collection_ == QLatin1String("gaming")) col_cat = "Gaming";
            else if (winget_collection_ == QLatin1String("utilities")) col_cat = "Utilities";
            else if (winget_collection_ == QLatin1String("security")) col_cat = "Security";
            for (auto& a : parsed) {
                if (a.category == "Apps" || a.category.empty()) a.category = col_cat;
            }
            // Sort packages with real brand icons ahead of fallback icons
            std::stable_partition(parsed.begin(), parsed.end(), [](const StoreAppItem& a) {
                return a.icon.rfind("https://", 0) == 0 || a.icon.rfind("http://", 0) == 0;
            });
            if (!parsed.empty()) {
                remote_winget_ = std::move(parsed);
                rebuildCatalog();
            }
        }
    });
}

void StoreController::installApp(const QString& appId) {
    const auto* item = findItem(appId);
    if (!item) return;

    if (item->ecosystem == StoreEcosystem::Flatpak) {
        if (!finn::compat::executable_exists("flatpak")) {
            emit toastRequested(QStringLiteral("Flatpak is not installed on this system"), QStringLiteral("warning"));
            return;
        }
        // Ensure the user's Flathub remote is configured before installing
        QProcess::execute(QStringLiteral("flatpak"),
                          {QStringLiteral("remote-add"), QStringLiteral("--if-not-exists"),
                           QStringLiteral("--user"), QStringLiteral("flathub"),
                           QStringLiteral("https://dl.flathub.org/repo/flathub.flatpakrepo")});
        const QString fpid = qstr(item->install_ref);
        runProcessInstall(appId, QStringLiteral("flatpak"),
                          {QStringLiteral("install"), QStringLiteral("-y"),
                           QStringLiteral("--noninteractive"), QStringLiteral("--user"),
                           QStringLiteral("flathub"), fpid},
                          QStringLiteral("Installed %1").arg(qstr(item->name)));
        return;
    }

    if (item->ecosystem == StoreEcosystem::Android) {
        installFDroidAndroidApp(*item);
        return;
    }

    if (item->ecosystem == StoreEcosystem::Windows) {
        installWingetApp(*item);
        return;
    }

    if (item->ecosystem == StoreEcosystem::Native) {
        if (finn::compat::is_finnapp_bundle(item->install_ref)) {
            const auto res = finn::compat::install_finnapp(item->install_ref);
            if (res.ok) {
                emit toastRequested(QStringLiteral("Installed %1").arg(qstr(item->name)), QStringLiteral("success"));
                reload();
            } else {
                emit toastRequested(qstr(res.error), QStringLiteral("error"));
            }
        } else {
            emit toastRequested(QStringLiteral("%1 is a pre-installed FinnOS core app").arg(qstr(item->name)),
                                QStringLiteral("info"));
        }
    }
}

void StoreController::installFDroidAndroidApp(const StoreAppItem& item) {
    const auto rt = finn::compat::probe_android_runtime();
    if (!rt.finn_android_installed) {
        emit toastRequested(qstr(rt.summary), QStringLiteral("warning"));
        return;
    }
    const QString appId = qstr(item.id);
    const QString pkg = qstr(item.install_ref);
    const QString name = qstr(item.name);
    setTaskState(appId, 0.12, QStringLiteral("Resolving Android package..."));

    QNetworkRequest req(QUrl(QStringLiteral("https://f-droid.org/api/v1/packages/") + pkg));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, item, appId, pkg, name, rt]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();

        qint64 ver_code = 0;
        if (ok && !raw.isEmpty()) {
            const QJsonObject obj = QJsonDocument::fromJson(raw).object();
            ver_code = obj.value(QStringLiteral("suggestedVersionCode")).toVariant().toLongLong();
            if (ver_code <= 0) {
                const QJsonArray pkgs = obj.value(QStringLiteral("packages")).toArray();
                if (!pkgs.isEmpty()) {
                    ver_code = pkgs.first().toObject().value(QStringLiteral("versionCode")).toVariant().toLongLong();
                }
            }
        }
        if (ver_code <= 0) {
            // Package is not on F-Droid; fall back to Aurora Play delivery
            installAuroraAndroidApp(item);
            return;
        }

        const QString apk_url = QStringLiteral("https://f-droid.org/repo/%1_%2.apk").arg(pkg).arg(ver_code);
        const QString stage = QDir::temp().filePath(QStringLiteral("finn-fdroid-") + pkg);
        QDir().mkpath(stage);
        const QString apk_path = stage + QStringLiteral("/base.apk");
        downloadFileFast(appId, apk_url, apk_path, 0.18, 0.65,
                         [this, appId, apk_path, name, rt](bool dl_ok, const QString& err_str) {
                             if (!dl_ok) {
                                 clearTaskState(appId);
                                 emit toastRequested(QStringLiteral("APK download failed: %1").arg(err_str),
                                                     QStringLiteral("error"));
                                 return;
                             }
                             setTaskState(appId, 0.88, QStringLiteral("Installing APK..."));
                             runProcessInstall(appId, qstr(rt.finn_android_bin),
                                               {QStringLiteral("install"), apk_path},
                                               QStringLiteral("Installed %1").arg(name));
                         });
    });
}

void StoreController::installAuroraAndroidApp(const StoreAppItem& item) {
    const auto rt = finn::compat::probe_android_runtime();
    if (!rt.finn_android_installed) {
        emit toastRequested(qstr(rt.summary), QStringLiteral("warning"));
        return;
    }
    const QString appId = qstr(item.id);
    const QString pkg = qstr(item.install_ref);
    const QString name = qstr(item.name);
    setTaskState(appId, 0.1, QStringLiteral("Contacting Aurora dispenser..."));

    ensureAuroraAuth([this, appId, pkg, name, rt](bool ok) {
        if (!ok) {
            clearTaskState(appId);
            emit toastRequested(
                QStringLiteral("Aurora anonymous auth unavailable (%1). You can still sideload local .apk/.xapk files.")
                    .arg(qstr(aurora_session_.error)),
                QStringLiteral("warning"));
            return;
        }
        setTaskState(appId, 0.25, QStringLiteral("Requesting Play delivery..."));
        QUrl url(QString::fromLatin1(finn::store::kGooglePlayFdfeBaseUrl) + QStringLiteral("/delivery"));
        QUrlQuery uq;
        uq.addQueryItem(QStringLiteral("doc"), pkg);
        uq.addQueryItem(QStringLiteral("ot"), QStringLiteral("1"));
        url.setQuery(uq);
        QNetworkRequest req(url);
        for (const auto& [k, v] : finn::store::build_play_fdfe_headers(aurora_session_)) {
            req.setRawHeader(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
        }
        QNetworkReply* reply = nam_->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, appId, pkg, name, rt]() {
            const QByteArray raw = reply->readAll();
            const bool net_ok = (reply->error() == QNetworkReply::NoError);
            const QString err_str = reply->errorString();
            reply->deleteLater();
            if (!net_ok || raw.isEmpty()) {
                clearTaskState(appId);
                emit toastRequested(QStringLiteral("Google Play delivery failed: %1").arg(err_str),
                                    QStringLiteral("error"));
                return;
            }
            const auto bundle = finn::store::parse_play_delivery_protobuf(
                std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())),
                pkg.toStdString());
            if (!bundle.ok) {
                clearTaskState(appId);
                emit toastRequested(qstr(bundle.error), QStringLiteral("error"));
                return;
            }

            // Download base.apk (and splits if present) then invoke finn-android install
            const QString stage = QDir::temp().filePath(QStringLiteral("finn-aurora-") + pkg);
            QDir().mkpath(stage);
            const QString base_path = stage + QStringLiteral("/base.apk");
            setTaskState(appId, 0.45, QStringLiteral("Downloading APK..."));

            QNetworkRequest dlreq(QUrl(qstr(bundle.base_apk.download_url)));
            if (!bundle.cookie_name.empty()) {
                dlreq.setRawHeader("Cookie", QByteArray::fromStdString(
                    bundle.cookie_name + "=" + bundle.cookie_value));
            }
            QNetworkReply* dl = nam_->get(dlreq);
            connect(dl, &QNetworkReply::downloadProgress, this, [this, appId](qint64 rec, qint64 tot) {
                if (tot > 0) {
                    setTaskState(appId, 0.45 + 0.45 * (static_cast<double>(rec) / tot),
                                 QStringLiteral("Downloading APK..."));
                }
            });
            connect(dl, &QNetworkReply::finished, this, [this, dl, appId, base_path, name, rt]() {
                const QByteArray bytes = dl->readAll();
                const bool dl_ok = (dl->error() == QNetworkReply::NoError);
                dl->deleteLater();
                if (!dl_ok || bytes.isEmpty()) {
                    clearTaskState(appId);
                    emit toastRequested(QStringLiteral("APK download failed"), QStringLiteral("error"));
                    return;
                }
                QFile out(base_path);
                if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    clearTaskState(appId);
                    emit toastRequested(QStringLiteral("Failed to write downloaded APK"), QStringLiteral("error"));
                    return;
                }
                out.write(bytes);
                out.close();
                runProcessInstall(appId, qstr(rt.finn_android_bin),
                                  {QStringLiteral("install"), base_path},
                                  QStringLiteral("Installed %1").arg(name));
            });
        });
    });
}

void StoreController::installWingetApp(const StoreAppItem& item) {
    const auto wine = finn::compat::probe_wine_runtime();
    if (!wine.ready) {
        emit toastRequested(qstr(wine.summary), QStringLiteral("warning"));
        return;
    }
    const QString appId = qstr(item.id);
    const QString wgid = qstr(item.install_ref);
    const QString name = qstr(item.name);
    const QString runner = wine.finn_win_installed
        ? qstr(wine.finn_win_bin)
        : qstr(wine.proton_installed ? wine.proton_bin : wine.wine_bin);

    const QStringList parts = wgid.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        emit toastRequested(QStringLiteral("Invalid Windows package ID: %1").arg(wgid), QStringLiteral("error"));
        return;
    }
    const QString first_ch = parts.first().left(1).toLower();
    const QString rel_dir = QStringLiteral("manifests/%1/%2").arg(first_ch, parts.join(QLatin1Char('/')));

    // Primary version lookup: winget.run /v2/packages/<Publisher>/<Rest> (no GitHub 60/hr API rate limit)
    const QString publisher = parts.first();
    const QString pkg_rest = parts.mid(1).join(QLatin1Char('.'));
    const QString wr_url = QString::fromLatin1(finn::store::kWingetRunApiBase) +
                           QStringLiteral("/packages/") + QUrl::toPercentEncoding(publisher) +
                           QLatin1Char('/') + QUrl::toPercentEncoding(pkg_rest);

    setTaskState(appId, 0.08, QStringLiteral("Resolving Windows package..."));
    QNetworkRequest req{QUrl(wr_url)};
    req.setRawHeader("User-Agent", "FinnStore/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId, wgid, name, rel_dir, runner]() {
        const QByteArray raw = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        QString latest_ver;
        if (ok && !raw.isEmpty()) {
            const QJsonObject pkg = QJsonDocument::fromJson(raw).object().value(QStringLiteral("Package")).toObject();
            const QJsonArray vers = pkg.value(QStringLiteral("Versions")).toArray();
            if (!vers.isEmpty()) {
                latest_ver = vers.first().toString();
            }
        }
        if (!latest_ver.isEmpty()) {
            fetchWingetYamlForVersion(appId, wgid, name, rel_dir, latest_ver, runner, true);
            return;
        }
        // Fallback: GitHub contents API for microsoft/winget-pkgs
        const QString list_url = QStringLiteral("https://api.github.com/repos/microsoft/winget-pkgs/contents/") + rel_dir;
        QNetworkRequest greq{QUrl(list_url)};
        greq.setRawHeader("User-Agent", "FinnStore/1.0");
        QNetworkReply* grep = nam_->get(greq);
        connect(grep, &QNetworkReply::finished, this, [this, grep, appId, wgid, name, rel_dir, runner]() {
            const QByteArray graw = grep->readAll();
            const bool gok = (grep->error() == QNetworkReply::NoError);
            const QString gerr = grep->errorString();
            grep->deleteLater();
            if (!gok || graw.isEmpty()) {
                clearTaskState(appId);
                emit toastRequested(QStringLiteral("Could not resolve winget versions for %1 (%2)").arg(name, gerr),
                                    QStringLiteral("error"));
                return;
            }
            const QJsonArray entries = QJsonDocument::fromJson(graw).array();
            QString ver_found;
            for (const QJsonValue& v : entries) {
                const QJsonObject obj = v.toObject();
                if (obj.value(QStringLiteral("type")).toString() != QLatin1String("dir")) continue;
                const QString ver = obj.value(QStringLiteral("name")).toString();
                if (ver.isEmpty() || ver.startsWith(QLatin1Char('.'))) continue;
                ver_found = ver;
            }
            if (ver_found.isEmpty()) {
                clearTaskState(appId);
                emit toastRequested(QStringLiteral("No release version found in winget-pkgs for %1").arg(name),
                                    QStringLiteral("error"));
                return;
            }
            fetchWingetYamlForVersion(appId, wgid, name, rel_dir, ver_found, runner, false);
        });
    });
}

void StoreController::fetchWingetYamlForVersion(const QString& appId, const QString& wgid,
                                                const QString& name, const QString& relDir,
                                                const QString& version, const QString& runner,
                                                bool allowGithubFallback) {
    const QString yaml_url =
        QStringLiteral("https://raw.githubusercontent.com/microsoft/winget-pkgs/master/%1/%2/%3.installer.yaml")
            .arg(relDir, version, wgid);
    setTaskState(appId, 0.15, QStringLiteral("Fetching manifest (%1)...").arg(version));

    QNetworkRequest yreq{QUrl(yaml_url)};
    yreq.setRawHeader("User-Agent", "FinnStore/1.0");
    yreq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* yrep = nam_->get(yreq);
    connect(yrep, &QNetworkReply::finished, this,
            [this, yrep, appId, wgid, name, relDir, version, runner, allowGithubFallback]() {
                const QByteArray yraw = yrep->readAll();
                const bool yok = (yrep->error() == QNetworkReply::NoError);
                yrep->deleteLater();
                if (yok && !yraw.isEmpty()) {
                    downloadAndRunWingetManifest(appId, wgid, name, yraw, runner);
                    return;
                }
                const QString single_url =
                    QStringLiteral("https://raw.githubusercontent.com/microsoft/winget-pkgs/master/%1/%2/%3.yaml")
                        .arg(relDir, version, wgid);
                QNetworkRequest sreq{QUrl(single_url)};
                sreq.setRawHeader("User-Agent", "FinnStore/1.0");
                QNetworkReply* srep = nam_->get(sreq);
                connect(srep, &QNetworkReply::finished, this,
                        [this, srep, appId, wgid, name, relDir, runner, allowGithubFallback]() {
                            const QByteArray sraw = srep->readAll();
                            const bool sok = (srep->error() == QNetworkReply::NoError);
                            srep->deleteLater();
                            if (sok && !sraw.isEmpty()) {
                                downloadAndRunWingetManifest(appId, wgid, name, sraw, runner);
                                return;
                            }
                            if (allowGithubFallback) {
                                // winget.run version may be stale/delisted; query GitHub contents API for current version
                                const QString list_url = QStringLiteral("https://api.github.com/repos/microsoft/winget-pkgs/contents/") + relDir;
                                QNetworkRequest greq{QUrl(list_url)};
                                greq.setRawHeader("User-Agent", "FinnStore/1.0");
                                QNetworkReply* grep = nam_->get(greq);
                                connect(grep, &QNetworkReply::finished, this,
                                        [this, grep, appId, wgid, name, relDir, runner]() {
                                            const QByteArray graw = grep->readAll();
                                            const bool gok = (grep->error() == QNetworkReply::NoError);
                                            grep->deleteLater();
                                            QString ver_found;
                                            if (gok && !graw.isEmpty()) {
                                                for (const QJsonValue& v : QJsonDocument::fromJson(graw).array()) {
                                                    const QJsonObject obj = v.toObject();
                                                    if (obj.value(QStringLiteral("type")).toString() != QLatin1String("dir")) continue;
                                                    const QString ver = obj.value(QStringLiteral("name")).toString();
                                                    if (!ver.isEmpty() && !ver.startsWith(QLatin1Char('.'))) ver_found = ver;
                                                }
                                            }
                                            if (!ver_found.isEmpty()) {
                                                fetchWingetYamlForVersion(appId, wgid, name, relDir, ver_found, runner, false);
                                                return;
                                            }
                                            clearTaskState(appId);
                                            emit toastRequested(QStringLiteral("Could not fetch winget manifest for %1").arg(name),
                                                                QStringLiteral("error"));
                                        });
                                return;
                            }
                            clearTaskState(appId);
                            emit toastRequested(QStringLiteral("Could not fetch winget manifest for %1").arg(name),
                                                QStringLiteral("error"));
                        });
            });
}

void StoreController::downloadAndRunWingetManifest(const QString& appId, const QString& wgid,
                                                   const QString& name, const QByteArray& yamlBytes,
                                                   const QString& runner) {
    const auto manifest = finn::store::parse_winget_installer_yaml(
        std::string_view(yamlBytes.constData(), static_cast<std::size_t>(yamlBytes.size())));
    if (!manifest.valid || manifest.installer_url.empty()) {
        clearTaskState(appId);
        emit toastRequested(QStringLiteral("No downloadable Windows installer URL in winget manifest for %1").arg(name),
                            QStringLiteral("error"));
        return;
    }

    const QString dl_url = qstr(manifest.installer_url);
    const QString expected_sha = qstr(manifest.sha256).toLower();
    const QString url_fname = QFileInfo(QUrl(dl_url).path()).fileName();
    const QString ext = dl_url.endsWith(QStringLiteral(".msi"), Qt::CaseInsensitive)
                            ? QStringLiteral(".msi")
                            : QStringLiteral(".exe");
    const QString safe_name = url_fname.isEmpty() ? (QStringLiteral("setup") + ext)
                                                  : (url_fname.endsWith(ext, Qt::CaseInsensitive) ? url_fname : (url_fname + ext));
    const QString dest = QDir::temp().filePath(QStringLiteral("finn-winget-") + safe_name);
    const QString silent = qstr(manifest.silent_switches).trimmed();
    const QString inst_type = qstr(manifest.installer_type).toLower();
    const auto file_exts = manifest.file_extensions;

    downloadFileFast(appId, dl_url, dest, 0.20, 0.58,
                     [this, appId, wgid, dest, expected_sha, name, runner, silent, inst_type, file_exts](
                         bool dl_ok, const QString& err_str) {
                         if (!dl_ok) {
                             clearTaskState(appId);
                             emit toastRequested(QStringLiteral("Windows installer download failed: %1").arg(err_str),
                                                 QStringLiteral("error"));
                             return;
                         }
                         if (!expected_sha.isEmpty()) {
                             setTaskState(appId, 0.80, QStringLiteral("Verifying SHA-256..."));
                             QFile f(dest);
                             if (!f.open(QIODevice::ReadOnly)) {
                                 clearTaskState(appId);
                                 emit toastRequested(QStringLiteral("Cannot read downloaded installer"), QStringLiteral("error"));
                                 return;
                             }
                             QCryptographicHash hasher(QCryptographicHash::Sha256);
                             hasher.addData(&f);
                             const QString got = QString::fromLatin1(hasher.result().toHex());
                             if (got != expected_sha) {
                                 clearTaskState(appId);
                                 emit toastRequested(
                                     QStringLiteral("SHA-256 checksum mismatch for %1 — aborted").arg(name),
                                     QStringLiteral("error"));
                                 return;
                             }
                         }

                         const auto wine_rt = finn::compat::probe_wine_runtime();
                         const QString prefix = qstr(wine_rt.default_prefix);
                         const QSet<QString> before_exes = snapshot_wine_exes(prefix);

                         QStringList args{dest};
                         if (!silent.isEmpty()) {
                             args.append(silent.split(QLatin1Char(' '), Qt::SkipEmptyParts));
                         } else if (inst_type == QLatin1String("nullsoft") || inst_type == QLatin1String("nsis")) {
                             args.append(QStringLiteral("/S"));
                         } else if (inst_type == QLatin1String("inno")) {
                             args.append({QStringLiteral("/VERYSILENT"), QStringLiteral("/NORESTART")});
                         }
                         setTaskState(appId, 0.86, QStringLiteral("Installing via finn-win..."));
                         runProcessInstall(
                             appId, runner, args,
                             QStringLiteral("Installed %1").arg(name),
                             [appId, wgid, name, prefix, before_exes, file_exts]() {
                                 const QString exe = find_best_windows_exe(prefix, before_exes, name);
                                 if (!exe.isEmpty()) {
                                     register_windows_app_and_mimes(appId, wgid, name, exe, file_exts);
                                 }
                             });
                     });
}

void StoreController::launchApp(const QString& appId) {
    const auto* item = findItem(appId);
    if (!item) return;

    // Primary path: launch via finn-session broker (respects all 6 OS ecosystems + Wayland env)
    const std::string target_id = item->session_app_id.empty() ? item->id : item->session_app_id;
    finn::SessionClient client;
    std::string err;
    if (client.connect(&err) && client.launch(target_id, &err)) {
        emit toastRequested(QStringLiteral("Launched %1").arg(qstr(item->name)), QStringLiteral("success"));
        return;
    }

    // Fallback when finn-session is not running (e.g. host dev testing)
    bool ok = false;
    if (item->ecosystem == StoreEcosystem::Flatpak) {
        ok = QProcess::startDetached(QStringLiteral("flatpak"),
                                     {QStringLiteral("run"), qstr(item->install_ref)});
    } else if (item->ecosystem == StoreEcosystem::Android) {
        const auto rt = finn::compat::probe_android_runtime();
        if (rt.finn_android_installed) {
            ok = QProcess::startDetached(qstr(rt.finn_android_bin),
                                         {QStringLiteral("run"), qstr(item->install_ref)});
        }
    } else if (item->ecosystem == StoreEcosystem::Windows) {
        const auto wine = finn::compat::probe_wine_runtime();
        const QString conf = QDir::homePath() + QStringLiteral("/") + QString::fromLatin1(finn::kUserWindowsConfRel);
        const auto entries = finn::compat::load_windows_apps(conf.toStdString());
        for (const auto& e : entries) {
            if (e.id == item->id || e.id == item->session_app_id) {
                const QString runner = wine.finn_win_installed ? qstr(wine.finn_win_bin) : qstr(wine.wine_bin);
                ok = QProcess::startDetached(runner, {qstr(e.exe_path)});
                break;
            }
        }
    } else if (item->ecosystem == StoreEcosystem::Macos) {
        const auto rt = finn::compat::probe_macos_runtime();
        if (rt.finn_darwin_installed) {
            ok = QProcess::startDetached(qstr(rt.finn_darwin_bin),
                                         {QStringLiteral("run"), qstr(item->install_ref)});
        }
    } else if (item->ecosystem == StoreEcosystem::Ohos) {
        const auto rt = finn::compat::probe_ohos_runtime();
        if (rt.finn_ohos_installed) {
            ok = QProcess::startDetached(qstr(rt.finn_ohos_bin),
                                         {QStringLiteral("run"), qstr(item->install_ref)});
        }
    } else {
        ok = QProcess::startDetached(qstr(item->install_ref), {});
    }
    if (ok) {
        emit toastRequested(QStringLiteral("Launched %1").arg(qstr(item->name)), QStringLiteral("success"));
    } else {
        emit toastRequested(QStringLiteral("Could not launch %1").arg(qstr(item->name)), QStringLiteral("error"));
    }
}

void StoreController::uninstallApp(const QString& appId) {
    const auto* item = findItem(appId);
    if (!item) return;

    if (item->ecosystem == StoreEcosystem::Flatpak) {
        runProcessInstall(appId, QStringLiteral("flatpak"),
                          {QStringLiteral("uninstall"), QStringLiteral("-y"),
                           QStringLiteral("--noninteractive"), QStringLiteral("--user"),
                           qstr(item->install_ref)},
                          QStringLiteral("Removed %1").arg(qstr(item->name)));
        return;
    }
    if (item->ecosystem == StoreEcosystem::Android) {
        const auto rt = finn::compat::probe_android_runtime();
        if (rt.finn_android_installed) {
            runProcessInstall(appId, qstr(rt.finn_android_bin),
                              {QStringLiteral("uninstall"), qstr(item->install_ref)},
                              QStringLiteral("Removed %1").arg(qstr(item->name)));
            return;
        }
    }
    if (item->ecosystem == StoreEcosystem::Macos) {
        const auto rt = finn::compat::probe_macos_runtime();
        if (rt.finn_darwin_installed) {
            runProcessInstall(appId, qstr(rt.finn_darwin_bin),
                              {QStringLiteral("uninstall"), qstr(item->install_ref)},
                              QStringLiteral("Removed %1").arg(qstr(item->name)));
            return;
        }
    }
    if (item->ecosystem == StoreEcosystem::Ohos) {
        const auto rt = finn::compat::probe_ohos_runtime();
        if (rt.finn_ohos_installed) {
            runProcessInstall(appId, qstr(rt.finn_ohos_bin),
                              {QStringLiteral("uninstall"), qstr(item->install_ref)},
                              QStringLiteral("Removed %1").arg(qstr(item->name)));
            return;
        }
    }

    // Try finn-session APP.UNINSTALL for native .finnapp / Windows / AppImage
    finn::SessionClient client;
    std::string err;
    const std::string target_id = item->session_app_id.empty() ? item->id : item->session_app_id;
    if (client.connect(&err) && client.uninstall(target_id, &err)) {
        emit toastRequested(QStringLiteral("Removed %1").arg(qstr(item->name)), QStringLiteral("success"));
        reload();
        return;
    }
    emit toastRequested(
        QStringLiteral("Cannot uninstall %1: %2")
            .arg(qstr(item->name), err.empty() ? QStringLiteral("protected system app") : qstr(err)),
        QStringLiteral("warning"));
}

void StoreController::checkForUpdates() {
    if (!finn::compat::executable_exists("flatpak")) {
        emit toastRequested(QStringLiteral("All installed apps are up to date"), QStringLiteral("info"));
        return;
    }
    busy_ = true;
    busy_message_ = QStringLiteral("Checking for Flatpak updates...");
    emit busyChanged();

    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
                const QByteArray out = proc->readAllStandardOutput();
                proc->deleteLater();
                busy_ = false;
                busy_message_.clear();
                emit busyChanged();
                updatable_flatpaks_.clear();
                if (code == 0) {
                    for (const auto& id : finn::store::parse_flatpak_updates_output(
                             std::string_view(out.constData(), static_cast<std::size_t>(out.size())))) {
                        updatable_flatpaks_.append(qstr(id));
                    }
                }
                rebuildCatalog();
                emit toastRequested(
                    updatable_flatpaks_.isEmpty()
                        ? QStringLiteral("Everything is up to date")
                        : QStringLiteral("%1 update(s) available").arg(updatable_flatpaks_.size()),
                    QStringLiteral("info"));
            });
    proc->start(QStringLiteral("flatpak"),
                {QStringLiteral("remote-ls"), QStringLiteral("--updates"),
                 QStringLiteral("--columns=application,version")});
}

void StoreController::updateAll() {
    if (!finn::compat::executable_exists("flatpak")) return;
    runProcessInstall(QStringLiteral("__updates__"), QStringLiteral("flatpak"),
                      {QStringLiteral("update"), QStringLiteral("-y"),
                       QStringLiteral("--noninteractive"), QStringLiteral("--user")},
                      QStringLiteral("All Flatpak apps updated"));
}

void StoreController::browseLocalPackage() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Install Package File"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStringLiteral("Installable Packages (*.apk *.xapk *.apkm *.apks *.flatpak *.flatpakref *.flatpakrepo *.appimage *.exe *.msi *.finnapp);;All Files (*)"));
    if (path.isEmpty()) return;

    // Windows installers (.exe / .msi) launch directly via Windows Installer (finn-win / Wine).
    if (ends_with_ci(path, ".exe") || ends_with_ci(path, ".msi")) {
        const auto plan = finn::compat::build_wine_launch(path.toStdString());
        if (!plan.ok) {
            emit toastRequested(qstr(plan.error), QStringLiteral("warning"));
            return;
        }
        QStringList args;
        for (std::size_t i = 1; i < plan.argv.size(); ++i) args.append(qstr(plan.argv[i]));
        if (QProcess::startDetached(qstr(plan.argv[0]), args)) {
            emit toastRequested(
                QStringLiteral("Launching Windows installer for %1...").arg(QFileInfo(path).fileName()),
                QStringLiteral("info"));
        } else {
            emit toastRequested(QStringLiteral("Could not launch Windows installer"), QStringLiteral("error"));
        }
        return;
    }

    // Android (.apk/.xapk) & Linux Flatpak (.flatpak/.flatpakref) open in Finn Store's install sheet.
    inspectLocalPackage(path);
}

void StoreController::inspectLocalPackage(const QString& path) {
    const QFileInfo fi(path);
    const std::string p = fi.absoluteFilePath().toStdString();
    finn::store::LocalPackagePreview pv;
    pv.path = p;
    pv.filename = fi.fileName().toStdString();

    if (!fi.exists()) {
        pv.error = "File does not exist: " + p;
    } else if (finn::compat::is_finnapp_bundle(p)) {
        const auto m = finn::compat::inspect_finnapp(p);
        pv.valid = m.valid;
        pv.format_label = "FinnOS Native Bundle (.finnapp)";
        pv.ecosystem = StoreEcosystem::Native;
        pv.app_name = m.name;
        pv.app_id = m.id;
        pv.version = m.version;
        pv.summary = m.note.empty() ? "Native FinnOS application bundle" : m.note;
        pv.runtime_ready = true;
        pv.runtime_note = "Native Wayland — installs directly into ~/.local/share/finn/apps";
        pv.error = m.error;
    } else if (finn::compat::detect_flatpak_file_kind(p) != finn::compat::FlatpakFileKind::Unknown) {
        pv.valid = true;
        pv.format_label = "Linux Flatpak Package";
        pv.ecosystem = StoreEcosystem::Flatpak;
        pv.app_name = fi.completeBaseName().toStdString();
        pv.summary = "Installs into your user Flatpak repository";
        pv.runtime_ready = finn::compat::executable_exists("flatpak");
        pv.runtime_note = pv.runtime_ready ? "Flatpak ready" : "Install flatpak to proceed";
    } else if (ends_with_ci(fi.fileName(), ".appimage")) {
        const auto info = finn::compat::inspect_appimage_file(p);
        pv.valid = info.valid;
        pv.format_label = "Linux AppImage";
        pv.ecosystem = StoreEcosystem::Flatpak;
        pv.app_name = fi.completeBaseName().toStdString();
        pv.summary = info.summary;
        pv.runtime_ready = info.valid && info.arch_compatible;
        pv.runtime_note = "Integrates into ~/.local/share/finn/appimages with +x";
        pv.error = info.error;
    } else if (finn::compat::is_apk_package_path(p) || ends_with_ci(fi.fileName(), ".apk") ||
               ends_with_ci(fi.fileName(), ".xapk") || ends_with_ci(fi.fileName(), ".apkm") ||
               ends_with_ci(fi.fileName(), ".apks")) {
        const auto rt = finn::compat::probe_android_runtime();
        pv.valid = true;
        pv.format_label = ends_with_ci(fi.fileName(), ".apk")
                              ? "Android Package (.apk)"
                              : "Android Split Bundle (.xapk/.apkm)";
        pv.ecosystem = StoreEcosystem::Android;
        pv.app_name = fi.completeBaseName().toStdString();
        pv.summary = "Installs into FinnOS's clean-room finn-android runtime";
        pv.runtime_ready = rt.finn_android_installed;
        pv.runtime_note = rt.summary;
    } else if (ends_with_ci(fi.fileName(), ".exe") || ends_with_ci(fi.fileName(), ".msi")) {
        const auto rt = finn::compat::probe_wine_runtime();
        pv.valid = true;
        pv.format_label = ends_with_ci(fi.fileName(), ".msi")
                              ? "Windows MSI Installer (.msi)"
                              : "Windows Executable (.exe)";
        pv.ecosystem = StoreEcosystem::Windows;
        pv.app_name = fi.completeBaseName().toStdString();
        pv.summary = "Runs inside FinnOS's finn-win Windows compatibility prefix";
        pv.runtime_ready = rt.ready;
        pv.runtime_note = rt.summary;
    } else if (fi.isDir() && ends_with_ci(fi.fileName(), ".app")) {
        const auto app = finn::compat::inspect_macos_app_bundle(p);
        const auto rt = finn::compat::probe_macos_runtime();
        pv.valid = app.valid;
        pv.format_label = "macOS Application Bundle (.app)";
        pv.ecosystem = StoreEcosystem::Macos;
        pv.app_name = app.name.empty() ? fi.completeBaseName().toStdString() : app.name;
        pv.app_id = app.bundle_id;
        pv.version = app.version;
        pv.summary = "Installs into ~/Applications via finn-darwin";
        pv.runtime_ready = rt.finn_darwin_installed;
        pv.runtime_note = rt.summary;
        pv.error = app.error;
    } else if (ends_with_ci(fi.fileName(), ".dmg") || ends_with_ci(fi.fileName(), ".pkg") ||
               ends_with_ci(fi.fileName(), ".mpkg")) {
        const auto info = finn::compat::inspect_macos_installer(p);
        const auto rt = finn::compat::probe_macos_runtime();
        pv.valid = info.valid;
        pv.format_label = info.format_detail.empty() ? "macOS Installer" : info.format_detail;
        pv.ecosystem = StoreEcosystem::Macos;
        pv.app_name = fi.completeBaseName().toStdString();
        pv.summary = "Extracts and installs macOS application via finn-darwin";
        pv.runtime_ready = rt.finn_darwin_installed;
        pv.runtime_note = rt.summary;
        pv.error = info.error;
    } else if (finn::compat::is_ohos_package_path(p)) {
        const auto rt = finn::compat::probe_ohos_runtime();
        pv.valid = true;
        pv.format_label = "OpenHarmony Package (.hap/.hsp/.app)";
        pv.ecosystem = StoreEcosystem::Ohos;
        pv.app_name = fi.completeBaseName().toStdString();
        pv.summary = "Installs via finn-ohos (OpenHarmony 6.1 LTS runtime)";
        pv.runtime_ready = rt.finn_ohos_installed;
        pv.runtime_note = rt.summary;
    } else {
        pv.error = "Unsupported package format: " + pv.filename;
    }

    local_preview_ = pv;
    QVariantMap m;
    m.insert(QStringLiteral("valid"), pv.valid);
    m.insert(QStringLiteral("path"), qstr(pv.path));
    m.insert(QStringLiteral("filename"), qstr(pv.filename));
    m.insert(QStringLiteral("formatLabel"), qstr(pv.format_label));
    m.insert(QStringLiteral("ecosystemLabel"), QString::fromLatin1(finn::store::ecosystem_label(pv.ecosystem)));
    m.insert(QStringLiteral("appName"), qstr(pv.app_name));
    m.insert(QStringLiteral("version"), qstr(pv.version));
    m.insert(QStringLiteral("summary"), qstr(pv.summary));
    m.insert(QStringLiteral("runtimeReady"), pv.runtime_ready);
    m.insert(QStringLiteral("runtimeNote"), qstr(pv.runtime_note));
    m.insert(QStringLiteral("error"), qstr(pv.error));
    local_package_ = std::move(m);
    local_package_open_ = true;
    emit localPackageChanged();
}

void StoreController::closeLocalPackage() {
    local_package_open_ = false;
    emit localPackageChanged();
}

void StoreController::confirmInstallLocalPackage() {
    if (!local_preview_.valid) return;
    const std::string p = local_preview_.path;
    const QString qp = qstr(p);
    const QString name = qstr(local_preview_.app_name);
    closeLocalPackage();

    if (finn::compat::is_finnapp_bundle(p)) {
        const auto res = finn::compat::install_finnapp(p);
        emit toastRequested(res.ok ? QStringLiteral("Installed %1").arg(name) : qstr(res.error),
                            res.ok ? QStringLiteral("success") : QStringLiteral("error"));
        if (res.ok) reload();
        return;
    }
    if (ends_with_ci(qp, ".appimage")) {
        std::string err;
        const std::string dst = finn::compat::integrate_appimage(p, {}, &err);
        emit toastRequested(dst.empty() ? qstr(err) : QStringLiteral("Integrated %1").arg(name),
                            dst.empty() ? QStringLiteral("error") : QStringLiteral("success"));
        if (!dst.empty()) reload();
        return;
    }
    if (local_preview_.ecosystem == StoreEcosystem::Flatpak) {
        const auto plan = finn::compat::build_flatpak_install_plan(p);
        if (!plan.ok) {
            emit toastRequested(qstr(plan.error), QStringLiteral("error"));
            return;
        }
        QStringList args;
        for (std::size_t i = 1; i < plan.argv.size(); ++i) args.append(qstr(plan.argv[i]));
        runProcessInstall(qp, qstr(plan.argv[0]), args, QStringLiteral("Installed %1").arg(name));
        return;
    }
    if (local_preview_.ecosystem == StoreEcosystem::Android) {
        const auto rt = finn::compat::probe_android_runtime();
        if (!rt.finn_android_installed) {
            emit toastRequested(qstr(rt.summary), QStringLiteral("warning"));
            return;
        }
        runProcessInstall(qp, qstr(rt.finn_android_bin), {QStringLiteral("install"), qp},
                          QStringLiteral("Installed %1").arg(name));
        return;
    }
    if (local_preview_.ecosystem == StoreEcosystem::Macos) {
        const auto plan = finn::compat::build_macos_install_plan(p);
        if (!plan.ok) {
            emit toastRequested(qstr(plan.error), QStringLiteral("error"));
            return;
        }
        QStringList args;
        for (std::size_t i = 1; i < plan.argv.size(); ++i) args.append(qstr(plan.argv[i]));
        runProcessInstall(qp, qstr(plan.argv[0]), args, QStringLiteral("Installed %1").arg(name));
        return;
    }
    if (local_preview_.ecosystem == StoreEcosystem::Ohos) {
        const auto plan = finn::compat::build_ohos_install_plan(p);
        if (!plan.ok) {
            emit toastRequested(qstr(plan.error), QStringLiteral("error"));
            return;
        }
        QStringList args;
        for (std::size_t i = 1; i < plan.argv.size(); ++i) args.append(qstr(plan.argv[i]));
        runProcessInstall(qp, qstr(plan.argv[0]), args, QStringLiteral("Installed %1").arg(name));
        return;
    }
    if (local_preview_.ecosystem == StoreEcosystem::Windows) {
        const auto plan = finn::compat::build_wine_launch(p);
        if (!plan.ok) {
            emit toastRequested(qstr(plan.error), QStringLiteral("error"));
            return;
        }
        QStringList args;
        for (std::size_t i = 1; i < plan.argv.size(); ++i) args.append(qstr(plan.argv[i]));
        runProcessInstall(qp, qstr(plan.argv[0]), args, QStringLiteral("Installed %1").arg(name));
    }
}

}  // namespace finn::apps
