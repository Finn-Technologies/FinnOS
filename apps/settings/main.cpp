// finn-settings: Native Qt 6 / FinnUI Adaptive System Settings.
// Supports Desktop (1:1 Figma About 20:98 & Sound 71:54), Tablet, Mobile, and TV.

#include "settings_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"
#include <FinnSDK/FinnSDK.hpp>

#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QString initial_page = QStringLiteral("about");
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-settings 1.0.0");
            return 0;
        }
        if ((std::strcmp(argv[i], "--page") == 0 || std::strcmp(argv[i], "-p") == 0) &&
            i + 1 < argc) {
            initial_page = QString::fromLocal8Bit(argv[++i]).trimmed().toLower();
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-settings"),
                                  QStringLiteral("Settings"),
                                  QStringLiteral("1.0.0"));

    // Keep Lock Screen Wallpaper action wired directly via FinnThemeManager::setLockWallpaper
    auto* controller = new finn::apps::SettingsController(&app.engine());
    app.setContextProperty(QStringLiteral("SettingsCtrl"), controller);
    app.setContextProperty(QStringLiteral("InitialPage"), initial_page);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnSettings/SettingsApp.qml")))) {
        std::fprintf(stderr, "finn-settings: failed to load SettingsApp.qml\n");
        return 1;
    }

    return app.exec();
}
