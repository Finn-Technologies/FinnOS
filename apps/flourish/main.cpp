#include <FinnSDK/FinnSDK.hpp>
#include <QTimer>
#include <QUrl>

#include "browser_window.hpp"
#include "tab_model.hpp"

int main(int argc, char** argv) {
    finn::ui::FinnApplication app(argc, argv,
                                  QStringLiteral("flourish"),
                                  QStringLiteral("Flourish"),
                                  QStringLiteral("0.1.0"));

    QUrl initial_url;
    QString screenshot_path;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--screenshot") && i + 1 < args.size()) {
            screenshot_path = args.at(++i);
        } else if (initial_url.isEmpty()) {
            initial_url = flourish::normalize_user_url(args.at(i));
        }
    }

    flourish::BrowserWindow window(initial_url);
    window.show();

    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(2500, &window, [&window, &app, screenshot_path]() {
            window.grab().save(screenshot_path);
            app.quit();
        });
    }

    return app.exec();
}
