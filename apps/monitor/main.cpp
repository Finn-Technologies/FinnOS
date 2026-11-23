// finn-monitor: Native Qt 6 / FinnUI Adaptive System Monitor.

#include "monitor_controller.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QQmlEngine>
#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-monitor 1.0.0");
            return 0;
        }
    }

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-monitor"),
                                  QStringLiteral("Finn Monitor"),
                                  QStringLiteral("1.0.0"));

    qmlRegisterType<finn::apps::CpuSparklineItem>("FinnMonitor", 1, 0, "CpuSparkline");
    auto* ctrl = new finn::apps::MonitorController(&app.engine());
    app.setContextProperty(QStringLiteral("MonitorCtrl"), ctrl);

    if (!app.load(QUrl(QStringLiteral("qrc:/qt/qml/FinnMonitor/MonitorApp.qml")))) {
        std::fprintf(stderr, "finn-monitor: failed to load MonitorApp.qml\n");
        return 1;
    }

    return app.exec();
}
