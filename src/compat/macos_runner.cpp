#include "compat/macos_runner.hpp"

#include "common/paths.hpp"
#include "compat/desktop_entry.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

std::uint32_t read_u32_le(std::string_view b, std::size_t off) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[off])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3])) << 24);
}

std::uint32_t read_u32_be(std::string_view b, std::size_t off) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3]));
}

MachoArch cputype_to_arch(std::uint32_t cputype) {
    // Apple Mach-O CPU_TYPE_* constants (ABI64 bit = 0x01000000).
    switch (cputype) {
        case 0x00000007u: return MachoArch::X86;
        case 0x01000007u: return MachoArch::X86_64;
        case 0x0000000cu: return MachoArch::Arm;
        case 0x0100000cu: return MachoArch::Arm64;
        default: return MachoArch::Unknown;
    }
}

void push_unique_arch(std::vector<MachoArch>& list, MachoArch a) {
    if (a == MachoArch::Unknown) return;
    if (std::find(list.begin(), list.end(), a) == list.end()) {
        list.push_back(a);
    }
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

std::string host_machine() {
    struct utsname uts{};
    if (::uname(&uts) == 0) return uts.machine;
    return "unknown";
}

std::string extract_plist_string(std::string_view xml, std::string_view key) {
    const std::string needle = "<key>" + std::string(key) + "</key>";
    const std::size_t kpos = xml.find(needle);
    if (kpos == std::string_view::npos) return {};
    const std::size_t after_key = kpos + needle.size();
    const std::size_t open = xml.find("<string>", after_key);
    if (open == std::string_view::npos) return {};
    // Guard against skipping past another <key> if this key has a non-string value.
    const std::size_t next_key = xml.find("<key>", after_key);
    if (next_key != std::string_view::npos && next_key < open) return {};
    const std::size_t val_start = open + 8;
    const std::size_t close = xml.find("</string>", val_start);
    if (close == std::string_view::npos) return {};
    return std::string(xml.substr(val_start, close - val_start));
}

}  // namespace

std::string macho_arch_name(MachoArch arch) {
    switch (arch) {
        case MachoArch::X86: return "x86";
        case MachoArch::X86_64: return "x86_64";
        case MachoArch::Arm: return "arm";
        case MachoArch::Arm64: return "arm64";
        case MachoArch::Unknown: break;
    }
    return "unknown";
}

std::string macho_arches_summary(const MachoInfo& info) {
    if (!info.valid || info.arches.empty()) return "unknown";
    std::string out;
    for (std::size_t i = 0; i < info.arches.size(); ++i) {
        if (i > 0) out += ", ";
        out += macho_arch_name(info.arches[i]);
    }
    if (info.is_universal) out += " (Universal)";
    return out;
}

MachoInfo inspect_macho_bytes(std::string_view bytes) {
    MachoInfo info;
    if (bytes.size() < 8) {
        info.error = "file smaller than Mach-O header";
        return info;
    }

    const std::uint32_t magic_be = read_u32_be(bytes, 0);
    const std::uint32_t magic_le = read_u32_le(bytes, 0);

    // Thin Mach-O (little-endian host or big-endian)
    if (magic_le == 0xfeedfaceu || magic_le == 0xfeedfacfu) {
        const std::uint32_t cputype = read_u32_le(bytes, 4);
        push_unique_arch(info.arches, cputype_to_arch(cputype));
        info.valid = true;
        return info;
    }
    if (magic_be == 0xfeedfaceu || magic_be == 0xfeedfacfu) {
        const std::uint32_t cputype = read_u32_be(bytes, 4);
        push_unique_arch(info.arches, cputype_to_arch(cputype));
        info.valid = true;
        return info;
    }

    // Universal / Fat Mach-O (always big-endian fat_header; guard against Java .class)
    if (magic_be == 0xcafebabeu || magic_be == 0xcafebabfu) {
        const bool fat64 = (magic_be == 0xcafebabfu);
        const std::uint32_t nfat = read_u32_be(bytes, 4);
        if (nfat == 0 || nfat > 16) {
            info.error = "not a valid Universal Mach-O header";
            return info;
        }
        const std::size_t entry_size = fat64 ? 32 : 20;
        if (8 + static_cast<std::size_t>(nfat) * entry_size > bytes.size()) {
            info.error = "truncated Universal Mach-O header";
            return info;
        }
        info.is_universal = true;
        for (std::uint32_t i = 0; i < nfat; ++i) {
            const std::uint32_t cputype = read_u32_be(bytes, 8 + i * entry_size);
            push_unique_arch(info.arches, cputype_to_arch(cputype));
        }
        if (info.arches.empty()) {
            info.error = "no recognized CPU architectures in Universal Mach-O";
            return info;
        }
        info.valid = true;
        return info;
    }

    info.error = "missing Mach-O magic";
    return info;
}

