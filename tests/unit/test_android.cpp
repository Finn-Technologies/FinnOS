// Host unit tests for the clean-room finn-android runtime. No display, no
// Waydroid, no Android SDK: every fixture below is a real binary built byte by
// byte, and every assert checks a real parser/VM/installer result.

#include "android/android_format.hpp"
#include "android/android_installer.hpp"
#include "android/art_bridge.hpp"
#include "android/dex_runtime.hpp"
#include "android/ui_bridge.hpp"
#include "android/zip_reader.hpp"
#include "compat/android_runner.hpp"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace android = finn::android;

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
    explicit TempDir(const std::string& tag) {
        root_ = fs::temp_directory_path() /
                ("finn-android-test-" + tag + "-" + std::to_string(::getpid()));
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

void u16(std::vector<std::uint8_t>& o, std::uint16_t v) {
    o.push_back(static_cast<std::uint8_t>(v));
    o.push_back(static_cast<std::uint8_t>(v >> 8));
}
void u32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
}
void uleb(std::vector<std::uint8_t>& o, std::uint32_t v) {
    do {
        std::uint8_t b = static_cast<std::uint8_t>(v & 0x7fu);
        v >>= 7;
        if (v) b |= 0x80u;
        o.push_back(b);
    } while (v != 0);
}

std::vector<std::uint8_t> deflate_bytes(const std::vector<std::uint8_t>& in) {
    z_stream s{};
    if (::deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                       Z_DEFAULT_STRATEGY) != Z_OK) return {};
    std::vector<std::uint8_t> out(in.size() + 256);
    s.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.data()));
    s.avail_in = static_cast<uInt>(in.size());
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    const int rc = ::deflate(&s, Z_FINISH);
    ::deflateEnd(&s);
    if (rc != Z_STREAM_END) return {};
    out.resize(s.total_out);
    return out;
}

// Minimal ZIP builder with Stored and Deflate entries.
class ZipBuilder {
public:
    void add(const std::string& name, const std::vector<std::uint8_t>& data,
             bool compress = true) {
        items_.push_back({name, data, compress});
    }
    void add_text(const std::string& name, const std::string& text, bool compress = true) {
        add(name, std::vector<std::uint8_t>(text.begin(), text.end()), compress);
    }
    std::vector<std::uint8_t> build() const {
        struct Cd {
            std::string name;
            std::uint16_t method;
            std::uint32_t crc;
            std::uint32_t csize;
            std::uint32_t usize;
            std::uint32_t off;
        };
        std::vector<std::uint8_t> out;
        std::vector<Cd> cds;
        for (const auto& it : items_) {
            const std::uint32_t crc = static_cast<std::uint32_t>(
                ::crc32(0L, it.data.empty() ? Z_NULL : it.data.data(),
                        static_cast<uInt>(it.data.size())));
            std::vector<std::uint8_t> payload = it.data;
            std::uint16_t method = android::kZipMethodStore;
            if (it.compress && !it.data.empty()) {
                auto d = deflate_bytes(it.data);
                if (!d.empty()) {
                    payload = std::move(d);
                    method = android::kZipMethodDeflate;
                }
            }
            const std::uint32_t off = static_cast<std::uint32_t>(out.size());
            u32(out, android::kZipLocalFileSig);
            u16(out, 20); u16(out, 0); u16(out, method);
            u16(out, 0); u16(out, 0);
            u32(out, crc);
            u32(out, static_cast<std::uint32_t>(payload.size()));
            u32(out, static_cast<std::uint32_t>(it.data.size()));
            u16(out, static_cast<std::uint16_t>(it.name.size()));
            u16(out, 0);
            out.insert(out.end(), it.name.begin(), it.name.end());
            out.insert(out.end(), payload.begin(), payload.end());
            cds.push_back({it.name, method, crc,
                           static_cast<std::uint32_t>(payload.size()),
                           static_cast<std::uint32_t>(it.data.size()), off});
        }
        const std::uint32_t cd_off = static_cast<std::uint32_t>(out.size());
        for (const auto& c : cds) {
            u32(out, android::kZipCentralDirSig);
            u16(out, 20); u16(out, 20); u16(out, 0); u16(out, c.method);
            u16(out, 0); u16(out, 0);
            u32(out, c.crc); u32(out, c.csize); u32(out, c.usize);
            u16(out, static_cast<std::uint16_t>(c.name.size()));
            u16(out, 0); u16(out, 0); u16(out, 0); u16(out, 0);
            u32(out, 0); u32(out, c.off);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const std::uint32_t cd_size = static_cast<std::uint32_t>(out.size()) - cd_off;
        u32(out, android::kZipEndCentralDirSig);
        u16(out, 0); u16(out, 0);
        u16(out, static_cast<std::uint16_t>(cds.size()));
        u16(out, static_cast<std::uint16_t>(cds.size()));
        u32(out, cd_size); u32(out, cd_off); u16(out, 0);
        return out;
    }
    bool write(const fs::path& p) const {
        const auto b = build();
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(b.data()),
                  static_cast<std::streamsize>(b.size()));
        return static_cast<bool>(out);
    }

private:
    struct Item {
        std::string name;
        std::vector<std::uint8_t> data;
        bool compress;
    };
    std::vector<Item> items_;
};

