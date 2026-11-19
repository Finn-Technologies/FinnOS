// Bounds-checked, dependency-free reader for ZIP central-directory entry names.
//
// The macOS (.zip app archives) and OpenHarmony (.hap/.hsp/.app) installers both
// need to know what is inside a ZIP before claiming it is installable. Keeping
// one reader means the two paths agree on what a real package contains, and a
// truncated or hostile archive cannot make either walk off the end of a buffer.

#ifndef FINN_COMPAT_ZIP_ENTRIES_HPP
#define FINN_COMPAT_ZIP_ENTRIES_HPP

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace finn::compat {
namespace zip_detail {

inline std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t read_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace zip_detail

// Reads the uncompressed central directory of a small ZIP (HAP or .app archive).
// Returns false for anything that is not a well-formed ZIP rather than guessing.
inline bool read_zip_entry_names(const std::string& path,
                                 std::vector<std::string>* names) {
    if (names == nullptr) return false;
    names->clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size < 22 || size > 512u * 1024u * 1024u) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::uint8_t lead[4] = {};
    in.read(reinterpret_cast<char*>(lead), 4);
    if (in.gcount() != 4) return false;
    const std::uint32_t magic = zip_detail::read_u32_le(lead);
    // A local file header for a normal archive, or an empty archive's EOCD.
    if (magic != 0x04034b50u && magic != 0x06054b50u) return false;

    const std::size_t tail_len = static_cast<std::size_t>(
        std::min<std::uintmax_t>(size, 65557u));
    std::vector<std::uint8_t> tail(tail_len);
    in.seekg(static_cast<std::streamoff>(size - tail_len), std::ios::beg);
    in.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail_len));
    if (static_cast<std::size_t>(in.gcount()) != tail_len) return false;

    std::size_t eocd = std::string::npos;
    for (std::size_t pos = tail_len - 22;; --pos) {
        if (zip_detail::read_u32_le(tail.data() + pos) == 0x06054b50u) {
            const std::uint16_t comment_len = zip_detail::read_u16_le(tail.data() + pos + 20);
            if (pos + 22u + comment_len == tail_len) {
                eocd = pos;
                break;
            }
        }
        // Stop at zero. Decrementing below it wraps and reads out of bounds.
        if (pos == 0) break;
    }
    if (eocd == std::string::npos) return false;

    const std::uint16_t count = zip_detail::read_u16_le(tail.data() + eocd + 10);
    const std::uint32_t cd_size = zip_detail::read_u32_le(tail.data() + eocd + 12);
    const std::uint32_t cd_off = zip_detail::read_u32_le(tail.data() + eocd + 16);
    if (cd_size > 16u * 1024u * 1024u ||
        static_cast<std::uint64_t>(cd_off) + cd_size > size) {
        return false;
    }

    std::vector<std::uint8_t> cd(cd_size);
    in.seekg(static_cast<std::streamoff>(cd_off), std::ios::beg);
    in.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cd_size));
    if (static_cast<std::size_t>(in.gcount()) != cd_size) return false;

    std::size_t cur = 0;
    for (std::uint16_t i = 0; i < count && cur + 46 <= cd.size(); ++i) {
        if (zip_detail::read_u32_le(cd.data() + cur) != 0x02014b50u) return false;
        const std::uint16_t name_len = zip_detail::read_u16_le(cd.data() + cur + 28);
        const std::uint16_t extra_len = zip_detail::read_u16_le(cd.data() + cur + 30);
        const std::uint16_t comment = zip_detail::read_u16_le(cd.data() + cur + 32);
        const std::size_t next = cur + 46u + name_len + extra_len + comment;
        if (next > cd.size()) return false;
        names->emplace_back(reinterpret_cast<const char*>(cd.data() + cur + 46), name_len);
        cur = next;
    }
    return true;
}

// True only when the archive actually carries a .app/Contents/Info.plist; a ZIP
// full of unrelated files is not a macOS app archive and must not be labelled
// as one. This reads names, not the app payload, so it stays cheap for Files.
inline bool zip_contains_macos_app(const std::string& path) {
    std::vector<std::string> names;
    if (!read_zip_entry_names(path, &names)) return false;
    for (const std::string& name : names) {
        const std::size_t app = name.find(".app/");
        if (app == std::string::npos) continue;
        const std::string after = name.substr(app + 5);
        if (after.rfind("Contents/Info.plist", 0) == 0) return true;
    }
    return false;
}

}  // namespace finn::compat

#endif
