#include "compat/msi_installer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lower_str(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower_ascii(c);
    return out;
}

std::string basename_of(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// Mirrors Python's os.path.splitext: the extension is the final dot of the
// basename, and a leading or trailing dot of the name is not an extension.
std::string extension_of(std::string_view path) {
    const std::string base = basename_of(path);
    const std::size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == base.size()) return {};
    return lower_str(base.substr(dot));
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

struct ExeSilentGuess {
    std::vector<std::string> argv;
    std::string note;
};

// Best guess of a well-known installer family's silent switch from the file
// name. Arbitrary installers have no known switch; the caller notes that.
ExeSilentGuess exe_silent_guess(std::string_view name_lower) {
    // NSIS installers are commonly named *_setup.exe or contain "nsis".
    if (name_lower.find("nsis") != std::string_view::npos ||
        ends_with(name_lower, "_setup.exe")) {
        return {{"/S"},
                "NSIS detected from the filename; its silent switch is /S - verify the installer honors it"};
    }
    if (name_lower.find("inno") != std::string_view::npos) {
        return {{"/VERYSILENT", "/NORESTART"},
                "Inno Setup detected from the filename; its silent switch is /VERYSILENT /NORESTART - verify the installer honors it"};
    }
    return {{}, ""};
}

}  // namespace

WindowsPackageKind detect_windows_package_kind(std::string_view path) {
    const std::string ext = extension_of(path);
    if (ext == ".msi") return WindowsPackageKind::Msi;
    if (ext == ".exe") return WindowsPackageKind::Exe;
    if (ext == ".msix") return WindowsPackageKind::Msix;
    if (ext == ".bat") return WindowsPackageKind::Bat;
    if (ext == ".cmd") return WindowsPackageKind::Cmd;
    return WindowsPackageKind::Unknown;
}

const char* windows_package_kind_label(WindowsPackageKind kind) {
    switch (kind) {
        case WindowsPackageKind::Exe: return "EXE";
        case WindowsPackageKind::Msi: return "MSI";
        case WindowsPackageKind::Msix: return "MSIX";
        case WindowsPackageKind::Bat: return "BAT";
        case WindowsPackageKind::Cmd: return "CMD";
        case WindowsPackageKind::Unknown: break;
    }
    return "Unknown";
}

bool is_product_code(std::string_view target) {
    std::string g(target);
    if (g.size() == 38 && g.front() == '{' && g.back() == '}') {
        g = g.substr(1, 36);
    }
    const auto hex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    if (g.size() == 32) {
        return std::all_of(g.begin(), g.end(), hex);
    }
    if (g.size() == 36) {
        if (g[8] != '-' || g[13] != '-' || g[18] != '-' || g[23] != '-') {
            return false;
        }
        for (std::size_t i = 0; i < g.size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) continue;
            if (!hex(g[i])) return false;
        }
        return true;
    }
    return false;
}

