// finn-capture: Native Qt 6 / FinnUI Spectacle-grade Screenshot utility.

#include "capture_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-capture 1.0.0");
            return 0;
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-capture"),
                                  QStringLiteral("Finn Capture"),
                                  QStringLiteral("1.0.0"));

    auto* ctrl = new finn::apps::CaptureController(&app.engine());
    app.setContextProperty(QStringLiteral("CaptureCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnCapture/CaptureApp.qml")))) {
        std::fprintf(stderr, "finn-capture: failed to load CaptureApp.qml\n");
        return 1;
    }

    return app.exec();
}
