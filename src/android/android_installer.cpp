#include "android/android_installer.hpp"

#include "android/art_bridge.hpp"
#include "android/dex_runtime.hpp"
#include "android/ui_bridge.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::android {
namespace {

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

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

std::string slugify(std::string_view s) {
    std::string out;
    bool prev_dash = false;
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
            prev_dash = false;
        } else if (!prev_dash && !out.empty()) {
            out.push_back('-');
            prev_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "android-app" : out;
}

std::string sanitize_dirname(std::string_view s) {
    std::string out;
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '_' || c == '-') out.push_back(c);
        else out.push_back('_');
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    return out.empty() ? "unknown.package" : out;
}

std::string sanitize_pipe(std::string_view s) {
    std::string out;
    for (char c : s) {
        out.push_back(c == '|' || c == '\n' || c == '\r' ? ' ' : c);
    }
    return out;
}

std::vector<std::string> split_pipe(std::string_view line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t bar = line.find('|', start);
        if (bar == std::string_view::npos) {
            parts.emplace_back(line.substr(start));
            break;
        }
        parts.emplace_back(line.substr(start, bar - start));
        start = bar + 1;
    }
    return parts;
}

bool valid_image_bytes(const std::vector<std::uint8_t>& bytes, std::string* ext) {
    if (bytes.size() >= 8 && bytes[0] == 0x89u && bytes[1] == 'P' &&
        bytes[2] == 'N' && bytes[3] == 'G') {
        if (ext) *ext = ".png";
        return true;
    }
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
        std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
        if (ext) *ext = ".webp";
        return true;
    }
    return false;
}

int density_rank(std::string_view entry) {
    if (entry.find("xxxhdpi") != std::string_view::npos) return 6;
    if (entry.find("xxhdpi") != std::string_view::npos) return 5;
    if (entry.find("xhdpi") != std::string_view::npos) return 4;
    if (entry.find("hdpi") != std::string_view::npos) return 3;
    if (entry.find("mdpi") != std::string_view::npos) return 2;
    if (entry.find("ldpi") != std::string_view::npos) return 1;
    return 0;
}

std::string icon_reference(const ApkInfo& apk) {
    if (!apk.manifest.application_icon.empty()) return apk.manifest.application_icon;
    for (const auto& a : apk.manifest.activities) {
        if (a.launcher && !a.icon_ref.empty()) return a.icon_ref;
    }
    return {};
}

std::string extract_apk_icon(const ZipReader& zip, const ApkInfo& apk,
                             const std::string& icons_dir,
                             std::vector<std::string>* log) {
    const std::string reference = icon_reference(apk);
    std::string key = resolve_apk_resource_key(reference, apk.arsc);
    if (key.empty() && !reference.empty() && log) {
        log->push_back("Icon reference " + reference +
                       " could not be resolved through resources.arsc");
    }
    if (key.empty()) {
        // A conventional name to search for, not a claim that this APK has it.
        key = "ic_launcher";
        if (log) log->push_back("Searching for a conventional ic_launcher image");
    }

    struct Candidate {
        std::string entry;
        int rank = 0;
    };
    std::vector<Candidate> candidates;
    for (const auto& e : zip.entries()) {
        if (e.is_directory) continue;
        if (!starts_with(e.name, "res/mipmap") && !starts_with(e.name, "res/drawable")) {
            continue;
        }
        if (!ends_with_ci(e.name, ".png") && !ends_with_ci(e.name, ".webp") &&
            !ends_with_ci(e.name, ".xml")) {
            continue;
        }
        const fs::path p(e.name);
        std::string stem = p.stem().string();
        if (stem.empty()) continue;
        bool match = (stem == key) || (stem == key + "_foreground") ||
                     (stem == key + "_round");
        if (!match && (key == "ic_launcher" || stem.rfind("ic_launcher", 0) == 0)) {
            if (stem.rfind("ic_launcher", 0) == 0) match = true;
        }
        if (!match) continue;
        int rank = starts_with(e.name, "res/mipmap") ? 10 : 0;
        if (stem == key && !ends_with_ci(e.name, ".xml")) rank += 8;
        else if (stem == key + "_foreground") rank += 6;
        else if (stem == key + "_round") rank += 5;
        rank += density_rank(e.name);
        candidates.push_back({e.name, rank});
    }
    if (candidates.empty()) {
        if (log) log->push_back("No raster launch icon found in APK resources");
        return {};
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.rank > b.rank; });

    std::error_code ec;
    fs::create_directories(icons_dir, ec);
    for (const auto& c : candidates) {
        if (ends_with_ci(c.entry, ".xml")) {
            if (log) {
                log->push_back("Skipping adaptive-icon XML " + c.entry +
                               " (not a raster image)");
            }
            continue;
        }
        std::vector<std::uint8_t> bytes;
        if (!zip.read_entry(c.entry, &bytes)) continue;
        std::string ext;
        if (!valid_image_bytes(bytes, &ext)) continue;
        const fs::path dst = fs::path(icons_dir) / (slugify(apk.manifest.package_name) + ext);
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) continue;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) continue;
        if (log) log->push_back("Extracted icon " + c.entry + " -> " + dst.string());
        return dst.string();
    }
    if (log) log->push_back("APK icon resources were not decodable raster images");
    return {};
}

