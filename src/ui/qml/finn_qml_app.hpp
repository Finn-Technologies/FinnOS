// Clean C++20 / Qt 6 QML application harness for FinnOS System UI and native
// FinnSDK apps. Handles Fixel Display font registration, HugeIcons path
// resolution, form-factor CLI overrides, and offscreen screenshot capture.

#ifndef FINN_UI_QML_FINN_QML_APP_HPP
#define FINN_UI_QML_FINN_QML_APP_HPP

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QString>
#include <QUrl>

#include "ui/qml/finn_qml_types.hpp"

namespace finn::ui::qml {

struct LaunchOptions {
    QString form_factor;
    QString screenshot_path;
    QString open_panel;
    int width = 0;
    int height = 0;
    int screenshot_delay_ms = 260;
    bool force_dark = false;
    bool force_light = false;
    bool lock_on_start = false;
    bool show_version = false;
};

class FinnQmlApp {
public:
    FinnQmlApp(int& argc, char** argv,
               const QString& app_id,
               const QString& display_name,
               const QString& version = QStringLiteral("1.0.0"));

    const LaunchOptions& options() const { return options_; }
    QGuiApplication& guiApp() { return app_; }
    QQmlApplicationEngine& engine() { return engine_; }
    FinnThemeBridge* theme() const { return theme_; }

    void setContextProperty(const QString& name, QObject* obj);
    void setContextProperty(const QString& name, const QVariant& value);

    bool load(const QUrl& qml_url);
    int exec();

    static void configureEnvironment(const QString& app_id);

private:
    static LaunchOptions parseArgs(int argc, char** argv);

    QGuiApplication app_;
    QQmlApplicationEngine engine_;
    FinnThemeBridge* theme_ = nullptr;
    LaunchOptions options_;
    QString app_id_;
    QString version_;
};

}  // namespace finn::ui::qml

#endif  // FINN_UI_QML_FINN_QML_APP_HPP
