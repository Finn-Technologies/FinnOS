#include "ui/qml/finn_qml_types.hpp"

#include "common/paths.hpp"
#include "ui/finn/components.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QUrlQuery>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace finn::ui::qml {
namespace {

QString resolve_existing_asset_file(const QString& preferred_path,
                                    const QString& rel_fallback) {
    if (!preferred_path.isEmpty() && QFileInfo::exists(preferred_path)) {
        return preferred_path;
    }
    const QString installed = QString::fromLatin1(finn::kDefaultWallpaperPath);
    if (rel_fallback == QLatin1String("wallpaper/default.webp") &&
        QFileInfo::exists(installed)) {
        return installed;
    }
    // Development repo fallbacks relative to executable or CWD
    const QStringList roots = {
        QDir::currentPath() + QStringLiteral("/assets/"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../assets/"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../assets/"),
        QStringLiteral("/usr/share/finn/"),
    };
    for (const QString& root : roots) {
        const QString candidate = root + rel_fallback;
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }
    return {};
}

// Fast 3-pass separable sliding-window box blur approximating a true Gaussian
// kernel (Figma BACKGROUND_BLUR radius=35) at 1/4 resolution (~0.4ms on ARM64).
void box_blur_horizontal(const QRgb* src, QRgb* dst, int w, int h, int r) {
    const int diam = 2 * r + 1;
    for (int y = 0; y < h; ++y) {
        const QRgb* row_in = src + y * w;
        QRgb* row_out = dst + y * w;
        int sum_r = 0, sum_g = 0, sum_b = 0;
        for (int i = -r; i <= r; ++i) {
            const QRgb px = row_in[std::clamp(i, 0, w - 1)];
            sum_r += qRed(px);
            sum_g += qGreen(px);
            sum_b += qBlue(px);
        }
        for (int x = 0; x < w; ++x) {
            row_out[x] = qRgb(sum_r / diam, sum_g / diam, sum_b / diam);
            const QRgb out_px = row_in[std::clamp(x - r, 0, w - 1)];
            const QRgb in_px = row_in[std::clamp(x + r + 1, 0, w - 1)];
            sum_r += qRed(in_px) - qRed(out_px);
            sum_g += qGreen(in_px) - qGreen(out_px);
            sum_b += qBlue(in_px) - qBlue(out_px);
        }
    }
}

void box_blur_vertical(const QRgb* src, QRgb* dst, int w, int h, int r) {
    const int diam = 2 * r + 1;
    for (int x = 0; x < w; ++x) {
        int sum_r = 0, sum_g = 0, sum_b = 0;
        for (int i = -r; i <= r; ++i) {
            const QRgb px = src[std::clamp(i, 0, h - 1) * w + x];
            sum_r += qRed(px);
            sum_g += qGreen(px);
            sum_b += qBlue(px);
        }
        for (int y = 0; y < h; ++y) {
            dst[y * w + x] = qRgb(sum_r / diam, sum_g / diam, sum_b / diam);
            const QRgb out_px = src[std::clamp(y - r, 0, h - 1) * w + x];
            const QRgb in_px = src[std::clamp(y + r + 1, 0, h - 1) * w + x];
            sum_r += qRed(in_px) - qRed(out_px);
            sum_g += qGreen(in_px) - qGreen(out_px);
            sum_b += qBlue(in_px) - qBlue(out_px);
        }
    }
}

const QImage& blurred_wallpaper_for_size(const QSize& screen_size) {
    static QString cached_path;
    static QSize cached_size;
    static QImage cached_blur;

    if (screen_size.width() <= 0 || screen_size.height() <= 0) {
        return cached_blur;
    }

    const QString wp_path = resolve_existing_asset_file(
        FinnThemeManager::instance().wallpaperPath(),
        QStringLiteral("wallpaper/default.webp"));
    if (wp_path.isEmpty()) return cached_blur;

    if (wp_path == cached_path && screen_size == cached_size && !cached_blur.isNull()) {
        return cached_blur;
    }

    QImage raw;
    if (!raw.load(wp_path)) return cached_blur;

    // Cover-crop to screen_size matching Image.PreserveAspectCrop
    const QImage cover = raw.scaled(screen_size, Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
    const int cx = std::max(0, (cover.width() - screen_size.width()) / 2);
    const int cy = std::max(0, (cover.height() - screen_size.height()) / 2);
    const QImage cropped = cover.copy(cx, cy, screen_size.width(), screen_size.height());

    // Downsample 4x, run 3-pass separable Gaussian approximation (r=9 at 1/4 scale == r=36 at 1x)
    const int sw = std::max(16, screen_size.width() / 4);
    const int sh = std::max(16, screen_size.height() / 4);
    QImage small = cropped.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGB32);
    QImage scratch(sw, sh, QImage::Format_RGB32);
    const int rad = std::clamp(finn::ui::blur::blur_glass / 4, 4, 14);

    for (int pass = 0; pass < 3; ++pass) {
        box_blur_horizontal(reinterpret_cast<const QRgb*>(small.constBits()),
                            reinterpret_cast<QRgb*>(scratch.bits()), sw, sh, rad);
        box_blur_vertical(reinterpret_cast<const QRgb*>(scratch.constBits()),
                          reinterpret_cast<QRgb*>(small.bits()), sw, sh, rad);
    }

    cached_blur = small.scaled(screen_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    cached_path = wp_path;
    cached_size = screen_size;
    return cached_blur;
}

// Resolve a floating Wayland window's live desktop coordinates & output size
// from /run/finn/state/windows.tsv so real-time sidebar blur tracks window moves.
struct LiveWindowDesktopPos {
    QSize output_size{1280, 800};
    QPointF window_pos{0.0, 0.0};
    bool found = false;
};

LiveWindowDesktopPos resolve_live_window_pos(const QQuickWindow* win) {
    LiveWindowDesktopPos out;
    if (!win) return out;
    if (win->screen()) {
        const QSize ss = win->screen()->size();
        if (ss.width() > 0 && ss.height() > 0) out.output_size = ss;
    }
    const QString title = win->title();
    QFile f(QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/windows.tsv"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith(QLatin1String("#OUT\t"))) {
                const QStringList p = line.split(QLatin1Char('\t'));
                if (p.size() >= 3) {
                    const int ow = p[1].toInt();
                    const int oh = p[2].toInt();
                    if (ow > 0 && oh > 0) out.output_size = QSize(ow, oh);
                }
                continue;
            }
            const QStringList cols = line.split(QLatin1Char('\t'));
            if (cols.size() >= 7 && !title.isEmpty() && cols[2] == title) {
                out.window_pos = QPointF(cols[3].toDouble(), cols[4].toDouble());
                out.found = true;
                break;
            }
        }
    }
    if (!out.found) {
        // Standalone / host fallback: center window inside output_size
        out.window_pos = QPointF(
            std::max<qreal>(0.0, (out.output_size.width() - win->width()) * 0.5),
            std::max<qreal>(0.0, (out.output_size.height() - win->height()) * 0.5));
    }
    return out;
}

