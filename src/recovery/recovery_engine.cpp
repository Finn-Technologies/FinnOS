#include "recovery/recovery_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::recovery {
namespace {

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMinFinnRootBytes = 12ULL * kGiB;
constexpr std::uint64_t kMinAbDiskBytes = 14ULL * kGiB;
constexpr std::uint64_t kAutoEspBytes = 512ULL * kMiB;
constexpr std::uint64_t kSpaciousEspThresholdBytes = 300ULL * kMiB;
constexpr std::uint64_t kMinEspFreeForBootMountBytes = 220ULL * kMiB;
constexpr std::uint64_t kMinDiskForRecoveryPartBytes = 24ULL * kGiB;

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n\"'");
    if (a == std::string_view::npos) return {};
    const std::size_t b = s.find_last_not_of(" \t\r\n\"'");
    return std::string(s.substr(a, b - a + 1));
}

int trailing_part_number(std::string_view path) {
    std::size_t end = path.size();
    std::size_t pos = end;
    while (pos > 0 && std::isdigit(static_cast<unsigned char>(path[pos - 1]))) --pos;
    if (pos == end) return 0;
    return std::atoi(std::string(path.substr(pos, end - pos)).c_str());
}

PartitionTableKind parse_pttype(std::string_view raw) {
    const std::string t = to_lower(raw);
    if (t == "gpt") return PartitionTableKind::GPT;
    if (t == "dos" || t == "mbr" || t == "msdos") return PartitionTableKind::MBR;
    return PartitionTableKind::None;
}

// ── Pure C++20 SHA-512 (FIPS 180-4) ─────────────────────────────────────────
struct Sha512 {
    static constexpr std::array<std::uint64_t, 80> kK = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
    };

    std::array<std::uint64_t, 8> h = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
    };
    std::array<std::uint8_t, 128> buf{};
    std::size_t buf_len = 0;
    std::uint64_t total_lo = 0;
    std::uint64_t total_hi = 0;

    static constexpr std::uint64_t rotr(std::uint64_t x, unsigned n) noexcept {
        return (x >> n) | (x << (64U - n));
    }

    void compress(const std::uint8_t* block) noexcept {
        std::array<std::uint64_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            std::uint64_t v = 0;
            for (std::size_t j = 0; j < 8; ++j) {
                v = (v << 8) | static_cast<std::uint64_t>(block[i * 8 + j]);
            }
            w[i] = v;
        }
        for (std::size_t i = 16; i < 80; ++i) {
            const std::uint64_t s0 = rotr(w[i - 15], 1) ^ rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const std::uint64_t s1 = rotr(w[i - 2], 19) ^ rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (std::size_t i = 0; i < 80; ++i) {
            const std::uint64_t S1 = rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41);
            const std::uint64_t ch = (e & f) ^ ((~e) & g);
            const std::uint64_t t1 = hh + S1 + ch + kK[i] + w[i];
            const std::uint64_t S0 = rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39);
            const std::uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint64_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const std::uint8_t* data, std::size_t len) noexcept {
        const std::uint64_t prev_lo = total_lo;
        total_lo += static_cast<std::uint64_t>(len);
        if (total_lo < prev_lo) ++total_hi;
        std::size_t off = 0;
        if (buf_len > 0) {
            const std::size_t take = std::min(len, 128 - buf_len);
            std::memcpy(buf.data() + buf_len, data, take);
            buf_len += take;
            off += take;
            if (buf_len == 128) {
                compress(buf.data());
                buf_len = 0;
            }
        }
        while (off + 128 <= len) {
            compress(data + off);
            off += 128;
        }
        if (off < len) {
            buf_len = len - off;
            std::memcpy(buf.data(), data + off, buf_len);
        }
    }

    std::string final_hex() {
        const std::uint64_t bits_lo = total_lo << 3;
        const std::uint64_t bits_hi = (total_hi << 3) | (total_lo >> 61);
        const std::uint8_t pad_one = 0x80;
        update(&pad_one, 1);
        const std::uint8_t zero = 0x00;
        while (buf_len != 112) update(&zero, 1);
        std::array<std::uint8_t, 16> len_bytes{};
        for (int i = 0; i < 8; ++i) {
            len_bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((bits_hi >> (56 - 8 * i)) & 0xffU);
            len_bytes[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>((bits_lo >> (56 - 8 * i)) & 0xffU);
        }
        update(len_bytes.data(), 16);
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(128);
        for (std::uint64_t word : h) {
            for (int shift = 60; shift >= 0; shift -= 4) {
                out.push_back(kHex[(word >> shift) & 0x0fU]);
            }
        }
        return out;
    }
};

// ── Minimal strict recursive-descent JSON reader for lsblk -J output ────────
struct JsonVal {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool b = false;
    std::uint64_t u = 0;
    std::string s;
    std::vector<JsonVal> arr;
    std::vector<std::pair<std::string, JsonVal>> obj;

    const JsonVal* field(std::string_view key) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    std::string str(std::string_view key) const {
        const JsonVal* v = field(key);
        if (!v || v->kind != Kind::String) return {};
        return v->s;
    }

    std::uint64_t u64(std::string_view key) const {
        const JsonVal* v = field(key);
        if (!v) return 0;
        if (v->kind == Kind::Number) return v->u;
        if (v->kind == Kind::String && !v->s.empty()) {
            return std::strtoull(v->s.c_str(), nullptr, 10);
        }
        return 0;
    }

    bool flag(std::string_view key) const {
        const JsonVal* v = field(key);
        if (!v) return false;
        if (v->kind == Kind::Bool) return v->b;
        if (v->kind == Kind::Number) return v->u != 0;
        if (v->kind == Kind::String) return v->s == "1" || v->s == "true";
        return false;
    }
};

struct JsonParser {
    std::string_view src;
    std::size_t i = 0;

    void skip_ws() {
        while (i < src.size() &&
               (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) {
            ++i;
        }
    }

    bool parse_string(std::string* out) {
        skip_ws();
        if (i >= src.size() || src[i] != '"') return false;
        ++i;
        out->clear();
        while (i < src.size()) {
            const char c = src[i++];
            if (c == '"') return true;
            if (c == '\\' && i < src.size()) {
                const char e = src[i++];
                switch (e) {
                    case '"': out->push_back('"'); break;
                    case '\\': out->push_back('\\'); break;
                    case '/': out->push_back('/'); break;
                    case 'n': out->push_back('\n'); break;
                    case 'r': out->push_back('\r'); break;
                    case 't': out->push_back('\t'); break;
                    default: out->push_back(e); break;
                }
            } else {
                out->push_back(c);
            }
        }
        return false;
    }

    bool parse_value(JsonVal* out, int depth = 0) {
        if (depth > 32) return false;
        skip_ws();
        if (i >= src.size()) return false;
        const char c = src[i];
        if (c == '"') {
            out->kind = JsonVal::Kind::String;
            return parse_string(&out->s);
        }
        if (c == '{') {
            ++i;
            out->kind = JsonVal::Kind::Object;
            skip_ws();
            if (i < src.size() && src[i] == '}') { ++i; return true; }
            while (i < src.size()) {
                std::string k;
                if (!parse_string(&k)) return false;
                skip_ws();
                if (i >= src.size() || src[i] != ':') return false;
                ++i;
                JsonVal v;
                if (!parse_value(&v, depth + 1)) return false;
                out->obj.emplace_back(std::move(k), std::move(v));
                skip_ws();
                if (i < src.size() && src[i] == ',') { ++i; continue; }
                if (i < src.size() && src[i] == '}') { ++i; return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            ++i;
            out->kind = JsonVal::Kind::Array;
            skip_ws();
            if (i < src.size() && src[i] == ']') { ++i; return true; }
            while (i < src.size()) {
                JsonVal v;
                if (!parse_value(&v, depth + 1)) return false;
                out->arr.push_back(std::move(v));
                skip_ws();
                if (i < src.size() && src[i] == ',') { ++i; continue; }
                if (i < src.size() && src[i] == ']') { ++i; return true; }
                return false;
            }
            return false;
        }
        if (src.substr(i, 4) == "true") {
            i += 4;
            out->kind = JsonVal::Kind::Bool;
            out->b = true;
            return true;
        }
        if (src.substr(i, 5) == "false") {
            i += 5;
            out->kind = JsonVal::Kind::Bool;
            out->b = false;
            return true;
        }
        if (src.substr(i, 4) == "null") {
            i += 4;
            out->kind = JsonVal::Kind::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t start = i;
            if (src[i] == '-') ++i;
            while (i < src.size() &&
                   (std::isdigit(static_cast<unsigned char>(src[i])) ||
                    src[i] == '.' || src[i] == 'e' || src[i] == 'E' ||
                    src[i] == '+' || src[i] == '-')) {
                ++i;
            }
            out->kind = JsonVal::Kind::Number;
            const std::string num(src.substr(start, i - start));
            out->u = (num[0] == '-') ? 0 : std::strtoull(num.c_str(), nullptr, 10);
            return true;
        }
        return false;
    }
};

void classify_partition(PartitionInfo& p, PartitionTableKind parent_table) {
    const std::string fs_l = to_lower(p.fstype);
    const std::string pt_l = to_lower(p.parttype);
    const std::string pl_l = to_lower(p.partlabel);
    const std::string lb_l = to_lower(p.label);

    // BIOS Boot partition (GPT GUID 21686148-6449-6E6F-744E-656564454649)
    if (pt_l == "21686148-6449-6e6f-744e-656564454649" ||
        pl_l == "bios_boot" || pl_l == "bios boot") {
        p.is_bios_boot = true;
    }

    // MBR Extended / Logical classification
    if (parent_table == PartitionTableKind::MBR ||
        pt_l == "0x5" || pt_l == "0x05" || pt_l == "5" ||
        pt_l == "0xf" || pt_l == "0x0f" || pt_l == "f" ||
        pt_l == "0x85" || pt_l == "85") {
        if (pt_l == "0x5" || pt_l == "0x05" || pt_l == "5" ||
            pt_l == "0xf" || pt_l == "0x0f" || pt_l == "f" ||
            pt_l == "0x85" || pt_l == "85") {
            p.is_extended = true;
        }
        if (parent_table == PartitionTableKind::MBR && p.part_number >= 5) {
            p.is_logical = true;
        }
    }

    // Dedicated FinnOS Offline Recovery partition
    if (pl_l == "finnos_recovery" || lb_l == "finnos_recovery") {
        p.is_recovery_part = true;
    }

    // EFI System Partition: GPT GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B,
    // MBR type 0xef, labeled/mounted as EFI, or a small FAT32 boot partition (#1, <= 2 GiB).
    constexpr std::uint64_t kMaxEspBytes = 2ULL * kGiB;
    if (pt_l == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" ||
        pt_l == "0xef" || pt_l == "ef" ||
        (fs_l == "vfat" && !p.is_recovery_part &&
         (pl_l.find("efi") != std::string::npos ||
          lb_l.find("efi") != std::string::npos ||
          p.mountpoint == "/boot" || p.mountpoint == "/boot/efi" ||
          (p.part_number == 1 && p.size_bytes > 0 && p.size_bytes <= kMaxEspBytes)))) {
        p.is_esp = true;
    }

    // Determine safe offline shrink capability honestly.
    if (p.is_esp) {
        p.can_shrink = false;
        p.shrink_reason = "EFI System Partition cannot be shrunk";
    } else if (p.is_bios_boot) {
        p.can_shrink = false;
        p.shrink_reason = "BIOS Boot partition cannot be shrunk";
    } else if (p.is_extended) {
        p.can_shrink = false;
        p.shrink_reason = "Extended container partition cannot be shrunk directly";
    } else if (p.is_recovery_part) {
        p.can_shrink = false;
        p.shrink_reason = "FinnOS Recovery partition holds the offline OS image";
    } else if (fs_l == "ext4" || fs_l == "ext3" || fs_l == "ext2") {
        p.can_shrink = p.size_bytes >= (20ULL * kGiB);
        p.min_size_bytes = 8ULL * kGiB;
        p.shrink_reason = p.can_shrink ? "Safe offline resize via e2fsck + resize2fs"
                                       : "Partition too small to shrink for dual-boot";
    } else if (fs_l == "ntfs") {
        p.can_shrink = p.size_bytes >= (32ULL * kGiB);
        p.min_size_bytes = 20ULL * kGiB;
        p.shrink_reason = p.can_shrink ? "Safe resize via ntfsresize (disable Windows Fast Startup first)"
                                       : "NTFS partition too small to shrink safely";
    } else if (fs_l == "btrfs") {
        p.can_shrink = p.size_bytes >= (20ULL * kGiB);
        p.min_size_bytes = 8ULL * kGiB;
        p.shrink_reason = p.can_shrink ? "Safe resize via btrfs filesystem resize"
                                       : "btrfs partition too small to shrink";
    } else if (fs_l == "xfs") {
        p.can_shrink = false;
        p.shrink_reason = "XFS filesystems cannot be shrunk";
    } else if (fs_l == "apfs" || pt_l == "7c3457ef-0000-11aa-aa11-00306543ecac") {
        p.can_shrink = false;
        p.shrink_reason = "Resize APFS in macOS Disk Utility first, then choose Install into free space";
    } else if (fs_l == "bitlocker" || fs_l == "crypto_luks") {
        p.can_shrink = false;
        p.shrink_reason = "Encrypted volume must be unlocked/resized before dual-booting";
    } else {
        p.can_shrink = false;
        p.shrink_reason = fs_l.empty() ? "Unformatted partition"
                                       : ("Shrinking " + p.fstype + " is not supported");
    }

    // Heuristic OS classification from partition labels/GUIDs (upgraded by directory probing).
    if (pl_l == "finnos_a" || pl_l == "finnos_b" || lb_l == "finnos" ||
        lb_l == "finnos_a" || lb_l == "finnos_b") {
        p.os_kind = OsKind::FinnOS;
        p.os_name = "FinnOS";
    } else if (pt_l == "7c3457ef-0000-11aa-aa11-00306543ecac" ||
               pt_l == "48465300-0000-11aa-aa11-00306543ecac" ||
               fs_l == "apfs" || fs_l == "hfsplus") {
        p.os_kind = OsKind::MacOS;
        p.os_name = "macOS";
    } else if (fs_l == "ntfs" &&
               pl_l.find("recovery") == std::string::npos &&
               lb_l.find("recovery") == std::string::npos &&
               lb_l != "system reserved" &&
               p.size_bytes >= 2ULL * kGiB) {
        p.os_kind = OsKind::Windows;
        p.os_name = lb_l.empty() ? "Windows" : ("Windows (" + p.label + ")");
    }
}

std::string read_os_release_field(const fs::path& os_release_path, std::string_view key) {
    std::ifstream in(os_release_path);
    if (!in) return {};
    std::string line;
    const std::string prefix = std::string(key) + "=";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return trim(line.substr(prefix.size()));
        }
    }
    return {};
}

const DiskInfo* find_disk(const std::vector<DiskInfo>& disks, std::string_view path) {
    for (const auto& d : disks) {
        if (d.path == path || d.name == path) return &d;
    }
    return nullptr;
}

const PartitionInfo* find_partition(const std::vector<DiskInfo>& disks,
                                    std::string_view path,
                                    const DiskInfo** out_disk = nullptr) {
    for (const auto& d : disks) {
        for (const auto& p : d.partitions) {
            if (p.path == path || p.name == path) {
                if (out_disk) *out_disk = &d;
                return &p;
            }
        }
    }
    return nullptr;
}

// Decide whether an existing ESP is spacious enough to mount at /boot (systemd-boot)
// or should be mounted at /boot/efi (GRUB UEFI) to avoid ENOSPC on 100 MiB Windows ESPs.
bool esp_is_spacious_for_boot_mount(const PartitionInfo& esp) noexcept {
    if (esp.size_bytes < kSpaciousEspThresholdBytes) return false;
    if (esp.fs_avail_bytes > 0 && esp.fs_avail_bytes < kMinEspFreeForBootMountBytes) {
        return false;
    }
    return true;
}

// Check if an MBR disk can accept `needed_partitions` new partitions without violating
// the 4-primary-partition MBR limit.
bool mbr_can_add_partitions(const DiskInfo& disk,
                            int needed_partitions,
                            bool inside_existing_extended,
                            std::string* out_error) {
    if (disk.table_kind != PartitionTableKind::MBR) return true;
    if (inside_existing_extended && disk.has_extended_part) return true;
    const int free_primary_slots = std::max(0, 4 - disk.primary_part_count);
    if (needed_partitions <= free_primary_slots) return true;
    // If at least 1 primary slot is free and no extended partition exists yet,
    // we can create an extended partition holding any number of logical partitions!
    if (free_primary_slots >= 1 && !disk.has_extended_part) return true;
    if (out_error) {
        *out_error = "MBR disk " + disk.path + " already uses all 4 primary partition slots"
                     " with no extended container. Delete an unused partition in Disk Utility"
                     " or choose Erase Disk.";
    }
    return false;
}

void attach_offline_image_args(InstallPlan& plan,
                               const InstallRequest& req,
                               const HostEnvironment& env) {
    if (req.offline_only) {
        plan.finn_install_args.push_back("--offline-only");
    }
    if (env.image.found && !env.image.source_path.empty()) {
        plan.finn_install_args.push_back("--image");
        plan.finn_install_args.push_back(env.image.source_path);
    }
    plan.finn_install_args.push_back("--firmware");
    plan.finn_install_args.push_back(std::string(firmware_mode_id(env.firmware)));
}

}  // namespace

