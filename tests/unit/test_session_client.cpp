#include "session/client.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

void serve_two(const std::string& path, std::atomic<bool>* ready) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size());
    assert(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    assert(::listen(fd, 2) == 0);
    ready->store(true);
    for (int i = 0; i < 2; ++i) {
        const int client = ::accept(fd, nullptr, nullptr);
        assert(client >= 0);
        char request[64] = {};
        assert(::read(client, request, sizeof request - 1) > 0);
        assert(std::strcmp(request, "APP.LIST\n") == 0);
        const char reply[] = "OK count=1\nfiles|1|Finn Files|files.svg\n";
        assert(::write(client, reply, sizeof reply - 1) ==
               static_cast<ssize_t>(sizeof reply - 1));
        ::close(client);
    }
    ::close(fd);
    ::unlink(path.c_str());
}

}  // namespace

int main() {
    const std::string path =
        "/tmp/finn-session-client-" + std::to_string(static_cast<long>(::getpid())) +
        ".sock";
    ::unlink(path.c_str());
    setenv("FINN_SESSION_SOCK", path.c_str(), 1);

    std::atomic<bool> ready{false};
    std::thread server(serve_two, path, &ready);
    while (!ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    finn::SessionClient client;
    std::string error;
    for (int i = 0; i < 2; ++i) {
        std::vector<finn::SessionApp> apps;
        if (!client.list(&apps, &error)) {
            std::fprintf(stderr, "list %d: %s\n", i, error.c_str());
            return 1;
        }
        assert(apps.size() == 1);
        assert(apps[0].id == "files");
        assert(apps[0].installed);
    }
    server.join();
    std::puts("test_session_client: ok");
    return 0;
}