InstallPlan build_install_plan(const std::string& path,
                               const std::string& prefix,
                               const InstallOptions& options) {
    InstallPlan plan;
    plan.kind = detect_windows_package_kind(path);
    if (!prefix.empty()) plan.env.emplace_back("WINEPREFIX", prefix);

    const auto append_extras = [&](std::vector<std::string>& out) {
        out.insert(out.end(), options.extra_args.begin(), options.extra_args.end());
    };

    switch (plan.kind) {
        case WindowsPackageKind::Msi: {
            if (!file_exists(path)) {
                plan.error = "installer file not found: " + path;
                break;
            }
            plan.argv = {"msiexec", "/i", path};
            if (options.silent) {
                plan.argv.push_back("/qn");
                plan.argv.push_back("/norestart");
            }
            if (!options.log_path.empty()) {
                plan.argv.push_back("/L*v");
                plan.argv.push_back(options.log_path);
            }
            append_extras(plan.argv);
            if (!prefix.empty()) {
                // MSI has no universal prefix property, so the plan does not
                // guess one; the caller can verify INSTALLDIR/TARGETDIR.
                plan.notes.push_back("prefix '" + prefix +
                                     "' has no universal MSI property; the plan does not guess one "
                                     "(verify INSTALLDIR/TARGETDIR)");
            }
            plan.ok = true;
            break;
        }
        case WindowsPackageKind::Exe: {
            if (!file_exists(path)) {
                plan.error = "installer file not found: " + path;
                break;
            }
            const std::string base = basename_of(path);
            plan.argv = {path};
            if (options.silent) {
                const ExeSilentGuess guess = exe_silent_guess(lower_str(base));
                if (!guess.argv.empty()) {
                    plan.argv.insert(plan.argv.end(), guess.argv.begin(), guess.argv.end());
                    plan.notes.push_back(guess.note);
                } else {
                    plan.notes.push_back("silent install requested but no silent switch was "
                                         "identified for '" + base +
                                         "'; the installer's own switch is unknown");
                }
            }
            append_extras(plan.argv);
            plan.ok = true;
            break;
        }
        case WindowsPackageKind::Bat:
        case WindowsPackageKind::Cmd: {
            if (!file_exists(path)) {
                plan.error = "installer file not found: " + path;
                break;
            }
            plan.argv = {"cmd", "/c", path};
            append_extras(plan.argv);
            plan.ok = true;
            break;
        }
        case WindowsPackageKind::Msix: {
            plan.error = "MSIX packages cannot be installed by Wine here: AppX/App Installer is "
                         "not implemented in FinnOS; install it natively or use Finn Store";
            break;
        }
        case WindowsPackageKind::Unknown:
        default:
            plan.error = "unrecognized package extension for '" + path +
                         "' (expected .msi/.exe/.msix/.bat/.cmd)";
            break;
    }
    plan.silent = options.silent;
    return plan;
}

InstallPlan build_uninstall_plan(const std::string& target,
                                 const std::string& prefix,
                                 const InstallOptions& options) {
    InstallPlan plan;
    if (!prefix.empty()) plan.env.emplace_back("WINEPREFIX", prefix);

    if (target.empty()) {
        plan.error = "nothing to uninstall: no target given";
        plan.silent = options.silent;
        return plan;
    }

    const bool code = is_product_code(target);
    plan.kind = code ? WindowsPackageKind::Msi : detect_windows_package_kind(target);

    if (code) {
        plan.argv = {"msiexec", "/x", target};
        if (options.silent) plan.argv.push_back("/qn");
        plan.argv.push_back("/norestart");
        if (!options.log_path.empty()) {
            plan.argv.push_back("/L*v");
            plan.argv.push_back(options.log_path);
        }
        plan.argv.insert(plan.argv.end(), options.extra_args.begin(), options.extra_args.end());
        plan.ok = true;
    } else if (plan.kind == WindowsPackageKind::Msi) {
        if (!file_exists(target)) {
            plan.error = "msi package not found: " + target;
        } else {
            plan.argv = {"msiexec", "/x", target, "/qn", "/norestart"};
            if (!options.log_path.empty()) {
                plan.argv.push_back("/L*v");
                plan.argv.push_back(options.log_path);
            }
            plan.argv.insert(plan.argv.end(), options.extra_args.begin(), options.extra_args.end());
            plan.ok = true;
        }
    } else if (plan.kind == WindowsPackageKind::Exe || plan.kind == WindowsPackageKind::Bat ||
               plan.kind == WindowsPackageKind::Cmd || plan.kind == WindowsPackageKind::Msix) {
        plan.error = std::string("cannot uninstall a ") + windows_package_kind_label(plan.kind) +
                     " package through msiexec here; pass an .msi path or an MSI product code";
    } else {
        plan.error = "nothing sensible to uninstall: '" + target +
                     "' is not an .msi path or an MSI product code";
    }
    plan.silent = options.silent;
    return plan;
}

