// Exhaustive ASan/UBSan unit tests for FinnOS Recovery Mode engine:
//   - GPT & MBR lsblk parsing, unallocated free-space detection (parted)
//   - Cross-OS detection (Windows UEFI/BIOS, macOS APFS, Linux, FinnOS)
//   - Pure C++20 SHA-512 cryptographic integrity verification & tamper refusal
//   - Offline image discovery (live root, ISO .sfs, recovery partition, .tar.zst)
//   - Auto-remediation of missing EFI partitions, tiny 100 MiB Windows ESPs,
//     unformatted ESPs, missing GPT BIOS Boot partitions, and MBR 4-primary limits
//   - Clean Erase (GPT, MBR, A/B, Recovery partition), Dual-Boot Free Space,
//     Dual-Boot Resize, Target Partition, Reinstall Preserve Data, Boot Repair
//   - GPT vs MBR safe Disk Utility operations & Live Media protection.

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

bool has_arg(const std::vector<std::string>& args, std::string_view needle) {
    return std::find(args.begin(), args.end(), needle) != args.end();
}

constexpr const char* kFixtureLsblk = R"json({
  "blockdevices": [
    {
      "name": "sda",
      "path": "/dev/sda",
      "size": 16000000000,
      "type": "disk",
      "pttype": "dos",
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
      "pttype": "gpt",
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
    assert(disks[0].table_kind == PartitionTableKind::MBR);
    assert(!disks[1].is_live_medium);
    assert(disks[1].table_kind == PartitionTableKind::GPT);
    assert(disks[1].esp_partition_path == "/dev/nvme0n1p1");
    assert(disks[1].esp_formatted);
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

void test_sha512_and_offline_image_integrity() {
    // FIPS 180-4 official SHA-512 test vectors
    assert(sha512_hex("") ==
           "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
           "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    assert(sha512_hex("abc") ==
           "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
           "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    const fs::path tmp = fs::temp_directory_path() / "finn-recovery-img-test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);

    // 1. Recovery partition SquashFS with valid SHA-512 sidecar
    const std::string payload = "hsqs-fake-finnos-recovery-squashfs-payload-0123456789";
    {
        std::ofstream f(tmp / "finnos.sfs", std::ios::binary);
        f << payload;
    }
    {
        std::ofstream s(tmp / "finnos.sha512");
        s << sha512_hex(payload) << "  finnos.sfs\n";
    }
    auto img = discover_offline_image({tmp.string()}, "x86_64");
    assert(img.found);
    assert(img.kind == OfflineImageKind::RecoveryPartitionSquashfs);
    assert(verify_offline_image(img));
    assert(img.integrity_ok);

    // 2. Tamper 1 byte -> SHA-512 verification MUST FAIL and block plan_install!
    {
        std::ofstream f(tmp / "finnos.sfs", std::ios::binary);
        f << "hsqs-TAMPERED-finnos-recovery-squashfs-payload-0123456789";
    }
    assert(!verify_offline_image(img));
    assert(!img.integrity_ok);

    const auto disks = parse_lsblk_json(kFixtureLsblk);
    HostEnvironment bad_env;
    bad_env.firmware = FirmwareMode::UEFI;
    bad_env.image = img;
    InstallRequest req;
    req.mode = InstallMode::CleanErase;
    req.target_disk = "/dev/nvme0n1";
    req.confirmed = true;
    const auto bad_plan = plan_install(req, disks, bad_env);
    assert(!bad_plan.valid);
    assert(bad_plan.error.find("integrity") != std::string::npos);

    fs::remove_all(tmp, ec);
}

void test_clean_erase_gpt_mbr_ab_and_offline_only() {
    const auto disks = parse_lsblk_json(kFixtureLsblk);

    // 1. Live medium erase MUST be blocked.
    InstallRequest live_req;
    live_req.mode = InstallMode::CleanErase;
    live_req.target_disk = "/dev/sda";
    live_req.confirmed = true;
    assert(!plan_install(live_req, disks).valid);

    // 2. Clean Erase A/B GPT + Recovery partition: offline-only, NO user config flags.
    InstallRequest ab_req;
    ab_req.mode = InstallMode::CleanEraseAB;
    ab_req.target_disk = "/dev/nvme0n1";
    ab_req.create_recovery_partition = true;
    ab_req.offline_only = true;
    ab_req.confirmed = true;
    const auto ab_plan = plan_install(ab_req, disks);
    assert(ab_plan.valid);
    assert(ab_plan.target_table == PartitionTableKind::GPT);
    assert(ab_plan.bootloader == BootloaderStrategy::HybridUefiAndBios);
    assert(ab_plan.create_recovery_partition);
    assert(has_arg(ab_plan.finn_install_args, "--ab-slots"));
    assert(has_arg(ab_plan.finn_install_args, "--offline-only"));
    assert(has_arg(ab_plan.finn_install_args, "--create-recovery-part"));
    assert(!has_arg(ab_plan.finn_install_args, "--hostname"));
    assert(!has_arg(ab_plan.finn_install_args, "--timezone"));

    // 3. Clean Erase MBR on Legacy BIOS
    HostEnvironment bios_env;
    bios_env.firmware = FirmwareMode::LegacyBios;
    InstallRequest mbr_req;
    mbr_req.mode = InstallMode::CleanErase;
    mbr_req.preferred_table = PartitionTableKind::MBR;
    mbr_req.target_disk = "/dev/nvme0n1";
    mbr_req.confirmed = true;
    const auto mbr_plan = plan_install(mbr_req, disks, bios_env);
    assert(mbr_plan.valid);
    assert(mbr_plan.target_table == PartitionTableKind::MBR);
    assert(mbr_plan.bootloader == BootloaderStrategy::GrubBios);
    assert(mbr_plan.esp_action == EspAction::None);
    assert(has_arg(mbr_plan.finn_install_args, "mbr"));
    assert(has_arg(mbr_plan.finn_install_args, "bios"));
}

void test_windows11_100m_esp_and_missing_efi_auto_remediation() {
    // Realistic Windows 11 GPT disk: 100 MiB ESP + 230 GiB C: + 650 MiB WinRE after C:.
    constexpr const char* kWin11Json = R"json({
      "blockdevices": [
        {
          "name": "nvme0n1",
          "path": "/dev/nvme0n1",
          "size": 256000000000,
          "type": "disk",
          "pttype": "gpt",
          "model": "Win11 NVMe",
          "tran": "nvme",
          "rm": false,
          "ro": false,
          "children": [
            {
              "name": "nvme0n1p1",
              "path": "/dev/nvme0n1p1",
              "size": 104857600,
              "type": "part",
              "fstype": "vfat",
              "label": "SYSTEM",
              "parttype": "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
            },
            {
              "name": "nvme0n1p2",
              "path": "/dev/nvme0n1p2",
              "size": 240000000000,
              "type": "part",
              "fstype": "ntfs",
              "label": "Windows"
            },
            {
              "name": "nvme0n1p3",
              "path": "/dev/nvme0n1p3",
              "size": 681574400,
              "type": "part",
              "fstype": "ntfs",
              "label": "Recovery",
              "partlabel": "Basic data partition"
            }
          ]
        }
      ]
    })json";

    const auto win11_disks = parse_lsblk_json(kWin11Json);
    assert(win11_disks.size() == 1);
    assert(win11_disks[0].esp_partition_path == "/dev/nvme0n1p1");
    assert(win11_disks[0].esp_size_bytes == 104857600ULL);

    HostEnvironment uefi_env;
    uefi_env.firmware = FirmwareMode::UEFI;

    // 100 MiB Windows ESP MUST be mounted at /boot/efi with GRUB UEFI (never /boot)!
    InstallRequest db_win11;
    db_win11.mode = InstallMode::DualBootResize;
    db_win11.resize_partition = "/dev/nvme0n1p2";
    db_win11.resize_new_size_mb = 160000;
    db_win11.confirmed = true;
    const auto win11_plan = plan_install(db_win11, win11_disks, uefi_env);
    assert(win11_plan.valid);
    assert(win11_plan.esp_action == EspAction::UseExistingAtBootEfi);
    assert(win11_plan.bootloader == BootloaderStrategy::GrubUefi);
    assert(has_arg(win11_plan.finn_install_args, "share-boot-efi"));
    assert(!win11_plan.auto_fixes.empty());

    // GPT disk with NO ESP at all under UEFI:
    constexpr const char* kNoEspGptJson = R"json({
      "blockdevices": [
        {
          "name": "sda",
          "path": "/dev/sda",
          "size": 256000000000,
          "type": "disk",
          "pttype": "gpt",
          "model": "No-ESP GPT SSD",
          "tran": "sata",
          "rm": false,
          "ro": false,
          "children": [
            {
              "name": "sda1",
              "path": "/dev/sda1",
              "size": 250000000000,
              "type": "part",
              "fstype": "ext4",
              "label": "Data"
            }
          ]
        }
      ]
    })json";
    const auto no_esp_disks = parse_lsblk_json(kNoEspGptJson);
    assert(no_esp_disks[0].esp_partition_path.empty());

    // A) DualBootResize on UEFI with NO ESP -> auto-creates 512M ESP from freed space!
    InstallRequest db_no_esp;
    db_no_esp.mode = InstallMode::DualBootResize;
    db_no_esp.resize_partition = "/dev/sda1";
    db_no_esp.resize_new_size_mb = 180000;
    db_no_esp.confirmed = true;
    const auto p_create_esp = plan_install(db_no_esp, no_esp_disks, uefi_env);
    assert(p_create_esp.valid);
    assert(p_create_esp.esp_action == EspAction::CreateNewEspFromFreeSpace);
    assert(has_arg(p_create_esp.finn_install_args, "create-esp"));
    assert(!p_create_esp.auto_fixes.empty());

    // B) TargetPartition on UEFI with NO ESP -> auto-splits target partition into ESP + root!
    InstallRequest tp_no_esp;
    tp_no_esp.mode = InstallMode::TargetPartition;
    tp_no_esp.target_partition = "/dev/sda1";
    tp_no_esp.confirmed = true;
    const auto p_split_esp = plan_install(tp_no_esp, no_esp_disks, uefi_env);
    assert(p_split_esp.valid);
    assert(p_split_esp.esp_action == EspAction::SplitTargetForEsp);
    assert(has_arg(p_split_esp.finn_install_args, "split-esp"));
    assert(!p_split_esp.auto_fixes.empty());

    // C) TargetPartition on Legacy BIOS GPT with NO BIOS Boot partition -> auto-carves ef02!
    HostEnvironment bios_env;
    bios_env.firmware = FirmwareMode::LegacyBios;
    const auto p_bios_gpt = plan_install(tp_no_esp, no_esp_disks, bios_env);
    assert(p_bios_gpt.valid);
    assert(p_bios_gpt.bootloader == BootloaderStrategy::GrubBios);
    assert(p_bios_gpt.need_bios_boot);
    assert(has_arg(p_bios_gpt.finn_install_args, "--need-bios-boot"));
}

