#include "ohos/abc_runtime.hpp"
#include "ohos/arkui_declarative.hpp"
#include "ohos/arkui_bytecode_ui.hpp"

#include <dlfcn.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

struct finn_napi_value__ {
    finn::ohos::AbcValue val;
};

struct finn_napi_callback_info__ {
    finn::ohos::AbcValue this_val;
    std::vector<finn::ohos::AbcValue> args;
    void* data = nullptr;
};

struct finn_napi_env__ {
    finn::ohos::AbcRuntime* runtime = nullptr;
    std::vector<std::unique_ptr<finn_napi_value__>> arena;
    std::vector<napi_module*> registered_modules;
    std::string last_error;

    napi_value alloc(finn::ohos::AbcValue v) {
        auto ptr = std::make_unique<finn_napi_value__>();
        ptr->val = std::move(v);
        napi_value raw = ptr.get();
        arena.push_back(std::move(ptr));
        return raw;
    }
};

namespace {

thread_local std::vector<napi_module*>* g_active_napi_reg_sink = nullptr;
std::vector<napi_module*> g_global_napi_modules;

}  // namespace

extern "C" {

void napi_module_register(napi_module* mod) {
    if (!mod) return;
    if (g_active_napi_reg_sink) {
        g_active_napi_reg_sink->push_back(mod);
    } else {
        g_global_napi_modules.push_back(mod);
    }
}

napi_status napi_create_string_utf8(napi_env env, const char* str,
                                    std::size_t length, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    std::string s;
    if (str) {
        if (length == NAPI_AUTO_LENGTH) s.assign(str);
        else s.assign(str, length);
    }
    *result = env->alloc(finn::ohos::AbcValue::make_string(std::move(s)));
    return napi_ok;
}

napi_status napi_create_int32(napi_env env, std::int32_t value,
                              napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_int(value));
    return napi_ok;
}

napi_status napi_create_double(napi_env env, double value, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_double(value));
    return napi_ok;
}

napi_status napi_create_object(napi_env env, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_object("NapiObject"));
    return napi_ok;
}

napi_status napi_set_named_property(napi_env env, napi_value object,
                                    const char* utf8name, napi_value value) {
    if (!env || !object || !utf8name || !value) return napi_invalid_arg;
    if (object->val.kind != finn::ohos::AbcValue::Kind::Object || !object->val.obj_val) {
        return napi_object_expected;
    }
    object->val.obj_val->properties[utf8name] = value->val;
    return napi_ok;
}

napi_status napi_get_named_property(napi_env env, napi_value object,
                                    const char* utf8name, napi_value* result) {
    if (!env || !object || !utf8name || !result) return napi_invalid_arg;
    if (object->val.kind != finn::ohos::AbcValue::Kind::Object || !object->val.obj_val) {
        return napi_object_expected;
    }
    const auto it = object->val.obj_val->properties.find(utf8name);
    if (it == object->val.obj_val->properties.end()) {
        *result = env->alloc(finn::ohos::AbcValue::make_undefined());
    } else {
        *result = env->alloc(it->second);
    }
    return napi_ok;
}

napi_status napi_define_properties(napi_env env, napi_value object,
                                   std::size_t property_count,
                                   const napi_property_descriptor* properties) {
    if (!env || !object || (!properties && property_count > 0)) {
        return napi_invalid_arg;
    }
    if (object->val.kind != finn::ohos::AbcValue::Kind::Object || !object->val.obj_val) {
        return napi_object_expected;
    }
    for (std::size_t i = 0; i < property_count; ++i) {
        const auto& prop = properties[i];
        std::string pname;
        if (prop.utf8name) pname = prop.utf8name;
        else if (prop.name) pname = prop.name->val.to_string();
        if (pname.empty()) continue;

        if (prop.method) {
            napi_callback cb = prop.method;
            void* user_data = prop.data;
            object->val.obj_val->properties[pname] = finn::ohos::AbcValue::make_native(
                [env, cb, user_data](finn::ohos::AbcRuntime&,
                                     const finn::ohos::AbcValue& this_val,
                                     const std::vector<finn::ohos::AbcValue>& args) {
                    finn_napi_callback_info__ info;
                    info.this_val = this_val;
                    info.args = args;
                    info.data = user_data;
                    napi_value rv = cb(env, &info);
                    return rv ? rv->val : finn::ohos::AbcValue::make_undefined();
                });
        } else if (prop.value) {
            object->val.obj_val->properties[pname] = prop.value->val;
        }
    }
    return napi_ok;
}

napi_status napi_get_cb_info(napi_env env, napi_callback_info cbinfo,
                             std::size_t* argc, napi_value* argv,
                             napi_value* this_arg, void** data) {
    if (!env || !cbinfo) return napi_invalid_arg;
    if (argc) {
        const std::size_t cap = *argc;
        const std::size_t n = std::min(cap, cbinfo->args.size());
        if (argv) {
            for (std::size_t i = 0; i < n; ++i) {
                argv[i] = env->alloc(cbinfo->args[i]);
            }
            for (std::size_t i = n; i < cap; ++i) {
                argv[i] = env->alloc(finn::ohos::AbcValue::make_undefined());
            }
        }
        *argc = cbinfo->args.size();
    }
    if (this_arg) *this_arg = env->alloc(cbinfo->this_val);
    if (data) *data = cbinfo->data;
    return napi_ok;
}

napi_status napi_get_value_int32(napi_env env, napi_value value,
                                 std::int32_t* result) {
    if (!env || !value || !result) return napi_invalid_arg;
    *result = value->val.to_int32();
    return napi_ok;
}

napi_status napi_get_value_double(napi_env env, napi_value value,
                                  double* result) {
    if (!env || !value || !result) return napi_invalid_arg;
    *result = value->val.to_number();
    return napi_ok;
}

napi_status napi_get_value_string_utf8(napi_env env, napi_value value,
                                       char* buf, std::size_t bufsize,
                                       std::size_t* result) {
    if (!env || !value) return napi_invalid_arg;
    const std::string s = value->val.to_string();
    if (!buf) {
        if (result) *result = s.size();
        return napi_ok;
    }
    const std::size_t copy_len = (bufsize > 0) ? std::min(s.size(), bufsize - 1) : 0;
    if (copy_len > 0) std::memcpy(buf, s.data(), copy_len);
    if (bufsize > 0) buf[copy_len] = '\0';
    if (result) *result = copy_len;
    return napi_ok;
}

napi_status napi_get_undefined(napi_env env, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_undefined());
    return napi_ok;
}

napi_status napi_get_null(napi_env env, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_null());
    return napi_ok;
}

napi_status napi_get_boolean(napi_env env, bool value, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_bool(value));
    return napi_ok;
}

napi_status napi_typeof(napi_env env, napi_value value, napi_valuetype* result) {
    if (!env || !value || !result) return napi_invalid_arg;
    using Kind = finn::ohos::AbcValue::Kind;
    switch (value->val.kind) {
        case Kind::Undefined: *result = napi_undefined; break;
        case Kind::Null: *result = napi_null; break;
        case Kind::Bool: *result = napi_boolean; break;
        case Kind::Int:
        case Kind::Double: *result = napi_number; break;
        case Kind::String: *result = napi_string; break;
        case Kind::Object: *result = napi_object; break;
        case Kind::NativeFunction:
        case Kind::AbcFunction: *result = napi_function; break;
    }
    return napi_ok;
}

napi_status napi_create_int64(napi_env env, std::int64_t value, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_int(value));
    return napi_ok;
}

napi_status napi_get_value_int64(napi_env env, napi_value value,
                                 std::int64_t* result) {
    if (!env || !value || !result) return napi_invalid_arg;
    *result = value->val.to_int64();
    return napi_ok;
}

napi_status napi_create_uint32(napi_env env, std::uint32_t value, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_int(static_cast<std::int64_t>(value)));
    return napi_ok;
}

napi_status napi_get_value_bool(napi_env env, napi_value value, bool* result) {
    if (!env || !value || !result) return napi_invalid_arg;
    *result = value->val.is_truthy();
    return napi_ok;
}

napi_status napi_create_array(napi_env env, napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    *result = env->alloc(finn::ohos::AbcValue::make_array());
    return napi_ok;
}

napi_status napi_create_array_with_length(napi_env env, std::size_t length,
                                          napi_value* result) {
    if (!env || !result) return napi_invalid_arg;
    // Cap a host-allotted dense array so a hostile module cannot force an
    // unbounded allocation; larger requests fail honestly.
    if (length > (1u << 20u)) return napi_invalid_arg;
    auto arr = finn::ohos::AbcValue::make_array();
    arr.obj_val->array_val.assign(length, finn::ohos::AbcValue::make_undefined());
    *result = env->alloc(std::move(arr));
    return napi_ok;
}

napi_status napi_is_array(napi_env env, napi_value value, bool* result) {
    if (!env || !value || !result) return napi_invalid_arg;
    *result = value->val.kind == finn::ohos::AbcValue::Kind::Object &&
              value->val.obj_val && value->val.obj_val->class_name == "Array";
    return napi_ok;
}

napi_status napi_get_array_length(napi_env env, napi_value value,
                                  std::uint32_t* result) {
    if (!env || !value || !result) return napi_invalid_arg;
    if (value->val.kind != finn::ohos::AbcValue::Kind::Object ||
        !value->val.obj_val || value->val.obj_val->class_name != "Array") {
        return napi_object_expected;
    }
    *result = static_cast<std::uint32_t>(value->val.obj_val->array_val.size());
    return napi_ok;
}

napi_status napi_set_element(napi_env env, napi_value object,
                             std::uint32_t index, napi_value value) {
    if (!env || !object || !value) return napi_invalid_arg;
    if (object->val.kind != finn::ohos::AbcValue::Kind::Object ||
        !object->val.obj_val) {
        return napi_object_expected;
    }
    if (index >= (1u << 20u)) return napi_invalid_arg;
    auto& cells = object->val.obj_val->array_val;
    if (static_cast<std::size_t>(index) >= cells.size()) {
        cells.resize(static_cast<std::size_t>(index) + 1,
                     finn::ohos::AbcValue::make_undefined());
    }
    cells[index] = value->val;
    return napi_ok;
}

napi_status napi_get_element(napi_env env, napi_value object,
                             std::uint32_t index, napi_value* result) {
    if (!env || !object || !result) return napi_invalid_arg;
    if (object->val.kind != finn::ohos::AbcValue::Kind::Object ||
        !object->val.obj_val) {
        return napi_object_expected;
    }
    const auto& cells = object->val.obj_val->array_val;
    *result = (index < cells.size())
                  ? env->alloc(cells[index])
                  : env->alloc(finn::ohos::AbcValue::make_undefined());
    return napi_ok;
}

napi_status napi_throw_error(napi_env env, const char* code, const char* message) {
    if (!env) return napi_invalid_arg;
    env->last_error = std::string(code ? code : "Error") + ": " +
                      std::string(message ? message : "unknown error");
    return napi_ok;
}

}  // extern "C"

namespace finn::ohos {
namespace {

std::string default_ohos_root() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.local/share/finn/ohos";
    }
    return "/tmp/finn-ohos";
}

std::string read_mutf8_at(const std::vector<std::uint8_t>& buf,
                          std::size_t* pos) {
    std::uint32_t utf16_tag = 0;
    if (!read_uleb128(buf.data(), buf.size(), pos, &utf16_tag)) return {};
    const std::size_t start = *pos;
    std::size_t end = start;
    while (end < buf.size() && buf[end] != 0u) ++end;
    std::string out(reinterpret_cast<const char*>(buf.data() + start), end - start);
    *pos = (end < buf.size()) ? (end + 1) : buf.size();
    return out;
}

void push_unique_string(std::vector<std::string>* list, const std::string& s) {
    if (s.empty()) return;
    if (std::find(list->begin(), list->end(), s) == list->end()) {
        list->push_back(s);
    }
}

std::string format_hilog(const std::string& fmt,
                         const std::vector<AbcValue>& args,
                         std::size_t first_arg) {
    std::string out;
    std::size_t arg_idx = first_arg;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size()) {
            if (fmt[i + 1] == '%') {
                out.push_back('%');
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            if (fmt[j] == '{') {
                const std::size_t close = fmt.find('}', j + 1);
                if (close != std::string::npos && close + 1 < fmt.size()) {
                    j = close + 1;
                }
            }
            const char spec = fmt[j];
            if (spec == 's' || spec == 'd' || spec == 'i' || spec == 'f' ||
                spec == 'x' || spec == 'o') {
                if (arg_idx < args.size()) {
                    out += args[arg_idx++].to_string();
                }
                i = j;
                continue;
            }
        }
        out.push_back(fmt[i]);
    }
    return out;
}

