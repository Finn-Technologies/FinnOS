// Clean-room Apple XML and Binary Property List (bplist00) parser.
// Pure C++20 with strict bounds checking so Info.plist and UDIF blkx tables
// parse safely on Linux and macOS without external libraries.

#ifndef FINN_DARWIN_PLIST_HPP
#define FINN_DARWIN_PLIST_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finn::darwin {

struct PlistValue {
    enum class Kind {
        Null,
        Bool,
        Int,
        Real,
        String,
        Data,
        Array,
        Dict,
    };

    Kind kind = Kind::Null;
    bool bool_val = false;
    std::int64_t int_val = 0;
    double real_val = 0.0;
    std::string string_val;
    std::vector<std::uint8_t> data_val;
    std::vector<PlistValue> array_val;
    std::map<std::string, PlistValue> dict_val;

    const PlistValue* get(std::string_view key) const;
    std::string get_string(std::string_view key, std::string_view fallback = {}) const;
    bool get_bool(std::string_view key, bool fallback = false) const;
};

// Parses either XML (<plist>...</plist>) or Binary (bplist00) property lists.
std::optional<PlistValue> parse_plist_bytes(std::string_view bytes,
                                            std::string* error = nullptr);
std::optional<PlistValue> parse_plist_file(const std::string& path,
                                           std::string* error = nullptr);

// Base64 decoder used for XML <data> elements (e.g. DMG blkx tables).
std::vector<std::uint8_t> decode_base64(std::string_view text);

}  // namespace finn::darwin

#endif
