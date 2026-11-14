// Custom FinnOS QPlatformTheme plugin: binds all Qt6 apps to FinnOS fonts,
// color tokens, colorScheme, motionPreference, icon engine, and live settings reload.

#ifndef FINN_QTPLUGIN_PLATFORM_THEME_HPP
#define FINN_QTPLUGIN_PLATFORM_THEME_HPP

#include <qpa/qplatformtheme.h>
#include <qpa/qplatformthemeplugin.h>

#include <QFont>
#include <QObject>
#include <QPalette>
#include <memory>

#include "ui/finn/tokens.hpp"

class QFileSystemWatcher;
class QTimer;

namespace finn::qtplugin {

class FinnPlatformTheme final : public QPlatformTheme {
public:
    FinnPlatformTheme();
    ~FinnPlatformTheme() override;

    Qt::ColorScheme colorScheme() const override;
    Qt::MotionPreference motionPreference() const override;
    const QPalette* palette(Palette type = SystemPalette) const override;
    const QFont* font(Font type = SystemFont) const override;
    QVariant themeHint(ThemeHint hint) const override;
    QIconEngine* createIconEngine(const QString& iconName) const override;

    void reload_settings();

private:
    void rebuild_palettes_and_fonts();
    void attach_watcher();

    finn::ui::FinnTokens tokens_;
    QPalette system_palette_;
    QPalette menu_palette_;
    QPalette tooltip_palette_;
    QPalette button_palette_;

    QFont system_font_;
    QFont menu_font_;
    QFont small_font_;
    QFont title_font_;
    QFont fixed_font_;

    QObject* watcher_owner_ = nullptr;
};

class FinnPlatformThemePlugin final : public QPlatformThemePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformThemeFactoryInterface_iid FILE "finnplatformtheme.json")

public:
    QPlatformTheme* create(const QString& key, const QStringList& paramList) override;
};

}  // namespace finn::qtplugin

#endif
