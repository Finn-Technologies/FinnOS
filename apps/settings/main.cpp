// FinnOS system Settings application.
// Manages desktop appearance preferences and displays operating system
// version information and runtime session details.

#include <finn/ui.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using finn::ui::App;
using finn::ui::Canvas;
using finn::ui::Rect;

std::filesystem::path user_settings_path() {
    const char* home = std::getenv("HOME");
    const std::filesystem::path base =
        home != nullptr && *home != '\0' ? home : "/home/finn";
    return base / ".config" / "finn" / "settings";
}

// Checks user-specific configuration first before falling back to system defaults,
// ensuring personal appearance preferences override OS defaults.
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

// Performs an atomic write using a temporary file and rename to avoid corrupting
// user settings if power or process termination occurs during disk flush.
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

class Settings final : public App {
public:
    Settings() : App("Settings", 880, 620), dark_(read_dark()) {}

    void draw(Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const Rect content = content_rect();
        canvas.fill(content, theme.tile, 0);

        double y = content.y + 54;
        canvas.text(content.x + 36, y, "Settings", 26, theme.ink);
        y += 48;

        canvas.text(content.x + 36, y, "Appearance", 15, theme.ink);
        y += 18;
        if (canvas.toggle("dark", Rect{content.x + 36, y, content.w - 72, 48},
                          "Dark appearance", dark_)) {
            if (!write_dark(dark_)) {
                error_ = "Could not write the setting";
            } else {
                error_.clear();
            }
            request_frame();
        }
        y += 62;
        canvas.text(content.x + 36, y,
                    error_.empty() ? "Apps and the shell pick this up on their next frame."
                                   : error_,
                    11, error_.empty() ? theme.mute : theme.dim);
        y += 44;

        canvas.text(content.x + 36, y, "About", 15, theme.ink);
        y += 20;
        canvas.line(content.x + 36, y, content.x + content.w - 36, y, theme.hair, 1);
        y += 28;

        const std::vector<std::pair<std::string, std::string>> rows = {
            {"Version", read_version()},
            {"Display", "finn-comp (wlroots 0.20)"},
            {"Session", "finn-session"},
            {"Access", "finn, one graphical session"},
        };
        for (const auto& [label, value] : rows) {
            canvas.text(content.x + 36, y, label, 12, theme.mute);
            canvas.text(content.x + 220, y, value, 12, theme.ink);
            y += 28;
        }
    }

private:
    bool dark_ = false;
    std::string error_;
};

}  // namespace

int main() {
    Settings app;
    return app.run();
}
