#include "system/sys_files.hpp"

#include "common/paths.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::sys {
namespace {

std::string trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

bool read_file_text(const fs::path& p, std::string* out) {
    std::ifstream in(p);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

bool parse_int(std::string_view s, int* out) {
    const std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(t.c_str(), &end, 10);
    if (end == t.c_str() || *end != '\0') return false;
    *out = static_cast<int>(v);
    return true;
}

bool parse_bool(std::string_view s) {
    const std::string t = trim(s);
    return t == "1" || t == "true" || t == "TRUE" || t == "yes";
}

bool write_atomic(const fs::path& target, std::string_view content, std::string* error) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot open " + tmp.string();
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good()) {
            fs::remove(tmp, ec);
            if (error) *error = "write failed for " + target.string();
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (error) *error = "rename failed: " + ec.message();
        return false;
    }
    return true;
}

// Recursive descent calculator for Search bar quick math (+, -, *, /, (), decimals).
struct CalcParser {
    std::string_view s;
    std::size_t i = 0;
    bool ok = true;
    int ops = 0;

    void skip_ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    }

    double parse_expr() {
        double v = parse_term();
        while (ok) {
            skip_ws();
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                const char op = s[i++];
                ++ops;
                const double r = parse_term();
                v = (op == '+') ? (v + r) : (v - r);
            } else {
                break;
            }
        }
        return v;
    }

    double parse_term() {
        double v = parse_factor();
        while (ok) {
            skip_ws();
            if (i < s.size() && (s[i] == '*' || s[i] == '/')) {
                const char op = s[i++];
                ++ops;
                const double r = parse_factor();
                if (op == '*') {
                    v *= r;
                } else {
                    if (r == 0.0) {
                        ok = false;
                        return 0.0;
                    }
                    v /= r;
                }
            } else {
                break;
            }
        }
        return v;
    }

    double parse_factor() {
        skip_ws();
        if (i >= s.size()) {
            ok = false;
            return 0.0;
        }
        if (s[i] == '+') {
            ++i;
            return parse_factor();
        }
        if (s[i] == '-') {
            ++i;
            ++ops;
            return -parse_factor();
        }
        if (s[i] == '(') {
            ++i;
            const double v = parse_expr();
            skip_ws();
            if (i >= s.size() || s[i] != ')') {
                ok = false;
                return 0.0;
            }
            ++i;
            return v;
        }
        const std::size_t start = i;
        bool dot = false;
        while (i < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i])) ||
                (!dot && s[i] == '.'))) {
            if (s[i] == '.') dot = true;
            ++i;
        }
        if (i == start) {
            ok = false;
            return 0.0;
        }
        const std::string num(s.substr(start, i - start));
        return std::strtod(num.c_str(), nullptr);
    }
};

}  // namespace

std::string user_settings_path() {
    if (const char* override_path = std::getenv("FINN_SETTINGS_PATH");
        override_path && *override_path) {
        return override_path;
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/" + kUserSettingsRel;
    }
    return kSettingsPath;
}

SettingsData parse_settings(std::string_view text) {
    SettingsData d;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string val = trim(trimmed.substr(eq + 1));

        if (key == "dark") d.dark = parse_bool(val);
        else if (key == "reduced_motion") d.reduced_motion = parse_bool(val);
        else if (key == "high_contrast") d.high_contrast = parse_bool(val);
        else if (key == "color_filter" && !val.empty()) d.color_filter = val;
        else if (key == "screen_reader") d.screen_reader = parse_bool(val);
        else if (key == "sticky_keys") d.sticky_keys = parse_bool(val);
        else if (key == "filter_keys") d.filter_keys = parse_bool(val);
        else if (key == "cursor_size") {
            int cs = 28;
            if (parse_int(val, &cs) && cs >= 16 && cs <= 96) d.cursor_size = cs;
        } else if (key == "text_scale" && !val.empty()) {
            const double ts = std::strtod(val.c_str(), nullptr);
            if (ts >= 0.75 && ts <= 2.5) d.text_scale = ts;
        } else if (key == "auto_lock_minutes") {
            int alm = 5;
            if (parse_int(val, &alm) && alm >= 0 && alm <= 120) d.auto_lock_minutes = alm;
        } else if (key == "lock_on_sleep") d.lock_on_sleep = parse_bool(val);
        else if (key == "accent" && !val.empty()) d.accent = val;
        else if (key == "avatar_color" && !val.empty()) d.avatar_color = val;
        else if (key == "wallpaper") d.wallpaper = val;
        else if (key == "lock_wallpaper") d.lock_wallpaper = val;
        else if (key == "form_factor" && !val.empty()) {
            if (val == "auto" || val == "desktop" || val == "tablet" ||
                val == "mobile" || val == "phone" || val == "tv") {
                d.form_factor = (val == "phone") ? "mobile" : val;
            }
        } else if (key == "kb_layout" && !val.empty()) {
            d.kb_layout = val;
        } else if (key == "kb_variant") {
            d.kb_variant = val;
        } else if (key == "alert_sound" && !val.empty()) d.alert_sound = val;
        else if (key == "alert_volume") {
            int av = 75;
            if (parse_int(val, &av)) d.alert_volume = std::clamp(av, 0, 100);
        } else if (key == "startup_sound") d.startup_sound = parse_bool(val);
        else if (key == "ui_sounds") d.ui_sounds = parse_bool(val);
        else if (key == "volume_feedback") d.volume_feedback = parse_bool(val);
        else if (key == "scale" && !val.empty()) {
            const double parsed = std::strtod(val.c_str(), nullptr);
            if (parsed >= 0.5 && parsed <= 4.0) d.scale = parsed;
        } else if (key == "dock_pins") {
            d.dock_pins.clear();
            std::stringstream ss(val);
            std::string item;
            while (std::getline(ss, item, ',')) {
                item = trim(item);
                if (!item.empty()) d.dock_pins.push_back(item);
            }
        }
    }
    if (d.dock_pins.empty()) {
        d.dock_pins = {"files", "terminal", "notes", "store", "settings"};
    }
    return d;
}