MachoInfo inspect_macho_file(const std::string& path) {
    MachoInfo info;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        info.error = "unable to open Mach-O file";
        return info;
    }
    std::string buf(4096, '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return inspect_macho_bytes(buf);
}

MacosAppInfo parse_macos_info_plist(std::string_view xml_content,
                                    const std::string& bundle_path) {
    MacosAppInfo info;
    info.bundle_path = bundle_path;
    if (xml_content.find("<plist") == std::string_view::npos &&
        xml_content.find("<dict>") == std::string_view::npos) {
        info.error = "Info.plist is not an XML property list";
        return info;
    }
    info.name = extract_plist_string(xml_content, "CFBundleDisplayName");
    if (info.name.empty()) {
        info.name = extract_plist_string(xml_content, "CFBundleName");
    }
    info.bundle_id = extract_plist_string(xml_content, "CFBundleIdentifier");
    info.version = extract_plist_string(xml_content, "CFBundleShortVersionString");
    if (info.version.empty()) {
        info.version = extract_plist_string(xml_content, "CFBundleVersion");
    }
    info.executable_name = extract_plist_string(xml_content, "CFBundleExecutable");

    if (info.name.empty() && !bundle_path.empty()) {
        info.name = fs::path(bundle_path).stem().string();
    }
    if (info.name.empty() && info.executable_name.empty()) {
        info.error = "Info.plist missing CFBundleName and CFBundleExecutable";
        return info;
    }
    info.valid = true;
    return info;
}

MacosAppInfo inspect_macos_app_bundle(const std::string& app_bundle_path) {
    MacosAppInfo info;
    info.bundle_path = app_bundle_path;
    std::error_code ec;
    const fs::path root(app_bundle_path);
    if (!fs::is_directory(root, ec)) {
        info.error = "not a .app bundle directory";
        return info;
    }
    const fs::path plist_path = root / "Contents" / "Info.plist";
    if (!fs::is_regular_file(plist_path, ec)) {
        info.error = "missing Contents/Info.plist";
        return info;
    }
    std::ifstream in(plist_path);
    if (!in) {
        info.error = "unable to read Contents/Info.plist";
        return info;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    info = parse_macos_info_plist(buf.str(), app_bundle_path);
    if (!info.valid) return info;

    if (!info.executable_name.empty()) {
        const fs::path bin = root / "Contents" / "MacOS" / info.executable_name;
        if (fs::is_regular_file(bin, ec)) {
            info.executable_path = bin.string();
            info.binary = inspect_macho_file(info.executable_path);
        }
    }
    return info;
}

MacosInstallerInfo inspect_macos_installer(const std::string& path) {
    MacosInstallerInfo info;
    info.path = path;
    if (ends_with_ci(path, ".dmg")) {
        info.kind = MacosInstallerKind::Dmg;
    } else if (ends_with_ci(path, ".pkg") || ends_with_ci(path, ".mpkg")) {
        info.kind = MacosInstallerKind::Pkg;
    } else {
        info.error = "unsupported macOS installer extension";
        return info;
    }

    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        info.error = "installer file does not exist: " + path;
        return info;
    }
    const auto sz = fs::file_size(path, ec);
    if (ec || sz < 4) {
        info.error = "installer file is empty or unreadable";
        return info;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        info.error = "unable to open installer file";
        return info;
    }

    if (info.kind == MacosInstallerKind::Pkg) {
        char magic[4] = {};
        in.read(magic, 4);
        if (in.gcount() == 4 && magic[0] == 'x' && magic[1] == 'a' &&
            magic[2] == 'r' && magic[3] == '!') {
            info.valid = true;
            info.format_detail = "XAR macOS Installer Package";
            return info;
        }
        info.error = "missing xar! header (not a valid macOS .pkg)";
        return info;
    }

    // .dmg: standard Apple UDIF disk images carry a 512-byte 'koly' trailer at EOF.
    if (sz >= 512) {
        in.seekg(static_cast<std::streamoff>(sz - 512), std::ios::beg);
        char trailer[4] = {};
        in.read(trailer, 4);
        if (in.gcount() == 4 && trailer[0] == 'k' && trailer[1] == 'o' &&
            trailer[2] == 'l' && trailer[3] == 'y') {
            info.valid = true;
            info.format_detail = "Apple UDIF Disk Image";
            return info;
        }
    }

    info.error = "missing UDIF koly trailer (not a standard macOS .dmg)";
    return info;
}