std::string sanitize_conf_field(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n' || c == '\r') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

// RFC 4648 base64 over the raw bytes of a UTF-8 string. Decoding is strict:
// invalid alphabet characters or a bad length produce an empty result rather
// than a plausible-looking guess.
std::string base64_encode_bytes(std::string_view in) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size()) {
        const std::uint32_t v = (static_cast<std::uint8_t>(in[i]) << 16) |
                                (static_cast<std::uint8_t>(in[i + 1]) << 8) |
                                static_cast<std::uint8_t>(in[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3fu]);
        out.push_back(kAlphabet[(v >> 12) & 0x3fu]);
        out.push_back(kAlphabet[(v >> 6) & 0x3fu]);
        out.push_back(kAlphabet[v & 0x3fu]);
        i += 3;
    }
    const std::size_t rem = in.size() - i;
    if (rem == 1) {
        const std::uint32_t v = static_cast<std::uint8_t>(in[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 0x3fu]);
        out.push_back(kAlphabet[(v >> 12) & 0x3fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const std::uint32_t v = (static_cast<std::uint8_t>(in[i]) << 16) |
                                (static_cast<std::uint8_t>(in[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 0x3fu]);
        out.push_back(kAlphabet[(v >> 12) & 0x3fu]);
        out.push_back(kAlphabet[(v >> 6) & 0x3fu]);
        out.push_back('=');
    }
    return out;
}

std::string base64_decode_bytes(std::string_view in) {
    auto sextet = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    if (in.size() % 4 != 0) return {};
    std::string out;
    out.reserve((in.size() / 4) * 3);
    for (std::size_t i = 0; i < in.size(); i += 4) {
        const bool pad2 = (in[i + 2] == '=');
        const bool pad1 = (in[i + 3] == '=');
        if (pad2 && !pad1) return {};  // '=' in position 3 requires one in position 4
        const int a = sextet(in[i]);
        const int b = sextet(in[i + 1]);
        const int c = pad2 ? 0 : sextet(in[i + 2]);
        const int d = pad1 ? 0 : sextet(in[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return {};
        // Canonical RFC 4648: unused low bits of the final sextet(s) must be 0.
        if (pad1 && (c & 0x03u) != 0) return {};
        if (pad2 && (b & 0x0fu) != 0) return {};
        const std::uint32_t v = (static_cast<std::uint32_t>(a) << 18) |
                                (static_cast<std::uint32_t>(b) << 12) |
                                (static_cast<std::uint32_t>(c) << 6) |
                                static_cast<std::uint32_t>(d);
        out.push_back(static_cast<char>((v >> 16) & 0xffu));
        if (!pad2) out.push_back(static_cast<char>((v >> 8) & 0xffu));
        if (!pad1) out.push_back(static_cast<char>(v & 0xffu));
    }
    return out;
}

std::string generate_uuid_v4() {
    std::random_device rd;
    std::mt19937_64 gen((static_cast<std::uint64_t>(rd()) << 32) ^ rd());
    std::uniform_int_distribution<std::uint16_t> byte_dist(0, 0xffu);
    std::uint8_t b[16] = {};
    for (auto& x : b) x = static_cast<std::uint8_t>(byte_dist(gen));
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0fu) | 0x40u);  // version 4
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3fu) | 0x80u);  // variant RFC4122
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kHex[(b[i] >> 4) & 0x0fu]);
        out.push_back(kHex[b[i] & 0x0fu]);
    }
    return out;
}

// Reads a Panda ES module record: [literalnum:u32][numRequests:u32]
// [request string ids:u32...][numRegularImports:u32]
// [localName:u32, importName:u32, requestIdx:u16 ...]. The trailing export
// sections are not needed to resolve imported bindings.
std::optional<AbcModuleRecord> parse_module_record_at(
    const std::vector<std::uint8_t>& buf, std::size_t pos) {
    const std::size_t size = buf.size();
    auto read_u32 = [&](std::uint32_t* out) -> bool {
        if (pos + 4 > size) return false;
        *out = read_le32(buf.data() + pos);
        pos += 4;
        return true;
    };
    auto read_str = [&](std::uint32_t id, std::string* out) -> bool {
        if (id == 0 || id >= size) return false;
        std::size_t p = id;
        std::string s = read_mutf8_at(buf, &p);
        if (s.empty()) return false;
        *out = std::move(s);
        return true;
    };

    std::uint32_t literal_num = 0;
    if (!read_u32(&literal_num)) return std::nullopt;
    (void)literal_num;
    std::uint32_t num_requests = 0;
    if (!read_u32(&num_requests) || num_requests == 0 || num_requests > 512) {
        return std::nullopt;
    }
    std::vector<std::string> requests;
    for (std::uint32_t i = 0; i < num_requests; ++i) {
        std::uint32_t id = 0;
        std::string s;
        if (!read_u32(&id) || !read_str(id, &s) || s.empty() || s.front() != '@') {
            return std::nullopt;
        }
        requests.push_back(std::move(s));
    }

    std::uint32_t num_imports = 0;
    if (!read_u32(&num_imports) || num_imports > 8192) return std::nullopt;
    AbcModuleRecord record;
    for (std::uint32_t i = 0; i < num_imports; ++i) {
        std::uint32_t local_id = 0;
        std::uint32_t import_id = 0;
        if (!read_u32(&local_id) || !read_u32(&import_id)) return std::nullopt;
        if (pos + 2 > size) return std::nullopt;
        const std::uint16_t req_idx = read_le16(buf.data() + pos);
        pos += 2;
        if (req_idx >= requests.size()) return std::nullopt;
        AbcModuleImport imp;
        if (!read_str(local_id, &imp.local_name) ||
            !read_str(import_id, &imp.import_name)) {
            return std::nullopt;
        }
        imp.module_request = requests[req_idx];
        record.imports.push_back(std::move(imp));
    }
    if (record.imports.empty()) return std::nullopt;
    return record;
}

// Minimal but real ECMAScript value helpers used by compiled ArkTS code.
AbcValue json_to_value(const JsonValue& j) {
    switch (j.kind) {
        case JsonValue::Kind::Null: return AbcValue::make_null();
        case JsonValue::Kind::Bool: return AbcValue::make_bool(j.bool_val);
        case JsonValue::Kind::Number: return AbcValue::make_double(j.num_val);
        case JsonValue::Kind::String: return AbcValue::make_string(j.str_val);
        case JsonValue::Kind::Array: {
            AbcValue arr = AbcValue::make_array();
            for (const auto& item : j.array_val) {
                arr.obj_val->array_val.push_back(json_to_value(item));
            }
            return arr;
        }
        case JsonValue::Kind::Object: {
            AbcValue obj = AbcValue::make_object("Object");
            for (const auto& [k, v] : j.object_val) {
                obj.obj_val->properties[k] = json_to_value(v);
            }
            return obj;
        }
    }
    return AbcValue::make_undefined();
}

std::string value_to_json(const AbcValue& v, int depth = 0) {
    if (depth > 32) return "null";
    switch (v.kind) {
        case AbcValue::Kind::Undefined: return "null";
        case AbcValue::Kind::Null: return "null";
        case AbcValue::Kind::Bool: return v.bool_val ? "true" : "false";
        case AbcValue::Kind::Int: return std::to_string(v.int_val);
        case AbcValue::Kind::Double: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", v.double_val);
            return buf;
        }
        case AbcValue::Kind::String: {
            std::string out = "\"";
            for (unsigned char c : v.str_val) {
                if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else out.push_back(static_cast<char>(c));
            }
            out.push_back('"');
            return out;
        }
        case AbcValue::Kind::Object: {
            if (v.obj_val && v.obj_val->class_name == "Array") {
                std::string out = "[";
                for (std::size_t i = 0; i < v.obj_val->array_val.size(); ++i) {
                    if (i) out += ',';
                    out += value_to_json(v.obj_val->array_val[i], depth + 1);
                }
                out.push_back(']');
                return out;
            }
            std::string out = "{";
            bool first = true;
            if (v.obj_val) {
                for (const auto& [k, val] : v.obj_val->properties) {
                    if (!first) out += ',';
                    first = false;
                    out += value_to_json(AbcValue::make_string(k));
                    out += ':';
                    out += value_to_json(val, depth + 1);
                }
            }
            out.push_back('}');
            return out;
        }
        default: return "null";
    }
}

bool is_supported_kit_specifier(std::string_view spec) {
    static constexpr std::string_view kSupported[] = {
        "@ohos.hilog",
        "@ohos.app.ability.UIAbility",
        "@ohos.app.ability.AbilityConstant",
        "@ohos.app.ability.Want",
        "@ohos.window",
        "@ohos.router",
        "@ohos.data.preferences",
        "@ohos.file.fs",
        "@ohos.deviceInfo",
        "@ohos.util",
        "@ohos.bundle.bundleManager",
        "@ohos.bundle",
        "@ohos.app",
        "@ohos.curves",
        "@ohos.matrix4",
        "@kit.ArkUI",
        "@kit.PerformanceAnalysisKit",
        "@kit.BasicServicesKit",
        "@kit.CoreFileKit",
        "@kit.ArkData",
        "@kit.AbilityKit",
    };
    for (const auto& s : kSupported) {
        if (spec == s) return true;
    }
    return false;
}

bool is_known_unsupported_kit(std::string_view spec) {
    static constexpr std::string_view kUnsupportedPrefixes[] = {
        "@kit.ArkWeb",
        "@kit.DistributedDataObject",
        "@kit.DistributedServiceKit",
        "@kit.TelephonyKit",
        "@kit.ConnectivityKit",
        "@kit.SensorServiceKit",
        "@kit.LocationKit",
        "@kit.CameraKit",
        "@kit.MediaKit",
        "@kit.AudioKit",
        "@kit.BluetoothKit",
        "@kit.NearLinkKit",
        "@kit.ShareKit",
        "@kit.AccountKit",
        "@kit.MapKit",
        "@kit.PushKit",
        "@kit.PaymentKit",
        "@kit.CryptoArchitectureKit",
        "@kit.GameControllerKit",
        "@kit.ArkGraphics2D",
        "@kit.ArkGraphics3D",
        "@ohos.web.webview",
        "@ohos.distributedDataObject",
        "@ohos.distributedKVStore",
        "@ohos.telephony",
        "@ohos.nfc",
        "@ohos.sensor",
        "@ohos.geoLocationManager",
        "@ohos.multimedia",
        "@ohos.bluetooth",
        "@ohos.wifiManager",
    };
    for (const auto& p : kUnsupportedPrefixes) {
        if (spec.rfind(p, 0) == 0) return true;
    }
    return false;
}

void scan_kit_tokens(std::string_view text,
                     std::set<std::string>* supported,
                     std::set<std::string>* unsupported,
                     std::set<std::string>* unknown) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t at = text.find('@', pos);
        if (at == std::string_view::npos) break;
        if (text.substr(at, 5) == "@kit." || text.substr(at, 6) == "@ohos.") {
            std::size_t end = at + 1;
            while (end < text.size()) {
                const unsigned char c = static_cast<unsigned char>(text[end]);
                if (std::isalnum(c) || c == '.' || c == '_' || c == '-') ++end;
                else break;
            }
            const std::string token(text.substr(at, end - at));
            if (is_known_unsupported_kit(token)) {
                unsupported->insert(token);
            } else if (is_supported_kit_specifier(token)) {
                supported->insert(token);
            } else {
                unknown->insert(token);
            }
            pos = end;
        } else {
            pos = at + 1;
        }
    }
}

}  // namespace

std::uint32_t compute_panda_adler32(const std::uint8_t* data, std::size_t size) {
    if (!data || size <= 12) return 0;
    const uLong init = ::adler32(0L, Z_NULL, 0);
    return static_cast<std::uint32_t>(
        ::adler32(init, data + 12, static_cast<uInt>(size - 12)));
}

std::string AbcFile::read_mutf8_string(std::uint32_t offset) const {
    if (offset >= raw_bytes.size()) return {};
    std::size_t pos = offset;
    return read_mutf8_at(raw_bytes, &pos);
}

std::string AbcFile::resolve_id16_string(std::uint32_t method_offset,
                                         std::uint16_t id16) const {
    for (const auto& reg : index_regions) {
        if (method_offset >= reg.start && method_offset < reg.end) {
            if (id16 < reg.method_idx.size()) {
                const std::string s = read_mutf8_string(reg.method_idx[id16]);
                if (!s.empty()) return s;
            }
            if (id16 < reg.field_idx.size()) {
                const std::string s = read_mutf8_string(reg.field_idx[id16]);
                if (!s.empty()) return s;
            }
            if (id16 < reg.class_idx.size()) {
                const std::string s = read_mutf8_string(reg.class_idx[id16]);
                if (!s.empty()) return s;
            }
        }
    }
    // Fallback when an index region covers the whole file or wasn't ranged narrowly
    for (const auto& reg : index_regions) {
        if (id16 < reg.method_idx.size()) {
            const std::string s = read_mutf8_string(reg.method_idx[id16]);
            if (!s.empty()) return s;
        }
    }
    // Direct offset fallback if file has no index headers
    return read_mutf8_string(id16);
}

