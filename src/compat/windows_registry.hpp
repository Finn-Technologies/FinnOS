// Wine registry text files (.reg) and minimal Windows shortcut (.lnk) parsing.
// Pure C++20 and headless, shared by finn-session and the headless unit tests.
//
// Honesty: only text registry files are parsed. Binary Windows registry hives
// are not, a registry entry is a claim and not proof that its program exists,
// and .lnk parsing resolves LinkInfo/RelativePath only -- target ID Lists and
// .ico extraction are not implemented and are reported as such.

#ifndef FINN_COMPAT_WINDOWS_REGISTRY_HPP
#define FINN_COMPAT_WINDOWS_REGISTRY_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

struct RegistryValue {
    enum class Kind { String, ExpandString, Dword, Binary, Unknown };

    std::string name;  // empty for the default @= value
    Kind kind = Kind::Unknown;
    std::string text;               // decoded string (or the raw hex text for dword)
    std::uint32_t dword = 0;
    std::vector<std::uint8_t> bytes;  // hex(...) values
};

struct RegistryKey {
    std::string path;  // full key, e.g. HKEY_LOCAL_MACHINE\...\Uninstall\App
    std::vector<RegistryValue> values;

    // Windows registry names are case-insensitive.
    const RegistryValue* find(std::string_view name) const;
    std::string string_value(std::string_view name) const;
};

struct UninstallEntry {
    std::string key;          // last key component; a readable fallback label
    std::string key_path;     // full registry key path
    std::string display_name;
    std::string publisher;
    std::string display_version;
    std::string icon_path;            // DisplayIcon, e.g. C:\App\app.exe,0
    std::string install_location;     // InstallLocation
    std::string uninstall_string;
    std::string quiet_uninstall_string;
    std::string source_reg_file;
    bool is_msi = false;  // GUID key or msiexec-based UninstallString
};

struct WindowsShortcutInfo {
    bool valid = false;
    std::string target_path;
    std::string arguments;
    std::string working_dir;
    std::string icon_location;
    std::vector<std::string> warnings;  // honest limitations of the minimal parser
    std::string error;
};

// Parses a Windows Registry Editor 5.00 / REGEDIT4 text file. Deleted keys and
// deleted values are ignored. Malformed lines are skipped, never guessed at.
std::vector<RegistryKey> parse_wine_reg_file(std::string_view content);

// Filters parsed keys to Uninstall subtrees with usable metadata.
std::vector<UninstallEntry> collect_uninstall_entries(
    const std::vector<RegistryKey>& keys);

// Reads one .reg file; reason receives a short diagnostic on failure.
std::vector<UninstallEntry> collect_uninstall_entries_from_file(
    const std::string& path, std::string* reason = nullptr);

// Reads user.reg, system.reg, userdef.reg and any other .reg file directly
// inside a Wine prefix. reason always receives a human-readable outcome.
std::vector<UninstallEntry> collect_uninstall_entries_from_prefix(
    const std::string& prefix, std::string* reason = nullptr);

// Path helpers. They return Windows-style paths (or an empty string when the
// entry does not carry one); mapping C:\... into a prefix is the caller's job.
std::string uninstall_icon_path(const UninstallEntry& entry);
std::string uninstall_exe_path(const UninstallEntry& entry);

// Minimal IShellLink parser: CLSID, LinkFlags, LinkInfo local base path, and
// NAME/RELATIVE_PATH/WORKING_DIR/ARGUMENTS/ICON_LOCATION string data.
WindowsShortcutInfo parse_windows_shortcut_bytes(std::string_view bytes);
WindowsShortcutInfo parse_windows_shortcut_file(const std::string& path);

// Headless self-test used by tests/unit/test_compat.cpp. Returns true on pass.
bool windows_registry_self_test();

}  // namespace finn::compat

#endif
