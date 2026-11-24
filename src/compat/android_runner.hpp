// Lightweight clean-room Android 17 compatibility probe, command planner and
// installed-app discovery. Pure C++20 with no zlib, binder or GUI dependency so
// finn-session, finn-shell, Finn Store and Finn Settings can link it directly.
//
// Waydroid is deliberately absent: finn-android parses APKs itself.

#ifndef FINN_COMPAT_ANDROID_RUNNER_HPP
#define FINN_COMPAT_ANDROID_RUNNER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace finn::compat {

enum class ApkPackageType { Unknown, Apk, Xapk, Apkm, Apks };

struct AndroidRuntimeStatus {
    std::string host_arch;
    std::string host_android_abi;
    int target_api_level = 37;
    std::string target_release = "Android 17";
    bool finn_android_installed = false;
    std::string finn_android_bin;
    bool ready = false;
    std::string summary;
};

struct AndroidAppEntry {
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
};

struct AndroidCommandPlan {
    bool ok = false;
    std::vector<std::string> argv;
    std::string note;
    std::string error;
};

ApkPackageType classify_apk_path(const std::string& path);
bool is_apk_package_path(const std::string& path);

AndroidRuntimeStatus probe_android_runtime();

AndroidCommandPlan build_android_install_plan(const std::string& package_path_or_url);
AndroidCommandPlan build_android_diagnose_plan(const std::string& package_path);
AndroidCommandPlan build_android_launch_plan(const std::string& package_or_apk);

std::vector<AndroidAppEntry> discover_android_apps(const std::string& registry_path = {});
// Kept as a source-compatible name for Finn Store's app list.
std::vector<AndroidAppEntry> list_android_apps(const std::string& registry_path = {});

}  // namespace finn::compat

#endif  // FINN_COMPAT_ANDROID_RUNNER_HPP
