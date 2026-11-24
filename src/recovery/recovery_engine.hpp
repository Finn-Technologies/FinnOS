// Pure C++20 Recovery Mode, Disk Prober, Cross-OS Detector & Dual-Boot Planner.
//
// Zero Qt dependency so every disk/partition/OS-detection/dual-boot safety
// invariant is tested under ASan/UBSan without requiring root or real disks.
// User/system configuration (language, region, keyboard, Wi-Fi, account,
// hostname, appearance) is intentionally excluded from installation plans and
// deferred strictly to first-boot OOBE.

#ifndef FINN_RECOVERY_ENGINE_HPP
#define FINN_RECOVERY_ENGINE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace finn::recovery {

enum class OsKind {
    None = 0,
    FinnOS,
    Windows,
    MacOS,
    Linux,
};

std::string_view os_kind_id(OsKind kind) noexcept;
std::string_view os_kind_label(OsKind kind) noexcept;

struct PartitionInfo {
    std::string name;        // e.g. nvme0n1p1
    std::string path;        // e.g. /dev/nvme0n1p1
    std::string parent_disk; // e.g. /dev/nvme0n1
    int part_number = 0;     // 1-based partition number
    std::uint64_t size_bytes = 0;
    std::string fstype;      // vfat, ext4, ntfs, apfs, btrfs, xfs, swap, ...
    std::string label;
    std::string partlabel;
    std::string uuid;
    std::string partuuid;
    std::string parttype;    // GPT type GUID (lowercase)
    std::string mountpoint;
    bool is_esp = false;
    bool can_shrink = false;
    std::string shrink_reason;
    std::uint64_t min_size_bytes = 0;
    OsKind os_kind = OsKind::None;
    std::string os_name;
};

struct DiskInfo {
    std::string name;        // e.g. nvme0n1
    std::string path;        // e.g. /dev/nvme0n1
    std::string model;
    std::string transport;   // nvme, sata, usb, virtio, ...
    std::uint64_t size_bytes = 0;
    bool removable = false;
    bool read_only = false;
    bool is_live_medium = false;
    std::string esp_partition_path;
    std::vector<PartitionInfo> partitions;
};

struct DetectedOs {
    OsKind kind = OsKind::None;
    std::string name;
    std::string detail;
    std::string disk_path;
    std::string partition_path;
    std::string esp_path;
    std::string efi_loader_path; // e.g. \EFI\Microsoft\Boot\bootmgfw.efi
};

enum class InstallMode {
    CleanErase = 0,
    CleanEraseAB,
    DualBootResize,
    TargetPartition,
    ReinstallPreserveData,
    RepairBootloader,
};

struct InstallRequest {
    InstallMode mode = InstallMode::CleanErase;
    std::string target_disk;           // whole disk (/dev/nvme0n1)
    std::string target_partition;      // root partition for TargetPartition/Reinstall/Repair
    std::string esp_partition;         // existing ESP to preserve/share for dual-boot
    std::string resize_partition;      // existing OS partition to shrink for DualBootResize
    std::uint64_t resize_new_size_mb = 0; // new size of shrunk partition in MiB
    bool confirmed = false;
};

struct InstallPlan {
    bool valid = false;
    std::string error;
    InstallMode mode = InstallMode::CleanErase;
    std::string summary;
    std::vector<std::string> warnings;
    std::vector<std::string> steps;
    std::vector<std::string> finn_install_args;
};

enum class DiskOpKind {
    FirstAidCheck = 0,
    FirstAidRepair,
    FormatExt4,
    FormatFat32,
    FormatBtrfs,
    Mount,
    Unmount,
    Resize,
};

struct DiskOpPlan {
    bool valid = false;
    std::string error;
    std::string program;
    std::vector<std::string> args;
    std::string summary;
};

// Parse `lsblk -J -b -o NAME,PATH,PKNAME,SIZE,TYPE,FSTYPE,LABEL,PARTLABEL,UUID,PARTUUID,PARTTYPE,MOUNTPOINT,MODEL,RO,RM,TRAN`.
std::vector<DiskInfo> parse_lsblk_json(std::string_view json,
                                       std::string_view live_backing_disk = {});

// Inspect a mounted directory (root filesystem or ESP) to identify installed OSes.
std::vector<DetectedOs> probe_os_directory(const std::string& mount_root,
                                           const PartitionInfo& part);

// Aggregate detected OSes from parsed disk/partition metadata and mounted paths.
std::vector<DetectedOs> detect_oses(const std::vector<DiskInfo>& disks);

// Validate and generate an installation / dual-boot / recovery execution plan.
// Never includes user account, hostname, or timezone flags (strictly OOBE).
InstallPlan plan_install(const InstallRequest& req,
                         const std::vector<DiskInfo>& disks);

// Build a safe Disk Utility command plan (First Aid, Format, Mount, Unmount, Resize).
DiskOpPlan plan_disk_op(DiskOpKind op,
                        const PartitionInfo& part,
                        std::uint64_t new_size_mb = 0,
                        const std::string& new_label = {});

// Format byte count as human-readable IEC/decimal size (e.g. "256.0 GB").
std::string format_bytes(std::uint64_t bytes);

// UAPI Boot Loader Spec generators shared with bootloader repair & dual-boot setup.
std::string generate_loader_conf(std::string_view default_entry, int timeout_sec = 5);
std::string generate_bls_entry(std::string_view title,
                               std::string_view version,
                               std::string_view linux_path,
                               const std::vector<std::string>& initrds,
                               std::string_view options,
                               std::string_view sort_key = "finnos");

}  // namespace finn::recovery

#endif  // FINN_RECOVERY_ENGINE_HPP
