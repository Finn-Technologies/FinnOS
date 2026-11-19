#include "android/zip_reader.hpp"

#include "android/android_format.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;

namespace finn::android {
namespace {

bool inflate_raw(const std::uint8_t* src, std::size_t src_len,
                 std::uint32_t expected_size, std::size_t max_bytes,
                 std::vector<std::uint8_t>* out, std::string* error) {
    if (expected_size > max_bytes) {
        if (error) *error = "ZIP entry uncompressed size exceeds safety limit";
        return false;
    }
    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    strm.avail_in = static_cast<uInt>(src_len);
    if (::inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
        if (error) *error = "zlib inflateInit2 failed";
        return false;
    }
    out->clear();
    if (expected_size > 0u && expected_size <= 64u * 1024u * 1024u) {
        out->reserve(expected_size);
    }
    std::uint8_t chunk[65536];
    int rc = Z_OK;
    while (rc == Z_OK) {
        strm.next_out = chunk;
        strm.avail_out = sizeof(chunk);
        rc = ::inflate(&strm, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            ::inflateEnd(&strm);
            if (error) *error = "corrupt Deflate stream in ZIP entry";
            return false;
        }
        const std::size_t produced = sizeof(chunk) - strm.avail_out;
        if (out->size() + produced > max_bytes) {
            ::inflateEnd(&strm);
            if (error) *error = "decompressed ZIP entry exceeds safety limit";
            return false;
        }
        out->insert(out->end(), chunk, chunk + produced);
    }
    ::inflateEnd(&strm);
    return rc == Z_STREAM_END;
}

}  // namespace

bool ZipReader::is_safe_entry_path(std::string_view name) {
    if (name.empty() || name.find('\0') != std::string_view::npos) return false;
    if (name.front() == '/' || name.front() == '\\') return false;
    if (name.size() >= 2u && std::isalpha(static_cast<unsigned char>(name[0])) &&
        name[1] == ':') {
        return false;
    }
    std::size_t pos = 0;
    while (pos < name.size()) {
        const std::size_t sep = name.find_first_of("/\\", pos);
        const std::string_view part =
            (sep == std::string_view::npos) ? name.substr(pos)
                                            : name.substr(pos, sep - pos);
        if (part == "..") return false;
        if (sep == std::string_view::npos) break;
        pos = sep + 1;
    }
    return true;
}

bool ZipReader::has_zip_magic(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::uint8_t magic[4] = {};
    in.read(reinterpret_cast<char*>(magic), 4);
    if (in.gcount() != 4) return false;
    return read_le32(magic) == kZipLocalFileSig;
}

bool ZipReader::open_file(const std::string& path, std::string* error) {
    open_ = false;
    data_.clear();
    entries_.clear();
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        if (error) *error = "ZIP/APK does not exist or is not a regular file: " + path;
        return false;
    }
    const auto sz = fs::file_size(path, ec);
    if (ec || sz < 22u) {
        if (error) *error = "file is too small to be a valid ZIP archive";
        return false;
    }
    if (sz > 1024ull * 1024ull * 1024ull) {
        if (error) *error = "ZIP archive exceeds 1 GiB safety limit";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "unable to open ZIP archive: " + path;
        return false;
    }
    data_.resize(static_cast<std::size_t>(sz));
    in.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(sz));
    if (static_cast<std::size_t>(in.gcount()) != data_.size()) {
        data_.clear();
        if (error) *error = "short read while loading ZIP archive";
        return false;
    }
    return parse_central_directory(error);
}

bool ZipReader::open_memory(const std::uint8_t* data, std::size_t size,
                            std::string* error) {
    open_ = false;
    data_.clear();
    entries_.clear();
    if (!data || size < 22u) {
        if (error) *error = "buffer is too small to be a valid ZIP archive";
        return false;
    }
    data_.assign(data, data + size);
    return parse_central_directory(error);
}

bool ZipReader::parse_central_directory(std::string* error) {
    const std::size_t total = data_.size();
    const std::size_t max_scan = std::min<std::size_t>(total, 65535u + 22u);
    const std::size_t min_pos = total - max_scan;

    std::size_t eocd = std::string::npos;
    for (std::size_t pos = total - 22u; ; --pos) {
        if (read_le32(data_.data() + pos) == kZipEndCentralDirSig) {
            const std::uint16_t comment_len = read_le16(data_.data() + pos + 20);
            if (static_cast<std::uint64_t>(pos) + 22u + comment_len == total) {
                eocd = pos;
                break;
            }
        }
        if (pos == min_pos) break;
    }
    if (eocd == std::string::npos) {
        if (error) *error = "missing ZIP End of Central Directory record";
        return false;
    }

    const std::uint16_t count = read_le16(data_.data() + eocd + 10);
    const std::uint32_t cd_size = read_le32(data_.data() + eocd + 12);
    const std::uint32_t cd_off = read_le32(data_.data() + eocd + 16);
    if (static_cast<std::uint64_t>(cd_off) + cd_size > eocd) {
        if (error) *error = "ZIP central directory bounds exceed archive size";
        return false;
    }

    std::size_t cur = cd_off;
    const std::size_t cd_end = static_cast<std::size_t>(cd_off) + cd_size;
    entries_.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        if (cur + 46u > cd_end ||
            read_le32(data_.data() + cur) != kZipCentralDirSig) {
            if (error) *error = "truncated or invalid ZIP central directory entry";
            entries_.clear();
            return false;
        }
        const std::uint16_t method = read_le16(data_.data() + cur + 10);
        const std::uint32_t crc = read_le32(data_.data() + cur + 16);
        const std::uint32_t comp_size = read_le32(data_.data() + cur + 20);
        const std::uint32_t uncomp_size = read_le32(data_.data() + cur + 24);
        const std::uint16_t name_len = read_le16(data_.data() + cur + 28);
        const std::uint16_t extra_len = read_le16(data_.data() + cur + 30);
        const std::uint16_t comment_len = read_le16(data_.data() + cur + 32);
        const std::uint32_t local_off = read_le32(data_.data() + cur + 42);
        const std::size_t next = cur + 46u + name_len + extra_len + comment_len;
        if (next > cd_end) {
            if (error) *error = "ZIP central directory metadata out of bounds";
            entries_.clear();
            return false;
        }
        std::string name(reinterpret_cast<const char*>(data_.data() + cur + 46), name_len);
        if (!is_safe_entry_path(name)) {
            if (error) *error = "unsafe path rejected in ZIP archive: " + name;
            entries_.clear();
            return false;
        }
        ZipEntry e;
        e.name = std::move(name);
        e.compression_method = method;
        e.crc32 = crc;
        e.compressed_size = comp_size;
        e.uncompressed_size = uncomp_size;
        e.local_header_offset = local_off;
        e.is_directory = (!e.name.empty() && e.name.back() == '/');
        entries_.push_back(std::move(e));
        cur = next;
    }
    open_ = true;
    return true;
}

