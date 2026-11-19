#include "darwin/appkit_bridge.hpp"
#include "darwin/compat_symbols.hpp"
#include "darwin/coregraphics.hpp"
#include "darwin/foundation.hpp"
#include "darwin/framework_coverage.hpp"
#include "darwin/icns.hpp"
#include "darwin/installer.hpp"
#include "darwin/libsystem.hpp"
#include "darwin/macho_loader.hpp"
#include "darwin/objc_runtime.hpp"
#include "darwin/plist.hpp"
#include "darwin/swift_runtime.hpp"
#include "darwin/carbon_compat.hpp"
#include "darwin/swiftui/abi_metadata.hpp"
#include "darwin/swiftui/state_and_combine.hpp"
#include "darwin/swiftui/swift_runtime_ext.hpp"
#include "darwin/swiftui/views_and_scene.hpp"
#include "darwin/swiftui/contract.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,        \
                         __LINE__, #cond);                                     \
            return false;                                                      \
        }                                                                      \
    } while (false)

template <typename Fn>
Fn from_void_ptr(void* p) {
    Fn out = nullptr;
    static_assert(sizeof(out) == sizeof(p));
    std::memcpy(&out, &p, sizeof(out));
    return out;
}

using finn::darwin::ObjcId;
using finn::darwin::ObjcImp;
using finn::darwin::ObjcSel;

// Matches Foundation's internal DarwinRange (NSRange) layout: two word-sized fields.
struct DarwinRange {
    std::size_t location;
    std::size_t length;
};

std::atomic<int> g_tmp_seq{0};

class TempDir {
public:
    TempDir() {
        const int seq = g_tmp_seq.fetch_add(1, std::memory_order_relaxed);
        root_ = fs::temp_directory_path() /
                ("finn-darwin-test-" + std::to_string(::getpid()) + "-" + std::to_string(seq));
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path path(std::string_view rel) const { return root_ / std::string(rel); }
    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

void append_le16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xffu));
}

void append_be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>(v & 0xffu));
}

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>(v & 0xffu));
}

void append_be64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    append_be32(out, static_cast<std::uint32_t>(v >> 32));
    append_be32(out, static_cast<std::uint32_t>(v & 0xffffffffu));
}

std::vector<std::uint8_t> zlib_compress(const std::uint8_t* data, std::size_t len, int wbits) {
    z_stream strm{};
    if (::deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, wbits, 8,
                       Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    std::vector<std::uint8_t> out(len + 128);
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    strm.avail_in = static_cast<uInt>(len);
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());
    const int rc = ::deflate(&strm, Z_FINISH);
    ::deflateEnd(&strm);
    if (rc != Z_STREAM_END) return {};
    out.resize(strm.total_out);
    return out;
}

void append_cpio_odc_entry(std::vector<std::uint8_t>& cpio,
                           const std::string& name,
                           unsigned int mode,
                           const std::vector<std::uint8_t>& body) {
    char hdr[77] = {};
    std::snprintf(hdr, sizeof(hdr),
                  "070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o",
                  0u, 1u, mode, 0u, 0u, 1u, 0u, 0u,
                  static_cast<unsigned int>(name.size() + 1),
                  static_cast<unsigned int>(body.size()));
    cpio.insert(cpio.end(), hdr, hdr + 76);
    cpio.insert(cpio.end(), name.begin(), name.end());
    cpio.push_back(0);
    cpio.insert(cpio.end(), body.begin(), body.end());
}

struct ZipEntrySpec {
    std::string name;
    std::vector<std::uint8_t> data;
    std::uint32_t unix_mode;
    bool deflate;
};

std::vector<std::uint8_t> build_zip_archive(const std::vector<ZipEntrySpec>& entries) {
    std::vector<std::uint8_t> zip;
    std::vector<std::uint8_t> cd;

    for (const auto& e : entries) {
        const std::uint32_t local_off = static_cast<std::uint32_t>(zip.size());
        const std::uint32_t crc = static_cast<std::uint32_t>(
            ::crc32(0u, e.data.empty() ? nullptr : e.data.data(),
                    static_cast<uInt>(e.data.size())));
        std::vector<std::uint8_t> payload = e.data;
        std::uint16_t method = 0;
        if (e.deflate && !e.data.empty()) {
            payload = zlib_compress(e.data.data(), e.data.size(), -MAX_WBITS);
            method = 8;
        }

        // Local file header
        append_le32(zip, 0x04034b50u);
        append_le16(zip, 20);
        append_le16(zip, 0);
        append_le16(zip, method);
        append_le16(zip, 0);
        append_le16(zip, 0);
        append_le32(zip, crc);
        append_le32(zip, static_cast<std::uint32_t>(payload.size()));
        append_le32(zip, static_cast<std::uint32_t>(e.data.size()));
        append_le16(zip, static_cast<std::uint16_t>(e.name.size()));
        append_le16(zip, 0);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), payload.begin(), payload.end());

        // Central directory header
        append_le32(cd, 0x02014b50u);
        append_le16(cd, static_cast<std::uint16_t>((3u << 8) | 20u));  // Unix
        append_le16(cd, 20);
        append_le16(cd, 0);
        append_le16(cd, method);
        append_le16(cd, 0);
        append_le16(cd, 0);
        append_le32(cd, crc);
        append_le32(cd, static_cast<std::uint32_t>(payload.size()));
        append_le32(cd, static_cast<std::uint32_t>(e.data.size()));
        append_le16(cd, static_cast<std::uint16_t>(e.name.size()));
        append_le16(cd, 0);
        append_le16(cd, 0);
        append_le16(cd, 0);
        append_le16(cd, 0);
        append_le32(cd, e.unix_mode << 16);
        append_le32(cd, local_off);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }

    const std::uint32_t cd_off = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), cd.begin(), cd.end());

    // End of central directory
    append_le32(zip, 0x06054b50u);
    append_le16(zip, 0);
    append_le16(zip, 0);
    append_le16(zip, static_cast<std::uint16_t>(entries.size()));
    append_le16(zip, static_cast<std::uint16_t>(entries.size()));
    append_le32(zip, static_cast<std::uint32_t>(cd.size()));
    append_le32(zip, cd_off);
    append_le16(zip, 0);
    return zip;
}

std::vector<std::uint8_t> make_test_icns() {
    static constexpr std::uint8_t kPngMagic[12] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 'I', 'H', 'D', 'R'};
    std::vector<std::uint8_t> png_small(kPngMagic, kPngMagic + 12);
    png_small.push_back(0x32);
    std::vector<std::uint8_t> png_big(kPngMagic, kPngMagic + 12);
    png_big.push_back(0x99);
    png_big.push_back(0xaa);

    std::vector<std::uint8_t> icns = {'i', 'c', 'n', 's'};
    const std::uint32_t total =
        static_cast<std::uint32_t>(8 + 8 + png_small.size() + 8 + png_big.size());
    append_be32(icns, total);

    icns.insert(icns.end(), {'i', 'c', 'p', '5'});
    append_be32(icns, static_cast<std::uint32_t>(8 + png_small.size()));
    icns.insert(icns.end(), png_small.begin(), png_small.end());

    icns.insert(icns.end(), {'i', 'c', '0', '8'});
    append_be32(icns, static_cast<std::uint32_t>(8 + png_big.size()));
    icns.insert(icns.end(), png_big.begin(), png_big.end());
    return icns;
}

std::vector<std::uint8_t> build_pkg_bytes(const std::string& app_name,
                                          const std::string& bundle_id,
                                          const std::vector<std::uint8_t>& exe_bytes,
                                           bool name_after_data = false) {
    const std::string info_plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\"><dict>"
        "<key>CFBundleName</key><string>" + app_name + "</string>"
        "<key>CFBundleIdentifier</key><string>" + bundle_id + "</string>"
        "<key>CFBundleShortVersionString</key><string>1.2.0</string>"
        "<key>CFBundleExecutable</key><string>" + app_name + "</string>"
        "<key>CFBundleIconFile</key><string>AppIcon</string>"
        "</dict></plist>\n";
    const auto icns_bytes = make_test_icns();

    std::vector<std::uint8_t> cpio;
    append_cpio_odc_entry(cpio, app_name + ".app", 0040755, {});
    append_cpio_odc_entry(cpio, app_name + ".app/Contents", 0040755, {});
    append_cpio_odc_entry(cpio, app_name + ".app/Contents/Info.plist", 0100644,
                          std::vector<std::uint8_t>(info_plist.begin(), info_plist.end()));
    append_cpio_odc_entry(cpio, app_name + ".app/Contents/Resources/AppIcon.icns", 0100644,
                          icns_bytes);
    append_cpio_odc_entry(cpio, app_name + ".app/Contents/MacOS/" + app_name, 0100755, exe_bytes);
    append_cpio_odc_entry(cpio, "TRAILER!!!", 0, {});

    const auto payload_gz = zlib_compress(cpio.data(), cpio.size(), 16 + MAX_WBITS);
    if (payload_gz.empty()) return {};

    const std::string name_tag = "<name>Payload</name>";
    const std::string toc_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><xar><toc>"
        "<file id=\"1\">" + (name_after_data ? "" : name_tag) + "<data>"
        "<offset>0</offset>"
        "<length>" + std::to_string(payload_gz.size()) + "</length>"
        "<size>" + std::to_string(cpio.size()) + "</size>"
        "<encoding style=\"application/x-gzip\"/>"
        "</data>" + (name_after_data ? name_tag : "") + "</file></toc></xar>";
    const auto toc_z = zlib_compress(
        reinterpret_cast<const std::uint8_t*>(toc_xml.data()), toc_xml.size(), MAX_WBITS);
    if (toc_z.empty()) return {};

    std::vector<std::uint8_t> pkg = {'x', 'a', 'r', '!'};
    append_be16(pkg, 28);
    append_be16(pkg, 1);
    append_be64(pkg, toc_z.size());
    append_be64(pkg, toc_xml.size());
    append_be32(pkg, 1);
    pkg.insert(pkg.end(), toc_z.begin(), toc_z.end());
    pkg.insert(pkg.end(), payload_gz.begin(), payload_gz.end());
    return pkg;
}

bool test_plist_xml_and_binary() {
    using namespace finn::darwin;

    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\"><dict>"
        "<key>CFBundleName</key><string>Finn &amp; Mac</string>"
        "<key>Count</key><integer>42</integer>"
        "<key>Enabled</key><true/>"
        "<key>Items</key><array><string>one</string><string>two</string></array>"
        "</dict></plist>";
    auto parsed = parse_plist_bytes(xml);
    CHECK(parsed.has_value());
    CHECK(parsed->get_string("CFBundleName") == "Finn & Mac");
    CHECK(parsed->get("Count")->int_val == 42);
    CHECK(parsed->get_bool("Enabled", false));
    CHECK(parsed->get("Items")->array_val.size() == 2);

    std::vector<std::uint8_t> bp = {'b', 'p', 'l', 'i', 's', 't', '0', '0'};
    const std::uint8_t off0 = static_cast<std::uint8_t>(bp.size());
    bp.push_back(0xd1);
    bp.push_back(1);
    bp.push_back(2);
    const std::uint8_t off1 = static_cast<std::uint8_t>(bp.size());
    bp.push_back(0x5c);
    const char* k = "CFBundleName";
    bp.insert(bp.end(), k, k + 12);
    const std::uint8_t off2 = static_cast<std::uint8_t>(bp.size());
    bp.push_back(0x59);
    const char* v = "BinaryApp";
    bp.insert(bp.end(), v, v + 9);

    const std::uint64_t off_table = bp.size();
    bp.push_back(off0);
    bp.push_back(off1);
    bp.push_back(off2);

    for (int i = 0; i < 6; ++i) bp.push_back(0);
    bp.push_back(1);
    bp.push_back(1);
    append_be64(bp, 3);
    append_be64(bp, 0);
    append_be64(bp, off_table);

    auto bparsed = parse_plist_bytes(
        std::string_view(reinterpret_cast<const char*>(bp.data()), bp.size()));
    CHECK(bparsed.has_value());
    CHECK(bparsed->get_string("CFBundleName") == "BinaryApp");
    return true;
}

bool test_icns_extraction() {
    using namespace finn::darwin;
    const auto icns = make_test_icns();
    const auto png = extract_best_png_from_icns(
        std::string_view(reinterpret_cast<const char*>(icns.data()), icns.size()));
    CHECK(png.size() == 14);
    CHECK(png[12] == 0x99 && png[13] == 0xaa);
    return true;
}