QPainterPath make_squircle_or_capsule_path(const QRectF& r, qreal rad) {
    const qreal max_r = std::min(r.width(), r.height()) * 0.5;
    if (rad >= max_r * 0.95) {
        QPainterPath path;
        path.addRoundedRect(r, max_r, max_r);
        return path;
    }
    return finn_rounded_path(r, rad);
}

}  // namespace

// ---- FinnThemeBridge ----

FinnThemeBridge::FinnThemeBridge(QObject* parent) : QObject(parent) {
    syncFromManager();
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, [this](const FinnTokens&) { syncFromManager(); });
}

void FinnThemeBridge::syncFromManager() {
    auto s = FinnThemeManager::instance().settings();
    if (has_dark_override_) {
        s.dark = dark_override_;
    }
    tokens_ = tokens_from_settings(s);
    finn::ui::set_reduced_motion_override(tokens_.reduced_motion);
    emit changed();
}

void FinnThemeBridge::setDark(bool dark) {
    has_dark_override_ = false;
    FinnThemeManager::instance().setDark(dark);
    syncFromManager();
}

void FinnThemeBridge::setReducedMotion(bool reduced) {
    FinnThemeManager::instance().setReducedMotion(reduced);
    syncFromManager();
}

void FinnThemeBridge::setHighContrast(bool hc) {
    FinnThemeManager::instance().setHighContrast(hc);
    syncFromManager();
}

