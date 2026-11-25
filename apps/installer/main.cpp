// FinnOS Recovery & USB Installer (finn-installer / finn-recovery).
// Built directly on the FinnOS OOBE design system and transition engine
// (apps/oobe/main.cpp):
//   - Full-bleed bone canvas with OobeBrandingHeader lockup
//   - Exact OOBE split two-column layout (80px gutter, 46x46 icon + 40px gap +
//     24px Regular prompt, 132px center gap, right 350px column at y=195)
//   - Exact OobeOptionRow (350x37, 18px Regular, 60fps hover/select interpolation)
//   - Exact OobeProgressiveList + ProgressiveBlurOverlay
//   - Exact OobeCircleNavButton (56x56 circular Figma bezier chevron, 60fps spring)
//   - Exact OobeSlideTransitionOverlay (0.52s sequential fade + whisper optical blur)
//   - Embedded draggable RecoveryTerminalWindow (works on eglfs, linuxfb, Wayland)
//   - Live keyboard layout switcher + bulletproof GPT/partition management.

#include <FinnSDK/FinnSDK.hpp>

#include "common/paths.hpp"
#include "recovery/recovery_engine.hpp"
#include "system/sys_files.hpp"

#include <QAbstractButton>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <functional>

namespace {

using namespace finn::ui;
using namespace finn::recovery;

QFont oobe_font(int pixel_size, QFont::Weight weight = QFont::Normal) {
    return finn_font_px(pixel_size, weight);
}

constexpr int kOobeGutter = 80;
constexpr int kOobeContentWidth = 350;

// Exact OOBE box blur helper (used by ProgressiveBlurOverlay and OobeSlideTransitionOverlay).
QImage box_blur_image(const QImage& src, int radius) {
    if (radius <= 0 || src.isNull()) return src;
    QImage a = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage b(a.size(), QImage::Format_ARGB32_Premultiplied);
    const int w = a.width();
    const int h = a.height();
    const int d = 2 * radius + 1;

    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < h; ++y) {
            const QRgb* in_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            QRgb* out_row = reinterpret_cast<QRgb*>(b.scanLine(y));
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb px = in_row[std::clamp(k, 0, w - 1)];
                sr += qRed(px);
                sg += qGreen(px);
                sb += qBlue(px);
                sa += qAlpha(px);
            }
            for (int x = 0; x < w; ++x) {
                out_row[x] = qRgba(sr / d, sg / d, sb / d, sa / d);
                const QRgb rem = in_row[std::clamp(x - radius, 0, w - 1)];
                const QRgb add = in_row[std::clamp(x + radius + 1, 0, w - 1)];
                sr += qRed(add) - qRed(rem);
                sg += qGreen(add) - qGreen(rem);
                sb += qBlue(add) - qBlue(rem);
                sa += qAlpha(add) - qAlpha(rem);
            }
        }
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb px = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(k, 0, h - 1)))[x];
                sr += qRed(px);
                sg += qGreen(px);
                sb += qBlue(px);
                sa += qAlpha(px);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(a.scanLine(y))[x] =
                    qRgba(sr / d, sg / d, sb / d, sa / d);
                const QRgb rem = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(y - radius, 0, h - 1)))[x];
                const QRgb add = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(y + radius + 1, 0, h - 1)))[x];
                sr += qRed(add) - qRed(rem);
                sg += qGreen(add) - qGreen(rem);
                sb += qBlue(add) - qBlue(rem);
                sa += qAlpha(add) - qAlpha(rem);
            }
        }
    }
    return a;
}

