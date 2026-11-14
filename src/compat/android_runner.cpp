// Waydroid runtime probing and app discovery for FinnOS.

#include "compat/android_runner.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace finn::compat {

namespace {

std::string find_in_path(const char* name) {
    const char* env_path = std::getenv("PATH");
    if (!env_path) return {};
    std::stringstream ss(env_path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        std::error_code ec;
        fs::path p = fs::path(dir) / name;
        if (fs::exists(p, ec) && ::access(p.c_str(), X_OK) == 0) {
            return p.string();
        }
    }
    return {};
}

} // namespace

AndroidRuntimeStatus probe_android_runtime() {
    AndroidRuntimeStatus st;
    st.waydroid_bin = find_in_path("waydroid");
    st.waydroid_installed = !st.waydroid_bin.empty();
    std::error_code ec;
    st.binder_present = fs::exists("/dev/binderfs", ec) || fs::exists("/dev/binder", ec);
    st.container_initialized = fs::exists("/var/lib/waydroid/waydroid.cfg", ec);
    st.ready = st.waydroid_installed && st.container_initialized;

    if (!st.waydroid_installed) {
        st.summary = "Waydroid not installed (pacman -S waydroid)";
    } else if (!st.binder_present) {
        st.summary = "Waydroid installed; binder kernel module not loaded";
    } else if (!st.container_initialized) {
        st.summary = "Waydroid installed; run 'sudo waydroid init' to initialize image";
    } else {
        st.summary = "Waydroid container ready for windowed Android apps";
    }
    return st;
}

std::vector<AndroidAppEntry> list_android_apps() {
    std::vector<AndroidAppEntry> out;
    if (!probe_android_runtime().ready) return out;

    FILE* fp = ::popen("waydroid app list 2>/dev/null", "r");
    if (!fp) return out;

    std::array<char, 512> buf{};
    AndroidAppEntry cur;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), fp)) {
        std::string line(buf.data());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (line.rfind("Name:", 0) == 0) {
            cur.name = line.substr(5);
            while (!cur.name.empty() && cur.name.front() == ' ') cur.name.erase(cur.name.begin());
        } else if (line.rfind("packageName:", 0) == 0) {
            cur.package_name = line.substr(12);
            while (!cur.package_name.empty() && cur.package_name.front() == ' ') {
                cur.package_name.erase(cur.package_name.begin());
            }
            if (!cur.package_name.empty()) {
                if (cur.name.empty()) cur.name = cur.package_name;
                out.push_back(cur);
            }
            cur = {};
        }
    }
    ::pclose(fp);
    return out;
}

} // namespace finn::compat
