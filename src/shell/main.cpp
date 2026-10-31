// Layer-shell client rendering the FinnOS desktop shell, dock, taskbar,
// app launcher, and control center. Manages dynamic input regions to keep
// background screen areas interactive while maintaining responsive bottom
// glass pills and floating popover menus matching the Figma specifications.

#include <finn/ui.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

#include "common/paths.hpp"
#include "session/client.hpp"

namespace {

using finn::ui::Canvas;
using finn::ui::Color;
using finn::ui::LayerApp;
using finn::ui::Rect;

struct RecentApp {
    std::string id;
    std::string name;
    std::string icon;
};

// Returns current time formatted in 24-hour HH:MM notation per Figma spec.
std::string time_text() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buf[16] = {};
    std::strftime(buf, sizeof buf, "%H:%M", &local);
    return std::string(buf);
}

// Queries the current user's login account name via POSIX user identity without
// fabricating mock profiles or services.
std::string get_username() {
    struct passwd* pw = getpwuid(getuid());
    if (pw != nullptr && pw->pw_name != nullptr && pw->pw_name[0] != '\0') {
        return pw->pw_name;
    }
    const char* user = std::getenv("USER");
    if (user != nullptr && *user != '\0') return user;
    return "finn";
}

std::filesystem::path user_settings_path() {
    const char* home = std::getenv("HOME");
    const std::filesystem::path base =
        home != nullptr && *home != '\0' ? home : "/home/finn";
    return base / ".config" / "finn" / "settings";
}

bool read_dark_setting() {
    const std::filesystem::path user = user_settings_path();
    for (const std::filesystem::path& path :
         {user, std::filesystem::path(finn::kSettingsPath)}) {
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line)) {
            if (line == "dark=1") return true;
            if (line == "dark=0") return false;
        }
    }
    return false;
}

