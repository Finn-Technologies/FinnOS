// Shared filesystem paths and IPC names across FinnOS modules.
// Only paths referenced by at least two modules belong here.

#ifndef FINN_COMMON_PATHS_HPP
#define FINN_COMMON_PATHS_HPP

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace finn {

inline constexpr const char* kCataloguePath = "/usr/share/finn/apps.registry";
inline constexpr const char* kInstalledPath = "/var/lib/finn/installed";
inline constexpr const char* kSettingsPath = "/var/lib/finn/settings";
inline constexpr const char* kSystemOobeCompletePath = "/var/lib/finn/oobe_complete";
inline constexpr const char* kPrimaryUserPath = "/var/lib/finn/primary_user";
inline constexpr const char* kRecoverySessionMarkerPath = "/run/finn/recovery-session";
inline constexpr const char* kIconDir = "/usr/share/finn/icons";
inline constexpr const char* kIconsDir = kIconDir;
inline constexpr const char* kPointersDir = "/usr/share/finn/pointers";
inline constexpr const char* kWallpaperDir = "/usr/share/finn/wallpaper";
inline constexpr const char* kDefaultWallpaperPath = "/usr/share/finn/wallpaper/default.webp";
inline constexpr const char* kFontDir = "/usr/share/fonts/OTF";
inline constexpr const char* kFontPath = "/usr/share/fonts/OTF/FixelDisplay-Regular.otf";
// Single source of truth for the boot splash mark (base/airootfs/.../mark.png)
// and OOBE intro logo (FinnRibbonAnimation::paintHiIntro) so the transition from
// Plymouth boot splash to first-time setup has zero size jump.
inline constexpr int kBootMarkSizePx = 72;

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
inline constexpr const char* kUserMacosAppsRel = ".local/share/finn/macos/Applications";
inline constexpr const char* kUserMacosIconsRel = ".local/share/finn/macos/icons";
inline constexpr const char* kUserOhosRootRel = ".local/share/finn/ohos";
inline constexpr const char* kUserOhosRegistryRel = ".local/share/finn/ohos/registry.conf";
inline constexpr const char* kUserAndroidRootRel = ".local/share/finn/android";
inline constexpr const char* kUserAndroidRegistryRel = ".local/share/finn/android/registry.conf";
inline constexpr const char* kRecentAppsRel = ".local/state/finn/recent-apps";
// The default Wine prefix is shared by finn-win, finn-session and Finn Files.
// It is the directory finn-win actually initialises (drive_c/system.reg live
// directly here); a "default" subdirectory was never created. Keep tools/finn-win's
// DEFAULT_PREFIX in step with this value (tests/test_wine_prefix_path.py checks).
inline constexpr const char* kWinePrefixRel = ".local/share/finn/wine";
inline constexpr const char* kOobeCompleteRel = ".config/finn/oobe_complete";
inline constexpr const char* kOobeBinPath = "/usr/local/bin/finn-oobe";
inline constexpr const char* kOobeApplyHelperPath = "/usr/local/libexec/finn-oobe-apply";
inline constexpr const char* kFinnTerminalBin = "/usr/local/bin/finn-terminal";
inline constexpr const char* kFinnInstallBin = "/usr/local/bin/finn-install";
inline constexpr const char* kFinnRecoveryBin = "/usr/local/bin/recovery-mode";
inline constexpr const char* kFinnUpdateBin = "/usr/local/bin/finn-update";
inline constexpr const char* kFinnGuestToolsBin = "/usr/local/bin/finn-guest-tools";
inline constexpr const char* kFinnDarwinBin = "/usr/local/bin/finn-darwin";
inline constexpr const char* kFinnOhosBin = "/usr/local/bin/finn-ohos";
inline constexpr const char* kFinnAndroidBin = "/usr/local/bin/finn-android";

/// Returns true when a kernel command line names a live ISO, USB installer, or recovery boot.
inline bool is_live_or_recovery_cmdline(std::string_view cmdline) noexcept {
    std::size_t pos = 0;
    while (pos < cmdline.size()) {
        while (pos < cmdline.size() &&
               (cmdline[pos] == ' ' || cmdline[pos] == '\t' ||
                cmdline[pos] == '\r' || cmdline[pos] == '\n')) {
            ++pos;
        }
        if (pos >= cmdline.size()) break;
        const std::size_t start = pos;
        while (pos < cmdline.size() &&
               cmdline[pos] != ' ' && cmdline[pos] != '\t' &&
               cmdline[pos] != '\r' && cmdline[pos] != '\n') {
            ++pos;
        }
        const std::string_view tok = cmdline.substr(start, pos - start);
        if (tok.rfind("archisobasedir=", 0) == 0 ||
            tok.rfind("archisolabel=", 0) == 0 ||
            tok.rfind("archisodevice=", 0) == 0 ||
            tok == "finn.install" || tok.rfind("finn.install=", 0) == 0 ||
            tok == "finn.recovery" || tok.rfind("finn.recovery=", 0) == 0 ||
            tok == "single" || tok == "rescue" || tok == "emergency" ||
            tok == "systemd.unit=rescue.target" ||
            tok == "systemd.unit=emergency.target") {
            return true;
        }
    }
    return false;
}

/// Decides whether OOBE should auto-launch on startup.
/// Runs strictly on the first normal boot of an installed FinnOS system
/// (never on live/recovery media, and never once system or user completion exists).
inline bool should_run_oobe_on_boot(std::string_view cmdline,
                                    bool live_or_recovery_marker,
                                    bool installed_system_marker,
                                    bool sys_oobe_complete,
                                    bool user_oobe_complete,
                                    const char* env_override = nullptr) noexcept {
    if (env_override && *env_override) {
        const std::string_view v(env_override);
        if (v == "0" || v == "false" || v == "skip") return false;
        if (v == "1" || v == "true" || v == "force") return true;
    }
    if (live_or_recovery_marker || is_live_or_recovery_cmdline(cmdline)) {
        return false;
    }
    if (!installed_system_marker) {
        return false;
    }
    if (sys_oobe_complete || user_oobe_complete) {
        return false;
    }
    return true;
}

/// Directories searched for AppImages. Shared by the session registry and the
/// registry cache so the set of scanned directories cannot drift.
inline std::vector<std::string> default_appimage_dirs() {
    std::vector<std::string> dirs;
    if (const char* home = std::getenv("HOME"); home && *home) {
        dirs.emplace_back(std::string(home) + "/Applications");
        dirs.emplace_back(std::string(home) + "/.local/share/finn/appimages");
    }
    dirs.emplace_back("/opt");
    return dirs;
}

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
