// Implements FinnWindowControls, FinnWindow, and FinnLayerWindow.

#include "ui/finn/window.hpp"

#include <LayerShellQt/Window>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>

#include "ui/finn/theme.hpp"

namespace finn::ui {
namespace {

constexpr int kShadowMargin = 10;
constexpr int kResizeBorder = 5;

}  // namespace

// ---- FinnWindowControls ----

FinnWindowControls::FinnWindowControls(QWidget* parent) : QWidget(parent) {
    setFixedSize(88, 28);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(10);

    max_btn_ = new FinnIconButton(QStringLiteral("expand"), 20, 14, this);
    max_btn_->setFixedSize(20, 20);
    max_btn_->setCornerRadius(4.0);
    max_btn_->setToolTip(QStringLiteral("Maximize"));

    min_btn_ = new FinnIconButton(QStringLiteral("minus-sign"), 20, 14, this);
    min_btn_->setFixedSize(20, 20);
    min_btn_->setCornerRadius(4.0);
    min_btn_->setToolTip(QStringLiteral("Minimize"));

    close_btn_ = new FinnIconButton(QStringLiteral("x"), 20, 13, this);
    close_btn_->setFixedSize(20, 20);
    close_btn_->setCornerRadius(4.0);
    close_btn_->setCustomHoverColor(QColor(0xFF, 0x02, 0x02), Qt::white);
    close_btn_->setToolTip(QStringLiteral("Close"));

    lay->addWidget(max_btn_);
    lay->addWidget(min_btn_);
    lay->addWidget(close_btn_);

    connect(min_btn_, &QAbstractButton::clicked, this,
            &FinnWindowControls::minimizeRequested);
    connect(max_btn_, &QAbstractButton::clicked, this,
            &FinnWindowControls::maximizeRequested);
    connect(close_btn_, &QAbstractButton::clicked, this,
            &FinnWindowControls::closeRequested);
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, qOverload<>(&QWidget::update));
}

void FinnWindowControls::setMaximizedState(bool maximized) {
    max_btn_->setToolTip(maximized ? QStringLiteral("Restore")
                                   : QStringLiteral("Maximize"));
}

void FinnWindowControls::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(t.glass);
    p.setPen(QPen(t.hair, 1.0));
    p.drawRoundedRect(r, kRadiusMd, kRadiusMd);
}

// ---- FinnWindow ----

