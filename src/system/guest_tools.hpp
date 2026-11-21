// Pure C++20 hypervisor detection and guest-tools probe for VMware Tools
// (open-vm-tools), VirtualBox Guest Additions (virtualbox-guest-utils-nox),
// and QEMU/KVM Guest Agent (qemu-guest-agent).
// Never fabricates a hypervisor or installed tool: every field is read from
// real sysfs, /proc, mounted filesystems, and installed executables.

#ifndef FINN_SYSTEM_GUEST_TOOLS_HPP
#define FINN_SYSTEM_GUEST_TOOLS_HPP

#include <string>
#include <vector>

namespace finn::sys {

enum class HypervisorKind {
    kNone,
    kVMware,
    kVirtualBox,
    kQemuKvm,
    kHyperV,
    kOther,
};

struct GuestToolSuiteStatus {
    std::string id;             // "vmware", "virtualbox", "qemu"
    std::string name;           // Human-readable product name
    std::string package_name;   // pacman package (no GTK3/Xorg dependency)
    std::string binary_path;    // Resolved daemon executable, or empty
    std::string service_name;   // systemd unit name
    bool installed = false;     // Daemon binary found on disk
    bool service_active = false;
    bool kernel_driver_loaded = false;
    std::vector<std::string> loaded_modules;
    bool shared_folders_ready = false;
    std::string shared_folders_path;
    std::string detected_cd_path;  // Mounted Guest Tools / Additions CD-ROM if present
    std::string summary;
};

struct GuestToolsProbePaths {
    std::string dmi_dir = "/sys/class/dmi/id";
    std::string pci_dir = "/sys/bus/pci/devices";
    std::string modules_dir = "/sys/module";
    std::string cpuinfo_path = "/proc/cpuinfo";
    std::string mounts_path = "/proc/mounts";
    std::vector<std::string> bin_dirs = {"/usr/bin", "/usr/local/bin", "/bin"};
    std::vector<std::string> media_dirs = {"/run/media", "/media", "/mnt"};
    // Optional override for unit tests; when true, skips shelling out to systemctl.
    bool skip_systemctl = false;
    std::vector<std::string> active_services_override;
};

struct GuestEnvironmentStatus {
    HypervisorKind hypervisor = HypervisorKind::kNone;
    std::string hypervisor_id;     // "none", "vmware", "virtualbox", "qemu", "hyperv", "other"
    std::string hypervisor_label;  // e.g. "VMware", "VirtualBox", "Physical Hardware"
    std::string dmi_vendor;
    std::string dmi_product;
    bool cpu_hypervisor_flag = false;
    GuestToolSuiteStatus vmware;
    GuestToolSuiteStatus virtualbox;
    GuestToolSuiteStatus qemu;
};

const char* hypervisor_id(HypervisorKind kind);
const char* hypervisor_label(HypervisorKind kind);

GuestEnvironmentStatus probe_guest_environment(
    const GuestToolsProbePaths& paths = {});

}  // namespace finn::sys

#endif
