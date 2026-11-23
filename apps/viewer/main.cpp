// finn-viewer: Native Qt 6 / FinnUI Adaptive Image & Vector Viewer.

#include "viewer_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QString open_target;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-viewer 1.0.0");
            return 0;
        }
        if (argv[i][0] != '-' && open_target.isEmpty()) {
            open_target = QString::fromLocal8Bit(argv[i]);
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-viewer"),
                                  QStringLiteral("Finn Viewer"),
                                  QStringLiteral("1.0.0"));

    auto* ctrl = new finn::apps::ViewerController(&app.engine());
    if (!open_target.isEmpty()) ctrl->openPath(open_target);
    app.setContextProperty(QStringLiteral("ViewerCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnViewer/ViewerApp.qml")))) {
        std::fprintf(stderr, "finn-viewer: failed to load ViewerApp.qml\n");
        return 1;
    }

    return app.exec();
}