QString FinnThemeBridge::fontFamily() const {
    return finn_font(12.0).family();
}

QString FinnThemeBridge::monoFontFamily() const {
    return finn_mono_font(10.0).family();
}

QString FinnThemeBridge::formFactorSetting() const {
    if (!runtime_ff_override_.isEmpty()) return runtime_ff_override_;
    return FinnThemeManager::instance().formFactorSetting();
}

void FinnThemeBridge::setFormFactorSetting(const QString& ff) {
    runtime_ff_override_.clear();
    FinnThemeManager::instance().setFormFactor(ff);
    syncFromManager();
}

QString FinnThemeBridge::avatarColor() const {
    return FinnThemeManager::instance().avatarColor();
}

void FinnThemeBridge::setAvatarColor(const QString& id) {
    FinnThemeManager::instance().setAvatarColor(id);
    syncFromManager();
}

QUrl FinnThemeBridge::wallpaperUrl() const {
    const QString resolved = resolve_existing_asset_file(
        FinnThemeManager::instance().wallpaperPath(),
        QStringLiteral("wallpaper/default.webp"));
    return resolved.isEmpty() ? QUrl() : QUrl::fromLocalFile(resolved);
}

QUrl FinnThemeBridge::lockWallpaperUrl() const {
    const QString lock_wp = FinnThemeManager::instance().lockWallpaperPath();
    if (!lock_wp.isEmpty() && QFileInfo::exists(lock_wp)) {
        return QUrl::fromLocalFile(lock_wp);
    }
    return wallpaperUrl();
}

QColor FinnThemeBridge::hairSoft() const {
    return QColor::fromRgba(tokens_.dark ? colors::hair_soft_dark
                                         : colors::hair_soft_light);
}

void FinnThemeBridge::setAccent(const QColor& c) {
    if (!c.isValid()) return;
    FinnThemeManager::instance().setAccent(c.name(QColor::HexRgb).toUpper());
    syncFromManager();
}

QColor FinnThemeBridge::glassPanel() const {
    QColor c = tokens_.glass;
    c.setAlpha(tokens_.high_contrast
                   ? 255
                   : static_cast<int>(std::lround(255.0 * (tokens_.dark ? 0.82 : weights::glass_panel))));
    return c;
}

QColor FinnThemeBridge::glassFooter() const {
    QColor c = tokens_.glass;
    c.setAlpha(tokens_.high_contrast
                   ? 255
                   : static_cast<int>(std::lround(255.0 * (tokens_.dark ? 0.62 : weights::glass_footer))));
    return c;
}

QColor FinnThemeBridge::notifGlass() const {
    QColor c = QColor::fromRgba(tokens_.dark ? colors::notif_glass_dark
                                             : colors::notif_glass_light);
    if (tokens_.high_contrast) c.setAlpha(255);
    return c;
}

QColor FinnThemeBridge::lockPill() const {
    QColor c = QColor::fromRgba(tokens_.dark ? colors::lock_pill_dark
                                             : colors::lock_pill_light);
    if (tokens_.high_contrast) c.setAlpha(255);
    return c;
}

QColor FinnThemeBridge::soundBadge() const {
    return QColor::fromRgba(tokens_.dark ? colors::sound_badge_dark
                                         : colors::sound_badge_light);
}

QString FinnThemeBridge::resolveFormFactor(int widthDp, int heightDp,
                                           bool tvHint) const {
    const std::string setting = formFactorSetting().toStdString();
    const AdaptiveFormFactor ff =
        resolve_adaptive_form_factor(widthDp, heightDp, setting, tvHint);
    return QString::fromLatin1(form_factor_id(ff).data(),
                               static_cast<qsizetype>(form_factor_id(ff).size()));
}

