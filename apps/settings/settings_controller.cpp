// C++20 System Settings backend implementation for finn-settings.

#include "settings_controller.hpp"

#include <FinnSDK/FinnSDK.hpp>
#include "common/paths.hpp"
#include "compat/android_runner.hpp"
#include "compat/wine_runner.hpp"
#include "system/sys_files.hpp"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace finn::apps {
namespace {

QString format_bytes_gb(qint64 bytes) {
    if (bytes <= 0) return QStringLiteral("Unavailable");
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1000.0) {
        return QStringLiteral("%1 TB").arg(gb / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(gb >= 100.0 ? std::round(gb) : gb, 0, 'f', gb >= 100.0 ? 0 : 1);
}

QString read_first_line(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

#ifdef __APPLE__
QString sysctl_string(const char* name) {
    size_t len = 0;
    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0) return {};
    QByteArray buf(static_cast<int>(len), '\0');
    if (sysctlbyname(name, buf.data(), &len, nullptr, 0) != 0) return {};
    return QString::fromUtf8(buf.constData()).trimmed();
}

uint64_t sysctl_u64(const char* name) {
    uint64_t val = 0;
    size_t len = sizeof(val);
    if (sysctlbyname(name, &val, &len, nullptr, 0) != 0) return 0;
    return val;
}
#endif

}  // namespace

SettingsController::SettingsController(QObject* parent)
    : QObject(parent), services_(this) {
    probeHardwareStatic();
    probeDisplayModes();
    probeWallpapers();
    probeCompatRuntimes();
    loadPersistedSettings();
    probeSystemUsers();

    // Check once whether passwordless sudo is available so the Users UI knows
    // whether to prompt for an administrator password.
    if (::geteuid() == 0) {
        sudo_password_required_ = false;
    } else {
        const QString sudo_bin = QStandardPaths::findExecutable(QStringLiteral("sudo"));
        if (!sudo_bin.isEmpty()) {
            sudo_password_required_ = (QProcess::execute(sudo_bin, {QStringLiteral("-n"), QStringLiteral("true")}) != 0);
        }
    }

    connect(&services_, &finn::sys::FinnDesktopServices::stateUpdated,
            this, &SettingsController::hardwareChanged);

    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this, [this]() {
                loadPersistedSettings();
                emit appearanceChanged();
                emit soundChanged();
            });

    services_.refreshAll();
    update_status_ = QStringLiteral("Not checked yet");
}

void SettingsController::probeHardwareStatic() {
    hostname_ = QSysInfo::machineHostName();
    if (hostname_.isEmpty()) hostname_ = QStringLiteral("finn-device");

    const QString user = qEnvironmentVariable("USER");
    current_user_ = user.isEmpty() ? QStringLiteral("finn") : user;
    if (!current_user_.isEmpty()) {
        current_user_[0] = current_user_[0].toUpper();
    }

    os_name_ = QStringLiteral("FinnOS");
    QString ver = read_first_line(QStringLiteral("/usr/share/finn/version"));
    if (ver.isEmpty()) ver = read_first_line(QStringLiteral("VERSION"));
    os_version_ = ver.isEmpty() ? QStringLiteral("1.0.0") : ver;
    kernel_version_ = QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion();
    arch_ = QSysInfo::currentCpuArchitecture();

    // Device name
    QString product = read_first_line(QStringLiteral("/sys/class/dmi/id/product_name"));
    if (product.isEmpty() || product.contains(QStringLiteral("Standard PC"), Qt::CaseInsensitive)) {
        product = QStringLiteral("FinnOS Workstation");
    }
    device_name_ = product;

    // CPU probe
    QString cpu_raw;
#ifdef __APPLE__
    cpu_raw = sysctl_string("machdep.cpu.brand_string");
#else
    QFile cpuinfo(QStringLiteral("/proc/cpuinfo"));
    if (cpuinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(cpuinfo.readAll()).split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (line.startsWith(QStringLiteral("model name")) ||
                line.startsWith(QStringLiteral("Model")) ||
                line.startsWith(QStringLiteral("Hardware"))) {
                cpu_raw = line.section(QLatin1Char(':'), 1).trimmed();
                if (!cpu_raw.isEmpty()) break;
            }
        }
    }
