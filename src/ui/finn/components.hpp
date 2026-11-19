// FinnUI custom C++20 Qt6 component library matching FinnOS Figma tokens and motion.

#ifndef FINN_UI_FINN_COMPONENTS_HPP
#define FINN_UI_FINN_COMPONENTS_HPP

#include <QAbstractButton>
#include <QColor>
#include <QDialog>
#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <functional>

#include "ui/finn/spring.hpp"
#include "ui/finn/tokens.hpp"

class QHBoxLayout;
class QScrollArea;
class QShowEvent;
class QVBoxLayout;

namespace finn::ui {

enum class FinnFormFactor { Desktop, Tablet, Phone };

FinnFormFactor resolve_form_factor(int width_dp = -1);
int touch_target_height(int desktop_height, FinnFormFactor ff);

// Figma smooth corners: one cubic-Bezier path shared by every painted Finn
// surface (and by FinnWindow's clip/mask) so cornerSmoothing = 0.6 is applied
// consistently instead of approximating it with a circular arc per surface.
QPainterPath finn_rounded_path(const QRectF& rect, qreal radius);

/* Lightweight QTimer-backed driver connecting SpringAnimator (and <=100ms
 * ReducedCrossfade fallback) to a QWidget. */
struct FinnSpringDriver {
    SpringAnimator animator;
    ReducedCrossfade crossfade;
    bool using_crossfade = false;
    QTimer* timer = nullptr;
    QElapsedTimer clock;
    std::function<void(double)> on_step;

    ~FinnSpringDriver() {
        if (timer) timer->stop();
    }

    void init(QWidget* owner, const SpringParams& params, double initial,
              std::function<void(double)> step_cb);
    void retarget(double target);
    void retarget(double target, const SpringParams& params);
    void retargetOrCrossfade(double from_on_reduced, double target,
                             const SpringParams& params,
                             double reduced_seconds = kMotionReducedCrossfade);
    void setImmediate(double value);
    double value() const {
        return using_crossfade ? crossfade.value : animator.value();
    }
    bool active() const { return timer != nullptr && timer->isActive(); }
};

class FinnSurface : public QWidget {
    Q_OBJECT

public:
    enum class Role { Canvas, Bone, Tile, TileOn, Transparent };

    explicit FinnSurface(Role role = Role::Tile, qreal radius = kRadiusLg,
                         bool border = true, QWidget* parent = nullptr);

    void setRole(Role role);
    void setRadius(qreal radius);
    void setBordered(bool bordered);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Role role_;
    qreal radius_;
    bool bordered_;
};

class FinnCard : public QWidget {
    Q_OBJECT

public:
    explicit FinnCard(qreal radius = kRadiusLg, bool clickable = false,
                      QWidget* parent = nullptr);

    void setClickable(bool clickable);
    void setSelected(bool selected);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    qreal radius_;
    bool clickable_ = false;
    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    qreal hover_progress_ = 0.0;
    qreal press_scale_ = 1.0;
    FinnSpringDriver hover_spring_;
    FinnSpringDriver press_spring_;
};

class FinnGlassPanel : public QWidget {
    Q_OBJECT

public:
    explicit FinnGlassPanel(qreal radius = kRadiusXl, QWidget* parent = nullptr);

    void setRadius(qreal radius);
    void setOpacityAlpha(int alpha);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal radius_;
    int alpha_ = -1;
};

class FinnLabel : public QLabel {
    Q_OBJECT

public:
    enum class Role { TitleLarge, Title, Headline, Body, Caption, Mono };
    enum class Tone { Ink, Dim, Mute, Accent, Success, Warning, Danger, OnAccent };

    explicit FinnLabel(const QString& text = {}, Role role = Role::Body,
                       Tone tone = Tone::Ink, QWidget* parent = nullptr);

    void setRole(Role role);
    void setTone(Tone tone);

private:
    void applyTypographyAndColor();

    Role role_;
    Tone tone_;
};

class FinnButton : public QPushButton {
    Q_OBJECT

public:
    enum class Variant { Secondary, Primary, Ghost, Danger };

    explicit FinnButton(const QString& text = {},
                        Variant variant = Variant::Secondary,
                        const QString& icon_name = {},
                        QWidget* parent = nullptr);

    void setVariant(Variant variant);
    void setIconName(const QString& icon_name);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    Variant variant_;
    QString icon_name_;
    bool hovered_ = false;
    qreal hover_progress_ = 0.0;
    qreal press_scale_ = 1.0;
    FinnSpringDriver hover_spring_;
    FinnSpringDriver press_spring_;
};

class FinnIconButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit FinnIconButton(const QString& icon_name = {}, int box_size = 30,
                            int icon_size = 16, QWidget* parent = nullptr);

