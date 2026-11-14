// Implements the custom FinnOS QProxyStyle and FinnStylePlugin.

#include "qtplugin/finn_style.hpp"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QStyleOption>
#include <QWidget>

namespace finn::qtplugin {
namespace {

QRectF hairline_rect(const QRect& r, qreal inset = 0.5) {
    return QRectF(r).adjusted(inset, inset, -inset, -inset);
}

void draw_chevron_down(QPainter* p, const QRectF& rect, const QColor& color) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = rect.center();
    QPainterPath path;
    path.moveTo(c.x() - 4.0, c.y() - 2.0);
    path.lineTo(c.x(), c.y() + 2.5);
    path.lineTo(c.x() + 4.0, c.y() - 2.0);
    p->setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
    p->restore();
}

void draw_chevron_up(QPainter* p, const QRectF& rect, const QColor& color) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = rect.center();
    QPainterPath path;
    path.moveTo(c.x() - 3.5, c.y() + 2.0);
    path.lineTo(c.x(), c.y() - 2.0);
    path.lineTo(c.x() + 3.5, c.y() + 2.0);
    p->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
    p->restore();
}

bool is_primary_button(const QStyleOptionButton* btn, const QWidget* w) {
    if (w && w->property("finnVariant").toString() == QStringLiteral("primary")) {
        return true;
    }
    return btn && (btn->features & QStyleOptionButton::DefaultButton);
}

bool is_danger_button(const QWidget* w) {
    return w && w->property("finnVariant").toString() == QStringLiteral("danger");
}

}  // namespace

FinnStyle::FinnStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

finn::ui::FinnTokens FinnStyle::current_tokens() const {
    if (tokens_dirty_) {
        cached_tokens_ = finn::ui::load_tokens();
        tokens_dirty_ = false;
    }
    return cached_tokens_;
}

bool FinnStyle::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ThemeChange ||
        event->type() == QEvent::ApplicationPaletteChange) {
        tokens_dirty_ = true;
    }
    return QProxyStyle::eventFilter(watched, event);
}

void FinnStyle::polish(QApplication* app) {
    tokens_dirty_ = true;
    QProxyStyle::polish(app);
    if (app) {
        const auto t = current_tokens();
        app->setPalette(finn::ui::build_finn_palette(t));
        app->setFont(finn::ui::finn_font(10.5, QFont::Normal));
        app->installEventFilter(this);
    }
}

void FinnStyle::polish(QPalette& palette) {
    tokens_dirty_ = true;
    palette = finn::ui::build_finn_palette(current_tokens());
}

void FinnStyle::polish(QWidget* widget) {
    QProxyStyle::polish(widget);
    if (!widget) return;
    if (qobject_cast<QPushButton*>(widget) ||
        qobject_cast<QCheckBox*>(widget) ||
        qobject_cast<QRadioButton*>(widget) ||
        qobject_cast<QComboBox*>(widget) ||
        qobject_cast<QSlider*>(widget) ||
        qobject_cast<QAbstractItemView*>(widget)) {
        widget->setAttribute(Qt::WA_Hover, true);
    }
    if (auto* area = qobject_cast<QAbstractScrollArea*>(widget)) {
        area->setFrameShape(QFrame::NoFrame);
    }
}

void FinnStyle::unpolish(QWidget* widget) {
    QProxyStyle::unpolish(widget);
}

int FinnStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                           const QWidget* widget) const {
    switch (metric) {
        case PM_ButtonMargin:
            return 10;
        case PM_DefaultFrameWidth:
        case PM_ComboBoxFrameWidth:
        case PM_SpinBoxFrameWidth:
        case PM_MenuPanelWidth:
            return 1;
        case PM_MenuVMargin:
        case PM_MenuHMargin:
            return 4;
        case PM_ScrollBarExtent:
            return 10;
        case PM_ScrollBarSliderMin:
            return 28;
        case PM_IndicatorWidth:
        case PM_IndicatorHeight:
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight:
            return 18;
        case PM_SliderLength:
            return 16;
        case PM_SliderThickness:
            return 22;
        case PM_ProgressBarChunkWidth:
            return 6;
        case PM_TabBarTabVSpace:
            return 8;
        case PM_TabBarTabHSpace:
            return 16;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

QSize FinnStyle::sizeFromContents(ContentsType type, const QStyleOption* option,
                                  const QSize& contentsSize,
                                  const QWidget* widget) const {
    QSize sz = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    switch (type) {
        case CT_PushButton:
            sz.setHeight(std::max(sz.height() + 4, 32));
            sz.setWidth(std::max(sz.width() + 14, 64));
            break;
        case CT_LineEdit:
            sz.setHeight(std::max(sz.height() + 4, 34));
            break;
        case CT_ComboBox:
            sz.setHeight(std::max(sz.height() + 4, 34));
            break;
        case CT_MenuItem:
            if (const auto* mi = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
                if (mi->menuItemType != QStyleOptionMenuItem::Separator) {
                    sz.setHeight(std::max(sz.height(), 30));
                    sz.setWidth(sz.width() + 16);
                }
            }
            break;
        case CT_CheckBox:
            if (widget && widget->property("finnSwitch").toBool()) {
                sz.setWidth(sz.width() + 18);
            }
            break;
        default:
            break;
    }
    return sz;
}

int FinnStyle::styleHint(StyleHint hint, const QStyleOption* option,
                         const QWidget* widget,
                         QStyleHintReturn* returnData) const {
    switch (hint) {
        case SH_EtchDisabledText:
        case SH_ComboBox_Popup:
        case SH_RubberBand_Mask:
            return 0;
        case SH_Menu_MouseTracking:
        case SH_ItemView_ShowDecorationSelected:
        case SH_Slider_AbsoluteSetButtons:
            return 1;
        case SH_Slider_PageSetButtons:
            return Qt::MiddleButton;
        default:
            return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

QRect FinnStyle::subControlRect(ComplexControl cc, const QStyleOptionComplex* opt,
                                SubControl sc, const QWidget* widget) const {
    if (cc == CC_ScrollBar) {
        if (sc == SC_ScrollBarAddLine || sc == SC_ScrollBarSubLine) {
            return QRect();
        }
        if (sc == SC_ScrollBarGroove || sc == SC_ScrollBarAddPage ||
            sc == SC_ScrollBarSubPage) {
            if (sc == SC_ScrollBarGroove) return opt->rect;
        }
    }
    return QProxyStyle::subControlRect(cc, opt, sc, widget);
}

void FinnStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption* opt,
                              QPainter* p, const QWidget* widget) const {
    const auto t = current_tokens();
    const bool enabled = opt->state & State_Enabled;
    const bool hovered = enabled && (opt->state & State_MouseOver);
    const bool sunken = enabled && (opt->state & State_Sunken);
    const bool focused = enabled && (opt->state & State_HasFocus);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    switch (pe) {
        case PE_PanelButtonCommand: {
            const auto* btn = qstyleoption_cast<const QStyleOptionButton*>(opt);
            const bool primary = is_primary_button(btn, widget);
            const bool danger = is_danger_button(widget);
            const QRectF r = hairline_rect(opt->rect);

            QColor bg = t.surface_input;
            QColor border = t.hair;
            if (!enabled) {
                bg = t.surface_canvas;
            } else if (danger) {
                bg = sunken ? t.danger_hover
                            : (hovered ? t.danger_hover : t.danger);
                border = Qt::transparent;
            } else if (primary) {
                bg = sunken ? t.accent.darker(115)
                            : (hovered ? t.accent.lighter(110) : t.accent);
                border = Qt::transparent;
            } else if (btn && (btn->state & State_On)) {
                bg = t.tile_on;
                border = t.accent;
            } else if (sunken) {
                bg = t.dark ? t.surface_input.lighter(128) : QColor(0xE8, 0xEC, 0xF2);
            } else if (hovered) {
                bg = t.dark ? t.surface_input.lighter(115) : QColor(0xF1, 0xF4, 0xF8);
            }

            p->setBrush(bg);
            p->setPen(border == Qt::transparent ? Qt::NoPen
                                                : QPen(border, 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            if (focused && !primary && !danger) {
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(t.accent, 1.5));
                p->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5),
                                   finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            }
            p->restore();
            return;
        }

        case PE_FrameLineEdit: {
            const QRectF r = hairline_rect(opt->rect);
            p->setBrush(enabled ? t.surface_input : t.surface_canvas);
            p->setPen(QPen(focused ? t.accent : t.hair, focused ? 1.5 : 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            p->restore();
            return;
        }

        case PE_IndicatorCheckBox: {
            const bool on = opt->state & State_On;
            if (widget && widget->property("finnSwitch").toBool()) {
                const QRectF track_r(opt->rect.center().x() - 17.0,
                                     opt->rect.center().y() - 10.0, 34.0, 20.0);
                p->setBrush(on ? t.accent : t.track);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(track_r, 10.0, 10.0);

                const qreal knob_x = on ? (track_r.right() - 18.0) : (track_r.left() + 2.0);
                p->setBrush(t.knob);
                p->drawEllipse(QRectF(knob_x, track_r.top() + 2.0, 16.0, 16.0));
                p->restore();
                return;
            }

            const QRectF r(opt->rect.center().x() - 8.5,
                           opt->rect.center().y() - 8.5, 17.0, 17.0);
            if (on) {
                p->setBrush(enabled ? t.accent : t.mute);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(r, finn::ui::kRadiusSm, finn::ui::kRadiusSm);

                QPainterPath check;
                check.moveTo(r.left() + 4.0, r.center().y() + 0.5);
                check.lineTo(r.left() + 7.2, r.bottom() - 4.2);
                check.lineTo(r.right() - 3.8, r.top() + 4.5);
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->drawPath(check);
            } else {
                p->setBrush(hovered ? t.surface_hover : t.surface_input);
                p->setPen(QPen(focused ? t.accent : t.dim, 1.3));
                p->drawRoundedRect(r, finn::ui::kRadiusSm, finn::ui::kRadiusSm);
            }
            p->restore();
            return;
        }

        case PE_IndicatorRadioButton: {
            const bool on = opt->state & State_On;
            const QRectF r(opt->rect.center().x() - 8.5,
                           opt->rect.center().y() - 8.5, 17.0, 17.0);
            if (on) {
                p->setBrush(enabled ? t.accent : t.mute);
                p->setPen(Qt::NoPen);
                p->drawEllipse(r);
                p->setBrush(Qt::white);
                p->drawEllipse(r.adjusted(5.2, 5.2, -5.2, -5.2));
            } else {
                p->setBrush(hovered ? t.surface_hover : t.surface_input);
                p->setPen(QPen(focused ? t.accent : t.dim, 1.3));
                p->drawEllipse(r);
            }
            p->restore();
            return;
        }

        case PE_PanelMenu: {
            const QRectF r = hairline_rect(opt->rect);
            p->setBrush(t.tile);
            p->setPen(QPen(t.hair, 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusLg, finn::ui::kRadiusLg);
            p->restore();
            return;
        }

        case PE_PanelTipLabel: {
            const QRectF r = hairline_rect(opt->rect);
            p->setBrush(t.tile);
            p->setPen(QPen(t.hair, 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            p->restore();
            return;
        }

        case PE_FrameFocusRect: {
            const QRectF r = hairline_rect(opt->rect, 1.0);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(t.accent, 1.5));
            p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            p->restore();
            return;
        }

        case PE_PanelItemViewItem: {
            if (opt->state & State_Selected) {
                p->setBrush(t.tile_on);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(QRectF(opt->rect).adjusted(2, 1, -2, -1),
                                   finn::ui::kRadiusMd, finn::ui::kRadiusMd);
                p->restore();
                return;
            }
            if (hovered) {
                p->setBrush(t.surface_hover);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(QRectF(opt->rect).adjusted(2, 1, -2, -1),
                                   finn::ui::kRadiusMd, finn::ui::kRadiusMd);
                p->restore();
                return;
            }
            p->restore();
            return;
        }

        case PE_FrameGroupBox: {
            const QRectF r = hairline_rect(opt->rect);
            p->setBrush(t.tile);
            p->setPen(QPen(t.hair, 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusLg, finn::ui::kRadiusLg);
            p->restore();
            return;
        }

        case PE_Frame: {
            const QRectF r = hairline_rect(opt->rect);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(t.hair, 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            p->restore();
            return;
        }

        case PE_PanelScrollAreaCorner: {
            p->restore();
            return;
        }

        default:
            p->restore();
            QProxyStyle::drawPrimitive(pe, opt, p, widget);
            return;
    }
}

void FinnStyle::drawControl(ControlElement ce, const QStyleOption* opt,
                            QPainter* p, const QWidget* widget) const {
    const auto t = current_tokens();

    switch (ce) {
        case CE_PushButtonLabel: {
            if (const auto* btn = qstyleoption_cast<const QStyleOptionButton*>(opt)) {
                QStyleOptionButton copy(*btn);
                const bool enabled = copy.state & State_Enabled;
                if (enabled && (is_primary_button(btn, widget) || is_danger_button(widget))) {
                    copy.palette.setColor(QPalette::ButtonText, Qt::white);
                } else if (!enabled) {
                    copy.palette.setColor(QPalette::ButtonText, t.mute);
                } else {
                    copy.palette.setColor(QPalette::ButtonText, t.ink);
                }
                QProxyStyle::drawControl(ce, &copy, p, widget);
                return;
            }
            break;
        }

        case CE_ProgressBar: {
            if (const auto* pb = qstyleoption_cast<const QStyleOptionProgressBar*>(opt)) {
                p->save();
                p->setRenderHint(QPainter::Antialiasing, true);
                const qreal bar_h = 6.0;
                const QRectF track(pb->rect.left(),
                                   pb->rect.center().y() - bar_h / 2.0,
                                   pb->rect.width(), bar_h);
                p->setPen(Qt::NoPen);
                p->setBrush(t.track);
                p->drawRoundedRect(track, bar_h / 2.0, bar_h / 2.0);

                const int span = pb->maximum - pb->minimum;
                if (span > 0 && pb->progress > pb->minimum) {
                    const qreal ratio =
                        std::clamp<qreal>(qreal(pb->progress - pb->minimum) / span, 0.0, 1.0);
                    QRectF fill = track;
                    fill.setWidth(std::max<qreal>(bar_h, track.width() * ratio));
                    p->setBrush(t.accent);
                    p->drawRoundedRect(fill, bar_h / 2.0, bar_h / 2.0);
                }
                p->restore();
                return;
            }
            break;
        }

        case CE_MenuItem: {
            if (const auto* mi = qstyleoption_cast<const QStyleOptionMenuItem*>(opt)) {
                p->save();
                p->setRenderHint(QPainter::Antialiasing, true);
                if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
                    p->setPen(QPen(t.hair, 1.0));
                    const int y = mi->rect.center().y();
                    p->drawLine(mi->rect.left() + 8, y, mi->rect.right() - 8, y);
                    p->restore();
                    return;
                }
                const bool active = (mi->state & State_Selected) && (mi->state & State_Enabled);
                if (active) {
                    p->setBrush(t.tile_on);
                    p->setPen(Qt::NoPen);
                    p->drawRoundedRect(QRectF(mi->rect).adjusted(4, 1, -4, -1),
                                       finn::ui::kRadiusMd, finn::ui::kRadiusMd);
                }
                p->restore();

                QStyleOptionMenuItem copy(*mi);
                copy.state &= ~State_Selected;
                if (!(mi->state & State_Enabled)) {
                    copy.palette.setColor(QPalette::Text, t.mute);
                } else {
                    copy.palette.setColor(QPalette::Text, t.ink);
                }
                QProxyStyle::drawControl(ce, &copy, p, widget);
                return;
            }
            break;
        }

        case CE_TabBarTabShape: {
            if (const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(opt)) {
                p->save();
                p->setRenderHint(QPainter::Antialiasing, true);
                const bool selected = tab->state & State_Selected;
                const bool hovered = tab->state & State_MouseOver;
                const QRectF r = QRectF(tab->rect).adjusted(2, 2, -2, -2);
                if (selected) {
                    p->setBrush(t.tile_on);
                    p->setPen(QPen(t.accent, 1.0));
                    p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
                } else if (hovered) {
                    p->setBrush(t.surface_hover);
                    p->setPen(Qt::NoPen);
                    p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
                }
                p->restore();
                return;
            }
            break;
        }

        case CE_TabBarTabLabel: {
            if (const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(opt)) {
                QStyleOptionTab copy(*tab);
                const bool selected = copy.state & State_Selected;
                const bool enabled = copy.state & State_Enabled;
                const QColor fg =
                    !enabled ? t.mute : (selected ? t.on : t.ink);
                copy.palette.setColor(QPalette::WindowText, fg);
                copy.palette.setColor(QPalette::ButtonText, fg);
                QProxyStyle::drawControl(ce, &copy, p, widget);
                return;
            }
            break;
        }

        case CE_HeaderSection:
        case CE_HeaderEmptyArea: {
            p->save();
            p->setBrush(t.surface_canvas);
            p->setPen(Qt::NoPen);
            p->drawRect(opt->rect);
            p->setPen(QPen(t.hair, 1.0));
            p->drawLine(opt->rect.bottomLeft(), opt->rect.bottomRight());
            p->restore();
            return;
        }

        case CE_HeaderLabel: {
            if (const auto* hdr = qstyleoption_cast<const QStyleOptionHeader*>(opt)) {
                QStyleOptionHeader copy(*hdr);
                copy.palette.setColor(QPalette::ButtonText, t.dim);
                copy.palette.setColor(QPalette::WindowText, t.dim);
                QProxyStyle::drawControl(ce, &copy, p, widget);
                return;
            }
            break;
        }

        case CE_MenuBarEmptyArea: {
            p->save();
            p->setBrush(t.bone);
            p->setPen(Qt::NoPen);
            p->drawRect(opt->rect);
            p->setPen(QPen(t.hair, 1.0));
            p->drawLine(opt->rect.bottomLeft(), opt->rect.bottomRight());
            p->restore();
            return;
        }

        default:
            break;
    }

    QProxyStyle::drawControl(ce, opt, p, widget);
}

void FinnStyle::drawComplexControl(ComplexControl cc,
                                   const QStyleOptionComplex* opt,
                                   QPainter* p,
                                   const QWidget* widget) const {
    const auto t = current_tokens();

    if (cc == CC_Slider) {
        if (const auto* sl = qstyleoption_cast<const QStyleOptionSlider*>(opt)) {
            const QRect groove = subControlRect(CC_Slider, sl, SC_SliderGroove, widget);
            const QRect handle = subControlRect(CC_Slider, sl, SC_SliderHandle, widget);
            const bool horizontal = sl->orientation == Qt::Horizontal;
            const bool hovered = (sl->state & State_Enabled) &&
                                 (sl->activeSubControls & SC_SliderHandle) &&
                                 (sl->state & State_MouseOver);

            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setPen(Qt::NoPen);

            QRectF track_r;
            if (horizontal) {
                track_r = QRectF(groove.left(), groove.center().y() - 2.5,
                                 groove.width(), 5.0);
            } else {
                track_r = QRectF(groove.center().x() - 2.5, groove.top(),
                                 5.0, groove.height());
            }
            p->setBrush(t.track);
            p->drawRoundedRect(track_r, 2.5, 2.5);

            QRectF active_r = track_r;
            if (horizontal) {
                active_r.setRight(handle.center().x());
            } else {
                active_r.setTop(handle.center().y());
            }
            p->setBrush(sl->state & State_Enabled ? t.accent : t.mute);
            p->drawRoundedRect(active_r, 2.5, 2.5);

            const qreal r = hovered ? 8.5 : 7.5;
            const QPointF hc = handle.center();
            const QRectF knob_r(hc.x() - r, hc.y() - r, r * 2.0, r * 2.0);
            p->setBrush(t.knob);
            p->setPen(QPen(t.hair, 1.0));
            p->drawEllipse(knob_r);
            p->restore();
            return;
        }
    }

    if (cc == CC_ScrollBar) {
        if (const auto* sb = qstyleoption_cast<const QStyleOptionSlider*>(opt)) {
            const QRect handle = subControlRect(CC_ScrollBar, sb, SC_ScrollBarSlider, widget);
            if (!handle.isValid() || handle.isEmpty()) return;

            const bool hovered = (sb->activeSubControls & SC_ScrollBarSlider) &&
                                 (sb->state & State_MouseOver);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            const bool horiz = sb->orientation == Qt::Horizontal;
            QRectF pill = horiz
                              ? QRectF(handle).adjusted(2, 2.5, -2, -2.5)
                              : QRectF(handle).adjusted(2.5, 2, -2.5, -2);
            QColor col = t.dim;
            col.setAlpha(hovered ? 190 : 105);
            p->setBrush(col);
            p->setPen(Qt::NoPen);
            const qreal rad = std::min(pill.width(), pill.height()) / 2.0;
            p->drawRoundedRect(pill, rad, rad);
            p->restore();
            return;
        }
    }

    if (cc == CC_ComboBox) {
        if (const auto* cb = qstyleoption_cast<const QStyleOptionComboBox*>(opt)) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            const bool enabled = cb->state & State_Enabled;
            const bool focused = enabled && (cb->state & State_HasFocus);
            const QRectF r = hairline_rect(cb->rect);
            p->setBrush(enabled ? t.surface_input : t.surface_canvas);
            p->setPen(QPen(focused ? t.accent : t.hair, focused ? 1.5 : 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            p->restore();

            const QRect arrow_r = subControlRect(CC_ComboBox, cb, SC_ComboBoxArrow, widget);
            draw_chevron_down(p, arrow_r, enabled ? t.dim : t.mute);
            return;
        }
    }

    if (cc == CC_SpinBox) {
        if (const auto* sp = qstyleoption_cast<const QStyleOptionSpinBox*>(opt)) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            const bool enabled = sp->state & State_Enabled;
            const bool focused = enabled && (sp->state & State_HasFocus);
            const QRectF r = hairline_rect(sp->rect);
            p->setBrush(enabled ? t.surface_input : t.surface_canvas);
            p->setPen(QPen(focused ? t.accent : t.hair, focused ? 1.5 : 1.0));
            p->drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
            p->restore();

            const QRect up_r = subControlRect(CC_SpinBox, sp, SC_SpinBoxUp, widget);
            const QRect dn_r = subControlRect(CC_SpinBox, sp, SC_SpinBoxDown, widget);
            const QColor col = enabled ? t.dim : t.mute;
            draw_chevron_up(p, up_r, col);
            draw_chevron_down(p, dn_r, col);
            return;
        }
    }

    QProxyStyle::drawComplexControl(cc, opt, p, widget);
}

QStyle* FinnStylePlugin::create(const QString& key) {
    if (key.compare(QStringLiteral("finn"), Qt::CaseInsensitive) == 0 ||
        key.compare(QStringLiteral("finnos"), Qt::CaseInsensitive) == 0) {
        return new FinnStyle();
    }
    return nullptr;
}

}  // namespace finn::qtplugin
