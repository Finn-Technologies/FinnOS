// Qt composition for the phone / tablet shell.  All geometry and motion values
// come from mobile_shell_layout.hpp (tokens + Figma measurements); this file
// only builds real finn::ui widgets and drives them with the existing spring
// solver.  No Q_OBJECT is declared here: callbacks are plain std::function,
// so these classes need no moc step.

#include "shell/mobile_shell_layout.hpp"

#include "shell/shell_layout.hpp"
#include "ui/finn/components.hpp"
#include "ui/finn/theme.hpp"
#include "ui/finn/tokens.hpp"

#include <QAbstractButton>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QtGlobal>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace finn::shell::mobile {
namespace {

using finn::shell::ShellSpringDriver;
using finn::ui::FinnButton;
using finn::ui::FinnDivider;
using finn::ui::FinnLabel;
using finn::ui::FinnNavRail;
using finn::ui::FinnProgressBar;
using finn::ui::FinnSurface;
using finn::ui::FinnThemeManager;
using finn::ui::finn_font_px;

// The system navigation handle and the bottom-sheet drag handle are the same
// shape at two sizes; both use the ink token, never a hardcoded grey.
class HandleBar final : public QWidget {
public:
    HandleBar(int width, int height, QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(width, height);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QColor ink = t.ink;
        ink.setAlpha(t.dark ? 150 : 110);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0),
                          height() * 0.5, height() * 0.5);
    }
};

// Old/new step snapshots cross-fade and travel under a token spring.  The
// surface_open token (280 ms, out_back) is used forward, surface_close
// (180 ms, in_out) on back.  Reduced motion is handled by ShellSpringDriver,
// which settles the spring instantly.
class StepTransition final : public QWidget {
public:
    StepTransition(QWidget* parent, QPixmap old_frame, QPixmap new_frame,
                   bool forward, const finn::ui::SpringParams& params)
        : QWidget(parent),
          old_(std::move(old_frame)),
          new_(std::move(new_frame)),
          forward_(forward) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (parent) setGeometry(parent->rect());
        driver_ = std::make_unique<ShellSpringDriver>(
            this, params, 0.0,
            [this](double value) {
                progress_ = std::clamp(value, 0.0, 1.0);
                update();
            },
            [this]() { deleteLater(); });
        driver_->retarget(1.0);
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const qreal dir = forward_ ? 1.0 : -1.0;
        const qreal travel = std::min<qreal>(140.0, width() * 0.12);
        if (progress_ < 1.0 && !old_.isNull()) {
            p.setOpacity(1.0 - progress_);
            p.drawPixmap(QPointF(-dir * travel * progress_, 0.0), old_);
        }
        p.setOpacity(progress_);
        if (!new_.isNull()) {
            p.drawPixmap(QPointF(dir * travel * (1.0 - progress_), 0.0), new_);
        }
        p.setOpacity(1.0);
    }

private:
    QPixmap old_;
    QPixmap new_;
    bool forward_ = true;
    double progress_ = 0.0;
    std::unique_ptr<ShellSpringDriver> driver_;
};

// Modal bottom sheet: scrim opacity uses scrim_in (200 ms, out), the sheet's
// upward travel uses surface_open (280 ms, out_back).  Closing swaps to the
// matching close tokens instead of running the entrance backwards at the same
// speed — a surface takes longer to arrive than to leave.
class SheetOverlay final : public QWidget {
public:
    SheetOverlay(QWidget* parent, QWidget* content, int sheet_height,
                 const LayoutMetrics& metrics)
        : QWidget(parent), content_(content), metrics_(metrics) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (parent) setGeometry(parent->rect());
        close_scrim_params_ = motion_spring(MotionToken::ScrimOut);
        close_sheet_params_ = motion_spring(MotionToken::SurfaceClose);
        sheet_height_ = sheet_height > 0 ? sheet_height : height() / 2;

        if (content_) content_->setParent(this);
        handle_ = new HandleBar(36, 4, this);
        handle_->raise();

