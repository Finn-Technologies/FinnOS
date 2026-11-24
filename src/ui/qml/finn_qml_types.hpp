// Native C++20 / Qt 6 Quick (QML) bridge for FinnUI (org.finnos.ui / FinnUI 1.0).
// Exposes live-reloading Figma tokens, adaptive form-factor metrics, analytic
// 120 Hz spring physics, Figma superellipse (cornerSmoothing=0.6) geometry,
// HugeIcons SVG image provider, and FinnOS brand primitives to QML apps and System UI.

#ifndef FINN_UI_QML_FINN_QML_TYPES_HPP
#define FINN_UI_QML_FINN_QML_TYPES_HPP

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QQuickImageProvider>
#include <QQuickPaintedItem>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "ui/finn/form_factor.hpp"
#include "ui/finn/spring.hpp"
#include "ui/finn/theme.hpp"
#include "ui/finn/tokens.hpp"

class QQmlEngine;

namespace finn::ui::qml {

class FinnThemeBridge final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY changed)
    Q_PROPERTY(double textScale READ textScale NOTIFY changed)
    Q_PROPERTY(double scale READ scale NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(QString monoFontFamily READ monoFontFamily CONSTANT)
    Q_PROPERTY(QString fontMono READ monoFontFamily CONSTANT)
    Q_PROPERTY(QString formFactorSetting READ formFactorSetting WRITE setFormFactorSetting NOTIFY changed)
    Q_PROPERTY(QString avatarColor READ avatarColor WRITE setAvatarColor NOTIFY changed)
    Q_PROPERTY(QUrl wallpaperUrl READ wallpaperUrl NOTIFY changed)
    Q_PROPERTY(QUrl lockWallpaperUrl READ lockWallpaperUrl NOTIFY changed)

    // Core palette tokens from ui/_tokens.fui
    Q_PROPERTY(QColor bone READ bone NOTIFY changed)
    Q_PROPERTY(QColor tile READ tile NOTIFY changed)
    Q_PROPERTY(QColor surface READ tile NOTIFY changed)
    Q_PROPERTY(QColor surfaceCanvas READ surfaceCanvas NOTIFY changed)
    Q_PROPERTY(QColor surfaceInput READ surfaceInput NOTIFY changed)
    Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY changed)
    Q_PROPERTY(QColor surfacePressed READ surfacePressed NOTIFY changed)
    Q_PROPERTY(QColor tileOn READ tileOn NOTIFY changed)
    Q_PROPERTY(QColor navSel READ navSel NOTIFY changed)
    Q_PROPERTY(QColor heroTint READ heroTint NOTIFY changed)
    Q_PROPERTY(QColor hair READ hair NOTIFY changed)
    Q_PROPERTY(QColor hairSoft READ hairSoft NOTIFY changed)
    Q_PROPERTY(QColor ink READ ink NOTIFY changed)
    Q_PROPERTY(QColor dim READ dim NOTIFY changed)
    Q_PROPERTY(QColor mute READ mute NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY changed)
    Q_PROPERTY(QColor on READ on NOTIFY changed)
    Q_PROPERTY(QColor track READ track NOTIFY changed)
    Q_PROPERTY(QColor knob READ knob NOTIFY changed)
    Q_PROPERTY(QColor fill READ fill NOTIFY changed)
    Q_PROPERTY(QColor glass READ glass NOTIFY changed)
    Q_PROPERTY(QColor glassPanel READ glassPanel NOTIFY changed)
    Q_PROPERTY(QColor glassFooter READ glassFooter NOTIFY changed)
    Q_PROPERTY(QColor notifGlass READ notifGlass NOTIFY changed)
    Q_PROPERTY(QColor lockPill READ lockPill NOTIFY changed)
    Q_PROPERTY(QColor soundBadge READ soundBadge NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor dangerHover READ dangerHover NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)

    // Radii, spacing, blur, elevation, motion tokens from generated_tokens.hpp
    Q_PROPERTY(double radiusXs READ radiusXs CONSTANT)
    Q_PROPERTY(double radiusNav READ radiusNav CONSTANT)
    Q_PROPERTY(double radiusSm READ radiusSm CONSTANT)
    Q_PROPERTY(double radiusMd READ radiusMd CONSTANT)
    Q_PROPERTY(double radiusLg READ radiusLg CONSTANT)
    Q_PROPERTY(double radiusFolder READ radiusFolder CONSTANT)
    Q_PROPERTY(double radiusXl READ radiusXl CONSTANT)
    Q_PROPERTY(double radiusPill READ radiusPill CONSTANT)
    Q_PROPERTY(double cornerSmoothing READ cornerSmoothing CONSTANT)

    Q_PROPERTY(int sp1 READ sp1 CONSTANT)
    Q_PROPERTY(int sp2 READ sp2 CONSTANT)
    Q_PROPERTY(int sp3 READ sp3 CONSTANT)
    Q_PROPERTY(int sp4 READ sp4 CONSTANT)
    Q_PROPERTY(int sp5 READ sp5 CONSTANT)
    Q_PROPERTY(int sp6 READ sp6 CONSTANT)
    Q_PROPERTY(int sp8 READ sp8 CONSTANT)
    Q_PROPERTY(int sp12 READ sp12 CONSTANT)

    Q_PROPERTY(int blurGlass READ blurGlass CONSTANT)
    Q_PROPERTY(int blurNotification READ blurNotification CONSTANT)
    Q_PROPERTY(int motionSurfaceOpen READ motionSurfaceOpen CONSTANT)
    Q_PROPERTY(int motionSurfaceClose READ motionSurfaceClose CONSTANT)
    Q_PROPERTY(int motionHoverIn READ motionHoverIn CONSTANT)
    Q_PROPERTY(int motionPressIn READ motionPressIn CONSTANT)
    Q_PROPERTY(int motionNote READ motionNote CONSTANT)
    Q_PROPERTY(int motionTheme READ motionTheme CONSTANT)
    Q_PROPERTY(double tvTypeScale READ tvTypeScale CONSTANT)

