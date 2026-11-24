#include "common/paths.hpp"
#include "compat/appimage_runner.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/finnapp_installer.hpp"
#include "compat/flatpak_installer.hpp"
#include "compat/linux_app_probe.hpp"
#include "compat/linux_sandbox.hpp"
#include "compat/macos_runner.hpp"
#include "compat/msi_installer.hpp"
#include "compat/pe_icon.hpp"
#include "compat/windows_registry.hpp"
#include "compat/wine_prefix.hpp"
#include "compat/wine_runner.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,    \
                         __LINE__, #condition);                            \
            return false;                                                  \
        }                                                                  \
    } while (false)

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("finn-compat-test-" + std::to_string(::getpid()));
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    std::string write(std::string_view name, std::string_view content) const {
        const auto file = path_ / std::string(name);
        std::ofstream out(file);
        out << content;
        return file.string();
    }
    std::string path(std::string_view name) const {
        return (path_ / std::string(name)).string();
    }

private:
    std::filesystem::path path_;
};

std::string build_fake_pe(std::uint16_t machine, std::uint16_t magic,
                          std::uint16_t subsystem, bool dotnet,
                          std::uint32_t pe_off = 0x80) {
    std::string buf(std::max<std::size_t>(512, pe_off + 300), '\0');
    buf[0] = 'M';
    buf[1] = 'Z';
    buf[60] = static_cast<char>(pe_off & 0xff);
    buf[61] = static_cast<char>((pe_off >> 8) & 0xff);
    buf[62] = static_cast<char>((pe_off >> 16) & 0xff);
    buf[63] = static_cast<char>((pe_off >> 24) & 0xff);
    buf[pe_off + 0] = 'P';
    buf[pe_off + 1] = 'E';
    buf[pe_off + 2] = '\0';
    buf[pe_off + 3] = '\0';

    const std::size_t coff = pe_off + 4;
    buf[coff + 0] = static_cast<char>(machine & 0xff);
    buf[coff + 1] = static_cast<char>((machine >> 8) & 0xff);
    const std::uint16_t opt_size = 240;
    buf[coff + 16] = static_cast<char>(opt_size & 0xff);
    buf[coff + 17] = static_cast<char>((opt_size >> 8) & 0xff);

    const std::size_t opt = coff + 20;
    buf[opt + 0] = static_cast<char>(magic & 0xff);
    buf[opt + 1] = static_cast<char>((magic >> 8) & 0xff);
    buf[opt + 68] = static_cast<char>(subsystem & 0xff);
    buf[opt + 69] = static_cast<char>((subsystem >> 8) & 0xff);

    const std::size_t rva_count_off = (magic == 0x020b) ? (opt + 108) : (opt + 92);
    const std::size_t clr_dir_off = (magic == 0x020b) ? (opt + 224) : (opt + 208);
    buf[rva_count_off] = 16;
    if (dotnet) {
        buf[clr_dir_off] = 0x10;
        buf[clr_dir_off + 1] = 0x20;
    }
    return buf;
}

bool test_desktop_parsing() {
    const std::string text =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Firefox Web Browser\n"
        "Comment=Browse the World Wide Web\n"
        "Exec=/usr/bin/firefox --new-tab %u\n"
        "Icon=firefox\n"
        "Categories=Network;WebBrowser;\n"
        "MimeType=text/html;x-scheme-handler/https;\n"
        "Terminal=false\n";
    const auto entry = finn::compat::parse_desktop_content(
        text, "/usr/share/applications/firefox.desktop", "desktop", false);
    CHECK(entry.has_value());
    CHECK(entry->id == "linux:firefox");
    CHECK(entry->name == "Firefox Web Browser");
    CHECK(entry->source == "desktop");
    CHECK(!entry->terminal);

    const auto argv = finn::compat::expand_desktop_exec(
        entry->exec, entry->icon, entry->name, entry->desktop_file,
        "https://finnos.org");
    CHECK(argv.size() == 3);
    CHECK(argv[0] == "/usr/bin/firefox");
    CHECK(argv[1] == "--new-tab");
    CHECK(argv[2] == "https://finnos.org");

    const std::string hidden =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Hidden Tool\n"
        "Exec=/bin/true\n"
        "NoDisplay=true\n";
    CHECK(!finn::compat::parse_desktop_content(
        hidden, "/usr/share/applications/hidden.desktop", "desktop", false)
               .has_value());
    return true;
}

