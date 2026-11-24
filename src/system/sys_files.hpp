// Pure C++20 persistence and sysfs parsers for FinnOS settings, recent apps,
// hardware discovery (backlight, battery, network interfaces), and /proc stats.

#ifndef FINN_SYSTEM_SYS_FILES_HPP
#define FINN_SYSTEM_SYS_FILES_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace finn::sys {

struct SettingsData {
    bool dark = false;
    bool reduced_motion = false;
    bool high_contrast = false;
    std::string color_filter = "none";   // none, grayscale, protanopia, deuteranopia, tritanopia, inverted
    bool screen_reader = false;
    bool sticky_keys = false;
    bool filter_keys = false;
    int cursor_size = 28;                // 28, 36, 48, 64
    double text_scale = 1.0;             // 0.85 .. 1.75
    int auto_lock_minutes = 5;           // 0 = never
    bool lock_on_sleep = true;
    std::string accent = "#007BFF";
    std::string avatar_color = "blue";   // blue, indigo, mint, amber, coral, pink, teal, slate
    std::string wallpaper;               // Empty means default wallpaper
    std::string lock_wallpaper;          // Empty means use the desktop wallpaper
    std::string form_factor = "auto";    // auto, desktop, tablet, mobile, tv
    std::string kb_layout = "us";        // XKB layout code (e.g. us, gb, de, fr, jp)
    std::string kb_variant;              // Optional XKB variant (e.g. dvorak, colemak, intl)
    std::string alert_sound = "Default"; // Figma Settings - Sound (71:239)
    int alert_volume = 75;               // 0..100
    bool startup_sound = true;
    bool ui_sounds = true;
    bool volume_feedback = true;
    double scale = 1.0;
    std::vector<std::string> dock_pins;  // Ordered app ids pinned to the dock
};

struct BacklightInfo {
    bool available = false;
    std::string device;
    int brightness = 0;
    int max_brightness = 0;
    int percent = 0;
    std::string sysfs_path;
};

struct BatteryInfo {
    bool available = false;
    std::string name;
    int capacity_percent = 0;
    bool charging = false;
    bool ac_online = false;
    std::string status_text;
};

struct MemoryInfo {
    std::uint64_t total_kib = 0;
    std::uint64_t available_kib = 0;
    std::uint64_t used_kib = 0;
};

struct CpuSample {
    std::string name;
    std::uint64_t idle = 0;
    std::uint64_t total = 0;
};

struct ProcessInfo {
    int pid = 0;
    int ppid = 0;
    std::string name;
    std::string state;
    std::uint64_t rss_kib = 0;
    std::uint64_t cpu_ticks = 0;
};

// Settings helpers
std::string user_settings_path();
SettingsData parse_settings(std::string_view text);
std::string serialize_settings(const SettingsData& data);
SettingsData load_merged_settings();
bool save_settings(const SettingsData& data, std::string* error = nullptr);

// Recent apps tracking (~/.local/state/finn/recent-apps)
std::string recent_apps_path();
std::vector<std::string> load_recent_apps(const std::string& path = {});
bool record_recent_app(std::string_view id, const std::string& path = {},
                       std::size_t max_entries = 16);

// Honest sysfs hardware readers (take optional root dir for unit testing)
BacklightInfo read_backlight(const std::string& sysfs_base = "/sys/class/backlight");
bool write_backlight_percent(int percent,
                             const std::string& sysfs_base = "/sys/class/backlight",
                             std::string* error = nullptr);
BatteryInfo read_battery(const std::string& sysfs_base = "/sys/class/power_supply");

// /proc readers
MemoryInfo parse_meminfo(std::string_view meminfo_text);
MemoryInfo read_meminfo();
std::vector<CpuSample> parse_proc_stat(std::string_view stat_text);
std::vector<CpuSample> read_proc_stat();
std::vector<ProcessInfo> read_processes(const std::string& proc_root = "/proc");

// Inline math helper for Search (returns empty string if not an arithmetic expr)
std::string evaluate_math_expression(std::string_view expr);

}  // namespace finn::sys

#endif
