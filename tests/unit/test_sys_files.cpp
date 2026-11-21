#include "system/guest_tools.hpp"
#include "system/sys_files.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,    \
                         __LINE__, #condition);                            \
            return false;                                                  \
        }                                                                  \
    } while (false)

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("finn-sys-test-" + std::to_string(::getpid()));
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    std::string dir_str() const { return path_.string(); }
    void write(std::string_view rel, std::string_view content) const {
        const auto file = path_ / std::string(rel);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file);
        out << content;
    }

private:
    std::filesystem::path path_;
};

bool test_settings_roundtrip() {
    const std::string raw =
        "dark=1\n"
        "accent=#4F46E5\n"
        "avatar_color=mint\n"
        "dock_pins=settings,files,terminal\n"
        "reduced_motion=1\n"
        "scale=1.25\n";
    const auto s = finn::sys::parse_settings(raw);
    CHECK(s.dark);
    CHECK(s.accent == "#4F46E5");
    CHECK(s.avatar_color == "mint");
    CHECK(s.reduced_motion);
    CHECK(s.dock_pins.size() == 3);
    const auto s2 = finn::sys::parse_settings(finn::sys::serialize_settings(s));
    CHECK(s2.dark);
    CHECK(s2.avatar_color == "mint");
    CHECK(s2.reduced_motion);
    return true;
}

bool test_math_eval() {
    CHECK(finn::sys::evaluate_math_expression("2 + 3 * 4") == "14");
    CHECK(finn::sys::evaluate_math_expression("(10 + 2) / 3") == "4");
    CHECK(finn::sys::evaluate_math_expression("1 / 0").empty());
    CHECK(finn::sys::evaluate_math_expression("hello world").empty());
    return true;
}

bool test_honest_sysfs_readers() {
    const TempDir empty_sys;
    CHECK(!finn::sys::read_backlight(empty_sys.dir_str()).available);
    CHECK(!finn::sys::read_battery(empty_sys.dir_str()).available);

    const TempDir real_sys;
    real_sys.write("intel_backlight/brightness", "400\n");
    real_sys.write("intel_backlight/max_brightness", "800\n");
    const auto bl = finn::sys::read_backlight(real_sys.dir_str());
    CHECK(bl.available);
    CHECK(bl.percent == 50);

    real_sys.write("BAT0/type", "Battery\n");
    real_sys.write("BAT0/capacity", "84\n");
    real_sys.write("BAT0/status", "Discharging\n");
    const auto bat = finn::sys::read_battery(real_sys.dir_str());
    CHECK(bat.available);
    CHECK(bat.capacity_percent == 84);
    CHECK(!bat.charging);
    return true;
}

bool test_proc_parsers() {
    const auto mem = finn::sys::parse_meminfo(
        "MemTotal:        8000000 kB\n"
        "MemFree:         2000000 kB\n"
        "MemAvailable:    5000000 kB\n");
    CHECK(mem.total_kib == 8000000ULL);
    CHECK(mem.available_kib == 5000000ULL);
    CHECK(mem.used_kib == 3000000ULL);

    const auto cpus = finn::sys::parse_proc_stat(
        "cpu  100 10 40 850 0 0 0 0 0 0\n"
        "cpu0 50 5 20 425 0 0 0 0 0 0\n");
    CHECK(cpus.size() == 2);
    CHECK(cpus[0].name == "cpu");
    CHECK(cpus[0].total == 1000ULL);
    CHECK(cpus[0].idle == 850ULL);
    return true;
}

