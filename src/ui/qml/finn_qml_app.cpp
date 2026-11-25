#include "ui/qml/finn_qml_app.hpp"

#include "ui/finn/tokens.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace finn::ui::qml {

LaunchOptions FinnQmlApp::parseArgs(int argc, char** argv) {
    LaunchOptions opt;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        auto next_val = [&]() -> QString {
            if (i + 1 < argc) return QString::fromLocal8Bit(argv[++i]);
            return {};
        };
        if (arg == QLatin1String("--version")) {
            opt.show_version = true;
        } else if (arg == QLatin1String("--form-factor") || arg == QLatin1String("-f")) {
            opt.form_factor = next_val().trimmed().toLower();
        } else if (arg == QLatin1String("--screenshot")) {
            opt.screenshot_path = next_val();
        } else if (arg == QLatin1String("--screenshot-delay")) {
            opt.screenshot_delay_ms = std::max(100, next_val().toInt());
        } else if (arg == QLatin1String("--open")) {
            opt.open_panel = next_val().trimmed().toLower();
        } else if (arg == QLatin1String("--width")) {
            opt.width = next_val().toInt();
        } else if (arg == QLatin1String("--height")) {
            opt.height = next_val().toInt();
        } else if (arg == QLatin1String("--dark")) {
            opt.force_dark = true;
        } else if (arg == QLatin1String("--light")) {
            opt.force_light = true;
        } else if (arg == QLatin1String("--lock")) {
            opt.lock_on_start = true;
        }
    }
    return opt;
}

void FinnQmlApp::configureEnvironment(const QString& app_id) {
    Q_UNUSED(app_id);
    // Point FINN_ICON_DIR to local repo assets if running uninstalled on dev host.
    if (qEnvironmentVariableIsEmpty("FINN_ICON_DIR")) {
        const QStringList candidates = {
            QDir::currentPath() + QStringLiteral("/assets/icons"),
            QStringLiteral("/usr/share/finn/icons"),
        };
        for (const QString& dir : candidates) {
            if (QFileInfo::exists(dir)) {
                qputenv("FINN_ICON_DIR", dir.toUtf8());
                break;
            }
        }
    }
    // Use Qt Quick Controls Basic style as lightweight zero-overhead base so
    // our custom FinnUI controls own every single pixel with minimal RAM.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    }
}

FinnQmlApp::FinnQmlApp(int& argc, char** argv,
                       const QString& app_id,
                       const QString& display_name,
                       const QString& version)
    : app_([&]() -> int& {
          configureEnvironment(app_id);
          return argc;
      }(), argv),
      options_(parseArgs(argc, argv)),
      app_id_(app_id),
      version_(version) {
    QGuiApplication::setApplicationName(app_id);
    QGuiApplication::setApplicationDisplayName(display_name);
    QGuiApplication::setApplicationVersion(version);
    QGuiApplication::setOrganizationName(QStringLiteral("FinnOS"));
    QGuiApplication::setDesktopFileName(app_id);

    // Load Fixel Display font family from local repo assets or /usr/share/fonts/OTF
    const QStringList font_dirs = {
        QDir::currentPath() + QStringLiteral("/assets/fonts/FixelDisplay"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/fonts/FixelDisplay"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../assets/fonts/FixelDisplay"),
        QStringLiteral("/usr/share/fonts/OTF"),
    };
    for (const QString& fd : font_dirs) {
        const QDir dir(fd);
        if (!dir.exists()) continue;
        for (const QString& otf : dir.entryList({QStringLiteral("*.otf")}, QDir::Files)) {
            QFontDatabase::addApplicationFont(dir.filePath(otf));
        }
    }
    QGuiApplication::setFont(finn::ui::finn_font(12.0));

    theme_ = register_finn_qml_engine(&engine_);
    if (!options_.form_factor.isEmpty()) {
        theme_->setRuntimeFormFactorOverride(options_.form_factor);
    }
    if (options_.force_dark) {
        theme_->setRuntimeDarkOverride(true, true);
    } else if (options_.force_light) {
        theme_->setRuntimeDarkOverride(true, false);
    }

    engine_.rootContext()->setContextProperty(QStringLiteral("FinnLaunchWidth"), options_.width);
    engine_.rootContext()->setContextProperty(QStringLiteral("FinnLaunchHeight"), options_.height);
    engine_.rootContext()->setContextProperty(QStringLiteral("FinnLaunchOpenPanel"), options_.open_panel);
    engine_.rootContext()->setContextProperty(QStringLiteral("FinnLaunchLocked"), options_.lock_on_start);
}

void FinnQmlApp::setContextProperty(const QString& name, QObject* obj) {
    engine_.rootContext()->setContextProperty(name, obj);
}

void FinnQmlApp::setContextProperty(const QString& name, const QVariant& value) {
    engine_.rootContext()->setContextProperty(name, value);
}

bool FinnQmlApp::load(const QUrl& qml_url) {
    if (options_.show_version) {
        std::printf("%s %s\n", qPrintable(app_id_), qPrintable(version_));
        return true;
    }

    bool ok = true;
    QObject::connect(
        &engine_, &QQmlApplicationEngine::objectCreationFailed, &app_,
        [&ok]() { ok = false; }, Qt::DirectConnection);
    engine_.load(qml_url);
    if (!ok || engine_.rootObjects().isEmpty()) {
        return false;
    }

    if (auto* win = qobject_cast<QQuickWindow*>(engine_.rootObjects().first())) {
        if (options_.width > 0 && options_.height > 0) {
            win->resize(options_.width, options_.height);
        }
        if (!options_.screenshot_path.isEmpty()) {
            const QString out_path = options_.screenshot_path;
            QTimer::singleShot(options_.screenshot_delay_ms, win, [win, out_path]() {
                const QImage img = win->grabWindow();
                if (!img.isNull()) {
                    QDir().mkpath(QFileInfo(out_path).absolutePath());
                    img.save(out_path, "PNG");
                    std::fprintf(stderr, "finn: saved screenshot %s (%dx%d)\n",
                                 qPrintable(out_path), img.width(), img.height());
                } else {
                    std::fprintf(stderr, "finn: grabWindow() returned null image\n");
                }
                QCoreApplication::quit();
            });
        }
    }
    return true;
}

int FinnQmlApp::exec() {
    if (options_.show_version) return 0;
    return app_.exec();
}

}  // namespace finn::ui::qml