public:
    explicit FinnThemeBridge(QObject* parent = nullptr);

    bool dark() const { return tokens_.dark; }
    void setDark(bool dark);
    bool reducedMotion() const { return tokens_.reduced_motion; }
    void setReducedMotion(bool reduced);
    bool highContrast() const { return tokens_.high_contrast; }
    void setHighContrast(bool hc);
    double textScale() const { return tokens_.text_scale; }
    double scale() const { return tokens_.scale; }
    QString fontFamily() const;
    QString monoFontFamily() const;
    QString formFactorSetting() const;
    void setFormFactorSetting(const QString& ff);
    QString avatarColor() const;
    void setAvatarColor(const QString& id);
    QUrl wallpaperUrl() const;
    QUrl lockWallpaperUrl() const;

    QColor bone() const { return tokens_.bone; }
    QColor tile() const { return tokens_.tile; }
    QColor surfaceCanvas() const { return tokens_.surface_canvas; }
    QColor surfaceInput() const { return tokens_.surface_input; }
    QColor surfaceHover() const { return tokens_.surface_hover; }
    QColor surfacePressed() const { return tokens_.surface_pressed; }
    QColor tileOn() const { return tokens_.tile_on; }
    QColor navSel() const { return tokens_.nav_sel; }
    QColor heroTint() const { return tokens_.hero_tint; }
    QColor hair() const { return tokens_.hair; }
    QColor hairSoft() const;
    QColor ink() const { return tokens_.ink; }
    QColor dim() const { return tokens_.dim; }
    QColor mute() const { return tokens_.mute; }
    QColor accent() const { return tokens_.accent; }
    void setAccent(const QColor& c);
    QColor on() const { return tokens_.on; }
    QColor track() const { return tokens_.track; }
    QColor knob() const { return tokens_.knob; }
    QColor fill() const { return tokens_.fill; }
    QColor glass() const { return tokens_.glass; }
    QColor glassPanel() const;
    QColor glassFooter() const;
    QColor notifGlass() const;
    QColor lockPill() const;
    QColor soundBadge() const;
    QColor danger() const { return tokens_.danger; }
    QColor dangerHover() const { return tokens_.danger_hover; }
    QColor success() const { return tokens_.success; }
    QColor warning() const { return tokens_.warning; }

    double radiusXs() const { return finn::ui::radii::r_xs; }
    double radiusNav() const { return finn::ui::radii::r_nav; }
    double radiusSm() const { return finn::ui::radii::r_sm; }
    double radiusMd() const { return finn::ui::radii::r_md; }
    double radiusLg() const { return finn::ui::radii::r_lg; }
    double radiusFolder() const { return finn::ui::radii::r_folder; }
    double radiusXl() const { return finn::ui::radii::r_xl; }
    double radiusPill() const { return finn::ui::radii::r_pill; }
    double cornerSmoothing() const { return finn::ui::radii::corner_smoothing; }

    int sp1() const { return finn::ui::spacing::sp_1; }
    int sp2() const { return finn::ui::spacing::sp_2; }
    int sp3() const { return finn::ui::spacing::sp_3; }
    int sp4() const { return finn::ui::spacing::sp_4; }
    int sp5() const { return finn::ui::spacing::sp_5; }
    int sp6() const { return finn::ui::spacing::sp_6; }
    int sp8() const { return finn::ui::spacing::sp_8; }
    int sp12() const { return finn::ui::spacing::sp_12; }

    int blurGlass() const { return finn::ui::blur::blur_glass; }
    int blurNotification() const { return finn::ui::blur::blur_notification; }
    int motionSurfaceOpen() const { return finn::ui::motion::surface_open_ms; }
    int motionSurfaceClose() const { return finn::ui::motion::surface_close_ms; }
    int motionHoverIn() const { return finn::ui::motion::hover_in_ms; }
    int motionPressIn() const { return finn::ui::motion::press_in_ms; }
    int motionNote() const { return finn::ui::motion::note_ms; }
    int motionTheme() const { return finn::ui::motion::theme_ms; }
    double tvTypeScale() const { return finn::ui::tv::type_scale; }

    Q_INVOKABLE QString resolveFormFactor(int widthDp, int heightDp = 0,
                                          bool tvHint = false) const;
    Q_INVOKABLE QVariantMap formFactorMetrics(const QString& formFactor,
                                              int widthDp = 1280,
                                              int heightDp = 832) const;
    Q_INVOKABLE QString iconUrl(const QString& name, const QColor& tint = QColor(),
                                int sizePx = 24) const;
    Q_INVOKABLE QColor withAlpha(const QColor& color, double alpha01) const;
    Q_INVOKABLE void reload();

    // Runtime override (used by CLI flags / tests without clobbering user settings)
    void setRuntimeFormFactorOverride(const QString& ff);
    void setRuntimeDarkOverride(bool enabled, bool dark);

