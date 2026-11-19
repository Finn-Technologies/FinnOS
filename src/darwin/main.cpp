// finn-darwin: FinnOS clean-room macOS compatibility runtime & installer CLI.
// Loads unmodified 64-bit Mach-O executables and .app bundles, translates
// libSystem/libobjc/Foundation/AppKit onto FinnOS, and installs .dmg/.pkg/.app.

#include "darwin/appkit_bridge.hpp"
#include "darwin/installer.hpp"
#include "darwin/macho_loader.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

void print_usage() {
    std::cout
        << "finn-darwin — FinnOS clean-room macOS compatibility runtime\n"
        << "Usage:\n"
        << "  finn-darwin status\n"
        << "  finn-darwin diagnose <binary | App.app>\n"
        << "  finn-darwin install <App.app | Installer.pkg | Disk.dmg>\n"
        << "  finn-darwin run [--allow-missing] <binary | App.app> [args...]\n";
}

int cmd_status() {
    struct utsname u{};
    const std::string arch = (::uname(&u) == 0) ? u.machine : "unknown";
    const auto apps = finn::darwin::discover_installed_macos_apps();
    std::cout << "finn-darwin (" << arch << ")\n"
              << "  loader:     64-bit Mach-O / Universal (LC_DYLD_CHAINED_FIXUPS + LC_DYLD_INFO)\n"
              << "  runtimes:   libSystem.B, libobjc.A (ABI v2 + msgSend asm), CoreFoundation, Foundation\n"
              << "  appkit:     "
              << (finn::darwin::appkit_qt_backend_available()
                      ? "Qt6/Wayland bridge active (basic NSWindow/NSView/NSButton/NSTextField/NSAlert)\n"
                      : "headless CLI build (AppKit Qt6 bridge compiles on Linux target)\n")
              << "  limits:     SwiftUI, Metal, WebKit, CoreData, App Store/iCloud not implemented\n"
              << "  apps_dir:   " << finn::darwin::default_macos_apps_dir() << "\n"
              << "  installed:  " << apps.size() << " .app bundle(s)\n";
    for (const auto& a : apps) {
        std::cout << "    - " << a.name << " (" << a.bundle_id << ") -> " << a.bundle_path << "\n";
    }
    return 0;
}

int cmd_diagnose(const std::string& target) {
    finn::darwin::MachoLoader loader;
    const auto d = loader.diagnose(target);
    if (!d.valid) {
        std::cerr << "finn-darwin: diagnose failed: " << d.error << "\n";
        return 1;
    }
    std::cout << "Target:          " << target << "\n"
              << "Host CPU:        " << d.host_arch << "\n"
              << "Selected slice:  " << d.selected_arch
              << (d.arch_compatible ? " (compatible)\n" : " (incompatible)\n")
              << "ObjC classes:    " << d.objc_class_count << "\n"
              << "Linked dylibs:   " << d.linked_dylibs.size() << "\n";
    for (const auto& lib : d.linked_dylibs) {
        std::cout << "  - " << lib << "\n";
    }
    std::cout << "Imported syms:   " << d.imported_symbols.size() << "\n"
              << "Missing syms:    " << d.missing_symbols.size() << "\n";
    const std::size_t show = std::min<std::size_t>(d.missing_symbols.size(), 25);
    for (std::size_t i = 0; i < show; ++i) {
        std::cout << "  ! " << d.missing_symbols[i] << "\n";
    }
    if (d.missing_symbols.size() > show) {
        std::cout << "  ... and " << (d.missing_symbols.size() - show) << " more\n";
    }
    std::cout << "Verdict:         " << d.summary << "\n";
    return d.runnable ? 0 : 2;
}

int cmd_install(const std::string& target) {
    finn::darwin::InstallResult res;
    std::error_code ec;
    if (fs::is_directory(target, ec) && ends_with_ci(target, ".app")) {
        res = finn::darwin::install_app_bundle(target);
    } else if (ends_with_ci(target, ".pkg") || ends_with_ci(target, ".mpkg")) {
        res = finn::darwin::install_pkg_native(target);
    } else if (ends_with_ci(target, ".dmg")) {
        res = finn::darwin::install_dmg_package(target);
    } else {
        std::cerr << "finn-darwin: unsupported macOS package format: " << target << "\n";
        return 1;
    }
    for (const auto& line : res.log) std::cout << line << "\n";
    if (!res.ok) {
        std::cerr << "finn-darwin: install failed: " << res.error << "\n";
        return 1;
    }
    for (const auto& app : res.installed_apps) {
        std::cout << "Ready: " << app.name << " (" << app.id << ")\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(argv[1], "--version") == 0) {
        std::puts("finn-darwin 0.1.0");
        return 0;
    }

    const std::string cmd = argv[1];
    if (cmd == "status") return cmd_status();
    if (cmd == "diagnose" && argc >= 3) return cmd_diagnose(argv[2]);
    if (cmd == "install" && argc >= 3) return cmd_install(argv[2]);

    if (cmd == "run" && argc >= 3) {
        bool allow_missing = false;
        int idx = 2;
        if (std::strcmp(argv[idx], "--allow-missing") == 0) {
            allow_missing = true;
            ++idx;
        }
        if (idx >= argc) {
            print_usage();
            return 1;
        }
        const std::string target = argv[idx];
        finn::darwin::MachoLoader loader;
        std::string err;
        const int rc = loader.load_and_run(target, argc - idx, argv + idx, &err, allow_missing);
        if (!err.empty()) {
            std::cerr << "finn-darwin: " << err << "\n";
        }
        return rc;
    }

    // Convenience: `finn-darwin /path/to/App.app` runs directly
    if (cmd[0] != '-') {
        finn::darwin::MachoLoader loader;
        std::string err;
        const int rc = loader.load_and_run(cmd, argc - 1, argv + 1, &err, false);
        if (!err.empty()) std::cerr << "finn-darwin: " << err << "\n";
        return rc;
    }

    print_usage();
    return 1;
}
