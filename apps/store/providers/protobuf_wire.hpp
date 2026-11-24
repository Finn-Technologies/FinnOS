// Self-contained, bounds-checked Protocol Buffers v2/v3 wire-format codec
// for the Aurora Store / Google Play FDFE binary protobuf protocol.
// Zero external protobuf library dependency.

#ifndef FINN_STORE_PROTOBUF_WIRE_HPP
#define FINN_STORE_PROTOBUF_WIRE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace finn::store::pb {

enum class WireType : std::uint8_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    StartGroup = 3,
    EndGroup = 4,
    Fixed32 = 5,
};

struct Field {
    std::uint32_t number = 0;
    WireType wire_type = WireType::Varint;
    std::uint64_t varint = 0;
    std::uint32_t fixed32 = 0;
    std::uint64_t fixed64 = 0;
    std::string_view bytes;  // slice into the original buffer for LengthDelimited
};

// Decodes all top-level fields of a protobuf message. Returns false if the
// wire data is truncated or malformed.
bool decode_message(std::string_view data, std::vector<Field>* out_fields);

// Convenience helpers for reading parsed fields
std::string first_string(const std::vector<Field>& fields, std::uint32_t field_num);
std::uint64_t first_varint(const std::vector<Field>& fields, std::uint32_t field_num,
                           std::uint64_t fallback = 0);
float first_float(const std::vector<Field>& fields, std::uint32_t field_num,
                  float fallback = 0.0f);
std::vector<std::string_view> all_bytes(const std::vector<Field>& fields,
                                        std::uint32_t field_num);

// Minimal protobuf wire encoder (used for FDFE requests and unit tests)
void encode_varint(std::string* out, std::uint64_t val);
void encode_tag(std::string* out, std::uint32_t field_num, WireType wt);
void encode_varint_field(std::string* out, std::uint32_t field_num, std::uint64_t val);
void encode_bytes_field(std::string* out, std::uint32_t field_num, std::string_view payload);
void encode_float_field(std::string* out, std::uint32_t field_num, float val);

}  // namespace finn::store::pb

#endif  // FINN_STORE_PROTOBUF_WIRE_HPP