signals:
    void changed();

private:
    void syncFromManager();

    FinnTokens tokens_;
    QString runtime_ff_override_;
    bool has_dark_override_ = false;
    bool dark_override_ = false;
};

// Analytic 120Hz spring physics object exposed to QML.
class FinnSpringValue : public QObject {
    Q_OBJECT
    Q_PROPERTY(double value READ value NOTIFY valueChanged)
    Q_PROPERTY(double target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(double velocity READ velocity NOTIFY valueChanged)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY presetChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit FinnSpringValue(QObject* parent = nullptr);

    double value() const { return spring_.value(); }
    double target() const { return spring_.target(); }
    void setTarget(double target);
    double velocity() const { return spring_.velocity(); }
    QString preset() const { return preset_; }
    void setPreset(const QString& name);
    bool running() const { return timer_.isActive(); }

    Q_INVOKABLE void snap(double value);
    Q_INVOKABLE void impulse(double velocityDelta, double newTarget);

signals:
    void valueChanged();
    void targetChanged();
    void presetChanged();
    void runningChanged();
    void settled();

private:
    void tick();
    static SpringParams paramsForPreset(const QString& name);

    SpringAnimator spring_{springs::kSmooth, 0.0};
    QString preset_ = QStringLiteral("smooth");
    QTimer timer_;
    QElapsedTimer clock_;
};

// Custom QQuickPaintedItem drawing Figma cornerSmoothing=0.6 superellipses
// with optional fill, hairline border, specular rim, and elevation shadow.
class FinnSquircle : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY visualChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY visualChanged)
    Q_PROPERTY(qreal borderWidth READ borderWidth WRITE setBorderWidth NOTIFY visualChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY visualChanged)
    Q_PROPERTY(bool specularRim READ specularRim WRITE setSpecularRim NOTIFY visualChanged)
    Q_PROPERTY(bool shadow READ shadow WRITE setShadow NOTIFY visualChanged)
    Q_PROPERTY(int shadowAlpha READ shadowAlpha WRITE setShadowAlpha NOTIFY visualChanged)
    Q_PROPERTY(bool backdropBlur READ backdropBlur WRITE setBackdropBlur NOTIFY visualChanged)
    Q_PROPERTY(qreal sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY visualChanged)
    Q_PROPERTY(QColor sidebarColor READ sidebarColor WRITE setSidebarColor NOTIFY visualChanged)
    Q_PROPERTY(bool sidebarBlur READ sidebarBlur WRITE setSidebarBlur NOTIFY visualChanged)

