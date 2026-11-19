// Honest launch-environment isolation for third-party Linux applications.
// FinnOS v1 has no enforced per-app sandbox or namespace: this module does real
// work only where a real tool exists — safe argv expansion (no shell), removal
// of loader/plugin hijack variables, icon resolution, and an optional
// bubblewrap argv built only when bwrap is actually installed. Every limitation
// is stated in the returned diagnostics instead of being implied as a feature.

#ifndef FINN_COMPAT_LINUX_SANDBOX_HPP
#define FINN_COMPAT_LINUX_SANDBOX_HPP

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace finn::compat {

struct LinuxSandboxOptions {
    bool third_party = true;    // Apply strict environment isolation.
    bool set_theme = true;      // Apply the FinnOS Qt platform theme and style.
    std::string wayland_display;
    std::string xdisplay;       // Empty when Xwayland has no display yet.
    std::string icon;           // Declared icon; empty when the app has none.
    std::vector<std::string> icon_dirs;  // Extra icon roots for extraction dirs.
};

struct LinuxLaunchEnv {
    std::vector<std::pair<std::string, std::string>> overrides;
    std::vector<std::string> strip;    // Keys removed even when inherited.
    std::vector<std::string> warnings;
    std::string diagnostics;
    std::string icon_path;             // Resolved icon, empty when none exists.
};

struct BwrapPlan {
    bool available = false;    // True only when bwrap is on PATH.
    std::vector<std::string> argv;
    std::string diagnostics;
};

// True for a variable that can redirect code loading in the child (LD_*,
// plugin paths, shell startup hooks). Stripped for third-party apps.
bool is_unsafe_env_key(std::string_view key);

// Tokenizes an Exec= line into an argv vector without ever invoking a shell.
// Field codes are expanded from caller-supplied literals only; shell operators
// and control characters are removed so a hostile desktop file cannot inject a
// second command even if a downstream caller joins the tokens into a string.
std::vector<std::string> expand_linux_exec(std::string_view exec_line,
                                           std::string_view target_arg = {});

// Resolves an icon against system/user icon roots. Returns empty when the icon
// is absent; a missing icon is never replaced with a plausible fake.
std::string resolve_linux_app_icon(std::string_view icon,
                                   const std::vector<std::string>& extra_dirs = {});

// Builds the overrides and strip list used by every spawned Linux app.
LinuxLaunchEnv build_linux_launch_env(const LinuxSandboxOptions& options);

// Applies the plan to an inherited environment and returns the exact
// "KEY=VALUE" vector to hand to posix_spawn.
std::vector<std::string> apply_linux_launch_env(
    const std::vector<std::pair<std::string, std::string>>& inherited,
    const LinuxLaunchEnv& plan);

// Returns a bubblewrap-style command only when bwrap exists. The plan is an
// optional namespace wrapper with honest caveats, not an enforced permission
// sandbox. An empty argv with available=false means no plan is offered.
BwrapPlan build_bwrap_plan(const std::vector<std::string>& command,
                           const LinuxLaunchEnv& env);
std::vector<std::string> build_bwrap_argv(const std::vector<std::string>& command,
                                          const LinuxLaunchEnv& env);

// Headless self-test of the pure helpers. Requires the matching .cpp at link
// time; prints failures to stderr and returns true when all checks pass.
inline bool linux_sandbox_self_test() {
    bool ok = true;
    const auto check = [&](bool condition, const char* what) {
        if (!condition) {
            std::fprintf(stderr, "linux_sandbox_self_test: %s\n", what);
            ok = false;
        }
    };

    check(is_unsafe_env_key("LD_PRELOAD"), "LD_PRELOAD is unsafe");
    check(is_unsafe_env_key("BASH_ENV"), "BASH_ENV is unsafe");
    check(is_unsafe_env_key("QT_PLUGIN_PATH"), "QT_PLUGIN_PATH is unsafe");
    check(!is_unsafe_env_key("WAYLAND_DISPLAY"), "WAYLAND_DISPLAY is safe");

    LinuxSandboxOptions opts;
    opts.third_party = true;
    opts.set_theme = true;
    opts.wayland_display = "wayland-0";
    const LinuxLaunchEnv env = build_linux_launch_env(opts);
    const std::vector<std::string> applied = apply_linux_launch_env(
        {{"LD_PRELOAD", "/tmp/evil.so"}, {"WAYLAND_DISPLAY", "wayland-9"},
         {"HOME", "/home/finn"}},
        env);
    bool has_ld_preload = false;
    bool has_old_display = false;
    bool has_new_display = false;
    bool has_home = false;
    for (const std::string& entry : applied) {
        has_ld_preload |= entry.rfind("LD_PRELOAD=", 0) == 0;
        has_old_display |= entry == "WAYLAND_DISPLAY=wayland-9";
        has_new_display |= entry == "WAYLAND_DISPLAY=wayland-0";
        has_home |= entry == "HOME=/home/finn";
    }
    check(!has_ld_preload && !has_old_display, "unsafe env removed");
    check(has_new_display && has_home, "safe env preserved");

    const std::vector<std::string> argv =
        expand_linux_exec("myapp --url=%u ; rm -rf /", "https://finnos.org");
    check(!argv.empty() && argv[0] == "myapp", "exec program parsed");
    bool injected = false;
    for (const std::string& token : argv) {
        injected |= token.find(';') != std::string::npos;
        injected |= token.find('|') != std::string::npos;
        injected |= token.find('&') != std::string::npos;
        injected |= token.find('`') != std::string::npos;
    }
    check(!injected, "shell operators stripped");
    bool target = false;
    for (const std::string& token : argv) {
        target |= token.find("https://finnos.org") != std::string::npos;
    }
    check(target, "field code expanded from caller literal");

    check(resolve_linux_app_icon("../etc/shadow").empty(), "traversal icon refused");

    const BwrapPlan plan = build_bwrap_plan({"myapp"}, env);
    check(plan.available == !plan.argv.empty(), "bwrap plan honest about availability");
    if (plan.available) {
        check(plan.argv[0] == "bwrap" && plan.argv.back() == "myapp",
              "bwrap plan structure");
    }
    return ok;
}

}  // namespace finn::compat

#endif