QVariantMap FinnThemeBridge::formFactorMetrics(const QString& formFactor,
                                               int widthDp,
                                               int heightDp) const {
    AdaptiveFormFactor ff = AdaptiveFormFactor::Desktop;
    if (!parse_form_factor_id(formFactor.toStdString(), &ff)) {
        ff = resolve_adaptive_form_factor(widthDp, heightDp,
                                          formFactorSetting().toStdString(), false);
    }
    const AdaptiveMetrics m = compute_adaptive_metrics(ff, widthDp, heightDp);
    QVariantMap out;
    out.insert(QStringLiteral("formFactor"),
               QString::fromLatin1(form_factor_id(m.form_factor).data(),
                                   static_cast<qsizetype>(form_factor_id(m.form_factor).size())));
    out.insert(QStringLiteral("displayName"),
               QString::fromLatin1(form_factor_display_name(m.form_factor).data(),
                                   static_cast<qsizetype>(form_factor_display_name(m.form_factor).size())));
    out.insert(QStringLiteral("typeScale"), m.type_scale);
    out.insert(QStringLiteral("minBodyPx"), m.min_body_px);
    out.insert(QStringLiteral("titlePx"), m.title_px);
    out.insert(QStringLiteral("displayClockPx"), m.display_clock_px);
    out.insert(QStringLiteral("pageMargin"), m.page_margin);
    out.insert(QStringLiteral("cardGap"), m.card_gap);
    out.insert(QStringLiteral("minHitTarget"), m.min_hit_target);
    out.insert(QStringLiteral("buttonHeight"), m.button_height);
    out.insert(QStringLiteral("inputHeight"), m.input_height);
    out.insert(QStringLiteral("navRowHeight"), m.nav_row_height);
    out.insert(QStringLiteral("toggleWidth"), m.toggle_width);
    out.insert(QStringLiteral("toggleHeight"), m.toggle_height);
    out.insert(QStringLiteral("topBarHeight"), m.top_bar_height);
    out.insert(QStringLiteral("bottomDockHeight"), m.bottom_dock_height);
    out.insert(QStringLiteral("bottomDockMargin"), m.bottom_dock_margin);
    out.insert(QStringLiteral("sidebarWidth"), m.sidebar_width);
    out.insert(QStringLiteral("navRailWidth"), m.nav_rail_width);
    out.insert(QStringLiteral("cornerRadiusWindow"), m.corner_radius_window);
    out.insert(QStringLiteral("cornerRadiusCard"), m.corner_radius_card);
    out.insert(QStringLiteral("cornerRadiusSheet"), m.corner_radius_sheet);
    out.insert(QStringLiteral("focusRingPx"), m.focus_ring_px);
    out.insert(QStringLiteral("edgeToEdgeWindows"), m.edge_to_edge_windows);
    out.insert(QStringLiteral("showWindowControls"), m.show_window_controls);
    out.insert(QStringLiteral("useBottomSheets"), m.use_bottom_sheets);
    out.insert(QStringLiteral("dpadFocusNavigation"), m.dpad_focus_navigation);
    out.insert(QStringLiteral("virtualDesktopCount"), m.virtual_desktop_count);
    return out;
}

QString FinnThemeBridge::iconUrl(const QString& name, const QColor& tint,
                                 int sizePx) const {
    if (name.isEmpty()) return {};
    if (name.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
        name.startsWith(QLatin1String("https://"), Qt::CaseInsensitive) ||
        name.startsWith(QLatin1String("file://"), Qt::CaseInsensitive) ||
        name.startsWith(QLatin1String("qrc:"))) {
        return name;
    }
    const QString encoded_id = name.startsWith(QLatin1Char('/'))
        ? (QStringLiteral("abs:") + QString::fromLatin1(QUrl::toPercentEncoding(name)))
        : name;
    QUrl url(QStringLiteral("image://finn-icon/") + encoded_id);
    QUrlQuery q;
    if (tint.isValid() && tint.alpha() > 0) {
        q.addQueryItem(QStringLiteral("color"), tint.name(QColor::HexArgb));
    }
    if (sizePx > 0) {
        q.addQueryItem(QStringLiteral("size"), QString::number(sizePx));
    }
    url.setQuery(q);
    return url.toString();
}

QColor FinnThemeBridge::withAlpha(const QColor& color, double alpha01) const {
    QColor c = color;
    c.setAlphaF(static_cast<float>(std::clamp(alpha01, 0.0, 1.0)));
    return c;
}

