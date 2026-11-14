// FinnApplication: configures Wayland/FinnOS environment, desktop app_id, fonts,
// custom FinnOS Qt palette, and live FinnThemeManager wiring for native apps.

#ifndef FINN_UI_FINN_APPLICATION_HPP
#define FINN_UI_FINN_APPLICATION_HPP

#include <QApplication>
#include <QString>

namespace finn::ui {

class FinnApplication : public QApplication {
    Q_OBJECT

public:
    FinnApplication(int& argc, char** argv, const QString& app_id = {},
                    const QString& display_name = {},
                    const QString& version = QStringLiteral("0.2.0"));
    ~FinnApplication() override = default;

    QString finnAppId() const { return app_id_; }

private:
    QString app_id_;
};

}  // namespace finn::ui

#endif
