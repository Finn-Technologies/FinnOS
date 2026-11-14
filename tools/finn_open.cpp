// finn-open: unified CLI opener routing files, URLs, .desktop, and .exe
// targets through finn-session so desktop, Linux, and Windows apps open cleanly.

#include <iostream>
#include <string>

#include "session/client.hpp"

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        std::cout << "Usage: finn-open <file | url | desktop-id>\n";
        return argc < 2 ? 2 : 0;
    }

    finn::SessionClient client;
    std::string err;
    if (!client.connect(&err)) {
        std::cerr << "finn-open: " << err << '\n';
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        if (!client.open_target(argv[i], &err)) {
            std::cerr << "finn-open: " << err << '\n';
            return 1;
        }
    }
    return 0;
}