    void setIconName(const QString& icon_name);
    void setActive(bool active);
    void setBadgeDot(bool show_dot);
    void setIconTint(const QColor& tint);
    void setCornerRadius(qreal radius);
    void setCustomHoverColor(const QColor& bg, const QColor& icon_tint);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QString icon_name_;
    int box_size_;
    int icon_size_;
    qreal radius_ = kRadiusMd;
    bool active_ = false;
    bool hovered_ = false;
    bool badge_dot_ = false;
    qreal hover_progress_ = 0.0;
    qreal press_scale_ = 1.0;
    FinnSpringDriver hover_spring_;
    FinnSpringDriver press_spring_;
    QColor custom_tint_;
    QColor hover_bg_;
    QColor hover_tint_;
};

class FinnToggle : public QAbstractButton {
    Q_OBJECT

public:
    explicit FinnToggle(bool checked = false, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void nextCheckState() override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    qreal progress_ = 0.0;
    FinnSpringDriver toggle_spring_;
};

class FinnSlider : public QWidget {
    Q_OBJECT

public:
    explicit FinnSlider(int min = 0, int max = 100, int value = 50,
                        QWidget* parent = nullptr);

    int value() const;
    void setValue(int v);
    void setRange(int min, int max);
    void setPillStyle(bool pill, const QString& icon_name = {}, int icon_x = 6,
                      int icon_size = 24);

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    QSize sizeHint() const override;

private:
    void updateFromX(int x, bool immediate);
    qreal currentRatio() const;

    int min_ = 0;
    int max_ = 100;
    int value_ = 50;
    qreal visual_ratio_ = 0.5;
    qreal knob_radius_ = 7.0;
    bool dragging_ = false;
    bool hovered_ = false;
    bool pill_ = false;
    QString pill_icon_;
    int pill_icon_x_ = 6;
    int pill_icon_size_ = 24;
    FinnSpringDriver ratio_spring_;
    FinnSpringDriver knob_spring_;
};

class FinnSearchField : public QWidget {
    Q_OBJECT

public:
    explicit FinnSearchField(const QString& placeholder = QStringLiteral("Search"),
                             QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& text);
    void setPlaceholderText(const QString& text);
    void setPillMode(bool pill);
    void focusInput();
    QLineEdit* lineEdit() const { return edit_; }

signals:
    void textChanged(const QString& text);
    void submitted(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void syncPalette();

    QLineEdit* edit_ = nullptr;
    FinnIconButton* clear_btn_ = nullptr;
    bool focused_ = false;
    bool pill_ = false;
};

class FinnTextField : public QLineEdit {
    Q_OBJECT

public:
    explicit FinnTextField(const QString& placeholder = {},
                           QWidget* parent = nullptr);
    void setPasswordMode(bool enabled);
};

class FinnProgressBar : public QWidget {
    Q_OBJECT

public:
    explicit FinnProgressBar(int height_px = 5, QWidget* parent = nullptr);

    void setValue(double ratio);  // 0.0..1.0
    double value() const { return value_; }
    void setUseLightFill(bool white_fill);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double value_ = 0.0;
    int height_px_;
    bool white_fill_ = false;
};

class FinnProgressRing : public QWidget {
    Q_OBJECT

public:
    explicit FinnProgressRing(int size_px = 20, QWidget* parent = nullptr);

    void setSpinning(bool spinning);
    void setValue(double ratio);  // >= 0 for determinate, < 0 for indeterminate

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int size_px_;
    double ratio_ = -1.0;
    int angle_deg_ = 0;
    QTimer* spin_timer_ = nullptr;
};

class FinnNavItem : public QAbstractButton {
    Q_OBJECT

public:
    explicit FinnNavItem(const QString& icon_name, const QString& label,
                         QWidget* parent = nullptr);

    void setBadgeText(const QString& badge);
    void setBadgeColor(const QColor& color);
    void setBadgeColor(const QColor& light, const QColor& dark);
    void setIconName(const QString& icon_name);
    void setSelected(bool selected);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QString icon_name_;
    QString badge_;
    QColor badge_color_;
    QColor badge_dark_color_;
    bool selected_ = false;
    bool hovered_ = false;
    qreal hover_progress_ = 0.0;
    qreal press_scale_ = 1.0;
    FinnSpringDriver hover_spring_;
    FinnSpringDriver press_spring_;
};

class FinnSidebar : public QWidget {
    Q_OBJECT

public:
    explicit FinnSidebar(int width_px = 250, QWidget* parent = nullptr);

