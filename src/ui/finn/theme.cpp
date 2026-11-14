// Implements FinnThemeManager: live file watcher, cached SVG renderer, settings writer.

#include "ui/finn/theme.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QFileSystemWatcher>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace finn::ui {

namespace {

// A monochrome glyph (one paint colour, or none beyond `none`) is recoloured
// with the theme ink; a multi-colour app icon keeps its own colours. The
// installed icon directory is flat, so the answer comes from the file's
// content, not from the folder it was copied from.
bool svg_is_monochrome(const QString& path) {
    static QHash<QString, bool> memo;
    if (const auto it = memo.constFind(path); it != memo.constEnd()) return *it;
    QFile f(path);
    bool mono = false;
    if (f.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(f.readAll());
        static const QRegularExpression re(
            QStringLiteral("(?:fill|stroke|stop-color)=\"([^\"]+)\""));
        QSet<QString> colours;
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QString v = it.next().captured(1).toLower();
            if (v != QLatin1String("none") && !v.startsWith(QLatin1String("url(")))
                colours.insert(v);
        }
        mono = colours.size() <= 1 && !text.contains(QLatin1String("<linearGradient")) &&
               !text.contains(QLatin1String("<radialGradient"));
    }
    memo.insert(path, mono);
    return mono;
}

bool svg_uses_current_color(const QString& path) {
    static QHash<QString, bool> memo;
    if (const auto it = memo.constFind(path); it != memo.constEnd()) return *it;
    QFile f(path);
    bool uses = false;
    if (f.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(f.readAll());
        uses = text.contains(QLatin1String("currentColor"), Qt::CaseInsensitive);
    }
    memo.insert(path, uses);
    return uses;
}

class FinnThemeIconEngine final : public QIconEngine {
   public:
    FinnThemeIconEngine(QString path, QColor tint)
        : path_(std::move(path)), tint_(tint) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State /*state*/) override {
        if (!painter || rect.isEmpty()) return;
        const qreal dpr =
            painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap pm =
            FinnThemeManager::instance().iconPixmap(path_, rect.size(), tint_, dpr);
        if (pm.isNull()) return;
        painter->save();
        if (mode == QIcon::Disabled) {
            painter->setOpacity(0.4);
        }
        painter->drawPixmap(rect, pm);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode,
                   QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode,
                         QIcon::State /*state*/, qreal scale) override {
        QPixmap pm = FinnThemeManager::instance().iconPixmap(path_, size, tint_, scale);
        if (pm.isNull() || mode != QIcon::Disabled) return pm;
        QImage img(pm.size(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setOpacity(0.4);
        p.drawPixmap(0, 0, pm);
        p.end();
        QPixmap out = QPixmap::fromImage(img);
        out.setDevicePixelRatio(pm.devicePixelRatio());
        return out;
    }

    QIconEngine* clone() const override {
        return new FinnThemeIconEngine(path_, tint_);
    }

   private:
    QString path_;
    QColor tint_;
};

}  // namespace

FinnThemeManager& FinnThemeManager::instance() {
    static FinnThemeManager manager;
    return manager;
}

FinnThemeManager::FinnThemeManager(QObject* parent) : QObject(parent) {
    QPixmapCache::setCacheLimit(32 * 1024);
    settings_ = finn::sys::load_merged_settings();
    tokens_ = tokens_from_settings(settings_);
    if (QCoreApplication::instance()) {
        setupWatcher();
    }
}

void FinnThemeManager::setupWatcher() {
    if (watcher_) return;
    watcher_ = new QFileSystemWatcher(this);
    reload_timer_ = new QTimer(this);
    reload_timer_->setSingleShot(true);
    reload_timer_->setInterval(50);
    connect(reload_timer_, &QTimer::timeout, this, &FinnThemeManager::reload);

    auto add_watch = [this](const QString& path) {
        if (QFileInfo::exists(path) && !watcher_->files().contains(path) &&
            !watcher_->directories().contains(path)) {
            watcher_->addPath(path);
        }
    };

    const QString sys_settings = QString::fromUtf8(kSettingsPath);
    add_watch(sys_settings);
    add_watch(QFileInfo(sys_settings).absolutePath());
    add_watch(QStringLiteral("/etc/finn"));

    const QString cfg_base =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (!cfg_base.isEmpty()) {
        const QString user_dir = cfg_base + QStringLiteral("/finn");
        QDir().mkpath(user_dir);
        add_watch(user_dir);
        add_watch(user_dir + QStringLiteral("/settings"));
    }

    connect(watcher_, &QFileSystemWatcher::fileChanged, this,
            [add_watch, this](const QString& path) {
                add_watch(path);
                reload_timer_->start();
            });
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            [add_watch, this](const QString& dir) {
                add_watch(dir + QStringLiteral("/settings"));
                reload_timer_->start();
            });
}

