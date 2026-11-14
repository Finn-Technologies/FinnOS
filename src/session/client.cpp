// SessionClient implementation for socket communication with finn-session.
// Executes line-oriented IPC transactions with connection-per-request semantics
// to prevent stale socket states across process operations.

#include "session/client.hpp"

#include "common/paths.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace finn {
namespace {

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

// Reads byte-by-byte to avoid consuming past protocol line boundaries.
bool read_line(int fd, std::string* line) {
    line->clear();
    char buffer[1];
    while (true) {
        const ssize_t count = read(fd, buffer, 1);
        if (count == 1) {
            if (buffer[0] == '\n') return true;
            if (line->size() >= 4096) return false;
            line->push_back(buffer[0]);
        } else if (count == 0) {
            return !line->empty();
        } else if (errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
}

std::vector<std::string> split(std::string_view line, char separator) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t end = line.find(separator, start);
        if (end == std::string_view::npos) {
            fields.emplace_back(line.substr(start));
            break;
        }
        fields.emplace_back(line.substr(start, end - start));
        start = end + 1;
    }
    return fields;
}

void set_error(std::string* error, std::string text) {
    if (error != nullptr) *error = std::move(text);
}

std::string strip_newlines(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != '\n' && c != '\r') out.push_back(c);
    }
    return out;
}

}  // namespace

std::string session_socket_path() {
    if (const char* path = std::getenv("FINN_SESSION_SOCK"); path && *path) {
        return path;
    }
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && *runtime != '\0') {
        return std::string(runtime) + "/" + kSessionDirectory + "/" + kSessionSocket;
    }
    return "/tmp/finn-session-" + std::to_string(static_cast<unsigned>(getuid())) +
           ".sock";
}

SessionClient::~SessionClient() { close(); }

void SessionClient::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SessionClient::connect(std::string* error) {
    close();
    const std::string path = session_socket_path();
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        set_error(error, "session socket path is too long");
        return false;
    }
    fd_ = ::socket(AF_UNIX, SOCK_STREAM
#ifdef SOCK_CLOEXEC
                              | SOCK_CLOEXEC
#endif
                              , 0);
    if (fd_ < 0) {
        set_error(error, std::string("cannot create socket: ") + std::strerror(errno));
        return false;
    }
#ifndef SOCK_CLOEXEC
    (void)fcntl(fd_, F_SETFD, FD_CLOEXEC);
#endif
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size());
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        set_error(error, "cannot reach finn-session at " + path + ": " +
                             std::strerror(errno));
        close();
        return false;
    }
    return true;
}

bool SessionClient::request_line(std::string_view req, std::string* reply,
                                 std::string* error) {
    close();
    if (!connect(error)) return false;
    std::string payload = strip_newlines(req);
    payload.push_back('\n');
    if (!write_all(fd_, payload)) {
        set_error(error, "cannot write request to finn-session");
        close();
        return false;
    }
    std::string line;
    if (!read_line(fd_, &line)) {
        set_error(error, "no reply from finn-session");
        close();
        return false;
    }
    close();
    if (line.rfind("OK", 0) != 0) {
        set_error(error, line.rfind("ERR ", 0) == 0 ? line.substr(4) : line);
        return false;
    }
    if (reply) *reply = std::move(line);
    return true;
}

bool SessionClient::list(std::vector<SessionApp>* apps, std::string* error) {
    close();
    if (!connect(error)) return false;
    if (!write_all(fd_, "APP.LIST\n")) {
        set_error(error, "cannot send APP.LIST");
        close();
        return false;
    }
    std::string line;
    if (!read_line(fd_, &line)) {
        set_error(error, "no reply from finn-session");
        close();
        return false;
    }
    if (line.rfind("OK count=", 0) != 0) {
        set_error(error, line.rfind("ERR ", 0) == 0 ? line.substr(4) : line);
        close();
        return false;
    }
    int count = 0;
    if (std::sscanf(line.c_str(), "OK count=%d", &count) != 1 || count < 0 ||
        count > 4096) {
        set_error(error, "malformed APP.LIST reply");
        close();
        return false;
    }
    std::vector<SessionApp> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (!read_line(fd_, &line)) {
            set_error(error, "APP.LIST ended early");
            close();
            return false;
        }
        const std::vector<std::string> fields = split(line, '|');
        if (fields.size() < 4) continue;
        SessionApp app;
        app.id = fields[0];
        app.installed = fields[1] == "1";
        app.name = fields[2];
        app.icon = fields[3];
        if (fields.size() >= 5 && !fields[4].empty()) app.source = fields[4];
        if (fields.size() >= 6 && !fields[5].empty()) app.category = fields[5];
        if (fields.size() >= 7) app.note = fields[6];
        result.push_back(std::move(app));
    }
    close();
    if (apps != nullptr) *apps = std::move(result);
    return true;
}

bool SessionClient::launch(std::string_view id, std::string* error) {
    return request_line("APP.LAUNCH " + std::string(id), nullptr, error);
}

bool SessionClient::launch_with_arg(std::string_view id, std::string_view arg,
                                    std::string* error) {
    if (arg.empty()) return launch(id, error);
    return request_line("APP.LAUNCH " + std::string(id) + "\t" + std::string(arg),
                        nullptr, error);
}

bool SessionClient::open_target(std::string_view target, std::string* error) {
    return request_line("APP.OPEN " + std::string(target), nullptr, error);
}

bool SessionClient::run_windows(std::string_view exe_path, std::string_view prefix,
                                 std::string* error) {
    return request_line("WIN.RUN " + std::string(exe_path) + "|" + std::string(prefix),
                        nullptr, error);
}

bool SessionClient::compat_status(CompatStatus* out, std::string* error) {
    std::string reply;
    if (!request_line("COMPAT.STATUS", &reply, error)) return false;
    if (!out) return true;
    *out = CompatStatus{};
    const auto fields = split(reply.size() > 3 ? reply.substr(3) : "", '|');
    for (const auto& f : fields) {
        const std::size_t eq = f.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = f.substr(0, eq);
        const std::string v = f.substr(eq + 1);
        if (k == "arch") out->arch = v;
        else if (k == "wayland") out->wayland_display = v;
        else if (k == "display") out->x_display = v;
        else if (k == "xwayland") out->xwayland_ready = (v == "1");
        else if (k == "wine") out->wine_ready = (v == "1");
        else if (k == "flatpak") out->flatpak_ready = (v == "1");
        else if (k == "wine_summary") out->wine_summary = v;
    }
    return true;
}

bool SessionClient::power_action(std::string_view action, std::string* error) {
    return request_line("POWER.ACTION " + std::string(action), nullptr, error);
}

}  // namespace finn
