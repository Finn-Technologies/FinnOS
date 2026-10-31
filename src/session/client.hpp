// IPC client library for interacting with finn-session over a UNIX socket.
// Provides type-safe requests for listing installed applications and requesting
// launches through the user-level session broker.

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
};

std::string session_socket_path();

/* The shell and scripts both talk to finn-session through this client. The
 * protocol is line-oriented: one request line, one reply line, and for
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

private:
    int fd_ = -1;
};

}  // namespace finn

#endif