// Top Branding lockup matching Figma 36:45 (20x20 four-shape mark + 15px gap + "FinnOS" 20px).
class OobeBrandingHeader final : public QWidget {
    Q_OBJECT
public:
    explicit OobeBrandingHeader(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(112, 24);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QColor ink = t.ink;
        FinnThemeManager::instance().paintIcon(
            &p, QRectF(0.0, 2.0, 20.0, 20.0),
            t.dark ? QStringLiteral("finn-logo-light") : QStringLiteral("finn-logo-dark"),
            ink);

        p.setFont(oobe_font(20, QFont::Normal));
        p.setPen(ink);
        p.drawText(QRectF(35.0, 0.0, width() - 35.0, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FinnOS"));
    }
};

// Exact OOBE circular 56x56 chevron navigation button (from apps/oobe/main.cpp).
class OobeCircleNavButton final : public QAbstractButton {
    Q_OBJECT
public:
    OobeCircleNavButton(bool is_back, bool filled_by_default,
                        QWidget* parent = nullptr)
        : QAbstractButton(parent),
          is_back_(is_back),
          filled_default_(filled_by_default) {
        setFixedSize(56, 56);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(is_back ? QStringLiteral("Back") : QStringLiteral("Next"));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        anim_timer_.setInterval(16);
        connect(&anim_timer_, &QTimer::timeout, this, [this]() {
            const qreal target_h = hovered_ ? 1.0 : 0.0;
            const qreal target_p = isDown() ? 1.0 : 0.0;
            hover_t_ += (target_h - hover_t_) * 0.28;
            press_t_ += (target_p - press_t_) * 0.36;
            if (std::abs(target_h - hover_t_) < 0.01 &&
                std::abs(target_p - press_t_) < 0.01) {
                hover_t_ = target_h;
                press_t_ = target_p;
                anim_timer_.stop();
            }
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal micro_scale = 1.0 + 0.045 * hover_t_ - 0.095 * press_t_;
        const QPointF center(width() * 0.5, height() * 0.5);
        p.translate(center);
        p.scale(micro_scale, micro_scale);
        p.translate(-center);

        const QRectF circle_r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const bool en = isEnabled();

        QColor fill = Qt::transparent;
        if (filled_default_) {
            fill = t.tile;
            if (press_t_ > 0.02) {
                const QColor down_c = t.surface_pressed;
                fill = QColor(
                    qRound(fill.red() + (down_c.red() - fill.red()) * press_t_),
                    qRound(fill.green() + (down_c.green() - fill.green()) * press_t_),
                    qRound(fill.blue() + (down_c.blue() - fill.blue()) * press_t_));
            } else if (hover_t_ > 0.02) {
                const QColor hov_c = t.surface_hover;
                fill = QColor(
                    qRound(fill.red() + (hov_c.red() - fill.red()) * hover_t_),
                    qRound(fill.green() + (hov_c.green() - fill.green()) * hover_t_),
                    qRound(fill.blue() + (hov_c.blue() - fill.blue()) * hover_t_));
            }
        } else if (en) {
            const qreal a = hover_t_ * (t.dark ? 22.0 : 195.0) + press_t_ * (t.dark ? 16.0 : 40.0);
            if (a > 0.5) {
                fill = QColor(t.fill.red(), t.fill.green(), t.fill.blue(),
                              std::min(255, qRound(a)));
            }
        }

        if (fill.alpha() > 0) {
            p.setPen(t.high_contrast ? QPen(t.ink, 2.0) : Qt::NoPen);
            p.setBrush(fill);
            p.drawEllipse(circle_r);
        }
        if (hasFocus()) {
            p.setPen(QPen(t.accent, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(circle_r.adjusted(1.0, 1.0, -1.0, -1.0));
        }

        QColor stroke_col = t.ink;
        if (!en) stroke_col.setAlpha(72);

        p.save();
        p.translate(10.0, 10.0);
        if (is_back_) {
            p.translate(36.0, 0.0);
            p.scale(-1.0, 1.0);
        }
        QPainterPath chev;
        chev.moveTo(13.0, 29.0);
        chev.cubicTo(13.0, 29.0, 24.0, 20.8987, 24.0, 18.0);
        chev.cubicTo(24.0, 15.1011, 13.0, 7.0, 13.0, 7.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(stroke_col, 2.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(chev);
        p.restore();
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void mousePressEvent(QMouseEvent* e) override {
        QAbstractButton::mousePressEvent(e);
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        QAbstractButton::mouseReleaseEvent(e);
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

private:
    bool is_back_ = false;
    bool filled_default_ = false;
    bool hovered_ = false;
    qreal hover_t_ = 0.0;
    qreal press_t_ = 0.0;
    QTimer anim_timer_;
};

// Exact OOBE selectable 350x37 pill row (from apps/oobe/main.cpp).
class OobeOptionRow final : public QWidget {
    Q_OBJECT
public:
    OobeOptionRow(const QString& label, const QString& detail = {},
                  bool selected = false, QWidget* parent = nullptr)
        : QWidget(parent),
          label_(label),
          detail_(detail),
          selected_(selected) {
        setFixedSize(kOobeContentWidth, 37);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(detail.isEmpty() ? label : QStringLiteral("%1, %2").arg(label, detail));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        sel_t_ = selected_ ? 1.0 : 0.0;
        anim_timer_.setInterval(16);
        connect(&anim_timer_, &QTimer::timeout, this, [this]() {
            const qreal target_h = hovered_ ? 1.0 : 0.0;
            const qreal target_s = selected_ ? 1.0 : 0.0;
            hover_t_ += (target_h - hover_t_) * 0.28;
            sel_t_ += (target_s - sel_t_) * 0.24;
            if (std::abs(target_h - hover_t_) < 0.01 &&
                std::abs(target_s - sel_t_) < 0.01) {
                hover_t_ = target_h;
                sel_t_ = target_s;
                anim_timer_.stop();
            }
            update();
        });
    }

    void setSelected(bool selected) {
        if (selected_ == selected) return;
        selected_ = selected;
        if (FinnThemeManager::instance().reducedMotion()) {
            sel_t_ = selected_ ? 1.0 : 0.0;
            update();
        } else if (!anim_timer_.isActive()) {
            anim_timer_.start();
        }
    }

    bool isSelected() const { return selected_; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r(0.0, 0.0, width(), height());
        const qreal base_a = t.dark
            ? (hover_t_ * 14.0 + sel_t_ * 28.0 + (pressed_ ? 12.0 : 0.0))
            : (hover_t_ * 110.0 + sel_t_ * 255.0 + (pressed_ ? 50.0 : 0.0));
        if (base_a > 0.5 && isEnabled()) {
            const QColor pill_base = t.dark ? t.tile_on : t.tile;
            const QColor bg(pill_base.red(), pill_base.green(), pill_base.blue(),
                            std::min(255, qRound(t.dark ? base_a * 8.0 : base_a)));
            p.setPen(t.high_contrast && selected_ ? QPen(t.ink, 2.0) : Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 10.0, 10.0);
        }
        if (hasFocus()) {
            p.setPen(QPen(t.accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0), 10.0, 10.0);
        }

        const QColor ink = isEnabled() ? t.ink : t.mute;
        if (detail_.isEmpty()) {
            p.setFont(oobe_font(18, QFont::Normal));
            p.setPen(ink);
            const QRectF text_r(10.0, 0.0, width() - 20.0, height());
            p.drawText(text_r, Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(label_, Qt::ElideRight, qRound(text_r.width())));
        } else {
            p.setFont(oobe_font(13, QFont::Normal));
            const int det_w = p.fontMetrics().horizontalAdvance(detail_) + 12;
            p.setPen(t.dim);
            p.drawText(QRectF(10.0, 0.0, width() - 20.0, height()),
                       Qt::AlignRight | Qt::AlignVCenter, detail_);

            p.setFont(oobe_font(16, QFont::Normal));
            p.setPen(ink);
            const QRectF text_r(10.0, 0.0, std::max<qreal>(40.0, width() - 20.0 - det_w), height());
            p.drawText(text_r, Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(label_, Qt::ElideRight, qRound(text_r.width())));
        }
    }

    void enterEvent(QEnterEvent*) override {
        if (!isEnabled()) return;
        hovered_ = true;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        pressed_ = false;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (isEnabled() && event->button() == Qt::LeftButton) {
            pressed_ = true;
            update();
        } else {
            QWidget::mousePressEvent(event);
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (isEnabled() && event->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            update();
            if (rect().contains(event->pos())) emit clicked();
        } else {
            QWidget::mouseReleaseEvent(event);
        }
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (isEnabled() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
                            event->key() == Qt::Key_Space)) {
            emit clicked();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QString label_;
    QString detail_;
    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    qreal hover_t_ = 0.0;
    qreal sel_t_ = 0.0;
    QTimer anim_timer_;
};

// Exact OOBE scrollable list container + progressive bottom blur overlay.
class OobeProgressiveList final : public QWidget {
    Q_OBJECT
public:
    explicit OobeProgressiveList(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedWidth(kOobeContentWidth + 6);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);

        scroll_ = new QScrollArea(this);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->viewport()->setAutoFillBackground(false);
        scroll_->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
        scroll_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

        host_ = new QWidget(scroll_);
        host_->setAutoFillBackground(false);
        host_->setAttribute(Qt::WA_TranslucentBackground, true);
        list_lay_ = new QVBoxLayout(host_);
        list_lay_->setContentsMargins(0, 0, 6, 80);
        list_lay_->setSpacing(8);
        scroll_->setWidget(host_);

        connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged,
                this, qOverload<>(&QWidget::update));
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    QVBoxLayout* listLayout() const { return list_lay_; }
    QWidget* hostWidget() const { return host_; }
    QScrollBar* verticalScrollBar() const { return scroll_ ? scroll_->verticalScrollBar() : nullptr; }
    int scrollOffset() const { return scroll_ && scroll_->verticalScrollBar() ? scroll_->verticalScrollBar()->value() : 0; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        scroll_->setGeometry(rect());
    }

private:
    QScrollArea* scroll_ = nullptr;
    QWidget* host_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
};

class ProgressiveBlurOverlay final : public QWidget {
    Q_OBJECT
public:
    explicit ProgressiveBlurOverlay(OobeProgressiveList* target_list, QWidget* parent = nullptr)
        : QWidget(parent), target_(target_list) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        if (target_ && target_->verticalScrollBar()) {
            connect(target_->verticalScrollBar(), &QScrollBar::valueChanged,
                    this, &ProgressiveBlurOverlay::invalidateCache);
        }
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &ProgressiveBlurOverlay::invalidateCache);
    }

    void invalidateCache() {
        cache_dirty_ = true;
        update();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        cache_dirty_ = true;
    }

    void paintEvent(QPaintEvent*) override {
        if (!target_ || height() < 40 || in_capture_) return;
        const auto& t = FinnThemeManager::instance().tokens();
        const QColor bg = t.bone;

        const int blur_top = qRound(height() * 0.68);
        const int blur_h = height() - blur_top;
        if (blur_h <= 8) return;

        const int scroll_y = target_->scrollOffset();
        if (cache_dirty_ || cached_comp_.isNull() || cached_size_ != size() ||
            cached_dark_ != t.dark || cached_scroll_ != scroll_y) {
            in_capture_ = true;
            QImage snap(width(), blur_h, QImage::Format_ARGB32_Premultiplied);
            snap.fill(bg);
            target_->render(&snap, QPoint(0, 0), QRegion(0, blur_top, width(), blur_h),
                            QWidget::DrawChildren);
            in_capture_ = false;

            const QImage b1 = box_blur_image(snap, 2);
            const QImage b2 = box_blur_image(b1, 4);
            const QImage b3 = box_blur_image(b2, 6);

            const int band = blur_h / 4;
            const int feather = std::max(12, blur_h / 5);

            QImage comp(width(), blur_h, QImage::Format_ARGB32_Premultiplied);
            comp.fill(Qt::transparent);
            {
                QPainter cp(&comp);
                cp.setRenderHint(QPainter::Antialiasing, true);
                cp.drawImage(0, 0, b1, 0, 0, width(), band * 2);
                cp.drawImage(0, band, b2, 0, band, width(), band * 2);
                cp.drawImage(0, band * 2, b3, 0, band * 2, width(), blur_h - band * 2);

                QLinearGradient mask(0, 0, 0, feather);
                mask.setColorAt(0.0, QColor(0, 0, 0, 0));
                mask.setColorAt(1.0, QColor(0, 0, 0, 255));
                cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                cp.fillRect(QRect(0, 0, width(), feather), mask);
            }
            cached_comp_ = std::move(comp);
            cached_size_ = size();
            cached_dark_ = t.dark;
            cached_scroll_ = scroll_y;
            cache_dirty_ = false;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawImage(0, blur_top, cached_comp_);

        QLinearGradient fade(0, blur_top, 0, height());
        QColor c0 = bg; c0.setAlpha(0);
        QColor c1 = bg; c1.setAlpha(110);
        QColor c2 = bg; c2.setAlpha(255);
        fade.setColorAt(0.0, c0);
        fade.setColorAt(0.55, c1);
        fade.setColorAt(1.0, c2);
        p.fillRect(QRect(0, blur_top, width(), blur_h), fade);
    }

private:
    OobeProgressiveList* target_ = nullptr;
    bool in_capture_ = false;
    bool cache_dirty_ = true;
    QImage cached_comp_;
    QSize cached_size_;
    bool cached_dark_ = false;
    int cached_scroll_ = -1;
};

// Exact OOBE buttery sequential fade + whisper optical blur transition (0.52s).
class OobeSlideTransitionOverlay final : public QWidget {
public:
    OobeSlideTransitionOverlay(QWidget* parent, QWidget* stack_widget,
                               QPixmap old_snap, QPixmap new_snap, bool forward)
        : QWidget(parent),
          stack_widget_(stack_widget),
          old_snap_(std::move(old_snap)),
          new_snap_(std::move(new_snap)),
          forward_(forward) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (stack_widget_) {
            setGeometry(stack_widget_->geometry());
            stack_widget_->setVisible(false);
        } else if (parent) {
            setGeometry(parent->rect());
        }
        old_blur_ = makeWhisperBlur(old_snap_);
        new_blur_ = makeWhisperBlur(new_snap_);
        elapsed_.start();
        timer_ = new QTimer(this);
        timer_->setInterval(8);
        connect(timer_, &QTimer::timeout, this, [this]() {
            if (stack_widget_ && geometry() != stack_widget_->geometry()) {
                setGeometry(stack_widget_->geometry());
            }
            if (elapsed_.nsecsElapsed() / 1e9 >= kDurationSec) {
                finish();
                return;
            }
            update();
        });
        timer_->start();
        show();
        raise();
    }

    ~OobeSlideTransitionOverlay() override {
        if (!cancelled_ && stack_widget_) {
            stack_widget_->setVisible(true);
        }
    }

    QPixmap grabCurrentFrame() {
        const qreal dpr = devicePixelRatioF();
        QPixmap pix(qRound(width() * dpr), qRound(height() * dpr));
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        render(&pix);
        return pix;
    }

    void cancel() {
        cancelled_ = true;
        if (timer_) timer_->stop();
        hide();
        deleteLater();
    }

    void finish() {
        if (timer_) timer_->stop();
        if (stack_widget_) {
            stack_widget_->setVisible(true);
            stack_widget_->update();
        }
        hide();
        deleteLater();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const double t = std::clamp((elapsed_.nsecsElapsed() / 1e9) / kDurationSec, 0.0, 1.0);
        constexpr qreal kDrift = 36.0;
        const qreal dir = forward_ ? 1.0 : -1.0;

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        if (t < 0.40) {
            const double u = std::clamp(t / 0.40, 0.0, 1.0);
            const double ease_in = u * u * (2.0 - u) * u;
            const double alpha = 1.0 - (u * u * (3.0 - 2.0 * u));
            const double blur_mix = u * u;
            const QPointF off(-dir * kDrift * ease_in, 0.0);

            const double sharp_a = alpha * (1.0 - blur_mix);
            const double blur_a = alpha * blur_mix;
            if (sharp_a > 0.002) {
                p.setOpacity(sharp_a);
                p.drawPixmap(off, old_snap_);
            }
            if (blur_a > 0.002 && !old_blur_.isNull()) {
                p.setOpacity(blur_a);
                p.drawPixmap(QRectF(off, QSizeF(size())), old_blur_, QRectF(old_blur_.rect()));
            }
        } else if (t >= 0.44) {
            const double u = std::clamp((t - 0.44) / 0.56, 0.0, 1.0);
            const double inv = 1.0 - u;
            const double ease_out = 1.0 - inv * inv * inv * inv;
            const double alpha = 1.0 - inv * inv * inv;
            const double blur_mix = inv * inv;
            const QPointF off(dir * kDrift * (1.0 - ease_out), 0.0);

            const double blur_a = alpha * blur_mix;
            const double sharp_a = alpha * (1.0 - blur_mix);
            if (blur_a > 0.002 && !new_blur_.isNull()) {
                p.setOpacity(blur_a);
                p.drawPixmap(QRectF(off, QSizeF(size())), new_blur_, QRectF(new_blur_.rect()));
            }
            if (sharp_a > 0.002) {
                p.setOpacity(sharp_a);
                p.drawPixmap(off, new_snap_);
            }
        }
        p.setOpacity(1.0);
    }

private:
    static QPixmap makeWhisperBlur(const QPixmap& src) {
        if (src.isNull()) return {};
        QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QSize half(std::max(1, img.width() / 2), std::max(1, img.height() / 2));
        QImage small = img.scaled(half, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage blurred = box_blur_image(small, 2);
        return QPixmap::fromImage(blurred);
    }

    static constexpr double kDurationSec = 0.52;
    QPointer<QWidget> stack_widget_;
    QPixmap old_snap_;
    QPixmap new_snap_;
    QPixmap old_blur_;
    QPixmap new_blur_;
    bool forward_ = true;
    bool cancelled_ = false;
    QElapsedTimer elapsed_;
    QTimer* timer_ = nullptr;
};

// Quiet inline action button.
class QuietLink final : public QAbstractButton {
    Q_OBJECT
public:
    explicit QuietLink(QString text, bool destructive = false, QWidget* parent = nullptr)
        : QAbstractButton(parent), text_(std::move(text)), destructive_(destructive) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(28);
        setFont(oobe_font(13, QFont::Medium));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    void setLabelText(const QString& t) {
        text_ = t;
        updateGeometry();
        update();
    }
    QSize sizeHint() const override {
        return QSize(fontMetrics().horizontalAdvance(text_) + 14, 28);
    }
protected:
    void enterEvent(QEnterEvent* e) override { hovered_ = true; update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent* e) override { hovered_ = false; update(); QAbstractButton::leaveEvent(e); }
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (hovered_ && isEnabled()) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12));
            p.drawRoundedRect(rect(), 8.0, 8.0);
        }
        p.setFont(font());
        QColor col = destructive_ ? (hovered_ ? t.danger : t.dim) : (hovered_ ? t.ink : t.dim);
        if (!isEnabled()) col = t.mute;
        p.setPen(col);
        p.drawText(rect(), Qt::AlignCenter, text_);
    }
private:
    QString text_;
    bool destructive_ = false;
    bool hovered_ = false;
};

// Minimalist thin proportional disk partition bar.
class SlimPartitionBar final : public QWidget {
    Q_OBJECT
public:
    explicit SlimPartitionBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(kOobeContentWidth, 8);
    }
    void setDisk(const DiskInfo& d) { disk_ = d; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF full = QRectF(rect());
        QPainterPath clip;
        clip.addRoundedRect(full, 4.0, 4.0);
        p.setClipPath(clip);
        p.fillRect(full, t.track);

        const std::uint64_t total = std::max<std::uint64_t>(1, disk_.size_bytes);
        qreal x = full.left();
        for (const auto& part : disk_.partitions) {
            const qreal seg_w = std::max<qreal>(6.0, full.width() *
                (static_cast<qreal>(part.size_bytes) / static_cast<qreal>(total)));
            QColor c = t.dim;
            if (part.is_esp) c = QColor(0xD9, 0x77, 0x06);
            else if (part.os_kind == OsKind::FinnOS) c = t.accent;
            else if (part.os_kind == OsKind::Windows) c = QColor(0x02, 0x84, 0xC7);
            else if (part.os_kind == OsKind::MacOS) c = QColor(0x64, 0x74, 0x8B);
            else if (part.os_kind == OsKind::Linux) c = QColor(0x16, 0xA3, 0x4A);
            p.fillRect(QRectF(x, full.top(), std::min(seg_w - 1.5, full.right() - x), full.height()), c);
            x += seg_w;
            if (x >= full.right()) break;
        }
    }
private:
    DiskInfo disk_;
};

// Embedded interactive draggable Terminal window inside Recovery Mode
// (works identically on eglfs, linuxfb, and Wayland — never exits the UI).
class RecoveryTerminalWindow final : public QWidget {
    Q_OBJECT
public:
    explicit RecoveryTerminalWindow(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(760, 480);
        setAttribute(Qt::WA_StyledBackground, false);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(1, 1, 1, 1);
        root->setSpacing(0);

        // Draggable titlebar
        titlebar_ = new QWidget(this);
        titlebar_->setFixedHeight(38);
        auto* tb = new QHBoxLayout(titlebar_);
        tb->setContentsMargins(14, 0, 10, 0);
        tb->setSpacing(8);

        auto* title_lbl = new QLabel(QStringLiteral("Terminal"), titlebar_);
        title_lbl->setFont(oobe_font(13, QFont::Medium));
        title_lbl->setStyleSheet(QStringLiteral("color: #EAEAEA; background: transparent;"));
        tb->addWidget(title_lbl);
        tb->addStretch(1);

        // Simple painted header pill buttons
        class MiniTermBtn final : public QAbstractButton {
        public:
            MiniTermBtn(QString label, QColor hover_bg, QWidget* p)
                : QAbstractButton(p), label_(std::move(label)), hover_bg_(hover_bg) {
                setCursor(Qt::PointingHandCursor);
                setFixedSize(label_.size() <= 1 ? 26 : (label_.size() * 8 + 16), 24);
            }
        protected:
            void enterEvent(QEnterEvent*) override { hov_ = true; update(); }
            void leaveEvent(QEvent*) override { hov_ = false; update(); }
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(hov_ ? hover_bg_ : QColor(255, 255, 255, 22));
                p.drawRoundedRect(rect(), 6.0, 6.0);
                p.setPen(QColor(0xEE, 0xEE, 0xEE));
                p.setFont(oobe_font(11, QFont::Medium));
                p.drawText(rect(), Qt::AlignCenter, label_);
            }
        private:
            QString label_;
            QColor hover_bg_;
            bool hov_ = false;
        };

        auto* btn_int = new MiniTermBtn(QStringLiteral("Ctrl+C"), QColor(217, 119, 6, 180), titlebar_);
        auto* btn_clr = new MiniTermBtn(QStringLiteral("Clear"), QColor(255, 255, 255, 45), titlebar_);
        auto* btn_cls = new MiniTermBtn(QStringLiteral("×"), QColor(220, 38, 38, 210), titlebar_);
        tb->addWidget(btn_int);
        tb->addWidget(btn_clr);
        tb->addWidget(btn_cls);
        root->addWidget(titlebar_);

        output_ = new QPlainTextEdit(this);
        output_->setReadOnly(true);
        output_->setFrameShape(QFrame::NoFrame);
        output_->setFont(finn_mono_font(12.0, QFont::Normal));
        output_->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background: #141416; color: #F2F2F2; border: none; padding: 10px 14px; }"));
        output_->setMaximumBlockCount(2000);
        root->addWidget(output_, 1);

        // Input prompt bar
        auto* in_bar = new QWidget(this);
        in_bar->setFixedHeight(40);
        in_bar->setStyleSheet(QStringLiteral(
            "background: #1C1C20; border-bottom-left-radius: 13px; border-bottom-right-radius: 13px;"));
        auto* il = new QHBoxLayout(in_bar);
        il->setContentsMargins(14, 0, 14, 0);
        il->setSpacing(8);

        auto* prompt_lbl = new QLabel(QStringLiteral("#"), in_bar);
        prompt_lbl->setFont(finn_mono_font(12.5, QFont::Bold));
        prompt_lbl->setStyleSheet(QStringLiteral("color: #60A5FA; background: transparent;"));
        il->addWidget(prompt_lbl);

        input_ = new QLineEdit(in_bar);
        input_->setFrame(false);
        input_->setFont(finn_mono_font(12.5, QFont::Normal));
        input_->setPlaceholderText(QStringLiteral("Type a command (e.g. lsblk, loadkeys it, finn-install)..."));
        input_->setStyleSheet(QStringLiteral(
            "QLineEdit { background: transparent; color: #FFFFFF; border: none; }"));
        input_->installEventFilter(this);
        il->addWidget(input_, 1);
        root->addWidget(in_bar);

        connect(input_, &QLineEdit::returnPressed, this, &RecoveryTerminalWindow::runInputLine);
        connect(btn_clr, &QAbstractButton::clicked, output_, &QPlainTextEdit::clear);
        connect(btn_int, &QAbstractButton::clicked, this, &RecoveryTerminalWindow::sendInterrupt);
        connect(btn_cls, &QAbstractButton::clicked, this, &QWidget::hide);

        startShell();
    }

    ~RecoveryTerminalWindow() override {
        if (shell_) {
            shell_->kill();
            shell_->waitForFinished(300);
        }
    }

    void focusInput() {
        if (input_) input_->setFocus();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(QColor(0x18, 0x18, 0x1B, 248));
        p.setPen(QPen(QColor(255, 255, 255, 36), 1.0));
        p.drawRoundedRect(r, 14.0, 14.0);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && e->position().y() <= 38.0) {
            dragging_ = true;
            drag_offset_ = e->globalPosition().toPoint() - pos();
            raise();
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_ && parentWidget()) {
            QPoint next = e->globalPosition().toPoint() - drag_offset_;
            const int max_x = std::max(0, parentWidget()->width() - width());
            const int max_y = std::max(0, parentWidget()->height() - height());
            next.setX(std::clamp(next.x(), 0, max_x));
            next.setY(std::clamp(next.y(), 0, max_y));
            move(next);
            e->accept();
            return;
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        dragging_ = false;
        QWidget::mouseReleaseEvent(e);
    }

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == input_ && ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_C) {
                sendInterrupt();
                return true;
            }
            if (ke->key() == Qt::Key_Up && !history_.isEmpty()) {
                hist_idx_ = std::max(0, hist_idx_ - 1);
                input_->setText(history_[hist_idx_]);
                return true;
            }
            if (ke->key() == Qt::Key_Down && !history_.isEmpty()) {
                hist_idx_ = std::min(static_cast<int>(history_.size()), hist_idx_ + 1);
                input_->setText(hist_idx_ < history_.size() ? history_[hist_idx_] : QString());
                return true;
            }
        }
        return QWidget::eventFilter(obj, ev);
    }

private:
    void startShell() {
        shell_ = new QProcess(this);
        shell_->setProcessChannelMode(QProcess::MergedChannels);
        connect(shell_, &QProcess::readyReadStandardOutput, this, [this]() {
            const QString chunk = QString::fromUtf8(shell_->readAllStandardOutput());
            if (!chunk.isEmpty()) {
                output_->moveCursor(QTextCursor::End);
                output_->insertPlainText(chunk);
                output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
            }
        });
        connect(shell_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this]() {
                    output_->appendPlainText(QStringLiteral("\n[shell exited — restarting]"));
                    shell_->deleteLater();
                    shell_ = nullptr;
                    QTimer::singleShot(150, this, &RecoveryTerminalWindow::startShell);
                });
        const QString sh = QFile::exists(QStringLiteral("/bin/bash"))
            ? QStringLiteral("/bin/bash") : QStringLiteral("/bin/sh");
        shell_->start(sh, {});
        output_->appendPlainText(QStringLiteral("FinnOS Recovery Terminal (bash). Type commands below.\n"));
    }