// ---- magic-based inspection ------------------------------------------------

namespace {

std::string_view trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower_ascii(c);
    return out;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    return ascii_lower(s.substr(s.size() - suffix.size())) == ascii_lower(suffix);
}

bool have(std::string_view b, std::size_t off, std::size_t len) {
    return off <= b.size() && len <= b.size() - off;
}

std::uint16_t u16(std::string_view b, std::size_t off) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off + 1])) << 8);
}

std::uint32_t u32(std::string_view b, std::size_t off) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 3])) << 24);
}

bool starts_with(std::string_view b, std::string_view magic) {
    return b.size() >= magic.size() && b.substr(0, magic.size()) == magic;
}

constexpr std::string_view kMsiMagic = "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1";
constexpr std::size_t kMaxCompoundBytes = 64u * 1024u * 1024u;

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::string utf16le_to_utf8(std::string_view bytes) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const std::uint16_t unit = u16(bytes, i);
        std::uint32_t cp = unit;
        if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < bytes.size()) {
            const std::uint16_t low = u16(bytes, i + 2);
            if (low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + ((static_cast<std::uint32_t>(unit) - 0xd800) << 10) +
                     (static_cast<std::uint32_t>(low) - 0xdc00);
                i += 2;
            } else {
                cp = 0xfffd;
            }
        } else if (unit >= 0xd800 && unit <= 0xdfff) {
            cp = 0xfffd;
        }
        append_utf8(out, cp);
    }
    return out;
}

bool is_guid(std::string_view text) {
    if (text.size() != 38 || text.front() != '{' || text.back() != '}') return false;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (c == '-') continue;
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

std::string find_guid(std::string_view text) {
    std::size_t pos = 0;
    while ((pos = text.find('{', pos)) != std::string_view::npos) {
        const std::size_t close = text.find('}', pos);
        if (close == std::string_view::npos) break;
        if (is_guid(text.substr(pos, close - pos + 1))) {
            return std::string(text.substr(pos, close - pos + 1));
        }
        pos = close + 1;
    }
    return {};
}

MsiPlan plan_from_argv(std::vector<std::string> argv, const MsiOptions& opts,
                       WindowsInstallerKind kind, std::string display_name,
                       std::string note) {
    MsiPlan plan;
    if (!opts.runner.empty()) argv.insert(argv.begin(), opts.runner);
    if (argv.empty()) return plan;
    const std::vector<std::string> quiet = msi_quiet_flags(opts.quiet, opts.log_path);
    argv.insert(argv.end(), quiet.begin(), quiet.end());
    argv.insert(argv.end(), opts.extra_args.begin(), opts.extra_args.end());
    plan.argv = std::move(argv);
    plan.kind = kind;
    plan.display_name = std::move(display_name);
    plan.note = std::move(note);
    if (!opts.prefix.empty()) plan.env.emplace_back("WINEPREFIX", opts.prefix);
    plan.ok = true;
    return plan;
}

}  // namespace

std::string windows_installer_kind_name(WindowsInstallerKind kind) {
    switch (kind) {
        case WindowsInstallerKind::PeExe: return "Windows PE executable";
        case WindowsInstallerKind::Msi: return "Windows MSI installer";
        case WindowsInstallerKind::Msix: return "Windows MSIX package";
        case WindowsInstallerKind::Unknown: break;
    }
    return "Unknown";
}

std::string msi_quiet_flag(MsiQuiet level) {
    switch (level) {
        case MsiQuiet::Basic: return "/qb";
        case MsiQuiet::Passive: return "/passive";
        case MsiQuiet::Silent: return "/qn";
        case MsiQuiet::None: break;
    }
    return {};
}

std::vector<std::string> msi_quiet_flags(MsiQuiet level, const std::string& log_path) {
    std::vector<std::string> flags;
    const std::string flag = msi_quiet_flag(level);
    if (!flag.empty()) flags.push_back(flag);
    flags.push_back("/norestart");
    if (!log_path.empty()) {
        flags.push_back("/log");
        flags.push_back(log_path);  // one argv element; no shell quoting involved
    }
    return flags;
}

