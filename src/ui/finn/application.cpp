// Implements FinnApplication initialization and live palette synchronization.

#include "ui/finn/application.hpp"

#include <QGuiApplication>
#include <QStyle>
#include <QStyleFactory>
#include <cstdlib>

#include "ui/finn/theme.hpp"
#include "ui/finn/tokens.hpp"

namespace finn::ui {
namespace {

void configure_finn_environment_before_qt() {
    // Allow automatic fallback between Wayland and Xwayland if started headless/X11.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "wayland;xcb");
    }
    // Use FinnOS custom platform theme & style when available in Qt plugin paths.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME")) {
        qputenv("QT_QPA_PLATFORMTHEME", "finn");
    }
    if (!qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE")) {
        qputenv("QT_STYLE_OVERRIDE", "finn");
    }
    // Disable compositor client-side server decorations from QtWayland since FinnWindow
    // renders its own FinnOS frameless rounded glass chrome + Figma control pill.
    if (!qEnvironmentVariableIsSet("QT_WAYLAND_DISABLE_WINDOWDECORATION")) {
        qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    }
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

struct EnvPreInit {
    EnvPreInit() { configure_finn_environment_before_qt(); }
};

}  // namespace

FinnApplication::FinnApplication(int& argc, char** argv, const QString& app_id,
                                 const QString& display_name,
                                 const QString& version)
    : QApplication((configure_finn_environment_before_qt(), argc), argv),
      app_id_(app_id) {
    setOrganizationName(QStringLiteral("FinnOS"));
    setOrganizationDomain(QStringLiteral("finnos.org"));
    if (!app_id.isEmpty()) {
        setApplicationName(app_id);
        setDesktopFileName(app_id);
    }
    if (!display_name.isEmpty()) {
        setApplicationDisplayName(display_name);
    }
    if (!version.isEmpty()) {
        setApplicationVersion(version);
    }

    // Force font registration and initial tokens.
    (void)primary_font_family();
    auto& mgr = FinnThemeManager::instance();
    mgr.reload();

    qputenv("XCURSOR_THEME", mgr.isDark() ? "finn-dark" : "finn-light");
    qputenv("XCURSOR_SIZE", QByteArray::number(mgr.tokens().cursor_size));

    if (style() &&
        style()->objectName().compare(QStringLiteral("finn"), Qt::CaseInsensitive) != 0) {
        if (QStyle* s = QStyleFactory::create(QStringLiteral("finn"))) {
            setStyle(s);
        }
    }

    auto apply = [](const FinnTokens& t) {
        qputenv("XCURSOR_THEME", t.dark ? "finn-dark" : "finn-light");
        qputenv("XCURSOR_SIZE", QByteArray::number(t.cursor_size));
        setPalette(build_finn_palette(t));
        setFont(finn_font(10.5, QFont::Normal));
    };
    apply(mgr.tokens());
    connect(&mgr, &FinnThemeManager::themeChanged, this, apply);

    const QString ico_name = app_id.isEmpty() ? QString() : app_id;
    if (!ico_name.isEmpty()) {
        const QIcon app_ico = mgr.icon(ico_name);
        if (!app_ico.isNull()) setWindowIcon(app_ico);
    }
}

}  // namespace finn::ui