std::string_view os_kind_id(OsKind kind) noexcept {
    switch (kind) {
        case OsKind::FinnOS:  return "finnos";
        case OsKind::Windows: return "windows";
        case OsKind::MacOS:   return "macos";
        case OsKind::Linux:   return "linux";
        case OsKind::None:
        default:              return "none";
    }
}

std::string_view os_kind_label(OsKind kind) noexcept {
    switch (kind) {
        case OsKind::FinnOS:  return "FinnOS";
        case OsKind::Windows: return "Windows";
        case OsKind::MacOS:   return "macOS";
        case OsKind::Linux:   return "Linux";
        case OsKind::None:
        default:              return "Unknown";
    }
}

std::string_view firmware_mode_id(FirmwareMode fw) noexcept {
    return (fw == FirmwareMode::UEFI) ? "uefi" : "bios";
}

std::string_view firmware_mode_label(FirmwareMode fw) noexcept {
    return (fw == FirmwareMode::UEFI) ? "UEFI" : "Legacy BIOS";
}

std::string_view partition_table_id(PartitionTableKind pt) noexcept {
    switch (pt) {
        case PartitionTableKind::GPT:  return "gpt";
        case PartitionTableKind::MBR:  return "mbr";
        case PartitionTableKind::None:
        default:                       return "none";
    }
}

std::string_view partition_table_label(PartitionTableKind pt) noexcept {
    switch (pt) {
        case PartitionTableKind::GPT:  return "GPT";
        case PartitionTableKind::MBR:  return "MBR";
        case PartitionTableKind::None:
        default:                       return "Unpartitioned";
    }
}

std::string_view esp_action_id(EspAction act) noexcept {
    switch (act) {
        case EspAction::UseExistingAtBoot:         return "share-boot";
        case EspAction::UseExistingAtBootEfi:      return "share-boot-efi";
        case EspAction::FormatExistingEsp:         return "format-esp";
        case EspAction::CreateNewEspFromFreeSpace: return "create-esp";
        case EspAction::SplitTargetForEsp:         return "split-esp";
        case EspAction::CreateNewTableWithEsp:     return "new-table";
        case EspAction::None:
        default:                                   return "none";
    }
}

std::string_view bootloader_strategy_id(BootloaderStrategy strat) noexcept {
    switch (strat) {
        case BootloaderStrategy::SystemdBoot:       return "systemd-boot";
        case BootloaderStrategy::GrubUefi:          return "grub-efi";
        case BootloaderStrategy::GrubBios:          return "grub-bios";
        case BootloaderStrategy::HybridUefiAndBios: return "hybrid";
        default:                                    return "systemd-boot";
    }
}

FirmwareMode detect_firmware_mode(std::string_view sys_efi_path) noexcept {
    std::error_code ec;
    return fs::is_directory(fs::path(sys_efi_path), ec)
        ? FirmwareMode::UEFI
        : FirmwareMode::LegacyBios;
}

std::vector<DiskInfo> parse_lsblk_json(std::string_view json,
                                       std::string_view live_backing_disk) {
    std::vector<DiskInfo> disks;
    JsonParser parser{json, 0};
    JsonVal root;
    if (!parser.parse_value(&root) || root.kind != JsonVal::Kind::Object) return disks;
    const JsonVal* blockdevs = root.field("blockdevices");
    if (!blockdevs || blockdevs->kind != JsonVal::Kind::Array) return disks;

    for (const JsonVal& dev : blockdevs->arr) {
        if (dev.kind != JsonVal::Kind::Object) continue;
        if (dev.str("type") != "disk") continue;
        const std::string name = dev.str("name");
        if (name.empty() || name.rfind("loop", 0) == 0 ||
            name.rfind("zram", 0) == 0 || name.rfind("ram", 0) == 0 ||
            name.rfind("sr", 0) == 0) {
            continue;
        }
        DiskInfo d;
        d.name = name;
        d.path = dev.str("path").empty() ? ("/dev/" + name) : dev.str("path");
        d.model = trim(dev.str("model"));
        d.transport = to_lower(dev.str("tran"));
        d.size_bytes = dev.u64("size");
        d.table_kind = parse_pttype(dev.str("pttype"));
        d.removable = dev.flag("rm");
        d.read_only = dev.flag("ro");
        if (!live_backing_disk.empty() &&
            (d.path == live_backing_disk || d.name == live_backing_disk)) {
            d.is_live_medium = true;
        }

        if (const JsonVal* children = dev.field("children");
            children && children->kind == JsonVal::Kind::Array) {
            // Infer partition table kind if lsblk omitted pttype on disk.
            if (d.table_kind == PartitionTableKind::None) {
                for (const JsonVal& ch : children->arr) {
                    if (ch.kind != JsonVal::Kind::Object) continue;
                    const std::string pt = to_lower(ch.str("parttype"));
                    const std::string child_pttype = to_lower(ch.str("pttype"));
                    if (!child_pttype.empty()) {
                        d.table_kind = parse_pttype(child_pttype);
                        break;
                    }
                    if (pt.size() == 36 && pt.find('-') != std::string::npos) {
                        d.table_kind = PartitionTableKind::GPT;
                        break;
                    }
                    if (pt.rfind("0x", 0) == 0 || pt == "ef" || pt == "83" || pt == "7" || pt == "c") {
                        d.table_kind = PartitionTableKind::MBR;
                        break;
                    }
                }
            }

            for (const JsonVal& ch : children->arr) {
                if (ch.kind != JsonVal::Kind::Object) continue;
                const std::string ctype = ch.str("type");
                if (ctype != "part" && ctype != "crypt") continue;
                PartitionInfo p;
                p.name = ch.str("name");
                p.path = ch.str("path").empty() ? ("/dev/" + p.name) : ch.str("path");
                p.parent_disk = d.path;
                p.part_number = trailing_part_number(p.path);
                p.start_bytes = ch.u64("start") * 512ULL;
                p.size_bytes = ch.u64("size");
                p.fs_avail_bytes = ch.u64("fsavail");
                p.fstype = ch.str("fstype");
                p.label = ch.str("label");
                p.partlabel = ch.str("partlabel");
                p.uuid = ch.str("uuid");
                p.partuuid = ch.str("partuuid");
                p.parttype = to_lower(ch.str("parttype"));
                p.mountpoint = ch.str("mountpoint");
                if (p.mountpoint.rfind("/run/archiso", 0) == 0 ||
                    p.mountpoint == "/run/finn/recovery") {
                    d.is_live_medium = true;
                }
                classify_partition(p, d.table_kind);
                if (p.is_bios_boot) {
                    d.has_bios_boot = true;
                }
                if (p.is_extended) {
                    d.has_extended_part = true;
                    d.extended_part_number = p.part_number;
                }
                if (d.table_kind == PartitionTableKind::MBR &&
                    p.part_number >= 1 && p.part_number <= 4) {
                    ++d.primary_part_count;
                }
                if (p.is_esp && d.esp_partition_path.empty()) {
                    d.esp_partition_path = p.path;
                    d.esp_size_bytes = p.size_bytes;
                    d.esp_free_bytes = p.fs_avail_bytes;
                    const std::string fsl = to_lower(p.fstype);
                    d.esp_formatted = (fsl == "vfat" || fsl == "fat32" || fsl == "fat16");
                }
                d.partitions.push_back(std::move(p));
            }
        }

        // Compute trailing unallocated space when start_bytes are available or
        // when the disk has partitions using substantially less than total size.
        if (d.free_regions.empty() && d.size_bytes > 2ULL * kMiB) {
            bool has_starts = !d.partitions.empty();
            for (const auto& p : d.partitions) {
                if (p.start_bytes == 0) { has_starts = false; break; }
            }
            if (has_starts) {
                std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
                for (const auto& p : d.partitions) {
                    if (p.is_logical) continue; // Covered by parent extended span
                    spans.emplace_back(p.start_bytes, p.start_bytes + p.size_bytes);
                }
                std::sort(spans.begin(), spans.end());
                std::uint64_t cursor = kMiB;
                for (const auto& sp : spans) {
                    if (sp.first > cursor + 4ULL * kMiB) {
                        d.free_regions.push_back(FreeRegion{
                            cursor, sp.first, sp.first - cursor, false});
                    }
                    cursor = std::max(cursor, sp.second);
                }
                const std::uint64_t usable_end = (d.size_bytes > kMiB) ? (d.size_bytes - kMiB) : d.size_bytes;
                if (usable_end > cursor + 4ULL * kMiB) {
                    d.free_regions.push_back(FreeRegion{
                        cursor, usable_end, usable_end - cursor, false});
                }
            }
        }

        disks.push_back(std::move(d));
    }
    return disks;
}

