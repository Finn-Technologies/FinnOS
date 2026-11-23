// finn-flourish: Zen-inspired Chromium web browser built with Qt 6 WebEngineQuick + FinnUI.

#include "ui/qml/finn_qml_app.hpp"

#include <QString>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QString initial_url;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-flourish 1.0.0");
            return 0;
        }
        if (argv[i][0] != '-' && initial_url.isEmpty()) {
            initial_url = QString::fromLocal8Bit(argv[i]);
        }
    }

    // On software framebuffer VMs without virgl 3D accel, enable software compositing
    // for Chromium so web pages render reliably while keeping GPU accel on bare metal.
    if (qEnvironmentVariable("QT_QUICK_BACKEND")
            .compare(QStringLiteral("software"), Qt::CaseInsensitive) == 0 &&
        !qEnvironmentVariableIsSet("QTWEBENGINE_CHROMIUM_FLAGS")) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
                "--disable-gpu --disable-gpu-compositing --disable-dev-shm-usage");
    }

    QtWebEngineQuick::initialize();

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-flourish"),
                                  QStringLiteral("Flourish"),
                                  QStringLiteral("1.0.0"));

    app.setContextProperty(QStringLiteral("FlourishInitialUrl"), initial_url);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnFlourish/FlourishApp.qml")))) {
        std::fprintf(stderr, "finn-flourish: failed to load FlourishApp.qml\n");
        return 1;
    }

    return app.exec();
}
