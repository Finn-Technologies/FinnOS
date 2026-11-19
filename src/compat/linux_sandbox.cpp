#include "compat/linux_sandbox.hpp"

#include "compat/desktop_entry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_set>

namespace finn::compat {
namespace {

std::string ascii_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

unsigned char uc(char c) { return static_cast<unsigned char>(c); }

std::string drop_control(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (uc(c) >= 0x20 && uc(c) != 0x7f) out.push_back(c);
    }
    return out;
}

bool is_shell_operator(char c) {
    switch (c) {
        case ';': case '|': case '&': case '`': case '$':
        case '<': case '>': case '\n': case '\r': case '\\':
            return true;
        default:
            return false;
    }
}

void append_args(std::vector<std::string>& argv, std::initializer_list<std::string> args) {
    for (const std::string& arg : args) argv.push_back(arg);
}

}  // namespace

bool is_unsafe_env_key(std::string_view key) {
    const std::string upper = ascii_upper(key);
    // Any LD_* variable can change what the dynamic loader maps into the
    // process; they are all removed rather than maintaining a fragile list.
    if (starts_with(upper, "LD_")) return true;
    static constexpr const char* kExact[] = {
        "GCONV_PATH", "GETCONF_DIR", "HOSTALIASES", "LOCALDOMAIN", "RES_OPTIONS",
        "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "GTK_PATH", "GTK_IM_MODULE",
        "QT_IM_MODULE", "BASH_ENV", "ENV", "CDPATH", "IFS", "SHELLOPTS", "PS4",
        "PROMPT_COMMAND", "GLOBIGNORE", "DYLD_INSERT_LIBRARIES", "PYTHONPATH",
        "PERL5LIB", "NODE_OPTIONS",
    };
    for (const char* name : kExact) {
        if (upper == name) return true;
    }
    return false;
}

std::vector<std::string> expand_linux_exec(std::string_view exec_line,
                                           std::string_view target_arg) {
    const std::string target = drop_control(target_arg);

    // Tokenize with Freedesktop quoting rules ourselves. No shell is invoked,
    // a quoted target stays one argv element, and control characters never
    // reach the child. The caller's literal is inserted only after shell
    // operators have been removed from the Exec= text, so a filename may still
    // legally contain characters such as '$'.
    std::vector<std::string> raw;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool have_token = false;
    for (std::size_t i = 0; i < exec_line.size(); ++i) {
        const char c = exec_line[i];
        if (uc(c) < 0x20 && c != ' ' && c != '\t') continue;
        if (in_single) {
            if (c == '\'') in_single = false;
            else current.push_back(c);
            have_token = true;
            continue;
        }
        if (in_double) {
            if (c == '"') in_double = false;
            else if (c == '\\' && i + 1 < exec_line.size()) current.push_back(exec_line[++i]);
            else current.push_back(c);
            have_token = true;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (have_token) {
                raw.push_back(std::move(current));
                current.clear();
                have_token = false;
            }
            continue;
        }
        have_token = true;
        if (c == '\'') in_single = true;
        else if (c == '"') in_double = true;
        else if (c == '\\' && i + 1 < exec_line.size()) current.push_back(exec_line[++i]);
        else current.push_back(c);
    }
    if (have_token) raw.push_back(std::move(current));

    std::vector<std::string> out;
    out.reserve(raw.size());
    for (const std::string& token : raw) {
        std::string expanded;
        expanded.reserve(token.size() + target.size());
        for (std::size_t i = 0; i < token.size(); ++i) {
            if (token[i] == '%' && i + 1 < token.size()) {
                const char code = token[++i];
                switch (code) {
                    case '%': expanded.push_back('%'); break;
                    case 'f': case 'F': case 'u': case 'U': expanded += target; break;
                    default: break;  // No icon/name/desktop-file data was supplied.
                }
                continue;
            }
            if (!is_shell_operator(token[i])) expanded.push_back(token[i]);
        }
        if (!expanded.empty()) out.push_back(std::move(expanded));
    }
    return out;
}

std::string resolve_linux_app_icon(std::string_view icon,
                                   const std::vector<std::string>& extra_dirs) {
    if (icon.empty()) return {};
    return resolve_icon_path(icon, extra_dirs);
}

