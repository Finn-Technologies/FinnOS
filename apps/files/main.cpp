// finn-files: Native Qt 6 / FinnUI Adaptive File Manager.

#include "files_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QDir>
#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QString start_dir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-files 1.0.0");
            return 0;
        }
        if (argv[i][0] != '-' && start_dir.isEmpty()) {
            start_dir = QString::fromLocal8Bit(argv[i]);
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-files"),
                                  QStringLiteral("Finn Files"),
                                  QStringLiteral("1.0.0"));

    auto* ctrl = new finn::apps::FilesController(&app.engine());
    if (!start_dir.isEmpty() && QDir(start_dir).exists()) {
        ctrl->navigateTo(start_dir);
    }

    app.setContextProperty(QStringLiteral("FilesCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnFiles/FilesApp.qml")))) {
        std::fprintf(stderr, "finn-files: failed to load FilesApp.qml\n");
        return 1;
    }

    return app.exec();
}