bool test_native_pkg_dmg_and_zip_installers() {
    using namespace finn::darwin;
    const TempDir tmp;

    // 1. Real XAR .pkg
    const std::vector<std::uint8_t> fake_bin = {0xcf, 0xfa, 0xed, 0xfe, 0x0c, 0, 0, 1};
    const auto pkg = build_pkg_bytes("DemoMac", "org.finnos.DemoMac", fake_bin);
    CHECK(!pkg.empty());

    const fs::path pkg_path = tmp.path("DemoMac.pkg");
    {
        std::ofstream out(pkg_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(pkg.data()),
                  static_cast<std::streamsize>(pkg.size()));
    }

    const auto insp = inspect_pkg_native(pkg_path.string());
    CHECK(insp.valid);
    CHECK(insp.entries.size() == 1);
    CHECK(insp.entries[0].name == "Payload");

    const fs::path apps_dir = tmp.path("Applications");
    const fs::path icons_dir = tmp.path("icons");
    const auto res = install_pkg_native(pkg_path.string(), apps_dir.string(), icons_dir.string());
    CHECK(res.ok);
    CHECK(res.installed_apps.size() == 1);
    CHECK(res.installed_apps[0].name == "DemoMac");
    CHECK(res.installed_apps[0].bundle_id == "org.finnos.DemoMac");
    CHECK(!res.installed_apps[0].icon_path.empty());
    CHECK(fs::is_regular_file(res.installed_apps[0].icon_path));

    const auto discovered = discover_installed_macos_apps(apps_dir.string(), icons_dir.string());
    CHECK(discovered.size() == 1);
    CHECK(discovered[0].id == "macos:org-finnos-demomac");

    // 2. Native .zip installer + uninstall_macos_app
    const std::string zip_plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\"><dict>"
        "<key>CFBundleName</key><string>ZipApp</string>"
        "<key>CFBundleIdentifier</key><string>org.finnos.ZipApp</string>"
        "<key>CFBundleExecutable</key><string>ZipApp</string>"
        "<key>CFBundleIconFile</key><string>AppIcon</string>"
        "</dict></plist>\n";
    const auto zip_bytes = build_zip_archive({
        {"ZipApp.app/", {}, 0040755, false},
        {"ZipApp.app/Contents/Info.plist",
         std::vector<std::uint8_t>(zip_plist.begin(), zip_plist.end()), 0100644, true},
        {"ZipApp.app/Contents/Resources/AppIcon.icns", make_test_icns(), 0100644, true},
        {"ZipApp.app/Contents/MacOS/ZipApp", fake_bin, 0100755, false},
    });
    const fs::path zip_path = tmp.path("ZipApp.zip");
    {
        std::ofstream out(zip_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(zip_bytes.data()),
                  static_cast<std::streamsize>(zip_bytes.size()));
    }
    const auto zres = install_from_url_or_path(zip_path.string(), apps_dir.string(), icons_dir.string());
    CHECK(zres.ok);
    CHECK(zres.installed_apps.size() == 1);
    CHECK(zres.installed_apps[0].name == "ZipApp");
    CHECK(fs::is_regular_file(zres.installed_apps[0].icon_path));

    std::string removed_desc;
    std::string rm_err;
    CHECK(uninstall_macos_app("macos:org-finnos-zipapp", &removed_desc, &rm_err,
                              apps_dir.string(), icons_dir.string()));
    CHECK(!fs::exists(zres.installed_apps[0].bundle_path));
    CHECK(!fs::exists(zres.installed_apps[0].icon_path));

    // 3. Real UDIF .dmg inspection
    std::vector<std::uint8_t> mish(204 + 40, 0);
    std::memcpy(mish.data(), "mish", 4);
    mish[23] = 64;
    mish[203] = 1;
    mish[204] = 0x80;
    mish[207] = 0x05;
    mish[204 + 39] = 128;

    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (std::size_t i = 0; i < mish.size(); i += 3) {
        const std::uint32_t n =
            (static_cast<std::uint32_t>(mish[i]) << 16) |
            ((i + 1 < mish.size() ? static_cast<std::uint32_t>(mish[i + 1]) : 0u) << 8) |
            (i + 2 < mish.size() ? static_cast<std::uint32_t>(mish[i + 2]) : 0u);
        b64.push_back(kB64[(n >> 18) & 63]);
        b64.push_back(kB64[(n >> 12) & 63]);
        b64.push_back(i + 1 < mish.size() ? kB64[(n >> 6) & 63] : '=');
        b64.push_back(i + 2 < mish.size() ? kB64[n & 63] : '=');
    }

    const std::string dmg_xml =
        "<plist version=\"1.0\"><dict><key>resource-fork</key><dict><key>blkx</key><array>"
        "<dict><key>Name</key><string>Apple_HFS</string><key>Data</key><data>" +
        b64 + "</data></dict></array></dict></dict></plist>";

    std::vector<std::uint8_t> dmg(dmg_xml.begin(), dmg_xml.end());
    std::vector<std::uint8_t> koly(512, 0);
    std::memcpy(koly.data(), "koly", 4);
    std::uint64_t xlen = dmg_xml.size();
    for (int i = 0; i < 8; ++i) koly[224 + i] = static_cast<std::uint8_t>((xlen >> (56 - i * 8)) & 0xffu);
    koly[499] = 64;
    dmg.insert(dmg.end(), koly.begin(), koly.end());

    const fs::path dmg_path = tmp.path("DemoMac.dmg");
    {
        std::ofstream out(dmg_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(dmg.data()),
                  static_cast<std::streamsize>(dmg.size()));
    }
    const auto d_insp = inspect_dmg_native(dmg_path.string());
    CHECK(d_insp.valid);
    CHECK(d_insp.sector_count == 64);
    CHECK(d_insp.partitions.size() == 1);
    CHECK(d_insp.partitions[0].name == "Apple_HFS");
    CHECK(d_insp.partitions[0].compression == "UDZO (zlib)");
    return true;
}

