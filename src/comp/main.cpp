// Entry point for finn-comp, the FinnOS Wayland display server.
// Parses command-line arguments and hands control to the Server event loop
// which manages outputs, seats, and window hierarchies.

#include "comp/server.hpp"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-comp");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts(
                "usage: finn-comp\n"
                "\n"
                "Environment:\n"
                "  XDG_RUNTIME_DIR  where wayland-0 is created (required)\n"
                "  WLR_BACKENDS     drm, headless (wlroots)\n"
                "  WLR_RENDERER     gles2, pixman (wlroots)\n");
            return 0;
        }
        std::fprintf(stderr, "finn-comp: unknown argument: %s\n", argv[i]);
        return 2;
    }

    // Server manages the entire wlroots backend lifecycle, blocking until
    // display termination or fatal compositor error.
    finn::Server server;
    return server.run();
}
