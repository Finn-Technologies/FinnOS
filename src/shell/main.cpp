// finn-shell: Qt6 + LayerShellQt desktop environment coordinator combining
// bottom 4-pill Figma glass dock, Home Menu, Spotlight Search, Control Center,
// Open Windows switcher, Lock Screen, and Freedesktop Notifications server.

#include <FinnSDK/FinnSDK.hpp>

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/paths.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/wine_runner.hpp"
#include "session/app_registry.hpp"
#include "session/client.hpp"
#include "shell/dock.hpp"
#include "shell/foreign_windows.hpp"
#include "shell/notifications.hpp"
#include "shell/popovers.hpp"
#include "system/sys_files.hpp"

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

    finn::AppRegistry registry =
        finn::AppRegistry::load_all(finn::kCataloguePath, finn::kInstalledPath);
    auto reload_apps = [&registry]() {
        registry =
            finn::AppRegistry::load_all(finn::kCataloguePath, finn::kInstalledPath);
    };

    auto* notifications = new finn::shell::NotificationServer(&app);
    if (!notifications->startDBusService()) {
        std::fprintf(stderr,
                     "finn-shell: D-Bus notification service unavailable (continuing locally)\n");
    }

    auto* foreign_windows = new finn::shell::ForeignWindowsManager(&app);
    auto* lock_win = new finn::shell::LockScreenWindow();

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
    QObject::connect(app_watcher, &QFileSystemWatcher::directoryChanged, &app,
                     [&]() {
                         reload_apps();
                         home_popover->refreshApps();
                         dock->rebuildAppsPill();
                     });

    dock->show();

    auto* boot_splash = new finn::shell::BootSplashWindow();
    const char* open_env = std::getenv("FINN_SHELL_OPEN");
    const char* no_splash_env = std::getenv("FINN_NO_BOOT_SPLASH");
    const char* force_splash_env = std::getenv("FINN_BOOT_SPLASH");
    const char* hold_splash_env = std::getenv("FINN_BOOT_SPLASH_HOLD");
    const bool skip_splash =
        (no_splash_env && *no_splash_env == '1') ||
        (open_env && *open_env != '\0') ||
        (finn::ui::FinnThemeManager::instance().reducedMotion() &&
         !(force_splash_env && *force_splash_env == '1'));
    if (!skip_splash) {
        const bool hold = (hold_splash_env && *hold_splash_env == '1');
        boot_splash->startBootAnimation(hold);
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
    return app.exec();
}
