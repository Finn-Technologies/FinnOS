// finn-notes: Native Qt 6 / FinnUI Adaptive Notes & Markdown Editor.

#include "notes_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-notes 1.0.0");
            return 0;
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-notes"),
                                  QStringLiteral("Finn Notes"),
                                  QStringLiteral("1.0.0"));

    auto* ctrl = new finn::apps::NotesController(&app.engine());
    app.setContextProperty(QStringLiteral("NotesCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnNotes/NotesApp.qml")))) {
        std::fprintf(stderr, "finn-notes: failed to load NotesApp.qml\n");
        return 1;
    }

    return app.exec();
}