        scrim_driver_ = std::make_unique<ShellSpringDriver>(
            this, motion_spring(MotionToken::ScrimIn), 0.0,
            [this](double value) {
                scrim_alpha_ = std::clamp(value, 0.0, 1.0);
                update();
            },
            [this]() { finish_if_closed(); });
        sheet_driver_ = std::make_unique<ShellSpringDriver>(
            this, motion_spring(MotionToken::SurfaceOpen), 0.0,
            [this](double value) {
                sheet_progress_ = std::clamp(value, 0.0, 1.0);
                place_content();
                update();
            },
            [this]() { finish_if_closed(); });
        scrim_driver_->retarget(1.0);
        sheet_driver_->retarget(1.0);
        place_content();
        show();
        raise();
    }

    void close() {
        if (closing_) return;
        closing_ = true;
        scrim_driver_->retarget(0.0, &close_scrim_params_);
        sheet_driver_->retarget(0.0, &close_sheet_params_);
        finish_if_closed();  // reduced motion settles on retarget
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (parentWidget() && size() != parentWidget()->size()) {
            setGeometry(parentWidget()->rect());
        }
        place_content();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (!closing_ && !sheet_visual_rect_.contains(event->pos())) close();
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Scrim: a real dim, not a fake glass panel.  Its opacity is measured
        // by the spring so a tap-out during the entrance still dismisses.
        QColor scrim(0, 0, 0, qRound(120.0 * scrim_alpha_));
        p.setPen(Qt::NoPen);
        p.setBrush(scrim);
        p.fillRect(rect(), scrim);

        const int h = std::min(sheet_height_, height());
        QRectF sheet(0.0, static_cast<qreal>(height() - h),
                     static_cast<qreal>(width()), static_cast<qreal>(h));
        sheet.translate(0.0, (1.0 - sheet_progress_) * h);
        QPainterPath path = finn_rounded_path(
            sheet, static_cast<qreal>(metrics_.sheet_top_radius));
        p.setBrush(t.tile);
        p.setPen(QPen(t.hair, 1.0));
        p.drawPath(path);
    }

private:
    void place_content() {
        const int h = std::min(sheet_height_, height());
        const int y = sheet_progress_ >= 1.0
                          ? height() - h
                          : height() - h + qRound((1.0 - sheet_progress_) * h);
        sheet_visual_rect_ = QRect(0, y, width(), h);
        if (handle_) {
            handle_->move((width() - handle_->width()) / 2, y + finn::ui::spacing::sp_2);
        }
        if (content_) {
            const int inset = finn::ui::spacing::sp_4;
            const int handle_space = finn::ui::spacing::sp_6 + finn::ui::spacing::sp_2;
            content_->setGeometry(sheet_visual_rect_.adjusted(
                inset, handle_space, -inset, -inset));
        }
    }

    void finish_if_closed() {
        if (!closing_) return;
        if ((scrim_driver_ && scrim_driver_->running()) ||
            (sheet_driver_ && sheet_driver_->running())) {
            return;
        }
        hide();
        deleteLater();
    }

    QPointer<QWidget> content_;
    HandleBar* handle_ = nullptr;
    QRect sheet_visual_rect_;
    int sheet_height_ = 0;
    double scrim_alpha_ = 0.0;
    double sheet_progress_ = 0.0;
    bool closing_ = false;
    finn::ui::SpringParams close_scrim_params_;
    finn::ui::SpringParams close_sheet_params_;
    std::unique_ptr<ShellSpringDriver> scrim_driver_;
    std::unique_ptr<ShellSpringDriver> sheet_driver_;
    LayoutMetrics metrics_;
};