void test_dualboot_free_space_and_parted_free_parser() {
    const std::string parted_out =
        "BYT;\n"
        "/dev/nvme0n1:512110190592B:nvme:512:512:gpt:Mac SSD:;\n"
        "1:20480B:314593279B:314572800B:fat32:EFI System Partition:boot, esp;\n"
        "2:314593280B:350000000000B:349685406720B:apfs:macOS:;\n"
        "1:350000000000B:512100000000B:162100000000B:free;\n";
    const auto regs = parse_parted_free_regions(parted_out);
    assert(regs.size() == 1);
    assert(regs[0].size_bytes == 162100000000ULL);

    // Simulate macOS APFS disk with 150 GiB unallocated free space + 300 MiB ESP.
    auto disks = parse_lsblk_json(kFixtureLsblk);
    disks[1].free_regions = regs;
    HostEnvironment uefi_env;
    uefi_env.firmware = FirmwareMode::UEFI;

    InstallRequest free_req;
    free_req.mode = InstallMode::DualBootFreeSpace;
    free_req.target_disk = "/dev/nvme0n1";
    free_req.confirmed = true;
    const auto free_plan = plan_install(free_req, disks, uefi_env);
    assert(free_plan.valid);
    assert(has_arg(free_plan.finn_install_args, "dualboot-free"));
    assert(has_arg(free_plan.finn_install_args, "--free-start-mb"));
    assert(has_arg(free_plan.finn_install_args, "/dev/nvme0n1p1"));
}

