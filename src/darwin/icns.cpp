#include "darwin/icns.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::darwin {
namespace {

std::uint32_t read_u32_be(std::string_view b, std::size_t off) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3]));
}

int icns_chunk_rank(std::string_view tag) {
    // Prefer crisp 256/512/128 PNG icons for FinnOS dock & launcher cards.
    if (tag == "ic08" || tag == "ic13") return 100;  // 256x256
    if (tag == "ic09" || tag == "ic14") return 95;   // 512x512
    if (tag == "ic07" || tag == "ic12") return 90;   // 128x128
    if (tag == "ic10") return 85;                    // 1024x1024
    if (tag == "icp6" || tag == "ic11") return 70;   // 64x64
    if (tag == "icp5") return 50;                    // 32x32
    if (tag == "icp4") return 30;                    // 16x16
    return 10;
}

bool has_png_magic(std::string_view payload) {
    static constexpr unsigned char kPng[8] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    return payload.size() >= 8 && std::memcmp(payload.data(), kPng, 8) == 0;
}

}  // namespace

std::vector<std::uint8_t> extract_best_png_from_icns(std::string_view icns_bytes) {
    if (icns_bytes.size() < 8 || icns_bytes.substr(0, 4) != "icns") return {};
    const std::uint32_t total_len = read_u32_be(icns_bytes, 4);
    const std::size_t limit = std::min<std::size_t>(icns_bytes.size(), total_len);

    int best_rank = -1;
    std::string_view best_payload;

    std::size_t pos = 8;
    while (pos + 8 <= limit) {
        const std::string_view tag = icns_bytes.substr(pos, 4);
        const std::uint32_t chunk_len = read_u32_be(icns_bytes, pos + 4);
        if (chunk_len < 8 || pos + chunk_len > limit) break;
        const std::string_view payload = icns_bytes.substr(pos + 8, chunk_len - 8);
        if (has_png_magic(payload)) {
            const int rank = icns_chunk_rank(tag);
            if (rank > best_rank) {
                best_rank = rank;
                best_payload = payload;
            }
        }
        pos += chunk_len;
    }

    if (best_payload.empty()) return {};
    return std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(best_payload.data()),
        reinterpret_cast<const std::uint8_t*>(best_payload.data() + best_payload.size()));
}

bool extract_icns_to_png(const std::string& icns_path,
                         const std::string& png_out_path,
                         std::string* error) {
    std::ifstream in(icns_path, std::ios::binary);
    if (!in) {
        if (error) *error = "unable to read .icns file: " + icns_path;
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const auto png = extract_best_png_from_icns(buf.str());
    if (png.empty()) {
        if (error) *error = "no PNG icon payload found in " + icns_path;
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(png_out_path).parent_path(), ec);
    std::ofstream out(png_out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "unable to write PNG icon: " + png_out_path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()),
              static_cast<std::streamsize>(png.size()));
    return out.good();
}

}  // namespace finn::darwin
