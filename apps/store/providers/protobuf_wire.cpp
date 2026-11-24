#include "protobuf_wire.hpp"

#include <cstring>

namespace finn::store::pb {
namespace {

bool read_varint(std::string_view data, std::size_t* pos, std::uint64_t* out) {
    std::uint64_t val = 0;
    int shift = 0;
    while (*pos < data.size() && shift < 64) {
        const std::uint8_t byte = static_cast<std::uint8_t>(data[(*pos)++]);
        val |= static_cast<std::uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            *out = val;
            return true;
        }
        shift += 7;
    }
    return false;
}

}  // namespace

bool decode_message(std::string_view data, std::vector<Field>* out_fields) {
    if (!out_fields) return false;
    out_fields->clear();
    std::size_t pos = 0;
    while (pos < data.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(data, &pos, &tag)) return false;
        const std::uint32_t field_num = static_cast<std::uint32_t>(tag >> 3);
        const std::uint8_t wt_raw = static_cast<std::uint8_t>(tag & 0x7u);
        if (field_num == 0) return false;

        Field f;
        f.number = field_num;
        f.wire_type = static_cast<WireType>(wt_raw);

        switch (f.wire_type) {
            case WireType::Varint:
                if (!read_varint(data, &pos, &f.varint)) return false;
                break;
            case WireType::Fixed64:
                if (pos + 8 > data.size()) return false;
                std::memcpy(&f.fixed64, data.data() + pos, 8);
                pos += 8;
                break;
            case WireType::LengthDelimited: {
                std::uint64_t len = 0;
                if (!read_varint(data, &pos, &len)) return false;
                if (len > data.size() - pos) return false;
                f.bytes = data.substr(pos, static_cast<std::size_t>(len));
                pos += static_cast<std::size_t>(len);
                break;
            }
            case WireType::Fixed32:
                if (pos + 4 > data.size()) return false;
                std::memcpy(&f.fixed32, data.data() + pos, 4);
                pos += 4;
                break;
            case WireType::StartGroup:
            case WireType::EndGroup:
                // Legacy groups skipped cleanly
                break;
            default:
                return false;
        }
        out_fields->push_back(f);
    }
    return true;
}

std::string first_string(const std::vector<Field>& fields, std::uint32_t field_num) {
    for (const auto& f : fields) {
        if (f.number == field_num && f.wire_type == WireType::LengthDelimited) {
            return std::string(f.bytes);
        }
    }
    return {};
}

std::uint64_t first_varint(const std::vector<Field>& fields, std::uint32_t field_num,
                           std::uint64_t fallback) {
    for (const auto& f : fields) {
        if (f.number == field_num && f.wire_type == WireType::Varint) {
            return f.varint;
        }
    }
    return fallback;
}

float first_float(const std::vector<Field>& fields, std::uint32_t field_num,
                  float fallback) {
    for (const auto& f : fields) {
        if (f.number == field_num && f.wire_type == WireType::Fixed32) {
            float v = fallback;
            std::memcpy(&v, &f.fixed32, sizeof(float));
            return v;
        }
    }
    return fallback;
}

std::vector<std::string_view> all_bytes(const std::vector<Field>& fields,
                                        std::uint32_t field_num) {
    std::vector<std::string_view> out;
    for (const auto& f : fields) {
        if (f.number == field_num && f.wire_type == WireType::LengthDelimited) {
            out.push_back(f.bytes);
        }
    }
    return out;
}

void encode_varint(std::string* out, std::uint64_t val) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(val & 0x7Fu);
        val >>= 7;
        if (val != 0) byte |= 0x80u;
        out->push_back(static_cast<char>(byte));
    } while (val != 0);
}

void encode_tag(std::string* out, std::uint32_t field_num, WireType wt) {
    encode_varint(out, (static_cast<std::uint64_t>(field_num) << 3) |
                       static_cast<std::uint64_t>(wt));
}

void encode_varint_field(std::string* out, std::uint32_t field_num, std::uint64_t val) {
    encode_tag(out, field_num, WireType::Varint);
    encode_varint(out, val);
}

void encode_bytes_field(std::string* out, std::uint32_t field_num, std::string_view payload) {
    encode_tag(out, field_num, WireType::LengthDelimited);
    encode_varint(out, payload.size());
    out->append(payload.data(), payload.size());
}

void encode_float_field(std::string* out, std::uint32_t field_num, float val) {
    encode_tag(out, field_num, WireType::Fixed32);
    std::uint32_t raw = 0;
    std::memcpy(&raw, &val, sizeof(float));
    char buf[4];
    std::memcpy(buf, &raw, 4);
    out->append(buf, 4);
}

}  // namespace finn::store::pb