std::string serialize_settings(const SettingsData& data) {
    std::ostringstream out;
    out << "dark=" << (data.dark ? "1" : "0") << '\n';
    out << "reduced_motion=" << (data.reduced_motion ? "1" : "0") << '\n';
    out << "high_contrast=" << (data.high_contrast ? "1" : "0") << '\n';
    out << "color_filter=" << (data.color_filter.empty() ? "none" : data.color_filter) << '\n';
    out << "screen_reader=" << (data.screen_reader ? "1" : "0") << '\n';
    out << "sticky_keys=" << (data.sticky_keys ? "1" : "0") << '\n';
    out << "filter_keys=" << (data.filter_keys ? "1" : "0") << '\n';
    out << "cursor_size=" << data.cursor_size << '\n';
    out << "text_scale=" << data.text_scale << '\n';
    out << "auto_lock_minutes=" << data.auto_lock_minutes << '\n';
    out << "lock_on_sleep=" << (data.lock_on_sleep ? "1" : "0") << '\n';
    out << "accent=" << (data.accent.empty() ? "#007BFF" : data.accent) << '\n';
    out << "avatar_color=" << (data.avatar_color.empty() ? "blue" : data.avatar_color) << '\n';
    if (!data.wallpaper.empty()) out << "wallpaper=" << data.wallpaper << '\n';
    if (!data.lock_wallpaper.empty()) out << "lock_wallpaper=" << data.lock_wallpaper << '\n';
    out << "form_factor=" << (data.form_factor.empty() ? "auto" : data.form_factor) << '\n';
    out << "kb_layout=" << (data.kb_layout.empty() ? "us" : data.kb_layout) << '\n';
    if (!data.kb_variant.empty()) out << "kb_variant=" << data.kb_variant << '\n';
    out << "alert_sound=" << (data.alert_sound.empty() ? "Default" : data.alert_sound) << '\n';
    out << "alert_volume=" << std::clamp(data.alert_volume, 0, 100) << '\n';
    out << "startup_sound=" << (data.startup_sound ? "1" : "0") << '\n';
    out << "ui_sounds=" << (data.ui_sounds ? "1" : "0") << '\n';
    out << "volume_feedback=" << (data.volume_feedback ? "1" : "0") << '\n';
    out << "scale=" << data.scale << '\n';
    out << "dock_pins=";
    for (std::size_t i = 0; i < data.dock_pins.size(); ++i) {
        if (i > 0) out << ',';
        out << data.dock_pins[i];
    }
    out << '\n';
    return out.str();
}

SettingsData load_merged_settings() {
    std::string raw;
    if (read_file_text(user_settings_path(), &raw)) return parse_settings(raw);
    if (read_file_text(kSettingsPath, &raw)) return parse_settings(raw);
    return parse_settings({});
}

bool save_settings(const SettingsData& data, std::string* error) {
    const std::string body = serialize_settings(data);
    bool user_ok = write_atomic(user_settings_path(), body, error);
    // Keep /var/lib/finn/settings in sync when writable (shared with root finn-comp)
    std::string ignored;
    if (::access(kSettingsPath, W_OK) == 0 ||
        ::access(fs::path(kSettingsPath).parent_path().c_str(), W_OK) == 0) {
        (void)write_atomic(kSettingsPath, body, &ignored);
    }
    return user_ok;
}