void FinnThemeBridge::reload() {
    FinnThemeManager::instance().reload();
    syncFromManager();
}

void FinnThemeBridge::setRuntimeFormFactorOverride(const QString& ff) {
    runtime_ff_override_ = ff.trimmed().toLower();
    emit changed();
}

void FinnThemeBridge::setRuntimeDarkOverride(bool enabled, bool dark) {
    has_dark_override_ = enabled;
    dark_override_ = dark;
    syncFromManager();
}

// ---- FinnSpringValue ----

FinnSpringValue::FinnSpringValue(QObject* parent) : QObject(parent) {
    timer_.setInterval(8);  // 120 Hz analytic stepping
    connect(&timer_, &QTimer::timeout, this, &FinnSpringValue::tick);
}

SpringParams FinnSpringValue::paramsForPreset(const QString& name) {
    const QString p = name.trimmed().toLower();
    if (p == QLatin1String("snappy")) return springs::kSnappy;
    if (p == QLatin1String("bouncy")) return springs::kBouncy;
    if (p == QLatin1String("gentle")) return springs::kGentle;
    if (p == QLatin1String("overscroll")) return springs::kOverscroll;
    return springs::kSmooth;
}

void FinnSpringValue::setPreset(const QString& name) {
    if (preset_ == name) return;
    preset_ = name;
    spring_.set_params(paramsForPreset(name));
    emit presetChanged();
}

void FinnSpringValue::setTarget(double target) {
    if (qFuzzyCompare(spring_.target() + 1.0, target + 1.0)) return;
    spring_.retarget(target);
    emit targetChanged();

    if (finn::ui::reduced_motion() || spring_.settled()) {
        const bool was_running = timer_.isActive();
        timer_.stop();
        spring_.set(target, 0.0);
        emit valueChanged();
        if (was_running) emit runningChanged();
        emit settled();
        return;
    }
    if (!timer_.isActive()) {
        clock_.restart();
        timer_.start();
        emit runningChanged();
    }
}

void FinnSpringValue::snap(double value) {
    const bool was_running = timer_.isActive();
    timer_.stop();
    spring_.set(value, 0.0);
    emit targetChanged();
    emit valueChanged();
    if (was_running) emit runningChanged();
}

void FinnSpringValue::impulse(double velocityDelta, double newTarget) {
    spring_.retarget(newTarget);
    spring_.add_velocity(velocityDelta);
    emit targetChanged();
    if (finn::ui::reduced_motion()) {
        snap(newTarget);
        emit settled();
        return;
    }
    if (!timer_.isActive()) {
        clock_.restart();
        timer_.start();
        emit runningChanged();
    }
}

void FinnSpringValue::tick() {
    const qint64 ns = clock_.nsecsElapsed();
    clock_.restart();
    const double dt = std::clamp(static_cast<double>(ns) * 1e-9, 0.001, 0.05);
    spring_.step(dt);
    emit valueChanged();
    if (spring_.settled()) {
        timer_.stop();
        emit runningChanged();
        emit settled();
    }
}

// ---- FinnSquircle ----

FinnSquircle::FinnSquircle(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setSmooth(true);
}

void FinnSquircle::setColor(const QColor& c) {
    if (color_ == c) return;
    color_ = c;
    update();
    emit visualChanged();
}

void FinnSquircle::setBorderColor(const QColor& c) {
    if (border_color_ == c) return;
    border_color_ = c;
    update();
    emit visualChanged();
}

void FinnSquircle::setBorderWidth(qreal w) {
    if (qFuzzyCompare(border_width_ + 1.0, w + 1.0)) return;
    border_width_ = std::max<qreal>(0.0, w);
    update();
    emit visualChanged();
}

void FinnSquircle::setRadius(qreal r) {
    if (qFuzzyCompare(radius_ + 1.0, r + 1.0)) return;
    radius_ = std::max<qreal>(0.0, r);
    update();
    emit visualChanged();
}

void FinnSquircle::setSpecularRim(bool on) {
    if (specular_rim_ == on) return;
    specular_rim_ = on;
    update();
    emit visualChanged();
}

void FinnSquircle::setShadow(bool on) {
    if (shadow_ == on) return;
    shadow_ = on;
    update();
    emit visualChanged();
}