FinnWindow::FinnWindow(const QString& title, const QString& icon_name,
                       QWidget* parent)
    : QWidget(parent), icon_name_(icon_name) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    if (!title.isEmpty()) setWindowTitle(title);

    outer_layout_ = new QVBoxLayout(this);
    outer_layout_->setContentsMargins(kShadowMargin, kShadowMargin,
                                      kShadowMargin, kShadowMargin);
    outer_layout_->setSpacing(0);

    frame_root_ = new QWidget(this);
    auto* root_col = new QVBoxLayout(frame_root_);
    root_col->setContentsMargins(0, 0, 0, 0);
    root_col->setSpacing(0);

    // Header bar (48px)
    title_bar_ = new QWidget(frame_root_);
    title_bar_->setFixedHeight(48);
    auto* tb = new QHBoxLayout(title_bar_);
    tb->setContentsMargins(14, 8, 12, 8);
    tb->setSpacing(8);

    sidebar_toggle_btn_ =
        new FinnIconButton(QStringLiteral("sidebar-toggle"), 28, 16, title_bar_);
    sidebar_toggle_btn_->setVisible(false);
    sidebar_toggle_btn_->setToolTip(QStringLiteral("Toggle Sidebar"));
    connect(sidebar_toggle_btn_, &QAbstractButton::clicked, this,
            [this]() { setSidebarCollapsed(!sidebar_collapsed_); });
    tb->addWidget(sidebar_toggle_btn_);

    title_icon_ = new QLabel(title_bar_);
    title_icon_->setFixedSize(20, 20);
    title_icon_->setVisible(!icon_name_.isEmpty());
    tb->addWidget(title_icon_);

    title_label_ = new FinnLabel(title, FinnLabel::Role::Headline,
                                 FinnLabel::Tone::Ink, title_bar_);
    tb->addWidget(title_label_);

    header_slot_ = new QHBoxLayout();
    header_slot_->setContentsMargins(6, 0, 6, 0);
    header_slot_->setSpacing(6);
    tb->addLayout(header_slot_, 1);

    controls_ = new FinnWindowControls(title_bar_);
    connect(controls_, &FinnWindowControls::minimizeRequested, this,
            &QWidget::showMinimized);
    connect(controls_, &FinnWindowControls::maximizeRequested, this,
            &FinnWindow::toggleMaximized);
    connect(controls_, &FinnWindowControls::closeRequested, this,
            &QWidget::close);
    tb->addWidget(controls_, 0, Qt::AlignVCenter);

    root_col->addWidget(title_bar_);
    title_divider_ = new FinnDivider(Qt::Horizontal, frame_root_);
    root_col->addWidget(title_divider_);

    // Split body (optional collapsible sidebar + content slot)
    body_split_ = new QWidget(frame_root_);
    body_layout_ = new QHBoxLayout(body_split_);
    body_layout_->setContentsMargins(0, 0, 0, 0);
    body_layout_->setSpacing(0);

    sidebar_container_ = new QWidget(body_split_);
    sidebar_slot_ = new QVBoxLayout(sidebar_container_);
    sidebar_slot_->setContentsMargins(0, 0, 0, 0);
    sidebar_slot_->setSpacing(0);
    sidebar_container_->setVisible(false);

    sidebar_divider_ = new FinnDivider(Qt::Vertical, body_split_);
    sidebar_divider_->setVisible(false);

    content_container_ = new QWidget(body_split_);
    content_slot_ = new QVBoxLayout(content_container_);
    content_slot_->setContentsMargins(0, 0, 0, 0);
    content_slot_->setSpacing(0);

    body_layout_->addWidget(sidebar_container_);
    body_layout_->addWidget(sidebar_divider_);
    body_layout_->addWidget(content_container_, 1);
    root_col->addWidget(body_split_, 1);

    outer_layout_->addWidget(frame_root_);

    toast_ = new FinnToast(frame_root_);

    auto refresh_icon = [this]() {
        if (!icon_name_.isEmpty()) {
            title_icon_->setPixmap(FinnThemeManager::instance().iconPixmap(
                icon_name_, QSize(18, 18), QColor(), devicePixelRatioF()));
        }
        update();
    };
    refresh_icon();
    connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
            this, refresh_icon);

    sidebar_spring_.init(this, springs::kSmooth, 250.0, [this](double v) {
        if (!sidebar_container_) return;
        const int w = std::max(0, qRound(v));
        if (!sidebar_collapsed_ && sidebar_spring_.animator.settled()) {
            sidebar_container_->setMaximumWidth(QWIDGETSIZE_MAX);
        } else {
            sidebar_container_->setMaximumWidth(w);
            if (sidebar_collapsed_ && w == 0 && sidebar_spring_.animator.settled()) {
                sidebar_container_->setVisible(false);
                if (sidebar_divider_) sidebar_divider_->setVisible(false);
            }
        }
        update();
    });

    entrance_spring_.init(this, springs::kSmooth, 1.0, [this](double v) {
        setWindowOpacity(std::clamp(v, 0.0, 1.0));
    });

    updateFrameMask();
}

void FinnWindow::setChromeMode(ChromeMode mode) {
    chrome_mode_ = mode;
    const bool split = (mode == ChromeMode::FigmaSplit);
    title_bar_->setVisible(!split);
    if (title_divider_) title_divider_->setVisible(!split);
    if (split) {
        controls_->setParent(frame_root_);
        controls_->show();
        controls_->raise();
        controls_->move(std::max(0, frame_root_->width() - controls_->width() - 6), 6);
    } else {
        controls_->setParent(title_bar_);
        if (auto* tb = qobject_cast<QHBoxLayout*>(title_bar_->layout())) {
            tb->addWidget(controls_, 0, Qt::AlignVCenter);
        }
        controls_->show();
    }
    update();
}