    void runInputLine() {
        if (!shell_ || shell_->state() != QProcess::Running) return;
        const QString cmd = input_->text();
        input_->clear();
        if (cmd.trimmed() == QLatin1String("clear")) {
            output_->clear();
            return;
        }
        if (!cmd.trimmed().isEmpty()) {
            history_.push_back(cmd);
            hist_idx_ = history_.size();
        }
        output_->moveCursor(QTextCursor::End);
        output_->insertPlainText(QStringLiteral("# %1\n").arg(cmd));
        shell_->write((cmd + QLatin1Char('\n')).toUtf8());
    }

    void sendInterrupt() {
        if (shell_ && shell_->processId() > 0) {
            ::kill(static_cast<pid_t>(shell_->processId()), SIGINT);
            output_->appendPlainText(QStringLiteral("^C"));
        }
    }

    QWidget* titlebar_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QLineEdit* input_ = nullptr;
    QProcess* shell_ = nullptr;
    QStringList history_;
    int hist_idx_ = 0;
    bool dragging_ = false;
    QPoint drag_offset_;
};

// Detect the live USB/ISO backing disk so we NEVER default to it or wipe it.
std::string detect_live_backing_disk() {
    for (const char* mnt : {"/run/archiso/bootmnt", "/run/archiso/airootfs", "/"}) {
        QProcess p;
        p.start(QStringLiteral("findmnt"), {QStringLiteral("-no"), QStringLiteral("SOURCE"), QString::fromLatin1(mnt)});
        if (!p.waitForFinished(1000)) continue;
        const QString src = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (!src.startsWith(QStringLiteral("/dev/"))) continue;
        QProcess p2;
        p2.start(QStringLiteral("lsblk"), {QStringLiteral("-no"), QStringLiteral("PKNAME"), src});
        if (p2.waitForFinished(1000)) {
            const QString pk = QString::fromUtf8(p2.readAllStandardOutput()).trimmed().section(QLatin1Char('\n'), 0, 0);
            if (!pk.isEmpty()) return ("/dev/" + pk).toStdString();
        }
        return src.toStdString();
    }
    return {};
}