    void setFormFactor(FinnFormFactor form_factor);
    void setHeaderTitle(const QString& title);
    void setSearchEnabled(bool enabled, const QString& placeholder = QStringLiteral("Search"));
    void addSectionHeader(const QString& title);
    void addSectionSpacer(int height_px = 16);
    FinnNavItem* addPinnedItem(const QString& id, const QString& icon_name,
                               const QString& label,
                               const QColor& badge_color = {},
                               const QString& badge = {});
    FinnNavItem* addItem(const QString& id, const QString& icon_name,
                         const QString& label, const QString& badge = {});
    FinnNavItem* addItem(const QString& id, const QString& icon_name,
                         const QString& label, const QColor& badge_color,
                         const QString& badge = {});
    FinnNavItem* addItem(const QString& id, const QString& icon_name,
                         const QString& label, const QColor& light_badge,
                         const QColor& dark_badge,
                         const QString& badge = {});
    void addStretch();
    void addFooterWidget(QWidget* widget);
    void setCurrentId(const QString& id);
    QString currentId() const { return current_id_; }
    FinnLabel* headerLabel() const { return header_label_; }
    FinnSearchField* searchField() const { return search_field_; }

signals:
    void selected(const QString& id);

private:
    void filterItems(const QString& query);
    FinnNavItem* createAndWireItem(QVBoxLayout* target_layout, const QString& id,
                                   const QString& icon_name, const QString& label,
                                   const QColor& badge_color, const QString& badge,
                                   bool pinned);

    QVBoxLayout* root_layout_ = nullptr;
    FinnLabel* header_label_ = nullptr;
    FinnSearchField* search_field_ = nullptr;
    QVBoxLayout* pinned_layout_ = nullptr;
    QVBoxLayout* items_layout_ = nullptr;
    QVector<QPair<QString, FinnNavItem*>> pinned_items_;
    QVector<QPair<QString, FinnNavItem*>> items_;
    QString current_id_;
    int desktop_width_ = 250;
    FinnFormFactor form_factor_ = FinnFormFactor::Desktop;
};

class FinnStatCard : public QWidget {
    Q_OBJECT

public:
    explicit FinnStatCard(const QString& caption, const QString& value,
                          QWidget* parent = nullptr);
    FinnStatCard(const QString& caption, const QString& primary_value,
                 const QString& secondary_suffix, QWidget* parent = nullptr);

