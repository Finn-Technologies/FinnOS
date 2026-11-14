// Android application compatibility via Waydroid (probing, APK install, launch).

#ifndef FINN_COMPAT_ANDROID_RUNNER_HPP
#define FINN_COMPAT_ANDROID_RUNNER_HPP

#include <string>
#include <vector>

namespace finn::compat {

struct AndroidRuntimeStatus {
    bool waydroid_installed = false;
    std::string waydroid_bin;
    bool binder_present = false;
    bool container_initialized = false;
    bool ready = false;
    std::string summary;
};

struct AndroidAppEntry {
    std::string name;
    std::string package_name;
};

AndroidRuntimeStatus probe_android_runtime();
std::vector<AndroidAppEntry> list_android_apps();

} // namespace finn::compat

#endif // FINN_COMPAT_ANDROID_RUNNER_HPP
