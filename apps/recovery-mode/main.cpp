// FinnOS Recovery Mode (recovery-mode).
// Built pixel-for-pixel on the FinnOS OOBE design language (apps/oobe/main.cpp):
//   - Responsive OOBE form factors (Phone / Tablet / Desktop)
//   - Full-bleed bone canvas with NO top-left branding header on split pages
//   - Exact OOBE split two-column layout (46x46 icon + 40px gap + 24px prompt,
//     right 350px column at y=195, stationary circular Back/Next chevrons)
//   - Exact OobeOptionRow, OobeToggleRow, OobeInputField, OobeCircleNavButton,
//     OobeGetStartedPill, OobeBrandingHeader, OobeProgressiveList +
//     ProgressiveBlurOverlay (60.7%), and OobeSlideTransitionOverlay (0.52s)
//   - Exact OOBE plain, calm Completion Screen matching OOBE's visual system
//   - 100% Offline OS installation from local live/recovery media with SHA-512
//     integrity verification, auto-generated EFI/BIOS-Boot partitions, GPT & MBR
//     support, dual-boot alongside Windows / macOS / Linux, Disk Utility,
//     Bootloader Repair, System Logs, World Keyboards, Wi-Fi, and PTY Terminal.

#include <FinnSDK/FinnSDK.hpp>

#include "common/paths.hpp"
#include "finn_terminal_widget.hpp"
#include "recovery/recovery_engine.hpp"
#include "system/sys_files.hpp"
#include "world_keyboards.hpp"

#include <QAbstractButton>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
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
#include <QRegularExpression>
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

enum class OobeForm { Phone, Tablet, Desktop };

int oobe_screen_width() {
    bool ok = false;
    const int simulated = qgetenv("FINN_SIM_WIDTH").toInt(&ok);
    if (ok && simulated > 0) return simulated;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const int w = screen->availableGeometry().width();
        if (w > 0) return w;
    }
    return 1280;
}

OobeForm oobe_form() {
    const QByteArray ov = qgetenv("FINN_FORM_FACTOR").trimmed().toLower();
    if (ov == "phone") return OobeForm::Phone;
    if (ov == "tablet") return OobeForm::Tablet;
    if (ov == "desktop") return OobeForm::Desktop;
    const int w = oobe_screen_width();
    if (w <= finn::ui::breakpoints::bp_phone_max) return OobeForm::Phone;
    if (w <= finn::ui::breakpoints::bp_tablet_max) return OobeForm::Tablet;
    return OobeForm::Desktop;
}

int oobe_gutter() {
    switch (oobe_form()) {
        case OobeForm::Phone:   return 16;
        case OobeForm::Tablet:  return 24;
        case OobeForm::Desktop: return 80;
    }
    return 80;
}

int oobe_content_width() {
    return qMax(240, qMin(350, oobe_screen_width() - 2 * oobe_gutter()));
}

void set_wrapped_label_text(QLabel* lbl, const QString& text) {
    if (!lbl) return;
    lbl->setText(text);
    const int w = lbl->width() > 0 ? lbl->width() : lbl->maximumWidth();
    if (w > 0 && w < QWIDGETSIZE_MAX) {
        const QFontMetrics fm(lbl->font());
        const QRect br = fm.boundingRect(
            0, 0, w, 1000,
            static_cast<int>(lbl->alignment()) | Qt::TextWordWrap, text);
        lbl->setMinimumHeight(std::max(fm.lineSpacing() + 6, br.height() + 10));
    }
    lbl->updateGeometry();
}

// Exact OOBE box blur helper.
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
                sr += qRed(px); sg += qGreen(px); sb += qBlue(px); sa += qAlpha(px);
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
                sr += qRed(px); sg += qGreen(px); sb += qBlue(px); sa += qAlpha(px);
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

// Exact OOBE Branding Header (Figma 36:45, used on Completion screen).
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

// Exact OOBE circular 56x56 chevron navigation button.
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

    void enterEvent(QEnterEvent*) override { hovered_ = true; if (!anim_timer_.isActive()) anim_timer_.start(); }
    void leaveEvent(QEvent*) override { hovered_ = false; if (!anim_timer_.isActive()) anim_timer_.start(); }
    void mousePressEvent(QMouseEvent* e) override { QAbstractButton::mousePressEvent(e); if (!anim_timer_.isActive()) anim_timer_.start(); }
    void mouseReleaseEvent(QMouseEvent* e) override { QAbstractButton::mouseReleaseEvent(e); if (!anim_timer_.isActive()) anim_timer_.start(); }

private:
    bool is_back_ = false;
    bool filled_default_ = false;
    bool hovered_ = false;
    qreal hover_t_ = 0.0;
    qreal press_t_ = 0.0;
    QTimer anim_timer_;
};

// Exact OOBE "Get started" / "Restart now" action pill.
class OobeGetStartedPill final : public QAbstractButton {
    Q_OBJECT
public:
    explicit OobeGetStartedPill(const QString& label, QWidget* parent = nullptr)
        : QAbstractButton(parent) {
        setText(label);
        setAccessibleName(label);
        setFocusPolicy(Qt::TabFocus);
        setFixedSize(164, 48);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = t.tile;
        if (isDown()) bg = t.surface_pressed;
        else if (hovered_) bg = t.surface_hover;
        p.setBrush(bg);
        p.setPen(QPen(hasFocus() ? t.accent : t.hair, hasFocus() ? 2.0 : 1.0));
        p.drawRoundedRect(r, 24.0, 24.0);
        p.setFont(oobe_font(15, QFont::Medium));
        p.setPen(t.ink);
        p.drawText(rect(), Qt::AlignCenter, text());
    }
    void enterEvent(QEnterEvent*) override { hovered_ = true; update(); }
    void leaveEvent(QEvent*) override { hovered_ = false; update(); }

private:
    bool hovered_ = false;
};

// Exact OOBE selectable 350x37 pill row.
class OobeOptionRow final : public QWidget {
    Q_OBJECT
public:
    OobeOptionRow(const QString& label, const QString& detail = {},
                  bool selected = false, QWidget* parent = nullptr)
        : QWidget(parent),
          label_(label),
          detail_(detail),
          selected_(selected) {
        setFixedSize(oobe_content_width(), 37);
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

    void enterEvent(QEnterEvent*) override { if (!isEnabled()) return; hovered_ = true; if (!anim_timer_.isActive()) anim_timer_.start(); }
    void leaveEvent(QEvent*) override { hovered_ = false; pressed_ = false; if (!anim_timer_.isActive()) anim_timer_.start(); }
    void mousePressEvent(QMouseEvent* e) override {
        if (isEnabled() && e->button() == Qt::LeftButton) { pressed_ = true; update(); }
        else QWidget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (isEnabled() && e->button() == Qt::LeftButton && pressed_) {
            pressed_ = false; update();
            if (rect().contains(e->pos())) emit clicked();
        } else QWidget::mouseReleaseEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (isEnabled() && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space)) {
            emit clicked();
            return;
        }
        QWidget::keyPressEvent(e);
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

// Exact OOBE 350x42 rounded input field.
class OobeInputField final : public QWidget {
    Q_OBJECT
public:
    OobeInputField(const QString& placeholder, bool password = false, QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(oobe_content_width(), 42);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 4, 12, 4);
        lay->setSpacing(8);

        edit_ = new QLineEdit(this);
        edit_->setFrame(false);
        edit_->setPlaceholderText(placeholder);
        edit_->setAccessibleName(placeholder);
        edit_->setFont(oobe_font(16, QFont::Normal));
        if (password) edit_->setEchoMode(QLineEdit::Password);
        edit_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        lay->addWidget(edit_, 1);

        connect(edit_, &QLineEdit::textChanged, this, [this]() { update(); });
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &OobeInputField::syncColors);
        syncColors();
    }

    QLineEdit* lineEdit() const { return edit_; }
    QString text() const { return edit_->text(); }
    void setText(const QString& t) { edit_->setText(t); }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(edit_->hasFocus()
                     ? QPen(t.accent, t.high_contrast ? 2.5 : 1.5)
                     : QPen(t.hair, t.high_contrast ? 2.0 : 1.0));
        p.setBrush(t.surface_input);
        p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 10.0, 10.0);
    }

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = edit_->palette();
        pal.setColor(QPalette::Text, t.ink);
        pal.setColor(QPalette::PlaceholderText, t.mute);
        edit_->setPalette(pal);
        update();
    }
    QLineEdit* edit_ = nullptr;
};

// Exact OOBE 350x39 toggle row (Figma 37:98).
class OobeToggleRow final : public QWidget {
    Q_OBJECT
public:
    OobeToggleRow(const QString& label, bool initial_checked, QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(oobe_content_width(), 39);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->setSpacing(10);

        label_ = new QLabel(label, this);
        label_->setFont(oobe_font(16, QFont::Normal));
        lay->addWidget(label_, 1);

        toggle_ = new FinnToggle(initial_checked, this);
        toggle_->setAccessibleName(label);
        toggle_->setFocusPolicy(Qt::TabFocus);
        lay->addWidget(toggle_);

        connect(toggle_, &QAbstractButton::toggled, this, &OobeToggleRow::toggled);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &OobeToggleRow::syncColors);
        syncColors();
    }

    FinnToggle* toggle() const { return toggle_; }
    void setLabel(const QString& text) {
        if (label_) label_->setText(text);
        if (toggle_) toggle_->setAccessibleName(text);
    }

signals:
    void toggled(bool checked);

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = label_->palette();
        pal.setColor(QPalette::WindowText, t.ink);
        label_->setPalette(pal);
        update();
    }
    QLabel* label_ = nullptr;
    FinnToggle* toggle_ = nullptr;
};

// Exact OOBE scrollable list + progressive blur overlay (startOffset 60.7%).
class OobeProgressiveList final : public QWidget {
    Q_OBJECT
public:
    explicit OobeProgressiveList(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedWidth(oobe_content_width() + 6);
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
        list_lay_->setSpacing(10);
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

    void invalidateCache() { cache_dirty_ = true; update(); }

protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); cache_dirty_ = true; }

    void paintEvent(QPaintEvent*) override {
        if (!target_ || height() < 40 || in_capture_) return;
        const auto& t = FinnThemeManager::instance().tokens();
        const QColor bg = t.bone;

        const int blur_top = qRound(height() * 0.607);
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

// Exact OOBE sequential fade + whisper optical blur transition (0.52s).
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
        if (!cancelled_ && stack_widget_) stack_widget_->setVisible(true);
    }

    QPixmap grabCurrentFrame() {
        const qreal dpr = devicePixelRatioF();
        QPixmap pix(qRound(width() * dpr), qRound(height() * dpr));
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        render(&pix);
        return pix;
    }

    void cancel() { cancelled_ = true; if (timer_) timer_->stop(); hide(); deleteLater(); }
    void finish() {
        if (timer_) timer_->stop();
        if (stack_widget_) { stack_widget_->setVisible(true); stack_widget_->update(); }
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
            if (sharp_a > 0.002) { p.setOpacity(sharp_a); p.drawPixmap(off, old_snap_); }
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
            if (sharp_a > 0.002) { p.setOpacity(sharp_a); p.drawPixmap(off, new_snap_); }
        }
        p.setOpacity(1.0);
    }

