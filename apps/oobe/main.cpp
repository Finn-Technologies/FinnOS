// FinnOS First-Time Setup (finn-oobe).
// Full-screen minimalist onboarding built with FinnUI matching Figma First Time
// Setup ("hi", "How do we talk?") and the 6-ring 3D-to-slinky ribbon animation.

#include <FinnSDK/FinnSDK.hpp>

#include "common/paths.hpp"
#include "system/desktop_services.hpp"

#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using namespace finn::ui;

QFont oobe_font(int pixel_size, QFont::Weight weight = QFont::Normal) {
    QFont f(primary_font_family());
    f.setPixelSize(pixel_size);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
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

    void setRevealOpacity(qreal opacity) {
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (opacity_ <= 0.005) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(opacity_);

        const QColor ink = t.dark ? t.ink : QColor(0, 0, 0);
        FinnThemeManager::instance().paintIcon(
            &p, QRectF(0.0, 2.0, 20.0, 20.0), QStringLiteral("boot-mark"), ink);

        p.setFont(oobe_font(20, QFont::Normal));
        p.setPen(ink);
        p.drawText(QRectF(35.0, 0.0, width() - 35.0, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FinnOS"));
    }

private:
    qreal opacity_ = 1.0;
};

// Circular 56x56 chevron navigation button matching Figma 36:48 / 36:66 / 36:54.
class OobeCircleNavButton final : public QAbstractButton {
    Q_OBJECT

public:
    OobeCircleNavButton(bool is_back, bool filled_by_default,
                        QWidget* parent = nullptr)
        : QAbstractButton(parent),
          is_back_(is_back),
          filled_default_(filled_by_default) {
        setFixedSize(56, 56);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setRevealOpacity(qreal opacity) {
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        setAttribute(Qt::WA_TransparentForMouseEvents, opacity_ < 0.25);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (opacity_ <= 0.005) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(opacity_);

        const QRectF circle_r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool en = isEnabled();

        QColor fill = Qt::transparent;
        if (filled_default_) {
            fill = t.dark ? QColor(0x26, 0x2B, 0x38) : QColor(0xFF, 0xFF, 0xFF);
            if (isDown()) {
                fill = t.dark ? QColor(0x30, 0x36, 0x46) : QColor(0xEB, 0xEB, 0xEB);
            } else if (hovered_) {
                fill = t.dark ? QColor(0x2C, 0x32, 0x42) : QColor(0xFF, 0xFF, 0xFF);
            }
        } else if (en) {
            if (isDown()) {
                fill = t.dark ? QColor(255, 255, 255, 34) : QColor(255, 255, 255, 235);
            } else if (hovered_) {
                fill = t.dark ? QColor(255, 255, 255, 22) : QColor(255, 255, 255, 195);
            }
        }

        if (fill.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawEllipse(circle_r);
        }

        // Exact chevron vector from Figma 36:46 (36x36 box inside 56x56 at offset 10,10):
        // M13 29 C13 29 24 20.8987 24 18 C24 15.1011 13 7 13 7 (stroke-width 2.75)
        QColor stroke_col = t.dark ? t.ink : QColor(0, 0, 0);
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
        update();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }

private:
    bool is_back_ = false;
    bool filled_default_ = false;
    bool hovered_ = false;
    qreal opacity_ = 1.0;
};

// Pill button matching the video's "Get started" action pill on the final screen.
class OobeGetStartedPill final : public QAbstractButton {
    Q_OBJECT

public:
    explicit OobeGetStartedPill(const QString& label, QWidget* parent = nullptr)
        : QAbstractButton(parent) {
        setText(label);
        setFixedSize(142, 48);
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
        QColor bg = t.dark ? QColor(0x26, 0x2B, 0x38) : QColor(0xFF, 0xFF, 0xFF);
        if (isDown()) {
            bg = t.dark ? QColor(0x32, 0x38, 0x48) : QColor(0xEC, 0xEE, 0xF2);
        } else if (hovered_) {
            bg = t.dark ? QColor(0x2D, 0x33, 0x43) : QColor(0xFA, 0xFB, 0xFC);
        }
        p.setBrush(bg);
        p.setPen(QPen(t.dark ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 12), 1.0));
        p.drawRoundedRect(r, 24.0, 24.0);

        p.setFont(oobe_font(15, QFont::Medium));
        p.setPen(t.dark ? t.ink : QColor(0x18, 0x18, 0x20));
        p.drawText(rect(), Qt::AlignCenter, text());
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }

private:
    bool hovered_ = false;
};

// Selectable 350x37 row matching Figma 37:98 ("Language" item in "How do we talk?").
// Selected row draws a solid white #FFFFFF pill (cornerRadius 10); unselected is transparent.
class OobeOptionRow final : public QWidget {
    Q_OBJECT

public:
    OobeOptionRow(const QString& label, const QString& detail = {},
                  bool selected = false, QWidget* parent = nullptr)
        : QWidget(parent),
          label_(label),
          detail_(detail),
          selected_(selected) {
        setFixedSize(350, 37);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setSelected(bool selected) {
        if (selected_ == selected) return;
        selected_ = selected;
        update();
    }

    bool isSelected() const { return selected_; }
    QString label() const { return label_; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r(0.0, 0.0, width(), height());
        QColor bg = Qt::transparent;
        if (selected_) {
            bg = t.dark ? QColor(0x28, 0x2E, 0x3D) : QColor(0xFF, 0xFF, 0xFF);
        } else if (pressed_) {
            bg = t.dark ? QColor(255, 255, 255, 26) : QColor(255, 255, 255, 160);
        } else if (hovered_) {
            bg = t.dark ? QColor(255, 255, 255, 14) : QColor(255, 255, 255, 110);
        }

        if (bg.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 10.0, 10.0);
        }

        const QColor ink = t.dark ? t.ink : QColor(0, 0, 0);
        p.setFont(oobe_font(18, QFont::Normal));
        p.setPen(ink);
        const QRectF text_r(8.0, 0.0, width() - 16.0, height());
        p.drawText(text_r, Qt::AlignLeft | Qt::AlignVCenter, label_);

        if (!detail_.isEmpty()) {
            p.setFont(oobe_font(13, QFont::Normal));
            p.setPen(t.dim);
            p.drawText(text_r, Qt::AlignRight | Qt::AlignVCenter, detail_);
        }
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        pressed_ = false;
        update();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            update();
        } else {
            QWidget::mousePressEvent(event);
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            update();
            if (rect().contains(event->pos())) emit clicked();
        } else {
            QWidget::mouseReleaseEvent(event);
        }
    }

private:
    QString label_;
    QString detail_;
    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
};

// Minimalist 350px text input row matching Figma 37:98 pill geometry.
class OobeInputField final : public QWidget {
    Q_OBJECT

public:
    OobeInputField(const QString& placeholder, bool password = false,
                   QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(350, 42);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 4, 12, 4);
        lay->setSpacing(8);

        edit_ = new QLineEdit(this);
        edit_->setFrame(false);
        edit_->setPlaceholderText(placeholder);
        edit_->setFont(oobe_font(17, QFont::Normal));
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
        const QColor bg = t.dark ? QColor(0x28, 0x2E, 0x3D) : QColor(0xFF, 0xFF, 0xFF);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(QRectF(0, 0, width(), height()), 10.0, 10.0);
    }

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = edit_->palette();
        pal.setColor(QPalette::Text, t.dark ? t.ink : QColor(0, 0, 0));
        pal.setColor(QPalette::PlaceholderText, t.mute);
        edit_->setPalette(pal);
        update();
    }

    QLineEdit* edit_ = nullptr;
};