#endif
    cpu_raw.remove(QStringLiteral("(R)"));
    cpu_raw.remove(QStringLiteral("(TM)"));
    cpu_raw = cpu_raw.simplified();
    if (cpu_raw.contains(QStringLiteral(" @ "))) {
        cpu_primary_ = cpu_raw.section(QStringLiteral(" @ "), 0, 0).trimmed();
        cpu_suffix_ = cpu_raw.section(QStringLiteral(" @ "), 1).trimmed();
    } else {
        cpu_primary_ = cpu_raw.isEmpty() ? arch_ : cpu_raw;
        const int cores = qMax(1, QThread::idealThreadCount());
        cpu_suffix_ = QStringLiteral("%1 cores").arg(cores);
    }

    // Graphics probe
#ifdef __APPLE__
    gpu_primary_ = cpu_primary_.contains(QStringLiteral("Apple"), Qt::CaseInsensitive)
                       ? QStringLiteral("Apple Integrated GPU")
                       : QStringLiteral("Metal Graphics");
    gpu_suffix_ = QStringLiteral("Qt 6 RHI");
#else
    gpu_primary_ = QStringLiteral("Wayland DRM / KMS");
    gpu_suffix_ = QStringLiteral("Qt 6 Scene Graph");
    QDir drm_dir(QStringLiteral("/sys/class/drm"));
    for (const QString& entry : drm_dir.entryList(QStringList{QStringLiteral("card*")}, QDir::Dirs)) {
        if (entry.contains(QLatin1Char('-'))) continue;
        const QString vendor = read_first_line(drm_dir.filePath(entry + QStringLiteral("/device/vendor"))).toLower();
        if (vendor == QLatin1String("0x8086")) gpu_primary_ = QStringLiteral("Intel Graphics");
        else if (vendor == QLatin1String("0x1002")) gpu_primary_ = QStringLiteral("AMD Radeon");
        else if (vendor == QLatin1String("0x10de")) gpu_primary_ = QStringLiteral("NVIDIA Graphics");
        else if (vendor == QLatin1String("0x1af4")) gpu_primary_ = QStringLiteral("VirtIO GPU");
        if (!vendor.isEmpty()) break;
    }
#endif

    // Memory probe
    qint64 mem_bytes = 0;
#ifdef __APPLE__
    mem_bytes = static_cast<qint64>(sysctl_u64("hw.memsize"));
#else
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(meminfo.readAll()).split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (line.startsWith(QStringLiteral("MemTotal:"))) {
                const QStringList parts = line.simplified().split(QLatin1Char(' '));
                if (parts.size() >= 2) {
                    mem_bytes = parts[1].toLongLong() * 1024LL;
                }
                break;
            }
        }
    }
#endif
    mem_primary_ = format_bytes_gb(mem_bytes);
    mem_suffix_ = QStringLiteral("Unified Memory");

    // Storage probe
    const QStorageInfo root_storage = QStorageInfo::root();
    if (root_storage.isValid() && root_storage.bytesTotal() > 0) {
        disk_primary_ = format_bytes_gb(root_storage.bytesTotal());
        disk_suffix_ = format_bytes_gb(root_storage.bytesAvailable()) + QStringLiteral(" available");
    } else {
        disk_primary_ = QStringLiteral("Unavailable");
        disk_suffix_ = QStringLiteral("No root volume");
    }

    // Display probe
    if (QScreen* s = QGuiApplication::primaryScreen()) {
        const QSize sz = s->size();
        const int hz = qRound(s->refreshRate());
        display_summary_ = QStringLiteral("%1 x %2 @ %3 Hz")
                               .arg(sz.width())
                               .arg(sz.height())
                               .arg(hz > 0 ? hz : 60);
    } else {
        display_summary_ = QStringLiteral("Headless display");
    }
}

void SettingsController::probeWallpapers() {
    wallpapers_.clear();
    const QStringList dirs = {
        QString::fromLatin1(finn::kWallpaperDir),
        QStringLiteral("assets/wallpaper"),
        QStringLiteral("assets/figma/wallpaper"),
    };
    QStringList seen;
    for (const QString& dpath : dirs) {
        QDir d(dpath);
        if (!d.exists()) continue;
        const QStringList files = d.entryList(
            {QStringLiteral("*.webp"), QStringLiteral("*.jpg"),
             QStringLiteral("*.jpeg"), QStringLiteral("*.png")},
            QDir::Files, QDir::Name);
        for (const QString& fname : files) {
            const QString full = d.absoluteFilePath(fname);
            if (seen.contains(full)) continue;
            seen.append(full);
            QVariantMap m;
            m.insert(QStringLiteral("name"), QFileInfo(fname).completeBaseName());
            m.insert(QStringLiteral("path"), full);
            m.insert(QStringLiteral("url"), QUrl::fromLocalFile(full).toString());
            wallpapers_.append(m);
        }
    }
}

