// Clean-room bounds-checked ZIP/APK reader. Uses zlib directly (Stored and
// Deflate), rejects path traversal, and verifies CRC-32 on every entry.

#ifndef FINN_ANDROID_ZIP_READER_HPP
#define FINN_ANDROID_ZIP_READER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace finn::android {

struct ZipEntry {
    std::string name;
    std::uint16_t compression_method = 0;
    std::uint32_t crc32 = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t local_header_offset = 0;
    bool is_directory = false;
};

class ZipReader {
public:
    bool open_file(const std::string& path, std::string* error = nullptr);
    bool open_memory(const std::uint8_t* data, std::size_t size,
                     std::string* error = nullptr);

    bool is_open() const { return open_; }
    const std::vector<ZipEntry>& entries() const { return entries_; }

    bool has_entry(std::string_view name) const;
    const ZipEntry* find_entry(std::string_view name) const;

    bool read_entry(std::string_view name, std::vector<std::uint8_t>* out,
                    std::string* error = nullptr,
                    std::size_t max_bytes = 256u * 1024u * 1024u) const;
    bool read_entry_text(std::string_view name, std::string* out,
                         std::string* error = nullptr,
                         std::size_t max_bytes = 64u * 1024u * 1024u) const;

    bool extract_all(const std::string& dest_dir,
                     std::vector<std::string>* extracted_files = nullptr,
                     std::string* error = nullptr) const;

    // Rejects absolute paths, drive prefixes, NUL bytes and ".." traversal.
    static bool is_safe_entry_path(std::string_view name);
    // Checks for the ZIP local file magic at offset zero.
    static bool has_zip_magic(const std::string& path);

private:
    bool parse_central_directory(std::string* error);

    bool open_ = false;
    std::vector<std::uint8_t> data_;
    std::vector<ZipEntry> entries_;
};

// True when a path names a regular file ending in .apk (case-insensitive).
bool is_apk_path(std::string_view path);

}  // namespace finn::android

#endif  // FINN_ANDROID_ZIP_READER_HPP