WindowsInstallerKind detect_windows_installer_kind(std::string_view path) {
    if (ends_with_ci(path, ".msi")) return WindowsInstallerKind::Msi;
    if (ends_with_ci(path, ".msix") || ends_with_ci(path, ".appx")) {
        return WindowsInstallerKind::Msix;
    }
    if (ends_with_ci(path, ".exe")) return WindowsInstallerKind::PeExe;
    return WindowsInstallerKind::Unknown;
}

std::vector<std::string> read_compound_stream_names(std::string_view bytes) {
    std::vector<std::string> names;
    if (!starts_with(bytes, kMsiMagic) || bytes.size() < 512) return names;
    const std::uint16_t shift = u16(bytes, 30);
    if (shift < 9 || shift > 12) return names;
    const std::size_t sector_size = std::size_t{1} << shift;
    const std::size_t header_size = (sector_size == 4096) ? 4096 : 512;
    if (bytes.size() < header_size) return names;

    const std::uint32_t first_dir = u32(bytes, 48);
    const std::uint32_t num_difat = u32(bytes, 72);
    std::vector<std::uint32_t> difat;
    difat.reserve(128);
    for (std::size_t i = 0; i < 109; ++i) difat.push_back(u32(bytes, 76 + 4 * i));

    std::uint32_t difat_sector = u32(bytes, 68);
    for (std::uint32_t n = 0; n < num_difat && difat_sector < 0xfffffffa; ++n) {
        const std::size_t per_sector = sector_size / 4 - 1;
        if (!have(bytes, header_size + static_cast<std::size_t>(difat_sector) * sector_size,
                  sector_size)) {
            break;
        }
        const std::size_t base = header_size + static_cast<std::size_t>(difat_sector) * sector_size;
        for (std::size_t i = 0; i < per_sector; ++i) difat.push_back(u32(bytes, base + 4 * i));
        difat_sector = u32(bytes, base + 4 * per_sector);
    }

    std::vector<std::uint32_t> fat;
    for (std::uint32_t sector : difat) {
        if (sector >= 0xfffffffa) break;
        if (!have(bytes, header_size + static_cast<std::size_t>(sector) * sector_size,
                  sector_size)) {
            break;
        }
        const std::size_t base = header_size + static_cast<std::size_t>(sector) * sector_size;
        for (std::size_t i = 0; i < sector_size / 4; ++i) fat.push_back(u32(bytes, base + 4 * i));
    }

    std::uint32_t sector = first_dir;
    std::vector<std::uint32_t> dir_sectors;
    while (sector < 0xfffffffa && dir_sectors.size() < 4096) {
        if (sector >= fat.size()) break;
        dir_sectors.push_back(sector);
        sector = fat[sector];
    }
    for (std::uint32_t ds : dir_sectors) {
        if (!have(bytes, header_size + static_cast<std::size_t>(ds) * sector_size,
                  sector_size)) {
            break;
        }
        const std::size_t base = header_size + static_cast<std::size_t>(ds) * sector_size;
        for (std::size_t e = 0; e < sector_size / 128; ++e) {
            const std::size_t entry = base + e * 128;
            const std::uint16_t name_len = u16(bytes, entry + 64);
            const std::uint8_t type = static_cast<std::uint8_t>(bytes[entry + 66]);
            if (name_len < 2 || name_len > 64 || (name_len % 2) != 0) continue;
            if (type != 1 && type != 2) continue;  // storage or stream
            if (!have(bytes, entry, name_len)) continue;
            const std::string name = utf16le_to_utf8(bytes.substr(entry, name_len - 2));
            if (!name.empty()) names.push_back(name);
        }
    }
    return names;
}

