// Shared filesystem paths and IPC names across FinnOS modules.
// Only paths referenced by at least two modules belong here.

#ifndef FINN_COMMON_PATHS_HPP
#define FINN_COMMON_PATHS_HPP

namespace finn {

inline constexpr const char* kCataloguePath = "/usr/share/finn/apps.registry";
inline constexpr const char* kInstalledPath = "/var/lib/finn/installed";
inline constexpr const char* kSettingsPath = "/var/lib/finn/settings";
inline constexpr const char* kIconDir = "/usr/share/finn/icons";
inline constexpr const char* kPointersDir = "/usr/share/finn/pointers";
inline constexpr const char* kWallpaperDir = "/usr/share/finn/wallpaper";
inline constexpr const char* kFontDir = "/usr/share/fonts/OTF";
inline constexpr const char* kFontPath = "/usr/share/fonts/OTF/FixelDisplay-Regular.otf";

inline constexpr const char* kWaylandPath = "/run/finn/wayland-0";
inline constexpr const char* kXwaylandDisplayFile = "/run/finn/xdisplay";
inline constexpr const char* kCompositorStateDir = "/run/finn/state";

inline constexpr const char* kSessionDirectory = "finn";
inline constexpr const char* kSessionSocket = "session.sock";

inline constexpr const char* kUserSettingsRel = ".config/finn/settings";
inline constexpr const char* kUserAppsRel = ".local/share/finn/apps";
inline constexpr const char* kUserWindowsConfRel = ".local/share/finn/windows.conf";
inline constexpr const char* kRecentAppsRel = ".local/state/finn/recent-apps";
inline constexpr const char* kWinePrefixRel = ".local/share/finn/wine/default";
inline constexpr const char* kOobeCompleteRel = ".config/finn/oobe_complete";
inline constexpr const char* kOobeBinPath = "/usr/local/bin/finn-oobe";

}  // namespace finn

#endif
