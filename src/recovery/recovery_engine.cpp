#include "recovery/recovery_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

// Minimal, strict recursive-descent JSON reader for lsblk -J output (zero external deps).
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

void classify_partition(PartitionInfo& p) {
    const std::string fs_l = to_lower(p.fstype);
    const std::string pt_l = to_lower(p.parttype);
    const std::string pl_l = to_lower(p.partlabel);
    const std::string lb_l = to_lower(p.label);

    // EFI System Partition GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    if (pt_l == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" ||
        (fs_l == "vfat" && (pl_l.find("efi") != std::string::npos ||
                            lb_l.find("efi") != std::string::npos ||
                            p.mountpoint == "/boot" || p.mountpoint == "/boot/efi"))) {
        p.is_esp = true;
    }

    // Determine safe offline shrink capability honestly.
    if (p.is_esp) {
        p.can_shrink = false;
        p.shrink_reason = "EFI System Partition cannot be shrunk";
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
        p.shrink_reason = p.can_shrink ? "Online/mounted shrink via btrfs filesystem resize"
                                       : "btrfs partition too small to shrink";
    } else if (fs_l == "xfs") {
        p.can_shrink = false;
        p.shrink_reason = "XFS filesystems cannot be shrunk";
    } else if (fs_l == "apfs" || pt_l == "7c3457ef-0000-11aa-aa11-00306543ecac") {
        p.can_shrink = false;
        p.shrink_reason = "Resize APFS containers from macOS Disk Utility first, then select free space";
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
    } else if (fs_l == "ntfs" && pl_l.find("recovery") == std::string::npos) {
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
            name.rfind("zram", 0) == 0 || name.rfind("ram", 0) == 0) {
            continue;
        }
        DiskInfo d;
        d.name = name;
        d.path = dev.str("path").empty() ? ("/dev/" + name) : dev.str("path");
        d.model = trim(dev.str("model"));
        d.transport = to_lower(dev.str("tran"));
        d.size_bytes = dev.u64("size");
        d.removable = dev.flag("rm");
        d.read_only = dev.flag("ro");
        if (!live_backing_disk.empty() &&
            (d.path == live_backing_disk || d.name == live_backing_disk)) {
            d.is_live_medium = true;
        }

        if (const JsonVal* children = dev.field("children");
            children && children->kind == JsonVal::Kind::Array) {
            for (const JsonVal& ch : children->arr) {
                if (ch.kind != JsonVal::Kind::Object) continue;
                const std::string ctype = ch.str("type");
                if (ctype != "part" && ctype != "crypt") continue;
                PartitionInfo p;
                p.name = ch.str("name");
                p.path = ch.str("path").empty() ? ("/dev/" + p.name) : ch.str("path");
                p.parent_disk = d.path;
                p.part_number = trailing_part_number(p.path);
                p.size_bytes = ch.u64("size");
                p.fstype = ch.str("fstype");
                p.label = ch.str("label");
                p.partlabel = ch.str("partlabel");
                p.uuid = ch.str("uuid");
                p.partuuid = ch.str("partuuid");
                p.parttype = to_lower(ch.str("parttype"));
                p.mountpoint = ch.str("mountpoint");
                if (p.mountpoint == "/run/archiso/bootmnt" || p.mountpoint == "/run/archiso") {
                    d.is_live_medium = true;
                }
                classify_partition(p);
                if (p.is_esp && d.esp_partition_path.empty()) {
                    d.esp_partition_path = p.path;
                }
                d.partitions.push_back(std::move(p));
            }
        }
        disks.push_back(std::move(d));
    }
    return disks;
}

