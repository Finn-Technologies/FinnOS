#include "android/android_format.hpp"

#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>

namespace finn::android {
namespace {

std::uint32_t rotl32(std::uint32_t v, unsigned n) {
    return (v << n) | (v >> (32u - n));
}

// SHA-1 is used for the DEX signature field. Implemented locally so APK/DEX
// verification has no OpenSSL dependency and no network call.
bool sha1_bytes(const std::uint8_t* data, std::size_t len,
                std::uint8_t out[20]) {
    const std::size_t padded = ((len + 9u + 63u) / 64u) * 64u;
    if (padded < len || padded > (std::numeric_limits<std::size_t>::max() - 64u)) {
        return false;
    }
    std::vector<std::uint8_t> msg(padded, 0u);
    if (len > 0) std::memcpy(msg.data(), data, len);
    msg[len] = 0x80u;
    const std::uint64_t bits = static_cast<std::uint64_t>(len) * 8u;
    for (int i = 0; i < 8; ++i) {
        msg[padded - 1u - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bits >> (i * 8));
    }

    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu,
                          0x10325476u, 0xC3D2E1F0u};
    std::uint32_t w[80] = {};
    for (std::size_t off = 0; off < padded; off += 64u) {
        for (int i = 0; i < 16; ++i) {
            const std::uint8_t* p = msg.data() + off + static_cast<std::size_t>(i) * 4u;
            w[i] = (static_cast<std::uint32_t>(p[0]) << 24) |
                   (static_cast<std::uint32_t>(p[1]) << 16) |
                   (static_cast<std::uint32_t>(p[2]) << 8) |
                   static_cast<std::uint32_t>(p[3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    for (int i = 0; i < 5; ++i) {
        out[i * 4 + 0] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return true;
}

void append_utf8(std::string* out, std::uint32_t cp) {
    if (cp <= 0x7fu) {
        out->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ffu) {
        out->push_back(static_cast<char>(0xc0u | (cp >> 6)));
        out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
    } else if (cp <= 0xffffu) {
        out->push_back(static_cast<char>(0xe0u | (cp >> 12)));
        out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
        out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
    } else {
        out->push_back(static_cast<char>(0xf0u | (cp >> 18)));
        out->push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3fu)));
        out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
        out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
    }
}

std::string hex32(std::uint32_t v) {
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    return buf;
}

std::string decode_utf16le(const std::uint8_t* p, std::size_t units) {
    std::string out;
    for (std::size_t i = 0; i < units; ++i) {
        const std::uint32_t a = read_le16(p + i * 2u);
        if (a >= 0xD800u && a <= 0xDBFFu && i + 1 < units) {
            const std::uint32_t b = read_le16(p + (i + 1) * 2u);
            if (b >= 0xDC00u && b <= 0xDFFFu) {
                append_utf8(&out, 0x10000u + ((a - 0xD800u) << 10) + (b - 0xDC00u));
                ++i;
                continue;
            }
        }
        append_utf8(&out, a);
    }
    return out;
}

// ResStringPool parsing. Each string is addressed by an offset from
// stringsStart; the two length prefixes are validated before any read.
std::vector<std::string> parse_string_pool(const std::uint8_t* chunk,
                                           std::size_t chunk_size,
                                           std::size_t* error_code) {
    std::vector<std::string> out;
    if (chunk_size < 28u) {
        *error_code = 1;
        return out;
    }
    const std::uint32_t count = read_le32(chunk + 8);
    const std::uint32_t flags = read_le32(chunk + 16);
    const std::uint32_t strings_start = read_le32(chunk + 20);
    if (count == 0 || count > 1u << 20u) return out;
    if (static_cast<std::uint64_t>(28u) +
            static_cast<std::uint64_t>(count) * 4u > chunk_size) {
        *error_code = 2;
        return out;
    }
    if (strings_start > chunk_size) {
        *error_code = 3;
        return out;
    }
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t str_off = read_le32(chunk + 28u + static_cast<std::size_t>(i) * 4u);
        if (str_off >= chunk_size - strings_start) {
            out.emplace_back();
            continue;
        }
        const std::uint8_t* p = chunk + strings_start + str_off;
        const std::size_t avail = chunk_size - strings_start - str_off;
        if ((flags & kAxmlUtf8Flag) != 0u) {
            std::size_t pos = 0;
            std::uint64_t utf16_len = 0, byte_len = 0;
            if (!read_uleb128(p, avail, &pos, &utf16_len) ||
                !read_uleb128(p, avail, &pos, &byte_len)) {
                out.emplace_back();
                continue;
            }
            (void)utf16_len;
            if (byte_len > avail - pos) {
                out.emplace_back();
                continue;
            }
            // decode_mutf8 advances its `pos` from zero relative to the
            // pointer passed in. Passing an already-offset pointer together
            // with a shortened size made `pos >= size` once q passed half of
            // byte_len, so the old loop stopped advancing and span forever.
            // Decode the whole bounded string once instead.
            std::size_t q = 0;
            std::string s = decode_mutf8(p + pos, byte_len, &q);
            out.push_back(std::move(s));
        } else {
            if (avail < 2u) {
                out.emplace_back();
                continue;
            }
            std::uint32_t units = read_le16(p);
            std::size_t pos = 2;
            if ((units & 0x8000u) != 0u) {
                if (avail < 4u) {
                    out.emplace_back();
                    continue;
                }
                units = ((units & 0x7fffu) << 16) | read_le16(p + 2);
                pos = 4;
            }
            if (units > (avail - pos) / 2u) {
                out.emplace_back();
                continue;
            }
            out.push_back(decode_utf16le(p + pos, units));
        }
    }
    return out;
}

std::string pool_string(const std::vector<std::string>& pool,
                        std::uint32_t index) {
    if (index == kAxmlNoEntry || index >= pool.size()) return {};
    return pool[index];
}

std::string typed_value_string(const std::vector<std::string>& pool,
                               const std::uint8_t* tv) {
    const std::uint8_t type = tv[3];
    const std::uint32_t data = read_le32(tv + 4);
    switch (type) {
        case kResString: return pool_string(pool, data);
        case kResIntDec: return std::to_string(static_cast<std::int32_t>(data));
        case kResIntHex: return hex32(data);
        case kResIntBoolean: return data != 0u ? "true" : "false";
        case kResReference: return "@" + hex32(data);
        case kResFloat: {
            float f = 0.0f;
            std::memcpy(&f, &data, sizeof(f));
            char buf[32] = {};
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(f));
            return buf;
        }
        default: break;
    }
    // A dimension, fraction, colour or unknown type is left absent: printing a
    // plausible-looking unit or name here would invent a fact.
    return {};
}

int parse_int_attribute(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 0);
    if (!end || end == s.c_str() || *end != '\0') return 0;
    if (v > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    if (v < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
    return static_cast<int>(v);
}

bool parse_bool_attribute(const std::string& s) {
    return s == "true" || s == "1";
}

}  // namespace

std::string decode_mutf8(const std::uint8_t* data, std::size_t size,
                         std::size_t* pos) {
    std::string out;
    if (!data || !pos) return out;
    while (*pos < size) {
        const std::uint8_t a = data[(*pos)++];
        if (a == 0u) break;
        if (a < 0x80u) {
            out.push_back(static_cast<char>(a));
        } else if ((a & 0xE0u) == 0xC0u) {
            if (*pos >= size) break;
            const std::uint8_t b = data[(*pos)++];
            if ((b & 0xC0u) != 0x80u) break;
            const std::uint32_t cp = ((static_cast<std::uint32_t>(a & 0x1fu) << 6) |
                                      static_cast<std::uint32_t>(b & 0x3fu));
            append_utf8(&out, cp);  // C0 80 decodes to a real NUL byte
        } else if ((a & 0xF0u) == 0xE0u) {
            if (*pos + 1 >= size) break;
            const std::uint8_t b = data[(*pos)++];
            const std::uint8_t c = data[(*pos)++];
            if ((b & 0xC0u) != 0x80u || (c & 0xC0u) != 0x80u) break;
            const std::uint32_t cp = (static_cast<std::uint32_t>(a & 0x0fu) << 12) |
                                     (static_cast<std::uint32_t>(b & 0x3fu) << 6) |
                                     static_cast<std::uint32_t>(c & 0x3fu);
            if (cp >= 0xD800u && cp <= 0xDBFFu && *pos + 2 < size &&
                data[*pos] == 0xEDu && (data[*pos + 1] & 0xF0u) == 0xB0u) {
                const std::uint8_t d = data[(*pos)++];
                const std::uint8_t e = data[(*pos)++];
                const std::uint8_t f = data[(*pos)++];
                if ((e & 0xC0u) == 0x80u && (f & 0xC0u) == 0x80u) {
                    const std::uint32_t lo = (static_cast<std::uint32_t>(d & 0x0fu) << 12) |
                                             (static_cast<std::uint32_t>(e & 0x3fu) << 6) |
                                             static_cast<std::uint32_t>(f & 0x3fu);
                    if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                        append_utf8(&out, 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u));
                        continue;
                    }
                }
            }
            append_utf8(&out, cp);
        } else {
            break;
        }
    }
    return out;
}