std::vector<FreeRegion> parse_parted_free_regions(std::string_view parted_machine_output) {
    std::vector<FreeRegion> regions;
    auto parse_bytes_token = [](std::string_view tok) -> std::uint64_t {
        while (!tok.empty() && (tok.back() == 'B' || tok.back() == 'b' || tok.back() == ';')) {
            tok.remove_suffix(1);
        }
        if (tok.empty()) return 0;
        return std::strtoull(std::string(tok).c_str(), nullptr, 10);
    };

    std::size_t pos = 0;
    while (pos < parted_machine_output.size()) {
        std::size_t nl = parted_machine_output.find('\n', pos);
        if (nl == std::string_view::npos) nl = parted_machine_output.size();
        std::string_view line = parted_machine_output.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        // Machine output line for free space: `1:1048576B:53687091199B:53686042624B:free;`
        std::vector<std::string_view> cols;
        std::size_t cstart = 0;
        while (cstart <= line.size()) {
            std::size_t colon = line.find(':', cstart);
            if (colon == std::string_view::npos) {
                cols.push_back(line.substr(cstart));
                break;
            }
            cols.push_back(line.substr(cstart, colon - cstart));
            cstart = colon + 1;
        }
        if (cols.size() >= 5) {
            std::string_view kind = cols[4];
            if (!kind.empty() && kind.back() == ';') kind.remove_suffix(1);
            if (kind == "free") {
                const std::uint64_t start_b = parse_bytes_token(cols[1]);
                const std::uint64_t end_b = parse_bytes_token(cols[2]);
                const std::uint64_t size_b = parse_bytes_token(cols[3]);
                if (size_b >= 4ULL * kMiB) {
                    regions.push_back(FreeRegion{start_b, end_b, size_b, false});
                }
            }
        }
    }
    return regions;
}

const FreeRegion* find_best_free_region(const DiskInfo& disk, std::uint64_t min_bytes) noexcept {
    const FreeRegion* best = nullptr;
    for (const auto& r : disk.free_regions) {
        if (r.size_bytes < min_bytes) continue;
        if (!best || r.size_bytes > best->size_bytes) {
            best = &r;
        }
    }
    return best;
}

std::vector<DetectedOs> probe_os_directory(const std::string& mount_root,
                                           const PartitionInfo& part) {
    std::vector<DetectedOs> found;
    std::error_code ec;
    const fs::path root(mount_root);
    if (!fs::is_directory(root, ec)) return found;

    // 1. EFI System Partition inspection (Windows Boot Manager, macOS, Linux)
    const fs::path win_efi = root / "EFI" / "Microsoft" / "Boot" / "bootmgfw.efi";
    if (fs::is_regular_file(win_efi, ec)) {
        DetectedOs os;
        os.kind = OsKind::Windows;
        os.name = "Windows Boot Manager";
        os.detail = "UEFI bootloader on " + part.path;
        os.disk_path = part.parent_disk;
        os.esp_path = part.path;
        os.partition_path = part.path;
        os.efi_loader_path = "\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
        found.push_back(std::move(os));
    }

    const fs::path apple_efi = root / "EFI" / "Apple" / "Boot" / "boot.efi";
    const fs::path mac_core = root / "System" / "Library" / "CoreServices" / "boot.efi";
    if (fs::is_regular_file(apple_efi, ec) || fs::is_regular_file(mac_core, ec)) {
        DetectedOs os;
        os.kind = OsKind::MacOS;
        os.name = "macOS";
        os.detail = "UEFI bootloader on " + part.path;
        os.disk_path = part.parent_disk;
        os.esp_path = part.path;
        os.partition_path = part.path;
        os.efi_loader_path = fs::is_regular_file(mac_core, ec)
            ? "\\System\\Library\\CoreServices\\boot.efi"
            : "\\EFI\\Apple\\Boot\\boot.efi";
        found.push_back(std::move(os));
    }

    // 2. Windows NTFS root check (UEFI & Legacy BIOS)
    if (fs::is_regular_file(root / "Windows" / "System32" / "ntoskrnl.exe", ec) ||
        fs::is_regular_file(root / "Windows" / "System32" / "winload.efi", ec) ||
        fs::is_regular_file(root / "Windows" / "System32" / "winload.exe", ec)) {
        DetectedOs os;
        os.kind = OsKind::Windows;
        os.name = "Microsoft Windows";
        os.detail = "NTFS system volume on " + part.path;
        os.disk_path = part.parent_disk;
        os.partition_path = part.path;
        found.push_back(std::move(os));
    }

    // 3. macOS root volume check
    if (fs::is_regular_file(root / "System" / "Library" / "CoreServices" / "SystemVersion.plist", ec)) {
        DetectedOs os;
        os.kind = OsKind::MacOS;
        os.name = "macOS";
        os.detail = "System volume on " + part.path;
        os.disk_path = part.parent_disk;
        os.partition_path = part.path;
        found.push_back(std::move(os));
    }

    // 4. Linux / FinnOS /etc/os-release inspection
    const fs::path os_rel = root / "etc" / "os-release";
    if (fs::is_regular_file(os_rel, ec)) {
        const std::string id = to_lower(read_os_release_field(os_rel, "ID"));
        const std::string pretty = read_os_release_field(os_rel, "PRETTY_NAME");
        DetectedOs os;
        os.kind = (id == "finnos") ? OsKind::FinnOS : OsKind::Linux;
        os.name = pretty.empty() ? (id == "finnos" ? "FinnOS" : "Linux") : pretty;
        os.detail = part.fstype + " root on " + part.path;
        os.disk_path = part.parent_disk;
        os.partition_path = part.path;
        found.push_back(std::move(os));
    }

    return found;
}

std::vector<DetectedOs> detect_oses(const std::vector<DiskInfo>& disks) {
    std::vector<DetectedOs> out;
    auto push_unique = [&out](DetectedOs cand) {
        for (const auto& existing : out) {
            if (existing.kind == cand.kind &&
                existing.partition_path == cand.partition_path &&
                existing.efi_loader_path == cand.efi_loader_path) {
                return;
            }
        }
        out.push_back(std::move(cand));
    };

    for (const auto& d : disks) {
        if (d.is_live_medium) continue;
        for (const auto& p : d.partitions) {
            bool probed = false;
            if (!p.mountpoint.empty() && p.mountpoint != "/") {
                auto found = probe_os_directory(p.mountpoint, p);
                if (!found.empty()) {
                    probed = true;
                    for (auto& os : found) {
                        if (os.esp_path.empty()) os.esp_path = d.esp_partition_path;
                        push_unique(std::move(os));
                    }
                }
            }
            if (!probed && p.os_kind != OsKind::None) {
                DetectedOs os;
                os.kind = p.os_kind;
                os.name = p.os_name.empty() ? std::string(os_kind_label(p.os_kind)) : p.os_name;
                os.detail = (p.fstype.empty() ? "partition" : p.fstype) + " on " + p.path +
                            " (" + format_bytes(p.size_bytes) + ")";
                os.disk_path = d.path;
                os.partition_path = p.path;
                os.esp_path = d.esp_partition_path;
                push_unique(std::move(os));
            }
        }
    }
    return out;
}

const PartitionInfo* find_best_shrinkable_partition(const DiskInfo& disk) noexcept {
    const PartitionInfo* best = nullptr;
    for (const auto& p : disk.partitions) {
        if (!p.can_shrink) continue;
        if (!best || p.size_bytes > best->size_bytes) {
            best = &p;
        }
    }
    return best;
}

std::string sha512_hex(std::string_view data) {
    Sha512 hasher;
    if (!data.empty()) {
        hasher.update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    }
    return hasher.final_hex();
}

bool sha512_file_hex(const std::string& path, std::string* out_hex, std::string* out_error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (out_error) *out_error = "Cannot open file for SHA-512 verification: " + path;
        return false;
    }
    Sha512 hasher;
    std::array<char, 65536> buf{};
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            hasher.update(reinterpret_cast<const std::uint8_t*>(buf.data()),
                          static_cast<std::size_t>(got));
        }
    }
    if (in.bad()) {
        if (out_error) *out_error = "I/O error reading file for SHA-512 verification: " + path;
        return false;
    }
    if (out_hex) *out_hex = hasher.final_hex();
    return true;
}

