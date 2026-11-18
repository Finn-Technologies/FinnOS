// Implements the FinnOS QPlatformTheme plugin and FinnSvgIconEngine.

#include "qtplugin/finn_platform_theme.hpp"

#include <qpa/qwindowsysteminterface.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTimer>
#include <QVariant>

namespace finn::qtplugin {
namespace {

class FinnSvgIconEngine final : public QIconEngine {
public:
    explicit FinnSvgIconEngine(QString path, bool monochrome)
        : path_(std::move(path)), monochrome_(monochrome) {}

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override {
        return size;
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State) override {
        if (rect.isEmpty()) return;
        const qreal dpr =
            painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QSize px_size(
            std::max(1, qRound(rect.width() * dpr)),
            std::max(1, qRound(rect.height() * dpr)));

        QImage img(px_size, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        QPainter img_painter(&img);
        img_painter.setRenderHint(QPainter::Antialiasing, true);
        img_painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (path_.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
            QSvgRenderer renderer(path_);
            renderer.render(&img_painter, QRectF(0, 0, px_size.width(), px_size.height()));
        } else {
            QImage src(path_);
            if (!src.isNull()) {
                img_painter.drawImage(
                    QRectF(0, 0, px_size.width(), px_size.height()), src);
            }
        }
        if (monochrome_) {
            const auto t = finn::ui::load_tokens();
            QColor tint = t.ink;
            if (mode == QIcon::Disabled) tint = t.mute;
            img_painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            img_painter.fillRect(img.rect(), tint);
        } else if (mode == QIcon::Disabled) {
            img_painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            img_painter.fillRect(img.rect(), QColor(0, 0, 0, 110));
        }
        img_painter.end();

        img.setDevicePixelRatio(dpr);
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(rect, img);
        painter->restore();
    }

    QIconEngine* clone() const override {
        return new FinnSvgIconEngine(path_, monochrome_);
    }

    QString key() const override { return QStringLiteral("FinnSvgIconEngine"); }

private:
    QString path_;
    bool monochrome_ = false;
};

class WatcherHelper final : public QObject {
public:
    explicit WatcherHelper(FinnPlatformTheme* theme)
        : QObject(nullptr), theme_(theme) {
        QTimer::singleShot(0, this, [this]() { setup(); });
    }

private:
    void setup() {
        auto* watcher = new QFileSystemWatcher(this);
        auto* debounce = new QTimer(this);
        debounce->setSingleShot(true);
        debounce->setInterval(80);
        connect(debounce, &QTimer::timeout, this,
                [this]() { theme_->reload_settings(); });

        auto watch_if_exists = [watcher](const QString& p) {
            if (QFileInfo::exists(p)) watcher->addPath(p);
        };
        const QString sys_settings = QString::fromUtf8(kSettingsPath);
        watch_if_exists(sys_settings);
        watch_if_exists(QFileInfo(sys_settings).absolutePath());

        const QString cfg_base =
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (!cfg_base.isEmpty()) {
            const QString user_dir = cfg_base + QStringLiteral("/finn");
            QDir().mkpath(user_dir);
            watch_if_exists(user_dir);
            watch_if_exists(user_dir + QStringLiteral("/settings"));
        }

        connect(watcher, &QFileSystemWatcher::fileChanged, this,
                [watcher, debounce](const QString& path) {
                    if (QFileInfo::exists(path) && !watcher->files().contains(path)) {
                        watcher->addPath(path);
                    }
                    debounce->start();
                });
        connect(watcher, &QFileSystemWatcher::directoryChanged, this,
                [watcher, debounce](const QString& dir) {
                    const QString settings_file = dir + QStringLiteral("/settings");
                    if (QFileInfo::exists(settings_file) &&
                        !watcher->files().contains(settings_file)) {
                        watcher->addPath(settings_file);
                    }
                    debounce->start();
                });
    }

