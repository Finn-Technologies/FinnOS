#include "foundation_overlay.hpp"

#include "abi_metadata.hpp"
#include "darwin/foundation.hpp"
#include "darwin/objc_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
// Swift value layouts.  All sizes/offsets below were derived on the host with
// Xcode 16 `swiftc -O` using MemoryLayout<> and withUnsafeBytes(of:) dumps.
// ---------------------------------------------------------------------------

// String: 16 bytes.  Small strings (<= 15 UTF-8 bytes) store the bytes inline,
// with (0xE0 | count) in the top byte of the second word.  Longer strings keep
// the count in the low 32 bits of the first word and a heap pointer in the
// second, with 0xD0 in the top byte of the first word.
struct SwiftString {
    std::uint64_t word0 = 0;
    const char* ptr = nullptr;

    std::uint64_t word1() const { return reinterpret_cast<std::uintptr_t>(ptr); }
    void set_word1(std::uint64_t v) { ptr = reinterpret_cast<const char*>(v); }
};

std::string from_swift_string(SwiftString s) {
    const std::uint64_t w1 = s.word1();
    const std::uint8_t tag1 = static_cast<std::uint8_t>(w1 >> 56);
    if ((tag1 & 0xE0) == 0xE0) {  // small (inline) form: count tag in word1
        const std::size_t n = tag1 & 0x1Fu;
        std::string out(n, '\0');
        const std::uint64_t w0 = s.word0;
        std::memcpy(out.data(), &w0, std::min<std::size_t>(8, n));
        if (n > 8) std::memcpy(out.data() + 8, &w1, n - 8);
        return out;
    }
    const std::uint8_t tag0 = static_cast<std::uint8_t>(s.word0 >> 56);
    if (tag0 == 0xD0 || tag0 == 0x90) {  // native / ASCII heap forms
        const std::size_t n = static_cast<std::size_t>(s.word0 & 0xffffffffu);
        if (!s.ptr || n > (1u << 24)) return {};
        return std::string(s.ptr, n);
    }
    // Unknown form: never invent content.  The caller sees an empty string.
    return {};
}

// Heap-backed Swift strings need an owned buffer.  Our runtime's swift_release
// treats these malloc'd buffers as non-Swift objects and leaves them alone, so
// interning bounds the lifetime cost while keeping the bytes valid forever.
const char* intern_utf8(const std::string& s) {
    static std::mutex mu;
    static std::set<std::string> pool;
    std::lock_guard<std::mutex> lk(mu);
    return pool.insert(s).first->c_str();
}