// Binary AXML builder. String indices are real; the attribute table uses the
// real ResXMLTree_attribute layout (dataType at byte 15, data at byte 16).
class AxmlBuilder {
public:
    std::uint32_t string(const std::string& s) {
        auto it = idx_.find(s);
        if (it != idx_.end()) return it->second;
        const std::uint32_t idx = static_cast<std::uint32_t>(strings_.size());
        strings_.push_back(s);
        idx_[s] = idx;
        return idx;
    }

    struct Attr {
        std::uint32_t name;
        std::string value;             // raw string value when non-empty
        std::uint8_t type = android::kResNull;
        std::uint32_t data = 0;
    };

    void start(const std::string& name, std::vector<Attr> attrs) {
        string(name);
        // Register every raw string value before finish() builds the pool;
        // otherwise late string() calls would produce indices outside it.
        for (const auto& a : attrs) {
            if (!a.value.empty()) string(a.value);
        }
        elements_.push_back({true, string(name), std::move(attrs)});
    }
    void end(const std::string& name) {
        elements_.push_back({false, string(name), {}});
    }

    std::vector<std::uint8_t> finish() {
        std::vector<std::uint8_t> pool;
        u16(pool, android::kAxmlStringPool);  // type
        u16(pool, 28);                        // headerSize
        u32(pool, 0);                         // chunk size, patched below
        std::vector<std::uint8_t> strings_blob;
        std::vector<std::uint32_t> offsets;
        for (const auto& s : strings_) {
            offsets.push_back(static_cast<std::uint32_t>(strings_blob.size()));
            uleb(strings_blob, static_cast<std::uint32_t>(s.size()));
            uleb(strings_blob, static_cast<std::uint32_t>(s.size()));
            strings_blob.insert(strings_blob.end(), s.begin(), s.end());
            strings_blob.push_back(0);
        }
        u32(pool, static_cast<std::uint32_t>(strings_.size()));  // stringCount
        u32(pool, 0);                                            // styleCount
        u32(pool, android::kAxmlUtf8Flag);                       // flags
        u32(pool, 28u + 4u * static_cast<std::uint32_t>(strings_.size()));
        u32(pool, 0);
        for (std::uint32_t off : offsets) u32(pool, off);
        pool.insert(pool.end(), strings_blob.begin(), strings_blob.end());
        const std::uint32_t pool_size = static_cast<std::uint32_t>(pool.size());
        for (int i = 0; i < 4; ++i)
            pool[4 + i] = static_cast<std::uint8_t>(pool_size >> (i * 8));

        std::vector<std::uint8_t> out;
        u16(out, android::kAxmlType); u16(out, 8); u32(out, 0);
        out.insert(out.end(), pool.begin(), pool.end());
        for (const auto& el : elements_) {
            const bool is_start = el.start;
            const std::uint32_t size = is_start
                                           ? 36u + static_cast<std::uint32_t>(el.attrs.size()) * 20u
                                           : 24u;
            u16(out, is_start ? android::kAxmlStartElement : android::kAxmlEndElement);
            u16(out, 16); u32(out, size); u32(out, 1); u32(out, 0);  // line/comment
            u32(out, 0);                       // ns
            u32(out, el.name);                 // name
            if (is_start) {
                u16(out, 20);                  // attributeStart
                u16(out, 20);                  // attributeSize
                u16(out, static_cast<std::uint16_t>(el.attrs.size()));
                u16(out, 0); u16(out, 0); u16(out, 0);
                for (const auto& a : el.attrs) {
                    u32(out, 0);               // ns
                    u32(out, a.name);          // name
                    u32(out, a.value.empty() ? android::kAxmlNoEntry : string(a.value));
                    u16(out, 8);               // Res_value size
                    out.push_back(0);          // res0
                    out.push_back(a.type);     // dataType  <-- byte 15
                    u32(out, a.data);          // data      <-- bytes 16..19
                }
            }
        }
        const std::uint32_t total = static_cast<std::uint32_t>(out.size());
        for (int i = 0; i < 4; ++i)
            out[4 + i] = static_cast<std::uint8_t>(total >> (i * 8));
        return out;
    }

private:
    struct Element {
        bool start;
        std::uint32_t name;
        std::vector<Attr> attrs;
    };
    std::vector<std::string> strings_;
    std::map<std::string, std::uint32_t> idx_;
    std::vector<Element> elements_;
};