    FinnPlatformTheme* theme_;
};

}  // namespace

FinnPlatformTheme::FinnPlatformTheme() {
    rebuild_palettes_and_fonts();
    attach_watcher();
}

FinnPlatformTheme::~FinnPlatformTheme() {
    delete watcher_owner_;
    watcher_owner_ = nullptr;
}

void FinnPlatformTheme::attach_watcher() {
    if (!QCoreApplication::instance() || watcher_owner_) return;
    watcher_owner_ = new WatcherHelper(this);
}

void FinnPlatformTheme::rebuild_palettes_and_fonts() {
    tokens_ = finn::ui::load_tokens();
    finn::ui::set_active_text_scale(tokens_.text_scale);

    system_palette_ = finn::ui::build_finn_palette(tokens_);
    menu_palette_ = system_palette_;
    menu_palette_.setColor(QPalette::Window, tokens_.tile);
    menu_palette_.setColor(QPalette::Base, tokens_.tile);
    menu_palette_.setColor(QPalette::Button, tokens_.tile);

    tooltip_palette_ = system_palette_;
    tooltip_palette_.setColor(QPalette::ToolTipBase, tokens_.tile);
    tooltip_palette_.setColor(QPalette::ToolTipText, tokens_.ink);

    button_palette_ = system_palette_;
    button_palette_.setColor(QPalette::Button, tokens_.surface_input);
    button_palette_.setColor(QPalette::ButtonText, tokens_.ink);

    system_font_ = finn::ui::finn_font(10.5, QFont::Normal);
    menu_font_ = finn::ui::finn_font(10.0, QFont::Medium);
    small_font_ = finn::ui::finn_font(9.0, QFont::Normal);
    title_font_ = finn::ui::finn_font(10.5, QFont::DemiBold);
    fixed_font_ = finn::ui::finn_mono_font(10.0, QFont::Normal);
}

void FinnPlatformTheme::reload_settings() {
    const finn::ui::FinnTokens next = finn::ui::load_tokens();
    const bool changed = (next.dark != tokens_.dark ||
                          next.reduced_motion != tokens_.reduced_motion ||
                          next.high_contrast != tokens_.high_contrast ||
                          next.color_filter != tokens_.color_filter ||
                          next.cursor_size != tokens_.cursor_size ||
                          std::abs(next.text_scale - tokens_.text_scale) > 1e-3 ||
                          next.accent != tokens_.accent ||
                          std::abs(next.scale - tokens_.scale) > 1e-3);
    if (!changed) return;
    rebuild_palettes_and_fonts();
    QWindowSystemInterface::handleThemeChange();
}

Qt::ColorScheme FinnPlatformTheme::colorScheme() const {
    return tokens_.dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light;
}

Qt::MotionPreference FinnPlatformTheme::motionPreference() const {
    return tokens_.reduced_motion ? Qt::MotionPreference::ReducedMotion
                                  : Qt::MotionPreference::NoPreference;
}

const QPalette* FinnPlatformTheme::palette(Palette type) const {
    switch (type) {
        case MenuPalette:
        case MenuBarPalette:
            return &menu_palette_;
        case ToolTipPalette:
            return &tooltip_palette_;
        case ButtonPalette:
        case ToolButtonPalette:
            return &button_palette_;
        default:
            return &system_palette_;
    }
}

const QFont* FinnPlatformTheme::font(Font type) const {
    switch (type) {
        case MenuFont:
        case MenuItemFont:
        case MenuBarFont:
            return &menu_font_;
        case SmallFont:
        case MiniFont:
        case TipLabelFont:
        case StatusBarFont:
            return &small_font_;
        case TitleBarFont:
        case MdiSubWindowTitleFont:
        case GroupBoxTitleFont:
        case HeaderViewFont:
            return &title_font_;
        case FixedFont:
            return &fixed_font_;
        default:
            return &system_font_;
    }
}

QVariant FinnPlatformTheme::themeHint(ThemeHint hint) const {
    switch (hint) {
        case StyleNames:
            return QStringList{QStringLiteral("finn"), QStringLiteral("Fusion")};
        case SystemIconThemeName:
            return QStringLiteral("finn");
        case SystemIconFallbackThemeName:
            return QStringLiteral("hicolor");
        case IconThemeSearchPaths:
            return QStringList{
                QString::fromUtf8(kIconDir),
                QStringLiteral("/usr/share/icons"),
                QStringLiteral("/usr/local/share/icons"),
            };
        case ShowIconsInMenus:
            return true;
        case UnderlineShortcut:
            return false;
        case DropShadow:
            return true;
        case TabFocusBehavior:
            return QVariant::fromValue(static_cast<int>(Qt::TabFocusAllControls));
        case ShowShortcutsInContextMenus:
            return true;
        case WheelScrollLines:
            return 3;
        case MouseDoubleClickDistance:
            return 5;
        case StartDragDistance:
            return 8;
        case CursorFlashTime:
            return tokens_.reduced_motion ? 0 : 1000;
        case ItemViewActivateItemOnSingleClick:
            return false;
        case DialogButtonBoxLayout:
            return 0;  // Mac/modern trailing primary order cleanly aligned
        case DialogButtonBoxButtonsHaveIcons:
            return false;
        default:
            return QPlatformTheme::themeHint(hint);
    }
}

QIconEngine* FinnPlatformTheme::createIconEngine(const QString& iconName) const {
    const QString path = finn::ui::resolve_icon_path(iconName);
    if (path.isEmpty()) return nullptr;
    const bool monochrome = path.contains(QStringLiteral("/shell/"));
    return new FinnSvgIconEngine(path, monochrome);
}

QPlatformTheme* FinnPlatformThemePlugin::create(const QString& key,
                                                const QStringList&) {
    if (key.compare(QStringLiteral("finn"), Qt::CaseInsensitive) == 0 ||
        key.compare(QStringLiteral("finnos"), Qt::CaseInsensitive) == 0) {
        return new FinnPlatformTheme();
    }
    return nullptr;
}

}  // namespace finn::qtplugin