const AbcMethod* AbcFile::find_method(std::string_view name) const {
    for (const auto& cls : classes) {
        for (const auto& m : cls.methods) {
            if (m.name == name) return &m;
        }
    }
    return nullptr;
}

const AbcMethod* AbcFile::find_class_method(std::string_view class_name,
                                            std::string_view method_name) const {
    for (const auto& cls : classes) {
        if (cls.name == class_name ||
            cls.name == ("L" + std::string(class_name) + ";")) {
            for (const auto& m : cls.methods) {
                if (m.name == method_name) return &m;
            }
        }
    }
    return nullptr;
}

AbcFile parse_abc_bytes(const std::uint8_t* data, std::size_t size) {
    AbcFile file;
    if (!data || size < kPandaHeaderSize) {
        file.error = "file smaller than 60-byte Panda .abc header";
        return file;
    }
    if (std::memcmp(data, kPandaMagic, 8) != 0) {
        file.error = "missing Panda .abc magic (expected PANDA\\0\\0\\0)";
        return file;
    }

    file.raw_bytes.assign(data, data + size);
    AbcHeader& h = file.header;
    h.checksum = read_le32(data + 8);
    std::memcpy(h.version, data + 12, 4);
    h.version_string = std::to_string(h.version[0]) + "." +
                       std::to_string(h.version[1]) + "." +
                       std::to_string(h.version[2]) + "." +
                       std::to_string(h.version[3]);
    h.file_size = read_le32(data + 16);
    h.foreign_off = read_le32(data + 20);
    h.foreign_size = read_le32(data + 24);
    h.num_classes = read_le32(data + 28);
    h.class_idx_off = read_le32(data + 32);
    h.num_lnps = read_le32(data + 36);
    h.lnp_idx_off = read_le32(data + 40);
    h.num_literalarrays = read_le32(data + 44);
    h.literalarray_idx_off = read_le32(data + 48);
    h.num_indexes = read_le32(data + 52);
    h.index_section_off = read_le32(data + 56);

    if (h.file_size < kPandaHeaderSize || h.file_size > size) {
        file.error = "Panda .abc header file_size exceeds available bytes";
        return file;
    }

    const std::uint32_t expected_adler = compute_panda_adler32(data, h.file_size);
    h.checksum_valid = (expected_adler == h.checksum);
    h.valid = true;

    // Parse index headers (40 bytes each)
    if (h.num_indexes > 0 &&
        static_cast<std::uint64_t>(h.index_section_off) +
                static_cast<std::uint64_t>(h.num_indexes) * kPandaIndexHeaderSize <=
            h.file_size) {
        for (std::uint32_t i = 0; i < h.num_indexes; ++i) {
            const std::size_t off = h.index_section_off + i * kPandaIndexHeaderSize;
            AbcIndexRegion reg;
            reg.start = read_le32(data + off + 0);
            reg.end = read_le32(data + off + 4);
            const auto load_u32_table = [&](std::uint32_t count, std::uint32_t tbl_off,
                                            std::vector<std::uint32_t>* dst) {
                if (count == 0 ||
                    static_cast<std::uint64_t>(tbl_off) +
                            static_cast<std::uint64_t>(count) * 4u >
                        h.file_size) {
                    return;
                }
                dst->reserve(count);
                for (std::uint32_t k = 0; k < count; ++k) {
                    dst->push_back(read_le32(data + tbl_off + k * 4u));
                }
            };
            load_u32_table(read_le32(data + off + 8), read_le32(data + off + 12), &reg.class_idx);
            load_u32_table(read_le32(data + off + 16), read_le32(data + off + 20), &reg.method_idx);
            load_u32_table(read_le32(data + off + 24), read_le32(data + off + 28), &reg.field_idx);
            load_u32_table(read_le32(data + off + 32), read_le32(data + off + 36), &reg.proto_idx);

            for (std::uint32_t str_off : reg.method_idx) {
                const std::string s = file.read_mutf8_string(str_off);
                if (!s.empty() && s.size() < 512) push_unique_string(&file.strings, s);
            }
            file.index_regions.push_back(std::move(reg));
        }
    }

    // Parse class index and class/method descriptors
    if (h.num_classes > 0 &&
        static_cast<std::uint64_t>(h.class_idx_off) +
                static_cast<std::uint64_t>(h.num_classes) * 4u <=
            h.file_size) {
        for (std::uint32_t ci = 0; ci < h.num_classes; ++ci) {
            const std::uint32_t cls_off = read_le32(data + h.class_idx_off + ci * 4u);
            if (cls_off >= h.file_size) continue;

            AbcClass cls;
            cls.offset = cls_off;
            std::size_t pos = cls_off;
            cls.name = read_mutf8_at(file.raw_bytes, &pos);
            push_unique_string(&file.strings, cls.name);

            if (h.foreign_size > 0 && cls_off >= h.foreign_off &&
                cls_off < h.foreign_off + h.foreign_size) {
                cls.is_foreign = true;
                file.classes.push_back(std::move(cls));
                continue;
            }

            if (pos + 4 > h.file_size) {
                file.classes.push_back(std::move(cls));
                continue;
            }
            pos += 4;  // super_class_off
            std::uint32_t num_fields = 0;
            std::uint32_t num_methods = 0;
            if (!read_uleb128(data, h.file_size, &pos, &cls.access_flags) ||
                !read_uleb128(data, h.file_size, &pos, &num_fields) ||
                !read_uleb128(data, h.file_size, &pos, &num_methods)) {
                file.classes.push_back(std::move(cls));
                continue;
            }

            // Class tagged data
            while (pos < h.file_size) {
                const std::uint8_t tag = data[pos++];
                if (tag == panda_tag::kNothing) break;
                if (tag == panda_tag::kClassInterfaces) {
                    std::uint32_t cnt = 0;
                    if (!read_uleb128(data, h.file_size, &pos, &cnt)) break;
                    pos += static_cast<std::size_t>(cnt) * 2u;
                } else if (tag == panda_tag::kClassSourceLang) {
                    if (pos < h.file_size) ++pos;
                } else if (tag == panda_tag::kClassSourceFile) {
                    if (pos + 4 <= h.file_size) {
                        const std::uint32_t sf_off = read_le32(data + pos);
                        pos += 4;
                        cls.source_file = file.read_mutf8_string(sf_off);
                        push_unique_string(&file.strings, cls.source_file);
                    }
                } else if (tag >= panda_tag::kClassRuntimeAnnotation &&
                           tag <= panda_tag::kClassTypeAnnotation) {
                    pos += 4;
                } else {
                    break;
                }
            }

            // Fields
            for (std::uint32_t fi = 0; fi < num_fields && pos + 8 <= h.file_size; ++fi) {
                pos += 4;  // class_idx + type_idx
                const std::uint32_t name_off = read_le32(data + pos);
                pos += 4;
                std::uint32_t fflags = 0;
                if (!read_uleb128(data, h.file_size, &pos, &fflags)) break;
                const std::string fname = file.read_mutf8_string(name_off);
                cls.field_names.push_back(fname);
                push_unique_string(&file.strings, fname);
                while (pos < h.file_size) {
                    const std::uint8_t ftag = data[pos++];
                    if (ftag == panda_tag::kNothing) break;
                    if (ftag == panda_tag::kFieldIntValue) {
                        std::int32_t dummy = 0;
                        if (!read_sleb128(data, h.file_size, &pos, &dummy)) break;
                    } else if (ftag >= panda_tag::kFieldValue &&
                               ftag <= panda_tag::kFieldTypeAnnotation) {
                        pos += 4;
                    } else {
                        break;
                    }
                }
            }

            // Methods
            for (std::uint32_t mi = 0; mi < num_methods && pos + 8 <= h.file_size; ++mi) {
                AbcMethod m;
                m.class_name = cls.name;
                m.offset = static_cast<std::uint32_t>(pos);
                pos += 4;  // class_idx + proto_idx
                const std::uint32_t name_off = read_le32(data + pos);
                pos += 4;
                if (!read_uleb128(data, h.file_size, &pos, &m.access_flags)) break;
                m.name = file.read_mutf8_string(name_off);
                push_unique_string(&file.strings, m.name);

                while (pos < h.file_size) {
                    const std::uint8_t mtag = data[pos++];
                    if (mtag == panda_tag::kNothing) break;
                    if (mtag == panda_tag::kMethodCode) {
                        if (pos + 4 <= h.file_size) {
                            m.code_offset = read_le32(data + pos);
                            pos += 4;
                        }
                    } else if (mtag == panda_tag::kMethodSourceLang) {
                        if (pos < h.file_size) ++pos;
                    } else if (mtag == panda_tag::kMethodProfileSize) {
                        pos += 2;
                    } else if (mtag >= panda_tag::kMethodRuntimeAnnotation &&
                               mtag <= panda_tag::kMethodTypeAnnotation) {
                        pos += 4;
                    } else {
                        break;
                    }
                }

                // Parse Code item
                if (m.code_offset > 0 && m.code_offset < h.file_size) {
                    std::size_t cpos = m.code_offset;
                    std::uint32_t code_size = 0;
                    std::uint32_t tries_size = 0;
                    if (read_uleb128(data, h.file_size, &cpos, &m.num_vregs) &&
                        read_uleb128(data, h.file_size, &cpos, &m.num_args) &&
                        read_uleb128(data, h.file_size, &cpos, &code_size) &&
                        read_uleb128(data, h.file_size, &cpos, &tries_size)) {
                        if (cpos + code_size <= h.file_size) {
                            m.bytecode.assign(data + cpos, data + cpos + code_size);
                        }
                    }
                }

                cls.methods.push_back(std::move(m));
            }

            file.classes.push_back(std::move(cls));
        }
    }

    // Also scan printable ASCII runs in raw_bytes for @kit.* / @ohos.* imports
    std::string run;
    for (std::size_t i = kPandaHeaderSize; i < h.file_size; ++i) {
        const unsigned char c = data[i];
        if (c >= 0x20u && c <= 0x7eu) {
            run.push_back(static_cast<char>(c));
        } else {
            if (run.find("@kit.") != std::string::npos ||
                run.find("@ohos.") != std::string::npos) {
                push_unique_string(&file.strings, run);
            }
            run.clear();
        }
    }
    if (run.find("@kit.") != std::string::npos ||
        run.find("@ohos.") != std::string::npos) {
        push_unique_string(&file.strings, run);
    }

    // Locate ES module records by their validated structure. Each record's
    // regular-import list is the index space used by ldexternalmodulevar.
    for (std::size_t off = kPandaHeaderSize; off + 12 <= h.file_size; off += 4) {
        auto rec = parse_module_record_at(file.raw_bytes, off);
        if (!rec) continue;
        bool duplicate = false;
        for (const auto& existing : file.module_records) {
            if (existing.imports.size() == rec->imports.size() &&
                !existing.imports.empty() &&
                existing.imports.front().module_request ==
                    rec->imports.front().module_request &&
                existing.imports.front().local_name ==
                    rec->imports.front().local_name) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) file.module_records.push_back(std::move(*rec));
    }

    file.valid = true;
    return file;
}

AbcFile parse_abc_file(const std::string& path) {
    AbcFile f;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        f.error = "unable to open .abc file: " + path;
        return f;
    }
    const std::streamoff sz = in.tellg();
    if (sz <= 0) {
        f.error = ".abc file is empty";
        return f;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(buf.data()), sz);
    return parse_abc_bytes(buf.data(), buf.size());
}

KitAudit audit_ohos_kits(const std::vector<std::string>& strings) {
    std::set<std::string> sup;
    std::set<std::string> unsup;
    std::set<std::string> unknown;
    for (const auto& s : strings) {
        scan_kit_tokens(s, &sup, &unsup, &unknown);
    }
    KitAudit out;
    out.supported.assign(sup.begin(), sup.end());
    out.unsupported.assign(unsup.begin(), unsup.end());
    out.unknown.assign(unknown.begin(), unknown.end());
    return out;
}

// --- AbcValue ---
AbcValue AbcValue::make_undefined() {
    AbcValue v;
    v.kind = Kind::Undefined;
    return v;
}

AbcValue AbcValue::make_null() {
    AbcValue v;
    v.kind = Kind::Null;
    return v;
}

AbcValue AbcValue::make_bool(bool b) {
    AbcValue v;
    v.kind = Kind::Bool;
    v.bool_val = b;
    return v;
}

AbcValue AbcValue::make_int(std::int64_t n) {
    AbcValue v;
    v.kind = Kind::Int;
    v.int_val = n;
    return v;
}

AbcValue AbcValue::make_double(double d) {
    AbcValue v;
    v.kind = Kind::Double;
    v.double_val = d;
    return v;
}