void FinnWindow::setWindowTitleText(const QString& title) {
    setWindowTitle(title);
    title_label_->setText(title);
}

void FinnWindow::setHeaderWidget(QWidget* widget) {
    if (header_widget_) {
        header_slot_->removeWidget(header_widget_);
        header_widget_->deleteLater();
    }
    header_widget_ = widget;
    if (header_widget_) {
        header_widget_->setParent(title_bar_);
        header_slot_->addWidget(header_widget_, 1);
    }
}

void FinnWindow::setSidebarWidget(QWidget* widget) {
    if (sidebar_widget_) {
        sidebar_slot_->removeWidget(sidebar_widget_);
        sidebar_widget_->deleteLater();
    }
    sidebar_widget_ = widget;
    const bool has_sidebar = sidebar_widget_ != nullptr;
    sidebar_toggle_btn_->setVisible(has_sidebar);
    if (sidebar_widget_) {
        sidebar_widget_->setParent(sidebar_container_);
        sidebar_slot_->addWidget(sidebar_widget_, 1);
        sidebar_expanded_width_ =
            std::max(180, sidebar_widget_->sizeHint().width());
    }
    const bool show = has_sidebar && !sidebar_collapsed_;
    sidebar_container_->setMaximumWidth(show ? QWIDGETSIZE_MAX : 0);
    sidebar_spring_.animator.set(show ? sidebar_expanded_width_ : 0.0, 0.0);
    sidebar_container_->setVisible(show);
    sidebar_divider_->setVisible(show);
}

void FinnWindow::setContentWidget(QWidget* widget) {
    if (content_widget_) {
        content_slot_->removeWidget(content_widget_);
        content_widget_->deleteLater();
    }
    content_widget_ = widget;
    if (content_widget_) {
        content_widget_->setParent(content_container_);
        content_slot_->addWidget(content_widget_, 1);
    }
}

void FinnWindow::setSidebarCollapsed(bool collapsed) {
    if (sidebar_collapsed_ == collapsed) return;
    sidebar_collapsed_ = collapsed;
    if (!sidebar_widget_) {
        sidebar_container_->setVisible(false);
        sidebar_divider_->setVisible(false);
        return;
    }
    if (!collapsed) {
        sidebar_expanded_width_ =
            std::max(180, sidebar_widget_->sizeHint().width());
        if (!sidebar_container_->isVisible()) {
            sidebar_container_->setMaximumWidth(0);
            sidebar_spring_.animator.set(0.0, 0.0);
            sidebar_container_->setVisible(true);
            sidebar_divider_->setVisible(true);
        }
        sidebar_spring_.retarget(static_cast<double>(sidebar_expanded_width_),
                                 springs::kSmooth);
    } else {
        const int cur_w = std::max(0, sidebar_container_->width());
        if (cur_w > 0) {
            sidebar_expanded_width_ = cur_w;
            if (!sidebar_spring_.active()) {
                sidebar_spring_.animator.set(static_cast<double>(cur_w), 0.0);
            }
        }
        sidebar_spring_.retarget(0.0, springs::kSmooth);
    }
}

void FinnWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (shown_once_) return;
    shown_once_ = true;
    entrance_spring_.animator.set(0.88, 0.0);
    entrance_spring_.retargetOrCrossfade(0.6, 1.0, springs::kSmooth);
}

void FinnWindow::showToast(const QString& message, FinnToast::Severity severity,
                           int duration_ms) {
    toast_->showMessage(message, severity, duration_ms);
    positionToast();
}

void FinnWindow::toggleMaximized() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void FinnWindow::updateMarginsForWindowState() {
    const int m = (isMaximized() || isFullScreen()) ? 0 : kShadowMargin;
    outer_layout_->setContentsMargins(m, m, m, m);
    controls_->setMaximizedState(isMaximized() || isFullScreen());
    updateFrameMask();
    update();
}

