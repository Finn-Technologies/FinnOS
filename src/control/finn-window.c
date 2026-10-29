/*
 * finn-window — the Finn Window launcher binary.
 *
 * Speaks to the control plane over FINN_CONTROL_SOCK to open or focus the
 * Finn Window surface (Figma 12:2) in the compositor.
 */
#include "finnproto.h"
#include "finnsock.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *app = "window";
    const char *title = NULL;

    const char *prog = strrchr(argv[0], '/');
    prog = prog ? prog + 1 : argv[0];
    if (strstr(prog, "settings")) app = "settings";
    else if (strstr(prog, "files")) app = "files";
    else if (strstr(prog, "store")) app = "store";
    else if (strstr(prog, "browser")) app = "browser";
    else if (strstr(prog, "arc")) app = "arc";

    if (argc > 1 && argv[1][0]) {
        if (!strcmp(argv[1], "settings") || !strcmp(argv[1], "files") ||
            !strcmp(argv[1], "store") || !strcmp(argv[1], "browser") ||
            !strcmp(argv[1], "arc") || !strcmp(argv[1], "window")) {
            app = argv[1];
            if (argc > 2 && argv[2][0]) title = argv[2];
        } else {
            title = argv[1];
        }
    }

    char req[160];
    if (title)
        snprintf(req, sizeof req, "window.open app=%s title=%s", app, title);
    else
        snprintf(req, sizeof req, "window.open app=%s", app);

    char reply[256];
    if (finn_call(FINN_CONTROL_SOCK, req, reply, sizeof reply) < 0) {
        fprintf(stderr, "finn-window: cannot reach finnd at %s\n", FINN_CONTROL_SOCK);
        return 1;
    }
    if (strncmp(reply, "OK", 2) != 0) {
        fprintf(stderr, "finn-window: %s\n", reply);
        return 1;
    }
    return 0;
}
