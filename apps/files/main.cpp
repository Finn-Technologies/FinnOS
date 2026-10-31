// Finn Files file manager application.
// Provides directory navigation, item sizing, and basic file inspection
// within the user's home directory per Figma window specification.

#include <finn/ui.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

using finn::ui::App;
using finn::ui::Canvas;
using finn::ui::Color;
using finn::ui::Rect;

struct Entry {
    std::string name;
    bool directory = false;
    std::uintmax_t size = 0;
};

struct Place {
    std::string name;
    std::string icon;
    std::filesystem::path path;
};

std::string format_size(std::uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024ull * 1024ull * 1024ull) {
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    }
    return std::to_string(bytes / (1024ull * 1024ull * 1024ull)) + " GB";
}

class Files final : public App {
public:
    Files() : App("Finn Files", 900, 500, "files") {
        const char* home = std::getenv("HOME");
        home_path_ = home != nullptr && *home != '\0' ? std::filesystem::path(home)
                                                      : std::filesystem::path("/home/finn");
        path_ = home_path_;
        load_places();
        refresh();
        const char* col_env = std::getenv("FINN_FILES_COLLAPSED");
        if (col_env != nullptr && std::string_view(col_env) == "1") {
            sidebar_open_ = false;
        }
    }

    void draw(Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const double win_w = canvas.width();
        const double win_h = canvas.height();

        const double sidebar_w = sidebar_open_ ? 200.0 : 0.0;
        const double content_x = sidebar_w;
        const double content_w = win_w - sidebar_w;

        // 1. Content background card drawn first
        const Rect content_rect{content_x, 0, content_w, win_h};
        canvas.fill(content_rect, theme.tile, sidebar_open_ ? 8 : 0);

        // 2. Sidebar on the left (translucent glass per Figma specification)
        if (sidebar_open_) {
            const Rect sidebar_rect{0, 0, sidebar_w, win_h};
            const Color tint = theme.bone.with_alpha(theme.dark ? 214 : 178);
            canvas.glass(sidebar_rect, 8, tint, 15);
            canvas.line(content_x, 0, content_x, win_h, theme.hair, 1);

            // Sidebar toggle button to collapse (x: sidebar_w - 36, y: 12)
            if (canvas.icon_button("sidebar_toggle", Rect{sidebar_w - 36, 12, 24, 24},
                                   "Sidebar_Toggle.svg", theme.ink)) {
                sidebar_open_ = false;
                request_frame();
            }

            // Places header
            canvas.text(18, 72, "PLACES", 10, theme.mute);

            double py = 84;
            for (const auto& place : places_) {
                const bool selected = (path_ == place.path);
                if (canvas.sidebar_item("place:" + place.name, Rect{12, py, sidebar_w - 24, 32},
                                        place.icon, place.name, selected)) {
                    path_ = place.path;
                    refresh();
                    request_frame();
                }
                py += 36;
            }
        } else {
            // When collapsed, expand toggle button sits next to top-left App Name area
            if (canvas.icon_button("sidebar_toggle", Rect{100, 12, 24, 24},
                                   "Sidebar_Toggle.svg", theme.ink)) {
                sidebar_open_ = true;
                request_frame();
            }
        }

        // 3. Navigation Toolbar
        const double tb_top = 10;
        double bx = sidebar_open_ ? (content_x + 18.0) : 138.0;

        const bool can_go_back = path_.has_parent_path() && path_ != "/";
        if (canvas.icon_button("back", Rect{bx, tb_top, 28, 28}, "chevron-left.svg",
                               can_go_back ? theme.ink : theme.mute, can_go_back)) {
            path_ = path_.parent_path();
            refresh();
            request_frame();
        }
        bx += 34;

        if (canvas.icon_button("home", Rect{bx, tb_top, 28, 28}, "home-09.svg", theme.ink)) {
            path_ = home_path_;
            refresh();
            request_frame();
        }
        bx += 34;

        if (canvas.icon_button("refresh", Rect{bx, tb_top, 28, 28}, "refresh.svg", theme.ink)) {
            refresh();
            request_frame();
        }
        bx += 38;

        // Path breadcrumb text
        canvas.push_clip(Rect{bx, tb_top, std::max(20.0, content_w - (bx - content_x) - 100), 28});
        canvas.text(bx, tb_top + 19, path_.string(), 13, theme.ink);
        canvas.pop_clip();

        canvas.line(content_x, 46, content_x + content_w, 46, theme.hair, 1);

        // 4. File items list
        const Rect list_area{content_x + 12, 52, content_w - 24, win_h - 82};
        canvas.push_clip(list_area);
        const double row_h = 42;
        const double max_scroll = std::max(0.0, entries_.size() * row_h - list_area.h);
        scroll_ = std::clamp(scroll_ + canvas.input().scroll_y * 36.0, 0.0, max_scroll);
        double y = list_area.y - scroll_;

        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const Entry& entry = entries_[i];
            const Rect row{list_area.x, y, list_area.w, row_h - 2};
            if (row.y + row.h >= list_area.y && row.y <= list_area.y + list_area.h) {
                const std::string id = "entry:" + std::to_string(i) + ":" + entry.name;
                const std::string detail = entry.directory ? "Folder" : format_size(entry.size);
                const std::string icon = entry.directory ? "folder.svg" : "edit-01.svg";
                if (canvas.list_item_icon(id, row, icon, entry.name, detail,
                                          selected_ == entry.name)) {
                    if (entry.directory) {
                        path_ /= entry.name;
                        refresh();
                        request_frame();
                    } else {
                        selected_ = entry.name;
                        request_frame();
                    }
                }
            }
            y += row_h;
        }