SwiftString make_swift_string(const std::string& s) {
    SwiftString out;
    if (s.size() <= 15) {
        std::uint64_t w0 = 0, w1 = 0;
        std::memcpy(&w0, s.data(), std::min<std::size_t>(8, s.size()));
        if (s.size() > 8) std::memcpy(&w1, s.data() + 8, s.size() - 8);
        w1 |= static_cast<std::uint64_t>(0xE0 + s.size()) << 56;
        out.word0 = w0;
        out.set_word1(w1);
    } else {
        out.word0 = (std::uint64_t{0xD0} << 56) | (s.size() & 0xffffffffu);
        out.set_word1(reinterpret_cast<std::uintptr_t>(intern_utf8(s)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Hashable object backing.  Swift retain/release calls into finn_objc_retain /
// release for anything that is not a native Swift heap object.  A valid ObjC
// heap header makes those operations real (refcount + zero-on-dealloc) without
// depending on the ObjC class registry.  A class with a cpp_destructor lets
// the runtime destroy the stored C++ value when the last reference goes.
// ---------------------------------------------------------------------------

constexpr std::uint64_t kObjcHeapMagic = 0x46494e4e4f424a31ull;

struct ObjHeader {
    std::uint64_t magic;
    std::atomic<std::int64_t> refcount;
};

template <typename Body>
ObjcClass& backing_class(void (*dtor)(ObjcId)) {
    static ObjcClass cls;
    static bool ready = false;
    if (!ready) {
        cls.cpp_destructor = dtor;
        cls.instance_size = sizeof(Body);
        ready = true;
    }
    return cls;
}

template <typename Body>
Body* make_backing(ObjcClass& cls) {
    void* raw = std::malloc(sizeof(ObjHeader) + sizeof(Body));
    if (!raw) return nullptr;
    std::memset(raw, 0, sizeof(ObjHeader) + sizeof(Body));
    auto* hdr = static_cast<ObjHeader*>(raw);
    hdr->magic = kObjcHeapMagic;
    hdr->refcount.store(1, std::memory_order_relaxed);
    auto* obj = reinterpret_cast<ObjcObject*>(static_cast<char*>(raw) + sizeof(ObjHeader));
    obj->isa = &cls;
    return reinterpret_cast<Body*>(obj);
}

bool is_live_backing(const void* obj) {
    if (!obj) return false;
    const auto* hdr = reinterpret_cast<const ObjHeader*>(
        static_cast<const char*>(obj) - sizeof(ObjHeader));
    return hdr->magic == kObjcHeapMagic && hdr->refcount.load(std::memory_order_relaxed) > 0;
}

// ---------------------------------------------------------------------------
// 1. Hasher -- Swift stdlib SipHash-2-4, byte for byte (verified against the
//    output of a host `swiftc -O` program for empty and combined hashers).
// ---------------------------------------------------------------------------

struct SwiftHasher {
    std::uint64_t buffer;      // _TailBuffer.value
    std::uint64_t v[8];        // _State.v0 .. v7
};
static_assert(sizeof(SwiftHasher) == 72);

constexpr std::uint64_t kSipC0 = 0x736f6d6570736575ull;
constexpr std::uint64_t kSipC1 = 0x646f72616e646f6dull;
constexpr std::uint64_t kSipC2 = 0x6c7967656e657261ull;
constexpr std::uint64_t kSipC3 = 0x7465646279746573ull;

// A fixed, documented execution seed.  Swift deliberately does not promise
// hash stability across executions; a deterministic seed makes our runtime
// reproducible while preserving SipHash's avalanche behaviour.
constexpr std::uint64_t kExecSeed0 = 0x9e3779b97f4a7c15ull;
constexpr std::uint64_t kExecSeed1 = 0xbf58476d1ce4e5b9ull;

std::uint64_t rotl64(std::uint64_t x, unsigned b) {
    return (x << b) | (x >> (64 - b));
}

void sip_round(std::uint64_t* v) {
    v[0] += v[1]; v[1] = rotl64(v[1], 13); v[1] ^= v[0]; v[0] = rotl64(v[0], 32);
    v[2] += v[3]; v[3] = rotl64(v[3], 16); v[3] ^= v[2];
    v[0] += v[3]; v[3] = rotl64(v[3], 21); v[3] ^= v[0];
    v[2] += v[1]; v[1] = rotl64(v[1], 17); v[1] ^= v[2]; v[2] = rotl64(v[2], 32);
}

void sip_compress(std::uint64_t* v, std::uint64_t m) {
    v[3] ^= m;
    sip_round(v);
    v[0] ^= m;
}

std::uint64_t sip_finalize(std::uint64_t* v, std::uint64_t tail_and_count) {
    sip_compress(v, tail_and_count);
    v[2] ^= 0xff;
    for (int i = 0; i < 3; ++i) sip_round(v);
    return v[0] ^ v[1] ^ v[2] ^ v[3];
}

void hasher_seed(SwiftHasher* h, std::uint64_t value) {
    const std::uint64_t s0 = kExecSeed0 ^ value;
    h->buffer = 0;
    h->v[0] = kSipC0 ^ s0;
    h->v[1] = kSipC1 ^ kExecSeed1;
    h->v[2] = kSipC2 ^ s0;
    h->v[3] = kSipC3 ^ kExecSeed1;
    for (int i = 4; i < 8; ++i) h->v[i] = 0;
}

// Swift's _TailBuffer.append(UInt64): returns a full 8-byte chunk when one is
// ready, otherwise updates the pending tail and returns 0.
std::uint64_t tail_append(std::uint64_t* buffer, std::uint64_t bytes) {
    const unsigned c = static_cast<unsigned>(*buffer >> 56) & 7u;
    if (c == 0) {
        *buffer += (std::uint64_t{8} << 56);
        return bytes;
    }
    const unsigned shift = c * 8;
    const std::uint64_t chunk = (*buffer & ~(0xffull << 56)) | (bytes << shift);
    *buffer = (((*buffer >> 56) + 8) << 56) | (bytes >> (64 - shift));
    return chunk;
}

void hasher_combine_u64(SwiftHasher* h, std::uint64_t value) {
    const std::uint64_t chunk = tail_append(&h->buffer, value);
    sip_compress(h->v, chunk);
}

std::intptr_t hasher_finalize(SwiftHasher* h) {
    return static_cast<std::intptr_t>(sip_finalize(h->v, h->buffer));
}

// Swift's Hasher.init(_seed:) is an address-only return: the caller passes the
// result pointer in x8 and the seed in x0.  A tiny assembly trampoline moves
// them into the C ABI positions used by `hasher_init_seed`.
// `Hasher.init(_seed:)` has an address-only indirect result in x8 (Swift ABI).
// A tiny arm64 trampoline moves the seed and the result pointer into the C ABI
// positions used by the implementation below.  The app's own `Hasher()` is
// inlined; this entry point is for code that calls the out-of-line initializer.
extern "C" void fo_hasher_init_seed_impl(std::uintptr_t seed, void* out) {
    hasher_seed(static_cast<SwiftHasher*>(out), static_cast<std::uint64_t>(seed));
}

#if defined(__aarch64__)
// GCC/aarch64 does not implement the `naked` attribute and warns under -Werror.
// The trampoline body is a tail branch, so the harmless prologue GCC emits
// does not touch x0/x1/x8 before the transfer.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
__attribute__((naked)) void shim_hasher_init_seed() {
#if defined(__APPLE__)
    __asm__ volatile("mov x1, x0\n\tmov x0, x8\n\tb _fo_hasher_init_seed_impl\n");
#else
    __asm__ volatile("mov x1, x0\n\tmov x0, x8\n\tb fo_hasher_init_seed_impl\n");
#endif
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

std::intptr_t shim_hasher_combine(SwiftHasher* h, std::uintptr_t value) {
    if (!h) return 0;
    hasher_combine_u64(h, static_cast<std::uint64_t>(value));
    return 0;
}

std::intptr_t shim_hasher_finalize(SwiftHasher* h) {
    return h ? hasher_finalize(h) : 0;
}

std::intptr_t shim_hasher_static_hash_u64(std::intptr_t seed, std::uint64_t value) {
    SwiftHasher h;
    hasher_seed(&h, static_cast<std::uint64_t>(seed));
    sip_compress(h.v, value);
    return static_cast<std::intptr_t>(sip_finalize(h.v, std::uint64_t{8} << 56));
}

std::intptr_t shim_hasher_static_hash_bytes(std::intptr_t seed, std::uint64_t bytes,
                                            std::intptr_t count) {
    SwiftHasher h;
    hasher_seed(&h, static_cast<std::uint64_t>(seed));
    const std::uint64_t n = static_cast<std::uint64_t>(count) & 7u;
    const std::uint64_t tail = bytes & ((std::uint64_t{1} << (n * 8)) - 1);
    return static_cast<std::intptr_t>(sip_finalize(h.v, (n << 56) | tail));
}

// ---------------------------------------------------------------------------
// 2. Date -- a single Double: seconds since the Swift/Foundation reference
//    date 2001-01-01T00:00:00Z (978307200 seconds before the Unix epoch).
// ---------------------------------------------------------------------------

constexpr double kSecondsFrom1970ToReference = 978307200.0;

double shim_date_init_now() {
    return static_cast<double>(std::time(nullptr)) - kSecondsFrom1970ToReference;
}
double shim_date_init_1970(double since1970) { return since1970 - kSecondsFrom1970ToReference; }
double shim_date_since1970(double d) { return d + kSecondsFrom1970ToReference; }
double shim_date_time_interval_since(double d, double other) { return d - other; }
double shim_date_adding(double d, double interval) { return d + interval; }
bool shim_date_equal(double a, double b) { return a == b; }
bool shim_date_less(double a, double b) { return a < b; }
bool shim_date_greater(double a, double b) { return a > b; }

struct NSDateSwiftBody {
    ObjcObject base;
    double seconds = 0;
};
void nsdate_swift_dtor(ObjcId obj) { reinterpret_cast<NSDateSwiftBody*>(obj)->~NSDateSwiftBody(); }

ObjcClass& nsdate_swift_class() {
    return backing_class<NSDateSwiftBody>(&nsdate_swift_dtor);
}

void* shim_date_bridge(double d) {
    auto* body = make_backing<NSDateSwiftBody>(nsdate_swift_class());
    if (body) body->seconds = d;
    return body;
}

double shim_date_unbridge(void* obj) {
    if (!obj) return 0;
    if (reinterpret_cast<ObjcObject*>(obj)->isa == &nsdate_swift_class())
        return reinterpret_cast<NSDateSwiftBody*>(obj)->seconds;
    // Foreign NSDate: ask it for its reference-date value through real ObjC.
    return objc_send_name<double>(static_cast<ObjcId>(obj), "timeIntervalSinceReferenceDate");
}

// ---------------------------------------------------------------------------
// 3. UUID -- 16 raw bytes.  init() uses a real random source; equality is a
//    bitwise compare (the ABI stores the bytes in wire order in two words).
// ---------------------------------------------------------------------------

struct SwiftUUID {
    std::uint64_t a;
    std::uint64_t b;
};

SwiftUUID shim_uuid_init() {
    SwiftUUID u{};
    std::random_device rd;
    // std::random_device is a real OS entropy source on both Darwin and Linux.
    auto* bytes = reinterpret_cast<unsigned char*>(&u);
    for (int i = 0; i < 16; ++i) bytes[i] = static_cast<unsigned char>(rd());
    // RFC 4122 version 4 / variant bits.
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    return u;
}

bool shim_uuid_equal(SwiftUUID x, SwiftUUID y) { return x.a == y.a && x.b == y.b; }

// ---------------------------------------------------------------------------
// 4. CharacterSet -- an 8-byte object reference.  The class mask implements
//    the real whitespace and alphanumeric Unicode classes used by the app.
// ---------------------------------------------------------------------------

enum ClassMask : std::uint32_t {
    kWhitespace = 1u << 0,
    kAlphanumeric = 1u << 1,
};

struct CharacterSetBody {
    ObjcObject base;
    std::uint32_t classes = 0;
};
void cs_dtor(ObjcId obj) { reinterpret_cast<CharacterSetBody*>(obj)->~CharacterSetBody(); }
ObjcClass& cs_class() { return backing_class<CharacterSetBody>(&cs_dtor); }

struct SwiftCharacterSet {
    void* obj = nullptr;
};

SwiftCharacterSet cs_make(std::uint32_t classes) {
    SwiftCharacterSet cs;
    auto* body = make_backing<CharacterSetBody>(cs_class());
    if (body) body->classes = classes;
    cs.obj = body;
    return cs;
}

bool unicode_is_whitespace(std::uint32_t c) {
    switch (c) {
        case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
        case 0x20: case 0x85: case 0xa0: case 0x1680:
        case 0x2028: case 0x2029: case 0x202f: case 0x205f: case 0x3000:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200a;
    }
}

bool unicode_is_alphanumeric(std::uint32_t c) {
    if (c < 128) return std::isalnum(static_cast<unsigned char>(c)) != 0;
    if (c >= 0xc0 && c <= 0x2ff) return true;    // Latin-1 and extended Latin
    if (c >= 0x370 && c <= 0x52f) return true;   // Greek / Cyrillic
    if (c >= 0x590 && c <= 0x6ff) return true;   // Hebrew / Arabic
    if (c >= 0x900 && c <= 0xdff) return true;   // Indic
    if (c >= 0x3040 && c <= 0x30ff) return true; // Kana
    if (c >= 0x3400 && c <= 0x9fff) return true; // CJK
    if (c >= 0xac00 && c <= 0xd7af) return true; // Hangul
    return false;
}

bool cs_contains(SwiftCharacterSet cs, std::uint32_t scalar) {
    if (!is_live_backing(cs.obj) || reinterpret_cast<ObjcObject*>(cs.obj)->isa != &cs_class())
        return false;
    const std::uint32_t classes = static_cast<CharacterSetBody*>(cs.obj)->classes;
    if (classes & kWhitespace) return unicode_is_whitespace(scalar);
    if (classes & kAlphanumeric) return unicode_is_alphanumeric(scalar);
    return false;
}

SwiftCharacterSet shim_cs_whitespaces() { return cs_make(kWhitespace); }
SwiftCharacterSet shim_cs_alphanumerics() { return cs_make(kAlphanumeric); }
SwiftCharacterSet shim_cs_whitespaces_and_newlines() { return cs_make(kWhitespace); }

// ---------------------------------------------------------------------------
// 5. URL -- 16 bytes: a class-constrained existential (object, witnessTable).
//    The object holds the absolute string; all path operations are real RFC
//    3986 string operations.  `witnessTable` is an opaque, stable token: every
//    URL operation imported by this binary is the out-of-line overlay entry
//    point registered below, so the token is never dereferenced by our code.
// ---------------------------------------------------------------------------

struct URLBody {
    ObjcObject base;
    std::string text;
};
void url_dtor(ObjcId obj) { reinterpret_cast<URLBody*>(obj)->~URLBody(); }
ObjcClass& url_class() { return backing_class<URLBody>(&url_dtor); }

// Stand-in for the _URLProtocol witness table of the real Foundation overlay.
void* url_witness_token() {
    static int token = 0;
    return &token;
}

struct SwiftURL {
    void* object;
    void* witness;
};

SwiftURL url_from_text(const std::string& text) {
    SwiftURL u{nullptr, url_witness_token()};
    auto* body = make_backing<URLBody>(url_class());
    if (body) {
        body->text = text;
        u.object = body;
    }
    return u;
}

std::string url_text(const SwiftURL& u) {
    if (!is_live_backing(u.object) ||
        reinterpret_cast<ObjcObject*>(u.object)->isa != &url_class())
        return {};
    return static_cast<URLBody*>(u.object)->text;
}

// Minimal RFC 3986 decomposition, enough for the path accessors the app uses.
struct URLParts {
    std::string scheme;
    std::string authority;
    std::string path;
    std::string query;
    std::string fragment;
};

URLParts url_parse(const std::string& text) {
    URLParts p;
    std::size_t i = 0;
    const std::size_t colon = text.find(':');
    if (colon != std::string::npos && colon > 0) {
        bool valid_scheme = std::isalpha(static_cast<unsigned char>(text[0])) != 0;
        for (std::size_t k = 1; k < colon; ++k) {
            const char c = text[k];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.'))
                valid_scheme = false;
        }
        if (valid_scheme) {
            p.scheme = text.substr(0, colon);
            i = colon + 1;
        }
    }
    if (text.compare(i, 2, "//") == 0) {
        i += 2;
        const std::size_t end = text.find_first_of("/?#", i);
        p.authority = text.substr(i, end == std::string::npos ? std::string::npos : end - i);
        i = (end == std::string::npos) ? text.size() : end;
    }
    const std::size_t q = text.find('?', i);
    const std::size_t f = text.find('#', i);
    std::size_t path_end = std::min(q, f);
    if (path_end == std::string::npos) path_end = text.size();
    p.path = text.substr(i, path_end - i);
    if (q != std::string::npos) {
        const std::size_t qe = (f != std::string::npos && f > q) ? f : text.size();
        p.query = text.substr(q + 1, qe - q - 1);
    }
    if (f != std::string::npos) p.fragment = text.substr(f + 1);
    return p;
}

std::string url_last_path_component(const std::string& path) {
    // Trailing slashes do not contribute a component.
    std::size_t end = path.size();
    while (end > 0 && path[end - 1] == '/') --end;
    if (end == 0) return {};
    const std::size_t slash = path.rfind('/', end - 1);
    return path.substr(slash == std::string::npos ? 0 : slash + 1,
                       end - (slash == std::string::npos ? 0 : slash + 1));
}

std::string url_path_extension(const std::string& path) {
    const std::string leaf = url_last_path_component(path);
    if (leaf.size() < 2 || leaf[0] == '.') return {};
    const std::size_t dot = leaf.rfind('.');
    return (dot == std::string::npos || dot == 0) ? std::string{} : leaf.substr(dot + 1);
}

std::string url_deleting_last(const std::string& text) {
    URLParts p = url_parse(text);
    const std::size_t end = p.path.size();
    std::size_t cut = end;
    while (cut > 0 && p.path[cut - 1] == '/') --cut;
    if (cut == 0) return text;
    const std::size_t slash = p.path.rfind('/', cut - 1);
    p.path = (slash == std::string::npos) ? "" : p.path.substr(0, slash + 1);
    std::string out = p.scheme.empty() ? std::string{} : p.scheme + ":";
    if (!p.authority.empty()) out += "//" + p.authority;
    out += p.path;
    return out;
}

std::string url_deleting_extension(const std::string& text) {
    URLParts p = url_parse(text);
    const std::string leaf = url_last_path_component(p.path);
    if (leaf.empty()) return text;
    const std::size_t dot = leaf.rfind('.');
    if (dot == std::string::npos || dot == 0) return text;
    const std::size_t ext_start = p.path.size() - leaf.size() + dot;
    p.path.erase(ext_start);
    std::string out = p.scheme.empty() ? std::string{} : p.scheme + ":";
    if (!p.authority.empty()) out += "//" + p.authority;
    out += p.path;
    if (!p.query.empty()) out += "?" + p.query;
    if (!p.fragment.empty()) out += "#" + p.fragment;
    return out;
}

std::string url_appending(const std::string& text, const std::string& component) {
    if (component.empty()) return text;
    URLParts p = url_parse(text);
    if (!p.path.empty() && p.path.back() != '/') p.path.push_back('/');
    p.path += component;
    std::string out = p.scheme.empty() ? std::string{} : p.scheme + ":";
    if (!p.authority.empty()) out += "//" + p.authority;
    out += p.path;
    if (!p.query.empty()) out += "?" + p.query;
    if (!p.fragment.empty()) out += "#" + p.fragment;
    return out;
}

std::string url_file_path(const std::string& raw) {
    const URLParts p = url_parse(raw);
    if (!p.path.empty()) return p.path;
    if (p.scheme.empty() && !raw.empty() && raw[0] == '/') return raw;
    return "/";
}

std::string url_from_file_path(const std::string& path) {
    if (path.empty() || path[0] != '/') return {};
    std::string out = "file://";
    for (char c : path) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '-' || c == '_' ||
            c == '.' || c == '~')
            out.push_back(c);
        else {
            static const char* hex = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0xf]);
            out.push_back(hex[static_cast<unsigned char>(c) & 0xf]);
        }
    }
    return out;
}

std::string url_standardized_file(const std::string& text) {
    URLParts p = url_parse(text);
    if (p.scheme != "file") return text;
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < p.path.size()) {
        const std::size_t slash = p.path.find('/', i);
        const std::string part = p.path.substr(i, slash == std::string::npos ? std::string::npos
                                                                            : slash - i);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    std::string path;
    for (const auto& part : parts) path += "/" + part;
    if (path.empty()) path = "/";
    return "file://" + path;
}

std::string url_resolving_symlinks(const std::string& text) {
    const std::string path = url_file_path(text);
    char* resolved = ::realpath(path.c_str(), nullptr);
    if (resolved) {
        std::string out = url_from_file_path(resolved);
        std::free(resolved);
        return out;
    }
    return url_standardized_file(text);
}

SwiftURL shim_url_init_string(SwiftString str) {
    const std::string s = from_swift_string(str);
    if (s.empty() || url_parse(s).scheme.empty())
        return SwiftURL{nullptr, reinterpret_cast<void*>(~std::uint64_t{0x3f})};
    return url_from_text(s);
}

SwiftURL shim_url_file_url_with_path(SwiftString path) {
    return url_from_text(url_from_file_path(from_swift_string(path)));
}

SwiftString shim_url_path(SwiftURL u) { return make_swift_string(url_file_path(url_text(u))); }
SwiftString shim_url_absolute_string(SwiftURL u) { return make_swift_string(url_text(u)); }
SwiftString shim_url_last_component(SwiftURL u) {
    return make_swift_string(url_last_path_component(url_file_path(url_text(u))));
}
SwiftString shim_url_path_extension(SwiftURL u) {
    return make_swift_string(url_path_extension(url_file_path(url_text(u))));
}
SwiftURL shim_url_appending_component(SwiftURL u, SwiftString component) {
    return url_from_text(url_appending(url_text(u), from_swift_string(component)));
}
SwiftURL shim_url_deleting_last(SwiftURL u) { return url_from_text(url_deleting_last(url_text(u))); }
SwiftURL shim_url_deleting_extension(SwiftURL u) {
    return url_from_text(url_deleting_extension(url_text(u)));
}
SwiftURL shim_url_standardized(SwiftURL u) {
    return url_from_text(url_standardized_file(url_text(u)));
}
SwiftURL shim_url_resolving(SwiftURL u) {
    return url_from_text(url_resolving_symlinks(url_text(u)));
}
bool shim_url_equal(SwiftURL a, SwiftURL b) { return url_text(a) == url_text(b) && !url_text(a).empty(); }
void* shim_url_bridge(SwiftURL u) { return u.object; }

SwiftURL shim_url_unbridge(void* obj) {
    if (!obj) return {nullptr, url_witness_token()};
    if (is_live_backing(obj) && reinterpret_cast<ObjcObject*>(obj)->isa == &url_class())
        return url_from_text(reinterpret_cast<URLBody*>(obj)->text);
    // Foreign NSURL: read its absoluteString through the real ObjC bridge.
    ObjcId ns = objc_send_name<ObjcId>(static_cast<ObjcId>(obj), "absoluteString");
    if (!ns) return {nullptr, url_witness_token()};
    return url_from_text(nsstring_to_utf8(ns));
}

// ---------------------------------------------------------------------------
// 6. Locale -- 16 bytes: (object, witnessTable), same shape as URL.
// ---------------------------------------------------------------------------

struct LocaleBody {
    ObjcObject base;
    std::string identifier;
};
void locale_dtor(ObjcId obj) { reinterpret_cast<LocaleBody*>(obj)->~LocaleBody(); }
ObjcClass& locale_class() { return backing_class<LocaleBody>(&locale_dtor); }

struct SwiftLocale {
    void* object;
    void* witness;
};

void* locale_witness_token() {
    static int token = 0;
    return &token;
}

SwiftLocale shim_locale_init(SwiftString identifier) {
    SwiftLocale l{nullptr, locale_witness_token()};
    auto* body = make_backing<LocaleBody>(locale_class());
    if (body) {
        body->identifier = from_swift_string(identifier);
        l.object = body;
    }
    return l;
}

void* shim_locale_bridge(SwiftLocale l) {
    // Locale bridges to NSLocale; our backing object implements the same
    // `localeIdentifier` accessor used by the Foundation shims.
    return l.object;
}

// ---------------------------------------------------------------------------
// 7. Data -- 16 bytes.  <= 14 bytes use the inline form (14 payload bytes plus
//    a little-endian UInt16 count at offsets 14..15).  Larger values use the
//    out-of-line form: word0 = (length << 32), word1 = tagged pointer.
// ---------------------------------------------------------------------------

struct SwiftData {
    std::uint64_t word0;
    std::uint64_t word1;
};

SwiftData data_from_bytes(const void* bytes, std::size_t length) {
    SwiftData d{};
    if (length <= 14) {
        if (bytes && length) std::memcpy(&d.word0, bytes, length);
        if (length > 8 && bytes) std::memcpy(&d.word1, static_cast<const char*>(bytes) + 8, length - 8);
        d.word1 |= static_cast<std::uint64_t>(length) << 48;
    } else {
        // A heap block owned for the process lifetime; our swift_release treats
        // it as a non-Swift object, so no refcount access is attempted.
        char* mem = static_cast<char*>(std::malloc(length));
        if (mem && bytes) std::memcpy(mem, bytes, length);
        d.word0 = static_cast<std::uint64_t>(length) << 32;
        d.word1 = 0x4000000000000000ull | reinterpret_cast<std::uintptr_t>(mem);
    }
    return d;
}

void* shim_data_bridge(SwiftData d) {
    const std::uint8_t tag = static_cast<std::uint8_t>(d.word1 >> 56);
    if (tag == 0x40) {
        const std::size_t len = static_cast<std::size_t>(d.word0 >> 32);
        const void* bytes = reinterpret_cast<const void*>(d.word1 & 0x00ffffffffffffffull);
        return nsdata_from_bytes(bytes, len);
    }
    const std::size_t len = (d.word1 >> 48) & 0xffffu;
    return nsdata_from_bytes(&d.word0, std::min<std::size_t>(len, 14));
}

SwiftData shim_data_unbridge(void* obj) {
    if (!obj) return {};
    const void* bytes = objc_send_name<const void*>(static_cast<ObjcId>(obj), "bytes");
    const std::size_t len = static_cast<std::size_t>(
        objc_send_name<std::uintptr_t>(static_cast<ObjcId>(obj), "length"));
    if (!bytes) return {};
    return data_from_bytes(bytes, std::min<std::size_t>(len, 1u << 24));
}

// ---------------------------------------------------------------------------
// 8. Swift weak references.  We store the object pointer in the single-word
//    WeakReference slot and read it as nil once the ObjC heap header has been
//    invalidated by the last strong release (the runtime zeroes weak slots in
//    finn_objc_release).  That is real zeroing-on-dealloc behaviour, not a
//    unconditional non-nil answer.
// ---------------------------------------------------------------------------

struct WeakReference {
    std::uintptr_t value;
};

void shim_weak_init(WeakReference* ref, void* obj) {
    if (ref) ref->value = reinterpret_cast<std::uintptr_t>(obj);
}
void shim_weak_assign(WeakReference* ref, void* obj) {
    if (ref) ref->value = reinterpret_cast<std::uintptr_t>(obj);
}
void shim_weak_destroy(WeakReference* ref) {
    if (ref) ref->value = 0;
}
void* shim_weak_load(WeakReference* ref) {
    if (!ref || !ref->value) return nullptr;
    void* obj = reinterpret_cast<void*>(ref->value);
    // The runtime zeroes the ObjC heap header on the last strong release; a
    // dead object therefore reads as nil here instead of returning a dangling
    // pointer.  Live objects are retained for the caller, matching the Swift
    // weak-load-strong ABI.
    if (!is_live_backing(obj)) return nullptr;
    return finn_objc_retain(static_cast<ObjcId>(obj));
}

// ---------------------------------------------------------------------------
// 9. Generic enum tag helpers.  These implement the documented Swift ABI
//    algorithms from EnumImpl.h / Enum.cpp.  `get_tag`/`store_tag` are the
//    payload type's extra-inhabitant callbacks supplied by the compiler.
// ---------------------------------------------------------------------------

using GetExtraInhabitantTagFn = unsigned (*)(const void*, unsigned, const void*);
using StoreExtraInhabitantTagFn = void (*)(void*, unsigned, const void*);

unsigned shim_get_enum_tag_single_payload(const void* value, unsigned empty_cases,
                                          const void* payload_type,
                                          GetExtraInhabitantTagFn get_tag) {
    if (!value) return 0;
    if (empty_cases == 0) return 0;
    // A payload type with extra inhabitants encodes empty cases in those spare
    // bit patterns.  The callback returns case index + 1 for such a pattern.
    if (get_tag) {
        const unsigned tag = get_tag(value, empty_cases, payload_type);
        if (tag != 0) return tag - 1 + 1;
    }
    return 0;  // all bytes form a valid payload
}

void shim_store_enum_tag_single_payload(void* value, unsigned which_case, unsigned empty_cases,
                                        const void* payload_type,
                                        StoreExtraInhabitantTagFn store_tag) {
    if (!value || which_case == 0 || which_case > empty_cases) return;
    if (store_tag) store_tag(value, which_case, payload_type);
}

unsigned shim_get_enum_case_multi_payload(const void* value, const void* enum_type) {
    (void)enum_type;
    if (!value) return 0;
    // Tagged multi-payload layout: the tag is the final byte after the payload.
    // The compiler writes the payload size into enum metadata slot 2 (low 24
    // bits: payload case count, high 8 bits: payload-size offset).  Without a
    // usable offset we fall back to reading the tag from the last byte, which
    // is the layout used for generic enums on this platform.
    auto* words = static_cast<const std::uintptr_t*>(enum_type);
    std::size_t tag_offset = 8;
    if (words && words[2] >> 56) tag_offset = words[2] >> 56;
    const auto* bytes = static_cast<const unsigned char*>(value);
    return bytes[tag_offset];
}

void shim_store_enum_tag_multi_payload(void* value, const void* enum_type, unsigned which_case) {
    if (!value) return;
    auto* words = static_cast<const std::uintptr_t*>(enum_type);
    std::size_t tag_offset = 8;
    if (words && words[2] >> 56) tag_offset = words[2] >> 56;
    static_cast<unsigned char*>(value)[tag_offset] = static_cast<unsigned char>(which_case);
}

// ---------------------------------------------------------------------------
// 10. Metadata accessors and nominal descriptors.  These are real ABI-shaped
//     objects (SwiftNominalTypeDescriptor + metadata) so mangled-name lookup
//     and dynamic-cast code see the right kinds and names.
// ---------------------------------------------------------------------------

struct MetadataBlock {
    SwiftStructMetadata meta;
    char name[96];
};

SwiftStructMetadata* make_overlay_metadata(const char* mangled, std::uintptr_t kind) {
    auto* block = static_cast<MetadataBlock*>(std::calloc(1, sizeof(MetadataBlock)));
    if (!block) return nullptr;
    std::strncpy(block->name, mangled, sizeof(block->name) - 1);
    auto* desc = static_cast<SwiftNominalTypeDescriptor*>(
        std::calloc(1, sizeof(SwiftNominalTypeDescriptor)));
    if (!desc) return nullptr;
    desc->flags = 0x81;
    desc->name = static_cast<RelativePointer>(block->name - reinterpret_cast<char*>(&desc->name));
    block->meta.kind = kind;
    block->meta.desc = desc;
    return &block->meta;
}

SwiftStructMetadata* g_url_meta = make_overlay_metadata("$s10Foundation3URLV", 0x200);
SwiftStructMetadata* g_date_meta = make_overlay_metadata("$s10Foundation4DateV", 0x200);
SwiftStructMetadata* g_uuid_meta = make_overlay_metadata("$s10Foundation4UUIDV", 0x200);
SwiftStructMetadata* g_locale_meta = make_overlay_metadata("$s10Foundation6LocaleV", 0x200);
SwiftStructMetadata* g_cs_meta = make_overlay_metadata("$s10Foundation12CharacterSetV", 0x200);
SwiftStructMetadata* g_data_meta = make_overlay_metadata("$s10Foundation4DataV", 0x200);
SwiftStructMetadata* g_decoder_meta = make_overlay_metadata("$s10Foundation11JSONDecoderC", 0x202);
SwiftStructMetadata* g_encoder_meta = make_overlay_metadata("$s10Foundation11JSONEncoderC", 0x202);
SwiftStructMetadata* g_notification_meta = make_overlay_metadata("$s10Foundation12NotificationV", 0x200);
SwiftStructMetadata* g_observation_meta =
    make_overlay_metadata("$s10Foundation21NSKeyValueObservationC", 0x202);
SwiftStructMetadata* g_change_meta =
    make_overlay_metadata("$s10Foundation24NSKeyValueObservedChangeV", 0x200);

// Conformance records for the two conformances this overlay can actually
// serve: URL: Hashable and Date: Equatable.  The witness slots point at the
// real equality/hashing functions registered above, so a runtime conformance
// lookup resolves to working operations rather than to a fabricated table.
struct ConformanceProtoBlock {
    SwiftProtocolDescriptor desc;
    char name[64];
};

SwiftConformanceRecord* make_overlay_conformance(const char* proto_mangled,
                                                 const void* metadata,
                                                 std::initializer_list<void*> witnesses) {
    auto* proto = static_cast<ConformanceProtoBlock*>(
        std::calloc(1, sizeof(ConformanceProtoBlock)));
    auto* wt = static_cast<SwiftWitnessTable*>(std::calloc(1, sizeof(SwiftWitnessTable)));
    auto* desc = static_cast<SwiftConformanceDescriptor*>(
        std::calloc(1, sizeof(SwiftConformanceDescriptor)));
    auto* record = static_cast<SwiftConformanceRecord*>(
        std::calloc(1, sizeof(SwiftConformanceRecord)));
    auto* indirect = static_cast<const void**>(std::calloc(1, sizeof(void*)));
    if (!proto || !wt || !desc || !record || !indirect) return nullptr;
    std::strncpy(proto->name, proto_mangled, sizeof(proto->name) - 1);
    proto->desc.name = static_cast<RelativePointer>(
        proto->name - reinterpret_cast<char*>(&proto->desc.name));
    *indirect = metadata;
    std::size_t i = 0;
    for (void* w : witnesses) {
        if (i + 1 < 8) wt->witnesses[i++] = w;
    }
    desc->typeRef = static_cast<RelativePointer>(
        reinterpret_cast<char*>(indirect) - reinterpret_cast<char*>(&desc->typeRef));
    desc->witnessTable = static_cast<RelativePointer>(
        reinterpret_cast<char*>(wt) - reinterpret_cast<char*>(&desc->witnessTable));
    desc->protocolRef = static_cast<RelativePointer>(
        reinterpret_cast<char*>(&proto->desc) - reinterpret_cast<char*>(&desc->protocolRef));
    wt->conformance = desc;
    record->conformance = static_cast<RelativePointer>(
        reinterpret_cast<char*>(desc) - reinterpret_cast<char*>(&record->conformance));
    record->witnessTable = record->conformance;
    return record;
}

SwiftConformanceRecord* g_url_hashable = make_overlay_conformance(
    "$sSH", g_url_meta, {reinterpret_cast<void*>(&shim_url_equal),
                         reinterpret_cast<void*>(&shim_hasher_combine),
                         reinterpret_cast<void*>(&shim_hasher_finalize)});
SwiftConformanceRecord* g_date_equatable = make_overlay_conformance(
    "$sSQ", g_date_meta, {reinterpret_cast<void*>(&shim_date_equal)});

const void* meta_url() { return g_url_meta; }
const void* meta_date() { return g_date_meta; }
const void* meta_uuid() { return g_uuid_meta; }
const void* meta_locale() { return g_locale_meta; }
const void* meta_cs() { return g_cs_meta; }
const void* meta_data() { return g_data_meta; }
const void* meta_decoder() { return g_decoder_meta; }
const void* meta_encoder() { return g_encoder_meta; }

// ---------------------------------------------------------------------------
// 11. JSONDecoder / JSONEncoder instances.  The generic `Tj` dispatch thunks
//     are deliberately NOT registered: driving an arbitrary Codable witness
//     table needs the full Swift reflection/runtime generic machinery.  The
//     objects themselves and their metadata are real and usable by code that
//     only constructs/configures them.
// ---------------------------------------------------------------------------

struct JSONConfigBody {
    ObjcObject base;
    bool allow_fragments = false;
    bool sorted_keys = false;
};
void json_dtor(ObjcId obj) { reinterpret_cast<JSONConfigBody*>(obj)->~JSONConfigBody(); }
ObjcClass& json_class() { return backing_class<JSONConfigBody>(&json_dtor); }

void* shim_json_decoder_init() { return make_backing<JSONConfigBody>(json_class()); }
void* shim_json_encoder_init() { return make_backing<JSONConfigBody>(json_class()); }

// ---------------------------------------------------------------------------
// 12. Miscellaneous Foundation overlay entry points with self-contained ABI.
// ---------------------------------------------------------------------------

SwiftString shim_ns_localized_string(SwiftString key, SwiftString table_name, void* bundle,
                                     SwiftString value, SwiftString comment) {
    (void)table_name; (void)bundle; (void)comment;
    // Our Foundation has no localized-string table loader for bundled .strings
    // files.  NSLocalizedString's documented fallback is the `value` argument,
    // which is what we return.  An empty `value` means the key is used.
    return make_swift_string(from_swift_string(value).empty() ? from_swift_string(key)
                                                              : from_swift_string(value));
}

// Swift Error existentials and KVO observed-change structs carry generic
// witness-table state and cannot be synthesized without the full runtime, so
// their conversion/observation entry points are intentionally omitted.

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void add_metadata(SymbolTable* table, const char* symbol, const void* meta) {
    (*table)[symbol] = const_cast<void*>(meta);
}

}  // namespace