bool test_live_http_download_install_and_exec() {
    using namespace finn::darwin;
    const TempDir tmp;

    // Prepare payload executable (real compiled Mach-O on Apple arm64, stub elsewhere)
    bool has_real_macho = false;
    std::vector<std::uint8_t> exe_bytes = {0xcf, 0xfa, 0xed, 0xfe, 0x0c, 0, 0, 1};
    const fs::path marker_file = tmp.path("http_app_ran.txt");

#if defined(__APPLE__) && defined(__aarch64__)
    {
        const fs::path src_m = tmp.path("http_app.m");
        const fs::path bin_m = tmp.path("HttpDemo");
        {
            std::ofstream out(src_m);
            out << "#import <Foundation/Foundation.h>\n"
                << "int main(int argc, char **argv) {\n"
                << "  @autoreleasepool {\n"
                << "    NSString *bundleId = [[NSBundle mainBundle] objectForInfoDictionaryKey:@\"CFBundleIdentifier\"];\n"
                << "    if (![bundleId isEqualToString:@\"org.finnos.HttpDemo\"]) return 11;\n"
                << "    if (argc < 2) return 12;\n"
                << "    NSString *dst = [NSString stringWithUTF8String:argv[1]];\n"
                << "    NSError *err = nil;\n"
                << "    BOOL ok = [@\"HTTP_APP_OK\" writeToFile:dst atomically:YES encoding:NSUTF8StringEncoding error:&err];\n"
                << "    return (ok && err == nil) ? 0 : 13;\n"
                << "  }\n"
                << "}\n";
        }
        const std::string cmd =
            "clang -O2 -fobjc-arc -framework Foundation \"" + src_m.string() +
            "\" -o \"" + bin_m.string() + "\" 2>/dev/null";
        if (std::system(cmd.c_str()) == 0) {
            std::ifstream in(bin_m, std::ios::binary | std::ios::ate);
            const std::streamoff sz = in.tellg();
            exe_bytes.resize(static_cast<std::size_t>(sz));
            in.seekg(0, std::ios::beg);
            in.read(reinterpret_cast<char*>(exe_bytes.data()), sz);
            has_real_macho = true;
        }
    }
#endif

    const auto pkg_bytes = build_pkg_bytes("HttpDemo", "org.finnos.HttpDemo", exe_bytes);
    CHECK(!pkg_bytes.empty());

    // Start ephemeral localhost HTTP/1.1 server on 127.0.0.1:0
    const int srv_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(srv_fd >= 0);
    int opt = 1;
    (void)::setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    CHECK(::bind(srv_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    CHECK(::listen(srv_fd, 4) == 0);

    socklen_t alen = sizeof(addr);
    CHECK(::getsockname(srv_fd, reinterpret_cast<struct sockaddr*>(&addr), &alen) == 0);
    const std::uint16_t port = ntohs(addr.sin_port);
    CHECK(port > 0);

    std::thread server_thread([srv_fd, &pkg_bytes]() {
        const int cfd = ::accept(srv_fd, nullptr, nullptr);
        if (cfd >= 0) {
            char req[1024] = {};
            (void)::recv(cfd, req, sizeof(req) - 1, 0);
            const std::string hdr =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: " + std::to_string(pkg_bytes.size()) + "\r\n"
                "Connection: close\r\n\r\n";
            (void)::send(cfd, hdr.data(), hdr.size(), 0);
            std::size_t off = 0;
            while (off < pkg_bytes.size()) {
                const ssize_t n = ::send(cfd, pkg_bytes.data() + off, pkg_bytes.size() - off, 0);
                if (n <= 0) break;
                off += static_cast<std::size_t>(n);
            }
            ::close(cfd);
        }
        ::close(srv_fd);
    });

    const fs::path apps_dir = tmp.path("Applications");
    const fs::path icons_dir = tmp.path("icons");
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/HttpDemo.pkg";
    const auto res = install_from_url_or_path(url, apps_dir.string(), icons_dir.string());
    server_thread.join();

    CHECK(res.ok);
    CHECK(res.installed_apps.size() == 1);
    CHECK(res.installed_apps[0].name == "HttpDemo");
    CHECK(res.installed_apps[0].bundle_id == "org.finnos.HttpDemo");
    CHECK(!res.installed_apps[0].icon_path.empty());
    CHECK(fs::is_regular_file(res.installed_apps[0].icon_path));

    if (has_real_macho) {
        CHECK(res.installed_apps[0].runnable);
        MachoLoader loader;
        std::string marker_arg = marker_file.string();
        char arg0[] = "HttpDemo";
        char* argv[] = {arg0, marker_arg.data(), nullptr};
        std::string run_err;
        const int rc = loader.load_and_run(res.installed_apps[0].bundle_path, 2, argv, &run_err);
        CHECK(rc == 0);
        CHECK(run_err.empty());
        CHECK(fs::is_regular_file(marker_file));
        std::ifstream mf(marker_file);
        std::string content;
        std::getline(mf, content);
        CHECK(content == "HTTP_APP_OK");
    }
    return true;
}

bool test_libsystem_expanded_symbols() {
    using namespace finn::darwin;
    std::unordered_map<std::string, void*> syms;
    register_libsystem_symbols(&syms);

    // Verify CommonCrypto SHA-256 against NIST FIPS 180-4 vectors
    CHECK(syms.count("_CC_SHA256") == 1);
    auto cc_sha256 = from_void_ptr<unsigned char*(*)(const void*, std::uint32_t, unsigned char*)>(
        syms["_CC_SHA256"]);
    unsigned char digest[32] = {};
    cc_sha256("abc", 3, digest);
    static constexpr unsigned char kExpectedAbc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    CHECK(std::memcmp(digest, kExpectedAbc, 32) == 0);

    // Verify arc4random_uniform & arc4random_buf
    auto arc4_u = from_void_ptr<std::uint32_t(*)(std::uint32_t)>(syms["_arc4random_uniform"]);
    for (int i = 0; i < 32; ++i) CHECK(arc4_u(10) < 10);
    auto arc4_buf = from_void_ptr<void(*)(void*, std::size_t)>(syms["_arc4random_buf"]);
    std::uint8_t rbuf[16] = {};
    arc4_buf(rbuf, sizeof(rbuf));

    // Exercise fcntl (F_GETFL/F_SETFL O_NONBLOCK), mmap/mprotect/munmap, opendir/readdir/closedir
    const TempDir tmp;
    {
        std::ofstream f(tmp.path("probe.txt"));
        f << "ok";
    }
    auto shim_opendir = from_void_ptr<void*(*)(const char*)>(syms["_opendir"]);
    struct DarwinDirent {
        std::uint64_t d_ino;
        std::uint64_t d_seekoff;
        std::uint16_t d_reclen;
        std::uint16_t d_namlen;
        std::uint8_t d_type;
        char d_name[1024];
    };
    auto shim_readdir = from_void_ptr<DarwinDirent*(*)(void*)>(syms["_readdir"]);
    auto shim_closedir = from_void_ptr<int(*)(void*)>(syms["_closedir"]);
    void* dh = shim_opendir(tmp.root().c_str());
    CHECK(dh != nullptr);
    bool saw_probe = false;
    while (DarwinDirent* ent = shim_readdir(dh)) {
        if (std::strcmp(ent->d_name, "probe.txt") == 0) saw_probe = true;
    }
    CHECK(shim_closedir(dh) == 0);
    CHECK(saw_probe);

    int pipe_fds[2] = {-1, -1};
    CHECK(::pipe(pipe_fds) == 0);
    auto shim_fcntl = from_void_ptr<int(*)(int, int, ...)>(syms["_fcntl"]);
    const int orig_fl = shim_fcntl(pipe_fds[0], F_GETFL);
    CHECK(orig_fl >= 0);
    // fcntl flags here are Darwin values: the libSystem shim translates them
    // to the host and back, so Darwin O_NONBLOCK (0x0004) is the right mask.
    CHECK(shim_fcntl(pipe_fds[0], F_SETFL, orig_fl | 0x0004) == 0);
    const int new_fl = shim_fcntl(pipe_fds[0], F_GETFL);
#if defined(__APPLE__)
    // The F_SETFL shim consumes Darwin arm64 variadic register slots. On a
    // Darwin host a plain C variadic call reaches it with the right ABI, so
    // the translated O_NONBLOCK bit is observable through F_GETFL.
    CHECK((new_fl & 0x0004) != 0);
#else
    // On non-Darwin hosts the same call cannot supply Darwin's register
    // slots, so the bit cannot be observed this way. F_SETFL success above is
    // still a real check; the full ABI is exercised by the live Mach-O test.
    (void)new_fl;
#endif
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);

    auto shim_mmap = from_void_ptr<void*(*)(void*, std::size_t, int, int, int, off_t)>(syms["_mmap"]);
    auto shim_mprotect = from_void_ptr<int(*)(void*, std::size_t, int)>(syms["_mprotect"]);
    auto shim_munmap = from_void_ptr<int(*)(void*, std::size_t)>(syms["_munmap"]);
    void* mem = shim_mmap(nullptr, 4096, 3 /* RW */, 0x1002 /* PRIVATE|ANON */, -1, 0);
    CHECK(mem != nullptr && mem != reinterpret_cast<void*>(-1));
    std::memset(mem, 0x5a, 64);
    CHECK(shim_mprotect(mem, 4096, 1 /* R */) == 0);
    CHECK(shim_munmap(mem, 4096) == 0);

    // Exercise pthread_create + pthread_join
    auto shim_pth_create = from_void_ptr<int(*)(pthread_t*, const pthread_attr_t*,
                                                void*(*)(void*), void*)>(syms["_pthread_create"]);
    auto shim_pth_join = from_void_ptr<int(*)(pthread_t, void**)>(syms["_pthread_join"]);
    pthread_t thr{};
    int worker_val = 0;
    auto worker_fn = [](void* p) -> void* {
        *static_cast<int*>(p) = 99;
        return nullptr;
    };
    CHECK(shim_pth_create(&thr, nullptr, worker_fn, &worker_val) == 0);
    CHECK(shim_pth_join(thr, nullptr) == 0);
    CHECK(worker_val == 99);

    // Verify remaining POSIX/socket/fortified symbols registered
    CHECK(syms.count("_pthread_cond_wait") == 1);
    CHECK(syms.count("_pthread_rwlock_rdlock") == 1);
    CHECK(syms.count("_sysconf") == 1);
    CHECK(syms.count("_realpath") == 1);
    CHECK(syms.count("_mkstemp") == 1);
    CHECK(syms.count("_chmod") == 1);
    CHECK(syms.count("_symlink") == 1);
    CHECK(syms.count("_readlink") == 1);
    CHECK(syms.count("_socket") == 1);
    CHECK(syms.count("_getaddrinfo") == 1);
    CHECK(syms.count("_freeaddrinfo") == 1);
    CHECK(syms.count("___memcpy_chk") == 1);
    CHECK(syms.count("___memmove_chk") == 1);
    CHECK(syms.count("___memset_chk") == 1);
    return true;
}

bool test_objc_foundation_cg_swift_and_appkit() {
    using namespace finn::darwin;
    const TempDir tmp;

    // Redirect HOME for isolated NSUserDefaults persistence verification
    const char* old_home_env = std::getenv("HOME");
    const std::string saved_home = old_home_env ? old_home_env : "";
    ::setenv("HOME", tmp.root().c_str(), 1);

    init_foundation();
    init_coregraphics();
    char arg0[] = "test_darwin";
    char arg1[] = "--flag";
    char* argv[] = {arg0, arg1, nullptr};
    set_foundation_process_args(2, argv);
    init_appkit(2, argv);

    void* pool = finn_objc_autoreleasePoolPush();

    // nil receiver must safely return 0 through objc_msgsend.S
    CHECK(objc_send_name<std::intptr_t>(nullptr, "length") == 0);

    // Expanded NSString
    ObjcId s = nsstring_from_utf8("/Users/finn/docs/notes.txt");
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(s, "stringByDeletingLastPathComponent")) ==
          "/Users/finn/docs");
    ObjcId replaced = objc_send_name<ObjcId>(
        s, "stringByReplacingOccurrencesOfString:withString:",
        nsstring_from_utf8("notes"), nsstring_from_utf8("report"));
    CHECK(nsstring_to_utf8(replaced) == "/Users/finn/docs/report.txt");
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(nsstring_from_utf8("abcdef"), "substringFromIndex:", 2)) == "cdef");
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(nsstring_from_utf8("abcdef"), "substringToIndex:", 3)) == "abc");
    ObjcId parts = objc_send_name<ObjcId>(
        nsstring_from_utf8("a,b,c"), "componentsSeparatedByString:", nsstring_from_utf8(","));
    CHECK(objc_send_name<std::size_t>(parts, "count") == 3);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(parts, "objectAtIndex:", 1)) == "b");

    // NSURL parsing
    ObjcClass* url_cls = finn_objc_getClass("NSURL");
    ObjcId u = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(url_cls), "URLWithString:",
        nsstring_from_utf8("https://finnos.org/downloads/App.pkg"));
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(u, "scheme")) == "https");
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(u, "host")) == "finnos.org");
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(u, "lastPathComponent")) == "App.pkg");
    ObjcId u2 = objc_send_name<ObjcId>(u, "URLByAppendingPathComponent:", nsstring_from_utf8("extra"));
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(u2, "lastPathComponent")) == "extra");

    // NSJSONSerialization round-trip
    ObjcClass* json_cls = finn_objc_getClass("NSJSONSerialization");
    const std::string raw_json = R"({"name":"Finn","ver":2,"ok":true,"empty":null,"arr":[1,2,3]})";
    ObjcId jdata = nsdata_from_bytes(raw_json.data(), raw_json.size());
    ObjcId jerr = nullptr;
    ObjcId jobj = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(json_cls), "JSONObjectWithData:options:error:",
        jdata, std::uintptr_t{0}, &jerr);
    CHECK(jobj != nullptr && jerr == nullptr);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(jobj, "objectForKey:", nsstring_from_utf8("name"))) == "Finn");
    ObjcId re_encoded = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(json_cls), "dataWithJSONObject:options:error:",
        jobj, std::uintptr_t{0}, &jerr);
    CHECK(re_encoded != nullptr && jerr == nullptr);
    CHECK(objc_send_name<std::size_t>(re_encoded, "length") > 20);

    // NSUserDefaults disk persistence + removeObjectForKey
    ObjcClass* ud_cls = finn_objc_getClass("NSUserDefaults");
    ObjcId ud = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(ud_cls), "standardUserDefaults");
    objc_send_name<void>(ud, "setObject:forKey:", nsstring_from_utf8("dark"), nsstring_from_utf8("theme"));
    objc_send_name<void>(ud, "setInteger:forKey:", std::intptr_t{42}, nsstring_from_utf8("count"));
    objc_send_name<void>(ud, "setBool:forKey:", static_cast<signed char>(1), nsstring_from_utf8("enabled"));
    CHECK(objc_send_name<signed char>(ud, "synchronize") == 1);
    CHECK(fs::is_regular_file(user_defaults_file_path()));
    reload_user_defaults();
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(ud, "stringForKey:", nsstring_from_utf8("theme"))) == "dark");
    CHECK(objc_send_name<std::intptr_t>(ud, "integerForKey:", nsstring_from_utf8("count")) == 42);
    CHECK(objc_send_name<signed char>(ud, "boolForKey:", nsstring_from_utf8("enabled")) == 1);
    objc_send_name<void>(ud, "removeObjectForKey:", nsstring_from_utf8("theme"));
    CHECK(objc_send_name<ObjcId>(ud, "stringForKey:", nsstring_from_utf8("theme")) == nullptr);

    // NSMutableData + writeToFile / dataWithContentsOfFile
    ObjcClass* mdata_cls = finn_objc_getClass("NSMutableData");
    ObjcId md = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(mdata_cls), "dataWithCapacity:", std::size_t{16});
    objc_send_name<void>(md, "appendBytes:length:", "Finn", std::size_t{4});
    objc_send_name<void>(md, "appendBytes:length:", "OS!", std::size_t{3});
    CHECK(objc_send_name<std::size_t>(md, "length") == 7);
    const fs::path bin_file = tmp.path("mut.bin");
    CHECK(objc_send_name<signed char>(md, "writeToFile:atomically:",
                                      nsstring_from_utf8(bin_file.string()),
                                      static_cast<signed char>(1)) == 1);
    ObjcClass* data_cls = finn_objc_getClass("NSData");
    ObjcId rd = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(data_cls),
                                       "dataWithContentsOfFile:",
                                       nsstring_from_utf8(bin_file.string()));
    CHECK(objc_send_name<std::size_t>(rd, "length") == 7);
    CHECK(std::memcmp(objc_send_name<const void*>(rd, "bytes"), "FinnOS!", 7) == 0);

    // CoreFoundation CFArray, CFDictionary, CFData, CFNumber
    std::unordered_map<std::string, void*> cf_syms;
    register_foundation_symbols(&cf_syms);
    auto cf_arr_new = from_void_ptr<ObjcId(*)(void*, std::intptr_t, const void*)>(cf_syms["_CFArrayCreateMutable"]);
    auto cf_arr_add = from_void_ptr<void(*)(ObjcId, const void*)>(cf_syms["_CFArrayAppendValue"]);
    auto cf_arr_cnt = from_void_ptr<std::intptr_t(*)(ObjcId)>(cf_syms["_CFArrayGetCount"]);
    auto cf_dict_new = from_void_ptr<ObjcId(*)(void*, std::intptr_t, const void*, const void*)>(cf_syms["_CFDictionaryCreateMutable"]);
    auto cf_dict_set = from_void_ptr<void(*)(ObjcId, const void*, const void*)>(cf_syms["_CFDictionarySetValue"]);
    auto cf_dict_get = from_void_ptr<const void*(*)(ObjcId, const void*)>(cf_syms["_CFDictionaryGetValue"]);
    auto cf_num_new = from_void_ptr<ObjcId(*)(void*, std::intptr_t, const void*)>(cf_syms["_CFNumberCreate"]);
    auto cf_num_get = from_void_ptr<unsigned char(*)(ObjcId, std::intptr_t, void*)>(cf_syms["_CFNumberGetValue"]);
    auto cf_data_new = from_void_ptr<ObjcId(*)(void*, const std::uint8_t*, std::intptr_t)>(cf_syms["_CFDataCreate"]);
    auto cf_data_len = from_void_ptr<std::intptr_t(*)(ObjcId)>(cf_syms["_CFDataGetLength"]);
    auto cf_release = from_void_ptr<void(*)(ObjcId)>(cf_syms["_CFRelease"]);

    ObjcId cfa = cf_arr_new(nullptr, 0, nullptr);
    ObjcId item_s = nsstring_from_utf8("cf-val");
    cf_arr_add(cfa, item_s);
    CHECK(cf_arr_cnt(cfa) == 1);

    std::int64_t num_in = 12345;
    std::int64_t num_out = 0;
    ObjcId cfn = cf_num_new(nullptr, 4 /* kCFNumberSInt64Type */, &num_in);
    CHECK(cf_num_get(cfn, 4, &num_out) == 1 && num_out == 12345);

    ObjcId cfd = cf_dict_new(nullptr, 0, nullptr, nullptr);
    ObjcId cf_key = nsstring_from_utf8("k");
    cf_dict_set(cfd, cf_key, cfn);
    CHECK(cf_dict_get(cfd, cf_key) == cfn);

    const std::uint8_t raw_b[3] = {1, 2, 3};
    ObjcId cfdata = cf_data_new(nullptr, raw_b, 3);
    CHECK(cf_data_len(cfdata) == 3);
    cf_release(cfdata);
    cf_release(cfd);
    cf_release(cf_key);
    cf_release(cfn);
    cf_release(cfa);
    cf_release(item_s);

    // NSRunLoop
    ObjcClass* rl_cls = finn_objc_getClass("NSRunLoop");
    CHECK(objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(rl_cls), "mainRunLoop") != nullptr);
    CHECK(objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(rl_cls), "currentRunLoop") != nullptr);

    // NSProcessInfo
    ObjcClass* pi_cls = finn_objc_getClass("NSProcessInfo");
    ObjcId pi = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(pi_cls), "processInfo");
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(pi, "processName")) == "test_darwin");
    ObjcId pargs = objc_send_name<ObjcId>(pi, "arguments");
    CHECK(objc_send_name<std::size_t>(pargs, "count") == 2);
    ObjcId penv = objc_send_name<ObjcId>(pi, "environment");
    CHECK(objc_send_name<std::size_t>(penv, "count") > 0);

    // CoreGraphics CGBitmapContext + CGRect geometry + QuartzCore CALayer
    std::unordered_map<std::string, void*> cg_syms;
    register_coregraphics_symbols(&cg_syms);
    auto cs_create = from_void_ptr<void*(*)()>(cg_syms["_CGColorSpaceCreateDeviceRGB"]);
    auto cs_release = from_void_ptr<void(*)(void*)>(cg_syms["_CGColorSpaceRelease"]);
    auto ctx_create = from_void_ptr<void*(*)(void*, std::size_t, std::size_t, std::size_t,
                                             std::size_t, void*, std::uint32_t)>(
        cg_syms["_CGBitmapContextCreate"]);
    auto ctx_data = from_void_ptr<void*(*)(void*)>(cg_syms["_CGBitmapContextGetData"]);
    auto ctx_fill_rgb = from_void_ptr<void(*)(void*, double, double, double, double)>(
        cg_syms["_CGContextSetRGBFillColor"]);
    struct Rect { double x, y, w, h; };
    auto ctx_fill_rect = from_void_ptr<void(*)(void*, Rect)>(cg_syms["_CGContextFillRect"]);
    auto ctx_release = from_void_ptr<void(*)(void*)>(cg_syms["_CGContextRelease"]);
    auto rect_empty = from_void_ptr<bool(*)(Rect)>(cg_syms["_CGRectIsEmpty"]);
    auto rect_eq = from_void_ptr<bool(*)(Rect, Rect)>(cg_syms["_CGRectEqualToRect"]);
    auto rect_inter = from_void_ptr<Rect(*)(Rect, Rect)>(cg_syms["_CGRectIntersection"]);
    auto rect_union = from_void_ptr<Rect(*)(Rect, Rect)>(cg_syms["_CGRectUnion"]);

    CHECK(rect_empty({0, 0, 0, 10}));
    CHECK(!rect_empty({0, 0, 10, 10}));
    CHECK(rect_eq(rect_inter({0, 0, 10, 10}, {5, 5, 10, 10}), {5, 5, 5, 5}));
    CHECK(rect_eq(rect_union({0, 0, 10, 10}, {5, 5, 10, 10}), {0, 0, 15, 15}));

    void* cs = cs_create();
    void* ctx = ctx_create(nullptr, 4, 4, 8, 16, cs, 1u);
    CHECK(ctx != nullptr);
    ctx_fill_rgb(ctx, 1.0, 0.5, 0.0, 1.0);
    ctx_fill_rect(ctx, {0.0, 0.0, 4.0, 4.0});
    const auto* px = static_cast<const std::uint8_t*>(ctx_data(ctx));
    CHECK(px[0] == 255 && px[1] == 128 && px[2] == 0 && px[3] == 255);
    ctx_release(ctx);
    cs_release(cs);

    auto cg_color_create = from_void_ptr<void*(*)(double, double, double, double)>(cg_syms["_CGColorCreateSRGB"]);
    auto cg_color_release = from_void_ptr<void(*)(void*)>(cg_syms["_CGColorRelease"]);
    void* bg = cg_color_create(0.2, 0.4, 0.8, 1.0);
    ObjcClass* layer_cls = finn_objc_getClass("CALayer");
    ObjcId layer = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(layer_cls), "layer");
    objc_send_name<void>(layer, "setCornerRadius:", 12.5);
    objc_send_name<void>(layer, "setBackgroundColor:", bg);
    CHECK(std::fabs(objc_send_name<double>(layer, "cornerRadius") - 12.5) < 1e-6);
    CHECK(objc_send_name<void*>(layer, "backgroundColor") == bg);
    cg_color_release(bg);

    // Swift runtime core ABI
    std::unordered_map<std::string, void*> sw_syms;
    register_swift_runtime_symbols(&sw_syms);
    auto sw_alloc = from_void_ptr<void*(*)(void*, std::size_t, std::size_t)>(sw_syms["_swift_allocObject"]);
    auto sw_retain = from_void_ptr<void*(*)(void*)>(sw_syms["_swift_retain"]);
    auto sw_release = from_void_ptr<void(*)(void*)>(sw_syms["_swift_release"]);
    auto sw_uniq = from_void_ptr<bool(*)(void*)>(sw_syms["_swift_isUniquelyReferenced_nonNull_native"]);
    auto sw_once = from_void_ptr<void(*)(std::intptr_t*, void(*)(void*), void*)>(sw_syms["_swift_once"]);

    const std::size_t base_live = swift_live_object_count();
    int dummy_meta = 0;
    void* sobj = sw_alloc(&dummy_meta, 32, 7);
    CHECK(sobj != nullptr);
    CHECK(swift_live_object_count() == base_live + 1);
    CHECK(sw_uniq(sobj));
    sw_retain(sobj);
    CHECK(!sw_uniq(sobj));
    sw_release(sobj);
    CHECK(sw_uniq(sobj));
    sw_release(sobj);
    CHECK(swift_live_object_count() == base_live);

    std::intptr_t once_pred = 0;
    int once_calls = 0;
    auto once_fn = [](void* p) { (*static_cast<int*>(p))++; };
    sw_once(&once_pred, once_fn, &once_calls);
    sw_once(&once_pred, once_fn, &once_calls);
    CHECK(once_calls == 1);

    // AppKit NSMenu / NSTextView / NSSlider / NSButton / NSPasteboard
    ObjcClass* menu_cls = finn_objc_getClass("NSMenu");
    ObjcId menu = objc_send_name<ObjcId>(
        objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(menu_cls), "alloc"),
        "initWithTitle:", nsstring_from_utf8("File"));
    ObjcId item = objc_send_name<ObjcId>(
        menu, "addItemWithTitle:action:keyEquivalent:",
        nsstring_from_utf8("Quit"), finn_sel_registerName("terminate:"), nsstring_from_utf8("q"));
    CHECK(item != nullptr);
    CHECK(objc_send_name<std::intptr_t>(menu, "numberOfItems") == 1);

    ObjcClass* tv_cls = finn_objc_getClass("NSTextView");
    ObjcId tv = objc_send_name<ObjcId>(
        objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(tv_cls), "alloc"),
        "initWithFrame:", Rect{0, 0, 200, 100});
    objc_send_name<void>(tv, "setString:", nsstring_from_utf8("Hello TextView"));
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(tv, "string")) == "Hello TextView");

    ObjcClass* sl_cls = finn_objc_getClass("NSSlider");
    ObjcId sl = objc_send_name<ObjcId>(
        objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(sl_cls), "alloc"),
        "initWithFrame:", Rect{0, 0, 120, 20});
    objc_send_name<void>(sl, "setMinValue:", 0.0);
    objc_send_name<void>(sl, "setMaxValue:", 50.0);
    objc_send_name<void>(sl, "setDoubleValue:", 25.0);
    CHECK(std::fabs(objc_send_name<double>(sl, "doubleValue") - 25.0) < 1e-6);

    ObjcClass* pb_cls = finn_objc_getClass("NSPasteboard");
    ObjcId pb = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(pb_cls), "generalPasteboard");
    objc_send_name<std::intptr_t>(pb, "clearContents");
    objc_send_name<signed char>(pb, "setString:forType:",
                                nsstring_from_utf8("ClipboardVal"),
                                nsstring_from_utf8("public.utf8-plain-text"));
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(
              pb, "stringForType:", nsstring_from_utf8("public.utf8-plain-text"))) == "ClipboardVal");

    finn_objc_autoreleasePoolPop(pool);
    if (!saved_home.empty()) ::setenv("HOME", saved_home.c_str(), 1);
    reload_user_defaults();
    return true;
}