private:
    static QPixmap makeWhisperBlur(const QPixmap& src) {
        if (src.isNull()) return {};
        QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QSize half(std::max(1, img.width() / 2), std::max(1, img.height() / 2));
        QImage small = img.scaled(half, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return QPixmap::fromImage(box_blur_image(small, 2));
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

// Quiet inline utility button.
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
    void setLabelText(const QString& t) { text_ = t; updateGeometry(); update(); }
    QSize sizeHint() const override { return QSize(fontMetrics().horizontalAdvance(text_) + 14, 28); }

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

// Rounded pill progress bar matching the FinnOS boot splash progress bar.
// Advances ONLY on real reported steps (never invents fake percentages).
class OobeProgressBar final : public QWidget {
    Q_OBJECT
public:
    explicit OobeProgressBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(oobe_content_width(), 8);
        sweep_timer_.setInterval(16);
        connect(&sweep_timer_, &QTimer::timeout, this, [this]() {
            sweep_phase_ = std::fmod(sweep_phase_ + 0.014, 1.0);
            update();
        });
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setIndeterminate() {
        has_progress_ = false;
        progress_ = 0.0;
        if (!FinnThemeManager::instance().reducedMotion()) sweep_timer_.start();
        update();
    }

    void setProgress(double fraction) {
        has_progress_ = true;
        progress_ = std::clamp(fraction, 0.0, 1.0);
        sweep_timer_.stop();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF track(0.0, 0.0, width(), height());
        p.setPen(Qt::NoPen);
        p.setBrush(t.track);
        p.drawRoundedRect(track, height() * 0.5, height() * 0.5);

        p.setBrush(t.ink);
        if (has_progress_) {
            const qreal w = std::max<qreal>(height(), track.width() * progress_);
            if (progress_ > 0.0) {
                p.drawRoundedRect(QRectF(0.0, 0.0, w, height()), height() * 0.5, height() * 0.5);
            }
        } else {
            const qreal span = track.width() * 0.22;
            const qreal eased = 0.5 - 0.5 * std::cos(sweep_phase_ * 2.0 * 3.14159265358979323846);
            const qreal x = (track.width() - span) * eased;
            p.drawRoundedRect(QRectF(x, 0.0, span, height()), height() * 0.5, height() * 0.5);
        }
    }

private:
    bool has_progress_ = false;
    double progress_ = 0.0;
    double sweep_phase_ = 0.0;
    QTimer sweep_timer_;
};

// Minimalist proportional disk partition bar.
class SlimPartitionBar final : public QWidget {
    Q_OBJECT
public:
    explicit SlimPartitionBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(oobe_content_width(), 8);
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
            if (part.is_extended) continue;
            const qreal seg_w = std::max<qreal>(6.0, full.width() *
                (static_cast<qreal>(part.size_bytes) / static_cast<qreal>(total)));
            QColor c = t.dim;
            if (part.is_esp) c = QColor(0xD9, 0x77, 0x06);
            else if (part.is_recovery_part) c = QColor(0x8B, 0x5C, 0xF6);
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

// Embedded interactive draggable & resizable Finn Terminal floating window
// backed by a real POSIX PTY + VT100/ANSI emulator (FinnTerminalWidget).
class RecoveryTerminalWindow final : public QWidget {
    Q_OBJECT
public:
    explicit RecoveryTerminalWindow(QWidget* parent = nullptr) : QWidget(parent) {
        resize(780, 490);
        setMinimumSize(460, 280);
        setMouseTracking(true);
        setAttribute(Qt::WA_StyledBackground, false);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(1, 1, 1, 1);
        root->setSpacing(0);

        titlebar_ = new QWidget(this);
        titlebar_->setFixedHeight(38);
        auto* tb = new QHBoxLayout(titlebar_);
        tb->setContentsMargins(14, 0, 10, 0);
        tb->setSpacing(8);

        title_lbl_ = new QLabel(QStringLiteral("Finn Terminal"), titlebar_);
        title_lbl_->setFont(oobe_font(13, QFont::Medium));
        title_lbl_->setStyleSheet(QStringLiteral("color: #EAEAEA; background: transparent;"));
        tb->addWidget(title_lbl_);
        tb->addStretch(1);

        class MiniTermBtn final : public QAbstractButton {
        public:
            MiniTermBtn(QString label, QColor hover_bg, QWidget* p)
                : QAbstractButton(p), label_(std::move(label)), hover_bg_(hover_bg) {
                setCursor(Qt::PointingHandCursor);
                setFixedSize(label_.size() <= 1 ? 26 : (static_cast<int>(label_.size()) * 8 + 16), 24);
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
        auto* btn_cls = new MiniTermBtn(QStringLiteral("\u00D7"), QColor(220, 38, 38, 210), titlebar_);
        tb->addWidget(btn_int);
        tb->addWidget(btn_clr);
        tb->addWidget(btn_cls);
        root->addWidget(titlebar_);

        term_ = new FinnTerminalWidget(this);
        root->addWidget(term_, 1);

        auto* foot = new QWidget(this);
        foot->setFixedHeight(16);
        foot->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        root->addWidget(foot);

        connect(term_, &FinnTerminalWidget::titleChanged, this, [this](const QString& t) {
            if (title_lbl_ && !t.isEmpty()) title_lbl_->setText(t);
        });
        connect(term_, &FinnTerminalWidget::shellExited, this, [this](int) {
            QTimer::singleShot(150, this, [this]() {
                if (term_ && !term_->running()) term_->startShell();
            });
        });
        connect(btn_int, &QAbstractButton::clicked, this, [this]() {
            if (term_) { term_->sendControl(QStringLiteral("ctrl_c")); term_->setFocus(); }
        });
        connect(btn_clr, &QAbstractButton::clicked, this, [this]() {
            if (term_) { term_->clearScreen(); term_->setFocus(); }
        });
        connect(btn_cls, &QAbstractButton::clicked, this, &QWidget::hide);

        term_->startShell();
    }

    void focusTerminal() {
        if (term_) {
            if (!term_->running()) term_->startShell();
            term_->setFocus(Qt::OtherFocusReason);
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(QColor(0x16, 0x18, 0x1F, 252));
        p.setPen(QPen(QColor(255, 255, 255, 42), 1.0));
        p.drawRoundedRect(r, 14.0, 14.0);

        p.setPen(QPen(QColor(255, 255, 255, 85), 1.2, Qt::SolidLine, Qt::RoundCap));
        const int w = width(), h = height();
        p.drawLine(w - 13, h - 5, w - 5, h - 13);
        p.drawLine(w - 9, h - 5, w - 5, h - 9);
    }

    bool inResizeZone(const QPointF& pos) const {
        return pos.x() >= width() - 20 && pos.y() >= height() - 20;
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            raise();
            if (inResizeZone(e->position())) {
                resizing_ = true;
                resize_start_global_ = e->globalPosition().toPoint();
                resize_start_size_ = size();
                e->accept();
                return;
            }
            if (e->position().y() <= 38.0) {
                dragging_ = true;
                drag_offset_ = e->globalPosition().toPoint() - pos();
                e->accept();
                return;
            }
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (resizing_ && parentWidget()) {
            const QPoint delta = e->globalPosition().toPoint() - resize_start_global_;
            const int max_w = std::max(minimumWidth(), parentWidget()->width() - x() - 12);
            const int max_h = std::max(minimumHeight(), parentWidget()->height() - y() - 12);
            resize(std::clamp(resize_start_size_.width() + delta.x(), minimumWidth(), max_w),
                   std::clamp(resize_start_size_.height() + delta.y(), minimumHeight(), max_h));
            e->accept();
            return;
        }
        if (dragging_ && parentWidget()) {
            QPoint next = e->globalPosition().toPoint() - drag_offset_;
            next.setX(std::clamp(next.x(), 0, std::max(0, parentWidget()->width() - width())));
            next.setY(std::clamp(next.y(), 0, std::max(0, parentWidget()->height() - height())));
            move(next);
            e->accept();
            return;
        }
        setCursor(inResizeZone(e->position()) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        dragging_ = false;
        resizing_ = false;
        QWidget::mouseReleaseEvent(e);
    }

private:
    QWidget* titlebar_ = nullptr;
    QLabel* title_lbl_ = nullptr;
    FinnTerminalWidget* term_ = nullptr;
    bool dragging_ = false;
    bool resizing_ = false;
    QPoint drag_offset_;
    QPoint resize_start_global_;
    QSize resize_start_size_;
};

// Detect the live USB / ISO / Recovery partition backing disk so we NEVER wipe it.
std::string detect_live_backing_disk() {
    for (const char* mnt : {"/run/archiso/bootmnt", "/run/archiso/airootfs", "/run/finn/recovery", "/"}) {
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
             QStringLiteral("NAME,PATH,PKNAME,SIZE,TYPE,FSTYPE,LABEL,PARTLABEL,UUID,PARTUUID,PARTTYPE,PTTYPE,MOUNTPOINT,MODEL,RO,RM,TRAN,FSAVAIL,START")});
    if (!p.waitForFinished(2500)) return {};
    const QByteArray raw = p.readAllStandardOutput();
    const std::string live = detect_live_backing_disk();
    auto disks = parse_lsblk_json(std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())), live);

    // Enrich non-live disks with exact unallocated free regions via parted (for Free Space dual-boot).
    if (!QStandardPaths::findExecutable(QStringLiteral("parted")).isEmpty()) {
        for (auto& d : disks) {
            if (d.is_live_medium || d.read_only) continue;
            QProcess pp;
            pp.start(QStringLiteral("parted"),
                     {QStringLiteral("-ms"), QString::fromStdString(d.path),
                      QStringLiteral("unit"), QStringLiteral("B"),
                      QStringLiteral("print"), QStringLiteral("free")});
            if (pp.waitForFinished(1500) && pp.exitCode() == 0) {
                const QByteArray out = pp.readAllStandardOutput();
                auto regs = parse_parted_free_regions(
                    std::string_view(out.constData(), static_cast<std::size_t>(out.size())));
                if (!regs.empty()) d.free_regions = std::move(regs);
            }
        }
    }
    return disks;
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

// Exact responsive OOBE split page (1:1 match with createSplitStepPage in apps/oobe/main.cpp).
QWidget* create_oobe_split_page(const QString& icon_name,
                                const QString& prompt_text,
                                const QString& sub_caption,
                                QWidget* right_column,
                                QLabel** out_sub = nullptr,
                                QLabel** out_prompt = nullptr) {
    auto* page = new QWidget();
    page->setAutoFillBackground(false);

    const OobeForm form = oobe_form();
    const int gutter = oobe_gutter();

    auto* main_col = new QVBoxLayout(page);
    main_col->setContentsMargins(gutter, form == OobeForm::Phone ? gutter : 0,
                                 gutter, form == OobeForm::Phone ? 132 : 0);
    main_col->setSpacing(0);

    QBoxLayout* row = (form == OobeForm::Phone)
        ? static_cast<QBoxLayout*>(new QVBoxLayout())
        : static_cast<QBoxLayout*>(new QHBoxLayout());
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(form == OobeForm::Phone ? 12 : (form == OobeForm::Tablet ? 16 : 32));

    auto* left_host = new QWidget(page);
    auto* left = new QVBoxLayout(left_host);
    left->setContentsMargins(form == OobeForm::Desktop ? 80 : 0, 0,
                             form == OobeForm::Desktop ? 20 : 0, 0);
    left->setSpacing(0);
    if (form != OobeForm::Phone) left->addStretch(49);

    auto* icon_lbl = new QLabel(left_host);
    icon_lbl->setFixedSize(46, 46);
    icon_lbl->setAlignment(Qt::AlignCenter);
    left->addWidget(icon_lbl, 0, Qt::AlignHCenter);
    left->addSpacing(form == OobeForm::Phone ? 12 : (form == OobeForm::Tablet ? 24 : 40));

    auto* title_lbl = new QLabel(left_host);
    title_lbl->setFont(oobe_font(form == OobeForm::Phone ? 20 : 24, QFont::Normal));
    title_lbl->setAlignment(Qt::AlignCenter);
    title_lbl->setWordWrap(true);
    title_lbl->setFixedWidth(form == OobeForm::Desktop ? 380 : qMin(300, oobe_content_width()));
    title_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    set_wrapped_label_text(title_lbl, prompt_text);
    if (out_prompt) *out_prompt = title_lbl;
    left->addWidget(title_lbl, 0, Qt::AlignHCenter);

    QLabel* sub_lbl = nullptr;
    if (!sub_caption.isEmpty() || out_sub != nullptr) {
        left->addSpacing(10);
        sub_lbl = new QLabel(sub_caption, left_host);
        sub_lbl->setFont(oobe_font(14, QFont::Normal));
        sub_lbl->setAlignment(Qt::AlignCenter);
        sub_lbl->setWordWrap(true);
        sub_lbl->setFixedWidth(qMin(300, oobe_content_width()));
        sub_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        sub_lbl->setVisible(!sub_caption.isEmpty());
        left->addWidget(sub_lbl, 0, Qt::AlignHCenter);
        if (out_sub) *out_sub = sub_lbl;
    }
    if (form != OobeForm::Phone) left->addStretch(51);

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
    const int right_top = (form == OobeForm::Desktop) ? 195
                        : (form == OobeForm::Tablet) ? 96 : 0;
    const int right_bot = (form == OobeForm::Desktop) ? 160
                        : (form == OobeForm::Tablet) ? 110 : 12;
    right_col->setContentsMargins(0, right_top, form == OobeForm::Desktop ? 45 : 0, right_bot);
    right_col->setSpacing(0);
    right_column->setParent(right_host);
    right_col->addWidget(right_column, 1,
                         form == OobeForm::Desktop ? Qt::AlignRight : Qt::AlignHCenter);

    if (form == OobeForm::Phone) {
        row->addWidget(left_host, 0, Qt::AlignHCenter);
        row->addWidget(right_host, 1);
    } else {
        row->addWidget(left_host, 1);
        row->addSpacing(form == OobeForm::Tablet ? 16 : 132);
        row->addWidget(right_host, 1, Qt::AlignRight);
    }
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
        PageComplete = 5,
        PageDiskUtility = 6,
        PageBootRepair = 7,
        PageLogs = 8,
        PageKeyboard = 9,
        PageNetwork = 10,
    };

    RecoveryWindow()
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("recovery-mode")) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::Exclusive);
        setAccessibleName(QStringLiteral("FinnOS Recovery Mode"));

        host_env_.firmware = detect_firmware_mode();
        host_env_.image = discover_offline_image();
        disks_ = probe_system_disks();
        detected_oses_ = detect_oses(disks_);
        selected_disk_idx_ = first_installable_disk_index(disks_);
        du_selected_disk_ = selected_disk_idx_;
        world_kb_layouts_ = load_world_keyboard_layouts();
        xkb_filter_ = new LiveXkbKeymapFilter(this);
        if (auto* core = QCoreApplication::instance()) {
            core->installEventFilter(xkb_filter_);
        }

        // Default to Dual-Boot (Free Space if available, else Resize) whenever an
        // existing OS is detected so nobody accidentally erases Windows/macOS/Linux.
        if (!detected_oses_.empty()) {
            selected_mode_ = chooseDefaultDualBootMode();
        }

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        stack_ = new FinnPageStack(this);
        stack_->setAnimatedTransitions(false);
        stack_->addWidget(buildHubPage());              // 0
        stack_->addWidget(buildInstallModePage());      // 1
        stack_->addWidget(buildInstallTargetPage());    // 2
        stack_->addWidget(buildInstallReviewPage());    // 3
        stack_->addWidget(buildInstallProgressPage());  // 4
        stack_->addWidget(buildCompletePage());         // 5
        stack_->addWidget(buildDiskUtilityPage());      // 6
        stack_->addWidget(buildBootRepairPage());       // 7
        stack_->addWidget(buildLogsPage());             // 8
        stack_->addWidget(buildKeyboardPage());         // 9
        stack_->addWidget(buildNetworkPage());          // 10
        root->addWidget(stack_, 1);

        // Exact OOBE stationary Back/Next circular navigation bar.
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

        // Unobtrusive bottom-left utility bar (Terminal, Network, Keyboard, Restart, Shut Down).
        footer_links_ = new QWidget(this);
        auto* fl = new QHBoxLayout(footer_links_);
        fl->setContentsMargins(0, 0, 0, 0);
        fl->setSpacing(10);
        auto* l_term = new QuietLink(QStringLiteral("Terminal"), false, footer_links_);
        net_link_ = new QuietLink(QStringLiteral("Network"), false, footer_links_);
        kb_link_ = new QuietLink(QStringLiteral("Keyboard: US"), false, footer_links_);
        auto* l_reb = new QuietLink(QStringLiteral("Restart"), false, footer_links_);
        auto* l_off = new QuietLink(QStringLiteral("Shut Down"), false, footer_links_);

        connect(l_term, &QAbstractButton::clicked, this, &RecoveryWindow::toggleTerminalWindow);
        connect(net_link_, &QAbstractButton::clicked, this, [this]() {
            if (stack_->currentIndex() == PageNetwork) { goBack(); return; }
            if (stack_->currentIndex() != PageKeyboard) prev_utility_page_ = static_cast<Page>(stack_->currentIndex());
            refreshNetwork();
            goTo(PageNetwork);
        });
        connect(kb_link_, &QAbstractButton::clicked, this, [this]() {
            if (stack_->currentIndex() == PageKeyboard) { goBack(); return; }
            if (stack_->currentIndex() != PageNetwork) prev_utility_page_ = static_cast<Page>(stack_->currentIndex());
            goTo(PageKeyboard);
        });
        connect(l_reb, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
        });
        connect(l_off, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
        });
        fl->addWidget(l_term);
        fl->addWidget(net_link_);
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
        } else if (p == QLatin1String("target") || p == QLatin1String("drive")) {
            refreshTargets();
            goTo(PageInstallTarget, false);
        } else if (p == QLatin1String("review")) {
            refreshReview();
            goTo(PageInstallReview, false);
        } else if (p == QLatin1String("complete")) {
            goTo(PageComplete, false);
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
        } else if (p == QLatin1String("keyboard") || p == QLatin1String("kb")) {
            goTo(PageKeyboard, false);
        } else if (p == QLatin1String("network") || p == QLatin1String("wifi")) {
            refreshNetwork();
            goTo(PageNetwork, false);
        } else {
            goTo(PageHub, false);
        }
    }

    void openTerminalOverlay() {
        if (!terminal_win_ || !terminal_win_->isVisible()) toggleTerminalWindow();
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
            if (terminal_win_ && terminal_win_->isVisible()) { terminal_win_->hide(); return; }
            if (stack_->currentIndex() != PageHub && stack_->currentIndex() != PageInstallProgress) goBack();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (terminal_win_ && terminal_win_->isVisible()) {
                FinnLayerWindow::keyPressEvent(e);
                return;
            }
            if (stack_->currentIndex() == PageInstallReview) return;
            if (nav_next_->isVisible() && nav_next_->isEnabled()) goNext();
            return;
        }
        FinnLayerWindow::keyPressEvent(e);
    }

