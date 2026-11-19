// MSI / Windows package install and uninstall plans for the Wine path.
// Pure C++20 so wine_runner-style probes and headless unit tests can share
// one implementation: build_install_plan and build_uninstall_plan return data
// only - nothing here executes or touches the environment.

#ifndef FINN_COMPAT_MSI_INSTALLER_HPP
#define FINN_COMPAT_MSI_INSTALLER_HPP

#include "compat/windows_registry.hpp"
#include "compat/wine_runner.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace finn::compat {

enum class WindowsPackageKind {
    Unknown,
    Exe,
    Msi,
    Msix,
    Bat,
    Cmd,
};

struct InstallOptions {
    bool silent = true;                   // the MSI switch set is /qn /norestart
    std::string log_path;                 // optional msiexec /L*v log path
    std::vector<std::string> extra_args;  // appended after the built-in switch set
};

// What the caller should run, and what it should set. argv is the Wine-side
// command line; the caller prepends its runner binary (see wine_runner.hpp).
struct InstallPlan {
    WindowsPackageKind kind = WindowsPackageKind::Unknown;
    std::vector<std::string> argv;
    std::vector<std::pair<std::string, std::string>> env;
    bool ok = false;
    bool silent = false;
    std::string error;               // set when ok is false
    std::vector<std::string> notes;  // honest caveats ("silent switch unknown", ...)
};

// Case-insensitive extension detection from the path (.msi/.exe/.msix/.bat/.cmd).
WindowsPackageKind detect_windows_package_kind(std::string_view path);
const char* windows_package_kind_label(WindowsPackageKind kind);

// MSI product codes are GUIDs; the braced {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
// and the 32-hex-digit forms identify an uninstallable product.
bool is_product_code(std::string_view target);

// Plan for installing a package through Wine. Wine has no working AppX /
// App Installer, so Msix comes back ok=false with an honest error instead of
// a pretend command. .exe silent switches are filename heuristics: the plan
// says in notes when no known installer family was identified.
InstallPlan build_install_plan(const std::string& path,
                               const std::string& prefix = {},
                               const InstallOptions& options = {});

// Plan for uninstalling an .msi path or product code (msiexec /x ...).
InstallPlan build_uninstall_plan(const std::string& product_code_or_path,
                                 const std::string& prefix = {},
                                 const InstallOptions& options = {});

// ---- magic-based inspection and msiexec plans -----------------------------
// The API above detects by extension; the types below inspect real container
// magic (MZ / OLE2 / ZIP) and build plans with an explicit runner argument.

enum class WindowsInstallerKind {
    Unknown,
    PeExe,  // MZ/PE executable, including self-extracting installers
    Msi,    // OLE compound document with the MSI magic
    Msix,   // ZIP-based .msix/.appx -- detected, never faked as installable
};

std::string windows_installer_kind_name(WindowsInstallerKind kind);

struct WindowsInstallerInfo {
    bool valid = false;  // a known container magic was recognized
    WindowsInstallerKind kind = WindowsInstallerKind::Unknown;
    std::string path;
    std::string format_detail;
    bool installable = false;
    std::string unsupported_reason;
    bool streams_verified = false;       // MSI OLE directory streams were read
    std::vector<std::string> streams;
    PeInfo pe;  // populated for PeExe
    std::string error;
};

enum class MsiQuiet { None, Basic, Passive, Silent };

std::string msi_quiet_flag(MsiQuiet level);
std::vector<std::string> msi_quiet_flags(MsiQuiet level,
                                         const std::string& log_path = {});

struct MsiOptions {
    MsiQuiet quiet = MsiQuiet::Silent;
    std::string log_path;
    std::string runner = "wine";  // prepended to argv; empty disables it
    std::string prefix;
    std::vector<std::string> extra_args;  // forwarded verbatim, one argv element each
};

struct MsiPlan {
    bool ok = false;
    WindowsInstallerKind kind = WindowsInstallerKind::Unknown;
    std::string display_name;
    std::string action_label;
    std::vector<std::string> argv;
    std::vector<std::pair<std::string, std::string>> env;
    std::string note;
    std::string error;
};

WindowsInstallerKind detect_windows_installer_kind(std::string_view path);
WindowsInstallerInfo inspect_windows_installer_bytes(
    std::string_view bytes, std::string_view path_hint = {});
WindowsInstallerInfo inspect_windows_installer(const std::string& path);
std::vector<std::string> read_compound_stream_names(std::string_view bytes);

MsiPlan build_msi_install_plan(const std::string& path, const MsiOptions& opts = {});
MsiPlan build_msi_uninstall_plan(const std::string& product_code_or_path,
                                 const MsiOptions& opts = {});
MsiPlan build_msi_uninstall_plan(const UninstallEntry& entry,
                                 const MsiOptions& opts = {});

std::string msi_product_code_from_key(std::string_view registry_key);
std::string msi_product_code_from_uninstall(const UninstallEntry& entry);

// UninstallString tokenizer: rejects shell control characters and returns argv.
std::vector<std::string> parse_uninstall_command(std::string_view command,
                                                 std::string* error = nullptr);

// Headless self-tests. Return true on pass.
bool msi_installer_self_test();
bool windows_package_self_test();

}  // namespace finn::compat

#endif