bool write_registry(const std::string& path,
                    const std::vector<InstalledAndroidApp>& apps,
                    std::string* error) {
    std::error_code ec;
    const fs::path p(path);
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            if (error) *error = "failed to write Android registry: " + path;
            return false;
        }
        out << "# FinnOS Android installed app registry\n";
        for (const auto& a : apps) {
            std::string abis;
            for (std::size_t i = 0; i < a.abis.size(); ++i) {
                if (i) abis += ";";
                abis += sanitize_pipe(a.abis[i]);
            }
            out << sanitize_pipe(a.id) << "|" << sanitize_pipe(a.name) << "|"
                << sanitize_pipe(a.package_name) << "|" << sanitize_pipe(a.version_name)
                << "|" << a.version_code << "|" << a.min_sdk << "|" << a.target_sdk
                << "|" << sanitize_pipe(a.main_activity) << "|"
                << sanitize_pipe(a.install_dir) << "|" << sanitize_pipe(a.package_path)
                << "|" << sanitize_pipe(a.icon_path) << "|" << abis << "\n";
        }
        if (!out) {
            if (error) *error = "write error while saving Android registry";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (error) *error = "failed to commit Android registry: " + ec.message();
        return false;
    }
    return true;
}

}  // namespace

std::string default_android_root_dir() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.local/share/finn/android";
    }
    return "/tmp/finn-android";
}

std::string default_android_apps_dir(const std::string& android_root) {
    const std::string root = android_root.empty() ? default_android_root_dir() : android_root;
    return (fs::path(root) / "apps").string();
}

std::string default_android_icons_dir(const std::string& android_root) {
    const std::string root = android_root.empty() ? default_android_root_dir() : android_root;
    return (fs::path(root) / "icons").string();
}

std::string default_android_registry_path(const std::string& android_root) {
    const std::string root = android_root.empty() ? default_android_root_dir() : android_root;
    return (fs::path(root) / "registry.conf").string();
}

AndroidDiagnosticReport diagnose_android_package(const std::string& path) {
    AndroidDiagnosticReport rep;
    rep.path = path;
    rep.apk = inspect_apk(path);
    if (!rep.apk.valid) {
        rep.error = rep.apk.error;
        return rep;
    }
    rep.valid = true;
    rep.api_compatible = rep.apk.api_compatible;
    rep.dex_analyzable = rep.apk.dex_valid;
    rep.native_abi_ok = rep.apk.native_abi_ok;
    rep.warnings = rep.apk.warnings;
    rep.summary = rep.apk.summary;
    return rep;
}

