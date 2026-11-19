// Native Android package formats for finn-android: ZIP/APK constants, binary
// AndroidManifest.xml (AXML) structures, resources.arsc basics, DEX header
// layout and ABI checks. Clean-room: no Android SDK, no AAPT, no Waydroid.
//
// Every field with no value stays absent/zero. Nothing here claims a capability
// the parser has not actually verified.

#ifndef FINN_ANDROID_FORMAT_HPP
#define FINN_ANDROID_FORMAT_HPP

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace finn::android {

// The runtime targets Android 17. The API level is a fact about the platform,
// not a claim that every API 37 surface is bridged: diagnose reports coverage.
inline constexpr int kAndroidTargetApiLevel = 37;
inline constexpr const char* kAndroidTargetRelease = "Android 17";
inline constexpr const char* kAndroidTargetCodename = "";

// ZIP signatures and compression methods (same wire format an APK uses).
inline constexpr std::uint32_t kZipLocalFileSig = 0x04034b50u;
inline constexpr std::uint32_t kZipCentralDirSig = 0x02014b50u;
inline constexpr std::uint32_t kZipEndCentralDirSig = 0x06054b50u;
inline constexpr std::uint16_t kZipMethodStore = 0u;
inline constexpr std::uint16_t kZipMethodDeflate = 8u;

// Android binary XML (AXML) chunk types.
inline constexpr std::uint16_t kAxmlType = 0x0003u;
inline constexpr std::uint16_t kAxmlStringPool = 0x0001u;
inline constexpr std::uint16_t kAxmlResourceMap = 0x0180u;
inline constexpr std::uint16_t kAxmlStartNamespace = 0x0100u;
inline constexpr std::uint16_t kAxmlEndNamespace = 0x0101u;
inline constexpr std::uint16_t kAxmlStartElement = 0x0102u;
inline constexpr std::uint16_t kAxmlEndElement = 0x0103u;
inline constexpr std::uint16_t kAxmlCData = 0x0104u;

inline constexpr std::uint32_t kAxmlUtf8Flag = 0x00000100u;
inline constexpr std::uint32_t kAxmlUtf16Flag = 0x00000000u;
inline constexpr std::uint32_t kAxmlNoEntry = 0xFFFFFFFFu;

// Res_value data types we decode without inventing a value.
inline constexpr std::uint8_t kResNull = 0x00u;
inline constexpr std::uint8_t kResReference = 0x01u;
inline constexpr std::uint8_t kResAttribute = 0x02u;
inline constexpr std::uint8_t kResString = 0x03u;
inline constexpr std::uint8_t kResFloat = 0x04u;
inline constexpr std::uint8_t kResDimension = 0x05u;
inline constexpr std::uint8_t kResFraction = 0x06u;
inline constexpr std::uint8_t kResIntDec = 0x10u;
inline constexpr std::uint8_t kResIntHex = 0x11u;
inline constexpr std::uint8_t kResIntBoolean = 0x12u;
inline constexpr std::uint8_t kResColor = 0x1cu;

// resources.arsc chunk types.
inline constexpr std::uint16_t kArscTable = 0x0002u;
inline constexpr std::uint16_t kArscPackage = 0x0200u;
inline constexpr std::uint16_t kArscTypeString = 0x0201u;
inline constexpr std::uint16_t kArscTypeSpec = 0x0202u;
inline constexpr std::uint16_t kArscLibrary = 0x0203u;

// DEX header. The magic is "dex\nNNN\0" with a three-digit version.
inline constexpr std::size_t kDexHeaderSize = 0x70u;
inline constexpr std::uint32_t kDexEndianLittle = 0x12345678u;
inline constexpr std::uint32_t kDexEndianBig = 0x78563412u;
inline constexpr std::size_t kDexMapItemSize = 12u;

inline constexpr std::uint16_t kDexTypeHeaderItem = 0x0000u;
inline constexpr std::uint16_t kDexTypeStringIdItem = 0x0001u;
inline constexpr std::uint16_t kDexTypeTypeIdItem = 0x0002u;
inline constexpr std::uint16_t kDexTypeProtoIdItem = 0x0003u;
inline constexpr std::uint16_t kDexTypeFieldIdItem = 0x0004u;
inline constexpr std::uint16_t kDexTypeMethodIdItem = 0x0005u;
inline constexpr std::uint16_t kDexTypeClassDefItem = 0x0006u;
inline constexpr std::uint16_t kDexTypeMapList = 0x1000u;
inline constexpr std::uint16_t kDexTypeCodeItem = 0x2000u;

// ELF machine types used by native libraries inside an APK.
inline constexpr std::uint16_t kElfMachineNone = 0u;
inline constexpr std::uint16_t kElfMachine386 = 3u;
inline constexpr std::uint16_t kElfMachineArm = 40u;
inline constexpr std::uint16_t kElfMachineX86_64 = 62u;
inline constexpr std::uint16_t kElfMachineAarch64 = 183u;
inline constexpr std::uint16_t kElfMachineRiscv = 243u;

// A field with no value is absent, never a plausible default.
enum class AndroidAbi {
    ArarmeabiV7a,
    Arm64V8a,
    X86,
    X86_64,
    Riscv64,
    Mips,
    Unknown,
};

inline std::string abi_name(AndroidAbi abi) {
    switch (abi) {
        case AndroidAbi::ArarmeabiV7a: return "armeabi-v7a";
        case AndroidAbi::Arm64V8a: return "arm64-v8a";
        case AndroidAbi::X86: return "x86";
        case AndroidAbi::X86_64: return "x86_64";
        case AndroidAbi::Riscv64: return "riscv64";
        case AndroidAbi::Mips: return "mips";
        case AndroidAbi::Unknown: break;
    }
    return {};
}

inline AndroidAbi abi_from_name(std::string_view name) {
    if (name == "armeabi-v7a" || name == "armeabi") return AndroidAbi::ArarmeabiV7a;
    if (name == "arm64-v8a") return AndroidAbi::Arm64V8a;
    if (name == "x86") return AndroidAbi::X86;
    if (name == "x86_64") return AndroidAbi::X86_64;
    if (name == "riscv64") return AndroidAbi::Riscv64;
    if (name == "mips" || name == "mips64") return AndroidAbi::Mips;
    return AndroidAbi::Unknown;
}

inline std::uint16_t abi_elf_machine(AndroidAbi abi) {
    switch (abi) {
        case AndroidAbi::ArarmeabiV7a: return kElfMachineArm;
        case AndroidAbi::Arm64V8a: return kElfMachineAarch64;
        case AndroidAbi::X86: return kElfMachine386;
        case AndroidAbi::X86_64: return kElfMachineX86_64;
        case AndroidAbi::Riscv64: return kElfMachineRiscv;
        case AndroidAbi::Mips:
        case AndroidAbi::Unknown: break;
    }
    return kElfMachineNone;
}

inline std::string host_cpu_machine() {
    struct utsname uts{};
    if (::uname(&uts) == 0) return uts.machine;
    return {};
}

inline AndroidAbi host_abi() {
    const std::string m = host_cpu_machine();
    if (m == "aarch64" || m == "arm64") return AndroidAbi::Arm64V8a;
    if (m == "x86_64" || m == "amd64") return AndroidAbi::X86_64;
    if (m.rfind("arm", 0) == 0) return AndroidAbi::ArarmeabiV7a;
    if (m == "riscv64") return AndroidAbi::Riscv64;
    if (m == "i386" || m == "i686") return AndroidAbi::X86;
    return AndroidAbi::Unknown;
}

// Bounds-safe little-endian readers. Every call site must have validated the
// span first; these helpers never read past the pointer they are handed.
inline std::uint16_t read_le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t read_le64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(read_le32(p)) |
           (static_cast<std::uint64_t>(read_le32(p + 4)) << 32);
}