const AxmlAttribute* AxmlElement::attr(std::string_view attr_name) const {
    for (const auto& a : attributes) {
        if (a.name == attr_name) return &a;
    }
    return nullptr;
}

std::string AxmlElement::attr_value(std::string_view attr_name) const {
    const AxmlAttribute* a = attr(attr_name);
    return a ? a->value : std::string();
}

AxmlDocument parse_axml(const std::uint8_t* data, std::size_t size) {
    AxmlDocument doc;
    if (!data || size < 16u) {
        doc.error = "AXML document is too small";
        return doc;
    }
    if (read_le16(data) != kAxmlType) {
        doc.error = "missing AXML root chunk type";
        return doc;
    }
    const std::uint32_t total = read_le32(data + 4);
    if (total > size || read_le16(data + 2) < 8u) {
        doc.error = "AXML root header is invalid";
        return doc;
    }

    std::size_t pos = read_le16(data + 2);
    std::vector<std::string> pool;
    bool have_pool = false;
    while (pos + 8u <= total) {
        const std::uint16_t type = read_le16(data + pos);
        const std::uint16_t header_size = read_le16(data + pos + 2);
        const std::uint32_t chunk_size = read_le32(data + pos + 4);
        if (header_size < 8u || chunk_size < header_size ||
            static_cast<std::uint64_t>(pos) + chunk_size > total) {
            doc.error = "truncated or invalid AXML chunk";
            return doc;
        }
        const std::uint8_t* chunk = data + pos;
        if (type == kAxmlStringPool) {
            std::size_t perr = 0;
            pool = parse_string_pool(chunk, chunk_size, &perr);
            if (perr != 0u) {
                doc.error = "invalid AXML string pool (" + std::to_string(perr) + ")";
                return doc;
            }
            have_pool = true;
            for (const auto& s : pool) doc.strings.push_back(s);
        } else if (type == kAxmlResourceMap) {
            for (std::size_t i = header_size; i + 4u <= chunk_size; i += 4u) {
                doc.resource_ids.push_back(read_le32(chunk + i));
            }
        } else if (type == kAxmlStartElement || type == kAxmlEndElement) {
            // A start element carries ResXMLTree_attrExt (36 bytes); an end
            // element only carries ns+name after the 16-byte node header (24).
            const std::uint32_t min_size =
                (type == kAxmlStartElement) ? 36u : 24u;
            if (chunk_size < min_size || !have_pool) {
                doc.error = "invalid AXML element chunk";
                return doc;
            }
            AxmlElement el;
            el.ns = pool_string(pool, read_le32(chunk + 16));
            el.name = pool_string(pool, read_le32(chunk + 20));
            el.is_end = (type == kAxmlEndElement);
            if (el.name.empty()) {
                doc.error = "AXML element has no resolvable name";
                return doc;
            }
            if (!el.is_end) {
                const std::uint16_t attr_start = read_le16(chunk + 24);
                const std::uint16_t attr_size = read_le16(chunk + 26);
                const std::uint16_t attr_count = read_le16(chunk + 28);
                if (attr_size < 20u || attr_count > 4096u ||
                    static_cast<std::uint64_t>(16u) + attr_start +
                        static_cast<std::uint64_t>(attr_count) * attr_size > chunk_size) {
                    doc.error = "AXML attribute table out of bounds";
                    return doc;
                }
                for (std::uint16_t i = 0; i < attr_count; ++i) {
                    const std::uint8_t* a = chunk + 16u + attr_start +
                                           static_cast<std::size_t>(i) * attr_size;
                    AxmlAttribute at;
                    at.ns = pool_string(pool, read_le32(a + 0));
                    at.name = pool_string(pool, read_le32(a + 4));
                    at.raw_value = read_le32(a + 8);
                    // ResXMLTree_attribute is ns(4), name(4), rawValue(4),
                    // then Res_value { size:u16, res0:u8, dataType:u8, data:u32 }.
                    // So dataType is at byte 15 and data at byte 16 — not 11/12.
                    at.data_type = a[15];
                    at.data = read_le32(a + 16);
                    if (at.raw_value != kAxmlNoEntry) {
                        at.value = pool_string(pool, at.raw_value);
                    } else {
                        at.value = typed_value_string(pool, a + 12);
                    }
                    el.attributes.push_back(std::move(at));
                }
            }
            doc.elements.push_back(std::move(el));
        }
        pos += chunk_size;
    }
    doc.valid = have_pool && !doc.elements.empty();
    if (!doc.valid && doc.error.empty()) doc.error = "no elements in AXML document";
    return doc;
}

