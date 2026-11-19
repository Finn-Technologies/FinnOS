// finn-shell: Qt6 + LayerShellQt desktop environment coordinator combining
// bottom 4-pill Figma glass dock, Home Menu, Spotlight Search, Control Center,
// Open Windows switcher, Lock Screen, and Freedesktop Notifications server.

#include <FinnSDK/FinnSDK.hpp>

#include <QDBusConnection>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "common/paths.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/wine_runner.hpp"
#include "session/app_registry.hpp"
#include "session/client.hpp"
#include "shell/dock.hpp"
#include "shell/foreign_windows.hpp"
#include "shell/notifications.hpp"
#include "shell/oobe_capability_probe.hpp"
#include "shell/popovers.hpp"
#include "system/sys_files.hpp"

namespace {

void startOtaStatusPoll(QObject* parent,
                        finn::shell::NotificationServer* notifications,
                        const std::shared_ptr<QString>& last_notified_version,
                        const std::shared_ptr<bool>& notified_pending,
                        const std::shared_ptr<bool>& notified_rollback) {
    QString exe = QStandardPaths::findExecutable(QStringLiteral("finn-update"));
    if (exe.isEmpty()) {
        const QString fallback = QString::fromLatin1(finn::kFinnUpdateBin);
        if (QFileInfo::exists(fallback)) exe = fallback;
    }
    if (exe.isEmpty()) return;

    auto* proc = new QProcess(parent);
    QObject::connect(
        proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), parent,
        [proc, notifications, last_notified_version, notified_pending,
         notified_rollback](int exit_code, QProcess::ExitStatus status) {
            const QByteArray raw = proc->readAllStandardOutput();
            proc->deleteLater();
            if (status != QProcess::NormalExit || exit_code != 0) return;

            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            if (!doc.isObject()) return;
            const QJsonObject obj = doc.object();

            if (obj.contains(QStringLiteral("slot_committed")) &&
                !obj.value(QStringLiteral("slot_committed")).toBool(true)) {
                if (!*notified_rollback) {
                    *notified_rollback = true;
                    notifications->pushNotification(
                        QStringLiteral("System Update"), 0,
                        QStringLiteral("info"),
                        QStringLiteral("OS Update Rolled Back"),
                        QStringLiteral("Boot returned to the previous healthy OS slot."),
                        7000);
                }
                return;
            }

            const QString pending =
                obj.value(QStringLiteral("pending_slot")).toString().trimmed();
            const QJsonValue avail_val =
                obj.value(QStringLiteral("available_update"));
            QString avail_ver;
            if (avail_val.isObject()) {
                avail_ver = avail_val.toObject()
                                .value(QStringLiteral("version"))
                                .toString()
                                .trimmed();
            }

            if (!pending.isEmpty()) {
                if (!*notified_pending) {
                    *notified_pending = true;
                    const QString target =
                        avail_ver.isEmpty() ? pending : avail_ver;
                    notifications->pushNotification(
                        QStringLiteral("System Update"), 0,
                        QStringLiteral("settings-app"),
                        QStringLiteral("Restart Required to Finish Update"),
                        QStringLiteral("FinnOS (%1) is staged and ready on reboot.")
                            .arg(target),
                        7000);
                }
                return;
            }

            if (!avail_ver.isEmpty() && avail_ver != *last_notified_version) {
                *last_notified_version = avail_ver;
                notifications->pushNotification(
                    QStringLiteral("System Update"), 0,
                    QStringLiteral("settings-app"),
                    QStringLiteral("FinnOS %1 Available").arg(avail_ver),
                    QStringLiteral("Open Settings to review and install."),
                    6000);
            }
        });
    QObject::connect(proc, &QProcess::errorOccurred, parent,
                     [proc](QProcess::ProcessError) { proc->deleteLater(); });
    proc->start(exe, {QStringLiteral("status"), QStringLiteral("--json")});
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-shell 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-shell [--version]");
            return 0;
        }
    }

    finn::ui::FinnApplication app(argc, argv, QStringLiteral("finn-shell"),
                                  QStringLiteral("Finn Shell"));
    app.setQuitOnLastWindowClosed(false);

    // Opt-in startup instrumentation. The host cannot run the Qt/Wayland
    // shell, so these marks are for the VM (`FINN_SHELL_TIMINGS=1`). They log
    // elapsed wall time only; no number is reported unless it was measured.
    QElapsedTimer boot_clock;
    boot_clock.start();
    const char* timing_env = std::getenv("FINN_SHELL_TIMINGS");
    const bool timings = timing_env && *timing_env == '1';
    auto mark = [&](const char* label) {
        if (timings) {
            std::fprintf(stderr, "finn-shell startup: %-24s %6lld ms\n", label,
                         static_cast<long long>(boot_clock.elapsed()));
        }
    };
    mark("qt application ready");

    // One form-factor decision for the whole session. An explicit
    // FINN_FORM_FACTOR (used by captures and finn-sdk simulate) always wins;
    // otherwise the token-backed model resolves it from the primary screen.
    finn::shell::mobile::LayoutMetrics layout =
        finn::shell::current_layout_metrics();
    bool sim_ok = false;
    const int sim_w = QByteArray(qgetenv("FINN_SIM_WIDTH")).toInt(&sim_ok);
    if (sim_ok && sim_w > 0) {
        bool h_ok = false;
        const int sim_h = QByteArray(qgetenv("FINN_SIM_HEIGHT")).toInt(&h_ok);
        layout = finn::shell::mobile::compute_layout(
            sim_w, h_ok && sim_h > 0 ? sim_h : layout.height_dp);
    }
    if (qgetenv("FINN_FORM_FACTOR").isEmpty()) {
        qputenv("FINN_FORM_FACTOR", layout.layout == finn::shell::mobile::LayoutClass::Phone
                                        ? "phone"
                                        : layout.layout == finn::shell::mobile::LayoutClass::Tablet
                                            ? "tablet" : "desktop");
    }
    if (timings) {
        std::fprintf(stderr, "finn-shell layout: %s %dx%d dp (margin %d, row %d, pill %dx%d)\n",
                     finn::shell::mobile::layout_name(layout.layout),
                     layout.width_dp, layout.height_dp, layout.page_margin,
                     layout.row_height, layout.gesture_pill_width,
                     layout.gesture_pill_height);
    }

    finn::AppRegistry registry =
        finn::AppRegistry::load_all(finn::kCataloguePath, finn::kInstalledPath);
    auto reload_apps = [&registry]() {
        registry =
            finn::AppRegistry::load_all(finn::kCataloguePath, finn::kInstalledPath);
    };
    mark("app registry loaded");

    auto* notifications = new finn::shell::NotificationServer(&app);
    if (!notifications->startDBusService()) {
        std::fprintf(stderr,
                     "finn-shell: D-Bus notification service unavailable (continuing locally)\n");
    }

    auto* foreign_windows = new finn::shell::ForeignWindowsManager(&app);
    auto* lock_win = new finn::shell::LockScreenWindow();

    // A lock request is a small file in /run/finn/state, written by finn-comp
    // on Super+L or when the idle timeout in Settings is reached. The shell
    // owns the overlay and the password check; the compositor owns no UI.
    const QString state_dir = QString::fromLatin1(finn::kCompositorStateDir);
    const QString lock_request = QString::fromLatin1(finn::kLockRequestPath);
    auto* lock_watcher = new QFileSystemWatcher(&app);
    auto consume_lock_request = [lock_win, lock_request]() {
        if (!QFile::exists(lock_request)) return;
        QFile::remove(lock_request);
        lock_win->lockNow();
    };
    if (QDir(state_dir).exists()) {
        lock_watcher->addPath(state_dir);
        consume_lock_request();
    } else {
        const QString parent_dir = QFileInfo(state_dir).absolutePath();
        if (QDir(parent_dir).exists()) {
            lock_watcher->addPath(parent_dir);
        }
    }
    QObject::connect(
        lock_watcher, &QFileSystemWatcher::directoryChanged, &app,
        [lock_watcher, state_dir, consume_lock_request](const QString&) {
            if (QDir(state_dir).exists() &&
                !lock_watcher->directories().contains(state_dir)) {
                lock_watcher->addPath(state_dir);
            }
            consume_lock_request();
        });

    // Lock before suspend when the Settings toggle is on. logind's signal is
    // a real event; no timer is pretending to know when the machine sleeps.
    if (finn::sys::load_merged_settings().lock_on_sleep) {
        QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForSuspend"),
            lock_win, SLOT(lockNow()));
    }

    auto launch_app = [notifications](const std::string& app_id) {
        finn::SessionClient client;
        std::string err;
        if (!client.launch(app_id, &err)) {
            notifications->pushNotification(
                QStringLiteral("FinnOS"), 0, QStringLiteral("info"),
                QStringLiteral("Could not launch '%1'").arg(
                    QString::fromStdString(app_id)),
                QString::fromStdString(err), 4500);
            return;
        }
        finn::sys::record_recent_app(app_id);
    };

    auto open_target = [notifications](const std::string& target) {
        finn::SessionClient client;
        std::string err;
        if (!client.open_target(target, &err)) {
            notifications->pushNotification(
                QStringLiteral("FinnOS"), 0, QStringLiteral("info"),
                QStringLiteral("Could not open target"),
                QString::fromStdString(err), 4500);
        }
    };

    const finn::shell::ShellActions actions{
        launch_app,
        open_target,
        [lock_win]() { lock_win->lockNow(); },
        reload_apps,
    };

    auto* home_popover = new finn::shell::HomeMenuPopover(&registry.apps(), actions);
    auto* search_popover = new finn::shell::SpotlightPopover(&registry.apps(), actions);
    auto* control_popover =
        new finn::shell::ControlCenterPopover(notifications, actions);
    auto* windows_popover = new finn::shell::OpenWindowsPopover(foreign_windows);

    auto* dock = new finn::shell::DockWindow(
        &registry.apps(), foreign_windows, launch_app);
    mark("shell surfaces constructed");

    auto sync_dock_indicators = [&]() {
        dock->setActivePopovers(home_popover->isVisible(),
                                windows_popover->isVisible(),
                                search_popover->isVisible(),
                                control_popover->isVisible());
    };

    auto close_other_popovers = [&](QWidget* keep) {
        for (QWidget* w : {static_cast<QWidget*>(home_popover),
                           static_cast<QWidget*>(search_popover),
                           static_cast<QWidget*>(control_popover),
                           static_cast<QWidget*>(windows_popover)}) {
            if (w != keep && w->isVisible()) w->hide();
        }
        sync_dock_indicators();
    };

    QObject::connect(dock, &finn::shell::DockWindow::homeClicked, &app, [&]() {
        if (home_popover->isVisible()) {
            home_popover->hide();
        } else {
            close_other_popovers(home_popover);
            home_popover->openMenu();
        }
        sync_dock_indicators();
    });
    QObject::connect(dock, &finn::shell::DockWindow::windowsClicked, &app, [&]() {
        if (windows_popover->isVisible()) {
            windows_popover->hide();
        } else {
            close_other_popovers(windows_popover);
            windows_popover->openOverview();
        }
        sync_dock_indicators();
    });
    QObject::connect(dock, &finn::shell::DockWindow::searchClicked, &app, [&]() {
        if (search_popover->isVisible()) {
            search_popover->hide();
        } else {
            close_other_popovers(search_popover);
            search_popover->openSearch();
        }
        sync_dock_indicators();
    });
    QObject::connect(dock, &finn::shell::DockWindow::statusClicked, &app, [&]() {
        if (control_popover->isVisible()) {
            control_popover->hide();
        } else {
            close_other_popovers(control_popover);
            control_popover->openDrawer();
        }
        sync_dock_indicators();
    });
    QObject::connect(dock, &finn::shell::DockWindow::dockAppActivated, &app, [&]() {
        close_other_popovers(nullptr);
    });

    QObject::connect(home_popover, &finn::shell::HomeMenuPopover::closed, &app,
                     sync_dock_indicators);
    QObject::connect(search_popover, &finn::shell::SpotlightPopover::closed, &app,
                     sync_dock_indicators);
    QObject::connect(control_popover, &finn::shell::ControlCenterPopover::closed,
                     &app, sync_dock_indicators);
    QObject::connect(windows_popover, &finn::shell::OpenWindowsPopover::closed,
                     &app, sync_dock_indicators);

    // Watch application registry / .desktop directories to keep app menu current.
    auto* app_watcher = new QFileSystemWatcher(&app);
    for (const auto& dir : finn::compat::default_desktop_dirs()) {
        const QString qdir = QString::fromStdString(dir.first);
        if (QFileInfo::exists(qdir)) app_watcher->addPath(qdir);
    }
    // Installing one app writes several files in a row; without a debounce
    // each write rebuilt the whole registry, the Home grid and the dock.
    auto* registry_reload_timer = new QTimer(&app);
    registry_reload_timer->setSingleShot(true);
    registry_reload_timer->setInterval(200);
    QObject::connect(registry_reload_timer, &QTimer::timeout, &app, [&]() {
        reload_apps();
        home_popover->refreshApps();
        dock->rebuildAppsPill();
    });
    QObject::connect(app_watcher, &QFileSystemWatcher::directoryChanged, &app,
                     [registry_reload_timer]() {
                         registry_reload_timer->start();
                     });

    const char* open_env = std::getenv("FINN_SHELL_OPEN");
    const char* no_oobe_env = std::getenv("FINN_NO_OOBE");
    const QString oobe_marker =
        QDir::homePath() + QLatin1Char('/') +
        QString::fromLatin1(finn::kOobeCompleteRel);
    const QString oobe_bin = QString::fromLatin1(finn::kOobeBinPath);
    const bool is_live_iso = QFileInfo::exists(QStringLiteral("/run/archiso"));
    const bool oobe_pending =
        !QFileInfo::exists(oobe_marker) && !is_live_iso &&
        !(no_oobe_env && *no_oobe_env == '1') &&
        (!open_env || *open_env == '\0') && QFileInfo::exists(oobe_bin);

    if (oobe_pending) {
        // Preflight uses the same measured probe the OOBE flow uses. Every
        // capability is logged with its real state; nothing is assumed ready
        // because a package is listed. Unknown means "not probed".
        const auto caps = finn::shell::oobe::probe::capabilities();
        const QString preflight_path = QDir::homePath() + QLatin1Char('/') +
            QString::fromLatin1(finn::shell::oobe::probe::kOobeCapabilitiesRel);
        if (QDir().mkpath(QFileInfo(preflight_path).absolutePath())) {
            if (!finn::shell::oobe::probe::write_preflight(preflight_path, caps)) {
                std::fprintf(stderr, "finn-shell: OOBE capability preflight not writable: %s\n",
                             qPrintable(preflight_path));
            }
        }
        for (const QString& line : finn::shell::oobe::probe::serialize(caps).split('\n')) {
            if (!line.isEmpty()) std::fprintf(stderr, "finn-shell oobe capability: %s\n",
                                               qPrintable(line));
        }

        auto* oobe_proc = new QProcess(&app);
        QObject::connect(
            oobe_proc,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &app,
            [dock, oobe_proc](int, QProcess::ExitStatus) {
                dock->show();
                oobe_proc->deleteLater();
            });
        QObject::connect(
            oobe_proc, &QProcess::errorOccurred, &app,
            [dock, oobe_proc](QProcess::ProcessError) {
                if (!dock->isVisible()) dock->show();
                oobe_proc->deleteLater();
            });
        oobe_proc->start(oobe_bin, {QStringLiteral("--capabilities"), preflight_path});
    } else {
        // A normal boot is the Plymouth splash: the Finn mark on a progress
        // bar, drawn from inside the initramfs. The ribbon "hi." animation
        // belongs to first-time setup only — finn-oobe plays it on its first
        // step — so nothing is drawn over a completed desktop here.
        dock->show();
        const char* no_lock_env = std::getenv("FINN_NO_LOCK");
        if ((!open_env || *open_env == '\0') &&
            !(no_lock_env && *no_lock_env == '1')) {
            lock_win->lockNow();
        }
    }

    // Opens one popover shortly after start (home | windows | search | control).
    // It exists so a capture or a test can see a popover without a pointer; it
    // goes through the same dock signal a click does.
    if (open_env) {
        const std::string which = open_env;
        QTimer::singleShot(1500, dock, [dock, which]() {
            if (which == "home") emit dock->homeClicked();
            else if (which == "windows") emit dock->windowsClicked();
            else if (which == "search") emit dock->searchClicked();
            else if (which == "control") emit dock->statusClicked();
        });
    }
    // Poll `finn-update status --json` asynchronously (when present) to surface
    // available, staged, or rolled-back OTA states via NotificationServer.
    auto ota_last_ver = std::make_shared<QString>();
    auto ota_notified_pending = std::make_shared<bool>(false);
    auto ota_notified_rollback = std::make_shared<bool>(false);
    QTimer::singleShot(5000, &app, [&, ota_last_ver, ota_notified_pending,
                                     ota_notified_rollback]() {
        startOtaStatusPoll(&app, notifications, ota_last_ver,
                           ota_notified_pending, ota_notified_rollback);
    });
    auto* ota_poll_timer = new QTimer(&app);
    ota_poll_timer->setInterval(4 * 3600 * 1000);
    QObject::connect(ota_poll_timer, &QTimer::timeout, &app,
                     [&, ota_last_ver, ota_notified_pending,
                      ota_notified_rollback]() {
                         startOtaStatusPoll(&app, notifications, ota_last_ver,
                                            ota_notified_pending,
                                            ota_notified_rollback);
                     });
    ota_poll_timer->start();

    mark("entering event loop");
    return app.exec();
}
