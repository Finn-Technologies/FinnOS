// Custom FinnOS QProxyStyle plugin: replaces stock Qt/Fusion primitives with
// FinnOS 6px/8px radii, 1px hairline surfaces, Fixel Display spacing, pill sliders,
// capsule scrollbars, vector chevrons, and token-aware light/dark colors.

#ifndef FINN_QTPLUGIN_FINN_STYLE_HPP
#define FINN_QTPLUGIN_FINN_STYLE_HPP

#include <QProxyStyle>
#include <QStylePlugin>

#include "ui/finn/tokens.hpp"

namespace finn::qtplugin {

class FinnStyle final : public QProxyStyle {
    Q_OBJECT

public:
    FinnStyle();
    ~FinnStyle() override = default;

    void polish(QWidget* widget) override;
    void unpolish(QWidget* widget) override;
    void polish(QApplication* app) override;
    void polish(QPalette& palette) override;

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                           const QSize& contentsSize,
                           const QWidget* widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override;
    QRect subControlRect(ComplexControl cc, const QStyleOptionComplex* opt,
                         SubControl sc,
                         const QWidget* widget = nullptr) const override;

    void drawPrimitive(PrimitiveElement pe, const QStyleOption* opt,
                       QPainter* p, const QWidget* widget = nullptr) const override;
    void drawControl(ControlElement ce, const QStyleOption* opt, QPainter* p,
                     const QWidget* widget = nullptr) const override;
    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex* opt,
                            QPainter* p,
                            const QWidget* widget = nullptr) const override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    finn::ui::FinnTokens current_tokens() const;
    mutable finn::ui::FinnTokens cached_tokens_;
    mutable bool tokens_dirty_ = true;
};

class FinnStylePlugin final : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "finnstyle.json")

public:
    QStyle* create(const QString& key) override;
};

}  // namespace finn::qtplugin

#endif