OfflineImageInfo discover_offline_image(const std::vector<std::string>& extra_search_roots,
                                        std::string_view arch) {
    OfflineImageInfo info;
    std::error_code ec;
    const std::string arch_s(arch.empty() ? "x86_64" : arch);

    // Build ordered search roots: caller-supplied roots first (for testing/recovery),
    // then standard live ISO and recovery partition mount points.
    std::vector<fs::path> roots;
    for (const auto& r : extra_search_roots) {
        if (!r.empty()) roots.emplace_back(r);
    }
    if (extra_search_roots.empty()) {
        roots = {
            fs::path("/run/archiso/airootfs"),
            fs::path("/run/archiso/bootmnt"),
            fs::path("/run/finn/recovery"),
            fs::path("/recovery"),
            fs::path("/var/lib/finn/recovery"),
            fs::path("/finnos-iso"),
        };
    }

    auto find_kernel_near = [&](const fs::path& base) -> std::string {
        const std::vector<fs::path> cands = {
            base / "finn" / "boot" / arch_s / "vmlinuz-linux",
            base / "finn" / "boot" / arch_s / "vmlinuz-linux-aarch64",
            base / "vmlinuz-linux",
            base / "boot" / "vmlinuz-linux",
            fs::path("/run/archiso/bootmnt/finn/boot") / arch_s / "vmlinuz-linux",
            fs::path("/run/archiso/bootmnt/finn/boot") / arch_s / "vmlinuz-linux-aarch64",
        };
        for (const auto& k : cands) {
            if (fs::is_regular_file(k, ec)) return k.string();
        }
        return {};
    };

    for (const auto& root : roots) {
        if (!fs::exists(root, ec)) continue;

        // 1. Live unpacked squashfs root directory (e.g. /run/archiso/airootfs)
        if (fs::is_directory(root / "usr" / "bin", ec) &&
            fs::is_directory(root / "var" / "lib" / "pacman" / "local", ec)) {
            info.found = true;
            info.kind = OfflineImageKind::LiveSquashfsRoot;
            info.source_path = root.string();
            info.label = "Live Media Image";
            info.kernel_path = find_kernel_near(root.parent_path() / "bootmnt");
            const fs::path sha = root.parent_path() / "bootmnt" / "finn" / arch_s / "airootfs.sha512";
            const fs::path sfs = root.parent_path() / "bootmnt" / "finn" / arch_s / "airootfs.sfs";
            if (fs::is_regular_file(sha, ec) && fs::is_regular_file(sfs, ec)) {
                info.checksum_path = sha.string();
                info.size_bytes = fs::file_size(sfs, ec);
            }
            return info;
        }

        // 2. Squashfs image inside ISO layout (<root>/finn/<arch>/airootfs.sfs)
        const fs::path iso_sfs = root / "finn" / arch_s / "airootfs.sfs";
        if (fs::is_regular_file(iso_sfs, ec)) {
            info.found = true;
            info.kind = OfflineImageKind::SquashfsFile;
            info.source_path = iso_sfs.string();
            info.label = "Installation Media SquashFS";
            info.size_bytes = fs::file_size(iso_sfs, ec);
            info.kernel_path = find_kernel_near(root);
            const fs::path sha = root / "finn" / arch_s / "airootfs.sha512";
            if (fs::is_regular_file(sha, ec)) info.checksum_path = sha.string();
            return info;
        }

        // 3. Dedicated Recovery Partition squashfs (<root>/finnos.sfs or <root>/airootfs.sfs)
        for (const char* name : {"finnos.sfs", "recovery.sfs", "airootfs.sfs"}) {
            const fs::path cand = root / name;
            if (fs::is_regular_file(cand, ec)) {
                info.found = true;
                info.kind = OfflineImageKind::RecoveryPartitionSquashfs;
                info.source_path = cand.string();
                info.label = "Offline Recovery Partition Image";
                info.size_bytes = fs::file_size(cand, ec);
                info.kernel_path = find_kernel_near(root);
                fs::path sha = cand;
                sha.replace_extension(".sha512");
                if (fs::is_regular_file(sha, ec)) info.checksum_path = sha.string();
                return info;
            }
        }

        // 4. Local rootfs tarball (<root>/finnos-rootfs.tar.zst)
        const fs::path tar = root / "finnos-rootfs.tar.zst";
        if (fs::is_regular_file(tar, ec)) {
            info.found = true;
            info.kind = OfflineImageKind::RootfsTarball;
            info.source_path = tar.string();
            info.label = "Offline Recovery Archive";
            info.size_bytes = fs::file_size(tar, ec);
            info.kernel_path = find_kernel_near(root);
            const fs::path sha = root / "finnos-rootfs.tar.zst.sha512";
            if (fs::is_regular_file(sha, ec)) info.checksum_path = sha.string();
            return info;
        }
    }

    info.found = false;
    info.integrity_detail = "No offline FinnOS image found on live media or recovery partition.";
    return info;
}

bool verify_offline_image(OfflineImageInfo& image) {
    image.integrity_checked = true;
    image.integrity_ok = false;
    std::error_code ec;

    if (!image.found || image.source_path.empty()) {
        image.integrity_detail = "No offline FinnOS image is available.";
        return false;
    }

    // If a SHA-512 sidecar exists, cryptographically verify the underlying image file!
    if (!image.checksum_path.empty() && fs::is_regular_file(image.checksum_path, ec)) {
        std::ifstream sf(image.checksum_path);
        std::string line;
        std::getline(sf, line);
        std::istringstream iss(line);
        std::string expected_hex;
        iss >> expected_hex;
        expected_hex = to_lower(trim(expected_hex));
        if (expected_hex.size() != 128) {
            image.integrity_detail = "Malformed SHA-512 checksum file: " + image.checksum_path;
            return false;
        }

        // Determine which concrete archive file the sidecar hashes.
        fs::path file_to_hash(image.source_path);
        if (image.kind == OfflineImageKind::LiveSquashfsRoot) {
            file_to_hash = fs::path(image.checksum_path).parent_path() / "airootfs.sfs";
        }
        std::string actual_hex;
        std::string err;
        if (!sha512_file_hex(file_to_hash.string(), &actual_hex, &err)) {
            image.integrity_detail = err;
            return false;
        }
        if (actual_hex != expected_hex) {
            image.integrity_detail = "SHA-512 mismatch on " + file_to_hash.filename().string() +
                                     " (image is corrupted or tampered).";
            return false;
        }
        image.integrity_ok = true;
        image.integrity_detail = "SHA-512 verified (" + actual_hex.substr(0, 16) + "...)";
        return true;
    }

    // Without a checksum sidecar, verify real structural integrity (never rubber-stamp).
    if (image.kind == OfflineImageKind::LiveSquashfsRoot) {
        const fs::path root(image.source_path);
        if (!fs::is_directory(root / "usr" / "bin", ec) ||
            !fs::is_directory(root / "var" / "lib" / "pacman" / "local", ec)) {
            image.integrity_detail = "Live root image is missing /usr/bin or package database.";
            return false;
        }
        image.integrity_ok = true;
        image.integrity_detail = "Live image structure verified (no .sha512 sidecar present)";
        return true;
    }

    if (image.kind == OfflineImageKind::SquashfsFile ||
        image.kind == OfflineImageKind::RecoveryPartitionSquashfs) {
        // Verify SquashFS magic bytes ("hsqs" little-endian or "sqsh" big-endian).
        std::ifstream f(image.source_path, std::ios::binary);
        char magic[4] = {};
        if (!f || !f.read(magic, 4)) {
            image.integrity_detail = "Cannot read SquashFS header from " + image.source_path;
            return false;
        }
        const bool ok_magic = (std::memcmp(magic, "hsqs", 4) == 0 ||
                               std::memcmp(magic, "sqsh", 4) == 0);
        if (!ok_magic) {
            image.integrity_detail = "Invalid SquashFS header in " + image.source_path;
            return false;
        }
        image.integrity_ok = true;
        image.integrity_detail = "SquashFS header verified (no .sha512 sidecar present)";
        return true;
    }

    if (image.kind == OfflineImageKind::RootfsTarball) {
        // Verify zstd frame magic (0x28 0xB5 0x2F 0xFD).
        std::ifstream f(image.source_path, std::ios::binary);
        unsigned char magic[4] = {};
        if (!f || !f.read(reinterpret_cast<char*>(magic), 4)) {
            image.integrity_detail = "Cannot read zstd archive header from " + image.source_path;
            return false;
        }
        if (!(magic[0] == 0x28 && magic[1] == 0xB5 && magic[2] == 0x2F && magic[3] == 0xFD)) {
            image.integrity_detail = "Invalid zstd archive header in " + image.source_path;
            return false;
        }
        image.integrity_ok = true;
        image.integrity_detail = "zstd archive header verified (no .sha512 sidecar present)";
        return true;
    }

    image.integrity_detail = "Unknown offline image format.";
    return false;
}

InstallPlan plan_install(const InstallRequest& req,
                         const std::vector<DiskInfo>& disks) {
    HostEnvironment default_env;
    default_env.firmware = FirmwareMode::UEFI;
    return plan_install(req, disks, default_env);
}