MacosRuntimeStatus probe_macos_runtime() {
    MacosRuntimeStatus st;
    st.host_arch = host_machine();
    std::error_code ec;

    if (fs::is_regular_file(kFinnDarwinBin, ec)) {
        st.finn_darwin_installed = true;
        st.finn_darwin_bin = kFinnDarwinBin;
    } else if (executable_exists("finn-darwin")) {
        st.finn_darwin_installed = true;
        st.finn_darwin_bin = "finn-darwin";
    }

    if (executable_exists("reef")) {
        st.reef_installed = true;
        st.reef_bin = "reef";
    } else if (fs::is_regular_file("/usr/local/bin/reef", ec)) {
        st.reef_installed = true;
        st.reef_bin = "/usr/local/bin/reef";
    }

    if (executable_exists("darling")) {
        st.darling_installed = true;
        st.darling_bin = "darling";
    }

    for (const char* tool : {"7z", "7zz", "darling-dmg", "dmg2img"}) {
        if (executable_exists(tool)) {
            st.dmg_extractor_installed = true;
            st.dmg_extractor_bin = tool;
            break;
        }
    }
    for (const char* tool : {"xar", "7z", "7zz"}) {
        if (executable_exists(tool)) {
            st.pkg_extractor_installed = true;
            st.pkg_extractor_bin = tool;
            break;
        }
    }

    st.gui_ready = st.finn_darwin_installed || st.reef_installed;
    st.cli_ready = st.finn_darwin_installed || st.reef_installed || st.darling_installed;

    if (st.finn_darwin_installed) {
        st.summary =
            "FinnOS macOS runtime (finn-darwin) ready on " + st.host_arch +
            " — Mach-O, libSystem, Objective-C 2.0, Foundation, and basic AppKit-on-Qt6 "
            "(SwiftUI/Metal/WebKit/CoreData apps report missing frameworks cleanly).";
    } else if (st.reef_installed) {
        st.summary = "macOS runtime ready via " + st.reef_bin + " on " + st.host_arch;
    } else if (st.darling_installed) {
        st.summary =
            "Darling CLI runtime detected on " + st.host_arch +
            " (Darwin CLI only; install finn-darwin for Objective-C/Foundation/AppKit).";
    } else {
        st.summary =
            "finn-darwin is not installed in PATH on " + st.host_arch +
            ". Build and install src/darwin (finn-darwin) to run compatible Mach-O apps "
            "and natively unpack .pkg/.dmg installers.";
    }
    return st;
}

MacosCommandPlan build_macos_extract_plan(const std::string& installer_path,
                                          const std::string& dest_dir) {
    MacosCommandPlan plan;
    const MacosInstallerInfo info = inspect_macos_installer(installer_path);
    if (!info.valid) {
        plan.error = info.error;
        return plan;
    }
    if (dest_dir.empty()) {
        plan.error = "destination directory is empty";
        return plan;
    }

    const MacosRuntimeStatus rt = probe_macos_runtime();
    if (info.kind == MacosInstallerKind::Dmg) {
        if (rt.dmg_extractor_bin == "7z" || rt.dmg_extractor_bin == "7zz") {
            plan.argv = {rt.dmg_extractor_bin, "x", "-y", "-o" + dest_dir, installer_path};
            plan.note = "Extract .dmg contents with " + rt.dmg_extractor_bin;
            plan.ok = true;
            return plan;
        }
        if (rt.dmg_extractor_bin == "darling-dmg") {
            plan.argv = {"darling-dmg", installer_path, dest_dir};
            plan.note = "Mount .dmg with darling-dmg";
            plan.ok = true;
            return plan;
        }
        plan.error = "No .dmg extractor installed (install 7zip/p7zip or darling-dmg).";
        return plan;
    }

    if (rt.pkg_extractor_bin == "xar") {
        plan.argv = {"xar", "-xf", installer_path, "-C", dest_dir};
        plan.note = "Extract .pkg archive with xar";
        plan.ok = true;
        return plan;
    }
    if (rt.pkg_extractor_bin == "7z" || rt.pkg_extractor_bin == "7zz") {
        plan.argv = {rt.pkg_extractor_bin, "x", "-y", "-o" + dest_dir, installer_path};
        plan.note = "Extract .pkg archive with " + rt.pkg_extractor_bin;
        plan.ok = true;
        return plan;
    }
    plan.error = "No .pkg extractor installed (install xar or 7zip/p7zip).";
    return plan;
}

