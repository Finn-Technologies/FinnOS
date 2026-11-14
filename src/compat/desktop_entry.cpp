#include "compat/desktop_entry.hpp"

#include "common/paths.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

std::string trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::vector<std::string> split_semicolon(std::string_view value) {
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t end = value.find(';', start);
        const std::string item = trim(
            end == std::string_view::npos ? value.substr(start)
                                          : value.substr(start, end - start));
        if (!item.empty()) items.push_back(item);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return items;
}

bool contains_ci(const std::vector<std::string>& list, std::string_view needle) {
    const std::string target = ascii_lower(needle);
    for (const std::string& item : list) {
        if (ascii_lower(item) == target) return true;
    }
    return false;
}

std::string desktop_stem(const std::string& path) {
    fs::path p(path);
    return p.stem().string();
}

std::string normalize_category(const std::vector<std::string>& cats) {
    for (const std::string& c : cats) {
        const std::string low = ascii_lower(c);
        if (low == "network" || low == "webbrowser" || low == "email") return "Internet";
        if (low == "development" || low == "ide") return "Developer";
        if (low == "audiovideo" || low == "audio" || low == "video" ||
            low == "graphics" || low == "photography")
            return "Media";
        if (low == "office" || low == "texteditor" || low == "education")
            return "Productivity";
        if (low == "game") return "Games";
        if (low == "settings" || low == "system" || low == "monitor") return "System";
        if (low == "utility" || low == "accessories") return "Utilities";
    }
    return "Linux";
}

bool is_exec_file(const fs::path& p) {
    std::error_code ec;
    const fs::file_status st = fs::status(p, ec);
    if (ec || !fs::is_regular_file(st)) return false;
    return ::access(p.c_str(), X_OK) == 0;
}

std::string probe_icon_dir(const fs::path& dir, std::string_view icon_name) {
    static const char* kExts[] = {".svg", ".png", ".webp", ".xpm"};
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    for (const char* ext : kExts) {
        const fs::path direct = dir / (std::string(icon_name) + ext);
        if (fs::is_regular_file(direct, ec)) return direct.string();
    }
    const fs::path exact = dir / std::string(icon_name);
    if (fs::is_regular_file(exact, ec)) return exact.string();
    return {};
}

}  // namespace

bool executable_exists(std::string_view program) {
    if (program.empty()) return false;
    if (program.find('/') != std::string_view::npos) {
        return is_exec_file(fs::path(program));
    }
    const char* path_env = std::getenv("PATH");
    if (!path_env || !*path_env) path_env = "/usr/local/bin:/usr/bin:/bin";
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        if (is_exec_file(fs::path(dir) / std::string(program))) return true;
    }
    return false;
}

std::optional<DesktopEntry> parse_desktop_content(std::string_view content,
                                                  const std::string& path,
                                                  std::string_view source,
                                                  bool check_try_exec) {
    std::istringstream in{std::string(content)};
    std::string line;
    bool in_entry = false;
    bool saw_entry = false;
    std::string type;
    std::vector<std::string> only_show_in;
    std::vector<std::string> not_show_in;

    DesktopEntry entry;
    entry.desktop_file = path;
    entry.desktop_id = desktop_stem(path);
    entry.source = std::string(source);

    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            if (trimmed == "[Desktop Entry]") {
                in_entry = true;
                saw_entry = true;
            } else {
                // Stop reading on [Desktop Action ...] or secondary groups.
                if (in_entry) break;
                in_entry = false;
            }
            continue;
        }
        if (!in_entry) continue;

        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string val = trim(trimmed.substr(eq + 1));

        // Skip localized keys (e.g. Name[fr]) in favor of canonical unlocalized keys.
        if (key.find('[') != std::string::npos) continue;

        if (key == "Type") type = val;
        else if (key == "Name") entry.name = val;
        else if (key == "GenericName") entry.generic_name = val;
        else if (key == "Comment") entry.comment = val;
        else if (key == "Exec") entry.exec = val;
        else if (key == "TryExec") entry.try_exec = val;
        else if (key == "Icon") entry.icon = val;
        else if (key == "Categories") entry.categories = split_semicolon(val);
        else if (key == "MimeType") entry.mime_types = split_semicolon(val);
        else if (key == "Terminal") entry.terminal = (ascii_lower(val) == "true");
        else if (key == "NoDisplay") entry.no_display = (ascii_lower(val) == "true");
        else if (key == "Hidden") entry.hidden = (ascii_lower(val) == "true");
        else if (key == "OnlyShowIn") only_show_in = split_semicolon(val);
        else if (key == "NotShowIn") not_show_in = split_semicolon(val);
    }

    if (!saw_entry || type != "Application") return std::nullopt;
    if (entry.hidden || entry.no_display) return std::nullopt;
    if (entry.name.empty() || entry.exec.empty()) return std::nullopt;

    if (!not_show_in.empty() && contains_ci(not_show_in, "FinnOS")) {
        return std::nullopt;
    }
    if (!only_show_in.empty() && !contains_ci(only_show_in, "FinnOS")) {
        return std::nullopt;
    }
    if (check_try_exec && !entry.try_exec.empty() &&
        !executable_exists(entry.try_exec)) {
        return std::nullopt;
    }

    const std::string prefix = (source == "flatpak") ? "flatpak:" : "linux:";
    entry.id = prefix + entry.desktop_id;
    entry.category = normalize_category(entry.categories);
    return entry;
}

