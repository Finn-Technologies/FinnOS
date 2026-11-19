#include "compat/wine_prefix.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool is_slug_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || c == '.';
}

// FNV-1a over the source; the digest tail keeps a truncated id unique across
// sources that share a long common stem.
std::string fnv1a_hex8(std::string_view s) {
    std::uint64_t h = 14695981039346656037ull;
    for (char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(8, '0');
    for (int i = 7; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] =
            kHex[static_cast<std::size_t>(h & 0xf)];
        h >>= 4;
    }
    return out;
}

}  // namespace

std::string resolve_prefix_root() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string(home) + "/.local/share/finn/wine/prefixes";
    }
    return {};
}

std::string prefix_path_for(const std::string& id) {
    const std::string root = resolve_prefix_root();
    if (root.empty()) return {};
    if (id.empty()) return root;
    // An id from prefix_id_for cannot contain separators or ".."; anything
    // else here could point outside the prefix root, so it is rejected.
    if (id.find_first_of("/\\") != std::string::npos || id == "." ||
        id.find("..") != std::string::npos) {
        return {};
    }
    return root + "/" + id;
}

std::string prefix_id_for(const std::string& app_or_exe) {
    if (app_or_exe.empty()) return {};
    const std::size_t sep = app_or_exe.find_last_of("/\\");
    const std::string base =
        sep == std::string::npos ? app_or_exe : app_or_exe.substr(sep + 1);
    if (base.empty() || base == "." || base.find("..") != std::string::npos) {
        return {};
    }
    std::string slug;
    slug.reserve(base.size());
    for (char c : base) {
        const char lc = ascii_lower(c);
        slug.push_back(is_slug_char(lc) ? lc : '_');
    }
    const std::string kExeExt = ".exe";
    if (slug.size() > kExeExt.size() &&
        slug.compare(slug.size() - kExeExt.size(), kExeExt.size(), kExeExt) == 0) {
        slug.erase(slug.size() - kExeExt.size());
    }
    if (slug.empty()) return {};
    if (slug.size() > kPrefixIdMaxLength) {
        const std::string digest = fnv1a_hex8(app_or_exe);
        slug = slug.substr(0, kPrefixIdMaxLength - digest.size() - 1) + "-" + digest;
    }
    return slug;
}

PrefixInfo prefix_info(const std::string& id) {
    PrefixInfo info;
    info.id = id;
    info.path = prefix_path_for(id);
    if (info.path.empty()) return info;
    std::error_code ec;
    const fs::path root(info.path);
    info.drive_c_present = fs::is_directory(root / "drive_c", ec);
    info.system_reg_present = fs::exists(root / "system.reg", ec);
    if (info.drive_c_present) {
        const fs::path windows = root / "drive_c" / "windows";
        if (fs::is_directory(windows / "SysWOW64", ec)) {
            info.arch = "win64";
        } else if (fs::is_directory(windows / "system32", ec)) {
            info.arch = "win32";
        }
    }
    return info;
}

std::vector<PrefixInfo> list_prefixes() {
    std::vector<PrefixInfo> out;
    const std::string root = resolve_prefix_root();
    if (root.empty()) return out;
    std::error_code ec;
    fs::directory_iterator it(fs::path(root), ec);
    if (ec) return out;
    const fs::directory_iterator end;
    while (it != end) {
        if (it->is_directory()) {
            const std::string id = it->path().filename().string();
            if (!id.empty()) out.push_back(prefix_info(id));
        }
        it.increment(ec);
        if (ec) break;
    }
    std::sort(out.begin(), out.end(),
              [](const PrefixInfo& a, const PrefixInfo& b) { return a.id < b.id; });
    return out;
}

WinePrefixInitPlan build_prefix_init_plan(const std::string& prefix_id) {
    WinePrefixInitPlan plan;
    plan.prefix_path = prefix_path_for(prefix_id);
    plan.arch = prefix_info(prefix_id).arch;
    std::error_code ec;
    plan.prefix_exists =
        !plan.prefix_path.empty() && fs::is_directory(fs::path(plan.prefix_path), ec);

    WinePrefixInitStep step;
    step.argv = {"wine", "wineboot", "-u"};
    if (!plan.prefix_path.empty()) step.env.emplace_back("WINEPREFIX", plan.prefix_path);
    plan.steps.push_back(std::move(step));
    return plan;
}

WinePrefixRemovalPlan build_prefix_removal_plan(const std::string& prefix_id) {
    WinePrefixRemovalPlan plan;
    plan.prefix_path = prefix_path_for(prefix_id);
    std::error_code ec;
    plan.prefix_exists =
        !plan.prefix_path.empty() && fs::is_directory(fs::path(plan.prefix_path), ec);
    if (plan.prefix_exists) {
        WinePrefixRemovalStep step;
        step.action = "rm -rf";
        step.path = plan.prefix_path;
        plan.steps.push_back(std::move(step));
    }
    return plan;
}

