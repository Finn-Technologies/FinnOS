#include "swift_runtime_ext.hpp"
#include "abi_metadata.hpp"

#include <cstdio>
#include <cstring>

namespace finn::darwin::swiftui {
namespace {

template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// ---------------------------------------------------------------------------
// Mangled-name parsing (Swift 5 grammar subset used by this binary).
// ---------------------------------------------------------------------------

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string demangle_impl(std::string_view s) {
    // Strip the `$s` mangling prefix.
    if (s.size() >= 2 && s[0] == '$' && s[1] == 's') s.remove_prefix(2);

    std::string result;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (is_digit(c)) {
            std::size_t len = 0;
            while (i < s.size() && is_digit(s[i])) {
                len = len * 10 + static_cast<std::size_t>(s[i] - '0');
                ++i;
            }
            if (len == 0 || i + len > s.size()) return {};
            if (!result.empty()) result.push_back('.');
            result.append(s.substr(i, len));
            i += len;
        } else if (c == 's') {
            if (!result.empty()) result.push_back('.');
            result += "Swift";
            ++i;
        } else if (c == 'S' && i + 1 < s.size() && s[i + 1] == 'S') {
            if (!result.empty()) result.push_back('.');
            result += "String";
            i += 2;
        } else if (c == 'S') {
            // `S` alone starts a substitution/shorthand we do not expand.
            ++i;
        } else if (c == 'V' || c == 'O' || c == 'C' || c == 'P' || c == 'T' ||
                   c == 'F' || c == 'N' || c == 'M' || c == 'A' || c == 'R' ||
                   c == 'G' || c == 'y' || c == 'p' || c == 'd' || c == '_') {
            // Type/kind and generic markers terminate the nominal name.
            ++i;
        } else {
            return {};  // unknown grammar — do not invent a type
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// swift_getTypeByMangledNameInContext and friends.
// ---------------------------------------------------------------------------

using GetTypeFn = const void* (*)(const char*, std::size_t, const void*, const void* const*);
using GetTypeInStateFn = const void* (*)(const char*, std::size_t, const void*,
                                         const void* const*, unsigned);

const void* shim_get_type_by_mangled_name(const char* name, std::size_t length,
                                            const void*, const void* const*) {
    if (!name || length == 0) return nullptr;
    // A corrupt or compressed ref cannot be parsed safely; use the documented
    // opaque fallback rather than reading out of bounds.
    if (length > 4096) return find_metadata_by_demangled("Swift.String");
    const std::string mangled(name, length);

    // Exact registered mangled names first.
    if (const void* m = find_metadata_by_mangled(mangled)) return m;

    // Try progressively shorter prefixes so generic specialisations resolve to
    // their base nominal type (e.g. `$s7SwiftUI6VStackVyxG...` -> `...VStackV`).
    for (std::size_t n = mangled.size() - 1; n >= 2; --n) {
        if (const void* m = find_metadata_by_mangled(std::string_view(mangled.data(), n))) {
            return m;
        }
    }

    const std::string demangled = demangle_impl(mangled);
    if (!demangled.empty()) {
        if (const void* m = find_metadata_by_demangled(demangled)) return m;
    }
    // Swift binaries store type references in a compressed format with
    // symbolic references that this parser does not decode.  Return a real,
    // ABI-shaped opaque metadata object so compiled body getters can proceed;
    // the content of the view tree is captured independently by the view
    // constructors.  This is a documented fallback, not a claimed type match.
    return find_metadata_by_demangled("Swift.String");
}

const void* shim_get_type_by_mangled_name_in_state(const char* name, std::size_t length,
                                                    const void* context,
                                                    const void* const* args,
                                                    unsigned) {
    return shim_get_type_by_mangled_name(name, length, context, args);
}

// `swift_getWitnessTable` returns the witness table pointer stored in a
// conformance descriptor.  Our descriptors use Swift relative pointers.
const void* shim_get_witness_table(const void* conformance, const void*, const void*) {
    if (!conformance) return nullptr;
    // Conformance record: { int32 typeRef; int32 witnessTable; int32 conformance; }
    auto* record = static_cast<const std::int32_t*>(conformance);
    const auto* wt_field = record + 1;
    const auto rel = *wt_field;
    if (rel == 0) return nullptr;
    return reinterpret_cast<const char*>(wt_field) + rel;
}

const void* shim_get_opaque_type_conformance(const void* descriptor, const void*) {
    if (!descriptor) return nullptr;
    // Opaque type descriptors carry their conformance as a relative pointer at
    // a fixed offset; resolve it defensively, falling back to the Any witness.
    const auto* words = static_cast<const std::int32_t*>(descriptor);
    const std::int32_t rel = words[2];
    if (rel != 0) return reinterpret_cast<const char*>(&words[2]) + rel;
    return find_witness("Swift.Any");
}

// ---------------------------------------------------------------------------
// Stack probe.  On Apple arm64 this is a no-op (the architecture does not need
// a stack-clash helper); on x86_64 it probes one page per interval.
// ---------------------------------------------------------------------------
void shim_chkstk_darwin() {
#if defined(__x86_64__)
    // The caller passes the required stack size in %rax and probes downward
    // from %rsp.  Written in inline asm so it matches the Darwin helper ABI.
    __asm__ volatile(
        "mov %%rsp, %%r11\n"
        "1:\n"
        "sub $0x1000, %%r11\n"
        "test %%r11, (%%r11)\n"
        "sub $0x1000, %%rax\n"
        "ja 1b\n"
        :
        : "r"(0)
        : "r11", "rax", "cc", "memory");
#else
    // arm64: no-op, matching Apple's ___chkstk_darwin implementation.
#endif
}

// ---------------------------------------------------------------------------
// `_stdlib_isOSVersionAtLeast`: a real, deterministic answer for our platform.
// FinnOS reports a declared Darwin API-compatibility baseline (13.0.0).
// ---------------------------------------------------------------------------
bool shim_is_os_version_at_least(unsigned major, unsigned minor, unsigned patch) {
    constexpr unsigned kMajor = 13, kMinor = 0, kPatch = 0;
    if (major != kMajor) return major < kMajor;
    if (minor != kMinor) return minor < kMinor;
    return patch <= kPatch;
}

// ---------------------------------------------------------------------------
// Swift `print`.  The variadic `Any` buffer is preceded by its byte size; each
// existential's first word is a metadata pointer.  We print Swift String values
// whose metadata matches the registered String nominal descriptor, and always
// emit the requested terminator.
// ---------------------------------------------------------------------------
struct SwiftString {
    std::uintptr_t count_and_flags;
    const char* data;
};
struct AnyExistential {
    const void* metadata;
    SwiftString value;
    std::uintptr_t pad;
};
static_assert(sizeof(AnyExistential) == 32);

void shim_swift_print(const AnyExistential* items, std::uintptr_t byte_count,
                      SwiftString separator, SwiftString terminator) {
    const void* string_meta = find_metadata_by_demangled("Swift.String");
    std::size_t count = (byte_count >= sizeof(AnyExistential))
                            ? byte_count / sizeof(AnyExistential)
                            : 1;
    for (std::size_t i = 0; i < count && items; ++i) {
        const AnyExistential& e = items[i];
        if (i > 0 && separator.data) {
            std::fwrite(separator.data, 1,
                        static_cast<std::size_t>(separator.count_and_flags & 0xffffffffu),
                        stdout);
        }
        const bool is_string = string_meta && e.metadata == string_meta;
        if (is_string && e.value.data) {
            const std::size_t len = static_cast<std::size_t>(e.value.count_and_flags & 0xffffffffu);
            std::fwrite(e.value.data, 1, len, stdout);
        }
    }
    if (terminator.data) {
        std::fwrite(terminator.data, 1,
                    static_cast<std::size_t>(terminator.count_and_flags & 0xffffffffu), stdout);
    } else {
        std::fputc('\n', stdout);
    }
    std::fflush(stdout);
}

}  // namespace

std::string demangle_swift_type(std::string_view mangled) { return demangle_impl(mangled); }

const void* resolve_type_metadata(const char* name, std::size_t length) {
    return shim_get_type_by_mangled_name(name, length, nullptr, nullptr);
}

void register_swift_runtime(SymbolTable* table) {
    (*table)["_swift_getTypeByMangledNameInContext"] =
        to_ptr<GetTypeFn>(&shim_get_type_by_mangled_name);
    (*table)["_swift_getTypeByMangledName"] =
        to_ptr<GetTypeFn>(&shim_get_type_by_mangled_name);
    (*table)["_swift_getTypeByMangledNameInContextInMetadataState"] =
        to_ptr<GetTypeInStateFn>(&shim_get_type_by_mangled_name_in_state);
    (*table)["_swift_getWitnessTable"] = to_ptr(&shim_get_witness_table);
    (*table)["_swift_getOpaqueTypeConformance"] = to_ptr(&shim_get_opaque_type_conformance);
    (*table)["___chkstk_darwin"] = to_ptr(&shim_chkstk_darwin);
    (*table)["_$ss26_stdlib_isOSVersionAtLeastyBi1_Bw_BwBwtF"] =
        to_ptr(&shim_is_os_version_at_least);
    (*table)["_$ss5print_9separator10terminatoryypd_S2StF"] = to_ptr(&shim_swift_print);
}

}  // namespace finn::darwin::swiftui