bool test_pe_inspection_and_bounds() {
    CHECK(!finn::compat::inspect_pe_bytes("").valid);
    CHECK(!finn::compat::inspect_pe_bytes("NOTAPE").valid);

    const auto x64 = finn::compat::inspect_pe_bytes(
        build_fake_pe(0x8664, 0x020b, 2, false));
    CHECK(x64.valid);
    CHECK(x64.arch == finn::compat::PeArch::X86_64);
    CHECK(x64.is_gui);
    CHECK(!x64.is_dotnet);

    const auto x86_dotnet = finn::compat::inspect_pe_bytes(
        build_fake_pe(0x014c, 0x010b, 3, true));
    CHECK(x86_dotnet.valid);
    CHECK(x86_dotnet.arch == finn::compat::PeArch::X86);
    CHECK(x86_dotnet.is_console);
    CHECK(x86_dotnet.is_dotnet);

    // PE header beyond 4096 bytes must still parse via inspect_pe_file (m10).
    const TempDir dir;
    const std::string large_pe_path = dir.path("large_stub.exe");
    {
        const std::string bytes = build_fake_pe(0x8664, 0x020b, 2, false, 0x2000);
        std::ofstream out(large_pe_path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto large_pe = finn::compat::inspect_pe_file(large_pe_path);
    CHECK(large_pe.valid);
    CHECK(large_pe.arch == finn::compat::PeArch::X86_64);
    CHECK(large_pe.is_gui);
    return true;
}

bool test_windows_conf_roundtrip() {
    const TempDir dir;
    const std::string conf = dir.path("windows.conf");

    finn::compat::WindowsAppEntry app;
    app.id = "win:notepadpp";
    app.name = "Notepad++";
    app.exe_path = "/home/finn/apps/npp.exe";
    app.prefix = "/home/finn/.local/share/finn/wine";
    app.args = "-nosession";
    app.icon = "windows.svg";

    std::string err;
    CHECK(finn::compat::save_windows_app(conf, app, &err));
    const auto loaded = finn::compat::load_windows_apps(conf);
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].id == "win:notepadpp");
    CHECK(loaded[0].name == "Notepad++");
    CHECK(loaded[0].args == "-nosession");

    CHECK(finn::compat::remove_windows_app(conf, "win:notepadpp", &err));
    CHECK(finn::compat::load_windows_apps(conf).empty());
    return true;
}

bool test_recursive_wine_desktop_discovery() {
    const TempDir dir;
    const std::filesystem::path sub =
        std::filesystem::path(dir.path("applications")) / "wine" / "Programs" / "MyWinApp";
    std::filesystem::create_directories(sub);
    {
        std::ofstream out(sub / "MyWinApp.desktop");
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=My Windows App\n"
            << "Exec=env WINEPREFIX=/tmp/p wine C:\\\\Program\\ Files\\\\MyWinApp\\\\app.exe\n"
            << "Categories=Utility;\n";
    }
    const auto entries = finn::compat::scan_desktop_dirs(
        {{dir.path("applications"), "desktop"}}, false);
    CHECK(entries.size() == 1);
    CHECK(entries[0].name == "My Windows App");
    CHECK(entries[0].id == "linux:MyWinApp");
    return true;
}

bool test_flatpak_file_formats() {
    using namespace finn::compat;
    CHECK(detect_flatpak_file_kind("org.mozilla.firefox.flatpakref") ==
          FlatpakFileKind::FlatpakRef);
    CHECK(detect_flatpak_file_kind("/tmp/Bundle.FLATPAK") ==
          FlatpakFileKind::FlatpakBundle);
    CHECK(detect_flatpak_file_kind("/tmp/flathub.flatpakrepo") ==
          FlatpakFileKind::FlatpakRepo);
    CHECK(detect_flatpak_file_kind("/tmp/notes.txt") == FlatpakFileKind::Unknown);

    const TempDir dir;
    const std::string ref_path = dir.write(
        "firefox.flatpakref",
        "[Flatpak Ref]\n"
        "Title=Firefox\n"
        "Name=org.mozilla.firefox\n"
        "Url=https://dl.flathub.org/repo/\n"
        "RuntimeRepo=https://dl.flathub.org/repo/flathub.flatpakrepo\n");
    const auto ref_info = inspect_flatpak_file(ref_path);
    CHECK(ref_info.valid);
    CHECK(ref_info.name == "org.mozilla.firefox");
    CHECK(ref_info.title == "Firefox");

    const auto ref_plan = build_flatpak_install_plan(ref_path, false);
    CHECK(ref_plan.ok);
    CHECK(ref_plan.argv.size() == 6);
    CHECK(ref_plan.argv[0] == "flatpak");
    CHECK(ref_plan.argv[1] == "install");
    CHECK(ref_plan.argv[4] == "--from");
    CHECK(ref_plan.argv[5] == ref_path);

    const std::string bundle_path = dir.write("demo.flatpak", "flatpak-bundle-bytes");
    const auto bundle_plan = build_flatpak_install_plan(bundle_path, false);
    CHECK(bundle_plan.ok);
    CHECK(bundle_plan.argv.size() == 5);
    CHECK(bundle_plan.argv[4] == bundle_path);

    const std::string repo_path = dir.write(
        "flathub.flatpakrepo",
        "[Flatpak Repo]\n"
        "Title=Flathub\n"
        "Url=https://dl.flathub.org/repo/\n");
    const auto repo_plan = build_flatpak_install_plan(repo_path, false);
    CHECK(repo_plan.ok);
    CHECK(repo_plan.argv.size() == 7);
    CHECK(repo_plan.argv[1] == "remote-add");
    CHECK(repo_plan.argv[5] == "flathub");
    CHECK(repo_plan.argv[6] == repo_path);
    return true;
}