        if (entries_.empty()) {
            canvas.text(list_area.x + 16, list_area.y + 34,
                        error_.empty() ? "This folder is empty" : error_, 13,
                        theme.mute);
        }
        canvas.pop_clip();

        // 5. Status bar footer
        canvas.line(content_x, win_h - 26, content_x + content_w, win_h - 26, theme.hair, 1);
        canvas.text(content_x + 20, win_h - 10,
                    std::to_string(entries_.size()) + " items", 10, theme.mute);
        if (!selected_.empty()) {
            canvas.text(content_x + 100, win_h - 10, selected_, 10, theme.mute);
        }
    }

private:
    void load_places() {
        places_.clear();

        struct Candidate {
            const char* name;
            const char* icon;
            std::filesystem::path path;
        };

        const Candidate candidates[] = {
            {"Home", "home-09.svg", home_path_},
            {"Documents", "folder.svg", home_path_ / "Documents"},
            {"Downloads", "folder.svg", home_path_ / "Downloads"},
            {"Pictures", "folder.svg", home_path_ / "Pictures"},
            {"Music", "folder.svg", home_path_ / "Music"},
            {"Videos", "folder.svg", home_path_ / "Videos"},
            {"Desktop", "folder.svg", home_path_ / "Desktop"},
        };

        for (const auto& c : candidates) {
            struct stat st{};
            if (stat(c.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                places_.push_back({c.name, c.icon, c.path});
            }
        }
    }

    void refresh() {
        entries_.clear();
        error_.clear();
        selected_.clear();
        scroll_ = 0;
        std::error_code error;
        std::filesystem::directory_iterator iterator(path_, error);
        if (error) {
            error_ = "Cannot read " + path_.string();
            return;
        }
        for (const auto& item : iterator) {
            const std::string name = item.path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            Entry entry;
            entry.name = name;
            std::error_code kind_error;
            entry.directory = item.is_directory(kind_error);
            if (!entry.directory) {
                std::error_code size_error;
                entry.size = item.file_size(size_error);
            }
            entries_.push_back(std::move(entry));
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            if (a.directory != b.directory) return a.directory;
            return a.name < b.name;
        });
    }

    std::filesystem::path home_path_;
    std::filesystem::path path_;
    std::vector<Place> places_;
    std::vector<Entry> entries_;
    std::string error_;
    std::string selected_;
    double scroll_ = 0;
    bool sidebar_open_ = true;
};

}  // namespace

int main() {
    Files app;
    return app.run();
}
