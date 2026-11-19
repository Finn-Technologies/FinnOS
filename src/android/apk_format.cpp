#include "android/apk_format.hpp"

#include "android/dex_runtime.hpp"

#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::android {
namespace {

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

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

ApkNativeLibInfo inspect_native_lib(const std::string& entry,
                                    const std::vector<std::uint8_t>& bytes) {
    ApkNativeLibInfo info;
    info.entry = entry;
    if (starts_with(entry, "lib/")) {
        const std::size_t slash = entry.find('/', 4);
        if (slash != std::string::npos) info.abi = entry.substr(4, slash - 4);
    }
    if (bytes.size() < 20 || bytes[0] != 0x7fu || bytes[1] != 'E' ||
        bytes[2] != 'L' || bytes[3] != 'F') {
        return info;
    }
    info.valid_elf = true;
    info.elf_machine = read_le16(bytes.data() + 18);
    const AndroidAbi host = host_abi();
    info.host_compatible = (!info.abi.empty() && info.abi == abi_name(host) &&
                            info.elf_machine == abi_elf_machine(host));
    return info;
}

std::string package_tail(const std::string& package) {
    const std::size_t dot = package.rfind('.');
    std::string tail = (dot != std::string::npos && dot + 1 < package.size())
                           ? package.substr(dot + 1)
                           : package;
    if (tail.empty()) return {};
    tail[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(tail[0])));
    return tail;
}

// ---- In-memory buffer builders used only by android_self_test() ----

void put_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xffu));
    b.push_back(static_cast<std::uint8_t>(v >> 8u));
}

void put_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        b.push_back(static_cast<std::uint8_t>(v >> (i * 8u)));
    }
}

void put_uleb(std::vector<std::uint8_t>& b, std::uint32_t v) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(v & 0x7fu);
        v >>= 7u;
        if (v != 0u) byte |= 0x80u;
        b.push_back(byte);
    } while (v != 0u);
}

void patch_u32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>(v >> (i * 8u));
    }
}

std::vector<std::uint8_t> build_test_zip(const std::string& name,
                                         const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> b;
    const std::uint32_t crc = static_cast<std::uint32_t>(
        ::crc32(0L, payload.empty() ? Z_NULL : payload.data(),
                static_cast<uInt>(payload.size())));
    const std::uint32_t size = static_cast<std::uint32_t>(payload.size());

    put_u32(b, kZipLocalFileSig);
    put_u16(b, 20);  // version needed
    put_u16(b, 0);   // flags
    put_u16(b, 0);   // stored
    put_u16(b, 0);   // time
    put_u16(b, 0);   // date
    put_u32(b, crc);
    put_u32(b, size);
    put_u32(b, size);
    put_u16(b, static_cast<std::uint16_t>(name.size()));
    put_u16(b, 0);
    b.insert(b.end(), name.begin(), name.end());
    b.insert(b.end(), payload.begin(), payload.end());
    const std::uint32_t cd_off = static_cast<std::uint32_t>(b.size());

    put_u32(b, kZipCentralDirSig);
    put_u16(b, 20);  // made by
    put_u16(b, 20);  // needed
    put_u16(b, 0);   // flags
    put_u16(b, 0);   // method
    put_u16(b, 0);   // time
    put_u16(b, 0);   // date
    put_u32(b, crc);
    put_u32(b, size);
    put_u32(b, size);
    put_u16(b, static_cast<std::uint16_t>(name.size()));
    put_u16(b, 0);  // extra
    put_u16(b, 0);  // comment
    put_u16(b, 0);  // disk start
    put_u16(b, 0);  // internal attrs
    put_u32(b, 0);  // external attrs
    put_u32(b, 0);  // local header is at offset zero
    b.insert(b.end(), name.begin(), name.end());
    const std::uint32_t cd_size = static_cast<std::uint32_t>(b.size() - cd_off);

    put_u32(b, kZipEndCentralDirSig);
    put_u16(b, 0);
    put_u16(b, 0);
    put_u16(b, 1);
    put_u16(b, 1);
    put_u32(b, cd_size);
    put_u32(b, cd_off);
    put_u16(b, 0);
    return b;
}

struct TestAttr {
    std::string name;
    std::string value;
};

struct TestElem {
    std::string name;
    bool end = false;
    std::vector<TestAttr> attrs;
};