AbcValue AbcValue::make_string(std::string s) {
    AbcValue v;
    v.kind = Kind::String;
    v.str_val = std::move(s);
    return v;
}

AbcValue AbcValue::make_object(std::string class_name) {
    AbcValue v;
    v.kind = Kind::Object;
    v.obj_val = std::make_shared<AbcObject>();
    v.obj_val->class_name = std::move(class_name);
    return v;
}

AbcValue AbcValue::make_array() {
    return make_object("Array");
}

AbcValue AbcValue::make_native(NativeFn fn) {
    AbcValue v;
    v.kind = Kind::NativeFunction;
    v.native_fn = std::move(fn);
    return v;
}

AbcValue AbcValue::make_abc_method(const AbcMethod* method) {
    AbcValue v;
    v.kind = Kind::AbcFunction;
    v.abc_method = method;
    return v;
}

bool AbcValue::is_truthy() const {
    switch (kind) {
        case Kind::Undefined:
        case Kind::Null: return false;
        case Kind::Bool: return bool_val;
        case Kind::Int: return int_val != 0;
        case Kind::Double: return double_val != 0.0 && !std::isnan(double_val);
        case Kind::String: return !str_val.empty();
        case Kind::Object:
        case Kind::NativeFunction:
        case Kind::AbcFunction: return true;
    }
    return false;
}

double AbcValue::to_number() const {
    switch (kind) {
        case Kind::Int: return static_cast<double>(int_val);
        case Kind::Double: return double_val;
        case Kind::Bool: return bool_val ? 1.0 : 0.0;
        case Kind::String: return std::strtod(str_val.c_str(), nullptr);
        case Kind::Null: return 0.0;
        case Kind::Undefined:
        case Kind::Object:
        case Kind::NativeFunction:
        case Kind::AbcFunction: break;
    }
    return 0.0;
}

std::int32_t AbcValue::to_int32() const {
    if (kind == Kind::Int) return static_cast<std::int32_t>(int_val);
    return static_cast<std::int32_t>(to_number());
}

std::int64_t AbcValue::to_int64() const {
    if (kind == Kind::Int) return int_val;
    if (kind == Kind::Double) return static_cast<std::int64_t>(double_val);
    if (kind == Kind::Bool) return bool_val ? 1 : 0;
    if (kind == Kind::String) return std::strtoll(str_val.c_str(), nullptr, 10);
    return 0;
}

