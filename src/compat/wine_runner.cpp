#include "compat/wine_runner.hpp"

#include "common/paths.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/wine_prefix.hpp"

#include <sys/utsname.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

std::uint16_t read_u16(std::string_view b, std::size_t off) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off + 1])) << 8);
}

std::uint32_t read_u32(std::string_view b, std::size_t off) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 3])) << 24);
}

std::string default_wine_prefix_path() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/" + kWinePrefixRel;
    }
    return "/tmp/finn-wine-default";
}

std::string sanitize_field(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c == '|' || c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char a = s[off + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

}  // namespace

std::string pe_arch_name(PeArch arch) {
    switch (arch) {
        case PeArch::X86: return "x86 (32-bit)";
        case PeArch::X86_64: return "x86_64 (64-bit)";
        case PeArch::Arm64: return "ARM64";
        case PeArch::Unknown:
        default: return "Unknown";
    }
}

PeInfo inspect_pe_bytes(std::string_view b) {
    PeInfo info;
    if (b.size() < 64) {
        info.error = "file too short for DOS header";
        return info;
    }
    if (b[0] != 'M' || b[1] != 'Z') {
        info.error = "missing MZ signature";
        return info;
    }
    const std::uint32_t pe_off = read_u32(b, 60);
    if (pe_off > b.size() || b.size() - pe_off < 24) {
        info.error = "PE header offset out of bounds";
        return info;
    }
    if (b[pe_off] != 'P' || b[pe_off + 1] != 'E' || b[pe_off + 2] != '\0' ||
        b[pe_off + 3] != '\0') {
        info.error = "missing PE signature";
        return info;
    }

    const std::size_t coff = pe_off + 4;
    const std::uint16_t machine = read_u16(b, coff);
    switch (machine) {
        case 0x014c: info.arch = PeArch::X86; break;
        case 0x8664: info.arch = PeArch::X86_64; break;
        case 0xaa64: info.arch = PeArch::Arm64; break;
        default: info.arch = PeArch::Unknown; break;
    }

    const std::uint16_t opt_size = read_u16(b, coff + 16);
    const std::size_t opt = coff + 20;
    if (opt_size >= 70 && b.size() >= opt + 70) {
        const std::uint16_t magic = read_u16(b, opt);
        const std::uint16_t subsystem = read_u16(b, opt + 68);
        info.is_gui = (subsystem == 2);
        info.is_console = (subsystem == 3);

        // CLR Runtime Header is DataDirectory[14]: offset 208 (PE32) or 224 (PE32+).
        const std::size_t rva_count_off = (magic == 0x020b) ? (opt + 108) : (opt + 92);
        const std::size_t clr_dir_off = (magic == 0x020b) ? (opt + 224) : (opt + 208);
        if (b.size() >= rva_count_off + 4 && b.size() >= clr_dir_off + 8) {
            const std::uint32_t num_rvas = read_u32(b, rva_count_off);
            if (num_rvas > 14 && read_u32(b, clr_dir_off) != 0) {
                info.is_dotnet = true;
            }
        }
    }

    info.valid = true;
    return info;
}

PeInfo inspect_pe_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        PeInfo info;
        info.error = "cannot open file";
        return info;
    }
    std::string buf(4096, '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    if (buf.size() == 4096 && buf[0] == 'M' && buf[1] == 'Z') {
        const std::uint32_t pe_off = read_u32(buf, 60);
        constexpr std::size_t kMaxPeScanBytes = 1024 * 1024;
        if (pe_off < kMaxPeScanBytes) {
            const std::size_t need = std::min<std::size_t>(
                kMaxPeScanBytes, static_cast<std::size_t>(pe_off) + 512);
            if (need > buf.size()) {
                const std::size_t prev = buf.size();
                buf.resize(need, '\0');
                in.read(buf.data() + prev,
                        static_cast<std::streamsize>(need - prev));
                buf.resize(prev + static_cast<std::size_t>(in.gcount()));
            }
        }
    }
    return inspect_pe_bytes(buf);
}

