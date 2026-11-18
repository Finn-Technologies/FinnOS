// Shared filesystem paths and IPC names across FinnOS modules.
// Only paths referenced by at least two modules belong here.

#ifndef FINN_COMMON_PATHS_HPP
#define FINN_COMMON_PATHS_HPP

#include <cstdlib>
#include <string>

namespace finn {

inline constexpr const char* kCataloguePath = "/usr/share/finn/apps.registry";
inline constexpr const char* kInstalledPath = "/var/lib/finn/installed";
inline constexpr const char* kSettingsPath = "/var/lib/finn/settings";
inline constexpr const char* kIconDir = "/usr/share/finn/icons";
inline constexpr const char* kIconsDir = kIconDir;
inline constexpr const char* kPointersDir = "/usr/share/finn/pointers";
inline constexpr const char* kWallpaperDir = "/usr/share/finn/wallpaper";
inline constexpr const char* kDefaultWallpaperPath = "/usr/share/finn/wallpaper/default.webp";
inline constexpr const char* kFontDir = "/usr/share/fonts/OTF";
inline constexpr const char* kFontPath = "/usr/share/fonts/OTF/FixelDisplay-Regular.otf";

inline constexpr const char* kWaylandPath = "/run/finn/wayland-0";
inline constexpr const char* kXwaylandDisplayFile = "/run/finn/xdisplay";
inline constexpr const char* kCompositorStateDir = "/run/finn/state";
// finn-comp writes this when Super+L or the idle timer asks for a lock;
// finn-shell watches the state directory, opens the lock overlay and removes
// the file. It is a request, not an assertion that the screen is locked.
inline constexpr const char* kLockRequestPath = "/run/finn/state/lock-request";

inline constexpr const char* kSessionDirectory = "finn";
inline constexpr const char* kSessionSocket = "session.sock";

inline constexpr const char* kUserSettingsRel = ".config/finn/settings";
inline constexpr const char* kUserAppsRel = ".local/share/finn/apps";
inline constexpr const char* kUserWindowsConfRel = ".local/share/finn/windows.conf";
inline constexpr const char* kRecentAppsRel = ".local/state/finn/recent-apps";
inline constexpr const char* kWinePrefixRel = ".local/share/finn/wine/default";
inline constexpr const char* kOobeCompleteRel = ".config/finn/oobe_complete";
inline constexpr const char* kOobeBinPath = "/usr/local/bin/finn-oobe";
inline constexpr const char* kFinnTerminalBin = "/usr/local/bin/finn-terminal";
inline constexpr const char* kFinnInstallBin = "/usr/local/bin/finn-install";
inline constexpr const char* kFinnUpdateBin = "/usr/local/bin/finn-update";

/// Resolves the active user settings file path via FINN_SETTINGS_PATH, $HOME, or /home/finn.
inline std::string user_settings_path() {
    if (const char* override_path = std::getenv("FINN_SETTINGS_PATH");
        override_path && *override_path) {
        return override_path;
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/" + kUserSettingsRel;
    }
    return std::string("/home/finn/") + kUserSettingsRel;
}

}  // namespace finn

#endif
