// Clean-room macOS .dmg (UDIF), .pkg (XAR + gzip/cpio), and .app bundle
// installer and discovery engine for FinnOS.

#ifndef FINN_DARWIN_INSTALLER_HPP
#define FINN_DARWIN_INSTALLER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace finn::darwin {

struct InstalledMacosApp {
    std::string id;            // "macos:<slug>"
    std::string name;
    std::string bundle_id;
    std::string version;
    std::string bundle_path;
    std::string executable_path;
    std::string icon_path;     // Extracted PNG path or empty
};

struct DmgPartitionSummary {
    std::string name;
    std::uint64_t sectors = 0;
    std::uint64_t compressed_bytes = 0;
    std::string compression;   // "UDZO (zlib)", "RAW", "UDBZ", "LZFSE", etc.
};

struct DmgInspection {
    bool valid = false;
    std::uint64_t sector_count = 0;
    std::uint64_t data_fork_length = 0;
    std::vector<DmgPartitionSummary> partitions;
    std::string error;
};

struct PkgFileEntry {
    std::string name;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::uint64_t extracted_size = 0;
    std::string encoding;      // "application/x-gzip", "application/octet-stream", etc.
};

struct PkgInspection {
    bool valid = false;
    std::uint32_t toc_length_compressed = 0;
    std::uint32_t toc_length_uncompressed = 0;
    std::uint64_t heap_offset = 0;
    std::vector<PkgFileEntry> entries;
    std::string error;
};

struct InstallResult {
    bool ok = false;
    std::vector<InstalledMacosApp> installed_apps;
    std::vector<std::string> log;
    std::string error;
};

std::string default_macos_apps_dir();
std::string default_macos_icons_dir();

// Native UDIF .dmg header + blkx mish partition table inspector.
DmgInspection inspect_dmg_native(const std::string& dmg_path);

// Native XAR .pkg header + zlib-compressed XML TOC inspector.
PkgInspection inspect_pkg_native(const std::string& pkg_path);

// Installs a .app bundle directory into apps_dir (default: ~/.local/share/finn/macos/Applications)
// and extracts its .icns PNG icon into icons_dir.
InstallResult install_app_bundle(const std::string& app_bundle_path,
                                 const std::string& apps_dir = {},
                                 const std::string& icons_dir = {});

// Natively extracts a .pkg (XAR + gzip/cpio) and installs discovered .app bundles.
InstallResult install_pkg_native(const std::string& pkg_path,
                                 const std::string& apps_dir = {},
                                 const std::string& icons_dir = {});

// Inspects and installs .app bundles from a .dmg (native UDIF + 7z/darling-dmg extractor).
InstallResult install_dmg_package(const std::string& dmg_path,
                                  const std::string& apps_dir = {},
                                  const std::string& icons_dir = {});

// Scans installed macOS .app directories cleanly.
std::vector<InstalledMacosApp> discover_installed_macos_apps(
    const std::string& apps_dir = {},
    const std::string& icons_dir = {});

// Removes an installed .app bundle cleanly.
bool remove_installed_macos_app(const std::string& bundle_path,
                                std::string* error = nullptr);

}  // namespace finn::darwin

#endif