template <typename Fn>
ObjcImp test_to_imp(Fn fn) {
    ObjcImp out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

int g_notification_count = 0;
ObjcId g_last_notification = nullptr;
void test_notification_observer(ObjcId, ObjcSel, ObjcId notification) {
    ++g_notification_count;
    // Posted notifications are autoreleased; an observer that keeps one must
    // own it. Holding the raw pointer alone dangles after a pool drains.
    finn_objc_release(g_last_notification);
    g_last_notification = finn_objc_retain(notification);
}

bool test_ns_attributed_string() {
    using namespace finn::darwin;
    init_foundation();
    void* pool = finn_objc_autoreleasePoolPush();

    ObjcClass* cls = finn_objc_getClass("NSAttributedString");
    ObjcClass* mcls = finn_objc_getClass("NSMutableAttributedString");
    CHECK(cls != nullptr && mcls != nullptr);

    ObjcId keys[1] = {nsstring_from_utf8("NSColor")};
    ObjcId vals[1] = {nsstring_from_utf8("red")};
    ObjcId attrs = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(finn_objc_getClass("NSDictionary")),
                                          "dictionaryWithObjects:forKeys:count:", vals, keys,
                                          std::size_t{1});
    CHECK(attrs != nullptr && objc_send_name<std::size_t>(attrs, "count") == 1);

    ObjcId a = objc_send_name<ObjcId>(objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "alloc"),
                                      "initWithString:attributes:", nsstring_from_utf8("Hello"),
                                      attrs);
    CHECK(a != nullptr);
    CHECK(objc_send_name<std::size_t>(a, "length") == 5);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(a, "string")) == "Hello");

    DarwinRange effective{};
    ObjcId got = objc_send_name<ObjcId>(a, "attributesAtIndex:effectiveRange:", std::size_t{2},
                                        &effective);
    CHECK(got != nullptr && objc_send_name<std::size_t>(got, "count") == 1);
    CHECK(effective.location == 0 && effective.length == 5);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(got, "objectForKey:", keys[0])) == "red");

    ObjcId sub = objc_send_name<ObjcId>(a, "attributedSubstringFromRange:",
                                        DarwinRange{1, 3});
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(sub, "string")) == "ell");
    ObjcId sub_attrs = objc_send_name<ObjcId>(sub, "attributesAtIndex:effectiveRange:",
                                              std::size_t{0}, &effective);
    CHECK(objc_send_name<std::size_t>(sub_attrs, "count") == 1);

    ObjcId a2 = objc_send_name<ObjcId>(objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "alloc"),
                                       "initWithString:attributes:", nsstring_from_utf8("Hello"),
                                       attrs);
    CHECK(objc_send_name<signed char>(a, "isEqualToAttributedString:", a2) == 1);
    ObjcId plain = objc_send_name<ObjcId>(objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "alloc"),
                                          "initWithString:", nsstring_from_utf8("Hello"));
    CHECK(objc_send_name<signed char>(a, "isEqualToAttributedString:", plain) == 0);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(a, "description")) == "Hello");

    ObjcId m = objc_send_name<ObjcId>(objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(mcls), "alloc"),
                                      "initWithString:", nsstring_from_utf8("abcdef"));
    CHECK(objc_send_name<std::size_t>(m, "length") == 6);
    objc_send_name<void>(m, "addAttribute:value:range:", nsstring_from_utf8("NSUnderline"),
                         nsstring_from_utf8("single"), DarwinRange{1, 2});
    ObjcId at1 = objc_send_name<ObjcId>(m, "attributesAtIndex:effectiveRange:", std::size_t{1},
                                        &effective);
    CHECK(effective.location == 1 && effective.length == 2);
    CHECK(objc_send_name<std::size_t>(at1, "count") == 1);
    ObjcId at0 = objc_send_name<ObjcId>(m, "attributesAtIndex:effectiveRange:", std::size_t{0},
                                        &effective);
    CHECK(objc_send_name<std::size_t>(at0, "count") == 0);

    objc_send_name<void>(m, "removeAttribute:range:", nsstring_from_utf8("NSUnderline"),
                         DarwinRange{1, 2});
    at1 = objc_send_name<ObjcId>(m, "attributesAtIndex:effectiveRange:", std::size_t{1},
                                 &effective);
    CHECK(objc_send_name<std::size_t>(at1, "count") == 0);

    objc_send_name<void>(m, "setAttributes:range:", attrs, DarwinRange{0, 2});
    ObjcId at_02 = objc_send_name<ObjcId>(m, "attributesAtIndex:effectiveRange:", std::size_t{0},
                                          &effective);
    CHECK(objc_send_name<std::size_t>(at_02, "count") == 1 && effective.length == 2);

    objc_send_name<void>(m, "replaceCharactersInRange:withString:", DarwinRange{0, 1},
                         nsstring_from_utf8("ZZ"));
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(m, "string")) == "ZZbcdef");
    CHECK(objc_send_name<std::size_t>(m, "length") == 7);

    objc_send_name<void>(m, "appendAttributedString:", a);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(m, "string")) == "ZZbcdefHello");

    finn_objc_autoreleasePoolPop(pool);
    return true;
}