class MobileShellHostImpl final : public QWidget, public MobileShellHostApi {
public:
    explicit MobileShellHostImpl(QWidget* parent = nullptr) : QWidget(parent) {
        setAutoFillBackground(false);
        metrics_ = compute_layout(width(), height());

        background_ = new FinnSurface(FinnSurface::Role::Canvas, 0.0, false, this);
        background_->lower();

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        header_ = new QWidget(this);
        auto* header_layout = new QHBoxLayout(header_);
        header_layout->setContentsMargins(finn::ui::spacing::sp_6,
                                          finn::ui::spacing::sp_4,
                                          finn::ui::spacing::sp_6,
                                          finn::ui::spacing::sp_2);
        title_ = new FinnLabel(QStringLiteral("hi."), FinnLabel::Role::TitleLarge,
                               FinnLabel::Tone::Ink, header_);
        counter_ = new FinnLabel(QStringLiteral("1 / 8"), FinnLabel::Role::Caption,
                                 FinnLabel::Tone::Dim, header_);
        progress_ = new FinnProgressBar(5, header_);
        header_layout->addWidget(title_, 1);
        header_layout->addWidget(counter_);
        header_layout->addWidget(progress_);
        root->addWidget(header_);

        body_ = new QWidget(this);
        body_layout_ = new QHBoxLayout(body_);
        body_layout_->setContentsMargins(0, 0, 0, 0);
        body_layout_->setSpacing(0);
        rail_ = new FinnNavRail(metrics_.nav_rail_width, body_);
        rail_divider_ = new FinnDivider(Qt::Vertical, body_);
        body_layout_->addWidget(rail_);
        body_layout_->addWidget(rail_divider_);
        scroll_ = new QScrollArea(body_);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll_->viewport()->setAutoFillBackground(false);
        content_host_ = new QWidget(scroll_);
        content_layout_ = new QVBoxLayout(content_host_);
        scroll_->setWidget(content_host_);
        body_layout_->addWidget(scroll_, 1);
        root->addWidget(body_, 1);

        nav_ = new QWidget(this);
        auto* nav_layout = new QHBoxLayout(nav_);
        nav_layout->setContentsMargins(finn::ui::spacing::sp_4, 0,
                                       finn::ui::spacing::sp_4, 0);
        back_ = new FinnButton(QStringLiteral("Back"), FinnButton::Variant::Secondary,
                               QStringLiteral("chevron-left"), nav_);
        next_ = new FinnButton(QStringLiteral("Next"), FinnButton::Variant::Primary,
                               QStringLiteral("chevron-right"), nav_);
        back_->setAccessibleName(QStringLiteral("Back"));
        next_->setAccessibleName(QStringLiteral("Next"));
        nav_layout->addStretch(1);
        nav_layout->addWidget(back_);
        nav_layout->addSpacing(metrics_.nav_cluster_gap);
        nav_layout->addWidget(next_);
        nav_layout->addStretch(1);
        root->addWidget(nav_);

        gesture_ = new HandleBar(metrics_.gesture_pill_width,
                                 metrics_.gesture_pill_height, this);
        gesture_->raise();

        QObject::connect(back_, &QAbstractButton::clicked, this,
                         [this]() { if (on_back_) on_back_(); });
        QObject::connect(next_, &QAbstractButton::clicked, this,
                         [this]() { if (on_next_) on_next_(); });
        QObject::connect(rail_, &FinnNavRail::selected, this,
                         [this](const QString& id) {
                             if (on_rail_) on_rail_(id.toStdString());
                         });
        QObject::connect(&FinnThemeManager::instance(),
                         &FinnThemeManager::themeChanged, this,
                         [this]() { apply_metrics(); update(); });
        apply_metrics();
    }

    // MobileShellHostApi -------------------------------------------------
    void setStepTitle(const std::string& title) override {
        title_->setText(QString::fromStdString(title));
    }

    void setStepContent(QWidget* content) override {
        if (!content) return;
        if (content_widget_ && content_widget_ != content) {
            content_layout_->removeWidget(content_widget_);
            delete content_widget_;
        }
        content_widget_ = content;
        content_layout_->addWidget(content_widget_);
    }

    void animateStepContent(QWidget* content, bool forward) override {
        if (!content) return;
        const QPixmap old_frame = scroll_->grab();
        setStepContent(content);
        scroll_->layout()->activate();
        const QPixmap new_frame = scroll_->grab();
        const MotionToken token = forward ? MotionToken::SurfaceOpen
                                          : MotionToken::SurfaceClose;
        auto* transition = new StepTransition(this, old_frame, new_frame, forward,
                                              motion_spring(token));
        transition->setGeometry(body_->geometry());
        transition->raise();
        transitions_.push_back(transition);
    }

    void setStepProgress(int current, int total) override {
        const int safe_total = std::max(1, total);
        const int safe_current = std::clamp(current, 0, safe_total);
        counter_->setText(QStringLiteral("%1 / %2")
                              .arg(safe_current + 1)
                              .arg(safe_total));
        progress_->setValue(static_cast<double>(safe_current + 1) /
                            static_cast<double>(safe_total));
    }

    void setNavigationEnabled(bool back, bool next) override {
        back_->setEnabled(back);
        next_->setEnabled(next);
    }

    void setNavigationCallbacks(std::function<void()> on_back,
                                std::function<void()> on_next) override {
        on_back_ = std::move(on_back);
        on_next_ = std::move(on_next);
    }

