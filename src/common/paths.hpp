#ifndef FINN_COMMON_PATHS_HPP
#define FINN_COMMON_PATHS_HPP

namespace finn {

/* One copy of every path the system shares. A path repeated in two modules is
 * a bug in one of them. */
inline constexpr const char* kCataloguePath = "/usr/share/finn/apps.registry";
inline constexpr const char* kInstalledPath = "/var/lib/finnpkg/installed";
inline constexpr const char* kSettingsPath = "/var/lib/finn/settings";
inline constexpr const char* kIconDir = "/usr/share/finn/icons";
inline constexpr const char* kFontPath =
    "/usr/share/fonts/OTF/FixelDisplay-Regular.otf";
inline constexpr const char* kWaylandDisplay = "wayland-0";
/* The compositor runs as root and owns the display, while apps run as finn.
 * A socket under /run/user/0 is unreachable to finn, so the display lives in
 * the shared runtime directory and clients use an absolute path. */
inline constexpr const char* kWaylandPath = "/run/finn/wayland-0";
inline constexpr const char* kSessionDirectory = "finn";
inline constexpr const char* kSessionSocket = "session.sock";

}  // namespace finn

#endif
