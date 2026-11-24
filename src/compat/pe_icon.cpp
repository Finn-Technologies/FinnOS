#include "compat/pe_icon.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

constexpr std::uint32_t kRtIcon = 3;
constexpr std::uint32_t kRtGroupIcon = 14;
constexpr std::size_t kMaxIconReadBytes = 64u * 1024u * 1024u;

std::uint16_t rd16(std::string_view b, std::size_t off) {
    if (off + 2 > b.size()) return 0;
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off + 1])) << 8);
}

std::uint32_t rd32(std::string_view b, std::size_t off) {
    if (off + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 3])) << 24);
}

bool is_png_magic(std::string_view s) {
    return s.size() >= 8 && static_cast<unsigned char>(s[0]) == 0x89u &&
           s[1] == 'P' && s[2] == 'N' && s[3] == 'G' &&
           s[4] == '\r' && s[5] == '\n' &&
           static_cast<unsigned char>(s[6]) == 0x1au && s[7] == '\n';
}

struct PeSection {
    std::uint32_t vaddr = 0;
    std::uint32_t vsize = 0;
    std::uint32_t raw_off = 0;
    std::uint32_t raw_sz = 0;
};

bool rva_to_offset(const std::vector<PeSection>& secs, std::uint32_t rva,
                   std::size_t* out_off) {
    for (const auto& s : secs) {
        const std::uint32_t span = std::max(s.vsize, s.raw_sz);
        if (rva >= s.vaddr && rva < s.vaddr + span) {
            const std::uint32_t delta = rva - s.vaddr;
            if (delta < s.raw_sz) {
                *out_off = static_cast<std::size_t>(s.raw_off) + delta;
                return true;
            }
        }
    }
    return false;
}

struct RsrcLeaf {
    std::uint32_t type_id = 0;
    std::uint32_t name_id = 0;
    std::uint32_t data_rva = 0;
    std::uint32_t data_size = 0;
};

void walk_rsrc_dir(std::string_view pe, std::size_t rsrc_base, std::size_t dir_off,
                   int depth, std::uint32_t type_id, std::uint32_t name_id,
                   std::vector<RsrcLeaf>* leaves) {
    if (depth > 3 || leaves->size() > 256) return;
    if (dir_off + 16 > pe.size()) return;
    const std::uint16_t n_named = rd16(pe, dir_off + 12);
    const std::uint16_t n_id = rd16(pe, dir_off + 14);
    const std::size_t total = static_cast<std::size_t>(n_named) + n_id;
    if (total > 512) return;

    for (std::size_t i = 0; i < total; ++i) {
        const std::size_t ent_off = dir_off + 16 + i * 8;
        if (ent_off + 8 > pe.size()) break;
        const std::uint32_t id = rd32(pe, ent_off);
        const std::uint32_t sub = rd32(pe, ent_off + 4);
        const bool is_dir = (sub & 0x80000000u) != 0;
        const std::uint32_t rel = sub & 0x7FFFFFFFu;

        std::uint32_t next_type = type_id;
        std::uint32_t next_name = name_id;
        if (depth == 0) {
            if (id != kRtIcon && id != kRtGroupIcon) continue;
            next_type = id;
        } else if (depth == 1) {
            next_name = id;
        }

        if (is_dir) {
            walk_rsrc_dir(pe, rsrc_base, rsrc_base + rel, depth + 1,
                          next_type, next_name, leaves);
        } else if (depth == 2) {
            const std::size_t data_ent = rsrc_base + rel;
            if (data_ent + 16 <= pe.size()) {
                RsrcLeaf leaf;
                leaf.type_id = next_type;
                leaf.name_id = next_name;
                leaf.data_rva = rd32(pe, data_ent);
                leaf.data_size = rd32(pe, data_ent + 4);
                leaves->push_back(leaf);
            }
        }
    }
}

std::vector<std::uint8_t> wrap_dib_as_ico(std::uint8_t w, std::uint8_t h,
                                          std::uint16_t planes, std::uint16_t bpp,
                                          std::string_view payload) {
    std::vector<std::uint8_t> out;
    out.resize(22 + payload.size(), 0);
    // ICONDIR: reserved=0, type=1 (ICO), count=1
    out[2] = 1;
    out[4] = 1;
    // ICONDIRENTRY (16 bytes)
    out[6] = w;
    out[7] = h;
    out[10] = static_cast<std::uint8_t>(planes & 0xFFu);
    out[11] = static_cast<std::uint8_t>((planes >> 8) & 0xFFu);
    out[12] = static_cast<std::uint8_t>(bpp & 0xFFu);
    out[13] = static_cast<std::uint8_t>((bpp >> 8) & 0xFFu);
    const std::uint32_t sz = static_cast<std::uint32_t>(payload.size());
    out[14] = static_cast<std::uint8_t>(sz & 0xFFu);
    out[15] = static_cast<std::uint8_t>((sz >> 8) & 0xFFu);
    out[16] = static_cast<std::uint8_t>((sz >> 16) & 0xFFu);
    out[17] = static_cast<std::uint8_t>((sz >> 24) & 0xFFu);
    out[18] = 22;  // ImageOffset = 6 + 16
    std::memcpy(out.data() + 22, payload.data(), payload.size());
    return out;
}

}  // namespace

