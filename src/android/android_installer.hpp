// finn-android APK inspector, per-user installer/uninstaller, real icon
// extractor and registry. Clean-room: no Android SDK, no Waydroid, no
// fabricated icon or launch capability.

#ifndef FINN_ANDROID_INSTALLER_HPP
#define FINN_ANDROID_INSTALLER_HPP

#include "android/apk_format.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace finn::android {

struct InstalledAndroidApp {
    std::string id;  // "android:<slug>"
    std::string name;
    std::string package_name;
    std::string version_name;
    std::uint32_t version_code = 0;
    int min_sdk = 0;
    int target_sdk = 0;
    std::string main_activity;
    std::string install_dir;
    std::string package_path;
    std::string icon_path;
    std::vector<std::string> abis;
};

struct AndroidInstallResult {
    bool ok = false;
    InstalledAndroidApp app;
    std::vector<std::string> log;
    std::string error;
};

struct AndroidDiagnosticReport {
    bool valid = false;
    std::string path;
    ApkInfo apk;
    bool api_compatible = false;
    bool dex_analyzable = false;
    bool native_abi_ok = true;
    std::vector<std::string> warnings;
    std::string summary;
    std::string error;
};

struct AndroidRunReport {
    bool ok = false;
    std::string package_name;
    std::string activity;
    std::vector<std::string> lifecycle_log;
    std::string summary;
    std::string error;
};

std::string default_android_root_dir();
std::string default_android_apps_dir(const std::string& android_root = {});
std::string default_android_icons_dir(const std::string& android_root = {});
std::string default_android_registry_path(const std::string& android_root = {});

// Static pre-flight. Never installs, launches or mutates the package.
AndroidDiagnosticReport diagnose_android_package(const std::string& path);

// Installs a local .apk into the invoking user's FinnOS data directory.
// Remote URLs are rejected honestly rather than silently skipped.
AndroidInstallResult install_android_package(const std::string& path,
                                             const std::string& android_root = {});

bool uninstall_android_app(const std::string& package_or_id,
                           const std::string& android_root = {},
                           std::string* error = nullptr);

std::vector<InstalledAndroidApp> discover_installed_android_apps(
    const std::string& android_root = {});

// Bridges an APK's declared launcher activity lifecycle into the FinnUI/Qt6
// widget bridge. Only lifecycle methods that actually exist in the DEX and
// decode are reported as executed; everything else is logged as skipped.
AndroidRunReport run_android_package(const std::string& package_or_apk,
                                     bool headless = true,
                                     const std::string& android_root = {},
                                     std::string* error_out = nullptr);

}  // namespace finn::android

#endif  // FINN_ANDROID_INSTALLER_HPP
