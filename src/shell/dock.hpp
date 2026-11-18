// Bottom 4-pill Figma glass dock layer-shell surface with dynamic running app
// indicators, live clock/network/audio/battery status, and masked input regions.

#ifndef FINN_SHELL_DOCK_HPP
#define FINN_SHELL_DOCK_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QStringList>
#include <QVector>
#include <functional>

#include "session/app_registry.hpp"
#include "shell/foreign_windows.hpp"

class QHBoxLayout;
class QWidget;

namespace finn::shell {

class HomeButtonsPill;
class HardwarePill;
class TimePill;

class DockWindow final : public finn::ui::FinnLayerWindow {
    Q_OBJECT

public:
    DockWindow(const std::vector<finn::App>* apps,
               ForeignWindowsManager* windows,
               std::function<void(const std::string& app_id)> launch_app);
    ~DockWindow() override;

    void rebuildAppsPill();
    void setActivePopovers(bool home, bool windows_open, bool search, bool control);

signals:
    void homeClicked();
    void windowsClicked();
    void searchClicked();
    void statusClicked();
    void dockAppActivated();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyResponsiveLayout();
    void updateStatusPill();
    void updateInputMask();
    void updateThemeVisuals();

    const std::vector<finn::App>* apps_;
    ForeignWindowsManager* windows_;
    std::function<void(const std::string&)> launch_app_;

    QHBoxLayout* root_layout_ = nullptr;
    QWidget* apps_pill_ = nullptr;
    HomeButtonsPill* home_pill_ = nullptr;
    HardwarePill* status_pill_ = nullptr;
    TimePill* time_pill_ = nullptr;
    QWidget* phone_status_bar_ = nullptr;

    QHBoxLayout* apps_pill_layout_ = nullptr;
    QStringList shown_app_ids_;
    bool any_popover_open_ = false;
};

}  // namespace finn::shell

#endif