WineRuntimeStatus probe_wine_runtime() {
    WineRuntimeStatus status;
    struct utsname u{};
    if (::uname(&u) == 0) status.host_arch = u.machine;
    else status.host_arch = "unknown";

    std::error_code ec;
    // Detect Proton (GE-Proton / Steam Proton) before plain Wine
    const char* home_env = std::getenv("HOME");
    std::vector<fs::path> proton_roots = {
        "/usr/share/steam/compatibilitytools.d",
    };
    if (home_env && *home_env) {
        proton_roots.push_back(fs::path(home_env) / ".local/share/Steam/compatibilitytools.d");
        proton_roots.push_back(fs::path(home_env) / ".local/share/Steam/steamapps/common");
    }
    for (const auto& root : proton_roots) {
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) continue;
            for (const char* rel : {"files/bin/wine64", "files/bin/wine"}) {
                const fs::path cand = entry.path() / rel;
                if (fs::is_regular_file(cand, ec)) {
                    status.proton_installed = true;
                    status.proton_bin = cand.string();
                    break;
                }
            }
            if (status.proton_installed) break;
        }
        if (status.proton_installed) break;
    }

    if (executable_exists("wine")) {
        status.wine_installed = true;
        status.wine_bin = "wine";
    } else if (executable_exists("wine64")) {
        status.wine_installed = true;
        status.wine_bin = "wine64";
    }

    status.dxvk_available = fs::is_directory("/usr/share/dxvk/x64", ec) ||
                            fs::is_directory("/usr/lib/dxvk/x64", ec) ||
                            fs::is_directory("/usr/share/wine/dxvk/x64", ec);
    status.vkd3d_available = fs::is_directory("/usr/share/vkd3d-proton/x64", ec) ||
                             fs::is_directory("/usr/lib/vkd3d-proton/x64", ec);
    if (fs::is_regular_file("/usr/local/bin/finn-win", ec)) {
        status.finn_win_installed = true;
        status.finn_win_bin = "/usr/local/bin/finn-win";
    } else if (executable_exists("finn-win")) {
        status.finn_win_installed = true;
        status.finn_win_bin = "finn-win";
    }

    if (executable_exists("box64")) {
        status.emulator_installed = true;
        status.emulator_bin = "box64";
    } else if (executable_exists("FEXInterpreter") || executable_exists("FEXBash")) {
        status.emulator_installed = true;
        status.emulator_bin = executable_exists("FEXInterpreter") ? "FEXInterpreter" : "FEXBash";
    }

    status.xwayland_available = executable_exists("Xwayland") ||
                                fs::exists(kXwaylandDisplayFile, ec);
    status.default_prefix = default_wine_prefix_path();
    status.prefix_initialized = fs::is_directory(fs::path(status.default_prefix) / "drive_c", ec);

    const bool has_engine = status.proton_installed || status.wine_installed;
    if (has_engine) {
        status.ready = true;
        const std::string engine = status.proton_installed ? "Proton" : ("Wine (" + status.wine_bin + ")");
        status.summary = "Windows 11 ready via " + engine +
                         (status.dxvk_available ? " + DXVK" : "") +
                         (status.vkd3d_available ? " + VKD3D" : "") +
                         " on " + status.host_arch;
    } else if (status.host_arch == "aarch64" || status.host_arch == "arm64") {
        status.ready = false;
        status.summary =
            "Windows (.exe) runtime requires x86_64 Proton/Wine or an x86_64 compatibility "
            "translator (box64/fex-emu) on aarch64; not installed on this host.";
    } else {
        status.ready = false;
        status.summary = "Proton/Wine is not installed on this system (install wine or GE-Proton on x86_64).";
    }
    return status;
}

