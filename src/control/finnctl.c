/*
 * finnctl — control-plane client for FinnOS.
 *
 *   finnctl state.get
 *   finnctl state.set battery=42 charging=1
 *   finnctl pointer.move x=300 y=400
 *   finnctl screen.capture -o shot.ppm step=2
 *   finnctl log 20
 *
 * Speaks the line protocol from finnproto.h to /run/finn/control.sock
 * (finnd), and to nothing else. There is no automatic fallback: the
 * compositor's socket is the provider underneath the control plane, not a
 * second door, and a device whose control plane is down is unreachable
 * rather than quietly unaudited. `--direct` is an explicit development
 * flag.
 *
 * This is also the transport finn-mcp uses, so every verb is scriptable
 * with nothing but ssh and this binary.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "finnproto.h"
#include "finnsock.h"

static void usage(void) {
    fprintf(stderr,
            "usage: finnctl [--direct] [-o file] <verb> [key=value ...]\n"
            "       finnctl log [lines]\n"
            "\n"
            "verbs: ping version state.get state.set state.clear\n"
            "       screen.info screen.capture screen.pointer screen.shell\n"
            "       pointer.move pointer.by pointer.click pointer.down\n"
            "       pointer.up pointer.scroll power\n"
            "       sound.get sound.set display.get display.set\n"
            "       radio.set appearance.set\n"
            "       notify.post notify.list notify.clear\n"
            "       app.list app.launch app.install app.remove\n"
            "       window.list window.open window.close window.focus\n"
            "       window.minimize window.maximize window.restore\n"
            "       window.get window.move window.resize window.snap\n"
            "       setup.get setup.show setup.complete\n"
            "       settings.page settings.get\n"
            "       key.press key.release key.tap\n"
            "\n"
            "  notify.post takes one argument: '<title>|<body>'\n"
            "  app.launch takes the app's id: 'finnctl app.launch notes'\n"
            "  app.install and app.remove take the catalogue id:\n"
            "    'finnctl app.install store'\n"
            "  power takes one word: reboot, restart, shutdown or poweroff\n"
            "\n"
            "  --direct   development and diagnosis only: talk to the\n"
            "             compositor socket directly. This bypasses finnd\n"
            "             and its audit log, and needs root, because the\n"
            "             compositor's socket is 0600.\n"
            "  -o FILE    write a binary reply (screen.capture) to FILE\n"
            "             use -o - for stdout\n");
}

/* `log` is a host-side concern, not a compositor one. */
static int cmd_log(int lines) {
    char cmd[128];
    snprintf(cmd, sizeof cmd,
             "journalctl -u finn-comp -n %d --no-pager -o cat 2>/dev/null || "
             "tail -n %d /var/log/finn/comp.log 2>/dev/null", lines, lines);
    FILE *p = popen(cmd, "r");
    if (!p) return 1;
    char buf[1024];
    while (fgets(buf, sizeof buf, p)) fputs(buf, stdout);
    return pclose(p) ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *sock = FINN_CONTROL_SOCK;
    int direct = 0;
    const char *outfile = NULL;
    const char *pos[64];
    int npos = 0;

    /* options may appear before or after the verb */
    for (int k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--direct")) { direct = 1; continue; }
        if (!strcmp(argv[k], "-o")) {
            if (k + 1 >= argc) { usage(); return 2; }
            outfile = argv[++k];
            continue;
        }
        if (!strcmp(argv[k], "-h") || !strcmp(argv[k], "--help")) { usage(); return 0; }
        if (npos < (int)(sizeof pos / sizeof pos[0])) pos[npos++] = argv[k];
    }

    if (npos == 0) { usage(); return 2; }

    if (!strcmp(pos[0], "log")) {
        int n = (npos > 1) ? atoi(pos[1]) : 40;
        return cmd_log(n);
    }

    /* assemble the request line */
    char line[FINN_MAX_LINE];
    int n = 0;
    for (int k = 0; k < npos; k++) {
        int w = snprintf(line + n, sizeof line - (size_t)n, "%s%s", k ? " " : "", pos[k]);
        if (w < 0 || n + w >= (int)sizeof line - 2) { fprintf(stderr, "finnctl: request too long\n"); return 2; }
        n += w;
    }
    line[n++] = '\n';
    line[n] = 0;

    if (direct) {
        sock = FINN_COMP_SOCK;
        fprintf(stderr, "finnctl: talking to the compositor directly: this bypasses finnd and its audit log\n");
    }

    int fd = finn_connect(sock);
    if (fd < 0) {
        fprintf(stderr, "finnctl: %s unavailable: %s\n", sock, strerror(errno));
        return 1;
    }

    if (finn_write_all(fd, line, (size_t)n) != 0) { fprintf(stderr, "finnctl: write failed\n"); close(fd); return 1; }

    char reply[FINN_MAX_LINE];
    int r = finn_read_line(fd, reply, sizeof reply);
    if (r == -2) { fprintf(stderr, "finnctl: reply too long\n"); close(fd); return 1; }
    if (r <= 0) { fprintf(stderr, "finnctl: no reply\n"); close(fd); return 1; }

    if (strncmp(reply, "ERR", 3) == 0) { fprintf(stderr, "%s\n", reply); close(fd); return 1; }

    if (strncmp(reply, "OK BINARY ", 10) == 0) {
        long len = atol(reply + 10);
        int out = STDOUT_FILENO;
        int opened = 0;
        if (outfile && strcmp(outfile, "-")) {
            out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) { perror("open"); close(fd); return 1; }
            opened = 1;
        }
        char buf[65536];
        long got = 0;
        while (got < len) {
            size_t want = (size_t)((len - got) < (long)sizeof buf ? (len - got) : (long)sizeof buf);
            ssize_t g = read(fd, buf, want);
            if (g <= 0) break;
            if (finn_write_all(out, buf, (size_t)g) != 0) break;
            got += g;
        }
        if (opened) close(out);
        fprintf(stderr, "finnctl: %ld bytes\n", got);
        close(fd);
        return got == len ? 0 : 1;
    }

    if (!strcmp(reply, "OK")) { close(fd); return 0; }
    if (!strncmp(reply, "OK ", 3)) { printf("%s\n", reply + 3); close(fd); return 0; }

    printf("%s\n", reply);
    close(fd);
    return 0;
}