void test_mbr_esp_four_primary_limit_and_disk_ops() {
    constexpr const char* kMbrJson = R"json({
      "blockdevices": [
        {
          "name": "sdb",
          "path": "/dev/sdb",
          "size": 32000000000,
          "type": "disk",
          "pttype": "dos",
          "model": "Live Stick",
          "tran": "usb",
          "rm": true,
          "ro": false,
          "children": [
            {
              "name": "sdb1",
              "path": "/dev/sdb1",
              "size": 4000000000,
              "type": "part",
              "fstype": "vfat",
              "parttype": "0xef"
            }
          ]
        },
        {
          "name": "sda",
          "path": "/dev/sda",
          "size": 250000000000,
          "type": "disk",
          "pttype": "dos",
          "model": "SATA MBR SSD",
          "tran": "sata",
          "rm": false,
          "ro": false,
          "children": [
            {
              "name": "sda1",
              "path": "/dev/sda1",
              "size": 536870912,
              "type": "part",
              "fstype": "vfat",
              "parttype": "0xc"
            },
            {
              "name": "sda2",
              "path": "/dev/sda2",
              "size": 200000000000,
              "type": "part",
              "fstype": "btrfs",
              "label": "LinuxRoot"
            }
          ]
        },
        {
          "name": "sdc",
          "path": "/dev/sdc",
          "size": 250000000000,
          "type": "disk",
          "pttype": "dos",
          "model": "Full 4-Primary MBR",
          "tran": "sata",
          "rm": false,
          "ro": false,
          "children": [
            {"name": "sdc1", "path": "/dev/sdc1", "size": 104857600, "type": "part", "fstype": "ntfs", "parttype": "0x7"},
            {"name": "sdc2", "path": "/dev/sdc2", "size": 150000000000, "type": "part", "fstype": "ntfs", "parttype": "0x7"},
            {"name": "sdc3", "path": "/dev/sdc3", "size": 50000000000, "type": "part", "fstype": "ext4", "parttype": "0x83"},
            {"name": "sdc4", "path": "/dev/sdc4", "size": 524288000, "type": "part", "fstype": "ntfs", "parttype": "0x27"}
          ]
        }
      ]
    })json";

    const auto disks = parse_lsblk_json(kMbrJson, "/dev/sdb");
    assert(disks.size() == 3);
    assert(disks[0].is_live_medium);
    assert(disks[1].table_kind == PartitionTableKind::MBR);
    assert(disks[1].partitions[0].is_esp);
    assert(disks[1].esp_partition_path == "/dev/sda1");

    // Disk Utility MUST refuse Live USB.
    assert(!plan_disk_wipe_gpt(disks[0]).valid);
    assert(!plan_disk_wipe_mbr(disks[0]).valid);
    assert(!plan_disk_create_partition(disks[0], 32, false, "ext4").valid);
    assert(!plan_disk_delete_partition(disks[0], disks[0].partitions[0]).valid);

    // Disk Utility on MBR disk MUST use parted (NEVER sgdisk, which converts MBR to GPT!).
    const auto mbr_del = plan_disk_delete_partition(disks[1], disks[1].partitions[1]);
    assert(mbr_del.valid && mbr_del.program == "parted");
    assert(mbr_del.args.size() == 4 && mbr_del.args[2] == "rm" && mbr_del.args[3] == "2");

    const auto mbr_create = plan_disk_create_partition(disks[1], 32, false, "ext4");
    assert(mbr_create.valid && mbr_create.program == "bash");
    assert(mbr_create.args[1].find("parted -s") != std::string::npos);
    assert(mbr_create.args[1].find("sgdisk") == std::string::npos);

    // Full 4-primary MBR disk with NO extended container MUST refuse DualBootResize BEFORE shrinking!
    HostEnvironment bios_env;
    bios_env.firmware = FirmwareMode::LegacyBios;
    InstallRequest full_mbr_req;
    full_mbr_req.mode = InstallMode::DualBootResize;
    full_mbr_req.resize_partition = "/dev/sdc2";
    full_mbr_req.resize_new_size_mb = 100000;
    full_mbr_req.confirmed = true;
    const auto full_mbr_plan = plan_install(full_mbr_req, disks, bios_env);
    assert(!full_mbr_plan.valid);
    assert(full_mbr_plan.error.find("4 primary") != std::string::npos);

    // GPT disk ops use sgdisk cleanly.
    const auto gpt_disks = parse_lsblk_json(kFixtureLsblk);
    const auto gpt_wipe = plan_disk_wipe_gpt(gpt_disks[1]);
    assert(gpt_wipe.valid && gpt_wipe.args[1].find("sgdisk --zap-all") != std::string::npos);
    const auto mbr_wipe = plan_disk_wipe_mbr(gpt_disks[1]);
    assert(mbr_wipe.valid && mbr_wipe.args[1].find("mklabel msdos") != std::string::npos);
    const auto gpt_del = plan_disk_delete_partition(gpt_disks[1], gpt_disks[1].partitions[3]);
    assert(gpt_del.valid && gpt_del.program == "sgdisk" && gpt_del.args[1] == "4");
    const auto gpt_create = plan_disk_create_partition(gpt_disks[1], 0, false, "efi");
    assert(gpt_create.valid && gpt_create.args[4] == "0:0:+512M" && gpt_create.args[5] == "ef00");
}

