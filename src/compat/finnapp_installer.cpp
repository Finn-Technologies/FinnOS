#include "compat/finnapp_installer.hpp"

#include "common/paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

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

std::string trim(std::string_view s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

std::string slugify(std::string_view s) {
    std::string out;
    bool dash = false;
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
            dash = false;
        } else if (!dash && !out.empty()) {
            out.push_back('-');
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "finn-app" : out;
}

std::string default_apps_root() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/" + kUserAppsRel;
    }
    return "/tmp/finn-apps";
}

std::string default_desktop_dir() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.local/share/applications";
    }
    return "/tmp/finn-desktop";
}

}  // namespace

bool is_finnapp_bundle(const std::string& path) {
    if (!ends_with_ci(path, ".finnapp")) return false;
    std::error_code ec;
    return fs::is_directory(path, ec) &&
           fs::is_regular_file(fs::path(path) / "manifest.conf", ec);
}

FinnAppMeta inspect_finnapp(const std::string& bundle_path) {
    FinnAppMeta m;
    m.bundle_path = bundle_path;
    std::error_code ec;
    if (!fs::is_directory(bundle_path, ec)) {
        m.error = "not a .finnapp directory: " + bundle_path;
        return m;
    }
    const fs::path mf = fs::path(bundle_path) / "manifest.conf";
    std::ifstream in(mf);
    if (!in) {
        m.error = "missing manifest.conf inside .finnapp bundle";
        return m;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(t.substr(0, eq));
        const std::string v = trim(t.substr(eq + 1));
        if (k == "id") m.id = v;
        else if (k == "name") m.name = v;
        else if (k == "version") m.version = v;
        else if (k == "exec") m.exec_rel = v;
        else if (k == "icon") m.icon_rel = v;
        else if (k == "category") m.category = v;
        else if (k == "note" || k == "comment") m.note = v;
    }
    if (m.name.empty()) m.name = fs::path(bundle_path).stem().string();
    if (m.id.empty()) m.id = slugify(m.name);
    if (m.exec_rel.empty()) {
        m.error = "manifest.conf missing exec= entry";
        return m;
    }
    if (!fs::is_regular_file(fs::path(bundle_path) / m.exec_rel, ec)) {
        m.error = "declared executable missing: " + m.exec_rel;
        return m;
    }
    m.valid = true;
    return m;
}

FinnAppInstallResult install_finnapp(const std::string& bundle_path,
                                     const std::string& apps_root,
                                     const std::string& desktop_dir) {
    FinnAppInstallResult res;
    res.meta = inspect_finnapp(bundle_path);
    if (!res.meta.valid) {
        res.error = res.meta.error;
        return res;
    }
    const std::string root = apps_root.empty() ? default_apps_root() : apps_root;
    const std::string desk = desktop_dir.empty() ? default_desktop_dir() : desktop_dir;
    const std::string safe_id = slugify(res.meta.id);

    std::error_code ec;
    fs::create_directories(root, ec);
    fs::create_directories(desk, ec);

    const fs::path dst_dir = fs::path(root) / (safe_id + ".finnapp");
    fs::remove_all(dst_dir, ec);
    ec.clear();
    fs::copy(bundle_path, dst_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        res.error = "failed to copy bundle: " + ec.message();
        return res;
    }

    const fs::path bin_path = dst_dir / res.meta.exec_rel;
    fs::permissions(bin_path,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);

    std::string icon_field = "app-grid.svg";
    if (!res.meta.icon_rel.empty() && fs::is_regular_file(dst_dir / res.meta.icon_rel, ec)) {
        icon_field = (dst_dir / res.meta.icon_rel).string();
    }

    const fs::path manifest_file = fs::path(root) / (safe_id + ".manifest");
    {
        std::ofstream mf(manifest_file, std::ios::trunc);
        if (!mf) {
            res.error = "failed to write manifest: " + manifest_file.string();
            return res;
        }
        mf << safe_id << "|" << res.meta.name << "|" << icon_field << "|"
           << bin_path.string() << "|" << res.meta.note << "|native|"
           << res.meta.category << "\n";
    }

    const fs::path desktop_file = fs::path(desk) / ("finn-app-" + safe_id + ".desktop");
    {
        std::ofstream df(desktop_file, std::ios::trunc);
        if (df) {
            df << "[Desktop Entry]\n"
               << "Type=Application\n"
               << "Name=" << res.meta.name << "\n"
               << "Comment=" << (res.meta.note.empty() ? "FinnOS native application" : res.meta.note) << "\n"
               << "Exec=" << bin_path.string() << "\n"
               << "Icon=" << icon_field << "\n"
               << "Categories=" << res.meta.category << ";\n";
        }
    }

    res.ok = true;
    res.install_dir = dst_dir.string();
    res.manifest_path = manifest_file.string();
    res.desktop_path = desktop_file.string();
    return res;
}

bool uninstall_finnapp(const std::string& app_id,
                       const std::string& apps_root,
                       const std::string& desktop_dir,
                       std::string* error) {
    if (app_id.empty()) {
        if (error) *error = "empty app id";
        return false;
    }
    const std::string root = apps_root.empty() ? default_apps_root() : apps_root;
    const std::string desk = desktop_dir.empty() ? default_desktop_dir() : desktop_dir;
    const std::string safe_id = slugify(app_id);

    std::error_code ec;
    const fs::path dst_dir = fs::path(root) / (safe_id + ".finnapp");
    const fs::path manifest_file = fs::path(root) / (safe_id + ".manifest");
    const fs::path desktop_file = fs::path(desk) / ("finn-app-" + safe_id + ".desktop");

    bool removed = false;
    if (fs::exists(dst_dir, ec)) {
        fs::remove_all(dst_dir, ec);
        removed = true;
    }
    if (fs::exists(manifest_file, ec)) {
        fs::remove(manifest_file, ec);
        removed = true;
    }
    if (fs::exists(desktop_file, ec)) {
        fs::remove(desktop_file, ec);
        removed = true;
    }
    if (!removed) {
        if (error) *error = "native .finnapp not found: " + safe_id;
        return false;
    }
    return true;
}

}  // namespace finn::compat
