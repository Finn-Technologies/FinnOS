#include "compat/wine_runner.hpp"

#include "common/paths.hpp"
#include "compat/desktop_entry.hpp"

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
    return inspect_pe_bytes(buf);
}

WineRuntimeStatus probe_wine_runtime() {
    WineRuntimeStatus status;
    struct utsname u{};
    if (::uname(&u) == 0) status.host_arch = u.machine;
    else status.host_arch = "unknown";

    if (executable_exists("wine")) {
        status.wine_installed = true;
        status.wine_bin = "wine";
    } else if (executable_exists("wine64")) {
        status.wine_installed = true;
        status.wine_bin = "wine64";
    }

    if (executable_exists("box64")) {
        status.emulator_installed = true;
        status.emulator_bin = "box64";
    } else if (executable_exists("FEXBash")) {
        status.emulator_installed = true;
        status.emulator_bin = "FEXBash";
    }

    std::error_code ec;
    status.xwayland_available = executable_exists("Xwayland") ||
                                fs::exists(kXwaylandDisplayFile, ec);
    status.default_prefix = default_wine_prefix_path();
    status.prefix_initialized = fs::is_directory(fs::path(status.default_prefix) / "drive_c", ec);

    if (status.wine_installed) {
        status.ready = true;
        status.summary = "Wine ready (" + status.wine_bin + " on " + status.host_arch + ")";
    } else if (status.host_arch == "aarch64" || status.host_arch == "arm64") {
        status.ready = false;
        status.summary =
            "Windows (.exe) runtime requires x86_64 Wine or an x86_64 compatibility "
            "translator (box64/fex-emu) on aarch64; not installed on this host.";
    } else {
        status.ready = false;
        status.summary = "Wine is not installed on this system (install wine package on x86_64).";
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
        return plan;
    }

    const bool is_msi = ends_with_ci(exe_path, ".msi");
    if (!is_msi) {
        const PeInfo pe = inspect_pe_file(exe_path);
        if (!pe.valid) {
            plan.error = "not a valid Windows PE executable (" + pe.error + ")";
            return plan;
        }
    }

    const WineRuntimeStatus runtime = probe_wine_runtime();
    if (!runtime.ready) {
        plan.error = runtime.summary;
        return plan;
    }

    const std::string resolved_prefix = prefix.empty() ? runtime.default_prefix : prefix;
    plan.env.emplace_back("WINEPREFIX", resolved_prefix);
    plan.env.emplace_back("WINEARCH", "win64");
    plan.env.emplace_back("WINEDEBUG", "-all");
    if (!xdisplay.empty()) plan.env.emplace_back("DISPLAY", xdisplay);

    if (is_msi) {
        plan.argv = {runtime.wine_bin, "msiexec", "/i", exe_path};
    } else {
        plan.argv = {runtime.wine_bin, exe_path};
    }
    for (const std::string& arg : extra_args) plan.argv.push_back(arg);
    plan.ok = true;
    return plan;
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
