// finn-terminal: Native Qt 6 / FinnUI Adaptive Terminal Emulator.

#include "terminal_view.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QQmlEngine>
#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-terminal 1.0.0");
            return 0;
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-terminal"),
                                  QStringLiteral("Finn Terminal"),
                                  QStringLiteral("1.0.0"));

    qmlRegisterType<finn::apps::FinnTerminalView>("FinnTerminal", 1, 0, "FinnTerminalView");

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnTerminal/TerminalApp.qml")))) {
        std::fprintf(stderr, "finn-terminal: failed to load TerminalApp.qml\n");
        return 1;
    }

    return app.exec();
}
