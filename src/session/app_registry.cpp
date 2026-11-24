#include "session/app_registry.hpp"

#include "common/paths.hpp"
#include "compat/android_runner.hpp"
#include "compat/appimage_runner.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/macos_runner.hpp"
#include "compat/ohos_runner.hpp"
#include "compat/pe_icon.hpp"
#include "compat/windows_registry.hpp"
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

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string unquote(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\"");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\"");
    return std::string(s.substr(first, last - first + 1));
}

// A Windows path inside a prefix maps to drive_<letter>/... . We only accept a
// path when it actually resolves to a file or directory on disk, so a stale
// Uninstall key never produces a launch entry that cannot work.
std::string wine_path_to_host(const std::string& prefix, std::string_view win_path,
                              bool require_file) {
    const std::string p = unquote(win_path);
    if (p.size() < 3 || p[1] != ':') return {};
    const char drive = ascii_lower(std::string_view(&p[0], 1))[0];
    if (drive < 'a' || drive > 'z') return {};
    std::string rel;
    rel.reserve(p.size());
    for (std::size_t i = 2; i < p.size(); ++i) {
        rel.push_back(p[i] == '\\' ? '/' : p[i]);
    }
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    if (rel.empty()) return {};

    std::error_code ec;
    const fs::path host =
        fs::path(prefix) / (std::string("drive_") + drive) / rel;
    const bool present = require_file ? fs::is_regular_file(host, ec)
                                      : fs::is_directory(host, ec);
    return present ? host.string() : std::string{};
}