std::string recent_apps_path() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/" + kRecentAppsRel;
    }
    return "/tmp/finn-recent-apps";
}

std::vector<std::string> load_recent_apps(const std::string& path) {
    const std::string target = path.empty() ? recent_apps_path() : path;
    std::ifstream in(target);
    if (!in) return {};
    std::vector<std::string> items;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) items.push_back(line);
    }
    return items;
}

bool record_recent_app(std::string_view id, const std::string& path,
                       std::size_t max_entries) {
    if (id.empty()) return false;
    const std::string target = path.empty() ? recent_apps_path() : path;
    std::vector<std::string> items = load_recent_apps(target);
    items.erase(std::remove(items.begin(), items.end(), std::string(id)), items.end());
    items.insert(items.begin(), std::string(id));
    if (items.size() > max_entries) items.resize(max_entries);

    std::ostringstream out;
    for (const auto& item : items) out << item << '\n';
    std::string err;
    return write_atomic(target, out.str(), &err);
}

BacklightInfo read_backlight(const std::string& sysfs_base) {
    BacklightInfo info;
    std::error_code ec;
    if (!fs::is_directory(sysfs_base, ec)) return info;
    std::vector<fs::path> entries;
    for (const auto& e : fs::directory_iterator(sysfs_base, ec)) entries.push_back(e.path());
    std::sort(entries.begin(), entries.end());

    for (const fs::path& dir : entries) {
        std::string cur_s, max_s;
        if (!read_file_text(dir / "brightness", &cur_s)) continue;
        if (!read_file_text(dir / "max_brightness", &max_s)) continue;
        int cur = 0, max_v = 0;
        if (!parse_int(cur_s, &cur) || !parse_int(max_s, &max_v) || max_v <= 0) continue;
        info.available = true;
        info.device = dir.filename().string();
        info.brightness = cur;
        info.max_brightness = max_v;
        info.percent = std::clamp(static_cast<int>(std::lround(100.0 * cur / max_v)), 0, 100);
        info.sysfs_path = dir.string();
        return info;
    }
    return info;
}

bool write_backlight_percent(int percent, const std::string& sysfs_base,
                             std::string* error) {
    const BacklightInfo info = read_backlight(sysfs_base);
    if (!info.available) {
        if (error) *error = "no backlight hardware available";
        return false;
    }
    const int clamped = std::clamp(percent, 5, 100);
    const int raw = std::clamp(
        static_cast<int>(std::lround(clamped * info.max_brightness / 100.0)), 1,
        info.max_brightness);
    std::ofstream out(fs::path(info.sysfs_path) / "brightness");
    if (!out) {
        if (error) *error = "backlight permission denied (" + info.sysfs_path + ")";
        return false;
    }
    out << raw;
    if (!out.good()) {
        if (error) *error = "failed writing backlight value";
        return false;
    }
    return true;
}

BatteryInfo read_battery(const std::string& sysfs_base) {
    BatteryInfo info;
    std::error_code ec;
    if (!fs::is_directory(sysfs_base, ec)) {
        info.status_text = "No battery";
        return info;
    }
    std::vector<fs::path> entries;
    for (const auto& e : fs::directory_iterator(sysfs_base, ec)) entries.push_back(e.path());
    std::sort(entries.begin(), entries.end());

    for (const fs::path& dir : entries) {
        std::string type;
        if (!read_file_text(dir / "type", &type)) continue;
        type = trim(type);
        if (type == "Mains") {
            std::string online;
            if (read_file_text(dir / "online", &online)) info.ac_online = parse_bool(online);
            continue;
        }
        if (type != "Battery") continue;
        std::string cap_s, status_s;
        if (!read_file_text(dir / "capacity", &cap_s)) continue;
        int cap = 0;
        if (!parse_int(cap_s, &cap)) continue;
        (void)read_file_text(dir / "status", &status_s);
        status_s = trim(status_s);

        info.available = true;
        info.name = dir.filename().string();
        info.capacity_percent = std::clamp(cap, 0, 100);
        info.charging = (status_s == "Charging" || status_s == "Full");
        info.status_text = std::to_string(info.capacity_percent) + "% · " +
                           (status_s.empty() ? "Unknown" : status_s);
        return info;
    }
    info.status_text = info.ac_online ? "AC Power" : "No battery";
    return info;
}

