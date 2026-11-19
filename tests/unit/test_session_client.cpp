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

void serve_requests(const std::string& path, std::atomic<bool>* ready) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size());
    assert(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    assert(::listen(fd, 8) == 0);
    ready->store(true);

    const char* expected[] = {
        "APP.LIST\n",
        "APP.LIST\n",
        "APP.LAUNCH files\t/tmp/dir\n",
        "APP.OPEN https://finnos.org/docs\n",
        "COMPAT.STATUS\n",
        "POWER.ACTION bad\n",
    };
    const char* replies[] = {
        "OK count=1\nfiles|1|Finn Files|files.svg\n",
        "OK count=1\nfiles|1|Finn Files|files.svg\n",
        "OK launched\n",
        "OK opened\n",
        "OK arch=aarch64|wayland=/run/finn/wayland-0|display=|xwayland=0|wine=0|flatpak=1|wine_summary=unavailable\n",
        "ERR unsupported power action: bad\n",
    };
    for (int i = 0; i < 6; ++i) {
        const int client = ::accept(fd, nullptr, nullptr);
        assert(client >= 0);
        char request[128] = {};
        assert(::read(client, request, sizeof request - 1) > 0);
        assert(std::strcmp(request, expected[i]) == 0);
        const std::size_t len = std::strlen(replies[i]);
        assert(::write(client, replies[i], len) == static_cast<ssize_t>(len));
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
    std::thread server(serve_requests, path, &ready);
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
    assert(client.launch_with_arg("files", "/tmp/dir", &error));
    assert(client.open_target("https://finnos.org/docs", &error));
    finn::CompatStatus st;
    assert(client.compat_status(&st, &error));
    assert(st.arch == "aarch64");
    assert(st.flatpak_ready);
    assert(!st.wine_ready);
    assert(!client.power_action("bad", &error));
    assert(error == "unsupported power action: bad");
    server.join();
    std::puts("test_session_client: ok");
    return 0;
}
