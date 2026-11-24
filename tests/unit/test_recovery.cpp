// ASan/UBSan unit tests for Recovery Mode disk parsing, cross-OS detection,
// dual-boot resize planning, reinstall, bootloader repair, and disk ops.

#include "recovery/recovery_engine.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

using namespace finn::recovery;

constexpr const char* kFixtureLsblk = R"json({
  "blockdevices": [
    {
      "name": "sda",
      "path": "/dev/sda",
      "size": 16000000000,
      "type": "disk",
      "model": "FinnOS USB",
      "tran": "usb",
      "rm": true,
      "ro": false,
      "children": [
        {
          "name": "sda1",
          "path": "/dev/sda1",
          "size": 2000000000,
          "type": "part",
          "fstype": "iso9660",
          "label": "FINNOS",
          "mountpoint": "/run/archiso/bootmnt"
        }
      ]
    },
    {
      "name": "nvme0n1",
      "path": "/dev/nvme0n1",
      "size": 512000000000,
      "type": "disk",
      "model": "NVMe SSD 512GB",
      "tran": "nvme",
      "rm": false,
      "ro": false,
      "children": [
        {
          "name": "nvme0n1p1",
          "path": "/dev/nvme0n1p1",
          "size": 536870912,
          "type": "part",
          "fstype": "vfat",
          "label": "SYSTEM",
          "parttype": "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
        },
        {
          "name": "nvme0n1p2",
          "path": "/dev/nvme0n1p2",
          "size": 256000000000,
          "type": "part",
          "fstype": "ntfs",
          "label": "Windows"
        },
        {
          "name": "nvme0n1p3",
          "path": "/dev/nvme0n1p3",
          "size": 128000000000,
          "type": "part",
          "fstype": "apfs",
          "parttype": "7c3457ef-0000-11aa-aa11-00306543ecac"
        },
        {
          "name": "nvme0n1p4",
          "path": "/dev/nvme0n1p4",
          "size": 100000000000,
          "type": "part",
          "fstype": "ext4",
          "label": "FINNOS_A",
          "partlabel": "FINNOS_A"
        }
      ]
    }
  ]
})json";

void test_lsblk_parsing_and_os_detection() {
    const auto disks = parse_lsblk_json(kFixtureLsblk);
    assert(disks.size() == 2);
    assert(disks[0].is_live_medium);
    assert(!disks[1].is_live_medium);
    assert(disks[1].esp_partition_path == "/dev/nvme0n1p1");
    assert(disks[1].partitions.size() == 4);
    assert(disks[1].partitions[0].is_esp);
    assert(disks[1].partitions[1].can_shrink);  // NTFS 256GB
    assert(!disks[1].partitions[2].can_shrink); // APFS cannot shrink offline
    assert(disks[1].partitions[3].can_shrink);  // ext4 100GB

    const auto oses = detect_oses(disks);
    assert(oses.size() == 3);
    bool has_win = false, has_mac = false, has_finn = false;
    for (const auto& o : oses) {
        if (o.kind == OsKind::Windows) has_win = true;
        if (o.kind == OsKind::MacOS) has_mac = true;
        if (o.kind == OsKind::FinnOS) has_finn = true;
    }
    assert(has_win && has_mac && has_finn);
}

void test_probe_os_directory() {
    const fs::path tmp = fs::temp_directory_path() / "finn-recovery-probe-test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "EFI" / "Microsoft" / "Boot");
    fs::create_directories(tmp / "etc");
    std::ofstream(tmp / "EFI" / "Microsoft" / "Boot" / "bootmgfw.efi") << "MZ";
    std::ofstream(tmp / "etc" / "os-release") << "ID=ubuntu\nPRETTY_NAME=\"Ubuntu 24.04 LTS\"\n";

    PartitionInfo p;
    p.path = "/dev/nvme0n1p1";
    p.parent_disk = "/dev/nvme0n1";
    p.fstype = "vfat";
    const auto found = probe_os_directory(tmp.string(), p);
    assert(found.size() == 2);
    assert(found[0].kind == OsKind::Windows);
    assert(found[1].kind == OsKind::Linux);
    assert(found[1].name == "Ubuntu 24.04 LTS");
    fs::remove_all(tmp, ec);
}