private:
    InstallMode chooseDefaultDualBootMode() const {
        if (!disks_.empty() && selected_disk_idx_ >= 0 &&
            selected_disk_idx_ < static_cast<int>(disks_.size())) {
            const auto& d = disks_[static_cast<std::size_t>(selected_disk_idx_)];
            if (find_best_free_region(d) != nullptr) return InstallMode::DualBootFreeSpace;
            if (find_best_shrinkable_partition(d) != nullptr) return InstallMode::DualBootResize;
        }
        return InstallMode::DualBootResize;
    }

    void positionChrome() {
        const int bottom = (oobe_form() == OobeForm::Phone) ? 24 : 145;
        if (shared_nav_box_) {
            const int y = std::max(16, height() - bottom - shared_nav_box_->height());
            const int x = (width() - shared_nav_box_->width()) / 2;
            shared_nav_box_->move(x, y);
            shared_nav_box_->raise();
        }
        if (footer_links_) {
            footer_links_->adjustSize();
            footer_links_->move(oobe_gutter(), height() - 44);
            footer_links_->raise();
        }
        if (terminal_win_ && terminal_win_->isVisible()) terminal_win_->raise();
    }

    void toggleTerminalWindow() {
        if (!terminal_win_) terminal_win_ = new RecoveryTerminalWindow(this);
        if (terminal_win_->isVisible()) {
            terminal_win_->hide();
        } else {
            terminal_win_->move(std::max(24, (width() - terminal_win_->width()) / 2),
                                std::max(24, (height() - terminal_win_->height()) / 2));
            terminal_win_->show();
            terminal_win_->raise();
            terminal_win_->focusTerminal();
        }
    }

    void applyKeyboardLayout(int idx) {
        if (idx < 0 || idx >= static_cast<int>(world_kb_layouts_.size())) return;
        kb_idx_ = idx;
        const auto& kb = world_kb_layouts_[static_cast<std::size_t>(idx)];
        kb_link_->setLabelText(QStringLiteral("Keyboard: %1").arg(kb.code_badge));
        footer_links_->adjustSize();
        for (const auto& pair : kb_rows_) pair.second->setSelected(pair.first == idx);
        if (kb_blur_ov_) kb_blur_ov_->invalidateCache();
        QProcess::startDetached(QStringLiteral("loadkeys"), {kb.loadkeys_name});
        QProcess::startDetached(QStringLiteral("localectl"),
                                {QStringLiteral("--no-convert"), QStringLiteral("set-keymap"), kb.loadkeys_name});
        FinnThemeManager::instance().setKeyboardLayout(kb.xkb_layout, kb.xkb_variant);
        if (xkb_filter_) xkb_filter_->setLayout(kb.xkb_layout, kb.xkb_variant);
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
        if (prev == idx) { updateChrome(); return; }

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
            if (stack_->widget(idx)->layout()) stack_->widget(idx)->layout()->activate();
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
        const bool is_done = (idx == PageComplete);
        shared_nav_box_->setVisible(!is_prog && !is_done);
        nav_back_->setVisible(!is_hub && !is_prog && !is_done);
        nav_next_->setVisible(!is_prog && !is_done && idx != PageDiskUtility &&
                              idx != PageLogs && idx != PageKeyboard && idx != PageNetwork);
        if (idx == PageHub) {
            nav_next_->setEnabled(hub_choice_ >= 0);
        } else if (idx == PageInstallTarget) {
            const bool has_valid = !disks_.empty() &&
                !disks_[static_cast<std::size_t>(selected_disk_idx_)].is_live_medium;
            nav_next_->setEnabled(has_valid);
        } else if (idx == PageInstallReview) {
            const bool needs_confirm =
                (selected_mode_ == InstallMode::CleanErase ||
                 selected_mode_ == InstallMode::CleanEraseAB ||
                 selected_mode_ == InstallMode::TargetPartition);
            nav_next_->setEnabled(current_plan_.valid && (!needs_confirm || destructive_confirmed_));
        } else {
            nav_next_->setEnabled(true);
        }
        positionChrome();
    }

    void goBack() {
        const int idx = stack_->currentIndex();
        if (idx == PageKeyboard || idx == PageNetwork) { goTo(prev_utility_page_); return; }
        if (idx == PageDiskUtility && du_return_to_target_) {
            du_return_to_target_ = false;
            disks_ = probe_system_disks();
            detected_oses_ = detect_oses(disks_);
            refreshTargets();
            goTo(PageInstallTarget);
            return;
        }
        if (idx == PageInstallMode || idx == PageDiskUtility || idx == PageBootRepair ||
            idx == PageLogs || idx == PageInstallProgress || idx == PageComplete) {
            goTo(PageHub);
        } else if (idx == PageInstallTarget) {
            goTo(selected_mode_ == InstallMode::ReinstallPreserveData ? PageHub : PageInstallMode);
        } else if (idx == PageInstallReview) {
            goTo(PageInstallTarget);
        }
    }

    void goNext() {
        const int idx = stack_->currentIndex();
        if (idx == PageHub) {
            if (hub_choice_ == 0) {
                goTo(PageInstallMode);
            } else if (hub_choice_ == 1) {
                selected_mode_ = InstallMode::ReinstallPreserveData;
                disks_ = probe_system_disks();
                detected_oses_ = detect_oses(disks_);
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
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        auto* prog_list = new OobeProgressiveList(list_wrap);
        auto* blur_ov = new ProgressiveBlurOverlay(prog_list, list_wrap);
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(prog_list, 0, 0);
        stack_lay->addWidget(blur_ov, 0, 0);

        struct HubItem { const char* title; QString meta; };
        const QString img_meta = host_env_.image.found
            ? QStringLiteral("Offline")
            : QStringLiteral("No image");
        const HubItem items[] = {
            {"Install FinnOS", img_meta},
            {"Reinstall FinnOS", QStringLiteral("Keep data")},
            {"Partition & Disks", QStringLiteral("GPT / MBR")},
            {"Repair Startup", QString::fromUtf8(firmware_mode_label(host_env_.firmware).data(),
                                                 static_cast<int>(firmware_mode_label(host_env_.firmware).size()))},
            {"System Logs & Verify", QStringLiteral("Integrity")},
        };

        for (int i = 0; i < 5; ++i) {
            auto* row = new OobeOptionRow(QString::fromUtf8(items[i].title),
                                          items[i].meta, i == 0, prog_list->hostWidget());
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

        QStringList sub_parts;
        sub_parts << QStringLiteral("%1 · %2")
                         .arg(QString::fromUtf8(firmware_mode_label(host_env_.firmware).data(),
                                                static_cast<int>(firmware_mode_label(host_env_.firmware).size())),
                              host_env_.image.found ? QString::fromStdString(host_env_.image.label)
                                                    : QStringLiteral("Offline image missing"));
        if (!detected_oses_.empty()) {
            QStringList names;
            for (const auto& os : detected_oses_) {
                const QString n = QString::fromStdString(os.name);
                if (!names.contains(n)) names << n;
            }
            sub_parts << (QStringLiteral("Found: ") + names.join(QStringLiteral(", ")));
        }
        return create_oobe_split_page(QStringLiteral("boot-mark"),
                                      QStringLiteral("What do you need?"),
                                      sub_parts.join(QLatin1Char('\n')), list_wrap);
    }

    // ── Page 1: Install Mode ─────────────────────────────────────────────────
    QWidget* buildInstallModePage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(oobe_content_width() + 6);
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
            {InstallMode::DualBootResize, "Alongside existing OS", "Shrink & dual-boot"},
            {InstallMode::DualBootFreeSpace, "Use free space", "Dual-boot gap"},
            {InstallMode::CleanErase, "Erase disk", "GPT or MBR"},
            {InstallMode::CleanEraseAB, "Erase disk (A/B)", "Rollback OTA"},
            {InstallMode::TargetPartition, "Specific partition", "Manual"},
        };
        for (int i = 0; i < 5; ++i) {
            const InstallMode m = opts[i].mode;
            auto* row = new OobeOptionRow(QString::fromUtf8(opts[i].title),
                                          QString::fromUtf8(opts[i].meta),
                                          m == selected_mode_, prog_list->hostWidget());
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
                                      QStringLiteral("Installs directly offline. Personal setup runs on first boot."),
                                      list_wrap);
    }

    // ── Page 2: Pick Drive / Partition + Dual-Boot Controls ──────────────────
    QWidget* buildInstallTargetPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
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

        // Clean Erase options (GPT vs MBR toggle + Offline Recovery Partition toggle)
        clean_opts_host_ = new QWidget(host);
        auto* cl = new QVBoxLayout(clean_opts_host_);
        cl->setContentsMargins(0, 4, 0, 0);
        cl->setSpacing(6);
        mbr_toggle_row_ = new OobeToggleRow(QStringLiteral("Use MBR table (Legacy)"), false, clean_opts_host_);
        connect(mbr_toggle_row_, &OobeToggleRow::toggled, this, [this](bool on) {
            preferred_table_ = on ? PartitionTableKind::MBR : PartitionTableKind::GPT;
        });
        recovery_part_toggle_row_ = new OobeToggleRow(QStringLiteral("Save offline Recovery partition"), true, clean_opts_host_);
        connect(recovery_part_toggle_row_, &OobeToggleRow::toggled, this, [this](bool on) {
            create_recovery_part_ = on;
        });
        cl->addWidget(mbr_toggle_row_);
        cl->addWidget(recovery_part_toggle_row_);
        clean_opts_host_->setVisible(false);
        col->addWidget(clean_opts_host_);

        // Dual-Boot Resize slider controls
        resize_host_ = new QWidget(host);
        auto* rl = new QVBoxLayout(resize_host_);
        rl->setContentsMargins(2, 6, 2, 0);
        rl->setSpacing(6);
        resize_label_ = new QLabel(resize_host_);
        resize_label_->setFont(oobe_font(13, QFont::Normal));
        resize_label_->setWordWrap(true);
        resize_slider_ = new QSlider(Qt::Horizontal, resize_host_);
        resize_slider_->setRange(14, 200);
        resize_slider_->setValue(40);
        connect(resize_slider_, &QSlider::valueChanged, this, [this](int gb) {
            resize_gib_ = gb;
            updateResizeLabel();
        });
        rl->addWidget(resize_label_);
        rl->addWidget(resize_slider_);
        resize_host_->setVisible(false);
        col->addWidget(resize_host_);

        target_note_lbl_ = new QLabel(host);
        target_note_lbl_->setFont(oobe_font(12, QFont::Normal));
        target_note_lbl_->setWordWrap(true);
        target_note_lbl_->setVisible(false);
        col->addWidget(target_note_lbl_);

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

    void updateResizeLabel() {
        if (disks_.empty() || !resize_label_) return;
        const auto& d = disks_[static_cast<std::size_t>(selected_disk_idx_)];
        const PartitionInfo* best = find_best_shrinkable_partition(d);
        if (!best) return;
        const std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
        const std::uint64_t freed = static_cast<std::uint64_t>(resize_gib_) * kGiB;
        const std::uint64_t kept = (best->size_bytes > freed) ? (best->size_bytes - freed) : best->min_size_bytes;
        resize_label_->setText(
            QStringLiteral("Shrink %1: keep %2, give %3 GiB to FinnOS")
                .arg(QString::fromStdString(best->path),
                     QString::fromStdString(format_bytes(kept)))
                .arg(resize_gib_));
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
            clean_opts_host_->setVisible(false);
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
                    if (part.is_esp || part.is_bios_boot || part.is_extended || part.is_recovery_part) continue;
                    const int idx = row_idx++;
                    const QString lbl = QString::fromStdString(
                        part.path + (part.fstype.empty() ? "  (raw)" : "  " + part.fstype));
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
                    if (idx == 0) { selected_disk_idx_ = di; selected_partition_path_ = ppath; }
                    target_part_paths_.push_back(ppath);
                    target_rows_.push_back(row);
                    target_list_lay_->addWidget(row);
                }
            }
            if (target_rows_.empty()) {
                auto* empty = new QLabel(
                    QStringLiteral("No eligible partitions found. Use Partition drive or choose Erase disk."),
                    target_list_host_);
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
                const QString label = QString::fromStdString(d.model.empty() ? d.path : d.model);
                QString meta;
                if (d.is_live_medium) {
                    meta = QStringLiteral("Live Media");
                } else {
                    const QString pt = QString::fromUtf8(
                        partition_table_label(d.table_kind).data(),
                        static_cast<int>(partition_table_label(d.table_kind).size()));
                    meta = QStringLiteral("%1 · %2").arg(QString::fromStdString(format_bytes(d.size_bytes)), pt);
                }
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

        const bool is_clean = (selected_mode_ == InstallMode::CleanErase ||
                               selected_mode_ == InstallMode::CleanEraseAB);
        const bool is_dual_resize = (selected_mode_ == InstallMode::DualBootResize);
        const bool is_dual_free = (selected_mode_ == InstallMode::DualBootFreeSpace);

        clean_opts_host_->setVisible(is_clean);
        if (mbr_toggle_row_) {
            mbr_toggle_row_->setVisible(selected_mode_ == InstallMode::CleanErase);
        }
        if (recovery_part_toggle_row_) {
            const bool can_rec = (d.size_bytes == 0 || d.size_bytes >= 24ULL * 1024ULL * 1024ULL * 1024ULL);
            recovery_part_toggle_row_->setEnabled(can_rec);
            if (!can_rec) {
                recovery_part_toggle_row_->toggle()->setChecked(false);
                create_recovery_part_ = false;
            }
        }

        resize_host_->setVisible(is_dual_resize);
        if (is_dual_resize) {
            const PartitionInfo* best = find_best_shrinkable_partition(d);
            if (!best) {
                resize_slider_->setEnabled(false);
                resize_label_->setText(
                    QStringLiteral("No shrinkable NTFS/ext4/btrfs partition on %1. "
                                   "For macOS APFS, shrink in macOS Disk Utility and pick 'Use free space'.")
                        .arg(QString::fromStdString(d.path)));
            } else {
                constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
                const int min_free_gb = (host_env_.firmware == FirmwareMode::UEFI && d.esp_partition_path.empty()) ? 13 : 12;
                const int max_free_gb = static_cast<int>((best->size_bytes - best->min_size_bytes) / kGiB);
                const int hi = std::max(min_free_gb, max_free_gb);
                resize_slider_->setEnabled(true);
                resize_slider_->setRange(min_free_gb, hi);
                resize_gib_ = std::clamp(resize_gib_, min_free_gb, hi);
                resize_slider_->setValue(resize_gib_);
                updateResizeLabel();
            }
        }

        // Show automatic EFI / BIOS / Free Space status honestly right on the target screen.
        QStringList notes;
        if (is_dual_free) {
            if (const FreeRegion* gap = find_best_free_region(d)) {
                notes << QStringLiteral("Found %1 of unallocated space on %2.")
                             .arg(QString::fromStdString(format_bytes(gap->size_bytes)),
                                  QString::fromStdString(d.path));
            } else {
                notes << QStringLiteral("No unallocated space >= 12 GiB on %1.").arg(QString::fromStdString(d.path));
            }
        }
        if (!is_clean && host_env_.firmware == FirmwareMode::UEFI && d.esp_partition_path.empty()) {
            notes << QStringLiteral("No EFI partition on this drive — Recovery Mode will generate a 512 MB FAT32 EFI partition automatically.");
        } else if (!is_clean && host_env_.firmware == FirmwareMode::LegacyBios &&
                   d.table_kind == PartitionTableKind::GPT && !d.has_bios_boot) {
            notes << QStringLiteral("GPT drive lacks a BIOS Boot partition — Recovery Mode will generate one automatically.");
        }
        if (target_note_lbl_) {
            const QString txt = notes.join(QLatin1Char(' '));
            target_note_lbl_->setText(txt);
            target_note_lbl_->setVisible(!txt.isEmpty());
            const auto& t = FinnThemeManager::instance().tokens();
            QPalette pal = target_note_lbl_->palette();
            pal.setColor(QPalette::WindowText, t.dim);
            target_note_lbl_->setPalette(pal);
        }
    }

    // ── Page 3: Review & Explicit Destructive Confirmation ───────────────────
    QWidget* buildInstallReviewPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(12);

        review_label_ = new QLabel(host);
        review_label_->setFont(oobe_font(14, QFont::Normal));
        review_label_->setWordWrap(true);
        col->addWidget(review_label_);

        review_confirm_host_ = new QWidget(host);
        review_confirm_lay_ = new QVBoxLayout(review_confirm_host_);
        review_confirm_lay_->setContentsMargins(0, 6, 0, 0);
        review_confirm_lay_->setSpacing(4);
        col->addWidget(review_confirm_host_);

        col->addStretch(1);
        return create_oobe_split_page(QStringLiteral("shield"),
                                      QStringLiteral("Ready to install?"),
                                      QStringLiteral("Review carefully before proceeding."),
                                      host);
    }

    InstallRequest buildCurrentRequest() const {
        InstallRequest req;
        req.mode = selected_mode_;
        req.preferred_table = preferred_table_;
        req.create_recovery_partition = create_recovery_part_;
        req.offline_only = true;
        req.confirmed = true;
        if (!disks_.empty() && selected_disk_idx_ >= 0 &&
            selected_disk_idx_ < static_cast<int>(disks_.size())) {
            const auto& d = disks_[static_cast<std::size_t>(selected_disk_idx_)];
            req.target_disk = d.path;
            req.esp_partition = d.esp_partition_path;
            if (req.mode == InstallMode::DualBootResize) {
                if (const PartitionInfo* best = find_best_shrinkable_partition(d)) {
                    req.resize_partition = best->path;
                    constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
                    const std::uint64_t want = static_cast<std::uint64_t>(resize_gib_) * kGiB;
                    const std::uint64_t new_bytes =
                        (best->size_bytes > want && (best->size_bytes - want) >= best->min_size_bytes)
                            ? (best->size_bytes - want)
                            : best->min_size_bytes;
                    req.resize_new_size_mb = new_bytes / (1024ULL * 1024ULL);
                }
            } else if (req.mode == InstallMode::TargetPartition ||
                       req.mode == InstallMode::ReinstallPreserveData) {
                req.target_partition = selected_partition_path_;
                if (req.target_partition.empty()) {
                    for (const auto& p : d.partitions) {
                        if (!p.is_esp && !p.is_bios_boot && !p.is_extended) {
                            req.target_partition = p.path;
                            break;
                        }
                    }
                }
            }
        }
        return req;
    }

    void refreshReview() {
        destructive_confirmed_ = false;
        if (review_confirm_lay_) {
            while (QLayoutItem* it = review_confirm_lay_->takeAt(0)) {
                if (it->widget()) it->widget()->deleteLater();
                delete it;
            }
        }

        const InstallRequest req = buildCurrentRequest();
        current_plan_ = plan_install(req, disks_, host_env_);

        std::string destructive_target = req.target_disk;
        if (req.mode == InstallMode::TargetPartition) destructive_target = req.target_partition;

        if (!current_plan_.valid) {
            review_label_->setText(QString::fromStdString(current_plan_.error));
        } else {
            QStringList lines;
            for (const auto& fix : current_plan_.auto_fixes) {
                lines << QStringLiteral("\u2713 Auto-fix: ") + QString::fromStdString(fix);
            }
            for (const auto& step : current_plan_.steps) {
                lines << QStringLiteral("\u2022 ") + QString::fromStdString(step);
            }
            for (const auto& warn : current_plan_.warnings) {
                lines << QStringLiteral("\u26A0 ") + QString::fromStdString(warn);
            }
            review_label_->setText(lines.join(QLatin1Char('\n')));

            const bool needs_confirm =
                (selected_mode_ == InstallMode::CleanErase ||
                 selected_mode_ == InstallMode::CleanEraseAB ||
                 selected_mode_ == InstallMode::TargetPartition);
            if (needs_confirm && review_confirm_lay_) {
                auto* toggle = new OobeToggleRow(
                    QStringLiteral("Erase %1").arg(QString::fromStdString(destructive_target)),
                    false, review_confirm_host_);
                connect(toggle, &OobeToggleRow::toggled, this, [this](bool checked) {
                    destructive_confirmed_ = checked;
                    updateChrome();
                });
                review_confirm_lay_->addWidget(toggle);
            }
        }
        updateChrome();
    }

    // ── Page 4: OOBE-Styled Install Progress ─────────────────────────────────
    QWidget* buildInstallProgressPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(12);

        install_step_lbl_ = new QLabel(QStringLiteral("Verifying offline OS image..."), host);
        install_step_lbl_->setFont(oobe_font(16, QFont::Medium));
        install_step_lbl_->setWordWrap(true);
        col->addWidget(install_step_lbl_);

        install_bar_ = new OobeProgressBar(host);
        col->addWidget(install_bar_);

        install_detail_lbl_ = new QLabel(QStringLiteral("Installing directly from local media."), host);
        install_detail_lbl_->setFont(oobe_font(13, QFont::Normal));
        install_detail_lbl_->setWordWrap(true);
        col->addWidget(install_detail_lbl_);

        auto* toggle_log = new QuietLink(QStringLiteral("Show install log"), false, host);
        connect(toggle_log, &QAbstractButton::clicked, this, [this, toggle_log]() {
            const bool vis = !install_log_->isVisible();
            install_log_->setVisible(vis);
            toggle_log->setLabelText(vis ? QStringLiteral("Hide install log") : QStringLiteral("Show install log"));
        });
        col->addWidget(toggle_log, 0, Qt::AlignLeft);

        install_log_ = new QPlainTextEdit(host);
        install_log_->setReadOnly(true);
        install_log_->setFrameShape(QFrame::NoFrame);
        install_log_->setFixedHeight(220);
        install_log_->setFont(finn_mono_font(11.0, QFont::Normal));
        install_log_->setVisible(false);
        col->addWidget(install_log_);
        col->addStretch(1);

        return create_oobe_split_page(QStringLiteral("download"),
                                      QStringLiteral("Installing FinnOS"),
                                      QStringLiteral("This takes a few minutes."),
                                      host, &install_sub_lbl_);
    }

    void parseInstallStepLine(const QString& line) {
        static const QRegularExpression step_re(QStringLiteral(R"(=====\s*\[(\d+)/(\d+)\]\s*(.+?)\s*=====)"));
        const auto m = step_re.match(line);
        if (m.hasMatch()) {
            const int cur = m.captured(1).toInt();
            const int tot = std::max(1, m.captured(2).toInt());
            const QString label = m.captured(3).trimmed();
            if (install_step_lbl_) install_step_lbl_->setText(label);
            if (install_detail_lbl_) {
                install_detail_lbl_->setText(QStringLiteral("Step %1 of %2").arg(cur).arg(tot));
            }
            if (install_bar_) install_bar_->setProgress(static_cast<double>(cur) / static_cast<double>(tot));
        }
    }

    void startInstall() {
        if (!current_plan_.valid || current_plan_.finn_install_args.empty()) return;
        if (!disks_.empty() && selected_disk_idx_ >= 0 &&
            selected_disk_idx_ < static_cast<int>(disks_.size())) {
            unmountDiskPartitions(disks_[static_cast<std::size_t>(selected_disk_idx_)]);
        }
        goTo(PageInstallProgress);
        install_log_->clear();
        install_log_->setVisible(false);
        install_step_lbl_->setText(QStringLiteral("Starting offline installer..."));
        install_detail_lbl_->setText(QStringLiteral("Verifying local image integrity."));
        install_bar_->setIndeterminate();

        auto* proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        QStringList args;
        for (const auto& a : current_plan_.finn_install_args) {
            args << QString::fromStdString(a);
        }
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
            const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
            for (const QString& ln : chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                install_log_->appendPlainText(ln);
                parseInstallStepLine(ln);
            }
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc](int code, QProcess::ExitStatus st) {
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        install_bar_->setProgress(1.0);
                        complete_title_->setText(QStringLiteral("FinnOS is installed"));
                        complete_sub_->setText(QStringLiteral("Remove installation media and restart. First-time setup will greet you on boot."));
                        goTo(PageComplete);
                    } else {
                        install_step_lbl_->setText(QStringLiteral("Installation did not complete"));
                        install_detail_lbl_->setText(QStringLiteral("Installer exited with code %1. Review the log below.").arg(code));
                        install_log_->setVisible(true);
                        shared_nav_box_->setVisible(true);
                        nav_back_->setVisible(true);
                        nav_next_->setVisible(false);
                    }
                });
        proc->start(QString::fromUtf8(finn::kFinnInstallBin), args);
    }

    // ── Page 5: Plain, calm OOBE-matched Completion Screen ───────────────────
    QWidget* buildCompletePage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(oobe_gutter(), 0, oobe_gutter(), 0);
        col->setSpacing(0);

        col->addStretch(45);
        complete_icon_lbl_ = new QLabel(page);
        complete_icon_lbl_->setFixedSize(56, 56);
        complete_icon_lbl_->setAlignment(Qt::AlignCenter);
        col->addWidget(complete_icon_lbl_, 0, Qt::AlignHCenter);
        col->addSpacing(28);

        complete_title_ = new QLabel(QStringLiteral("You\u2019re all set"), page);
        complete_title_->setFont(oobe_font(26, QFont::Medium));
        complete_title_->setAlignment(Qt::AlignCenter);
        col->addWidget(complete_title_, 0, Qt::AlignHCenter);
        col->addSpacing(10);

        complete_sub_ = new QLabel(
            QStringLiteral("Remove installation media and restart."), page);
        complete_sub_->setFont(oobe_font(14, QFont::Normal));
        complete_sub_->setAlignment(Qt::AlignCenter);
        complete_sub_->setWordWrap(true);
        complete_sub_->setFixedWidth(440);
        col->addWidget(complete_sub_, 0, Qt::AlignHCenter);
        col->addSpacing(32);

        auto* restart_pill = new OobeGetStartedPill(QStringLiteral("Restart now"), page);
        connect(restart_pill, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
        });
        col->addWidget(restart_pill, 0, Qt::AlignHCenter);
        col->addSpacing(10);

        auto* back_hub = new QuietLink(QStringLiteral("Back to Recovery"), false, page);
        connect(back_hub, &QAbstractButton::clicked, this, [this]() { goTo(PageHub); });
        col->addWidget(back_hub, 0, Qt::AlignHCenter);
        col->addStretch(55);

        auto sync_colors = [this]() {
            const auto& t = FinnThemeManager::instance().tokens();
            if (complete_icon_lbl_) {
                complete_icon_lbl_->setPixmap(FinnThemeManager::instance().iconPixmap(
                    QStringLiteral("boot-mark"), QSize(56, 56), t.ink,
                    complete_icon_lbl_->devicePixelRatioF()));
            }
            QPalette p1 = complete_title_->palette();
            p1.setColor(QPalette::WindowText, t.ink);
            complete_title_->setPalette(p1);
            QPalette p2 = complete_sub_->palette();
            p2.setColor(QPalette::WindowText, t.dim);
            complete_sub_->setPalette(p2);
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged, page, sync_colors);
        sync_colors();
        return page;
    }

    // ── Page 6: Partition & Disk Utility (GPT & MBR Safe) ────────────────────
    QWidget* buildDiskUtilityPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(10);

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

        auto* act_row1 = new QHBoxLayout();
        act_row1->setSpacing(6);
        auto* b_new = new QuietLink(QStringLiteral("New"), false, host);
        auto* b_del = new QuietLink(QStringLiteral("Delete"), true, host);
        auto* b_fmt = new QuietLink(QStringLiteral("Format"), false, host);
        auto* b_chk = new QuietLink(QStringLiteral("First Aid"), false, host);
        auto* b_ref = new QuietLink(QStringLiteral("Refresh"), false, host);
        connect(b_new, &QAbstractButton::clicked, this, [this]() {
            du_create_panel_->setVisible(!du_create_panel_->isVisible());
            updateNewPartitionSliderBounds();
        });
        connect(b_del, &QAbstractButton::clicked, this, &RecoveryWindow::deleteSelectedPartition);
        connect(b_fmt, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FormatExt4); });
        connect(b_chk, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FirstAidCheck); });
        connect(b_ref, &QAbstractButton::clicked, this, [this]() {
            disks_ = probe_system_disks();
            detected_oses_ = detect_oses(disks_);
            refreshDiskUtility();
            du_status_->setText(QStringLiteral("Refreshed disk table."));
        });
        act_row1->addWidget(b_new);
        act_row1->addWidget(b_del);
        act_row1->addWidget(b_fmt);
        act_row1->addWidget(b_chk);
        act_row1->addWidget(b_ref);
        act_row1->addStretch(1);
        col->addLayout(act_row1);

        auto* act_row2 = new QHBoxLayout();
        act_row2->setSpacing(6);
        auto* b_gpt = new QuietLink(QStringLiteral("New GPT"), true, host);
        auto* b_mbr = new QuietLink(QStringLiteral("New MBR"), true, host);
        connect(b_gpt, &QAbstractButton::clicked, this, [this]() { wipeDiskTable(PartitionTableKind::GPT); });
        connect(b_mbr, &QAbstractButton::clicked, this, [this]() { wipeDiskTable(PartitionTableKind::MBR); });
        act_row2->addWidget(b_gpt);
        act_row2->addWidget(b_mbr);
        act_row2->addStretch(1);
        col->addLayout(act_row2);

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
        cp_row->setSpacing(8);
        auto* b_fs = new QuietLink(QStringLiteral("FS: ext4"), false, du_create_panel_);
        connect(b_fs, &QAbstractButton::clicked, this, [this, b_fs]() {
            du_new_fs_ = (du_new_fs_ == QLatin1String("ext4")) ? QStringLiteral("btrfs")
                       : (du_new_fs_ == QLatin1String("btrfs")) ? QStringLiteral("fat32")
                       : QStringLiteral("ext4");
            b_fs->setLabelText(QStringLiteral("FS: %1").arg(du_new_fs_));
            const bool is_max = (du_new_size_gib_ >= du_new_size_slider_->maximum());
            du_new_size_lbl_->setText(is_max
                ? QStringLiteral("New partition: All remaining space (%1)").arg(du_new_fs_)
                : QStringLiteral("New partition: %1 GiB (%2)").arg(du_new_size_gib_).arg(du_new_fs_));
        });
        auto* b_efi = new QuietLink(QStringLiteral("+512M EFI"), false, du_create_panel_);
        connect(b_efi, &QAbstractButton::clicked, this, [this]() {
            if (disks_.empty()) return;
            const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
            const auto plan = plan_disk_create_partition(d, 0, false, "efi");
            if (!plan.valid) { du_status_->setText(QString::fromStdString(plan.error)); return; }
            unmountDiskPartitions(d);
            du_create_panel_->setVisible(false);
            QStringList args;
            for (const auto& a : plan.args) args << QString::fromStdString(a);
            runDiskCommand(QString::fromStdString(plan.program), args, QString::fromStdString(plan.summary));
        });
        auto* b_apply = new QuietLink(QStringLiteral("Create & Format"), false, du_create_panel_);
        connect(b_apply, &QAbstractButton::clicked, this, &RecoveryWindow::createNewPartition);
        cp_row->addWidget(b_fs);
        cp_row->addWidget(b_efi);
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
                                      QStringLiteral("Manage GPT or MBR partitions safely."),
                                      host);
    }

    void updateNewPartitionSliderBounds() {
        if (disks_.empty() || !du_new_size_slider_) return;
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        std::uint64_t used = 0;
        for (const auto& p : d.partitions) if (!p.is_extended) used += p.size_bytes;
        const std::uint64_t free_bytes = (d.size_bytes > used) ? (d.size_bytes - used) : 0;
        const int free_gib = std::max(1, static_cast<int>(free_bytes / (1024ULL * 1024ULL * 1024ULL)));
        du_new_size_slider_->setRange(1, free_gib);
        du_new_size_slider_->setValue(free_gib);
        du_new_size_gib_ = free_gib;
        du_new_size_lbl_->setText(
            QStringLiteral("New partition: All remaining (~%1 GiB, %2)").arg(free_gib).arg(du_new_fs_));
    }

    void refreshDiskUtility() {
        if (!du_list_lay_ || !du_disk_lay_) return;
        while (QLayoutItem* it = du_disk_lay_->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
        while (QLayoutItem* it = du_list_lay_->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
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

        for (int di = 0; di < static_cast<int>(disks_.size()); ++di) {
            const auto& d = disks_[static_cast<std::size_t>(di)];
            const QString lbl = QString::fromStdString(d.model.empty() ? d.path : (d.model + " (" + d.name + ")"));
            const QString meta = d.is_live_medium
                ? QStringLiteral("Live Media")
                : QStringLiteral("%1 · %2").arg(
                    QString::fromStdString(format_bytes(d.size_bytes)),
                    QString::fromUtf8(partition_table_label(d.table_kind).data(),
                                      static_cast<int>(partition_table_label(d.table_kind).size())));
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
        if (du_selected_part_ >= static_cast<int>(sel_disk.partitions.size())) du_selected_part_ = 0;

        for (int pi = 0; pi < static_cast<int>(sel_disk.partitions.size()); ++pi) {
            const auto& part = sel_disk.partitions[static_cast<std::size_t>(pi)];
            du_parts_.push_back(part);
            QString tag = part.is_esp ? QStringLiteral("EFI")
                        : part.is_bios_boot ? QStringLiteral("BIOS")
                        : part.is_recovery_part ? QStringLiteral("Recovery")
                        : QString::fromStdString(part.fstype.empty() ? "raw" : part.fstype);
            const QString lbl = QStringLiteral("%1  %2").arg(QString::fromStdString(part.path), tag);
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
        if (disks_.empty()) return;
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        const bool use_all = (du_new_size_gib_ >= du_new_size_slider_->maximum());
        const auto plan = plan_disk_create_partition(
            d, static_cast<std::uint64_t>(std::max(1, du_new_size_gib_)),
            use_all, du_new_fs_.toStdString());
        if (!plan.valid) { du_status_->setText(QString::fromStdString(plan.error)); return; }
        unmountDiskPartitions(d);
        du_create_panel_->setVisible(false);
        QStringList args;
        for (const auto& a : plan.args) args << QString::fromStdString(a);
        runDiskCommand(QString::fromStdString(plan.program), args, QString::fromStdString(plan.summary));
    }

    void deleteSelectedPartition() {
        if (du_parts_.empty() || du_selected_part_ >= static_cast<int>(du_parts_.size())) {
            du_status_->setText(QStringLiteral("Select a partition to delete."));
            return;
        }
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        const auto& part = du_parts_[static_cast<std::size_t>(du_selected_part_)];
        const auto plan = plan_disk_delete_partition(d, part);
        if (!plan.valid) { du_status_->setText(QString::fromStdString(plan.error)); return; }
        QProcess::execute(QStringLiteral("swapoff"), {QString::fromStdString(part.path)});
        QProcess::execute(QStringLiteral("umount"), {QStringLiteral("-f"), QString::fromStdString(part.path)});
        QStringList args;
        for (const auto& a : plan.args) args << QString::fromStdString(a);
        runDiskCommand(QString::fromStdString(plan.program), args, QString::fromStdString(plan.summary));
    }

    void wipeDiskTable(PartitionTableKind kind) {
        if (disks_.empty()) return;
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        const auto plan = (kind == PartitionTableKind::MBR) ? plan_disk_wipe_mbr(d) : plan_disk_wipe_gpt(d);
        if (!plan.valid) { du_status_->setText(QString::fromStdString(plan.error)); return; }
        unmountDiskPartitions(d);
        QStringList args;
        for (const auto& a : plan.args) args << QString::fromStdString(a);
        runDiskCommand(QString::fromStdString(plan.program), args, QString::fromStdString(plan.summary));
    }

    void runDiskAction(DiskOpKind kind) {
        if (du_parts_.empty() || du_selected_part_ >= static_cast<int>(du_parts_.size())) {
            du_status_->setText(QStringLiteral("Select a partition first."));
            return;
        }
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        if (d.is_live_medium) { du_status_->setText(QStringLiteral("Cannot modify the active Live Media.")); return; }
        const auto& part = du_parts_[static_cast<std::size_t>(du_selected_part_)];
        QProcess::execute(QStringLiteral("swapoff"), {QString::fromStdString(part.path)});
        QProcess::execute(QStringLiteral("umount"), {QStringLiteral("-f"), QString::fromStdString(part.path)});
        PartitionInfo clean_part = part;
        clean_part.mountpoint.clear();
        const auto plan = plan_disk_op(kind, clean_part);
        if (!plan.valid) { du_status_->setText(QString::fromStdString(plan.error)); return; }
        QStringList args;
        for (const auto& a : plan.args) args << QString::fromStdString(a);
        runDiskCommand(QString::fromStdString(plan.program), args, QString::fromStdString(plan.summary));
    }

    // ── Page 7: Bootloader Repair ────────────────────────────────────────────
    QWidget* buildBootRepairPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(8);

        const QStringList opts = {
            QStringLiteral("Reinstall FinnOS bootloader"),
            QStringLiteral("Rescan Windows, macOS & Linux"),
            QStringLiteral("Verify EFI & boot entries"),
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
                                      QStringLiteral("Fixes UEFI, GRUB, missing ESP, and dual-boot entries."),
                                      host);
    }

    void runBootRepair() {
        disks_ = probe_system_disks();
        detected_oses_ = detect_oses(disks_);

        if (boot_choice_ == 0 || boot_choice_ == 1) {
            InstallRequest req;
            req.mode = InstallMode::RepairBootloader;
            req.confirmed = true;
            for (const auto& os : detected_oses_) {
                if (os.kind == OsKind::FinnOS && !os.partition_path.empty()) {
                    req.target_partition = os.partition_path;
                    req.esp_partition = os.esp_path;
                    break;
                }
            }
            if (req.target_partition.empty()) {
                for (const auto& d : disks_) {
                    if (d.is_live_medium) continue;
                    for (const auto& p : d.partitions) {
                        if (!p.is_esp && !p.is_bios_boot && !p.is_recovery_part &&
                            (p.fstype == "ext4" || p.fstype == "btrfs")) {
                            req.target_partition = p.path;
                            req.esp_partition = d.esp_partition_path;
                            break;
                        }
                    }
                    if (!req.target_partition.empty()) break;
                }
            }

            const auto plan = plan_install(req, disks_, host_env_);
            const QString install_bin = QString::fromUtf8(finn::kFinnInstallBin);
            if (plan.valid && QFileInfo(install_bin).isExecutable()) {
                boot_status_->setText(QString::fromStdString("Running: " + plan.summary));
                auto* proc = new QProcess(this);
                proc->setProcessChannelMode(QProcess::MergedChannels);
                QStringList args;
                for (const auto& a : plan.finn_install_args) args << QString::fromStdString(a);
                connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                        this, [this, proc](int code, QProcess::ExitStatus st) {
                            const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                            proc->deleteLater();
                            if (st == QProcess::NormalExit && code == 0) {
                                complete_title_->setText(QStringLiteral("Startup repaired"));
                                complete_sub_->setText(QStringLiteral("Bootloader and dual-boot entries have been rebuilt."));
                                goTo(PageComplete);
                            } else {
                                const QString tail = out.section(QLatin1Char('\n'), -1).trimmed();
                                boot_status_->setText(tail.isEmpty()
                                    ? QStringLiteral("Boot repair failed (exit %1).").arg(code)
                                    : tail);
                            }
                        });
                proc->start(install_bin, args);
                return;
            }
        }

        const QString bootctl = QStandardPaths::findExecutable(QStringLiteral("bootctl"));
        if (bootctl.isEmpty()) {
            boot_status_->setText(QStringLiteral("Neither finn-install nor bootctl is available on this host."));
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
                        boot_status_->setText(out.isEmpty() ? QStringLiteral("Startup check complete.")
                                                            : out.section(QLatin1Char('\n'), 0, 0));
                    } else {
                        boot_status_->setText(out.isEmpty() ? QStringLiteral("Check failed (exit %1).").arg(code)
                                                            : out.section(QLatin1Char('\n'), -1));
                    }
                });
        proc->start(bootctl, args);
    }

    // ── Page 8: System Logs & Offline Image Integrity Verification ───────────
    QWidget* buildLogsPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(8);

        auto* tabs = new QHBoxLayout();
        tabs->setSpacing(4);
        const QStringList sources = {
            QStringLiteral("Summary"),
            QStringLiteral("Integrity"),
            QStringLiteral("dmesg"),
            QStringLiteral("lsblk"),
            QStringLiteral("Install"),
        };
        for (int i = 0; i < sources.size(); ++i) {
            auto* b = new QuietLink(sources[i], false, host);
            connect(b, &QAbstractButton::clicked, this, [this, i]() {
                log_source_idx_ = i;
                refreshLogs();
            });
            tabs->addWidget(b);
        }
        tabs->addStretch(1);
        col->addLayout(tabs);

        logs_edit_ = new QPlainTextEdit(host);
        logs_edit_->setReadOnly(true);
        logs_edit_->setFrameShape(QFrame::NoFrame);
        logs_edit_->setFixedHeight(310);
        logs_edit_->setFont(finn_mono_font(11.0, QFont::Normal));
        col->addWidget(logs_edit_);
        col->addStretch(1);

        refreshLogs();
        return create_oobe_split_page(QStringLiteral("terminal-square"),
                                      QStringLiteral("System logs"),
                                      QStringLiteral("Image integrity, storage, kernel & install logs."),
                                      host);
    }

    void refreshLogs() {
        if (!logs_edit_) return;
        auto run_cap = [](const QString& prog, const QStringList& args) -> QString {
            if (QStandardPaths::findExecutable(prog).isEmpty()) {
                return QStringLiteral("%1 is not available on this host.").arg(prog);
            }
            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(prog, args);
            if (!p.waitForFinished(2500)) { p.kill(); return QStringLiteral("%1 timed out.").arg(prog); }
            const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            return out.isEmpty() ? QStringLiteral("(no output)") : out;
        };

        if (log_source_idx_ == 1) {
            host_env_.image = discover_offline_image();
            const bool ok = verify_offline_image(host_env_.image);
            QString txt;
            txt += QStringLiteral("Offline OS Image Integrity Check\n");
            txt += QStringLiteral("────────────────────────────────\n");
            txt += QStringLiteral("Firmware: %1\n").arg(
                QString::fromUtf8(firmware_mode_label(host_env_.firmware).data(),
                                  static_cast<int>(firmware_mode_label(host_env_.firmware).size())));
            txt += QStringLiteral("Source:   %1\n").arg(
                host_env_.image.found ? QString::fromStdString(host_env_.image.source_path)
                                      : QStringLiteral("(none found)"));
            txt += QStringLiteral("Checksum: %1\n").arg(
                host_env_.image.checksum_path.empty() ? QStringLiteral("(no .sha512 sidecar)")
                                                      : QString::fromStdString(host_env_.image.checksum_path));
            txt += QStringLiteral("Status:   %1\n").arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
            txt += QStringLiteral("Detail:   %1\n").arg(QString::fromStdString(host_env_.image.integrity_detail));
            logs_edit_->setPlainText(txt);
            return;
        }
        if (log_source_idx_ == 2) {
            logs_edit_->setPlainText(run_cap(QStringLiteral("dmesg"), {QStringLiteral("--color=never")}));
            logs_edit_->verticalScrollBar()->setValue(logs_edit_->verticalScrollBar()->maximum());
            return;
        }
        if (log_source_idx_ == 3) {
            logs_edit_->setPlainText(run_cap(QStringLiteral("lsblk"),
                                             {QStringLiteral("-o"),
                                              QStringLiteral("NAME,SIZE,TYPE,PTTYPE,FSTYPE,LABEL,MOUNTPOINT")}));
            return;
        }
        if (log_source_idx_ == 4) {
            QFile f(QStringLiteral("/tmp/finn-install.log"));
            if (!f.exists()) {
                logs_edit_->setPlainText(QStringLiteral("No /tmp/finn-install.log found yet."));
            } else if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString body = QString::fromUtf8(f.readAll());
                logs_edit_->setPlainText(body.isEmpty() ? QStringLiteral("Install log is empty.") : body);
                logs_edit_->verticalScrollBar()->setValue(logs_edit_->verticalScrollBar()->maximum());
            } else {
                logs_edit_->setPlainText(QStringLiteral("Could not open /tmp/finn-install.log."));
            }
            return;
        }

        QString txt = run_cap(QStringLiteral("uname"), {QStringLiteral("-a")});
        txt += QStringLiteral("\nFirmware: %1\nOffline image: %2\nDetected disks: %3\nDetected OSes: %4\n")
                   .arg(QString::fromUtf8(firmware_mode_label(host_env_.firmware).data(),
                                          static_cast<int>(firmware_mode_label(host_env_.firmware).size())),
                        host_env_.image.found ? QString::fromStdString(host_env_.image.source_path)
                                              : QStringLiteral("not found"))
                   .arg(disks_.size()).arg(detected_oses_.size());
        for (const auto& d : disks_) {
            txt += QStringLiteral("  %1 (%2, %3)%4\n")
                       .arg(QString::fromStdString(d.path),
                            QString::fromStdString(format_bytes(d.size_bytes)),
                            QString::fromUtf8(partition_table_label(d.table_kind).data(),
                                              static_cast<int>(partition_table_label(d.table_kind).size())),
                            d.is_live_medium ? QStringLiteral(" [LIVE MEDIA]") : QString());
        }
        for (const auto& os : detected_oses_) {
            txt += QStringLiteral("  OS: %1 — %2\n")
                       .arg(QString::fromStdString(os.name),
                            QString::fromStdString(os.detail));
        }
        logs_edit_->setPlainText(txt);
    }

    // ── Page 9: World Keyboard Layout Picker (OOBE Exact) ────────────────────
    QWidget* buildKeyboardPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        host->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        host->setAttribute(Qt::WA_TranslucentBackground, true);

        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(10);

        auto* search = new OobeInputField(QStringLiteral("Search keyboard layouts (it, de, jp...)"), false, host);
        connect(search->lineEdit(), &QLineEdit::textChanged, this, &RecoveryWindow::rebuildKeyboardList);
        col->addWidget(search);

        auto* list_wrap = new QWidget(host);
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        kb_prog_list_ = new OobeProgressiveList(list_wrap);
        kb_blur_ov_ = new ProgressiveBlurOverlay(kb_prog_list_, list_wrap);
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(kb_prog_list_, 0, 0);
        stack_lay->addWidget(kb_blur_ov_, 0, 0);
        col->addWidget(list_wrap, 1);

        rebuildKeyboardList({});
        return create_oobe_split_page(
            QStringLiteral("keyboard"),
            QStringLiteral("How do you type?"),
            QStringLiteral("%1 world layouts available. Applied live.").arg(world_kb_layouts_.size()),
            host);
    }

    void rebuildKeyboardList(const QString& filter) {
        if (!kb_prog_list_) return;
        auto* lay = kb_prog_list_->listLayout();
        while (QLayoutItem* it = lay->takeAt(0)) { if (QWidget* w = it->widget()) w->deleteLater(); delete it; }
        kb_rows_.clear();

        const QString q = filter.trimmed();
        int shown = 0;
        for (int i = 0; i < static_cast<int>(world_kb_layouts_.size()); ++i) {
            const auto& kb = world_kb_layouts_[static_cast<std::size_t>(i)];
            if (!q.isEmpty() &&
                !kb.display_name.contains(q, Qt::CaseInsensitive) &&
                !kb.xkb_layout.contains(q, Qt::CaseInsensitive) &&
                !kb.code_badge.contains(q, Qt::CaseInsensitive)) {
                continue;
            }
            auto* row = new OobeOptionRow(kb.display_name, kb.code_badge,
                                          i == kb_idx_, kb_prog_list_->hostWidget());
            connect(row, &OobeOptionRow::clicked, this, [this, i]() { applyKeyboardLayout(i); });
            kb_rows_.emplace_back(i, row);
            lay->addWidget(row);
            ++shown;
        }
        lay->addStretch(1);
        if (kb_blur_ov_) { kb_blur_ov_->setVisible(shown > 6); kb_blur_ov_->invalidateCache(); }
    }

    // ── Page 10: Network / Wi-Fi (Exact OOBE Split Layout) ───────────────────
    QWidget* buildNetworkPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        host->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        host->setAttribute(Qt::WA_TranslucentBackground, true);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(8);

        auto* list_wrap = new QWidget(host);
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        net_prog_list_ = new OobeProgressiveList(list_wrap);
        net_blur_ov_ = new ProgressiveBlurOverlay(net_prog_list_, list_wrap);
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(net_prog_list_, 0, 0);
        stack_lay->addWidget(net_blur_ov_, 0, 0);
        col->addWidget(list_wrap, 1);

        auto* bar = new QHBoxLayout();
        bar->setSpacing(10);
        auto* rescan = new QuietLink(QStringLiteral("Rescan Wi-Fi"), false, host);
        connect(rescan, &QAbstractButton::clicked, this, &RecoveryWindow::refreshNetwork);
        net_status_lbl_ = new QLabel(host);
        net_status_lbl_->setFont(oobe_font(12, QFont::Normal));
        net_status_lbl_->setWordWrap(true);
        bar->addWidget(rescan);
        bar->addWidget(net_status_lbl_, 1);
        col->addLayout(bar);

        return create_oobe_split_page(QStringLiteral("wifi-02"),
                                      QStringLiteral("Where do we connect?"),
                                      QStringLiteral("Optional — FinnOS installs completely offline."),
                                      host);
    }

    QString promptWifiPassphrase(const QString& ssid) {
        const auto& t = FinnThemeManager::instance().tokens();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Wi-Fi Password"));
        dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dlg.setModal(true);

        auto* root = new QVBoxLayout(&dlg);
        root->setContentsMargins(22, 20, 22, 20);
        root->setSpacing(12);

        auto* title = new QLabel(QStringLiteral("Connect to %1").arg(ssid), &dlg);
        title->setFont(oobe_font(17, QFont::Medium));
        root->addWidget(title);

        auto* field = new OobeInputField(QStringLiteral("Wi-Fi Password"), true, &dlg);
        root->addWidget(field);

        auto* btn_row = new QHBoxLayout();
        btn_row->setSpacing(10);
        btn_row->addStretch(1);
        auto* cancel_btn = new QuietLink(QStringLiteral("Cancel"), false, &dlg);
        auto* join_btn = new QuietLink(QStringLiteral("Connect"), false, &dlg);
        btn_row->addWidget(cancel_btn);
        btn_row->addWidget(join_btn);
        root->addLayout(btn_row);

        QPalette dpal = dlg.palette();
        dpal.setColor(QPalette::Window, t.tile);
        dpal.setColor(QPalette::WindowText, t.ink);
        dlg.setPalette(dpal);
        dlg.setAutoFillBackground(true);

        connect(cancel_btn, &QAbstractButton::clicked, &dlg, &QDialog::reject);
        connect(join_btn, &QAbstractButton::clicked, &dlg, &QDialog::accept);
        connect(field->lineEdit(), &QLineEdit::returnPressed, &dlg, &QDialog::accept);
        field->lineEdit()->setFocus();

        return (dlg.exec() == QDialog::Accepted) ? field->text() : QString();
    }

    void connectWifiNetwork(const QString& ssid, bool secured) {
        QString pwd;
        if (secured) {
            pwd = promptWifiPassphrase(ssid);
            if (pwd.isEmpty()) return;
        }
        if (net_status_lbl_) net_status_lbl_->setText(QStringLiteral("Connecting to %1...").arg(ssid));
        auto* proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        QStringList args = {QStringLiteral("dev"), QStringLiteral("wifi"),
                            QStringLiteral("connect"), ssid};
        if (!pwd.isEmpty()) args << QStringLiteral("password") << pwd;
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc, ssid](int code, QProcess::ExitStatus st) {
                    const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        if (net_status_lbl_) net_status_lbl_->setText(QStringLiteral("Connected to %1.").arg(ssid));
                        refreshNetwork();
                    } else {
                        const QString tail = out.section(QLatin1Char('\n'), -1).trimmed();
                        if (net_status_lbl_) {
                            net_status_lbl_->setText(tail.isEmpty()
                                ? QStringLiteral("Failed to connect (exit %1).").arg(code)
                                : tail);
                        }
                    }
                });
        proc->start(QStringLiteral("nmcli"), args);
    }

    void refreshNetwork() {
        if (!net_prog_list_) return;
        auto* lay = net_prog_list_->listLayout();
        while (QLayoutItem* it = lay->takeAt(0)) { if (QWidget* w = it->widget()) w->deleteLater(); delete it; }

        if (QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty()) {
            auto* row = new OobeOptionRow(QStringLiteral("Offline installation ready"),
                                          QStringLiteral("No network needed"), false,
                                          net_prog_list_->hostWidget());
            row->setEnabled(false);
            lay->addWidget(row);
            lay->addStretch(1);
            if (net_status_lbl_) net_status_lbl_->setText(QStringLiteral("NetworkManager is unavailable; offline install works without network."));
            if (net_blur_ov_) { net_blur_ov_->setVisible(false); net_blur_ov_->invalidateCache(); }
            return;
        }

        if (net_status_lbl_) net_status_lbl_->setText(QStringLiteral("Scanning networks..."));

        bool wired_ok = false;
        QString wired_name;
        {
            QProcess p;
            p.start(QStringLiteral("nmcli"),
                    {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("TYPE,STATE,CONNECTION"),
                     QStringLiteral("dev"), QStringLiteral("status")});
            if (p.waitForFinished(1500)) {
                for (const QString& ln : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'))) {
                    const QStringList cols = ln.split(QLatin1Char(':'));
                    if (cols.size() >= 3 && cols[0] == QLatin1String("ethernet") &&
                        cols[1] == QLatin1String("connected")) {
                        wired_ok = true;
                        wired_name = cols[2];
                        break;
                    }
                }
            }
        }

        auto* proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc, wired_ok, wired_name](int, QProcess::ExitStatus) {
                    const QString raw = QString::fromUtf8(proc->readAllStandardOutput());
                    proc->deleteLater();
                    if (!net_prog_list_) return;
                    auto* list_lay = net_prog_list_->listLayout();
                    while (QLayoutItem* it = list_lay->takeAt(0)) { if (QWidget* w = it->widget()) w->deleteLater(); delete it; }

                    int row_count = 0;
                    QString active_ssid;
                    if (wired_ok) {
                        auto* eth = new OobeOptionRow(QStringLiteral("Ethernet"),
                                                      wired_name.isEmpty() ? QStringLiteral("Connected") : wired_name,
                                                      true, net_prog_list_->hostWidget());
                        list_lay->addWidget(eth);
                        ++row_count;
                    }

                    QStringList seen;
                    for (const QString& ln : raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                        const QStringList cols = ln.split(QLatin1Char(':'));
                        if (cols.size() < 4) continue;
                        const bool active = (cols[0] == QLatin1String("yes"));
                        const QString ssid = cols[1].trimmed();
                        const QString sig = cols[2].trimmed();
                        const QString sec = cols[3].trimmed();
                        if (ssid.isEmpty() || seen.contains(ssid)) continue;
                        seen.append(ssid);
                        if (active) active_ssid = ssid;
                        const bool secured = !sec.isEmpty() && sec != QLatin1String("--");
                        const QString detail = active
                            ? QStringLiteral("Connected")
                            : QStringLiteral("%1%%2").arg(sig, secured ? QStringLiteral(" \u00B7 Lock") : QString());
                        auto* row = new OobeOptionRow(ssid, detail, active, net_prog_list_->hostWidget());
                        connect(row, &OobeOptionRow::clicked, this, [this, ssid, active, secured]() {
                            if (!active) connectWifiNetwork(ssid, secured);
                        });
                        list_lay->addWidget(row);
                        ++row_count;
                    }

                    if (row_count == 0) {
                        auto* empty = new OobeOptionRow(QStringLiteral("No Wi-Fi networks found"),
                                                        QStringLiteral("Offline OK"), false,
                                                        net_prog_list_->hostWidget());
                        empty->setEnabled(false);
                        list_lay->addWidget(empty);
                        ++row_count;
                    }
                    list_lay->addStretch(1);

                    if (net_link_) {
                        if (!active_ssid.isEmpty()) net_link_->setLabelText(QStringLiteral("Wi-Fi: %1").arg(active_ssid));
                        else if (wired_ok) net_link_->setLabelText(QStringLiteral("Network: Ethernet"));
                        else net_link_->setLabelText(QStringLiteral("Network"));
                        footer_links_->adjustSize();
                    }
                    if (net_status_lbl_) {
                        net_status_lbl_->setText(
                            !active_ssid.isEmpty() ? QStringLiteral("Connected to %1.").arg(active_ssid)
                            : wired_ok ? QStringLiteral("Ethernet connected.")
                            : QStringLiteral("Network is optional — FinnOS installs offline."));
                    }
                    if (net_blur_ov_) {
                        net_blur_ov_->setVisible(row_count > 6);
                        net_blur_ov_->invalidateCache();
                    }
                });
        proc->start(QStringLiteral("nmcli"),
                    {QStringLiteral("-t"), QStringLiteral("-f"),
                     QStringLiteral("ACTIVE,SSID,SIGNAL,SECURITY"),
                     QStringLiteral("dev"), QStringLiteral("wifi"),
                     QStringLiteral("list"), QStringLiteral("--rescan"), QStringLiteral("auto")});
    }

    HostEnvironment host_env_;
    std::vector<DiskInfo> disks_;
    std::vector<DetectedOs> detected_oses_;

    FinnPageStack* stack_ = nullptr;
    QPointer<OobeSlideTransitionOverlay> slide_overlay_;
    QWidget* shared_nav_box_ = nullptr;
    OobeCircleNavButton* nav_back_ = nullptr;
    OobeCircleNavButton* nav_next_ = nullptr;
    QWidget* footer_links_ = nullptr;
    QuietLink* net_link_ = nullptr;
    QuietLink* kb_link_ = nullptr;
    int kb_idx_ = 0;
    Page prev_utility_page_ = PageHub;
    RecoveryTerminalWindow* terminal_win_ = nullptr;

    std::vector<WorldKeyboardLayout> world_kb_layouts_;
    LiveXkbKeymapFilter* xkb_filter_ = nullptr;
    OobeProgressiveList* kb_prog_list_ = nullptr;
    std::vector<std::pair<int, OobeOptionRow*>> kb_rows_;
    ProgressiveBlurOverlay* kb_blur_ov_ = nullptr;

    OobeProgressiveList* net_prog_list_ = nullptr;
    ProgressiveBlurOverlay* net_blur_ov_ = nullptr;
    QLabel* net_status_lbl_ = nullptr;

    int hub_choice_ = 0;
    std::vector<OobeOptionRow*> hub_rows_;

    InstallMode selected_mode_ = InstallMode::CleanErase;
    PartitionTableKind preferred_table_ = PartitionTableKind::GPT;
    bool create_recovery_part_ = true;
    std::vector<OobeOptionRow*> mode_rows_;

    int selected_disk_idx_ = 0;
    std::string selected_partition_path_;
    int resize_gib_ = 40;
    QWidget* target_list_host_ = nullptr;
    QVBoxLayout* target_list_lay_ = nullptr;
    std::vector<OobeOptionRow*> target_rows_;
    std::vector<std::string> target_part_paths_;
    SlimPartitionBar* target_bar_ = nullptr;
    QWidget* clean_opts_host_ = nullptr;
    OobeToggleRow* mbr_toggle_row_ = nullptr;
    OobeToggleRow* recovery_part_toggle_row_ = nullptr;
    QWidget* resize_host_ = nullptr;
    QLabel* resize_label_ = nullptr;
    QSlider* resize_slider_ = nullptr;
    QLabel* target_note_lbl_ = nullptr;
    QLabel* target_sub_lbl_ = nullptr;

    InstallPlan current_plan_;
    bool destructive_confirmed_ = false;
    QLabel* review_label_ = nullptr;
    QWidget* review_confirm_host_ = nullptr;
    QVBoxLayout* review_confirm_lay_ = nullptr;

    QLabel* install_step_lbl_ = nullptr;
    QLabel* install_detail_lbl_ = nullptr;
    QLabel* install_sub_lbl_ = nullptr;
    OobeProgressBar* install_bar_ = nullptr;
    QPlainTextEdit* install_log_ = nullptr;

    QLabel* complete_icon_lbl_ = nullptr;
    QLabel* complete_title_ = nullptr;
    QLabel* complete_sub_ = nullptr;

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

    int log_source_idx_ = 0;
    QPlainTextEdit* logs_edit_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    QString initial_page;
    QString screenshot_path;
    bool open_terminal = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QLatin1String("--version")) {
            std::puts("recovery-mode 2.0.0");
            return 0;
        }
        if (arg == QLatin1String("--page") && i + 1 < argc) {
            initial_page = QString::fromUtf8(argv[++i]);
        } else if (arg == QLatin1String("--screenshot") && i + 1 < argc) {
            screenshot_path = QString::fromUtf8(argv[++i]);
        } else if (arg == QLatin1String("--open-terminal")) {
            open_terminal = true;
        }
    }

    FinnApplication app(argc, argv, QStringLiteral("recovery-mode"));
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
    if (open_terminal) {
        w.openTerminalOverlay();
    }

    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(350, &w, [&w, screenshot_path]() {
            w.grab().save(screenshot_path);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}

#include "main.moc"
