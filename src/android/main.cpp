// finn-android: FinnOS clean-room Android 17 application runtime. Native APK
// parsing, binary AXML manifest/layout inspection, DEX header verification and
// bytecode analysis, and a Qt6/Wayland view bridge on the FinnOS theme.
// No Waydroid, no container, no binder, no Android SDK.

#include "android/android_format.hpp"
#include "android/android_installer.hpp"
#include "android/apk_format.hpp"
#include "android/art_bridge.hpp"
#include "android/ui_bridge.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "finn-android — FinnOS clean-room Android 17 runtime & installer\n"
        << "Usage:\n"
        << "  finn-android status\n"
        << "  finn-android self-test\n"
        << "  finn-android diagnose <app.apk>\n"
        << "  finn-android install <local.apk>\n"
        << "  finn-android uninstall <package | android:slug>\n"
        << "  finn-android list\n"
        << "  finn-android run <package | android:slug | app.apk> [--headless]\n"
        << "Remote APK URLs are not supported: download the .apk first.\n";
}

int cmd_status() {
    const auto apps = finn::android::discover_installed_android_apps();
    std::cout
        << "finn-android (" << finn::android::host_cpu_machine() << ", ABI "
        << finn::android::abi_name(finn::android::host_abi()) << ")\n"
        << "  target:     " << finn::android::kAndroidTargetRelease << " (API "
        << finn::android::kAndroidTargetApiLevel << ")\n"
        << "  packages:   .apk (native zlib ZIP reader)\n"
        << "  format:     binary AndroidManifest.xml (AXML), resources.arsc basics, DEX headers\n"
        << "  runtime:    Dalvik opcode decoder + ART-style analysis + strict-subset interpreter\n"
        << "  ui:         "
        << (finn::android::AndroidUiEngine::qt_backend_available()
                ? "Qt6/Wayland view bridge active (FinnOS theme)\n"
                : "headless CLI build (Qt6 bridge compiles when Qt6::Widgets is present)\n")
        << "  limits:     no Android framework, JNI, ART runtime, GMS or Waydroid container\n"
        << "  root_dir:   " << finn::android::default_android_root_dir() << "\n"
        << "  installed:  " << apps.size() << " Android app(s)\n";
    for (const auto& a : apps) {
        std::cout << "    - " << a.name << " (" << a.package_name
                  << ", v" << a.version_name << ", targetSdk " << a.target_sdk << ")\n";
    }
    return 0;
}

int cmd_diagnose(const std::string& target) {
    const auto d = finn::android::diagnose_android_package(target);
    if (!d.valid) {
        std::cerr << "finn-android: diagnose failed: "
                  << (d.error.empty() ? "invalid APK" : d.error) << "\n";
        return 1;
    }
    const auto& a = d.apk;
    std::cout << "Target:          " << d.path << "\n"
              << "Package:         " << a.manifest.package_name << " (v"
              << a.manifest.version_name << ", code " << a.manifest.version_code << ")\n"
              << "Label:           " << finn::android::apk_display_name(a) << "\n"
              << "Activity:        " << a.manifest.main_activity() << "\n"
              << "SDK range:       min=" << a.manifest.min_sdk
              << ", target=" << a.manifest.target_sdk
              << (d.api_compatible ? " [compatible]\n" : " [INCOMPATIBLE]\n")
              << "Host ABI:        " << a.host_abi << "\n";
    if (a.has_arsc && a.arsc.valid) {
        std::cout << "resources.arsc:  " << a.arsc.packages.size() << " package(s), "
                  << a.arsc.resources.size() << " resolvable entry/entries\n";
    }
    if (!a.dex_files.empty()) {
        std::cout << "DEX files:       " << a.dex_files.size() << "\n";
        for (const auto& dex : a.dex_files) {
            std::cout << "  - " << dex.entry << " (" << dex.size << " bytes) "
                      << (dex.header_valid ? dex.version : "INVALID")
                      << " checksum=" << (dex.checksum_valid ? "OK" : "MISMATCH")
                      << " signature=" << (dex.signature_valid ? "OK" : "MISMATCH")
                      << "\n";
        }
    } else {
        std::cout << "DEX files:       absent\n";
    }
    if (!a.native_libs.empty()) {
        std::cout << "Native libs:     " << a.native_libs.size() << "\n";
        for (const auto& lib : a.native_libs) {
            std::cout << "  - " << lib.entry << " [" << lib.abi << "] "
                      << (lib.host_compatible ? "(matches host ABI)" : "(non-host ABI)") << "\n";
        }
    }
    for (const auto& w : d.warnings) std::cout << "Warning:         " << w << "\n";
    std::cout << "Verdict:         " << d.summary << "\n"
              << "Runnable:        " << (a.runnable ? "yes" : "no") << "\n";
    return a.runnable ? 0 : 2;
}

int cmd_install(const std::string& target) {
    const auto res = finn::android::install_android_package(target);
    for (const auto& line : res.log) std::cout << line << "\n";
    if (!res.ok) {
        std::cerr << "finn-android: install failed: " << res.error << "\n";
        return 1;
    }
    std::cout << "Ready: " << res.app.name << " (" << res.app.id << ")\n";
    return 0;
}

int cmd_uninstall(const std::string& target) {
    std::string err;
    if (!finn::android::uninstall_android_app(target, {}, &err)) {
        std::cerr << "finn-android: uninstall failed: " << err << "\n";
        return 1;
    }
    std::cout << "Uninstalled: " << target << "\n";
    return 0;
}

int cmd_list() {
    const auto apps = finn::android::discover_installed_android_apps();
    if (apps.empty()) {
        std::cout << "No Android apps installed.\n";
        return 0;
    }
    for (const auto& a : apps) {
        std::cout << a.package_name << " | " << a.name << " | " << a.version_name
                  << " (targetSdk " << a.target_sdk << ") | " << a.install_dir << "\n";
    }
    return 0;
}

int cmd_run(int argc, char** argv) {
    bool headless = false;
    std::string target;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) headless = true;
        else if (target.empty()) target = argv[i];
    }
    if (target.empty()) {
        print_usage();
        return 1;
    }
    std::string err;
    const auto report = finn::android::run_android_package(target, headless, {}, &err);
    for (const auto& line : report.lifecycle_log) std::cout << line << "\n";
    if (!report.summary.empty()) std::cout << report.summary;
    if (!err.empty() || !report.ok) {
        std::cerr << "finn-android: "
                  << (err.empty() ? (report.error.empty() ? "run failed" : report.error)
                                  : err)
                  << "\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 ||
        std::strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(argv[1], "--version") == 0) {
        std::puts("finn-android 0.1.0 (Android 17 / API 37 clean-room runtime)");
        return 0;
    }
    if (std::strcmp(argv[1], "self-test") == 0) {
        return finn::android::android_self_test() ? 0 : 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "status") return cmd_status();
    if (cmd == "list") return cmd_list();
    if (cmd == "diagnose" && argc >= 3) return cmd_diagnose(argv[2]);
    if (cmd == "install" && argc >= 3) return cmd_install(argv[2]);
    if (cmd == "uninstall" && argc >= 3) return cmd_uninstall(argv[2]);
    if (cmd == "run" && argc >= 3) return cmd_run(argc, argv);
    print_usage();
    return 1;
}