void SettingsController::probeCompatRuntimes() {
    compat_runtimes_.clear();
    auto add_probe = [this](const QString& name, const QString& kind,
                            const QString& bin_name, const QString& desc) {
        const bool found = !QStandardPaths::findExecutable(bin_name).isEmpty();
        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("kind"), kind);
        m.insert(QStringLiteral("available"), found);
        m.insert(QStringLiteral("status"), found ? QStringLiteral("Installed") : QStringLiteral("Not installed"));
        m.insert(QStringLiteral("description"), desc);
        compat_runtimes_.append(m);
    };

    add_probe(QStringLiteral("Linux Flatpak"), QStringLiteral("Linux"),
              QStringLiteral("flatpak"), QStringLiteral("Sandboxed Linux desktop apps"));
    const auto wine_rt = finn::compat::probe_wine_runtime();
    {
        QVariantMap m;
        m.insert(QStringLiteral("name"), QStringLiteral("Windows (Wine / finn-win)"));
        m.insert(QStringLiteral("kind"), QStringLiteral("Windows"));
        m.insert(QStringLiteral("available"), wine_rt.ready);
        m.insert(QStringLiteral("status"), wine_rt.ready ? QStringLiteral("Installed") : QStringLiteral("Not installed"));
        m.insert(QStringLiteral("description"), QString::fromStdString(wine_rt.summary));
        compat_runtimes_.append(m);
    }
    add_probe(QStringLiteral("Finn Android Runtime"), QStringLiteral("Android"),
              QStringLiteral("finn-android"), QStringLiteral("Native APK / DEX application runner"));
    add_probe(QStringLiteral("Finn Darwin Runtime"), QStringLiteral("macOS"),
              QStringLiteral("finn-darwin"), QStringLiteral("Mach-O / SwiftUI / AppKit compatibility layer"));
    add_probe(QStringLiteral("Finn OpenHarmony Runtime"), QStringLiteral("HarmonyOS"),
              QStringLiteral("finn-ohos"), QStringLiteral("HAP / ArkUI bytecode compatibility layer"));
}

void SettingsController::loadPersistedSettings() {
    auto& tm = finn::ui::FinnThemeManager::instance();
    alert_sound_ = tm.alertSound();
    alert_volume_ = std::clamp(static_cast<double>(tm.alertVolume()) / 100.0, 0.0, 1.0);
    startup_sound_ = tm.startupSound();
    ui_sounds_ = tm.uiSounds();
    volume_feedback_ = tm.volumeFeedback();

    const auto merged = finn::sys::load_merged_settings();
    auto_lock_minutes_ = merged.auto_lock_minutes;
}

QString SettingsController::wifiSsid() const {
    const auto& n = services_.network();
    if (!n.wifi_hardware) return QStringLiteral("No Wi-Fi hardware detected");
    if (!n.wifi_enabled) return QStringLiteral("Off");
    if (!n.active_wifi_ssid.isEmpty()) return n.active_wifi_ssid;
    return QStringLiteral("Not connected");
}

QVariantList SettingsController::wifiNetworks() const {
    QVariantList out;
    for (const auto& ap : services_.network().networks) {
        QVariantMap m;
        m.insert(QStringLiteral("ssid"), ap.ssid);
        m.insert(QStringLiteral("strength"), ap.signal);
        m.insert(QStringLiteral("secured"), ap.secured);
        m.insert(QStringLiteral("active"), ap.active);
        out.append(m);
    }
    return out;
}

QString SettingsController::bluetoothSubtitle() const {
    const auto& bt = services_.bluetooth();
    if (!bt.available) return QStringLiteral("No Bluetooth adapter detected");
    if (!bt.powered) return QStringLiteral("Off");
    return QStringLiteral("Discoverable");
}

QString SettingsController::audioDeviceName() const {
    const auto& a = services_.audio();
    if (!a.available) return QStringLiteral("No audio backend detected");
    return a.sink_name.isEmpty() ? QStringLiteral("Default Output") : a.sink_name;
}

