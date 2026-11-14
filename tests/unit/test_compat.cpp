#include "compat/desktop_entry.hpp"
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
                          std::uint16_t subsystem, bool dotnet) {
    std::string buf(512, '\0');
    buf[0] = 'M';
    buf[1] = 'Z';
    const std::uint32_t pe_off = 0x80;
    buf[60] = static_cast<char>(pe_off & 0xff);
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

}  // namespace

int main() {
    bool ok = true;
    ok &= test_desktop_parsing();
    ok &= test_pe_inspection_and_bounds();
    ok &= test_windows_conf_roundtrip();
    if (!ok) return 1;
    std::puts("test_compat: ok");
    return 0;
}