void test_install_plans_and_no_user_config() {
    const auto disks = parse_lsblk_json(kFixtureLsblk);

    // 1. Live medium erase MUST be blocked.
    InstallRequest live_req;
    live_req.mode = InstallMode::CleanErase;
    live_req.target_disk = "/dev/sda";
    live_req.confirmed = true;
    assert(!plan_install(live_req, disks).valid);

    // 2. Clean install must NOT include --hostname or --timezone (strictly OOBE).
    InstallRequest clean_req;
    clean_req.mode = InstallMode::CleanEraseAB;
    clean_req.target_disk = "/dev/nvme0n1";
    clean_req.confirmed = true;
    const auto clean_plan = plan_install(clean_req, disks);
    assert(clean_plan.valid);
    for (const auto& arg : clean_plan.finn_install_args) {
        assert(arg != "--hostname");
        assert(arg != "--timezone");
    }
    assert(std::find(clean_plan.finn_install_args.begin(),
                     clean_plan.finn_install_args.end(), "--ab-slots") !=
           clean_plan.finn_install_args.end());

    // 3. Dual-boot NTFS resize plan succeeds and preserves ESP.
    InstallRequest db_req;
    db_req.mode = InstallMode::DualBootResize;
    db_req.resize_partition = "/dev/nvme0n1p2";
    db_req.resize_new_size_mb = 180000;  // ~180 GiB kept, ~64 GiB freed for FinnOS
    db_req.confirmed = true;
    const auto db_plan = plan_install(db_req, disks);
    assert(db_plan.valid);
    assert(std::find(db_plan.finn_install_args.begin(),
                     db_plan.finn_install_args.end(), "dualboot-resize") !=
           db_plan.finn_install_args.end());
    assert(std::find(db_plan.finn_install_args.begin(),
                     db_plan.finn_install_args.end(), "/dev/nvme0n1p1") !=
           db_plan.finn_install_args.end());

    // 4. APFS offline shrink must be refused honestly.
    InstallRequest apfs_req = db_req;
    apfs_req.resize_partition = "/dev/nvme0n1p3";
    assert(!plan_install(apfs_req, disks).valid);

    // 5. Reinstall & Bootloader Repair plans validate cleanly.
    InstallRequest rein_req;
    rein_req.mode = InstallMode::ReinstallPreserveData;
    rein_req.target_partition = "/dev/nvme0n1p4";
    rein_req.confirmed = true;
    assert(plan_install(rein_req, disks).valid);

    InstallRequest boot_req = rein_req;
    boot_req.mode = InstallMode::RepairBootloader;
    assert(plan_install(boot_req, disks).valid);
}

void test_disk_ops_and_bls() {
    PartitionInfo ext_p;
    ext_p.path = "/dev/nvme0n1p4";
    ext_p.fstype = "ext4";
    ext_p.size_bytes = 100ULL * 1024ULL * 1024ULL * 1024ULL;
    ext_p.can_shrink = true;
    ext_p.min_size_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    const auto check = plan_disk_op(DiskOpKind::FirstAidCheck, ext_p);
    assert(check.valid && check.program == "e2fsck");
    const auto fmt = plan_disk_op(DiskOpKind::FormatExt4, ext_p, 0, "finnos");
    assert(fmt.valid && fmt.program == "mkfs.ext4");

    ext_p.mountpoint = "/mnt";
    assert(!plan_disk_op(DiskOpKind::FormatExt4, ext_p).valid);

    const std::string lc = generate_loader_conf("finnos.conf", 5);
    assert(lc.find("default finnos.conf") != std::string::npos);
    assert(lc.find("auto-entries yes") != std::string::npos);
    const std::string bls = generate_bls_entry(
        "FinnOS", "1.0", "/vmlinuz-linux", {"/intel-ucode.img", "/initramfs-linux.img"},
        "root=UUID=abc rw quiet splash");
    assert(bls.find("initrd  /intel-ucode.img") != std::string::npos);
    assert(bls.find("initrd  /initramfs-linux.img") != std::string::npos);
}

}  // namespace

int main() {
    test_lsblk_parsing_and_os_detection();
    test_probe_os_directory();
    test_install_plans_and_no_user_config();
    test_disk_ops_and_bls();
    std::puts("test_recovery: ok");
    return 0;
}
