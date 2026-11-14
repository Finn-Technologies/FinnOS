// Windows PE inspection, Wine compatibility detection, and registered .exe manager.
// Pure C++20 so runtime probes and unit tests remain headless-compatible.

#ifndef FINN_COMPAT_WINE_RUNNER_HPP
#define FINN_COMPAT_WINE_RUNNER_HPP

#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

enum class PeArch {
    Unknown,
    X86,
    X86_64,
    Arm64,
};

struct PeInfo {
    bool valid = false;
    PeArch arch = PeArch::Unknown;
    bool is_gui = false;
    bool is_console = false;
    bool is_dotnet = false;
    std::string error;
};

struct WineRuntimeStatus {
    std::string host_arch;         // "x86_64", "aarch64", etc.
    bool wine_installed = false;
    std::string wine_bin;          // "wine" or "wine64"
    bool emulator_installed = false; // box64 / fex-emu on aarch64
    std::string emulator_bin;
    bool xwayland_available = false;
    std::string default_prefix;
    bool prefix_initialized = false;
    bool ready = false;
    std::string summary;
};

struct WindowsAppEntry {
    std::string id;         // "win:<slug>"
    std::string name;
    std::string exe_path;
    std::string prefix;
    std::string args;
    std::string icon;       // "windows.svg" by default
};

struct WineLaunchPlan {
    std::vector<std::string> argv;
    std::vector<std::pair<std::string, std::string>> env;
    bool ok = false;
    std::string error;
};

// Safely parses a PE/PE32+ header from memory or file path.
PeInfo inspect_pe_bytes(std::string_view bytes);
PeInfo inspect_pe_file(const std::string& path);
std::string pe_arch_name(PeArch arch);

// Inspects local host architecture, wine/box64 binaries, and default Wine prefix.
WineRuntimeStatus probe_wine_runtime();

// Builds an honest execution plan for a Windows executable or MSI installer.
WineLaunchPlan build_wine_launch(const std::string& exe_path,
                                 const std::vector<std::string>& extra_args = {},
                                 const std::string& prefix = {},
                                 const std::string& xdisplay = {});

// Loads and saves user-registered Windows apps in windows.conf (tab/pipe clean format).
std::vector<WindowsAppEntry> load_windows_apps(const std::string& conf_path);
bool save_windows_app(const std::string& conf_path, const WindowsAppEntry& entry,
                      std::string* error = nullptr);
bool remove_windows_app(const std::string& conf_path, std::string_view id,
                        std::string* error = nullptr);

}  // namespace finn::compat

#endif