// The frame paints its own rounded corners, but the widgets inside it (scroll
// viewports, text edits, image canvases) paint square backgrounds over them.
// Clipping the frame to the same rounded region keeps all four corners round
// for every app, whichever widget is in the content slot.
void FinnWindow::updateFrameMask() {
    if (!frame_root_) return;
    const QRect r = frame_root_->rect();
    if (r.isEmpty()) return;
    const int d = 2 * kRadiusXl;
    QRegion mask(QRect(r.left() + kRadiusXl, r.top(),
                       r.width() - d, r.height()));
    mask |= QRegion(QRect(r.left(), r.top() + kRadiusXl,
                          r.width(), r.height() - d));
    mask |= QRegion(QRect(r.left(), r.top(), d, d), QRegion::Ellipse);
    mask |= QRegion(QRect(r.right() - d + 1, r.top(), d, d), QRegion::Ellipse);
    mask |= QRegion(QRect(r.left(), r.bottom() - d + 1, d, d), QRegion::Ellipse);
    mask |= QRegion(QRect(r.right() - d + 1, r.bottom() - d + 1, d, d),
                    QRegion::Ellipse);
    frame_root_->setMask(mask);
}

void FinnWindow::positionToast() {
    if (!toast_ || !toast_->isVisible()) return;
    toast_->repositionInParent();
}

Qt::Edges FinnWindow::hitTestResizeEdges(const QPoint& pos) const {
    if (isMaximized() || isFullScreen()) return {};
    if (controls_ && controls_->isVisible() &&
        controls_->geometry().adjusted(-4, -4, 4, 4).contains(
            frame_root_->mapFrom(this, pos))) {
        return {};
    }
    const QRect frame_r = frame_root_->geometry();
    Qt::Edges edges;
    if (pos.x() <= frame_r.left() + kResizeBorder) edges |= Qt::LeftEdge;
    if (pos.x() >= frame_r.right() - kResizeBorder) edges |= Qt::RightEdge;
    if (pos.y() <= frame_r.top() + kResizeBorder) edges |= Qt::TopEdge;
    if (pos.y() >= frame_r.bottom() - kResizeBorder) edges |= Qt::BottomEdge;
    return edges;
}

