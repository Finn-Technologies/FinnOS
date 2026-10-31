// FinnOS system Settings application.
// Manages desktop appearance preferences and displays operating system
// version information and runtime session details per Figma specification.

#include <finn/ui.hpp>

#include <sys/statvfs.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using finn::ui::App;
using finn::ui::Canvas;
using finn::ui::Color;
using finn::ui::Rect;

std::filesystem::path user_settings_path() {
    const char* home = std::getenv("HOME");
    const std::filesystem::path base =
        home != nullptr && *home != '\0' ? home : "/home/finn";
    return base / ".config" / "finn" / "settings";
}

bool read_dark() {
    const std::filesystem::path user = user_settings_path();
    for (const std::filesystem::path& path :
         {user, std::filesystem::path("/var/lib/finn/settings")}) {
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line)) {
            if (line == "dark=1") return true;
            if (line == "dark=0") return false;
        }
    }
    return false;
}

bool write_dark(bool dark) {
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

std::string read_version() {
    std::ifstream file("/usr/share/finn/version");
    std::string version;
    std::getline(file, version);
    return version.empty() ? "unknown" : version;
}

std::string read_arch() {
    utsname buf{};
    if (uname(&buf) == 0) return buf.machine;
    return "unknown";
}

std::string read_memory() {
    std::ifstream file("/proc/meminfo");
    std::string line;
    unsigned long mem_total_kb = 0;
    while (std::getline(file, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::sscanf(line.c_str(), "MemTotal: %lu kB", &mem_total_kb);
            break;
        }
    }
    if (mem_total_kb > 0) {
        double gb = static_cast<double>(mem_total_kb) / (1024.0 * 1024.0);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f GB", gb);
        return buf;
    }
    return "3.8 GB";
}

std::string read_storage() {
    struct statvfs st{};
    if (statvfs("/", &st) == 0 && st.f_frsize > 0) {
        double total_gb = static_cast<double>(st.f_blocks) * st.f_frsize / (1024.0 * 1024.0 * 1024.0);
        double free_gb = static_cast<double>(st.f_bavail) * st.f_frsize / (1024.0 * 1024.0 * 1024.0);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.0fGB (%.0fGB free)", total_gb, free_gb);
        return buf;
    }
    return "31GB";
}

void draw_finn_logo(Canvas& canvas, double x, double y, Color color) {
    canvas.fill(Rect{x, y, 9, 9}, color, 2.7);
    canvas.fill(Rect{x + 11, y, 9, 9}, color, 4.5);
    canvas.fill(Rect{x + 2, y + 13, 5, 5}, color, 1);
    canvas.line(x + 4.5, y + 11, x + 4.5, y + 20, color, 1.5);
    canvas.line(x, y + 15.5, x + 9, y + 15.5, color, 1.5);
    canvas.fill(Rect{x + 12, y + 12, 7, 7}, color, 1.5);
}

enum class SettingsPage {
    Appearance,
    About
};

class Settings final : public App {
public:
    Settings() : App("Settings", 900, 500, "settings"), dark_(read_dark()) {
        const char* page_env = std::getenv("FINN_SETTINGS_PAGE");
        if (page_env != nullptr && std::string_view(page_env) == "about") {
            page_ = SettingsPage::About;
        }
        const char* col_env = std::getenv("FINN_SETTINGS_COLLAPSED");
        if (col_env != nullptr && std::string_view(col_env) == "1") {
            sidebar_open_ = false;
        }
    }

    void draw(Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const double win_w = canvas.width();
        const double win_h = canvas.height();

        const double sidebar_w = sidebar_open_ ? 250.0 : 0.0;
        const double content_x = sidebar_w;
        const double content_w = win_w - sidebar_w;

        // 1. Content background card (solid tile per Figma 186:3907) drawn first
        const Rect content_rect{content_x, 0, content_w, win_h};
        canvas.fill(content_rect, theme.tile, sidebar_open_ ? 8 : 0);

        // 2. Sidebar rendering
        if (sidebar_open_) {
            const Rect sidebar_rect{0, 0, sidebar_w, win_h};
            const Color tint = theme.bone.with_alpha(theme.dark ? 214 : 178);
            canvas.glass(sidebar_rect, 8, tint, 15);
            canvas.line(content_x, 0, content_x, win_h, theme.hair, 1);

            // Toggle button to collapse sidebar (top right of sidebar header, x: 214, y: 12)
            if (canvas.icon_button("sidebar_toggle", Rect{sidebar_w - 36, 12, 24, 24},
                                   "Sidebar_Toggle.svg", theme.ink)) {
                sidebar_open_ = false;
                request_frame();
            }

            // Section items (only sections that actually exist in v1: Appearance, About)
            canvas.text(18, 72, "SETTINGS", 10, theme.mute);

            if (canvas.sidebar_item("nav_appearance", Rect{12, 84, sidebar_w - 24, 34},
                                    "paint-brush.svg", "Appearance",
                                    page_ == SettingsPage::Appearance)) {
                page_ = SettingsPage::Appearance;
                request_frame();
            }

            if (canvas.sidebar_item("nav_about", Rect{12, 124, sidebar_w - 24, 34},
                                    "info.svg", "About",
                                    page_ == SettingsPage::About)) {
                page_ = SettingsPage::About;
                request_frame();
            }
        } else {
            // When collapsed, expand toggle button sits next to top-left App Name area
            if (canvas.icon_button("sidebar_toggle", Rect{92, 12, 24, 24},
                                   "Sidebar_Toggle.svg", theme.ink)) {
                sidebar_open_ = true;
                request_frame();
            }
        }

        // 3. Content page rendering
        const double page_head_y = sidebar_open_ ? 44.0 : 66.0;
        const double page_body_y = sidebar_open_ ? 70.0 : 94.0;

        if (page_ == SettingsPage::Appearance) {
            canvas.text(content_x + 36, page_head_y, "Appearance", 16, theme.ink);

            const Rect toggle_rect{content_x + 36, page_body_y,
                                   std::min(460.0, content_w - 72), 48};
            canvas.fill(toggle_rect, theme.tile_on.with_alpha(theme.dark ? 60 : 30), 8);
            canvas.stroke(toggle_rect, theme.hair, 8, 1);
            if (canvas.toggle("dark", toggle_rect.inset(10), "Dark appearance", dark_)) {
                if (!write_dark(dark_)) {
                    error_ = "Could not write the setting";
                } else {
                    error_.clear();
                }
                request_frame();
            }

            canvas.text(content_x + 36, page_body_y + 70,
                        error_.empty() ? "Apps and the shell pick this up on their next frame."
                                       : error_,
                        11, error_.empty() ? theme.mute : theme.dim);
        } else if (page_ == SettingsPage::About) {
            canvas.text(content_x + 20, page_head_y, "System Information", 16, theme.ink);

            // FinnOS Info Card (Figma node 186:3924: 305x160)
            const Rect info_card{content_x + 20, page_body_y, 280, 150};
            const Color card_bg = theme.dark ? Color::rgb(0x1F232D) : Color::rgb(0xF4FAF9);
            canvas.fill(info_card, card_bg, 6);
            canvas.stroke(info_card, theme.hair, 6, 1);

            draw_finn_logo(canvas, info_card.x + 32, info_card.y + 64, theme.ink);
            canvas.text(info_card.x + 65, info_card.y + 76, "FinnOS One", 16, theme.ink);
            canvas.text(info_card.x + 162, info_card.y + 76, "Beta", 16, theme.mute);

            // 4 Spec Cards (RAM, Storage, CPU Arch, Display)
            const double card_w = std::max(120.0, (content_w - 320 - 30) / 2.0);
            const double col1_x = content_x + 315;
            const double col2_x = col1_x + card_w + 10;
            const double row1_y = page_body_y;
            const double row2_y = page_body_y + 80;
            const double spec_h = 70;

            auto draw_spec = [&](Rect r, std::string_view label, std::string_view sub,
                                 const std::string& val) {
                canvas.fill(r, theme.tile_on.with_alpha(theme.dark ? 50 : 25), 4);
                canvas.stroke(r, theme.hair, 4, 1);
                canvas.text(r.x + 10, r.y + 18, label, 10, theme.mute);
                canvas.text(r.x + 10, r.y + 40, sub, 10, theme.mute);
                canvas.text(r.x + 10, r.y + 58, val, 12, theme.ink);
            };

            draw_spec(Rect{col1_x, row1_y, card_w, spec_h}, "RAM", "System Memory", read_memory());
            draw_spec(Rect{col2_x, row1_y, card_w, spec_h}, "Storage", "Root Filesystem", read_storage());
            draw_spec(Rect{col1_x, row2_y, card_w, spec_h}, "CPU", "Architecture", read_arch());
            draw_spec(Rect{col2_x, row2_y, card_w, spec_h}, "Display", "Compositor", "finn-comp (wlroots 0.20)");

            // Detail rows below spec cards
            double y = page_body_y + 175;
            canvas.line(content_x + 20, y, content_x + content_w - 20, y, theme.hair, 1);
            y += 24;

            const std::vector<std::pair<std::string, std::string>> rows = {
                {"OS Version", read_version()},
                {"Session", "finn-session"},
                {"Access", "finn, one graphical session"},
            };
            for (const auto& [label, value] : rows) {
                canvas.text(content_x + 20, y, label, 12, theme.mute);
                canvas.text(content_x + 160, y, value, 12, theme.ink);
                y += 26;
            }
        }
    }

private:
    bool dark_ = false;
    bool sidebar_open_ = true;
    SettingsPage page_ = SettingsPage::Appearance;
    std::string error_;
};

}  // namespace

int main() {
    Settings app;
    return app.run();
}