InstallPlan plan_install(const InstallRequest& req,
                         const std::vector<DiskInfo>& disks,
                         const HostEnvironment& env) {
    InstallPlan plan;
    plan.mode = req.mode;
    plan.firmware = env.firmware;

    if (!req.confirmed) {
        plan.error = "Explicit confirmation is required before modifying storage.";
        return plan;
    }

    // If caller explicitly verified an offline image and it failed integrity, refuse immediately.
    if (req.mode != InstallMode::RepairBootloader &&
        env.image.integrity_checked && !env.image.integrity_ok) {
        plan.error = "Offline OS image integrity verification failed: " + env.image.integrity_detail;
        return plan;
    }

    auto resolve_esp = [&](const DiskInfo& disk) -> const PartitionInfo* {
        if (!req.esp_partition.empty()) {
            const PartitionInfo* ov = find_partition(disks, req.esp_partition);
            if (ov) return ov;
        }
        if (!disk.esp_partition_path.empty()) {
            return find_partition(disks, disk.esp_partition_path);
        }
        // Check if another non-live internal disk already carries an ESP (multi-drive dual-boot).
        for (const auto& other : disks) {
            if (other.is_live_medium || other.path == disk.path) continue;
            if (!other.esp_partition_path.empty()) {
                return find_partition(disks, other.esp_partition_path);
            }
        }
        return nullptr;
    };

    switch (req.mode) {
        case InstallMode::CleanErase:
        case InstallMode::CleanEraseAB: {
            const DiskInfo* disk = find_disk(disks, req.target_disk);
            if (!disk) {
                plan.error = "Select a valid target disk.";
                return plan;
            }
            if (disk->is_live_medium) {
                plan.error = "Cannot erase the active live/recovery medium (" + disk->path + ").";
                return plan;
            }
            if (disk->read_only) {
                plan.error = "Target disk " + disk->path + " is read-only.";
                return plan;
            }
            if (disk->size_bytes > 0 && disk->size_bytes < kMinFinnRootBytes) {
                plan.error = "Target disk is smaller than the 12 GiB minimum for FinnOS.";
                return plan;
            }
            if (req.mode == InstallMode::CleanEraseAB &&
                disk->size_bytes > 0 && disk->size_bytes < kMinAbDiskBytes) {
                plan.error = "A/B OTA slots require at least 14 GiB of storage.";
                return plan;
            }

            // A/B OTA always uses GPT (PARTLABEL=FINNOS_A/B); CleanErase respects
            // GPT (default, boots BOTH UEFI and Legacy BIOS) or explicit MBR.
            const PartitionTableKind table =
                (req.mode == InstallMode::CleanEraseAB)
                    ? PartitionTableKind::GPT
                    : (req.preferred_table == PartitionTableKind::MBR
                           ? PartitionTableKind::MBR
                           : PartitionTableKind::GPT);

            const bool make_recovery =
                req.create_recovery_partition &&
                (disk->size_bytes == 0 || disk->size_bytes >= kMinDiskForRecoveryPartBytes);

            plan.valid = true;
            plan.target_table = table;
            plan.esp_action = (table == PartitionTableKind::GPT || env.firmware == FirmwareMode::UEFI)
                ? EspAction::CreateNewTableWithEsp
                : EspAction::None;
            plan.bootloader = (table == PartitionTableKind::GPT)
                ? BootloaderStrategy::HybridUefiAndBios
                : (env.firmware == FirmwareMode::UEFI
                       ? BootloaderStrategy::GrubUefi
                       : BootloaderStrategy::GrubBios);
            plan.need_bios_boot = (table == PartitionTableKind::GPT);
            plan.create_recovery_partition = make_recovery;

            if (req.mode == InstallMode::CleanEraseAB) {
                plan.summary = "Erase " + disk->path +
                               " and install FinnOS offline with GPT A/B OTA slots (FINNOS_A / FINNOS_B / FINNOS_DATA).";
            } else if (table == PartitionTableKind::MBR) {
                plan.summary = "Erase " + disk->path +
                               " and install FinnOS offline with MBR partition table (" +
                               std::string(firmware_mode_label(env.firmware)) + ").";
            } else {
                plan.summary = "Erase " + disk->path +
                               " and install FinnOS offline (GPT: 512 MiB FAT32 ESP + 1 MiB BIOS Boot + ext4 root).";
            }

            plan.warnings.push_back("All existing partitions and data on " + disk->path + " will be permanently erased.");
            plan.warnings.push_back("Language, Wi-Fi, user account, hostname, and appearance will be configured on first boot in OOBE.");

            if (table == PartitionTableKind::GPT) {
                plan.steps.push_back("Create clean GPT partition table on " + disk->path);
                plan.steps.push_back(req.mode == InstallMode::CleanEraseAB
                    ? "Format 1024 MiB FAT32 ESP + 1 MiB BIOS Boot + FINNOS_A, FINNOS_B, FINNOS_DATA ext4 volumes"
                    : "Format 512 MiB FAT32 ESP + 1 MiB BIOS Boot + ext4 root volume");
            } else {
                plan.steps.push_back("Create clean MBR partition table on " + disk->path);
                plan.steps.push_back(env.firmware == FirmwareMode::UEFI
                    ? "Format 512 MiB FAT32 EFI partition (0xef) + ext4 root partition"
                    : "Format bootable ext4 root partition");
            }
            if (make_recovery) {
                plan.steps.push_back("Create FINNOS_RECOVERY partition and copy verified offline OS image");
            }
            plan.steps.push_back("Deploy FinnOS directly from local offline image (zero internet required)");
            plan.steps.push_back(table == PartitionTableKind::GPT
                ? "Install FinnOS UEFI (finn-boot / systemd-boot) + BIOS (GRUB) bootloaders and mark first-boot OOBE pending"
                : (env.firmware == FirmwareMode::UEFI
                       ? "Install GRUB UEFI bootloader on MBR ESP and mark first-boot OOBE pending"
                       : "Install GRUB BIOS bootloader on MBR and mark first-boot OOBE pending"));

            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "clean",
                "--table", std::string(partition_table_id(table)),
            };
            if (req.mode == InstallMode::CleanEraseAB) {
                plan.finn_install_args.push_back("--ab-slots");
            }
            if (make_recovery) {
                plan.finn_install_args.push_back("--create-recovery-part");
            }
            attach_offline_image_args(plan, req, env);
            plan.finn_install_args.push_back("--yes");
            return plan;
        }

        case InstallMode::DualBootFreeSpace: {
            const DiskInfo* disk = find_disk(disks, req.target_disk);
            if (!disk) {
                plan.error = "Select a valid target disk with unallocated free space.";
                return plan;
            }
            if (disk->is_live_medium) {
                plan.error = "Cannot partition the active live/recovery medium.";
                return plan;
            }
            if (disk->read_only) {
                plan.error = "Target disk " + disk->path + " is read-only.";
                return plan;
            }

            std::uint64_t start_mb = req.free_start_mb;
            std::uint64_t size_mb = req.free_size_mb;
            bool inside_ext = false;
            if (size_mb == 0) {
                const FreeRegion* best = find_best_free_region(*disk, kMinFinnRootBytes);
                if (!best) {
                    plan.error = "No unallocated free space of at least 12 GiB found on " + disk->path + ".";
                    return plan;
                }
                start_mb = (best->start_bytes + kMiB - 1) / kMiB;
                size_mb = best->size_bytes / kMiB;
                inside_ext = best->inside_extended;
            }

            const PartitionTableKind table =
                (disk->table_kind == PartitionTableKind::None)
                    ? PartitionTableKind::GPT
                    : disk->table_kind;

            const PartitionInfo* esp = resolve_esp(*disk);
            EspAction esp_act = EspAction::None;
            BootloaderStrategy strat = BootloaderStrategy::GrubBios;
            bool need_bios_boot = false;
            int parts_needed = 1; // ext4 root

            if (env.firmware == FirmwareMode::UEFI) {
                if (esp) {
                    const std::string esp_fs = to_lower(esp->fstype);
                    const bool esp_fmt = (esp_fs == "vfat" || esp_fs == "fat32" || esp_fs == "fat16");
                    if (!esp_fmt) {
                        esp_act = EspAction::FormatExistingEsp;
                        strat = (table == PartitionTableKind::GPT && esp_is_spacious_for_boot_mount(*esp))
                            ? BootloaderStrategy::SystemdBoot
                            : BootloaderStrategy::GrubUefi;
                        plan.auto_fixes.push_back("Existing EFI partition " + esp->path +
                                                  " is unformatted — will format as FAT32 automatically.");
                    } else if (table == PartitionTableKind::GPT && esp_is_spacious_for_boot_mount(*esp)) {
                        esp_act = EspAction::UseExistingAtBoot;
                        strat = BootloaderStrategy::SystemdBoot;
                    } else {
                        esp_act = EspAction::UseExistingAtBootEfi;
                        strat = BootloaderStrategy::GrubUefi;
                        plan.auto_fixes.push_back("Existing EFI partition " + esp->path + " (" +
                                                  format_bytes(esp->size_bytes) +
                                                  ") will be mounted at /boot/efi with GRUB UEFI so it never runs out of space.");
                    }
                } else {
                    esp_act = EspAction::CreateNewEspFromFreeSpace;
                    strat = (table == PartitionTableKind::GPT)
                        ? BootloaderStrategy::SystemdBoot
                        : BootloaderStrategy::GrubUefi;
                    ++parts_needed;
                    plan.auto_fixes.push_back("No EFI System Partition found — Recovery Mode will automatically create a 512 MiB FAT32 EFI partition.");
                }
            } else {
                // Legacy BIOS
                strat = BootloaderStrategy::GrubBios;
                if (table == PartitionTableKind::GPT && !disk->has_bios_boot) {
                    need_bios_boot = true;
                    ++parts_needed;
                    plan.auto_fixes.push_back("GPT disk has no BIOS Boot partition — Recovery Mode will automatically create a 1 MiB BIOS Boot (ef02) partition.");
                }
            }

            const std::uint64_t required_mb =
                (kMinFinnRootBytes / kMiB) +
                (esp_act == EspAction::CreateNewEspFromFreeSpace ? 512ULL : 0ULL) +
                (need_bios_boot ? 2ULL : 0ULL);
            if (size_mb < required_mb) {
                plan.error = "Unallocated space (" + format_bytes(size_mb * kMiB) +
                             ") is smaller than the " + format_bytes(required_mb * kMiB) + " required.";
                return plan;
            }

            std::string mbr_err;
            if (!mbr_can_add_partitions(*disk, parts_needed, inside_ext, &mbr_err)) {
                plan.error = mbr_err;
                return plan;
            }

            plan.valid = true;
            plan.target_table = table;
            plan.esp_action = esp_act;
            plan.bootloader = strat;
            plan.need_bios_boot = need_bios_boot;
            plan.summary = "Install FinnOS offline into " + format_bytes(size_mb * kMiB) +
                           " of unallocated space on " + disk->path + " alongside existing OSes.";
            plan.warnings.push_back("All existing OS partitions (Windows, macOS, Linux) remain untouched.");
            plan.warnings.push_back("Personal setup (user account, language, Wi-Fi) runs on first boot in OOBE.");

            if (esp_act == EspAction::CreateNewEspFromFreeSpace) {
                plan.steps.push_back("Create 512 MiB FAT32 EFI System Partition in unallocated space on " + disk->path);
            }
            if (need_bios_boot) {
                plan.steps.push_back("Create 1 MiB BIOS Boot (ef02) partition on GPT disk " + disk->path);
            }
            plan.steps.push_back("Create and format ext4 FinnOS root partition in unallocated space on " + disk->path);
            if (esp && (esp_act == EspAction::UseExistingAtBoot || esp_act == EspAction::UseExistingAtBootEfi)) {
                plan.steps.push_back(esp_act == EspAction::UseExistingAtBoot
                    ? ("Share existing EFI partition " + esp->path + " at /mnt/boot without formatting")
                    : ("Share existing EFI partition " + esp->path + " at /mnt/boot/efi without formatting"));
            }
            plan.steps.push_back("Deploy FinnOS offline from local recovery/live image");
            plan.steps.push_back("Install dual-boot bootloader preserving Windows, macOS, and Linux entries");

            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "dualboot-free",
                "--free-start-mb", std::to_string(start_mb),
                "--free-end-mb", std::to_string(start_mb + size_mb),
                "--esp-action", std::string(esp_action_id(esp_act)),
            };
            if (esp && esp_act != EspAction::CreateNewEspFromFreeSpace && esp_act != EspAction::None) {
                plan.finn_install_args.push_back("--esp-part");
                plan.finn_install_args.push_back(esp->path);
            }
            if (need_bios_boot) {
                plan.finn_install_args.push_back("--need-bios-boot");
            }
            attach_offline_image_args(plan, req, env);
            plan.finn_install_args.push_back("--yes");
            return plan;
        }

        case InstallMode::DualBootResize: {
            const DiskInfo* disk = nullptr;
            const PartitionInfo* part = find_partition(disks, req.resize_partition, &disk);
            if (!part || !disk) {
                plan.error = "Select an existing partition to shrink for dual-boot.";
                return plan;
            }
            if (disk->is_live_medium) {
                plan.error = "Cannot partition the active live/recovery medium.";
                return plan;
            }
            if (disk->read_only) {
                plan.error = "Target disk " + disk->path + " is read-only.";
                return plan;
            }
            if (!part->can_shrink) {
                plan.error = part->shrink_reason.empty()
                    ? ("Partition " + part->path + " cannot be shrunk safely.")
                    : part->shrink_reason;
                return plan;
            }

            const PartitionTableKind table =
                (disk->table_kind == PartitionTableKind::None)
                    ? PartitionTableKind::GPT
                    : disk->table_kind;

            const PartitionInfo* esp = resolve_esp(*disk);
            EspAction esp_act = EspAction::None;
            BootloaderStrategy strat = BootloaderStrategy::GrubBios;
            bool need_bios_boot = false;
            int parts_needed = 1; // New FinnOS root partition

            if (env.firmware == FirmwareMode::UEFI) {
                if (esp) {
                    const std::string esp_fs = to_lower(esp->fstype);
                    const bool esp_fmt = (esp_fs == "vfat" || esp_fs == "fat32" || esp_fs == "fat16");
                    if (!esp_fmt) {
                        esp_act = EspAction::FormatExistingEsp;
                        strat = (table == PartitionTableKind::GPT && esp_is_spacious_for_boot_mount(*esp))
                            ? BootloaderStrategy::SystemdBoot
                            : BootloaderStrategy::GrubUefi;
                        plan.auto_fixes.push_back("Existing EFI partition " + esp->path +
                                                  " is unformatted — will format as FAT32 automatically.");
                    } else if (table == PartitionTableKind::GPT && esp_is_spacious_for_boot_mount(*esp)) {
                        esp_act = EspAction::UseExistingAtBoot;
                        strat = BootloaderStrategy::SystemdBoot;
                    } else {
                        esp_act = EspAction::UseExistingAtBootEfi;
                        strat = BootloaderStrategy::GrubUefi;
                        plan.auto_fixes.push_back("Existing EFI partition " + esp->path + " (" +
                                                  format_bytes(esp->size_bytes) +
                                                  ") will be mounted at /boot/efi with GRUB UEFI to avoid filling a small Windows/OEM ESP.");
                    }
                } else {
                    // Automatically carve a 512 MiB ESP from the freed dual-boot space on UEFI!
                    // On legacy MBR disk with NO ESP and no explicit UEFI requirement, allow BIOS GRUB
                    // ONLY when firmware is LegacyBios; when firmware is UEFI, auto-create ESP!
                    esp_act = EspAction::CreateNewEspFromFreeSpace;
                    strat = (table == PartitionTableKind::GPT)
                        ? BootloaderStrategy::SystemdBoot
                        : BootloaderStrategy::GrubUefi;
                    ++parts_needed;
                    plan.auto_fixes.push_back("No EFI System Partition detected on UEFI system — Recovery Mode will automatically generate a 512 MiB FAT32 EFI partition.");
                }
            } else {
                strat = BootloaderStrategy::GrubBios;
                if (table == PartitionTableKind::GPT && !disk->has_bios_boot) {
                    need_bios_boot = true;
                    ++parts_needed;
                    plan.auto_fixes.push_back("GPT disk has no BIOS Boot partition — Recovery Mode will automatically generate a 1 MiB BIOS Boot (ef02) partition.");
                }
            }

            const std::uint64_t new_bytes = req.resize_new_size_mb * kMiB;
            if (new_bytes < part->min_size_bytes) {
                plan.error = "Shrunk size must be at least " + format_bytes(part->min_size_bytes) + ".";
                return plan;
            }
            const std::uint64_t extra_bytes =
                (esp_act == EspAction::CreateNewEspFromFreeSpace ? kAutoEspBytes : 0ULL) +
                (need_bios_boot ? 2ULL * kMiB : 0ULL);
            const std::uint64_t min_freed = kMinFinnRootBytes + extra_bytes;
            if (new_bytes >= part->size_bytes || (part->size_bytes - new_bytes) < min_freed) {
                plan.error = "Shrinking must free at least " + format_bytes(min_freed) + " for FinnOS.";
                return plan;
            }

            std::string mbr_err;
            if (!mbr_can_add_partitions(*disk, parts_needed, part->is_logical, &mbr_err)) {
                plan.error = mbr_err;
                return plan;
            }

            plan.valid = true;
            plan.target_table = table;
            plan.esp_action = esp_act;
            plan.bootloader = strat;
            plan.need_bios_boot = need_bios_boot;
            plan.summary = "Shrink " + part->path + " (" + part->fstype + ") to " +
                           format_bytes(new_bytes) + ", install FinnOS offline into " +
                           format_bytes(part->size_bytes - new_bytes) + " freed space" +
                           ((env.firmware == FirmwareMode::UEFI && esp)
                                ? (", preserving ESP " + esp->path + ".")
                                : (esp_act == EspAction::CreateNewEspFromFreeSpace
                                       ? ", and auto-create a 512 MiB FAT32 EFI partition."
                                       : " (BIOS/MBR bootloader)."));
            if (env.firmware == FirmwareMode::UEFI && esp && esp_act != EspAction::FormatExistingEsp) {
                plan.warnings.push_back("Existing EFI bootloaders on " + esp->path +
                                        " (Windows/macOS/Linux) will be preserved untouched.");
            }
            plan.warnings.push_back("Any trailing recovery partition (e.g. Windows Recovery) will be preserved untouched.");
            plan.warnings.push_back("User account, hostname, Wi-Fi, and preferences will be set on first normal boot in OOBE.");

            plan.steps.push_back("Verify & shrink " + part->path + " (" + part->fstype + ") to " +
                                 std::to_string(req.resize_new_size_mb) + " MiB");
            if (esp_act == EspAction::CreateNewEspFromFreeSpace) {
                plan.steps.push_back("Automatically create 512 MiB FAT32 EFI System Partition in freed space");
            }
            if (need_bios_boot) {
                plan.steps.push_back("Automatically create 1 MiB BIOS Boot (ef02) partition on " + disk->path);
            }
            plan.steps.push_back("Create new ext4 FinnOS root partition in the freed space on " + disk->path);
            if (esp && esp_act == EspAction::UseExistingAtBoot) {
                plan.steps.push_back("Mount existing ESP " + esp->path + " at /mnt/boot without formatting");
            } else if (esp && esp_act == EspAction::UseExistingAtBootEfi) {
                plan.steps.push_back("Mount existing ESP " + esp->path + " at /mnt/boot/efi without formatting (GRUB UEFI)");
            } else if (esp && esp_act == EspAction::FormatExistingEsp) {
                plan.steps.push_back("Format uninitialized ESP " + esp->path + " as FAT32");
            } else if (env.firmware == FirmwareMode::LegacyBios) {
                plan.steps.push_back("Install BIOS/MBR GRUB bootloader on " + disk->path);
            }
            plan.steps.push_back("Deploy FinnOS offline from local recovery/live media and register dual-boot OSes");

            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "dualboot-resize",
                "--resize-part", part->path,
                "--resize-mb", std::to_string(req.resize_new_size_mb),
                "--esp-action", std::string(esp_action_id(esp_act)),
            };
            if (esp && esp_act != EspAction::CreateNewEspFromFreeSpace && esp_act != EspAction::None) {
                plan.finn_install_args.push_back("--esp-part");
                plan.finn_install_args.push_back(esp->path);
            }
            if (need_bios_boot) {
                plan.finn_install_args.push_back("--need-bios-boot");
            }
            attach_offline_image_args(plan, req, env);
            plan.finn_install_args.push_back("--yes");
            return plan;
        }

        case InstallMode::TargetPartition: {
            const DiskInfo* disk = nullptr;
            const PartitionInfo* part = find_partition(disks, req.target_partition, &disk);
            if (!part || !disk) {
                plan.error = "Select a target partition for FinnOS.";
                return plan;
            }
            if (disk->is_live_medium) {
                plan.error = "Cannot install onto the active live/recovery medium.";
                return plan;
            }
            if (disk->read_only) {
                plan.error = "Target disk " + disk->path + " is read-only.";
                return plan;
            }
            if (part->is_esp || part->is_bios_boot || part->is_extended) {
                plan.error = "Cannot install FinnOS root onto an EFI, BIOS Boot, or extended container partition.";
                return plan;
            }

            const PartitionTableKind table =
                (disk->table_kind == PartitionTableKind::None)
                    ? PartitionTableKind::GPT
                    : disk->table_kind;

            const PartitionInfo* esp = resolve_esp(*disk);
            EspAction esp_act = EspAction::None;
            BootloaderStrategy strat = BootloaderStrategy::GrubBios;
            bool need_bios_boot = false;
            int extra_parts = 0;

            if (env.firmware == FirmwareMode::UEFI) {
                if (esp) {
                    const std::string esp_fs = to_lower(esp->fstype);
                    const bool esp_fmt = (esp_fs == "vfat" || esp_fs == "fat32" || esp_fs == "fat16");
                    if (!esp_fmt) {
                        esp_act = EspAction::FormatExistingEsp;
                        strat = (table == PartitionTableKind::GPT && esp_is_spacious_for_boot_mount(*esp))
                            ? BootloaderStrategy::SystemdBoot
                            : BootloaderStrategy::GrubUefi;
                        plan.auto_fixes.push_back("Existing EFI partition " + esp->path +
                                                  " is unformatted — will format as FAT32 automatically.");
                    } else if (table == PartitionTableKind::GPT && esp_is_spacious_for_boot_mount(*esp)) {
                        esp_act = EspAction::UseExistingAtBoot;
                        strat = BootloaderStrategy::SystemdBoot;
                    } else {
                        esp_act = EspAction::UseExistingAtBootEfi;
                        strat = BootloaderStrategy::GrubUefi;
                        plan.auto_fixes.push_back("Existing EFI partition " + esp->path + " (" +
                                                  format_bytes(esp->size_bytes) +
                                                  ") will be mounted at /boot/efi with GRUB UEFI.");
                    }
                } else {
                    // Check if there's >= 512 MiB unallocated free space on the disk first!
                    const FreeRegion* free_esp = find_best_free_region(*disk, kAutoEspBytes + 4ULL * kMiB);
                    if (free_esp) {
                        esp_act = EspAction::CreateNewEspFromFreeSpace;
                        plan.auto_fixes.push_back("No EFI System Partition found — Recovery Mode will automatically create a 512 MiB FAT32 EFI partition in free space.");
                    } else {
                        esp_act = EspAction::SplitTargetForEsp;
                        plan.auto_fixes.push_back("No EFI System Partition found — Recovery Mode will automatically carve a 512 MiB FAT32 EFI partition from " + part->path + ".");
                    }
                    strat = (table == PartitionTableKind::GPT)
                        ? BootloaderStrategy::SystemdBoot
                        : BootloaderStrategy::GrubUefi;
                    ++extra_parts;
                }
            } else {
                strat = BootloaderStrategy::GrubBios;
                if (table == PartitionTableKind::GPT && !disk->has_bios_boot) {
                    need_bios_boot = true;
                    ++extra_parts;
                    plan.auto_fixes.push_back("GPT disk has no BIOS Boot partition — Recovery Mode will automatically carve a 1 MiB BIOS Boot (ef02) partition.");
                }
            }

            const std::uint64_t min_part_bytes =
                kMinFinnRootBytes +
                (esp_act == EspAction::SplitTargetForEsp ? kAutoEspBytes : 0ULL) +
                (need_bios_boot ? 2ULL * kMiB : 0ULL);
            if (part->size_bytes > 0 && part->size_bytes < min_part_bytes) {
                plan.error = "Selected partition is smaller than the " +
                             format_bytes(min_part_bytes) + " minimum required.";
                return plan;
            }

            std::string mbr_err;
            if (extra_parts > 0 && !mbr_can_add_partitions(*disk, extra_parts, part->is_logical, &mbr_err)) {
                plan.error = mbr_err;
                return plan;
            }

            plan.valid = true;
            plan.target_table = table;
            plan.esp_action = esp_act;
            plan.bootloader = strat;
            plan.need_bios_boot = need_bios_boot;
            plan.summary = "Format " + part->path + " as ext4, install FinnOS offline" +
                           (esp ? (", sharing ESP " + esp->path + ".")
                                : (esp_act == EspAction::SplitTargetForEsp || esp_act == EspAction::CreateNewEspFromFreeSpace
                                       ? ", and auto-generate a 512 MiB FAT32 EFI partition."
                                       : " (BIOS/MBR bootloader)."));
            plan.warnings.push_back("Data on " + part->path + " will be erased; other partitions are preserved.");

            if (esp_act == EspAction::SplitTargetForEsp) {
                plan.steps.push_back("Automatically split " + part->path + " into 512 MiB FAT32 ESP + ext4 FinnOS root");
            } else if (esp_act == EspAction::CreateNewEspFromFreeSpace) {
                plan.steps.push_back("Automatically create 512 MiB FAT32 ESP in unallocated space on " + disk->path);
                plan.steps.push_back("Format " + part->path + " as ext4 (label finnos)");
            } else {
                plan.steps.push_back("Format " + part->path + " as ext4 (label finnos)");
            }
            if (need_bios_boot) {
                plan.steps.push_back("Automatically create 1 MiB BIOS Boot (ef02) partition on " + disk->path);
            }
            if (esp && esp_act == EspAction::UseExistingAtBoot) {
                plan.steps.push_back("Mount existing ESP " + esp->path + " at /mnt/boot without formatting");
            } else if (esp && esp_act == EspAction::UseExistingAtBootEfi) {
                plan.steps.push_back("Mount existing ESP " + esp->path + " at /mnt/boot/efi without formatting");
            }
            plan.steps.push_back("Deploy FinnOS offline from local recovery/live image");
            plan.steps.push_back("Register FinnOS + existing OSes in bootloader");

            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "partition",
                "--root-part", part->path,
                "--esp-action", std::string(esp_action_id(esp_act)),
            };
            if (esp && esp_act != EspAction::CreateNewEspFromFreeSpace &&
                esp_act != EspAction::SplitTargetForEsp && esp_act != EspAction::None) {
                plan.finn_install_args.push_back("--esp-part");
                plan.finn_install_args.push_back(esp->path);
            }
            if (need_bios_boot) {
                plan.finn_install_args.push_back("--need-bios-boot");
            }
            attach_offline_image_args(plan, req, env);
            plan.finn_install_args.push_back("--yes");
            return plan;
        }

        case InstallMode::ReinstallPreserveData: {
            const DiskInfo* disk = nullptr;
            const PartitionInfo* part = find_partition(disks, req.target_partition, &disk);
            if (!part || !disk) {
                plan.error = "Select an existing FinnOS root partition to reinstall.";
                return plan;
            }
            if (disk->is_live_medium) {
                plan.error = "Cannot reinstall onto the active live/recovery medium.";
                return plan;
            }
            const std::string fsl = to_lower(part->fstype);
            if (fsl != "ext4" && fsl != "btrfs") {
                plan.error = "Reinstall requires an existing ext4 or btrfs FinnOS root partition.";
                return plan;
            }
            const PartitionInfo* esp = resolve_esp(*disk);
            EspAction esp_act = EspAction::None;
            BootloaderStrategy strat = BootloaderStrategy::GrubBios;
            if (env.firmware == FirmwareMode::UEFI && esp) {
                const std::string esp_fs = to_lower(esp->fstype);
                const bool esp_fmt = (esp_fs == "vfat" || esp_fs == "fat32" || esp_fs == "fat16");
                if (!esp_fmt) {
                    esp_act = EspAction::FormatExistingEsp;
                    strat = BootloaderStrategy::GrubUefi;
                    plan.auto_fixes.push_back("ESP " + esp->path + " has no FAT32 filesystem — will repair/format it.");
                } else if (disk->table_kind != PartitionTableKind::MBR && esp_is_spacious_for_boot_mount(*esp)) {
                    esp_act = EspAction::UseExistingAtBoot;
                    strat = BootloaderStrategy::SystemdBoot;
                } else {
                    esp_act = EspAction::UseExistingAtBootEfi;
                    strat = BootloaderStrategy::GrubUefi;
                }
            } else if (env.firmware == FirmwareMode::UEFI && !esp) {
                const FreeRegion* free_esp = find_best_free_region(*disk, kAutoEspBytes + 4ULL * kMiB);
                if (free_esp) {
                    esp_act = EspAction::CreateNewEspFromFreeSpace;
                    strat = BootloaderStrategy::SystemdBoot;
                    plan.auto_fixes.push_back("Missing EFI partition — will automatically create a 512 MiB FAT32 ESP in free space.");
                }
            }

            plan.valid = true;
            plan.target_table = disk->table_kind;
            plan.esp_action = esp_act;
            plan.bootloader = strat;
            plan.summary = "Reinstall FinnOS offline on " + part->path +
                           " while preserving /home, accounts, hostname, and Wi-Fi profiles.";
            plan.steps = {
                esp ? ("Mount " + part->path + " and ESP " + esp->path + " without formatting root")
                    : ("Mount " + part->path + " without formatting"),
                "Preserve /home, user accounts/passwords, /etc/hostname, OOBE completion, and Wi-Fi profiles",
                "Refresh FinnOS kernel, initramfs, and userland directly from local offline image",
                "Verify & refresh dual-boot bootloader entries",
            };
            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "reinstall",
                "--root-part", part->path,
                "--esp-action", std::string(esp_action_id(esp_act)),
            };
            if (esp && esp_act != EspAction::CreateNewEspFromFreeSpace && esp_act != EspAction::None) {
                plan.finn_install_args.push_back("--esp-part");
                plan.finn_install_args.push_back(esp->path);
            }
            attach_offline_image_args(plan, req, env);
            plan.finn_install_args.push_back("--yes");
            return plan;
        }

        case InstallMode::RepairBootloader: {
            const DiskInfo* disk = nullptr;
            const PartitionInfo* part = find_partition(disks, req.target_partition, &disk);
            if (!part || !disk) {
                plan.error = "Select the FinnOS root partition whose bootloader should be repaired.";
                return plan;
            }
            if (disk->is_live_medium) {
                plan.error = "Cannot repair bootloader on the active live/recovery medium.";
                return plan;
            }
            const PartitionInfo* esp = resolve_esp(*disk);
            EspAction esp_act = EspAction::None;
            BootloaderStrategy strat = BootloaderStrategy::GrubBios;
            bool need_bios_boot = false;

            if (env.firmware == FirmwareMode::UEFI) {
                if (esp) {
                    const std::string esp_fs = to_lower(esp->fstype);
                    const bool esp_fmt = (esp_fs == "vfat" || esp_fs == "fat32" || esp_fs == "fat16");
                    if (!esp_fmt) {
                        esp_act = EspAction::FormatExistingEsp;
                        strat = (disk->table_kind != PartitionTableKind::MBR && esp_is_spacious_for_boot_mount(*esp))
                            ? BootloaderStrategy::SystemdBoot
                            : BootloaderStrategy::GrubUefi;
                        plan.auto_fixes.push_back("ESP " + esp->path + " was corrupted/unformatted — will format as FAT32 and reinstall bootloader.");
                    } else if (disk->table_kind != PartitionTableKind::MBR && esp_is_spacious_for_boot_mount(*esp)) {
                        esp_act = EspAction::UseExistingAtBoot;
                        strat = BootloaderStrategy::SystemdBoot;
                    } else {
                        esp_act = EspAction::UseExistingAtBootEfi;
                        strat = BootloaderStrategy::GrubUefi;
                    }
                } else {
                    const FreeRegion* free_esp = find_best_free_region(*disk, kAutoEspBytes + 4ULL * kMiB);
                    if (free_esp) {
                        esp_act = EspAction::CreateNewEspFromFreeSpace;
                        strat = BootloaderStrategy::SystemdBoot;
                        plan.auto_fixes.push_back("No EFI partition existed — Recovery Mode will automatically create a 512 MiB FAT32 ESP in free space.");
                    }
                }
            } else {
                strat = BootloaderStrategy::GrubBios;
                if (disk->table_kind == PartitionTableKind::GPT && !disk->has_bios_boot) {
                    need_bios_boot = true;
                    plan.auto_fixes.push_back("GPT disk lacks a BIOS Boot partition — will create one if free space exists.");
                }
            }

            plan.valid = true;
            plan.target_table = disk->table_kind;
            plan.esp_action = esp_act;
            plan.bootloader = strat;
            plan.need_bios_boot = need_bios_boot;
            plan.summary = (env.firmware == FirmwareMode::LegacyBios || (!esp && esp_act == EspAction::None))
                ? ("Repair BIOS/MBR GRUB bootloader on " + disk->path + " and rebuild initramfs for " + part->path + ".")
                : ("Repair UEFI bootloader (" + std::string(bootloader_strategy_id(strat)) + ") on " +
                   (esp ? esp->path : disk->path) + " and rebuild initramfs for " + part->path + ".");
            plan.steps = {
                esp ? ("Mount " + part->path + " at /mnt and ESP " + esp->path +
                       (esp_act == EspAction::UseExistingAtBootEfi ? " at /mnt/boot/efi" : " at /mnt/boot"))
                    : ("Mount " + part->path + " at /mnt"),
                (env.firmware == FirmwareMode::LegacyBios || (!esp && esp_act == EspAction::None))
                    ? ("Reinstall GRUB BIOS bootloader on " + disk->path)
                    : ("Reinstall FinnOS UEFI bootloader (" + std::string(bootloader_strategy_id(strat)) + ")"),
                "Regenerate boot entries preserving Windows, macOS, and Linux dual-boot OSes",
                "Rebuild initramfs (mkinitcpio -P)",
            };
            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "repair-boot",
                "--root-part", part->path,
                "--esp-action", std::string(esp_action_id(esp_act)),
            };
            if (esp && esp_act != EspAction::CreateNewEspFromFreeSpace && esp_act != EspAction::None) {
                plan.finn_install_args.push_back("--esp-part");
                plan.finn_install_args.push_back(esp->path);
            }
            if (need_bios_boot) {
                plan.finn_install_args.push_back("--need-bios-boot");
            }
            plan.finn_install_args.push_back("--firmware");
            plan.finn_install_args.push_back(std::string(firmware_mode_id(env.firmware)));
            plan.finn_install_args.push_back("--yes");
            return plan;
        }
    }

    plan.error = "Unsupported install mode.";
    return plan;
}