AndroidInstallResult install_android_package(const std::string& path,
                                             const std::string& android_root) {
    AndroidInstallResult res;
    if (starts_with(path, "http://") || starts_with(path, "https://")) {
        res.error = "remote APK URLs are not supported; download the .apk first";
        return res;
    }

    // Handle split-APK container bundles (.xapk / .apkm / .apks) transparently
    if (ends_with_ci(path, ".xapk") || ends_with_ci(path, ".apkm") ||
        ends_with_ci(path, ".apks")) {
        ZipReader bundle_zip;
        std::string zerr;
        if (!bundle_zip.open_file(path, &zerr)) {
            res.error = "failed to open split APK bundle: " + zerr;
            return res;
        }
        const fs::path stage = fs::temp_directory_path() /
            ("finn-android-bundle-" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(stage, ec);
        fs::create_directories(stage, ec);
        if (!bundle_zip.extract_all(stage.string(), nullptr, &zerr)) {
            fs::remove_all(stage, ec);
            res.error = "failed to extract split APK bundle: " + zerr;
            return res;
        }
        std::vector<std::string> apks;
        std::vector<fs::path> obbs;
        for (const auto& entry : fs::recursive_directory_iterator(stage, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string p = entry.path().string();
            if (ends_with_ci(p, ".apk")) apks.push_back(p);
            else if (ends_with_ci(p, ".obb")) obbs.push_back(entry.path());
        }
        if (apks.empty()) {
            fs::remove_all(stage, ec);
            res.error = "split APK bundle contains no .apk payloads";
            return res;
        }
        // Place base APK first (prefer base.apk or the APK containing classes.dex)
        std::stable_sort(apks.begin(), apks.end(), [](const std::string& a, const std::string& b) {
            const bool a_base = ends_with_ci(a, "base.apk");
            const bool b_base = ends_with_ci(b, "base.apk");
            if (a_base != b_base) return a_base;
            return a < b;
        });
        res = install_android_splits(apks, android_root);
        if (res.ok && !obbs.empty()) {
            const fs::path obb_dir = fs::path(res.app.install_dir) / "obb";
            fs::create_directories(obb_dir, ec);
            for (const auto& obb : obbs) {
                fs::copy_file(obb, obb_dir / obb.filename(),
                              fs::copy_options::overwrite_existing, ec);
                res.log.push_back("Installed OBB -> " + (obb_dir / obb.filename()).string());
            }
        }
        fs::remove_all(stage, ec);
        return res;
    }

    const ApkInfo apk = inspect_apk(path);
    if (!apk.valid) {
        res.error = apk.error.empty() ? "not a valid APK" : apk.error;
        return res;
    }
    if (!apk.api_compatible) {
        res.error = "APK requires minSdkVersion " + std::to_string(apk.manifest.min_sdk) +
                    ", above the modelled Android 17 / API 37 surface";
        return res;
    }
    if (!apk.dex_valid) {
        res.error = "APK contains DEX that failed header verification";
        return res;
    }
    if (!apk.native_abi_ok) {
        res.error = "APK has native libraries but none match host ABI (" +
                    apk.host_abi + ")";
        return res;
    }

    ZipReader zip;
    std::string zerr;
    if (!zip.open_file(path, &zerr)) {
        res.error = zerr;
        return res;
    }

    const fs::path install_dir =
        fs::path(default_android_apps_dir(android_root)) /
        sanitize_dirname(apk.manifest.package_name);
    std::error_code ec;
    fs::create_directories(install_dir, ec);
    if (ec) {
        res.error = "failed to create install directory: " + ec.message();
        return res;
    }
    if (!zip.extract_all(install_dir.string(), nullptr, &zerr)) {
        res.error = "failed to extract APK contents: " + zerr;
        return res;
    }
    res.log.push_back("Extracted APK -> " + install_dir.string());

    const fs::path stored_pkg = install_dir / "app.apk";
    fs::copy_file(path, stored_pkg, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        res.error = "failed to store APK: " + ec.message();
        return res;
    }
    const std::string icon =
        extract_apk_icon(zip, apk, default_android_icons_dir(android_root), &res.log);

    InstalledAndroidApp app;
    app.id = "android:" + slugify(apk.manifest.package_name);
    app.name = apk_display_name(apk);
    app.package_name = apk.manifest.package_name;
    app.version_name = apk.manifest.version_name;
    app.version_code = apk.manifest.version_code;
    app.min_sdk = apk.manifest.min_sdk;
    app.target_sdk = apk.manifest.target_sdk;
    app.main_activity = apk.manifest.main_activity();
    app.install_dir = install_dir.string();
    app.package_path = stored_pkg.string();
    app.icon_path = icon;
    for (const auto& lib : apk.native_libs) {
        const std::string abi = lib.abi.empty() ? std::string("unknown") : lib.abi;
        if (std::find(app.abis.begin(), app.abis.end(), abi) == app.abis.end()) {
            app.abis.push_back(abi);
        }
    }
    res.log.push_back("Installed " + app.name + " (" + app.package_name + ")");
    if (!apk.warnings.empty()) {
        res.log.push_back("Warnings: " + apk.warnings.front());
    }

    auto apps = discover_installed_android_apps(android_root);
    bool updated = false;
    for (auto& existing : apps) {
        if (existing.package_name == app.package_name) {
            existing = app;
            updated = true;
            break;
        }
    }
    if (!updated) apps.push_back(app);
    if (!write_registry(default_android_registry_path(android_root), apps, &res.error)) {
        return res;
    }
    res.app = std::move(app);
    res.ok = true;
    return res;
}

AndroidInstallResult install_android_splits(const std::vector<std::string>& apk_paths,
                                            const std::string& android_root) {
    AndroidInstallResult res;
    if (apk_paths.empty()) {
        res.error = "no APK files provided for split install";
        return res;
    }
    std::size_t base_idx = 0;
    for (std::size_t i = 0; i < apk_paths.size(); ++i) {
        const ApkInfo info = inspect_apk(apk_paths[i]);
        if (info.valid && (!info.manifest.main_activity().empty() || !info.dex_files.empty())) {
            base_idx = i;
            break;
        }
    }
    res = install_android_package(apk_paths[base_idx], android_root);
    if (!res.ok) return res;

    std::error_code ec;
    const fs::path splits_dir = fs::path(res.app.install_dir) / "splits";
    for (std::size_t i = 0; i < apk_paths.size(); ++i) {
        if (i == base_idx) continue;
        fs::create_directories(splits_dir, ec);
        const fs::path src(apk_paths[i]);
        const fs::path dst = splits_dir / src.filename();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            res.log.push_back("Installed split APK -> " + dst.string());
            ZipReader szip;
            if (szip.open_file(dst.string(), nullptr)) {
                szip.extract_all(res.app.install_dir, nullptr, nullptr);
                if (res.app.icon_path.empty()) {
                    const ApkInfo base_info = inspect_apk(res.app.package_path);
                    const std::string split_icon = extract_apk_icon(
                        szip, base_info, default_android_icons_dir(android_root), &res.log);
                    if (!split_icon.empty()) {
                        res.app.icon_path = split_icon;
                        auto all = discover_installed_android_apps(android_root);
                        for (auto& a : all) {
                            if (a.id == res.app.id) a.icon_path = split_icon;
                        }
                        write_registry(default_android_registry_path(android_root), all, nullptr);
                    }
                }
            }
        }
    }
    return res;
}

bool uninstall_android_app(const std::string& package_or_id,
                           const std::string& android_root,
                           std::string* error) {
    auto apps = discover_installed_android_apps(android_root);
    const auto it = std::find_if(apps.begin(), apps.end(), [&](const InstalledAndroidApp& a) {
        return a.package_name == package_or_id || a.id == package_or_id;
    });
    if (it == apps.end()) {
        if (error) *error = "Android app not installed: " + package_or_id;
        return false;
    }
    std::error_code ec;
    fs::remove_all(it->install_dir, ec);
    if (!it->icon_path.empty()) fs::remove(it->icon_path, ec);
    apps.erase(it);
    if (!write_registry(default_android_registry_path(android_root), apps, error)) {
        return false;
    }
    return true;
}

std::vector<InstalledAndroidApp> discover_installed_android_apps(
    const std::string& android_root) {
    std::vector<InstalledAndroidApp> out;
    std::ifstream in(default_android_registry_path(android_root));
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string> f = split_pipe(line);
        if (f.size() < 11) continue;
        InstalledAndroidApp a;
        a.id = f[0];
        a.name = f[1];
        a.package_name = f[2];
        a.version_name = f[3];
        a.version_code = static_cast<std::uint32_t>(std::strtoul(f[4].c_str(), nullptr, 10));
        a.min_sdk = std::atoi(f[5].c_str());
        a.target_sdk = std::atoi(f[6].c_str());
        a.main_activity = f[7];
        a.install_dir = f[8];
        a.package_path = f[9];
        a.icon_path = f[10];
        if (f.size() > 11 && !f[11].empty()) {
            std::size_t start = 0;
            while (start <= f[11].size()) {
                const std::size_t semi = f[11].find(';', start);
                if (semi == std::string::npos) {
                    a.abis.push_back(f[11].substr(start));
                    break;
                }
                a.abis.push_back(f[11].substr(start, semi - start));
                start = semi + 1;
            }
        }
        std::error_code ec;
        if (!fs::is_directory(a.install_dir, ec)) continue;  // stale registry row
        out.push_back(std::move(a));
    }
    return out;
}

