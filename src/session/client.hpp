// IPC client library for interacting with finn-session over a UNIX socket.
// Provides type-safe requests for listing, launching, and opening native,
// Linux, Flatpak, and Windows applications plus runtime compatibility status.

#ifndef FINN_SESSION_CLIENT_HPP
#define FINN_SESSION_CLIENT_HPP

#include <string>
#include <string_view>
#include <vector>

namespace finn {

struct SessionApp {
    std::string id;
    std::string name;
    std::string icon;
    bool installed = false;
    std::string source = "native";
    std::string category = "System";
    std::string note;
};

struct CompatStatus {
    std::string arch;
    std::string wayland_display;
    std::string x_display;
    bool xwayland_ready = false;
    bool wine_ready = false;
    bool flatpak_ready = false;
    bool finn_win_ready = false;
    bool wine_prefix_ready = false;
    std::string wine_prefix;
    std::string wine_summary;
};

std::string session_socket_path();

/* The shell, settings, files, and tools talk to finn-session through this client.
 * The protocol is line-oriented: one request line, one reply line, and for
 * APP.LIST that line is followed by one line per app. */
class SessionClient {
public:
    SessionClient() = default;
    ~SessionClient();
    SessionClient(const SessionClient&) = delete;
    SessionClient& operator=(const SessionClient&) = delete;

    bool connect(std::string* error);
    void close();

    bool list(std::vector<SessionApp>* apps, std::string* error);
    bool launch(std::string_view id, std::string* error);
    bool launch_with_arg(std::string_view id, std::string_view arg,
                         std::string* error);
    bool open_target(std::string_view target, std::string* error);
    bool run_windows(std::string_view exe_path, std::string_view prefix,
                     std::string* error);
    bool compat_status(CompatStatus* out, std::string* error);
    bool power_action(std::string_view action, std::string* error);

private:
    bool request_line(std::string_view req, std::string* reply, std::string* error);
    int fd_ = -1;
};

}  // namespace finn

#endif
