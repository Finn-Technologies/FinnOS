// Lightweight OpenHarmony 6.1 LTS (API Level 23) compatibility probe, command
// planner, and installed app discovery for FinnOS.
//
// Pure C++20 with zero zlib or GUI dependencies so finn-session, finn-shell,
// Finn Store, and Finn Files can link it directly.

#ifndef FINN_COMPAT_OHOS_RUNNER_HPP
#define FINN_COMPAT_OHOS_RUNNER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace finn::compat {

enum class OhosPackageType {
    Unknown,
    Hap,
    Hsp,
    AppPack,
};

struct OhosRuntimeStatus {
    std::string host_arch;
    std::string host_ohos_abi;
    int supported_api_version = 23;  // OpenHarmony 6.1 LTS
    std::string ohos_version = "6.1 LTS";
    bool finn_ohos_installed = false;
    std::string finn_ohos_bin;
    bool ready = false;
    std::string summary;
};

struct OhosAppEntry {
    std::string id;  // "ohos:<slug>"
    std::string name;
    std::string bundle_name;
    std::string version_name;
    std::uint32_t version_code = 0;
    int target_api_version = 0;
    int min_api_version = 0;
    std::string main_ability;
    std::string module_type;
    std::string install_dir;
    std::string package_path;
    std::string icon_path;
};

struct OhosCommandPlan {
    bool ok = false;
    std::vector<std::string> argv;
    std::string note;
    std::string error;
};

// Distinguishes OpenHarmony .hap/.hsp/.app ZIP archives (and HTTP(S) URLs) from
// macOS .app directories cleanly without requiring zlib.
OhosPackageType classify_ohos_package_path(const std::string& path);
bool is_ohos_package_path(const std::string& path);

// Probes host for the finn-ohos runtime binary and reports API 23 readiness.
OhosRuntimeStatus probe_ohos_runtime();

// Builds finn-ohos install, diagnose, and launch command plans when installed.
OhosCommandPlan build_ohos_install_plan(const std::string& package_path_or_url);
OhosCommandPlan build_ohos_diagnose_plan(const std::string& package_path);
OhosCommandPlan build_ohos_launch_plan(const std::string& bundle_or_package_path);

// Discovers installed OpenHarmony apps from ~/.local/share/finn/ohos/registry.conf
// (or an explicit registry_path override for unit tests).
std::vector<OhosAppEntry> discover_ohos_apps(const std::string& registry_path = {});

}  // namespace finn::compat

#endif