std::vector<std::uint8_t> build_test_axml(const std::vector<std::string>& strings,
                                          const std::vector<TestElem>& elements) {
    auto index_of = [&](const std::string& s) -> std::uint32_t {
        const auto it = std::find(strings.begin(), strings.end(), s);
        return static_cast<std::uint32_t>(it - strings.begin());
    };

    // ResStringPool, UTF-8 encoded. Both length prefixes are the UTF-16 and
    // byte lengths; our test strings are ASCII so they are identical.
    std::vector<std::uint32_t> offsets;
    std::size_t data_len = 0;
    for (const auto& s : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data_len));
        data_len += 1u + 1u + s.size() + 1u;
    }
    const std::uint32_t strings_start =
        static_cast<std::uint32_t>(28u + strings.size() * 4u);

    std::vector<std::uint8_t> pool;
    put_u16(pool, kAxmlStringPool);
    put_u16(pool, 28);
    put_u32(pool, 0);  // patched
    put_u32(pool, static_cast<std::uint32_t>(strings.size()));
    put_u32(pool, 0);  // style count
    put_u32(pool, kAxmlUtf8Flag);
    put_u32(pool, strings_start);
    put_u32(pool, 0);  // styles start
    for (std::uint32_t off : offsets) put_u32(pool, off);
    for (const auto& s : strings) {
        put_uleb(pool, static_cast<std::uint32_t>(s.size()));
        put_uleb(pool, static_cast<std::uint32_t>(s.size()));
        pool.insert(pool.end(), s.begin(), s.end());
        pool.push_back(0);
    }
    while (pool.size() % 4u != 0u) pool.push_back(0);
    patch_u32(pool, 4, static_cast<std::uint32_t>(pool.size()));

    std::vector<std::uint8_t> body;
    for (const auto& el : elements) {
        std::vector<std::uint8_t> c;
        put_u16(c, el.end ? kAxmlEndElement : kAxmlStartElement);
        put_u16(c, 16);
        put_u32(c, 0);  // patched
        put_u32(c, 1);         // line number
        put_u32(c, kAxmlNoEntry);  // comment
        put_u32(c, kAxmlNoEntry);  // namespace
        put_u32(c, index_of(el.name));
        put_u16(c, 20);  // attributeStart
        put_u16(c, 20);  // attributeSize
        put_u16(c, static_cast<std::uint16_t>(el.attrs.size()));
        put_u16(c, 0);  // idIndex
        put_u16(c, 0);  // classIndex
        put_u16(c, 0);  // styleIndex
        for (const auto& a : el.attrs) {
            put_u32(c, kAxmlNoEntry);
            put_u32(c, index_of(a.name));
            put_u32(c, index_of(a.value));  // rawValue: the string-pool path
            put_u16(c, 8);                  // Res_value size
            c.push_back(0);                 // res0
            c.push_back(kResString);        // dataType
            put_u32(c, index_of(a.value));
        }
        patch_u32(c, 4, static_cast<std::uint32_t>(c.size()));
        body.insert(body.end(), c.begin(), c.end());
    }

    std::vector<std::uint8_t> out;
    put_u16(out, kAxmlType);
    put_u16(out, 8);
    put_u32(out, static_cast<std::uint32_t>(8u + pool.size() + body.size()));
    out.insert(out.end(), pool.begin(), pool.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace

bool is_apk_file(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    if (!is_apk_path(path) || !ZipReader::has_zip_magic(path)) return false;
    ZipReader zip;
    return zip.open_file(path) && zip.has_entry("AndroidManifest.xml");
}

ApkInfo inspect_apk(const std::string& path) {
    ApkInfo info;
    info.path = path;
    info.host_abi = abi_name(host_abi());

    ZipReader zip;
    std::string zerr;
    if (!zip.open_file(path, &zerr)) {
        info.error = zerr;
        return info;
    }

    std::vector<std::uint8_t> manifest_bytes;
    if (!zip.read_entry("AndroidManifest.xml", &manifest_bytes, &zerr)) {
        info.error = "APK has no readable binary AndroidManifest.xml: " + zerr;
        return info;
    }
    const AxmlDocument axml = parse_axml(manifest_bytes.data(), manifest_bytes.size());
    if (!axml.valid) {
        info.error = axml.error.empty() ? "binary AndroidManifest.xml is invalid" : axml.error;
        return info;
    }
    info.manifest = parse_binary_manifest(axml);
    info.has_manifest = info.manifest.valid;
    if (!info.manifest.valid) {
        info.error = info.manifest.error.empty() ? "manifest has no package name"
                                                 : info.manifest.error;
        return info;
    }

    std::vector<std::uint8_t> arsc_bytes;
    if (zip.read_entry("resources.arsc", &arsc_bytes, &zerr)) {
        info.arsc = parse_arsc(arsc_bytes.data(), arsc_bytes.size());
        info.has_arsc = info.arsc.valid;
        if (!info.arsc.valid && !info.arsc.error.empty()) {
            info.warnings.push_back("resources.arsc: " + info.arsc.error);
        }
    } else {
        info.warnings.push_back("APK has no resources.arsc");
    }

    bool saw_dex = false;
    bool all_dex_valid = true;
    for (const auto& e : zip.entries()) {
        if (e.is_directory) continue;
        const bool dex = (e.name == "classes.dex") ||
                         (starts_with(e.name, "classes") && ends_with_ci(e.name, ".dex"));
        if (dex) {
            saw_dex = true;
            std::vector<std::uint8_t> bytes;
            ApkDexInfo d;
            d.entry = e.name;
            if (!zip.read_entry(e.name, &bytes, &zerr)) {
                d.error = zerr;
            } else {
                d.size = bytes.size();
                const DexVerification v = verify_dex_header(bytes.data(), bytes.size());
                d.header_valid = v.valid;
                d.checksum_valid = v.checksum_valid;
                d.signature_valid = v.signature_valid;
                d.version = v.header.version > 0 ? v.header.magic.substr(4) : std::string();
                if (!v.error.empty()) {
                    d.error = v.error;
                } else if (!v.issues.empty()) {
                    d.error = v.issues.front();
                }
            }
            if (!d.header_valid) {
                all_dex_valid = false;
                info.warnings.push_back(d.entry + ": " +
                                        (d.error.empty() ? "invalid DEX header" : d.error));
            }
            info.dex_files.push_back(std::move(d));
        } else if (starts_with(e.name, "res/")) {
            info.resource_entries.push_back(e.name);
        } else if (starts_with(e.name, "assets/")) {
            info.asset_entries.push_back(e.name);
        }
    }
    info.dex_valid = saw_dex && all_dex_valid;
    if (!saw_dex) info.warnings.push_back("APK contains no classes*.dex");

    bool saw_native = false;
    bool any_host_native = false;
    for (const auto& e : zip.entries()) {
        if (e.is_directory || !starts_with(e.name, "lib/") || !ends_with_ci(e.name, ".so")) {
            continue;
        }
        saw_native = true;
        std::vector<std::uint8_t> bytes;
        if (!zip.read_entry(e.name, &bytes, &zerr)) {
            info.warnings.push_back("unreadable native library " + e.name + ": " + zerr);
            continue;
        }
        ApkNativeLibInfo lib = inspect_native_lib(e.name, bytes);
        if (lib.host_compatible) any_host_native = true;
        info.native_libs.push_back(std::move(lib));
    }
    info.native_abi_ok = !saw_native || any_host_native;
    if (saw_native && !any_host_native) {
        info.warnings.push_back("no native library matches the host ABI (" +
                                info.host_abi + ")");
    }

    info.api_compatible = info.manifest.min_sdk <= kAndroidTargetApiLevel;
    if (!info.api_compatible) {
        info.warnings.push_back("minSdkVersion " +
                                std::to_string(info.manifest.min_sdk) +
                                " exceeds the modelled Android 17 / API 37 surface");
    }
    info.valid = true;
    info.runnable = info.valid && info.api_compatible && info.dex_valid && info.native_abi_ok;

    std::ostringstream ss;
    ss << info.manifest.package_name << " v" << info.manifest.version_name
       << " (code " << info.manifest.version_code << "), minSdk "
       << info.manifest.min_sdk << ", targetSdk " << info.manifest.target_sdk
       << "; " << info.dex_files.size() << " DEX file(s), "
       << info.native_libs.size() << " native lib(s)";
    if (!info.dex_valid) ss << "; DEX bytecode is not fully verified";
    if (!info.native_abi_ok) ss << "; native ABI does not match " << info.host_abi;
    ss << ". This is a static pre-flight, not the Android framework.";
    info.summary = ss.str();
    return info;
}

std::string resolve_apk_resource_key(const std::string& reference,
                                     const ArscInfo& arsc) {
    if (reference.empty()) return {};
    std::string ref = reference;
    if (ref.front() == '@') ref.erase(ref.begin());
    if (ref.empty()) return {};

    if (starts_with(ref, "0x") || starts_with(ref, "0X")) {
        char* end = nullptr;
        const unsigned long long id = std::strtoull(ref.c_str(), &end, 16);
        if (end == ref.c_str() || id > 0xffffffffull) return {};
        return arsc.resolve_key(static_cast<std::uint32_t>(id));
    }
    const std::size_t slash = ref.find('/');
    if (slash != std::string::npos) return ref.substr(slash + 1);
    return {};
}

std::string apk_display_name(const ApkInfo& info) {
    const std::string& label = info.manifest.application_label;
    if (!label.empty() && label.front() != '@') return label;
    // An @string/@0x reference is absent here: a resource *key* is not a
    // localized label, so it is never substituted as a display name.
    const std::string tail = package_tail(info.manifest.package_name);
    return tail.empty() ? std::string("Android App") : tail;
}

bool android_self_test() {
    bool ok = true;
    const auto check = [&ok](bool cond) { if (!cond) ok = false; };

    // ZIP path traversal guards and APK extension detection.
    check(ZipReader::is_safe_entry_path("res/layout/main.xml"));
    check(!ZipReader::is_safe_entry_path("../secret"));
    check(!ZipReader::is_safe_entry_path("/absolute/path"));
    check(!ZipReader::is_safe_entry_path("C:\\windows\\file"));
    check(!ZipReader::is_safe_entry_path("a/../../b"));
    check(!ZipReader::is_safe_entry_path(std::string("a\0b", 3)));
    check(!ZipReader::is_safe_entry_path(""));
    check(is_apk_path("Foo.APK"));
    check(!is_apk_path("Foo.apk.txt"));

    // Android modified UTF-8: C0 80 is a real NUL, not a replacement char.
    const std::uint8_t mutf8[] = {'a', 0xc0u, 0x80u, 'b'};
    std::size_t mutf8_pos = 0;
    const std::string decoded = decode_mutf8(mutf8, sizeof(mutf8), &mutf8_pos);
    check(decoded.size() == 3u && decoded[0] == 'a' && decoded[1] == '\0' &&
          decoded[2] == 'b');
    check(mutf8_pos == sizeof(mutf8));

    // A real stored ZIP entry: central directory, bounds and CRC-32 on read.
    const std::vector<std::uint8_t> payload = {'f', 'i', 'n', 'n', '-', 'a',
                                                'n', 'd', 'r', 'o', 'i', 'd'};
    const std::vector<std::uint8_t> archive = build_test_zip("notes.txt", payload);
    ZipReader zip;
    std::string err;
    check(zip.open_memory(archive.data(), archive.size(), &err));
    check(zip.has_entry("notes.txt"));
    std::vector<std::uint8_t> out;
    check(zip.read_entry("notes.txt", &out, &err));
    check(out == payload);
    check(err.empty());

    // A real binary AndroidManifest: string pool + start/end manifest element.
    const std::vector<std::string> strings = {
        "manifest", "package", "versionCode", "com.example.finn", "37"};
    const std::vector<TestElem> elements = {
        {"manifest", false, {{"package", "com.example.finn"},
                             {"versionCode", "37"}}},
        {"manifest", true, {}},
    };
    const std::vector<std::uint8_t> axml = build_test_axml(strings, elements);
    const AxmlDocument doc = parse_axml(axml.data(), axml.size());
    check(doc.valid);
    const ApkManifest manifest = parse_binary_manifest(doc);
    check(manifest.valid);
    check(manifest.package_name == "com.example.finn");
    check(manifest.version_code == 37u);

    // A real subset of Dalvik opcodes with correct operand widths, plus an
    // unmapped opcode that must be reported as unsupported rather than guessed.
    const std::uint16_t insns[] = {
        0x000eu,  // return-void
        0x2312u,  // const/4 v3, #2  (lit=2 -> 0x2000, reg=3 -> 0x0300, op=0x12)
        0x030fu,  // return v3
    };
    DexInstruction ins{};
    check(decode_dex_instruction(insns, 3u, 0u, &ins) && ins.mnemonic == "return-void");
    check(decode_dex_instruction(insns, 3u, 1u, &ins) && ins.opcode == 0x12u &&
          ins.literal == 2 && ins.registers.size() == 1u && ins.registers[0] == 3u);
    check(decode_dex_instruction(insns, 3u, 2u, &ins) && ins.mnemonic == "return" &&
          !ins.registers.empty() && ins.registers[0] == 3u);
    const std::uint16_t unsupported[] = {0x003eu};  // aget: outside the subset
    check(!decode_dex_instruction(unsupported, 1u, 0u, &ins) && !ins.supported &&
          !ins.reason.empty());
    DexCodeItem code;
    code.insns_size = 3u;
    code.insns.assign(insns, insns + 3u);
    const DexDisassembly dis = disassemble_dex_code(code);
    check(dis.complete && dis.instructions.size() == 3u);

    return ok;
}

}  // namespace finn::android