void FinnSquircle::setShadowAlpha(int a) {
    const int clamped = std::clamp(a, 0, 255);
    if (shadow_alpha_ == clamped) return;
    shadow_alpha_ = clamped;
    update();
    emit visualChanged();
}

void FinnSquircle::setBackdropBlur(bool on) {
    if (backdrop_blur_ == on) return;
    backdrop_blur_ = on;
    update();
    if (on) {
        QTimer::singleShot(0, this, [this]() { update(); });
    }
    emit visualChanged();
}

void FinnSquircle::setSidebarWidth(qreal w) {
    const qreal clamped = std::max<qreal>(0.0, w);
    if (qFuzzyCompare(sidebar_width_ + 1.0, clamped + 1.0)) return;
    sidebar_width_ = clamped;
    update();
    emit visualChanged();
}

void FinnSquircle::setSidebarColor(const QColor& c) {
    if (sidebar_color_ == c) return;
    sidebar_color_ = c;
    update();
    emit visualChanged();
}

void FinnSquircle::setSidebarBlur(bool on) {
    if (sidebar_blur_ == on) return;
    sidebar_blur_ = on;
    update();
    emit visualChanged();
}

void FinnSquircle::itemChange(ItemChange change, const ItemChangeData& value) {
    if ((change == ItemSceneChange || change == ItemVisibleHasChanged ||
         change == ItemParentHasChanged) &&
        (backdrop_blur_ || sidebar_blur_)) {
        update();
    }
    QQuickPaintedItem::itemChange(change, value);
}

void FinnSquircle::paint(QPainter* p) {
    const qreal w = width();
    const qreal h = height();
    if (w <= 0.0 || h <= 0.0) return;

    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal pad = shadow_ ? 6.0 : 0.0;
    const QRectF body(pad, pad, std::max<qreal>(0.0, w - 2.0 * pad),
                      std::max<qreal>(0.0, h - 2.0 * pad));
    if (body.width() <= 0.0 || body.height() <= 0.0) return;

    const qreal eff_r = std::min(radius_, std::min(body.width(), body.height()) * 0.5);

    if (shadow_) {
        for (int i = 4; i >= 1; --i) {
            QColor sc(0, 0, 0, std::max(1, (shadow_alpha_ * (5 - i)) / 10));
            p->setPen(QPen(sc, i * 1.6));
            p->setBrush(Qt::NoBrush);
            p->drawPath(make_squircle_or_capsule_path(
                body.adjusted(-i * 0.3, i * 0.5, i * 0.3, i * 1.1),
                eff_r + i * 0.5));
        }
    }

    const QPainterPath path = make_squircle_or_capsule_path(body, eff_r);

    // Clip all interior fills (main body + optional frosted sidebar) to the exact
    // 4-corner rounded squircle so no sidebar rectangle ever squares off left corners.
    p->save();
    p->setClipPath(path);

    if (sidebar_width_ > 0.0) {
        const qreal sw = std::min(sidebar_width_, body.width());
        const QRectF sb_rect(body.left(), body.top(), sw, body.height());
        const QRectF main_rect(body.left() + sw, body.top(),
                               std::max<qreal>(0.0, body.width() - sw), body.height());
        // Paint opaque content pane ONLY on [sw, width] so [0, sw] remains genuinely
        // translucent in the Wayland buffer for real-time compositor behind-window blur.
        p->fillRect(main_rect, color_);
        if (sidebar_blur_ && window()) {
            const LiveWindowDesktopPos lp = resolve_live_window_pos(window());
            const QImage& blur_img = blurred_wallpaper_for_size(lp.output_size);
            if (!blur_img.isNull()) {
                const QPointF scene_pt = mapToScene(sb_rect.topLeft());
                const qreal sx = std::clamp(lp.window_pos.x() + scene_pt.x(),
                                            0.0, std::max<qreal>(0.0, blur_img.width() - sw));
                const qreal sy = std::clamp(lp.window_pos.y() + scene_pt.y(),
                                            0.0, std::max<qreal>(0.0, blur_img.height() - sb_rect.height()));
                p->drawImage(sb_rect, blur_img, QRectF(sx, sy, sw, sb_rect.height()));
            }
        }
        p->fillRect(sb_rect, sidebar_color_);
        // Subtle vertical divider separating frosted sidebar from content pane
        p->setPen(QPen(QColor(0, 0, 0, color_.lightness() < 128 ? 45 : 18), 1.0));
        p->drawLine(QPointF(body.left() + sw, body.top()),
                    QPointF(body.left() + sw, body.bottom()));
    } else {
        if (backdrop_blur_ && window()) {
            const QSize win_sz = window()->size();
            const QPointF scene_pt = mapToScene(body.topLeft());
            const QImage& blur_img = blurred_wallpaper_for_size(win_sz);
            if (!blur_img.isNull()) {
                p->drawImage(body, blur_img,
                             QRectF(scene_pt.x(), scene_pt.y(), body.width(), body.height()));
            }
        }
        p->fillRect(body, color_);
    }

    p->restore();

    if (specular_rim_) {
        QColor spec(255, 255, 255, color_.lightness() < 128 ? 24 : 150);
        p->setPen(QPen(spec, 1.0));
        p->setBrush(Qt::NoBrush);
        p->drawPath(make_squircle_or_capsule_path(
            body.adjusted(1.0, 1.0, -1.0, -1.0),
            std::max<qreal>(0.0, eff_r - 1.0)));
    }

    if (border_width_ > 0.0 && border_color_.alpha() > 0) {
        const qreal inset = border_width_ * 0.5;
        p->setPen(QPen(border_color_, border_width_));
        p->setBrush(Qt::NoBrush);
        p->drawPath(make_squircle_or_capsule_path(
            body.adjusted(inset, inset, -inset, -inset),
            std::max<qreal>(0.0, eff_r - inset)));
    }
}

