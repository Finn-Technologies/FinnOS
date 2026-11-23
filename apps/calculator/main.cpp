// finn-calculator: Native Qt 6 / FinnUI KCalc-grade Scientific & Standard Calculator.

#include "ui/qml/finn_qml_app.hpp"

#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-calculator 1.0.0");
            return 0;
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-calculator"),
                                  QStringLiteral("Finn Calculator"),
                                  QStringLiteral("1.0.0"));

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnCalculator/CalculatorApp.qml")))) {
        std::fprintf(stderr, "finn-calculator: failed to load CalculatorApp.qml\n");
        return 1;
    }

    return app.exec();
}