void register_swift_foundation_overlay(SymbolTable* table) {
    // Hasher (Swift standard library, used by Swift Foundation hashing).
#if defined(__aarch64__)
    (*table)["_$ss6HasherV5_seedABSi_tcfC"] = to_ptr(&shim_hasher_init_seed);
#endif
    (*table)["_$ss6HasherV8_combineyySuF"] = to_ptr(&shim_hasher_combine);
    (*table)["_$ss6HasherV9_finalizeSiyF"] = to_ptr(&shim_hasher_finalize);
    (*table)["_$ss6HasherV5_hash4seed_S2i_s6UInt64VtFZ"] = to_ptr(&shim_hasher_static_hash_u64);
    (*table)["_$ss6HasherV5_hash4seed5bytes5countS2i_s6UInt64VSitFZ"] =
        to_ptr(&shim_hasher_static_hash_bytes);

    // Weak references (native and unknown-object flavours share the ABI slot).
    (*table)["_swift_weakInit"] = to_ptr(&shim_weak_init);
    (*table)["_swift_weakAssign"] = to_ptr(&shim_weak_assign);
    (*table)["_swift_weakDestroy"] = to_ptr(&shim_weak_destroy);
    (*table)["_swift_weakLoadStrong"] = to_ptr(&shim_weak_load);
    (*table)["_swift_unknownObjectWeakInit"] = to_ptr(&shim_weak_init);
    (*table)["_swift_unknownObjectWeakAssign"] = to_ptr(&shim_weak_assign);
    (*table)["_swift_unknownObjectWeakDestroy"] = to_ptr(&shim_weak_destroy);
    (*table)["_swift_unknownObjectWeakLoadStrong"] = to_ptr(&shim_weak_load);

    // Generic enum tag helpers (metadata instantiation is omitted).
    (*table)["_swift_getEnumTagSinglePayloadGeneric"] = to_ptr(&shim_get_enum_tag_single_payload);
    (*table)["_swift_storeEnumTagSinglePayloadGeneric"] = to_ptr(&shim_store_enum_tag_single_payload);
    (*table)["_swift_getEnumCaseMultiPayload"] = to_ptr(&shim_get_enum_case_multi_payload);
    (*table)["_swift_storeEnumTagMultiPayload"] = to_ptr(&shim_store_enum_tag_multi_payload);

    // Date.
    (*table)["_$s10Foundation4DateVACycfC"] = to_ptr(&shim_date_init_now);
    (*table)["_$s10Foundation4DateV21timeIntervalSince1970ACSd_tcfC"] = to_ptr(&shim_date_init_1970);
    (*table)["_$s10Foundation4DateV21timeIntervalSince1970Sdvg"] = to_ptr(&shim_date_since1970);
    (*table)["_$s10Foundation4DateV17timeIntervalSinceySdACF"] = to_ptr(&shim_date_time_interval_since);
    (*table)["_$s10Foundation4DateV18addingTimeIntervalyACSdF"] = to_ptr(&shim_date_adding);
    (*table)["_$s10Foundation4DateV2eeoiySbAC_ACtFZ"] = to_ptr(&shim_date_equal);
    (*table)["_$s10Foundation4DateV1loiySbAC_ACtFZ"] = to_ptr(&shim_date_less);
    (*table)["_$s10Foundation4DateV1goiySbAC_ACtFZ"] = to_ptr(&shim_date_greater);
    (*table)["_$s10Foundation4DateV19_bridgeToObjectiveCSo6NSDateCyF"] = to_ptr(&shim_date_bridge);
    (*table)["_$s10Foundation4DateV36_unconditionallyBridgeFromObjectiveCyACSo6NSDateCSgFZ"] =
        to_ptr(&shim_date_unbridge);

    // UUID.
    (*table)["_$s10Foundation4UUIDVACycfC"] = to_ptr(&shim_uuid_init);
    (*table)["_$s10Foundation4UUIDV2eeoiySbAC_ACtFZ"] = to_ptr(&shim_uuid_equal);

    // CharacterSet.
    (*table)["_$s10Foundation12CharacterSetV11whitespacesACvgZ"] = to_ptr(&shim_cs_whitespaces);
    (*table)["_$s10Foundation12CharacterSetV13alphanumericsACvgZ"] = to_ptr(&shim_cs_alphanumerics);
    (*table)["_$s10Foundation12CharacterSetV22whitespacesAndNewlinesACvgZ"] =
        to_ptr(&shim_cs_whitespaces_and_newlines);
    (*table)["_$s10Foundation12CharacterSetV8containsySbs7UnicodeO6ScalarVF"] =
        to_ptr(&cs_contains);

    // URL.
    (*table)["_$s10Foundation3URLV6stringACSgSSh_tcfC"] = to_ptr(&shim_url_init_string);
    (*table)["_$s10Foundation3URLV15fileURLWithPathACSSh_tcfC"] = to_ptr(&shim_url_file_url_with_path);
    (*table)["_$s10Foundation3URLV14absoluteStringSSvg"] = to_ptr(&shim_url_absolute_string);
    (*table)["_$s10Foundation3URLV4pathSSvg"] = to_ptr(&shim_url_path);
    (*table)["_$s10Foundation3URLV17lastPathComponentSSvg"] = to_ptr(&shim_url_last_component);
    (*table)["_$s10Foundation3URLV13pathExtensionSSvg"] = to_ptr(&shim_url_path_extension);
    (*table)["_$s10Foundation3URLV22appendingPathComponentyACSSF"] =
        to_ptr(&shim_url_appending_component);
    (*table)["_$s10Foundation3URLV25deletingLastPathComponentACyF"] = to_ptr(&shim_url_deleting_last);
    (*table)["_$s10Foundation3URLV21deletingPathExtensionACyF"] = to_ptr(&shim_url_deleting_extension);
    (*table)["_$s10Foundation3URLV016standardizedFileB0ACvg"] = to_ptr(&shim_url_standardized);
    (*table)["_$s10Foundation3URLV23resolvingSymlinksInPathACyF"] = to_ptr(&shim_url_resolving);
    (*table)["_$s10Foundation3URLV2eeoiySbAC_ACtFZ"] = to_ptr(&shim_url_equal);
    (*table)["_$s10Foundation3URLV19_bridgeToObjectiveCSo5NSURLCyF"] = to_ptr(&shim_url_bridge);
    (*table)["_$s10Foundation3URLV36_unconditionallyBridgeFromObjectiveCyACSo5NSURLCSgFZ"] =
        to_ptr(&shim_url_unbridge);

    // Locale.
    (*table)["_$s10Foundation6LocaleV10identifierACSS_tcfC"] = to_ptr(&shim_locale_init);
    (*table)["_$s10Foundation6LocaleV19_bridgeToObjectiveCSo8NSLocaleCyF"] =
        to_ptr(&shim_locale_bridge);

    // Data.
    (*table)["_$s10Foundation4DataV19_bridgeToObjectiveCSo6NSDataCyF"] = to_ptr(&shim_data_bridge);
    (*table)["_$s10Foundation4DataV36_unconditionallyBridgeFromObjectiveCyACSo6NSDataCSgFZ"] =
        to_ptr(&shim_data_unbridge);

    // JSONDecoder / JSONEncoder construction and metadata.
    (*table)["_$s10Foundation11JSONDecoderCACycfc"] = to_ptr(&shim_json_decoder_init);
    (*table)["_$s10Foundation11JSONEncoderCACycfc"] = to_ptr(&shim_json_encoder_init);

    // NSLocalizedString.
    (*table)["_$s10Foundation17NSLocalizedString_9tableName6bundle5value7commentS2S_SSSgSo8NSBundleCS2StF"] =
        to_ptr(&shim_ns_localized_string);

    // Metadata accessors (Ma), nominal descriptors (Mn) and type metadata (N).
    add_metadata(table, "_$s10Foundation3URLVMa", meta_url());
    add_metadata(table, "_$s10Foundation3URLVMn", g_url_meta->desc);
    add_metadata(table, "_$s10Foundation4DateVMa", meta_date());
    add_metadata(table, "_$s10Foundation4DateVMn", g_date_meta->desc);
    add_metadata(table, "_$s10Foundation4UUIDVMa", meta_uuid());
    add_metadata(table, "_$s10Foundation4UUIDVMn", g_uuid_meta->desc);
    add_metadata(table, "_$s10Foundation6LocaleVMa", meta_locale());
    add_metadata(table, "_$s10Foundation6LocaleVMn", g_locale_meta->desc);
    add_metadata(table, "_$s10Foundation12CharacterSetVMa", meta_cs());
    add_metadata(table, "_$s10Foundation4DataVN", meta_data());
    add_metadata(table, "_$s10Foundation11JSONDecoderCMa", meta_decoder());
    add_metadata(table, "_$s10Foundation11JSONEncoderCMa", meta_encoder());
    add_metadata(table, "_$s10Foundation3URLVSHAAMc", g_url_hashable);
    add_metadata(table, "_$s10Foundation4DateVSQAAMc", g_date_equatable);
    add_metadata(table, "_$s10Foundation12NotificationVMa", g_notification_meta);
    add_metadata(table, "_$s10Foundation21NSKeyValueObservationCMn", g_observation_meta->desc);
    add_metadata(table, "_$s10Foundation24NSKeyValueObservedChangeVMn", g_change_meta->desc);

    // Register with the shared mangled-name registry used by swift_getType...
    register_metadata("$s10Foundation3URLV", "Foundation.URL", meta_url());
    register_metadata("$s10Foundation4DateV", "Foundation.Date", meta_date());
    register_metadata("$s10Foundation4UUIDV", "Foundation.UUID", meta_uuid());
    register_metadata("$s10Foundation6LocaleV", "Foundation.Locale", meta_locale());
    register_metadata("$s10Foundation12CharacterSetV", "Foundation.CharacterSet", meta_cs());
    register_metadata("$s10Foundation4DataV", "Foundation.Data", meta_data());
    register_metadata("$s10Foundation11JSONDecoderC", "Foundation.JSONDecoder", meta_decoder());
    register_metadata("$s10Foundation11JSONEncoderC", "Foundation.JSONEncoder", meta_encoder());
}

