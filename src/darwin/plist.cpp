#include "darwin/plist.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace finn::darwin {
namespace {

std::uint64_t read_be_uint(std::string_view b, std::size_t off, std::size_t nbytes) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < nbytes; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(static_cast<unsigned char>(b[off + i]));
    }
    return v;
}

std::string unescape_xml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            if (s.substr(i, 4) == "&lt;") { out.push_back('<'); i += 3; continue; }
            if (s.substr(i, 4) == "&gt;") { out.push_back('>'); i += 3; continue; }
            if (s.substr(i, 5) == "&amp;") { out.push_back('&'); i += 4; continue; }
            if (s.substr(i, 6) == "&quot;") { out.push_back('"'); i += 5; continue; }
            if (s.substr(i, 6) == "&apos;") { out.push_back('\''); i += 5; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

// --- XML Plist Recursive Descent Parser ---
class XmlPlistParser {
public:
    explicit XmlPlistParser(std::string_view src) : src_(src) {}

    std::optional<PlistValue> parse(std::string* error) {
        skip_prolog();
        auto root = parse_value(0, error);
        if (!root) return std::nullopt;
        return root;
    }

private:
    void skip_ws() {
        while (pos_ < src_.size() &&
               (src_[pos_] == ' ' || src_[pos_] == '\t' ||
                src_[pos_] == '\r' || src_[pos_] == '\n')) {
            ++pos_;
        }
    }

    void skip_comments_and_pi() {
        while (pos_ < src_.size()) {
            skip_ws();
            if (src_.substr(pos_, 4) == "<!--") {
                const std::size_t end = src_.find("-->", pos_ + 4);
                pos_ = (end == std::string_view::npos) ? src_.size() : end + 3;
                continue;
            }
            if (src_.substr(pos_, 2) == "<?") {
                const std::size_t end = src_.find("?>", pos_ + 2);
                pos_ = (end == std::string_view::npos) ? src_.size() : end + 2;
                continue;
            }
            if (src_.substr(pos_, 9) == "<!DOCTYPE") {
                const std::size_t end = src_.find('>', pos_ + 9);
                pos_ = (end == std::string_view::npos) ? src_.size() : end + 1;
                continue;
            }
            break;
        }
    }

    void skip_prolog() {
        skip_comments_and_pi();
        if (src_.substr(pos_, 6) == "<plist") {
            const std::size_t gt = src_.find('>', pos_);
            if (gt != std::string_view::npos) pos_ = gt + 1;
        }
        skip_comments_and_pi();
    }

    bool read_open_tag(std::string* name, bool* self_closing) {
        skip_comments_and_pi();
        if (pos_ >= src_.size() || src_[pos_] != '<') return false;
        if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') return false;
        const std::size_t gt = src_.find('>', pos_ + 1);
        if (gt == std::string_view::npos) return false;
        std::string_view inner = src_.substr(pos_ + 1, gt - pos_ - 1);
        *self_closing = !inner.empty() && inner.back() == '/';
        if (*self_closing) inner.remove_suffix(1);
        const std::size_t sp = inner.find_first_of(" \t\r\n");
        *name = std::string(sp == std::string_view::npos ? inner : inner.substr(0, sp));
        pos_ = gt + 1;
        return true;
    }

    bool peek_close_tag(std::string_view name) {
        skip_comments_and_pi();
        const std::string close = "</" + std::string(name) + ">";
        return src_.substr(pos_, close.size()) == close;
    }

    bool consume_close_tag(std::string_view name) {
        skip_comments_and_pi();
        const std::string close = "</" + std::string(name) + ">";
        if (src_.substr(pos_, close.size()) != close) return false;
        pos_ += close.size();
        return true;
    }

    std::string read_text_until_close(std::string_view name) {
        const std::string close = "</" + std::string(name) + ">";
        const std::size_t end = src_.find(close, pos_);
        if (end == std::string_view::npos) {
            const std::string text(src_.substr(pos_));
            pos_ = src_.size();
            return unescape_xml(text);
        }
        const std::string text = unescape_xml(src_.substr(pos_, end - pos_));
        pos_ = end + close.size();
        return text;
    }

    std::optional<PlistValue> parse_value(int depth, std::string* error) {
        if (depth > 64) {
            if (error) *error = "XML plist nesting too deep";
            return std::nullopt;
        }
        std::string tag;
        bool self_closing = false;
        if (!read_open_tag(&tag, &self_closing)) {
            if (error) *error = "expected XML plist element";
            return std::nullopt;
        }

        PlistValue val;
        if (tag == "true") {
            val.kind = PlistValue::Kind::Bool;
            val.bool_val = true;
            if (!self_closing) (void)consume_close_tag("true");
            return val;
        }
        if (tag == "false") {
            val.kind = PlistValue::Kind::Bool;
            val.bool_val = false;
            if (!self_closing) (void)consume_close_tag("false");
            return val;
        }
        if (tag == "string" || tag == "date") {
            val.kind = PlistValue::Kind::String;
            val.string_val = self_closing ? std::string{} : read_text_until_close(tag);
            return val;
        }
        if (tag == "integer") {
            val.kind = PlistValue::Kind::Int;
            const std::string text = self_closing ? "0" : read_text_until_close(tag);
            try {
                val.int_val = std::stoll(text, nullptr, 0);
            } catch (...) {
                val.int_val = 0;
            }
            return val;
        }
        if (tag == "real") {
            val.kind = PlistValue::Kind::Real;
            const std::string text = self_closing ? "0" : read_text_until_close(tag);
            try {
                val.real_val = std::stod(text);
            } catch (...) {
                val.real_val = 0.0;
            }
            return val;
        }
        if (tag == "data") {
            val.kind = PlistValue::Kind::Data;
            if (!self_closing) {
                val.data_val = decode_base64(read_text_until_close(tag));
            }
            return val;
        }
        if (tag == "array") {
            val.kind = PlistValue::Kind::Array;
            if (self_closing) return val;
            while (pos_ < src_.size() && !peek_close_tag("array")) {
                auto elem = parse_value(depth + 1, error);
                if (!elem) return std::nullopt;
                val.array_val.push_back(std::move(*elem));
            }
            (void)consume_close_tag("array");
            return val;
        }
        if (tag == "dict") {
            val.kind = PlistValue::Kind::Dict;
            if (self_closing) return val;
            while (pos_ < src_.size() && !peek_close_tag("dict")) {
                std::string ktag;
                bool kself = false;
                if (!read_open_tag(&ktag, &kself) || ktag != "key") {
                    if (error) *error = "expected <key> inside <dict>";
                    return std::nullopt;
                }
                const std::string key = kself ? std::string{} : read_text_until_close("key");
                auto item = parse_value(depth + 1, error);
                if (!item) return std::nullopt;
                val.dict_val[key] = std::move(*item);
            }
            (void)consume_close_tag("dict");
            return val;
        }

        if (error) *error = "unexpected XML plist tag <" + tag + ">";
        return std::nullopt;
    }

    std::string_view src_;
    std::size_t pos_ = 0;
};

// --- Binary Plist (bplist00) Parser ---
class BinaryPlistParser {
public:
    explicit BinaryPlistParser(std::string_view data) : data_(data) {}

    std::optional<PlistValue> parse(std::string* error) {
        if (data_.size() < 40 || data_.substr(0, 8) != "bplist00") {
            if (error) *error = "invalid bplist00 header";
            return std::nullopt;
        }
        const std::size_t trailer = data_.size() - 32;
        offset_size_ = static_cast<unsigned char>(data_[trailer + 6]);
        ref_size_ = static_cast<unsigned char>(data_[trailer + 7]);
        num_objects_ = read_be_uint(data_, trailer + 8, 8);
        top_object_ = read_be_uint(data_, trailer + 16, 8);
        offset_table_off_ = read_be_uint(data_, trailer + 24, 8);

        if (offset_size_ == 0 || offset_size_ > 8 || ref_size_ == 0 || ref_size_ > 8) {
            if (error) *error = "invalid bplist00 trailer sizes";
            return std::nullopt;
        }
        if (num_objects_ == 0 || num_objects_ > 1000000 || top_object_ >= num_objects_) {
            if (error) *error = "invalid bplist00 object count";
            return std::nullopt;
        }
        if (offset_table_off_ > trailer ||
            num_objects_ * offset_size_ > trailer - offset_table_off_) {
            if (error) *error = "bplist00 offset table out of bounds";
            return std::nullopt;
        }
        return read_object(top_object_, 0, error);
    }

private:
    bool read_length(std::uint8_t low_nibble, std::size_t* cursor,
                     std::uint64_t* out_len) const {
        if (low_nibble != 0x0fu) {
            *out_len = low_nibble;
            return true;
        }
        if (*cursor >= offset_table_off_) return false;
        const std::uint8_t int_marker = static_cast<unsigned char>(data_[(*cursor)++]);
        if ((int_marker & 0xf0u) != 0x10u) return false;
        const std::size_t nbytes = static_cast<std::size_t>(1u) << (int_marker & 0x0fu);
        if (nbytes > 8 || *cursor + nbytes > offset_table_off_) return false;
        *out_len = read_be_uint(data_, *cursor, nbytes);
        *cursor += nbytes;
        return true;
    }

    std::optional<PlistValue> read_object(std::uint64_t obj_idx, int depth,
                                          std::string* error) const {
        if (depth > 64 || obj_idx >= num_objects_) {
            if (error) *error = "bplist00 object index/depth invalid";
            return std::nullopt;
        }
        const std::uint64_t obj_off =
            read_be_uint(data_, offset_table_off_ + obj_idx * offset_size_, offset_size_);
        if (obj_off >= offset_table_off_) {
            if (error) *error = "bplist00 object offset out of range";
            return std::nullopt;
        }

        std::size_t cursor = static_cast<std::size_t>(obj_off);
        const std::uint8_t marker = static_cast<unsigned char>(data_[cursor++]);
        const std::uint8_t hi = marker & 0xf0u;
        const std::uint8_t lo = marker & 0x0fu;

        PlistValue val;
        if (hi == 0x00u) {
            if (lo == 0x08u) {
                val.kind = PlistValue::Kind::Bool;
                val.bool_val = false;
            } else if (lo == 0x09u) {
                val.kind = PlistValue::Kind::Bool;
                val.bool_val = true;
            } else {
                val.kind = PlistValue::Kind::Null;
            }
            return val;
        }
        if (hi == 0x10u) {
            const std::size_t nbytes = static_cast<std::size_t>(1u) << lo;
            if (nbytes > 8 || cursor + nbytes > offset_table_off_) return std::nullopt;
            val.kind = PlistValue::Kind::Int;
            const std::uint64_t raw = read_be_uint(data_, cursor, nbytes);
            val.int_val = static_cast<std::int64_t>(raw);
            return val;
        }
        if (hi == 0x20u) {
            const std::size_t nbytes = static_cast<std::size_t>(1u) << lo;
            if ((nbytes != 4 && nbytes != 8) || cursor + nbytes > offset_table_off_) {
                return std::nullopt;
            }
            val.kind = PlistValue::Kind::Real;
            if (nbytes == 4) {
                const std::uint32_t bits = static_cast<std::uint32_t>(read_be_uint(data_, cursor, 4));
                float f = 0.0f;
                std::memcpy(&f, &bits, sizeof(f));
                val.real_val = f;
            } else {
                const std::uint64_t bits = read_be_uint(data_, cursor, 8);
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof(d));
                val.real_val = d;
            }
            return val;
        }
        if (hi == 0x30u) {
            // Date (8-byte float seconds since 2001-01-01)
            if (cursor + 8 > offset_table_off_) return std::nullopt;
            val.kind = PlistValue::Kind::Real;
            const std::uint64_t bits = read_be_uint(data_, cursor, 8);
            double d = 0.0;
            std::memcpy(&d, &bits, sizeof(d));
            val.real_val = d;
            return val;
        }
        if (hi == 0x40u) {
            std::uint64_t len = 0;
            if (!read_length(lo, &cursor, &len) || len > offset_table_off_ - cursor) {
                return std::nullopt;
            }
            val.kind = PlistValue::Kind::Data;
            val.data_val.assign(
                reinterpret_cast<const std::uint8_t*>(data_.data() + cursor),
                reinterpret_cast<const std::uint8_t*>(data_.data() + cursor + len));
            return val;
        }
        if (hi == 0x50u) {
            // ASCII string
            std::uint64_t len = 0;
            if (!read_length(lo, &cursor, &len) || len > offset_table_off_ - cursor) {
                return std::nullopt;
            }
            val.kind = PlistValue::Kind::String;
            val.string_val.assign(data_.data() + cursor, static_cast<std::size_t>(len));
            return val;
        }
        if (hi == 0x60u) {
            // UTF-16BE string -> UTF-8
            std::uint64_t chars = 0;
            if (!read_length(lo, &cursor, &chars) ||
                chars * 2 > offset_table_off_ - cursor) {
                return std::nullopt;
            }
            val.kind = PlistValue::Kind::String;
            for (std::uint64_t i = 0; i < chars; ++i) {
                const std::uint16_t u =
                    static_cast<std::uint16_t>(read_be_uint(data_, cursor + i * 2, 2));
                if (u < 0x80u) {
                    val.string_val.push_back(static_cast<char>(u));
                } else if (u < 0x800u) {
                    val.string_val.push_back(static_cast<char>(0xc0u | (u >> 6)));
                    val.string_val.push_back(static_cast<char>(0x80u | (u & 0x3fu)));
                } else {
                    val.string_val.push_back(static_cast<char>(0xe0u | (u >> 12)));
                    val.string_val.push_back(static_cast<char>(0x80u | ((u >> 6) & 0x3fu)));
                    val.string_val.push_back(static_cast<char>(0x80u | (u & 0x3fu)));
                }
            }
            return val;
        }
        if (hi == 0xa0u || hi == 0xc0u) {
            // Array or Set
            std::uint64_t count = 0;
            if (!read_length(lo, &cursor, &count) ||
                count * ref_size_ > offset_table_off_ - cursor) {
                return std::nullopt;
            }
            val.kind = PlistValue::Kind::Array;
            val.array_val.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t i = 0; i < count; ++i) {
                const std::uint64_t ref = read_be_uint(data_, cursor + i * ref_size_, ref_size_);
                auto elem = read_object(ref, depth + 1, error);
                if (!elem) return std::nullopt;
                val.array_val.push_back(std::move(*elem));
            }
            return val;
        }
        if (hi == 0xd0u) {
            // Dictionary
            std::uint64_t count = 0;
            if (!read_length(lo, &cursor, &count) ||
                count * 2 * ref_size_ > offset_table_off_ - cursor) {
                return std::nullopt;
            }
            val.kind = PlistValue::Kind::Dict;
            for (std::uint64_t i = 0; i < count; ++i) {
                const std::uint64_t kref = read_be_uint(data_, cursor + i * ref_size_, ref_size_);
                const std::uint64_t vref =
                    read_be_uint(data_, cursor + (count + i) * ref_size_, ref_size_);
                auto kobj = read_object(kref, depth + 1, error);
                auto vobj = read_object(vref, depth + 1, error);
                if (!kobj || !vobj) return std::nullopt;
                if (kobj->kind == PlistValue::Kind::String) {
                    val.dict_val[kobj->string_val] = std::move(*vobj);
                }
            }
            return val;
        }

        if (error) *error = "unsupported bplist00 object marker";
        return std::nullopt;
    }

    std::string_view data_;
    std::size_t offset_size_ = 0;
    std::size_t ref_size_ = 0;
    std::uint64_t num_objects_ = 0;
    std::uint64_t top_object_ = 0;
    std::uint64_t offset_table_off_ = 0;
};

}  // namespace