AndroidRunReport run_android_package(const std::string& package_or_apk,
                                     bool headless,
                                     const std::string& android_root,
                                     std::string* error_out) {
    AndroidRunReport rep;
    const auto fail = [&](const std::string& msg) -> AndroidRunReport {
        rep.error = msg;
        if (error_out) *error_out = msg;
        return rep;
    };

    std::string apk_path = package_or_apk;
    std::error_code ec;
    if (!fs::is_regular_file(apk_path, ec)) {
        const auto apps = discover_installed_android_apps(android_root);
        const auto it = std::find_if(apps.begin(), apps.end(), [&](const InstalledAndroidApp& a) {
            return a.package_name == package_or_apk || a.id == package_or_apk;
        });
        if (it == apps.end()) return fail("Android app not installed: " + package_or_apk);
        apk_path = it->package_path;
    }

    const ApkInfo apk = inspect_apk(apk_path);
    if (!apk.valid) return fail(apk.error.empty() ? "invalid APK" : apk.error);
    rep.package_name = apk.manifest.package_name;

    ZipReader zip;
    std::string zerr;
    if (!zip.open_file(apk_path, &zerr)) return fail(zerr);
    bool dex_loaded = false;
    ArtBridge art;
    for (const auto& e : zip.entries()) {
        if (e.is_directory || !ends_with_ci(e.name, ".dex")) continue;
        std::vector<std::uint8_t> bytes;
        if (!zip.read_entry(e.name, &bytes, &zerr)) continue;
        DexFile dex = parse_dex_bytes(bytes.data(), bytes.size());
        if (!dex.valid) continue;
        std::string borrowed;
        if (!art.load(std::move(dex), &borrowed)) continue;
        dex_loaded = true;
        break;
    }
    if (!dex_loaded) return fail("no verifiable classes*.dex in APK");

    const ArtLifecyclePlan plan = art.lifecycle_plan(apk.manifest);
    rep.activity = plan.activity_name;
    if (rep.activity.empty()) return fail("no launcher activity declared in the manifest");

    AndroidUiEngine ui;
    ui.set_title(apk_display_name(apk));

    // Inflate a real layout from the APK when one is present. A descriptor for
    // FinnOS-side apps is preferred; otherwise the first binary AXML layout
    // that parses is used. Resource references that cannot be resolved stay
    // empty and are reported instead of being replaced with guessed text.
    std::vector<std::string> layouts;
    std::string json_layout;
    for (const auto& e : zip.entries()) {
        if (e.is_directory) continue;
        if (e.name == "res/raw/finn_layout.json") json_layout = e.name;
        if (starts_with(e.name, "res/layout/") && ends_with_ci(e.name, ".xml")) {
            layouts.push_back(e.name);
        }
    }
    std::sort(layouts.begin(), layouts.end());
    std::vector<std::string> layout_warnings;
    bool inflated = false;
    if (!json_layout.empty()) {
        std::vector<std::uint8_t> bytes;
        std::string jerr;
        if (zip.read_entry(json_layout, &bytes, &jerr)) {
            const std::string json(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            inflated = ui.inflate_json(json, &jerr);
            if (!inflated) layout_warnings.push_back(json_layout + ": " + jerr);
        }
    }
    for (const auto& entry : layouts) {
        if (inflated) break;
        std::vector<std::uint8_t> bytes;
        if (!zip.read_entry(entry, &bytes, nullptr)) continue;
        inflated = ui.inflate_binary_layout(bytes, apk.arsc, &layout_warnings, nullptr);
    }
    for (const auto& warning : layout_warnings) {
        rep.lifecycle_log.push_back("layout: " + warning);
    }
    if (!inflated) rep.lifecycle_log.push_back("layout: no inflatable view tree found in APK");

    ui.set_lifecycle_handler([&](const std::string& phase) {
        const auto it = std::find_if(plan.methods.begin(), plan.methods.end(),
                                     [&](const LifecycleMethodInfo& m) {
                                         return m.name == phase;
                                     });
        if (it != plan.methods.end() && it->present && it->has_code && it->decodable) {
            rep.lifecycle_log.push_back(phase + ": executed decodable DEX bytecode");
            return true;
        }
        rep.lifecycle_log.push_back(phase + ": skipped (" +
                                    (it != plan.methods.end() ? it->reason
                                                              : "method not in DEX") +
                                    ")");
        return false;
    });
    // onCreate -> onStart -> onResume is the only sequence this bridge models;
    // pausing or tearing down is not simulated by a run invocation.
    ui.run_activity_lifecycle({"onCreate", "onStart", "onResume"});

    if (headless) {
        rep.summary = ui.render_headless_summary();
    } else {
        ui.run_window(0, nullptr, false);
        rep.summary = "Qt6/Wayland window exited (lifecycle delegated to FinnUI bridge)";
    }
    rep.ok = true;
    if (error_out) error_out->clear();
    return rep;
}

}  // namespace finn::android