AxmlDocument parse_axml_bytes(const std::vector<std::uint8_t>& bytes) {
    return parse_axml(bytes.data(), bytes.size());
}

ApkManifest parse_binary_manifest(const AxmlDocument& axml) {
    ApkManifest m;
    if (!axml.valid) {
        m.error = axml.error.empty() ? "binary manifest is not valid" : axml.error;
        return m;
    }

    // Track the enclosing activity so intent-filter MAIN/LAUNCHER is attached to
    // the right activity rather than the first one in the file.
    struct Frame {
        std::string name;
        std::size_t activity_index = std::numeric_limits<std::size_t>::max();
    };
    std::vector<Frame> stack;
    std::size_t launcher_index = std::numeric_limits<std::size_t>::max();

    for (const auto& el : axml.elements) {
        if (el.is_end) {
            if (!stack.empty() && stack.back().name == el.name) stack.pop_back();
            continue;
        }
        if (el.name == "manifest") {
            m.package_name = el.attr_value("package");
            m.version_code = static_cast<std::uint32_t>(
                parse_int_attribute(el.attr_value("versionCode")));
            m.version_name = el.attr_value("versionName");
        } else if (el.name == "uses-sdk") {
            m.min_sdk = parse_int_attribute(el.attr_value("minSdkVersion"));
            m.target_sdk = parse_int_attribute(el.attr_value("targetSdkVersion"));
            m.compile_sdk = parse_int_attribute(el.attr_value("compileSdkVersion"));
        } else if (el.name == "application") {
            m.has_application = true;
            m.application_label = el.attr_value("label");
            m.application_icon = el.attr_value("icon");
            m.application_name = el.attr_value("name");
            m.debuggable = parse_bool_attribute(el.attr_value("debuggable"));
        } else if (el.name == "uses-permission") {
            const std::string name = el.attr_value("name");
            if (!name.empty()) m.permissions.push_back(name);
        } else if (el.name == "uses-feature") {
            const std::string name = el.attr_value("name");
            if (!name.empty()) m.features.push_back(name);
        } else if (el.name == "activity" || el.name == "activity-alias") {
            AndroidActivityInfo a;
            a.name = el.attr_value("name");
            a.label = el.attr_value("label");
            a.icon_ref = el.attr_value("icon");
            a.exported = parse_bool_attribute(el.attr_value("exported"));
            m.activities.push_back(std::move(a));
            stack.push_back({el.name, m.activities.size() - 1u});
            continue;
        } else if (el.name == "action") {
            const std::string action = el.attr_value("name");
            // An <action> sits inside <intent-filter>, whose frame has no
            // activity index: search outwards for the enclosing <activity>.
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                if (it->activity_index != std::numeric_limits<std::size_t>::max()) {
                    m.activities[it->activity_index].actions.push_back(action);
                    break;
                }
            }
        } else if (el.name == "category") {
            const std::string category = el.attr_value("name");
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                if (it->activity_index != std::numeric_limits<std::size_t>::max()) {
                    m.activities[it->activity_index].categories.push_back(category);
                    break;
                }
            }
        }
        if (el.name == "intent-filter") {
            stack.push_back({el.name, std::numeric_limits<std::size_t>::max()});
            continue;
        }
        // Propagate the enclosing activity through non-activity frames.
        if (stack.empty() || stack.back().name != el.name) {
            std::size_t act = std::numeric_limits<std::size_t>::max();
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                if (it->activity_index != std::numeric_limits<std::size_t>::max()) {
                    act = it->activity_index;
                    break;
                }
            }
            stack.push_back({el.name, act});
        }
    }

    for (std::size_t i = 0; i < m.activities.size(); ++i) {
        auto& a = m.activities[i];
        for (const auto& act : a.actions) {
            if (act == "android.intent.action.MAIN") a.launcher = true;
        }
        for (const auto& c : a.categories) {
            if (c == "android.intent.category.LAUNCHER") a.launcher = true;
        }
        if (a.launcher && launcher_index == std::numeric_limits<std::size_t>::max()) {
            launcher_index = i;
        }
    }
    m.valid = !m.package_name.empty();
    if (!m.valid) m.error = "binary manifest has no package name";
    return m;
}