std::vector<DiskInfo> probe_system_disks() {
    QProcess p;
    p.start(QStringLiteral("lsblk"),
            {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"),
             QStringLiteral("NAME,PATH,PKNAME,SIZE,TYPE,FSTYPE,LABEL,PARTLABEL,UUID,PARTUUID,PARTTYPE,MOUNTPOINT,MODEL,RO,RM,TRAN")});
    if (!p.waitForFinished(2500)) return {};
    const QByteArray raw = p.readAllStandardOutput();
    const std::string live = detect_live_backing_disk();
    return parse_lsblk_json(std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())), live);
}

int first_installable_disk_index(const std::vector<DiskInfo>& disks) {
    for (int i = 0; i < static_cast<int>(disks.size()); ++i) {
        if (!disks[static_cast<std::size_t>(i)].is_live_medium &&
            !disks[static_cast<std::size_t>(i)].read_only) {
            return i;
        }
    }
    return 0;
}

// Exact pixel-for-pixel OOBE split page (matching createSplitStepPage in apps/oobe/main.cpp).
QWidget* create_oobe_split_page(const QString& icon_name,
                                const QString& prompt_text,
                                const QString& sub_caption,
                                QWidget* right_column,
                                QLabel** out_sub = nullptr) {
    auto* page = new QWidget();
    page->setAutoFillBackground(false);

    auto* main_col = new QVBoxLayout(page);
    main_col->setContentsMargins(kOobeGutter, 0, kOobeGutter, 0);
    main_col->setSpacing(0);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(32);

    auto* left_host = new QWidget(page);
    auto* left = new QVBoxLayout(left_host);
    left->setContentsMargins(80, 0, 20, 0);
    left->setSpacing(0);
    left->addStretch(49);

    auto* icon_lbl = new QLabel(left_host);
    icon_lbl->setFixedSize(46, 46);
    icon_lbl->setAlignment(Qt::AlignCenter);
    left->addWidget(icon_lbl, 0, Qt::AlignHCenter);
    left->addSpacing(40);

    auto* title_lbl = new QLabel(prompt_text, left_host);
    title_lbl->setFont(oobe_font(24, QFont::Normal));
    title_lbl->setAlignment(Qt::AlignCenter);
    title_lbl->setWordWrap(true);
    title_lbl->setFixedWidth(380);
    title_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    left->addWidget(title_lbl, 0, Qt::AlignHCenter);

    QLabel* sub_lbl = nullptr;
    if (!sub_caption.isEmpty() || out_sub != nullptr) {
        left->addSpacing(10);
        sub_lbl = new QLabel(sub_caption, left_host);
        sub_lbl->setFont(oobe_font(14, QFont::Normal));
        sub_lbl->setAlignment(Qt::AlignCenter);
        sub_lbl->setWordWrap(true);
        sub_lbl->setFixedWidth(280);
        sub_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        sub_lbl->setVisible(!sub_caption.isEmpty());
        left->addWidget(sub_lbl, 0, Qt::AlignHCenter);
        if (out_sub) *out_sub = sub_lbl;
    }
    left->addStretch(51);

    auto sync_colors = [icon_lbl, icon_name, title_lbl, sub_lbl]() {
        const auto& t = FinnThemeManager::instance().tokens();
        icon_lbl->setPixmap(FinnThemeManager::instance().iconPixmap(
            icon_name, QSize(46, 46), t.ink, icon_lbl->devicePixelRatioF()));
        QPalette p1 = title_lbl->palette();
        p1.setColor(QPalette::WindowText, t.ink);
        title_lbl->setPalette(p1);
        if (sub_lbl) {
            QPalette p2 = sub_lbl->palette();
            p2.setColor(QPalette::WindowText, t.dim);
            sub_lbl->setPalette(p2);
        }
    };
    QObject::connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                     page, sync_colors);
    sync_colors();

    auto* right_host = new QWidget(page);
    auto* right_col = new QVBoxLayout(right_host);
    right_col->setContentsMargins(0, 195, 45, 160);
    right_col->setSpacing(0);
    right_column->setParent(right_host);
    right_col->addWidget(right_column, 1, Qt::AlignRight);

    row->addWidget(left_host, 1);
    row->addSpacing(132);
    row->addWidget(right_host, 1, Qt::AlignRight);
    main_col->addLayout(row, 1);
    return page;
}

class RecoveryWindow final : public FinnLayerWindow {
    Q_OBJECT
public:
    enum Page {
        PageHub = 0,
        PageInstallMode = 1,
        PageInstallTarget = 2,
        PageInstallReview = 3,
        PageInstallProgress = 4,
        PageDiskUtility = 5,
        PageBootRepair = 6,
        PageLogs = 7,
    };

    RecoveryWindow()
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-recovery")) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::Exclusive);
        setAccessibleName(QStringLiteral("FinnOS Recovery"));

        disks_ = probe_system_disks();
        detected_oses_ = detect_oses(disks_);
        selected_disk_idx_ = first_installable_disk_index(disks_);
        du_selected_disk_ = selected_disk_idx_;

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        stack_ = new FinnPageStack(this);
        // OobeSlideTransitionOverlay owns the exact OOBE slide+blur transition.
        stack_->setAnimatedTransitions(false);
        stack_->addWidget(buildHubPage());              // 0
        stack_->addWidget(buildInstallModePage());      // 1
        stack_->addWidget(buildInstallTargetPage());    // 2
        stack_->addWidget(buildInstallReviewPage());    // 3
        stack_->addWidget(buildInstallProgressPage());  // 4
        stack_->addWidget(buildDiskUtilityPage());      // 5
        stack_->addWidget(buildBootRepairPage());       // 6
        stack_->addWidget(buildLogsPage());             // 7
        root->addWidget(stack_, 1);

        // Top FinnOS branding lockup (matching OOBE)
        branding_ = new OobeBrandingHeader(this);
        branding_->raise();

        // Exact OOBE stationary Back/Next circular navigation bar
        shared_nav_box_ = new QWidget(this);
        shared_nav_box_->setFixedSize(132, 56);
        auto* nav_row = new QHBoxLayout(shared_nav_box_);
        nav_row->setContentsMargins(0, 0, 0, 0);
        nav_row->setSpacing(20);
        nav_back_ = new OobeCircleNavButton(true, false, shared_nav_box_);
        nav_next_ = new OobeCircleNavButton(false, true, shared_nav_box_);
        connect(nav_back_, &QAbstractButton::clicked, this, &RecoveryWindow::goBack);
        connect(nav_next_, &QAbstractButton::clicked, this, &RecoveryWindow::goNext);
        nav_row->addWidget(nav_back_);
        nav_row->addWidget(nav_next_);
        shared_nav_box_->raise();

        // Bottom-left utility links (always available: Terminal window, Keyboard layout, Restart, Shut Down)
        footer_links_ = new QWidget(this);
        auto* fl = new QHBoxLayout(footer_links_);
        fl->setContentsMargins(0, 0, 0, 0);
        fl->setSpacing(10);
        auto* l_term = new QuietLink(QStringLiteral("Terminal"), false, footer_links_);
        kb_link_ = new QuietLink(QStringLiteral("Keyboard: US"), false, footer_links_);
        auto* l_reb = new QuietLink(QStringLiteral("Restart"), false, footer_links_);
        auto* l_off = new QuietLink(QStringLiteral("Shut Down"), false, footer_links_);