std::vector<std::uint8_t> make_manifest_axml() {
    AxmlBuilder b;
    const std::uint32_t kPackage = b.string("package");
    (void)kPackage;
    b.start("manifest", {
        {b.string("package"), "com.example.demo", android::kResNull, 0},
        {b.string("versionCode"), "", android::kResIntDec, 7},
        {b.string("versionName"), "1.2.3", android::kResNull, 0},
    });
    b.start("uses-sdk", {
        {b.string("minSdkVersion"), "", android::kResIntDec, 21},
        {b.string("targetSdkVersion"), "", android::kResIntDec, 37},
    });
    b.end("uses-sdk");
    b.start("application", {
        {b.string("label"), "Demo App", android::kResNull, 0},
        {b.string("name"), "com.example.demo.DemoApp", android::kResNull, 0},
    });
    b.start("activity", {
        {b.string("name"), ".MainActivity", android::kResNull, 0},
        {b.string("exported"), "", android::kResIntBoolean, 1},
    });
    b.start("intent-filter", {});
    b.start("action", {{b.string("name"), "android.intent.action.MAIN",
                        android::kResNull, 0}});
    b.end("action");
    b.start("category", {{b.string("name"), "android.intent.category.LAUNCHER",
                          android::kResNull, 0}});
    b.end("category");
    b.end("intent-filter");
    b.end("activity");
    b.end("application");
    b.end("manifest");
    return b.finish();
}

std::vector<std::uint8_t> make_layout_axml() {
    AxmlBuilder b;
    b.start("LinearLayout", {{b.string("orientation"), "vertical", android::kResNull, 0}});
    b.start("TextView", {
        {b.string("id"), "@+id/demo_text", android::kResNull, 0},
        {b.string("text"), "Hello FinnOS", android::kResNull, 0},
    });
    b.end("TextView");
    b.start("Button", {
        {b.string("id"), "@+id/tap_button", android::kResNull, 0},
        {b.string("text"), "Tap me", android::kResNull, 0},
        {b.string("onClick"), "onTap", android::kResNull, 0},
    });
    b.end("Button");
    b.end("LinearLayout");
    return b.finish();
}

