// Pure C++20 Recovery Mode Engine: Disk/Partition Prober, Firmware & Table
// Detector, Cross-OS Detector, Offline Image Integrity Verifier (SHA-512),
// Auto-Remediating Dual-Boot / GPT / MBR Planner, and Disk Utility Planner.
//
// Zero Qt dependency so every disk, partition, EFI, MBR/GPT, dual-boot,
// offline-image integrity, and safety invariant is verified under ASan/UBSan
// without requiring root or physical disks.
//
// Personal configuration (language, region, keyboard, Wi-Fi, user account,
// hostname, appearance) is excluded from installation plans and deferred
// strictly to first-boot OOBE.

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

enum class FirmwareMode {
    UEFI = 0,
    LegacyBios,
};

std::string_view firmware_mode_id(FirmwareMode fw) noexcept;
std::string_view firmware_mode_label(FirmwareMode fw) noexcept;

enum class PartitionTableKind {
    None = 0,
    GPT,
    MBR,
};

std::string_view partition_table_id(PartitionTableKind pt) noexcept;
std::string_view partition_table_label(PartitionTableKind pt) noexcept;

enum class EspAction {
    None = 0,
    UseExistingAtBoot,       // Spacious FAT32 ESP (>= 300 MiB) mounted at /boot
    UseExistingAtBootEfi,    // Small existing ESP (e.g. 100 MiB Windows ESP) mounted at /boot/efi (GRUB UEFI)
    FormatExistingEsp,       // Existing ESP partition has no valid FAT32 filesystem -> format it
    CreateNewEspFromFreeSpace, // Carve a fresh 512 MiB FAT32 ESP from freed/unallocated space
    SplitTargetForEsp,       // Automatically split selected target partition into 512 MiB ESP + root
    CreateNewTableWithEsp,   // Whole-disk wipe creating fresh table + ESP
};

std::string_view esp_action_id(EspAction act) noexcept;

enum class BootloaderStrategy {
    SystemdBoot = 0,         // finn-boot / systemd-boot BLS on spacious GPT ESP at /boot
    GrubUefi,                // GRUB UEFI with ESP at /boot/efi (handles 100M Windows ESP & MBR UEFI)
    GrubBios,                // GRUB BIOS (i386-pc) on MBR or GPT-with-BIOS-Boot
    HybridUefiAndBios,       // Clean GPT disk: finn-boot/systemd-boot UEFI + GRUB BIOS fallback
};

std::string_view bootloader_strategy_id(BootloaderStrategy strat) noexcept;

enum class OfflineImageKind {
    None = 0,
    LiveSquashfsRoot,          // Mounted /run/archiso/airootfs
    SquashfsFile,              // .sfs file on live medium (e.g. /run/archiso/bootmnt/finn/<arch>/airootfs.sfs)
    RecoveryPartitionSquashfs, // .sfs inside FINNOS_RECOVERY partition (/recovery or mounted recovery part)
    RootfsTarball,             // .tar.zst rootfs archive on local media / recovery partition
};

struct OfflineImageInfo {
    bool found = false;
    OfflineImageKind kind = OfflineImageKind::None;
    std::string source_path;     // Directory, .sfs, or .tar.zst path
    std::string kernel_path;     // Companion vmlinuz path if external to rootfs
    std::string checksum_path;   // Optional .sha512 sidecar path
    std::string label;           // Human-readable source label
    std::uint64_t size_bytes = 0;
    bool integrity_checked = false;
    bool integrity_ok = false;
    std::string integrity_detail;
};

struct FreeRegion {
    std::uint64_t start_bytes = 0;
    std::uint64_t end_bytes = 0;
    std::uint64_t size_bytes = 0;
    bool inside_extended = false;
};

