// Native .finnapp bundle inspector, per-user installer and uninstaller.
// Pure C++20: installs a .finnapp directory bundle into ~/.local/share/finn/apps/
// with a .manifest entry and Freedesktop .desktop file, and removes both cleanly.

#ifndef FINN_COMPAT_FINNAPP_INSTALLER_HPP
#define FINN_COMPAT_FINNAPP_INSTALLER_HPP

#include <string>
#include <vector>

namespace finn::compat {

struct FinnAppMeta {
    bool valid = false;
    std::string id;
    std::string name;
    std::string version;
    std::string exec_rel;
    std::string icon_rel;
    std::string category = "System";
    std::string note;
    std::string bundle_path;
    std::string error;
};

struct FinnAppInstallResult {
    bool ok = false;
    FinnAppMeta meta;
    std::string install_dir;
    std::string manifest_path;
    std::string desktop_path;
    std::string error;
};

bool is_finnapp_bundle(const std::string& path);
FinnAppMeta inspect_finnapp(const std::string& bundle_path);

FinnAppInstallResult install_finnapp(const std::string& bundle_path,
                                     const std::string& apps_root = {},
                                     const std::string& desktop_dir = {});

bool uninstall_finnapp(const std::string& app_id,
                       const std::string& apps_root = {},
                       const std::string& desktop_dir = {},
                       std::string* error = nullptr);

}  // namespace finn::compat

#endif  // FINN_COMPAT_FINNAPP_INSTALLER_HPP