// Minimalist 350px toggle row matching Figma 37:98 typography and spacing.
class OobeToggleRow final : public QWidget {
    Q_OBJECT

public:
    OobeToggleRow(const QString& label, bool initial_checked,
                  QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(350, 39);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->setSpacing(10);

        label_ = new QLabel(label, this);
        label_->setFont(oobe_font(18, QFont::Normal));
        lay->addWidget(label_, 1);

        toggle_ = new FinnToggle(initial_checked, this);
        lay->addWidget(toggle_);

        connect(toggle_, &QAbstractButton::toggled, this,
                &OobeToggleRow::toggled);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &OobeToggleRow::syncColors);
        syncColors();
    }

    FinnToggle* toggle() const { return toggle_; }

signals:
    void toggled(bool checked);

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = label_->palette();
        pal.setColor(QPalette::WindowText, t.dark ? t.ink : QColor(0, 0, 0));
        label_->setPalette(pal);
        update();
    }

    QLabel* label_ = nullptr;
    FinnToggle* toggle_ = nullptr;
};

// Progressive bottom blur overlay matching Figma 37:96 (LAYER_BLUR PROGRESSIVE,
// startOffset y=0.607 to 1.0, radius 20 -> CSS blur bands 2.15px..8.60px).
QImage box_blur_image(const QImage& src, int radius) {
    if (radius <= 0 || src.isNull()) return src;
    QImage a = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage b(a.size(), QImage::Format_ARGB32_Premultiplied);
    const int w = a.width();
    const int h = a.height();
    const int d = 2 * radius + 1;

    for (int pass = 0; pass < 2; ++pass) {
        // Horizontal pass
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
        // Vertical pass
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

class OobeProgressiveList final : public QWidget {
    Q_OBJECT

public:
    explicit OobeProgressiveList(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedWidth(356);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        scroll_ = new QScrollArea(this);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->viewport()->setAutoFillBackground(false);
        scroll_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

        host_ = new QWidget(scroll_);
        host_->setAutoFillBackground(false);
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

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        scroll_->setGeometry(rect());
    }

    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        return QWidget::eventFilter(watched, event);
    }

private:
    QScrollArea* scroll_ = nullptr;
    QWidget* host_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
};

// Overlay widget placed over the bottom of OobeProgressiveList to apply the
// progressive blur & fade from y=60.7% to 100% without blocking mouse events.
class ProgressiveBlurOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit ProgressiveBlurOverlay(QWidget* target_list, QWidget* parent = nullptr)
        : QWidget(parent), target_(target_list) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!target_ || height() < 40 || in_capture_) return;
        const auto& t = FinnThemeManager::instance().tokens();
        const QColor bg = t.dark ? QColor(0x16, 0x19, 0x22) : QColor(0xF5, 0xF5, 0xF5);

        const int blur_top = qRound(height() * 0.607);
        const int blur_h = height() - blur_top;
        if (blur_h <= 8) return;

        in_capture_ = true;
        QImage snap(width(), blur_h, QImage::Format_ARGB32_Premultiplied);
        snap.fill(bg);
        target_->render(&snap, QPoint(0, 0), QRegion(0, blur_top, width(), blur_h));
        in_capture_ = false;

        const QImage b1 = box_blur_image(snap, 2);
        const QImage b2 = box_blur_image(b1, 3);
        const QImage b3 = box_blur_image(b2, 4);

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

            // Feather top entry of the blur region so there is no hard horizontal step.
            QLinearGradient mask(0, 0, 0, feather);
            mask.setColorAt(0.0, QColor(0, 0, 0, 0));
            mask.setColorAt(1.0, QColor(0, 0, 0, 255));
            cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            cp.fillRect(QRect(0, 0, width(), feather), mask);
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawImage(0, blur_top, comp);

        // Smooth progressive surface fade at the bottom edge
        QLinearGradient fade(0, blur_top, 0, height());
        QColor c0 = bg;
        c0.setAlpha(0);
        QColor c1 = bg;
        c1.setAlpha(45);
        QColor c2 = bg;
        c2.setAlpha(185);
        fade.setColorAt(0.0, c0);
        fade.setColorAt(0.45, c1);
        fade.setColorAt(1.0, c2);
        p.fillRect(QRect(0, blur_top, width(), blur_h), fade);
    }