LinuxLaunchEnv build_linux_launch_env(const LinuxSandboxOptions& options) {
    LinuxLaunchEnv plan;
    if (options.third_party) {
        plan.strip.emplace_back("LD_PRELOAD");
        plan.strip.emplace_back("LD_LIBRARY_PATH");
        plan.strip.emplace_back("QT_PLUGIN_PATH");
        plan.strip.emplace_back("GCONV_PATH");
    }

    if (!options.wayland_display.empty()) {
        plan.overrides.emplace_back("WAYLAND_DISPLAY", options.wayland_display);
    }
    if (!options.xdisplay.empty()) {
        plan.overrides.emplace_back("DISPLAY", options.xdisplay);
    }

    if (options.set_theme) {
        // Qt6 apps on FinnOS get the same palette, fonts and widget style as a
        // native FinnSDK app. These plugins ship in the base image.
        plan.overrides.emplace_back("QT_QPA_PLATFORM", "wayland;xcb");
        plan.overrides.emplace_back("QT_QPA_PLATFORMTHEME", "finn");
        plan.overrides.emplace_back("QT_STYLE_OVERRIDE", "finn");
        plan.overrides.emplace_back("XDG_CURRENT_DESKTOP", "FinnOS");
        plan.overrides.emplace_back("XDG_SESSION_TYPE", "wayland");
        plan.overrides.emplace_back("GDK_BACKEND", "wayland,x11");
        plan.overrides.emplace_back("SDL_VIDEODRIVER", "wayland,x11");
    }

    if (!options.icon.empty()) {
        plan.icon_path = resolve_linux_app_icon(options.icon, options.icon_dirs);
        if (plan.icon_path.empty()) {
            plan.warnings.emplace_back("Icon '" + options.icon +
                                       "' was not found on any standard icon path; "
                                       "no icon will be shown.");
        }
    }

    plan.warnings.emplace_back(
        "FinnOS v1 has no enforced per-app sandbox or namespace isolation: "
        "third-party apps run with your user authority.");
    plan.warnings.emplace_back(
        "Qt6 apps receive the FinnOS platform theme and style. GTK apps receive "
        "GDK_BACKEND and XDG_CURRENT_DESKTOP only; FinnOS ships no GTK theme.");
    plan.warnings.emplace_back(
        "LD_* loader variables and Qt/GTK plugin paths are removed before launch "
        "to prevent a desktop file or parent process from injecting code.");

    plan.diagnostics = std::string("sandbox=none (honest) theme=") +
                       (options.set_theme ? "finn" : "none") +
                       " isolation=" + (options.third_party ? "env" : "off") +
                       " icon=" + (plan.icon_path.empty() ? "none" : plan.icon_path);
    return plan;
}

std::vector<std::string> apply_linux_launch_env(
    const std::vector<std::pair<std::string, std::string>>& inherited,
    const LinuxLaunchEnv& plan) {
    std::unordered_set<std::string> replaced;
    for (const auto& [key, value] : plan.overrides) {
        (void)value;
        replaced.insert(key);
    }
    for (const std::string& key : plan.strip) replaced.insert(key);

    std::vector<std::string> out;
    out.reserve(inherited.size() + plan.overrides.size() + 2);
    for (const auto& [key, value] : inherited) {
        if (key.empty() || replaced.count(key) > 0) continue;
        if (!plan.strip.empty() && is_unsafe_env_key(key)) continue;
        out.push_back(key + "=" + value);
    }
    for (const auto& [key, value] : plan.overrides) {
        out.push_back(key + "=" + value);
    }

    // Every Freedesktop icon lookup needs these roots. An inherited value is
    // preserved; a missing one is supplied rather than left to chance.
    if (std::none_of(out.begin(), out.end(), [](const std::string& e) {
            return starts_with(e, "PATH=");
        })) {
        out.emplace_back("PATH=/usr/local/bin:/usr/bin:/bin");
    }
    if (std::none_of(out.begin(), out.end(), [](const std::string& e) {
            return starts_with(e, "XDG_DATA_DIRS=");
        })) {
        out.emplace_back("XDG_DATA_DIRS=/usr/local/share:/usr/share");
    }
    return out;
}

BwrapPlan build_bwrap_plan(const std::vector<std::string>& command,
                           const LinuxLaunchEnv& env) {
    BwrapPlan plan;
    if (!executable_exists("bwrap")) {
        plan.diagnostics = "bubblewrap is not installed; no isolation plan is offered.";
        return plan;
    }

    plan.available = true;
    plan.argv = {"bwrap",
                 "--unshare-user", "--unshare-ipc", "--unshare-uts",
                 "--ro-bind", "/usr", "/usr",
                 "--ro-bind", "/etc", "/etc",
                 "--dev", "/dev",
                 "--proc", "/proc",
                 "--tmpfs", "/tmp"};
    if (const char* home = std::getenv("HOME"); home && *home) {
        append_args(plan.argv, {"--bind", home, home});
    }
    for (const auto& [key, value] : env.overrides) {
        append_args(plan.argv, {"--setenv", key, value});
    }
    for (const std::string& key : env.strip) {
        append_args(plan.argv, {"--unsetenv", key});
    }
    plan.argv.emplace_back("--");
    for (const std::string& arg : command) plan.argv.push_back(arg);

    plan.diagnostics =
        "bubblewrap is installed: an optional filesystem-namespace wrapper was "
        "planned. It is not an enforced permission sandbox, kernel settings can "
        "deny unprivileged user namespaces, and FinnOS v1 does not enable this "
        "wrapper by default.";
    return plan;
}

std::vector<std::string> build_bwrap_argv(const std::vector<std::string>& command,
                                          const LinuxLaunchEnv& env) {
    return build_bwrap_plan(command, env).argv;
}

}  // namespace finn::compat
