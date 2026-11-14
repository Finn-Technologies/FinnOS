#include "system/sys_files.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,    \
                         __LINE__, #condition);                            \
            return false;                                                  \
        }                                                                  \
    } while (false)

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("finn-sys-test-" + std::to_string(::getpid()));
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    std::string dir_str() const { return path_.string(); }
    void write(std::string_view rel, std::string_view content) const {
        const auto file = path_ / std::string(rel);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file);
        out << content;
    }

private:
    std::filesystem::path path_;
};

bool test_settings_roundtrip() {
    const std::string raw =
        "dark=1\n"
        "accent=#4F46E5\n"
        "dock_pins=settings,files,terminal\n"
        "reduced_motion=1\n"
        "scale=1.25\n";
    const auto s = finn::sys::parse_settings(raw);
    CHECK(s.dark);
    CHECK(s.accent == "#4F46E5");
    CHECK(s.reduced_motion);
    CHECK(s.dock_pins.size() == 3);
    const auto s2 = finn::sys::parse_settings(finn::sys::serialize_settings(s));
    CHECK(s2.dark);
    CHECK(s2.reduced_motion);
    return true;
}

bool test_math_eval() {
    CHECK(finn::sys::evaluate_math_expression("2 + 3 * 4") == "14");
    CHECK(finn::sys::evaluate_math_expression("(10 + 2) / 3") == "4");
    CHECK(finn::sys::evaluate_math_expression("1 / 0").empty());
    CHECK(finn::sys::evaluate_math_expression("hello world").empty());
    return true;
}

bool test_honest_sysfs_readers() {
    const TempDir empty_sys;
    CHECK(!finn::sys::read_backlight(empty_sys.dir_str()).available);
    CHECK(!finn::sys::read_battery(empty_sys.dir_str()).available);

    const TempDir real_sys;
    real_sys.write("intel_backlight/brightness", "400\n");
    real_sys.write("intel_backlight/max_brightness", "800\n");
    const auto bl = finn::sys::read_backlight(real_sys.dir_str());
    CHECK(bl.available);
    CHECK(bl.percent == 50);

    real_sys.write("BAT0/type", "Battery\n");
    real_sys.write("BAT0/capacity", "84\n");
    real_sys.write("BAT0/status", "Discharging\n");
    const auto bat = finn::sys::read_battery(real_sys.dir_str());
    CHECK(bat.available);
    CHECK(bat.capacity_percent == 84);
    CHECK(!bat.charging);
    return true;
}

bool test_proc_parsers() {
    const auto mem = finn::sys::parse_meminfo(
        "MemTotal:        8000000 kB\n"
        "MemFree:         2000000 kB\n"
        "MemAvailable:    5000000 kB\n");
    CHECK(mem.total_kib == 8000000ULL);
    CHECK(mem.available_kib == 5000000ULL);
    CHECK(mem.used_kib == 3000000ULL);

    const auto cpus = finn::sys::parse_proc_stat(
        "cpu  100 10 40 850 0 0 0 0 0 0\n"
        "cpu0 50 5 20 425 0 0 0 0 0 0\n");
    CHECK(cpus.size() == 2);
    CHECK(cpus[0].name == "cpu");
    CHECK(cpus[0].total == 1000ULL);
    CHECK(cpus[0].idle == 850ULL);
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_settings_roundtrip();
    ok &= test_math_eval();
    ok &= test_honest_sysfs_readers();
    ok &= test_proc_parsers();
    if (!ok) return 1;
    std::puts("test_sys_files: ok");
    return 0;
}