WindowsInstallerInfo inspect_windows_installer_bytes(std::string_view bytes,
                                                     std::string_view path_hint) {
    WindowsInstallerInfo info;
    info.path = std::string(path_hint);
    if (bytes.size() < 2) {
        info.error = "file too short to identify installer magic";
        return info;
    }
    if (starts_with(bytes, "MZ")) {
        info.kind = WindowsInstallerKind::PeExe;
        info.valid = true;
        info.installable = true;
        info.pe = inspect_pe_bytes(bytes.substr(0, std::min<std::size_t>(bytes.size(), 8192)));
        info.format_detail = info.pe.valid
                                 ? "PE executable (" + pe_arch_name(info.pe.arch) + ")"
                                 : "DOS MZ executable (PE header not recognized in inspected bytes)";
    } else if (starts_with(bytes, kMsiMagic)) {
        info.kind = WindowsInstallerKind::Msi;
        info.valid = true;
        info.installable = true;
        info.format_detail = "OLE compound document with MSI magic";
    } else if (starts_with(bytes, "PK\x03\x04") || starts_with(bytes, "PK\x05\x06") ||
               starts_with(bytes, "PK\x07\x08")) {
        if (ends_with_ci(path_hint, ".msix") || ends_with_ci(path_hint, ".appx")) {
            info.kind = WindowsInstallerKind::Msix;
            info.valid = true;
            info.installable = false;
            info.format_detail = "ZIP-based MSIX/APPX package";
            info.unsupported_reason =
                "MSIX installation is not implemented in FinnOS v1; the package is detected "
                "but no msiexec plan can be built. Use an MSI installer instead.";
        } else {
            info.format_detail = "generic ZIP archive";
            info.error = "ZIP archive is not a supported Windows installer format";
        }
    } else {
        info.error = "unrecognized installer magic (expected MZ, D0 CF 11 E0 A1 B1 1A E1, or PK)";
    }
    return info;
}

WindowsInstallerInfo inspect_windows_installer(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        WindowsInstallerInfo info;
        info.path = path;
        info.error = "file does not exist: " + path;
        return info;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        WindowsInstallerInfo info;
        info.path = path;
        info.error = "cannot open file: " + path;
        return info;
    }
    std::string head(8192, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));
    WindowsInstallerInfo info = inspect_windows_installer_bytes(head, path);

    if (info.kind == WindowsInstallerKind::Msi) {
        const auto size = fs::file_size(path, ec);
        if (!ec && size <= kMaxCompoundBytes) {
            in.clear();
            in.seekg(0);
            std::string content;
            content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            info.streams = read_compound_stream_names(content);
            bool has_property = false;
            bool has_feature = false;
            for (const std::string& name : info.streams) {
                const std::string lower = ascii_lower(name);
                has_property |= lower == "property" || lower == "\x05" "summaryinformation";
                has_feature |= lower == "feature";
            }
            info.streams_verified = has_property && has_feature;
            info.format_detail += info.streams_verified
                                      ? "; MSI directory streams verified"
                                      : "; MSI directory streams not verified (unrecognized OLE compound file)";
        } else {
            info.format_detail += "; directory streams not checked (file too large to read)";
        }
    }
    return info;
}

MsiPlan build_msi_install_plan(const std::string& path, const MsiOptions& opts) {
    const WindowsInstallerInfo info = inspect_windows_installer(path);
    MsiPlan plan;
    plan.kind = info.kind;
    plan.display_name = fs::path(path).stem().string();
    if (!info.valid) {
        plan.error = info.error.empty() ? "not a recognized Windows installer" : info.error;
        return plan;
    }
    if (!info.installable) {
        plan.error = info.unsupported_reason;
        return plan;
    }
    if (info.kind == WindowsInstallerKind::Msi) {
        plan = plan_from_argv({"msiexec", "/i", path}, opts, info.kind,
                              plan.display_name, info.format_detail);
        plan.action_label = "Install MSI";
    } else {
        plan = plan_from_argv({path}, opts, info.kind, plan.display_name,
                              "PE installer: silent switches are installer-specific and are not assumed.");
        plan.action_label = "Run Windows installer";
    }
    return plan;
}