std::string AbcValue::to_string() const {
    switch (kind) {
        case Kind::Undefined: return "undefined";
        case Kind::Null: return "null";
        case Kind::Bool: return bool_val ? "true" : "false";
        case Kind::Int: return std::to_string(int_val);
        case Kind::Double: {
            if (std::floor(double_val) == double_val && std::abs(double_val) < 1e12) {
                return std::to_string(static_cast<long long>(double_val));
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.12g", double_val);
            return buf;
        }
        case Kind::String: return str_val;
        case Kind::Object: return "[object " + (obj_val ? obj_val->class_name : "Object") + "]";
        case Kind::NativeFunction: return "[native function]";
        case Kind::AbcFunction: return "[abc method]";
    }
    return {};
}

bool AbcValue::equals(const AbcValue& other) const {
    if (kind == Kind::String && other.kind == Kind::String) {
        return str_val == other.str_val;
    }
    if ((kind == Kind::Int || kind == Kind::Double) &&
        (other.kind == Kind::Int || other.kind == Kind::Double)) {
        return to_number() == other.to_number();
    }
    if (kind != other.kind) return false;
    switch (kind) {
        case Kind::Undefined:
        case Kind::Null: return true;
        case Kind::Bool: return bool_val == other.bool_val;
        case Kind::Object: return obj_val == other.obj_val;
        case Kind::AbcFunction: return abc_method == other.abc_method;
        default: return false;
    }
}

// --- AbcRuntime ---
AbcRuntime::AbcRuntime() : napi_env_(std::make_unique<finn_napi_env__>()) {
    napi_env_->runtime = this;
    install_builtin_modules();
}

AbcRuntime::~AbcRuntime() {
    for (void* h : dl_handles_) {
        if (h) ::dlclose(h);
    }
}

void AbcRuntime::set_bundle_context(const std::string& bundle_name,
                                    const std::string& app_root_dir,
                                    const std::string& ohos_root_dir) {
    if (!bundle_name.empty()) {
        bundle_name_ = bundle_name;
        bundle_info_.name = bundle_name;
    }
    app_root_dir_ = app_root_dir;
    ohos_root_dir_ = ohos_root_dir.empty() ? default_ohos_root() : ohos_root_dir;
}

void AbcRuntime::set_bundle_info(OhosBundleInfo info) {
    bundle_info_ = std::move(info);
    // Keep the sandbox identity in step with the manifest facts.
    if (!bundle_info_.name.empty()) bundle_name_ = bundle_info_.name;
}

std::string AbcRuntime::last_napi_error() const {
    return napi_env_ ? napi_env_->last_error : std::string();
}

void AbcRuntime::set_arkui(ArkUiEngine* engine) {
    arkui_ = engine;
    if (arkui_) {
        arkui_->set_event_handler(
            [this](const std::string& handler_name, const std::string& node_id,
                   const std::string& payload) {
                const AbcValue node_v = AbcValue::make_string(node_id);
                const AbcValue payload_v = AbcValue::make_string(payload);
                const auto res = call_method_by_name(
                    handler_name, AbcValue::make_undefined(), {node_v, payload_v});
                if (!res.ok) {
                    log("[arkui] handler '" + handler_name + "' skipped: " + res.error);
                }
            });
    }
}

bool AbcRuntime::load_abc(AbcFile file, std::string* error) {
    if (!file.valid) {
        if (error) *error = file.error.empty() ? "invalid .abc file" : file.error;
        return false;
    }
    abc_ = std::move(file);
    // Register top-level methods as globals so bytecode and ArkUI callbacks find them cleanly.
    for (const auto& cls : abc_.classes) {
        for (const auto& m : cls.methods) {
            if (!m.name.empty() && globals_.find(m.name) == globals_.end()) {
                globals_[m.name] = AbcValue::make_abc_method(&m);
            }
        }
    }

    // Resolve ES import bindings by module-record index. An import of a
    // supported @ohos kit binds to the real shim; an app-internal binding that
    // finn-ohos cannot link gets a placeholder whose method calls are logged
    // as skipped (never silently presented as working).
    external_module_vars_.clear();
    if (!abc_.module_records.empty()) {
        for (const auto& imp : abc_.module_records.front().imports) {
            AbcValue binding;
            std::string spec = imp.module_request;
            if (spec.rfind("@ohos:", 0) == 0) {
                spec.replace(0, 6, "@ohos.");
            }
            const auto mod = modules_.find(spec);
            if (mod != modules_.end()) {
                binding = mod->second;
            } else if (const auto glob = globals_.find(imp.local_name);
                       glob != globals_.end() &&
                       glob->second.kind == AbcValue::Kind::Object) {
                binding = glob->second;
            } else {
                AbcValue placeholder = AbcValue::make_object(
                    "UnresolvedImport(" + imp.local_name + ")");
                const std::string label = imp.local_name;
                placeholder.obj_val->property_fallback =
                    [label](const std::string& member) {
                        return AbcValue::make_native(
                            [label, member](AbcRuntime& rt, const AbcValue& recv,
                                            const std::vector<AbcValue>&) {
                                rt.log("[module] " + label + "." + member +
                                       " skipped: import not linked by finn-ohos");
                                return recv;
                            });
                    };
                binding = std::move(placeholder);
            }
            external_module_vars_.push_back(std::move(binding));
        }
    }
    return true;
}

void AbcRuntime::set_external_module_var(std::size_t index, AbcValue value) {
    if (external_module_vars_.size() <= index) {
        external_module_vars_.resize(index + 1, AbcValue::make_undefined());
    }
    external_module_vars_[index] = std::move(value);
}

AbcValue AbcRuntime::unresolved_module_binding(std::size_t index) const {
    const std::string label = "externalModuleVar#" + std::to_string(index);
    AbcValue v = AbcValue::make_object("UnresolvedImport(" + label + ")");
    v.obj_val->property_fallback = [label](const std::string& member) {
        return AbcValue::make_native(
            [label, member](AbcRuntime& rt, const AbcValue& recv,
                            const std::vector<AbcValue>&) {
                rt.log("[module] " + label + "." + member +
                       " skipped: import not linked by finn-ohos");
                return recv;
            });
    };
    return v;
}

void AbcRuntime::set_global(const std::string& name, AbcValue value) {
    globals_[name] = std::move(value);
}

AbcValue AbcRuntime::get_global(const std::string& name) const {
    const auto it = globals_.find(name);
    if (it != globals_.end()) return it->second;
    if (const AbcMethod* m = abc_.find_method(name)) {
        return AbcValue::make_abc_method(m);
    }
    return AbcValue::make_undefined();
}

void AbcRuntime::log(const std::string& line) {
    logs_.push_back(line);
}

std::string AbcRuntime::prefs_file_path(const std::string& pref_name) const {
    const std::string root = ohos_root_dir_.empty() ? default_ohos_root() : ohos_root_dir_;
    const fs::path dir = fs::path(root) / "prefs";
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string suffix = bundle_name_;
    if (!pref_name.empty() && pref_name != "default" && pref_name != bundle_name_) {
        suffix += "." + pref_name;
    }
    return (dir / (suffix + ".conf")).string();
}

std::string AbcRuntime::sandbox_data_dir() const {
    const std::string root = ohos_root_dir_.empty() ? default_ohos_root() : ohos_root_dir_;
    const fs::path dir = fs::path(root) / "data" / bundle_name_;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

void AbcRuntime::install_builtin_modules() {
    // console
    AbcValue console_obj = AbcValue::make_object("Console");
    auto make_console_fn = [](std::string level) {
        return AbcValue::make_native(
            [level](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
                std::string line = "[console." + level + "]";
                for (const auto& a : args) line += " " + a.to_string();
                rt.log(line);
                return AbcValue::make_undefined();
            });
    };
    console_obj.obj_val->properties["log"] = make_console_fn("log");
    console_obj.obj_val->properties["info"] = make_console_fn("info");
    console_obj.obj_val->properties["warn"] = make_console_fn("warn");
    console_obj.obj_val->properties["error"] = make_console_fn("error");
    globals_["console"] = console_obj;

    // ECMAScript builtins that compiled ArkTS bytecode expects as globals.
    globals_["Number"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            return AbcValue::make_double(a.empty() ? 0.0 : a[0].to_number());
        });
    globals_["String"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            return AbcValue::make_string(a.empty() ? "" : a[0].to_string());
        });
    globals_["Boolean"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            return AbcValue::make_bool(!a.empty() && a[0].is_truthy());
        });
    AbcValue json_obj = AbcValue::make_object("JSON");
    json_obj.obj_val->properties["stringify"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            return AbcValue::make_string(a.empty() ? "undefined" : value_to_json(a[0]));
        });
    json_obj.obj_val->properties["parse"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            if (a.empty()) return AbcValue::make_undefined();
            auto parsed = parse_json5(a[0].to_string());
            return parsed ? json_to_value(*parsed) : AbcValue::make_undefined();
        });
    globals_["JSON"] = json_obj;

    // @ohos.hilog
    AbcValue hilog_obj = AbcValue::make_object("HiLog");
    auto make_hilog_fn = [](std::string level) {
        return AbcValue::make_native(
            [level](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
                const std::string domain = args.size() > 0 ? args[0].to_string() : "0";
                const std::string tag = args.size() > 1 ? args[1].to_string() : "OHOS";
                const std::string fmt = args.size() > 2 ? args[2].to_string() : "";
                const std::string msg = format_hilog(fmt, args, 3);
                rt.log("[hilog/" + level + "][" + domain + "/" + tag + "] " + msg);
                return AbcValue::make_undefined();
            });
    };
    hilog_obj.obj_val->properties["debug"] = make_hilog_fn("D");
    hilog_obj.obj_val->properties["info"] = make_hilog_fn("I");
    hilog_obj.obj_val->properties["warn"] = make_hilog_fn("W");
    hilog_obj.obj_val->properties["error"] = make_hilog_fn("E");
    hilog_obj.obj_val->properties["fatal"] = make_hilog_fn("F");
    // Conventional short aliases used by real ArkTS apps.
    hilog_obj.obj_val->properties["d"] = hilog_obj.obj_val->properties["debug"];
    hilog_obj.obj_val->properties["i"] = hilog_obj.obj_val->properties["info"];
    hilog_obj.obj_val->properties["w"] = hilog_obj.obj_val->properties["warn"];
    hilog_obj.obj_val->properties["e"] = hilog_obj.obj_val->properties["error"];
    globals_["hilog"] = hilog_obj;
    modules_["@ohos.hilog"] = hilog_obj;

    // @ohos.deviceInfo (honest OpenHarmony 6.1 LTS / API 23 identity)
    AbcValue dev_obj = AbcValue::make_object("DeviceInfo");
    dev_obj.obj_val->properties["sdkApiVersion"] = AbcValue::make_int(kOhosSupportedApiLevel);
    dev_obj.obj_val->properties["osFullName"] = AbcValue::make_string(kOhosOsFullName);
    dev_obj.obj_val->properties["brand"] = AbcValue::make_string("FinnOS");
    dev_obj.obj_val->properties["manufacture"] = AbcValue::make_string("FinnOS");
    dev_obj.obj_val->properties["productModel"] = AbcValue::make_string("FinnOS-Desktop");
    dev_obj.obj_val->properties["deviceType"] = AbcValue::make_string("default");
    dev_obj.obj_val->properties["abiList"] = AbcValue::make_string(host_ohos_abi());
    globals_["deviceInfo"] = dev_obj;
    modules_["@ohos.deviceInfo"] = dev_obj;

    // @ohos.util. Entropy comes from std::random_device; base64 is strict
    // RFC 4648 and reports invalid input with an empty result, never a guess.
    AbcValue util_obj = AbcValue::make_object("Util");
    util_obj.obj_val->properties["generateRandomUUID"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>&) {
            return AbcValue::make_string(generate_uuid_v4());
        });
    util_obj.obj_val->properties["getRandom"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            if (a.size() < 2) return AbcValue::make_int(0);
            std::int64_t lo = a[0].to_int64();
            std::int64_t hi = a[1].to_int64();
            if (lo > hi) std::swap(lo, hi);
            std::random_device rd;
            std::mt19937_64 gen((static_cast<std::uint64_t>(rd()) << 32) ^ rd());
            std::uniform_int_distribution<std::int64_t> dist(lo, hi);
            return AbcValue::make_int(dist(gen));
        });
    AbcValue base64_obj = AbcValue::make_object("Base64");
    base64_obj.obj_val->properties["encode"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            return AbcValue::make_string(
                a.empty() ? "" : base64_encode_bytes(a[0].to_string()));
        });
    base64_obj.obj_val->properties["decode"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            return AbcValue::make_string(
                a.empty() ? "" : base64_decode_bytes(a[0].to_string()));
        });
    util_obj.obj_val->properties["Base64"] = base64_obj;
    util_obj.obj_val->properties["Base64Encoder"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>&) {
            AbcValue enc = AbcValue::make_object("Base64Encoder");
            enc.obj_val->properties["encodeSync"] = AbcValue::make_native(
                [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
                    return AbcValue::make_string(
                        a.empty() ? "" : base64_encode_bytes(a[0].to_string()));
                });
            return enc;
        });
    util_obj.obj_val->properties["Base64Decoder"] = AbcValue::make_native(
        [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>&) {
            AbcValue dec = AbcValue::make_object("Base64Decoder");
            dec.obj_val->properties["decodeSync"] = AbcValue::make_native(
                [](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
                    return AbcValue::make_string(
                        a.empty() ? "" : base64_decode_bytes(a[0].to_string()));
                });
            return dec;
        });
    globals_["util"] = util_obj;
    modules_["@ohos.util"] = util_obj;

    // @ohos.bundle.bundleManager. FinnOS returns the BundleInfo synchronously
    // from real manifest facts (no Promise / no IPC); see docs/ohos-kit-coverage.md.
    AbcValue bm_obj = AbcValue::make_object("BundleManager");
    bm_obj.obj_val->properties["getBundleInfoForSelf"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>&) {
            const OhosBundleInfo& info = rt.bundle_info();
            AbcValue v = AbcValue::make_object("BundleInfo");
            v.obj_val->properties["name"] = AbcValue::make_string(info.name);
            v.obj_val->properties["bundleName"] = AbcValue::make_string(info.name);
            v.obj_val->properties["versionName"] = AbcValue::make_string(info.version_name);
            v.obj_val->properties["versionCode"] = AbcValue::make_int(info.version_code);
            v.obj_val->properties["moduleName"] = AbcValue::make_string(info.module_name);
            v.obj_val->properties["mainElement"] = AbcValue::make_string(info.main_element);
            v.obj_val->properties["apiVersion"] = AbcValue::make_int(info.api_version);
            return v;
        });
    globals_["bundleManager"] = bm_obj;
    modules_["@ohos.bundle.bundleManager"] = bm_obj;

    // @ohos.app — real manifest-derived identity for the running bundle.
    AbcValue app_obj = AbcValue::make_object("App");
    app_obj.obj_val->properties["getAppInfo"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>&) {
            const OhosBundleInfo& info = rt.bundle_info();
            AbcValue v = AbcValue::make_object("AppInfo");
            v.obj_val->properties["name"] = AbcValue::make_string(info.name);
            v.obj_val->properties["versionName"] = AbcValue::make_string(info.version_name);
            v.obj_val->properties["versionCode"] = AbcValue::make_int(info.version_code);
            v.obj_val->properties["apiVersion"] = AbcValue::make_int(info.api_version);
            return v;
        });
    globals_["app"] = app_obj;
    modules_["@ohos.app"] = app_obj;

    // @ohos.curves — real pure easing functions (t clamped to 0..1).
    AbcValue curves_obj = AbcValue::make_object("Curves");
    auto make_curve = [](int mode) {
        return AbcValue::make_native(
            [mode](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
                double t = a.empty() ? 0.0 : a[0].to_number();
                t = std::clamp(t, 0.0, 1.0);
                double y = t;
                switch (mode) {
                    case 1: y = t * t; break;                                  // easeIn
                    case 2: y = 1.0 - (1.0 - t) * (1.0 - t); break;            // easeOut
                    case 3: y = t < 0.5 ? 2.0 * t * t
                                        : 1.0 - 2.0 * (1.0 - t) * (1.0 - t);  // easeInOut
                    default: break;                                            // linear
                }
                return AbcValue::make_double(y);
            });
    };
    curves_obj.obj_val->properties["linear"] = make_curve(0);
    curves_obj.obj_val->properties["easeIn"] = make_curve(1);
    curves_obj.obj_val->properties["easeOut"] = make_curve(2);
    curves_obj.obj_val->properties["easeInOut"] = make_curve(3);
    globals_["curves"] = curves_obj;
    modules_["@ohos.curves"] = curves_obj;

    // @ohos.matrix4 — real column-major 4x4 matrix arithmetic.
    AbcValue matrix_obj = AbcValue::make_object("Matrix4");
    auto make_matrix_value = [](std::vector<double> cells) {
        AbcValue v = AbcValue::make_array();
        v.obj_val->array_val.reserve(cells.size());
        for (double c : cells) v.obj_val->array_val.push_back(AbcValue::make_double(c));
        return v;
    };
    auto read_matrix = [](const AbcValue& v) {
        std::vector<double> m(16, 0.0);
        if (v.kind == AbcValue::Kind::Object && v.obj_val) {
            for (std::size_t i = 0; i < 16 && i < v.obj_val->array_val.size(); ++i) {
                m[i] = v.obj_val->array_val[i].to_number();
            }
        }
        return m;
    };
    matrix_obj.obj_val->properties["identity"] = AbcValue::make_native(
        [make_matrix_value](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>&) {
            std::vector<double> m(16, 0.0);
            m[0] = m[5] = m[10] = m[15] = 1.0;
            return make_matrix_value(std::move(m));
        });
    matrix_obj.obj_val->properties["translation"] = AbcValue::make_native(
        [make_matrix_value](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            std::vector<double> m(16, 0.0);
            m[0] = m[5] = m[10] = m[15] = 1.0;
            for (int i = 0; i < 3 && i < static_cast<int>(a.size()); ++i) {
                m[12 + i] = a[static_cast<std::size_t>(i)].to_number();
            }
            return make_matrix_value(std::move(m));
        });
    matrix_obj.obj_val->properties["scaling"] = AbcValue::make_native(
        [make_matrix_value](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
            std::vector<double> m(16, 0.0);
            m[15] = 1.0;
            for (int i = 0; i < 3 && i < static_cast<int>(a.size()); ++i) {
                m[5 * i + i] = a[static_cast<std::size_t>(i)].to_number();
            }
            return make_matrix_value(std::move(m));
        });
    matrix_obj.obj_val->properties["multiply"] = AbcValue::make_native(
        [make_matrix_value, read_matrix](AbcRuntime&, const AbcValue&,
                                         const std::vector<AbcValue>& a) {
            if (a.size() < 2) return make_matrix_value(std::vector<double>(16, 0.0));
            const auto lhs = read_matrix(a[0]);
            const auto rhs = read_matrix(a[1]);
            std::vector<double> out(16, 0.0);
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    double sum = 0.0;
                    for (int k = 0; k < 4; ++k) {
                        sum += lhs[static_cast<std::size_t>(k * 4 + r)] *
                               rhs[static_cast<std::size_t>(c * 4 + k)];
                    }
                    out[static_cast<std::size_t>(c * 4 + r)] = sum;
                }
            }
            return make_matrix_value(std::move(out));
        });
    globals_["matrix4"] = matrix_obj;
    modules_["@ohos.matrix4"] = matrix_obj;

    // @ohos.router
    AbcValue router_obj = AbcValue::make_object("Router");
    router_obj.obj_val->properties["pushUrl"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            std::string url;
            if (!args.empty()) {
                if (args[0].kind == AbcValue::Kind::Object && args[0].obj_val) {
                    const auto it = args[0].obj_val->properties.find("url");
                    if (it != args[0].obj_val->properties.end()) url = it->second.to_string();
                } else {
                    url = args[0].to_string();
                }
            }
            if (!url.empty()) {
                rt.router_stack_.push_back(url);
                rt.load_declarative_page(url);
                rt.log("[router] pushUrl " + url);
            }
            return AbcValue::make_undefined();
        });
    router_obj.obj_val->properties["replaceUrl"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            std::string url;
            if (!args.empty()) {
                if (args[0].kind == AbcValue::Kind::Object && args[0].obj_val) {
                    const auto it = args[0].obj_val->properties.find("url");
                    if (it != args[0].obj_val->properties.end()) url = it->second.to_string();
                } else {
                    url = args[0].to_string();
                }
            }
            if (!url.empty()) {
                if (rt.router_stack_.empty()) rt.router_stack_.push_back(url);
                else rt.router_stack_.back() = url;
                rt.load_declarative_page(url);
                rt.log("[router] replaceUrl " + url);
            }
            return AbcValue::make_undefined();
        });
    router_obj.obj_val->properties["back"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>&) {
            if (rt.router_stack_.size() > 1) {
                rt.router_stack_.pop_back();
                rt.load_declarative_page(rt.router_stack_.back());
            }
            return AbcValue::make_undefined();
        });
    router_obj.obj_val->properties["getLength"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>&) {
            return AbcValue::make_string(std::to_string(rt.router_stack_.size()));
        });
    globals_["router"] = router_obj;
    modules_["@ohos.router"] = router_obj;

    // @ohos.data.preferences (persisted per bundle under ~/.local/share/finn/ohos/prefs/<bundle>.conf)
    AbcValue prefs_mod = AbcValue::make_object("PreferencesModule");
    auto open_prefs = [](AbcRuntime& rt, const AbcValue&,
                         const std::vector<AbcValue>& args) -> AbcValue {
        std::string pname = "default";
        if (args.size() >= 2) {
            if (args[1].kind == AbcValue::Kind::Object && args[1].obj_val) {
                const auto it = args[1].obj_val->properties.find("name");
                if (it != args[1].obj_val->properties.end()) pname = it->second.to_string();
            } else {
                pname = args[1].to_string();
            }
        } else if (args.size() == 1 && args[0].kind == AbcValue::Kind::String) {
            pname = args[0].str_val;
        }
        const std::string path = rt.prefs_file_path(pname);
        auto store = std::make_shared<std::unordered_map<std::string, std::string>>();
        {
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line)) {
                const std::size_t eq = line.find('=');
                if (eq != std::string::npos && eq > 0) {
                    (*store)[line.substr(0, eq)] = line.substr(eq + 1);
                }
            }
        }
        AbcValue obj = AbcValue::make_object("Preferences");
        obj.obj_val->properties["getSync"] = AbcValue::make_native(
            [store](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
                const std::string key = a.size() > 0 ? a[0].to_string() : "";
                const auto it = store->find(key);
                if (it != store->end()) return AbcValue::make_string(it->second);
                return a.size() > 1 ? a[1] : AbcValue::make_undefined();
            });
        obj.obj_val->properties["putSync"] = AbcValue::make_native(
            [store](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
                if (a.size() >= 2) (*store)[a[0].to_string()] = a[1].to_string();
                return AbcValue::make_undefined();
            });
        obj.obj_val->properties["hasSync"] = AbcValue::make_native(
            [store](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
                const std::string key = a.size() > 0 ? a[0].to_string() : "";
                return AbcValue::make_bool(store->find(key) != store->end());
            });
        obj.obj_val->properties["deleteSync"] = AbcValue::make_native(
            [store](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>& a) {
                if (!a.empty()) store->erase(a[0].to_string());
                return AbcValue::make_undefined();
            });
        auto flush_fn = AbcValue::make_native(
            [store, path](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>&) {
                std::ofstream out(path, std::ios::trunc);
                for (const auto& [k, v] : *store) {
                    out << sanitize_conf_field(k) << "=" << sanitize_conf_field(v) << "\n";
                }
                return AbcValue::make_undefined();
            });
        obj.obj_val->properties["flushSync"] = flush_fn;
        obj.obj_val->properties["flush"] = flush_fn;
        return obj;
    };
    prefs_mod.obj_val->properties["getPreferencesSync"] = AbcValue::make_native(open_prefs);
    prefs_mod.obj_val->properties["getPreferences"] = AbcValue::make_native(open_prefs);
    globals_["preferences"] = prefs_mod;
    modules_["@ohos.data.preferences"] = prefs_mod;

    // @ohos.file.fs
    AbcValue fs_mod = AbcValue::make_object("FileFs");
    auto resolve_fs_path = [](const AbcRuntime& rt, const std::string& raw) -> fs::path {
        fs::path p(raw);
        if (p.is_absolute()) return p;
        return fs::path(rt.sandbox_data_dir()) / p;
    };
    fs_mod.obj_val->properties["accessSync"] = AbcValue::make_native(
        [resolve_fs_path](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& a) {
            if (a.empty()) return AbcValue::make_bool(false);
            std::error_code ec;
            return AbcValue::make_bool(fs::exists(resolve_fs_path(rt, a[0].to_string()), ec));
        });
    fs_mod.obj_val->properties["mkdirSync"] = AbcValue::make_native(
        [resolve_fs_path](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& a) {
            if (!a.empty()) {
                std::error_code ec;
                fs::create_directories(resolve_fs_path(rt, a[0].to_string()), ec);
            }
            return AbcValue::make_undefined();
        });
    fs_mod.obj_val->properties["writeTextSync"] = AbcValue::make_native(
        [resolve_fs_path](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& a) {
            if (a.size() >= 2) {
                const fs::path p = resolve_fs_path(rt, a[0].to_string());
                std::error_code ec;
                fs::create_directories(p.parent_path(), ec);
                std::ofstream out(p, std::ios::binary | std::ios::trunc);
                const std::string txt = a[1].to_string();
                out.write(txt.data(), static_cast<std::streamsize>(txt.size()));
            }
            return AbcValue::make_undefined();
        });
    fs_mod.obj_val->properties["readTextSync"] = AbcValue::make_native(
        [resolve_fs_path](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& a) {
            if (a.empty()) return AbcValue::make_string("");
            std::ifstream in(resolve_fs_path(rt, a[0].to_string()), std::ios::binary);
            if (!in) return AbcValue::make_string("");
            std::ostringstream ss;
            ss << in.rdbuf();
            return AbcValue::make_string(ss.str());
        });
    fs_mod.obj_val->properties["unlinkSync"] = AbcValue::make_native(
        [resolve_fs_path](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& a) {
            if (!a.empty()) {
                std::error_code ec;
                fs::remove(resolve_fs_path(rt, a[0].to_string()), ec);
            }
            return AbcValue::make_undefined();
        });
    globals_["fs"] = fs_mod;
    modules_["@ohos.file.fs"] = fs_mod;

    // @ohos.window & @ohos.app.ability.UIAbility
    AbcValue win_mod = AbcValue::make_object("WindowModule");
    globals_["window"] = win_mod;
    modules_["@ohos.window"] = win_mod;

    AbcValue ui_ability_ctor = AbcValue::make_object("UIAbility");
    globals_["UIAbility"] = ui_ability_ctor;
    modules_["@ohos.app.ability.UIAbility"] = ui_ability_ctor;

    // ArkUI bridge global for live @State mutation from ArkTS bytecode
    AbcValue arkui_obj = AbcValue::make_object("ArkUI");
    arkui_obj.obj_val->properties["setState"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            if (rt.arkui_ && args.size() >= 2) {
                rt.arkui_->set_state(args[0].to_string(), args[1].to_string());
            }
            return AbcValue::make_undefined();
        });
    arkui_obj.obj_val->properties["getState"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            if (!rt.arkui_ || args.empty()) return AbcValue::make_string("");
            return AbcValue::make_string(rt.arkui_->get_state(args[0].to_string()));
        });
    arkui_obj.obj_val->properties["getStateInt"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            if (!rt.arkui_ || args.empty()) return AbcValue::make_int(0);
            const std::string v = rt.arkui_->get_state(args[0].to_string(), "0");
            return AbcValue::make_int(std::strtoll(v.c_str(), nullptr, 10));
        });
    arkui_obj.obj_val->properties["setTitle"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            if (rt.arkui_ && !args.empty()) rt.arkui_->set_title(args[0].to_string());
            return AbcValue::make_undefined();
        });
    arkui_obj.obj_val->properties["showAlert"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            if (rt.arkui_ && args.size() >= 2) {
                rt.arkui_->show_alert(args[0].to_string(), args[1].to_string());
            }
            return AbcValue::make_undefined();
        });
    globals_["arkui"] = arkui_obj;

    // Dynamic module importer
    globals_["requireModule"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            if (args.empty()) return AbcValue::make_undefined();
            const std::string spec = args[0].to_string();
            const auto it = rt.modules_.find(spec);
            if (it != rt.modules_.end()) return it->second;
            rt.log("[runtime] module not implemented: " + spec);
            return AbcValue::make_undefined();
        });

    // Real apps routinely import a module by one local name and call a member
    // finn-ohos does not implement. Report the skipped member honestly instead
    // of leaving an undefined value that aborts the whole UIAbility lifecycle.
    for (auto& [spec, mod] : modules_) {
        if (mod.kind != AbcValue::Kind::Object || !mod.obj_val ||
            mod.obj_val->property_fallback) {
            continue;
        }
        const std::string label = spec;
        mod.obj_val->property_fallback = [label](const std::string& member) {
            return AbcValue::make_native(
                [label, member](AbcRuntime& rt, const AbcValue& recv,
                                const std::vector<AbcValue>&) {
                    rt.log("[module] " + label + "." + member +
                           " skipped: not implemented by finn-ohos");
                    return recv;
                });
        };
    }
}