// SHA-1 for the DEX signature field (independent copy from the runtime's one).
void sha1(const std::uint8_t* data, std::size_t len, std::uint8_t out[20]) {
    const std::size_t padded = ((len + 9u + 63u) / 64u) * 64u;
    std::vector<std::uint8_t> m(padded, 0);
    if (len) std::memcpy(m.data(), data, len);
    m[len] = 0x80u;
    const std::uint64_t bits = static_cast<std::uint64_t>(len) * 8u;
    for (int i = 0; i < 8; ++i)
        m[padded - 1u - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bits >> (i * 8));
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu,
                          0x10325476u, 0xC3D2E1F0u};
    std::uint32_t w[80] = {};
    auto rot = [](std::uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); };
    for (std::size_t off = 0; off < padded; off += 64u) {
        for (int i = 0; i < 16; ++i) {
            const std::uint8_t* p = m.data() + off + static_cast<std::size_t>(i) * 4;
            w[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
                   (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rot(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999u; }
            else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = bb ^ c ^ d; k = 0xCA62C1D6u; }
            const std::uint32_t t = rot(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rot(bb, 30); bb = a; a = t;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
}

void patch32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (i * 8));
}

// A real, verifiable DEX with three methods: addOne (interpretable),
// onCreate (return-void) and unsupported (opcode outside the decoder subset).
std::vector<std::uint8_t> make_test_dex() {
    const std::vector<std::string> strings = {
        "Demo.java", "Lcom/example/Demo;", "V", "I", "addOne", "onCreate",
        "unsupported", "DemoActivity",
    };
    std::vector<std::uint8_t> b(android::kDexHeaderSize, 0);
    std::memcpy(b.data(), "dex\n035\0", 8);
    patch32(b, 36, android::kDexHeaderSize);
    patch32(b, 40, android::kDexEndianLittle);

    std::vector<std::uint8_t> body;
    std::vector<std::uint32_t> str_off;
    for (const auto& s : strings) {
        // DEX offsets are file-relative; strings live after the 0x70 header.
        str_off.push_back(static_cast<std::uint32_t>(
            android::kDexHeaderSize + body.size()));
        uleb(body, static_cast<std::uint32_t>(s.size()));
        body.insert(body.end(), s.begin(), s.end());
        body.push_back(0);
    }
    while (body.size() % 4u) body.push_back(0);
    const std::uint32_t string_ids_off = static_cast<std::uint32_t>(b.size() + body.size());
    for (std::uint32_t off : str_off) u32(body, off);

    // Index spaces: string indices are 0..7; type_ids are 0(LDemo;) and 1(V);
    // method_ids reference a type index and a string index for the name.
    const std::uint16_t demo_type_idx = 0;
    const std::uint16_t void_type_idx = 1;
    const std::uint32_t type_ids_off = static_cast<std::uint32_t>(b.size() + body.size());
    u32(body, 1);  // descriptor_idx for Lcom/example/Demo;
    u32(body, 2);  // descriptor_idx for V

    // proto: shorty "V" (string 2), return type V (type 1), no params
    const std::uint32_t proto_ids_off = static_cast<std::uint32_t>(b.size() + body.size());
    u32(body, 2); u32(body, void_type_idx); u32(body, 0);

    const std::uint32_t method_ids_off = static_cast<std::uint32_t>(b.size() + body.size());
    const std::uint32_t addOne_idx = 0, onCreate_idx = 1, unsupported_idx = 2;
    for (std::uint32_t name_idx : {4u, 5u, 6u}) {
        u16(body, demo_type_idx);
        u16(body, 0);  // proto
        u32(body, name_idx);
    }

    auto code_item = [](std::uint16_t regs, std::uint16_t ins,
                        const std::vector<std::uint16_t>& insns) {
        std::vector<std::uint8_t> c;
        u16(c, regs); u16(c, ins); u16(c, 0); u16(c, 0);
        u32(c, 0);
        u32(c, static_cast<std::uint32_t>(insns.size()));
        for (std::uint16_t i : insns) u16(c, i);
        return c;
    };
    // addOne(int x): v2 = x; const/4 v1, #1; add-int v0, v2, v1; return v0
    const std::vector<std::uint16_t> add_code = {
        0x1112u,  // const/4 v1, #1 (opcode in low byte of first unit)
        0x0090u,  // add-int opcode; vAA in high byte = v0 (destination)
        0x0102u,  // vBB = v2, vCC = v1  -> v0 = v2 + v1
        0x000fu,  // return v0
    };
    const std::vector<std::uint16_t> create_code = {0x000eu};  // return-void
    const std::vector<std::uint16_t> bad_code = {0x00e3u};     // unsupported

    const std::uint32_t code_add = static_cast<std::uint32_t>(b.size() + body.size());
    auto c1 = code_item(3, 1, add_code); body.insert(body.end(), c1.begin(), c1.end());
    const std::uint32_t code_create = static_cast<std::uint32_t>(b.size() + body.size());
    auto c2 = code_item(1, 0, create_code); body.insert(body.end(), c2.begin(), c2.end());
    const std::uint32_t code_bad = static_cast<std::uint32_t>(b.size() + body.size());
    auto c3 = code_item(1, 0, bad_code); body.insert(body.end(), c3.begin(), c3.end());

    while (body.size() % 4u) body.push_back(0);
    const std::uint32_t class_data_off = static_cast<std::uint32_t>(b.size() + body.size());
    uleb(body, 0);  // static fields
    uleb(body, 0);  // instance fields
    uleb(body, 3);  // direct methods
    uleb(body, 0);  // virtual methods
    uleb(body, addOne_idx);        uleb(body, 1); uleb(body, code_add);
    uleb(body, onCreate_idx - addOne_idx);  uleb(body, 1); uleb(body, code_create);
    uleb(body, unsupported_idx - onCreate_idx); uleb(body, 1); uleb(body, code_bad);

    while (body.size() % 4u) body.push_back(0);
    const std::uint32_t class_defs_off = static_cast<std::uint32_t>(b.size() + body.size());
    u32(body, 0);            // class_idx
    u32(body, 1);            // access_flags
    u32(body, 0xffffffffu);  // superclass_idx (absent)
    u32(body, 0); u32(body, 0); u32(body, 0);  // interfaces, source_file_idx, annotations
    u32(body, class_data_off); u32(body, 0);

    const std::uint32_t data_off = static_cast<std::uint32_t>(b.size() + body.size() -
                                                               (class_defs_off - (b.size() + body.size() - 0)));
    (void)data_off;
    b.insert(b.end(), body.begin(), body.end());
    const std::uint32_t file_size = static_cast<std::uint32_t>(b.size());
    patch32(b, 32, file_size);
    patch32(b, 56, static_cast<std::uint32_t>(strings.size()));
    patch32(b, 60, string_ids_off);
    patch32(b, 64, 2); patch32(b, 68, type_ids_off);
    patch32(b, 72, 1); patch32(b, 76, proto_ids_off);
    patch32(b, 88, 3); patch32(b, 92, method_ids_off);
    patch32(b, 96, 1); patch32(b, 100, class_defs_off);
    // The checksum covers bytes 12..end, including the signature field, so
    // write the signature first and only then compute Adler-32.
    std::uint8_t digest[20] = {};
    sha1(b.data() + 32, file_size - 32u, digest);
    std::memcpy(b.data() + 12, digest, 20);
    const std::uint32_t adler = static_cast<std::uint32_t>(
        ::adler32(::adler32(0L, Z_NULL, 0), b.data() + 12,
                  static_cast<uInt>(file_size - 12u)));
    patch32(b, 8, adler);
    return b;
}

std::vector<std::uint8_t> make_test_elf(std::uint16_t machine) {
    std::vector<std::uint8_t> e(64, 0);
    e[0] = 0x7f; e[1] = 'E'; e[2] = 'L'; e[3] = 'F';
    e[4] = 2; e[5] = 1; e[6] = 1; e[16] = 3;
    e[18] = static_cast<std::uint8_t>(machine);
    e[19] = static_cast<std::uint8_t>(machine >> 8);
    return e;
}

bool test_zip_reader() {
    ZipBuilder z;
    z.add_text("stored.txt", "stored payload", false);
    z.add_text("nested/deflated.txt", std::string(512, 'Z') + " end", true);
    const auto bytes = z.build();
    android::ZipReader r;
    std::string err;
    CHECK(r.open_memory(bytes.data(), bytes.size(), &err));
    CHECK(r.entries().size() == 2);
    std::string s;
    CHECK(r.read_entry_text("stored.txt", &s, &err));
    CHECK(s == "stored payload");
    CHECK(r.read_entry_text("nested/deflated.txt", &s, &err));
    CHECK(s == std::string(512, 'Z') + " end");
    CHECK(!android::ZipReader::is_safe_entry_path("../evil"));
    CHECK(!android::ZipReader::is_safe_entry_path("/etc/passwd"));
    CHECK(android::ZipReader::is_safe_entry_path("res/layout/main.xml"));
    ZipBuilder evil;
    evil.add_text("../evil.txt", "x", false);
    const auto eb = evil.build();
    CHECK(!r.open_memory(eb.data(), eb.size(), &err));
    return true;
}

bool test_axml_typed_values_and_manifest() {
    const auto bytes = make_manifest_axml();
    const auto doc = android::parse_axml_bytes(bytes);
    CHECK(doc.valid);
    CHECK(doc.strings.size() > 8);
    int saw_int = 0;
    for (const auto& el : doc.elements) {
        if (el.name != "manifest" || el.is_end) continue;
        const auto* vc = el.attr("versionCode");
        CHECK(vc != nullptr);
        // Typed integer: rawValue is absent, dataType is INT_DEC, data is 7.
        CHECK(vc->raw_value == android::kAxmlNoEntry);
        CHECK(vc->data_type == android::kResIntDec);
        CHECK(vc->data == 7);
        CHECK(vc->value == "7");
        ++saw_int;
    }
    CHECK(saw_int == 1);
    const auto m = android::parse_binary_manifest(doc);
    CHECK(m.valid);
    CHECK(m.package_name == "com.example.demo");
    CHECK(m.version_code == 7);          // the typed-value offset regression
    CHECK(m.version_name == "1.2.3");
    CHECK(m.min_sdk == 21);
    CHECK(m.target_sdk == 37);
    CHECK(m.application_label == "Demo App");
    CHECK(m.activities.size() == 1);
    CHECK(m.activities[0].launcher);     // MAIN + LAUNCHER attached to activity
    CHECK(m.activities[0].name == ".MainActivity");
    CHECK(m.activities[0].exported);
    return true;
}

bool test_dex_decode_and_interpreter() {
    const auto bytes = make_test_dex();
    const auto v = android::verify_dex_header(bytes.data(), bytes.size());
    CHECK(v.valid);
    CHECK(v.checksum_valid);
    CHECK(v.signature_valid);
    const auto dex = android::parse_dex_bytes(bytes);
    CHECK(dex.valid);
    CHECK(dex.classes.size() == 1);
    CHECK(dex.methods.size() == 3);
    CHECK(dex.code_method_count() == 3);

    android::ArtBridge art;
    std::string err;
    CHECK(art.load(dex, &err));
    const auto* add = art.dex().find_method("Lcom/example/Demo;", "addOne");
    CHECK(add != nullptr);
    const auto dis = android::disassemble_dex_code([&] {
        for (const auto& c : art.dex().classes)
            for (const auto& m : c.direct_methods)
                if (m.name == "addOne") return m.code;
        return android::DexCodeItem{};
    }());
    CHECK(dis.complete);
    CHECK(dis.instructions.size() == 3);     // const/4, add-int (2 units), return
    CHECK(dis.instructions[0].mnemonic == "const/4");
    CHECK(dis.instructions[1].width == 2);   // add-int is 23x, two code units
    CHECK(dis.instructions[2].mnemonic == "return");

    const auto result = art.execute_simple(*add, {41}, 3u, &err);
    CHECK(result.ok);
    CHECK(result.return_value == 42);

    const auto* bad = art.dex().find_method("Lcom/example/Demo;", "unsupported");
    CHECK(bad != nullptr);
    const auto bad_result = art.execute_simple(*bad, {}, 1u);
    CHECK(!bad_result.ok);
    CHECK(!bad_result.supported);
    CHECK(bad_result.error.find("outside") != std::string::npos);

    const auto audit = art.audit();
    CHECK(audit.valid);
    CHECK(audit.code_method_count == 3);
    CHECK(audit.fully_decoded_methods == 2);
    CHECK(audit.partial_methods == 1);
    CHECK(!audit.unsupported_opcodes.empty());
    return true;
}

bool test_install_diagnose_run() {
    TempDir tmp("install");
    const std::string root = tmp.path("android_root").string();

    ZipBuilder z;
    const auto manifest = make_manifest_axml();
    z.add("AndroidManifest.xml", manifest, true);
    z.add_text("res/layout/main.xml", std::string(), false);  // replaced below
    // Replace the placeholder with real binary layout bytes.
    {
        ZipBuilder z2;
        z2.add("AndroidManifest.xml", manifest, true);
        z2.add("res/layout/main.xml", make_layout_axml(), true);
        z2.add("classes.dex", make_test_dex(), true);
        z2.add("lib/" + android::abi_name(android::host_abi()) + "/libdemo.so",
               make_test_elf(android::abi_elf_machine(android::host_abi())), true);
        const auto bytes = z2.build();
        const fs::path apk = tmp.path("demo.apk");
        std::ofstream out(apk, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();

        CHECK(finn::compat::is_apk_package_path(apk.string()));
        const auto d = android::diagnose_android_package(apk.string());
        CHECK(d.valid);
        CHECK(d.apk.manifest.package_name == "com.example.demo");
        CHECK(d.apk.manifest.version_code == 7);
        CHECK(d.api_compatible);
        CHECK(d.dex_analyzable);
        CHECK(!d.apk.dex_files.empty() && d.apk.dex_files[0].header_valid);
        CHECK(!d.apk.native_libs.empty() && d.apk.native_libs[0].host_compatible);

        const auto install = android::install_android_package(apk.string(), root);
        CHECK(install.ok);
        CHECK(install.app.id == "android:com-example-demo");
        CHECK(install.app.version_code == 7);
        CHECK(fs::exists(install.app.package_path));

        auto apps = android::discover_installed_android_apps(root);
        CHECK(apps.size() == 1);
        CHECK(apps[0].package_name == "com.example.demo");
        const auto compat_apps = finn::compat::discover_android_apps(
            android::default_android_registry_path(root));
        CHECK(compat_apps.size() == 1);
        CHECK(compat_apps[0].id == apps[0].id);

        std::string run_err;
        const auto report = android::run_android_package(
            "com.example.demo", true, root, &run_err);
        CHECK(report.ok);
        CHECK(report.activity == "com.example.demo.MainActivity");
        CHECK(!report.lifecycle_log.empty());

        CHECK(android::uninstall_android_app("com.example.demo", root, &run_err));
        CHECK(android::discover_installed_android_apps(root).empty());
    }
    return true;
}

bool test_ui_bridge_json() {
    android::AndroidUiEngine ui;
    const std::string json = R"({
      "title":"Demo",
      "state":{"count":1},
      "root":{"type":"LinearLayout","id":"root","children":[
        {"type":"TextView","id":"label","text":"Count: {{count}}"},
        {"type":"Button","id":"btn","text":"Tap","onClick":"onTap"}
      ]}
    })";
    std::string err;
    CHECK(ui.inflate_json(json, &err));
    CHECK(ui.title() == "Demo");
    const auto* label = ui.find_node("label");
    CHECK(label != nullptr);
    CHECK(ui.resolve_text(*label) == "Count: 1");
    ui.set_state("count", "2");
    CHECK(ui.resolve_text(*label) == "Count: 2");
    int clicks = 0;
    ui.set_event_handler([&](const std::string& h, const std::string& id,
                             const std::string&) {
        if (h == "onTap" && id == "btn") ++clicks;
    });
    CHECK(ui.trigger_click("btn"));
    CHECK(clicks == 1);
    CHECK(ui.set_text("label", "Changed"));
    CHECK(ui.get_text("label") == "Changed");
    ui.run_activity_lifecycle({"onCreate", "onResume", "bogus"});
    CHECK(ui.lifecycle_log().size() == 3);
    CHECK(ui.lifecycle_log()[2].find("unknown lifecycle phase") != std::string::npos);
    return true;
}

}  // namespace

bool android_unit_checks() {
    return test_zip_reader() && test_axml_typed_values_and_manifest() &&
           test_dex_decode_and_interpreter() && test_ui_bridge_json() &&
           test_install_diagnose_run();
}

int main() {
    // The runtime's own clean-room self-test (built from real in-memory ZIP,
    // AXML and DEX buffers) plus the host-side installer/VM checks below.
    if (!android::android_self_test() || !android_unit_checks()) {
        std::fputs("android tests: FAILED\n", stderr);
        return 1;
    }
    std::puts("android tests: OK");
    return 0;
}
