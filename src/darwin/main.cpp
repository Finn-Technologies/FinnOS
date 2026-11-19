// finn-darwin: FinnOS clean-room macOS compatibility runtime & installer CLI.
// Loads unmodified 64-bit Mach-O executables and .app bundles, translates
// libSystem/libobjc/Foundation/CoreGraphics/QuartzCore/Swift core/AppKit onto
// FinnOS, and installs from HTTP(S) URLs, .dmg, .pkg, .zip, or .app bundles.

#include "darwin/appkit_bridge.hpp"
#include "darwin/framework_coverage.hpp"
#include "darwin/installer.hpp"
#include "darwin/macho_loader.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "finn-darwin — FinnOS clean-room macOS compatibility runtime\n"
        << "Usage:\n"
        << "  finn-darwin status\n"
        << "  finn-darwin list\n"
        << "  finn-darwin diagnose <binary | App.app>\n"
        << "  finn-darwin install <http(s)://... | .dmg | .pkg | .zip | .app>\n"
        << "  finn-darwin uninstall <id | bundle-id | name | App.app>\n"
        << "  finn-darwin run [--allow-missing] <binary | App.app> [args...]\n";
}

int cmd_status() {
    struct utsname u{};
    const std::string machine = (::uname(&u) == 0) ? u.machine : "unknown";
    const auto apps = finn::darwin::discover_installed_macos_apps();
    std::cout << "finn-darwin (" << machine << ")\n"
              << "  loader:     64-bit Mach-O / Universal (LC_DYLD_CHAINED_FIXUPS + LC_DYLD_INFO)\n"
              << "  runtimes:   libSystem.B, libobjc.A (ABI v2 + msgSend asm), CoreFoundation,\n"
              << "              Foundation (JSON/UserDefaults/FileManager), CoreGraphics (RGBA8),\n"
              << "              QuartzCore (CALayer), Swift core ABI (HeapObject/ARC/once)\n"
              << "  appkit:     "
              << (finn::darwin::appkit_qt_backend_available()
                      ? "Qt6/Wayland bridge active (NSWindow/NSMenu/NSButton/NSTextView/NSScrollView/NSSlider/NSOpenPanel/NSTimer)\n"
                      : "headless CLI build (AppKit Qt6 bridge compiles on Linux target)\n")
              << "  installer:  HTTP(S) direct download, .dmg (UDIF), .pkg (XAR+cpio), .zip (zlib), .app\n"
              << "  limits:     the following are reported honestly, never faked:\n"
               << finn::darwin::coverage_report()
              << "  apps_dir:   " << finn::darwin::default_macos_apps_dir() << "\n"
              << "  installed:  " << apps.size() << " .app bundle(s)\n";
    for (const auto& a : apps) {
        std::cout << "    - " << a.name << " (" << a.bundle_id << ") -> " << a.bundle_path << "\n";
    }
    return 0;
}

int cmd_list() {
    const auto apps = finn::darwin::discover_installed_macos_apps();
    if (apps.empty()) {
        std::cout << "No macOS .app bundles installed in "
                  << finn::darwin::default_macos_apps_dir() << "\n";
        return 0;
    }
    for (const auto& a : apps) {
        std::cout << a.id << "\t" << a.name;
        if (!a.version.empty()) std::cout << " " << a.version;
        if (!a.bundle_id.empty()) std::cout << " (" << a.bundle_id << ")";
        std::cout << "\n  bundle: " << a.bundle_path << "\n";
        if (!a.icon_path.empty()) std::cout << "  icon:   " << a.icon_path << "\n";
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

    if (!d.framework_gaps.empty()) {
        std::cout << "Unimplemented frameworks linked by this binary:\n";
        for (const auto& g : d.framework_gaps) {
            std::cout << "  ! " << g.name << (g.linked ? " (linked)" : " (imported)")
                      << " — " << g.reason << "\n";
        }
    }
    if (!d.symbol_gaps.empty()) {
        std::cout << "Explained missing symbols:\n";
        for (const auto& g : d.symbol_gaps) {
            std::cout << "  ! " << g.name << " — " << g.reason << "\n";
        }
    }

    // diagnose is the pre-flight authority: it must print every missing symbol,
    // not a sample. Truncating here contradicted the documented contract and hid
    // the exact ABI gap the caller needs to see.
    std::cout << "All missing symbols:\n";
    for (const auto& sym : d.missing_symbols) {
        std::cout << "  ! " << sym << "\n";
    }
    std::cout << "Verdict:         " << d.summary << "\n";
    return d.runnable ? 0 : 2;
}

int cmd_install(const std::string& target) {
    const finn::darwin::InstallResult res = finn::darwin::install_from_url_or_path(target);
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

int cmd_uninstall(const std::string& query) {
    std::string removed;
    std::string err;
    if (!finn::darwin::uninstall_macos_app(query, &removed, &err)) {
        std::cerr << "finn-darwin: uninstall failed: " << err << "\n";
        return 1;
    }
    std::cout << "Removed: " << removed << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(argv[1], "--version") == 0) {
        std::puts("finn-darwin 0.2.0");
        return 0;
    }

    const std::string cmd = argv[1];
    if (cmd == "status") return cmd_status();
    if (cmd == "list") return cmd_list();
    if (cmd == "diagnose" && argc >= 3) return cmd_diagnose(argv[2]);
    if (cmd == "install" && argc >= 3) return cmd_install(argv[2]);
    if (cmd == "uninstall" && argc >= 3) return cmd_uninstall(argv[2]);

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