std::string recommend_arch(PeArch arch) {
    switch (arch) {
        case PeArch::X86:
            return "win32";
        case PeArch::X86_64:
            return "win64";
        case PeArch::Arm64:
            return "unsupported: arm64 PEs are not supported; prefixes are created "
                   "as win32 or win64 here";
        case PeArch::Unknown:
        default:
            return "unsupported: the PE architecture is unknown";
    }
}

// ---- per-app prefix planning ----------------------------------------------

namespace {

std::string lower_copy(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = ascii_lower(c);
    return out;
}

std::string prefix_slug(std::string_view app_id) {
    std::string_view id = app_id;
    if (lower_copy(id.substr(0, 4)) == "win:") id.remove_prefix(4);
    std::string slug;
    slug.reserve(id.size());
    bool dash = false;
    for (char c : id) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9');
        if (alnum) {
            if (c >= 'A' && c <= 'Z') c = ascii_lower(c);
            slug.push_back(c);
            dash = false;
        } else if (!dash && !slug.empty()) {
            slug.push_back('-');
            dash = true;
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "app";
    if (slug.size() > 64) slug.resize(64);
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.empty() ? std::string("app") : slug;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    return lower_copy(s.substr(s.size() - suffix.size())) == lower_copy(suffix);
}

const char* arch_env_value(WineArch arch) {
    switch (arch) {
        case WineArch::Win32: return "win32";
        case WineArch::Win64: return "win64";
        case WineArch::Unknown: break;
    }
    return "";
}

WineArch arch_from_reg_header(std::string_view content) {
    const std::string lower = lower_copy(content);
    const std::size_t pos = lower.find("#arch=");
    if (pos == std::string_view::npos) return WineArch::Unknown;
    const std::size_t end = lower.find_first_of("\r\n", pos);
    const std::string value = lower.substr(pos + 6, end == std::string_view::npos
                                                       ? std::string_view::npos
                                                       : end - pos - 6);
    if (value.find("win64") != std::string::npos) return WineArch::Win64;
    if (value.find("win32") != std::string::npos) return WineArch::Win32;
    return WineArch::Unknown;
}

}  // namespace

std::string wine_arch_name(WineArch arch) {
    switch (arch) {
        case WineArch::Win32: return "win32 (32-bit)";
        case WineArch::Win64: return "win64 (64-bit)";
        case WineArch::Unknown: break;
    }
    return "Unknown";
}

std::string wine_prefix_root(std::string_view app_id, const std::string& base_dir) {
    std::string base = base_dir.empty() ? resolve_prefix_root() : base_dir;
    if (base.empty()) {
        base = "/tmp/finn-wine-prefixes";
    }
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    return base + "/" + prefix_slug(app_id);
}

WinePrefixLayout wine_prefix_layout(const std::string& root, WineArch arch) {
    WinePrefixLayout layout;
    layout.root = root;
    layout.drive_c = root + "/drive_c";
    layout.windows_dir = layout.drive_c + "/windows";
    layout.system32 = layout.windows_dir + "/system32";
    if (arch == WineArch::Win64) layout.syswow64 = layout.windows_dir + "/syswow64";
    layout.system_reg = root + "/system.reg";
    layout.user_reg = root + "/user.reg";
    layout.stamp = root + "/.finn-win-ready";
    return layout;
}

WineArch wine_arch_from_pe(const PeInfo& pe) {
    if (!pe.valid) return WineArch::Unknown;
    switch (pe.arch) {
        case PeArch::X86: return WineArch::Win32;
        case PeArch::X86_64:
        case PeArch::Arm64: return WineArch::Win64;
        case PeArch::Unknown: break;
    }
    return WineArch::Unknown;
}

WineArch wine_arch_from_pe_file(const std::string& path) {
    return wine_arch_from_pe(inspect_pe_file(path));
}

std::vector<WineDllOverride> plan_dll_overrides(const WinePrefixOptions& opts) {
    std::vector<WineDllOverride> overrides;
    if (!opts.allow_mono) {
        overrides.push_back({"mscoree,mshtml", "",
                             "Mono/MSHTML downloads disabled to avoid first-run prompts; "
                             ".NET applications will not run unless Mono is installed."});
    }
    if (opts.dxvk_available) {
        overrides.push_back({"d3d9,d3d10core,d3d11,dxgi", "n,b",
                             "DXVK Vulkan translation detected on the host."});
    }
    if (opts.vkd3d_available) {
        overrides.push_back({"d3d12,d3d12core", "n,b",
                             "VKD3D-Proton detected on the host."});
    }
    return overrides;
}

std::string format_dll_overrides(const std::vector<WineDllOverride>& overrides) {
    std::string out;
    for (const auto& ov : overrides) {
        if (ov.dlls.empty()) continue;
        if (!out.empty()) out.push_back(';');
        out += ov.dlls + "=" + ov.setting;
    }
    return out;
}

std::vector<std::string> wine_prefix_unsupported_apis() {
    return {
        "MSIX installation is not implemented; .msix/.appx packages are detected but cannot be installed.",
        "Full Windows shortcut (.lnk) resolution and .ico extraction are not implemented.",
        "Direct3D acceleration is not provided by FinnOS itself; it requires DXVK/VKD3D on the host.",
        "Binary Windows registry hives (SOFTWARE/SYSTEM) are not parsed; only text .reg files are read.",
    };
}

WinePrefixPlan build_wine_prefix_plan(const std::string& app_id,
                                      const std::string& exe_path,
                                      const WinePrefixOptions& opts) {
    WinePrefixPlan plan;
    if (app_id.empty()) {
        plan.error = "empty application id";
        return plan;
    }
    plan.app_id = app_id;
    plan.path = wine_prefix_root(app_id, opts.base_dir);
    plan.arch = opts.arch;

    if (!exe_path.empty() && !ends_with_ci(exe_path, ".bat") &&
        !ends_with_ci(exe_path, ".cmd")) {
        const PeInfo pe = inspect_pe_file(exe_path);
        if (!pe.valid) {
            plan.error = "not a valid Windows PE executable (" + pe.error + ")";
            return plan;
        }
        const WineArch from_pe = wine_arch_from_pe(pe);
        if (from_pe == WineArch::Unknown) {
            plan.unsupported.push_back("Unrecognized PE machine type; prefix architecture could not be selected from the binary.");
        } else {
            plan.arch = from_pe;
        }
        if (pe.arch == PeArch::Arm64) {
            plan.unsupported.push_back("ARM64 PE binaries need native ARM64 Wine; x86_64 Wine/Proton cannot run them.");
        }
        if (pe.is_dotnet) {
            plan.unsupported.push_back(".NET executables need Mono; .NET Framework is not installed by FinnOS.");
        }
    } else if (!exe_path.empty()) {
        plan.unsupported.push_back("Batch files run through Wine cmd; no PE validation or per-binary architecture detection is possible.");
    }

    plan.layout = wine_prefix_layout(plan.path, plan.arch);
    const WinePrefixHealth health = check_wine_prefix(plan.path, plan.arch);
    plan.needs_creation = !health.has_drive_c || !health.has_system_reg;

    if (plan.needs_creation) {
        plan.argv = {opts.runner.empty() ? "wine" : opts.runner, "wineboot", "-u"};
    }
    plan.env.emplace_back("WINEPREFIX", plan.path);
    plan.env.emplace_back("FINN_WIN_PREFIX", plan.path);
    const char* arch_value = arch_env_value(plan.arch);
    if (*arch_value) plan.env.emplace_back("WINEARCH", arch_value);
    plan.env.emplace_back("WINEDEBUG", "-all");
    if (opts.fsync) {
        plan.env.emplace_back("WINEESYNC", "1");
        plan.env.emplace_back("WINEFSYNC", "1");
    }
    if (opts.wayland_driver) plan.env.emplace_back("PROTON_ENABLE_WAYLAND", "1");
    if (!opts.xdisplay.empty()) plan.env.emplace_back("DISPLAY", opts.xdisplay);

    plan.overrides = plan_dll_overrides(opts);
    const std::string override_value = format_dll_overrides(plan.overrides);
    if (!override_value.empty()) plan.env.emplace_back("WINEDLLOVERRIDES", override_value);

    const std::vector<std::string> gaps = wine_prefix_unsupported_apis();
    plan.unsupported.insert(plan.unsupported.end(), gaps.begin(), gaps.end());
    plan.ok = true;
    return plan;
}

WinePrefixHealth check_wine_prefix(const std::string& root, WineArch expected) {
    WinePrefixHealth health;
    if (root.empty()) {
        health.problems.push_back("empty prefix path");
        health.summary = "wine prefix path is empty";
        return health;
    }
    std::error_code ec;
    health.exists = fs::is_directory(root, ec);
    if (!health.exists) {
        health.problems.push_back("prefix directory does not exist");
        health.summary = "wine prefix does not exist: " + root;
        return health;
    }
    const WinePrefixLayout layout = wine_prefix_layout(root, expected);
    health.has_drive_c = fs::is_directory(layout.drive_c, ec);
    health.has_system_reg = fs::is_regular_file(layout.system_reg, ec);
    health.has_user_reg = fs::is_regular_file(layout.user_reg, ec);
    health.has_finn_stamp = fs::is_regular_file(layout.stamp, ec) ||
                            fs::is_regular_file(root + "/.finn-win11-ready", ec);
    if (!health.has_drive_c) health.problems.push_back("missing drive_c directory");
    if (!health.has_system_reg) health.problems.push_back("missing system.reg");
    if (!health.has_user_reg) health.problems.push_back("missing user.reg (prefix may not be initialized)");
    if (!health.has_finn_stamp) health.problems.push_back("no FinnOS/finn-win initialization stamp");

    if (health.has_system_reg) {
        std::ifstream in(layout.system_reg, std::ios::binary);
        if (in) {
            std::string head(8192, '\0');
            in.read(head.data(), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<std::size_t>(in.gcount()));
            health.detected_arch = arch_from_reg_header(head);
        }
        if (health.detected_arch == WineArch::Unknown) {
            health.problems.push_back("system.reg has no readable #arch= comment");
        }
    }
    health.arch_matches = expected == WineArch::Unknown || health.detected_arch == expected;
    if (!health.arch_matches) {
        health.problems.push_back("prefix architecture is " +
                                  wine_arch_name(health.detected_arch) +
                                  ", expected " + wine_arch_name(expected));
    }
    health.healthy = health.exists && health.has_drive_c && health.has_system_reg &&
                     health.has_user_reg && health.arch_matches;
    health.summary = health.healthy
                         ? "wine prefix ready (" + wine_arch_name(health.detected_arch) + ")"
                         : "wine prefix incomplete: " +
                               (health.problems.empty() ? std::string("unknown reason")
                                                        : health.problems.front());
    return health;
}

bool create_wine_prefix_layout(const std::string& root, WineArch arch,
                               std::string* error) {
    if (root.empty()) {
        if (error) *error = "empty prefix path";
        return false;
    }
    const WinePrefixLayout layout = wine_prefix_layout(root, arch);
    const std::vector<std::string> dirs = {
        layout.root, layout.drive_c, layout.windows_dir, layout.system32, layout.syswow64};
    std::error_code ec;
    for (const std::string& dir : dirs) {
        if (dir.empty()) continue;  // syswow64 is absent in a win32 prefix
        fs::create_directories(dir, ec);
        if (ec) {
            if (error) *error = "cannot create " + dir + ": " + ec.message();
            return false;
        }
    }
    return true;
}

bool wine_prefix_self_test() {
    const std::string root = wine_prefix_root("win:My App!!");
    if (root.find("/prefixes/my-app") == std::string::npos &&
        root.find("/apps/my-app") == std::string::npos) {
        return false;
    }
    if (wine_prefix_root("../escape", "/tmp/prefixes") != "/tmp/prefixes/escape") return false;
    if (wine_arch_name(WineArch::Win32) != "win32 (32-bit)") return false;

    PeInfo pe;
    pe.valid = true;
    pe.arch = PeArch::X86;
    if (wine_arch_from_pe(pe) != WineArch::Win32) return false;
    pe.arch = PeArch::X86_64;
    if (wine_arch_from_pe(pe) != WineArch::Win64) return false;
    pe.valid = false;
    if (wine_arch_from_pe(pe) != WineArch::Unknown) return false;

    WinePrefixOptions opts;
    opts.dxvk_available = true;
    opts.vkd3d_available = true;
    if (format_dll_overrides(plan_dll_overrides(opts)) !=
        "mscoree,mshtml=;d3d9,d3d10core,d3d11,dxgi=n,b;d3d12,d3d12core=n,b") {
        return false;
    }
    const WinePrefixLayout layout = wine_prefix_layout("/tmp/p", WineArch::Win64);
    if (layout.syswow64 != "/tmp/p/drive_c/windows/syswow64") return false;
    if (!wine_prefix_layout("/tmp/p", WineArch::Win32).syswow64.empty()) return false;
    if (check_wine_prefix("/tmp/finn-prefix-does-not-exist").healthy) return false;

    const WinePrefixPlan plan = build_wine_prefix_plan("win:self-test", "", opts);
    if (!plan.ok || plan.arch != WineArch::Win64 || plan.path.empty()) return false;
    bool has_prefix_env = false;
    for (const auto& kv : plan.env) {
        if (kv.first == "WINEPREFIX" && kv.second == plan.path) has_prefix_env = true;
    }
    if (!has_prefix_env) return false;
    if (plan.unsupported.empty()) return false;
    return true;
}

}  // namespace finn::compat