void FinnThemeManager::reload() {
    if (QCoreApplication::instance() && !watcher_) {
        setupWatcher();
    }
    const auto next_settings = finn::sys::load_merged_settings();
    const bool same =
        next_settings.dark == settings_.dark &&
        next_settings.reduced_motion == settings_.reduced_motion &&
        next_settings.high_contrast == settings_.high_contrast &&
        next_settings.color_filter == settings_.color_filter &&
        next_settings.screen_reader == settings_.screen_reader &&
        next_settings.sticky_keys == settings_.sticky_keys &&
        next_settings.filter_keys == settings_.filter_keys &&
        next_settings.cursor_size == settings_.cursor_size &&
        std::abs(next_settings.text_scale - settings_.text_scale) < 1e-3 &&
        next_settings.auto_lock_minutes == settings_.auto_lock_minutes &&
        next_settings.lock_on_sleep == settings_.lock_on_sleep &&
        next_settings.accent == settings_.accent &&
        next_settings.wallpaper == settings_.wallpaper &&
        std::abs(next_settings.scale - settings_.scale) < 1e-3 &&
        next_settings.dock_pins == settings_.dock_pins;
    if (same) return;

    settings_ = next_settings;
    tokens_ = tokens_from_settings(settings_);
    QPixmapCache::clear();
    emit themeChanged(tokens_);
}

QStringList FinnThemeManager::dockPins() const {
    QStringList out;
    for (const auto& id : settings_.dock_pins) {
        out << QString::fromStdString(id);
    }
    return out;
}

bool FinnThemeManager::saveUpdated(const finn::sys::SettingsData& next) {
    if (!finn::sys::save_settings(next)) return false;
    settings_ = next;
    tokens_ = tokens_from_settings(settings_);
    QPixmapCache::clear();
    emit themeChanged(tokens_);
    return true;
}

bool FinnThemeManager::setDark(bool dark) {
    auto next = settings_;
    next.dark = dark;
    return saveUpdated(next);
}

bool FinnThemeManager::setReducedMotion(bool reduced) {
    auto next = settings_;
    next.reduced_motion = reduced;
    return saveUpdated(next);
}

bool FinnThemeManager::setHighContrast(bool high_contrast) {
    auto next = settings_;
    next.high_contrast = high_contrast;
    return saveUpdated(next);
}

bool FinnThemeManager::setColorFilter(const QString& filter) {
    auto next = settings_;
    next.color_filter = filter.trimmed().toStdString();
    return saveUpdated(next);
}

bool FinnThemeManager::setScreenReader(bool enabled) {
    auto next = settings_;
    next.screen_reader = enabled;
    return saveUpdated(next);
}

bool FinnThemeManager::setStickyKeys(bool enabled) {
    auto next = settings_;
    next.sticky_keys = enabled;
    return saveUpdated(next);
}

bool FinnThemeManager::setFilterKeys(bool enabled) {
    auto next = settings_;
    next.filter_keys = enabled;
    return saveUpdated(next);
}

bool FinnThemeManager::setCursorSize(int size_px) {
    auto next = settings_;
    next.cursor_size = std::clamp(size_px, 16, 96);
    return saveUpdated(next);
}

bool FinnThemeManager::setTextScale(double text_scale) {
    auto next = settings_;
    next.text_scale = std::clamp(text_scale, 0.75, 2.5);
    return saveUpdated(next);
}

bool FinnThemeManager::setAutoLockMinutes(int minutes) {
    auto next = settings_;
    next.auto_lock_minutes = std::clamp(minutes, 0, 120);
    return saveUpdated(next);
}

bool FinnThemeManager::setLockOnSleep(bool enabled) {
    auto next = settings_;
    next.lock_on_sleep = enabled;
    return saveUpdated(next);
}

bool FinnThemeManager::setAccent(const QString& hex_or_empty) {
    auto next = settings_;
    next.accent = hex_or_empty.trimmed().toStdString();
    if (next.accent.empty()) {
        next.accent = "#007BFF";
    }
    return saveUpdated(next);
}

bool FinnThemeManager::setWallpaper(const QString& path) {
    auto next = settings_;
    next.wallpaper = path.toStdString();
    return saveUpdated(next);
}

bool FinnThemeManager::setDockPins(const QStringList& pins) {
    auto next = settings_;
    next.dock_pins.clear();
    for (const auto& pin : pins) {
        if (!pin.trimmed().isEmpty()) {
            next.dock_pins.push_back(pin.trimmed().toStdString());
        }
    }
    return saveUpdated(next);
}