// ---- FinnBootMarkItem ----

FinnBootMarkItem::FinnBootMarkItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void FinnBootMarkItem::setColor(const QColor& c) {
    if (color_ == c) return;
    color_ = c;
    update();
    emit colorChanged();
}

void FinnBootMarkItem::paint(QPainter* p) {
    const qreal side = std::min(width(), height());
    if (side <= 0.0) return;

    p->setRenderHint(QPainter::Antialiasing, true);
    p->save();
    p->translate((width() - side) * 0.5, (height() - side) * 0.5);
    const qreal s = side / 50.0;
    p->scale(s, s);
    p->setPen(Qt::NoPen);
    p->setBrush(color_);

    // 1. Top-left rounded square (Figma 84:374: 22.725 x 22.725, r=6.82)
    p->drawPath(finn_rounded_path(QRectF(0.0, 0.0, 22.725, 22.725), 6.82));

    // 2. Top-right circle (Figma 84:375: 27.5, 0, 22.725 x 22.725)
    p->drawEllipse(QRectF(27.275, 0.0, 22.725, 22.725));

    // 3. Bottom-left 4-point sparkle (Figma 84:376: 0, 27.5, 22.725 x 22.725)
    {
        const qreal cx = 11.3625;
        const qreal cy = 38.6375;
        const qreal r = 11.3625;
        const qreal c = 2.6;
        QPainterPath star;
        star.moveTo(cx, cy - r);
        star.quadTo(cx + c, cy - c, cx + r, cy);
        star.quadTo(cx + c, cy + c, cx, cy + r);
        star.quadTo(cx - c, cy + c, cx - r, cy);
        star.quadTo(cx - c, cy - c, cx, cy - r);
        star.closeSubpath();
        p->drawPath(star);
    }

    // 4. Bottom-right rotated diamond (Figma 84:377)
    p->save();
    p->translate(38.6375, 38.6375);
    p->rotate(45.0);
    p->drawPath(finn_rounded_path(QRectF(-8.033, -8.033, 16.066, 16.066), 1.8));
    p->restore();

    p->restore();
}

// ---- FinnAvatarItem ----

FinnAvatarItem::FinnAvatarItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, [this](const FinnTokens&) { update(); });
}

void FinnAvatarItem::setColorId(const QString& id) {
    if (color_id_ == id) return;
    color_id_ = id;
    update();
    emit colorIdChanged();
}

