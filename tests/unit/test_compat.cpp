#include "compat/desktop_entry.hpp"
#include "compat/flatpak_installer.hpp"
#include "compat/macos_runner.hpp"
#include "compat/wine_runner.hpp"

#include <cstdio>
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
    app.prefix = "/home/finn/.local/share/finn/wine/default";
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

}  // namespace

int main() {
    bool ok = true;
    ok &= test_desktop_parsing();
    ok &= test_pe_inspection_and_bounds();
    ok &= test_windows_conf_roundtrip();
    ok &= test_recursive_wine_desktop_discovery();
    ok &= test_flatpak_file_formats();
    ok &= test_macos_inspection();
    if (!ok) return 1;
    std::puts("test_compat: ok");
    return 0;
}