WineLaunchPlan build_wine_launch(const std::string& exe_path,
                                 const std::vector<std::string>& extra_args,
                                 const std::string& prefix,
                                 const std::string& xdisplay) {
    WineLaunchPlan plan;
    std::error_code ec;
    if (!fs::is_regular_file(exe_path, ec)) {
        plan.error = "executable does not exist: " + exe_path;
        plan.diagnostics.push_back(plan.error);
        return plan;
    }

    const bool is_msi = ends_with_ci(exe_path, ".msi");
    PeInfo pe;
    if (!is_msi) {
        pe = inspect_pe_file(exe_path);
        if (!pe.valid) {
            plan.error = "not a valid Windows PE executable (" + pe.error + ")";
            plan.diagnostics.push_back(plan.error);
            return plan;
        }
    }

    const WineRuntimeStatus runtime = probe_wine_runtime();
    if (!runtime.ready) {
        plan.error = runtime.summary;
        plan.diagnostics.push_back(plan.error);
        return plan;
    }

    const std::string resolved_prefix = prefix.empty() ? runtime.default_prefix : prefix;
    plan.env.emplace_back("WINEPREFIX", resolved_prefix);
    plan.env.emplace_back("FINN_WIN_PREFIX", resolved_prefix);
    plan.env.emplace_back("WINEARCH", "win64");
    plan.env.emplace_back("WINEESYNC", "1");
    plan.env.emplace_back("WINEFSYNC", "1");
    plan.env.emplace_back("PROTON_ENABLE_WAYLAND", "1");
    plan.env.emplace_back("WINEDEBUG", "-all");
    if (!xdisplay.empty()) plan.env.emplace_back("DISPLAY", xdisplay);

    if (runtime.finn_win_installed) {
        plan.argv = {runtime.finn_win_bin, exe_path};
    } else {
        const std::string runner = runtime.proton_installed ? runtime.proton_bin : runtime.wine_bin;
        if (is_msi) {
            plan.argv = {runner, "msiexec", "/i", exe_path};
        } else {
            plan.argv = {runner, exe_path};
        }
    }
    for (const std::string& arg : extra_args) plan.argv.push_back(arg);
    plan.ok = true;

    const WineDllOverridePlan dll_plan = build_wine_dll_override_plan(runtime, pe);
    plan.diagnostics.insert(plan.diagnostics.end(), dll_plan.diagnostics.begin(),
                            dll_plan.diagnostics.end());
    if (!fs::is_directory(fs::path(resolved_prefix) / "drive_c", ec)) {
        plan.diagnostics.push_back("prefix " + resolved_prefix +
                                   " is not initialized yet (no drive_c)");
    }
    return plan;
}

WineDllOverridePlan build_wine_dll_override_plan(const WineRuntimeStatus& runtime,
                                                 const PeInfo& target,
                                                 bool mono_present,
                                                 bool mono_disabled) {
    WineDllOverridePlan plan;
    if (runtime.dxvk_available) {
        plan.overrides.push_back("d3d11=n,b");
        plan.overrides.push_back("dxgi=n,b");
    } else {
        plan.diagnostics.push_back(
            "DXVK is not installed; d3d11/dxgi overrides are not applied");
    }
    if (runtime.vkd3d_available) {
        plan.overrides.push_back("d3d12=n,b");
        plan.overrides.push_back("d3d12core=n,b");
    } else {
        plan.diagnostics.push_back(
            "VKD3D is not installed; d3d12/d3d12core overrides are not applied");
    }
    if (mono_disabled) {
        plan.overrides.push_back("mscoree,mshtml=");
        plan.diagnostics.push_back(
            "Mono is deliberately disabled; mscoree,mshtml= override applied");
    }
    if (target.is_dotnet) {
        if (mono_disabled) {
            plan.diagnostics.push_back(
                "target needs .NET; Mono is deliberately disabled in this runtime");
        } else if (!mono_present) {
            plan.diagnostics.push_back(
                "target needs .NET; Mono is not confirmed installed; .NET support "
                "needs Mono or an equivalent runtime");
        }
    }
    if (target.arch == PeArch::Arm64) {
        const bool native_arm =
            runtime.host_arch == "aarch64" || runtime.host_arch == "arm64";
        if (!native_arm && !runtime.emulator_installed) {
            plan.diagnostics.push_back(
                "target is ARM64; this host cannot run arm64 PEs (no "
                "emulator/translator installed)");
        }
    }
    return plan;
}