void FinnAvatarItem::paint(QPainter* p) {
    const int side = std::max(1, static_cast<int>(std::floor(std::min(width(), height()))));
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    const QPixmap pm = FinnThemeManager::instance().avatarPixmap(
        QSize(side, side), dpr, color_id_);
    if (pm.isNull()) return;
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    p->drawPixmap(QRectF((width() - side) * 0.5, (height() - side) * 0.5, side, side),
                  pm, QRectF(0, 0, pm.width(), pm.height()));
}

// ---- FinnIconProvider ----

FinnIconProvider::FinnIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap FinnIconProvider::requestPixmap(const QString& id, QSize* size,
                                        const QSize& requestedSize) {
    QString name = id;
    QColor tint;
    int px = 24;

    const qsizetype qidx = id.indexOf(QLatin1Char('?'));
    if (qidx >= 0) {
        name = id.left(qidx);
        const QUrlQuery query(id.mid(qidx + 1));
        if (query.hasQueryItem(QStringLiteral("color"))) {
            const QColor parsed(query.queryItemValue(QStringLiteral("color")));
            if (parsed.isValid() && parsed.alpha() > 0) tint = parsed;
        }
        if (query.hasQueryItem(QStringLiteral("size"))) {
            bool ok = false;
            const int s = query.queryItemValue(QStringLiteral("size")).toInt(&ok);
            if (ok && s > 0 && s <= 512) px = s;
        }
    }
    if (name.startsWith(QLatin1String("abs:"))) {
        name = QUrl::fromPercentEncoding(name.mid(4).toLatin1());
    }
    if (requestedSize.isValid() && requestedSize.width() > 0) {
        px = std::max(requestedSize.width(), requestedSize.height());
    }

    // Preserve multi-color app icons and raster icons automatically (never flatten them with a tint).
    const QString resolved = resolve_icon_path(name);
    const bool is_raster =
        resolved.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ||
        resolved.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive) ||
        resolved.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive) ||
        resolved.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive) ||
        resolved.endsWith(QStringLiteral(".ico"), Qt::CaseInsensitive) ||
        name.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ||
        name.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive);
    const bool is_app_icon =
        is_raster ||
        resolved.contains(QStringLiteral("/apps/")) ||
        resolved.contains(QStringLiteral("/icons/hicolor/")) ||
        name.endsWith(QStringLiteral("-app")) ||
        name == QLatin1String("files") || name == QLatin1String("terminal") ||
        name == QLatin1String("notes") || name == QLatin1String("store") ||
        name == QLatin1String("viewer") || name == QLatin1String("monitor") ||
        name == QLatin1String("calculator") || name == QLatin1String("archive") ||
        name == QLatin1String("capture") || name == QLatin1String("flourish") ||
        name == QLatin1String("settings-app") || name == QLatin1String("browser");

    const QColor effective_tint = is_app_icon ? QColor() : tint;
    const QPixmap pm = FinnThemeManager::instance().iconPixmap(
        name, QSize(px, px), effective_tint, 2.0);
    if (size) {
        *size = pm.isNull() ? QSize(px, px) : QSize(px, px);
    }
    return pm;
}

// ---- Engine Registration ----

FinnThemeBridge* register_finn_qml_engine(QQmlEngine* engine) {
    if (!engine) return nullptr;

    static bool types_registered = false;
    if (!types_registered) {
        types_registered = true;
        qmlRegisterType<FinnSpringValue>("FinnUI", 1, 0, "FinnSpring");
        qmlRegisterType<FinnSquircle>("FinnUI", 1, 0, "FinnSquircle");
        qmlRegisterType<FinnBootMarkItem>("FinnUI", 1, 0, "FinnBootMark");
        qmlRegisterType<FinnAvatarItem>("FinnUI", 1, 0, "FinnAvatar");
    }

    auto* theme = new FinnThemeBridge(engine);
    engine->addImageProvider(QStringLiteral("finn-icon"), new FinnIconProvider());
    engine->rootContext()->setContextProperty(QStringLiteral("FinnTheme"), theme);
    engine->addImportPath(QStringLiteral("qrc:/"));
    engine->addImportPath(QStringLiteral("qrc:/qt/qml"));
    return theme;
}

}  // namespace finn::ui::qml
