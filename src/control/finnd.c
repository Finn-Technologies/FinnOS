/*
 * finnd — the FinnOS control plane.
 *
 * The single, permissioned entry point for controlling a running FinnOS
 * system: state, input, display, power. Clients are scripts, the Settings
 * app, and — when it is explicitly opted into — FinnAI by way of the MCP
 * adapter. finnd itself is small, has no dependencies, and never speaks a
 * protocol it does not own.
 *
 * Design:
 *   - /run/finn/control.sock, mode 0660, root and the finn group.
 *   - No TCP listener. Remote control is an explicit decision, not a default.
 *   - Every call is logged with its peer uid.
 *   - State verbs are handled here so they keep working when the compositor
 *     is restarting; display and input verbs are proxied to finn-comp.
 *
 * This is root-equivalent authority. It is bound to the same intent as the
 * zero-AI-by-default policy: present, capable, and switched off until the
 * user turns it on.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "finnproto.h"
#include "finnsock.h"
#include "finnstate.h"
#include "finnapps.h"

#define LOG_DIR  "/var/log/finn"
#define LOG_FILE LOG_DIR "/control.log"

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

static uid_t g_finn_uid = (uid_t)-1;
static gid_t g_finn_gid = (gid_t)-1;

static void log_call(uid_t uid, const char *verb, const char *result) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    char ts[32];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", &tm);
    fprintf(f, "%s uid=%u %s -> %s\n", ts, (unsigned)uid, verb, result);
    fclose(f);
}

static int peer_uid(int fd, uid_t *out) {
    struct ucred cr;
    socklen_t len = sizeof cr;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &len) < 0) return -1;
    *out = cr.uid;
    return 0;
}

/* relay one reply, including a binary payload, from provider to client */
static int relay(int from, int to) {
    char line[FINN_MAX_LINE];
    int n = finn_read_line(from, line, sizeof line);
    if (n <= 0) { finn_write_str(to, "ERR provider closed\n"); return -1; }

    if (strncmp(line, "OK BINARY ", 10) == 0) {
        long len = atol(line + 10);
        finn_write_str(to, line);
        finn_write_str(to, "\n");
        char buf[65536];
        long got = 0;
        while (got < len) {
            size_t want = (size_t)((len - got) < (long)sizeof buf ? (len - got) : (long)sizeof buf);
            ssize_t g = read(from, buf, want);
            if (g <= 0) break;
            if (finn_write_all(to, buf, (size_t)g) != 0) break;
            got += g;
        }
        return got == len ? 0 : -1;
    }

    finn_write_str(to, line);
    finn_write_str(to, "\n");
    return 0;
}

static int proxy_to_comp(const char *line, int to) {
    int p = finn_connect(FINN_COMP_SOCK);
    if (p < 0) { finn_write_str(to, "ERR compositor unavailable\n"); return -1; }
    finn_write_str(p, line);
    finn_write_str(p, "\n");
    int r = relay(p, to);
    close(p);
    return r;
}

/*
 * Start an app.
 *
 * The child is a child of the control plane and nothing else: it is detached
 * into its own session, it runs as the user rather than as the daemon, and
 * it is reaped by init rather than by us. The daemon answers the request and
 * forgets about it — a program that outlives the request is the whole point
 * of launching one.
 */
static int launch_app(const finn_app *a) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* the child */
        if (g_finn_uid != (uid_t)-1) {
            gid_t gid = g_finn_uid;
            setgroups(0, NULL);
            setgid(gid);
            if (setuid(gid) != 0)
                fprintf(stderr, "finnd: setuid(%u) failed: %s\n",
                        (unsigned)gid, strerror(errno));
        } else {
            fprintf(stderr, "finnd: no finn user; the app runs as root\n");
        }
        setsid();

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }

        execl(a->command, a->command, (char *)NULL);
        _exit(127);
    }
    return 0;
}

