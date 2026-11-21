#include "system/guest_tools.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace fs = std::filesystem;

namespace finn::sys {
namespace {

std::string trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

bool read_file_text(const fs::path& p, std::string* out) {
    std::ifstream in(p);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

std::string find_executable(const std::vector<std::string>& dirs, std::string_view name) {
    std::error_code ec;
    for (const auto& dir : dirs) {
        const fs::path candidate = fs::path(dir) / std::string(name);
        if (fs::is_regular_file(candidate, ec) && ::access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return {};
}

bool module_loaded(const std::string& modules_dir, std::string_view mod) {
    std::error_code ec;
    return fs::is_directory(fs::path(modules_dir) / std::string(mod), ec);
}

bool has_pci_vendor(const std::string& pci_dir, std::string_view vendor_hex_lower) {
    std::error_code ec;
    if (!fs::is_directory(pci_dir, ec)) return false;
    for (const auto& entry : fs::directory_iterator(pci_dir, ec)) {
        std::string v;
        if (!read_file_text(entry.path() / "vendor", &v)) continue;
        if (to_lower(trim(v)) == vendor_hex_lower) return true;
    }
    return false;
}

bool cpuinfo_has_hypervisor(const std::string& cpuinfo_path) {
    std::string text;
    if (!read_file_text(cpuinfo_path, &text)) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("flags", 0) != 0 && line.rfind("Features", 0) != 0) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::istringstream tokens(line.substr(colon + 1));
        std::string tok;
        while (tokens >> tok) {
            if (tok == "hypervisor") return true;
        }
    }
    return false;
}

bool is_systemd_service_active(const std::string& unit,
                               const GuestToolsProbePaths& paths) {
    if (paths.skip_systemctl) {
        return std::find(paths.active_services_override.begin(),
                         paths.active_services_override.end(),
                         unit) != paths.active_services_override.end();
    }
    const std::string systemctl = find_executable(paths.bin_dirs, "systemctl");
    if (systemctl.empty()) return false;

    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Silence child stdout/stderr before exec.
        if (FILE* devnull = std::fopen("/dev/null", "w")) {
            ::dup2(fileno(devnull), STDOUT_FILENO);
            ::dup2(fileno(devnull), STDERR_FILENO);
            std::fclose(devnull);
        }
        ::execl(systemctl.c_str(), "systemctl", "is-active", "--quiet", unit.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool has_mount_fstype_or_target(const std::string& mounts_path,
                                std::string_view fstype_substr,
                                std::string_view target_prefix,
                                std::string* matched_target) {
    std::string text;
    if (!read_file_text(mounts_path, &text)) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string dev, target, fstype;
        if (!(ls >> dev >> target >> fstype)) continue;
        const bool fs_ok = fstype_substr.empty() ||
                           contains_ci(fstype, fstype_substr);
        const bool tgt_ok = target_prefix.empty() ||
                            target.rfind(target_prefix, 0) == 0;
        if (fs_ok && tgt_ok) {
            if (matched_target) *matched_target = target;
            return true;
        }
    }
    return false;
}

std::string find_mounted_guest_cd(const std::vector<std::string>& media_dirs,
                                  const std::vector<std::string>& marker_prefixes) {
    std::error_code ec;
    auto dir_matches = [&](const fs::path& dir) -> bool {
        if (!fs::is_directory(dir, ec)) return false;
        for (const auto& item : fs::directory_iterator(dir, ec)) {
            const std::string fname = item.path().filename().string();
            for (const auto& prefix : marker_prefixes) {
                if (fname.rfind(prefix, 0) == 0) return true;
            }
        }
        return false;
    };

    for (const auto& base : media_dirs) {
        const fs::path root(base);
        if (!fs::is_directory(root, ec)) continue;
        if (dir_matches(root)) return root.string();
        for (const auto& lvl1 : fs::directory_iterator(root, ec)) {
            if (!lvl1.is_directory(ec)) continue;
            if (dir_matches(lvl1.path())) return lvl1.path().string();
            for (const auto& lvl2 : fs::directory_iterator(lvl1.path(), ec)) {
                if (!lvl2.is_directory(ec)) continue;
                if (dir_matches(lvl2.path())) return lvl2.path().string();
            }
        }
    }
    return {};
}

std::string join_modules(const std::vector<std::string>& mods) {
    std::string out;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (i > 0) out += ", ";
        out += mods[i];
    }
    return out;
}

}  // namespace

const char* hypervisor_id(HypervisorKind kind) {
    switch (kind) {
        case HypervisorKind::kNone:       return "none";
        case HypervisorKind::kVMware:     return "vmware";
        case HypervisorKind::kVirtualBox: return "virtualbox";
        case HypervisorKind::kQemuKvm:    return "qemu";
        case HypervisorKind::kHyperV:     return "hyperv";
        case HypervisorKind::kOther:      return "other";
    }
    return "none";
}

const char* hypervisor_label(HypervisorKind kind) {
    switch (kind) {
        case HypervisorKind::kNone:       return "Physical Hardware";
        case HypervisorKind::kVMware:     return "VMware";
        case HypervisorKind::kVirtualBox: return "VirtualBox";
        case HypervisorKind::kQemuKvm:    return "QEMU / KVM";
        case HypervisorKind::kHyperV:     return "Hyper-V";
        case HypervisorKind::kOther:      return "Virtual Machine";
    }
    return "Physical Hardware";
}

GuestEnvironmentStatus probe_guest_environment(const GuestToolsProbePaths& paths) {
    GuestEnvironmentStatus env;

    std::string sys_vendor, product_name, board_vendor;
    (void)read_file_text(fs::path(paths.dmi_dir) / "sys_vendor", &sys_vendor);
    (void)read_file_text(fs::path(paths.dmi_dir) / "product_name", &product_name);
    (void)read_file_text(fs::path(paths.dmi_dir) / "board_vendor", &board_vendor);
    env.dmi_vendor = trim(sys_vendor);
    env.dmi_product = trim(product_name);
    env.cpu_hypervisor_flag = cpuinfo_has_hypervisor(paths.cpuinfo_path);

    const std::string combined_dmi = env.dmi_vendor + " " + env.dmi_product + " " + trim(board_vendor);
    const bool pci_vmware = has_pci_vendor(paths.pci_dir, "0x15ad");
    const bool pci_vbox = has_pci_vendor(paths.pci_dir, "0x80ee");
    const bool pci_virtio = has_pci_vendor(paths.pci_dir, "0x1af4");

    if (contains_ci(combined_dmi, "vmware") || pci_vmware) {
        env.hypervisor = HypervisorKind::kVMware;
    } else if (contains_ci(combined_dmi, "virtualbox") ||
               contains_ci(combined_dmi, "innotek") || pci_vbox) {
        env.hypervisor = HypervisorKind::kVirtualBox;
    } else if (contains_ci(combined_dmi, "qemu") ||
               contains_ci(combined_dmi, "kvm") ||
               contains_ci(combined_dmi, "bochs") || pci_virtio) {
        env.hypervisor = HypervisorKind::kQemuKvm;
    } else if (contains_ci(combined_dmi, "microsoft corporation") &&
               contains_ci(combined_dmi, "virtual machine")) {
        env.hypervisor = HypervisorKind::kHyperV;
    } else if (env.cpu_hypervisor_flag) {
        env.hypervisor = HypervisorKind::kOther;
    } else {
        env.hypervisor = HypervisorKind::kNone;
    }
    env.hypervisor_id = hypervisor_id(env.hypervisor);
    env.hypervisor_label = hypervisor_label(env.hypervisor);

    // 1. VMware Tools (open-vm-tools)
    {
        auto& vmw = env.vmware;
        vmw.id = "vmware";
        vmw.name = "VMware Tools (open-vm-tools)";
        vmw.package_name = "open-vm-tools";
        vmw.service_name = "vmtoolsd.service";
        vmw.binary_path = find_executable(paths.bin_dirs, "vmtoolsd");
        vmw.installed = !vmw.binary_path.empty();
        vmw.service_active = is_systemd_service_active(vmw.service_name, paths);
        for (const char* mod : {"vmwgfx", "vmw_vmci", "vmw_balloon", "vmw_vsock_vmci_transport"}) {
            if (module_loaded(paths.modules_dir, mod)) vmw.loaded_modules.emplace_back(mod);
        }
        vmw.kernel_driver_loaded = !vmw.loaded_modules.empty();

        std::string hgfs_target;
        if (has_mount_fstype_or_target(paths.mounts_path, "vmhgfs", "/mnt/hgfs", &hgfs_target) ||
            has_mount_fstype_or_target(paths.mounts_path, "vmhgfs", "", &hgfs_target)) {
            vmw.shared_folders_ready = true;
            vmw.shared_folders_path = hgfs_target;
        }
        vmw.detected_cd_path = find_mounted_guest_cd(
            paths.media_dirs, {"VMwareTools-", "vmware-tools-upgrader"});

        if (vmw.installed && vmw.service_active) {
            vmw.summary = "Active (vmtoolsd running";
            if (!vmw.loaded_modules.empty()) vmw.summary += ", modules: " + join_modules(vmw.loaded_modules);
            if (vmw.shared_folders_ready) vmw.summary += ", HGFS mounted at " + vmw.shared_folders_path;
            vmw.summary += ")";
        } else if (vmw.installed) {
            vmw.summary = (env.hypervisor == HypervisorKind::kVMware)
                              ? "Installed (vmtoolsd not active — click Enable or run finn-guest-tools enable vmware)"
                              : "Installed (auto-starts only on VMware guests)";
        } else if (!vmw.detected_cd_path.empty()) {
            vmw.summary = "VMware Tools CD detected at " + vmw.detected_cd_path +
                          " — install open-vm-tools for modern kernel support";
        } else {
            vmw.summary = "Not installed (open-vm-tools provides time sync, guest control, HGFS, and SVGA resize)";
        }
    }

    // 2. VirtualBox Guest Additions (virtualbox-guest-utils-nox)
    {
        auto& vb = env.virtualbox;
        vb.id = "virtualbox";
        vb.name = "VirtualBox Guest Additions";
        vb.package_name = "virtualbox-guest-utils-nox";
        vb.service_name = "vboxservice.service";
        vb.binary_path = find_executable(paths.bin_dirs, "VBoxService");
        vb.installed = !vb.binary_path.empty();
        vb.service_active = is_systemd_service_active(vb.service_name, paths);
        for (const char* mod : {"vboxguest", "vboxsf", "vboxvideo"}) {
            if (module_loaded(paths.modules_dir, mod)) vb.loaded_modules.emplace_back(mod);
        }
        vb.kernel_driver_loaded = !vb.loaded_modules.empty();

        std::string sf_target;
        if (has_mount_fstype_or_target(paths.mounts_path, "vboxsf", "", &sf_target)) {
            vb.shared_folders_ready = true;
            vb.shared_folders_path = sf_target;
        }
        vb.detected_cd_path = find_mounted_guest_cd(
            paths.media_dirs, {"VBoxLinuxAdditions.run"});

        if (vb.installed && vb.service_active) {
            vb.summary = "Active (vboxservice running";
            if (!vb.loaded_modules.empty()) vb.summary += ", modules: " + join_modules(vb.loaded_modules);
            if (vb.shared_folders_ready) vb.summary += ", shared folder at " + vb.shared_folders_path;
            vb.summary += ")";
        } else if (vb.installed) {
            vb.summary = (env.hypervisor == HypervisorKind::kVirtualBox)
                             ? "Installed (vboxservice not active — click Enable or run finn-guest-tools enable virtualbox)"
                             : "Installed (auto-starts only on VirtualBox guests)";
        } else if (!vb.detected_cd_path.empty()) {
            vb.summary = "Guest Additions CD detected at " + vb.detected_cd_path +
                         " — install virtualbox-guest-utils-nox or run finn-guest-tools install virtualbox";
        } else {
            vb.summary = "Not installed (virtualbox-guest-utils-nox provides time sync, vboxsf shared folders, and DRM resize without GTK3)";
        }
    }

    // 3. QEMU / KVM Guest Agent (qemu-guest-agent)
    {
        auto& q = env.qemu;
        q.id = "qemu";
        q.name = "QEMU / KVM Guest Agent";
        q.package_name = "qemu-guest-agent";
        q.service_name = "qemu-guest-agent.service";
        q.binary_path = find_executable(paths.bin_dirs, "qemu-ga");
        q.installed = !q.binary_path.empty();
        q.service_active = is_systemd_service_active(q.service_name, paths);
        for (const char* mod : {"virtio_gpu", "virtio_balloon", "virtio_console"}) {
            if (module_loaded(paths.modules_dir, mod)) q.loaded_modules.emplace_back(mod);
        }
        q.kernel_driver_loaded = !q.loaded_modules.empty();

        if (q.installed && q.service_active) {
            q.summary = "Active (qemu-ga running)";
        } else if (q.installed) {
            q.summary = (env.hypervisor == HypervisorKind::kQemuKvm)
                            ? "Installed (activates automatically when QEMU virtio-serial guest agent channel is attached)"
                            : "Installed (activates only on QEMU/KVM guests with virtio-serial channel)";
        } else {
            q.summary = "Not installed (qemu-guest-agent provides host quiesce, shutdown, and IP reporting)";
        }
    }

    return env;
}

}  // namespace finn::sys