struct PartitionInfo {
    std::string name;        // e.g. nvme0n1p1
    std::string path;        // e.g. /dev/nvme0n1p1
    std::string parent_disk; // e.g. /dev/nvme0n1
    int part_number = 0;     // 1-based partition number
    std::uint64_t start_bytes = 0;
    std::uint64_t size_bytes = 0;
    std::uint64_t fs_avail_bytes = 0; // Reported free bytes inside mounted/probed FS (0 if unknown)
    std::string fstype;      // vfat, ext4, ntfs, apfs, btrfs, xfs, swap, ...
    std::string label;
    std::string partlabel;
    std::string uuid;
    std::string partuuid;
    std::string parttype;    // GPT type GUID or MBR hex type (lowercase)
    std::string mountpoint;
    bool is_esp = false;
    bool is_bios_boot = false;
    bool is_extended = false;
    bool is_logical = false;
    bool is_recovery_part = false;
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
    PartitionTableKind table_kind = PartitionTableKind::None;
    bool removable = false;
    bool read_only = false;
    bool is_live_medium = false;
    std::string esp_partition_path;
    std::uint64_t esp_size_bytes = 0;
    std::uint64_t esp_free_bytes = 0;
    bool esp_formatted = false;
    bool has_bios_boot = false;
    int primary_part_count = 0;
    bool has_extended_part = false;
    int extended_part_number = 0;
    std::vector<PartitionInfo> partitions;
    std::vector<FreeRegion> free_regions;
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

struct HostEnvironment {
    FirmwareMode firmware = FirmwareMode::UEFI;
    OfflineImageInfo image;
};

enum class InstallMode {
    CleanErase = 0,
    CleanEraseAB,
    DualBootFreeSpace,
    DualBootResize,
    TargetPartition,
    ReinstallPreserveData,
    RepairBootloader,
};

struct InstallRequest {
    InstallMode mode = InstallMode::CleanErase;
    PartitionTableKind preferred_table = PartitionTableKind::GPT; // For CleanErase (GPT or MBR)
    std::string target_disk;           // whole disk (/dev/nvme0n1)
    std::string target_partition;      // root partition for TargetPartition/Reinstall/Repair
    std::string esp_partition;         // optional override ESP partition
    std::string resize_partition;      // existing OS partition to shrink for DualBootResize
    std::uint64_t resize_new_size_mb = 0; // new size of shrunk partition in MiB
    std::uint64_t free_start_mb = 0;   // unallocated region start (MiB) for DualBootFreeSpace
    std::uint64_t free_size_mb = 0;    // unallocated region size (MiB) for DualBootFreeSpace
    bool create_recovery_partition = false; // copy offline recovery image to FINNOS_RECOVERY partition
    bool offline_only = true;          // never fetch from the internet
    bool confirmed = false;
};

struct InstallPlan {
    bool valid = false;
    std::string error;
    InstallMode mode = InstallMode::CleanErase;
    FirmwareMode firmware = FirmwareMode::UEFI;
    PartitionTableKind target_table = PartitionTableKind::GPT;
    EspAction esp_action = EspAction::None;
    BootloaderStrategy bootloader = BootloaderStrategy::SystemdBoot;
    bool need_bios_boot = false;
    bool create_recovery_partition = false;
    std::string summary;
    std::vector<std::string> auto_fixes; // Transparent list of auto-generated partitions/remediations
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

// Detect active firmware mode (/sys/firmware/efi -> UEFI, else LegacyBios).
FirmwareMode detect_firmware_mode(std::string_view sys_efi_path = "/sys/firmware/efi") noexcept;

// Parse `lsblk -J -b -o NAME,PATH,PKNAME,SIZE,TYPE,FSTYPE,LABEL,PARTLABEL,UUID,PARTUUID,PARTTYPE,PTTYPE,MOUNTPOINT,MODEL,RO,RM,TRAN,FSAVAIL,START`.
std::vector<DiskInfo> parse_lsblk_json(std::string_view json,
                                       std::string_view live_backing_disk = {});

// Parse `parted -ms <disk> unit B print free` to extract unallocated free regions.
std::vector<FreeRegion> parse_parted_free_regions(std::string_view parted_machine_output);

// Return the largest unallocated free region on a disk (>= min_bytes), or nullptr.
const FreeRegion* find_best_free_region(const DiskInfo& disk,
                                        std::uint64_t min_bytes = 12ULL * 1024ULL * 1024ULL * 1024ULL) noexcept;

// Inspect a mounted directory (root filesystem or ESP) to identify installed OSes.
std::vector<DetectedOs> probe_os_directory(const std::string& mount_root,
                                           const PartitionInfo& part);

// Aggregate detected OSes from parsed disk/partition metadata and mounted paths.
std::vector<DetectedOs> detect_oses(const std::vector<DiskInfo>& disks);

// Return the largest safely shrinkable partition on a disk (or nullptr if none).
// Avoids picking tiny Windows System Reserved / Recovery NTFS partitions.
const PartitionInfo* find_best_shrinkable_partition(const DiskInfo& disk) noexcept;

// Compute lowercase hex SHA-512 digest of a byte buffer or file (pure C++20, zero deps).
std::string sha512_hex(std::string_view data);
bool sha512_file_hex(const std::string& path, std::string* out_hex, std::string* out_error = nullptr);

// Discover local offline OS installation image (live squashfs, ISO .sfs, or recovery partition).
// Never touches the network.
OfflineImageInfo discover_offline_image(const std::vector<std::string>& extra_search_roots = {},
                                        std::string_view arch = "x86_64");

// Verify integrity of a discovered offline image (checks required structure + SHA-512 sidecar if present).
bool verify_offline_image(OfflineImageInfo& image);

// Validate and generate an auto-remediating installation / dual-boot / recovery plan.
// Automatically handles missing EFI partitions, tiny 100 MiB Windows ESPs, missing
// GPT BIOS Boot partitions, MBR 4-primary limits, and offline-only deployment.
// Never includes user account, hostname, or timezone flags (strictly OOBE).
InstallPlan plan_install(const InstallRequest& req,
                         const std::vector<DiskInfo>& disks,
                         const HostEnvironment& env);

// Overload defaulting to UEFI host environment (with offline image check skipped unless provided).
InstallPlan plan_install(const InstallRequest& req,
                         const std::vector<DiskInfo>& disks);

// Build a safe Disk Utility command plan (First Aid, Format, Mount, Unmount, Resize).
DiskOpPlan plan_disk_op(DiskOpKind op,
                        const PartitionInfo& part,
                        std::uint64_t new_size_mb = 0,
                        const std::string& new_label = {});

// Build safe Disk Utility whole-disk / partition-table command plans (GPT & MBR safe).
// Every plan refuses the active live/recovery medium and read-only disks.
DiskOpPlan plan_disk_wipe_gpt(const DiskInfo& disk);
DiskOpPlan plan_disk_wipe_mbr(const DiskInfo& disk);
DiskOpPlan plan_disk_delete_partition(const DiskInfo& disk, const PartitionInfo& part);
DiskOpPlan plan_disk_create_partition(const DiskInfo& disk,
                                      std::uint64_t size_gib,
                                      bool use_all_remaining,
                                      std::string_view fstype);

// Format byte count as human-readable size (e.g. "256.0 GB").
std::string format_bytes(std::uint64_t bytes);

// UAPI Boot Loader Spec generators shared with bootloader repair & dual-boot setup.
std::string generate_loader_conf(std::string_view default_entry, int timeout_sec = 3);
std::string generate_bls_entry(std::string_view title,
                               std::string_view version,
                               std::string_view linux_path,
                               const std::vector<std::string>& initrds,
                               std::string_view options,
                               std::string_view sort_key = "finnos");

}  // namespace finn::recovery

#endif  // FINN_RECOVERY_ENGINE_HPP