bool test_macos_inspection() {
    using namespace finn::compat;

    // Thin 64-bit arm64 Mach-O (little-endian)
    std::string thin_arm64(16, '\0');
    thin_arm64[0] = static_cast<char>(0xcf);
    thin_arm64[1] = static_cast<char>(0xfa);
    thin_arm64[2] = static_cast<char>(0xed);
    thin_arm64[3] = static_cast<char>(0xfe);
    thin_arm64[4] = 0x0c;
    thin_arm64[7] = 0x01;
    const auto m_arm = inspect_macho_bytes(thin_arm64);
    CHECK(m_arm.valid);
    CHECK(!m_arm.is_universal);
    CHECK(m_arm.arches.size() == 1);
    CHECK(m_arm.arches[0] == MachoArch::Arm64);

    // Universal Mach-O (x86_64 + arm64)
    std::string fat(8 + 2 * 20, '\0');
    fat[0] = static_cast<char>(0xca);
    fat[1] = static_cast<char>(0xfe);
    fat[2] = static_cast<char>(0xba);
    fat[3] = static_cast<char>(0xbe);
    fat[7] = 2;
    // Slice 0: CPU_TYPE_X86_64 = 0x01000007 (big-endian)
    fat[8] = 0x01;
    fat[11] = 0x07;
    // Slice 1: CPU_TYPE_ARM64 = 0x0100000c (big-endian)
    fat[28] = 0x01;
    fat[31] = 0x0c;
    const auto m_fat = inspect_macho_bytes(fat);
    CHECK(m_fat.valid);
    CHECK(m_fat.is_universal);
    CHECK(m_fat.arches.size() == 2);
    CHECK(macho_arches_summary(m_fat) == "x86_64, arm64 (Universal)");

    // .app bundle inspection
    const TempDir dir;
    const std::filesystem::path bundle = std::filesystem::path(dir.path("CotEditor.app"));
    std::filesystem::create_directories(bundle / "Contents" / "MacOS");
    {
        std::ofstream plist(bundle / "Contents" / "Info.plist");
        plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<plist version=\"1.0\"><dict>\n"
              << "<key>CFBundleName</key><string>CotEditor</string>\n"
              << "<key>CFBundleIdentifier</key><string>com.coteditor.CotEditor</string>\n"
              << "<key>CFBundleShortVersionString</key><string>5.0.0</string>\n"
              << "<key>CFBundleExecutable</key><string>CotEditor</string>\n"
              << "</dict></plist>\n";
    }
    {
        std::ofstream bin(bundle / "Contents" / "MacOS" / "CotEditor", std::ios::binary);
        bin.write(fat.data(), static_cast<std::streamsize>(fat.size()));
    }
    const auto app = inspect_macos_app_bundle(bundle.string());
    CHECK(app.valid);
    CHECK(app.name == "CotEditor");
    CHECK(app.bundle_id == "com.coteditor.CotEditor");
    CHECK(app.version == "5.0.0");
    CHECK(app.binary.valid);
    CHECK(app.binary.is_universal);

    // .dmg (UDIF koly trailer) and .pkg (xar! magic)
    const std::string dmg_path = dir.path("App.dmg");
    {
        std::string dmg(1024, '\0');
        dmg[512] = 'k';
        dmg[513] = 'o';
        dmg[514] = 'l';
        dmg[515] = 'y';
        std::ofstream out(dmg_path, std::ios::binary);
        out.write(dmg.data(), static_cast<std::streamsize>(dmg.size()));
    }
    const auto dmg_info = inspect_macos_installer(dmg_path);
    CHECK(dmg_info.valid);
    CHECK(dmg_info.kind == MacosInstallerKind::Dmg);

    const std::string pkg_path = dir.write("Installer.pkg", "xar!\x00\x1c\x00\x01");
    const auto pkg_info = inspect_macos_installer(pkg_path);
    CHECK(pkg_info.valid);
    CHECK(pkg_info.kind == MacosInstallerKind::Pkg);

    const std::string bad_pkg = dir.write("Bad.pkg", "notxar");
    CHECK(!inspect_macos_installer(bad_pkg).valid);
    return true;
}

// A missing icon for one bundle must not stop the scan of later bundles. The
// discovery loop used to share one error_code between the directory iterator
// and the icon lookup, so a failed is_regular_file() aborted the rest.
bool test_macos_bundle_discovery_with_missing_icons() {
    using namespace finn::compat;
    const TempDir dir;
    const std::filesystem::path home = dir.path("home");
    std::filesystem::create_directories(home);
    const std::filesystem::path apps = home / finn::kUserMacosAppsRel;
    const std::filesystem::path icons = home / finn::kUserMacosIconsRel;
    std::filesystem::create_directories(apps);
    std::filesystem::create_directories(icons);  // deliberately empty

    auto add_bundle = [&](const std::string& name, const std::string& id) {
        const std::filesystem::path bundle = apps / (name + ".app");
        std::filesystem::create_directories(bundle / "Contents" / "MacOS");
        std::ofstream plist(bundle / "Contents" / "Info.plist");
        plist << "<plist version=\"1.0\"><dict>"
              << "<key>CFBundleName</key><string>" << name << "</string>\n"
              << "<key>CFBundleIdentifier</key><string>" << id << "</string>\n"
              << "<key>CFBundleExecutable</key><string>" << name << "</string>\n"
              << "</dict></plist>\n";
        std::ofstream bin(bundle / "Contents" / "MacOS" / name, std::ios::binary);
        bin.write("stub", 4);
    };
    add_bundle("AlphaApp", "org.finnos.Alpha");
    add_bundle("BetaApp", "org.finnos.Beta");

    // No icon files exist. Both bundles must still be discovered: a failed
    // icon lookup must not abort the rest of the directory scan.
    const char* old_home = std::getenv("HOME");
    const std::string saved_home = old_home ? old_home : "";
    ::setenv("HOME", home.c_str(), 1);
    const auto found = discover_macos_bundles("");
    if (saved_home.empty()) ::unsetenv("HOME");
    else ::setenv("HOME", saved_home.c_str(), 1);

    CHECK(found.size() == 2);
    bool alpha = false, beta = false;
    for (const auto& b : found) {
        alpha |= b.id == "macos:org-finnos-alpha";
        beta |= b.id == "macos:org-finnos-beta";
        CHECK(b.icon_path.empty());
    }
    CHECK(alpha);
    CHECK(beta);
    return true;
}

