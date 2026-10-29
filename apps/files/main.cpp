#include <finn/ui.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

using finn::ui::App;
using finn::ui::Canvas;
using finn::ui::Rect;

struct Entry {
    std::string name;
    bool directory = false;
    std::uintmax_t size = 0;
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
    Files() : App("Finn Files", 920, 620) {
        const char* home = std::getenv("HOME");
        path_ = home != nullptr && *home != '\0' ? std::filesystem::path(home)
                                                  : std::filesystem::path("/home/finn");
        refresh();
    }

    void draw(Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const Rect content = content_rect();
        canvas.fill(content, theme.tile, 0);

        const double top = content.y + 18;
        double x = content.x + 18;
        if (canvas.icon_button("back", Rect{x, top, 34, 34}, "chevron-left.svg",
                               path_.has_parent_path() ? theme.ink : theme.mute) &&
            path_.has_parent_path()) {
            path_ = path_.parent_path();
            refresh();
        }
        x += 42;
        if (canvas.icon_button("home", Rect{x, top, 34, 34}, "home-09.svg",
                               theme.ink)) {
            const char* home = std::getenv("HOME");
            path_ = home != nullptr && *home != '\0' ? home : "/home/finn";
            refresh();
        }
        x += 42;

        canvas.push_clip(Rect{x, top, content.w - (x - content.x) - 140, 34});
        canvas.text(x, top + 22, path_.string(), 14, theme.ink);
        canvas.pop_clip();

        if (canvas.icon_button("refresh", Rect{content.x + content.w - 52, top, 34, 34},
                               "refresh.svg", theme.ink)) {
            refresh();
        }
        canvas.line(content.x + 18, top + 44, content.x + content.w - 18,
                    top + 44, theme.hair, 1);

        const Rect list_area{content.x + 12, top + 54, content.w - 24,
                             content.h - (top - content.y) - 74};
        canvas.push_clip(list_area);
        const double row_h = 46;
        const double max_scroll = std::max(
            0.0, entries_.size() * row_h - list_area.h);
        scroll_ = std::clamp(scroll_ + canvas.input().scroll_y * 36.0, 0.0,
                             max_scroll);
        double y = list_area.y - scroll_;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const Entry& entry = entries_[i];
            const Rect row{list_area.x, y, list_area.w, row_h - 2};
            if (row.y + row.h >= list_area.y && row.y <= list_area.y + list_area.h) {
                const std::string id = "entry:" + std::to_string(i) + ":" + entry.name;
                const std::string detail =
                    entry.directory ? "Folder" : format_size(entry.size);
                const std::string icon = entry.directory ? "folder.svg" : "edit-01.svg";
                if (canvas.list_item_icon(id, row, icon, entry.name, detail,
                                          selected_ == entry.name)) {
                    if (entry.directory) {
                        path_ /= entry.name;
                        refresh();
                    } else {
                        selected_ = entry.name;
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

        canvas.text(content.x + 20, content.y + content.h - 18,
                    std::to_string(entries_.size()) + " items", 10, theme.mute);
        if (!selected_.empty()) {
            canvas.text(content.x + 100, content.y + content.h - 18,
                        selected_, 10, theme.mute);
        }
    }

private:
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
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a,
                                                        const Entry& b) {
            if (a.directory != b.directory) return a.directory;
            return a.name < b.name;
        });
    }

    std::filesystem::path path_;
    std::vector<Entry> entries_;
    std::string error_;
    std::string selected_;
    double scroll_ = 0;
};

}  // namespace

int main() {
    Files app;
    return app.run();
}