        connect(l_term, &QAbstractButton::clicked, this, &RecoveryWindow::toggleTerminalWindow);
        connect(kb_link_, &QAbstractButton::clicked, this, &RecoveryWindow::cycleKeyboardLayout);
        connect(l_reb, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
        });
        connect(l_off, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
        });
        fl->addWidget(l_term);
        fl->addWidget(kb_link_);
        fl->addWidget(l_reb);
        fl->addWidget(l_off);
        footer_links_->raise();

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        updateChrome();
        positionChrome();
    }

    void openPage(const QString& page) {
        const QString p = page.trimmed().toLower();
        if (p == QLatin1String("install")) {
            goTo(PageInstallMode, false);
        } else if (p == QLatin1String("reinstall")) {
            selected_mode_ = InstallMode::ReinstallPreserveData;
            refreshTargets();
            goTo(PageInstallTarget, false);
        } else if (p == QLatin1String("disk") || p == QLatin1String("disks") || p == QLatin1String("partition")) {
            du_return_to_target_ = false;
            refreshDiskUtility();
            goTo(PageDiskUtility, false);
        } else if (p == QLatin1String("boot") || p == QLatin1String("bootloader")) {
            goTo(PageBootRepair, false);
        } else if (p == QLatin1String("logs") || p == QLatin1String("info")) {
            refreshLogs();
            goTo(PageLogs, false);
        } else {
            goTo(PageHub, false);
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.fillRect(rect(), t.bone);
    }

    void resizeEvent(QResizeEvent* e) override {
        FinnLayerWindow::resizeEvent(e);
        positionChrome();
    }

    void keyPressEvent(QKeyEvent* e) override {
        if ((e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) == (Qt::ControlModifier | Qt::AltModifier) &&
            e->key() == Qt::Key_T) {
            toggleTerminalWindow();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            if (terminal_win_ && terminal_win_->isVisible()) {
                terminal_win_->hide();
                return;
            }
            if (stack_->currentIndex() == PageHub) close();
            else goBack();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (terminal_win_ && terminal_win_->isVisible()) {
                FinnLayerWindow::keyPressEvent(e);
                return;
            }
            if (nav_next_->isVisible() && nav_next_->isEnabled()) goNext();
            return;
        }
        FinnLayerWindow::keyPressEvent(e);
    }

private:
    void positionChrome() {
        if (branding_) {
            branding_->move(kOobeGutter, 44);
            branding_->raise();
        }
        if (shared_nav_box_) {
            const int y = std::max(16, height() - 145 - 56);
            const int x = (width() - shared_nav_box_->width()) / 2;
            shared_nav_box_->move(x, y);
            shared_nav_box_->raise();
        }
        if (footer_links_) {
            footer_links_->adjustSize();
            footer_links_->move(kOobeGutter, height() - 48);
            footer_links_->raise();
        }
        if (terminal_win_ && terminal_win_->isVisible()) {
            terminal_win_->raise();
        }
    }

    void toggleTerminalWindow() {
        if (!terminal_win_) {
            terminal_win_ = new RecoveryTerminalWindow(this);
        }
        if (terminal_win_->isVisible()) {
            terminal_win_->hide();
        } else {
            const int x = std::max(24, (width() - terminal_win_->width()) / 2);
            const int y = std::max(24, (height() - terminal_win_->height()) / 2);
            terminal_win_->move(x, y);
            terminal_win_->show();
            terminal_win_->raise();
            terminal_win_->focusInput();
        }
    }

    void cycleKeyboardLayout() {
        struct Kb { const char* code; const char* label; };
        static constexpr Kb kLayouts[] = {
            {"us", "Keyboard: US"},
            {"it", "Keyboard: IT"},
            {"de", "Keyboard: DE"},
            {"fr", "Keyboard: FR"},
            {"es", "Keyboard: ES"},
            {"uk", "Keyboard: UK"},
        };
        kb_idx_ = (kb_idx_ + 1) % static_cast<int>(sizeof(kLayouts) / sizeof(kLayouts[0]));
        const QString code = QString::fromLatin1(kLayouts[kb_idx_].code);
        kb_link_->setLabelText(QString::fromLatin1(kLayouts[kb_idx_].label));
        footer_links_->adjustSize();
        QProcess::startDetached(QStringLiteral("loadkeys"), {code});
        FinnThemeManager::instance().setKeyboardLayout(code == QLatin1String("uk") ? QStringLiteral("gb") : code, {});
    }

    QPixmap grabTransparentStack() const {
        if (!stack_ || stack_->size().isEmpty()) return {};
        const qreal dpr = devicePixelRatioF();
        QPixmap pix(qRound(stack_->width() * dpr), qRound(stack_->height() * dpr));
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        const bool was_visible = stack_->isVisible();
        if (!was_visible) stack_->setVisible(true);
        stack_->render(&pix, QPoint(), QRegion(), QWidget::DrawChildren);
        if (!was_visible) stack_->setVisible(false);
        return pix;
    }

    void goTo(Page p, bool animate = true) {
        const int idx = static_cast<int>(p);
        const int prev = stack_->currentIndex();
        if (prev == idx) {
            updateChrome();
            return;
        }

        const bool do_slide = animate && prev >= 0 && isVisible() &&
                              !FinnThemeManager::instance().tokens().reduced_motion;
        QPixmap old_snap;
        if (do_slide) {
            if (slide_overlay_) {
                old_snap = slide_overlay_->grabCurrentFrame();
                slide_overlay_->cancel();
                slide_overlay_.clear();
            } else {
                old_snap = grabTransparentStack();
            }
        } else if (slide_overlay_) {
            slide_overlay_->finish();
            slide_overlay_.clear();
        }

        stack_->setCurrentIndex(idx);
        updateChrome();

        if (do_slide && !old_snap.isNull()) {
            stack_->widget(idx)->resize(stack_->size());
            if (stack_->widget(idx)->layout()) {
                stack_->widget(idx)->layout()->activate();
            }
            const QPixmap new_snap = grabTransparentStack();
            slide_overlay_ = new OobeSlideTransitionOverlay(
                this, stack_, std::move(old_snap), new_snap, idx > prev);
            positionChrome();
        }
    }

    void updateChrome() {
        if (!nav_back_ || !nav_next_ || !shared_nav_box_) return;
        const int idx = stack_->currentIndex();
        const bool is_hub = (idx == PageHub);
        const bool is_prog = (idx == PageInstallProgress);
        shared_nav_box_->setVisible(!is_prog);
        nav_back_->setVisible(!is_hub && !is_prog);
        nav_next_->setVisible(!is_prog && idx != PageDiskUtility && idx != PageLogs);
        if (idx == PageHub) {
            nav_next_->setEnabled(hub_choice_ >= 0);
        } else if (idx == PageInstallTarget) {
            const bool has_valid = !disks_.empty() &&
                !disks_[static_cast<std::size_t>(selected_disk_idx_)].is_live_medium;
            nav_next_->setEnabled(has_valid);
        } else if (idx == PageInstallReview) {
            nav_next_->setEnabled(current_plan_.valid);
        } else {
            nav_next_->setEnabled(true);
        }
        positionChrome();
    }

    void goBack() {
        const int idx = stack_->currentIndex();
        if (idx == PageDiskUtility && du_return_to_target_) {
            du_return_to_target_ = false;
            disks_ = probe_system_disks();
            detected_oses_ = detect_oses(disks_);
            refreshTargets();
            goTo(PageInstallTarget);
            return;
        }
        if (idx == PageInstallMode || idx == PageDiskUtility ||
            idx == PageBootRepair || idx == PageLogs || idx == PageInstallProgress) {
            goTo(PageHub);
        } else if (idx == PageInstallTarget) {
            goTo(PageInstallMode);
        } else if (idx == PageInstallReview) {
            goTo(PageInstallTarget);
        }
    }

    void goNext() {
        const int idx = stack_->currentIndex();
        if (idx == PageHub) {
            if (hub_choice_ == 0) goTo(PageInstallMode);
            else if (hub_choice_ == 1) {
                selected_mode_ = InstallMode::ReinstallPreserveData;
                refreshTargets();
                goTo(PageInstallTarget);
            } else if (hub_choice_ == 2) {
                du_return_to_target_ = false;
                disks_ = probe_system_disks();
                refreshDiskUtility();
                goTo(PageDiskUtility);
            } else if (hub_choice_ == 3) {
                goTo(PageBootRepair);
            } else if (hub_choice_ == 4) {
                refreshLogs();
                goTo(PageLogs);
            }
        } else if (idx == PageInstallMode) {
            disks_ = probe_system_disks();
            detected_oses_ = detect_oses(disks_);
            refreshTargets();
            goTo(PageInstallTarget);
        } else if (idx == PageInstallTarget) {
            refreshReview();
            goTo(PageInstallReview);
        } else if (idx == PageInstallReview) {
            startInstall();
        } else if (idx == PageBootRepair) {
            runBootRepair();
        }
    }

    // ── Page 0: Recovery Hub ─────────────────────────────────────────────────
    QWidget* buildHubPage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(kOobeContentWidth + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        auto* prog_list = new OobeProgressiveList(list_wrap);
        auto* blur_ov = new ProgressiveBlurOverlay(prog_list, list_wrap);
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(prog_list, 0, 0);
        stack_lay->addWidget(blur_ov, 0, 0);

        const QStringList items = {
            QStringLiteral("Install FinnOS"),
            QStringLiteral("Reinstall FinnOS"),
            QStringLiteral("Partition & Disks"),
            QStringLiteral("Repair Startup"),
            QStringLiteral("System Logs"),
        };

        for (int i = 0; i < items.size(); ++i) {
            auto* row = new OobeOptionRow(items[i], {}, i == 0, prog_list->hostWidget());
            connect(row, &OobeOptionRow::clicked, this, [this, i, blur_ov]() {
                hub_choice_ = i;
                for (int j = 0; j < static_cast<int>(hub_rows_.size()); ++j) {
                    hub_rows_[static_cast<std::size_t>(j)]->setSelected(j == i);
                }
                blur_ov->invalidateCache();
                updateChrome();
            });
            hub_rows_.push_back(row);
            prog_list->listLayout()->addWidget(row);
        }
        prog_list->listLayout()->addStretch(1);

        QString sub;
        if (!detected_oses_.empty()) {
            QStringList names;
            for (const auto& os : detected_oses_) names << QString::fromStdString(os.name);
            sub = QStringLiteral("Found: ") + names.join(QStringLiteral(", "));
        }
        return create_oobe_split_page(QStringLiteral("boot-mark"),
                                      QStringLiteral("What do you need?"),
                                      sub, list_wrap);
    }

    // ── Page 1: Install Mode ─────────────────────────────────────────────────
    QWidget* buildInstallModePage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(kOobeContentWidth + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        auto* prog_list = new OobeProgressiveList(list_wrap);
        auto* blur_ov = new ProgressiveBlurOverlay(prog_list, list_wrap);
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(prog_list, 0, 0);
        stack_lay->addWidget(blur_ov, 0, 0);

        struct Opt { InstallMode mode; const char* title; const char* meta; };
        const Opt opts[] = {
            {InstallMode::CleanErase, "Erase disk", "Clean"},
            {InstallMode::CleanEraseAB, "Erase disk (A/B)", "Rollback"},
            {InstallMode::DualBootResize, "Alongside existing OS", "Dual-boot"},
            {InstallMode::TargetPartition, "Specific partition", "Manual"},
        };
        for (int i = 0; i < 4; ++i) {
            auto* row = new OobeOptionRow(QString::fromUtf8(opts[i].title),
                                          QString::fromUtf8(opts[i].meta),
                                          i == 0, prog_list->hostWidget());
            const InstallMode m = opts[i].mode;
            connect(row, &OobeOptionRow::clicked, this, [this, i, m, blur_ov]() {
                selected_mode_ = m;
                for (int j = 0; j < static_cast<int>(mode_rows_.size()); ++j) {
                    mode_rows_[static_cast<std::size_t>(j)]->setSelected(j == i);
                }
                blur_ov->invalidateCache();
            });
            mode_rows_.push_back(row);
            prog_list->listLayout()->addWidget(row);
        }
        prog_list->listLayout()->addStretch(1);

        return create_oobe_split_page(QStringLiteral("download"),
                                      QStringLiteral("How should we install?"),
                                      QStringLiteral("Personal setup runs on first boot."),
                                      list_wrap);
    }

    // ── Page 2: Pick Drive / Partition + Dual-Boot Resize ────────────────────
    QWidget* buildInstallTargetPage() {
        auto* host = new QWidget();
        host->setFixedWidth(kOobeContentWidth + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(10);

        target_list_host_ = new QWidget(host);
        target_list_lay_ = new QVBoxLayout(target_list_host_);
        target_list_lay_->setContentsMargins(0, 0, 0, 0);
        target_list_lay_->setSpacing(8);
        col->addWidget(target_list_host_);

        target_bar_ = new SlimPartitionBar(host);
        col->addWidget(target_bar_);

        resize_host_ = new QWidget(host);
        auto* rl = new QVBoxLayout(resize_host_);
        rl->setContentsMargins(2, 6, 2, 0);
        rl->setSpacing(6);
        resize_label_ = new QLabel(resize_host_);
        resize_label_->setFont(oobe_font(13, QFont::Normal));
        resize_slider_ = new QSlider(Qt::Horizontal, resize_host_);
        resize_slider_->setRange(16, 200);
        resize_slider_->setValue(40);
        connect(resize_slider_, &QSlider::valueChanged, this, [this](int gb) {
            resize_gib_ = gb;
            resize_label_->setText(QStringLiteral("Space for FinnOS: %1 GiB").arg(gb));
        });
        rl->addWidget(resize_label_);
        rl->addWidget(resize_slider_);
        resize_host_->setVisible(false);
        col->addWidget(resize_host_);

        auto* edit_parts = new QuietLink(QStringLiteral("Partition drive..."), false, host);
        connect(edit_parts, &QAbstractButton::clicked, this, [this]() {
            du_return_to_target_ = true;
            du_selected_disk_ = selected_disk_idx_;
            refreshDiskUtility();
            goTo(PageDiskUtility);
        });
        col->addWidget(edit_parts, 0, Qt::AlignLeft);
        col->addStretch(1);

        refreshTargets();
        return create_oobe_split_page(QStringLiteral("hard-drive"),
                                      QStringLiteral("Pick a drive"),
                                      {}, host, &target_sub_lbl_);
    }

    void refreshTargets() {
        if (!target_list_lay_) return;
        while (QLayoutItem* it = target_list_lay_->takeAt(0)) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        target_rows_.clear();
        target_part_paths_.clear();

        if (disks_.empty()) {
            auto* empty = new QLabel(QStringLiteral("No internal drives found."), target_list_host_);
            empty->setFont(oobe_font(15, QFont::Normal));
            target_list_lay_->addWidget(empty);
            resize_host_->setVisible(false);
            updateChrome();
            return;
        }

        const bool pick_partition =
            (selected_mode_ == InstallMode::TargetPartition ||
             selected_mode_ == InstallMode::ReinstallPreserveData);

        if (pick_partition) {
            int row_idx = 0;
            for (int di = 0; di < static_cast<int>(disks_.size()); ++di) {
                const auto& d = disks_[static_cast<std::size_t>(di)];
                if (d.is_live_medium) continue;
                for (const auto& part : d.partitions) {
                    if (part.is_esp) continue;
                    const int idx = row_idx++;
                    const QString lbl = QString::fromStdString(part.path + (part.fstype.empty() ? "" : "  " + part.fstype));
                    const QString meta = QString::fromStdString(format_bytes(part.size_bytes));
                    auto* row = new OobeOptionRow(lbl, meta, idx == 0, target_list_host_);
                    const std::string ppath = part.path;
                    connect(row, &OobeOptionRow::clicked, this, [this, idx, di, ppath]() {
                        selected_disk_idx_ = di;
                        selected_partition_path_ = ppath;
                        for (int j = 0; j < static_cast<int>(target_rows_.size()); ++j) {
                            target_rows_[static_cast<std::size_t>(j)]->setSelected(j == idx);
                        }
                        syncTargetDetails();
                    });
                    if (idx == 0) {
                        selected_disk_idx_ = di;
                        selected_partition_path_ = ppath;
                    }
                    target_part_paths_.push_back(ppath);
                    target_rows_.push_back(row);
                    target_list_lay_->addWidget(row);
                }
            }
            if (target_rows_.empty()) {
                auto* empty = new QLabel(QStringLiteral("No partitions found. Use Partition drive first."), target_list_host_);
                empty->setFont(oobe_font(14, QFont::Normal));
                empty->setWordWrap(true);
                target_list_lay_->addWidget(empty);
            }
        } else {
            selected_partition_path_.clear();
            if (selected_disk_idx_ >= static_cast<int>(disks_.size()) ||
                disks_[static_cast<std::size_t>(selected_disk_idx_)].is_live_medium) {
                selected_disk_idx_ = first_installable_disk_index(disks_);
            }
            for (int i = 0; i < static_cast<int>(disks_.size()); ++i) {
                const auto& d = disks_[static_cast<std::size_t>(i)];
                QString label = QString::fromStdString(d.model.empty() ? d.path : d.model);
                QString meta = QString::fromStdString(format_bytes(d.size_bytes));
                if (d.is_live_medium) meta = QStringLiteral("Live USB");
                auto* row = new OobeOptionRow(label, meta, i == selected_disk_idx_, target_list_host_);
                row->setEnabled(!d.is_live_medium && !d.read_only);
                connect(row, &OobeOptionRow::clicked, this, [this, i]() {
                    selected_disk_idx_ = i;
                    for (int j = 0; j < static_cast<int>(target_rows_.size()); ++j) {
                        target_rows_[static_cast<std::size_t>(j)]->setSelected(j == i);
                    }
                    syncTargetDetails();
                    updateChrome();
                });
                target_rows_.push_back(row);
                target_list_lay_->addWidget(row);
            }
        }
        syncTargetDetails();
        updateChrome();
    }

    void syncTargetDetails() {
        if (disks_.empty()) return;
        const auto& d = disks_[static_cast<std::size_t>(selected_disk_idx_)];
        target_bar_->setDisk(d);
        const bool is_dual = (selected_mode_ == InstallMode::DualBootResize);
        resize_host_->setVisible(is_dual);
        if (is_dual) {
            const int max_gb = std::max(16, static_cast<int>(d.size_bytes / (1024ULL * 1024ULL * 1024ULL)) - 16);
            resize_slider_->setRange(16, std::max(16, max_gb));
            resize_slider_->setValue(std::clamp(resize_gib_, 16, std::max(16, max_gb)));
            resize_label_->setText(QStringLiteral("Space for FinnOS: %1 GiB").arg(resize_slider_->value()));
        }
    }

    // ── Page 3: Quiet Install Summary ────────────────────────────────────────
    QWidget* buildInstallReviewPage() {
        auto* host = new QWidget();
        host->setFixedWidth(kOobeContentWidth + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(12);

        review_label_ = new QLabel(host);
        review_label_->setFont(oobe_font(15, QFont::Normal));
        review_label_->setWordWrap(true);
        col->addWidget(review_label_);
        col->addStretch(1);

        return create_oobe_split_page(QStringLiteral("shield"),
                                      QStringLiteral("Ready to install?"),
                                      QStringLiteral("Press > to begin."),
                                      host);
    }

    void refreshReview() {
        InstallRequest req;
        req.mode = selected_mode_;
        if (!disks_.empty()) {
            const auto& d = disks_[static_cast<std::size_t>(selected_disk_idx_)];
            req.target_disk = d.path;
            for (const auto& p : d.partitions) {
                if (p.is_esp && req.esp_partition.empty()) req.esp_partition = p.path;
                if (req.mode == InstallMode::DualBootResize && req.resize_partition.empty() &&
                    (p.fstype == "ntfs" || p.fstype == "ext4" || p.fstype == "btrfs")) {
                    req.resize_partition = p.path;
                    const std::uint64_t want = static_cast<std::uint64_t>(resize_gib_) * 1024ULL * 1024ULL * 1024ULL;
                    const std::uint64_t new_bytes = (p.size_bytes > want) ? (p.size_bytes - want) : (p.size_bytes / 2);
                    req.resize_new_size_mb = new_bytes / (1024ULL * 1024ULL);
                }
            }
            if (req.mode == InstallMode::TargetPartition || req.mode == InstallMode::ReinstallPreserveData) {
                req.target_partition = selected_partition_path_;
                if (req.target_partition.empty()) {
                    for (const auto& p : d.partitions) {
                        if (!p.is_esp) {
                            req.target_partition = p.path;
                            break;
                        }
                    }
                }
            }
        }
        req.confirmed = true;
        current_plan_ = plan_install(req, disks_);

        if (!current_plan_.valid) {
            review_label_->setText(QString::fromStdString(current_plan_.error));
        } else {
            QStringList lines;
            for (const auto& step : current_plan_.steps) {
                lines << QStringLiteral("• ") + QString::fromStdString(step);
            }
            lines << QStringLiteral("\nLanguage, Wi-Fi, account, and hostname will be configured on first boot.");
            review_label_->setText(lines.join(QLatin1Char('\n')));
        }
        updateChrome();
    }

    // ── Page 4: Install Progress ─────────────────────────────────────────────
    QWidget* buildInstallProgressPage() {
        auto* host = new QWidget();
        host->setFixedWidth(kOobeContentWidth + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(12);

        install_log_ = new QPlainTextEdit(host);
        install_log_->setReadOnly(true);
        install_log_->setFrameShape(QFrame::NoFrame);
        install_log_->setFixedHeight(320);
        install_log_->setFont(finn_mono_font(11.0, QFont::Normal));
        col->addWidget(install_log_);
        col->addStretch(1);

        return create_oobe_split_page(QStringLiteral("download"),
                                      QStringLiteral("Installing FinnOS"),
                                      QStringLiteral("This takes a few minutes."),
                                      host);
    }

    void startInstall() {
        if (!current_plan_.valid || current_plan_.finn_install_args.empty()) return;
        goTo(PageInstallProgress);
        install_log_->clear();
        install_log_->appendPlainText(QStringLiteral("Running installer..."));

        auto* proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        QStringList args;
        for (const auto& a : current_plan_.finn_install_args) {
            args << QString::fromStdString(a);
        }
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
            install_log_->appendPlainText(QString::fromUtf8(proc->readAllStandardOutput()).trimmed());
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc](int code, QProcess::ExitStatus st) {
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        install_log_->appendPlainText(QStringLiteral("\nDone. Remove USB media and restart."));
                    } else {
                        install_log_->appendPlainText(QStringLiteral("\nInstall exited with code %1.").arg(code));
                    }
                    shared_nav_box_->setVisible(true);
                    nav_back_->setVisible(true);
                    nav_next_->setVisible(false);
                });
        proc->start(QString::fromUtf8(finn::kFinnInstallBin), args);
    }

    // ── Page 5: Partition & Disk Utility ─────────────────────────────────────
    QWidget* buildDiskUtilityPage() {
        auto* host = new QWidget();
        host->setFixedWidth(kOobeContentWidth + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(10);

        // Disk selector row (lets user switch between internal drives cleanly)
        du_disk_host_ = new QWidget(host);
        du_disk_lay_ = new QVBoxLayout(du_disk_host_);
        du_disk_lay_->setContentsMargins(0, 0, 0, 0);
        du_disk_lay_->setSpacing(4);
        col->addWidget(du_disk_host_);

        du_bar_ = new SlimPartitionBar(host);
        col->addWidget(du_bar_);

        du_list_host_ = new QWidget(host);
        du_list_lay_ = new QVBoxLayout(du_list_host_);
        du_list_lay_->setContentsMargins(0, 0, 0, 0);
        du_list_lay_->setSpacing(6);
        col->addWidget(du_list_host_);

        // Inline partitioning action bar
        auto* act_row = new QHBoxLayout();
        act_row->setSpacing(6);
        auto* b_new = new QuietLink(QStringLiteral("New"), false, host);
        auto* b_del = new QuietLink(QStringLiteral("Delete"), true, host);
        auto* b_fmt = new QuietLink(QStringLiteral("Format"), false, host);
        auto* b_chk = new QuietLink(QStringLiteral("First Aid"), false, host);
        auto* b_gpt = new QuietLink(QStringLiteral("New GPT"), true, host);
        auto* b_ref = new QuietLink(QStringLiteral("Refresh"), false, host);
        connect(b_new, &QAbstractButton::clicked, this, [this]() {
            du_create_panel_->setVisible(!du_create_panel_->isVisible());
            updateNewPartitionSliderBounds();
        });
        connect(b_del, &QAbstractButton::clicked, this, &RecoveryWindow::deleteSelectedPartition);
        connect(b_fmt, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FormatExt4); });
        connect(b_chk, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FirstAidCheck); });
        connect(b_gpt, &QAbstractButton::clicked, this, &RecoveryWindow::wipeDiskGpt);
        connect(b_ref, &QAbstractButton::clicked, this, [this]() {
            disks_ = probe_system_disks();
            detected_oses_ = detect_oses(disks_);
            refreshDiskUtility();
            du_status_->setText(QStringLiteral("Refreshed disk table."));
        });
        act_row->addWidget(b_new);
        act_row->addWidget(b_del);
        act_row->addWidget(b_fmt);
        act_row->addWidget(b_chk);
        act_row->addWidget(b_gpt);
        act_row->addWidget(b_ref);
        act_row->addStretch(1);
        col->addLayout(act_row);

        // Inline New Partition panel
        du_create_panel_ = new QWidget(host);
        auto* cp = new QVBoxLayout(du_create_panel_);
        cp->setContentsMargins(0, 4, 0, 4);
        cp->setSpacing(6);

        du_new_size_lbl_ = new QLabel(QStringLiteral("New partition: 32 GiB (ext4)"), du_create_panel_);
        du_new_size_lbl_->setFont(oobe_font(13, QFont::Normal));
        du_new_size_slider_ = new QSlider(Qt::Horizontal, du_create_panel_);
        du_new_size_slider_->setRange(1, 512);
        du_new_size_slider_->setValue(32);
        connect(du_new_size_slider_, &QSlider::valueChanged, this, [this](int gb) {
            du_new_size_gib_ = gb;
            const bool is_max = (gb >= du_new_size_slider_->maximum());
            du_new_size_lbl_->setText(is_max
                ? QStringLiteral("New partition: All remaining space (%1)").arg(du_new_fs_)
                : QStringLiteral("New partition: %1 GiB (%2)").arg(gb).arg(du_new_fs_));
        });

        auto* cp_row = new QHBoxLayout();
        cp_row->setSpacing(10);
        auto* b_fs = new QuietLink(QStringLiteral("Filesystem: ext4"), false, du_create_panel_);
        connect(b_fs, &QAbstractButton::clicked, this, [this, b_fs]() {
            du_new_fs_ = (du_new_fs_ == QLatin1String("ext4")) ? QStringLiteral("btrfs")
                       : (du_new_fs_ == QLatin1String("btrfs")) ? QStringLiteral("fat32")
                       : QStringLiteral("ext4");
            b_fs->setLabelText(QStringLiteral("Filesystem: %1").arg(du_new_fs_));
            const bool is_max = (du_new_size_gib_ >= du_new_size_slider_->maximum());
            du_new_size_lbl_->setText(is_max
                ? QStringLiteral("New partition: All remaining space (%1)").arg(du_new_fs_)
                : QStringLiteral("New partition: %1 GiB (%2)").arg(du_new_size_gib_).arg(du_new_fs_));
        });
        auto* b_apply = new QuietLink(QStringLiteral("Create & Format"), false, du_create_panel_);
        connect(b_apply, &QAbstractButton::clicked, this, &RecoveryWindow::createNewPartition);
        cp_row->addWidget(b_fs);
        cp_row->addWidget(b_apply);
        cp_row->addStretch(1);

        cp->addWidget(du_new_size_lbl_);
        cp->addWidget(du_new_size_slider_);
        cp->addLayout(cp_row);
        du_create_panel_->setVisible(false);
        col->addWidget(du_create_panel_);

        du_status_ = new QLabel(host);
        du_status_->setFont(oobe_font(12, QFont::Normal));
        du_status_->setWordWrap(true);
        col->addWidget(du_status_);
        col->addStretch(1);

        refreshDiskUtility();
        return create_oobe_split_page(QStringLiteral("hard-drive"),
                                      QStringLiteral("Disks"),
                                      QStringLiteral("Create, delete, or format partitions."),
                                      host);
    }

    void updateNewPartitionSliderBounds() {
        if (disks_.empty() || !du_new_size_slider_) return;
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        std::uint64_t used = 0;
        for (const auto& p : d.partitions) used += p.size_bytes;
        const std::uint64_t free_bytes = (d.size_bytes > used) ? (d.size_bytes - used) : 0;
        const int free_gib = std::max(1, static_cast<int>(free_bytes / (1024ULL * 1024ULL * 1024ULL)));
        du_new_size_slider_->setRange(1, free_gib);
        du_new_size_slider_->setValue(free_gib);
        du_new_size_gib_ = free_gib;
        du_new_size_lbl_->setText(
            QStringLiteral("New partition: All remaining space (~%1 GiB, %2)").arg(free_gib).arg(du_new_fs_));
    }

    void refreshDiskUtility() {
        if (!du_list_lay_ || !du_disk_lay_) return;
        while (QLayoutItem* it = du_disk_lay_->takeAt(0)) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        while (QLayoutItem* it = du_list_lay_->takeAt(0)) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        du_disk_rows_.clear();
        du_part_rows_.clear();
        du_parts_.clear();

        if (disks_.empty()) {
            auto* l = new QLabel(QStringLiteral("No disks detected."), du_list_host_);
            l->setFont(oobe_font(15, QFont::Normal));
            du_list_lay_->addWidget(l);
            return;
        }
        if (du_selected_disk_ >= static_cast<int>(disks_.size()) ||
            disks_[static_cast<std::size_t>(du_selected_disk_)].is_live_medium) {
            du_selected_disk_ = first_installable_disk_index(disks_);
        }

        // Disk selector rows
        for (int di = 0; di < static_cast<int>(disks_.size()); ++di) {
            const auto& d = disks_[static_cast<std::size_t>(di)];
            const QString lbl = QString::fromStdString(
                (d.model.empty() ? d.path : (d.model + " (" + d.name + ")")));
            const QString meta = d.is_live_medium
                ? QStringLiteral("Live USB")
                : QString::fromStdString(format_bytes(d.size_bytes));
            auto* row = new OobeOptionRow(lbl, meta, di == du_selected_disk_, du_disk_host_);
            row->setEnabled(!d.is_live_medium && !d.read_only);
            connect(row, &OobeOptionRow::clicked, this, [this, di]() {
                du_selected_disk_ = di;
                du_selected_part_ = 0;
                refreshDiskUtility();
            });
            du_disk_rows_.push_back(row);
            du_disk_lay_->addWidget(row);
        }

        const auto& sel_disk = disks_[static_cast<std::size_t>(du_selected_disk_)];
        du_bar_->setDisk(sel_disk);

        if (du_selected_part_ >= static_cast<int>(sel_disk.partitions.size())) {
            du_selected_part_ = 0;
        }

        for (int pi = 0; pi < static_cast<int>(sel_disk.partitions.size()); ++pi) {
            const auto& part = sel_disk.partitions[static_cast<std::size_t>(pi)];
            du_parts_.push_back(part);
            const QString lbl = QString::fromStdString(
                part.path + (part.fstype.empty() ? "  (raw)" : "  " + part.fstype));
            const QString meta = QString::fromStdString(format_bytes(part.size_bytes));
            auto* row = new OobeOptionRow(lbl, meta, pi == du_selected_part_, du_list_host_);
            connect(row, &OobeOptionRow::clicked, this, [this, pi]() {
                du_selected_part_ = pi;
                for (int j = 0; j < static_cast<int>(du_part_rows_.size()); ++j) {
                    du_part_rows_[static_cast<std::size_t>(j)]->setSelected(j == pi);
                }
            });
            du_part_rows_.push_back(row);
            du_list_lay_->addWidget(row);
        }

        if (du_parts_.empty()) {
            auto* row = new OobeOptionRow(
                QString::fromStdString(sel_disk.path + "  (unpartitioned)"),
                QString::fromStdString(format_bytes(sel_disk.size_bytes)),
                true, du_list_host_);
            du_part_rows_.push_back(row);
            du_list_lay_->addWidget(row);
        }
        updateNewPartitionSliderBounds();
    }

    void unmountDiskPartitions(const DiskInfo& d) {
        if (d.is_live_medium) return;
        for (const auto& p : d.partitions) {
            QProcess::execute(QStringLiteral("swapoff"), {QString::fromStdString(p.path)});
            QProcess::execute(QStringLiteral("umount"), {QStringLiteral("-f"), QString::fromStdString(p.path)});
        }
    }

    void runDiskCommand(const QString& program, const QStringList& args, const QString& ok_msg) {
        if (QStandardPaths::findExecutable(program).isEmpty()) {
            du_status_->setText(QStringLiteral("%1 is not available on this host.").arg(program));
            return;
        }
        du_status_->setText(QStringLiteral("Running %1...").arg(program));
        auto* proc = new QProcess(this);
        // Merge stdout + stderr so sgdisk/parted messages are always captured.
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc, ok_msg](int code, QProcess::ExitStatus st) {
                    const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        QProcess::execute(QStringLiteral("partprobe"), {});
                        QProcess::execute(QStringLiteral("udevadm"), {QStringLiteral("settle"), QStringLiteral("--timeout=5")});
                        disks_ = probe_system_disks();
                        detected_oses_ = detect_oses(disks_);
                        refreshDiskUtility();
                        du_status_->setText(ok_msg);
                    } else {
                        const QString last_line = out.section(QLatin1Char('\n'), -1).trimmed();
                        du_status_->setText(last_line.isEmpty()
                            ? QStringLiteral("Operation failed (exit %1).").arg(code)
                            : last_line);
                    }
                });
        proc->start(program, args);
    }

    void createNewPartition() {
        if (disks_.empty()) {
            du_status_->setText(QStringLiteral("No target disk available."));
            return;
        }
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        if (d.is_live_medium) {
            du_status_->setText(QStringLiteral("Cannot partition the active Live USB drive."));
            return;
        }
        unmountDiskPartitions(d);
        const QString disk_path = QString::fromStdString(d.path);
        const QString type_guid = (du_new_fs_ == QLatin1String("fat32"))
            ? QStringLiteral("ef00")
            : QStringLiteral("8300");
        const bool use_all = (du_new_size_gib_ >= du_new_size_slider_->maximum());
        const QString size_arg = use_all
            ? QStringLiteral("0:0:0")
            : QStringLiteral("0:0:+%1G").arg(du_new_size_gib_);
        const QString mkfs_cmd = (du_new_fs_ == QLatin1String("fat32"))
            ? QStringLiteral("mkfs.vfat -F32 \"$NEWPART\"")
            : (du_new_fs_ == QLatin1String("btrfs"))
            ? QStringLiteral("mkfs.btrfs -f \"$NEWPART\"")
            : QStringLiteral("mkfs.ext4 -F \"$NEWPART\"");

        const QString script = QStringLiteral(
            "set -e; "
            "DISK=\"$1\"; SIZE=\"$2\"; TYPE=\"$3\"; "
            "sgdisk -p \"$DISK\" >/dev/null 2>&1 || sgdisk -o \"$DISK\"; "
            "sgdisk -n \"$SIZE\" -t \"0:$TYPE\" \"$DISK\"; "
            "partprobe \"$DISK\" 2>/dev/null || true; "
            "udevadm settle --timeout=10 2>/dev/null || true; "
            "NEWPART=$(lsblk -lno PATH,TYPE \"$DISK\" | awk '$2==\"part\"{p=$1} END{print p}'); "
            "if [ -b \"$NEWPART\" ]; then %1; fi").arg(mkfs_cmd);

        du_create_panel_->setVisible(false);
        runDiskCommand(QStringLiteral("bash"),
                       {QStringLiteral("-c"), script, QStringLiteral("_"), disk_path, size_arg, type_guid},
                       QStringLiteral("Created & formatted %1 partition on %2.").arg(du_new_fs_, disk_path));
    }

    void deleteSelectedPartition() {
        if (du_parts_.empty() || du_selected_part_ >= static_cast<int>(du_parts_.size())) {
            du_status_->setText(QStringLiteral("Select a partition to delete."));
            return;
        }
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        if (d.is_live_medium) {
            du_status_->setText(QStringLiteral("Cannot modify the active Live USB drive."));
            return;
        }
        const auto& part = du_parts_[static_cast<std::size_t>(du_selected_part_)];
        if (part.part_number <= 0 || part.parent_disk.empty()) {
            du_status_->setText(QStringLiteral("Cannot determine partition number."));
            return;
        }
        QProcess::execute(QStringLiteral("swapoff"), {QString::fromStdString(part.path)});
        QProcess::execute(QStringLiteral("umount"), {QStringLiteral("-f"), QString::fromStdString(part.path)});
        runDiskCommand(QStringLiteral("sgdisk"),
                       {QStringLiteral("-d"), QString::number(part.part_number),
                        QString::fromStdString(part.parent_disk)},
                       QStringLiteral("Deleted %1.").arg(QString::fromStdString(part.path)));
    }

    void wipeDiskGpt() {
        if (disks_.empty()) return;
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        if (d.is_live_medium) {
            du_status_->setText(QStringLiteral("Cannot wipe the active Live USB drive."));
            return;
        }
        unmountDiskPartitions(d);
        const QString disk_path = QString::fromStdString(d.path);
        const QString script = QStringLiteral(
            "set -e; DISK=\"$1\"; "
            "wipefs -a \"$DISK\" 2>/dev/null || true; "
            "sgdisk --zap-all \"$DISK\"; "
            "sgdisk -o \"$DISK\"; "
            "partprobe \"$DISK\" 2>/dev/null || true");
        runDiskCommand(QStringLiteral("bash"),
                       {QStringLiteral("-c"), script, QStringLiteral("_"), disk_path},
                       QStringLiteral("Created clean GPT table on %1.").arg(disk_path));
    }

    void runDiskAction(DiskOpKind kind) {
        if (du_parts_.empty() || du_selected_part_ >= static_cast<int>(du_parts_.size())) {
            du_status_->setText(QStringLiteral("Select a partition first."));
            return;
        }
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        if (d.is_live_medium) {
            du_status_->setText(QStringLiteral("Cannot modify the active Live USB drive."));
            return;
        }
        const auto& part = du_parts_[static_cast<std::size_t>(du_selected_part_)];
        QProcess::execute(QStringLiteral("swapoff"), {QString::fromStdString(part.path)});
        QProcess::execute(QStringLiteral("umount"), {QStringLiteral("-f"), QString::fromStdString(part.path)});
        PartitionInfo clean_part = part;
        clean_part.mountpoint.clear();
        const auto plan = plan_disk_op(kind, clean_part);
        if (!plan.valid) {
            du_status_->setText(QString::fromStdString(plan.error));
            return;
        }
        QStringList args;
        for (const auto& a : plan.args) args << QString::fromStdString(a);
        runDiskCommand(QString::fromStdString(plan.program), args, QString::fromStdString(plan.summary));
    }

    // ── Page 6: Bootloader Repair ────────────────────────────────────────────
    QWidget* buildBootRepairPage() {
        auto* host = new QWidget();
        host->setFixedWidth(kOobeContentWidth + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(8);

        const QStringList opts = {
            QStringLiteral("Reinstall UEFI bootloader"),
            QStringLiteral("Rescan Windows, macOS & Linux"),
            QStringLiteral("Verify /loader/entries/*.conf"),
        };
        for (int i = 0; i < opts.size(); ++i) {
            auto* row = new OobeOptionRow(opts[i], {}, i == 0, host);
            connect(row, &OobeOptionRow::clicked, this, [this, i]() {
                boot_choice_ = i;
                for (int j = 0; j < static_cast<int>(boot_rows_.size()); ++j) {
                    boot_rows_[static_cast<std::size_t>(j)]->setSelected(j == i);
                }
            });
            boot_rows_.push_back(row);
            col->addWidget(row);
        }

        boot_status_ = new QLabel(host);
        boot_status_->setFont(oobe_font(12, QFont::Normal));
        boot_status_->setWordWrap(true);
        col->addSpacing(8);
        col->addWidget(boot_status_);
        col->addStretch(1);

        return create_oobe_split_page(QStringLiteral("shield"),
                                      QStringLiteral("Repair startup"),
                                      QStringLiteral("Press > to run."),
                                      host);
    }

    void runBootRepair() {
        const QString bootctl = QStandardPaths::findExecutable(QStringLiteral("bootctl"));
        if (bootctl.isEmpty()) {
            boot_status_->setText(QStringLiteral("bootctl is not available on this host."));
            return;
        }
        QStringList args = {QStringLiteral("status")};
        if (boot_choice_ == 0) args = {QStringLiteral("install")};
        else if (boot_choice_ == 1) args = {QStringLiteral("update")};
        boot_status_->setText(QStringLiteral("Running bootctl..."));
        auto* proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc](int code, QProcess::ExitStatus st) {
                    const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        boot_status_->setText(out.isEmpty() ? QStringLiteral("Startup repair complete.")
                                                            : out.section(QLatin1Char('\n'), 0, 0));
                    } else {
                        boot_status_->setText(out.isEmpty() ? QStringLiteral("Repair failed (exit %1).").arg(code)
                                                            : out.section(QLatin1Char('\n'), -1));
                    }
                });
        proc->start(bootctl, args);
    }

    // ── Page 7: System Logs ──────────────────────────────────────────────────
    QWidget* buildLogsPage() {
        auto* host = new QWidget();
        host->setFixedWidth(kOobeContentWidth + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(8);

        logs_edit_ = new QPlainTextEdit(host);
        logs_edit_->setReadOnly(true);
        logs_edit_->setFrameShape(QFrame::NoFrame);
        logs_edit_->setFixedHeight(320);
        logs_edit_->setFont(finn_mono_font(11.0, QFont::Normal));
        col->addWidget(logs_edit_);
        col->addStretch(1);

        refreshLogs();
        return create_oobe_split_page(QStringLiteral("terminal-square"),
                                      QStringLiteral("System logs"),
                                      {}, host);
    }

    void refreshLogs() {
        if (!logs_edit_) return;
        QProcess p;
        p.start(QStringLiteral("uname"), {QStringLiteral("-a")});
        p.waitForFinished(1000);
        QString txt = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        txt += QStringLiteral("\nDetected disks: %1\nDetected OSes: %2\n")
                   .arg(disks_.size()).arg(detected_oses_.size());
        for (const auto& d : disks_) {
            txt += QStringLiteral("  %1 (%2)%3\n")
                       .arg(QString::fromStdString(d.path),
                            QString::fromStdString(format_bytes(d.size_bytes)),
                            d.is_live_medium ? QStringLiteral(" [LIVE USB]") : QString());
        }
        logs_edit_->setPlainText(txt);
    }

    std::vector<DiskInfo> disks_;
    std::vector<DetectedOs> detected_oses_;

    FinnPageStack* stack_ = nullptr;
    QPointer<OobeSlideTransitionOverlay> slide_overlay_;
    OobeBrandingHeader* branding_ = nullptr;
    QWidget* shared_nav_box_ = nullptr;
    OobeCircleNavButton* nav_back_ = nullptr;
    OobeCircleNavButton* nav_next_ = nullptr;
    QWidget* footer_links_ = nullptr;
    QuietLink* kb_link_ = nullptr;
    int kb_idx_ = 0;
    RecoveryTerminalWindow* terminal_win_ = nullptr;

    int hub_choice_ = 0;
    std::vector<OobeOptionRow*> hub_rows_;

    InstallMode selected_mode_ = InstallMode::CleanErase;
    std::vector<OobeOptionRow*> mode_rows_;

    int selected_disk_idx_ = 0;
    std::string selected_partition_path_;
    int resize_gib_ = 40;
    QWidget* target_list_host_ = nullptr;
    QVBoxLayout* target_list_lay_ = nullptr;
    std::vector<OobeOptionRow*> target_rows_;
    std::vector<std::string> target_part_paths_;
    SlimPartitionBar* target_bar_ = nullptr;
    QWidget* resize_host_ = nullptr;
    QLabel* resize_label_ = nullptr;
    QSlider* resize_slider_ = nullptr;
    QLabel* target_sub_lbl_ = nullptr;

    InstallPlan current_plan_;
    QLabel* review_label_ = nullptr;
    QPlainTextEdit* install_log_ = nullptr;

    bool du_return_to_target_ = false;
    int du_selected_disk_ = 0;
    int du_selected_part_ = 0;
    int du_new_size_gib_ = 32;
    QString du_new_fs_ = QStringLiteral("ext4");
    QWidget* du_disk_host_ = nullptr;
    QVBoxLayout* du_disk_lay_ = nullptr;
    std::vector<OobeOptionRow*> du_disk_rows_;
    QWidget* du_list_host_ = nullptr;
    QVBoxLayout* du_list_lay_ = nullptr;
    std::vector<OobeOptionRow*> du_part_rows_;
    std::vector<PartitionInfo> du_parts_;
    SlimPartitionBar* du_bar_ = nullptr;
    QWidget* du_create_panel_ = nullptr;
    QLabel* du_new_size_lbl_ = nullptr;
    QSlider* du_new_size_slider_ = nullptr;
    QLabel* du_status_ = nullptr;

    int boot_choice_ = 0;
    std::vector<OobeOptionRow*> boot_rows_;
    QLabel* boot_status_ = nullptr;

    QPlainTextEdit* logs_edit_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    QString initial_page;
    QString screenshot_path;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QLatin1String("--version")) {
            std::puts("finn-recovery 1.0.0");
            return 0;
        }
        if (arg == QLatin1String("--page") && i + 1 < argc) {
            initial_page = QString::fromUtf8(argv[++i]);
        } else if (arg == QLatin1String("--screenshot") && i + 1 < argc) {
            screenshot_path = QString::fromUtf8(argv[++i]);
        }
    }

    FinnApplication app(argc, argv, QStringLiteral("installer"));
    RecoveryWindow w;
    if (!initial_page.isEmpty()) w.openPage(initial_page);
    const QString plat = QGuiApplication::platformName();
    if (screenshot_path.isEmpty() &&
        (plat == QLatin1String("eglfs") || plat == QLatin1String("linuxfb"))) {
        w.showFullScreen();
    } else {
        w.resize(1280, 832);
        w.show();
    }

    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(250, &w, [&w, screenshot_path]() {
            w.grab().save(screenshot_path);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}

#include "main.moc"