std::string slugify(std::string_view name) {
    std::string slug;
    slug.reserve(name.size());
    bool dash = false;
    for (char c : ascii_lower(name)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) {
            slug.push_back(c);
            dash = false;
        } else if (!dash && !slug.empty()) {
            slug.push_back('-');
            dash = true;
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "app";
    if (slug.size() > 64) slug.resize(64);
    return slug;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    return ascii_lower(s.substr(s.size() - suffix.size())) == ascii_lower(suffix);
}

// Resolve the executable an Uninstall key actually points at. The icon value is
// preferred (it commonly is "<path>,0"), then a single unambiguous executable
// in the install directory. When neither yields a real file we return empty:
// a launcher must never invent a command for a registry entry.
std::string resolve_uninstall_exe(const compat::UninstallEntry& entry,
                                  const std::string& prefix) {
    if (!entry.icon_path.empty()) {
        std::string icon = unquote(entry.icon_path);
        const std::size_t comma = icon.rfind(',');
        if (comma != std::string::npos &&
            icon.find_first_not_of("0123456789", comma + 1) == std::string::npos) {
            icon.resize(comma);
        }
        const std::string host = wine_path_to_host(prefix, icon, true);
        if (!host.empty() && ends_with_ci(host, ".exe")) return host;
    }

    std::string dir;
    if (!entry.install_location.empty()) {
        dir = wine_path_to_host(prefix, entry.install_location, false);
    }
    if (dir.empty() && !entry.uninstall_string.empty()) {
        const std::string s = unquote(entry.uninstall_string);
        const std::size_t close = s.find_first_of(" \t");
        const std::string candidate =
            close == std::string::npos ? s : s.substr(0, close);
        const std::string exe = wine_path_to_host(prefix, candidate, true);
        if (!exe.empty() && ends_with_ci(exe, ".exe")) return exe;
    }
    if (dir.empty()) return {};

    std::error_code ec;
    std::vector<fs::path> exes;
    for (const auto& it : fs::directory_iterator(dir, ec)) {
        if (!it.is_regular_file(ec)) continue;
        if (ends_with_ci(it.path().string(), ".exe")) exes.push_back(it.path());
    }
    if (exes.size() == 1) return exes.front().string();

    // Several executables: accept one only when its stem is a word of the
    // display name. Anything else stays unregistered rather than guessed.
    const std::string name_words = ascii_lower(entry.display_name);
    std::string match;
    for (const auto& exe : exes) {
        const std::string stem = ascii_lower(exe.stem().string());
        if (stem.size() < 3) continue;
        if (name_words.find(stem) != std::string::npos) {
            if (!match.empty()) return {};  // ambiguous
            match = exe.string();
        }
    }
    return match;
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
        case AppSource::kOhos: return "ohos";
        case AppSource::kAndroid: return "android";
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
    if (text == "ohos" || text == "openharmony") return AppSource::kOhos;
    if (text == "android" || text == "apk") return AppSource::kAndroid;
    return AppSource::kUnknown;
}

bool App::launchable() const {
    if (command.empty()) return false;
    return source == AppSource::kNative || source == AppSource::kDesktop ||
           source == AppSource::kFlatpak || source == AppSource::kWindows ||
           source == AppSource::kMacos || source == AppSource::kOhos ||
           source == AppSource::kAndroid;
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
    reg.merge_appimage_entries();
    reg.merge_windows_entries();
    reg.merge_macos_entries();
    reg.merge_ohos_entries();
    reg.merge_android_entries();
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

void AppRegistry::merge_appimage_entries(const std::vector<std::string>& dirs) {
    const auto candidates = compat::discover_appimages(
        dirs.empty() ? default_appimage_dirs() : dirs);
    for (const auto& candidate : candidates) {
        if (find(candidate.id) != nullptr) continue;
        App app;
        app.id = candidate.id;
        app.name = candidate.display_name;
        app.icon = "linux.svg";
        app.command = candidate.path;
        app.appimage_path = candidate.path;
        app.note = candidate.info.summary;
        app.category = "Linux";
        app.source = AppSource::kDesktop;  // A Linux app; Store shows "Linux".
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
        if (app.icon == "windows.svg") {
            const std::string extracted =
                compat::extract_and_cache_pe_icon(w.exe_path, slugify(w.id));
            if (!extracted.empty()) app.icon = extracted;
        }
        app.command = w.exe_path;
        app.desktop_file = w.prefix;  // Stores optional prefix cleanly
        app.note = "Windows application via finn-win";
        app.launch_args = w.args;
        app.category = "Windows";
        app.source = AppSource::kWindows;
        std::error_code ec;
        app.installed = fs::is_regular_file(w.exe_path, ec);
        apps_.push_back(std::move(app));
    }

    // Auto-discover installed Windows 11 apps from the finn-win prefix when initialized.
    if (const char* home = std::getenv("HOME"); home && *home) {
        const fs::path prefix = fs::path(home) / kWinePrefixRel;
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

    // Uninstall keys left by installed Windows programs are the other source
    // of launch entries. Only keys whose executable really exists in the prefix
    // become apps: a registry entry is a claim, not proof that a program is
    // still there.
    std::vector<std::string> prefixes;
    if (const char* home = std::getenv("HOME"); home && *home) {
        prefixes.push_back(std::string(home) + "/" + kWinePrefixRel);
    }
    for (const auto& w : entries) {
        if (!w.prefix.empty()) prefixes.push_back(w.prefix);
    }
    for (const auto& prefix : prefixes) {
        std::string reason;
        const auto uninstall = compat::collect_uninstall_entries_from_prefix(prefix, &reason);
        for (const auto& u : uninstall) {
            const std::string exe = resolve_uninstall_exe(u, prefix);
            if (exe.empty()) continue;
            const std::string label = u.display_name.empty() ? u.key : u.display_name;
            const std::string id = "win:" + slugify(label);
            if (find(id) != nullptr) continue;
            // Deduplicate against winemenubuilder .desktop entries that already
            // registered the same Windows app name or executable.
            bool dup = false;
            const std::string label_slug = slugify(label);
            for (const auto& existing : apps_) {
                if (existing.command == exe ||
                    (existing.source == AppSource::kDesktop &&
                     existing.desktop_file.find("/wine/") != std::string::npos &&
                     slugify(existing.name) == label_slug)) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            App app;
            app.id = id;
            app.name = label;
            const std::string extracted =
                compat::extract_and_cache_pe_icon(exe, label_slug);
            app.icon = extracted.empty() ? "windows.svg" : extracted;
            app.command = exe;
            app.desktop_file = prefix;
            app.note = u.publisher.empty()
                           ? (u.display_version.empty()
                                  ? "Windows application (Uninstall key)"
                                  : "Windows " + u.display_version)
                           : (u.publisher + (u.display_version.empty()
                                                 ? std::string{}
                                                 : " " + u.display_version));
            app.category = "Windows";
            app.source = AppSource::kWindows;
            app.installed = true;
            apps_.push_back(std::move(app));
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
        app.icon = b.icon_path.empty() ? "darwin.svg" : b.icon_path;
        app.command = b.bundle_path;
        app.note = b.version.empty() ? "macOS application via finn-darwin"
                                     : ("macOS application v" + b.version + " via finn-darwin");
        app.category = "macOS";
        app.source = AppSource::kMacos;
        app.installed = !b.executable_path.empty();
        apps_.push_back(std::move(app));
    }
}

void AppRegistry::merge_ohos_entries(const std::string& ohos_registry_path) {
    const auto entries = compat::discover_ohos_apps(ohos_registry_path);
    for (const auto& e : entries) {
        if (find(e.id) != nullptr) continue;
        App app;
        app.id = e.id;
        app.name = e.name;
        app.icon = e.icon_path.empty() ? "ohos.svg" : e.icon_path;
        app.command = e.bundle_name;
        app.note = e.version_name.empty()
                       ? "OpenHarmony 6.1 LTS app via finn-ohos"
                       : ("OpenHarmony 6.1 LTS v" + e.version_name + " (API " +
                          std::to_string(e.target_api_version) + ")");
        app.category = "OpenHarmony";
        app.source = AppSource::kOhos;
        app.installed = true;
        apps_.push_back(std::move(app));
    }
}

void AppRegistry::merge_android_entries(const std::string& android_registry_path) {
    const auto entries = compat::discover_android_apps(android_registry_path);
    for (const auto& e : entries) {
        if (find(e.id) != nullptr) continue;
        App app;
        app.id = e.id;
        app.name = e.name;
        app.icon = e.icon_path.empty() ? "android.svg" : e.icon_path;
        app.command = e.package_name;
        app.note = e.version_name.empty()
                       ? "Android app via finn-android (clean-room runtime)"
                       : ("Android v" + e.version_name + " (targetSdk " +
                          std::to_string(e.target_sdk) + ")");
        app.category = "Android";
        app.source = AppSource::kAndroid;
        app.installed = true;
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