QStringList SettingsController::alertSoundChoices() const {
    return {
        QStringLiteral("Default"),
        QStringLiteral("Crystal"),
        QStringLiteral("Glass"),
        QStringLiteral("Pulse"),
        QStringLiteral("Subtle"),
    };
}

void SettingsController::setAlertSound(const QString& sound) {
    if (sound == alert_sound_) return;
    alert_sound_ = sound;
    finn::ui::FinnThemeManager::instance().setAlertSound(sound);
    emit soundChanged();
}

void SettingsController::setAlertVolume(double v) {
    const double clamped = std::clamp(v, 0.0, 1.0);
    if (std::abs(clamped - alert_volume_) < 0.002) return;
    alert_volume_ = clamped;
    finn::ui::FinnThemeManager::instance().setAlertVolume(qRound(clamped * 100.0));
    emit soundChanged();
}

void SettingsController::setStartupSound(bool on) {
    if (on == startup_sound_) return;
    startup_sound_ = on;
    finn::ui::FinnThemeManager::instance().setStartupSound(on);
    emit soundChanged();
}

void SettingsController::setUiSounds(bool on) {
    if (on == ui_sounds_) return;
    ui_sounds_ = on;
    finn::ui::FinnThemeManager::instance().setUiSounds(on);
    emit soundChanged();
}

void SettingsController::setVolumeFeedback(bool on) {
    if (on == volume_feedback_) return;
    volume_feedback_ = on;
    finn::ui::FinnThemeManager::instance().setVolumeFeedback(on);
    emit soundChanged();
}

void SettingsController::setAutoLockMinutes(int minutes) {
    const int clamped = std::clamp(minutes, 0, 120);
    if (clamped == auto_lock_minutes_) return;
    auto_lock_minutes_ = clamped;
    auto merged = finn::sys::load_merged_settings();
    merged.auto_lock_minutes = clamped;
    finn::sys::save_settings(merged);
    emit securityChanged();
}

QString SettingsController::wallpaperPath() const {
    return finn::ui::FinnThemeManager::instance().wallpaperPath();
}

QString SettingsController::lockWallpaperPath() const {
    return finn::ui::FinnThemeManager::instance().lockWallpaperPath();
}

void SettingsController::refreshHardware() {
    services_.refreshAll();
    emit hardwareChanged();
}

void SettingsController::toggleWifi() {
    if (!services_.network().wifi_hardware) return;
    services_.setWifiEnabled(!services_.network().wifi_enabled);
}

void SettingsController::scanWifi() {
    if (!services_.network().wifi_hardware) return;
    services_.refreshNetwork();
    emit toastRequested(QStringLiteral("Scanning for Wi-Fi networks"), QStringLiteral("info"));
}

void SettingsController::toggleBluetooth() {
    if (!services_.bluetooth().available) return;
    services_.setBluetoothPowered(!services_.bluetooth().powered);
}

void SettingsController::setOutputVolume(double v) {
    if (!services_.audio().available) return;
    services_.setVolumePercent(qRound(std::clamp(v, 0.0, 1.0) * 100.0));
}

void SettingsController::setAudioMuted(bool m) {
    if (!services_.audio().available) return;
    if (services_.audio().muted != m) services_.toggleMute();
}

void SettingsController::setBrightnessLevel(double v) {
    if (!services_.backlight().available) return;
    services_.setBrightnessPercent(qRound(std::clamp(v, 0.05, 1.0) * 100.0));
}