std::string diagnose_wine_target(const std::string& exe_path) {
    const PeInfo pe = inspect_pe_file(exe_path);
    const WineRuntimeStatus runtime = probe_wine_runtime();
    const WineDllOverridePlan dll_plan = build_wine_dll_override_plan(runtime, pe);
    const std::string prefix_id = prefix_id_for(exe_path);

    std::string out = "target: " + exe_path + "\n";
    if (pe.valid) {
        out += std::string("pe: arch=") + pe_arch_name(pe.arch) +
               (pe.is_dotnet ? " dotnet=yes" : " dotnet=no") + "\n";
    } else {
        out += "pe: not a valid PE file (" + pe.error + ")\n";
    }
    out += "runtime: wine=" +
           (runtime.wine_installed ? runtime.wine_bin : std::string("absent")) +
           " proton=" +
           (runtime.proton_installed ? runtime.proton_bin : std::string("absent")) +
           " dxvk=" + (runtime.dxvk_available ? "present" : "absent") +
           " vkd3d=" + (runtime.vkd3d_available ? "present" : "absent") +
           " emulator=" +
           (runtime.emulator_installed ? runtime.emulator_bin : std::string("absent")) +
           " host=" + runtime.host_arch + "\n";
    if (dll_plan.overrides.empty()) {
        out += "overrides: none\n";
    } else {
        out += "overrides:";
        for (const std::string& o : dll_plan.overrides) out += " " + o;
        out += "\n";
    }
    for (const std::string& note : dll_plan.diagnostics) {
        out += "note: " + note + "\n";
    }
    if (prefix_id.empty()) {
        out += "prefix: none (no safe per-app prefix id derived from the exe path)\n";
    } else {
        const PrefixInfo pinfo = prefix_info(prefix_id);
        out += "prefix: id=" + pinfo.id +
               " path=" + (pinfo.path.empty() ? std::string("<unresolved>") : pinfo.path) +
               " arch=" + (pinfo.arch.empty() ? "unknown" : pinfo.arch) +
               " drive_c=" + (pinfo.drive_c_present ? "present" : "absent") +
               " system.reg=" + (pinfo.system_reg_present ? "present" : "absent") + "\n";
    }
    return out;
}

std::vector<WindowsAppEntry> load_windows_apps(const std::string& conf_path) {
    std::ifstream in(conf_path);
    if (!in) return {};
    std::vector<WindowsAppEntry> entries;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (start <= line.size()) {
            const std::size_t bar = line.find('|', start);
            if (bar == std::string::npos) {
                fields.emplace_back(line.substr(start));
                break;
            }
            fields.emplace_back(line.substr(start, bar - start));
            start = bar + 1;
        }
        if (fields.size() < 3) continue;
        WindowsAppEntry app;
        app.id = fields[0];
        app.name = fields[1];
        app.exe_path = fields[2];
        if (fields.size() >= 4) app.prefix = fields[3];
        if (fields.size() >= 5) app.args = fields[4];
        if (fields.size() >= 6 && !fields[5].empty()) app.icon = fields[5];
        else app.icon = "windows.svg";
        if (!app.id.empty() && !app.name.empty() && !app.exe_path.empty()) {
            entries.push_back(std::move(app));
        }
    }
    return entries;
}

bool save_windows_app(const std::string& conf_path, const WindowsAppEntry& entry,
                      std::string* error) {
    if (entry.id.empty() || entry.name.empty() || entry.exe_path.empty()) {
        if (error) *error = "invalid windows app entry";
        return false;
    }
    std::vector<WindowsAppEntry> entries = load_windows_apps(conf_path);
    bool updated = false;
    for (auto& e : entries) {
        if (e.id == entry.id) {
            e = entry;
            updated = true;
            break;
        }
    }
    if (!updated) entries.push_back(entry);

    std::error_code ec;
    fs::create_directories(fs::path(conf_path).parent_path(), ec);
    std::ofstream out(conf_path, std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + conf_path;
        return false;
    }
    for (const auto& e : entries) {
        out << sanitize_field(e.id) << '|' << sanitize_field(e.name) << '|'
            << sanitize_field(e.exe_path) << '|' << sanitize_field(e.prefix) << '|'
            << sanitize_field(e.args) << '|'
            << sanitize_field(e.icon.empty() ? "windows.svg" : e.icon) << '\n';
    }
    return out.good();
}

bool remove_windows_app(const std::string& conf_path, std::string_view id,
                        std::string* error) {
    std::vector<WindowsAppEntry> entries = load_windows_apps(conf_path);
    std::vector<WindowsAppEntry> kept;
    for (auto& e : entries) {
        if (e.id != id) kept.push_back(std::move(e));
    }
    std::error_code ec;
    fs::create_directories(fs::path(conf_path).parent_path(), ec);
    std::ofstream out(conf_path, std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + conf_path;
        return false;
    }
    for (const auto& e : kept) {
        out << sanitize_field(e.id) << '|' << sanitize_field(e.name) << '|'
            << sanitize_field(e.exe_path) << '|' << sanitize_field(e.prefix) << '|'
            << sanitize_field(e.args) << '|'
            << sanitize_field(e.icon.empty() ? "windows.svg" : e.icon) << '\n';
    }
    return out.good();
}

}  // namespace finn::compat
