// finn-comp entry point: FinnOS Qt 6 + Wayland Compositor.

#include "comp/server.hpp"
#include "ui/qml/finn_qml_app.hpp"

#include <QQuickWindow>
#include <QString>
#include <QUrl>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    bool preview_mode = false;
    QString socket_override;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-comp 1.0.0 (Qt 6 Wayland)");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-comp [--socket <path>] [--preview] [--form-factor desktop|tablet|mobile|tv] [--screenshot <png>] [--version]");
            return 0;
        }
        if (std::strcmp(argv[i], "--preview") == 0) {
            preview_mode = true;
        } else if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_override = QString::fromLocal8Bit(argv[++i]);
        }
    }

#ifndef FINN_HAVE_QT_WAYLAND_COMPOSITOR
    // On non-Wayland development hosts (macOS), run the compositor scene in preview mode.
    preview_mode = true;
#endif

    finn::ui::qml::FinnQmlApp app(argc, argv,
                                  QStringLiteral("finn-comp"),
                                  QStringLiteral("FinnOS Compositor"),
                                  QStringLiteral("1.0.0"));

    auto* server = new finn::comp::CompositorServer(&app.engine());
    if (!server->start(&app.engine(), socket_override, preview_mode)) {
        std::fprintf(stderr, "finn-comp: failed to start compositor server\n");
        return 1;
    }

    const QUrl scene_url = preview_mode
        ? QUrl(QStringLiteral("qrc:/qt/qml/FinnComp/CompositorScene.qml"))
        : QUrl(QStringLiteral("qrc:/qt/qml/FinnComp/WaylandCompositorScene.qml"));
    if (!app.load(scene_url)) {
        std::fprintf(stderr, "finn-comp: failed to load compositor scene\n");
        return 1;
    }

    if (!app.engine().rootObjects().isEmpty()) {
        if (auto* win = qobject_cast<QQuickWindow*>(app.engine().rootObjects().first())) {
            server->attachOutputWindow(win);
        }
    }

    return app.exec();
}
