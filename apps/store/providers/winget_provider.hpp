// Windows winget-pkgs catalog provider for Finn Store.
// Parses winget.run API v2 JSON search results and microsoft/winget-pkgs YAML
// installer manifests (PackageIdentifier, InstallerUrl, InstallerSha256,
// InstallerType) for verified download -> finn-win installation.

#ifndef FINN_STORE_WINGET_PROVIDER_HPP
#define FINN_STORE_WINGET_PROVIDER_HPP

#include "store_types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace finn::store {

inline constexpr const char* kWingetRunApiBase = "https://api.winget.run/v2";

struct WingetInstallerManifest {
    bool valid = false;
    std::string package_id;
    std::string version;
    std::string installer_type;  // "exe", "msi", "inno", "nullsoft", "portable"
    std::string architecture;    // "x64", "x86", "arm64"
    std::string installer_url;
    std::string sha256;
    std::string silent_switches;
    std::string error;
};

// Parses winget.run /v2/packages?query=... JSON into StoreAppItem entries.
std::vector<StoreAppItem> parse_winget_search_json(std::string_view json);

// Parses a microsoft/winget-pkgs installer YAML manifest, preferring x64.
WingetInstallerManifest parse_winget_installer_yaml(std::string_view yaml);

}  // namespace finn::store

#endif  // FINN_STORE_WINGET_PROVIDER_HPP