bool write_dark_setting(bool dark) {
    const std::filesystem::path path = user_settings_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    std::vector<std::string> lines;
    {
        std::ifstream existing(path);
        std::string line;
        while (std::getline(existing, line)) lines.push_back(line);
    }
    bool replaced = false;
    for (std::string& line : lines) {
        if (line.rfind("dark=", 0) == 0) {
            line = dark ? "dark=1" : "dark=0";
            replaced = true;
        }
    }
    if (!replaced) lines.push_back(dark ? "dark=1" : "dark=0");

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        for (const std::string& line : lines) out << line << '\n';
        if (!out) return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

class Shell final : public LayerApp {
public:
    Shell()
        : LayerApp("finn-shell", 0, 0, finn::ui::kAnchorAll,
                   finn::ui::kLayerOverlay,
                   finn::ui::KeyboardMode::none),
          dark_(read_dark_setting()) {
        set_tick_ms(1000);
        refresh();
        dock_in_.set(0.0);
        dock_in_.to(1.0, finn::ui::kMotionSlow);
        if (const char* all = std::getenv("FINN_SHELL_ALL_APPS");
            all != nullptr && *all != '\0' && *all != '0') {
            open_menu();
            all_apps_ = true;
        } else if (const char* menu = std::getenv("FINN_SHELL_MENU");
            menu != nullptr && *menu != '\0' && *menu != '0') {
            open_menu();
        } else if (const char* cc = std::getenv("FINN_SHELL_CONTROL_CENTER");
            cc != nullptr && *cc != '\0' && *cc != '0') {
            open_control_center();
        }
    }

    void on_tick() override {
        const auto now = std::chrono::steady_clock::now();
        if (!error_.empty() && now > error_until_) error_.clear();
        if (!session_error_.empty() && now > next_refresh_) {
            next_refresh_ = now + std::chrono::seconds(2);
            refresh();
        }
        dark_ = read_dark_setting();
        request_frame();
    }

    void draw(Canvas& canvas) override {
        const double width = canvas.width();
        const double height = canvas.height();
        if (width < 200 || height < 200) return;

        const double dt = canvas.delta_time();
        bool animating = false;
        if (dock_in_.running()) {
            dock_in_.update(dt);
            animating = true;
        }
        if (menu_anim_.running()) {
            menu_anim_.update(dt);
            animating = true;
            if (!menu_anim_.running() && !menu_open_) menu_visible_ = false;
        }
        if (control_center_anim_.running()) {
            control_center_anim_.update(dt);
            animating = true;
            if (!control_center_anim_.running() && !control_center_open_) {
                control_center_visible_ = false;
            }
        }

        const double ptr_x = canvas.input().x;
        const double ptr_y = canvas.input().y;

        // Dismiss open panels when the user clicks outside their bounding geometry.
        if (canvas.input().pointer_pressed) {
            if (menu_open_ && menu_rect_.w > 0 && !menu_rect_.contains(ptr_x, ptr_y) &&
                !left_dock_.contains(ptr_x, ptr_y)) {
                close_menu();
            }
            if (control_center_open_ && control_center_rect_.w > 0 &&
                !control_center_rect_.contains(ptr_x, ptr_y) &&
                !right_dock_.contains(ptr_x, ptr_y)) {
                close_control_center();
            }
        }

        const bool m_visible = menu_open_ || menu_anim_.value > 0.001;
        menu_visible_ = m_visible;
        const bool cc_visible = control_center_open_ || control_center_anim_.value > 0.001;
        control_center_visible_ = cc_visible;

        draw_dock(canvas, width, height);
        if (m_visible) draw_menu(canvas, width);
        if (cc_visible) draw_control_center(canvas);

        std::vector<Rect> region;
        if (m_visible || cc_visible) {
            region.push_back(Rect{0, 0, width, height});
        } else {
            region.push_back(taskbar_);
            region.push_back(left_dock_);
            region.push_back(time_weather_);
            region.push_back(right_dock_);
        }
        set_input_region(region);
        set_keyboard_mode((m_visible || cc_visible) ? finn::ui::KeyboardMode::on_demand
                                                    : finn::ui::KeyboardMode::none);

        if ((m_visible || cc_visible) && canvas.input().key(finn::ui::Key::escape)) {
            close_menu();
            close_control_center();
        }

        if (animating || canvas.animating()) request_frame();
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
        refresh();
        close_control_center();
        menu_open_ = true;
        menu_visible_ = true;
        all_apps_ = false;
        scroll_offset_ = 0.0;
        menu_anim_.to(1.0, finn::ui::kMotionStandard);
        request_frame();
    }

    void close_menu() {
        if (!menu_open_ && !menu_anim_.running()) {
            menu_visible_ = false;
            return;
        }
        menu_open_ = false;
        menu_anim_.to(0.0, finn::ui::kMotionFast);
        request_frame();
    }

    void open_control_center() {
        close_menu();
        control_center_open_ = true;
        control_center_visible_ = true;
        control_center_anim_.to(1.0, finn::ui::kMotionStandard);
        request_frame();
    }

    void close_control_center() {
        if (!control_center_open_ && !control_center_anim_.running()) {
            control_center_visible_ = false;
            return;
        }
        control_center_open_ = false;
        control_center_anim_.to(0.0, finn::ui::kMotionFast);
        request_frame();
    }

    void launch(const finn::SessionApp& app) {
        launch_by_id(app.id, app.name, app.icon);
    }

    void launch_by_id(std::string_view id, std::string_view name = {},
                      std::string_view icon = {}) {
        active_app_ = std::string(id);
        std::string error;
        if (!client_.launch(id, &error)) {
            error_ = error;
            error_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            request_frame();
            return;
        }
        error_.clear();

        // Track launch in recent activity history if not already present.
        if (!id.empty()) {
            std::string app_name = std::string(name);
            std::string app_icon = std::string(icon);
            if (app_name.empty()) {
                for (const auto& a : apps_) {
                    if (a.id == id) {
                        app_name = a.name;
                        app_icon = a.icon;
                        break;
                    }
                }
            }
            if (!app_name.empty()) {
                auto it = std::remove_if(recent_apps_.begin(), recent_apps_.end(),
                                         [&](const RecentApp& item) { return item.id == id; });
                recent_apps_.erase(it, recent_apps_.end());
                recent_apps_.insert(recent_apps_.begin(),
                                    RecentApp{std::string(id), app_name, app_icon});
                if (recent_apps_.size() > 4) recent_apps_.pop_back();
            }
        }

        close_menu();
        close_control_center();
    }

    // Lays out the four Figma bottom pills: Taskbar (115x42), Left Dock (116x42),
    // Time & Weather (139x42), and Right Dock (116x42), with 15px margins and gaps.
    void draw_dock(Canvas& canvas, double width, double height) {
        const auto& theme = canvas.theme();
        const Color glass_tint = Color::rgb(0xFFFFFF).with_alpha(166);  // 65% white
        const double lift = (1.0 - finn::ui::ease_out_cubic(dock_in_.value)) * 16.0;

        const double pill_h = 42.0;
        const double bottom_margin = 15.0;
        const double y_base = height - pill_h - bottom_margin;
        const double y = y_base + lift;

        // Anchor group around center x (Left Dock centered on screen at cx - 59).
        const double cx = width / 2.0;
        left_dock_ = Rect{cx - 59.0, y, 116.0, pill_h};
        taskbar_ = Rect{left_dock_.x - 15.0 - 115.0, y, 115.0, pill_h};
        time_weather_ = Rect{left_dock_.x + 116.0 + 15.0, y, 139.0, pill_h};
        right_dock_ = Rect{time_weather_.x + 139.0 + 15.0, y, 116.0, pill_h};

        // Render glass background blur on all four pills (radius 8, blur 15 per SPEC.md).
        canvas.glass(taskbar_, 8.0, glass_tint, 15.0);
        canvas.glass(left_dock_, 8.0, glass_tint, 15.0);
        canvas.glass(time_weather_, 8.0, glass_tint, 15.0);
        canvas.glass(right_dock_, 8.0, glass_tint, 15.0);

        // --- 1. Taskbar (Files, Arc, Settings with 26x26 rounded squircle icons) ---
        const double tb_y = taskbar_.y + 8.0;
        const Rect files_rect{taskbar_.x + 9.0, tb_y, 26.0, 26.0};
        const Rect arc_rect{taskbar_.x + 45.0, tb_y, 26.0, 26.0};
        const Rect settings_rect{taskbar_.x + 81.0, tb_y, 26.0, 26.0};

        // Active app indicator per Figma node 186:3891 (white bg 34x34 at y+4, blue bar at y+40)
        auto draw_active_indicator = [&](double icon_x) {
            canvas.fill(Rect{icon_x - 4.0, taskbar_.y + 4.0, 34.0, 34.0},
                        Color::rgb(0xFFFFFF), 4.0);
            canvas.line(icon_x + 11.0, taskbar_.y + 40.0, icon_x + 15.0,
                        taskbar_.y + 40.0, Color::rgb(0x1F4FFF), 1.5);
        };

        if (active_app_ == "files") {
            draw_active_indicator(files_rect.x);
        } else if (active_app_ == "arc") {
            draw_active_indicator(arc_rect.x);
        } else if (active_app_ == "settings") {
            draw_active_indicator(settings_rect.x);
        }

        canvas.app_icon("files.svg", files_rect);
        canvas.app_icon("arc.svg", arc_rect);
        canvas.app_icon("settings-app.svg", settings_rect);

        // Clicking taskbar icons launches or activates applications.
        if (canvas.input().pointer_released) {
            const double px = canvas.input().x;
            const double py = canvas.input().y;
            if (files_rect.contains(px, py)) {
                launch_by_id("files", "Finn Files", "files.svg");
            } else if (arc_rect.contains(px, py)) {
                active_app_ = "arc";
                request_frame();
            } else if (settings_rect.contains(px, py)) {
                launch_by_id("settings", "Settings", "settings-app.svg");
            }
        }

        // --- 2. Left Dock (Search, Home, View-open-windows at 22x22, y+10) ---
        const double ld_y = left_dock_.y + 10.0;
        const Rect search_rect{left_dock_.x + 10.0, ld_y, 22.0, 22.0};
        const Rect home_rect{left_dock_.x + 47.0, ld_y, 22.0, 22.0};
        const Rect win_rect{left_dock_.x + 84.0, ld_y, 22.0, 22.0};

        canvas.icon("search-01.svg", search_rect, theme.ink);
        canvas.icon("home-09.svg", home_rect, theme.ink);
        canvas.icon("view-open-windows.svg", win_rect, theme.ink);

        if (canvas.input().pointer_released &&
            home_rect.contains(canvas.input().x, canvas.input().y)) {
            if (menu_open_) {
                close_menu();
            } else {
                open_menu();
            }
        }

        // --- 3. Time & Weather Pill (Real time in 15:48 format; weather omitted) ---
        const std::string clock_str = time_text();
        // Centered horizontally within the 139px pill; font size 16 per SPEC.md.
        canvas.text(time_weather_.x + 48.0, time_weather_.y + 26.0, clock_str, 16.0,
                    theme.ink);

        // --- 4. Right Dock (Status icons: Volume, Wi-Fi, Battery at 22x22, y+10) ---
        const double rd_y = right_dock_.y + 10.0;
        const Rect vol_rect{right_dock_.x + 10.0, rd_y, 22.0, 22.0};
        const Rect wifi_rect{right_dock_.x + 47.0, rd_y, 22.0, 22.0};
        const Rect bat_rect{right_dock_.x + 84.0, rd_y, 22.0, 22.0};

        canvas.icon("volume-02.svg", vol_rect, theme.ink);
        canvas.icon("wifi-02.svg", wifi_rect, theme.ink);
        canvas.icon("battery.svg", bat_rect, theme.ink);

        if (canvas.input().pointer_released &&
            right_dock_.contains(canvas.input().x, canvas.input().y)) {
            if (control_center_open_) {
                close_control_center();
            } else {
                open_control_center();
            }
        }

        if (!error_.empty()) {
            canvas.text(left_dock_.x, left_dock_.y - 10.0, error_, 10.0, theme.mute);
        }
    }

    // Renders the 800x500 Application Menu popover matching Figma nodes 186:4172 and 218:107.
    void draw_menu(Canvas& canvas, double width) {
        const auto& theme = canvas.theme();
        const Color glass_tint = Color::rgb(0xFFFFFF).with_alpha(166);

        const double menu_w = 800.0;
        const double menu_h = 500.0;
        const double base_y = taskbar_.y - 15.0 - menu_h;
        const double y = base_y +
                         (1.0 - finn::ui::ease_out_cubic(menu_anim_.value)) * 12.0;
        const double x = (width - menu_w) / 2.0;

        menu_rect_ = Rect{x, base_y, menu_w, menu_h};
        const Rect menu{x, y, menu_w, menu_h};

        // Panel background with glass blur
        canvas.glass(menu, 8.0, glass_tint, 15.0);

        // Right Panel (385x480 at x: 405, y: 10 inside menu container per SPEC.md)
        const Rect right_panel{menu.x + 405.0, menu.y + 10.0, 385.0, 480.0};
        canvas.fill(right_panel, Color::rgb(0xFFFFFF).with_alpha(217), 8.0);

        // Placeholder widgets inside Right Panel (177.5x170 at x: 10 and x: 197.5)
        const Rect widget_1{right_panel.x + 10.0, right_panel.y + 10.0, 177.5, 170.0};
        const Rect widget_2{right_panel.x + 197.5, right_panel.y + 10.0, 177.5, 170.0};
        canvas.fill(widget_1, Color::rgb(0xB5B5B5), 8.0);
        canvas.fill(widget_2, Color::rgb(0xB5B5B5), 8.0);

        // Left Section Content
        if (!all_apps_) {
            // "Apps | Pinned" header
            canvas.text(menu.x + 20.0, menu.y + 36.0, "Apps", 14.0, theme.ink);
            canvas.text(menu.x + 58.0, menu.y + 36.0, "| Pinned", 14.0, theme.mute);

            // "View all >" pill button
            const Rect view_all_rect{menu.x + 307.0, menu.y + 18.0, 88.0, 26.0};
            canvas.fill(view_all_rect, Color::rgb(0xFFFFFF).with_alpha(200), 13.0);
            canvas.text(view_all_rect.x + 16.0, view_all_rect.y + 17.0, "View all >",
                        12.0, theme.ink);

            if (canvas.input().pointer_released &&
                view_all_rect.contains(canvas.input().x, canvas.input().y)) {
                all_apps_ = true;
                scroll_offset_ = 0.0;
                request_frame();
            }

        // Pinned Apps List (Settings, Files, Arc per Figma node 186:4172)
        const std::vector<std::pair<std::string, std::string>> pinned_apps = {
            {"settings", "Settings"},
            {"files", "Files"},
            {"arc", "Arc"}
        };

        double item_y = menu.y + 56.0;
        const double row_h = 38.0;

        for (const auto& [p_id, p_name] : pinned_apps) {
            std::string p_icon = (p_id == "settings") ? "settings-app.svg"
                               : (p_id == "files") ? "files.svg" : "arc.svg";
            // Check if present in registered catalogue for real icon/installed status
            bool installed = true;
            for (const auto& app : apps_) {
                if (app.id == p_id) {
                    p_icon = app.icon;
                    installed = app.installed;
                    break;
                }
            }

            const Rect row{menu.x + 20.0, item_y, 365.0, row_h};
            const Rect icon_rect{row.x, row.y + 6.0, 26.0, 26.0};
            canvas.app_icon(p_icon, icon_rect);
            canvas.text(row.x + 38.0, row.y + 24.0, p_name, 14.0, theme.ink);

            if (canvas.input().pointer_released &&
                row.contains(canvas.input().x, canvas.input().y) && installed) {
                launch_by_id(p_id, p_name, p_icon);
                return;
            }
            item_y += 42.0;
        }

            // "Recent activity" header
            canvas.text(menu.x + 20.0, menu.y + 206.0, "Recent activity", 14.0,
                        theme.ink);

            // Recent activity entries
            if (recent_apps_.empty()) {
                canvas.text(menu.x + 20.0, menu.y + 236.0, "No recent activity",
                            12.0, theme.mute);
            } else {
                double rec_y = menu.y + 224.0;
                for (const auto& item : recent_apps_) {
                    const Rect row{menu.x + 20.0, rec_y, 365.0, 38.0};
                    canvas.app_icon(item.icon, Rect{row.x, row.y + 6.0, 26.0, 26.0});
                    const std::string line = item.name + " | Launched";
                    canvas.text(row.x + 38.0, row.y + 24.0, line, 13.0, theme.ink);

                    if (canvas.input().pointer_released &&
                        row.contains(canvas.input().x, canvas.input().y)) {
                        launch_by_id(item.id, item.name, item.icon);
                        return;
                    }
                    rec_y += 40.0;
                }
            }
        } else {
            // "Desktop - Home - All Apps" view (node 218:107)
            const Rect back_rect{menu.x + 16.0, menu.y + 18.0, 26.0, 26.0};
            canvas.icon("chevron-left.svg",
                        Rect{back_rect.x + 4.0, back_rect.y + 4.0, 18.0, 18.0},
                        theme.ink);

            if (canvas.input().pointer_released &&
                back_rect.contains(canvas.input().x, canvas.input().y)) {
                all_apps_ = false;
                request_frame();
            }

            canvas.text(menu.x + 46.0, menu.y + 36.0, "Apps", 14.0, theme.ink);
            canvas.text(menu.x + 84.0, menu.y + 36.0, "| All apps", 14.0, theme.mute);

            // Scrollable list of ALL installed apps without hardcoded limits
            const Rect clip_rect{menu.x + 10.0, menu.y + 50.0, 385.0, 390.0};
            const double total_h = static_cast<double>(apps_.size()) * 42.0;
            const double max_scroll = std::max(0.0, total_h - clip_rect.h);

            if (menu.contains(canvas.input().x, canvas.input().y) &&
                canvas.input().scroll_y != 0.0) {
                scroll_offset_ =
                    std::clamp(scroll_offset_ - canvas.input().scroll_y * 18.0,
                               0.0, max_scroll);
                request_frame();
            }

            canvas.push_clip(clip_rect);
            double row_y = clip_rect.y + 6.0 - scroll_offset_;
            for (const auto& app : apps_) {
                const Rect row{menu.x + 20.0, row_y, 365.0, 38.0};
                if (row.y + row.h >= clip_rect.y && row.y <= clip_rect.y + clip_rect.h) {
                    canvas.app_icon(app.icon, Rect{row.x, row.y + 6.0, 26.0, 26.0});
                    canvas.text(row.x + 38.0, row.y + 24.0, app.name, 14.0, theme.ink);
                    if (!app.installed) {
                        canvas.text(row.x + 200.0, row.y + 24.0, "Not installed",
                                    11.0, theme.mute);
                    }
                    if (canvas.input().pointer_released &&
                        row.contains(canvas.input().x, canvas.input().y) &&
                        app.installed) {
                        launch(app);
                        canvas.pop_clip();
                        return;
                    }
                }
                row_y += 42.0;
            }
            canvas.pop_clip();
        }

        // Account Options at bottom (385x40 at x: 10, y: 450 per SPEC.md)
        const Rect acct{menu.x + 10.0, menu.y + 450.0, 385.0, 40.0};
        canvas.fill(acct, Color::rgb(0xFFFFFF).with_alpha(217), 4.0);

        canvas.app_icon("avatar.svg", Rect{acct.x + 8.0, acct.y + 8.0, 24.0, 24.0});
        const std::string username = get_username();
        canvas.text(acct.x + 38.0, acct.y + 25.0, username, 14.0, theme.ink);

        canvas.icon("refresh.svg",
                    Rect{acct.x + acct.w - 54.0, acct.y + 11.0, 18.0, 18.0},
                    theme.ink);
        canvas.icon("power.svg",
                    Rect{acct.x + acct.w - 28.0, acct.y + 11.0, 18.0, 18.0},
                    theme.ink);
    }

    // Renders the 405x500 Control Center panel matching Figma node 218:24.
    void draw_control_center(Canvas& canvas) {
        const auto& theme = canvas.theme();
        const Color glass_tint = Color::rgb(0xFFFFFF).with_alpha(166);

        const double cc_w = 405.0;
        const double cc_h = 500.0;
        const double base_y = taskbar_.y - 15.0 - cc_h;
        const double y =
            base_y +
            (1.0 - finn::ui::ease_out_cubic(control_center_anim_.value)) * 12.0;

        // Centered horizontally above the Right Dock (866 + 58 - 202.5 = 721.5 on 1280).
        const double x = right_dock_.x + right_dock_.w / 2.0 - cc_w / 2.0;

        control_center_rect_ = Rect{x, base_y, cc_w, cc_h};
        const Rect cc{x, y, cc_w, cc_h};

        // Panel container glass background
        canvas.glass(cc, 8.0, glass_tint, 15.0);

        // Row 1: Wi-Fi and Bluetooth tiles (y: 10, h: 40, w: 188 per SPEC.md)
        // Disabled visually as no system networking daemon (finnd) exists.
        const Rect wifi_tile{cc.x + 10.0, cc.y + 10.0, 188.0, 40.0};
        const Rect bt_tile{cc.x + 207.0, cc.y + 10.0, 188.0, 40.0};

        canvas.fill(wifi_tile, Color::rgb(0xFFFFFF).with_alpha(180), 4.14);
        canvas.fill(Rect{wifi_tile.x + 8.0, wifi_tile.y + 8.0, 24.0, 24.0},
                    Color::rgb(0x007AFF), 12.0);
        canvas.icon("wifi-02.svg",
                    Rect{wifi_tile.x + 10.0, wifi_tile.y + 10.0, 20.0, 20.0},
                    Color::rgb(0xFFFFFF));
        canvas.text(wifi_tile.x + 38.0, wifi_tile.y + 19.0, "Wi-Fi", 12.0, theme.ink);
        canvas.text(wifi_tile.x + 38.0, wifi_tile.y + 32.0, "Unavailable", 10.0,
                    theme.mute);

        canvas.fill(bt_tile, Color::rgb(0xFFFFFF).with_alpha(180), 4.14);
        canvas.fill(Rect{bt_tile.x + 8.0, bt_tile.y + 8.0, 24.0, 24.0},
                    Color::rgb(0x8E8E93), 12.0);
        canvas.icon("bluetooth.svg",
                    Rect{bt_tile.x + 12.0, bt_tile.y + 12.0, 16.0, 16.0},
                    Color::rgb(0xFFFFFF));
        canvas.text(bt_tile.x + 38.0, bt_tile.y + 19.0, "Bluetooth", 12.0, theme.ink);
        canvas.text(bt_tile.x + 38.0, bt_tile.y + 32.0, "Off", 10.0, theme.mute);

        // Row 2: Brightness Slider (w: 345, h: 35) & Dark/Light Toggle (w: 35, h: 35)
        const Rect bright_rect{cc.x + 10.0, cc.y + 60.0, 345.0, 35.0};
        const Rect dark_toggle_rect{cc.x + 360.0, cc.y + 60.0, 35.0, 35.0};

        // Brightness slider is drawn disabled (dimmed track, refuses drag)
        canvas.fill(bright_rect, Color::rgb(0x8FA3BA).with_alpha(150), 17.5);
        canvas.icon("sun-01.svg",
                    Rect{bright_rect.x + 8.0, bright_rect.y + 6.0, 23.0, 23.0},
                    Color::rgb(0x000000));
        canvas.icon("sun-01.svg",
                    Rect{bright_rect.x + bright_rect.w - 31.0,
                         bright_rect.y + 6.0, 23.0, 23.0},
                    Color::rgb(0x000000));

        // Dark/Light toggle IS LIVE and toggles the appearance setting
        canvas.fill(dark_toggle_rect, Color::rgb(0xFFFFFF), 17.5);
        canvas.icon(dark_ ? "moon.svg" : "sun-01.svg",
                    Rect{dark_toggle_rect.x + 6.0, dark_toggle_rect.y + 6.0, 23.0,
                         23.0},
                    Color::rgb(0x000000));

        if (canvas.input().pointer_released &&
            dark_toggle_rect.contains(canvas.input().x, canvas.input().y)) {
            dark_ = !dark_;
            write_dark_setting(dark_);
            request_frame();
        }

        // Row 3: Volume Slider (w: 385, h: 35 at y: 105 per SPEC.md)
        // Disabled visually as no sound daemon backend exists.
        const Rect vol_rect{cc.x + 10.0, cc.y + 105.0, 385.0, 35.0};
        canvas.fill(vol_rect, Color::rgb(0x8FA3BA).with_alpha(150), 17.5);
        canvas.fill(Rect{vol_rect.x, vol_rect.y, 192.5, vol_rect.h},
                    Color::rgb(0xFFFFFF), 17.5);
        canvas.icon("volume-02.svg",
                    Rect{vol_rect.x + 10.0, vol_rect.y + 6.0, 23.0, 23.0},
                    Color::rgb(0x000000));

        // Bottom Row: Account / Battery / Settings footer (w: 385, h: 40 at y: 450)
        const Rect footer{cc.x + 10.0, cc.y + 450.0, 385.0, 40.0};
        canvas.fill(footer, Color::rgb(0xFFFFFF).with_alpha(217), 4.14);

        canvas.icon("battery.svg",
                    Rect{footer.x + 9.0, footer.y + 9.0, 22.0, 22.0}, theme.ink);
        canvas.icon("edit-01.svg",
                    Rect{footer.x + footer.w - 54.0, footer.y + 11.0, 18.0, 18.0},
                    theme.mute);

        const Rect sett_icon_rect{footer.x + footer.w - 28.0, footer.y + 11.0, 18.0,
                                  18.0};
        canvas.icon("settings-02.svg", sett_icon_rect, theme.ink);

        if (canvas.input().pointer_released &&
            sett_icon_rect.contains(canvas.input().x, canvas.input().y)) {
            close_control_center();
            launch_by_id("settings", "Settings", "settings-app.svg");
        }
    }

    finn::SessionClient client_;
    std::vector<finn::SessionApp> apps_;
    std::vector<RecentApp> recent_apps_;

    Rect taskbar_;
    Rect left_dock_;
    Rect time_weather_;
    Rect right_dock_;

    Rect menu_rect_;
    Rect control_center_rect_;

    bool menu_open_ = false;
    bool menu_visible_ = false;
    bool all_apps_ = false;
    double scroll_offset_ = 0.0;

    bool control_center_open_ = false;
    bool control_center_visible_ = false;

    bool dark_ = false;
    std::string active_app_ = "settings";

    finn::ui::Animation dock_in_;
    finn::ui::Animation menu_anim_;
    finn::ui::Animation control_center_anim_;

    std::string error_;
    std::string session_error_;
    std::chrono::steady_clock::time_point error_until_{};
    std::chrono::steady_clock::time_point next_refresh_{};
};

}  // namespace

int main() {
    Shell shell;
    return shell.run();
}