private:
    QWidget* target_ = nullptr;
    bool in_capture_ = false;
};

class OobeWindow final : public FinnLayerWindow {
    Q_OBJECT

public:
    explicit OobeWindow(bool update_mode = false, double fixed_phase = -1.0)
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-oobe")),
          update_mode_(update_mode),
          fixed_phase_(fixed_phase) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::Exclusive);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        stack_ = new QStackedWidget(this);
        install_stack_animator(stack_);
        stack_->addWidget(buildHiPage());
        stack_->addWidget(buildLanguagePage());
        stack_->addWidget(buildWifiPage());
        stack_->addWidget(buildAccountPage());
        stack_->addWidget(buildAppearancePage());
        stack_->addWidget(buildCompletePage());
        root->addWidget(stack_, 1);

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        connect(&finn::sys::FinnDesktopServices::instance(),
                &finn::sys::FinnDesktopServices::stateUpdated,
                this, &OobeWindow::syncNetwork);
        finn::sys::FinnDesktopServices::instance().refreshAll();

        setStep(update_mode_ ? 5 : 0);
    }

    void openStep(int idx) {
        if (idx >= 0 && idx < stack_->count()) setStep(idx);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        // Exact #F5F5F5 canvas from Figma First Time Setup ("hi" & "How do we talk?")
        const QColor bg = t.dark ? QColor(0x16, 0x19, 0x22) : QColor(0xF5, 0xF5, 0xF5);
        p.fillRect(rect(), bg);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            advanceStep();
            return;
        }
        FinnLayerWindow::keyPressEvent(event);
    }

