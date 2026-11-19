#include "session/app_registry.hpp"

#include "common/paths.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/macos_runner.hpp"
#include "compat/wine_runner.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace finn {
namespace {

constexpr std::size_t kMaxCatalogueLine = 4096;

std::string trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

std::vector<std::string> split(std::string_view line, char sep) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t pos = line.find(sep, start);
        if (pos == std::string_view::npos) {
            fields.emplace_back(trim(line.substr(start)));
            break;
        }
        fields.emplace_back(trim(line.substr(start, pos - start)));
        start = pos + 1;
    }
    return fields;
}

std::string infer_native_category(std::string_view id) {
    if (id == "settings" || id == "monitor" || id == "store") return "System";
    if (id == "terminal") return "Developer";
    if (id == "notes" || id == "files") return "Productivity";
    if (id == "viewer") return "Media";
    return "System";
}

}  // namespace

const char* app_source_name(AppSource source) {
    switch (source) {
        case AppSource::kNative: return "native";
        case AppSource::kFlatpak: return "flatpak";
        case AppSource::kWinget: return "winget";
        case AppSource::kDesktop: return "desktop";
        case AppSource::kWindows: return "windows";
        case AppSource::kMacos: return "macos";
        case AppSource::kUnknown:
        default: return "unknown";
    }
}

AppSource parse_app_source(std::string_view text) {
    if (text.empty() || text == "native") return AppSource::kNative;
    if (text == "flatpak") return AppSource::kFlatpak;
    if (text == "winget") return AppSource::kWinget;
    if (text == "desktop" || text == "linux") return AppSource::kDesktop;
    if (text == "windows" || text == "wine") return AppSource::kWindows;
    if (text == "macos" || text == "darwin") return AppSource::kMacos;
    return AppSource::kUnknown;
}

bool App::launchable() const {
    if (command.empty()) return false;
    return source == AppSource::kNative || source == AppSource::kDesktop ||
           source == AppSource::kFlatpak || source == AppSource::kWindows ||
           source == AppSource::kMacos;
}

std::optional<AppRegistry> AppRegistry::load(const std::string& catalogue_path,
                                             const std::string& installed_path) {
    std::ifstream cat_in(catalogue_path);
    if (!cat_in) return std::nullopt;

    std::unordered_set<std::string> installed_ids;
    if (!installed_path.empty()) {
        std::ifstream inst_in(installed_path);
        std::string line;
        while (std::getline(inst_in, line)) {
            line = trim(line);
            if (!line.empty() && line[0] != '#') installed_ids.insert(line);
        }
    }

    AppRegistry reg;
    std::unordered_set<std::string> seen;
    std::string line;
    while (std::getline(cat_in, line)) {
        if (line.size() > kMaxCatalogueLine) continue;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        const std::vector<std::string> fields = split(trimmed, '|');
        if (fields.size() < 4) continue;
        if (fields[0].empty() || fields[1].empty()) continue;
        if (!seen.insert(fields[0]).second) continue;

        App app;
        app.id = fields[0];
        app.name = fields[1];
        app.icon = fields[2];
        app.command = fields[3];
        if (fields.size() >= 5) app.note = fields[4];
        app.source = fields.size() >= 6 ? parse_app_source(fields[5]) : AppSource::kNative;
        app.category = fields.size() >= 7 && !fields[6].empty()
                           ? fields[6]
                           : infer_native_category(app.id);
        app.installed = installed_ids.count(app.id) > 0;
        reg.apps_.push_back(std::move(app));
    }
    return reg;
}

AppRegistry AppRegistry::load_all(const std::string& catalogue_path,
                                  const std::string& installed_path) {
    auto base = load(catalogue_path, installed_path);
    AppRegistry reg = base.has_value() ? std::move(*base) : AppRegistry{};
    reg.merge_user_manifests();
    reg.merge_desktop_entries(true);
    reg.merge_windows_entries();
    reg.merge_macos_entries();
    return reg;
}

void AppRegistry::merge_user_manifests(const std::string& manifests_dir) {
    std::string dir = manifests_dir;
    if (dir.empty()) {
        if (const char* home = std::getenv("HOME"); home && *home) {
            dir = std::string(home) + "/" + kUserAppsRel;
        }
    }
    if (dir.empty()) return;

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".manifest") continue;
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() > kMaxCatalogueLine) continue;
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            const auto fields = split(t, '|');
            if (fields.size() < 4 || fields[0].empty() || fields[1].empty()) continue;
            if (find(fields[0]) != nullptr) continue;

            App app;
            app.id = fields[0];
            app.name = fields[1];
            app.icon = fields[2];
            app.command = fields[3];
            if (fields.size() >= 5) app.note = fields[4];
            app.category = (fields.size() >= 7 && !fields[6].empty()) ? fields[6]
                                                                      : "Developer";
            app.source = AppSource::kNative;
            app.installed = true;
            apps_.push_back(std::move(app));
        }
    }
}