void SettingsController::probeDisplayModes() {
    QStringList found;
    QDir drm_dir(QStringLiteral("/sys/class/drm"));
    if (drm_dir.exists()) {
        for (const QString& entry : drm_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString status_path = drm_dir.filePath(entry + QStringLiteral("/status"));
            QFile sf(status_path);
            if (!sf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            if (QString::fromUtf8(sf.readAll()).trimmed() != QLatin1String("connected")) continue;
            QFile mf(drm_dir.filePath(entry + QStringLiteral("/modes")));
            if (mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QStringList lines =
                    QString::fromUtf8(mf.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString& raw : lines) {
                    const QString mode = raw.trimmed();
                    if (!mode.isEmpty() && !found.contains(mode)) {
                        found.append(mode);
                    }
                }
            }
            if (!found.isEmpty()) break;
        }
    }

    // Prioritize common Mac / 16:10 / 16:9 desktop presets first
    const QStringList preferred = {
        QStringLiteral("1280x800"),  QStringLiteral("1440x900"),
        QStringLiteral("1680x1050"), QStringLiteral("1920x1080"),
        QStringLiteral("1920x1200"), QStringLiteral("2048x1152"),
        QStringLiteral("2560x1440"), QStringLiteral("2560x1600"),
        QStringLiteral("3840x2160"),
    };
    QStringList ordered;
    if (!found.isEmpty()) {
        for (const QString& p : preferred) {
            if (found.contains(p)) ordered.append(p);
        }
        for (const QString& m : found) {
            if (!ordered.contains(m) && ordered.size() < 12) ordered.append(m);
        }
    } else {
        ordered = preferred;
    }
    available_resolutions_ = ordered;

    // Read live compositor output resolution from /run/finn/state/windows.tsv
    QFile wf(QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/windows.tsv"));
    if (wf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString first = QString::fromUtf8(wf.readLine()).trimmed();
        if (first.startsWith(QLatin1String("#OUT\t"))) {
            const QStringList parts = first.split(QLatin1Char('\t'));
            if (parts.size() >= 3) {
                current_resolution_ = parts[1] + QLatin1Char('x') + parts[2];
            }
        }
    }
    if (current_resolution_.isEmpty()) {
        current_resolution_ = ordered.isEmpty() ? QStringLiteral("1280x800") : ordered.first();
    }
    emit displayChanged();
}

void SettingsController::applyResolution(const QString& resolution) {
    const QString res = resolution.trimmed();
    if (res.isEmpty() || !res.contains(QLatin1Char('x'))) return;
    current_resolution_ = res;
    emit displayChanged();
    emit toastRequested(QStringLiteral("Switching display to %1...").arg(res), QStringLiteral("info"));
    QString bin = QStandardPaths::findExecutable(QStringLiteral("finn-display"));
    if (bin.isEmpty()) bin = QStringLiteral("/usr/local/bin/finn-display");
    QProcess::startDetached(bin, {res});
}

void SettingsController::previewAlertSound() {
    if (!services_.audio().available) {
        emit toastRequested(
            QStringLiteral("Audio output unavailable — cannot preview \"%1\"").arg(alert_sound_),
            QStringLiteral("warning"));
        return;
    }
    emit toastRequested(
        QStringLiteral("Previewing alert sound: %1").arg(alert_sound_),
        QStringLiteral("info"));
}

void SettingsController::applyWallpaper(const QString& path) {
    finn::ui::FinnThemeManager::instance().setWallpaper(path);
    emit appearanceChanged();
    emit toastRequested(QStringLiteral("Desktop wallpaper updated"), QStringLiteral("success"));
}

void SettingsController::applyLockWallpaper(const QString& path) {
    // Lock Screen Wallpaper persistence via FinnThemeManager::setLockWallpaper
    finn::ui::FinnThemeManager::instance().setLockWallpaper(path);
    emit appearanceChanged();
    emit toastRequested(QStringLiteral("Lock Screen Wallpaper updated"), QStringLiteral("success"));
}

void SettingsController::checkForUpdates() {
    if (update_checking_ || update_applying_) return;
    QString updater = QStandardPaths::findExecutable(QStringLiteral("finn-update"));
    if (updater.isEmpty() && QFileInfo::exists(QStringLiteral("/usr/local/bin/finn-update"))) {
        updater = QStringLiteral("/usr/local/bin/finn-update");
    }
    if (updater.isEmpty()) {
        update_status_ = QStringLiteral("finn-update tool is not installed on this host");
        emit updateChanged();
        return;
    }
    update_checking_ = true;
    update_available_ = false;
    available_version_.clear();
    update_status_ = QStringLiteral("Checking FinnOS OTA channel...");
    emit updateChanged();

    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        update_checking_ = false;
        const QByteArray raw = proc->readAllStandardOutput().trimmed();
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (code == 0 && doc.isObject()) {
            const QJsonObject obj = doc.object();
            const bool avail = obj.value(QStringLiteral("available")).toBool(false);
            const QString target_ver = obj.value(QStringLiteral("target_version")).toString();
            const QString reason = obj.value(QStringLiteral("reason")).toString();
            update_available_ = avail;
            available_version_ = target_ver;
            if (avail && !target_ver.isEmpty()) {
                update_status_ = QStringLiteral("FinnOS %1 is available").arg(target_ver);
            } else if (!reason.isEmpty()) {
                update_status_ = reason;
            } else {
                update_status_ = QStringLiteral("FinnOS is up to date");
            }
        } else if (!err.isEmpty()) {
            update_status_ = err.section(QLatin1Char('\n'), 0, 0);
        } else {
            update_status_ = QStringLiteral("Unable to reach update channel");
        }
        emit updateChanged();
    });
    proc->start(updater, {QStringLiteral("--json"), QStringLiteral("check")});
}

