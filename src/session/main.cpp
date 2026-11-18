// finn-session: FinnOS user-level session broker & application launcher.
// Unifies native FinnOS Qt6 apps, Linux/Flatpak .desktop applications,
// FinnSDK developer apps, and Windows (Wine) executables behind a single
// SO_PEERCRED-protected UNIX socket with posix_spawn process isolation.

#include "common/paths.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/wine_runner.hpp"
#include "session/app_registry.hpp"
#include "session/client.hpp"
#include "system/sys_files.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace {

volatile sig_atomic_t g_quit = 0;

void handle_signal(int) { g_quit = 1; }

void set_cloexec(int fd) {
    if (fd < 0) return;
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) (void)::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void set_socket_timeouts(int fd, int seconds) {
    if (fd < 0) return;
    struct timeval tv{};
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

struct WatchedStamp {
    std::string path;
    bool exists = false;
    long long mtime = 0;
    bool operator==(const WatchedStamp& o) const = default;
};

class RegistryCache {
public:
    const finn::AppRegistry& get() {
        const auto stamp = capture_stamp();
        if (!loaded_ || stamp != stamp_) {
            registry_ = finn::AppRegistry::load_all(finn::kCataloguePath,
                                                    finn::kInstalledPath);
            stamp_ = stamp;
            loaded_ = true;
        }
        return registry_;
    }

private:
    static std::vector<WatchedStamp> capture_stamp() {
        std::vector<std::string> targets = {
            finn::kCataloguePath,
            finn::kInstalledPath,
        };
        if (const char* home = std::getenv("HOME"); home && *home) {
            targets.push_back(std::string(home) + "/" + finn::kUserAppsRel);
            targets.push_back(std::string(home) + "/" + finn::kUserWindowsConfRel);
        }
        for (const auto& [dir, _] : finn::compat::default_desktop_dirs()) {
            targets.push_back(dir);
        }

        std::vector<WatchedStamp> out;
        out.reserve(targets.size());
        std::error_code ec;
        for (const std::string& p : targets) {
            WatchedStamp ws;
            ws.path = p;
            ws.exists = fs::exists(p, ec);
            if (ws.exists) {
                const auto ft = fs::last_write_time(p, ec);
                if (!ec) ws.mtime = ft.time_since_epoch().count();
            }
            out.push_back(std::move(ws));
        }
        return out;
    }

    bool loaded_ = false;
    std::vector<WatchedStamp> stamp_;
    finn::AppRegistry registry_;
};

RegistryCache& registry_cache() {
    static RegistryCache cache;
    return cache;
}

std::vector<std::string> wrap_terminal_if_needed(bool terminal,
                                                 std::vector<std::string> argv) {
    if (!terminal || argv.empty()) return argv;
    std::vector<std::string> wrapped = {finn::kFinnTerminalBin, "-e"};
    wrapped.insert(wrapped.end(), argv.begin(), argv.end());
    return wrapped;
}

void reap_children() {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

std::string sanitize(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back((c == '|' || c == '\n' || c == '\r') ? ' ' : c);
    }
    return out;
}

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool ends_with_ci(std::string_view s, std::string_view ext) {
    if (s.size() < ext.size()) return false;
    return ascii_lower(s.substr(s.size() - ext.size())) == ext;
}

std::string read_xdisplay() {
    std::ifstream in(finn::kXwaylandDisplayFile);
    std::string line;
    if (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        return line;
    }
    const char* env_disp = std::getenv("DISPLAY");
    return env_disp ? std::string(env_disp) : std::string{};
}

bool write_all(int fd, std::string_view text) {
    std::size_t off = 0;
    while (off < text.size()) {
        const ssize_t n = ::write(fd, text.data() + off, text.size() - off);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool read_line(int fd, std::string* line) {
    line->clear();
    char ch = 0;
    while (true) {
        const ssize_t n = ::read(fd, &ch, 1);
        if (n == 1) {
            if (ch == '\n') return true;
            if (line->size() >= 4096) return false;
            line->push_back(ch);
        } else if (n == 0) {
            return !line->empty();
        } else if (errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
}

bool spawn_argv(const std::vector<std::string>& argv,
                const std::vector<std::pair<std::string, std::string>>& extra_env,
                std::string* error) {
    if (argv.empty()) {
        if (error) *error = "empty launch command";
        return false;
    }
    if (!finn::compat::executable_exists(argv[0])) {
        if (error) *error = "executable not found: " + argv[0];
        return false;
    }

    // Build merged child environment preserving user session + Qt FinnOS theme.
    std::vector<std::pair<std::string, std::string>> overrides = {
        {"WAYLAND_DISPLAY", finn::kWaylandPath},
        {"QT_QPA_PLATFORM", "wayland;xcb"},
        {"QT_QPA_PLATFORMTHEME", "finn"},
        {"QT_STYLE_OVERRIDE", "finn"},
        {"XDG_CURRENT_DESKTOP", "FinnOS"},
        {"XDG_SESSION_TYPE", "wayland"},
        {"GDK_BACKEND", "wayland,x11"},
        {"SDL_VIDEODRIVER", "wayland,x11"},
    };
    const std::string xdisp = read_xdisplay();
    if (!xdisp.empty()) overrides.emplace_back("DISPLAY", xdisp);
    for (const auto& kv : extra_env) overrides.push_back(kv);

    std::vector<std::string> env_storage;
    if (environ != nullptr) {
        for (char** ep = environ; *ep != nullptr; ++ep) {
            const std::string entry(*ep);
            const std::size_t eq = entry.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = entry.substr(0, eq);
            bool replaced = false;
            for (const auto& [ok, _] : overrides) {
                if (ok == key) {
                    replaced = true;
                    break;
                }
            }
            if (!replaced) env_storage.push_back(entry);
        }
    }
    for (const auto& [k, v] : overrides) {
        env_storage.push_back(k + "=" + v);
    }

    std::vector<char*> argv_ptrs;
    argv_ptrs.reserve(argv.size() + 1);
    for (const std::string& a : argv) argv_ptrs.push_back(const_cast<char*>(a.c_str()));
    argv_ptrs.push_back(nullptr);

    std::vector<char*> env_ptrs;
    env_ptrs.reserve(env_storage.size() + 1);
    for (std::string& e : env_storage) env_ptrs.push_back(const_cast<char*>(e.c_str()));
    env_ptrs.push_back(nullptr);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    pid_t pid = 0;
    const int rc = ::posix_spawnp(&pid, argv[0].c_str(), nullptr, &attr,
                                  argv_ptrs.data(), env_ptrs.data());
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        if (error) *error = std::string("spawn failed: ") + std::strerror(rc);
        return false;
    }
    return true;
}

bool spawn_app(const finn::App& app, std::string_view arg, std::string* error) {
    if (!app.installed || !app.launchable()) {
        if (error) *error = "app is not installed or launchable: " + app.id;
        return false;
    }

    if (app.source == finn::AppSource::kWindows) {
        std::vector<std::string> extra;
        if (!arg.empty()) extra.emplace_back(arg);
        const auto plan = finn::compat::build_wine_launch(
            app.command, extra, app.desktop_file, read_xdisplay());
        if (!plan.ok) {
            if (error) *error = plan.error;
            return false;
        }
        if (!spawn_argv(plan.argv, plan.env, error)) return false;
        finn::sys::record_recent_app(app.id);
        return true;
    }

    std::vector<std::string> argv;
    if (app.source == finn::AppSource::kDesktop ||
        app.source == finn::AppSource::kFlatpak) {
        argv = finn::compat::expand_desktop_exec(app.command, app.icon, app.name,
                                                 app.desktop_file, arg);
        argv = wrap_terminal_if_needed(app.terminal, std::move(argv));
    } else {
        argv = finn::compat::expand_desktop_exec(app.command, {}, {}, {}, arg);
    }

    if (!spawn_argv(argv, {}, error)) return false;
    finn::sys::record_recent_app(app.id);
    return true;
}

bool open_path_or_url(const finn::AppRegistry& reg, std::string_view target,
                      std::string* error) {
    const std::string t(target);
    if (t.empty()) {
        if (error) *error = "empty target";
        return false;
    }
    if (t.rfind("http://", 0) == 0 || t.rfind("https://", 0) == 0) {
        // Launch first installed Linux browser (.desktop Internet app) or xdg-open.
        for (const auto& app : reg.apps()) {
            if (app.installed && app.source != finn::AppSource::kNative &&
                app.category == "Internet") {
                return spawn_app(app, t, error);
            }
        }
        return spawn_argv({"xdg-open", t}, {}, error);
    }

    std::error_code ec;
    if (fs::is_directory(t, ec)) {
        if (const finn::App* files = reg.find("files")) return spawn_app(*files, t, error);
    }
    if (ends_with_ci(t, ".exe") || ends_with_ci(t, ".msi")) {
        const auto plan =
            finn::compat::build_wine_launch(t, {}, {}, read_xdisplay());
        if (!plan.ok) {
            if (error) *error = plan.error;
            return false;
        }
        return spawn_argv(plan.argv, plan.env, error);
    }
    if (ends_with_ci(t, ".png") || ends_with_ci(t, ".jpg") || ends_with_ci(t, ".jpeg") ||
        ends_with_ci(t, ".webp") || ends_with_ci(t, ".svg") || ends_with_ci(t, ".bmp") ||
        ends_with_ci(t, ".gif")) {
        if (const finn::App* viewer = reg.find("viewer")) return spawn_app(*viewer, t, error);
    }
    if (ends_with_ci(t, ".txt") || ends_with_ci(t, ".md") || ends_with_ci(t, ".cpp") ||
        ends_with_ci(t, ".hpp") || ends_with_ci(t, ".c") || ends_with_ci(t, ".h") ||
        ends_with_ci(t, ".py") || ends_with_ci(t, ".sh") || ends_with_ci(t, ".json") ||
        ends_with_ci(t, ".conf") || ends_with_ci(t, ".ini") || ends_with_ci(t, ".log")) {
        if (const finn::App* notes = reg.find("notes")) return spawn_app(*notes, t, error);
    }
    if (ends_with_ci(t, ".desktop")) {
        const auto entry = finn::compat::parse_desktop_file(t, "desktop", false);
        if (entry.has_value()) {
            auto argv = finn::compat::expand_desktop_exec(
                entry->exec, entry->icon, entry->name, entry->desktop_file, {});
            argv = wrap_terminal_if_needed(entry->terminal, std::move(argv));
            return spawn_argv(argv, {}, error);
        }
    }
    return spawn_argv({"xdg-open", t}, {}, error);
}

bool check_peer_cred(int client_fd) {
#ifdef SO_PEERCRED
    struct ucred cred{};
    socklen_t len = sizeof cred;
    if (::getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        return false;
    }
    return cred.uid == ::getuid() || cred.uid == 0;
#else
    (void)client_fd;
    return true;
#endif
}

std::string build_compat_reply() {
    const auto wine = finn::compat::probe_wine_runtime();
    const std::string xdisp = read_xdisplay();
    const bool has_xwayland = wine.xwayland_available || !xdisp.empty();
    const bool has_flatpak = finn::compat::executable_exists("flatpak");
    return "OK arch=" + wine.host_arch + "|wayland=" + finn::kWaylandPath +
           "|display=" + xdisp + "|xwayland=" + (has_xwayland ? "1" : "0") +
           "|wine=" + (wine.ready ? "1" : "0") +
           "|flatpak=" + (has_flatpak ? "1" : "0") +
           "|wine_summary=" + sanitize(wine.summary) + "\n";
}

void handle_client(int fd) {
    if (!check_peer_cred(fd)) {
        (void)write_all(fd, "ERR permission denied\n");
        return;
    }
    std::string line;
    if (!read_line(fd, &line)) return;

    if (line == "PING") {
        (void)write_all(fd, "OK pong\n");
        return;
    }
    if (line == "QUIT") {
        (void)write_all(fd, "OK bye\n");
        g_quit = 1;
        return;
    }
    if (line == "COMPAT.STATUS") {
        (void)write_all(fd, build_compat_reply());
        return;
    }

    if (line.rfind("WIN.RUN ", 0) == 0) {
        const std::string payload = line.substr(8);
        const std::size_t bar = payload.find('|');
        const std::string exe = bar == std::string::npos ? payload : payload.substr(0, bar);
        const std::string prefix = bar == std::string::npos ? "" : payload.substr(bar + 1);
        const auto plan = finn::compat::build_wine_launch(exe, {}, prefix, read_xdisplay());
        if (!plan.ok) {
            (void)write_all(fd, "ERR " + sanitize(plan.error) + "\n");
            return;
        }
        std::string err;
        if (!spawn_argv(plan.argv, plan.env, &err)) {
            (void)write_all(fd, "ERR " + sanitize(err) + "\n");
            return;
        }
        (void)write_all(fd, "OK launched\n");
        return;
    }

    if (line.rfind("POWER.ACTION ", 0) == 0) {
        const std::string action = line.substr(13);
        std::string err;
        bool ok = false;
        if (action == "reboot") {
            ok = spawn_argv({"systemctl", "reboot"}, {}, &err);
        } else if (action == "poweroff") {
            ok = spawn_argv({"systemctl", "poweroff"}, {}, &err);
        } else if (action == "suspend") {
            ok = spawn_argv({"systemctl", "suspend"}, {}, &err);
        } else {
            err = "unsupported power action: " + action;
        }
        if (!ok) {
            (void)write_all(fd, "ERR " + sanitize(err) + "\n");
            return;
        }
        (void)write_all(fd, "OK done\n");
        return;
    }

    const finn::AppRegistry& reg = registry_cache().get();

    if (line == "APP.LIST") {
        std::string reply = "OK count=" + std::to_string(reg.size()) + "\n";
        for (const finn::App& app : reg.apps()) {
            reply += sanitize(app.id) + "|" + (app.installed ? "1" : "0") + "|" +
                     sanitize(app.name) + "|" + sanitize(app.icon) + "|" +
                     finn::app_source_name(app.source) + "|" +
                     sanitize(app.category) + "|" + sanitize(app.note) + "\n";
        }
        (void)write_all(fd, reply);
        return;
    }

    if (line.rfind("APP.LAUNCH ", 0) == 0) {
        std::string payload = line.substr(11);
        std::string id = payload;
        std::string arg;
        const std::size_t tab = payload.find('\t');
        if (tab != std::string::npos) {
            id = payload.substr(0, tab);
            arg = payload.substr(tab + 1);
        }
        const finn::App* app = reg.find(id);
        if (!app) {
            (void)write_all(fd, "ERR unknown app: " + sanitize(id) + "\n");
            return;
        }
        std::string err;
        if (!spawn_app(*app, arg, &err)) {
            (void)write_all(fd, "ERR " + sanitize(err) + "\n");
            return;
        }
        (void)write_all(fd, "OK launched\n");
        return;
    }

    if (line.rfind("APP.OPEN ", 0) == 0) {
        std::string err;
        if (!open_path_or_url(reg, line.substr(9), &err)) {
            (void)write_all(fd, "ERR " + sanitize(err) + "\n");
            return;
        }
        (void)write_all(fd, "OK opened\n");
        return;
    }

    (void)write_all(fd, "ERR unknown command\n");
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-session");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("usage: finn-session [--list | --launch <id>]\n"
                      "  no argument: run the session broker\n"
                      "  --list:      ask the running broker for the catalogue\n"
                      "  --launch:    ask the running broker to launch an app");
            return 0;
        }
        if (std::strcmp(argv[i], "--list") == 0 ||
            std::strcmp(argv[i], "--launch") == 0) {
            // A client of the running broker, not a second broker.
            const bool launch = std::strcmp(argv[i], "--launch") == 0;
            if (launch && i + 1 >= argc) {
                std::fputs("finn-session: --launch needs an app id\n", stderr);
                return 2;
            }
            finn::SessionClient client;
            std::string err;
            if (!client.connect(&err)) {
                std::fprintf(stderr, "finn-session: %s\n", err.c_str());
                return 1;
            }
            if (launch) {
                if (!client.launch(argv[i + 1], &err)) {
                    std::fprintf(stderr, "finn-session: %s\n", err.c_str());
                    return 1;
                }
                std::puts("OK");
                return 0;
            }
            std::vector<finn::SessionApp> apps;
            if (!client.list(&apps, &err)) {
                std::fprintf(stderr, "finn-session: %s\n", err.c_str());
                return 1;
            }
            for (const auto& a : apps)
                std::printf("%s\t%s\t%s\t%s\n", a.id.c_str(),
                            a.installed ? "installed" : "not-installed",
                            a.source.c_str(), a.name.c_str());
            return 0;
        }
        std::fprintf(stderr, "finn-session: unknown argument: %s\n", argv[i]);
        return 2;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    const std::string sock_path = finn::session_socket_path();
    std::error_code ec;
    fs::create_directories(fs::path(sock_path).parent_path(), ec);
    ::unlink(sock_path.c_str());

    const int server_fd = ::socket(AF_UNIX, SOCK_STREAM
#ifdef SOCK_CLOEXEC
                                              | SOCK_CLOEXEC
#endif
                                   , 0);
    if (server_fd < 0) {
        std::perror("finn-session: socket");
        return 1;
    }
    set_cloexec(server_fd);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sock_path.size() >= sizeof(addr.sun_path)) {
        std::fprintf(stderr, "finn-session: socket path too long: %s\n",
                     sock_path.c_str());
        ::close(server_fd);
        return 1;
    }
    std::memcpy(addr.sun_path, sock_path.c_str(), sock_path.size());
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        std::perror("finn-session: bind");
        ::close(server_fd);
        return 1;
    }
    ::chmod(sock_path.c_str(), 0600);
    if (::listen(server_fd, 32) != 0) {
        std::perror("finn-session: listen");
        ::close(server_fd);
        return 1;
    }
    std::fprintf(stderr, "finn-session: listening on %s\n", sock_path.c_str());

    while (!g_quit) {
        reap_children();
        const int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        set_cloexec(client_fd);
        set_socket_timeouts(client_fd, 2);
        handle_client(client_fd);
        ::close(client_fd);
    }

    ::close(server_fd);
    ::unlink(sock_path.c_str());
    return 0;
}
