#include "compat/android_runner.hpp"

#include "compat/desktop_entry.hpp"
#include "compat/zip_entries.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

inline constexpr const char* kDefaultFinnAndroidBin = "/usr/local/bin/finn-android";
inline constexpr const char* kDefaultRegistryRel = ".local/share/finn/android/registry.conf";

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) return false;
    }
    return true;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

std::string host_machine() {
    struct utsname uts{};
    if (::uname(&uts) == 0) return uts.machine;
    return {};
}

std::string machine_to_abi(std::string_view m) {
    if (m == "aarch64" || m == "arm64") return "arm64-v8a";
    if (m == "x86_64" || m == "amd64") return "x86_64";
    if (m.rfind("arm", 0) == 0) return "armeabi-v7a";
    if (m == "riscv64") return "riscv64";
    return std::string(m);
}

std::vector<std::string> split_pipe(std::string_view line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t bar = line.find('|', start);
        if (bar == std::string_view::npos) {
            out.emplace_back(line.substr(start));
            break;
        }
        out.emplace_back(line.substr(start, bar - start));
        start = bar + 1;
    }
    return out;
}

}  // namespace

ApkPackageType classify_apk_path(const std::string& path) {
    if (path.empty()) return ApkPackageType::Unknown;
    if (starts_with_ci(path, "http://") || starts_with_ci(path, "https://")) {
        std::string clean = path;
        const std::size_t q = clean.find_first_of("?#");
        if (q != std::string::npos) clean.resize(q);
        return ends_with_ci(clean, ".apk") ? ApkPackageType::Apk : ApkPackageType::Unknown;
    }
    std::error_code ec;
    if (fs::is_directory(path, ec) || !fs::is_regular_file(path, ec)) {
        return ApkPackageType::Unknown;
    }
    if (!ends_with_ci(path, ".apk")) return ApkPackageType::Unknown;
    std::vector<std::string> names;
    if (!read_zip_entry_names(path, &names)) return ApkPackageType::Unknown;
    for (const auto& n : names) {
        if (n == "AndroidManifest.xml") return ApkPackageType::Apk;
    }
    return ApkPackageType::Unknown;
}

bool is_apk_package_path(const std::string& path) {
    return classify_apk_path(path) != ApkPackageType::Unknown;
}

AndroidRuntimeStatus probe_android_runtime() {
    AndroidRuntimeStatus st;
    st.host_arch = host_machine();
    st.host_android_abi = machine_to_abi(st.host_arch);
    st.target_api_level = 37;
    st.target_release = "Android 17";

    std::error_code ec;
    if (fs::is_regular_file(kDefaultFinnAndroidBin, ec)) {
        st.finn_android_installed = true;
        st.finn_android_bin = kDefaultFinnAndroidBin;
    } else if (executable_exists("finn-android")) {
        st.finn_android_installed = true;
        st.finn_android_bin = "finn-android";
    }
    st.ready = st.finn_android_installed;
    if (st.ready) {
        st.summary =
            "FinnOS clean-room Android 17 runtime (finn-android) ready on " +
            st.host_android_abi +
            " — native APK/ZIP, binary AXML, DEX analysis and Qt6/Wayland view bridge. "
            "No Waydroid container or binder is used; the Android framework, JNI and GMS "
            "are not implemented.";
    } else {
        st.summary =
            "finn-android is not installed in PATH on " + st.host_android_abi +
            ". Build and install src/android to inspect, install and run Android 17 APKs.";
    }
    return st;
}

AndroidCommandPlan build_android_install_plan(const std::string& package_path_or_url) {
    AndroidCommandPlan plan;
    const bool url = starts_with_ci(package_path_or_url, "http://") ||
                     starts_with_ci(package_path_or_url, "https://");
    if (!url) {
        std::error_code ec;
        if (!fs::is_regular_file(package_path_or_url, ec)) {
            plan.error = "APK does not exist: " + package_path_or_url;
            return plan;
        }
        if (!is_apk_package_path(package_path_or_url)) {
            plan.error = "not a valid APK with AndroidManifest.xml: " + package_path_or_url;
            return plan;
        }
    } else if (!is_apk_package_path(package_path_or_url)) {
        plan.error = "URL does not name an APK: " + package_path_or_url;
        return plan;
    }
    const AndroidRuntimeStatus rt = probe_android_runtime();
    if (!rt.finn_android_installed) {
        plan.error = "finn-android is not installed; cannot install APK.";
        return plan;
    }
    plan.argv = {rt.finn_android_bin, "install", package_path_or_url};
    plan.note = "Install Android APK via finn-android";
    plan.ok = true;
    return plan;
}

AndroidCommandPlan build_android_diagnose_plan(const std::string& package_path) {
    AndroidCommandPlan plan;
    std::error_code ec;
    if (!fs::is_regular_file(package_path, ec)) {
        plan.error = "APK does not exist: " + package_path;
        return plan;
    }
    if (!is_apk_package_path(package_path)) {
        plan.error = "not a valid APK: " + package_path;
        return plan;
    }
    const AndroidRuntimeStatus rt = probe_android_runtime();
    if (!rt.finn_android_installed) {
        plan.error = "finn-android is not installed.";
        return plan;
    }
    plan.argv = {rt.finn_android_bin, "diagnose", package_path};
    plan.note = "Analyze Android APK compatibility via finn-android";
    plan.ok = true;
    return plan;
}

AndroidCommandPlan build_android_launch_plan(const std::string& package_or_apk) {
    AndroidCommandPlan plan;
    if (package_or_apk.empty()) {
        plan.error = "Android launch target is empty";
        return plan;
    }
    const AndroidRuntimeStatus rt = probe_android_runtime();
    if (!rt.finn_android_installed) {
        plan.error = rt.summary;
        return plan;
    }
    plan.argv = {rt.finn_android_bin, "run", package_or_apk};
    plan.note = "Launch Android application via finn-android";
    plan.ok = true;
    return plan;
}

std::vector<AndroidAppEntry> discover_android_apps(const std::string& registry_path) {
    std::vector<AndroidAppEntry> out;
    fs::path reg_file;
    if (!registry_path.empty()) {
        reg_file = registry_path;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        reg_file = fs::path(home) / kDefaultRegistryRel;
    } else {
        return out;
    }
    std::ifstream in(reg_file);
    if (!in) return out;
    std::string line;
    std::error_code ec;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto f = split_pipe(line);
        // Registry rows are written by src/android/android_installer.cpp as:
        // id|name|package|versionName|versionCode|minSdk|targetSdk|activity|
        // installDir|packagePath|iconPath|abis
        if (f.size() < 11u) continue;
        if (!fs::is_directory(f[8], ec)) continue;
        AndroidAppEntry e;
        e.id = f[0];
        e.name = f[1];
        e.package_name = f[2];
        e.version_name = f[3];
        e.version_code = static_cast<std::uint32_t>(std::strtoul(f[4].c_str(), nullptr, 10));
        e.min_sdk = std::atoi(f[5].c_str());
        e.target_sdk = std::atoi(f[6].c_str());
        e.main_activity = f[7];
        e.install_dir = f[8];
        e.package_path = f[9];
        if (!f[10].empty() && fs::is_regular_file(f[10], ec)) e.icon_path = f[10];
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const AndroidAppEntry& a, const AndroidAppEntry& b) {
                  return a.name < b.name;
              });
    return out;
}

std::vector<AndroidAppEntry> list_android_apps(const std::string& registry_path) {
    return discover_android_apps(registry_path);
}

}  // namespace finn::compat