MsiPlan build_msi_uninstall_plan(const std::string& product_code_or_path,
                                 const MsiOptions& opts) {
    MsiPlan plan;
    const std::string value = std::string(trim(product_code_or_path));
    if (value.empty()) {
        plan.error = "empty product code or path";
        return plan;
    }
    if (is_guid(value)) {
        plan = plan_from_argv({"msiexec", "/x", value}, opts, WindowsInstallerKind::Msi,
                              value, "Uninstall by MSI product code.");
        plan.action_label = "Uninstall MSI product";
        return plan;
    }
    std::error_code ec;
    if (fs::is_regular_file(value, ec)) {
        const WindowsInstallerInfo info = inspect_windows_installer(value);
        plan.kind = info.kind;
        plan.display_name = fs::path(value).stem().string();
        if (!info.valid) {
            plan.error = info.error;
            return plan;
        }
        if (!info.installable) {
            plan.error = info.unsupported_reason;
            return plan;
        }
        if (info.kind == WindowsInstallerKind::Msi) {
            plan = plan_from_argv({"msiexec", "/x", value}, opts, info.kind,
                                  plan.display_name, "Uninstall using the original MSI package.");
        } else {
            plan = plan_from_argv({value}, opts, info.kind, plan.display_name,
                                  "Executable uninstaller; switches are installer-specific.");
        }
        plan.action_label = "Uninstall Windows application";
        return plan;
    }
    std::string parse_error;
    const auto tokens = parse_uninstall_command(value, &parse_error);
    if (!parse_error.empty() || tokens.empty()) {
        plan.error = "not a product code GUID, an existing installer file, or a safe uninstall command: " +
                     parse_error;
        return plan;
    }
    plan = plan_from_argv(tokens, opts, WindowsInstallerKind::Unknown, {},
                          "Uninstall command tokenized into argv; no shell is involved.");
    plan.action_label = "Uninstall Windows application";
    return plan;
}

MsiPlan build_msi_uninstall_plan(const UninstallEntry& entry, const MsiOptions& opts) {
    const std::string product_code = msi_product_code_from_uninstall(entry);
    if (!product_code.empty()) {
        MsiPlan plan = build_msi_uninstall_plan(product_code, opts);
        if (plan.ok && !entry.display_name.empty()) plan.display_name = entry.display_name;
        return plan;
    }
    const std::string command = entry.quiet_uninstall_string.empty()
                                    ? entry.uninstall_string
                                    : entry.quiet_uninstall_string;
    if (!command.empty()) {
        MsiPlan plan = build_msi_uninstall_plan(command, opts);
        if (plan.ok && !entry.display_name.empty()) plan.display_name = entry.display_name;
        return plan;
    }
    MsiPlan plan;
    plan.error = "registry entry has no product code or usable uninstall command";
    return plan;
}

std::string msi_product_code_from_key(std::string_view registry_key) {
    const std::size_t open = registry_key.rfind('{');
    if (open == std::string_view::npos) return {};
    const std::size_t close = registry_key.find('}', open);
    if (close == std::string_view::npos) return {};
    const std::string code = std::string(registry_key.substr(open, close - open + 1));
    return is_guid(code) ? code : std::string{};
}

std::string msi_product_code_from_uninstall(const UninstallEntry& entry) {
    std::string code = msi_product_code_from_key(entry.key_path);
    if (!code.empty()) return code;
    code = find_guid(entry.uninstall_string);
    if (!code.empty()) return code;
    return find_guid(entry.quiet_uninstall_string);
}