const PlistValue* PlistValue::get(std::string_view key) const {
    if (kind != Kind::Dict) return nullptr;
    const auto it = dict_val.find(std::string(key));
    return (it == dict_val.end()) ? nullptr : &it->second;
}

std::string PlistValue::get_string(std::string_view key, std::string_view fallback) const {
    const PlistValue* v = get(key);
    if (v && v->kind == Kind::String) return v->string_val;
    return std::string(fallback);
}

bool PlistValue::get_bool(std::string_view key, bool fallback) const {
    const PlistValue* v = get(key);
    if (v && v->kind == Kind::Bool) return v->bool_val;
    return fallback;
}

std::vector<std::uint8_t> decode_base64(std::string_view text) {
    static constexpr int kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::vector<std::uint8_t> out;
    out.reserve((text.size() * 3) / 4);
    std::uint32_t acc = 0;
    int bits = -8;
    for (char ch : text) {
        if (ch == '=') break;
        const int v = kDec[static_cast<unsigned char>(ch)];
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xffu));
            bits -= 8;
        }
    }
    return out;
}

std::optional<PlistValue> parse_plist_bytes(std::string_view bytes, std::string* error) {
    if (bytes.size() >= 8 && bytes.substr(0, 8) == "bplist00") {
        BinaryPlistParser bp(bytes);
        return bp.parse(error);
    }
    XmlPlistParser xp(bytes);
    return xp.parse(error);
}

std::optional<PlistValue> parse_plist_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "unable to read plist file: " + path;
        return std::nullopt;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse_plist_bytes(buf.str(), error);
}

}  // namespace finn::darwin