void SettingsController::applyUpdate() {
    if (update_applying_ || update_checking_) return;
    QString updater = QStandardPaths::findExecutable(QStringLiteral("finn-update"));
    if (updater.isEmpty() && QFileInfo::exists(QStringLiteral("/usr/local/bin/finn-update"))) {
        updater = QStringLiteral("/usr/local/bin/finn-update");
    }
    if (updater.isEmpty()) {
        emit toastRequested(QStringLiteral("finn-update is not installed"), QStringLiteral("warning"));
        return;
    }
    update_applying_ = true;
    update_status_ = QStringLiteral("Downloading and staging A/B update...");
    emit updateChanged();

    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        update_applying_ = false;
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        if (code == 0) {
            update_available_ = false;
            update_status_ = QStringLiteral("Update staged to inactive slot — restart to apply");
            emit toastRequested(update_status_, QStringLiteral("success"));
        } else {
            update_status_ = err.isEmpty() ? QStringLiteral("Update failed (exit %1)").arg(code)
                                           : err.section(QLatin1Char('\n'), -1);
            emit toastRequested(update_status_, QStringLiteral("error"));
        }
        emit updateChanged();
    });
    proc->start(updater, {QStringLiteral("--json"), QStringLiteral("apply")});
}

void SettingsController::copySystemSummary() {
    const QString summary = QStringLiteral(
        "%1 %2 (%3)\nDevice: %4\nProcessor: %5 (%6)\nGraphics: %7\nMemory: %8\nStorage: %9 (%10)\nDisplay: %11")
        .arg(os_name_, os_version_, kernel_version_, device_name_,
             cpu_primary_, cpu_suffix_, gpu_primary_,
             mem_primary_, disk_primary_, disk_suffix_, display_summary_);
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(summary);
    }
    emit toastRequested(QStringLiteral("System specification copied to clipboard"), QStringLiteral("success"));
}

void SettingsController::probeSystemUsers() {
    system_users_.clear();

    QSet<QString> admin_members;
    for (const char* grp_name : {"wheel", "sudo"}) {
        if (const struct group* gr = ::getgrnam(grp_name)) {
            if (gr->gr_mem) {
                for (char** m = gr->gr_mem; *m; ++m) {
                    admin_members.insert(QString::fromLocal8Bit(*m));
                }
            }
        }
    }

    const struct passwd* cur_pw = ::getpwuid(::getuid());
    const QString cur_name = (cur_pw && cur_pw->pw_name)
        ? QString::fromLocal8Bit(cur_pw->pw_name)
        : current_user_;

    QVariantList others;
    ::setpwent();
    while (const struct passwd* pw = ::getpwent()) {
        if (!pw->pw_name || !pw->pw_shell) continue;
        if (pw->pw_uid < 500 || pw->pw_uid >= 65534) continue;
        const QString sh = QString::fromLocal8Bit(pw->pw_shell);
        if (sh.isEmpty() || sh.endsWith(QLatin1String("/nologin")) ||
            sh.endsWith(QLatin1String("/false")) || sh.endsWith(QLatin1String("/sync"))) {
            continue;
        }
        const QString uname = QString::fromLocal8Bit(pw->pw_name);
        QString display = pw->pw_gecos
            ? QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed()
            : QString();
        if (display.isEmpty()) {
            display = uname;
            if (!display.isEmpty()) display[0] = display[0].toUpper();
        }
        QVariantMap u;
        u.insert(QStringLiteral("username"), uname);
        u.insert(QStringLiteral("displayName"), display);
        u.insert(QStringLiteral("uid"), static_cast<int>(pw->pw_uid));
        u.insert(QStringLiteral("homeDir"), pw->pw_dir ? QString::fromLocal8Bit(pw->pw_dir) : QString());
        u.insert(QStringLiteral("isCurrent"), uname == cur_name);
        u.insert(QStringLiteral("isAdmin"), admin_members.contains(uname));
        if (uname == cur_name) {
            system_users_.prepend(u);
        } else {
            others.append(u);
        }
    }
    ::endpwent();
    for (const QVariant& v : others) system_users_.append(v);
    emit usersChanged();
}

