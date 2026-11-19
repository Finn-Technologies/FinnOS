// OpenHarmony 6.1 LTS (API Level 23) package formats, Panda/Ark (.abc) binary
// constants, manifest structures, and JSON5 parser for finn-ohos.

#ifndef FINN_OHOS_FORMAT_HPP
#define FINN_OHOS_FORMAT_HPP

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finn::ohos {

inline constexpr int kOhosSupportedApiLevel = 23;
inline constexpr const char* kOhosReleaseName = "OpenHarmony 6.1 LTS";
inline constexpr const char* kOhosOsFullName = "OpenHarmony-6.1-LTS (FinnOS)";
inline constexpr std::uint8_t kPandaMagic[8] = {
    'P', 'A', 'N', 'D', 'A', '\0', '\0', '\0'};
inline constexpr std::size_t kPandaHeaderSize = 60;
inline constexpr std::size_t kPandaIndexHeaderSize = 40;

// ZIP format signatures & compression methods
inline constexpr std::uint32_t kZipLocalFileSig = 0x04034b50u;
inline constexpr std::uint32_t kZipCentralDirSig = 0x02014b50u;
inline constexpr std::uint32_t kZipEndCentralDirSig = 0x06054b50u;
inline constexpr std::uint16_t kZipMethodStore = 0u;
inline constexpr std::uint16_t kZipMethodDeflate = 8u;

// ELF header constants for native NAPI .so ABI verification
inline constexpr std::uint8_t kElfMagic[4] = {0x7fu, 'E', 'L', 'F'};
inline constexpr std::uint16_t kElfMachine386 = 3u;
inline constexpr std::uint16_t kElfMachineArm = 40u;
inline constexpr std::uint16_t kElfMachineX86_64 = 62u;
inline constexpr std::uint16_t kElfMachineAarch64 = 183u;
inline constexpr std::uint16_t kElfMachineRiscv = 243u;

// Panda/ArkCompiler class, field, and method metadata tags
namespace panda_tag {
inline constexpr std::uint8_t kNothing = 0x00u;
inline constexpr std::uint8_t kClassInterfaces = 0x01u;
inline constexpr std::uint8_t kClassSourceLang = 0x02u;
inline constexpr std::uint8_t kClassRuntimeAnnotation = 0x03u;
inline constexpr std::uint8_t kClassAnnotation = 0x04u;
inline constexpr std::uint8_t kClassRuntimeTypeAnnotation = 0x05u;
inline constexpr std::uint8_t kClassTypeAnnotation = 0x06u;
inline constexpr std::uint8_t kClassSourceFile = 0x07u;

inline constexpr std::uint8_t kFieldIntValue = 0x01u;
inline constexpr std::uint8_t kFieldValue = 0x02u;
inline constexpr std::uint8_t kFieldRuntimeAnnotation = 0x03u;
inline constexpr std::uint8_t kFieldAnnotation = 0x04u;
inline constexpr std::uint8_t kFieldRuntimeTypeAnnotation = 0x05u;
inline constexpr std::uint8_t kFieldTypeAnnotation = 0x06u;

inline constexpr std::uint8_t kMethodCode = 0x01u;
inline constexpr std::uint8_t kMethodSourceLang = 0x02u;
inline constexpr std::uint8_t kMethodRuntimeAnnotation = 0x03u;
inline constexpr std::uint8_t kMethodRuntimeParamAnnotation = 0x04u;
inline constexpr std::uint8_t kMethodDebugInfo = 0x05u;
inline constexpr std::uint8_t kMethodAnnotation = 0x06u;
inline constexpr std::uint8_t kMethodParamAnnotation = 0x07u;
inline constexpr std::uint8_t kMethodTypeAnnotation = 0x08u;
inline constexpr std::uint8_t kMethodProfileSize = 0x09u;
}  // namespace panda_tag

// ArkCompiler Panda bytecode opcodes (OpenHarmony 6.1 LTS / ArkTS ISA)
enum class AbcOpcode : std::uint8_t {
    LdUndefined = 0x00u,
    LdNull = 0x01u,
    LdTrue = 0x02u,
    LdFalse = 0x03u,
    CreateEmptyObject = 0x04u,
    CreateEmptyArray = 0x05u,
    CreateObjectWithBuffer = 0x07u,  // op_imm_8_id_16
    NewLexEnv = 0x09u,   // newlexenv imm:u8
    PopLexEnv = 0x69u,   // poplexenv
    DefineFunc = 0x33u,      // op_imm1_8_id_16_imm2_8
    NewObjRange = 0x08u,
    Add2 = 0x0au,
    Sub2 = 0x0bu,
    Mul2 = 0x0cu,
    Div2 = 0x0du,
    Mod2 = 0x0eu,
    Neg = 0x1fu,     // neg imm:u8 (acc = -acc)
    // istrue / isfalse: op_none, acc becomes a strict boolean test result
    // (public Panda ISA 0x23 / 0x24).
    IsTrue = 0x23u,
    IsFalse = 0x24u,
    // Comparison / call opcode values follow the public Panda ESM ISA
    // (arkcompiler_ets_runtime/ecmascript/isa.yaml), not a local guess.
    Eq = 0x0fu,
    NotEq = 0x10u,
    Less = 0x11u,
    Greater = 0x13u,
    CallArg0 = 0x29u,
    CallArg1 = 0x2au,
    CallArgs2 = 0x2bu,
    CallArgs3 = 0x2cu,
    CallThis0 = 0x2du,
    CallThis1 = 0x2eu,
    CallThis2 = 0x2fu,
    CallThis3 = 0x30u,
    LdaStr = 0x3eu,
    LdLexVar = 0x3cu,    // imm1:u4 (level), imm2:u4 (slot) packed in one byte
    StLexVar = 0x3du,
    TryLdGlobalByName8 = 0x3fu,
    TryStGlobalByName8 = 0x40u,
    LdObjByName8 = 0x42u,
    StObjByName8 = 0x43u,
    // stownbyname imm:u8, string_id, v:in:top — store an own property. The
    // public Panda ISA gives 0x7a (8-bit IC slot) and 0xcc (16-bit IC slot).
    // This runtime has no prototype chain, so own-property and object stores
    // share the same backing object.
    StOwnByName8 = 0x7au,
    StOwnByName16 = 0xccu,
    Mov4 = 0x44u,
    Mov8 = 0x45u,
    Jmp8 = 0x4du,
    Jmp16 = 0x4eu,
    Jeqz8 = 0x4fu,
    Jeqz16 = 0x50u,
    Jnez8 = 0x51u,
    Jnez16 = 0x9bu,
    Lda = 0x60u,
    Sta = 0x61u,
    Ldai = 0x62u,
    Fldai = 0x63u,
    Return = 0x64u,
    ReturnUndefined = 0x65u,
    LdGlobal = 0x6du,    // ldglobal (op_none)
    DefineFieldByName = 0xdbu,  // op_imm_8_id_16_v_8
    LdLocalModuleVar = 0x7du,
    // ldexternalmodulevar imm:u8 — load an imported binding into the
    // accumulator (public Panda ISA, format op_imm_8).
    LdExternalModuleVar = 0x7eu,
    TryLdGlobalByName16 = 0x8cu,
    TryStGlobalByName16 = 0x8du,
    LdObjByName16 = 0x90u,
    StObjByName16 = 0x91u,
    // Two-byte prefixed instructions: 0xfe selects the "throw" prefix.
    // Sub-opcode 0x09 is undefinedifholewithname (id16).
    ThrowPrefixed = 0xfeu,
};

enum class OhosPackageKind {
    Unknown,
    Hap,
    Hsp,
    AppPack,
};

struct OhosSkill {
    std::vector<std::string> entities;
    std::vector<std::string> actions;
    std::vector<std::string> uris;
};

struct OhosAbilityInfo {
    std::string name;
    std::string src_entry;
    std::string description;
    std::string icon;
    std::string label;
    std::string start_window_icon;
    std::string start_window_background;
    std::string launch_type;
    bool exported = false;
    bool allow_self_redirect = false;  // OpenHarmony 6.1 LTS (API 23)
    std::vector<OhosSkill> skills;
};

struct OhosDependency {
    std::string bundle_name;
    std::string module_name;
    std::uint32_t version_code = 0;
};

struct OhosModuleManifest {
    bool valid = false;
    std::string bundle_name;
    std::string bundle_type;
    std::string vendor;
    std::uint32_t version_code = 0;
    std::string version_name;
    std::string app_icon;
    std::string app_label;
    int min_api_version = 0;
    int target_api_version = 0;
    std::string api_release_type;
    std::string module_name;
    std::string module_type;  // "entry", "feature", "shared"
    std::string main_element;
    std::string pages;
    std::vector<std::string> device_types;
    std::vector<OhosAbilityInfo> abilities;
    std::vector<std::string> request_permissions;
    std::vector<OhosDependency> dependencies;
    std::string error;
};

struct OhosPackModuleEntry {
    std::string name;
    std::string module_name;
    std::string module_type;
    std::vector<std::string> device_types;
};

struct OhosPackInfo {
    bool valid = false;
    std::string bundle_name;
    std::string bundle_type;
    std::uint32_t version_code = 0;
    std::string version_name;
    int min_api_version = 0;
    int target_api_version = 0;
    std::vector<OhosPackModuleEntry> packages;
    std::string error;
};

struct OhosNativeLibInfo {
    std::string archive_path;
    std::string abi;
    std::string soname;
    std::string elf_machine;
    bool valid_elf = false;
    bool host_compatible = false;
};

struct OhosResourceIndexInfo {
    bool present = false;
    bool valid = false;
    std::string version;
    std::uint32_t file_size = 0;
    std::uint32_t limit_key_count = 0;
};

struct InstalledOhosApp {
    std::string id;  // "ohos:<slug>"
    std::string name;
    std::string bundle_name;
    std::string version_name;
    std::uint32_t version_code = 0;
    int target_api_version = 0;
    int min_api_version = 0;
    std::string main_ability;
    std::string module_type;
    std::string install_dir;
    std::string package_path;
    std::string icon_path;
    std::vector<std::string> shared_packages;
};

// Bounds-safe little-endian binary helpers
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
                         std::size_t* pos, std::uint32_t* out) {
    std::uint32_t result = 0;
    unsigned shift = 0;
    for (int i = 0; i < 5; ++i) {
        if (*pos >= size) return false;
        const std::uint8_t byte = data[(*pos)++];
        result |= static_cast<std::uint32_t>(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0u) {
            *out = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

inline bool read_sleb128(const std::uint8_t* data, std::size_t size,
                         std::size_t* pos, std::int32_t* out) {
    std::uint32_t result = 0;
    unsigned shift = 0;
    std::uint8_t byte = 0;
    for (int i = 0; i < 5; ++i) {
        if (*pos >= size) return false;
        byte = data[(*pos)++];
        result |= static_cast<std::uint32_t>(byte & 0x7fu) << shift;
        shift += 7;
        if ((byte & 0x80u) == 0u) {
            if (shift < 32 && (byte & 0x40u) != 0u) {
                result |= (~0u << shift);
            }
            *out = static_cast<std::int32_t>(result);
            return true;
        }
    }
    return false;
}

inline std::string host_cpu_machine() {
    struct utsname uts{};
    if (::uname(&uts) == 0) return uts.machine;
    return "unknown";
}

inline std::string host_ohos_abi() {
    const std::string m = host_cpu_machine();
    if (m == "aarch64" || m == "arm64") return "arm64-v8a";
    if (m == "x86_64" || m == "amd64") return "x86_64";
    if (m.rfind("arm", 0) == 0) return "armeabi-v7a";
    if (m == "riscv64") return "riscv64";
    return m;
}

inline std::string package_kind_name(OhosPackageKind kind) {
    switch (kind) {
        case OhosPackageKind::Hap: return "HAP";
        case OhosPackageKind::Hsp: return "HSP";
        case OhosPackageKind::AppPack: return "APP";
        case OhosPackageKind::Unknown: break;
    }
    return "Unknown";
}

// Clean-room JSON / JSON5 DOM parser shared by manifest, pack.info, and ArkUI pages.
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<JsonValue> array_val;
    std::map<std::string, JsonValue> object_val;

    const JsonValue* get(const std::string& key) const {
        if (kind != Kind::Object) return nullptr;
        const auto it = object_val.find(key);
        return (it == object_val.end()) ? nullptr : &it->second;
    }

    std::string get_string(const std::string& key, const std::string& def = {}) const {
        const JsonValue* v = get(key);
        if (!v) return def;
        if (v->kind == Kind::String) return v->str_val;
        return def;
    }

    int get_int(const std::string& key, int def = 0) const {
        const JsonValue* v = get(key);
        if (!v) return def;
        if (v->kind == Kind::Number) return static_cast<int>(v->num_val);
        if (v->kind == Kind::String) {
            char* end = nullptr;
            const long parsed = std::strtol(v->str_val.c_str(), &end, 10);
            if (end && end != v->str_val.c_str()) return static_cast<int>(parsed);
        }
        return def;
    }

    double get_double(const std::string& key, double def = 0.0) const {
        const JsonValue* v = get(key);
        if (!v) return def;
        if (v->kind == Kind::Number) return v->num_val;
        return def;
    }

    bool get_bool(const std::string& key, bool def = false) const {
        const JsonValue* v = get(key);
        if (!v) return def;
        if (v->kind == Kind::Bool) return v->bool_val;
        return def;
    }
};

namespace detail {

class Json5Parser {
public:
    explicit Json5Parser(std::string_view src) : src_(src) {}

    std::optional<JsonValue> parse(std::string* error) {
        skip_ws_and_comments();
        auto val = parse_value(0);
        if (!val) {
            if (error) *error = err_.empty() ? "invalid JSON5 syntax" : err_;
            return std::nullopt;
        }
        skip_ws_and_comments();
        if (pos_ != src_.size()) {
            if (error) *error = "trailing characters after JSON5 document";
            return std::nullopt;
        }
        return val;
    }

private:
    static constexpr int kMaxDepth = 64;

    void skip_ws_and_comments() {
        while (pos_ < src_.size()) {
            const unsigned char c = static_cast<unsigned char>(src_[pos_]);
            if (std::isspace(c)) {
                ++pos_;
                continue;
            }
            if (pos_ + 1 < src_.size() && src_[pos_] == '/' && src_[pos_ + 1] == '/') {
                pos_ += 2;
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
                continue;
            }
            if (pos_ + 1 < src_.size() && src_[pos_] == '/' && src_[pos_ + 1] == '*') {
                pos_ += 2;
                while (pos_ + 1 < src_.size() &&
                       !(src_[pos_] == '*' && src_[pos_ + 1] == '/')) {
                    ++pos_;
                }
                if (pos_ + 1 < src_.size()) pos_ += 2;
                else pos_ = src_.size();
                continue;
            }
            break;
        }
    }

    std::optional<JsonValue> parse_value(int depth) {
        if (depth > kMaxDepth) {
            err_ = "JSON5 nesting limit exceeded";
            return std::nullopt;
        }
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            err_ = "unexpected end of JSON5 input";
            return std::nullopt;
        }
        const char c = src_[pos_];
        if (c == '{') return parse_object(depth + 1);
        if (c == '[') return parse_array(depth + 1);
        if (c == '"' || c == '\'') {
            auto s = parse_string();
            if (!s) return std::nullopt;
            JsonValue v;
            v.kind = JsonValue::Kind::String;
            v.str_val = std::move(*s);
            return v;
        }
        if (c == 't' && src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            JsonValue v;
            v.kind = JsonValue::Kind::Bool;
            v.bool_val = true;
            return v;
        }
        if (c == 'f' && src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            JsonValue v;
            v.kind = JsonValue::Kind::Bool;
            v.bool_val = false;
            return v;
        }
        if (c == 'n' && src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            JsonValue v;
            v.kind = JsonValue::Kind::Null;
            return v;
        }
        if (c == '-' || c == '+' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number();
        }
        err_ = std::string("unexpected JSON5 token starting with '") + c + "'";
        return std::nullopt;
    }

    std::optional<std::string> parse_string() {
        const char quote = src_[pos_++];
        std::string out;
        while (pos_ < src_.size()) {
            const char c = src_[pos_++];
            if (c == quote) return out;
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    err_ = "unterminated escape sequence in string";
                    return std::nullopt;
                }
                const char esc = src_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\'': out.push_back('\''); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case '\n': break;  // JSON5 line continuation
                    case 'u': {
                        if (pos_ + 4 > src_.size()) {
                            err_ = "truncated \\u escape in JSON5 string";
                            return std::nullopt;
                        }
                        std::uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<std::uint32_t>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<std::uint32_t>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<std::uint32_t>(h - 'A' + 10);
                            else {
                                err_ = "invalid hex digit in \\u escape";
                                return std::nullopt;
                            }
                        }
                        if (cp < 0x80u) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800u) {
                            out.push_back(static_cast<char>(0xc0u | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
                        } else {
                            out.push_back(static_cast<char>(0xe0u | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
                            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        err_ = "unterminated JSON5 string";
        return std::nullopt;
    }

    std::optional<std::string> parse_key() {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            err_ = "expected object key";
            return std::nullopt;
        }
        if (src_[pos_] == '"' || src_[pos_] == '\'') return parse_string();
        const std::size_t start = pos_;
        while (pos_ < src_.size()) {
            const unsigned char c = static_cast<unsigned char>(src_[pos_]);
            if (std::isalnum(c) || c == '_' || c == '$') {
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) {
            err_ = "invalid unquoted object key";
            return std::nullopt;
        }
        return std::string(src_.substr(start, pos_ - start));
    }

    std::optional<JsonValue> parse_number() {
        const std::size_t start = pos_;
        if (pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) ++pos_;
        if (pos_ + 1 < src_.size() && src_[pos_] == '0' &&
            (src_[pos_ + 1] == 'x' || src_[pos_ + 1] == 'X')) {
            pos_ += 2;
            while (pos_ < src_.size() &&
                   std::isxdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
            const std::string tok(src_.substr(start, pos_ - start));
            char* end = nullptr;
            const long long val = std::strtoll(tok.c_str(), &end, 16);
            if (!end || end == tok.c_str()) {
                err_ = "invalid hex number";
                return std::nullopt;
            }
            JsonValue v;
            v.kind = JsonValue::Kind::Number;
            v.num_val = static_cast<double>(val);
            return v;
        }
        while (pos_ < src_.size() &&
               std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            while (pos_ < src_.size() &&
                   std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) ++pos_;
            while (pos_ < src_.size() &&
                   std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }
        const std::string tok(src_.substr(start, pos_ - start));
        char* end = nullptr;
        const double num = std::strtod(tok.c_str(), &end);
        if (!end || end == tok.c_str() || !std::isfinite(num)) {
            err_ = "invalid JSON5 number: " + tok;
            return std::nullopt;
        }
        JsonValue v;
        v.kind = JsonValue::Kind::Number;
        v.num_val = num;
        return v;
    }

    std::optional<JsonValue> parse_array(int depth) {
        ++pos_;  // '['
        JsonValue arr;
        arr.kind = JsonValue::Kind::Array;
        while (true) {
            skip_ws_and_comments();
            if (pos_ >= src_.size()) {
                err_ = "unterminated JSON5 array";
                return std::nullopt;
            }
            if (src_[pos_] == ']') {
                ++pos_;
                return arr;
            }
            auto elem = parse_value(depth);
            if (!elem) return std::nullopt;
            arr.array_val.push_back(std::move(*elem));
            skip_ws_and_comments();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < src_.size() && src_[pos_] == ']') {
                ++pos_;
                return arr;
            }
            err_ = "expected ',' or ']' in JSON5 array";
            return std::nullopt;
        }
    }

    std::optional<JsonValue> parse_object(int depth) {
        ++pos_;  // '{'
        JsonValue obj;
        obj.kind = JsonValue::Kind::Object;
        while (true) {
            skip_ws_and_comments();
            if (pos_ >= src_.size()) {
                err_ = "unterminated JSON5 object";
                return std::nullopt;
            }
            if (src_[pos_] == '}') {
                ++pos_;
                return obj;
            }
            auto key = parse_key();
            if (!key) return std::nullopt;
            skip_ws_and_comments();
            if (pos_ >= src_.size() || src_[pos_] != ':') {
                err_ = "expected ':' after object key";
                return std::nullopt;
            }
            ++pos_;
            auto val = parse_value(depth);
            if (!val) return std::nullopt;
            obj.object_val[std::move(*key)] = std::move(*val);
            skip_ws_and_comments();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < src_.size() && src_[pos_] == '}') {
                ++pos_;
                return obj;
            }
            err_ = "expected ',' or '}' in JSON5 object";
            return std::nullopt;
        }
    }

    std::string_view src_;
    std::size_t pos_ = 0;
    std::string err_;
};

}  // namespace detail

inline std::optional<JsonValue> parse_json5(std::string_view text,
                                            std::string* error = nullptr) {
    // Strip optional UTF-8 BOM
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xefu &&
        static_cast<unsigned char>(text[1]) == 0xbbu &&
        static_cast<unsigned char>(text[2]) == 0xbfu) {
        text.remove_prefix(3);
    }
    detail::Json5Parser parser(text);
    return parser.parse(error);
}

}  // namespace finn::ohos

#endif