bool test_guest_tools_probe() {
    // 1. Bare-metal environment: no hypervisor, no guest binaries, no modules.
    {
        const TempDir bare;
        bare.write("dmi/sys_vendor", "LENOVO\n");
        bare.write("dmi/product_name", "ThinkPad\n");
        bare.write("proc/cpuinfo", "flags\t: fpu vme de pse tsc\n");
        bare.write("proc/mounts", "/dev/sda2 / ext4 rw 0 0\n");
        finn::sys::GuestToolsProbePaths p;
        p.dmi_dir = bare.dir_str() + "/dmi";
        p.pci_dir = bare.dir_str() + "/pci";
        p.modules_dir = bare.dir_str() + "/modules";
        p.cpuinfo_path = bare.dir_str() + "/proc/cpuinfo";
        p.mounts_path = bare.dir_str() + "/proc/mounts";
        p.bin_dirs = {bare.dir_str() + "/bin"};
        p.media_dirs = {bare.dir_str() + "/media"};
        p.skip_systemctl = true;
        const auto env = finn::sys::probe_guest_environment(p);
        CHECK(env.hypervisor == finn::sys::HypervisorKind::kNone);
        CHECK(env.hypervisor_id == "none");
        CHECK(!env.vmware.installed);
        CHECK(!env.virtualbox.installed);
        CHECK(!env.qemu.installed);
    }

    // 2. VMware guest with open-vm-tools active, vmwgfx loaded, and HGFS mounted.
    {
        const TempDir vmw;
        vmw.write("dmi/sys_vendor", "VMware, Inc.\n");
        vmw.write("dmi/product_name", "VMware Virtual Platform\n");
        vmw.write("pci/0000:00:0f.0/vendor", "0x15ad\n");
        vmw.write("modules/vmwgfx/initstate", "live\n");
        vmw.write("modules/vmw_balloon/initstate", "live\n");
        vmw.write("proc/cpuinfo", "flags\t: fpu sse4_2 hypervisor\n");
        vmw.write("proc/mounts", "vmhgfs-fuse /mnt/hgfs fuse.vmhgfs-fuse rw 0 0\n");
        vmw.write("bin/vmtoolsd", "#!/bin/sh\n");
        ::chmod((vmw.dir_str() + "/bin/vmtoolsd").c_str(), 0755);
        finn::sys::GuestToolsProbePaths p;
        p.dmi_dir = vmw.dir_str() + "/dmi";
        p.pci_dir = vmw.dir_str() + "/pci";
        p.modules_dir = vmw.dir_str() + "/modules";
        p.cpuinfo_path = vmw.dir_str() + "/proc/cpuinfo";
        p.mounts_path = vmw.dir_str() + "/proc/mounts";
        p.bin_dirs = {vmw.dir_str() + "/bin"};
        p.media_dirs = {vmw.dir_str() + "/media"};
        p.skip_systemctl = true;
        p.active_services_override = {"vmtoolsd.service"};
        const auto env = finn::sys::probe_guest_environment(p);
        CHECK(env.hypervisor == finn::sys::HypervisorKind::kVMware);
        CHECK(env.vmware.installed);
        CHECK(env.vmware.service_active);
        CHECK(env.vmware.kernel_driver_loaded);
        CHECK(env.vmware.shared_folders_ready);
        CHECK(env.vmware.shared_folders_path == "/mnt/hgfs");
    }

    // 3. VirtualBox guest with mounted Guest Additions CD-ROM and vboxguest loaded.
    {
        const TempDir vb;
        vb.write("dmi/sys_vendor", "innotek GmbH\n");
        vb.write("dmi/product_name", "VirtualBox\n");
        vb.write("pci/0000:00:02.0/vendor", "0x80ee\n");
        vb.write("modules/vboxguest/initstate", "live\n");
        vb.write("media/VBox_GAs_7.2/VBoxLinuxAdditions.run", "#!/bin/sh\n");
        vb.write("proc/mounts", "share /media/sf_share vboxsf rw 0 0\n");
        vb.write("bin/VBoxService", "#!/bin/sh\n");
        ::chmod((vb.dir_str() + "/bin/VBoxService").c_str(), 0755);
        finn::sys::GuestToolsProbePaths p;
        p.dmi_dir = vb.dir_str() + "/dmi";
        p.pci_dir = vb.dir_str() + "/pci";
        p.modules_dir = vb.dir_str() + "/modules";
        p.cpuinfo_path = vb.dir_str() + "/proc/cpuinfo";
        p.mounts_path = vb.dir_str() + "/proc/mounts";
        p.bin_dirs = {vb.dir_str() + "/bin"};
        p.media_dirs = {vb.dir_str() + "/media"};
        p.skip_systemctl = true;
        p.active_services_override = {"vboxservice.service"};
        const auto env = finn::sys::probe_guest_environment(p);
        CHECK(env.hypervisor == finn::sys::HypervisorKind::kVirtualBox);
        CHECK(env.virtualbox.installed);
        CHECK(env.virtualbox.service_active);
        CHECK(env.virtualbox.kernel_driver_loaded);
        CHECK(env.virtualbox.shared_folders_ready);
        CHECK(!env.virtualbox.detected_cd_path.empty());
    }
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_settings_roundtrip();
    ok &= test_math_eval();
    ok &= test_honest_sysfs_readers();
    ok &= test_proc_parsers();
    ok &= test_guest_tools_probe();
    if (!ok) return 1;
    std::puts("test_sys_files: ok");
    return 0;
}
