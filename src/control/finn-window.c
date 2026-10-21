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
    char req[160];
    if (argc > 1 && argv[1][0])
        snprintf(req, sizeof req, "window.open app=window title=%s", argv[1]);
    else
        snprintf(req, sizeof req, "window.open app=window");

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