MacosCommandPlan build_macos_launch_plan(const std::string& app_or_binary_path) {
    MacosCommandPlan plan;
    std::error_code ec;
    if (!fs::exists(app_or_binary_path, ec)) {
        plan.error = "target does not exist: " + app_or_binary_path;
        return plan;
    }

    const MacosRuntimeStatus rt = probe_macos_runtime();
    if (rt.finn_darwin_installed) {
        plan.argv = {rt.finn_darwin_bin, "run", app_or_binary_path};
        plan.note = "Launch via FinnOS Darwin runtime (finn-darwin)";
        plan.ok = true;
        return plan;
    }
    if (rt.reef_installed) {
        plan.argv = {rt.reef_bin, app_or_binary_path};
        plan.note = "Launch via " + rt.reef_bin;
        plan.ok = true;
        return plan;
    }

    if (rt.darling_installed) {
        std::string bin = app_or_binary_path;
        if (fs::is_directory(app_or_binary_path, ec)) {
            const MacosAppInfo app = inspect_macos_app_bundle(app_or_binary_path);
            if (!app.valid || app.executable_path.empty()) {
                plan.error = app.error.empty() ? "bundle has no Mach-O executable" : app.error;
                return plan;
            }
            bin = app.executable_path;
        }
        plan.argv = {rt.darling_bin, "shell", bin};
        plan.note = "Launch via Darling (CLI/Darwin binaries only)";
        plan.ok = true;
        return plan;
    }

    plan.error = rt.summary;
    return plan;
}

MacosCommandPlan build_macos_install_plan(const std::string& dmg_pkg_or_app_path) {
    MacosCommandPlan plan;
    const bool is_url = dmg_pkg_or_app_path.rfind("http://", 0) == 0 ||
                        dmg_pkg_or_app_path.rfind("https://", 0) == 0;
    std::error_code ec;
    if (!is_url && !fs::exists(dmg_pkg_or_app_path, ec)) {
        plan.error = "package does not exist: " + dmg_pkg_or_app_path;
        return plan;
    }
    const MacosRuntimeStatus rt = probe_macos_runtime();
    if (!rt.finn_darwin_installed) {
        plan.error = "finn-darwin is not installed; cannot run native macOS installer.";
        return plan;
    }
    plan.argv = {rt.finn_darwin_bin, "install", dmg_pkg_or_app_path};
    plan.note = "Install macOS package via finn-darwin";
    plan.ok = true;
    return plan;
}

MacosCommandPlan build_macos_diagnose_plan(const std::string& app_or_binary_path) {
    MacosCommandPlan plan;
    std::error_code ec;
    if (!fs::exists(app_or_binary_path, ec)) {
        plan.error = "target does not exist: " + app_or_binary_path;
        return plan;
    }
    const MacosRuntimeStatus rt = probe_macos_runtime();
    if (!rt.finn_darwin_installed) {
        plan.error = "finn-darwin is not installed.";
        return plan;
    }
    plan.argv = {rt.finn_darwin_bin, "diagnose", app_or_binary_path};
    plan.note = "Check Mach-O compatibility via finn-darwin";
    plan.ok = true;
    return plan;
}

std::vector<MacosBundleEntry> discover_macos_bundles(const std::string& apps_dir) {
    std::vector<MacosBundleEntry> out;
    std::vector<fs::path> roots;
    fs::path icons_dir;
    if (!apps_dir.empty()) {
        roots.emplace_back(apps_dir);
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        const fs::path h(home);
        roots.push_back(h / kUserMacosAppsRel);
        roots.push_back(h / "Applications");
        icons_dir = h / kUserMacosIconsRel;
    }

    auto slugify = [](std::string_view s) {
        std::string slug;
        bool prev_dash = false;
        for (char c : s) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc)) {
                slug.push_back(static_cast<char>(std::tolower(uc)));
                prev_dash = false;
            } else if (!prev_dash && !slug.empty()) {
                slug.push_back('-');
                prev_dash = true;
            }
        }
        while (!slug.empty() && slug.back() == '-') slug.pop_back();
        return slug.empty() ? std::string("macos-app") : slug;
    };

    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            if (entry.path().extension() != ".app") continue;
            const MacosAppInfo info = inspect_macos_app_bundle(entry.path().string());
            MacosBundleEntry b;
            b.bundle_path = entry.path().string();
            b.name = (info.valid && !info.name.empty()) ? info.name : entry.path().stem().string();
            b.bundle_id = info.bundle_id;
            b.version = info.version;
            b.executable_path = info.executable_path;
            const std::string slug = slugify(b.bundle_id.empty() ? b.name : b.bundle_id);
            b.id = "macos:" + slug;
            if (!icons_dir.empty()) {
                const fs::path png = icons_dir / (slug + ".png");
                if (fs::is_regular_file(png, ec)) b.icon_path = png.string();
            }
            out.push_back(std::move(b));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const MacosBundleEntry& a, const MacosBundleEntry& b) {
                  return a.name < b.name;
              });
    return out;
}

}  // namespace finn::compat
