// APK-level inspection for finn-android: combines the ZIP/APK reader, binary
// AndroidManifest (AXML), resources.arsc, DEX headers and native libraries into
// one honest pre-flight report. Clean-room: no Android SDK, no aapt, no
// Waydroid, no fabricated values.
//
// A field with no value stays absent/empty. A reference that cannot be resolved
// is reported as unresolved rather than replaced with a plausible string.

#ifndef FINN_ANDROID_APK_FORMAT_HPP
#define FINN_ANDROID_APK_FORMAT_HPP

#include "android/android_format.hpp"
#include "android/zip_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace finn::android {

struct ApkDexInfo {
    std::string entry;
    std::size_t size = 0;
    bool header_valid = false;
    std::string version;
    bool checksum_valid = false;
    bool signature_valid = false;
    std::string error;
};

struct ApkNativeLibInfo {
    std::string entry;
    std::string abi;
    std::uint16_t elf_machine = kElfMachineNone;
    bool valid_elf = false;
    bool host_compatible = false;
};

struct ApkInfo {
    bool valid = false;
    std::string path;
    ApkManifest manifest;
    ArscInfo arsc;
    bool has_manifest = false;
    bool has_arsc = false;
    std::vector<ApkDexInfo> dex_files;
    std::vector<ApkNativeLibInfo> native_libs;
    std::vector<std::string> resource_entries;
    std::vector<std::string> asset_entries;
    std::vector<std::string> warnings;
    std::string host_abi;
    bool api_compatible = false;
    bool dex_valid = false;
    bool native_abi_ok = true;
    bool runnable = false;
    std::string summary;
    std::string error;
};

// True only when the file is a readable APK with a binary AndroidManifest.xml.
// An extension alone is never treated as proof.
bool is_apk_file(const std::string& path);

// Inspects a local .apk. Never installs, launches or mutates anything.
ApkInfo inspect_apk(const std::string& path);

// Resolves an @string/@id/0xPPTTEEEE reference to a real entry key through the
// APK's resources.arsc. Returns an empty string when unresolvable — the caller
// must not substitute a guessed label.
std::string resolve_apk_resource_key(const std::string& reference,
                                     const ArscInfo& arsc);

// Human-readable application name derived only from real manifest/arsc data.
// Falls back to the package's last segment; never invents a localized string.
std::string apk_display_name(const ApkInfo& info);

// Headless self-test of the clean-room parsers. Builds real in-memory ZIP,
// AXML and DEX instruction buffers; returns true only when every real
// assertion passes. No files are created and no capability is simulated.
bool android_self_test();

}  // namespace finn::android

#endif  // FINN_ANDROID_APK_FORMAT_HPP
