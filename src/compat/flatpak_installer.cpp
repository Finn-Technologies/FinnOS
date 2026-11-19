#include "compat/flatpak_installer.hpp"

#include "compat/desktop_entry.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

std::string trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

std::string sanitize_remote_name(std::string_view stem) {
    std::string out;
    out.reserve(stem.size());
    for (char c : stem) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_') {
            out.push_back(static_cast<char>(std::tolower(uc)));
        } else if (c == ' ' || c == '.') {
            out.push_back('-');
        }
    }
    if (out.empty()) out = "custom-remote";
    return out;
}

}  // namespace

FlatpakFileKind detect_flatpak_file_kind(std::string_view path) {
    if (ends_with_ci(path, ".flatpakref")) return FlatpakFileKind::FlatpakRef;
    if (ends_with_ci(path, ".flatpakrepo")) return FlatpakFileKind::FlatpakRepo;
    if (ends_with_ci(path, ".flatpak")) return FlatpakFileKind::FlatpakBundle;
    return FlatpakFileKind::Unknown;
}

std::string flatpak_kind_label(FlatpakFileKind kind) {
    switch (kind) {
        case FlatpakFileKind::FlatpakRef: return "Flatpak Installer";
        case FlatpakFileKind::FlatpakBundle: return "Flatpak Bundle";
        case FlatpakFileKind::FlatpakRepo: return "Flatpak Repository";
        case FlatpakFileKind::Unknown: break;
    }
    return "Unknown";
}

FlatpakFileInfo parse_flatpak_descriptor(std::string_view content,
                                         FlatpakFileKind kind,
                                         const std::string& path) {
    FlatpakFileInfo info;
    info.kind = kind;
    info.path = path;

    if (kind != FlatpakFileKind::FlatpakRef && kind != FlatpakFileKind::FlatpakRepo) {
        info.error = "not a text Flatpak descriptor";
        return info;
    }

    const std::string expected_section =
        (kind == FlatpakFileKind::FlatpakRef) ? "[Flatpak Ref]" : "[Flatpak Repo]";
    std::istringstream in{std::string(content)};
    std::string line;
    bool in_section = false;
    bool saw_section = false;

    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            if (t == expected_section) {
                in_section = true;
                saw_section = true;
            } else {
                if (in_section) break;
                in_section = false;
            }
            continue;
        }
        if (!in_section) continue;
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(t.substr(0, eq));
        const std::string val = trim(t.substr(eq + 1));
        if (key == "Name") info.name = val;
        else if (key == "Title") info.title = val;
        else if (key == "Url") info.url = val;
        else if (key == "RuntimeRepo") info.runtime_repo = val;
    }

    if (!saw_section) {
        info.error = "missing " + expected_section + " header";
        return info;
    }
    if (kind == FlatpakFileKind::FlatpakRef) {
        if (info.name.empty() && info.url.empty()) {
            info.error = "Flatpak Ref missing Name and Url";
            return info;
        }
        if (info.title.empty()) info.title = info.name;
    } else {
        if (info.url.empty()) {
            info.error = "Flatpak Repo missing Url";
            return info;
        }
        if (info.name.empty()) {
            const std::string stem = path.empty() ? std::string{} : fs::path(path).stem().string();
            info.name = sanitize_remote_name(stem.empty() ? info.title : stem);
        }
        if (info.title.empty()) info.title = info.name;
    }

    info.valid = true;
    return info;
}

FlatpakFileInfo inspect_flatpak_file(const std::string& path) {
    FlatpakFileInfo info;
    info.path = path;
    info.kind = detect_flatpak_file_kind(path);
    if (info.kind == FlatpakFileKind::Unknown) {
        info.error = "unsupported Flatpak file extension";
        return info;
    }

    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        info.error = "file does not exist: " + path;
        return info;
    }

    if (info.kind == FlatpakFileKind::FlatpakBundle) {
        const auto sz = fs::file_size(path, ec);
        if (ec || sz == 0) {
            info.error = "empty Flatpak bundle";
            return info;
        }
        info.name = fs::path(path).stem().string();
        info.title = fs::path(path).filename().string();
        info.valid = true;
        return info;
    }

    std::ifstream in(path);
    if (!in) {
        info.error = "unable to read file: " + path;
        return info;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse_flatpak_descriptor(buf.str(), info.kind, path);
}

FlatpakInstallPlan build_flatpak_install_plan(const std::string& path, bool check_binary) {
    FlatpakInstallPlan plan;
    const FlatpakFileInfo info = inspect_flatpak_file(path);
    plan.kind = info.kind;
    if (!info.valid) {
        plan.error = info.error;
        return plan;
    }

    if (check_binary && !executable_exists("flatpak")) {
        plan.error = "flatpak is not installed on this system";
        return plan;
    }

    plan.display_name = info.title.empty() ? info.name : info.title;
    switch (info.kind) {
        case FlatpakFileKind::FlatpakRef:
            plan.action_label = "Install Flatpak";
            plan.argv = {"flatpak", "install", "-y", "--user", "--from", path};
            break;
        case FlatpakFileKind::FlatpakBundle:
            plan.action_label = "Install Bundle";
            plan.argv = {"flatpak", "install", "-y", "--user", path};
            break;
        case FlatpakFileKind::FlatpakRepo:
            plan.action_label = "Add Remote";
            plan.argv = {"flatpak", "remote-add", "--user", "--if-not-exists",
                         "--from", info.name, path};
            break;
        case FlatpakFileKind::Unknown:
            plan.error = "unsupported Flatpak file";
            return plan;
    }

    plan.ok = true;
    return plan;
}

}  // namespace finn::compat