inline bool read_uleb128(const std::uint8_t* data, std::size_t size,
                         std::size_t* pos, std::uint64_t* out) {
    std::uint64_t result = 0;
    unsigned shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (*pos >= size) return false;
        const std::uint8_t byte = data[(*pos)++];
        if (i == 9 && (byte & 0x7fu) > 1u) return false;
        result |= static_cast<std::uint64_t>(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0u) {
            *out = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

inline bool read_sleb128(const std::uint8_t* data, std::size_t size,
                         std::size_t* pos, std::int64_t* out) {
    std::uint64_t result = 0;
    unsigned shift = 0;
    std::uint8_t byte = 0;
    for (int i = 0; i < 10; ++i) {
        if (*pos >= size) return false;
        byte = data[(*pos)++];
        result |= static_cast<std::uint64_t>(byte & 0x7fu) << shift;
        shift += 7;
        if ((byte & 0x80u) == 0u) {
            if (shift < 64 && (byte & 0x40u) != 0u) {
                result |= (~std::uint64_t{0}) << shift;
            }
            *out = static_cast<std::int64_t>(result);
            return true;
        }
    }
    return false;
}

// Android's modified UTF-8: NUL is encoded as C0 80 and supplementary characters
// as a surrogate pair. A decoded string never contains a fabricated replacement.
std::string decode_mutf8(const std::uint8_t* data, std::size_t size,
                         std::size_t* pos);

struct AndroidActivityInfo {
    std::string name;
    std::string label;
    std::string icon_ref;
    bool exported = false;
    bool launcher = false;
    std::vector<std::string> actions;
    std::vector<std::string> categories;
};

struct ApkManifest {
    bool valid = false;
    std::string package_name;
    std::uint32_t version_code = 0;
    std::string version_name;
    int min_sdk = 0;
    int target_sdk = 0;
    int compile_sdk = 0;
    std::string application_label;
    std::string application_icon;
    std::string application_name;
    bool debuggable = false;
    bool has_application = false;
    std::vector<AndroidActivityInfo> activities;
    std::vector<std::string> permissions;
    std::vector<std::string> features;
    std::string error;

    std::string main_activity() const;
};

struct AxmlAttribute {
    std::string ns;
    std::string name;
    std::string value;        // resolved literal or string-pool entry, when real
    std::uint32_t raw_value = kAxmlNoEntry;
    std::uint8_t data_type = kResNull;
    std::uint32_t data = 0;   // typed value payload; a resource id when reference
};

struct AxmlElement {
    std::string ns;
    std::string name;
    bool is_end = false;   // an end-element event; attributes are empty
    std::vector<AxmlAttribute> attributes;

    const AxmlAttribute* attr(std::string_view attr_name) const;
    std::string attr_value(std::string_view attr_name) const;
};

struct AxmlDocument {
    bool valid = false;
    std::vector<AxmlElement> elements;        // every start element, in order
    std::vector<std::string> strings;         // the real string pool
    std::vector<std::uint32_t> resource_ids;  // the real resource map, when present
    std::string error;
};

struct ArscResource {
    std::uint32_t id = 0;
    std::string package;
    std::string type;
    std::string key;
};

struct ArscInfo {
    bool valid = false;
    std::uint32_t package_count = 0;
    std::vector<ArscResource> resources;
    std::vector<std::string> packages;
    std::string error;

    // Resolves an 0xPPTTEEEE resource id to its real entry key when arsc data
    // was present; returns an empty string rather than a guessed name.
    std::string resolve_key(std::uint32_t id) const;
};

struct DexHeaderInfo {
    bool valid = false;
    std::string magic;
    int version = 0;
    std::uint32_t checksum = 0;
    std::uint8_t signature[20] = {};
    std::uint32_t file_size = 0;
    std::uint32_t header_size = 0;
    std::uint32_t endian_tag = 0;
    std::uint32_t map_off = 0;
    std::uint32_t string_ids_size = 0;
    std::uint32_t string_ids_off = 0;
    std::uint32_t type_ids_size = 0;
    std::uint32_t type_ids_off = 0;
    std::uint32_t proto_ids_size = 0;
    std::uint32_t proto_ids_off = 0;
    std::uint32_t field_ids_size = 0;
    std::uint32_t field_ids_off = 0;
    std::uint32_t method_ids_size = 0;
    std::uint32_t method_ids_off = 0;
    std::uint32_t class_defs_size = 0;
    std::uint32_t class_defs_off = 0;
    std::uint32_t data_size = 0;
    std::uint32_t data_off = 0;
};

struct DexVerification {
    bool valid = false;
    DexHeaderInfo header;
    bool checksum_valid = false;
    bool signature_valid = false;
    std::vector<std::string> issues;
    std::string error;
};

struct NativeLibInfo {
    std::string archive_path;
    std::string abi;
    std::uint16_t elf_machine = kElfMachineNone;
    bool valid_elf = false;
    bool host_compatible = false;
};

AxmlDocument parse_axml(const std::uint8_t* data, std::size_t size);
AxmlDocument parse_axml_bytes(const std::vector<std::uint8_t>& bytes);
ApkManifest parse_binary_manifest(const AxmlDocument& axml);

ArscInfo parse_arsc(const std::uint8_t* data, std::size_t size);
ArscInfo parse_arsc_bytes(const std::vector<std::uint8_t>& bytes);

DexVerification verify_dex_header(const std::uint8_t* data, std::size_t size);

}  // namespace finn::android

#endif  // FINN_ANDROID_FORMAT_HPP
