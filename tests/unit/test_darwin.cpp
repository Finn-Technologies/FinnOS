#include "darwin/appkit_bridge.hpp"
#include "darwin/foundation.hpp"
#include "darwin/icns.hpp"
#include "darwin/installer.hpp"
#include "darwin/macho_loader.hpp"
#include "darwin/objc_runtime.hpp"
#include "darwin/plist.hpp"

#include <unistd.h>
#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

class TempDir {
public:
    TempDir() {
        root_ = fs::temp_directory_path() /
                ("finn-darwin-test-" + std::to_string(::getpid()));
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

    // 32x32 chunk (icp5)
    icns.insert(icns.end(), {'i', 'c', 'p', '5'});
    append_be32(icns, static_cast<std::uint32_t>(8 + png_small.size()));
    icns.insert(icns.end(), png_small.begin(), png_small.end());

    // 256x256 chunk (ic08) — higher priority
    icns.insert(icns.end(), {'i', 'c', '0', '8'});
    append_be32(icns, static_cast<std::uint32_t>(8 + png_big.size()));
    icns.insert(icns.end(), png_big.begin(), png_big.end());
    return icns;
}

bool test_plist_xml_and_binary() {
    using namespace finn::darwin;

    // 1. XML plist
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

    // 2. Binary plist (bplist00): dict { "CFBundleName": "BinaryApp" }
    std::vector<std::uint8_t> bp = {'b', 'p', 'l', 'i', 's', 't', '0', '0'};
    const std::uint8_t off0 = static_cast<std::uint8_t>(bp.size());  // dict (1 entry)
    bp.push_back(0xd1);
    bp.push_back(1);  // key ref 1
    bp.push_back(2);  // val ref 2
    const std::uint8_t off1 = static_cast<std::uint8_t>(bp.size());  // "CFBundleName" (12)
    bp.push_back(0x5c);
    const char* k = "CFBundleName";
    bp.insert(bp.end(), k, k + 12);
    const std::uint8_t off2 = static_cast<std::uint8_t>(bp.size());  // "BinaryApp" (9)
    bp.push_back(0x59);
    const char* v = "BinaryApp";
    bp.insert(bp.end(), v, v + 9);

    const std::uint64_t off_table = bp.size();
    bp.push_back(off0);
    bp.push_back(off1);
    bp.push_back(off2);

    // 32-byte trailer
    for (int i = 0; i < 6; ++i) bp.push_back(0);
    bp.push_back(1);  // offset_size = 1
    bp.push_back(1);  // ref_size = 1
    append_be64(bp, 3);          // num_objects
    append_be64(bp, 0);          // top_object
    append_be64(bp, off_table);  // offset_table_off

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
    CHECK(png.size() == 14);  // Chose ic08 (256x256) over icp5 (32x32)
    CHECK(png[12] == 0x99 && png[13] == 0xaa);
    return true;
}

bool test_native_pkg_and_dmg_installers() {
    using namespace finn::darwin;
    const TempDir tmp;

    // 1. Build a real XAR .pkg with gzip+cpio Payload containing DemoMac.app + AppIcon.icns
    const std::string info_plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\"><dict>"
        "<key>CFBundleName</key><string>DemoMac</string>"
        "<key>CFBundleIdentifier</key><string>org.finnos.DemoMac</string>"
        "<key>CFBundleShortVersionString</key><string>1.2.0</string>"
        "<key>CFBundleExecutable</key><string>DemoMac</string>"
        "<key>CFBundleIconFile</key><string>AppIcon</string>"
        "</dict></plist>\n";
    const auto icns_bytes = make_test_icns();
    const std::vector<std::uint8_t> fake_bin = {0xcf, 0xfa, 0xed, 0xfe, 0x0c, 0, 0, 1};

    std::vector<std::uint8_t> cpio;
    append_cpio_odc_entry(cpio, "DemoMac.app", 0040755, {});
    append_cpio_odc_entry(cpio, "DemoMac.app/Contents", 0040755, {});
    append_cpio_odc_entry(cpio, "DemoMac.app/Contents/Info.plist", 0100644,
                          std::vector<std::uint8_t>(info_plist.begin(), info_plist.end()));
    append_cpio_odc_entry(cpio, "DemoMac.app/Contents/Resources/AppIcon.icns", 0100644,
                          icns_bytes);
    append_cpio_odc_entry(cpio, "DemoMac.app/Contents/MacOS/DemoMac", 0100755, fake_bin);
    append_cpio_odc_entry(cpio, "TRAILER!!!", 0, {});

    const auto payload_gz = zlib_compress(cpio.data(), cpio.size(), 16 + MAX_WBITS);
    CHECK(!payload_gz.empty());

    const std::string toc_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><xar><toc>"
        "<file id=\"1\"><name>Payload</name><data>"
        "<offset>0</offset>"
        "<length>" + std::to_string(payload_gz.size()) + "</length>"
        "<size>" + std::to_string(cpio.size()) + "</size>"
        "<encoding style=\"application/x-gzip\"/>"
        "</data></file></toc></xar>";
    const auto toc_z = zlib_compress(
        reinterpret_cast<const std::uint8_t*>(toc_xml.data()), toc_xml.size(), MAX_WBITS);
    CHECK(!toc_z.empty());

    std::vector<std::uint8_t> pkg = {'x', 'a', 'r', '!'};
    append_be16(pkg, 28);  // header size
    append_be16(pkg, 1);   // version
    append_be64(pkg, toc_z.size());
    append_be64(pkg, toc_xml.size());
    append_be32(pkg, 1);   // checksum alg
    pkg.insert(pkg.end(), toc_z.begin(), toc_z.end());
    pkg.insert(pkg.end(), payload_gz.begin(), payload_gz.end());

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

    // 2. Build a real UDIF .dmg with embedded XML blkx mish table
    std::vector<std::uint8_t> mish(204 + 40, 0);
    std::memcpy(mish.data(), "mish", 4);
    mish[23] = 64;   // 64 sectors
    mish[203] = 1;   // 1 run
    // Run 0 type = 0x80000005 (UDZO zlib)
    mish[204] = 0x80;
    mish[207] = 0x05;
    mish[204 + 39] = 128;  // compressed_bytes = 128

    // Base64 encode mish
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
    // xml_off = 0 at offset 216, xml_len at offset 224, sector_count = 64 at offset 492
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

bool test_objc_runtime_and_foundation() {
    using namespace finn::darwin;
    init_foundation();
    char arg0[] = "test_darwin";
    char* argv[] = {arg0, nullptr};
    init_appkit(1, argv);

    // nil receiver must safely return 0 through objc_msgsend.S
    CHECK(objc_send_name<std::intptr_t>(nullptr, "length") == 0);

    // NSString + NSMutableString via assembly trampoline
    ObjcId s = nsstring_from_utf8("FinnOS");
    CHECK(objc_send_name<std::size_t>(s, "length") == 6);
    ObjcId s2 = objc_send_name<ObjcId>(s, "stringByAppendingString:", nsstring_from_utf8(" Darwin"));
    CHECK(nsstring_to_utf8(s2) == "FinnOS Darwin");

    // NSArray + NSDictionary + NSNumber
    ObjcClass* arr_cls = finn_objc_getClass("NSMutableArray");
    ObjcId arr = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(arr_cls), "array");
    objc_send_name<void>(arr, "addObject:", nsstring_from_utf8("one"));
    objc_send_name<void>(arr, "addObject:", nsstring_from_utf8("two"));
    CHECK(objc_send_name<std::size_t>(arr, "count") == 2);
    ObjcId joined = objc_send_name<ObjcId>(arr, "componentsJoinedByString:", nsstring_from_utf8(","));
    CHECK(nsstring_to_utf8(joined) == "one,two");

    // AppKit NSWindow + NSTextField
    ObjcClass* lbl_cls = finn_objc_getClass("NSTextField");
    ObjcId lbl = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(lbl_cls), "labelWithString:", nsstring_from_utf8("Hi AppKit"));
    ObjcId val = objc_send_name<ObjcId>(lbl, "stringValue");
    CHECK(nsstring_to_utf8(val) == "Hi AppKit");
    return true;
}