bool test_ns_date_formatter_and_calendar() {
    using namespace finn::darwin;
    init_foundation();
    void* pool = finn_objc_autoreleasePoolPush();

    ObjcId fmt = objc_send_name<ObjcId>(objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSDateFormatter")), "alloc"), "init");
    CHECK(fmt != nullptr);
    objc_send_name<void>(fmt, "setDateFormat:", nsstring_from_utf8("yyyy-MM-dd HH:mm:ss"));
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(fmt, "dateFormat")) == "yyyy-MM-dd HH:mm:ss");

    ObjcId date = objc_send_name<ObjcId>(fmt, "dateFromString:",
                                         nsstring_from_utf8("2024-03-05 07:08:09"));
    CHECK(date != nullptr);
    const double expected = 1709622489.0;
    CHECK(std::fabs(objc_send_name<double>(date, "timeIntervalSince1970") - expected) < 1e-3);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(fmt, "stringFromDate:", date)) ==
          "2024-03-05 07:08:09");
    CHECK(objc_send_name<ObjcId>(fmt, "dateFromString:", nsstring_from_utf8("garbage")) == nullptr);
    CHECK(objc_send_name<ObjcId>(fmt, "stringFromDate:", nullptr) == nullptr);

    ObjcId cal = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(finn_objc_getClass("NSCalendar")),
                                        "currentCalendar");
    CHECK(cal != nullptr);
    const std::uintptr_t year = 1u << 2, month = 1u << 3, day = 1u << 4;
    const std::uintptr_t hour = 1u << 5, minute = 1u << 6, second = 1u << 7, weekday = 1u << 8;
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", year, date) == 2024);
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", month, date) == 3);
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", day, date) == 5);
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", hour, date) == 7);
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", minute, date) == 8);
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", second, date) == 9);
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", weekday, date) == 3);

    const std::uintptr_t units = year | month | day | hour | minute | second;
    ObjcId comps = objc_send_name<ObjcId>(cal, "components:fromDate:", units, date);
    CHECK(comps != nullptr);
    CHECK(objc_send_name<std::int64_t>(comps, "year") == 2024);
    CHECK(objc_send_name<std::int64_t>(comps, "month") == 3);
    CHECK(objc_send_name<std::int64_t>(comps, "day") == 5);
    CHECK(objc_send_name<std::int64_t>(comps, "valueForComponent:", year) == 2024);
    ObjcId rebuilt = objc_send_name<ObjcId>(cal, "dateFromComponents:", comps);
    CHECK(std::fabs(objc_send_name<double>(rebuilt, "timeIntervalSince1970") - expected) < 1e-3);

    ObjcId later = objc_send_name<ObjcId>(cal, "dateByAddingUnit:value:toDate:options:", day,
                                          std::int64_t{10}, date, std::uintptr_t{0});
    CHECK(later != nullptr);
    CHECK(objc_send_name<std::int64_t>(cal, "component:fromDate:", day, later) == 15);
    CHECK(objc_send_name<ObjcId>(cal, "dateByAddingUnit:value:toDate:options:",
                                 std::uintptr_t{1} << 30, std::int64_t{1}, date,
                                 std::uintptr_t{0}) == nullptr);
    CHECK(objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(finn_objc_getClass("NSCalendar")),
                                 "calendarWithIdentifier:", nsstring_from_utf8("NoSuchCalendar")) ==
          nullptr);

    finn_objc_autoreleasePoolPop(pool);
    return true;
}

bool test_ns_search_paths_and_notifications() {
    using namespace finn::darwin;
    const TempDir tmp;
    const char* old_home = std::getenv("HOME");
    const std::string saved_home = old_home ? old_home : "";
    ::setenv("HOME", tmp.root().c_str(), 1);
    init_foundation();

    std::unordered_map<std::string, void*> syms;
    register_foundation_symbols(&syms);
    CHECK(syms.count("_NSSearchPathForDirectoriesInDomains") == 1);
    CHECK(syms.count("_NSCalendarIdentifierGregorian") == 1);
    CHECK(syms.count("_NSFontAttributeName") == 1);
    CHECK(syms.count("_NSHomeDirectory") == 1);

    auto paths_fn = from_void_ptr<ObjcId (*)(std::uintptr_t, std::uintptr_t, signed char)>(
        syms["_NSSearchPathForDirectoriesInDomains"]);
    ObjcId documents = paths_fn(9, 1, 0);
    CHECK(objc_send_name<std::size_t>(documents, "count") == 1);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(documents, "objectAtIndex:", std::size_t{0})) ==
          (tmp.root().string() + "/Documents"));
    ObjcId caches = paths_fn(13, 1, 0);
    CHECK(objc_send_name<std::size_t>(caches, "count") == 1);
    CHECK(objc_send_name<std::size_t>(paths_fn(4242, 1, 0), "count") == 0);
    CHECK(objc_send_name<std::size_t>(paths_fn(9, 0, 0), "count") == 0);

    auto home_fn = from_void_ptr<const char* (*)()>(syms["_NSHomeDirectory"]);
    CHECK(std::string(home_fn()) == tmp.root().string());

    ObjcClass* center_cls = finn_objc_getClass("NSNotificationCenter");
    ObjcId center = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(center_cls), "defaultCenter");
    CHECK(center != nullptr);
    ObjcClass* observer_cls = finn_objc_allocateClassPair(finn_objc_getClass("NSObject"),
                                                          "FinnTestObserver");
    CHECK(finn_class_addMethod(observer_cls, finn_sel_registerName("onPing:"),
                               test_to_imp(&test_notification_observer)));
    finn_objc_registerClassPair(observer_cls);
    ObjcId observer = finn_objc_alloc_object(observer_cls);

    ObjcId name = nsstring_from_utf8("FinnPing");
    objc_send_name<void>(center, "addObserver:selector:name:object:", observer,
                         finn_sel_registerName("onPing:"), name, nullptr);
    g_notification_count = 0;
    objc_send_name<void>(center, "postNotificationName:object:userInfo:", name, nullptr, nullptr);
    CHECK(g_notification_count == 1);
    CHECK(g_last_notification != nullptr);
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(g_last_notification, "name")) == "FinnPing");
    CHECK(objc_send_name<ObjcId>(g_last_notification, "object") == nullptr);

    objc_send_name<void>(center, "removeObserver:name:object:", observer, name, nullptr);
    objc_send_name<void>(center, "postNotificationName:object:userInfo:", name, nullptr, nullptr);
    CHECK(g_notification_count == 1);

    ObjcId notification = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSNotification")),
        "notificationWithName:object:userInfo:", name, nullptr, nullptr);
    CHECK(notification != nullptr);
    CHECK(objc_send_name<ObjcId>(notification, "userInfo") == nullptr);
    objc_send_name<void>(center, "addObserver:selector:name:object:", observer,
                         finn_sel_registerName("onPing:"), name, nullptr);
    objc_send_name<void>(center, "postNotification:", notification);
    CHECK(g_notification_count == 2);
    objc_send_name<void>(center, "removeObserver:", observer);

    finn_objc_release(g_last_notification);
    g_last_notification = nullptr;
    if (!saved_home.empty()) ::setenv("HOME", saved_home.c_str(), 1);
    return true;
}

bool test_installer_symlink_and_apple_toc_regressions() {
    using namespace finn::darwin;
    const TempDir tmp;

    // BUG A: a self-referential "Applications" symlink must not abort the scan
    // before a sibling .app bundle is discovered.
    const fs::path stage = tmp.path("dmg-stage");
    fs::create_directories(stage / "Applications");
    fs::create_directories(stage / "Nightfall.app" / "Contents" / "MacOS");
    {
        std::ofstream plist(stage / "Nightfall.app" / "Contents" / "Info.plist");
        plist << "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
              << "<key>CFBundleName</key><string>Nightfall</string>"
              << "<key>CFBundleIdentifier</key><string>org.finnos.Nightfall</string>"
              << "<key>CFBundleExecutable</key><string>Nightfall</string>"
              << "</dict></plist>";
    }
    std::error_code ec;
    fs::create_directory_symlink(stage / "Applications", stage / "ApplicationsLoop", ec);
    if (ec) {
        ec.clear();
        fs::create_symlink(stage / "Applications", stage / "ApplicationsLoop", ec);
    }
    CHECK(!ec);
    const auto found = discover_app_bundles_in_tree(stage.string());
    CHECK(found.size() == 1);
    CHECK(fs::path(found[0]).filename() == "Nightfall.app");

    // BUG B: Apple's XAR TOC writes <name> AFTER </data>. The real Payload
    // must still be parsed and installed.
    const std::string exe = "APP";
    const std::vector<std::uint8_t> exe_bytes(exe.begin(), exe.end());
    const auto pkg = build_pkg_bytes("AquaSKK", "com.finnos.AquaSKK", exe_bytes, true);
    CHECK(!pkg.empty());
    const fs::path pkg_path = tmp.path("apple-toc.pkg");
    {
        std::ofstream out(pkg_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(pkg.data()),
                  static_cast<std::streamsize>(pkg.size()));
    }
    const auto insp = inspect_pkg_native(pkg_path.string());
    CHECK(insp.valid);
    CHECK(insp.entries.size() == 1);
    CHECK(insp.entries[0].name == "Payload");
    CHECK(insp.entries[0].length > 0);

    const auto res = install_pkg_native(pkg_path.string(), (tmp.path("apps")).string(),
                                        (tmp.path("icons")).string());
    if (!res.ok) std::fprintf(stderr, "apple-toc install error: %s\n", res.error.c_str());
    CHECK(res.ok);
    CHECK(res.installed_apps.size() == 1);
    CHECK(res.installed_apps[0].name == "AquaSKK");
    return true;
}

bool test_framework_coverage_honesty() {
    using namespace finn::darwin;
    const auto& frameworks = unimplemented_frameworks();
    for (const char* n : {"SwiftUI", "Swift", "Metal", "WebKit", "CoreData"}) {
        CHECK(std::any_of(frameworks.begin(), frameworks.end(),
                          [n](const FrameworkGap& f) { return f.name == n; }));
    }
    CHECK(framework_reason("SwiftUI").find("partial") != std::string::npos);
    CHECK(framework_reason("Foundation").empty());
    CHECK(framework_reason("CoreGraphics").empty());
    CHECK(symbol_reason("_swift_getTypeByMangledNameInContext2").find("metadata") !=
          std::string::npos);
    CHECK(symbol_reason("$s7SwiftUI4TextVyACSScfC").find("SwiftUI") != std::string::npos);
    CHECK(symbol_reason("_$ss5print_9separator").find("Swift") != std::string::npos);
    CHECK(symbol_reason("_MTLCreateSystemDefaultDevice").find("Metal") != std::string::npos);
    CHECK(symbol_reason("_some_unknown_symbol").empty());
    CHECK(coverage_report().find("SwiftUI") != std::string::npos);
    return true;
}

