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
    std::string host_arch;           // "x86_64", "aarch64", etc.
    bool proton_installed = false;
    std::string proton_bin;          // GE-Proton / Steam Proton wine64
    bool wine_installed = false;
    std::string wine_bin;            // "wine" or "wine64"
    bool dxvk_available = false;
    bool vkd3d_available = false;
    bool finn_win_installed = false;
    std::string finn_win_bin;
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
    // Honest notes about the plan: DLL override notes, prefix state and
    // failure reasons. Empty when nothing needs reporting.
    std::vector<std::string> diagnostics;
};

// DLL overrides derived from the runtime probe and the target PE info. Rules
// follow finn-win's conventions: DXVK present -> "d3d11=n,b"/"dxgi=n,b";
// VKD3D present -> "d3d12=n,b"/"d3d12core=n,b"; deliberately disabled Mono ->
// "mscoree,mshtml=". Diagnostics report what is absent honestly; they never
// claim support that is not installed.
struct WineDllOverridePlan {
    std::vector<std::string> overrides;   // e.g. "d3d11=n,b"
    std::vector<std::string> diagnostics; // honest notes
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

WineDllOverridePlan build_wine_dll_override_plan(const WineRuntimeStatus& runtime,
                                                 const PeInfo& target,
                                                 bool mono_present = false,
                                                 bool mono_disabled = false);

// Read-only report on the target: PE facts, runtime probe, DLL override plan
// and per-app prefix state. Creates or modifies nothing.
std::string diagnose_wine_target(const std::string& exe_path);

// Loads and saves user-registered Windows apps in windows.conf (tab/pipe clean format).
std::vector<WindowsAppEntry> load_windows_apps(const std::string& conf_path);
bool save_windows_app(const std::string& conf_path, const WindowsAppEntry& entry,
                      std::string* error = nullptr);
bool remove_windows_app(const std::string& conf_path, std::string_view id,
                        std::string* error = nullptr);

}  // namespace finn::compat

#endif
