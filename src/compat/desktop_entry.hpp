// Freedesktop .desktop parser and icon resolver for Linux/Flatpak compatibility.
// Pure C++20 with zero GUI dependencies so finn-session and unit tests share it.

#ifndef FINN_COMPAT_DESKTOP_ENTRY_HPP
#define FINN_COMPAT_DESKTOP_ENTRY_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

struct DesktopAction {
    std::string id;    // The id used in Actions=, e.g. "new-window".
    std::string name;  // Human label from the [Desktop Action] group.
    std::string icon;
    std::string exec;
};

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
    std::vector<std::string> actions;        // Ids advertised in Actions=.
    std::vector<DesktopAction> action_defs;  // Parsed [Desktop Action <id>] groups.
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

// Tokenizes an XDG Exec= line and expands field codes (%u, %U, %f, %F, %i, %c, %k).
// The result is an argv vector for posix_spawn: no shell is invoked, a target is
// one indivisible element, control characters are dropped and unknown field
// codes are stripped rather than handed to the child.
std::vector<std::string> expand_desktop_exec(std::string_view exec_line,
                                              std::string_view icon = {},
                                              std::string_view name = {},
                                              std::string_view desktop_file = {},
                                              std::string_view target_arg = {});

// Resolves a declared desktop action by id and expands its Exec= line with the
// same no-shell rules. Returns an empty argv for an unknown or Exec-less action.
std::optional<DesktopAction> find_desktop_action(const DesktopEntry& entry,
                                                  std::string_view action_id);
std::vector<std::string> expand_desktop_action_exec(const DesktopEntry& entry,
                                                     std::string_view action_id,
                                                     std::string_view target_arg = {});

// Returns standard system + user + Flatpak application directories.
std::vector<std::pair<std::string, std::string>> default_desktop_dirs();

// Scans application directories cleanly, preserving override precedence (earlier wins).
std::vector<DesktopEntry> scan_desktop_dirs(
    const std::vector<std::pair<std::string, std::string>>& dirs,
    bool check_try_exec = true);

// Resolves an icon name (or path) to an existing .svg/.png/.webp file on disk.
// Rejects traversal, control characters, device paths and overlong names.
std::string resolve_icon_path(std::string_view icon_name,
                               const std::vector<std::string>& extra_dirs = {});
bool is_safe_icon_name(std::string_view icon_name);

// Checks whether an executable name or path exists and is executable via PATH.
bool executable_exists(std::string_view program);

}  // namespace finn::compat

#endif