bool test_live_swift_honest_diagnosis() {
    using namespace finn::darwin;
    const TempDir tmp;
    const fs::path src = tmp.path("swift_probe.swift");
    const fs::path bin = tmp.path("swift_probe");
    {
        std::ofstream out(src);
        out << "print(\"hello from Swift\")\n";
    }
    const std::string cmd = "xcrun swiftc -O \"" + src.string() + "\" -o \"" +
                            bin.string() + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return true;  // Skip when swiftc/SDK is unavailable.

    MachoLoader diag_loader;
    const auto d = diag_loader.diagnose(bin.string());
    CHECK(d.valid);
    CHECK(d.arch_compatible);
    CHECK(!d.runnable);
    bool saw_metadata_gap = false;
    for (const auto& g : d.symbol_gaps) {
        if (g.name.find("swift_getTypeByMangledNameInContext2") != std::string::npos &&
            g.reason.find("metadata") != std::string::npos) {
            saw_metadata_gap = true;
        }
    }
    CHECK(saw_metadata_gap);

    char arg0[] = "swift_probe";
    char* argv[] = {arg0, nullptr};
    MachoLoader runner;
    std::string err;
    const int rc = runner.load_and_run(bin.string(), 1, argv, &err);
    CHECK(rc != 0);
    CHECK(err.find("cannot launch") != std::string::npos);
    CHECK(err.find("not implemented") != std::string::npos);
    return true;
}

#if defined(__APPLE__) && defined(__aarch64__)
bool test_live_macho_comprehensive() {
    using namespace finn::darwin;
    const TempDir tmp;

    const char* old_home_env = std::getenv("HOME");
    const std::string saved_home = old_home_env ? old_home_env : "";
    ::setenv("HOME", tmp.root().c_str(), 1);
    reload_user_defaults();

    // 1. Live unmodified Clang Mach-O exercising Foundation, CoreGraphics, QuartzCore,
    //    AppKit, CommonCrypto SHA-256, and Swift core runtime ABI!
    const fs::path src_m = tmp.path("live_full.m");
    const fs::path bin_m = tmp.path("live_full");
    {
        std::ofstream out(src_m);
        out << "#import <Foundation/Foundation.h>\n"
            << "#import <CoreGraphics/CoreGraphics.h>\n"
            << "#import <QuartzCore/QuartzCore.h>\n"
            << "#import <AppKit/AppKit.h>\n"
            << "#import <CommonCrypto/CommonDigest.h>\n"
            << "#include <stdint.h>\n"
            << "#include <string.h>\n"
            << "\n"
            << "extern void *swift_allocObject(void *meta, size_t size, size_t alignMask);\n"
            << "extern void *swift_retain(void *obj);\n"
            << "extern void swift_release(void *obj);\n"
            << "extern _Bool swift_isUniquelyReferenced_nonNull_native(void *obj);\n"
            << "\n"
            << "@interface WinDelegate : NSObject <NSWindowDelegate>\n"
            << "@property (nonatomic) int closedCount;\n"
            << "@end\n"
            << "@implementation WinDelegate\n"
            << "- (void)windowWillClose:(NSNotification *)n { (void)n; self.closedCount += 1; }\n"
            << "@end\n"
            << "\n"
            << "int main(int argc, char **argv) {\n"
            << "  @autoreleasepool {\n"
            << "    if (argc < 2) return 1;\n"
            << "    NSString *workDir = [NSString stringWithUTF8String:argv[1]];\n"
            << "\n"
            << "    // 1. NSJSONSerialization round-trip\n"
            << "    NSDictionary *orig = @{@\"os\": @\"FinnOS\", @\"ver\": @2, @\"ready\": @YES, @\"nilv\": [NSNull null]};\n"
            << "    NSError *err = nil;\n"
            << "    NSData *jdata = [NSJSONSerialization dataWithJSONObject:orig options:0 error:&err];\n"
            << "    if (!jdata || err) return 2;\n"
            << "    NSDictionary *decoded = [NSJSONSerialization JSONObjectWithData:jdata options:0 error:&err];\n"
            << "    if (!decoded || err) return 3;\n"
            << "    if (![decoded[@\"os\"] isEqualToString:@\"FinnOS\"]) return 4;\n"
            << "    if ([decoded[@\"ver\"] integerValue] != 2) return 5;\n"
            << "    if (![decoded[@\"ready\"] boolValue]) return 6;\n"
            << "    if (decoded[@\"nilv\"] != [NSNull null]) return 7;\n"
            << "\n"
            << "    // 2. NSFileManager create/write/list/remove\n"
            << "    NSFileManager *fm = [NSFileManager defaultManager];\n"
            << "    NSString *sub = [workDir stringByAppendingPathComponent:@\"nested/dir\"];\n"
            << "    if (![fm createDirectoryAtPath:sub withIntermediateDirectories:YES attributes:nil error:&err]) return 8;\n"
            << "    NSString *fpath = [sub stringByAppendingPathComponent:@\"hello.txt\"];\n"
            << "    if (![@\"Hello Darwin\" writeToFile:fpath atomically:YES encoding:NSUTF8StringEncoding error:&err]) return 9;\n"
            << "    NSString *readBack = [NSString stringWithContentsOfFile:fpath encoding:NSUTF8StringEncoding error:&err];\n"
            << "    if (![readBack isEqualToString:@\"Hello Darwin\"]) return 10;\n"
            << "    NSArray *entries = [fm contentsOfDirectoryAtPath:sub error:&err];\n"
            << "    if ([entries count] != 1 || ![entries[0] isEqualToString:@\"hello.txt\"]) return 11;\n"
            << "    if (![fm removeItemAtPath:fpath error:&err]) return 12;\n"
            << "\n"
            << "    // 3. NSUserDefaults disk persistence\n"
            << "    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];\n"
            << "    [ud setObject:@\"Nord\" forKey:@\"palette\"];\n"
            << "    [ud setInteger:77 forKey:@\"zoom\"];\n"
            << "    [ud setBool:YES forKey:@\"antialias\"];\n"
            << "    if (![ud synchronize]) return 13;\n"
            << "\n"
            << "    // 4. NSError & NSDate\n"
            << "    NSError *customErr = [NSError errorWithDomain:@\"org.finnos.test\" code:404 userInfo:@{NSLocalizedDescriptionKey: @\"Missing\"}];\n"
            << "    if (![[customErr domain] isEqualToString:@\"org.finnos.test\"] || [customErr code] != 404) return 14;\n"
            << "    if (![[customErr localizedDescription] isEqualToString:@\"Missing\"]) return 15;\n"
            << "    NSDate *now = [NSDate date];\n"
            << "    if ([now timeIntervalSince1970] < 1700000000.0) return 16;\n"
            << "\n"
            << "    // 5. CoreGraphics CGBitmapContextCreate + CGContextFillRect pixel verification\n"
            << "    uint8_t pixels[4 * 4 * 4];\n"
            << "    memset(pixels, 0, sizeof(pixels));\n"
            << "    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();\n"
            << "    CGContextRef ctx = CGBitmapContextCreate(pixels, 4, 4, 8, 16, cs, kCGImageAlphaPremultipliedLast);\n"
            << "    if (!ctx) return 17;\n"
            << "    CGContextSaveGState(ctx);\n"
            << "    CGContextSetRGBFillColor(ctx, 0.0, 1.0, 0.0, 1.0);\n"
            << "    CGContextFillRect(ctx, CGRectMake(0, 0, 4, 4));\n"
            << "    CGContextRestoreGState(ctx);\n"
            << "    if (pixels[0] != 0 || pixels[1] != 255 || pixels[2] != 0 || pixels[3] != 255) return 18;\n"
            << "    CGContextClearRect(ctx, CGRectMake(0, 3, 1, 1));\n"
            << "    if (pixels[1] != 0 || pixels[3] != 0) return 19;\n"
            << "    CGContextRelease(ctx);\n"
            << "    CGColorSpaceRelease(cs);\n"
            << "\n"
            << "    // 6. QuartzCore CALayer\n"
            << "    CALayer *rootLayer = [CALayer layer];\n"
            << "    [rootLayer setFrame:CGRectMake(0, 0, 100, 50)];\n"
            << "    [rootLayer setCornerRadius:8.0];\n"
            << "    CALayer *childLayer = [CALayer layer];\n"
            << "    [rootLayer addSublayer:childLayer];\n"
            << "    if ([rootLayer cornerRadius] != 8.0) return 20;\n"
            << "\n"
            << "    // 7. AppKit NSMenu, NSTextView, NSSlider, NSButton, NSWindow + delegate\n"
            << "    NSMenu *m = [[NSMenu alloc] initWithTitle:@\"App\"];\n"
            << "    NSMenuItem *mi = [m addItemWithTitle:@\"Open\" action:@selector(description) keyEquivalent:@\"o\"];\n"
            << "    if (![[mi title] isEqualToString:@\"Open\"]) return 21;\n"
            << "    NSTextView *tv = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 200, 80)];\n"
            << "    [tv setString:@\"FinnOS Notes\"];\n"
            << "    [tv setEditable:YES];\n"
            << "    if (![[tv string] isEqualToString:@\"FinnOS Notes\"]) return 22;\n"
            << "    NSSlider *sl = [[NSSlider alloc] initWithFrame:NSMakeRect(0, 0, 100, 20)];\n"
            << "    [sl setMinValue:0.0];\n"
            << "    [sl setMaxValue:100.0];\n"
            << "    [sl setDoubleValue:64.0];\n"
            << "    if ([sl doubleValue] != 64.0) return 23;\n"
            << "    NSButton *btn = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 80, 24)];\n"
            << "    [btn setButtonType:NSButtonTypeSwitch];\n"
            << "    [btn setState:NSControlStateValueOn];\n"
            << "    if ([btn state] != NSControlStateValueOn) return 24;\n"
            << "    NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300) styleMask:15 backing:2 defer:NO];\n"
            << "    WinDelegate *del = [[WinDelegate alloc] init];\n"
            << "    [win setDelegate:del];\n"
            << "    [win makeKeyAndOrderFront:nil];\n"
            << "    if (![win isVisible]) return 25;\n"
            << "    [win close];\n"
            << "    if ([win isVisible] || del.closedCount != 1) return 26;\n"
            << "\n"
            << "    // 8. Swift core runtime ABI\n"
            << "    int fakeMeta = 0;\n"
            << "    void *sw = swift_allocObject(&fakeMeta, 48, 7);\n"
            << "    if (!sw || !swift_isUniquelyReferenced_nonNull_native(sw)) return 27;\n"
            << "    swift_retain(sw);\n"
            << "    if (swift_isUniquelyReferenced_nonNull_native(sw)) return 28;\n"
            << "    swift_release(sw);\n"
            << "    if (!swift_isUniquelyReferenced_nonNull_native(sw)) return 29;\n"
            << "    swift_release(sw);\n"
            << "\n"
            << "    // 9. CommonCrypto CC_SHA256\n"
            << "    unsigned char md[CC_SHA256_DIGEST_LENGTH];\n"
            << "    CC_SHA256(\"abc\", 3, md);\n"
            << "    if (md[0] != 0xba || md[1] != 0x78 || md[31] != 0xad) return 30;\n"
            << "\n"
            << "    return 0;\n"
            << "  }\n"
            << "}\n";
    }

    const std::string cmd =
        "clang -O2 -fobjc-arc -framework Foundation -framework CoreGraphics "
        "-framework QuartzCore -framework AppKit -Wl,-undefined,dynamic_lookup \"" +
        src_m.string() + "\" -o \"" + bin_m.string() + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return true;  // Skip if clang unavailable

    MachoLoader loader;
    const auto diag = loader.diagnose(bin_m.string());
    if (!diag.runnable) {
        std::fprintf(stderr, "live_full diagnose not runnable: %s\n", diag.summary.c_str());
        for (const auto& ms : diag.missing_symbols) {
            std::fprintf(stderr, "  missing: %s\n", ms.c_str());
        }
    }
    CHECK(diag.valid);
    CHECK(diag.arch_compatible);
    CHECK(diag.runnable);
    CHECK(diag.objc_class_count == 1);

    std::string work_arg = tmp.path("work").string();
    char arg0[] = "live_full";
    char* argv[] = {arg0, work_arg.data(), nullptr};
    std::string err;
    const int rc = loader.load_and_run(bin_m.string(), 2, argv, &err);
    if (rc != 0) {
        std::fprintf(stderr, "live_full exited with code %d (err: %s)\n", rc, err.c_str());
    }
    CHECK(rc == 0);
    CHECK(err.empty());

    // Verify NSUserDefaults written by the live Mach-O persisted to disk and reloads cleanly!
    reload_user_defaults();
    ObjcClass* ud_cls = finn_objc_getClass("NSUserDefaults");
    ObjcId ud = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(ud_cls), "standardUserDefaults");
    CHECK(nsstring_to_utf8(objc_send_name<ObjcId>(ud, "stringForKey:", nsstring_from_utf8("palette"))) == "Nord");
    CHECK(objc_send_name<std::intptr_t>(ud, "integerForKey:", nsstring_from_utf8("zoom")) == 77);
    CHECK(objc_send_name<signed char>(ud, "boolForKey:", nsstring_from_utf8("antialias")) == 1);

    // 2. Verify honest diagnosis of unimplemented SwiftUI symbol (never faked!)
    const fs::path src_sui = tmp.path("swiftui_probe.c");
    const fs::path bin_sui = tmp.path("swiftui_probe");
    {
        std::ofstream out(src_sui);
        out << "extern void $s7SwiftUI4TextVyACSScfC(void) __asm__(\"_$s7SwiftUI4TextVyACSScfC\");\n"
            << "int main(void) { $s7SwiftUI4TextVyACSScfC(); return 0; }\n";
    }
    const std::string cmd_sui =
        "clang -O2 -Wl,-undefined,dynamic_lookup \"" + src_sui.string() +
        "\" -o \"" + bin_sui.string() + "\" 2>/dev/null";
    if (std::system(cmd_sui.c_str()) == 0) {
        MachoLoader diag_loader;
        const auto d_sui = diag_loader.diagnose(bin_sui.string());
        CHECK(d_sui.valid);
        CHECK(!d_sui.runnable);
        bool saw_swiftui = false;
        for (const auto& s : d_sui.missing_symbols) {
            if (s.find("SwiftUI") != std::string::npos) saw_swiftui = true;
        }
        CHECK(saw_swiftui);
    }

    if (!saved_home.empty()) ::setenv("HOME", saved_home.c_str(), 1);
    reload_user_defaults();
    return true;
}
#endif

