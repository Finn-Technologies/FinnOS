// Freedesktop .desktop parser and icon resolver for Linux/Flatpak compatibility.
// Pure C++20 with zero GUI dependencies so finn-session and unit tests share it.

#ifndef FINN_COMPAT_DESKTOP_ENTRY_HPP
#define FINN_COMPAT_DESKTOP_ENTRY_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

struct DesktopEntry {
    std::string id;           // Normalized launch id, e.g. "linux:firefox" or "flatpak:org.gimp.GIMP"
    std::string desktop_id;   // Filename stem, e.g. "firefox"
    std::string name;
    std::string generic_name;
    std::string comment;
    std::string exec;
    std::string try_exec;
    std::string icon;         // Icon name or resolved absolute path
    std::string source;       // "desktop" or "flatpak"
    std::string category;     // Primary normalized FinnOS category
    std::string desktop_file; // Absolute path to the .desktop file
    std::vector<std::string> categories;
    std::vector<std::string> mime_types;
    bool terminal = false;
    bool no_display = false;
    bool hidden = false;
};

// Parses a single .desktop file. Returns nullopt if invalid, hidden, NoDisplay,
// non-Application Type, or TryExec fails to resolve (when check_try_exec is true).
std::optional<DesktopEntry> parse_desktop_file(const std::string& path,
                                               std::string_view source = "desktop",
                                               bool check_try_exec = true);

// Parses raw .desktop file contents (for unit tests without filesystem).
std::optional<DesktopEntry> parse_desktop_content(std::string_view content,
                                                  const std::string& path,
                                                  std::string_view source = "desktop",
                                                  bool check_try_exec = false);

// Tokenizes an XDG Exec= line and strips/expands field codes (%u, %U, %f, %F, %i, %c, %k).
std::vector<std::string> expand_desktop_exec(std::string_view exec_line,
                                             std::string_view icon = {},
                                             std::string_view name = {},
                                             std::string_view desktop_file = {},
                                             std::string_view target_arg = {});

// Returns standard system + user + Flatpak application directories.
std::vector<std::pair<std::string, std::string>> default_desktop_dirs();

// Scans application directories cleanly, preserving override precedence (earlier wins).
std::vector<DesktopEntry> scan_desktop_dirs(
    const std::vector<std::pair<std::string, std::string>>& dirs,
    bool check_try_exec = true);

// Resolves an icon name (or path) to an existing .svg/.png/.webp file on disk.
std::string resolve_icon_path(std::string_view icon_name,
                              const std::vector<std::string>& extra_dirs = {});

// Checks whether an executable name or path exists and is executable via PATH.
bool executable_exists(std::string_view program);

}  // namespace finn::compat

#endif
