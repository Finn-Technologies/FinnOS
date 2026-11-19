// OpenHarmony 6.1 LTS (API Level 23) package inspector, HTTP/HTTPS downloader,
// installer (.hap/.hsp/.app), icon/native-lib extractor, registry, and
// pre-flight diagnostics engine for finn-ohos.

#ifndef FINN_OHOS_INSTALLER_HPP
#define FINN_OHOS_INSTALLER_HPP

#include "ohos/abc_runtime.hpp"
#include "ohos/ohos_format.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace finn::ohos {

struct OhosDiagnosticReport {
    bool valid = false;
    OhosPackageKind kind = OhosPackageKind::Unknown;
    std::string target_path;
    std::string host_cpu;
    std::string host_abi;
    OhosModuleManifest manifest;
    OhosPackInfo pack_info;
    OhosResourceIndexInfo resource_index;
    bool abc_present = false;
    bool abc_valid = false;
    bool abc_checksum_valid = false;
    std::string abc_version;
    std::size_t abc_class_count = 0;
    std::size_t abc_method_count = 0;
    std::vector<OhosNativeLibInfo> native_libs;
    std::vector<std::string> supported_kits;
    std::vector<std::string> unsupported_kits;   // known missing hardware/proprietary kits
    std::vector<std::string> unknown_kits;       // imports finn-ohos cannot classify
    std::vector<std::string> warnings;
    bool api_compatible = false;
    bool runnable = false;
    std::string summary;
    std::string error;
};

struct OhosInstallResult {
    bool ok = false;
    InstalledOhosApp app;
    std::vector<std::string> log;
    std::string error;
};

std::string default_ohos_root_dir();
std::string default_ohos_apps_dir(const std::string& ohos_root = {});
std::string default_ohos_icons_dir(const std::string& ohos_root = {});
std::string default_ohos_registry_path(const std::string& ohos_root = {});

bool is_http_url(std::string_view value);

// Distinguishes OpenHarmony .hap/.hsp/.app ZIP archives from macOS .app directories.
OhosPackageKind detect_ohos_package_kind(const std::string& path);

// JSON/JSON5 parsers for OpenHarmony 6.1 LTS module.json and .app pack.info.
OhosModuleManifest parse_module_json(std::string_view json_text);
OhosPackInfo parse_pack_info(std::string_view json_text);

// Parses the binary header of OpenHarmony resources.index.
OhosResourceIndexInfo parse_resources_index_header(const std::vector<std::uint8_t>& bytes);

// Inspects an ELF .so payload for CPU machine type and host ABI compatibility.
OhosNativeLibInfo inspect_elf_so_bytes(const std::string& archive_path,
                                       const std::vector<std::uint8_t>& bytes);

// Honest pre-flight compatibility analysis for .hap, .hsp, and .app packages.
OhosDiagnosticReport diagnose_ohos_package(const std::string& path);

// Downloads an HTTP (native POSIX sockets) or HTTPS (curl -fsSL) package to dest_path.
bool download_ohos_package(const std::string& url, const std::string& dest_path,
                           std::string* error = nullptr);

// Installs a local .hap/.hsp/.app file OR an http:///https:// URL into ohos_root.
OhosInstallResult install_ohos_package(const std::string& url_or_path,
                                       const std::string& ohos_root = {});

bool uninstall_ohos_app(const std::string& bundle_or_id,
                        const std::string& ohos_root = {},
                        std::string* error = nullptr);

std::vector<InstalledOhosApp> discover_installed_ohos_apps(
    const std::string& ohos_root = {});

// Runs an installed bundleName / "ohos:<slug>" or direct .hap/.app package.
int run_ohos_target(const std::string& target, int argc, char** argv,
                    bool headless, const std::string& ohos_root = {},
                    AbilityRunReport* report_out = nullptr,
                    std::string* error_out = nullptr);

}  // namespace finn::ohos

#endif
