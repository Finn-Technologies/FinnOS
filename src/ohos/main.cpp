// finn-ohos: FinnOS clean-room OpenHarmony 6.1 LTS (API Level 23) application
// runtime, ArkCompiler Panda (.abc) bytecode VM, ArkUI bridge, and .hap/.hsp/.app
// package installer CLI.

#include "ohos/abc_runtime.hpp"
#include "ohos/arkui_bridge.hpp"
#include "ohos/ohos_format.hpp"
#include "ohos/ohos_installer.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "finn-ohos — FinnOS clean-room OpenHarmony 6.1 LTS (API 23) runtime & installer\n"
        << "Usage:\n"
        << "  finn-ohos status\n"
        << "  finn-ohos diagnose <package.hap | library.hsp | pack.app>\n"
        << "  finn-ohos install <local-path | http(s)://url>\n"
        << "  finn-ohos uninstall <bundleName | ohos:slug>\n"
        << "  finn-ohos list\n"
        << "  finn-ohos run <bundleName | package.hap | pack.app> [--headless]\n";
}

int cmd_status() {
    const auto apps = finn::ohos::discover_installed_ohos_apps();
    std::cout
        << "finn-ohos (" << finn::ohos::host_cpu_machine() << ", ABI "
        << finn::ohos::host_ohos_abi() << ")\n"
        << "  target:     " << finn::ohos::kOhosReleaseName
        << " (API Level " << finn::ohos::kOhosSupportedApiLevel << ")\n"
        << "  packages:   .hap, .hsp, .app (native zlib ZIP reader + HTTP/HTTPS install)\n"
        << "  runtime:    ArkCompiler Panda .abc bytecode VM + UIAbility lifecycle + NAPI host bridge\n"
        << "  arkui:      "
        << (finn::ohos::ArkUiEngine::qt_backend_available()
                ? "Qt6/Wayland widget bridge active (declarative ArkUI + @State + API 23 hooks)\n"
                : "headless CLI build (Qt6/Wayland ArkUI bridge compiles when Qt6::Widgets is present)\n")
        << "  kits:       @ohos.hilog, UIAbility, window, router, preferences, file.fs, deviceInfo\n"
        << "  limits:     ArkWeb, DistributedDataObject, Telephony, NFC, Camera, Sensor, MapKit not bridged\n"
        << "  root_dir:   " << finn::ohos::default_ohos_root_dir() << "\n"
        << "  installed:  " << apps.size() << " OpenHarmony app(s)\n";
    for (const auto& a : apps) {
        std::cout << "    - " << a.name << " (" << a.bundle_name
                  << ", v" << a.version_name << ", API " << a.target_api_version << ")\n";
    }
    return 0;
}

int cmd_list() {
    const auto apps = finn::ohos::discover_installed_ohos_apps();
    if (apps.empty()) {
        std::cout << "No OpenHarmony apps installed.\n";
        return 0;
    }
    for (const auto& a : apps) {
        std::cout << a.bundle_name << " | " << a.name << " | "
                  << a.version_name << " (API " << a.target_api_version << ") | "
                  << a.install_dir << "\n";
    }
    return 0;
}

int cmd_diagnose(const std::string& target) {
    const auto d = finn::ohos::diagnose_ohos_package(target);
    if (!d.valid) {
        std::cerr << "finn-ohos: diagnose failed: " << d.error << "\n";
        return 1;
    }

    std::cout << "Target:          " << d.target_path << "\n"
              << "Package kind:    " << finn::ohos::package_kind_name(d.kind) << "\n"
              << "Host CPU / ABI:  " << d.host_cpu << " / " << d.host_abi << "\n"
              << "Bundle:          " << d.manifest.bundle_name
              << " (" << d.manifest.version_name
              << ", code " << d.manifest.version_code << ")\n"
              << "Module:          " << d.manifest.module_name
              << " [" << d.manifest.module_type << "], main=" << d.manifest.main_element << "\n"
              << "API versions:    min=" << d.manifest.min_api_version
              << ", target=" << d.manifest.target_api_version
              << " (" << d.manifest.api_release_type << ")"
              << (d.api_compatible ? " [compatible]\n" : " [INCOMPATIBLE]\n");

    if (d.resource_index.present) {
        std::cout << "resources.index: " << (d.resource_index.valid ? "valid" : "invalid")
                  << " (" << d.resource_index.version << ", "
                  << d.resource_index.limit_key_count << " config(s))\n";
    }
    if (d.abc_present) {
        std::cout << "Panda .abc:      "
                  << (d.abc_valid ? "valid" : "invalid")
                  << " v" << d.abc_version
                  << " (checksum " << (d.abc_checksum_valid ? "OK" : "MISMATCH")
                  << ", " << d.abc_class_count << " class(es), "
                  << d.abc_method_count << " method(s))\n";
    } else {
        std::cout << "Panda .abc:      absent\n";
    }

    if (!d.native_libs.empty()) {
        std::cout << "Native libs:     " << d.native_libs.size() << "\n";
        for (const auto& lib : d.native_libs) {
            std::cout << "  - " << lib.archive_path << " [" << lib.elf_machine << "] "
                      << (lib.host_compatible ? "(matches host ABI)" : "(non-host ABI)") << "\n";
        }
    }
    if (!d.supported_kits.empty()) {
        std::cout << "Supported kits:  " << d.supported_kits.size() << "\n";
        for (const auto& k : d.supported_kits) std::cout << "  + " << k << "\n";
    }
    if (!d.unsupported_kits.empty()) {
        std::cout << "Unsupported kits:" << " " << d.unsupported_kits.size() << "\n";
        for (const auto& k : d.unsupported_kits) std::cout << "  ! " << k << "\n";
    }
    for (const auto& w : d.warnings) {
        std::cout << "Warning:         " << w << "\n";
    }
    std::cout << "Verdict:         " << d.summary << "\n";
    return d.runnable ? 0 : 2;
}

int cmd_install(const std::string& target) {
    const auto res = finn::ohos::install_ohos_package(target);
    for (const auto& line : res.log) std::cout << line << "\n";
    if (!res.ok) {
        std::cerr << "finn-ohos: install failed: " << res.error << "\n";
        return 1;
    }
    std::cout << "Ready: " << res.app.name << " (" << res.app.id << ")\n";
    return 0;
}

int cmd_uninstall(const std::string& bundle) {
    std::string err;
    if (!finn::ohos::uninstall_ohos_app(bundle, {}, &err)) {
        std::cerr << "finn-ohos: uninstall failed: " << err << "\n";
        return 1;
    }
    std::cout << "Uninstalled: " << bundle << "\n";
    return 0;
}

int cmd_run(int argc, char** argv) {
    bool headless = false;
    std::string target;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            headless = true;
        } else if (target.empty()) {
            target = argv[i];
        }
    }
    if (target.empty()) {
        print_usage();
        return 1;
    }

    finn::ohos::AbilityRunReport report;
    std::string err;
    const int rc = finn::ohos::run_ohos_target(
        target, argc, argv, headless, {}, &report, &err);
    for (const auto& line : report.logs) std::cout << line << "\n";
    if (rc != 0 && !err.empty()) {
        std::cerr << "finn-ohos: " << err << "\n";
    }
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 ||
        std::strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(argv[1], "--version") == 0) {
        std::puts("finn-ohos 0.1.0 (OpenHarmony 6.1 LTS / API 23)");
        return 0;
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
