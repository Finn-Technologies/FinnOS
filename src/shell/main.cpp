#include <finn/ui.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "session/client.hpp"

namespace {

using finn::ui::Canvas;
using finn::ui::Color;
using finn::ui::LayerApp;
using finn::ui::Rect;

std::string clock_text() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char time[16] = {};
    char date[32] = {};
    std::strftime(time, sizeof time, "%H:%M", &local);
    std::strftime(date, sizeof date, "%a %d %b", &local);
    return std::string(time) + "\n" + date;
}

class Shell final : public LayerApp {
public:
    Shell()
        : LayerApp("finn-shell", 0, 0, finn::ui::kAnchorAll,
                   finn::ui::kLayerOverlay,
                   finn::ui::KeyboardMode::none) {
        set_tick_ms(1000);
        refresh();
        if (const char* menu = std::getenv("FINN_SHELL_MENU");
            menu != nullptr && *menu != '\0' && *menu != '0') {
            menu_open_ = true;
        }
    }

    void on_tick() override {
        const auto now = std::chrono::steady_clock::now();
        if (!error_.empty() &&
            now > error_until_) {
            error_.clear();
        }
        request_frame();
    }

    void draw(Canvas& canvas) override {
        const double width = canvas.width();
        const double height = canvas.height();
        if (width < 200 || height < 200) return;

        draw_dock(canvas, width, height);
        if (menu_open_) draw_menu(canvas, width);

        std::vector<Rect> region;
        if (menu_open_) {
            region.push_back(Rect{0, 0, width, height});
        } else {
            region.push_back(dock_);
        }
        set_input_region(region);

        const auto desired =
            menu_open_ ? finn::ui::KeyboardMode::on_demand : finn::ui::KeyboardMode::none;
        set_keyboard_mode(desired);
        if (menu_open_ && canvas.input().key(finn::ui::Key::escape)) {
            close_menu();
        }
    }

private:
    void refresh() {
        std::string error;
        std::vector<finn::SessionApp> apps;
        if (!client_.list(&apps, &error)) {
            apps_.clear();
            session_error_ = error;
            return;
        }
        session_error_.clear();
        apps_ = std::move(apps);
        std::sort(apps_.begin(), apps_.end(), [](const finn::SessionApp& a,
                                                  const finn::SessionApp& b) {
            if (a.installed != b.installed) return a.installed;
            return a.name < b.name;
        });
    }

    void open_menu() {
        menu_open_ = true;
        refresh();
        request_frame();
    }

    void close_menu() {
        menu_open_ = false;
        request_frame();
    }

    void launch(const finn::SessionApp& app) {
        std::string error;
        if (!client_.launch(app.id, &error)) {
            error_ = error;
            error_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            request_frame();
            return;
        }
        error_.clear();
        close_menu();
    }

    void draw_dock(Canvas& canvas, double width, double height) {
        const auto& theme = canvas.theme();
        const int installed = static_cast<int>(std::count_if(
            apps_.begin(), apps_.end(),
            [](const finn::SessionApp& app) { return app.installed; }));
        const int tiles = std::min(installed, 6);
        const double tile = 48;
        const double gap = 8;
        const double pad = 12;
        const double clock_w = 104;
        const double panel_w =
            pad * 2 + tile + gap + tiles * (tile + gap) + clock_w + 8;
        const double panel_h = tile + pad * 2;
        dock_ = Rect{(width - panel_w) / 2, height - panel_h - 18, panel_w, panel_h};

        canvas.fill(Rect{dock_.x + 1, dock_.y + 3, dock_.w, dock_.h},
                    Color::rgba(0x18000000), 16);
        canvas.fill(dock_, theme.glass.with_alpha(242), 16);
        canvas.stroke(dock_, theme.hair, 16, 1);

        double x = dock_.x + pad;
        const double y = dock_.y + pad;
        if (canvas.icon_button("launcher", Rect{x, y, tile, tile}, "app-grid.svg",
                               theme.ink)) {
            if (menu_open_) {
                close_menu();
            } else {
                open_menu();
            }
        }
        x += tile + gap;

        int shown = 0;
        for (const finn::SessionApp& app : apps_) {
            if (!app.installed) continue;
            if (shown++ >= 6) break;
            const std::string id = "dock:" + app.id;
            if (canvas.app_icon_button(id, Rect{x, y, tile, tile}, app.icon)) {
                launch(app);
            }
            x += tile + gap;
        }

        x = dock_.x + dock_.w - clock_w - pad;
        const std::string clock = clock_text();
        canvas.text(x, y + 20, clock.substr(0, 5), 16, theme.ink);
        canvas.text(x, y + 38, clock.substr(6), 10, theme.mute);
        if (!error_.empty()) {
            canvas.text(dock_.x + pad, dock_.y - 10, error_, 11, theme.mute);
        }
    }

    void draw_menu(Canvas& canvas, double width) {
        const auto& theme = canvas.theme();
        const double row_h = 58;
        const int rows = std::min(static_cast<int>(apps_.size()), 8);
        const double menu_w = 380;
        const double menu_h = 52 + rows * row_h + 12;
        const double x = (width - menu_w) / 2;
        const double y = std::max(20.0, dock_.y - menu_h - 14);
        const Rect menu{x, y, menu_w, menu_h};

        canvas.fill(Rect{menu.x + 1, menu.y + 4, menu.w, menu.h},
                    Color::rgba(0x20000000), 18);
        canvas.fill(menu, theme.glass.with_alpha(246), 18);
        canvas.stroke(menu, theme.hair, 18, 1);

        canvas.text(menu.x + 18, menu.y + 32, "Apps", 18, theme.ink);
        if (canvas.icon_button("menu-close", Rect{menu.x + menu.w - 44, menu.y + 8, 32, 32},
                               "x.svg", theme.mute)) {
            close_menu();
            return;
        }

        if (apps_.empty()) {
            const std::string message =
                session_error_.empty() ? "No apps installed" : session_error_;
            canvas.text(menu.x + 18, menu.y + 76, message, 12, theme.mute);
            return;
        }

        double row_y = menu.y + 52;
        for (int i = 0; i < rows; ++i) {
            const finn::SessionApp& app = apps_[static_cast<std::size_t>(i)];
            const Rect row{menu.x + 10, row_y, menu.w - 20, row_h - 4};
            const std::string id = "menu:" + app.id;
            const std::string detail = app.installed ? "" : "Not installed";
            if (canvas.app_list_item(id, row, app.icon, app.name, detail,
                                      app.installed) &&
                app.installed) {
                launch(app);
                return;
            }
            row_y += row_h;
        }
    }

    finn::SessionClient client_;
    std::vector<finn::SessionApp> apps_;
    Rect dock_;
    bool menu_open_ = false;
    std::string error_;
    std::string session_error_;
    std::chrono::steady_clock::time_point error_until_{};
};

}  // namespace

int main() {
    Shell shell;
    return shell.run();
}