void AppRegistry::merge_desktop_entries(bool check_try_exec) {
    const auto entries =
        compat::scan_desktop_dirs(compat::default_desktop_dirs(), check_try_exec);
    for (const auto& d : entries) {
        if (find(d.id) != nullptr || find(d.desktop_id) != nullptr) continue;
        App app;
        app.id = d.id;
        app.name = d.name;
        app.icon = compat::resolve_icon_path(d.icon);
        if (app.icon.empty()) {
            app.icon = d.icon.empty() ? "app-grid.svg" : d.icon;
        }
        app.command = d.exec;
        app.note = d.comment.empty() ? d.generic_name : d.comment;
        app.category = d.category;
        app.desktop_file = d.desktop_file;
        app.mime_types = d.mime_types;
        app.terminal = d.terminal;
        app.source = (d.source == "flatpak") ? AppSource::kFlatpak : AppSource::kDesktop;
        app.installed = true;
        apps_.push_back(std::move(app));
    }
}

void AppRegistry::merge_windows_entries(const std::string& windows_conf_path) {
    std::string conf = windows_conf_path;
    if (conf.empty()) {
        if (const char* home = std::getenv("HOME"); home && *home) {
            conf = std::string(home) + "/" + kUserWindowsConfRel;
        }
    }
    if (conf.empty()) return;
    const auto entries = compat::load_windows_apps(conf);
    for (const auto& w : entries) {
        if (find(w.id) != nullptr) continue;
        App app;
        app.id = w.id;
        app.name = w.name;
        app.icon = w.icon.empty() ? "windows.svg" : w.icon;
        app.command = w.exe_path;
        app.desktop_file = w.prefix;  // Stores optional prefix cleanly
        app.note = w.args.empty() ? "Windows 11 application" : w.args;
        app.category = "Windows";
        app.source = AppSource::kWindows;
        std::error_code ec;
        app.installed = fs::is_regular_file(w.exe_path, ec);
        apps_.push_back(std::move(app));
    }

    // Auto-discover installed Windows 11 apps from the finn-win prefix when initialized.
    if (const char* home = std::getenv("HOME"); home && *home) {
        const fs::path prefix = fs::path(home) / ".local" / "share" / "finn" / "wine";
        const fs::path sys32 = prefix / "drive_c" / "windows" / "system32";
        std::error_code ec;
        if (fs::is_directory(sys32, ec)) {
            struct BuiltinWinApp {
                const char* id;
                const char* name;
                const char* exe;
                const char* icon;
                const char* note;
            };
            static constexpr BuiltinWinApp kBuiltins[] = {
                {"win11-notepad", "Notepad (Win11)", "notepad.exe", "win-notepad.svg",
                 "Windows 11 Notepad via finn-win"},
                {"win11-winecfg", "Windows Settings", "winecfg.exe", "win-settings.svg",
                 "Windows 11 compatibility configuration"},
                {"win11-explorer", "Windows Explorer", "explorer.exe", "windows.svg",
                 "Windows 11 file explorer"},
                {"win11-cmd", "Command Prompt", "cmd.exe", "windows.svg",
                 "Windows 11 command interpreter"},
            };
            for (const auto& b : kBuiltins) {
                if (find(b.id) != nullptr) continue;
                const fs::path exe_path = sys32 / b.exe;
                if (!fs::is_regular_file(exe_path, ec)) continue;
                App app;
                app.id = b.id;
                app.name = b.name;
                app.icon = b.icon;
                app.command = exe_path.string();
                app.desktop_file = prefix.string();
                app.note = b.note;
                app.category = "Windows";
                app.source = AppSource::kWindows;
                app.installed = true;
                apps_.push_back(std::move(app));
            }
        }
    }
}

void AppRegistry::merge_macos_entries(const std::string& macos_apps_dir) {
    const auto bundles = compat::discover_macos_bundles(macos_apps_dir);
    for (const auto& b : bundles) {
        if (find(b.id) != nullptr) continue;
        App app;
        app.id = b.id;
        app.name = b.name;
        app.icon = b.icon_path.empty() ? "laptop.svg" : b.icon_path;
        app.command = b.bundle_path;
        app.note = b.version.empty() ? "macOS application via finn-darwin"
                                     : ("macOS application v" + b.version + " via finn-darwin");
        app.category = "macOS";
        app.source = AppSource::kMacos;
        app.installed = !b.executable_path.empty();
        apps_.push_back(std::move(app));
    }
}

const App* AppRegistry::find(std::string_view id) const {
    for (const App& app : apps_) {
        if (app.id == id) return &app;
    }
    return nullptr;
}

bool AppRegistry::installed(std::string_view id) const {
    const App* app = find(id);
    return app != nullptr && app->installed;
}

}  // namespace finn