MemoryInfo parse_meminfo(std::string_view meminfo_text) {
    MemoryInfo mem;
    std::uint64_t free_kib = 0, buffers_kib = 0, cached_kib = 0;
    bool has_avail = false;
    std::istringstream in{std::string(meminfo_text)};
    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        std::uint64_t val = 0;
        std::istringstream ls(line);
        if (!(ls >> key >> val)) continue;
        if (key == "MemTotal:") mem.total_kib = val;
        else if (key == "MemAvailable:") {
            mem.available_kib = val;
            has_avail = true;
        } else if (key == "MemFree:") free_kib = val;
        else if (key == "Buffers:") buffers_kib = val;
        else if (key == "Cached:") cached_kib = val;
    }
    if (!has_avail) mem.available_kib = free_kib + buffers_kib + cached_kib;
    if (mem.total_kib >= mem.available_kib) {
        mem.used_kib = mem.total_kib - mem.available_kib;
    }
    return mem;
}

MemoryInfo read_meminfo() {
    std::string text;
    if (!read_file_text("/proc/meminfo", &text)) return {};
    return parse_meminfo(text);
}

std::vector<CpuSample> parse_proc_stat(std::string_view stat_text) {
    std::vector<CpuSample> samples;
    std::istringstream in{std::string(stat_text)};
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("cpu", 0) != 0) continue;
        std::istringstream ls(line);
        CpuSample s;
        ls >> s.name;
        std::vector<std::uint64_t> cols;
        std::uint64_t v = 0;
        while (ls >> v) cols.push_back(v);
        if (cols.size() < 4) continue;
        for (std::uint64_t c : cols) s.total += c;
        s.idle = cols[3] + (cols.size() > 4 ? cols[4] : 0);
        samples.push_back(std::move(s));
    }
    return samples;
}

std::vector<CpuSample> read_proc_stat() {
    std::string text;
    if (!read_file_text("/proc/stat", &text)) return {};
    return parse_proc_stat(text);
}

std::vector<ProcessInfo> read_processes(const std::string& proc_root) {
    std::vector<ProcessInfo> procs;
    std::error_code ec;
    if (!fs::is_directory(proc_root, ec)) return procs;
    const long page_kib = ::sysconf(_SC_PAGESIZE) / 1024;
    for (const auto& entry : fs::directory_iterator(proc_root, ec)) {
        const std::string pid_str = entry.path().filename().string();
        if (pid_str.empty() ||
            !std::all_of(pid_str.begin(), pid_str.end(),
                         [](unsigned char c) { return std::isdigit(c) != 0; })) {
            continue;
        }
        std::string stat_text;
        if (!read_file_text(entry.path() / "stat", &stat_text)) continue;
        const std::size_t lp = stat_text.find('(');
        const std::size_t rp = stat_text.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp) continue;

        ProcessInfo p;
        p.pid = std::atoi(pid_str.c_str());
        p.name = stat_text.substr(lp + 1, rp - lp - 1);

        std::istringstream rest(stat_text.substr(rp + 2));
        std::string st;
        int ppid = 0;
        std::uint64_t ignore = 0, utime = 0, stime = 0;
        long rss_pages = 0;
        rest >> st >> ppid;
        p.state = st;
        p.ppid = ppid;
        // Fields after ppid: pgrp(4), session(5), tty_nr(6), tpgid(7), flags(8),
        // minflt(9), cminflt(10), majflt(11), cmajflt(12), utime(13), stime(14)
        for (int idx = 4; idx <= 12; ++idx) rest >> ignore;
        rest >> utime >> stime;
        p.cpu_ticks = utime + stime;
        // cutime(15), cstime(16), priority(17), nice(18), num_threads(19),
        // itrealvalue(20), starttime(21), vsize(22), rss(23)
        for (int idx = 15; idx <= 22; ++idx) rest >> ignore;
        rest >> rss_pages;
        if (rss_pages > 0) p.rss_kib = static_cast<std::uint64_t>(rss_pages) * page_kib;
        procs.push_back(std::move(p));
    }
    return procs;
}

std::string evaluate_math_expression(std::string_view expr) {
    const std::string t = trim(expr);
    if (t.empty()) return {};
    CalcParser p{t};
    const double val = p.parse_expr();
    p.skip_ws();
    if (!p.ok || p.i != t.size() || p.ops == 0 || !std::isfinite(val)) return {};
    if (std::fabs(val - std::round(val)) < 1e-9 &&
        std::fabs(val) < 9e15) {
        return std::to_string(static_cast<long long>(std::llround(val)));
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", val);
    return buf;
}

}  // namespace finn::sys