bool AbcRuntime::load_declarative_page(const std::string& page_path) {
    last_loaded_page_ = page_path;
    if (!arkui_) return false;
    if (app_root_dir_.empty()) return false;

    const fs::path root(app_root_dir_);
    arkui_->set_resource_root(app_root_dir_);

    std::vector<fs::path> candidates;
    if (!page_path.empty()) {
        candidates.push_back(root / "ets" / (page_path + ".json"));
        candidates.push_back(root / "ets" / page_path);
        candidates.push_back(root / "resources" / "rawfile" / (page_path + ".json"));
        const fs::path leaf = fs::path(page_path).filename();
        if (!leaf.empty()) {
            candidates.push_back(root / "ets" / "pages" / (leaf.string() + ".json"));
            candidates.push_back(root / "resources" / "rawfile" / (leaf.string() + ".json"));
        }
    }
    candidates.push_back(root / "resources" / "rawfile" / "ui.json");
    candidates.push_back(root / "ets" / "pages" / "Index.json");

    std::error_code ec;
    for (const auto& p : candidates) {
        if (!fs::is_regular_file(p, ec)) continue;
        std::ifstream in(p);
        if (!in) continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string err;
        if (arkui_->load_from_json(ss.str(), &err)) {
            arkui_->mount();
            log("[window] loadContent(" + page_path + ") mounted " + p.filename().string());
            return true;
        }
        log("[window] failed to parse " + p.string() + ": " + err);
    }
    return false;
}