void test_windows_bios_mbr_3_primary_dualboot_and_bls() {
    constexpr const char* kWinBiosMbr = R"json({
      "blockdevices": [
        {
          "name": "sda",
          "path": "/dev/sda",
          "size": 256000000000,
          "type": "disk",
          "pttype": "dos",
          "model": "Windows BIOS MBR SSD",
          "tran": "sata",
          "rm": false,
          "ro": false,
          "children": [
            {"name": "sda1", "path": "/dev/sda1", "size": 104857600, "type": "part", "fstype": "ntfs", "label": "System Reserved", "parttype": "0x7"},
            {"name": "sda2", "path": "/dev/sda2", "size": 240000000000, "type": "part", "fstype": "ntfs", "label": "Windows", "parttype": "0x7"},
            {"name": "sda3", "path": "/dev/sda3", "size": 524288000, "type": "part", "fstype": "ntfs", "label": "Recovery", "parttype": "0x27"}
          ]
        }
      ]
    })json";

    const auto disks = parse_lsblk_json(kWinBiosMbr);
    assert(disks.size() == 1);
    assert(disks[0].primary_part_count == 3);
    assert(disks[0].esp_partition_path.empty());

    // Must pick 240 GB Windows C:, NEVER System Reserved or Recovery.
    const PartitionInfo* best = find_best_shrinkable_partition(disks[0]);
    assert(best != nullptr && best->path == "/dev/sda2");

    HostEnvironment bios_env;
    bios_env.firmware = FirmwareMode::LegacyBios;
    InstallRequest db;
    db.mode = InstallMode::DualBootResize;
    db.resize_partition = best->path;
    db.resize_new_size_mb = 160000;
    db.confirmed = true;
    const auto plan = plan_install(db, disks, bios_env);
    assert(plan.valid);
    assert(plan.bootloader == BootloaderStrategy::GrubBios);
    assert(!has_arg(plan.finn_install_args, "--esp-part"));

    // BLS & loader.conf generation
    const std::string lc = generate_loader_conf("finnos.conf", 3);
    assert(lc.find("default finnos.conf") != std::string::npos);
    assert(lc.find("auto-entries yes") != std::string::npos);
    const std::string bls = generate_bls_entry(
        "FinnOS", "1.0", "/vmlinuz-linux", {"/intel-ucode.img", "/initramfs-linux.img"},
        "root=UUID=abc rw quiet splash");
    assert(bls.find("initrd  /intel-ucode.img") != std::string::npos);
    assert(bls.find("initrd  /initramfs-linux.img") != std::string::npos);
}