void SettingsController::refreshUsers() {
    probeSystemUsers();
}

void SettingsController::runPrivilegedUserScript(const QString& script,
                                                 const QStringList& args,
                                                 const QString& secretPayload,
                                                 const QString& adminPassword,
                                                 const QString& successMsg) {
    if (user_action_busy_) return;

    QString secret_path;
    if (!secretPayload.isEmpty()) {
        const QString run_dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        const QString base_dir = !run_dir.isEmpty() ? run_dir : QDir::tempPath();
        secret_path = base_dir + QStringLiteral("/finn-user-secret-%1.tmp").arg(::getpid());
        QFile sf(secret_path);
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit toastRequested(QStringLiteral("Failed to stage credentials"), QStringLiteral("error"));
            return;
        }
        ::chmod(secret_path.toLocal8Bit().constData(), 0600);
        sf.write(secretPayload.toUtf8());
        sf.close();
    }

    QString program;
    QStringList full_args;
    bool write_sudo_pw = false;

    if (::geteuid() == 0) {
        program = QStringLiteral("/bin/bash");
        full_args << QStringLiteral("-c") << script << QStringLiteral("finn-user-mgmt");
    } else {
        const QString sudo_bin = QStandardPaths::findExecutable(QStringLiteral("sudo"));
        if (sudo_bin.isEmpty()) {
            if (!secret_path.isEmpty()) QFile::remove(secret_path);
            emit toastRequested(QStringLiteral("sudo is not available on this system"), QStringLiteral("error"));
            return;
        }
        program = sudo_bin;
        if (!sudo_password_required_) {
            full_args << QStringLiteral("-n");
        } else if (!adminPassword.isEmpty()) {
            full_args << QStringLiteral("-S") << QStringLiteral("-p") << QString();
            write_sudo_pw = true;
        } else {
            if (!secret_path.isEmpty()) QFile::remove(secret_path);
            emit toastRequested(QStringLiteral("Enter your administrator password to apply user changes"),
                                QStringLiteral("warning"));
            return;
        }
        full_args << QStringLiteral("/bin/bash") << QStringLiteral("-c") << script
                  << QStringLiteral("finn-user-mgmt");
    }
    full_args.append(args);
    if (!secret_path.isEmpty()) full_args.append(secret_path);

    user_action_busy_ = true;
    emit usersChanged();

    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, proc, secret_path, successMsg](int code, QProcess::ExitStatus st) {
                if (!secret_path.isEmpty()) QFile::remove(secret_path);
                const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                user_action_busy_ = false;
                probeSystemUsers();
                if (st == QProcess::NormalExit && code == 0) {
                    emit toastRequested(successMsg, QStringLiteral("success"));
                } else {
                    const QString last = out.section(QLatin1Char('\n'), -1).trimmed();
                    emit toastRequested(last.isEmpty()
                                            ? QStringLiteral("User operation failed (exit %1)").arg(code)
                                            : last,
                                        QStringLiteral("error"));
                }
            });
    proc->start(program, full_args);
    if (write_sudo_pw) {
        proc->write((adminPassword + QLatin1Char('\n')).toUtf8());
        proc->closeWriteChannel();
    }
}