    void setCaption(const QString& caption);
    void setValue(const QString& value);
    void setValueParts(const QString& primary, const QString& suffix);
    QSize sizeHint() const override { return QSize(258, 70); }
    QSize minimumSizeHint() const override { return QSize(210, 70); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString caption_;
    QString value_;
    QString suffix_;
};

class FinnHeroCard : public QWidget {
    Q_OBJECT

public:
    explicit FinnHeroCard(const QString& title, const QString& subtitle = {},
                          const QString& icon_name = QStringLiteral("boot-mark"),
                          QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setInlineSuffix(const QString& suffix);
    void setSubtitle(const QString& subtitle);
    void setIconName(const QString& icon_name);
    QSize sizeHint() const override { return QSize(258, 150); }
    QSize minimumSizeHint() const override { return QSize(210, 150); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_;
    QString inline_suffix_;
    QString subtitle_;
    QString icon_name_;
};

class FinnNavRail : public QWidget {
    Q_OBJECT

public:
    explicit FinnNavRail(int width_px = 68, QWidget* parent = nullptr);

    void addItem(const QString& id, const QString& icon_name,
                 const QString& label, bool bottom = false);
    void setCurrentId(const QString& id);
    QString currentId() const { return current_id_; }

signals:
    void selected(const QString& id);

private:
    struct RailEntry {
        QString id;
        QString icon_name;
        QString label;
        QAbstractButton* btn = nullptr;
    };
    QVBoxLayout* top_layout_ = nullptr;
    QVBoxLayout* bottom_layout_ = nullptr;
    QVector<RailEntry> entries_;
    QString current_id_;
};

class FinnSettingRow : public FinnCard {
    Q_OBJECT

public:
    explicit FinnSettingRow(const QString& title, const QString& subtitle = {},
                            const QString& icon_name = {},
                            QWidget* trailing = nullptr,
                            QWidget* parent = nullptr);

    void setTrailingWidget(QWidget* trailing);
    void setSubtitle(const QString& subtitle);
    void setIconName(const QString& icon_name);
    void setIconBadgeColor(const QColor& color);

private:
    QHBoxLayout* row_layout_ = nullptr;
    QWidget* icon_box_ = nullptr;
    FinnLabel* subtitle_label_ = nullptr;
    QWidget* trailing_ = nullptr;
};

class FinnBadge : public QWidget {
    Q_OBJECT

public:
    enum class Tone { Neutral, Accent, Success, Warning, Danger };

    explicit FinnBadge(const QString& text, Tone tone = Tone::Neutral,
                       QWidget* parent = nullptr);

    void setText(const QString& text);
    void setTone(Tone tone);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    Tone tone_;
};

class FinnDivider : public QWidget {
    Q_OBJECT

public:
    explicit FinnDivider(Qt::Orientation orientation = Qt::Horizontal,
                         QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Qt::Orientation orientation_;
};

class FinnTabBar : public QWidget {
    Q_OBJECT

public:
    explicit FinnTabBar(QWidget* parent = nullptr);

    void setTabs(const QStringList& labels);
    void setCurrentIndex(int index);
    int currentIndex() const { return current_; }

signals:
    void tabChanged(int index);

private:
    QHBoxLayout* layout_ = nullptr;
    QVector<FinnButton*> buttons_;
    int current_ = 0;
};

class FinnBreadcrumbs : public QWidget {
    Q_OBJECT

public:
    explicit FinnBreadcrumbs(QWidget* parent = nullptr);

    void setSegments(const QVector<QPair<QString, QString>>& segments);

signals:
    void segmentActivated(const QString& target);

private:
    QHBoxLayout* layout_ = nullptr;
};

class FinnEmptyState : public QWidget {
    Q_OBJECT

public:
    explicit FinnEmptyState(const QString& icon_name, const QString& title,
                            const QString& description = {},
                            const QString& action_label = {},
                            QWidget* parent = nullptr);

signals:
    void actionClicked();

private:
    QWidget* icon_box_ = nullptr;
    FinnButton* action_btn_ = nullptr;
};

class FinnToast : public FinnGlassPanel {
    Q_OBJECT

public:
    enum class Severity { Info, Success, Warning, Error };

    explicit FinnToast(QWidget* parent = nullptr);
    void showMessage(const QString& message, Severity severity = Severity::Info,
                     int duration_ms = 2800);
    void repositionInParent();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    FinnLabel* label_ = nullptr;
    Severity severity_ = Severity::Info;
    QTimer* hide_timer_ = nullptr;
    qreal entrance_progress_ = 1.0;
    bool reduced_fade_ = false;
    QPoint target_pos_;
    FinnSpringDriver entrance_spring_;
};

void install_overscroll_physics(QScrollArea* scroll);
void install_stack_animator(QStackedWidget* stack);
void animate_stack_switch(QStackedWidget* stack, int next_index);

class FinnPageStack : public QStackedWidget {
    Q_OBJECT

public:
    explicit FinnPageStack(QWidget* parent = nullptr);
    void setAnimatedTransitions(bool enabled);
    bool animatedTransitions() const { return animated_; }

private:
    bool animated_ = true;
};

using FinnAnimatedStack = FinnPageStack;

class FinnDialog : public QDialog {
    Q_OBJECT

public:
    explicit FinnDialog(const QString& title, const QString& message,
                        const QString& confirm_label = QStringLiteral("Confirm"),
                        const QString& cancel_label = QStringLiteral("Cancel"),
                        bool destructive = false, QWidget* parent = nullptr);

    static bool confirm(QWidget* parent, const QString& title,
                        const QString& message,
                        const QString& confirm_label = QStringLiteral("Confirm"),
                        bool destructive = false);
    static QString prompt(QWidget* parent, const QString& title,
                          const QString& label,
                          const QString& initial_value = {},
                          bool password_mode = false);

protected:
    void showEvent(QShowEvent* event) override;

private:
    FinnSpringDriver opacity_spring_;
    bool shown_once_ = false;
};

// 6-ring 3D vortex-to-slinky coil animation and continuous rounding line that
// morphs into the cursive "hi." on FinnOS boot and OOBE.
class FinnRibbonAnimation : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        HiIntro,     // Rounding line going on and on, then drawing into "hi."
        SlinkyWave,  // 6-ring 3D vortex unfurling into horizontal slinky wave
    };

    explicit FinnRibbonAnimation(Mode mode = Mode::HiIntro, QWidget* parent = nullptr);
    explicit FinnRibbonAnimation(QWidget* parent);

    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    void restart();
    void setPhaseTime(double seconds);
    double phaseTime() const { return elapsed_; }
    void setAmbientGlow(bool enabled);

    // 0.0 while the rounding line / morph is running, 0.0 -> 1.0 after "hi." settles
    // so the rest of the Figma UI (Branding + Next circle button) fades in afterward.
    double uiRevealProgress() const;

signals:
    void introCompleted();
    void frameAdvanced(double elapsed_seconds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void tick();
    void paintHiIntro(QPainter& p, double w, double h, double t);
    void paintSlinkyWave(QPainter& p, double w, double h, double t);

    Mode mode_ = Mode::HiIntro;
    QTimer* timer_ = nullptr;
    qint64 last_ms_ = 0;
    double elapsed_ = 0.0;
    bool paused_ = false;
    bool intro_emitted_ = false;
    bool ambient_glow_ = true;
};

}  // namespace finn::ui

#endif