void test_reinstall_and_bootloader_repair() {
    const auto disks = parse_lsblk_json(kFixtureLsblk);
    HostEnvironment uefi_env;
    uefi_env.firmware = FirmwareMode::UEFI;

    InstallRequest rein;
    rein.mode = InstallMode::ReinstallPreserveData;
    rein.target_partition = "/dev/nvme0n1p4";
    rein.confirmed = true;
    const auto rp = plan_install(rein, disks, uefi_env);
    assert(rp.valid);
    assert(has_arg(rp.finn_install_args, "reinstall"));
    assert(has_arg(rp.finn_install_args, "--offline-only"));

    // NTFS cannot be used for FinnOS ReinstallPreserveData.
    InstallRequest bad_rein = rein;
    bad_rein.target_partition = "/dev/nvme0n1p2";
    assert(!plan_install(bad_rein, disks, uefi_env).valid);

    InstallRequest br;
    br.mode = InstallMode::RepairBootloader;
    br.target_partition = "/dev/nvme0n1p4";
    br.confirmed = true;
    const auto bp = plan_install(br, disks, uefi_env);
    assert(bp.valid);
    assert(has_arg(bp.finn_install_args, "repair-boot"));
    assert(bp.esp_action == EspAction::UseExistingAtBoot);
}

}  // namespace

int main() {
    test_lsblk_parsing_and_os_detection();
    test_probe_os_directory();
    test_sha512_and_offline_image_integrity();
    test_clean_erase_gpt_mbr_ab_and_offline_only();
    test_windows11_100m_esp_and_missing_efi_auto_remediation();
    test_dualboot_free_space_and_parted_free_parser();
    test_mbr_esp_four_primary_limit_and_disk_ops();
    test_windows_bios_mbr_3_primary_dualboot_and_bls();
    test_reinstall_and_bootloader_repair();
    std::puts("test_recovery: ok");
    return 0;
}
