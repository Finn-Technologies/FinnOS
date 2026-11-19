// Per-app Wine prefix management: ids, paths and honest on-disk reports.
// Pure C++20: nothing here runs Wine, executes a process or creates anything.

#ifndef FINN_COMPAT_WINE_PREFIX_HPP
#define FINN_COMPAT_WINE_PREFIX_HPP

#include "compat/wine_runner.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace finn::compat {

// Prefix ids are directory names under the prefix root, so they are capped;
// a truncated id keeps a stable FNV-1a digest of its source.
inline constexpr std::size_t kPrefixIdMaxLength = 64;

struct PrefixInfo {
    std::string id;   // slug under the prefix root; "" when no safe id exists
    std::string path; // prefix directory; "" when the root is unresolvable
    // "win64" only when drive_c/windows/SysWOW64 exists, "win32" only when
    // drive_c/windows/system32 exists without SysWOW64; "" (unknown) rather
    // than guessed otherwise.
    std::string arch;
    bool drive_c_present = false;
    bool system_reg_present = false;
};

// The per-app prefix root: $HOME/.local/share/finn/wine/prefixes. "" when
// $HOME is unset rather than inventing a location.
std::string resolve_prefix_root();

// Root + "/" + id. An id containing a path separator or ".." yields ""; an
// empty id yields the root itself.
std::string prefix_path_for(const std::string& id);

// Slug for an app id or exe path: lowercase, trailing ".exe" stripped, at
// most kPrefixIdMaxLength long. "" when the input cannot yield a safe id
// (empty, ".", "..", separator-only, or containing "..").
std::string prefix_id_for(const std::string& app_or_exe);

// Read-only report of what this prefix has on disk today.
PrefixInfo prefix_info(const std::string& id);

// Every prefix directory under the root, sorted by id.
std::vector<PrefixInfo> list_prefixes();

struct WinePrefixInitStep {
    std::vector<std::string> argv;                         // e.g. {"wine", "wineboot", "-u"}
    std::vector<std::pair<std::string, std::string>> env;  // e.g. WINEPREFIX=<path>
};

struct WinePrefixInitPlan {
    std::string prefix_path;  // "" when the root is unresolvable
    std::string arch;         // as reported by prefix_info ("" = unknown)
    bool prefix_exists = false;
    std::vector<WinePrefixInitStep> steps;
};

// A pure plan for `wine wineboot -u` on the prefix. Nothing is executed.
WinePrefixInitPlan build_prefix_init_plan(const std::string& prefix_id);

struct WinePrefixRemovalStep {
    std::string action;  // e.g. "rm -rf"
    std::string path;    // prefix directory the step would remove
};

struct WinePrefixRemovalPlan {
    std::string prefix_path;
    bool prefix_exists = false;
    // Empty when the prefix is absent: an absent prefix has nothing to remove.
    std::vector<WinePrefixRemovalStep> steps;
};

WinePrefixRemovalPlan build_prefix_removal_plan(const std::string& prefix_id);

// "win32" for X86, "win64" for X86_64. Arm64 and Unknown return
// "unsupported: <reason>"; an unsupported arch is never given a fallback guess.
std::string recommend_arch(PeArch arch);

// ---- per-app prefix planning, DLL overrides and health --------------------
// The functions above manage prefix ids on disk; the types below build a plan
// for an app-specific prefix before anything is created. Every plan is data:
// no process is executed and no environment is modified by building one.

enum class WineArch {
    Unknown,
    Win32,
    Win64,
};

std::string wine_arch_name(WineArch arch);

struct WinePrefixLayout {
    std::string root;
    std::string drive_c;
    std::string windows_dir;
    std::string system32;
    std::string syswow64;  // empty in a win32 prefix
    std::string system_reg;
    std::string user_reg;
    std::string stamp;
};

struct WineDllOverride {
    std::string dlls;     // comma-separated DLL names
    std::string setting;  // WINEDLLOVERRIDES setting: "n,b", "b", or "" = disabled
    std::string reason;   // why it is planned; never a guess
};

struct WinePrefixOptions {
    WineArch arch = WineArch::Win64;  // used only when no PE is inspected
    bool dxvk_available = false;
    bool vkd3d_available = false;
    bool allow_mono = false;
    bool wayland_driver = true;
    bool fsync = true;
    std::string xdisplay;
    std::string runner = "wine";
    std::string base_dir;  // overrides the per-app prefix root (for tests)
};

struct WinePrefixPlan {
    bool ok = false;
    std::string app_id;
    std::string path;
    WineArch arch = WineArch::Unknown;
    WinePrefixLayout layout;
    bool needs_creation = false;
    std::vector<std::string> argv;  // wineboot plan when the prefix is absent
    std::vector<std::pair<std::string, std::string>> env;
    std::vector<WineDllOverride> overrides;
    std::vector<std::string> unsupported;  // honest gaps, reported not faked
    std::string error;
};

struct WinePrefixHealth {
    bool exists = false;
    bool has_drive_c = false;
    bool has_system_reg = false;
    bool has_user_reg = false;
    bool has_finn_stamp = false;
    bool arch_matches = false;
    bool healthy = false;
    WineArch detected_arch = WineArch::Unknown;
    std::vector<std::string> problems;
    std::string summary;
};

std::string wine_prefix_root(std::string_view app_id, const std::string& base_dir = {});
WinePrefixLayout wine_prefix_layout(const std::string& root, WineArch arch);
WineArch wine_arch_from_pe(const PeInfo& pe);
WineArch wine_arch_from_pe_file(const std::string& path);
std::vector<WineDllOverride> plan_dll_overrides(const WinePrefixOptions& opts);
std::string format_dll_overrides(const std::vector<WineDllOverride>& overrides);
std::vector<std::string> wine_prefix_unsupported_apis();

// ok means "a coherent plan was produced", not that Wine is installed.
WinePrefixPlan build_wine_prefix_plan(const std::string& app_id,
                                      const std::string& exe_path = {},
                                      const WinePrefixOptions& opts = {});

// Reads #arch= from system.reg; an unreadable architecture stays Unknown.
WinePrefixHealth check_wine_prefix(const std::string& root,
                                   WineArch expected = WineArch::Unknown);

// Creates the directory skeleton only; Wine still performs initialization.
bool create_wine_prefix_layout(const std::string& root, WineArch arch,
                               std::string* error = nullptr);

// Headless self-test. Returns true on pass.
bool wine_prefix_self_test();

}  // namespace finn::compat

#endif  // FINN_COMPAT_WINE_PREFIX_HPP