    void setRailSteps(const std::vector<std::string>& ids,
                      const std::vector<std::string>& labels,
                      const std::string& current_id) override {
        if (rail_) {
            body_layout_->removeWidget(rail_);
            delete rail_;
        }
        rail_ = new FinnNavRail(metrics_.nav_rail_width, body_);
        const std::size_t count = std::min(ids.size(), labels.size());
        for (std::size_t i = 0; i < count; ++i) {
            rail_->addItem(QString::fromStdString(ids[i]),
                           QStringLiteral("settings"),
                           QString::fromStdString(labels[i]));
        }
        rail_->setCurrentId(QString::fromStdString(current_id));
        body_layout_->insertWidget(0, rail_);
        QObject::connect(rail_, &FinnNavRail::selected, this,
                         [this](const QString& id) {
                             if (on_rail_) on_rail_(id.toStdString());
                         });
        apply_metrics();
    }

    void setRailCallback(std::function<void(const std::string&)> on_select) override {
        on_rail_ = std::move(on_select);
    }

    void showBottomSheet(QWidget* sheet_content, int sheet_height_dp) override {
        if (!sheet_content) return;
        closeBottomSheet();
        sheet_ = new SheetOverlay(this, sheet_content, sheet_height_dp, metrics_);
    }

    void closeBottomSheet() override {
        if (sheet_) sheet_->close();
    }

    LayoutMetrics metrics() const override { return metrics_; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        apply_metrics();
    }

private:
    void apply_metrics() {
        metrics_ = compute_layout(width(), height());
        background_->setGeometry(rect());
        background_->lower();

        content_layout_->setContentsMargins(
            metrics_.page_margin, metrics_.section_gap,
            metrics_.page_margin, metrics_.section_gap);
        content_layout_->setSpacing(metrics_.list_gap);

        const bool tablet = metrics_.layout == LayoutClass::Tablet;
        rail_->setVisible(tablet);
        rail_divider_->setVisible(tablet);
        if (tablet) {
            rail_->setFixedWidth(metrics_.nav_rail_width);
            scroll_->setMinimumWidth(metrics_.content_min_width);
        } else {
            scroll_->setMinimumWidth(0);
        }

        back_->setFixedHeight(metrics_.primary_height);
        next_->setFixedHeight(metrics_.primary_height);
        const int nav_height = metrics_.nav_bar_height > 0
                                   ? metrics_.nav_bar_height
                                   : metrics_.primary_height + 2 * metrics_.page_margin;
        nav_->setFixedHeight(nav_height);

        const bool phone = metrics_.layout == LayoutClass::Phone;
        gesture_->setVisible(phone);
        if (phone) {
            gesture_->setFixedSize(metrics_.gesture_pill_width,
                                   metrics_.gesture_pill_height);
            gesture_->move((width() - metrics_.gesture_pill_width) / 2,
                           height() - metrics_.gesture_pill_height -
                               metrics_.gesture_pill_bottom);
            gesture_->raise();
        }
        title_->setFont(finn_font_px(metrics_.prompt_px));
        counter_->setFont(finn_font_px(finn::ui::spacing::sp_3 * 4));
    }

    FinnSurface* background_ = nullptr;
    QWidget* header_ = nullptr;
    FinnLabel* title_ = nullptr;
    FinnLabel* counter_ = nullptr;
    FinnProgressBar* progress_ = nullptr;
    QWidget* body_ = nullptr;
    QHBoxLayout* body_layout_ = nullptr;
    FinnNavRail* rail_ = nullptr;
    FinnDivider* rail_divider_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* content_host_ = nullptr;
    QVBoxLayout* content_layout_ = nullptr;
    QWidget* nav_ = nullptr;
    FinnButton* back_ = nullptr;
    FinnButton* next_ = nullptr;
    HandleBar* gesture_ = nullptr;
    QPointer<QWidget> content_widget_;
    QPointer<SheetOverlay> sheet_;
    std::vector<QPointer<QWidget>> transitions_;
    std::function<void()> on_back_;
    std::function<void()> on_next_;
    std::function<void(const std::string&)> on_rail_;
    LayoutMetrics metrics_;
};

}  // namespace

MobileShellHostApi* create_mobile_shell_host(QWidget* parent) {
    return new MobileShellHostImpl(parent);
}

}  // namespace finn::shell::mobile
