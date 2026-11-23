// finn-archive: Native Qt 6 / FinnUI Ark-grade Archive Manager.

#include "archive_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    QString open_target;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-archive 1.0.0");
            return 0;
        }
        if (argv[i][0] != '-' && open_target.isEmpty()) {
            open_target = QString::fromLocal8Bit(argv[i]);
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-archive"),
                                  QStringLiteral("Finn Archive"),
                                  QStringLiteral("1.0.0"));

    auto* ctrl = new finn::apps::ArchiveController(&app.engine());
    if (!open_target.isEmpty()) ctrl->openArchive(open_target);
    app.setContextProperty(QStringLiteral("ArchiveCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnArchive/ArchiveApp.qml")))) {
        std::fprintf(stderr, "finn-archive: failed to load ArchiveApp.qml\n");
        return 1;
    }

    return app.exec();
}