private:
    // Helper that builds the Figma 36:53 split two-column layout:
    //   - Left prompt ("How do we talk?") at x=225..320, y=408 (24px Fixel Display Regular)
    //   - Right 350px column at x=805, y=195
    //   - Centered Back/Next 56x56 circular chevron buttons at x=574, y=631 (itemSpacing 20)
    QWidget* createSplitStepPage(const QString& prompt_text,
                                 const QString& sub_caption,
                                 QWidget* right_column,
                                 QLabel** out_sub_label = nullptr) {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* main_col = new QVBoxLayout(page);
        main_col->setContentsMargins(80, 0, 80, 0);
        main_col->setSpacing(0);

        auto* content_row = new QHBoxLayout();
        content_row->setContentsMargins(0, 0, 0, 0);
        content_row->setSpacing(32);

        // Left column: conversational question centered vertically around y=408
        auto* left_host = new QWidget(page);
        auto* left_col = new QVBoxLayout(left_host);
        left_col->setContentsMargins(80, 0, 20, 0);
        left_col->setSpacing(10);
        left_col->addStretch(49);

        auto* prompt_lbl = new QLabel(prompt_text, left_host);
        prompt_lbl->setFont(oobe_font(24, QFont::Normal));
        prompt_lbl->setAlignment(Qt::AlignCenter);
        left_col->addWidget(prompt_lbl, 0, Qt::AlignHCenter);

        QLabel* sub_lbl = nullptr;
        if (!sub_caption.isEmpty() || out_sub_label != nullptr) {
            sub_lbl = new QLabel(sub_caption, left_host);
            sub_lbl->setFont(oobe_font(14, QFont::Normal));
            sub_lbl->setAlignment(Qt::AlignCenter);
            sub_lbl->setWordWrap(true);
            sub_lbl->setFixedWidth(280);
            sub_lbl->setVisible(!sub_caption.isEmpty());
            left_col->addWidget(sub_lbl, 0, Qt::AlignHCenter);
            if (out_sub_label) *out_sub_label = sub_lbl;
        }
        left_col->addStretch(51);

        auto sync_left_colors = [prompt_lbl, sub_lbl]() {
            const auto& t = FinnThemeManager::instance().tokens();
            QPalette p1 = prompt_lbl->palette();
            p1.setColor(QPalette::WindowText, t.dark ? t.ink : QColor(0, 0, 0));
            prompt_lbl->setPalette(p1);
            if (sub_lbl) {
                QPalette p2 = sub_lbl->palette();
                p2.setColor(QPalette::WindowText, t.dim);
                sub_lbl->setPalette(p2);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_left_colors);
        sync_left_colors();

        // Right column positioned at Figma's x=805, y=195 (top margin ~23.4% of 832)
        auto* right_host = new QWidget(page);
        auto* right_col = new QVBoxLayout(right_host);
        right_col->setContentsMargins(0, 175, 45, 0);
        right_col->setSpacing(0);
        right_col->addWidget(right_column, 1);

        content_row->addWidget(left_host, 1);

        // Center Back/Next navigation bar pinned near y=631 (bottom ~145px)
        auto* center_nav_col = new QVBoxLayout();
        center_nav_col->setContentsMargins(0, 0, 0, 130);
        center_nav_col->addStretch(1);

        auto* nav_box = new QWidget(page);
        nav_box->setFixedSize(132, 56);
        auto* nav_row = new QHBoxLayout(nav_box);
        nav_row->setContentsMargins(0, 0, 0, 0);
        nav_row->setSpacing(20);

        auto* back_btn = new OobeCircleNavButton(true, false, nav_box);
        auto* next_btn = new OobeCircleNavButton(false, false, nav_box);
        connect(back_btn, &QAbstractButton::clicked, this, &OobeWindow::previousStep);
        connect(next_btn, &QAbstractButton::clicked, this, &OobeWindow::advanceStep);
        nav_row->addWidget(back_btn);
        nav_row->addWidget(next_btn);
        center_nav_col->addWidget(nav_box, 0, Qt::AlignHCenter);

        content_row->addLayout(center_nav_col);
        content_row->addWidget(right_host, 1, Qt::AlignRight);

        main_col->addLayout(content_row, 1);
        return page;
    }

    // Step 0: Figma 36:3 ("hi") — continuous rounding arcs that draw into "hi.",
    // and only after that reveal the top Branding (36:45) and Next circle button (36:48)
    // in the exact Figma positions.
    QWidget* buildHiPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(0);

        hi_ribbon_ = new FinnRibbonAnimation(FinnRibbonAnimation::Mode::HiIntro, page);
        if (fixed_phase_ >= 0.0) hi_ribbon_->setPhaseTime(fixed_phase_);
        grid->addWidget(hi_ribbon_, 0, 0);

        auto* chrome_layer = new QWidget(page);
        chrome_layer->setAttribute(Qt::WA_NoSystemBackground, true);
        auto* col = new QVBoxLayout(chrome_layer);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);

        // Top stretch so Branding sits at y=180/832 (~21.6% from top)
        col->addStretch(216);

        hi_branding_ = new OobeBrandingHeader(chrome_layer);
        col->addWidget(hi_branding_, 0, Qt::AlignHCenter);

        // Middle stretch across the center where "hi." is drawn at y=50%
        col->addStretch(518);

        hi_next_btn_ = new OobeCircleNavButton(false, true, chrome_layer);
        connect(hi_next_btn_, &QAbstractButton::clicked, this, &OobeWindow::advanceStep);
        col->addWidget(hi_next_btn_, 0, Qt::AlignHCenter);

        // Bottom stretch so Next button sits at y=631/832 (~75.8% from top)
        col->addStretch(174);

        grid->addWidget(chrome_layer, 0, 0);

        auto sync_reveal = [this]() {
            if (!hi_ribbon_) return;
            const qreal rev = hi_ribbon_->uiRevealProgress();
            if (hi_branding_) hi_branding_->setRevealOpacity(rev);
            if (hi_next_btn_) hi_next_btn_->setRevealOpacity(rev);
        };
        connect(hi_ribbon_, &FinnRibbonAnimation::frameAdvanced, page,
                [sync_reveal](double) { sync_reveal(); });
        sync_reveal();

        return page;
    }

    // Step 1: Figma 36:53 ("How do we talk?") with exact 14 languages + progressive blur
    QWidget* buildLanguagePage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(356);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        auto* prog_list = new OobeProgressiveList(list_wrap);
        auto* blur_ov = new ProgressiveBlurOverlay(prog_list, list_wrap);

        // Keep prog_list and blur_ov stacked inside list_wrap
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(prog_list, 0, 0);
        stack_lay->addWidget(blur_ov, 0, 0);

        const QStringList languages = {
            QStringLiteral("English (UK)"),
            QStringLiteral("Spanish"),
            QStringLiteral("Italian"),
            QStringLiteral("French"),
            QStringLiteral("Catal\u00E0"),
            QStringLiteral("German"),
            QStringLiteral("Africaans"),
            QStringLiteral("Chinese (Simplified)"),
            QStringLiteral("Hindi"),
            QStringLiteral("Korean"),
            QStringLiteral("Japanese"),
            QStringLiteral("Arabic"),
            QStringLiteral("Chinese (Traditional)"),
            QStringLiteral("Portuguese"),
        };

        language_rows_.clear();
        for (int i = 0; i < languages.size(); ++i) {
            auto* row = new OobeOptionRow(languages[i], {}, i == 0,
                                          prog_list->hostWidget());
            language_rows_.push_back(row);
            connect(row, &OobeOptionRow::clicked, this, [this, i, blur_ov]() {
                for (int j = 0; j < language_rows_.size(); ++j) {
                    language_rows_[j]->setSelected(j == i);
                }
                blur_ov->update();
            });
            prog_list->listLayout()->addWidget(row);
        }
        prog_list->listLayout()->addStretch(1);

        return createSplitStepPage(QStringLiteral("How do we talk?"), {}, list_wrap);
    }

    // Step 2: Wi-Fi / Network in the same Figma 36:53 split layout
    QWidget* buildWifiPage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(356);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        wifi_prog_list_ = new OobeProgressiveList(list_wrap);
        wifi_blur_ov_ = new ProgressiveBlurOverlay(wifi_prog_list_, list_wrap);

        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(wifi_prog_list_, 0, 0);
        stack_lay->addWidget(wifi_blur_ov_, 0, 0);

        return createSplitStepPage(QStringLiteral("Where do we connect?"),
                                   QStringLiteral("Scanning nearby networks..."),
                                   list_wrap, &net_status_lbl_);
    }

    // Step 3: Account setup in the same Figma 36:53 split layout
    QWidget* buildAccountPage() {
        auto* col_host = new QWidget();
        col_host->setFixedWidth(356);
        auto* col = new QVBoxLayout(col_host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(12);

        fullname_edit_ = new OobeInputField(QStringLiteral("Full Name"), false, col_host);
        username_edit_ = new OobeInputField(QStringLiteral("Account Name"), false, col_host);
        username_edit_->setText(QString::fromLocal8Bit(qgetenv("USER")));
        pw_edit_ = new OobeInputField(QStringLiteral("Password"), true, col_host);
        pw_verify_edit_ = new OobeInputField(QStringLiteral("Verify Password"), true, col_host);

        col->addWidget(fullname_edit_);
        col->addWidget(username_edit_);
        col->addWidget(pw_edit_);
        col->addWidget(pw_verify_edit_);
        col->addStretch(1);

        return createSplitStepPage(QStringLiteral("What should we call you?"), {},
                                   col_host);
    }

    // Step 4: Appearance & Accessibility in the same Figma 36:53 split layout
    QWidget* buildAppearancePage() {
        auto* col_host = new QWidget();
        col_host->setFixedWidth(356);
        auto* col = new QVBoxLayout(col_host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(10);

        auto& tm = FinnThemeManager::instance();
        const auto s = finn::sys::load_merged_settings();

        light_row_ = new OobeOptionRow(QStringLiteral("Light Appearance"), {},
                                       !tm.isDark(), col_host);
        dark_row_ = new OobeOptionRow(QStringLiteral("Dark Appearance"), {},
                                      tm.isDark(), col_host);

        auto sync_appearance = [this]() {
            const bool dark = FinnThemeManager::instance().isDark();
            if (light_row_) light_row_->setSelected(!dark);
            if (dark_row_) dark_row_->setSelected(dark);
        };
        connect(light_row_, &OobeOptionRow::clicked, this, [sync_appearance]() {
            FinnThemeManager::instance().setDark(false);
            sync_appearance();
        });
        connect(dark_row_, &OobeOptionRow::clicked, this, [sync_appearance]() {
            FinnThemeManager::instance().setDark(true);
            sync_appearance();
        });

        col->addWidget(light_row_);
        col->addWidget(dark_row_);
        col->addSpacing(10);

        auto* sr = new OobeToggleRow(QStringLiteral("Screen Reader"),
                                     s.screen_reader, col_host);
        connect(sr, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setScreenReader(on);
        });
        col->addWidget(sr);

        auto* hc = new OobeToggleRow(QStringLiteral("High Contrast"),
                                     tm.tokens().high_contrast, col_host);
        connect(hc, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setHighContrast(on);
        });
        col->addWidget(hc);

        auto* rm = new OobeToggleRow(QStringLiteral("Reduce Motion"),
                                     tm.tokens().reduced_motion, col_host);
        connect(rm, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setReducedMotion(on);
        });
        col->addWidget(rm);

        auto* sk = new OobeToggleRow(QStringLiteral("Sticky Keys"),
                                     s.sticky_keys, col_host);
        connect(sk, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setStickyKeys(on);
        });
        col->addWidget(sk);

        connect(&tm, &FinnThemeManager::themeChanged, this,
                [sync_appearance, sr, hc, rm, sk]() {
                    sync_appearance();
                    const auto& st = FinnThemeManager::instance().settings();
                    QSignalBlocker b1(sr->toggle());
                    QSignalBlocker b2(hc->toggle());
                    QSignalBlocker b3(rm->toggle());
                    QSignalBlocker b4(sk->toggle());
                    sr->toggle()->setChecked(st.screen_reader);
                    hc->toggle()->setChecked(st.high_contrast);
                    rm->toggle()->setChecked(st.reduced_motion);
                    sk->toggle()->setChecked(st.sticky_keys);
                });

        col->addStretch(1);
        return createSplitStepPage(QStringLiteral("How should it look?"), {},
                                   col_host);
    }

    // Step 5: Video-matched Completion / Update Successful Screen with 6-Ring Slinky
    QWidget* buildCompletePage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);

        col->addStretch(8);

        auto* branding = new OobeBrandingHeader(page);
        col->addWidget(branding, 0, Qt::AlignHCenter);

        col->addSpacing(12);

        complete_ribbon_ = new FinnRibbonAnimation(
            FinnRibbonAnimation::Mode::SlinkyWave, page);
        complete_ribbon_->setFixedHeight(310);
        if (fixed_phase_ >= 0.0) complete_ribbon_->setPhaseTime(fixed_phase_);
        col->addWidget(complete_ribbon_);

        col->addSpacing(20);

        complete_title_ = new QLabel(
            update_mode_ ? QStringLiteral("Update successful")
                         : QStringLiteral("You\u2019re all set"),
            page);
        complete_title_->setFont(oobe_font(26, QFont::Medium));
        complete_title_->setAlignment(Qt::AlignCenter);
        col->addWidget(complete_title_, 0, Qt::AlignHCenter);

        auto sync_title_col = [this]() {
            if (!complete_title_) return;
            const auto& t = FinnThemeManager::instance().tokens();
            QPalette pal = complete_title_->palette();
            pal.setColor(QPalette::WindowText,
                         t.dark ? t.ink : QColor(0x18, 0x18, 0x20));
            complete_title_->setPalette(pal);
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_title_col);
        sync_title_col();

        col->addSpacing(28);

        auto* start_btn =
            new OobeGetStartedPill(QStringLiteral("Get started"), page);
        connect(start_btn, &QAbstractButton::clicked, this,
                &OobeWindow::finishSetup);
        col->addWidget(start_btn, 0, Qt::AlignHCenter);

        col->addStretch(14);
        return page;
    }

    void syncNetwork() {
        if (!wifi_prog_list_) return;
        const auto& net = finn::sys::FinnDesktopServices::instance().network();
        if (net_status_lbl_) {
            if (net.wired_connected) {
                net_status_lbl_->setText(
                    QStringLiteral("Ethernet connected (%1)").arg(net.ipv4_summary));
            } else if (!net.active_wifi_ssid.isEmpty()) {
                net_status_lbl_->setText(
                    QStringLiteral("Connected to %1").arg(net.active_wifi_ssid));
            } else {
                net_status_lbl_->setText(
                    net.nm_running
                        ? QStringLiteral("Choose a network or continue offline")
                        : QStringLiteral("NetworkManager unavailable"));
            }
            net_status_lbl_->setVisible(true);
        }

        auto* lay = wifi_prog_list_->listLayout();
        while (QLayoutItem* item = lay->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }

        if (net.wired_connected) {
            auto* eth_row = new OobeOptionRow(
                QStringLiteral("Ethernet"), net.ipv4_summary, true,
                wifi_prog_list_->hostWidget());
            lay->addWidget(eth_row);
        }

        for (const auto& w : net.networks) {
            const QString detail =
                w.active ? QStringLiteral("Connected")
                         : QStringLiteral("%1%").arg(w.signal);
            auto* row = new OobeOptionRow(w.ssid, detail, w.active,
                                          wifi_prog_list_->hostWidget());
            connect(row, &OobeOptionRow::clicked, this, [this, w]() {
                if (w.active) return;
                QString pwd;
                if (w.secured) {
                    pwd = FinnDialog::prompt(
                        this, QStringLiteral("Wi-Fi Password"),
                        QStringLiteral("Password for %1:").arg(w.ssid));
                    if (pwd.isEmpty()) return;
                }
                finn::sys::FinnDesktopServices::instance().connectWifi(w.ssid, pwd);
            });
            lay->addWidget(row);
        }

        auto* offline_row = new OobeOptionRow(
            QStringLiteral("Continue offline"), {},
            !net.wired_connected && net.active_wifi_ssid.isEmpty(),
            wifi_prog_list_->hostWidget());
        connect(offline_row, &OobeOptionRow::clicked, this,
                &OobeWindow::advanceStep);
        lay->addWidget(offline_row);
        lay->addStretch(1);

        if (wifi_blur_ov_) {
            const int row_count = (net.wired_connected ? 1 : 0) +
                                  static_cast<int>(net.networks.size()) + 1;
            wifi_blur_ov_->setVisible(row_count > 6);
            wifi_blur_ov_->update();
        }
    }

    void previousStep() {
        const int cur = stack_->currentIndex();
        if (cur > 0) setStep(cur - 1);
    }

    void advanceStep() {
        const int cur = stack_->currentIndex();
        if (cur + 1 < stack_->count()) {
            setStep(cur + 1);
        } else {
            finishSetup();
        }
    }

    void setStep(int idx) {
        stack_->setCurrentIndex(idx);
        if (idx == 0 && hi_ribbon_) {
            if (fixed_phase_ < 0.0) hi_ribbon_->restart();
            const qreal rev = hi_ribbon_->uiRevealProgress();
            if (hi_branding_) hi_branding_->setRevealOpacity(rev);
            if (hi_next_btn_) hi_next_btn_->setRevealOpacity(rev);
        } else if (idx == 5 && complete_ribbon_ && fixed_phase_ < 0.0) {
            complete_ribbon_->restart();
        }
    }

    void finishSetup() {
        const QString marker =
            QDir::homePath() + QStringLiteral("/.config/finn/oobe_complete");
        QDir().mkpath(QFileInfo(marker).absolutePath());
        QFile f(marker);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("done\n");
        }
        close();
    }

    bool update_mode_ = false;
    double fixed_phase_ = -1.0;
    QStackedWidget* stack_ = nullptr;
    FinnRibbonAnimation* hi_ribbon_ = nullptr;
    OobeBrandingHeader* hi_branding_ = nullptr;
    OobeCircleNavButton* hi_next_btn_ = nullptr;
    FinnRibbonAnimation* complete_ribbon_ = nullptr;
    QLabel* complete_title_ = nullptr;

    QVector<OobeOptionRow*> language_rows_;
    OobeProgressiveList* wifi_prog_list_ = nullptr;
    ProgressiveBlurOverlay* wifi_blur_ov_ = nullptr;
    QLabel* net_status_lbl_ = nullptr;

    OobeInputField* fullname_edit_ = nullptr;
    OobeInputField* username_edit_ = nullptr;
    OobeInputField* pw_edit_ = nullptr;
    OobeInputField* pw_verify_edit_ = nullptr;
    OobeOptionRow* light_row_ = nullptr;
    OobeOptionRow* dark_row_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    int initial_step = -1;
    bool update_mode = false;
    double fixed_phase = -1.0;

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QLatin1String("--step") && i + 1 < argc) {
            bool ok = false;
            const int parsed = QString::fromUtf8(argv[++i]).toInt(&ok);
            if (ok) initial_step = parsed;
        } else if (arg == QLatin1String("--phase") && i + 1 < argc) {
            bool ok = false;
            const double parsed = QString::fromUtf8(argv[++i]).toDouble(&ok);
            if (ok) fixed_phase = parsed;
        } else if (arg == QLatin1String("--update") ||
                   arg == QLatin1String("--update-complete") ||
                   arg == QLatin1String("--boot")) {
            update_mode = true;
        }
    }

    finn::ui::FinnApplication app(argc, argv, QStringLiteral("oobe"));
    OobeWindow w(update_mode, fixed_phase);
    if (initial_step >= 0) w.openStep(initial_step);
    w.show();
    return app.exec();
}

#include "main.moc"