ExtractedIcon extract_pe_or_ico_icon_bytes(std::string_view b) {
    ExtractedIcon res;
    if (b.size() < 22) {
        res.error = "buffer too small for PE or ICO";
        return res;
    }

    // Case 1: Standalone .ico file
    if (rd16(b, 0) == 0 && rd16(b, 2) == 1 && rd16(b, 4) >= 1) {
        const std::uint16_t count = rd16(b, 4);
        int best_score = -1;
        std::size_t best_off = 0;
        std::size_t best_sz = 0;
        for (std::uint16_t i = 0; i < count && std::size_t{6} + (std::size_t{i} + 1) * 16 <= b.size(); ++i) {
            const std::size_t e = 6 + i * 16;
            const int w = static_cast<unsigned char>(b[e]) == 0 ? 256 : static_cast<unsigned char>(b[e]);
            const int bpp = rd16(b, e + 6);
            const std::uint32_t sz = rd32(b, e + 8);
            const std::uint32_t off = rd32(b, e + 12);
            if (off > b.size() || sz > b.size() - off || sz == 0) continue;
            const bool png = is_png_magic(b.substr(off, sz));
            const int score = w * 1000 + bpp * 10 + (png ? 500000 : 0);
            if (score > best_score) {
                best_score = score;
                best_off = off;
                best_sz = sz;
            }
        }
        if (best_score >= 0) {
            const std::string_view img = b.substr(best_off, best_sz);
            if (is_png_magic(img)) {
                res.ok = true;
                res.ext = ".png";
                res.bytes.assign(img.begin(), img.end());
                return res;
            }
            res.ok = true;
            res.ext = ".ico";
            res.bytes.assign(b.begin(), b.end());
            return res;
        }
    }

    // Case 2: Windows PE executable / DLL
    if (b.size() < 64 || b[0] != 'M' || b[1] != 'Z') {
        res.error = "not a PE or ICO file";
        return res;
    }
    const std::uint32_t pe_off = rd32(b, 60);
    if (pe_off > b.size() || b.size() - pe_off < 24 ||
        b[pe_off] != 'P' || b[pe_off + 1] != 'E' || b[pe_off + 2] != '\0' || b[pe_off + 3] != '\0') {
        res.error = "invalid PE signature";
        return res;
    }
    const std::size_t coff = pe_off + 4;
    const std::uint16_t n_secs = rd16(b, coff + 2);
    const std::uint16_t opt_sz = rd16(b, coff + 16);
    const std::size_t opt = coff + 20;
    if (opt + opt_sz > b.size() || opt_sz < 112) {
        res.error = "truncated PE optional header";
        return res;
    }
    const std::uint16_t magic = rd16(b, opt);
    const std::size_t dd_off = (magic == 0x020bu) ? (opt + 112) : (opt + 96);
    // DataDirectory[2] = Resource Table
    if (dd_off + 24 > opt + opt_sz) {
        res.error = "PE has no resource directory";
        return res;
    }
    const std::uint32_t rsrc_rva = rd32(b, dd_off + 16);
    const std::uint32_t rsrc_sz = rd32(b, dd_off + 20);
    if (rsrc_rva == 0 || rsrc_sz == 0) {
        res.error = "PE resource directory is empty";
        return res;
    }

    std::vector<PeSection> secs;
    secs.reserve(n_secs);
    const std::size_t sec_tbl = opt + opt_sz;
    for (std::uint16_t i = 0; i < n_secs && sec_tbl + (i + 1) * 40 <= b.size(); ++i) {
        const std::size_t so = sec_tbl + i * 40;
        PeSection s;
        s.vsize = rd32(b, so + 8);
        s.vaddr = rd32(b, so + 12);
        s.raw_sz = rd32(b, so + 16);
        s.raw_off = rd32(b, so + 20);
        secs.push_back(s);
    }

    std::size_t rsrc_base = 0;
    if (!rva_to_offset(secs, rsrc_rva, &rsrc_base)) {
        res.error = "cannot map PE resource directory RVA";
        return res;
    }

    std::vector<RsrcLeaf> leaves;
    walk_rsrc_dir(b, rsrc_base, rsrc_base, 0, 0, 0, &leaves);

    // Find best icon entry via RT_GROUP_ICON -> RT_ICON
    int best_score = -1;
    std::uint8_t best_w = 0, best_h = 0;
    std::uint16_t best_planes = 1, best_bpp = 32;
    std::string_view best_payload;

    auto find_rt_icon = [&](std::uint32_t icon_id) -> std::string_view {
        for (const auto& lf : leaves) {
            if (lf.type_id == kRtIcon && lf.name_id == icon_id) {
                std::size_t off = 0;
                if (rva_to_offset(secs, lf.data_rva, &off) &&
                    off <= b.size() && lf.data_size <= b.size() - off) {
                    return b.substr(off, lf.data_size);
                }
            }
        }
        return {};
    };

    for (const auto& lf : leaves) {
        if (lf.type_id != kRtGroupIcon) continue;
        std::size_t off = 0;
        if (!rva_to_offset(secs, lf.data_rva, &off) || off + 6 > b.size() ||
            lf.data_size > b.size() - off) {
            continue;
        }
        const std::string_view grp = b.substr(off, lf.data_size);
        if (rd16(grp, 0) != 0 || rd16(grp, 2) != 1) continue;
        const std::uint16_t cnt = rd16(grp, 4);
        for (std::uint16_t i = 0; i < cnt && std::size_t{6} + (std::size_t{i} + 1) * 14 <= grp.size(); ++i) {
            const std::size_t e = 6 + i * 14;
            const std::uint8_t w = static_cast<std::uint8_t>(grp[e]);
            const std::uint8_t h = static_cast<std::uint8_t>(grp[e + 1]);
            const std::uint16_t planes = rd16(grp, e + 4);
            const std::uint16_t bpp = rd16(grp, e + 6);
            const std::uint16_t icon_id = rd16(grp, e + 12);
            const std::string_view payload = find_rt_icon(icon_id);
            if (payload.empty()) continue;
            const int dim = (w == 0 ? 256 : static_cast<int>(w));
            const bool png = is_png_magic(payload);
            const int score = dim * 1000 + bpp * 10 + (png ? 500000 : 0);
            if (score > best_score) {
                best_score = score;
                best_w = w;
                best_h = h;
                best_planes = planes;
                best_bpp = bpp;
                best_payload = payload;
            }
        }
    }

    if (best_score < 0 || best_payload.empty()) {
        res.error = "no RT_GROUP_ICON/RT_ICON resources found";
        return res;
    }

    if (is_png_magic(best_payload)) {
        res.ok = true;
        res.ext = ".png";
        res.bytes.assign(best_payload.begin(), best_payload.end());
        return res;
    }
    res.ok = true;
    res.ext = ".ico";
    res.bytes = wrap_dib_as_ico(best_w, best_h, best_planes, best_bpp, best_payload);
    return res;
}

