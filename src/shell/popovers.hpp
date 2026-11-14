// Shell popover layers: HomeMenuPopover, SpotlightPopover, ControlCenterPopover,
// OpenWindowsPopover, and LockScreenWindow.

#ifndef FINN_SHELL_POPOVERS_HPP
#define FINN_SHELL_POPOVERS_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>

#include "session/app_registry.hpp"
#include "shell/foreign_windows.hpp"
#include "shell/notifications.hpp"

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QVariantAnimation;

namespace finn::shell {

struct ShellActions {
    std::function<void(const std::string& app_id)> launch_app;
    std::function<void(const std::string& target)> open_target;
    std::function<void()> lock_screen;
    std::function<void()> reload_apps;
};

class HomeMenuPopover final : public finn::ui::FinnLayerWindow {
    Q_OBJECT

public:
    HomeMenuPopover(const std::vector<finn::App>* apps,
                    const ShellActions& actions);
    void refreshApps();
    void openMenu();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct HitItem {
        enum class Kind { LaunchApp, OpenCategory, BackToAll, Lock, Reboot, PowerOff };
        QRect rect;
        Kind kind = Kind::LaunchApp;
        QString key;
        QString tooltip;
    };

    void rebuildHits();
    std::vector<const finn::App*> favoriteApps() const;
    std::vector<const finn::App*> categoryApps(const QString& category) const;
    std::vector<const finn::App*> recentApps() const;

    const std::vector<finn::App>* apps_;
    ShellActions actions_;
    QString active_category_;
    QVector<HitItem> hits_;
    QVariantAnimation* open_anim_ = nullptr;
    qreal open_progress_ = 1.0;
    int hovered_hit_ = -1;
};

class SpotlightPopover final : public finn::ui::FinnLayerWindow {
    Q_OBJECT

public:
    SpotlightPopover(const std::vector<finn::App>* apps,
                     const ShellActions& actions);
    void openSearch();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct SearchHit {
        QRect rect;
        QString title;
        QString subtitle;
        QString icon;
        QString badge;
        bool is_app_icon = false;
        std::function<void()> activate;
    };

    void updateResults(const QString& query);
    void activateSelected();
    void applyQueryEditTheme();
    void updatePopoverInputRegions();

    const std::vector<finn::App>* apps_;
    ShellActions actions_;
    QLineEdit* query_edit_ = nullptr;
    QVariantAnimation* open_anim_ = nullptr;
    qreal open_progress_ = 1.0;
    QVector<SearchHit> empty_app_hits_;
    QVector<SearchHit> empty_query_hits_;
    QVector<SearchHit> hits_;
    int selected_idx_ = 0;
    int hovered_empty_app_ = -1;
    int hovered_empty_query_ = -1;
    int hovered_hit_ = -1;
};

class ControlCenterPopover final : public finn::ui::FinnLayerWindow {
    Q_OBJECT

public:
    ControlCenterPopover(NotificationServer* notifications,
                         const ShellActions& actions);
    void openDrawer();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    enum class ControlTarget {
        None,
        Wifi,
        Bluetooth,
        MediaPrev,
        MediaPlayPause,
        MediaNext,
        ThemeToggle,
        OpenSettings,
    };

    void syncSystemState();
    ControlTarget targetAt(const QPointF& pos) const;
    void updateHoverTooltip(ControlTarget target);

    ShellActions actions_;
    finn::ui::FinnSlider* brightness_slider_ = nullptr;
    finn::ui::FinnSlider* volume_slider_ = nullptr;
    QVariantAnimation* open_anim_ = nullptr;
    qreal open_progress_ = 1.0;
    ControlTarget hovered_ = ControlTarget::None;
};

class OpenWindowsPopover final : public finn::ui::FinnLayerWindow {
    Q_OBJECT

public:
    explicit OpenWindowsPopover(ForeignWindowsManager* windows);
    void openOverview();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    void rebuildCards();

    ForeignWindowsManager* windows_;
    QVBoxLayout* outer_layout_ = nullptr;
    finn::ui::FinnGlassPanel* panel_ = nullptr;
    QWidget* grid_host_ = nullptr;
    QGridLayout* grid_layout_ = nullptr;
    QVariantAnimation* open_anim_ = nullptr;
    qreal open_progress_ = 1.0;
};

class LockScreenWindow final : public finn::ui::FinnLayerWindow {
    Q_OBJECT

public:
    explicit LockScreenWindow(QWidget* parent = nullptr);
    void lockNow();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct UserAccount {
        QString username;
        QString display_name;
        int uid = 1000;
    };

    void updateClock();
    void loadUsers();
    void selectUser(const QString& username);
    void attemptUnlock();
    void shakeInput();
    void refreshTheme();

    finn::ui::FinnLabel* date_lbl_ = nullptr;
    finn::ui::FinnLabel* time_lbl_ = nullptr;
    QLabel* avatar_lbl_ = nullptr;
    finn::ui::FinnLabel* user_name_lbl_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    finn::ui::FinnLabel* error_lbl_ = nullptr;
    QHBoxLayout* users_row_ = nullptr;
    QWidget* auth_box_ = nullptr;
    QVector<UserAccount> users_;
    QString selected_user_;
};

class BootSplashWindow final : public finn::ui::FinnLayerWindow {
    Q_OBJECT

public:
    explicit BootSplashWindow(QWidget* parent = nullptr);

    void startBootAnimation(bool hold_open = false);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    finn::ui::FinnRibbonAnimation* ribbon_ = nullptr;
    QVariantAnimation* fade_anim_ = nullptr;
    qreal opacity_ = 1.0;
};

}  // namespace finn::shell

#endif