DiskOpPlan plan_disk_op(DiskOpKind op,
                        const PartitionInfo& part,
                        std::uint64_t new_size_mb,
                        const std::string& new_label) {
    DiskOpPlan plan;
    if (part.path.empty()) {
        plan.error = "No partition selected.";
        return plan;
    }
    const std::string fs_l = to_lower(part.fstype);

    switch (op) {
        case DiskOpKind::FirstAidCheck:
        case DiskOpKind::FirstAidRepair: {
            const bool repair = (op == DiskOpKind::FirstAidRepair);
            if (repair && !part.mountpoint.empty()) {
                plan.error = "Unmount " + part.path + " before running filesystem repair.";
                return plan;
            }
            if (fs_l == "ext4" || fs_l == "ext3" || fs_l == "ext2") {
                plan.valid = true;
                plan.program = "e2fsck";
                plan.args = {repair ? "-f" : "-fn", repair ? "-p" : part.path};
                if (repair) plan.args.push_back(part.path);
                plan.summary = (repair ? "Repair ext filesystem on " : "Verify ext filesystem on ") + part.path;
            } else if (fs_l == "vfat" || fs_l == "fat32") {
                plan.valid = true;
                plan.program = "fsck.fat";
                plan.args = {repair ? "-a" : "-n", part.path};
                plan.summary = (repair ? "Repair FAT32 volume " : "Verify FAT32 volume ") + part.path;
            } else if (fs_l == "ntfs") {
                plan.valid = true;
                plan.program = "ntfsfix";
                plan.args = repair ? std::vector<std::string>{part.path}
                                   : std::vector<std::string>{"--no-action", part.path};
                plan.summary = (repair ? "Repair NTFS volume " : "Inspect NTFS volume ") + part.path;
            } else if (fs_l == "btrfs") {
                plan.valid = true;
                plan.program = "btrfs";
                plan.args = repair ? std::vector<std::string>{"check", "--repair", part.path}
                                   : std::vector<std::string>{"check", "--readonly", part.path};
                plan.summary = (repair ? "Repair btrfs volume " : "Verify btrfs volume ") + part.path;
            } else {
                plan.error = "First Aid does not support filesystem type '" + part.fstype + "'.";
            }
            return plan;
        }

        case DiskOpKind::FormatExt4:
            if (!part.mountpoint.empty()) {
                plan.error = "Unmount " + part.path + " before formatting.";
                return plan;
            }
            plan.valid = true;
            plan.program = "mkfs.ext4";
            plan.args = {"-F"};
            if (!new_label.empty()) { plan.args.push_back("-L"); plan.args.push_back(new_label); }
            plan.args.push_back(part.path);
            plan.summary = "Format " + part.path + " as ext4";
            return plan;

        case DiskOpKind::FormatFat32:
            if (!part.mountpoint.empty()) {
                plan.error = "Unmount " + part.path + " before formatting.";
                return plan;
            }
            plan.valid = true;
            plan.program = "mkfs.vfat";
            plan.args = {"-F", "32"};
            if (!new_label.empty()) { plan.args.push_back("-n"); plan.args.push_back(new_label); }
            plan.args.push_back(part.path);
            plan.summary = "Format " + part.path + " as FAT32";
            return plan;

        case DiskOpKind::FormatBtrfs:
            if (!part.mountpoint.empty()) {
                plan.error = "Unmount " + part.path + " before formatting.";
                return plan;
            }
            plan.valid = true;
            plan.program = "mkfs.btrfs";
            plan.args = {"-f"};
            if (!new_label.empty()) { plan.args.push_back("-L"); plan.args.push_back(new_label); }
            plan.args.push_back(part.path);
            plan.summary = "Format " + part.path + " as btrfs";
            return plan;

        case DiskOpKind::Mount:
            if (!part.mountpoint.empty()) {
                plan.error = part.path + " is already mounted at " + part.mountpoint + ".";
                return plan;
            }
            plan.valid = true;
            plan.program = "mount";
            plan.args = {part.path, "/mnt/recovery-" + part.name};
            plan.summary = "Mount " + part.path + " at /mnt/recovery-" + part.name;
            return plan;

        case DiskOpKind::Unmount:
            if (part.mountpoint.empty()) {
                plan.error = part.path + " is not mounted.";
                return plan;
            }
            plan.valid = true;
            plan.program = "umount";
            plan.args = {part.path};
            plan.summary = "Unmount " + part.path;
            return plan;

        case DiskOpKind::Resize:
            if (!part.can_shrink) {
                plan.error = part.shrink_reason;
                return plan;
            }
            if (!part.mountpoint.empty() && fs_l != "btrfs") {
                plan.error = "Unmount " + part.path + " before resizing.";
                return plan;
            }
            if (new_size_mb * kMiB < part.min_size_bytes || new_size_mb * kMiB >= part.size_bytes) {
                plan.error = "Target resize size is out of safe range.";
                return plan;
            }
            plan.valid = true;
            if (fs_l == "ntfs") {
                plan.program = "ntfsresize";
                plan.args = {"--force", "--size", std::to_string(new_size_mb) + "M", part.path};
            } else if (fs_l == "btrfs") {
                plan.program = "btrfs";
                plan.args = {"filesystem", "resize", std::to_string(new_size_mb) + "M",
                             part.mountpoint.empty() ? "/mnt" : part.mountpoint};
            } else {
                plan.program = "resize2fs";
                plan.args = {part.path, std::to_string(new_size_mb) + "M"};
            }
            plan.summary = "Resize " + part.path + " to " + std::to_string(new_size_mb) + " MiB";
            return plan;
    }

    plan.error = "Unknown disk operation.";
    return plan;
}