AbcExecResult AbcRuntime::execute_method(const AbcMethod& method,
                                         const AbcValue& this_val,
                                         const std::vector<AbcValue>& args) {
    AbcExecResult res;
    if (call_depth_ >= 128) {
        res.error = "maximum Ark bytecode call depth (128) exceeded";
        return res;
    }

    const auto& code = method.bytecode;
    const std::size_t reg_count =
        std::max<std::size_t>(256, static_cast<std::size_t>(method.num_vregs) +
                                       static_cast<std::size_t>(method.num_args) +
                                       args.size() + 8);
    std::vector<AbcValue> regs(reg_count, AbcValue::make_undefined());

    // Place arguments in Panda argument registers v[num_vregs .. num_vregs + n - 1]
    const std::size_t arg_base = method.num_vregs;
    for (std::size_t i = 0; i < args.size() && arg_base + i < regs.size(); ++i) {
        regs[arg_base + i] = args[i];
    }

    AbcValue acc = AbcValue::make_undefined();
    std::size_t pc = 0;
    constexpr std::size_t kMaxSteps = 1000000u;
    ++call_depth_;

    auto invoke_callable = [&](const AbcValue& fn, const AbcValue& recv,
                               const std::vector<AbcValue>& call_args,
                               std::string* err_out) -> std::pair<bool, AbcValue> {
        if (fn.kind == AbcValue::Kind::NativeFunction && fn.native_fn) {
            return {true, fn.native_fn(*this, recv, call_args)};
        }
        if (fn.kind == AbcValue::Kind::AbcFunction && fn.abc_method) {
            const auto sub = execute_method(*fn.abc_method, recv, call_args);
            if (!sub.ok) {
                if (err_out) *err_out = sub.error;
                return {false, AbcValue::make_undefined()};
            }
            return {true, sub.return_value};
        }
        if (err_out) *err_out = "attempted to call non-callable value (" + fn.to_string() + ")";
        return {false, AbcValue::make_undefined()};
    };

    while (pc < code.size()) {
        if (++res.instructions_executed > kMaxSteps) {
            --call_depth_;
            res.error = "bytecode execution step limit exceeded in " + method.name;
            return res;
        }
        const std::size_t inst_pc = pc;
        const std::uint8_t op = code[pc++];

        auto need = [&](std::size_t n) -> bool {
            if (pc + n > code.size()) {
                res.error = "truncated bytecode instruction in " + method.name;
                return false;
            }
            return true;
        };

        switch (static_cast<AbcOpcode>(op)) {
            case AbcOpcode::LdUndefined:
                acc = AbcValue::make_undefined();
                break;
            case AbcOpcode::LdNull:
                acc = AbcValue::make_null();
                break;
            case AbcOpcode::LdTrue:
                acc = AbcValue::make_bool(true);
                break;
            case AbcOpcode::LdFalse:
                acc = AbcValue::make_bool(false);
                break;
            case AbcOpcode::CreateEmptyObject:
                acc = AbcValue::make_object("Object");
                break;
            case AbcOpcode::CreateEmptyArray:
                if (!need(1)) { --call_depth_; return res; }
                ++pc;  // imm8 IC slot
                acc = AbcValue::make_array();
                break;
            case AbcOpcode::Mov4: {
                if (!need(1)) { --call_depth_; return res; }
                const std::uint8_t packed = code[pc++];
                const std::size_t dst = packed & 0x0fu;
                const std::size_t src = (packed >> 4) & 0x0fu;
                regs[dst] = regs[src];
                break;
            }
            case AbcOpcode::Mov8: {
                if (!need(2)) { --call_depth_; return res; }
                const std::size_t dst = code[pc++];
                const std::size_t src = code[pc++];
                regs[dst] = regs[src];
                break;
            }
            case AbcOpcode::Lda: {
                if (!need(1)) { --call_depth_; return res; }
                acc = regs[code[pc++]];
                break;
            }
            case AbcOpcode::Sta: {
                if (!need(1)) { --call_depth_; return res; }
                regs[code[pc++]] = acc;
                break;
            }
            case AbcOpcode::Ldai: {
                if (!need(4)) { --call_depth_; return res; }
                const std::int32_t imm = static_cast<std::int32_t>(read_le32(code.data() + pc));
                pc += 4;
                acc = AbcValue::make_int(imm);
                break;
            }
            case AbcOpcode::Fldai: {
                if (!need(8)) { --call_depth_; return res; }
                const std::uint64_t bits = read_le64(code.data() + pc);
                pc += 8;
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof(d));
                acc = AbcValue::make_double(d);
                break;
            }
            case AbcOpcode::LdaStr: {
                // Public ISA: lda.str string_id has format op_id_16 (2 bytes).
                if (!need(2)) { --call_depth_; return res; }
                const std::uint16_t id16 = read_le16(code.data() + pc);
                pc += 2;
                acc = AbcValue::make_string(
                    abc_.resolve_id16_string(method.offset, id16));
                break;
            }
            case AbcOpcode::Add2: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;  // IC slot
                const AbcValue& lhs = regs[code[pc++]];
                if (lhs.kind == AbcValue::Kind::String || acc.kind == AbcValue::Kind::String) {
                    acc = AbcValue::make_string(lhs.to_string() + acc.to_string());
                } else if (lhs.kind == AbcValue::Kind::Int && acc.kind == AbcValue::Kind::Int) {
                    acc = AbcValue::make_int(lhs.int_val + acc.int_val);
                } else {
                    acc = AbcValue::make_double(lhs.to_number() + acc.to_number());
                }
                break;
            }
            case AbcOpcode::Sub2: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                if (lhs.kind == AbcValue::Kind::Int && acc.kind == AbcValue::Kind::Int) {
                    acc = AbcValue::make_int(lhs.int_val - acc.int_val);
                } else {
                    acc = AbcValue::make_double(lhs.to_number() - acc.to_number());
                }
                break;
            }
            case AbcOpcode::Mul2: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                if (lhs.kind == AbcValue::Kind::Int && acc.kind == AbcValue::Kind::Int) {
                    acc = AbcValue::make_int(lhs.int_val * acc.int_val);
                } else {
                    acc = AbcValue::make_double(lhs.to_number() * acc.to_number());
                }
                break;
            }
            case AbcOpcode::Div2: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                const double denom = acc.to_number();
                acc = AbcValue::make_double(denom == 0.0 ? 0.0 : lhs.to_number() / denom);
                break;
            }
            case AbcOpcode::Mod2: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                if (lhs.kind == AbcValue::Kind::Int && acc.kind == AbcValue::Kind::Int &&
                    acc.int_val != 0) {
                    acc = AbcValue::make_int(lhs.int_val % acc.int_val);
                } else {
                    const double denom = acc.to_number();
                    acc = AbcValue::make_double(denom == 0.0 ? 0.0 : std::fmod(lhs.to_number(), denom));
                }
                break;
            }
            case AbcOpcode::Eq: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                acc = AbcValue::make_bool(lhs.equals(acc));
                break;
            }
            case AbcOpcode::NotEq: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                acc = AbcValue::make_bool(!lhs.equals(acc));
                break;
            }
            case AbcOpcode::Less: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                acc = AbcValue::make_bool(lhs.to_number() < acc.to_number());
                break;
            }
            case AbcOpcode::Greater: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const AbcValue& lhs = regs[code[pc++]];
                acc = AbcValue::make_bool(lhs.to_number() > acc.to_number());
                break;
            }
            case AbcOpcode::Jmp8: {
                if (!need(1)) { --call_depth_; return res; }
                const auto rel = static_cast<std::int8_t>(code[pc++]);
                const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(inst_pc) + rel;
                if (target < 0 || static_cast<std::size_t>(target) > code.size()) {
                    --call_depth_;
                    res.error = "jmp8 target out of bounds";
                    return res;
                }
                pc = static_cast<std::size_t>(target);
                break;
            }
            case AbcOpcode::Jmp16: {
                if (!need(2)) { --call_depth_; return res; }
                const auto rel = static_cast<std::int16_t>(read_le16(code.data() + pc));
                pc += 2;
                const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(inst_pc) + rel;
                if (target < 0 || static_cast<std::size_t>(target) > code.size()) {
                    --call_depth_;
                    res.error = "jmp16 target out of bounds";
                    return res;
                }
                pc = static_cast<std::size_t>(target);
                break;
            }
            case AbcOpcode::Jeqz8: {
                if (!need(1)) { --call_depth_; return res; }
                const auto rel = static_cast<std::int8_t>(code[pc++]);
                if (!acc.is_truthy()) {
                    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(inst_pc) + rel;
                    if (target < 0 || static_cast<std::size_t>(target) > code.size()) {
                        --call_depth_;
                        res.error = "jeqz8 target out of bounds";
                        return res;
                    }
                    pc = static_cast<std::size_t>(target);
                }
                break;
            }
            case AbcOpcode::Jeqz16: {
                if (!need(2)) { --call_depth_; return res; }
                const auto rel = static_cast<std::int16_t>(read_le16(code.data() + pc));
                pc += 2;
                if (!acc.is_truthy()) {
                    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(inst_pc) + rel;
                    if (target < 0 || static_cast<std::size_t>(target) > code.size()) {
                        --call_depth_;
                        res.error = "jeqz16 target out of bounds";
                        return res;
                    }
                    pc = static_cast<std::size_t>(target);
                }
                break;
            }
            case AbcOpcode::Jnez8: {
                if (!need(1)) { --call_depth_; return res; }
                const auto rel = static_cast<std::int8_t>(code[pc++]);
                if (acc.is_truthy()) {
                    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(inst_pc) + rel;
                    if (target < 0 || static_cast<std::size_t>(target) > code.size()) {
                        --call_depth_;
                        res.error = "jnez8 target out of bounds";
                        return res;
                    }
                    pc = static_cast<std::size_t>(target);
                }
                break;
            }
            case AbcOpcode::Jnez16: {
                if (!need(2)) { --call_depth_; return res; }
                const auto rel = static_cast<std::int16_t>(read_le16(code.data() + pc));
                pc += 2;
                if (acc.is_truthy()) {
                    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(inst_pc) + rel;
                    if (target < 0 || static_cast<std::size_t>(target) > code.size()) {
                        --call_depth_;
                        res.error = "jnez16 target out of bounds";
                        return res;
                    }
                    pc = static_cast<std::size_t>(target);
                }
                break;
            }
            case AbcOpcode::CallArg0: {
                if (!need(1)) { --call_depth_; return res; }
                ++pc;
                auto [ok, rv] = invoke_callable(acc, this_val, {}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::CallArg1: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const std::uint8_t a0 = code[pc++];
                auto [ok, rv] = invoke_callable(acc, this_val, {regs[a0]}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::CallArgs2: {
                if (!need(3)) { --call_depth_; return res; }
                ++pc;
                const std::uint8_t a0 = code[pc++];
                const std::uint8_t a1 = code[pc++];
                auto [ok, rv] = invoke_callable(acc, this_val, {regs[a0], regs[a1]}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::CallArgs3: {
                if (!need(4)) { --call_depth_; return res; }
                ++pc;
                const std::uint8_t a0 = code[pc++];
                const std::uint8_t a1 = code[pc++];
                const std::uint8_t a2 = code[pc++];
                auto [ok, rv] = invoke_callable(
                    acc, this_val, {regs[a0], regs[a1], regs[a2]}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::CallThis0: {
                if (!need(2)) { --call_depth_; return res; }
                ++pc;
                const std::uint8_t recv = code[pc++];
                auto [ok, rv] = invoke_callable(acc, regs[recv], {}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::CallThis1: {
                if (!need(3)) { --call_depth_; return res; }
                ++pc;
                const std::uint8_t recv = code[pc++];
                const std::uint8_t a0 = code[pc++];
                auto [ok, rv] = invoke_callable(acc, regs[recv], {regs[a0]}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::CallThis2: {
                if (!need(4)) { --call_depth_; return res; }
                ++pc;
                const std::uint8_t recv = code[pc++];
                const std::uint8_t a0 = code[pc++];
                const std::uint8_t a1 = code[pc++];
                auto [ok, rv] = invoke_callable(
                    acc, regs[recv], {regs[a0], regs[a1]}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::CallThis3: {
                if (!need(5)) { --call_depth_; return res; }
                ++pc;
                const std::uint8_t recv = code[pc++];
                const std::uint8_t a0 = code[pc++];
                const std::uint8_t a1 = code[pc++];
                const std::uint8_t a2 = code[pc++];
                auto [ok, rv] = invoke_callable(
                    acc, regs[recv], {regs[a0], regs[a1], regs[a2]}, &res.error);
                if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                acc = std::move(rv);
                break;
            }
            case AbcOpcode::NewObjRange: {
                if (!need(3)) { --call_depth_; return res; }
                ++pc;  // IC slot
                const std::uint8_t count = code[pc++];
                const std::uint8_t first = code[pc++];
                const AbcValue& ctor = regs[first];
                std::vector<AbcValue> cargs;
                for (std::uint8_t i = 1; i < count && static_cast<std::size_t>(first) + i < regs.size(); ++i) {
                    cargs.push_back(regs[static_cast<std::size_t>(first) + i]);
                }
                AbcValue instance = AbcValue::make_object(
                    ctor.kind == AbcValue::Kind::Object && ctor.obj_val
                        ? ctor.obj_val->class_name
                        : "Object");
                if (ctor.kind == AbcValue::Kind::NativeFunction ||
                    ctor.kind == AbcValue::Kind::AbcFunction) {
                    auto [ok, rv] = invoke_callable(ctor, instance, cargs, &res.error);
                    if (!ok) { --call_depth_; if (res.error.find("pc=") == std::string::npos) res.error += " at pc=" + std::to_string(inst_pc) + " in " + method.name; return res; }
                    acc = (rv.kind == AbcValue::Kind::Object) ? std::move(rv) : std::move(instance);
                } else {
                    acc = std::move(instance);
                }
                break;
            }
            case AbcOpcode::LdObjByName8:
            case AbcOpcode::LdObjByName16: {
                const std::size_t ic_bytes = (static_cast<AbcOpcode>(op) == AbcOpcode::LdObjByName16) ? 2 : 1;
                if (!need(ic_bytes + 2)) { --call_depth_; return res; }
                pc += ic_bytes;
                const std::uint16_t id16 = read_le16(code.data() + pc);
                pc += 2;
                const std::string prop = abc_.resolve_id16_string(method.offset, id16);
                if (acc.kind == AbcValue::Kind::Object && acc.obj_val) {
                    const auto it = acc.obj_val->properties.find(prop);
                    if (it != acc.obj_val->properties.end()) {
                        acc = it->second;
                    } else if (acc.obj_val->property_fallback) {
                        acc = acc.obj_val->property_fallback(prop);
                    } else {
                        acc = AbcValue::make_undefined();
                    }
                } else {
                    acc = AbcValue::make_undefined();
                }
                break;
            }
            case AbcOpcode::StObjByName8:
            case AbcOpcode::StObjByName16:
            case AbcOpcode::StOwnByName8:
            case AbcOpcode::StOwnByName16: {
                const std::size_t ic_bytes =
                    (op == static_cast<std::uint8_t>(AbcOpcode::StObjByName16) ||
                     op == static_cast<std::uint8_t>(AbcOpcode::StOwnByName16)) ? 2 : 1;
                if (!need(ic_bytes + 3)) { --call_depth_; return res; }
                pc += ic_bytes;
                const std::uint16_t id16 = read_le16(code.data() + pc);
                pc += 2;
                const std::uint8_t obj_reg = code[pc++];
                const std::string prop = abc_.resolve_id16_string(method.offset, id16);
                if (regs[obj_reg].kind == AbcValue::Kind::Object && regs[obj_reg].obj_val) {
                    regs[obj_reg].obj_val->properties[prop] = acc;
                }
                break;
            }
            case AbcOpcode::TryLdGlobalByName8:
            case AbcOpcode::TryLdGlobalByName16: {
                const std::size_t ic_bytes =
                    (static_cast<AbcOpcode>(op) == AbcOpcode::TryLdGlobalByName16) ? 2 : 1;
                if (!need(ic_bytes + 2)) { --call_depth_; return res; }
                pc += ic_bytes;
                const std::uint16_t id16 = read_le16(code.data() + pc);
                pc += 2;
                const std::string name = abc_.resolve_id16_string(method.offset, id16);
                acc = get_global(name);
                break;
            }
            case AbcOpcode::TryStGlobalByName8:
            case AbcOpcode::TryStGlobalByName16: {
                const std::size_t ic_bytes =
                    (static_cast<AbcOpcode>(op) == AbcOpcode::TryStGlobalByName16) ? 2 : 1;
                if (!need(ic_bytes + 2)) { --call_depth_; return res; }
                pc += ic_bytes;
                const std::uint16_t id16 = read_le16(code.data() + pc);
                pc += 2;
                const std::string name = abc_.resolve_id16_string(method.offset, id16);
                globals_[name] = acc;
                break;
            }
            case AbcOpcode::Neg: {
                if (!need(1)) { --call_depth_; return res; }
                ++pc;  // imm8 IC slot
                acc = AbcValue::make_double(-acc.to_number());
                break;
            }
            case AbcOpcode::IsTrue:
            case AbcOpcode::IsFalse: {
                const bool strict_true =
                    acc.kind == AbcValue::Kind::Bool && acc.bool_val;
                acc = AbcValue::make_bool(op == static_cast<std::uint8_t>(AbcOpcode::IsTrue)
                                              ? strict_true
                                              : !strict_true);
                break;
            }
            case AbcOpcode::DefineFunc: {
                // Class/function definition is not linked; consume its
                // operands and leave the accumulator untouched.
                if (!need(4)) { --call_depth_; return res; }
                pc += 4;
                break;
            }
            case AbcOpcode::DefineFieldByName: {
                if (!need(4)) { --call_depth_; return res; }
                pc += 4;
                break;
            }
            case AbcOpcode::LdGlobal: {
                // Expose the real globals this runtime provides as globalThis.
                AbcValue g = AbcValue::make_object("GlobalThis");
                for (const auto& [name, value] : globals_) {
                    g.obj_val->properties[name] = value;
                }
                acc = std::move(g);
                break;
            }
            case AbcOpcode::CreateObjectWithBuffer: {
                if (!need(3)) { --call_depth_; return res; }
                pc += 3;  // imm8 IC slot + id16 literal-buffer id
                // Literal-array initialisation is not evaluated; an empty real
                // object is returned rather than a fabricated value.
                acc = AbcValue::make_object("Object");
                break;
            }
            case AbcOpcode::NewLexEnv: {
                if (!need(1)) { --call_depth_; return res; }
                ++pc;  // slot count; finn-ohos uses a flat lexical slot table
                acc = AbcValue::make_object("LexEnv");
                break;
            }
            case AbcOpcode::PopLexEnv:
                break;
            case AbcOpcode::LdExternalModuleVar:
            case AbcOpcode::LdLocalModuleVar: {
                if (!need(1)) { --call_depth_; return res; }
                const std::uint8_t index = code[pc++];
                if (index < external_module_vars_.size() &&
                    external_module_vars_[index].kind != AbcValue::Kind::Undefined) {
                    acc = external_module_vars_[index];
                } else {
                    acc = unresolved_module_binding(index);
                }
                break;
            }
            case AbcOpcode::LdLexVar:
            case AbcOpcode::StLexVar: {
                if (!need(1)) { --call_depth_; return res; }
                const std::uint8_t packed = code[pc++];
                const std::size_t key =
                    static_cast<std::size_t>((packed >> 4) & 0x0fu) * 16u +
                    static_cast<std::size_t>(packed & 0x0fu);
                if (static_cast<AbcOpcode>(op) == AbcOpcode::LdLexVar) {
                    acc = (key < lexical_slots_.size())
                              ? lexical_slots_[key]
                              : AbcValue::make_undefined();
                } else {
                    if (lexical_slots_.size() <= key) {
                        lexical_slots_.resize(key + 1, AbcValue::make_undefined());
                    }
                    lexical_slots_[key] = acc;
                }
                break;
            }
            case AbcOpcode::ThrowPrefixed: {
                if (!need(1)) { --call_depth_; return res; }
                const std::uint8_t sub = code[pc++];
                if (sub == 0x09u) {  // undefinedifholewithname id16
                    if (!need(2)) { --call_depth_; return res; }
                    pc += 2;  // name id; no HOLE kind in this runtime, acc passes through
                } else {
                    char hex[16];
                    std::snprintf(hex, sizeof(hex), "0x%02x", static_cast<unsigned>(sub));
                    --call_depth_;
                    res.error = std::string("unimplemented throw-prefixed sub-opcode ") +
                                hex + " in " + method.name;
                    return res;
                }
                break;
            }
            case AbcOpcode::Return:
                --call_depth_;
                res.ok = true;
                res.return_value = std::move(acc);
                return res;
            case AbcOpcode::ReturnUndefined:
                --call_depth_;
                res.ok = true;
                res.return_value = AbcValue::make_undefined();
                return res;
            default: {
                char hex[16];
                std::snprintf(hex, sizeof(hex), "0x%02x", static_cast<unsigned>(op));
                --call_depth_;
                res.error = std::string("unimplemented Panda bytecode opcode ") + hex +
                            " at pc=" + std::to_string(inst_pc) + " in " + method.name;
                return res;
            }
        }
    }

    --call_depth_;
    res.ok = true;
    res.return_value = std::move(acc);
    return res;
}

AbcExecResult AbcRuntime::call_method_by_name(const std::string& method_name,
                                              const AbcValue& this_val,
                                              const std::vector<AbcValue>& args) {
    const AbcMethod* m = abc_.find_method(method_name);
    if (!m) {
        AbcExecResult r;
        r.error = "method not found in .abc: " + method_name;
        return r;
    }
    return execute_method(*m, this_val, args);
}

AbilityRunReport AbcRuntime::run_ability_lifecycle(const OhosModuleManifest& manifest,
                                                   int argc, char** argv,
                                                   bool headless,
                                                   bool auto_close) {
    const auto t0 = std::chrono::steady_clock::now();
    AbilityRunReport report;
    report.bundle_name = manifest.bundle_name;
    report.ability_name = !manifest.main_element.empty()
                              ? manifest.main_element
                              : (!manifest.abilities.empty() ? manifest.abilities[0].name
                                                             : "EntryAbility");

    OhosBundleInfo binfo;
    binfo.name = manifest.bundle_name;
    binfo.version_name = manifest.version_name;
    binfo.version_code = manifest.version_code;
    binfo.module_name = manifest.module_name;
    binfo.main_element = report.ability_name;
    binfo.api_version = manifest.target_api_version;
    set_bundle_info(std::move(binfo));

    AbcValue ability_this = AbcValue::make_object(report.ability_name);
    AbcValue want = AbcValue::make_object("Want");
    want.obj_val->properties["bundleName"] = AbcValue::make_string(manifest.bundle_name);
    want.obj_val->properties["abilityName"] = AbcValue::make_string(report.ability_name);
    want.obj_val->properties["moduleName"] = AbcValue::make_string(manifest.module_name);

    const auto now_create = std::chrono::steady_clock::now();
    report.startup_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now_create - t0)
                                 .count();

    // OpenHarmony 6.1 LTS (API 23) LaunchParam with real startupTimeMs telemetry
    AbcValue launch_param = AbcValue::make_object("LaunchParam");
    launch_param.obj_val->properties["launchReason"] = AbcValue::make_int(1);
    launch_param.obj_val->properties["lastExitReason"] = AbcValue::make_int(0);
    launch_param.obj_val->properties["startupTimeMs"] = AbcValue::make_int(report.startup_time_ms);
    launch_param.obj_val->properties["apiVersion"] = AbcValue::make_int(kOhosSupportedApiLevel);

    // 1. onCreate(want, launchParam)
    if (const AbcMethod* m = abc_.find_method("onCreate")) {
        const auto r = execute_method(*m, ability_this, {want, launch_param});
        report.lifecycle_phases.push_back("onCreate");
        if (!r.ok) {
            report.error = "onCreate failed: " + r.error;
            report.logs = logs_;
            return report;
        }
    }

    // Build WindowStage with loadContent() bridged into ArkUiEngine
    bool content_loaded = false;
    AbcValue main_window = AbcValue::make_object("Window");
    main_window.obj_val->properties["setWindowBackgroundColor"] = AbcValue::make_native(
        [](AbcRuntime& rt, const AbcValue&, const std::vector<AbcValue>& args) {
            if (!args.empty()) rt.log("[window] background=" + args[0].to_string());
            return AbcValue::make_undefined();
        });

    AbcValue window_stage = AbcValue::make_object("WindowStage");
    window_stage.obj_val->properties["loadContent"] = AbcValue::make_native(
        [&content_loaded, &report](AbcRuntime& rt, const AbcValue&,
                                   const std::vector<AbcValue>& args) {
            const std::string page = args.empty() ? "pages/Index" : args[0].to_string();
            report.loaded_page = page;
            rt.router_stack_.push_back(page);
            content_loaded = rt.load_declarative_page(page);
            return AbcValue::make_undefined();
        });
    window_stage.obj_val->properties["getMainWindowSync"] = AbcValue::make_native(
        [main_window](AbcRuntime&, const AbcValue&, const std::vector<AbcValue>&) {
            return main_window;
        });

    // 2. onWindowStageCreate(windowStage)
    if (const AbcMethod* m = abc_.find_method("onWindowStageCreate")) {
        const auto r = execute_method(*m, ability_this, {window_stage});
        report.lifecycle_phases.push_back("onWindowStageCreate");
        if (!r.ok) {
            report.error = "onWindowStageCreate failed: " + r.error;
            report.logs = logs_;
            return report;
        }
    }

    // If package provided a declarative UI page but .abc didn't invoke loadContent, mount it now.
    if (!content_loaded && arkui_) {
        std::string declarative_error;
        bool declarative = build_arkui_from_panda_bytecode(
            app_root_dir_, arkui_, &declarative_error);
        if (!declarative) {
            declarative_error.clear();
            declarative = build_declarative_ui_from_hap(
                app_root_dir_, arkui_, &declarative_error);
        }
        if (declarative) {
            content_loaded = true;
            report.lifecycle_phases.push_back("declarative-ui-from-package");
        } else if (!load_declarative_page("pages/Index")) {
            // Honest fallback card showing real inspected manifest & bytecode facts.
            ArkUiNode root;
            root.kind = ArkComponentKind::Column;
            root.type_name = "Column";
            root.id = "ability_root";
            root.padding = 16;
            ArkUiNode title;
            title.kind = ArkComponentKind::Text;
            title.type_name = "Text";
            title.id = "ability_title";
            title.text = manifest.bundle_name + " (" + report.ability_name + ")";
            title.font_size = 20;
            root.children.push_back(std::move(title));
            ArkUiNode meta;
            meta.kind = ArkComponentKind::Text;
            meta.type_name = "Text";
            meta.id = "ability_meta";
            meta.text = "API " + std::to_string(manifest.target_api_version) +
                        " | " + std::to_string(abc_.classes.size()) + " Panda classes";
            root.children.push_back(std::move(meta));
            arkui_->set_title(manifest.bundle_name);
            arkui_->set_root(std::move(root));
            arkui_->mount();
        }
    }

    // 3. onForeground()
    if (const AbcMethod* m = abc_.find_method("onForeground")) {
        const auto r = execute_method(*m, ability_this, {});
        report.lifecycle_phases.push_back("onForeground");
        if (!r.ok) {
            report.error = "onForeground failed: " + r.error;
            report.logs = logs_;
            return report;
        }
    }

    // Run ArkUI window (Qt6 event loop when GUI build & !headless, or headless validation)
    if (arkui_ && (!headless || auto_close)) {
        (void)arkui_->run_window(argc, argv, headless);
    }

    // 4. onBackground() & onDestroy() (unless caller kept UI mounted for interactive testing)
    if (auto_close) {
        if (const AbcMethod* m = abc_.find_method("onBackground")) {
            const auto r = execute_method(*m, ability_this, {});
            report.lifecycle_phases.push_back("onBackground");
            if (!r.ok) {
                report.error = "onBackground failed: " + r.error;
                report.logs = logs_;
                return report;
            }
        }
        if (const AbcMethod* m = abc_.find_method("onDestroy")) {
            const auto r = execute_method(*m, ability_this, {});
            report.lifecycle_phases.push_back("onDestroy");
            if (!r.ok) {
                report.error = "onDestroy failed: " + r.error;
                report.logs = logs_;
                return report;
            }
        }
        if (arkui_ && arkui_->is_mounted()) {
            arkui_->unmount();
        }
    }

    report.ok = true;
    report.logs = logs_;
    return report;
}

bool AbcRuntime::load_native_module(const std::string& so_path, std::string* error) {
    std::error_code ec;
    if (!fs::is_regular_file(so_path, ec)) {
        if (error) *error = "native NAPI library does not exist: " + so_path;
        return false;
    }

    std::vector<napi_module*> newly_registered;
    g_active_napi_reg_sink = &newly_registered;
    void* handle = ::dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    g_active_napi_reg_sink = nullptr;

    if (!handle) {
        const char* dlerr = ::dlerror();
        if (error) *error = std::string("dlopen failed: ") + (dlerr ? dlerr : "unknown error");
        return false;
    }
    dl_handles_.push_back(handle);

    for (napi_module* mod : newly_registered) {
        if (!mod || !mod->nm_register_func || !mod->nm_modname) continue;
        napi_value exports = nullptr;
        napi_create_object(napi_env_.get(), &exports);
        napi_value result = mod->nm_register_func(napi_env_.get(), exports);
        AbcValue mod_val = result ? result->val : exports->val;
        modules_[mod->nm_modname] = mod_val;
        globals_[mod->nm_modname] = std::move(mod_val);
        napi_env_->registered_modules.push_back(mod);
        log(std::string("[napi] registered native module: ") + mod->nm_modname);
    }
    return true;
}

AbcValue AbcRuntime::get_native_module(const std::string& modname) const {
    const auto it = modules_.find(modname);
    return (it == modules_.end()) ? AbcValue::make_undefined() : it->second;
}

napi_env AbcRuntime::napi_env_handle() {
    return napi_env_.get();
}

AbcValue AbcRuntime::unwrap_napi_value(napi_value v) const {
    return v ? v->val : AbcValue::make_undefined();
}

napi_value AbcRuntime::wrap_napi_value(AbcValue v) {
    return napi_env_->alloc(std::move(v));
}

}  // namespace finn::ohos