// ---- Work package 1: AppImage detection, planning and discovery -------------

std::uint16_t host_elf_machine() {
    return finn::compat::appimage_host_arch() == "aarch64" ? 183 : 62;
}

void put16(std::string& buf, std::size_t off, std::uint16_t value) {
    buf[off] = static_cast<char>(value & 0xff);
    buf[off + 1] = static_cast<char>((value >> 8) & 0xff);
}

std::string fake_type2_appimage(std::uint16_t machine, bool valid_magic = true) {
    std::string buf(128, '\0');
    buf[0] = static_cast<char>(0x7f); buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2;  // ELF64
    buf[5] = 1;  // little endian
    buf[6] = 1;  // EV_CURRENT
    if (valid_magic) {
        buf[8] = 'A'; buf[9] = 'I'; buf[10] = 2;
        // Big-endian payload offset at byte 11 and a squashfs magic there.
        buf[14] = 0x40;
        buf[0x40] = 'h'; buf[0x41] = 's'; buf[0x42] = 'q'; buf[0x43] = 's';
    }
    put16(buf, 18, machine);
    return buf;
}

std::string fake_type1_appimage(std::uint16_t machine) {
    std::string buf(0x8000 + 24, '\0');
    // Type 1 is an ELF stub at offset 0 plus an ISO9660 volume descriptor at
    // sector 16 (0x8000), with the recommended AI\x01 marker at byte 8.
    buf[0] = static_cast<char>(0x7f); buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2; buf[5] = 1; buf[6] = 1;
    buf[8] = 'A'; buf[9] = 'I'; buf[10] = 1;
    put16(buf, 18, machine);
    const std::size_t off = 0x8000;
    buf[off + 1] = 'C'; buf[off + 2] = 'D';
    buf[off + 3] = '0'; buf[off + 4] = '0'; buf[off + 5] = '1';
    return buf;
}

