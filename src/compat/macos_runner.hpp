// Honest macOS application (.app), Mach-O binary, and installer (.dmg/.pkg)
// inspection, extraction planning, and runtime probing.
//
// FinnOS never fakes macOS GUI execution: Cutefish's Reef compatibility layer
// is not publicly released in CutefishOS repositories, and open-source Darling
// only supports Darwin/CLI binaries on Linux. Launch plans stay disabled unless
// a compatible runtime is actually installed on the host.

#ifndef FINN_COMPAT_MACOS_RUNNER_HPP
#define FINN_COMPAT_MACOS_RUNNER_HPP

#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

enum class MachoArch {
    Unknown,
    X86,
    X86_64,
    Arm,
    Arm64,
};

struct MachoInfo {
    bool valid = false;
    bool is_universal = false;
    std::vector<MachoArch> arches;
    std::string error;
};

struct MacosAppInfo {
    bool valid = false;
    std::string bundle_path;
    std::string name;
    std::string bundle_id;
    std::string version;
    std::string executable_name;
    std::string executable_path;
    MachoInfo binary;
    std::string error;
};

enum class MacosInstallerKind {
    Unknown,
    Dmg,  // Apple UDIF disk image (.dmg)
    Pkg,  // macOS XAR installer package (.pkg / .mpkg)
};

struct MacosInstallerInfo {
    bool valid = false;
    MacosInstallerKind kind = MacosInstallerKind::Unknown;
    std::string path;
    std::string format_detail;
    std::string error;
};

struct MacosRuntimeStatus {
    std::string host_arch;
    bool finn_darwin_installed = false;
    std::string finn_darwin_bin;
    bool reef_installed = false;
    std::string reef_bin;
    bool darling_installed = false;
    std::string darling_bin;
    bool dmg_extractor_installed = false;
    std::string dmg_extractor_bin;  // 7z, darling-dmg, or dmg2img
    bool pkg_extractor_installed = false;
    std::string pkg_extractor_bin;  // xar or 7z
    bool gui_ready = false;
    bool cli_ready = false;
    std::string summary;
};

struct MacosBundleEntry {
    std::string id;            // "macos:<slug>"
    std::string name;
    std::string bundle_id;
    std::string version;
    std::string bundle_path;
    std::string executable_path;
    std::string icon_path;
};

struct MacosCommandPlan {
    bool ok = false;
    std::vector<std::string> argv;
    std::string note;
    std::string error;
};

std::string macho_arch_name(MachoArch arch);
std::string macho_arches_summary(const MachoInfo& info);

// Parses thin (32/64-bit) and Universal/Fat Mach-O headers from bytes or file.
MachoInfo inspect_macho_bytes(std::string_view bytes);
MachoInfo inspect_macho_file(const std::string& path);

// Parses an XML Info.plist and inspects a macOS .app bundle directory.
MacosAppInfo parse_macos_info_plist(std::string_view xml_content,
                                    const std::string& bundle_path = {});
MacosAppInfo inspect_macos_app_bundle(const std::string& app_bundle_path);

// Inspects a .dmg (UDIF koly trailer) or .pkg (xar! header) installer file.
MacosInstallerInfo inspect_macos_installer(const std::string& path);

// Probes host for macOS runtimes (reef, darling) and .dmg/.pkg extractors.
MacosRuntimeStatus probe_macos_runtime();

// Builds an extraction command for .dmg or .pkg into dest_dir when tools exist.
MacosCommandPlan build_macos_extract_plan(const std::string& installer_path,
                                          const std::string& dest_dir);

// Builds a launch command only when a real runtime (finn-darwin, reef, or darling) is present.
MacosCommandPlan build_macos_launch_plan(const std::string& app_or_binary_path);

// Builds finn-darwin install and pre-flight diagnose command plans.
MacosCommandPlan build_macos_install_plan(const std::string& dmg_pkg_or_app_path);
MacosCommandPlan build_macos_diagnose_plan(const std::string& app_or_binary_path);

// Discovers installed macOS .app bundles in ~/.local/share/finn/macos/Applications
// and ~/Applications (or an explicit override directory for tests).
std::vector<MacosBundleEntry> discover_macos_bundles(const std::string& apps_dir = {});

}  // namespace finn::compat

#endif
