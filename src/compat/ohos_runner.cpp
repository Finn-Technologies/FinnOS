#include "compat/ohos_runner.hpp"

#include "compat/desktop_entry.hpp"
#include "compat/zip_entries.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

inline constexpr const char* kDefaultFinnOhosBin = "/usr/local/bin/finn-ohos";
inline constexpr const char* kDefaultOhosRegistryRel = ".local/share/finn/ohos/registry.conf";

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

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string host_machine() {
    struct utsname uts{};
    if (::uname(&uts) == 0) return uts.machine;
    return "unknown";
}

std::string machine_to_ohos_abi(std::string_view m) {
    if (m == "aarch64" || m == "arm64") return "arm64-v8a";
    if (m == "x86_64" || m == "amd64") return "x86_64";
    if (m.rfind("arm", 0) == 0) return "armeabi-v7a";
    return std::string(m);
}

std::string slugify(std::string_view s) {
    std::string slug;
    bool prev_dash = false;
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            slug.push_back(static_cast<char>(std::tolower(uc)));
            prev_dash = false;
        } else if (!prev_dash && !slug.empty()) {
            slug.push_back('-');
            prev_dash = true;
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.empty() ? "ohos-app" : slug;
}

std::vector<std::string> split_pipe_line(std::string_view line) {
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

// ZIP parsing lives in compat/zip_entries.hpp so the macOS and OpenHarmony
// installers agree on what counts as a package.

}  // namespace

OhosPackageType classify_ohos_package_path(const std::string& path) {
    if (path.empty()) return OhosPackageType::Unknown;

    // HTTP/HTTPS package URLs
    if (starts_with_ci(path, "http://") || starts_with_ci(path, "https://")) {
        std::string_view clean = path;
        if (const std::size_t q = clean.find_first_of("?#"); q != std::string_view::npos) {
            clean = clean.substr(0, q);
        }
        if (ends_with_ci(clean, ".hap")) return OhosPackageType::Hap;
        if (ends_with_ci(clean, ".hsp")) return OhosPackageType::Hsp;
        if (ends_with_ci(clean, ".app")) return OhosPackageType::AppPack;
        return OhosPackageType::Unknown;
    }

    std::error_code ec;
    // Never confuse a macOS .app directory bundle with an OpenHarmony .app ZIP pack.
    if (fs::is_directory(path, ec)) return OhosPackageType::Unknown;

    const bool exists = fs::is_regular_file(path, ec);
    if (ends_with_ci(path, ".hap")) {
        if (!exists) return OhosPackageType::Hap;
        std::vector<std::string> names;
        return read_zip_entry_names(path, &names) ? OhosPackageType::Hap : OhosPackageType::Unknown;
    }
    if (ends_with_ci(path, ".hsp")) {
        if (!exists) return OhosPackageType::Hsp;
        std::vector<std::string> names;
        return read_zip_entry_names(path, &names) ? OhosPackageType::Hsp : OhosPackageType::Unknown;
    }
    if (ends_with_ci(path, ".app")) {
        if (!exists) return OhosPackageType::Unknown;
        std::vector<std::string> names;
        if (!read_zip_entry_names(path, &names)) return OhosPackageType::Unknown;
        for (const auto& n : names) {
            if (n == "pack.info" || ends_with_ci(n, ".hap") || ends_with_ci(n, ".hsp")) {
                return OhosPackageType::AppPack;
            }
        }
        return OhosPackageType::Unknown;
    }

    return OhosPackageType::Unknown;
}

bool is_ohos_package_path(const std::string& path) {
    return classify_ohos_package_path(path) != OhosPackageType::Unknown;
}

OhosRuntimeStatus probe_ohos_runtime() {
    OhosRuntimeStatus st;
    st.host_arch = host_machine();
    st.host_ohos_abi = machine_to_ohos_abi(st.host_arch);
    st.supported_api_version = 23;
    st.ohos_version = "6.1 LTS";

    std::error_code ec;
    if (fs::is_regular_file(kDefaultFinnOhosBin, ec)) {
        st.finn_ohos_installed = true;
        st.finn_ohos_bin = kDefaultFinnOhosBin;
    } else if (executable_exists("finn-ohos")) {
        st.finn_ohos_installed = true;
        st.finn_ohos_bin = "finn-ohos";
    }

    st.ready = st.finn_ohos_installed;
    if (st.ready) {
        st.summary =
            "FinnOS OpenHarmony 6.1 LTS (API 23) runtime (finn-ohos) ready on " +
            st.host_ohos_abi +
            " — native .hap/.hsp/.app installer, ArkCompiler Panda .abc VM, ArkUI bridge, "
            "and NAPI host shim (ArkWeb/DistributedData/Telephony/NFC report unavailable cleanly).";
    } else {
        st.summary =
            "finn-ohos is not installed in PATH on " + st.host_ohos_abi +
            ". Build and install src/ohos (finn-ohos) to inspect, install, and run "
            "OpenHarmony 6.1 LTS (.hap/.hsp/.app) packages.";
    }
    return st;
}