public:
    explicit FinnSquircle(QQuickItem* parent = nullptr);

    QColor color() const { return color_; }
    void setColor(const QColor& c);
    QColor borderColor() const { return border_color_; }
    void setBorderColor(const QColor& c);
    qreal borderWidth() const { return border_width_; }
    void setBorderWidth(qreal w);
    qreal radius() const { return radius_; }
    void setRadius(qreal r);
    bool specularRim() const { return specular_rim_; }
    void setSpecularRim(bool on);
    bool shadow() const { return shadow_; }
    void setShadow(bool on);
    int shadowAlpha() const { return shadow_alpha_; }
    void setShadowAlpha(int a);
    bool backdropBlur() const { return backdrop_blur_; }
    void setBackdropBlur(bool on);
    qreal sidebarWidth() const { return sidebar_width_; }
    void setSidebarWidth(qreal w);
    QColor sidebarColor() const { return sidebar_color_; }
    void setSidebarColor(const QColor& c);
    bool sidebarBlur() const { return sidebar_blur_; }
    void setSidebarBlur(bool on);

    void paint(QPainter* painter) override;

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override;

signals:
    void visualChanged();

private:
    QColor color_{255, 255, 255, 179};
    QColor border_color_{0, 0, 0, 20};
    qreal border_width_ = 1.0;
    qreal radius_ = 12.0;
    bool specular_rim_ = false;
    bool shadow_ = false;
    int shadow_alpha_ = 16;
    bool backdrop_blur_ = false;
    qreal sidebar_width_ = 0.0;
    QColor sidebar_color_{243, 245, 248, 220};
    bool sidebar_blur_ = false;
};

// Exact Figma 84:373 4-element FinnOS boot/brand mark vector renderer.
class FinnBootMarkItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit FinnBootMarkItem(QQuickItem* parent = nullptr);

    QColor color() const { return color_; }
    void setColor(const QColor& c);
    void paint(QPainter* painter) override;

signals:
    void colorChanged();

private:
    QColor color_{Qt::black};
};

// Exact Figma 84:383 / 50:2 user avatar gradient circle renderer.
class FinnAvatarItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString colorId READ colorId WRITE setColorId NOTIFY colorIdChanged)

public:
    explicit FinnAvatarItem(QQuickItem* parent = nullptr);

    QString colorId() const { return color_id_; }
    void setColorId(const QString& id);
    void paint(QPainter* painter) override;

signals:
    void colorIdChanged();

private:
    QString color_id_;
};

// QQuickImageProvider serving HugeIcons and app-tile SVGs via image://finn-icon/<name>
class FinnIconProvider final : public QQuickImageProvider {
public:
    FinnIconProvider();
    QPixmap requestPixmap(const QString& id, QSize* size,
                          const QSize& requestedSize) override;
};

// Registers all FinnUI C++ types, singletons, and image://finn-icon onto an engine.
FinnThemeBridge* register_finn_qml_engine(QQmlEngine* engine);

}  // namespace finn::ui::qml

#endif  // FINN_UI_QML_FINN_QML_TYPES_HPP
