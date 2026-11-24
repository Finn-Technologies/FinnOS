// Live theme manager, token broadcaster, settings mutator, and cached HiDPI SVG renderer.

#ifndef FINN_UI_FINN_THEME_HPP
#define FINN_UI_FINN_THEME_HPP

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ui/finn/tokens.hpp"

class QFileSystemWatcher;
class QPainter;
class QTimer;

namespace finn::ui {

struct AvatarPreset {
    QString id;
    QString name;
    QColor top_color;
    QColor bottom_color;
};

class FinnThemeManager final : public QObject {
    Q_OBJECT

public:
    static FinnThemeManager& instance();

    const FinnTokens& tokens() const { return tokens_; }
    const finn::sys::SettingsData& settings() const { return settings_; }

    bool isDark() const { return tokens_.dark; }
    bool reducedMotion() const { return tokens_.reduced_motion; }
    QString wallpaperPath() const { return QString::fromStdString(settings_.wallpaper); }
    QString lockWallpaperPath() const { return QString::fromStdString(settings_.lock_wallpaper); }
    QString avatarColor() const { return QString::fromStdString(settings_.avatar_color); }
    QString formFactorSetting() const { return QString::fromStdString(settings_.form_factor); }
    QString keyboardLayout() const { return QString::fromStdString(settings_.kb_layout); }
    QString keyboardVariant() const { return QString::fromStdString(settings_.kb_variant); }
    QString alertSound() const { return QString::fromStdString(settings_.alert_sound); }
    int alertVolume() const { return settings_.alert_volume; }
    bool startupSound() const { return settings_.startup_sound; }
    bool uiSounds() const { return settings_.ui_sounds; }
    bool volumeFeedback() const { return settings_.volume_feedback; }
    static const QVector<AvatarPreset>& avatarPresets();
    QStringList dockPins() const;

    QPixmap avatarPixmap(const QSize& logical_size, qreal dpr = 1.0,
                         const QString& color_override = {}) const;
    QPixmap iconPixmap(const QString& name_or_path, const QSize& logical_size,
                       const QColor& tint = QColor(), qreal dpr = 1.0) const;
    QPixmap pointerPixmap(const QString& name, int size = 28, qreal dpr = 1.0) const;
    QIcon icon(const QString& name_or_path, const QColor& tint = QColor()) const;
    void paintIcon(QPainter* painter, const QRectF& rect,
                   const QString& name_or_path,
                   const QColor& tint = QColor()) const;

    bool setDark(bool dark);
    bool setReducedMotion(bool reduced);
    bool setHighContrast(bool high_contrast);
    bool setColorFilter(const QString& filter);
    bool setScreenReader(bool enabled);
    bool setStickyKeys(bool enabled);
    bool setFilterKeys(bool enabled);
    bool setCursorSize(int size_px);
    bool setTextScale(double text_scale);
    bool setAutoLockMinutes(int minutes);
    bool setLockOnSleep(bool enabled);
    bool setAccent(const QString& hex_or_empty);
    bool setAvatarColor(const QString& color_id);
    bool setWallpaper(const QString& path);
    bool setLockWallpaper(const QString& path);
    bool setFormFactor(const QString& form_factor);
    bool setKeyboardLayout(const QString& layout, const QString& variant = {});
    bool setAlertSound(const QString& name);
    bool setAlertVolume(int volume_percent);
    bool setStartupSound(bool enabled);
    bool setUiSounds(bool enabled);
    bool setVolumeFeedback(bool enabled);
    bool setDockPins(const QStringList& pins);
    bool setScale(double scale);
    void reload();

signals:
    void themeChanged(const finn::ui::FinnTokens& tokens);

private:
    explicit FinnThemeManager(QObject* parent = nullptr);
    void setupWatcher();
    bool saveUpdated(const finn::sys::SettingsData& next);

    finn::sys::SettingsData settings_;
    FinnTokens tokens_;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* reload_timer_ = nullptr;
};

}  // namespace finn::ui

#endif