OhosCommandPlan build_ohos_install_plan(const std::string& package_path_or_url) {
    OhosCommandPlan plan;
    const bool url = starts_with_ci(package_path_or_url, "http://") ||
                     starts_with_ci(package_path_or_url, "https://");
    if (!url) {
        std::error_code ec;
        if (!fs::is_regular_file(package_path_or_url, ec)) {
            plan.error = "OpenHarmony package does not exist: " + package_path_or_url;
            return plan;
        }
        if (!is_ohos_package_path(package_path_or_url)) {
            plan.error = "not a valid OpenHarmony .hap, .hsp, or .app ZIP archive: " +
                         package_path_or_url;
            return plan;
        }
    }

    const OhosRuntimeStatus rt = probe_ohos_runtime();
    if (!rt.finn_ohos_installed) {
        plan.error = "finn-ohos is not installed; cannot install OpenHarmony package.";
        return plan;
    }
    plan.argv = {rt.finn_ohos_bin, "install", package_path_or_url};
    plan.note = "Install OpenHarmony 6.1 LTS package via finn-ohos";
    plan.ok = true;
    return plan;
}

OhosCommandPlan build_ohos_diagnose_plan(const std::string& package_path) {
    OhosCommandPlan plan;
    std::error_code ec;
    if (!fs::is_regular_file(package_path, ec)) {
        plan.error = "OpenHarmony package does not exist: " + package_path;
        return plan;
    }
    if (!is_ohos_package_path(package_path)) {
        plan.error = "not an OpenHarmony .hap, .hsp, or .app ZIP archive: " + package_path;
        return plan;
    }
    const OhosRuntimeStatus rt = probe_ohos_runtime();
    if (!rt.finn_ohos_installed) {
        plan.error = "finn-ohos is not installed.";
        return plan;
    }
    plan.argv = {rt.finn_ohos_bin, "diagnose", package_path};
    plan.note = "Analyze OpenHarmony 6.1 LTS compatibility via finn-ohos";
    plan.ok = true;
    return plan;
}

OhosCommandPlan build_ohos_launch_plan(const std::string& bundle_or_package_path) {
    OhosCommandPlan plan;
    if (bundle_or_package_path.empty()) {
        plan.error = "OpenHarmony launch target is empty";
        return plan;
    }
    const OhosRuntimeStatus rt = probe_ohos_runtime();
    if (!rt.finn_ohos_installed) {
        plan.error = rt.summary;
        return plan;
    }
    plan.argv = {rt.finn_ohos_bin, "run", bundle_or_package_path};
    plan.note = "Launch OpenHarmony 6.1 LTS app via finn-ohos";
    plan.ok = true;
    return plan;
}

std::vector<OhosAppEntry> discover_ohos_apps(const std::string& registry_path) {
    std::vector<OhosAppEntry> out;
    fs::path reg_file;
    if (!registry_path.empty()) {
        reg_file = registry_path;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        reg_file = fs::path(home) / kDefaultOhosRegistryRel;
    } else {
        return out;
    }

    std::ifstream in(reg_file);
    if (!in) return out;

    std::string line;
    std::error_code ec;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto f = split_pipe_line(line);
        if (f.size() < 11) continue;
        if (!fs::is_directory(f[8], ec)) continue;

        OhosAppEntry e;
        e.bundle_name = f[0];
        e.id = "ohos:" + slugify(e.bundle_name);
        e.name = f[1];
        e.version_name = f[2];
        e.version_code = static_cast<std::uint32_t>(std::strtoul(f[3].c_str(), nullptr, 10));
        e.target_api_version = std::atoi(f[4].c_str());
        e.min_api_version = std::atoi(f[5].c_str());
        e.main_ability = f[6];
        e.module_type = f[7];
        // Shared libraries (.hsp) without a launchable ability do not belong in
        // the desktop app launcher.
        if (e.module_type == "shared" && e.main_ability.empty()) continue;
        e.install_dir = f[8];
        e.package_path = f[9];
        if (!f[10].empty() && fs::is_regular_file(f[10], ec)) {
            e.icon_path = f[10];
        }
        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(),
              [](const OhosAppEntry& a, const OhosAppEntry& b) {
                  return a.name < b.name;
              });
    return out;
}

}  // namespace finn::compat