bool FinnWindow::event(QEvent* event) {
    if (event->type() == QEvent::HoverMove) {
        const auto* he = static_cast<QHoverEvent*>(event);
        const Qt::Edges edges = hitTestResizeEdges(he->position().toPoint());
        if (edges == (Qt::TopEdge | Qt::LeftEdge) ||
            edges == (Qt::BottomEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edges == (Qt::TopEdge | Qt::RightEdge) ||
                   edges == (Qt::BottomEdge | Qt::LeftEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            unsetCursor();
        }
    }
    return QWidget::event(event);
}

void FinnWindow::mousePressEvent(QMouseEvent* event) {
    drag_candidate_ = false;
    if (event->button() == Qt::LeftButton && windowHandle()) {
        const Qt::Edges edges = hitTestResizeEdges(event->pos());
        if (edges != Qt::Edges()) {
            windowHandle()->startSystemResize(edges);
            return;
        }
        const QPoint in_frame = frame_root_->mapFrom(this, event->pos());
        const bool in_controls =
            controls_ && controls_->isVisible() &&
            controls_->geometry().adjusted(-4, -4, 4, 4).contains(
                controls_->parentWidget()
                    ? controls_->parentWidget()->mapFrom(this, event->pos())
                    : in_frame);
        if (!in_controls) {
            if (chrome_mode_ == ChromeMode::FigmaSplit) {
                if (in_frame.y() <= 40 && !controls_->geometry().contains(in_frame)) {
                    QWidget* hit = childAt(event->pos());
                    if (!hit || hit == frame_root_ || hit == sidebar_container_ ||
                        hit == content_container_ || hit == sidebar_widget_ ||
                        hit == content_widget_ || qobject_cast<FinnLabel*>(hit)) {
                        drag_candidate_ = true;
                        drag_start_pos_ = event->pos();
                    }
                }
            } else if (title_bar_->geometry().contains(in_frame)) {
                QWidget* hit = childAt(event->pos());
                if (!hit || hit == title_bar_ || hit == title_label_ || hit == title_icon_) {
                    drag_candidate_ = true;
                    drag_start_pos_ = event->pos();
                }
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void FinnWindow::mouseMoveEvent(QMouseEvent* event) {
    if (drag_candidate_ && (event->buttons() & Qt::LeftButton) &&
        (event->pos() - drag_start_pos_).manhattanLength() >= 5) {
        drag_candidate_ = false;
        if (QWindow* wh = windowHandle()) {
            wh->startSystemMove();
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

void FinnWindow::mouseReleaseEvent(QMouseEvent* event) {
    drag_candidate_ = false;
    QWidget::mouseReleaseEvent(event);
}

void FinnWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    drag_candidate_ = false;
    if (event->button() == Qt::LeftButton) {
        const QPoint in_frame = frame_root_->mapFrom(this, event->pos());
        if (chrome_mode_ == ChromeMode::FigmaSplit) {
            if (in_frame.y() <= 40 &&
                !(controls_ && controls_->geometry().adjusted(-4, -4, 4, 4).contains(in_frame))) {
                QWidget* hit = childAt(event->pos());
                if (!hit || hit == frame_root_ || hit == sidebar_container_ ||
                    hit == content_container_ || hit == sidebar_widget_ ||
                    hit == content_widget_ || qobject_cast<FinnLabel*>(hit)) {
                    toggleMaximized();
                    return;
                }
            }
        } else if (title_bar_->geometry().contains(in_frame)) {
            QWidget* hit = childAt(event->pos());
            if (!hit || hit == title_bar_ || hit == title_label_ || hit == title_icon_) {
                toggleMaximized();
                return;
            }
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void FinnWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateFrameMask();
    if (chrome_mode_ == ChromeMode::FigmaSplit && controls_) {
        controls_->move(std::max(0, frame_root_->width() - controls_->width() - 6), 6);
        controls_->raise();
    }
    positionToast();
}

void FinnWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        updateMarginsForWindowState();
    } else if (event->type() == QEvent::DevicePixelRatioChange &&
               !icon_name_.isEmpty()) {
        title_icon_->setPixmap(FinnThemeManager::instance().iconPixmap(
            icon_name_, QSize(18, 18), QColor(), devicePixelRatioF()));
    }
    QWidget::changeEvent(event);
}

void FinnWindow::paintEvent(QPaintEvent*) {
    const auto& t = FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF fr = QRectF(frame_root_->geometry());
    const qreal rad = (isMaximized() || isFullScreen()) ? 0.0 : kRadiusXl;

    if (!(isMaximized() || isFullScreen())) {
        // Soft multi-stop ambient shadow with gentle downward lift so windows float.
        for (int i = 6; i >= 1; --i) {
            const int alpha = t.dark ? (10 + i * 3) : (3 + i * 2);
            QColor sc(0, 0, 0, alpha);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(sc, i * 1.8));
            p.drawRoundedRect(fr.adjusted(-i * 0.4, i * 0.6, i * 0.4, i * 1.4),
                               rad + i * 0.8, rad + i * 0.8);
        }
    }

    QPainterPath clip;
    clip.addRoundedRect(fr, rad, rad);
    p.save();
    p.setClipPath(clip);
    p.fillRect(fr, t.bone);
    // Faint distinct surface tint for the left sidebar when open.
    if (sidebar_container_->isVisible() && sidebar_container_->width() > 0) {
        QRectF sb = fr;
        sb.setWidth(sidebar_container_->width());
        sb.setTop(title_bar_->isVisible()
                      ? (title_bar_->geometry().bottom() + 1)
                      : fr.top());
        p.fillRect(sb, t.surface_canvas);
    }
    p.restore();

    if (!(isMaximized() || isFullScreen())) {
        // Subtle top-inner specular rim + outer hairline border.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, t.dark ? 18 : 190), 1.0));
        p.drawRoundedRect(fr.adjusted(1.5, 1.5, -1.5, -1.5),
                          std::max(1.0, rad - 1.0), std::max(1.0, rad - 1.0));
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(fr.adjusted(0.5, 0.5, -0.5, -0.5), rad, rad);
    }
}

// ---- FinnLayerWindow ----

FinnLayerWindow::FinnLayerWindow(Layer layer, const QString& scope,
                                 QWidget* parent)
    : QWidget(parent), layer_(layer), scope_(scope) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, true);
    winId();
    applyLayerConfig();
}

void FinnLayerWindow::applyLayerConfig() {
    if (!windowHandle() ||
        !QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                  Qt::CaseInsensitive)) {
        return;
    }
    layer_win_ = LayerShellQt::Window::get(windowHandle());
    if (!layer_win_) return;

    layer_win_->setScope(scope_);
    switch (layer_) {
        case Layer::Background:
            layer_win_->setLayer(LayerShellQt::Window::LayerBackground);
            break;
        case Layer::Bottom:
            layer_win_->setLayer(LayerShellQt::Window::LayerBottom);
            break;
        case Layer::Top:
            layer_win_->setLayer(LayerShellQt::Window::LayerTop);
            break;
        case Layer::Overlay:
            layer_win_->setLayer(LayerShellQt::Window::LayerOverlay);
            break;
    }

    LayerShellQt::Window::Anchors la = LayerShellQt::Window::AnchorNone;
    if (anchors_ & AnchorTop) la |= LayerShellQt::Window::AnchorTop;
    if (anchors_ & AnchorBottom) la |= LayerShellQt::Window::AnchorBottom;
    if (anchors_ & AnchorLeft) la |= LayerShellQt::Window::AnchorLeft;
    if (anchors_ & AnchorRight) la |= LayerShellQt::Window::AnchorRight;
    layer_win_->setAnchors(la);

    switch (keyboard_mode_) {
        case KeyboardMode::None:
            layer_win_->setKeyboardInteractivity(
                LayerShellQt::Window::KeyboardInteractivityNone);
            break;
        case KeyboardMode::Exclusive:
            layer_win_->setKeyboardInteractivity(
                LayerShellQt::Window::KeyboardInteractivityExclusive);
            break;
        case KeyboardMode::OnDemand:
            layer_win_->setKeyboardInteractivity(
                LayerShellQt::Window::KeyboardInteractivityOnDemand);
            break;
    }

    layer_win_->setExclusiveZone(exclusive_zone_);
    layer_win_->setMargins(margins_);
    if (desired_size_.isValid()) layer_win_->setDesiredSize(desired_size_);
}

void FinnLayerWindow::setLayer(Layer layer) {
    layer_ = layer;
    applyLayerConfig();
}

void FinnLayerWindow::setAnchors(Anchors anchors) {
    anchors_ = anchors;
    applyLayerConfig();
}

void FinnLayerWindow::setKeyboardMode(KeyboardMode mode) {
    keyboard_mode_ = mode;
    applyLayerConfig();
}

void FinnLayerWindow::setExclusiveZone(int zone) {
    exclusive_zone_ = zone;
    applyLayerConfig();
}

void FinnLayerWindow::setLayerMargins(const QMargins& margins) {
    margins_ = margins;
    applyLayerConfig();
}

void FinnLayerWindow::setDesiredSize(const QSize& size) {
    desired_size_ = size;
    if (size.isValid()) resize(size);
    applyLayerConfig();
}

void FinnLayerWindow::setInputRegions(const QVector<QRect>& rects) {
    if (rects.isEmpty()) {
        // Explicit 1x1 off-region empty mask prevents eating clicks over overlays.
        setMask(QRegion(0, 0, 1, 1));
        return;
    }
    QRegion region;
    for (const QRect& r : rects) {
        if (!r.isEmpty()) region |= r;
    }
    if (region.isEmpty()) {
        setMask(QRegion(0, 0, 1, 1));
    } else {
        setMask(region);
    }
}

void FinnLayerWindow::clearInputRegions() { clearMask(); }

void FinnLayerWindow::showEvent(QShowEvent* event) {
    applyLayerConfig();
    QWidget::showEvent(event);
}

}  // namespace finn::ui