bool FinnThemeManager::setScale(double scale) {
    auto next = settings_;
    next.scale = std::clamp(scale, 0.75, 3.0);
    return saveUpdated(next);
}

QPixmap FinnThemeManager::iconPixmap(const QString& name_or_path,
                                     const QSize& logical_size,
                                     const QColor& tint, qreal dpr) const {
    if (name_or_path.isEmpty() || logical_size.isEmpty()) return {};

    const qreal effective_dpr = std::max<qreal>(1.0, dpr);
    const int pw = std::max(1, qRound(logical_size.width() * effective_dpr));
    const int ph = std::max(1, qRound(logical_size.height() * effective_dpr));
    const QString path = resolve_icon_path(name_or_path);
    if (path.isEmpty()) return {};

    // A multi-colour SVG without currentColor is never flattened, even when a
    // tint is passed; monochrome glyphs and two-tone currentColor shell icons
    // adapt dynamically to Light/Dark tokens_.ink or explicit tint.
    const bool is_svg = path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive);
    const bool mono = is_svg && svg_is_monochrome(path);
    const bool has_current_color = is_svg && svg_uses_current_color(path);
    QColor resolved_tint;
    if (!is_svg || mono || has_current_color) {
        resolved_tint =
            tint.isValid() ? tint : ((mono || has_current_color) ? tokens_.ink : QColor());
    }

    const QString cache_key = QStringLiteral("finn_ico|%1|%2x%3|%4|%5|%6")
                                  .arg(path)
                                  .arg(pw)
                                  .arg(ph)
                                  .arg(tokens_.dark ? 1 : 0)
                                  .arg(resolved_tint.isValid()
                                           ? QString::number(resolved_tint.rgba(), 16)
                                           : QStringLiteral("raw"))
                                  .arg(QString::number(effective_dpr, 'f', 2));
    QPixmap cached;
    if (QPixmapCache::find(cache_key, &cached)) {
        return cached;
    }

    QImage img(pw, ph, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (is_svg) {
        if (has_current_color && resolved_tint.isValid()) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                QString svg_text = QString::fromUtf8(f.readAll());
                svg_text.replace(QStringLiteral("currentColor"),
                                 resolved_tint.name(QColor::HexRgb),
                                 Qt::CaseInsensitive);
                QSvgRenderer svg(svg_text.toUtf8());
                svg.render(&p, QRectF(0, 0, pw, ph));
            }
        } else {
            QSvgRenderer svg(path);
            svg.render(&p, QRectF(0, 0, pw, ph));
        }
    } else {
        QImage raw(path);
        if (!raw.isNull()) {
            const QImage scaled =
                raw.scaled(pw, ph, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPointF offset((pw - scaled.width()) / 2.0,
                                 (ph - scaled.height()) / 2.0);
            p.drawImage(offset, scaled);
        }
    }
    if (resolved_tint.isValid()) {
        if (!is_svg || mono) {
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(img.rect(), resolved_tint);
        } else if (has_current_color && resolved_tint.alpha() < 255) {
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.fillRect(img.rect(), QColor(0, 0, 0, resolved_tint.alpha()));
        }
    }
    p.end();

    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(effective_dpr);
    QPixmapCache::insert(cache_key, pm);
    return pm;
}

QPixmap FinnThemeManager::pointerPixmap(const QString& name, int size, qreal dpr) const {
    const QString path = resolve_pointer_path(name);
    if (path.isEmpty()) return {};
    return iconPixmap(path, QSize(size, size), QColor(), dpr);
}

QIcon FinnThemeManager::icon(const QString& name_or_path, const QColor& tint) const {
    const QString path = resolve_icon_path(name_or_path);
    if (path.isEmpty()) {
        return QIcon::fromTheme(name_or_path);
    }
    return QIcon(new FinnThemeIconEngine(path, tint));
}

void FinnThemeManager::paintIcon(QPainter* painter, const QRectF& rect,
                                 const QString& name_or_path,
                                 const QColor& tint) const {
    if (!painter || rect.isEmpty()) return;
    const qreal dpr =
        painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QSize logical_sz(std::max(1, qRound(rect.width())),
                           std::max(1, qRound(rect.height())));
    QPixmap pm = iconPixmap(name_or_path, logical_sz, tint, dpr);
    if (pm.isNull()) {
        const QIcon fallback = QIcon::fromTheme(name_or_path);
        if (!fallback.isNull()) {
            fallback.paint(painter, rect.toRect());
        }
        return;
    }
    painter->drawPixmap(rect.toRect(), pm);
}

}  // namespace finn::ui