const ZipEntry* ZipReader::find_entry(std::string_view name) const {
    for (const auto& e : entries_) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

bool ZipReader::has_entry(std::string_view name) const {
    return find_entry(name) != nullptr;
}

bool ZipReader::read_entry(std::string_view name, std::vector<std::uint8_t>* out,
                           std::string* error, std::size_t max_bytes) const {
    if (!open_ || !out) {
        if (error) *error = "ZIP archive is not open";
        return false;
    }
    const ZipEntry* e = find_entry(name);
    if (!e) {
        if (error) *error = "ZIP entry not found: " + std::string(name);
        return false;
    }
    if (e->is_directory) {
        out->clear();
        return true;
    }
    const std::size_t lh = e->local_header_offset;
    if (lh + 30u > data_.size() ||
        read_le32(data_.data() + lh) != kZipLocalFileSig) {
        if (error) *error = "invalid ZIP local file header";
        return false;
    }
    const std::uint16_t name_len = read_le16(data_.data() + lh + 26);
    const std::uint16_t extra_len = read_le16(data_.data() + lh + 28);
    const std::size_t data_off = lh + 30u + name_len + extra_len;
    if (data_off + e->compressed_size > data_.size()) {
        if (error) *error = "ZIP entry payload out of bounds";
        return false;
    }
    const std::uint8_t* payload = data_.data() + data_off;
    if (e->compression_method == kZipMethodStore) {
        if (e->compressed_size > max_bytes) {
            if (error) *error = "stored ZIP entry exceeds safety limit";
            return false;
        }
        out->assign(payload, payload + e->compressed_size);
    } else if (e->compression_method == kZipMethodDeflate) {
        if (!inflate_raw(payload, e->compressed_size, e->uncompressed_size,
                         max_bytes, out, error)) {
            return false;
        }
    } else {
        if (error) {
            *error = "unsupported ZIP compression method " +
                     std::to_string(e->compression_method) + " for " + e->name;
        }
        return false;
    }
    if (out->size() != e->uncompressed_size) {
        if (error) *error = "ZIP entry size mismatch for " + e->name;
        return false;
    }
    const std::uint32_t actual = static_cast<std::uint32_t>(
        ::crc32(0L, out->empty() ? Z_NULL : out->data(),
                static_cast<uInt>(out->size())));
    if (actual != e->crc32) {
        if (error) *error = "ZIP entry CRC-32 mismatch for " + e->name;
        return false;
    }
    return true;
}

bool ZipReader::read_entry_text(std::string_view name, std::string* out,
                                std::string* error, std::size_t max_bytes) const {
    if (!out) return false;
    std::vector<std::uint8_t> bytes;
    if (!read_entry(name, &bytes, error, max_bytes)) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool ZipReader::extract_all(const std::string& dest_dir,
                            std::vector<std::string>* extracted_files,
                            std::string* error) const {
    if (!open_) {
        if (error) *error = "ZIP archive is not open";
        return false;
    }
    std::error_code ec;
    const fs::path root(dest_dir);
    fs::create_directories(root, ec);
    if (ec) {
        if (error) *error = "failed to create extraction directory: " + ec.message();
        return false;
    }
    for (const auto& e : entries_) {
        if (!is_safe_entry_path(e.name)) {
            if (error) *error = "unsafe path in ZIP entry: " + e.name;
            return false;
        }
        const fs::path target = root / fs::path(e.name);
        if (e.is_directory) {
            fs::create_directories(target, ec);
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        std::vector<std::uint8_t> buf;
        if (!read_entry(e.name, &buf, error)) return false;
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "failed to write extracted file: " + target.string();
            return false;
        }
        if (!buf.empty()) {
            out.write(reinterpret_cast<const char*>(buf.data()),
                      static_cast<std::streamsize>(buf.size()));
        }
        if (!out) {
            if (error) *error = "write error on extracted file: " + target.string();
            return false;
        }
        if (extracted_files) extracted_files->push_back(target.string());
    }
    return true;
}

bool is_apk_path(std::string_view path) {
    if (path.size() < 5) return false;
    const auto off = path.size() - 4;
    return path[off] == '.' &&
           std::tolower(static_cast<unsigned char>(path[off + 1])) == 'a' &&
           std::tolower(static_cast<unsigned char>(path[off + 2])) == 'p' &&
           std::tolower(static_cast<unsigned char>(path[off + 3])) == 'k';
}

}  // namespace finn::android