#if defined(__APPLE__) && defined(__aarch64__)
bool test_live_macho_execution() {
    using namespace finn::darwin;
    const TempDir tmp;
    const fs::path src_m = tmp.path("live_objc.m");
    const fs::path bin_m = tmp.path("live_objc");
    {
        std::ofstream out(src_m);
        out << "#import <Foundation/Foundation.h>\n"
            << "@interface Adder : NSObject\n"
            << "- (NSInteger)add:(NSInteger)a to:(NSInteger)b;\n"
            << "@end\n"
            << "@implementation Adder\n"
            << "- (NSInteger)add:(NSInteger)a to:(NSInteger)b { return a + b; }\n"
            << "@end\n"
            << "int main(void) {\n"
            << "  @autoreleasepool {\n"
            << "    Adder *x = [[Adder alloc] init];\n"
            << "    NSString *s = [NSString stringWithFormat:@\"sum=%ld\", (long)[x add:19 to:23]];\n"
            << "    return [s isEqualToString:@\"sum=42\"] ? 0 : 9;\n"
            << "  }\n"
            << "}\n";
    }
    const std::string cmd =
        "clang -O2 -fobjc-arc -framework Foundation \"" + src_m.string() +
        "\" -o \"" + bin_m.string() + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return true;  // Skip if clang unavailable

    MachoLoader loader;
    const auto diag = loader.diagnose(bin_m.string());
    CHECK(diag.valid);
    CHECK(diag.arch_compatible);
    CHECK(diag.runnable);
    CHECK(diag.objc_class_count == 1);

    char arg0[] = "live_objc";
    char* argv[] = {arg0, nullptr};
    std::string err;
    const int rc = loader.load_and_run(bin_m.string(), 1, argv, &err);
    CHECK(rc == 0);
    CHECK(err.empty());
    return true;
}
#endif

}  // namespace

int main() {
    bool ok = true;
    ok &= test_plist_xml_and_binary();
    ok &= test_icns_extraction();
    ok &= test_native_pkg_and_dmg_installers();
    ok &= test_objc_runtime_and_foundation();
#if defined(__APPLE__) && defined(__aarch64__)
    ok &= test_live_macho_execution();
#endif
    if (!ok) return 1;
    std::puts("test_darwin: ok");
    return 0;
}