std::vector<std::string> parse_uninstall_command(std::string_view command,
                                                 std::string* error) {
    std::vector<std::string> argv;
    const std::string_view text = trim(command);
    const std::string_view forbidden = "|;&<>`$\n\r";
    if (text.find_first_of(forbidden) != std::string_view::npos) {
        if (error) *error = "shell control character in uninstall command";
        return argv;
    }
    std::string current;
    bool in_quotes = false;
    bool started = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            in_quotes = !in_quotes;
            started = true;
        } else if ((c == ' ' || c == '\t') && !in_quotes) {
            if (started) {
                argv.push_back(current);
                current.clear();
                started = false;
            }
        } else if (c == '\\' && i + 1 < text.size() && text[i + 1] == '"') {
            current.push_back('"');
            ++i;
            started = true;
        } else {
            current.push_back(c);
            started = true;
        }
    }
    if (in_quotes) {
        if (error) *error = "unterminated quote in uninstall command";
        return {};
    }
    if (started) argv.push_back(current);
    return argv;
}

bool msi_installer_self_test() {
    constexpr std::string_view guid = "{11111111-2222-3333-4444-555555555555}";
    const auto pe = inspect_windows_installer_bytes("MZ stub");
    if (!pe.valid || pe.kind != WindowsInstallerKind::PeExe || !pe.installable) return false;
    const WindowsInstallerInfo msi = inspect_windows_installer_bytes(kMsiMagic);
    if (!msi.valid || msi.kind != WindowsInstallerKind::Msi || !msi.installable) return false;
    const WindowsInstallerInfo msix = inspect_windows_installer_bytes("PK\x03\x04rest", "app.msix");
    if (!msix.valid || msix.kind != WindowsInstallerKind::Msix || msix.installable) return false;
    if (msix.unsupported_reason.find("not implemented") == std::string::npos) return false;
    if (inspect_windows_installer_bytes("PK\x03\x04rest", "random.zip").valid) return false;
    if (msi_quiet_flag(MsiQuiet::Silent) != "/qn") return false;
    const auto flags = msi_quiet_flags(MsiQuiet::Silent, "/tmp/install.log");
    if (flags.size() != 4 || flags[0] != "/qn" || flags[1] != "/norestart" ||
        flags[2] != "/log" || flags[3] != "/tmp/install.log") {
        return false;
    }
    std::string err;
    const auto tokens = parse_uninstall_command("msiexec /x " + std::string(guid) + " /qn", &err);
    if (!err.empty() || tokens.size() != 4 || tokens[0] != "msiexec" ||
        tokens[2] != guid || tokens[3] != "/qn") {
        return false;
    }
    if (!parse_uninstall_command("foo | bar", &err).empty() || err.empty()) return false;
    const std::string key = "HKEY_LOCAL_MACHINE\\...\\Uninstall\\" + std::string(guid);
    if (msi_product_code_from_key(key) != guid) return false;
    const MsiPlan uninstall = build_msi_uninstall_plan(std::string(guid));
    if (!uninstall.ok || uninstall.argv.size() < 3 || uninstall.argv[1] != "msiexec" ||
        uninstall.argv[2] != "/x") {
        return false;
    }
    if (!read_compound_stream_names("not a compound file").empty()) return false;
    return true;
}

bool windows_package_self_test() {
    if (detect_windows_package_kind("Setup.EXE") != WindowsPackageKind::Exe) return false;
    if (detect_windows_package_kind("app.MSI") != WindowsPackageKind::Msi) return false;
    if (detect_windows_package_kind("pack.MSIX") != WindowsPackageKind::Msix) return false;
    if (detect_windows_package_kind("notes.txt") != WindowsPackageKind::Unknown) return false;
    if (!is_product_code("{11111111-2222-3333-4444-555555555555}")) return false;
    if (is_product_code("not-a-guid")) return false;
    const auto plan = build_uninstall_plan("{11111111-2222-3333-4444-555555555555}");
    if (!plan.ok || plan.argv.size() < 2 || plan.argv[0] != "msiexec" ||
        plan.argv[1] != "/x") {
        return false;
    }
    return true;
}

}  // namespace finn::compat
