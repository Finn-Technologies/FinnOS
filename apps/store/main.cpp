// finn-store: macOS App Store-inspired Native Qt 6 / FinnUI Software Center.
// Supports --install <path> for seamless cross-OS package sideloading,
// --tab <discover|native|flatpak|android|windows|updates>, and --screenshot <png>.

#include "store_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QImage>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QString install_path;
    QString initial_tab;
    QString screenshot_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-store 2.0.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--install") == 0 && i + 1 < argc) {
            install_path = QString::fromLocal8Bit(argv[++i]);
        } else if (std::strcmp(argv[i], "--tab") == 0 && i + 1 < argc) {
            initial_tab = QString::fromLocal8Bit(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = QString::fromLocal8Bit(argv[++i]);
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-store"),
                                  QStringLiteral("Finn Store"),
                                  QStringLiteral("2.0.0"));

    auto* ctrl = new finn::apps::StoreController(&app.engine());
    if (!initial_tab.isEmpty()) ctrl->setActiveTab(initial_tab);
    if (!install_path.isEmpty()) ctrl->inspectLocalPackage(install_path);
    app.setContextProperty(QStringLiteral("StoreCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnStore/StoreApp.qml")))) {
        std::fprintf(stderr, "finn-store: failed to load StoreApp.qml\n");
        return 1;
    }

    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(450, &app.engine(), [&app, screenshot_path]() {
            const auto roots = app.engine().rootObjects();
            if (!roots.isEmpty()) {
                if (auto* win = qobject_cast<QQuickWindow*>(roots.first())) {
                    const QImage img = win->grabWindow();
                    if (!img.isNull()) img.save(screenshot_path);
                }
            }
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
