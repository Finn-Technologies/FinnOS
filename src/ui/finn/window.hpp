// FinnWindow (custom frameless CSD Wayland application window with Figma
// window controls, collapsible sidebar, edge resize, and toast overlay) and
// FinnLayerWindow (LayerShellQt panel wrapper with click-through input masking).

#ifndef FINN_UI_FINN_WINDOW_HPP
#define FINN_UI_FINN_WINDOW_HPP

#include <QMargins>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

#include "ui/finn/components.hpp"

class QHBoxLayout;
class QVBoxLayout;

namespace LayerShellQt {
class Window;
}

namespace finn::ui {

class FinnWindowControls final : public QWidget {
    Q_OBJECT

public:
    explicit FinnWindowControls(QWidget* parent = nullptr);

    void setMaximizedState(bool maximized);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    FinnIconButton* min_btn_ = nullptr;
    FinnIconButton* max_btn_ = nullptr;
    FinnIconButton* close_btn_ = nullptr;
};

class FinnWindow : public QWidget {
    Q_OBJECT

public:
    enum class ChromeMode { Standard, FigmaSplit };

    explicit FinnWindow(const QString& title = {},
                        const QString& icon_name = {},
                        QWidget* parent = nullptr);
    ~FinnWindow() override = default;

    void setChromeMode(ChromeMode mode);
    void setWindowTitleText(const QString& title);
    void setHeaderWidget(QWidget* widget);
    void setSidebarWidget(QWidget* widget);
    void setContentWidget(QWidget* widget);
    void setSidebarCollapsed(bool collapsed);
    bool isSidebarCollapsed() const { return sidebar_collapsed_; }

    void showToast(const QString& message,
                   FinnToast::Severity severity = FinnToast::Severity::Info,
                   int duration_ms = 2800);
    FinnFormFactor formFactor() const { return form_factor_; }

signals:
    void formFactorChanged(finn::ui::FinnFormFactor form_factor);

public slots:
    void toggleMaximized();

protected:
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    Qt::Edges hitTestResizeEdges(const QPoint& pos) const;
    void updateMarginsForWindowState();
    void updateFrameMask();
    void positionToast();
    void applyFormFactorLayout(bool initial);

    QWidget* frame_root_ = nullptr;
    QVBoxLayout* outer_layout_ = nullptr;
    QWidget* title_bar_ = nullptr;
    FinnDivider* title_divider_ = nullptr;
    QHBoxLayout* header_slot_ = nullptr;
    ChromeMode chrome_mode_ = ChromeMode::Standard;
    QWidget* header_widget_ = nullptr;
    FinnIconButton* sidebar_toggle_btn_ = nullptr;
    QLabel* title_icon_ = nullptr;
    FinnLabel* title_label_ = nullptr;
    FinnWindowControls* controls_ = nullptr;

    QWidget* body_split_ = nullptr;
    QHBoxLayout* body_layout_ = nullptr;
    QWidget* sidebar_container_ = nullptr;
    QVBoxLayout* sidebar_slot_ = nullptr;
    FinnDivider* sidebar_divider_ = nullptr;
    QWidget* sidebar_widget_ = nullptr;
    QWidget* content_container_ = nullptr;
    QVBoxLayout* content_slot_ = nullptr;
    QWidget* content_widget_ = nullptr;

    FinnToast* toast_ = nullptr;
    QString icon_name_;
    FinnFormFactor form_factor_ = FinnFormFactor::Desktop;
    bool sidebar_collapsed_ = false;
    int sidebar_expanded_width_ = 250;
    bool shown_once_ = false;
    bool drag_candidate_ = false;
    QPoint drag_start_pos_;
    FinnSpringDriver sidebar_spring_;
    FinnSpringDriver entrance_spring_;
};

class FinnLayerWindow : public QWidget {
    Q_OBJECT

public:
    enum class Layer {
        Background = 0,
        Bottom = 1,
        Top = 2,
        Overlay = 3,
    };

    enum Anchor {
        AnchorNone = 0,
        AnchorTop = 1 << 0,
        AnchorBottom = 1 << 1,
        AnchorLeft = 1 << 2,
        AnchorRight = 1 << 3,
    };
    Q_DECLARE_FLAGS(Anchors, Anchor)

    enum class KeyboardMode {
        None = 0,
        Exclusive = 1,
        OnDemand = 2,
    };

    explicit FinnLayerWindow(Layer layer = Layer::Top,
                             const QString& scope = QStringLiteral("finn-shell"),
                             QWidget* parent = nullptr);
    ~FinnLayerWindow() override = default;

    void setLayer(Layer layer);
    void setAnchors(Anchors anchors);
    void setKeyboardMode(KeyboardMode mode);
    void setExclusiveZone(int zone);
    void setLayerMargins(const QMargins& margins);
    void setDesiredSize(const QSize& size);
    void setInputRegions(const QVector<QRect>& rects);
    void clearInputRegions();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void applyLayerConfig();

    Layer layer_;
    Anchors anchors_ = AnchorNone;
    KeyboardMode keyboard_mode_ = KeyboardMode::None;
    int exclusive_zone_ = 0;
    QMargins margins_;
    QSize desired_size_;
    QString scope_;
    LayerShellQt::Window* layer_win_ = nullptr;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(FinnLayerWindow::Anchors)

}  // namespace finn::ui

#endif
