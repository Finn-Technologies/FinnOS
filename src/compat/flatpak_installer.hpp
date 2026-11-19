// Flatpak installer file (.flatpakref, .flatpak, .flatpakrepo) inspection and
// installation planning. Pure C++20 so finn-session, Finn Store, Finn Files,
// and headless unit tests share one implementation.

#ifndef FINN_COMPAT_FLATPAK_INSTALLER_HPP
#define FINN_COMPAT_FLATPAK_INSTALLER_HPP

#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

enum class FlatpakFileKind {
    Unknown,
    FlatpakRef,     // .flatpakref single-click installer descriptor
    FlatpakBundle,  // .flatpak offline single-file bundle
    FlatpakRepo,    // .flatpakrepo remote repository descriptor
};

struct FlatpakFileInfo {
    bool valid = false;
    FlatpakFileKind kind = FlatpakFileKind::Unknown;
    std::string path;
    std::string name;          // App ID (FlatpakRef) or remote stem (FlatpakRepo)
    std::string title;         // Human-readable display title
    std::string url;           // Repository URL when present
    std::string runtime_repo;  // RuntimeRepo URL for .flatpakref
    std::string error;
};

struct FlatpakInstallPlan {
    bool ok = false;
    FlatpakFileKind kind = FlatpakFileKind::Unknown;
    std::string display_name;
    std::string action_label;  // "Install Flatpak", "Install Bundle", "Add Remote"
    std::vector<std::string> argv;
    std::string error;
};

// Detects whether a filename/path is a .flatpakref, .flatpak, or .flatpakrepo file.
FlatpakFileKind detect_flatpak_file_kind(std::string_view path);
std::string flatpak_kind_label(FlatpakFileKind kind);

// Parses raw .flatpakref or .flatpakrepo INI contents (headless unit-test friendly).
FlatpakFileInfo parse_flatpak_descriptor(std::string_view content,
                                         FlatpakFileKind kind,
                                         const std::string& path = {});

// Inspects a local .flatpakref, .flatpak, or .flatpakrepo file on disk.
FlatpakFileInfo inspect_flatpak_file(const std::string& path);

// Builds an unprivileged per-user `flatpak` command to install the file.
// When check_binary is true, verifies `flatpak` exists in PATH.
FlatpakInstallPlan build_flatpak_install_plan(const std::string& path,
                                              bool check_binary = true);

}  // namespace finn::compat

#endif