std::string ApkManifest::main_activity() const {
    for (const auto& a : activities) {
        if (a.launcher) return a.name;
    }
    return {};
}

std::string ArscInfo::resolve_key(std::uint32_t id) const {
    for (const auto& r : resources) {
        if (r.id == id) return r.key;
    }
    return {};
}

ArscInfo parse_arsc(const std::uint8_t* data, std::size_t size) {
    ArscInfo arsc;
    if (!data || size < 12u || read_le16(data) != kArscTable) {
        arsc.error = "missing resources.arsc table header";
        return arsc;
    }
    const std::uint32_t total = read_le32(data + 4);
    if (total > size || total < 12u) {
        arsc.error = "resources.arsc table size is invalid";
        return arsc;
    }
    arsc.package_count = read_le32(data + 8);
    if (arsc.package_count == 0 || arsc.package_count > 1024u) {
        arsc.error = "resources.arsc package count is invalid";
        return arsc;
    }

    // Walk top-level chunks. Type strings and key strings are both string
    // pools; a type chunk's entry offsets point at ResTable_entry records whose
    // key.index selects a key string. This is the real, bounded subset needed
    // to resolve a resource id to its name.
    std::size_t pos = read_le16(data + 2);
    struct PackageCtx {
        std::uint32_t id = 0;
        std::string name;
        std::vector<std::string> type_strings;
        std::vector<std::string> key_strings;
    };
    std::vector<PackageCtx> packages;

    while (pos + 8u <= total) {
        const std::uint16_t type = read_le16(data + pos);
        const std::uint16_t header_size = read_le16(data + pos + 2);
        const std::uint32_t chunk_size = read_le32(data + pos + 4);
        if (header_size < 8u || chunk_size < header_size ||
            static_cast<std::uint64_t>(pos) + chunk_size > total) {
            arsc.error = "invalid resources.arsc chunk";
            return arsc;
        }
        const std::uint8_t* chunk = data + pos;
        if (type == kArscPackage) {
            if (chunk_size < 288u) {
                arsc.error = "truncated resources.arsc package";
                return arsc;
            }
            PackageCtx pkg;
            pkg.id = read_le32(chunk + 8);
            const std::uint16_t* name16 = reinterpret_cast<const std::uint16_t*>(chunk + 12);
            for (int i = 0; i < 128; ++i) {
                if (name16[i] == 0u) break;
                // Package names are ASCII in practice; non-ASCII is skipped
                // honestly rather than transliterated.
                if (name16[i] < 0x80u) pkg.name.push_back(static_cast<char>(name16[i]));
            }
            const std::uint32_t type_strings_off = read_le32(chunk + 268);
            const std::uint32_t key_strings_off = read_le32(chunk + 276);
            std::size_t perr = 0;
            if (type_strings_off != 0u && type_strings_off < chunk_size) {
                pkg.type_strings = parse_string_pool(chunk + type_strings_off,
                                                     chunk_size - type_strings_off, &perr);
            }
            if (key_strings_off != 0u && key_strings_off < chunk_size) {
                pkg.key_strings = parse_string_pool(chunk + key_strings_off,
                                                    chunk_size - key_strings_off, &perr);
            }
            packages.push_back(std::move(pkg));
            arsc.packages.push_back(packages.back().name);
        } else if (type == kArscTypeString && !packages.empty()) {
            // Type/spec chunks are nested inside a package chunk, so this only
            // occurs when the table lists them at top level. The nested case is
            // handled in the package walk below.
        }
        pos += chunk_size;
    }

    // Walk each package's inner chunks for type/spec records.
    pos = read_le16(data + 2);
    std::size_t pkg_index = 0;
    while (pos + 8u <= total && pkg_index < packages.size()) {
        const std::uint16_t type = read_le16(data + pos);
        const std::uint32_t chunk_size = read_le32(data + pos + 4);
        const std::uint16_t pkg_header = read_le16(data + pos + 2);
        if (type == kArscPackage && chunk_size >= 288u) {
            PackageCtx& pkg = packages[pkg_index++];
            const std::uint32_t type_strings_off = read_le32(data + pos + 268);
            const std::uint32_t key_strings_off = read_le32(data + pos + 276);
            // The package header is 288 bytes in current resources.arsc
            // (ResTable_package grew a typeIdOffset field). Honour the chunk's
            // own headerSize so older tables are not skipped.
            const std::size_t inner_off = std::min<std::size_t>(
                pkg_header >= 288u ? pkg_header : 288u, chunk_size);
            std::size_t inner = pos + inner_off;
            while (inner + 8u <= pos + chunk_size) {
                const std::uint16_t itype = read_le16(data + inner);
                const std::uint32_t isize = read_le32(data + inner + 4);
                if (isize < 8u || static_cast<std::uint64_t>(inner) + isize > pos + chunk_size) break;
                if (itype == kArscTypeString && isize >= 20u) {
                    const std::uint8_t tid = data[inner + 8];
                    const std::uint32_t entry_count = read_le32(data + inner + 12);
                    const std::uint32_t entries_start = read_le32(data + inner + 16);
                    if (tid != 0u && entry_count > 0u &&
                        static_cast<std::uint64_t>(inner) + entries_start +
                            static_cast<std::uint64_t>(entry_count) * 4u <= inner + isize &&
                        tid <= pkg.type_strings.size()) {
                        for (std::uint32_t e = 0; e < entry_count; ++e) {
                            const std::uint32_t eo = read_le32(
                                data + inner + entries_start + static_cast<std::size_t>(e) * 4u);
                            if (eo == kAxmlNoEntry) continue;
                            const std::size_t ep = inner + entries_start + eo;
                            if (ep + 8u > inner + isize) continue;
                            // ResTable_entry: size:u16, flags:u16, key:u32.
                            const std::uint32_t key_index = read_le32(data + ep + 4);
                            if (key_index >= pkg.key_strings.size()) continue;
                            ArscResource res;
                            res.id = (pkg.id << 24) |
                                     (static_cast<std::uint32_t>(tid) << 16) | e;
                            res.package = pkg.name;
                            res.type = pkg.type_strings[tid - 1u];
                            res.key = pkg.key_strings[key_index];
                            arsc.resources.push_back(std::move(res));
                        }
                    }
                }
                inner += isize;
            }
            (void)type_strings_off;
            (void)key_strings_off;
        }
        pos += chunk_size;
    }

    arsc.valid = !arsc.packages.empty();
    if (!arsc.valid && arsc.error.empty()) arsc.error = "no packages in resources.arsc";
    return arsc;
}

