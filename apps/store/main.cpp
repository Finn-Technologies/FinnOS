// finn-store: Native Qt 6 / FinnUI Adaptive App Catalog & Software Center.

#include "store_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-store 1.0.0");
            return 0;
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-store"),
                                  QStringLiteral("Finn Store"),
                                  QStringLiteral("1.0.0"));

    auto* ctrl = new finn::apps::StoreController(&app.engine());
    app.setContextProperty(QStringLiteral("StoreCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnStore/StoreApp.qml")))) {
        std::fprintf(stderr, "finn-store: failed to load StoreApp.qml\n");
        return 1;
    }

    return app.exec();
}