/* Returns a short result string for the log. */
static const char *handle(char *line, int fd) {    /* work on a copy: the verb split must not disturb the request that gets
     * forwarded to the provider */
    char work[FINN_MAX_LINE];
    snprintf(work, sizeof work, "%s", line);

    char *rest = strchr(work, ' ');
    if (rest) *rest++ = 0; else rest = work + strlen(work);
    const char *verb = work;

    if (!*verb) { finn_write_str(fd, "ERR empty\n"); return "ERR empty"; }

    if (!strcmp(verb, "ping")) {
        finn_write_str(fd, "OK " FINN_PROTO_MAGIC "\n");
        return "OK";
    }
    if (!strcmp(verb, "version")) {
        finn_write_str(fd, "OK finnd=0.1 proto=" FINN_PROTO_MAGIC "\n");
        return "OK";
    }

    /* ---- state: owned here, so it survives a compositor restart ---- */
    if (!strcmp(verb, "state.get")) {
        /* the compositor merges overrides with real hardware, so prefer it */
        int p = finn_connect(FINN_COMP_SOCK);
        if (p >= 0) {
            finn_write_str(p, "state.get\n");
            int r = relay(p, fd);
            close(p);
            return r == 0 ? "OK" : "ERR relay";
        }
        finn_state st;
        finn_state_read(&st);
        char b[224];
        snprintf(b, sizeof b,
                 "OK battery=%d charging=%d wifi=%d volume=%d brightness=%d "
                 "bluetooth=%d wired=%d dark=%d\n",
                 st.battery_pct, st.charging, st.wifi, st.volume,
                 st.brightness, st.bluetooth, st.wired, st.dark);
        finn_write_str(fd, b);
        return "OK (local)";
    }
    if (!strcmp(verb, "state.set")) {
        char *save = NULL, *tok;
        int n = 0;
        for (tok = strtok_r(rest, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = 0;
            if (finn_state_set(tok, atoi(eq + 1)) == 0) n++;
        }
        if (!n) { finn_write_str(fd, "ERR no recognised keys\n"); return "ERR keys"; }
        finn_write_str(fd, "OK\n");
        return "OK";
    }
    if (!strcmp(verb, "state.clear")) {
        finn_state_clear();
        finn_write_str(fd, "OK\n");
        return "OK";
    }

    /* ---- power ---- */
    if (!strcmp(verb, "power")) {
        char what[32] = "reboot";
        char *save = NULL, *tok = strtok_r(rest, " \t", &save);
        if (tok) snprintf(what, sizeof what, "%s", tok);
        finn_write_str(fd, "OK\n");
        sync();
        if (!strcmp(what, "shutdown") || !strcmp(what, "poweroff")) system("/usr/bin/systemctl poweroff");
        else if (!strcmp(what, "restart-comp")) system("/usr/bin/systemctl restart finn-comp");
        else system("/usr/bin/systemctl reboot");
        return "OK";
    }

    /* ---- applications ----
     *
     * The registry says what an app is; this is where one becomes a process.
     * The control plane is the only place in the OS that starts user
     * programs: the compositor asks it, a script asks it, an agent asks it,
     * and every one of those goes through the same audit log.
     */
    if (!strcmp(verb, "app.list")) {
        int n = finn_apps_load(NULL, NULL);
        char b[2048];
        int used = snprintf(b, sizeof b, "OK count=%d", n < 0 ? 0 : n);
        for (int i = 0; i < n && used < (int)sizeof b - 96; i++) {
            const finn_app *a = finn_apps_at(i);
            if (!a) break;
            used += snprintf(b + used, sizeof b - (size_t)used, " [%s|%s|%s]",
                             a->id, a->installed ? "installed" : "available",
                             a->name);
        }
        snprintf(b + used, sizeof b - (size_t)used, "\n");
        finn_write_str(fd, b);
        return "OK";
    }
    if (!strcmp(verb, "app.launch")) {
        char id[64] = "";
        char *save = NULL, *tok = strtok_r(rest, " \t", &save);
        if (tok) snprintf(id, sizeof id, "%s", tok);

        finn_apps_load(NULL, NULL);
        const finn_app *a = finn_apps_find(id);
        if (!a) {
            finn_write_str(fd, "ERR unknown app\n");
            return "ERR unknown app";
        }
        if (!a->installed) {
            finn_write_str(fd, "ERR not installed\n");
            return "ERR not installed";
        }
        if (!a->command[0] || access(a->command, X_OK) != 0) {
            finn_write_str(fd, "ERR no runnable command\n");
            return "ERR not runnable";
        }
        if (launch_app(a) != 0) {
            finn_write_str(fd, "ERR could not start\n");
            return "ERR spawn failed";
        }
        char b[128];
        snprintf(b, sizeof b, "OK launched %s\n", a->id);
        finn_write_str(fd, b);
        return "OK";
    }

    /* ---- display + input + machine: proxied to the provider ---- */
    if (!strncmp(verb, "pointer.", 8) || !strncmp(verb, "screen.", 7) ||
        !strncmp(verb, "key.", 4) || !strncmp(verb, "sound.", 6) ||
        !strncmp(verb, "display.", 8) || !strncmp(verb, "radio.", 6) ||
        !strncmp(verb, "appearance.", 11) || !strncmp(verb, "notify.", 7)) {
        int r = proxy_to_comp(line, fd);
        return r == 0 ? "OK" : "ERR proxy";
    }

    if (!strncmp(verb, "window.", 7)) {
        finn_write_str(fd, "ERR not implemented yet\n");
        return "ERR unimplemented";
    }

    finn_write_str(fd, "ERR unknown verb\n");
    return "ERR unknown";
}

static void serve(int fd, uid_t uid) {
    char line[FINN_MAX_LINE];
    for (;;) {
        int n = finn_read_line(fd, line, sizeof line);
        if (n <= 0) return;
        char copy[FINN_MAX_LINE];
        snprintf(copy, sizeof copy, "%s", line);
        const char *res = handle(line, fd);
        if (res) log_call(uid, copy, res);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);
    /* apps we start are not our children to wait for: the kernel reaps them
     * so a launch never leaves a zombie behind */
    signal(SIGCHLD, SIG_IGN);

    mkdir("/run/finn", 0755);
    mkdir(FINN_STATE_DIR, 0755);
    mkdir(LOG_DIR, 0755);

    struct passwd *pw = getpwnam("finn");
    if (pw) { g_finn_uid = pw->pw_uid; g_finn_gid = pw->pw_gid; }

    int lfd = finn_listen(FINN_CONTROL_SOCK, 0660);
    if (lfd < 0) {
        fprintf(stderr, "finnd: cannot bind %s: %s\n", FINN_CONTROL_SOCK, strerror(errno));
        return 1;
    }
    /*
     * The socket belongs to root and to the finn group, and to nobody else.
     * The daemon runs as root in root's own group on purpose: systemd takes
     * CAP_SETUID away from a service whose group it had to change, and the
     * control plane needs that capability to start an app as the user rather
     * than as root. The group is therefore set on the socket itself, which is
     * where it actually matters.
     */
    if (g_finn_gid != (gid_t)-1 && chown(FINN_CONTROL_SOCK, 0, g_finn_gid) != 0)
        fprintf(stderr, "finnd: cannot give %s to the finn group: %s\n",
                FINN_CONTROL_SOCK, strerror(errno));
    fprintf(stderr, "finnd: control plane on %s\n", FINN_CONTROL_SOCK);
    fflush(stderr);

    while (!g_stop) {
        struct pollfd pf = { lfd, POLLIN, 0 };
        if (poll(&pf, 1, 500) <= 0) continue;

        int c = finn_accept(lfd);
        if (c < 0) continue;

        uid_t uid = (uid_t)-1;
        int allowed = 0;
        if (peer_uid(c, &uid) == 0) {
            allowed = (uid == 0) || (g_finn_uid != (uid_t)-1 && uid == g_finn_uid);
            /* an explicit development escape hatch, off unless asked for */
            if (!allowed && getenv("FINN_CONTROL_ALLOW_ANY")) allowed = 1;
        }

        if (!allowed) {
            finn_write_str(c, "ERR not permitted\n");
            log_call(uid, "(connect)", "ERR not permitted");
            close(c);
            continue;
        }

        serve(c, uid);
        close(c);
    }

    close(lfd);
    unlink(FINN_CONTROL_SOCK);
    fprintf(stderr, "finnd: stopped\n");
    return 0;
}