DiskOpPlan plan_disk_wipe_gpt(const DiskInfo& disk) {
    DiskOpPlan plan;
    if (disk.path.empty()) {
        plan.error = "No target disk selected.";
        return plan;
    }
    if (disk.is_live_medium) {
        plan.error = "Cannot wipe the active Live USB drive (" + disk.path + ").";
        return plan;
    }
    if (disk.read_only) {
        plan.error = "Target disk " + disk.path + " is read-only.";
        return plan;
    }
    plan.valid = true;
    plan.program = "bash";
    plan.args = {
        "-c",
        "set -e; DISK=\"$1\"; "
        "wipefs -a \"$DISK\" 2>/dev/null || true; "
        "sgdisk --zap-all \"$DISK\"; "
        "sgdisk -o \"$DISK\"; "
        "partprobe \"$DISK\" 2>/dev/null || true; "
        "udevadm settle --timeout=10 2>/dev/null || true",
        "_",
        disk.path,
    };
    plan.summary = "Created clean GPT table on " + disk.path + ".";
    return plan;
}

DiskOpPlan plan_disk_wipe_mbr(const DiskInfo& disk) {
    DiskOpPlan plan;
    if (disk.path.empty()) {
        plan.error = "No target disk selected.";
        return plan;
    }
    if (disk.is_live_medium) {
        plan.error = "Cannot wipe the active Live USB drive (" + disk.path + ").";
        return plan;
    }
    if (disk.read_only) {
        plan.error = "Target disk " + disk.path + " is read-only.";
        return plan;
    }
    plan.valid = true;
    plan.program = "bash";
    plan.args = {
        "-c",
        "set -e; DISK=\"$1\"; "
        "wipefs -a \"$DISK\" 2>/dev/null || true; "
        "parted -s \"$DISK\" mklabel msdos; "
        "partprobe \"$DISK\" 2>/dev/null || true; "
        "udevadm settle --timeout=10 2>/dev/null || true",
        "_",
        disk.path,
    };
    plan.summary = "Created clean MBR table on " + disk.path + ".";
    return plan;
}