void SettingsController::createUser(const QString& fullName,
                                    const QString& username,
                                    const QString& password,
                                    bool isAdmin,
                                    const QString& adminPassword) {
    const QString uname = username.trimmed().toLower();
    static const QRegularExpression kUserRe(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
    if (!kUserRe.match(uname).hasMatch() || uname == QLatin1String("root")) {
        emit toastRequested(QStringLiteral("Username must start with a lowercase letter and use only a-z, 0-9, _, -"),
                            QStringLiteral("warning"));
        return;
    }
    if (::getpwnam(uname.toLocal8Bit().constData()) != nullptr) {
        emit toastRequested(QStringLiteral("User '%1' already exists").arg(uname), QStringLiteral("warning"));
        return;
    }
    QString safe_name = fullName.trimmed();
    safe_name.remove(QRegularExpression(QStringLiteral("[:,;\\r\\n\\t]")));
    if (safe_name.isEmpty()) {
        safe_name = uname;
        safe_name[0] = safe_name[0].toUpper();
    }

    const QString script = QStringLiteral(
        "set -e\n"
        "UNAME=\"$1\"; FNAME=\"$2\"; ADMIN=\"$3\"; PWFILE=\"$4\"\n"
        "GROUPS_LIST=\"\"\n"
        "if [ \"$ADMIN\" = \"1\" ]; then GROUPS_LIST=\"wheel\"; fi\n"
        "for g in input video render seat finn; do\n"
        "  if getent group \"$g\" >/dev/null 2>&1; then\n"
        "    GROUPS_LIST=\"${GROUPS_LIST:+${GROUPS_LIST},}${g}\"\n"
        "  fi\n"
        "done\n"
        "if [ -n \"$GROUPS_LIST\" ]; then\n"
        "  useradd -m -c \"$FNAME\" -G \"$GROUPS_LIST\" -s /bin/bash \"$UNAME\"\n"
        "else\n"
        "  useradd -m -c \"$FNAME\" -s /bin/bash \"$UNAME\"\n"
        "fi\n"
        "if [ -n \"$PWFILE\" ] && [ -f \"$PWFILE\" ]; then\n"
        "  PW=$(cat \"$PWFILE\")\n"
        "  rm -f \"$PWFILE\"\n"
        "  printf '%s:%s\\n' \"$UNAME\" \"$PW\" | chpasswd\n"
        "else\n"
        "  passwd -d \"$UNAME\" >/dev/null 2>&1 || true\n"
        "fi\n"
        "UHOME=$(getent passwd \"$UNAME\" | cut -d: -f6)\n"
        "UGRP=$(id -gn \"$UNAME\" 2>/dev/null || echo \"$UNAME\")\n"
        "if [ -n \"$UHOME\" ] && [ -d \"$UHOME\" ]; then\n"
        "  install -d -m 0755 -o \"$UNAME\" -g \"$UGRP\" \"$UHOME/.config/finn\"\n"
        "  printf 'done\\n' > \"$UHOME/.config/finn/oobe_complete\"\n"
        "  chown -R \"$UNAME:$UGRP\" \"$UHOME/.config/finn\"\n"
        "fi\n"
    );

    runPrivilegedUserScript(script,
                            {uname, safe_name, isAdmin ? QStringLiteral("1") : QStringLiteral("0")},
                            password,
                            adminPassword,
                            QStringLiteral("Created user %1 (@%2)").arg(safe_name, uname));
}

void SettingsController::changeUserPassword(const QString& username,
                                            const QString& newPassword,
                                            const QString& adminPassword) {
    const QString uname = username.trimmed();
    if (uname.isEmpty() || uname == QLatin1String("root") ||
        ::getpwnam(uname.toLocal8Bit().constData()) == nullptr) {
        emit toastRequested(QStringLiteral("Invalid user account"), QStringLiteral("error"));
        return;
    }
    if (newPassword.isEmpty()) {
        emit toastRequested(QStringLiteral("New password cannot be empty"), QStringLiteral("warning"));
        return;
    }
    const QString script = QStringLiteral(
        "set -e\n"
        "UNAME=\"$1\"; PWFILE=\"$2\"\n"
        "PW=$(cat \"$PWFILE\")\n"
        "rm -f \"$PWFILE\"\n"
        "printf '%s:%s\\n' \"$UNAME\" \"$PW\" | chpasswd\n"
    );
    runPrivilegedUserScript(script, {uname}, newPassword, adminPassword,
                            QStringLiteral("Updated password for %1").arg(uname));
}

void SettingsController::deleteUser(const QString& username, const QString& adminPassword) {
    const QString uname = username.trimmed();
    const struct passwd* cur_pw = ::getpwuid(::getuid());
    const QString cur_name = (cur_pw && cur_pw->pw_name)
        ? QString::fromLocal8Bit(cur_pw->pw_name)
        : current_user_;
    if (uname.isEmpty() || uname == QLatin1String("root") || uname == cur_name) {
        emit toastRequested(QStringLiteral("Cannot remove the active user or root"), QStringLiteral("warning"));
        return;
    }
    const QString script = QStringLiteral(
        "set -e\n"
        "userdel -r \"$1\"\n"
    );
    runPrivilegedUserScript(script, {uname}, QString(), adminPassword,
                            QStringLiteral("Removed user %1").arg(uname));
}

}  // namespace finn::apps
