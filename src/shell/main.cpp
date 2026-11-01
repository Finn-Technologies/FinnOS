// Layer-shell client rendering the FinnOS desktop shell, dock, taskbar,
// app launcher, search overlay, and control center. Manages dynamic input regions to keep
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

// Formats live date and time matching Figma frame: "Sat 31 Oct  12:48".
std::string time_text() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buf[64] = {};
    std::strftime(buf, sizeof buf, "%a %d %b  %H:%M", &local);
    return std::string(buf);
}

// Queries the current user's login account name via POSIX user identity.
std::string get_username() {
    std::string name = "finn";
    struct passwd* pw = getpwuid(getuid());
    if (pw != nullptr && pw->pw_name != nullptr && pw->pw_name[0] != '\0') {
        name = pw->pw_name;
    } else if (const char* user = std::getenv("USER"); user != nullptr && *user != '\0') {
        name = user;
    }
    if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') {
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
    }
    return name;
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

        if (const char* search = std::getenv("FINN_SHELL_SEARCH");
            search != nullptr && *search != '\0' && *search != '0') {
            open_search();
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
        if (search_anim_.running()) {
            search_anim_.update(dt);
            animating = true;
            if (!search_anim_.running() && !search_open_) search_visible_ = false;
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
                !home_buttons_pill_.contains(ptr_x, ptr_y)) {
                close_menu();
            }
            if (search_open_ && search_rect_.w > 0 &&
                !search_rect_.contains(ptr_x, ptr_y) &&
                !search_rec_rect_.contains(ptr_x, ptr_y) &&
                !home_buttons_pill_.contains(ptr_x, ptr_y)) {
                close_search();
            }
            if (control_center_open_ && control_center_rect_.w > 0 &&
                !control_center_rect_.contains(ptr_x, ptr_y) &&
                !hardware_pill_.contains(ptr_x, ptr_y)) {
                close_control_center();
            }
        }

        const bool m_visible = menu_open_ || menu_anim_.value > 0.001;
        menu_visible_ = m_visible;
        const bool s_visible = search_open_ || search_anim_.value > 0.001;
        search_visible_ = s_visible;
        const bool cc_visible = control_center_open_ || control_center_anim_.value > 0.001;
        control_center_visible_ = cc_visible;

        draw_dock(canvas, width, height);
        if (m_visible) draw_home_menu(canvas, width);
        if (s_visible) draw_search(canvas, width, height);
        if (cc_visible) draw_control_center(canvas);

        std::vector<Rect> region;
        if (m_visible || s_visible || cc_visible) {
            region.push_back(Rect{0, 0, width, height});
        } else {
            region.push_back(apps_pill_);
            region.push_back(home_buttons_pill_);
            region.push_back(hardware_pill_);
            region.push_back(time_pill_);
        }
        set_input_region(region);
        set_keyboard_mode((m_visible || s_visible || cc_visible)
                              ? finn::ui::KeyboardMode::on_demand
                              : finn::ui::KeyboardMode::none);

        if ((m_visible || s_visible || cc_visible) && canvas.input().key(finn::ui::Key::escape)) {
            close_menu();
            close_search();
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
        close_search();
        close_control_center();
        menu_open_ = true;
        menu_visible_ = true;
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

    void open_search() {
        refresh();
        close_menu();
        close_control_center();
        search_open_ = true;
        search_visible_ = true;
        search_anim_.to(1.0, finn::ui::kMotionStandard);
        request_frame();
    }

    void close_search() {
        if (!search_open_ && !search_anim_.running()) {
            search_visible_ = false;
            return;
        }
        search_open_ = false;
        search_anim_.to(0.0, finn::ui::kMotionFast);
        request_frame();
    }

    void open_control_center() {
        close_menu();
        close_search();
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
                if (recent_apps_.size() > 8) recent_apps_.pop_back();
            }
        }

        close_menu();
        close_search();
        close_control_center();
    }

    // Renders the 4-pill bottom taskbar matching Figma Desktop frame (2:285):
    // Apps (254x44), Home Buttons (130x44), Hardware Options (130x44), Time (154x44).
    void draw_dock(Canvas& canvas, double width, double height) {
        const auto& theme = canvas.theme();
        const Color glass_tint = dark_ ? Color::rgb(0x23262E).with_alpha(178)
                                        : Color::rgb(0xFFFFFF).with_alpha(178);
        const double lift = (1.0 - finn::ui::ease_out_cubic(dock_in_.value)) * 16.0;

        const double pill_h = 44.0;
        const double bottom_margin = 15.0;
        const double total_w = 713.0;
        const double y_base = height - pill_h - bottom_margin;
        const double y = y_base + lift;

        const double x_start = (width - total_w) / 2.0;
        apps_pill_ = Rect{x_start, y, 254.0, pill_h};
        home_buttons_pill_ = Rect{apps_pill_.x + 254.0 + 15.0, y, 130.0, pill_h};
        hardware_pill_ = Rect{home_buttons_pill_.x + 130.0 + 15.0, y, 130.0, pill_h};
        time_pill_ = Rect{hardware_pill_.x + 130.0 + 15.0, y, 154.0, pill_h};

        // Render glass blur background for all four pills (radius 12, blur 35)
        canvas.glass(apps_pill_, 12.0, glass_tint, 35.0);
        canvas.glass(home_buttons_pill_, 12.0, glass_tint, 35.0);
        canvas.glass(hardware_pill_, 12.0, glass_tint, 35.0);
        canvas.glass(time_pill_, 12.0, glass_tint, 35.0);

        // --- 1. Apps Pill: 6 app slots (32x32, r=8, gap=10, pad=6) ---
        struct SlotApp {
            std::string id;
            std::string name;
            std::string icon;
        };
        const SlotApp dock_apps[6] = {
            {"files", "Finn Files", "files.svg"},
            {"settings", "Settings", "settings-app.svg"},
            {"arc", "Arc", "arc.svg"},
            {"store", "Finn Store", "store.svg"},
            {"browser", "Browser", "browser.svg"},
            {"library", "Library", "library.svg"},
        };

        const double px = canvas.input().x;
        const double py = canvas.input().y;

        for (int i = 0; i < 6; ++i) {
            const double sx = apps_pill_.x + 6.0 + i * (32.0 + 10.0);
            const double sy = apps_pill_.y + 6.0;
            const Rect slot_rect{sx, sy, 32.0, 32.0};

            const Color slot_bg = dark_ ? Color::rgb(0x3B4048).with_alpha(150)
                                        : Color::rgb(0xADADAD).with_alpha(150);
            canvas.fill(slot_rect, slot_bg, 8.0);
            canvas.app_icon(dock_apps[i].icon, Rect{slot_rect.x + 3.0, slot_rect.y + 3.0, 26.0, 26.0});

            // Active indicator bar
            if (active_app_ == dock_apps[i].id) {
                canvas.fill(Rect{slot_rect.x + 8.0, apps_pill_.y + 40.0, 16.0, 2.0},
                            Color::rgb(0x006FFF), 1.0);
            }

            if (canvas.input().pointer_released && slot_rect.contains(px, py)) {
                launch_by_id(dock_apps[i].id, dock_apps[i].name, dock_apps[i].icon);
            }
        }

        // --- 2. Home Buttons Pill: Search (28x28), Home (28x28), Open Windows (28x28) ---
        const Rect search_btn{home_buttons_pill_.x + 8.0, home_buttons_pill_.y + 8.0, 28.0, 28.0};
        const Rect home_btn{home_buttons_pill_.x + 51.0, home_buttons_pill_.y + 8.0, 28.0, 28.0};
        const Rect win_btn{home_buttons_pill_.x + 94.0, home_buttons_pill_.y + 8.0, 28.0, 28.0};

        if (search_open_) {
            canvas.fill(Rect{home_buttons_pill_.x + 4.0, home_buttons_pill_.y + 4.0, 36.0, 36.0},
                        dark_ ? Color::rgb(0x3B4048) : Color::rgb(0xFFFFFF), 8.0);
        }
        if (menu_open_) {
            canvas.fill(Rect{home_buttons_pill_.x + 47.0, home_buttons_pill_.y + 4.0, 36.0, 36.0},
                        dark_ ? Color::rgb(0x3B4048) : Color::rgb(0xFFFFFF), 8.0);
        }

        canvas.icon("search-01.svg", search_btn, theme.ink);
        canvas.icon("home-09.svg", home_btn, theme.ink);
        canvas.icon("view-open-windows.svg", win_btn, theme.ink);

        if (canvas.input().pointer_released) {
            if (search_btn.contains(px, py)) {
                if (search_open_) close_search(); else open_search();
            } else if (home_btn.contains(px, py)) {
                if (menu_open_) close_menu(); else open_menu();
            } else if (win_btn.contains(px, py)) {
                active_app_ = (active_app_ == "files") ? "settings" : "files";
                request_frame();
            }
        }

        // --- 3. Hardware Options Pill: Volume, Wi-Fi, Battery (each 28x28, gap=15) ---
        if (control_center_open_) {
            canvas.fill(Rect{hardware_pill_.x + 3.0, hardware_pill_.y + 3.0, 124.0, 38.0},
                        dark_ ? Color::rgb(0x3B4048) : Color::rgb(0xFFFFFF), 9.0);
        }
        canvas.icon("volume-02.svg", Rect{hardware_pill_.x + 8.0, hardware_pill_.y + 8.0, 28.0, 28.0}, theme.ink);
        canvas.icon("wifi-02.svg", Rect{hardware_pill_.x + 51.0, hardware_pill_.y + 8.0, 28.0, 28.0}, theme.ink);
        canvas.icon("battery.svg", Rect{hardware_pill_.x + 94.0, hardware_pill_.y + 8.0, 28.0, 28.0}, theme.ink);

        if (canvas.input().pointer_released && hardware_pill_.contains(px, py)) {
            if (control_center_open_) close_control_center(); else open_control_center();
        }

        // --- 4. Time Pill: Live formatted date and time ---
        const std::string clk = time_text();
        canvas.text_centered(time_pill_, clk, 13.0, theme.ink);

        if (!error_.empty()) {
            canvas.text(apps_pill_.x, apps_pill_.y - 10.0, error_, 10.0, theme.mute);
        }
    }

    // Renders the 585x460 Home Menu popover matching Figma frame (4:24).
    void draw_home_menu(Canvas& canvas, double width) {
        const auto& theme = canvas.theme();
        const Color glass_tint = dark_ ? Color::rgb(0x23262E).with_alpha(178)
                                        : Color::rgb(0xFFFFFF).with_alpha(178);

        const double menu_w = 585.0;
        const double menu_h = 460.0;
        const double base_y = apps_pill_.y - 15.0 - menu_h;
        const double y = base_y +
                         (1.0 - finn::ui::ease_out_cubic(menu_anim_.value)) * 12.0;
        const double x = (width - menu_w) / 2.0;

        menu_rect_ = Rect{x, base_y, menu_w, menu_h};
        const Rect menu{x, y, menu_w, menu_h};

        canvas.glass(menu, 12.0, glass_tint, 35.0);

        const double px = canvas.input().x;
        const double py = canvas.input().y;

        // Top-Left: "Your favorite apps"
        canvas.text(menu.x + 18.0, menu.y + 24.0, "Your favorite apps", 12.0, theme.mute);

        const struct FavApp {
            std::string id;
            std::string name;
            std::string icon;
        } favs[10] = {
            {"files", "Files", "files.svg"},
            {"settings", "Settings", "settings-app.svg"},
            {"arc", "Arc", "arc.svg"},
            {"terminal", "Terminal", "store.svg"},
            {"notes", "Notes", "library.svg"},
            {"browser", "Browser", "browser.svg"},
            {"music", "Music", "native-apps.svg"},
            {"code", "Code", "app-grid.svg"},
            {"calc", "Calculator", "windows.svg"},
            {"store", "Store", "store.svg"}
        };

        for (int i = 0; i < 10; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const double ax = menu.x + 18.0 + col * 140.0;
            const double ay = menu.y + 42.0 + row * 42.0;
            const Rect item_rect{ax, ay, 130.0, 32.0};

            const Color icon_bg = dark_ ? Color::rgb(0x3B4048).with_alpha(150)
                                        : Color::rgb(0xADADAD).with_alpha(150);
            canvas.fill(Rect{item_rect.x, item_rect.y, 32.0, 32.0}, icon_bg, 8.0);
            canvas.app_icon(favs[i].icon, Rect{item_rect.x + 4.0, item_rect.y + 4.0, 24.0, 24.0});
            canvas.text(item_rect.x + 40.0, item_rect.y + 20.0, favs[i].name, 12.0, theme.ink);

            if (canvas.input().pointer_released && item_rect.contains(px, py)) {
                launch_by_id(favs[i].id, favs[i].name, favs[i].icon);
                return;
            }
        }

        // Vertical divider
        canvas.line(menu.x + 292.0, menu.y + 40.0, menu.x + 292.0, menu.y + 245.0, theme.hair, 1.0);

        // Top-Right: "All Apps" categories
        canvas.text(menu.x + 310.0, menu.y + 24.0, "All Apps", 12.0, theme.mute);

        const char* categories[6] = {
            "Productivity", "System", "Media",
            "Utilities", "Internet", "Other"
        };
        for (int i = 0; i < 6; ++i) {
            const int col = i % 3;
            const int row = i / 3;
            const double cx = menu.x + 310.0 + col * 88.0;
            const double cy = menu.y + 42.0 + row * 105.0;
            const Rect cat_box{cx, cy, 78.0, 78.0};

            const Color folder_bg = dark_ ? Color::rgb(0x3B4048).with_alpha(140)
                                          : Color::rgb(0xFFFFFF).with_alpha(150);
            canvas.fill(cat_box, folder_bg, 12.0);

            // Folder mini icon preview
            const Color mini_bg = dark_ ? Color::rgb(0x4F5562) : Color::rgb(0xADADAD);
            canvas.fill(Rect{cat_box.x + 8.0, cat_box.y + 8.0, 28.0, 28.0}, mini_bg, 6.0);
            canvas.fill(Rect{cat_box.x + 42.0, cat_box.y + 8.0, 28.0, 28.0}, mini_bg, 6.0);
            canvas.fill(Rect{cat_box.x + 8.0, cat_box.y + 42.0, 28.0, 28.0}, mini_bg, 6.0);
            canvas.fill(Rect{cat_box.x + 42.0, cat_box.y + 42.0, 12.0, 12.0}, mini_bg, 3.0);
            canvas.fill(Rect{cat_box.x + 58.0, cat_box.y + 42.0, 12.0, 12.0}, mini_bg, 3.0);
            canvas.fill(Rect{cat_box.x + 42.0, cat_box.y + 58.0, 12.0, 12.0}, mini_bg, 3.0);
            canvas.fill(Rect{cat_box.x + 58.0, cat_box.y + 58.0, 12.0, 12.0}, mini_bg, 3.0);

            canvas.text(cat_box.x + 2.0, cat_box.y + 92.0, categories[i], 10.0, theme.ink);

            if (canvas.input().pointer_released && cat_box.contains(px, py)) {
                if (i == 1) launch_by_id("settings", "Settings", "settings-app.svg");
                else launch_by_id("files", "Files", "files.svg");
                return;
            }
        }

        // Section: "Recent activity"
        canvas.text(menu.x + 18.0, menu.y + 270.0, "Recent activity", 12.0, theme.mute);

        const struct RecActivity {
            std::string title;
            std::string sub;
            std::string icon;
        } rec_items[8] = {
            {"Documents", "2m ago", "files.svg"},
            {"Downloads", "15m ago", "files.svg"},
            {"Display", "1h ago", "settings-app.svg"},
            {"Arc", "3h ago", "arc.svg"},
            {"Notes", "Yesterday", "library.svg"},
            {"Network", "Yesterday", "settings-app.svg"},
            {"Storage", "2d ago", "settings-app.svg"},
            {"Projects", "3d ago", "files.svg"}
        };

        for (int i = 0; i < 8; ++i) {
            const int col = i % 4;
            const int row = i / 4;
            const double rx = menu.x + 18.0 + col * 138.0;
            const double ry = menu.y + 288.0 + row * 42.0;
            const Rect row_rect{rx, ry, 132.0, 32.0};

            const Color icon_bg = dark_ ? Color::rgb(0x3B4048).with_alpha(150)
                                        : Color::rgb(0xADADAD).with_alpha(150);
            canvas.fill(Rect{row_rect.x, row_rect.y, 32.0, 32.0}, icon_bg, 8.0);
            canvas.app_icon(rec_items[i].icon, Rect{row_rect.x + 4.0, row_rect.y + 4.0, 24.0, 24.0});
            canvas.text(row_rect.x + 38.0, row_rect.y + 14.0, rec_items[i].title, 11.0, theme.ink);
            canvas.text(row_rect.x + 38.0, row_rect.y + 26.0, rec_items[i].sub, 9.0, theme.mute);

            if (canvas.input().pointer_released && row_rect.contains(px, py)) {
                if (rec_items[i].icon == "settings-app.svg") {
                    launch_by_id("settings", "Settings", "settings-app.svg");
                } else {
                    launch_by_id("files", "Files", "files.svg");
                }
                return;
            }
        }

        // Bottom Bar: Avatar, Username, Restart, Power (585x50 at y+410)
        const double bar_y = menu.y + 410.0;
        canvas.line(menu.x + 15.0, bar_y, menu.x + menu_w - 15.0, bar_y, theme.hair, 1.0);

        // User profile avatar (blue circle 30x30 with user glyph)
        const Rect avatar_rect{menu.x + 18.0, bar_y + 10.0, 30.0, 30.0};
        canvas.fill(avatar_rect, Color::rgb(0x1F4FFF), 15.0);
        canvas.icon("avatar.svg", Rect{avatar_rect.x + 5.0, avatar_rect.y + 5.0, 20.0, 20.0}, Color::rgb(0xFFFFFF));

        const std::string username = get_username();
        canvas.text(avatar_rect.x + 38.0, bar_y + 30.0, username, 14.0, theme.ink);

        // Restart and Power buttons
        const Rect restart_rect{menu.x + menu_w - 74.0, bar_y + 14.0, 22.0, 22.0};
        const Rect power_rect{menu.x + menu_w - 38.0, bar_y + 14.0, 22.0, 22.0};
        canvas.icon("refresh.svg", restart_rect, theme.ink);
        canvas.icon("power.svg", power_rect, theme.ink);

        if (canvas.input().pointer_released) {
            if (restart_rect.contains(px, py)) {
                refresh();
                close_menu();
            } else if (power_rect.contains(px, py)) {
                close_menu();
            }
        }
    }

    // Renders the Search Overlay matching Figma frame (4:226).
    void draw_search(Canvas& canvas, double width, double height) {
        const auto& theme = canvas.theme();
        const Color glass_tint = dark_ ? Color::rgb(0x23262E).with_alpha(178)
                                        : Color::rgb(0xFFFFFF).with_alpha(178);

        const double search_w = 300.0;
        const double search_h = 44.0;
        const double base_y = std::max(60.0, height * 0.28);
        const double y = base_y +
                         (1.0 - finn::ui::ease_out_cubic(search_anim_.value)) * 12.0;
        const double x = (width - search_w) / 2.0;

        search_rect_ = Rect{x, base_y, search_w, search_h};
        const Rect bar{x, y, search_w, search_h};
        canvas.glass(bar, 12.0, glass_tint, 35.0);

        canvas.icon("search-01.svg", Rect{bar.x + 8.0, bar.y + 8.0, 28.0, 28.0}, theme.ink);
        if (search_query_.empty()) {
            canvas.text(bar.x + 44.0, bar.y + 27.0, "Search or ask anything", 13.0, theme.mute);
        } else {
            canvas.text(bar.x + 44.0, bar.y + 27.0, search_query_, 13.0, theme.ink);
        }

        // Recommendations Card (350x228, centered)
        const double rec_w = 350.0;
        const double rec_h = 228.0;
        const double rec_x = (width - rec_w) / 2.0;
        const double rec_y = bar.y + search_h + 15.0;

        search_rec_rect_ = Rect{rec_x, base_y + search_h + 15.0, rec_w, rec_h};
        const Rect rec{rec_x, rec_y, rec_w, rec_h};
        canvas.glass(rec, 12.0, glass_tint, 35.0);

        const double px = canvas.input().x;
        const double py = canvas.input().y;

        // Recommendations Header
        canvas.text(rec.x + 18.0, rec.y + 24.0, "Recommendations", 12.0, theme.mute);

        const struct RecApp {
            std::string id;
            std::string name;
            std::string icon;
        } rec_apps[5] = {
            {"files", "Files", "files.svg"},
            {"settings", "Settings", "settings-app.svg"},
            {"arc", "Arc", "arc.svg"},
            {"terminal", "Terminal", "store.svg"},
            {"notes", "Notes", "library.svg"}
        };

        for (int i = 0; i < 5; ++i) {
            const double ax = rec.x + 18.0 + i * 65.0;
            const double ay = rec.y + 38.0;
            const Rect slot_rect{ax, ay, 32.0, 32.0};

            const Color icon_bg = dark_ ? Color::rgb(0x3B4048).with_alpha(150)
                                        : Color::rgb(0xADADAD).with_alpha(150);
            canvas.fill(slot_rect, icon_bg, 8.0);
            canvas.app_icon(rec_apps[i].icon, Rect{slot_rect.x + 4.0, slot_rect.y + 4.0, 24.0, 24.0});
            canvas.text(slot_rect.x - 2.0, slot_rect.y + 44.0, rec_apps[i].name, 10.0, theme.ink);

            if (canvas.input().pointer_released &&
                Rect{slot_rect.x, slot_rect.y, 48.0, 48.0}.contains(px, py)) {
                launch_by_id(rec_apps[i].id, rec_apps[i].name, rec_apps[i].icon);
                return;
            }
        }

        // Recently asked Header
        canvas.text(rec.x + 18.0, rec.y + 118.0, "Recently asked", 12.0, theme.mute);

        const struct QueryChip {
            std::string label;
            std::string launch_id;
        } queries[4] = {
            {"Finn Files", "files"},
            {"Display settings", "settings"},
            {"Arc Browser", "arc"},
            {"System info", "settings"}
        };

        for (int i = 0; i < 4; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const double qx = rec.x + 18.0 + col * 160.0;
            const double qy = rec.y + 134.0 + row * 40.0;
            const Rect chip{qx, qy, 146.0, 32.0};

            const Color chip_bg = dark_ ? Color::rgb(0x3B4048).with_alpha(140)
                                        : Color::rgb(0xFFFFFF).with_alpha(150);
            canvas.fill(chip, chip_bg, 8.0);
            canvas.icon("search-01.svg", Rect{chip.x + 6.0, chip.y + 6.0, 20.0, 20.0}, theme.mute);
            canvas.text(chip.x + 30.0, chip.y + 20.0, queries[i].label, 11.0, theme.ink);

            if (canvas.input().pointer_released && chip.contains(px, py)) {
                launch_by_id(queries[i].launch_id);
                return;
            }
        }
    }

    // Renders the 444x300 Control Center panel matching Figma frame (11:19).
    void draw_control_center(Canvas& canvas) {
        const auto& theme = canvas.theme();
        const Color glass_tint = dark_ ? Color::rgb(0x23262E).with_alpha(178)
                                        : Color::rgb(0xFFFFFF).with_alpha(178);

        const double cc_w = 444.0;
        const double cc_h = 300.0;
        const double base_y = apps_pill_.y - 15.0 - cc_h;
        const double y = base_y +
                         (1.0 - finn::ui::ease_out_cubic(control_center_anim_.value)) * 12.0;

        // Right-aligned with the taskbar group (apps_pill_.x + 713 - 444)
        const double x = apps_pill_.x + 713.0 - cc_w;

        control_center_rect_ = Rect{x, base_y, cc_w, cc_h};
        const Rect cc{x, y, cc_w, cc_h};

        canvas.glass(cc, 12.0, glass_tint, 35.0);

        const Color tile_bg = dark_ ? Color::rgb(0x3B4048).with_alpha(150)
                                    : Color::rgb(0xFFFFFF).with_alpha(170);

        // --- 1. Top-Left: Wi-Fi Card (207x42, r=6) ---
        const Rect wifi_card{cc.x + 10.0, cc.y + 10.0, 207.0, 42.0};
        canvas.fill(wifi_card, tile_bg, 6.0);
        canvas.fill(Rect{wifi_card.x + 5.0, wifi_card.y + 5.0, 32.0, 32.0}, Color::rgb(0x006FFF), 16.0);
        canvas.icon("wifi-02.svg", Rect{wifi_card.x + 11.0, wifi_card.y + 11.0, 20.0, 20.0}, Color::rgb(0xFFFFFF));
        canvas.text(wifi_card.x + 44.0, wifi_card.y + 19.0, "Wi-Fi", 12.0, theme.ink);
        canvas.text(wifi_card.x + 44.0, wifi_card.y + 32.0, "Home Wi-Fi", 10.0, theme.mute);

        // --- 2. Middle-Left: Bluetooth Card (207x42, r=6) ---
        const Rect bt_card{cc.x + 10.0, cc.y + 58.0, 207.0, 42.0};
        canvas.fill(bt_card, tile_bg, 6.0);
        canvas.fill(Rect{bt_card.x + 5.0, bt_card.y + 5.0, 32.0, 32.0}, Color::rgb(0x8B9099), 16.0);
        canvas.icon("bluetooth.svg", Rect{bt_card.x + 13.0, bt_card.y + 13.0, 16.0, 16.0}, Color::rgb(0xFFFFFF));
        canvas.text(bt_card.x + 44.0, bt_card.y + 19.0, "Bluetooth", 12.0, theme.ink);
        canvas.text(bt_card.x + 44.0, bt_card.y + 32.0, "Off", 10.0, theme.mute);

        // --- 3. Top-Right: Media Control Card (211x90, r=6) ---
        const Rect media_card{cc.x + 223.0, cc.y + 10.0, 211.0, 90.0};
        canvas.fill(media_card, tile_bg, 6.0);

        // Album artwork
        const Rect art_rect{media_card.x + 6.0, media_card.y + 6.0, 78.0, 78.0};
        canvas.fill(art_rect, dark_ ? Color::rgb(0x4F5562) : Color::rgb(0xCCCCCC), 6.0);
        canvas.icon("native-apps.svg", Rect{art_rect.x + 24.0, art_rect.y + 24.0, 30.0, 30.0}, Color::rgb(0xFFFFFF));

        canvas.text(media_card.x + 92.0, media_card.y + 24.0, "Song", 12.0, theme.ink);
        canvas.text(media_card.x + 92.0, media_card.y + 38.0, "Author", 10.0, theme.mute);

        // Progress bar and timestamp
        const Rect prog_bg{media_card.x + 92.0, media_card.y + 56.0, 103.0, 3.0};
        canvas.fill(prog_bg, dark_ ? Color::rgb(0x555A64) : Color::rgb(0xD0D0D0), 1.5);
        canvas.fill(Rect{prog_bg.x, prog_bg.y, 35.0, 3.0}, Color::rgb(0x006FFF), 1.5);
        canvas.text(prog_bg.x, prog_bg.y + 16.0, "00:56", 9.0, theme.mute);

        // --- 4. Bottom Area: Brightness & Volume Sliders + Dark Appearance Toggle ---
        const Rect controls_card{cc.x + 10.0, cc.y + 108.0, 424.0, 180.0};
        canvas.fill(controls_card, tile_bg, 8.0);

        // Display Brightness Slider
        canvas.icon("sun-01.svg", Rect{controls_card.x + 12.0, controls_card.y + 18.0, 22.0, 22.0}, theme.ink);
        canvas.slider("cc_brightness", Rect{controls_card.x + 44.0, controls_card.y + 17.0, 360.0, 24.0},
                      brightness_, 0.0, 1.0);

        // Volume Slider
        canvas.icon("volume-02.svg", Rect{controls_card.x + 12.0, controls_card.y + 68.0, 22.0, 22.0}, theme.ink);
        canvas.slider("cc_volume", Rect{controls_card.x + 44.0, controls_card.y + 67.0, 360.0, 24.0},
                      volume_, 0.0, 1.0);

        // Dark Appearance Toggle
        canvas.icon(dark_ ? "moon.svg" : "sun-01.svg",
                    Rect{controls_card.x + 12.0, controls_card.y + 120.0, 22.0, 22.0}, theme.ink);
        canvas.text(controls_card.x + 44.0, controls_card.y + 135.0,
                    dark_ ? "Dark Appearance" : "Light Appearance", 13.0, theme.ink);

        bool toggle_dark = dark_;
        if (canvas.toggle("cc_dark_mode", Rect{controls_card.x + 356.0, controls_card.y + 118.0, 48.0, 26.0},
                          "", toggle_dark)) {
            dark_ = toggle_dark;
            write_dark_setting(dark_);
            request_frame();
        }
    }

    finn::SessionClient client_;
    std::vector<finn::SessionApp> apps_;
    std::vector<RecentApp> recent_apps_;

    Rect apps_pill_;
    Rect home_buttons_pill_;
    Rect hardware_pill_;
    Rect time_pill_;

    Rect menu_rect_;
    Rect search_rect_;
    Rect search_rec_rect_;
    Rect control_center_rect_;

    bool menu_open_ = false;
    bool menu_visible_ = false;

    bool search_open_ = false;
    bool search_visible_ = false;
    std::string search_query_;

    bool control_center_open_ = false;
    bool control_center_visible_ = false;

    bool dark_ = false;
    std::string active_app_ = "settings";

    double volume_ = 0.65;
    double brightness_ = 0.80;

    finn::ui::Animation dock_in_;
    finn::ui::Animation menu_anim_;
    finn::ui::Animation search_anim_;
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