DiskOpPlan plan_disk_delete_partition(const DiskInfo& disk, const PartitionInfo& part) {
    DiskOpPlan plan;
    if (disk.is_live_medium) {
        plan.error = "Cannot modify the active Live USB drive (" + disk.path + ").";
        return plan;
    }
    if (disk.read_only) {
        plan.error = "Target disk " + disk.path + " is read-only.";
        return plan;
    }
    if (part.path.empty() || part.part_number <= 0 || part.parent_disk.empty()) {
        plan.error = "Cannot determine partition number.";
        return plan;
    }
    plan.valid = true;
    if (disk.table_kind == PartitionTableKind::MBR) {
        // NEVER run sgdisk on an MBR disk (sgdisk converts MBR to GPT!).
        plan.program = "parted";
        plan.args = {"-s", part.parent_disk, "rm", std::to_string(part.part_number)};
    } else {
        plan.program = "sgdisk";
        plan.args = {"-d", std::to_string(part.part_number), part.parent_disk};
    }
    plan.summary = "Deleted " + part.path + ".";
    return plan;
}

DiskOpPlan plan_disk_create_partition(const DiskInfo& disk,
                                      std::uint64_t size_gib,
                                      bool use_all_remaining,
                                      std::string_view fstype) {
    DiskOpPlan plan;
    if (disk.path.empty()) {
        plan.error = "No target disk available.";
        return plan;
    }
    if (disk.is_live_medium) {
        plan.error = "Cannot partition the active Live USB drive (" + disk.path + ").";
        return plan;
    }
    if (disk.read_only) {
        plan.error = "Target disk " + disk.path + " is read-only.";
        return plan;
    }
    const std::string fs = to_lower(fstype);
    std::string type_guid;
    std::string parted_fs;
    std::string mkfs_cmd;
    std::string size_arg;
    std::uint64_t size_mib = 0;

    if (fs == "efi") {
        type_guid = "ef00";
        parted_fs = "fat32";
        mkfs_cmd = "mkfs.vfat -F32 -n FINNOS_EFI \"$NEWPART\"";
        size_arg = "0:0:+512M";
        size_mib = 512;
    } else if (fs == "ext4") {
        type_guid = "8300";
        parted_fs = "ext4";
        mkfs_cmd = "mkfs.ext4 -F \"$NEWPART\"";
    } else if (fs == "btrfs") {
        type_guid = "8300";
        parted_fs = "btrfs";
        mkfs_cmd = "mkfs.btrfs -f \"$NEWPART\"";
    } else if (fs == "fat32" || fs == "vfat") {
        type_guid = "ef00";
        parted_fs = "fat32";
        mkfs_cmd = "mkfs.vfat -F32 -n FINNOS_EFI \"$NEWPART\"";
    } else {
        plan.error = "Unsupported filesystem '" + std::string(fstype) + "'.";
        return plan;
    }

    if (size_arg.empty()) {
        if (!use_all_remaining && size_gib == 0) {
            plan.error = "Partition size must be at least 1 GiB.";
            return plan;
        }
        size_arg = use_all_remaining
            ? "0:0:0"
            : ("0:0:+" + std::to_string(size_gib) + "G");
        size_mib = use_all_remaining ? 0 : (size_gib * 1024ULL);
    }

    if (disk.table_kind == PartitionTableKind::MBR) {
        std::string mbr_err;
        if (!mbr_can_add_partitions(disk, 1, false, &mbr_err)) {
            plan.error = mbr_err;
            return plan;
        }
        const std::string end_expr = (size_mib == 0)
            ? "END_MB=\"$FREE_END\""
            : ("END_MB=$((FREE_START + " + std::to_string(size_mib) + ")); [ \"$END_MB\" -gt \"$FREE_END\" ] && END_MB=\"$FREE_END\"");
        const std::string esp_flag = (fs == "efi" || fs == "fat32" || fs == "vfat")
            ? "parted -s \"$DISK\" set \"$NEWNUM\" esp on 2>/dev/null || true; "
            : "";
        const std::string script =
            "set -e; "
            "DISK=\"$1\"; PTYPE=\"$2\"; "
            "FREE=$(parted -ms \"$DISK\" unit MiB print free | awk -F: '$5==\"free;\"{gsub(/MiB/,\"\",$2);gsub(/MiB/,\"\",$3);gsub(/MiB/,\"\",$4); if(int($4)>best){best=int($4);s=int($2)+1;e=int($3)}} END{if(best>0) print s,e}'); "
            "read -r FREE_START FREE_END <<< \"$FREE\"; "
            "[ -n \"$FREE_START\" ] || { echo 'No unallocated free space on disk' >&2; exit 1; }; " +
            end_expr + "; "
            "BEFORE=$(mktemp); AFTER=$(mktemp); trap 'rm -f \"$BEFORE\" \"$AFTER\"' EXIT; "
            "lsblk -lno PATH,TYPE \"$DISK\" | awk '$2==\"part\"{print $1}' | sort > \"$BEFORE\"; "
            "parted -s \"$DISK\" unit MiB mkpart primary \"$PTYPE\" \"${FREE_START}MiB\" \"${END_MB}MiB\"; "
            "partprobe \"$DISK\" 2>/dev/null || true; "
            "udevadm settle --timeout=10 2>/dev/null || true; "
            "lsblk -lno PATH,TYPE \"$DISK\" | awk '$2==\"part\"{print $1}' | sort > \"$AFTER\"; "
            "NEWPART=$(comm -13 \"$BEFORE\" \"$AFTER\" | head -n1); "
            "[ -b \"$NEWPART\" ] || NEWPART=$(lsblk -lno PATH,TYPE \"$DISK\" | awk '$2==\"part\"{p=$1} END{print p}'); "
            "NEWNUM=$(lsblk -no PARTN \"$NEWPART\" 2>/dev/null | tr -d '[:space:]'); " +
            esp_flag +
            "if [ -b \"$NEWPART\" ]; then " + mkfs_cmd + "; fi";
        plan.valid = true;
        plan.program = "bash";
        plan.args = {"-c", script, "_", disk.path, parted_fs};
        plan.summary = "Created & formatted " + fs + " MBR partition on " + disk.path + ".";
        return plan;
    }

    // GPT path: repair moved backup GPT header if needed, create partition, format.
    const std::string script =
        "set -e; "
        "DISK=\"$1\"; SIZE=\"$2\"; TYPE=\"$3\"; "
        "sgdisk -p \"$DISK\" >/dev/null 2>&1 || sgdisk -o \"$DISK\"; "
        "sgdisk -e \"$DISK\" >/dev/null 2>&1 || true; "
        "BEFORE=$(mktemp); AFTER=$(mktemp); trap 'rm -f \"$BEFORE\" \"$AFTER\"' EXIT; "
        "lsblk -lno PATH,TYPE \"$DISK\" | awk '$2==\"part\"{print $1}' | sort > \"$BEFORE\"; "
        "sgdisk -n \"$SIZE\" -t \"0:$TYPE\" \"$DISK\"; "
        "partprobe \"$DISK\" 2>/dev/null || true; "
        "udevadm settle --timeout=10 2>/dev/null || true; "
        "lsblk -lno PATH,TYPE \"$DISK\" | awk '$2==\"part\"{print $1}' | sort > \"$AFTER\"; "
        "NEWPART=$(comm -13 \"$BEFORE\" \"$AFTER\" | head -n1); "
        "[ -b \"$NEWPART\" ] || NEWPART=$(lsblk -lno PATH,TYPE \"$DISK\" | awk '$2==\"part\"{p=$1} END{print p}'); "
        "if [ -b \"$NEWPART\" ]; then " + mkfs_cmd + "; fi";

    plan.valid = true;
    plan.program = "bash";
    plan.args = {"-c", script, "_", disk.path, size_arg, type_guid};
    plan.summary = "Created & formatted " + fs + " partition on " + disk.path + ".";
    return plan;
}

std::string format_bytes(std::uint64_t bytes) {
    char buf[64];
    if (bytes >= 1000ULL * kGiB) {
        std::snprintf(buf, sizeof(buf), "%.2f TB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= kGiB) {
        std::snprintf(buf, sizeof(buf), "%.1f GB",
                      static_cast<double>(bytes) / static_cast<double>(kGiB));
    } else if (bytes >= kMiB) {
        std::snprintf(buf, sizeof(buf), "%.0f MB",
                      static_cast<double>(bytes) / static_cast<double>(kMiB));
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

std::string generate_loader_conf(std::string_view default_entry, int timeout_sec) {
    std::ostringstream out;
    out << "default " << (default_entry.empty() ? "finnos.conf" : default_entry) << '\n';
    out << "timeout " << std::clamp(timeout_sec, 0, 60) << '\n';
    out << "console-mode max\n";
    out << "editor no\n";
    out << "auto-entries yes\n";
    out << "auto-firmware yes\n";
    return out.str();
}

std::string generate_bls_entry(std::string_view title,
                               std::string_view version,
                               std::string_view linux_path,
                               const std::vector<std::string>& initrds,
                               std::string_view options,
                               std::string_view sort_key) {
    std::ostringstream out;
    out << "title   " << title << '\n';
    if (!version.empty()) out << "version " << version << '\n';
    if (!sort_key.empty()) out << "sort-key " << sort_key << '\n';
    out << "linux   " << linux_path << '\n';
    for (const auto& rd : initrds) {
        if (!rd.empty()) out << "initrd  " << rd << '\n';
    }
    if (!options.empty()) out << "options " << options << '\n';
    return out.str();
}

}  // namespace finn::recovery