// The real C/ObjC/C++/dispatch shims used by downloaded apps must behave, not
// merely exist as names in the loader table.
static bool test_compat_symbols() {
    using namespace finn::darwin;
    init_objc_runtime();
    init_foundation();

    std::unordered_map<std::string, void*> syms;
    register_compat_symbols(&syms);
    CHECK(compat_has_symbol("_NSEqualRects"));
    CHECK(compat_has_symbol("_dispatch_semaphore_create"));
    CHECK(compat_has_symbol("_objc_setAssociatedObject"));
    CHECK(compat_has_symbol("__Znwm"));
    CHECK(!compat_has_symbol("_MTLCreateSystemDefaultDevice"));
    CHECK(syms.count("_NSIntersectionRange") == 1);
    CHECK(syms.count("_NSViewNoIntrinsicMetric") == 1);

    // Foundation geometry is computed with real NS behaviour.
    struct Point { double x, y; };
    struct Size { double width, height; };
    struct Rect { Point origin; Size size; };
    struct Range { std::size_t location, length; };
    auto equal_rects = from_void_ptr<bool(*)(Rect, Rect)>(syms["_NSEqualRects"]);
    auto inset_rect = from_void_ptr<Rect(*)(Rect, double, double)>(syms["_NSInsetRect"]);
    auto union_rect = from_void_ptr<Rect(*)(Rect, Rect)>(syms["_NSUnionRect"]);
    auto point_in_rect = from_void_ptr<bool(*)(Point, Rect)>(syms["_NSPointInRect"]);
    auto intersect_range = from_void_ptr<Range(*)(Range, Range)>(syms["_NSIntersectionRange"]);
    auto string_from_range = from_void_ptr<ObjcId(*)(Range)>(syms["_NSStringFromRange"]);

    Rect a{{0, 0}, {10, 10}};
    Rect b{{5, 5}, {10, 10}};
    CHECK(equal_rects(a, {{0, 0}, {10, 10}}));
    CHECK(!equal_rects(a, b));
    Rect inset = inset_rect(a, 2, 3);
    CHECK(inset.origin.x == 2 && inset.origin.y == 3);
    CHECK(inset.size.width == 6 && inset.size.height == 4);
    Rect uni = union_rect(a, b);
    CHECK(uni.origin.x == 0 && uni.origin.y == 0);
    CHECK(uni.size.width == 15 && uni.size.height == 15);
    CHECK(point_in_rect({5, 5}, a));
    CHECK(!point_in_rect({10, 10}, a));
    Range ri = intersect_range({0, 10}, {5, 10});
    CHECK(ri.location == 5 && ri.length == 5);
    CHECK(intersect_range({0, 2}, {5, 2}).length == 0);
    ObjcId range_str = string_from_range({3, 7});
    CHECK(nsstring_to_utf8(range_str) == "{3, 7}");
    finn_objc_release(range_str);

    // Exported constants are real NSStrings / numbers, not placeholders.
    ObjcId* attr = reinterpret_cast<ObjcId*>(syms["_NSParagraphStyleAttributeName"]);
    CHECK(attr && *attr && nsstring_to_utf8(*attr) == "NSParagraphStyle");
    ObjcId* notif = reinterpret_cast<ObjcId*>(syms["_NSWindowDidResizeNotification"]);
    CHECK(notif && *notif && nsstring_to_utf8(*notif) == "NSWindowDidResizeNotification");
    double* weight = reinterpret_cast<double*>(syms["_NSFontWeightBold"]);
    CHECK(weight && *weight < 0);
    double* no_metric = reinterpret_cast<double*>(syms["_NSViewNoIntrinsicMetric"]);
    CHECK(no_metric && *no_metric == -1.0);
    Rect* zero = reinterpret_cast<Rect*>(syms["_NSZeroRect"]);
    CHECK(zero && zero->origin.x == 0 && zero->size.width == 0);

    // Associated objects, selector-backed properties and weak references.
    ObjcClass* obj_cls = finn_objc_getClass("NSObject");
    CHECK(obj_cls != nullptr);
    ObjcId owner = finn_objc_alloc_object(obj_cls);
    ObjcId value = nsstring_from_utf8("associated");
    auto set_assoc = from_void_ptr<void(*)(ObjcId, const void*, ObjcId, int)>(
        syms["_objc_setAssociatedObject"]);
    auto get_assoc = from_void_ptr<ObjcId(*)(ObjcId, const void*)>(
        syms["_objc_getAssociatedObject"]);
    static const char kKey = 'k';
    set_assoc(owner, &kKey, value, 1);
    CHECK(get_assoc(owner, &kKey) == value);
    set_assoc(owner, &kKey, nullptr, 0);
    CHECK(get_assoc(owner, &kKey) == nullptr);

    ObjcSel prop_sel = finn_sel_registerName("testProperty");
    auto set_prop = from_void_ptr<void(*)(ObjcId, ObjcSel, ObjcId, bool, bool)>(
        syms["_objc_setProperty"]);
    auto get_prop = from_void_ptr<ObjcId(*)(ObjcId, ObjcSel, bool)>(
        syms["_objc_getProperty"]);
    set_prop(owner, prop_sel, value, false, false);
    CHECK(get_prop(owner, prop_sel, false) == value);
    set_prop(owner, prop_sel, nullptr, false, false);

    auto copy_weak = from_void_ptr<void(*)(ObjcId*, ObjcId*)>(syms["_objc_copyWeak"]);
    ObjcId weak = nullptr;
    ObjcId strong = value;
    copy_weak(&weak, &strong);
    CHECK(weak == value);
    finn_objc_release(owner);
    finn_objc_release(value);
    // The weak slot must be zeroed when the object actually dies (checked with
    // a freshly allocated object whose only owner is released).
    ObjcId short_lived = finn_objc_alloc_object(obj_cls);
    ObjcId weak2 = nullptr;
    copy_weak(&weak2, &short_lived);
    CHECK(weak2 == short_lived);
    finn_objc_release(short_lived);
    CHECK(weak2 == nullptr);

    // libdispatch semaphores really block and really wake.
    auto sem_create = from_void_ptr<void*(*)(long)>(syms["_dispatch_semaphore_create"]);
    auto sem_signal = from_void_ptr<long(*)(void*)>(syms["_dispatch_semaphore_signal"]);
    auto sem_wait = from_void_ptr<long(*)(void*, std::uint64_t)>(syms["_dispatch_semaphore_wait"]);
    void* sem = sem_create(0);
    CHECK(sem != nullptr);
    CHECK(sem_wait(sem, 1) != 0);  // immediate timeout on a zero-count semaphore
    CHECK(sem_signal(sem) == 0);
    CHECK(sem_wait(sem, 1) == 0);
    std::atomic<bool> woken{false};
    std::thread waiter([&] {
        woken.store(sem_wait(sem, ~std::uint64_t{0}) == 0, std::memory_order_relaxed);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(sem_signal(sem) == 0);
    waiter.join();
    CHECK(woken.load(std::memory_order_relaxed));
    dispatch_semaphore_release(sem);

    // QuartzCore matrix math and monotonic clock.
    struct Transform3D { double m[16]; };
    auto media_time = from_void_ptr<double(*)()>(syms["_CACurrentMediaTime"]);
    CHECK(media_time() > 0);
    auto make_scale = from_void_ptr<Transform3D(*)(double, double, double)>(
        syms["_CATransform3DMakeScale"]);
    auto make_translation = from_void_ptr<Transform3D(*)(double, double, double)>(
        syms["_CATransform3DMakeTranslation"]);
    auto concat = from_void_ptr<Transform3D(*)(Transform3D, Transform3D)>(
        syms["_CATransform3DConcat"]);
    CHECK(make_scale != nullptr && make_translation != nullptr && concat != nullptr);
    Transform3D s = make_scale(2, 3, 4);
    CHECK(s.m[0] == 2 && s.m[5] == 3 && s.m[10] == 4 && s.m[15] == 1);
    Transform3D t = make_translation(5, 6, 7);
    CHECK(t.m[12] == 5 && t.m[13] == 6 && t.m[14] == 7);
    Transform3D identity = make_scale(1, 1, 1);
    Transform3D st = concat(identity, s);
    CHECK(st.m[0] == 2 && st.m[5] == 3 && st.m[10] == 4 && st.m[15] == 1);

    // Host libc / Itanium ABI entries are genuinely bound.
    CHECK(syms.count("_strerror") == 1 && syms["_strerror"] != nullptr);
    CHECK(syms.count("_fseeko") == 1 && syms["_fseeko"] != nullptr);
    CHECK(syms.count("__Znwm") == 1 && syms["__Znwm"] != nullptr);
    CHECK(syms.count("___cxa_throw") == 1 && syms["___cxa_throw"] != nullptr);
    CHECK(syms.count("__Unwind_Resume") == 1 && syms["__Unwind_Resume"] != nullptr);
    CHECK(syms.count("_objc_begin_catch") == 1);

    // Foundation reflection helpers use the real selector/class tables.
    auto class_from_string = from_void_ptr<ObjcId(*)(ObjcId)>(syms["_NSClassFromString"]);
    auto string_from_class = from_void_ptr<ObjcId(*)(ObjcClass*)>(syms["_NSStringFromClass"]);
    auto selector_from_string = from_void_ptr<ObjcSel(*)(ObjcId)>(syms["_NSSelectorFromString"]);
    auto string_from_selector = from_void_ptr<ObjcId(*)(ObjcSel)>(syms["_NSStringFromSelector"]);
    CHECK(class_from_string(nsstring_from_utf8("NSObject")) != nullptr);
    CHECK(class_from_string(nsstring_from_utf8("NoSuchClass")) == nullptr);
    CHECK(nsstring_to_utf8(string_from_class(finn_objc_getClass("NSObject"))) == "NSObject");
    ObjcSel sel = selector_from_string(nsstring_from_utf8("testProperty"));
    CHECK(sel != nullptr && nsstring_to_utf8(string_from_selector(sel)) == "testProperty");

    // NSDivideRect returns real slice/remainder rects.
    auto divide_rect = from_void_ptr<void(*)(Rect, Rect*, Rect*, double, int)>(
        syms["_NSDivideRect"]);
    Rect slice{}, remainder{};
    divide_rect({{0, 0}, {10, 10}}, &slice, &remainder, 3, 0);
    CHECK(slice.origin.x == 0 && slice.size.width == 3);
    CHECK(remainder.origin.x == 3 && remainder.size.width == 7);
    auto fpclass = from_void_ptr<int(*)(double)>(syms["__fpclassifyd"]);
    CHECK(fpclass(0.0) == FP_ZERO);
    CHECK(fpclass(1.0) == FP_NORMAL);
    return true;
}

bool test_nsarray_fast_enumeration() {
    using namespace finn::darwin;
    init_foundation();
    void* pool = finn_objc_autoreleasePoolPush();
    ObjcId arr = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSArray")), "arrayWithObjects:count:",
        std::array<ObjcId, 3>{nsstring_from_utf8("a"), nsstring_from_utf8("b"),
                              nsstring_from_utf8("c")}.data(),
        std::size_t{3});
    CHECK(arr != nullptr);
    struct FastState {
        unsigned long state;
        ObjcId* items;
        unsigned long* mutations;
        unsigned long extra[5];
    };
    FastState state{};
    ObjcId buffer[2] = {};
    const std::size_t first = objc_send_name<std::size_t>(
        arr, "countByEnumeratingWithState:objects:count:", &state, buffer, std::size_t{2});
    CHECK(first == 2);
    CHECK(state.items == buffer && state.state == 2 && state.mutations == &state.extra[4]);
    CHECK(nsstring_to_utf8(buffer[0]) == "a" && nsstring_to_utf8(buffer[1]) == "b");
    const std::size_t second = objc_send_name<std::size_t>(
        arr, "countByEnumeratingWithState:objects:count:", &state, buffer, std::size_t{2});
    CHECK(second == 1 && nsstring_to_utf8(buffer[0]) == "c");
    const std::size_t done = objc_send_name<std::size_t>(
        arr, "countByEnumeratingWithState:objects:count:", &state, buffer, std::size_t{2});
    CHECK(done == 0);
    finn_objc_autoreleasePoolPop(pool);
    return true;
}

bool test_carbon_compat() {
    using namespace finn::darwin;
    std::unordered_map<std::string, void*> syms;
    register_carbon_symbols(&syms);
    static const char* kNames[] = {
        "_RegisterEventHotKey", "_UnregisterEventHotKey",
        "_GetEventDispatcherTarget", "_GetEventClass", "_GetEventParameter",
        "_InstallEventHandler", "_RemoveEventHandler", "_CopySymbolicHotKeys",
        "_LMGetKbdType", "_TISCopyCurrentASCIICapableKeyboardLayoutInputSource",
        "_TISGetInputSourceProperty", "_UCKeyTranslate",
        "___availability_version_check", "__availability_version_check",
        "_NSAccessibilityPostNotificationWithUserInfo", "_CGRectDivide",
        "_CC_SHA1_Init", "_CC_SHA1_Update", "_CC_SHA1_Final",
        "_CFPreferencesCopyAppValue", "_CFUUIDCreate", "_xar_open",
        "_SecCodeCopySigningInformation",
    };
    for (const char* n : kNames) {
        CHECK(syms.count(n) == 1 && syms[n] != nullptr);
    }

    auto register_hotkey = from_void_ptr<std::uint32_t (*)(std::uint32_t, std::uint32_t,
                                                          std::uint64_t, void*, std::uint32_t,
                                                          void**)>(syms["_RegisterEventHotKey"]);
    auto unregister_hotkey = from_void_ptr<std::uint32_t (*)(void*)>(
        syms["_UnregisterEventHotKey"]);
    void* ref = nullptr;
    const std::size_t before = carbon_registered_hotkey_count();
    CHECK(register_hotkey(0, 0, 42, nullptr, 0, &ref) == 0);
    CHECK(ref != nullptr && carbon_registered_hotkey_count() == before + 1);
    CHECK(unregister_hotkey(ref) == 0);
    CHECK(carbon_registered_hotkey_count() == before);

    auto translate = from_void_ptr<std::uint32_t (*)(const void*, std::uint16_t, std::uint16_t,
                                                     std::uint32_t, std::uint32_t,
                                                     std::uint32_t, std::uint32_t*,
                                                     std::size_t, std::size_t*,
                                                     std::uint16_t*)>(syms["_UCKeyTranslate"]);
    std::uint16_t out[4] = {};
    std::size_t len = 0;
    CHECK(translate(nullptr, 0, 0, 0, 0, 0, nullptr, 4, &len, out) == 0);
    CHECK(len == 1 && out[0] == 'a');
    CHECK(translate(nullptr, 0, 0, 1u << 9, 0, 0, nullptr, 4, &len, out) == 0);
    CHECK(len == 1 && out[0] == 'A');

    auto keyboard_type = from_void_ptr<std::uint32_t (*)()>(syms["_LMGetKbdType"]);
    CHECK(keyboard_type() == 44);

    // SHA-1 must match the NIST FIPS 180-4 vector for "abc".
    struct Sha1Ctx { std::uint32_t h[5]; std::uint8_t block[64]; std::size_t used; std::uint64_t bits; };
    auto sha1_init = from_void_ptr<int (*)(Sha1Ctx*)>(syms["_CC_SHA1_Init"]);
    auto sha1_update = from_void_ptr<int (*)(Sha1Ctx*, const void*, std::size_t)>(
        syms["_CC_SHA1_Update"]);
    auto sha1_final = from_void_ptr<unsigned char* (*)(Sha1Ctx*, unsigned char*)>(
        syms["_CC_SHA1_Final"]);
    Sha1Ctx ctx{};
    unsigned char digest[20] = {};
    CHECK(sha1_init(&ctx) == 1);
    CHECK(sha1_update(&ctx, "abc", 3) == 1);
    CHECK(sha1_final(&ctx, digest) == digest);
    static constexpr unsigned char kAbcSha1[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    CHECK(std::memcmp(digest, kAbcSha1, 20) == 0);
    return true;
}

bool test_swiftui_symbols() {
    using namespace finn::darwin::swiftui;
    SymbolTable table;
    register_all_swiftui_symbols(&table);

    // Every symbol the SwiftHello Mach-O imports must now have a real binding.
    static const char* kRequired[] = {
        "_$sytWV", "_$s7SwiftUI4ViewMp", "_$s7SwiftUI4ViewP4BodyAC_AaBTn",
        "_$s4Body7SwiftUI4ViewPTl",
        "_$s7SwiftUI4ViewP05_makeC04view6inputsAA01_C7OutputsVAA11_GraphValueVyxG_AA01_C6InputsVtFZTq",
        "_$s7SwiftUI4ViewP05_makeC4List4view6inputsAA01_cE7OutputsVAA11_GraphValueVyxG_AA01_cE6InputsVtFZTq",
        "_$s7SwiftUI4ViewP14_viewListCount6inputsSiSgAA01_ceF6InputsV_tFZTq",
        "_$s7SwiftUI4ViewP4body4BodyQzvgTq", "_$s7SwiftUI3AppMp",
        "_$s7SwiftUI3AppP4BodyAC_AA5SceneTn", "_$s4Body7SwiftUI3AppPTl",
        "_$s7SwiftUI3AppP4body4BodyQzvgTq", "_$s7SwiftUI3AppPxycfCTq",
        "_$s7SwiftUI5SceneMp", "___chkstk_darwin",
        "_$s7SwiftUI11WindowGroupV7contentACyxGxyXE_tcfC",
        "_$s7SwiftUI11WindowGroupVMn", "_$s7SwiftUI11WindowGroupVyxGAA5SceneAAMc",
        "_$s7SwiftUI12SceneBuilderV10buildBlockyxxAA0C0RzlFZ",
        "_$s7SwiftUI13_VStackLayoutVMn", "_$s7SwiftUI13_VariadicViewO4TreeVMn",
        "_$s7SwiftUI18LocalizedStringKeyV13stringLiteralACSS_tcfC",
        "_$s7SwiftUI19HorizontalAlignmentV6centerACvgZ",
        "_$s7SwiftUI3AppPAAE4mainyyFZ", "_$s7SwiftUI4TextVMn",
        "_$s7SwiftUI4TextV_9tableName6bundle7commentAcA18LocalizedStringKeyV_SSSgSo8NSBundleCSgs06StaticI0VSgtcfC",
        "_$s7SwiftUI4ViewPAAE05_makeC04view6inputsAA01_C7OutputsVAA11_GraphValueVyxG_AA01_C6InputsVtFZ",
        "_$s7SwiftUI4ViewPAAE05_makeC4List4view6inputsAA01_cE7OutputsVAA11_GraphValueVyxG_AA01_cE6InputsVtFZ",
        "_$s7SwiftUI4ViewPAAE14_viewListCount6inputsSiSgAA01_ceF6InputsV_tFZ",
        "_$s7SwiftUI6ButtonVA2A4TextVRszrlE_6actionACyAEGAA18LocalizedStringKeyV_yyctcfC",
        "_$s7SwiftUI6ButtonVMn", "_$s7SwiftUI6VStackVMn",
        "_$s7SwiftUI6VStackVyxGAA4ViewAAMc", "_$s7SwiftUI9TupleViewVMn",
        "_$sSSN", "_$ss23_ContiguousArrayStorageCMn",
        "_$ss26_stdlib_isOSVersionAtLeastyBi1_Bw_BwBwtF",
        "_$ss5print_9separator10terminatoryypd_S2StF",
        "_swift_getOpaqueTypeConformance", "_swift_getTypeByMangledNameInContext",
        "_swift_getTypeByMangledNameInContextInMetadataState",
        "_swift_getWitnessTable",
    };
    for (const char* sym : kRequired) {
        CHECK(table.count(sym) == 1);
        CHECK(table[sym] != nullptr);
    }
    CHECK(swiftui_available());

    // The demangler resolves real type names and rejects unknown grammar.
    CHECK(demangle_swift_type("$s7SwiftUI4TextV") == "SwiftUI.Text");
    CHECK(demangle_swift_type("$sSSN").find("String") != std::string::npos);
    CHECK(demangle_swift_type("$s??bad").empty());

    // Mangled-name lookup returns real metadata with the ABI field offsets.
    const void* meta = resolve_type_metadata("$s7SwiftUI4TextV", 16);
    CHECK(meta != nullptr);
    const auto* sm = static_cast<const SwiftStructMetadata*>(meta);
    CHECK(sm->kind == static_cast<std::uintptr_t>(MetadataKind::Struct));
    CHECK(offsetof(SwiftStructMetadata, fieldOffset0) == 0x2c);
    // Compressed symbolic-reference manglings we cannot decode resolve to a
    // documented, ABI-shaped opaque fallback (never a claimed type match).
    const void* fallback = resolve_type_metadata("$s7SwiftUIQzUnknown", 19);
    CHECK(fallback != nullptr);
    CHECK(resolve_type_metadata(nullptr, 0) == nullptr);

    // State/Binding and Combine primitives have real behaviour.
    auto slot = std::make_shared<StateSlot>("start");
    int notifications = 0;
    std::string last;
    const auto token = slot->subscribe([&](const std::string& v) {
        ++notifications;
        last = v;
    });
    Binding binding(slot);
    CHECK(binding.valid() && binding.get() == "start");
    binding.set("changed");
    CHECK(notifications == 1 && last == "changed" && slot->value() == "changed");
    slot->unsubscribe(token);
    binding.set("again");
    CHECK(notifications == 1);
    CurrentValueSubject subject("one");
    std::string delivered;
    subject.sink([&](const std::string& v) { delivered = v; });
    CHECK(delivered == "one");
    subject.send("two");
    CHECK(delivered == "two" && subject.value() == "two");
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_plist_xml_and_binary();
    ok &= test_icns_extraction();
    ok &= test_native_pkg_dmg_and_zip_installers();
    ok &= test_live_http_download_install_and_exec();
    ok &= test_libsystem_expanded_symbols();
    ok &= test_objc_foundation_cg_swift_and_appkit();
    ok &= test_ns_attributed_string();
    ok &= test_ns_date_formatter_and_calendar();
    ok &= test_ns_search_paths_and_notifications();
    ok &= test_installer_symlink_and_apple_toc_regressions();
    ok &= test_framework_coverage_honesty();
    ok &= test_nsarray_fast_enumeration();
    ok &= test_carbon_compat();
    ok &= test_swiftui_symbols();
    ok &= test_compat_symbols();
    ok &= test_live_swift_honest_diagnosis();
#if defined(__APPLE__) && defined(__aarch64__)
    ok &= test_live_macho_comprehensive();
#endif
    if (!ok) return 1;
    std::puts("test_darwin: ok");
    return 0;
}
