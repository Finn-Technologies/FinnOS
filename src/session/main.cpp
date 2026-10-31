// Daemon and CLI entry point for finn-session, the user application broker.
// Manages application execution with sanitized environment blocks and enforces
// user credential boundaries using SO_PEERCRED on UNIX domain sockets.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <spawn.h>

#include "common/paths.hpp"
#include "session/app_registry.hpp"
#include "session/client.hpp"

extern char** environ;

namespace finn {
namespace {

volatile std::sig_atomic_t g_running = 1;

void stop_handler(int) { g_running = 0; }

std::string catalogue_path() {
    const char* value = std::getenv("FINN_APPS");
    return value != nullptr && *value != '\0' ? value : kCataloguePath;
}

std::string installed_path() {
    const char* value = std::getenv("FINN_INSTALLED");
    return value != nullptr && *value != '\0' ? value : kInstalledPath;
}

bool write_all(int fd, std::string_view text) {
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t count = write(fd, text.data() + done, text.size() - done);
        if (count > 0) {
            done += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool read_line(int fd, std::string* line) {
    line->clear();
    char byte = 0;
    while (true) {
        const ssize_t count = read(fd, &byte, 1);
        if (count == 1) {
            if (byte == '\n') return true;
            if (line->size() >= 4096) return false;
            line->push_back(byte);
        } else if (count == 0) {
            return !line->empty();
        } else if (errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
}

std::string sanitise(std::string_view value) {
    std::string out(value);
    for (char& c : out) {
        if (c == '|' || c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

std::string app_list_reply() {
    const auto registry = AppRegistry::load(catalogue_path(), installed_path());
    if (!registry.has_value()) {
        return "ERR catalogue unavailable\n";
    }
    std::string reply = "OK count=" + std::to_string(registry->size()) + "\n";
    for (const App& app : registry->apps()) {
        reply += app.id + "|" + (app.installed ? "1" : "0") + "|" +
                 sanitise(app.name) + "|" + sanitise(app.icon) + "\n";
    }
    return reply;
}

// Constructs a strictly filtered environment and redirects standard streams
// to /dev/null so launched applications cannot pollute or block session daemon IO.
std::string launch_reply(std::string_view id) {
    const auto registry = AppRegistry::load(catalogue_path(), installed_path());
    if (!registry.has_value()) return "ERR catalogue unavailable\n";
    const App* app = registry->find(id);
    if (app == nullptr) return "ERR unknown app\n";
    if (!app->installed) return "ERR not installed\n";
    if (!app->launchable()) return "ERR not a native launch\n";
    if (access(app->command.c_str(), X_OK) != 0) return "ERR command not executable\n";

    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || *runtime == '\0') return "ERR XDG_RUNTIME_DIR is unset\n";

    std::vector<std::string> envs;
    envs.push_back(std::string("WAYLAND_DISPLAY=") + kWaylandPath);
    envs.push_back(std::string("XDG_RUNTIME_DIR=") + runtime);
    envs.push_back("XDG_SESSION_TYPE=wayland");
    envs.push_back("XDG_CURRENT_DESKTOP=FinnOS");
    envs.push_back("PATH=/usr/local/bin:/usr/bin:/bin");
    envs.push_back("FINN_SESSION=1");
    if (const char* dark = std::getenv("FINN_DARK"); dark != nullptr && *dark != '\0') {
        envs.push_back(std::string("FINN_DARK=") + dark);
    }
    if (const char* icons = std::getenv("FINN_ICON_DIR");
        icons != nullptr && *icons != '\0') {
        envs.push_back(std::string("FINN_ICON_DIR=") + icons);
    }
    if (const char* font = std::getenv("FINN_FONT"); font != nullptr && *font != '\0') {
        envs.push_back(std::string("FINN_FONT=") + font);
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') envs.push_back(std::string("HOME=") + home);
    std::vector<char*> envp;
    envp.reserve(envs.size() + 1);
    for (std::string& value : envs) envp.push_back(value.data());
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(app->command.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int result = posix_spawn(&pid, app->command.c_str(), &actions, nullptr,
                                   argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        return "ERR could not start " + app->id + ": " + std::strerror(result) + "\n";
    }
    return "OK pid=" + std::to_string(static_cast<long>(pid)) + "\n";
}

std::string handle_request(std::string_view request) {
    if (request == "PING") return "OK finn-session 1\n";
    if (request == "APP.LIST") return app_list_reply();
    if (request.rfind("APP.LAUNCH ", 0) == 0) {
        const std::string_view id = request.substr(11);
        if (id.empty() || id.find(' ') != std::string_view::npos) {
            return "ERR APP.LAUNCH takes one app id\n";
        }
        return launch_reply(id);
    }
    if (request == "QUIT") return "OK\n";
    return "ERR unknown request\n";
}

std::string socket_path() { return session_socket_path(); }

// Ensures parent directory permissions are restricted to user-only (0700)
// and unlinks dangling sockets from unclean previous shutdowns before binding.
bool prepare_socket(const std::string& path, std::string* error) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code code;
        std::filesystem::create_directories(parent, code);
        if (code) {
            *error = "cannot create " + parent.string() + ": " + code.message();
            return false;
        }
        if (chmod(parent.c_str(), 0700) != 0 && errno != EPERM) {
            *error = std::string("cannot chmod ") + parent.string() + ": " +
                     std::strerror(errno);
            return false;
        }
    }
    struct stat status{};
    if (lstat(path.c_str(), &status) == 0) {
        if (!S_ISSOCK(status.st_mode)) {
            *error = path + " exists and is not a socket";
            return false;
        }
        if (unlink(path.c_str()) != 0) {
            *error = std::string("cannot remove stale socket: ") + std::strerror(errno);
            return false;
        }
    }
    return true;
}

int run_server() {
    const std::string path = socket_path();
    std::string error;
    if (!prepare_socket(path, &error)) {
        std::fprintf(stderr, "finn-session: %s\n", error.c_str());
        return 1;
    }
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::fprintf(stderr, "finn-session: socket path is too long\n");
        return 1;
    }

    const int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        std::fprintf(stderr, "finn-session: socket: %s\n", std::strerror(errno));
        return 1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size());
    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        std::fprintf(stderr, "finn-session: bind %s: %s\n", path.c_str(),
                     std::strerror(errno));
        close(listen_fd);
        return 1;
    }
    chmod(path.c_str(), 0600);
    if (listen(listen_fd, 16) != 0) {
        std::fprintf(stderr, "finn-session: listen: %s\n", std::strerror(errno));
        close(listen_fd);
        return 1;
    }

    struct sigaction action{};
    action.sa_handler = stop_handler;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    std::signal(SIGCHLD, SIG_IGN);

    std::fprintf(stderr, "finn-session: listening on %s\n", path.c_str());
    while (g_running != 0) {
        pollfd poll_fd{};
        poll_fd.fd = listen_fd;
        poll_fd.events = POLLIN;
        const int ready = poll(&poll_fd, 1, 500);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;
        const int fd = accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
#ifdef SO_PEERCRED
        // Rejects connections from processes running under a different UID to prevent
        // unauthorized users from spawning applications within this user's session.
        ucred credentials{};
        socklen_t length = sizeof credentials;
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0 &&
            credentials.uid != getuid()) {
            close(fd);
            continue;
        }
#endif
        std::string request;
        if (read_line(fd, &request)) {
            const std::string reply = handle_request(request);
            write_all(fd, reply);
            if (request == "QUIT") g_running = 0;
        }
        close(fd);
    }

    close(listen_fd);
    unlink(path.c_str());
    return 0;
}

int client_list() {
    SessionClient client;
    std::string error;
    std::vector<SessionApp> apps;
    if (!client.list(&apps, &error)) {
        std::fprintf(stderr, "finn-session: %s\n", error.c_str());
        return 1;
    }
    for (const SessionApp& app : apps) {
        std::printf("%s\t%s\t%s\n", app.id.c_str(),
                    app.installed ? "installed" : "available", app.name.c_str());
    }
    return 0;
}

int client_launch(std::string_view id) {
    SessionClient client;
    std::string error;
    if (!client.launch(id, &error)) {
        std::fprintf(stderr, "finn-session: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

}  // namespace
}  // namespace finn

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--help") == 0) {
        std::puts(
            "usage: finn-session [--list | --launch <id>]\n"
            "\n"
            "With no arguments it runs the session broker. The broker owns the\n"
            "catalogue, the installed list and app launch for this user.\n"
            "\n"
            "Environment: XDG_RUNTIME_DIR, FINN_APPS, FINN_INSTALLED,\n"
            "             FINN_SESSION_SOCK (tests).");
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::puts("finn-session");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) return finn::client_list();
    if (argc == 3 && std::strcmp(argv[1], "--launch") == 0) {
        return finn::client_launch(argv[2]);
    }
    if (argc != 1) {
        std::fprintf(stderr, "finn-session: unknown arguments\n");
        return 2;
    }
    return finn::run_server();
}