std::string extract_and_cache_pe_icon(const std::string& exe_or_ico_path,
                                      const std::string& slug,
                                      const std::string& cache_dir) {
    if (exe_or_ico_path.empty() || slug.empty()) return {};
    std::string dir = cache_dir;
    if (dir.empty()) {
        if (const char* home = std::getenv("HOME"); home && *home) {
            dir = std::string(home) + "/.local/share/finn/win-icons";
        } else {
            return {};
        }
    }
    std::error_code ec;
    for (const char* ext : {".png", ".ico"}) {
        const fs::path cached = fs::path(dir) / (slug + ext);
        if (fs::is_regular_file(cached, ec)) return cached.string();
    }

    const auto fsz = fs::file_size(exe_or_ico_path, ec);
    if (ec || fsz < 22 || fsz > kMaxIconReadBytes) return {};
    std::ifstream in(exe_or_ico_path, std::ios::binary);
    if (!in) return {};
    std::string buf(static_cast<std::size_t>(fsz), '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));

    const ExtractedIcon icon = extract_pe_or_ico_icon_bytes(buf);
    if (!icon.ok || icon.bytes.empty()) return {};

    fs::create_directories(dir, ec);
    const fs::path dst = fs::path(dir) / (slug + icon.ext);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return {};
    out.write(reinterpret_cast<const char*>(icon.bytes.data()),
              static_cast<std::streamsize>(icon.bytes.size()));
    if (!out) return {};
    return dst.string();
}

}  // namespace finn::compat