std::vector<DetectedOs> probe_os_directory(const std::string& mount_root,
                                           const PartitionInfo& part) {
    std::vector<DetectedOs> found;
    std::error_code ec;
    const fs::path root(mount_root);
    if (!fs::is_directory(root, ec)) return found;

    // 1. EFI System Partition inspection (Windows Boot Manager, macOS, FinnOS/Linux BLS)
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

    // 2. Windows NTFS root check
    if (fs::is_regular_file(root / "Windows" / "System32" / "ntoskrnl.exe", ec) ||
        fs::is_regular_file(root / "Windows" / "System32" / "winload.efi", ec)) {
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

InstallPlan plan_install(const InstallRequest& req,
                         const std::vector<DiskInfo>& disks) {
    InstallPlan plan;
    plan.mode = req.mode;

    if (!req.confirmed) {
        plan.error = "Explicit confirmation is required before modifying storage.";
        return plan;
    }

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

            plan.valid = true;
            plan.summary = (req.mode == InstallMode::CleanEraseAB)
                ? ("Erase " + disk->path + " and install FinnOS with A/B OTA slots (FINNOS_A / FINNOS_B / FINNOS_DATA).")
                : ("Erase " + disk->path + " and install FinnOS (FAT32 ESP + ext4 root).");
            plan.warnings.push_back("All existing partitions and data on " + disk->path + " will be permanently erased.");
            plan.warnings.push_back("Language, Wi-Fi, user account, hostname, and appearance will be configured on first boot in OOBE.");
            plan.steps = {
                "Create GPT partition table on " + disk->path,
                req.mode == InstallMode::CleanEraseAB
                    ? "Format 1024 MiB FAT32 ESP + FINNOS_A, FINNOS_B, FINNOS_DATA ext4 volumes"
                    : "Format 512 MiB FAT32 ESP + ext4 root volume",
                "Deploy FinnOS base system and userland (no user credentials pre-configured)",
                "Install FinnOS graphical UEFI bootloader (systemd-boot BLS) and mark first-boot OOBE pending",
            };
            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "clean",
            };
            if (req.mode == InstallMode::CleanEraseAB) {
                plan.finn_install_args.push_back("--ab-slots");
            }
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
            if (!part->can_shrink) {
                plan.error = part->shrink_reason.empty()
                    ? ("Partition " + part->path + " cannot be shrunk safely.")
                    : part->shrink_reason;
                return plan;
            }
            const std::uint64_t new_bytes = req.resize_new_size_mb * kMiB;
            if (new_bytes < part->min_size_bytes) {
                plan.error = "Shrunk size must be at least " + format_bytes(part->min_size_bytes) + ".";
                return plan;
            }
            if (new_bytes >= part->size_bytes || (part->size_bytes - new_bytes) < kMinFinnRootBytes) {
                plan.error = "Shrinking must free at least 12 GiB for FinnOS.";
                return plan;
            }
            const std::string esp = req.esp_partition.empty() ? disk->esp_partition_path : req.esp_partition;
            if (esp.empty()) {
                plan.error = "No EFI System Partition found on " + disk->path + " to share for dual-boot.";
                return plan;
            }

            plan.valid = true;
            plan.summary = "Shrink " + part->path + " (" + part->fstype + ") to " +
                           format_bytes(new_bytes) + ", install FinnOS into " +
                           format_bytes(part->size_bytes - new_bytes) + " freed space, and preserve ESP " + esp + ".";
            plan.warnings.push_back("Existing EFI bootloaders on " + esp + " (Windows/macOS/Linux) will be preserved untouched.");
            plan.warnings.push_back("User account, hostname, Wi-Fi, and preferences will be set on first normal boot in OOBE.");
            plan.steps = {
                "Verify & shrink " + part->path + " (" + part->fstype + ") to " + std::to_string(req.resize_new_size_mb) + " MiB",
                "Create new ext4 FinnOS root partition in the freed space on " + disk->path,
                "Mount existing ESP " + esp + " at /mnt/boot without formatting",
                "Deploy FinnOS and register graphical dual-boot UEFI picker (finn-boot / systemd-boot)",
            };
            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "dualboot-resize",
                "--resize-part", part->path,
                "--resize-mb", std::to_string(req.resize_new_size_mb),
                "--esp-part", esp,
                "--yes",
            };
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
            if (part->is_esp) {
                plan.error = "Cannot install FinnOS root onto an EFI System Partition.";
                return plan;
            }
            if (part->size_bytes > 0 && part->size_bytes < kMinFinnRootBytes) {
                plan.error = "Selected partition is smaller than 12 GiB.";
                return plan;
            }
            const std::string esp = req.esp_partition.empty() ? disk->esp_partition_path : req.esp_partition;
            if (esp.empty()) {
                plan.error = "Select an EFI System Partition (ESP) for UEFI boot registration.";
                return plan;
            }

            plan.valid = true;
            plan.summary = "Format " + part->path + " as ext4, install FinnOS, and share existing ESP " + esp + ".";
            plan.warnings.push_back("Data on " + part->path + " will be erased; other partitions and bootloaders on " + esp + " are preserved.");
            plan.steps = {
                "Format " + part->path + " as ext4 (label finnos)",
                "Mount existing ESP " + esp + " at /mnt/boot without formatting",
                "Deploy FinnOS base system and userland",
                "Register FinnOS + existing OSes in graphical UEFI bootloader",
            };
            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "partition",
                "--root-part", part->path,
                "--esp-part", esp,
                "--yes",
            };
            return plan;
        }

        case InstallMode::ReinstallPreserveData: {
            const DiskInfo* disk = nullptr;
            const PartitionInfo* part = find_partition(disks, req.target_partition, &disk);
            if (!part || !disk) {
                plan.error = "Select an existing FinnOS root partition to reinstall.";
                return plan;
            }
            if (to_lower(part->fstype) != "ext4" && to_lower(part->fstype) != "btrfs") {
                plan.error = "Reinstall requires an existing ext4 or btrfs FinnOS root partition.";
                return plan;
            }
            const std::string esp = req.esp_partition.empty() ? disk->esp_partition_path : req.esp_partition;
            if (esp.empty()) {
                plan.error = "No EFI System Partition found for " + part->path + ".";
                return plan;
            }

            plan.valid = true;
            plan.summary = "Reinstall FinnOS system files on " + part->path + " while preserving /home and user settings.";
            plan.steps = {
                "Mount " + part->path + " and ESP " + esp + " without formatting",
                "Preserve /home, /var/lib/finn/oobe_complete, and NetworkManager profiles",
                "Refresh FinnOS kernel, initramfs, and userland binaries",
                "Verify graphical UEFI bootloader entries",
            };
            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "reinstall",
                "--root-part", part->path,
                "--esp-part", esp,
                "--yes",
            };
            return plan;
        }

        case InstallMode::RepairBootloader: {
            const DiskInfo* disk = nullptr;
            const PartitionInfo* part = find_partition(disks, req.target_partition, &disk);
            if (!part || !disk) {
                plan.error = "Select the FinnOS root partition whose bootloader should be repaired.";
                return plan;
            }
            const std::string esp = req.esp_partition.empty() ? disk->esp_partition_path : req.esp_partition;
            if (esp.empty()) {
                plan.error = "No EFI System Partition found on " + disk->path + ".";
                return plan;
            }

            plan.valid = true;
            plan.summary = "Repair UEFI graphical bootloader (finn-boot / systemd-boot) on " + esp +
                           " and rebuild initramfs for " + part->path + ".";
            plan.steps = {
                "Mount " + part->path + " at /mnt and ESP " + esp + " at /mnt/boot",
                "Reinstall FinnOS graphical UEFI bootloader (EFI/BOOT and EFI/systemd)",
                "Regenerate /boot/loader/loader.conf and UAPI BLS entries preserving dual-boot OSes",
                "Rebuild initramfs (mkinitcpio -P)",
            };
            plan.finn_install_args = {
                "--disk", disk->path,
                "--mode", "repair-boot",
                "--root-part", part->path,
                "--esp-part", esp,
                "--yes",
            };
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

std::string format_bytes(std::uint64_t bytes) {
    char buf[64];
    if (bytes >= 1000ULL * kGiB) {
        std::snprintf(buf, sizeof(buf), "%.2f TB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= kGiB) {
        std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / static_cast<double>(kGiB));
    } else if (bytes >= kMiB) {
        std::snprintf(buf, sizeof(buf), "%.0f MB", static_cast<double>(bytes) / static_cast<double>(kMiB));
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