std::optional<DesktopEntry> parse_desktop_file(const std::string& path,
                                               std::string_view source,
                                               bool check_try_exec) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse_desktop_content(buf.str(), path, source, check_try_exec);
}

std::vector<std::string> expand_desktop_exec(std::string_view exec_line,
                                             std::string_view icon,
                                             std::string_view name,
                                             std::string_view desktop_file,
                                             std::string_view target_arg) {
    std::vector<std::string> raw_tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool have_token = false;

    for (std::size_t i = 0; i < exec_line.size(); ++i) {
        const char ch = exec_line[i];
        if (in_single) {
            if (ch == '\'') in_single = false;
            else current.push_back(ch);
            have_token = true;
            continue;
        }
        if (in_double) {
            if (ch == '"') {
                in_double = false;
            } else if (ch == '\\' && i + 1 < exec_line.size()) {
                current.push_back(exec_line[++i]);
            } else {
                current.push_back(ch);
            }
            have_token = true;
            continue;
        }
        if (ch == ' ' || ch == '\t') {
            if (have_token) {
                raw_tokens.push_back(std::move(current));
                current.clear();
                have_token = false;
            }
            continue;
        }
        have_token = true;
        if (ch == '\'') in_single = true;
        else if (ch == '"') in_double = true;
        else if (ch == '\\' && i + 1 < exec_line.size()) current.push_back(exec_line[++i]);
        else current.push_back(ch);
    }
    if (have_token) raw_tokens.push_back(std::move(current));

    std::vector<std::string> result;
    for (const std::string& tok : raw_tokens) {
        if (tok == "%f" || tok == "%F" || tok == "%u" || tok == "%U") {
            if (!target_arg.empty()) result.emplace_back(target_arg);
            continue;
        }
        if (tok == "%i") {
            if (!icon.empty()) {
                result.emplace_back("--icon");
                result.emplace_back(icon);
            }
            continue;
        }
        if (tok == "%c") {
            if (!name.empty()) result.emplace_back(name);
            continue;
        }
        if (tok == "%k") {
            if (!desktop_file.empty()) result.emplace_back(desktop_file);
            continue;
        }
        // Strip deprecated/unknown standalone field codes (%d, %D, %n, %N, %v, %m).
        if (tok.size() == 2 && tok[0] == '%' &&
            std::string_view("dDnNvm").find(tok[1]) != std::string_view::npos) {
            continue;
        }
        result.push_back(tok);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> default_desktop_dirs() {
    std::vector<std::pair<std::string, std::string>> dirs;
    if (const char* home = std::getenv("HOME"); home && *home) {
        dirs.emplace_back(std::string(home) + "/.local/share/applications", "desktop");
        dirs.emplace_back(std::string(home) + "/.local/share/flatpak/exports/share/applications",
                          "flatpak");
    }
    dirs.emplace_back("/var/lib/flatpak/exports/share/applications", "flatpak");
    dirs.emplace_back("/usr/local/share/applications", "desktop");
    dirs.emplace_back("/usr/share/applications", "desktop");
    return dirs;
}

std::vector<DesktopEntry> scan_desktop_dirs(
    const std::vector<std::pair<std::string, std::string>>& dirs,
    bool check_try_exec) {
    std::vector<DesktopEntry> out;
    std::unordered_set<std::string> seen;

    for (const auto& [dir, source] : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".desktop") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            const std::string key = std::string(source) + ":" + file.stem().string();
            if (!seen.insert(key).second) continue;
            auto parsed = parse_desktop_file(file.string(), source, check_try_exec);
            if (parsed.has_value()) out.push_back(std::move(*parsed));
        }
    }
    std::sort(out.begin(), out.end(), [](const DesktopEntry& a, const DesktopEntry& b) {
        return ascii_lower(a.name) < ascii_lower(b.name);
    });
    return out;
}

std::string resolve_icon_path(std::string_view icon_name,
                              const std::vector<std::string>& extra_dirs) {
    if (icon_name.empty()) return {};
    if (icon_name.front() == '/') {
        std::error_code ec;
        return fs::is_regular_file(fs::path(icon_name), ec) ? std::string(icon_name)
                                                             : std::string{};
    }

    for (const std::string& dir : extra_dirs) {
        if (const std::string hit = probe_icon_dir(dir, icon_name); !hit.empty()) return hit;
    }

    const fs::path finn_icons(kIconDir);
    for (const char* sub : {"apps", "shell"}) {
        if (const std::string hit = probe_icon_dir(finn_icons / sub, icon_name);
            !hit.empty()) {
            return hit;
        }
    }

    static const char* kThemeSubdirs[] = {
        "/usr/share/icons/hicolor/scalable/apps",
        "/usr/share/icons/hicolor/256x256/apps",
        "/usr/share/icons/hicolor/128x128/apps",
        "/usr/share/icons/hicolor/64x64/apps",
        "/usr/share/icons/hicolor/48x48/apps",
        "/var/lib/flatpak/exports/share/icons/hicolor/scalable/apps",
        "/var/lib/flatpak/exports/share/icons/hicolor/128x128/apps",
        "/usr/share/pixmaps",
    };
    for (const char* dir : kThemeSubdirs) {
        if (const std::string hit = probe_icon_dir(dir, icon_name); !hit.empty()) return hit;
    }
    return {};
}

}  // namespace finn::compat