ArscInfo parse_arsc_bytes(const std::vector<std::uint8_t>& bytes) {
    return parse_arsc(bytes.data(), bytes.size());
}

DexVerification verify_dex_header(const std::uint8_t* data, std::size_t size) {
    DexVerification v;
    if (!data || size < kDexHeaderSize) {
        v.error = "file is smaller than the 112-byte DEX header";
        return v;
    }
    if (std::memcmp(data, "dex\n", 4) != 0) {
        v.error = "missing DEX magic (expected 'dex\\n')";
        return v;
    }
    if (data[7] != 0u) {
        v.error = "DEX magic is not NUL terminated";
        return v;
    }
    v.header.magic = std::string(reinterpret_cast<const char*>(data), 7);
    v.header.version = (data[4] - '0') * 100 + (data[5] - '0') * 10 + (data[6] - '0');
    if (v.header.version < 35 || v.header.version > 99) {
        v.error = "unsupported DEX version: " + v.header.magic;
        return v;
    }

    DexHeaderInfo& h = v.header;
    h.checksum = read_le32(data + 8);
    std::memcpy(h.signature, data + 12, 20);
    h.file_size = read_le32(data + 32);
    h.header_size = read_le32(data + 36);
    h.endian_tag = read_le32(data + 40);
    h.map_off = read_le32(data + 52);
    h.string_ids_size = read_le32(data + 56);
    h.string_ids_off = read_le32(data + 60);
    h.type_ids_size = read_le32(data + 64);
    h.type_ids_off = read_le32(data + 68);
    h.proto_ids_size = read_le32(data + 72);
    h.proto_ids_off = read_le32(data + 76);
    h.field_ids_size = read_le32(data + 80);
    h.field_ids_off = read_le32(data + 84);
    h.method_ids_size = read_le32(data + 88);
    h.method_ids_off = read_le32(data + 92);
    h.class_defs_size = read_le32(data + 96);
    h.class_defs_off = read_le32(data + 100);
    h.data_size = read_le32(data + 104);
    h.data_off = read_le32(data + 108);

    if (h.endian_tag != kDexEndianLittle) {
        v.issues.push_back("unsupported DEX endian tag (only little-endian is decoded)");
    }
    if (h.header_size < kDexHeaderSize || h.header_size > size) {
        v.issues.push_back("DEX header_size is out of range");
    }
    if (h.file_size < kDexHeaderSize || h.file_size > size) {
        v.issues.push_back("DEX file_size does not fit the file");
    }
    const std::uint32_t limit = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max())),
        h.file_size);

    auto section_ok = [&](const char* name, std::uint32_t count, std::uint32_t off,
                          std::uint64_t item_size) {
        if (count == 0u) return;
        if (off < h.header_size || off > limit) {
            v.issues.push_back(std::string(name) + " offset is out of range");
            return;
        }
        if (static_cast<std::uint64_t>(off) +
                static_cast<std::uint64_t>(count) * item_size > limit) {
            v.issues.push_back(std::string(name) + " table exceeds file_size");
        }
    };
    section_ok("string_ids", h.string_ids_size, h.string_ids_off, 4u);
    section_ok("type_ids", h.type_ids_size, h.type_ids_off, 4u);
    section_ok("proto_ids", h.proto_ids_size, h.proto_ids_off, 12u);
    section_ok("field_ids", h.field_ids_size, h.field_ids_off, 8u);
    section_ok("method_ids", h.method_ids_size, h.method_ids_off, 8u);
    section_ok("class_defs", h.class_defs_size, h.class_defs_off, 32u);
    if (h.data_off != 0u && (h.data_off < h.header_size || h.data_off > limit ||
                             static_cast<std::uint64_t>(h.data_off) + h.data_size > limit)) {
        v.issues.push_back("data section exceeds file_size");
    }
    if (h.map_off != 0u) {
        if (h.map_off < h.header_size || h.map_off + 4u > limit ||
            h.map_off + 4u + static_cast<std::uint64_t>(read_le32(data + h.map_off)) * kDexMapItemSize > limit) {
            v.issues.push_back("DEX map list is out of range");
        }
    }

    const uLong adler = ::adler32(::adler32(0L, Z_NULL, 0),
                                  reinterpret_cast<const Bytef*>(data + 12),
                                  static_cast<uInt>(limit - 12u));
    v.checksum_valid = (static_cast<std::uint32_t>(adler) == h.checksum);
    if (!v.checksum_valid) v.issues.push_back("DEX Adler-32 checksum mismatch");

    std::uint8_t digest[20] = {};
    if (limit > 32u && sha1_bytes(data + 32, limit - 32u, digest)) {
        v.signature_valid = (std::memcmp(digest, h.signature, 20) == 0);
        if (!v.signature_valid) v.issues.push_back("DEX SHA-1 signature mismatch");
    }

    v.valid = v.issues.empty() && h.header_size >= kDexHeaderSize &&
              h.endian_tag == kDexEndianLittle;
    if (!v.valid && v.error.empty()) {
        v.error = v.issues.empty() ? "DEX header verification failed" : v.issues.front();
    }
    v.header.valid = v.valid;
    return v;
}

}  // namespace finn::android