bool test_appimage_inspection_and_planning() {
    using namespace finn::compat;
    const auto machine = host_elf_machine();

    const auto t2 = inspect_appimage_bytes(fake_type2_appimage(machine));
    CHECK(t2.valid);
    CHECK(t2.kind == AppImageKind::Type2);
    CHECK(t2.arch_compatible);
    CHECK(!t2.fuse_required);
    CHECK(t2.arch_name == appimage_host_arch());

    const auto plain = inspect_appimage_bytes(fake_type2_appimage(machine, false));
    CHECK(!plain.valid);
    CHECK(plain.kind == AppImageKind::PlainElf);
    CHECK(!plain.error.empty());

    const auto t1 = inspect_appimage_bytes(fake_type1_appimage(machine));
    CHECK(t1.valid);
    CHECK(t1.kind == AppImageKind::Type1);
    CHECK(t1.fuse_required);
    CHECK(std::string(appimage_kind_label(t1.kind)).find("Type 1") != std::string::npos);

    const TempDir dir;
    const std::string path = dir.path("Demo.AppImage");
    {
        const std::string bytes = fake_type2_appimage(machine);
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto inspected = inspect_appimage_file(path);
    CHECK(inspected.valid);
    CHECK(inspected.kind == AppImageKind::Type2);

    AppImageLaunchOptions opts;
    opts.check_runtime = false;  // Deterministic: assert the runtime is present.
    const auto fuse_plan = build_appimage_launch_plan(path, opts);
    CHECK(fuse_plan.ok);
    CHECK(fuse_plan.argv.size() == 1);
    CHECK(fuse_plan.argv[0] == path);
    CHECK(fuse_plan.diagnostics.find("no enforced per-app sandbox") != std::string::npos);

    opts.prefer_extract_and_run = true;
    const auto extract_run_plan = build_appimage_launch_plan(path, opts);
    CHECK(extract_run_plan.ok);
    CHECK(extract_run_plan.argv.size() == 2);
    CHECK(extract_run_plan.argv[1] == "--appimage-extract-and-run");
    bool saw_env = false;
    for (const auto& [key, value] : extract_run_plan.env) {
        saw_env |= key == "APPIMAGE_EXTRACT_AND_RUN" && value == "1";
    }
    CHECK(saw_env);

    const auto extract_plan = build_appimage_extract_plan(path);
    CHECK(extract_plan.ok);
    CHECK(extract_plan.argv[1] == "--appimage-extract");

    const std::string foreign = dir.path("Foreign.AppImage");
    {
        const std::string bytes = fake_type2_appimage(machine == 62 ? 183 : 62);
        std::ofstream out(foreign, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto foreign_plan = build_appimage_launch_plan(foreign, opts);
    CHECK(!foreign_plan.ok);
    CHECK(foreign_plan.error.find("architecture") != std::string::npos);

    const std::string junk = dir.write("notes.txt", "not an AppImage");
    CHECK(!inspect_appimage_file(junk).valid);

    // Discovery skips invalid images and only registers matching-architecture
    // ones with a stable id.
    const auto candidates = discover_appimages(dir.path(""));
    CHECK(candidates.size() == 1);
    CHECK(candidates[0].id == "appimage:demo");
    CHECK(candidates[0].path == path);
    return true;
}

// ---- Work package 2: desktop actions, Exec safety and environment ----------

bool test_desktop_actions_and_exec_hardening() {
    const std::string text =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Demo\n"
        "Exec=/usr/bin/demo %u\n"
        "Icon=demo\n"
        "Actions=new-window;open-url;\n"
        "\n"
        "[Desktop Action new-window]\n"
        "Name=New Window\n"
        "Exec=/usr/bin/demo --new-window\n"
        "\n"
        "[Desktop Action open-url]\n"
        "Name=Open URL\n"
        "Icon=demo\n"
        "Exec=/usr/bin/demo --url=%u\n";
    const auto entry = finn::compat::parse_desktop_content(
        text, "/usr/share/applications/demo.desktop", "desktop", false);
    CHECK(entry.has_value());
    CHECK(entry->actions.size() == 2);
    CHECK(entry->action_defs.size() == 2);
    const auto action = finn::compat::find_desktop_action(*entry, "open-url");
    CHECK(action.has_value());
    CHECK(action->name == "Open URL");
    CHECK(!finn::compat::find_desktop_action(*entry, "missing").has_value());

    const auto action_argv = finn::compat::expand_desktop_action_exec(
        *entry, "open-url", "https://finnos.org");
    CHECK(action_argv.size() == 2);
    CHECK(action_argv[0] == "/usr/bin/demo");
    CHECK(action_argv[1] == "--url=https://finnos.org");
    CHECK(finn::compat::expand_desktop_action_exec(*entry, "missing").empty());

    // A target with shell metacharacters stays one argv element. Field codes
    // embedded in a token are expanded in place, unknown codes are dropped,
    // and control characters never reach the child.
    const auto argv = finn::compat::expand_desktop_exec(
        "app --url=%u --name=%c --old=%n", "demo.svg", "My App", {},
        "https://x/$(id); echo pwned");
    CHECK(argv.size() == 4);
    CHECK(argv[0] == "app");
    CHECK(argv[1] == "--url=https://x/$(id); echo pwned");
    CHECK(argv[2] == "--name=My App");
    CHECK(argv[3] == "--old=");
    const auto cleaned = finn::compat::expand_desktop_exec("app\n --flag");
    CHECK(cleaned.size() == 2);
    CHECK(cleaned[0] == "app");
    CHECK(cleaned[1] == "--flag");

    CHECK(finn::compat::is_safe_icon_name("firefox"));
    CHECK(!finn::compat::is_safe_icon_name("../../etc/passwd"));
    CHECK(!finn::compat::is_safe_icon_name("/etc/passwd"));
    CHECK(!finn::compat::is_safe_icon_name(std::string("bad\nname")));
    CHECK(finn::compat::resolve_icon_path("/etc/passwd").empty());
    return true;
}

bool test_linux_env_isolation() {
    using namespace finn::compat;
    CHECK(is_unsafe_env_key("LD_PRELOAD"));
    CHECK(is_unsafe_env_key("LD_LIBRARY_PATH"));
    CHECK(is_unsafe_env_key("QT_PLUGIN_PATH"));
    CHECK(!is_unsafe_env_key("LANG"));
    CHECK(!is_unsafe_env_key("HOME"));

    LinuxSandboxOptions options;
    options.wayland_display = "/run/finn/wayland-0";
    options.xdisplay = ":99";
    const auto plan = build_linux_launch_env(options);
    bool honest_sandbox = false;
    for (const std::string& warning : plan.warnings) {
        honest_sandbox |= warning.find("no enforced per-app sandbox") != std::string::npos;
    }
    CHECK(honest_sandbox);
    bool has_theme = false, has_wayland = false, has_display = false;
    for (const auto& [key, value] : plan.overrides) {
        has_theme |= key == "QT_QPA_PLATFORMTHEME" && value == "finn";
        has_wayland |= key == "WAYLAND_DISPLAY" &&
                       value == "/run/finn/wayland-0";
        has_display |= key == "DISPLAY" && value == ":99";
    }
    CHECK(has_theme);
    CHECK(has_wayland);
    CHECK(has_display);

    const std::vector<std::pair<std::string, std::string>> inherited = {
        {"LD_PRELOAD", "/tmp/evil.so"}, {"QT_PLUGIN_PATH", "/tmp/plugins"},
        {"LANG", "C"}, {"HOME", "/home/finn"},
    };
    const auto env = apply_linux_launch_env(inherited, plan);
    bool evil = false, plugin = false, lang = false, data_dirs = false, finn_style = false;
    for (const std::string& entry : env) {
        evil |= entry.rfind("LD_PRELOAD=", 0) == 0;
        plugin |= entry.rfind("QT_PLUGIN_PATH=", 0) == 0;
        lang |= entry == "LANG=C";
        data_dirs |= entry.rfind("XDG_DATA_DIRS=", 0) == 0;
        finn_style |= entry == "QT_STYLE_OVERRIDE=finn";
    }
    CHECK(!evil);
    CHECK(!plugin);
    CHECK(lang);
    CHECK(data_dirs);
    CHECK(finn_style);
    return true;
}

// ---- Work package 3: ELF probe, missing libraries and id collisions --------

void put64(std::string& buf, std::size_t off, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        buf[off + static_cast<std::size_t>(i)] =
            static_cast<char>((value >> (i * 8)) & 0xff);
    }
}

std::string fake_elf_with_needed(std::uint16_t machine, const std::string& soname) {
    constexpr std::uint64_t base = 0x400000;
    constexpr std::size_t interp_off = 232;
    constexpr std::size_t dyn_off = 320;
    constexpr std::size_t str_off = 400;
    std::string buf(512, '\0');

    buf[0] = static_cast<char>(0x7f); buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2; buf[5] = 1; buf[6] = 1;  // ELF64, little endian, EV_CURRENT
    put16(buf, 16, 2);       // ET_EXEC
    put16(buf, 18, machine);
    put64(buf, 32, 64);      // e_phoff
    put16(buf, 54, 56);      // e_phentsize
    put16(buf, 56, 3);       // e_phnum

    // PT_LOAD covering the whole file.
    put16(buf, 64, 1);
    put64(buf, 64 + 8, 0);
    put64(buf, 64 + 16, base);
    put64(buf, 64 + 32, buf.size());
    // PT_INTERP.
    const std::string interp = "/lib/ld-linux.so.2";
    put16(buf, 64 + 56, 3);
    put64(buf, 64 + 56 + 8, interp_off);
    put64(buf, 64 + 56 + 16, base + interp_off);
    put64(buf, 64 + 56 + 32, interp.size() + 1);
    // PT_DYNAMIC.
    put16(buf, 64 + 112, 2);
    put64(buf, 64 + 112 + 8, dyn_off);
    put64(buf, 64 + 112 + 16, base + dyn_off);
    put64(buf, 64 + 112 + 32, 64);

    for (std::size_t i = 0; i < interp.size(); ++i) buf[interp_off + i] = interp[i];
    put64(buf, dyn_off, 1);              // DT_NEEDED
    put64(buf, dyn_off + 8, 1);          // offset into the string table
    put64(buf, dyn_off + 16, 5);         // DT_STRTAB
    put64(buf, dyn_off + 24, base + str_off);
    put64(buf, dyn_off + 32, 10);        // DT_STRSZ
    put64(buf, dyn_off + 40, soname.size() + 2);
    put64(buf, dyn_off + 48, 0);         // DT_NULL
    buf[str_off] = '\0';
    for (std::size_t i = 0; i < soname.size(); ++i) buf[str_off + 1 + i] = soname[i];
    return buf;
}

bool test_linux_app_probe_and_conflicts() {
    using namespace finn::compat;
    const auto machine = host_elf_machine();
    const std::string soname = "libFinnOSNoSuchLibrary999.so.1";
    const std::string bytes = fake_elf_with_needed(machine, soname);

    const auto elf = inspect_elf_bytes(bytes);
    CHECK(elf.valid);
    CHECK(elf.elf_class == ElfClass::Elf64);
    CHECK(elf.endian == ElfEndian::Little);
    CHECK(elf.arch_name == appimage_host_arch());
    CHECK(elf.interpreter == "/lib/ld-linux.so.2");
    CHECK(elf.needed.size() == 1);
    CHECK(elf.needed[0] == soname);
    CHECK(!inspect_elf_bytes("not an elf").valid);
    CHECK(!inspect_elf_bytes(bytes.substr(0, 8)).valid);

    const TempDir dir;
    const std::string path = dir.path("probe-elf");
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto report = probe_linux_binary(path);
    CHECK(report.has_binary);
    CHECK(report.arch_compatible);
    CHECK(report.libraries.size() == 1);
    CHECK(report.libraries[0].name == soname);
    CHECK(!report.libraries[0].found);
    CHECK(report.missing_libraries.size() == 1);
    CHECK(report.summary.find(appimage_host_arch()) != std::string::npos);
    CHECK(report.summary.find("missing") != std::string::npos);

    DesktopEntry a;
    a.id = "linux:demo"; a.desktop_id = "demo"; a.desktop_file = "/usr/share/a.desktop";
    DesktopEntry b = a;
    b.desktop_file = "/home/finn/.local/share/applications/b.desktop";
    const auto conflicts = detect_desktop_id_conflicts({a, b});
    CHECK(!conflicts.empty());
    bool saw_id = false;
    for (const auto& conflict : conflicts) saw_id |= conflict.id == "id:linux:demo";
    CHECK(saw_id);
    CHECK(desktop_conflicts_summary(conflicts).find("desktop-id conflict") !=
          std::string::npos);
    CHECK(desktop_conflicts_summary({}).find("No desktop-id conflicts") == 0);
    return true;
}

bool test_wine_prefix_planning() {
    using namespace finn::compat;
    const TempDir dir;
    const char* old_home = std::getenv("HOME");
    const std::string saved_home = old_home ? old_home : "";
    ::setenv("HOME", dir.path("home").c_str(), 1);
    std::filesystem::create_directories(dir.path("home"));

    const std::string root = resolve_prefix_root();
    CHECK(root == dir.path("home/.local/share/finn/wine/prefixes"));
    CHECK(prefix_id_for("..").empty());
    CHECK(prefix_id_for("").empty());
    const std::string escaped = prefix_id_for("/tmp/../escape.exe");
    CHECK(escaped.find("..") == std::string::npos);
    CHECK(escaped.find('/') == std::string::npos);
    const std::string id = prefix_id_for("Notepad++.exe");
    CHECK(!id.empty() && id.find("..") == std::string::npos);
    CHECK(prefix_path_for(id) == root + "/" + id);

    const auto info = prefix_info(id);
    CHECK(info.id == id);
    CHECK(!info.drive_c_present);
    CHECK(info.arch.empty());  // an absent prefix is not assumed to be win64

    const auto plan = build_prefix_init_plan(id);
    CHECK(plan.prefix_path == root + "/" + id);
    CHECK(!plan.prefix_exists);
    CHECK(plan.steps.size() == 1);
    CHECK(plan.steps[0].argv.size() >= 2 && plan.steps[0].argv[1] == "wineboot");
    CHECK(build_prefix_removal_plan(id).steps.empty());

    CHECK(recommend_arch(PeArch::X86) == "win32");
    CHECK(recommend_arch(PeArch::X86_64) == "win64");
    CHECK(recommend_arch(PeArch::Arm64).find("unsupported") == 0);

    if (saved_home.empty()) ::unsetenv("HOME");
    else ::setenv("HOME", saved_home.c_str(), 1);
    return true;
}

bool test_windows_installer_plans() {
    using namespace finn::compat;
    CHECK(detect_windows_package_kind("Setup.EXE") == WindowsPackageKind::Exe);
    CHECK(detect_windows_package_kind("app.MSI") == WindowsPackageKind::Msi);
    CHECK(detect_windows_package_kind("pack.MSIX") == WindowsPackageKind::Msix);
    CHECK(detect_windows_package_kind("go.bat") == WindowsPackageKind::Bat);
    CHECK(detect_windows_package_kind("notes.txt") == WindowsPackageKind::Unknown);

    const TempDir dir;
    const std::string msi = dir.path("app.msi");
    { std::ofstream out(msi, std::ios::binary); out << "msi bytes"; }
    const auto plan = build_install_plan(msi, dir.path("prefix"));
    CHECK(plan.ok);
    CHECK(plan.kind == WindowsPackageKind::Msi);
    CHECK(plan.argv[0] == "msiexec");
    CHECK(plan.argv[1] == "/i");
    CHECK(plan.argv[2] == msi);
    bool quiet = false, norestart = false;
    for (const auto& arg : plan.argv) {
        quiet |= arg == "/qn";
        norestart |= arg == "/norestart";
    }
    CHECK(quiet);
    CHECK(norestart);
    bool prefix_env = false;
    for (const auto& [key, value] : plan.env) {
        prefix_env |= key == "WINEPREFIX" && value == dir.path("prefix");
    }
    CHECK(prefix_env);

    // MSIX is detected but never faked as installable.
    const std::string msix = dir.path("app.msix");
    { std::ofstream out(msix, std::ios::binary); out << "PK\x03\x04"; }
    const auto unsupported = build_install_plan(msix);
    CHECK(!unsupported.ok);
    CHECK(!unsupported.error.empty());

    const auto uninstall = build_uninstall_plan("{11111111-2222-3333-4444-555555555555}");
    CHECK(uninstall.ok);
    CHECK(uninstall.argv[0] == "msiexec");
    CHECK(uninstall.argv[1] == "/x");
    return true;
}

bool test_windows_uninstall_discovery() {
    using namespace finn::compat;
    CHECK(windows_registry_self_test());

    const TempDir dir;
    const std::string prefix = dir.path("prefix");
    std::error_code ec;
    std::filesystem::create_directories(prefix, ec);
    std::string reason;
    CHECK(collect_uninstall_entries_from_prefix(prefix, &reason).empty());
    CHECK(!reason.empty());  // "no registry files" is reported, not invented

    std::filesystem::create_directories(
        std::filesystem::path(prefix) / "drive_c" / "Program Files" / "FinnTest", ec);
    const std::string app =
        (std::filesystem::path(prefix) / "drive_c" / "Program Files" / "FinnTest" / "app.exe")
            .string();
    { std::ofstream out(app, std::ios::binary); out << "MZ"; }

    const std::string reg =
        "Windows Registry Editor Version 5.00\n\n"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\FinnTest]\n"
        "\"DisplayName\"=\"Finn Test App\"\n"
        "\"DisplayVersion\"=\"2.0\"\n"
        "\"Publisher\"=\"FinnOS Tests\"\n"
        "\"DisplayIcon\"=\"C:\\\\Program Files\\\\FinnTest\\\\app.exe,0\"\n"
        "\"InstallLocation\"=\"C:\\\\Program Files\\\\FinnTest\"\n";
    { std::ofstream out(std::filesystem::path(prefix) / "system.reg"); out << reg; }
    const auto entries = collect_uninstall_entries_from_prefix(prefix, &reason);
    CHECK(entries.size() == 1);
    CHECK(entries[0].display_name == "Finn Test App");
    CHECK(entries[0].display_version == "2.0");

    const auto bad_lnk = parse_windows_shortcut_bytes("not a shortcut");
    CHECK(!bad_lnk.valid);
    CHECK(!bad_lnk.error.empty() || !bad_lnk.warnings.empty());
    return true;
}

bool test_bplist_pe_icon_and_finnapp_lifecycle() {
    using namespace finn::compat;
    // 1. Minimal synthetic bplist00 dictionary:
    //    obj 0: dict(2) keys=[1,2], vals=[3,4]
    //    obj 1: "CFBundleName" (12)
    //    obj 2: "CFBundleIdentifier" (18)
    //    obj 3: "XcodeApp" (8)
    //    obj 4: "com.example.xcode" (17)
    std::string bp = "bplist00";
    std::vector<std::uint8_t> offs;
    offs.push_back(static_cast<std::uint8_t>(bp.size()));  // 0: dict
    bp.append("\xD2\x01\x02\x03\x04", 5);
    auto add_str = [&](std::string_view s) {
        offs.push_back(static_cast<std::uint8_t>(bp.size()));
        if (s.size() < 15) {
            bp.push_back(static_cast<char>(0x50u | s.size()));
        } else {
            bp.push_back(static_cast<char>(0x5Fu));
            bp.push_back(static_cast<char>(0x10u));  // 1-byte int
            bp.push_back(static_cast<char>(s.size()));
        }
        bp.append(s.data(), s.size());
    };
    add_str("CFBundleName");
    add_str("CFBundleIdentifier");
    add_str("XcodeApp");
    add_str("com.example.xcode");
    const std::size_t off_tbl = bp.size();
    for (std::uint8_t o : offs) bp.push_back(static_cast<char>(o));
    // 32-byte trailer: 6 unused, offset_sz=1, ref_sz=1, num_objs=5, top=0, off_tbl
    std::string tr(32, '\0');
    tr[6] = 1;
    tr[7] = 1;
    tr[15] = 5;
    tr[31] = static_cast<char>(off_tbl);
    bp.append(tr);

    const auto binfo = parse_macos_info_plist(bp, "/tmp/XcodeApp.app");
    CHECK(binfo.valid);
    CHECK(binfo.name == "XcodeApp");
    CHECK(binfo.bundle_id == "com.example.xcode");

    // 2. Standalone ICO carrying a PNG payload
    std::string ico(22, '\0');
    ico[2] = 1;   // type = ICO
    ico[4] = 1;   // count = 1
    ico[6] = 0;   // 256px
    ico[7] = 0;
    ico[12] = 32; // 32 bpp
    const std::string fake_png = "\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR";
    ico[14] = static_cast<char>(fake_png.size());
    ico[18] = 22; // image offset
    ico.append(fake_png);
    const auto ex = extract_pe_or_ico_icon_bytes(ico);
    CHECK(ex.ok);
    CHECK(ex.ext == ".png");
    CHECK(ex.bytes.size() == fake_png.size());

    // 3. .finnapp bundle install & uninstall lifecycle
    const TempDir dir;
    const std::filesystem::path bundle = dir.path("Hello.finnapp");
    std::filesystem::create_directories(bundle / "bin");
    {
        std::ofstream mf(bundle / "manifest.conf");
        mf << "id=hello-native\nname=Hello Native\nversion=1.0\nexec=bin/hello\ncategory=Productivity\n";
        std::ofstream bin(bundle / "bin" / "hello");
        bin << "#!/bin/sh\necho hi\n";
    }
    const std::string apps_root = dir.path("apps");
    const std::string desk_dir = dir.path("desktop");
    const auto inst = install_finnapp(bundle.string(), apps_root, desk_dir);
    CHECK(inst.ok);
    CHECK(std::filesystem::is_regular_file(inst.manifest_path));
    CHECK(std::filesystem::is_regular_file(inst.desktop_path));
    std::string uerr;
    CHECK(uninstall_finnapp("hello-native", apps_root, desk_dir, &uerr));
    CHECK(!std::filesystem::exists(inst.manifest_path));
    CHECK(!std::filesystem::exists(inst.desktop_path));
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_desktop_parsing();
    ok &= test_pe_inspection_and_bounds();
    ok &= test_windows_conf_roundtrip();
    ok &= test_recursive_wine_desktop_discovery();
    ok &= test_flatpak_file_formats();
    ok &= test_macos_inspection();
    ok &= test_macos_bundle_discovery_with_missing_icons();
    ok &= test_appimage_inspection_and_planning();
    ok &= test_desktop_actions_and_exec_hardening();
    ok &= test_linux_env_isolation();
    ok &= test_linux_app_probe_and_conflicts();
    ok &= finn::compat::appimage_runner_self_test();
    ok &= finn::compat::linux_sandbox_self_test();
    ok &= finn::compat::linux_app_probe_self_test();
    ok &= test_wine_prefix_planning();
    ok &= test_windows_installer_plans();
    ok &= test_windows_uninstall_discovery();
    ok &= test_bplist_pe_icon_and_finnapp_lifecycle();
    if (!ok) return 1;
    std::puts("test_compat: ok");
    return 0;
}
