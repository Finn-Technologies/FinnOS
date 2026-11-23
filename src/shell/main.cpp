// finn-shell: Unified Adaptive FinnOS System UI (C++20 / Qt 6 / Wayland).
// Mirrors KDE Plasma's Corona architecture, seamlessly unifying Desktop
// (Plasma-style), Tablet (hybrid), Mobile (Plasma Mobile-style), and TV
// (Plasma Bigscreen-style) form factors inside one lightweight shell binary.

#include "common/paths.hpp"
#include "shell/core/shell_corona.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QQuickWindow>
#include <QUrl>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-shell 1.0.0 (Unified System UI)");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-shell [--form-factor desktop|tablet|mobile|tv] [--open home|search|control|windows|drawer] [--lock] [--dark|--light] [--width W --height H] [--screenshot out.png] [--version]");
            return 0;
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-shell"),
                                  QStringLiteral("FinnOS System UI"),
                                  QStringLiteral("1.0.0"));

    // Respect real lock-request (finn::kLockRequestPath) and logind
    // PrepareForSuspend triggers via ShellCorona. Normal boot uses Plymouth's
    // mark + bar; first-time setup runs finn-oobe.
    auto* corona = new finn::shell::ShellCorona(&app.engine());
    corona->startSystemIntegration();

    if (const char* open_env = std::getenv("FINN_SHELL_OPEN");
        open_env && *open_env) {
        corona->openPanel(QString::fromLocal8Bit(open_env));
    } else if (!app.options().open_panel.isEmpty()) {
        corona->openPanel(app.options().open_panel);
    }

    if (app.options().lock_on_start) {
        corona->lockNow();
    }

    // Seed sample notifications when explicitly requested for visual verification.
    if (qEnvironmentVariableIntValue("FINN_DEMO_NOTIFICATIONS") == 1) {
        corona->notificationModel()->push(
            QStringLiteral("Finn Files"), 0, QStringLiteral("files"),
            QStringLiteral("Transfer complete"),
            QStringLiteral("Copied 14 items to Documents."), 0);
        corona->notificationModel()->push(
            QStringLiteral("System Update"), 0, QStringLiteral("settings-app"),
            QStringLiteral("FinnOS 1.0 Ready"),
            QStringLiteral("System update verified and staged."), 0);
        corona->notificationModel()->push(
            QStringLiteral("Finn Notes"), 0, QStringLiteral("notes"),
            QStringLiteral("Saved workspace"),
            QStringLiteral("Design notes synced locally."), 0);
    }

    app.setContextProperty(QStringLiteral("ShellCorona"), corona);
    app.setContextProperty(QStringLiteral("ShellApps"), corona->appModel());
    app.setContextProperty(QStringLiteral("ShellNotifications"), corona->notificationModel());

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnShell/SystemUI.qml")))) {
        std::fprintf(stderr, "finn-shell: failed to load SystemUI.qml\n");
        return 1;
    }

    if (!app.engine().rootObjects().isEmpty()) {
        if (auto* win = qobject_cast<QQuickWindow*>(app.engine().rootObjects().first())) {
            corona->attachWindow(win);
        }
    }

    return app.exec();
}