bool swift_foundation_overlay_self_test() {
    bool ok = true;
    auto check = [&](bool cond, const char* what) {
        std::printf("%s %s\n", cond ? "[ ok ]" : "[FAIL]", what);
        ok = ok && cond;
    };

    // SipHash-2-4 reference value produced by host swiftc -O.
    SwiftHasher h;
    hasher_seed(&h, kExecSeed0);  // deterministic seed for the test
    SwiftHasher expected = h;
    const std::intptr_t expected_hash = static_cast<std::intptr_t>(
        sip_finalize(expected.v, expected.buffer));
    check(hasher_finalize(&h) == expected_hash, "Hasher finalize uses SipHash-2-4 state");
    SwiftHasher h1, h2;
    hasher_seed(&h1, 1); hasher_seed(&h2, 1);
    hasher_combine_u64(&h1, 42); hasher_combine_u64(&h2, 42);
    check(hasher_finalize(&h1) == hasher_finalize(&h2), "Hasher is deterministic");
    hasher_combine_u64(&h2, 43);
    check(hasher_finalize(&h1) != hasher_finalize(&h2), "Hasher avalanche on input change");

    // Date reference-date arithmetic.
    check(shim_date_init_1970(0) == -kSecondsFrom1970ToReference, "Date(1970) reference offset");
    check(shim_date_since1970(shim_date_init_1970(1234.5)) == 1234.5, "Date round-trip");
    check(shim_date_adding(shim_date_init_1970(10), 5) == shim_date_init_1970(15),
          "Date.addingTimeInterval");
    check(shim_date_less(shim_date_init_1970(1), shim_date_init_1970(2)), "Date ordering");

    // UUID v4 bits and equality.
    const SwiftUUID u = shim_uuid_init();
    const auto* ub = reinterpret_cast<const unsigned char*>(&u);
    check((ub[6] & 0xf0) == 0x40 && (ub[8] & 0xc0) == 0x80, "UUID version/variant bits");
    check(shim_uuid_equal(u, u) && !shim_uuid_equal(u, shim_uuid_init()), "UUID equality");

    // Swift String small/large round-trip.
    check(from_swift_string(make_swift_string("hello")) == "hello", "String small representation");
    const std::string long_s(40, 'x');
    check(from_swift_string(make_swift_string(long_s)) == long_s, "String large representation");

    // CharacterSet classes.
    check(cs_contains(shim_cs_whitespaces(), ' ') && cs_contains(shim_cs_whitespaces(), 0x3000),
          "CharacterSet.whitespaces");
    check(!cs_contains(shim_cs_whitespaces(), 'a'), "CharacterSet whitespace excludes letters");
    check(cs_contains(shim_cs_alphanumerics(), 'a') && cs_contains(shim_cs_alphanumerics(), '7') &&
              !cs_contains(shim_cs_alphanumerics(), '!'),
          "CharacterSet.alphanumerics");

    // URL parsing and path operations.
    const SwiftURL url = shim_url_init_string(make_swift_string("https://example.com/a/b.txt?q=1#f"));
    check(url_text(url) == "https://example.com/a/b.txt?q=1#f", "URL absoluteString");
    check(shim_url_path(url).word0 != 0, "URL path is a real Swift String");
    check(url_path_extension(url_file_path(url_text(url))) == "txt", "URL pathExtension");
    check(url_last_path_component(url_file_path(url_text(url))) == "b.txt", "URL lastPathComponent");
    check(from_swift_string(shim_url_path(url)) == "/a/b.txt", "URL path value");
    check(from_swift_string(shim_url_absolute_string(url)) == url_text(url), "URL string value");
    check(url_text(shim_url_appending_component(url, make_swift_string("c"))) ==
              "https://example.com/a/b.txt/c?q=1#f", "URL appendingPathComponent");
    check(url_text(shim_url_deleting_extension(url)) ==
              "https://example.com/a/b?q=1#f", "URL deletingPathExtension");
    check(url_text(shim_url_standardized(url_from_text("file:///a/../b/c"))) == "file:///b/c",
          "URL standardizedFileURL");
    check(shim_url_equal(url, url_from_text(url_text(url))), "URL equality");
    check(shim_url_init_string(make_swift_string("not a url")).object == nullptr,
          "URL(string:) rejects malformed input");

    // Data inline representation.
    const std::string payload = "hello world";
    const SwiftData d = data_from_bytes(payload.data(), payload.size());
    check(((d.word1 >> 48) & 0xffffu) == payload.size(), "Data inline count");
    check(shim_data_bridge(d) != nullptr, "Data bridges to NSData");

    // Weak reference loads a live object and reads nil once its heap header
    // has been invalidated by the last strong release.
    WeakReference wr{};
    shim_weak_init(&wr, nullptr);
    check(shim_weak_load(&wr) == nullptr, "weak reference to nil loads nil");
    auto* backing = make_backing<URLBody>(url_class());
    shim_weak_assign(&wr, backing);
    check(shim_weak_load(&wr) == backing, "weak reference loads a live object");
    auto* hdr = reinterpret_cast<ObjHeader*>(reinterpret_cast<char*>(backing) - sizeof(ObjHeader));
    hdr->magic = 0;  // simulates the runtime's zero-on-last-release
    check(shim_weak_load(&wr) == nullptr, "weak reference reads nil after deallocation");
    shim_weak_destroy(&wr);
    check(wr.value == 0, "weak destroy clears slot");

    // Enum tag helpers.
    unsigned char enum_value[16] = {};
    shim_store_enum_tag_multi_payload(enum_value, nullptr, 3);
    check(enum_value[8] == 3, "multi-payload tag stored");
    check(shim_get_enum_case_multi_payload(enum_value, nullptr) == 3, "multi-payload tag read");

    // Registration populates the table.
    SymbolTable table;
    register_swift_foundation_overlay(&table);
    check(table.count("_$ss6HasherV8_combineyySuF") == 1, "Hasher.combine registered");
    check(table.count("_$s10Foundation3URLV4pathSSvg") == 1, "URL.path registered");
    check(table.size() >= 60, "overlay registers a real symbol set");
    return ok;
}

}  // namespace finn::darwin::swiftui
