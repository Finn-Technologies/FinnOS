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
 *   - Every call is logged with its peer uid, in one line, escaped.
 *   - State verbs are handled here so they keep working when the compositor
 *     is restarting; display and input verbs are proxied to finn-comp.
 *   - One connection at a time. A connection may carry many requests; a
 *     second client waits in the listen backlog rather than being served
 *     concurrently. Multiplexing is a larger change than this file needs.
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
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "finnproto.h"
#include "finnsock.h"
#include "finnconn.h"
#include "finnstate.h"
#include "finnapps.h"
#include "finncompat.h"

#define LOG_DIR  "/var/log/finn"
#define LOG_FILE LOG_DIR "/control.log"

/* The audit log is opened once, at startup, 0600 and close-on-exec: it is
 * the record of root-equivalent actions, so no child and no other user
 * needs a descriptor on it. */
static int g_log_fd = -1;

/* A provider that promises more than this is not answering a capture; the
 * bound keeps a lying length from becoming an allocation or a loop. */
#define FINN_MAX_BINARY 33554432L

/* A compositor that dies mid-payload must not hold the control plane — and
 * everyone queued behind it — forever. */
#define PROVIDER_TIMEOUT_SEC 30

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

static uid_t g_finn_uid = (uid_t)-1;
static gid_t g_finn_gid = (gid_t)-1;
static unsigned long g_flatpak_job_sequence = 0;
static unsigned long g_flatpak_run_sequence = 0;
static unsigned long g_wine_run_sequence = 0;
static unsigned long g_winget_job_sequence = 0;
static unsigned long g_ota_job_sequence = 0;
static char g_finn_home[PATH_MAX] = "";

static void log_open(void) {
    g_log_fd = open(LOG_FILE, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    if (g_log_fd < 0) {
        fprintf(stderr, "finnd: cannot open %s: %s\n", LOG_FILE, strerror(errno));
        return;
    }
    /* A log left behind by an older build is 0644; repairing it here means
     * the mode is right after the first start, not after a reinstall. */
    fchmod(g_log_fd, 0600);
}

/* Everything that is not printable ASCII is escaped, because the request
 * text is the peer's words: a newline in a request must not become a
 * second, forged log line. */
static void log_escape(char *dst, size_t cap, const char *src) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        if (*p >= 0x20 && *p < 0x7f) {
            if (o + 1 < cap) dst[o++] = (char)*p;
        } else {
            if (o + 4 >= cap) break;
            o += (size_t)snprintf(dst + o, cap - o, "\\x%02x", *p);
        }
    }
    dst[o] = 0;
}

static void log_call(uid_t uid, const char *request, const char *result) {
    if (g_log_fd < 0) return;   /* said at startup; the call is refused */

    /* One connection at a time, so one scratch buffer is enough and the
     * stack stays small. */
    static char esc[FINN_MAX_LINE * 4 + 1];
    time_t t = time(NULL);
    char ts[32];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", &tm);
    log_escape(esc, sizeof esc, request);
    dprintf(g_log_fd, "%s uid=%u %s -> %s\n", ts, (unsigned)uid, esc, result);
}

static int peer_uid(int fd, uid_t *out) {
    struct ucred cr;
    socklen_t len = sizeof cr;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &len) < 0) return -1;
    *out = cr.uid;
    return 0;
}

/*
 * What the audit log says about a proxied request. The provider's own
 * status, not a canned "OK": a relayed "ERR ..." is a refusal and the log
 * has to say so. One connection at a time, so one buffer is enough.
 */
static char g_reply_status[160];

static void reply_status(const char *line) {
    size_t n = 0;
    while (line[n] && line[n] != ' ' && line[n] != '\r' && line[n] != '\n' &&
           n + 1 < sizeof g_reply_status) n++;
    memcpy(g_reply_status, line, n);
    g_reply_status[n] = 0;
}

/* relay one reply, including a binary payload, from provider to client */
static int relay(int from, int to) {
    char line[FINN_MAX_LINE];
    int n = finn_read_line(from, line, sizeof line);
    if (n <= 0) {
        finn_write_str(to, "ERR provider closed\n");
        snprintf(g_reply_status, sizeof g_reply_status, "ERR provider closed");
        return -1;
    }

    reply_status(line);

    if (strncmp(line, "OK BINARY ", 10) == 0) {
        char *end = NULL;
        errno = 0;
        long len = strtol(line + 10, &end, 10);
        if (end == line + 10 || *end || errno != 0 ||
            len < 0 || len > FINN_MAX_BINARY) {
            finn_write_str(to, "ERR provider sent a bad length\n");
            snprintf(g_reply_status, sizeof g_reply_status, "ERR bad length");
            return -1;
        }

        char head[64];
        int hn = snprintf(head, sizeof head, "OK BINARY %ld\n", len);
        if (hn <= 0 || finn_write_all(to, head, (size_t)hn) != 0) {
            snprintf(g_reply_status, sizeof g_reply_status, "ERR write");
            return -1;
        }

        snprintf(g_reply_status, sizeof g_reply_status, "OK BINARY");
        char buf[65536];
        long got = 0;
        while (got < len) {
            size_t want = (size_t)((len - got) < (long)sizeof buf ? (len - got) : (long)sizeof buf);
            ssize_t g = read(from, buf, want);
            if (g < 0 && errno == EINTR) continue;
            if (g <= 0) break;
            if (finn_write_all(to, buf, (size_t)g) != 0) break;
            got += (long)g;
        }
        if (got != len) {
            snprintf(g_reply_status, sizeof g_reply_status, "ERR short payload");
            return -1;
        }
        return 0;
    }

    finn_write_str(to, line);
    finn_write_str(to, "\n");
    return 0;
}

static int proxy_to_comp(const char *line, int to) {
    int p = finn_connect(FINN_COMP_SOCK);
    if (p < 0) {
        finn_write_str(to, "ERR compositor unavailable\n");
        snprintf(g_reply_status, sizeof g_reply_status, "ERR compositor unavailable");
        return -1;
    }

    /* The provider socket is blocking, so the wait is bounded here rather
     * than by making the read non-blocking and hoping. */
    struct timeval tv = { PROVIDER_TIMEOUT_SEC, 0 };
    setsockopt(p, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    finn_write_str(p, line);
    finn_write_str(p, "\n");
    int r = relay(p, to);
    close(p);
    return r;
}

/*
 * Start an app.
 *
 * The app is started by systemd, as the user, in its own unit: that is how
 * a program is started on a modern Linux, and it is the only way to start
 * one as the user from a daemon that does not hold CAP_SETUID. systemd
 * takes that capability away from a service with NoNewPrivileges, and the
 * control plane is hardened that way on purpose.
 *
 * The unit is transient and collected: it exists while the app runs and
 * leaves nothing behind. The daemon answers the request and forgets about
 * it — a program that outlives the request is the whole point of launching
 * one.
 */
static int launch_app(const finn_app *a) {
    char unit[96];
    snprintf(unit, sizeof unit, "finn-app-%s-%d", a->id, (int)getpid());

    /* The catalogue's command is one executable path, not a shell fragment.
     * Keeping it as one argv element prevents metacharacters in catalogue
     * data from becoming commands when finnd starts the transient unit. */
    char why[256] = "";
    if (finncompat_start_app("/usr/bin/systemd-run", unit, a->command,
                             (unsigned)g_finn_uid, (unsigned)g_finn_gid,
                             why, sizeof why) != 0) {
        fprintf(stderr, "finnd: %s did not start: %s\n", a->id,
                why[0] ? why : "systemd-run failed");
        return -1;
    }
    return 0;
}

/*
 * The Home panel's Recent activity is a report, so it may only show launches
 * that actually happened. finnd is the only program that starts an app, which
 * makes it the only place that can write this truthfully. The file is a small
 * newest-first list; a relaunch moves its app to the front instead of adding
 * a duplicate row. It is state, not a log: the audit log already has every
 * call, and this one exists to be read by the shell.
 */
static void recent_record(const char *id) {
    if (!id || !id[0]) return;

    char kept[FINN_RECENT_MAX][64];
    long stamp[FINN_RECENT_MAX];
    int n = 0;

    FILE *f = fopen(FINN_RECENT_FILE, "r");
    if (f) {
        char line[128];
        while (n < FINN_RECENT_MAX && fgets(line, sizeof line, f)) {
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = '\0';
            char *end = NULL;
            long when = strtol(line, &end, 10);
            if (!end || *end || when <= 0) continue;
            char *nl = strchr(tab + 1, '\n');
            if (nl) *nl = '\0';
            if (!tab[1] || strchr(tab + 1, '\t')) continue;
            if (!strcmp(tab + 1, id)) continue;
            snprintf(kept[n], sizeof kept[n], "%s", tab + 1);
            stamp[n] = when;
            n++;
        }
        fclose(f);
    }

    char tmp[sizeof FINN_RECENT_FILE + 8];
    snprintf(tmp, sizeof tmp, "%s.XXXXXX", FINN_RECENT_FILE);
    int fd = mkstemp(tmp);
    if (fd < 0) return;
    FILE *out = fdopen(fd, "w");
    if (!out) { close(fd); unlink(tmp); return; }

    fprintf(out, "%ld\t%s\n", (long)time(NULL), id);
    for (int i = 0; i < n; i++) fprintf(out, "%ld\t%s\n", stamp[i], kept[i]);
    if (fflush(out) != 0 || fsync(fileno(out)) != 0) {
        fclose(out);
        unlink(tmp);
        return;
    }
    fclose(out);
    chmod(tmp, 0644);
    if (rename(tmp, FINN_RECENT_FILE) != 0) unlink(tmp);
}


/*
 * Run one systemctl verb and wait for it: the caller's answer is the
 * command's exit status, so it has to be the command that was waited on.
 * Returns the exit status, or -1 when fork or wait failed.
 */
static int run_systemctl(const char *verb) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        execl("/usr/bin/systemctl", "systemctl", verb, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    for (;;) {
        pid_t done = waitpid(pid, &status, 0);
        if (done == pid) break;
        if (done < 0 && errno == EINTR) continue;
        return -1;
    }
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

/*
 * The package manager owns the install list, so the control plane runs it
 * instead of editing the list itself. Its own words are the answer: a
 * catalogue id it refuses is a refusal here too, and the reason it gives is
 * more useful to the person in the Store than a generic failure.
 *
 * The wait is bounded. finnpkg installs from the catalogue by marking an app
 * installed; it does not fetch anything, so this is a file edit and not a
 * download. A child that somehow takes longer than that is killed rather than
 * holding the single control connection — and every client queued behind it —
 * open.
 */
#define FINN_PKG_TIMEOUT_MS 1500

static int valid_pkg_id(const char *id) {
    if (!id || !(id[0] >= 'a' && id[0] <= 'z')) return 0;
    size_t n = 1;
    for (; id[n]; n++) {
        char c = id[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }
    return n <= 32;
}

static void pkg_message_clean(char *s) {
    char *p = s;
    while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
    char *end = strpbrk(p, "\r\n");
    if (end) *end = 0;
    if (!strncmp(p, "finnpkg: ", 9)) p += 9;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

/* 0 on success, -1 when the child could not run or said no, -2 on timeout.
 * `msg` carries finnpkg's first line, with its own prefix removed. */
static int run_pkg_verb(const char *verb, const char *id, char *msg, size_t cap) {
    if (msg && cap) msg[0] = 0;

    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]); close(p[1]);
        return -1;
    }

    if (pid == 0) {
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        dup2(p[1], STDERR_FILENO);
        if (p[1] > STDERR_FILENO) close(p[1]);
        setsid();
        execl("/usr/local/bin/finnpkg", "finnpkg", verb, id, (char *)NULL);
        _exit(127);
    }

    close(p[1]);

    struct pollfd pf = { p[0], POLLIN, 0 };
    int pr;
    do { pr = poll(&pf, 1, FINN_PKG_TIMEOUT_MS); } while (pr < 0 && errno == EINTR);

    int timed_out = 0;
    if (pr > 0 && msg && cap > 0) {
        ssize_t got = read(p[0], msg, cap - 1);
        if (got > 0) {
            msg[got] = 0;
            pkg_message_clean(msg);
        }
    } else if (pr == 0) {
        timed_out = 1;
    }
    close(p[0]);

    if (timed_out) {
        kill(pid, SIGKILL);
        if (msg && cap) snprintf(msg, cap, "the package manager did not answer");
    }

    int status = 0;
    for (int i = 0; i < 100; i++) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) { status = -1; break; }
        usleep(10000);
    }

    if (timed_out) return -2;
    if (!WIFEXITED(status)) return -1;
    if (WEXITSTATUS(status) != 0) {
        if (msg && cap && !msg[0])
            snprintf(msg, cap, "the package manager exited %d", WEXITSTATUS(status));
        return -1;
    }
    return 0;
}

static int send_binary(int fd, const uint8_t *data, size_t len) {
    char header[64];
    int n = snprintf(header, sizeof header, "OK BINARY %zu\n", len);
    if (n <= 0 || (size_t)n >= sizeof header) return -1;
    if (finn_write_all(fd, header, (size_t)n) != 0) return -1;
    return len ? finn_write_all(fd, data, len) : 0;
}

static char *trim_spaces(char *s) {
    if (!s) return (char *)"";
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    return s;
}

/* One protocol line from a small key=value payload: the package manager may
 * print one field per line, but the control protocol answers one line. */
static void payload_to_line(const uint8_t *data, size_t len,
                            char *dst, size_t cap) {
    size_t o = 0;
    int space = 0;
    for (size_t i = 0; i < len && o + 1 < cap; i++) {
        unsigned char c = data[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            space = o > 0;
            continue;
        }
        if (c < 0x20 || c == 0x7f) continue;
        if (space && o > 0) dst[o++] = ' ';
        space = 0;
        dst[o++] = (char)c;
    }
    dst[o] = 0;
}

static int flatpak_app_installed(const char *id, char *error, size_t error_cap) {
    uint8_t *output = NULL;
    size_t len = 0;
    if (finncompat_flatpak_query("/usr/local/bin/finnpkg", "list", NULL,
                                 &output, &len, error, error_cap) != 0)
        return -1;

    size_t pos = 0, id_len = strlen(id);
    int found = 0;
    while (pos < len) {
        size_t end = pos;
        while (end < len && output[end] != '\n' && output[end] != '\r') end++;
        size_t start = pos;
        while (start < end && (output[start] == ' ' || output[start] == '\t')) start++;
        size_t stop = start;
        while (stop < end && output[stop] != ' ' && output[stop] != '\t') stop++;
        if (stop - start == id_len && memcmp(output + start, id, id_len) == 0) {
            found = 1;
            break;
        }
        pos = end;
        while (pos < len && (output[pos] == '\n' || output[pos] == '\r')) pos++;
    }
    free(output);
    return found;
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

    /*
     * ping and version only identify the socket. Everything else reads machine
     * state, starts a program, moves the pointer or changes the machine, and a
     * call with no record of who asked is not a privilege the plane can
     * exercise. Refuse rather than run it unlogged, and say why on stderr so
     * the operator sees it without the log.
     */
    if (g_log_fd < 0 && strcmp(verb, "ping") && strcmp(verb, "version")) {
        fprintf(stderr, "finnd: refusing %.32s: the audit log is not open\n", verb);
        finn_write_str(fd, "ERR audit unavailable\n");
        return "ERR audit unavailable";
    }

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
            struct timeval tv = { PROVIDER_TIMEOUT_SEC, 0 };
            setsockopt(p, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            finn_write_str(p, "state.get\n");
            (void)relay(p, fd);         /* the status is the log's, either way */
            close(p);
            return g_reply_status;
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

    /* ---- power ----
     *
     * The word is a closed set: a typo must not reboot the machine, and a
     * missing argument must not mean "reboot". The reply is sent only after
     * systemctl has run, so it says what happened and not what was
     * intended. No shell: the argument comes from a fixed list, and a shell
     * would add a second place to get quoting wrong.
     */
    if (!strcmp(verb, "power")) {
        static const char *const words[] = { "reboot", "restart", "shutdown", "poweroff" };
        char *save = NULL;
        char *word = strtok_r(rest, " \t", &save);
        char *extra = word ? strtok_r(NULL, " \t", &save) : NULL;

        int known = 0;
        for (size_t i = 0; i < sizeof words / sizeof words[0]; i++)
            if (word && !strcmp(word, words[i])) known = 1;

        if (!word || extra || !known) {
            finn_write_str(fd, "ERR power <reboot|restart|shutdown|poweroff>\n");
            return "ERR power word";
        }

        int off = !strcmp(word, "shutdown") || !strcmp(word, "poweroff");
        if (off) sync();

        int st = run_systemctl(off ? "poweroff" : "reboot");
        static char result[96];
        char reply[96];
        if (st == 0) {
            snprintf(reply, sizeof reply, "OK\n");
            snprintf(result, sizeof result, "OK %s", off ? "poweroff" : "reboot");
        } else if (st > 0) {
            snprintf(reply, sizeof reply, "ERR systemctl exited %d\n", st);
            snprintf(result, sizeof result, "ERR systemctl exited %d", st);
        } else {
            snprintf(reply, sizeof reply, "ERR systemctl failed\n");
            snprintf(result, sizeof result, "ERR systemctl failed");
        }
        finn_write_str(fd, reply);
        return result;
    }

    /* ---- Flatpak package management ----
     *
     * Queries return the package manager's bounded text as a binary-framed
     * payload, so multi-line search results remain one protocol reply. Long
     * mutations are queued as systemd services: finnd stays responsive while
     * Flatpak downloads and applies a transaction. Installed apps are launched
     * as Finn with a Wayland session and the Flatpak sandbox's Wayland socket.
     */
    if (!strcmp(verb, "flatpak.list") || !strcmp(verb, "flatpak.search")) {
        char *query = trim_spaces(rest);
        const char *action = !strcmp(verb, "flatpak.list") ? "list" : "search";
        if (!strcmp(action, "list") && *query) {
            finn_write_str(fd, "ERR flatpak.list takes no arguments\n");
            return "ERR flatpak.list arguments";
        }

        uint8_t *output = NULL;
        size_t output_len = 0;
        char why[256] = "";
        if (finncompat_flatpak_query("/usr/local/bin/finnpkg", action,
                                     *query ? query : NULL,
                                     &output, &output_len,
                                     why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n", why[0] ? why : "Flatpak query failed");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR %s", why[0] ? why : "Flatpak query");
            return result;
        }
        int sent = send_binary(fd, output, output_len);
        free(output);
        if (sent != 0) return "ERR Flatpak query reply write";
        return !strcmp(action, "list") ? "OK flatpak.list" : "OK flatpak.search";
    }

    if (!strcmp(verb, "flatpak.install") || !strcmp(verb, "flatpak.remove")) {
        int install = !strcmp(verb, "flatpak.install");
        char *save = NULL;
        char *id = strtok_r(rest, " \t", &save);
        char *remote_arg = install && id ? strtok_r(NULL, " \t", &save) : NULL;
        char *extra = strtok_r(NULL, " \t", &save);
        const char *remote = install ? (remote_arg ? remote_arg : "flathub") : NULL;
        if (!id || extra || !finncompat_flatpak_id_valid(id) ||
            (install && !finncompat_flatpak_remote_valid(remote))) {
            finn_write_str(fd, "ERR Flatpak install needs a valid application id and optional remote; remove takes one valid application id\n");
            return install ? "ERR flatpak.install id remote" : "ERR flatpak.remove id";
        }

        char job_id[FINN_FLATPAK_JOB_ID_MAX];
        if (++g_flatpak_job_sequence == 0) ++g_flatpak_job_sequence;
        if (finncompat_flatpak_job_id_make(job_id, sizeof job_id,
                                            (unsigned long)getpid(),
                                            g_flatpak_job_sequence) != 0) {
            finn_write_str(fd, "ERR could not allocate a Flatpak job id\n");
            return "ERR Flatpak job id";
        }
        char why[256] = "";
        if (finncompat_flatpak_start("/usr/bin/systemd-run",
                                     "/usr/local/bin/finnpkg", job_id,
                                     install ? "install" : "remove", id,
                                     remote, why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n", why[0] ? why : "could not start Flatpak transaction");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR flatpak.%s %.96s",
                     install ? "install" : "remove", id);
            return result;
        }
        char reply[128];
        snprintf(reply, sizeof reply, "OK job=%s\n", job_id);
        finn_write_str(fd, reply);
        static char result[128];
        snprintf(result, sizeof result, "OK flatpak.%s job=%s",
                 install ? "install" : "remove", job_id);
        return result;
    }

    if (!strcmp(verb, "flatpak.job")) {
        char *save = NULL;
        char *id = strtok_r(rest, " \t", &save);
        char *extra = id ? strtok_r(NULL, " \t", &save) : NULL;
        char state[192] = "";
        char why[256] = "";
        if (!id || extra || !finncompat_flatpak_job_id_valid(id)) {
            finn_write_str(fd, "ERR flatpak.job needs one valid job id\n");
            return "ERR flatpak.job id";
        }
        if (finncompat_flatpak_status("/usr/bin/systemctl", id, state,
                                      sizeof state, why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n", why[0] ? why : "could not read Flatpak job status");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR flatpak.job %.120s", id);
            return result;
        }
        char reply[256];
        snprintf(reply, sizeof reply, "OK job=%s %s\n", id, state);
        finn_write_str(fd, reply);
        static char result[192];
        snprintf(result, sizeof result, "OK flatpak.job %.64s %.96s", id, state);
        return result;
    }

    if (!strcmp(verb, "flatpak.run")) {
        char *id = trim_spaces(rest);
        if (!finncompat_flatpak_id_valid(id)) {
            finn_write_str(fd, "ERR flatpak.run needs one valid application id\n");
            return "ERR flatpak.run id";
        }
        char why[256] = "";
        int installed = flatpak_app_installed(id, why, sizeof why);
        if (installed < 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n", why[0] ? why : "could not verify Flatpak installation");
            finn_write_str(fd, reply);
            return "ERR flatpak.run list";
        }
        if (!installed) {
            finn_write_str(fd, "ERR Flatpak app is not installed\n");
            return "ERR flatpak.run not installed";
        }
        if (++g_flatpak_run_sequence == 0) ++g_flatpak_run_sequence;
        char unit[128];
        snprintf(unit, sizeof unit, "finn-flatpak-run-%lu-%lu",
                 (unsigned long)getpid(), g_flatpak_run_sequence);
        if (finncompat_flatpak_run("/usr/bin/systemd-run", "/usr/bin/flatpak",
                                   unit, id, (unsigned)g_finn_uid,
                                   (unsigned)g_finn_gid, why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n", why[0] ? why : "Flatpak app could not start");
            finn_write_str(fd, reply);
            return "ERR flatpak.run start";
        }
        char reply[192];
        snprintf(reply, sizeof reply, "OK launched %s\n", id);
        finn_write_str(fd, reply);
        return "OK flatpak.run";
    }

    /*
     * What Wine is, before anything tries to use it. A backend is named only
     * when its executable is really there; on ARM an x86_64 Wine is not a
     * backend unless box64 is in front of it.
     */
    if (!strcmp(verb, "wine.status")) {
        if (*trim_spaces(rest)) {
            finn_write_str(fd, "ERR wine.status takes no arguments\n");
            return "ERR wine.status arguments";
        }
        finn_wine_runtime rt;
        char why[256] = "";
        if (finncompat_wine_status("/usr/bin", g_finn_home, &rt,
                                   why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "could not read the Wine runtime");
            finn_write_str(fd, reply);
            return "ERR wine.status";
        }
        /* One line has to fit in FINN_MAX_LINE. A prefix path longer than
         * the protocol line is reported as unknown rather than truncated
         * mid-value, because a truncated path would look like a real one. */
        const char *prefix = strlen(rt.prefix) < 1024 ? rt.prefix : "unknown";
        char reply[FINN_MAX_LINE];
        if (rt.available)
            snprintf(reply, sizeof reply,
                     "OK wine.status arch=%s available=1 backend=%s version=%s prefix=%s\n",
                     rt.arch, rt.backend, rt.version, prefix);
        else
            snprintf(reply, sizeof reply,
                     "OK wine.status arch=%s available=0 backend=%s version=%s prefix=%s reason=%s\n",
                     rt.arch, rt.backend, rt.version, prefix,
                     rt.reason[0] ? rt.reason : "no-runtime");
        finn_write_str(fd, reply);
        return "OK wine.status";
    }

    if (!strcmp(verb, "wine.list")) {
        if (*trim_spaces(rest)) {
            finn_write_str(fd, "ERR wine.list takes no arguments\n");
            return "ERR wine.list arguments";
        }
        if (g_finn_uid == (uid_t)-1 || !g_finn_home[0]) {
            finn_write_str(fd, "ERR Finn user is unavailable\n");
            return "ERR wine.list user";
        }
        char wine_root[PATH_MAX], why[256] = "";
        int root_len = snprintf(wine_root, sizeof wine_root,
                                "%s" FINN_WINE_DATA_SUFFIX, g_finn_home);
        uint8_t *output = NULL;
        size_t output_len = 0;
        if (root_len <= 0 || (size_t)root_len >= sizeof wine_root ||
            finncompat_wine_list(wine_root, (unsigned)g_finn_uid,
                                 &output, &output_len, why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "could not list registered Wine apps");
            finn_write_str(fd, reply);
            return "ERR wine.list registrations";
        }
        int sent = send_binary(fd, output, output_len);
        free(output);
        return sent == 0 ? "OK wine.list" : "ERR wine.list reply write";
    }

    if (!strcmp(verb, "wine.run")) {
        char *save = NULL;
        char *id = strtok_r(rest, " \t", &save);
        char *extra = id ? strtok_r(NULL, " \t", &save) : NULL;
        if (!id || extra || !finncompat_wine_id_valid(id)) {
            finn_write_str(fd, "ERR wine.run takes one valid registered app id\n");
            return "ERR wine.run id";
        }
        if (g_finn_uid == (uid_t)-1 || g_finn_gid == (gid_t)-1 || !g_finn_home[0]) {
            finn_write_str(fd, "ERR Finn user is unavailable\n");
            return "ERR wine.run user";
        }

        char wine_root[PATH_MAX], executable[PATH_MAX], prefix[PATH_MAX];
        int root_len = snprintf(wine_root, sizeof wine_root,
                                "%s" FINN_WINE_DATA_SUFFIX, g_finn_home);
        char why[256] = "";
        if (root_len <= 0 || (size_t)root_len >= sizeof wine_root ||
            finncompat_wine_app_read(wine_root, id, (unsigned)g_finn_uid,
                                     executable, sizeof executable,
                                     prefix, sizeof prefix,
                                     why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "Wine app is not registered");
            finn_write_str(fd, reply);
            return "ERR wine.run registration";
        }

        finn_wine_runtime rt;
        memset(&rt, 0, sizeof rt);
        if (finncompat_wine_status("/usr/bin", g_finn_home, &rt,
                                   why, sizeof why) != 0 || !rt.available) {
            char reply[320];
            snprintf(reply, sizeof reply,
                     "ERR Wine runtime is not available (%s)\n",
                     rt.reason[0] ? rt.reason : "no-runtime");
            finn_write_str(fd, reply);
            return "ERR wine.run no runtime";
        }

        /*
         * Configure Wine driver and theme palette in the prefix registry before
         * starting the application unit so the Wayland driver and dark/light
         * colors are in place before Wine reads its user registry.
         */
        finn_state st;
        finn_state_read(&st);
        int dark = (st.dark == 1) ? 1 : 0;
        if (finncompat_wine_configure_prefix(prefix, (unsigned)g_finn_uid,
                                             (unsigned)g_finn_gid, dark,
                                             why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "Wine prefix could not be configured");
            finn_write_str(fd, reply);
            return "ERR wine.run prefix";
        }

        if (++g_wine_run_sequence == 0) ++g_wine_run_sequence;
        char unit[128];
        snprintf(unit, sizeof unit, "finn-wine-%s-%lu-%lu", id,
                 (unsigned long)getpid(), g_wine_run_sequence);
        if (finncompat_wine_run_wrapped("/usr/bin/systemd-run", rt.binary,
                                        rt.wrapper, unit, g_finn_home,
                                        executable, prefix,
                                        (unsigned)g_finn_uid,
                                        (unsigned)g_finn_gid,
                                        why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "Wine app could not start");
            finn_write_str(fd, reply);
            return "ERR wine.run start";
        }
        char reply[192];
        snprintf(reply, sizeof reply, "OK launched %s\n", id);
        finn_write_str(fd, reply);
        return "OK wine.run";
    }

    /* ---- Windows catalogue (winget) ----
     *
     * The catalogue is remote and the installer is a long download, so the
     * two reads are bounded finnpkg queries and the install is a tracked
     * systemd unit. finnd validates the id and the job id before either
     * reaches a path, a cache directory or a unit name.
     */
    if (!strcmp(verb, "winget.search") || !strcmp(verb, "winget.info")) {
        int search = !strcmp(verb, "winget.search");
        char *argument = trim_spaces(rest);
        uint8_t *output = NULL;
        size_t output_len = 0;
        char why[256] = "";
        if (finncompat_winget_query("/usr/local/bin/finnpkg",
                                    search ? "search" : "info", argument,
                                    &output, &output_len,
                                    why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "winget query failed");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR %s %.120s",
                     search ? "winget.search" : "winget.info", why);
            return result;
        }
        int sent = send_binary(fd, output, output_len);
        free(output);
        if (sent != 0)
            return search ? "ERR winget.search reply write" : "ERR winget.info reply write";
        return search ? "OK winget.search" : "OK winget.info";
    }

    if (!strcmp(verb, "winget.install")) {
        char *save = NULL;
        char *id = strtok_r(rest, " \t", &save);
        char *extra = id ? strtok_r(NULL, " \t", &save) : NULL;
        if (!id || extra || !finncompat_winget_id_valid(id)) {
            finn_write_str(fd, "ERR winget.install takes one valid package id\n");
            return "ERR winget.install id";
        }
        if (g_finn_uid == (uid_t)-1 || g_finn_gid == (gid_t)-1 || !g_finn_home[0]) {
            finn_write_str(fd, "ERR Finn user is unavailable\n");
            return "ERR winget.install user";
        }

        char job_id[FINN_WINGET_JOB_ID_MAX];
        if (++g_winget_job_sequence == 0) ++g_winget_job_sequence;
        if (finncompat_winget_job_id_make(job_id, sizeof job_id,
                                           (unsigned long)getpid(),
                                           g_winget_job_sequence) != 0) {
            finn_write_str(fd, "ERR could not allocate a winget job id\n");
            return "ERR winget job id";
        }
        char why[256] = "";
        if (finncompat_winget_start("/usr/bin/systemd-run",
                                     "/usr/local/bin/finnpkg", job_id, id,
                                     (unsigned)g_finn_uid,
                                     (unsigned)g_finn_gid, g_finn_home,
                                     why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "could not start the winget install");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR winget.install %.96s", id);
            return result;
        }
        char reply[128];
        snprintf(reply, sizeof reply, "OK winget.install job=%s\n", job_id);
        finn_write_str(fd, reply);
        static char result[128];
        snprintf(result, sizeof result, "OK winget.install job=%s", job_id);
        return result;
    }

    if (!strcmp(verb, "winget.job")) {
        char *save = NULL;
        char *id = strtok_r(rest, " \t", &save);
        char *extra = id ? strtok_r(NULL, " \t", &save) : NULL;
        if (!id || extra || !finncompat_winget_job_id_valid(id)) {
            finn_write_str(fd, "ERR winget.job takes one valid job id\n");
            return "ERR winget.job id";
        }
        char word[16] = "";
        char why[256] = "";
        if (finncompat_job_word("/usr/bin/systemctl", id, word,
                                sizeof word, why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "could not read the winget job");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR winget.job %.120s", id);
            return result;
        }
        char reply[128];
        snprintf(reply, sizeof reply, "OK winget.job %s %s\n", id, word);
        finn_write_str(fd, reply);
        static char result[128];
        snprintf(result, sizeof result, "OK winget.job %s %s", id, word);
        return result;
    }

    /* ---- runtime OTA ----
     *
     * status reads the cache and stays fast; check is the one bounded GitHub
     * lookup. Applying an update is a root systemd unit, because the control
     * plane must answer before pacman replaces the package finnd lives in.
     */
    if (!strcmp(verb, "ota.status")) {
        if (*trim_spaces(rest)) {
            finn_write_str(fd, "ERR ota.status takes no arguments\n");
            return "ERR ota.status arguments";
        }
        uint8_t *output = NULL;
        size_t output_len = 0;
        char why[256] = "";
        if (finncompat_ota_query("/usr/local/bin/finnpkg", "status",
                                  &output, &output_len,
                                  why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "could not read the update status");
            finn_write_str(fd, reply);
            return "ERR ota.status";
        }
        char line[FINN_MAX_LINE - 32];
        payload_to_line(output, output_len, line, sizeof line);
        free(output);
        char reply[FINN_MAX_LINE];
        snprintf(reply, sizeof reply, "OK ota.status %s\n",
                 line[0] ? line : "current=unknown latest=unknown available=-1 state=idle last_check=never");
        finn_write_str(fd, reply);
        return "OK ota.status";
    }

    if (!strcmp(verb, "ota.check")) {
        if (*trim_spaces(rest)) {
            finn_write_str(fd, "ERR ota.check takes no arguments\n");
            return "ERR ota.check arguments";
        }
        uint8_t *output = NULL;
        size_t output_len = 0;
        char why[256] = "";
        if (finncompat_ota_query("/usr/local/bin/finnpkg", "check",
                                  &output, &output_len,
                                  why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "the update check failed");
            finn_write_str(fd, reply);
            return "ERR ota.check";
        }
        int sent = send_binary(fd, output, output_len);
        free(output);
        return sent == 0 ? "OK ota.check" : "ERR ota.check reply write";
    }

    if (!strcmp(verb, "ota.download") || !strcmp(verb, "ota.install") ||
        !strcmp(verb, "ota.rollback")) {
        const char *action = verb + 4;
        if (*trim_spaces(rest)) {
            char reply[96];
            snprintf(reply, sizeof reply, "ERR ota.%s takes no arguments\n", action);
            finn_write_str(fd, reply);
            return "ERR ota action arguments";
        }
        char job_id[FINN_OTA_JOB_ID_MAX];
        if (++g_ota_job_sequence == 0) ++g_ota_job_sequence;
        if (finncompat_ota_job_id_make(job_id, sizeof job_id,
                                        (unsigned long)getpid(),
                                        g_ota_job_sequence) != 0) {
            finn_write_str(fd, "ERR could not allocate an OTA job id\n");
            return "ERR ota job id";
        }
        char why[256] = "";
        if (finncompat_ota_start("/usr/bin/systemd-run",
                                  "/usr/local/bin/finnpkg", job_id, action,
                                  why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "could not start the update job");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR ota.%s", action);
            return result;
        }
        char reply[128];
        snprintf(reply, sizeof reply, "OK ota.%s job=%s\n", action, job_id);
        finn_write_str(fd, reply);
        static char result[128];
        snprintf(result, sizeof result, "OK ota.%s job=%s", action, job_id);
        return result;
    }

    if (!strcmp(verb, "ota.job")) {
        char *save = NULL;
        char *id = strtok_r(rest, " \t", &save);
        char *extra = id ? strtok_r(NULL, " \t", &save) : NULL;
        if (!id || extra || !finncompat_ota_job_id_valid(id)) {
            finn_write_str(fd, "ERR ota.job takes one valid job id\n");
            return "ERR ota.job id";
        }
        char word[16] = "";
        char why[256] = "";
        if (finncompat_job_word("/usr/bin/systemctl", id, word,
                                sizeof word, why, sizeof why) != 0) {
            char reply[320];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "could not read the update job");
            finn_write_str(fd, reply);
            static char result[160];
            snprintf(result, sizeof result, "ERR ota.job %.120s", id);
            return result;
        }
        char reply[128];
        snprintf(reply, sizeof reply, "OK ota.job %s %s\n", id, word);
        finn_write_str(fd, reply);
        static char result[128];
        snprintf(result, sizeof result, "OK ota.job %s %s", id, word);
        return result;
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
        recent_record(a->id);
        char b[128];
        snprintf(b, sizeof b, "OK launched %s\n", a->id);
        finn_write_str(fd, b);
        return "OK";
    }

    /*
     * Installing and removing app packages.
     *
     * The catalogue says what exists; finnpkg says whether it is on this
     * machine, and is the only writer of that list. Both verbs take exactly
     * one id: an extra word is a malformed request, not a second app. The id
     * pattern is the one in finnpkg, checked here as well because it reaches
     * a path and a root-owned list on the other side.
     */
    if (!strcmp(verb, "app.install") || !strcmp(verb, "app.remove")) {
        int install = !strcmp(verb, "app.install");
        char id[64] = "";
        char *save = NULL;
        char *tok = strtok_r(rest, " \t", &save);
        char *extra = tok ? strtok_r(NULL, " \t", &save) : NULL;

        if (tok) snprintf(id, sizeof id, "%s", tok);
        if (!tok || extra || !valid_pkg_id(id)) {
            finn_write_str(fd, "ERR app.install and app.remove take one app id\n");
            return install ? "ERR install id" : "ERR remove id";
        }

        finn_apps_load(NULL, NULL);
        if (!finn_apps_find(id)) {
            finn_write_str(fd, "ERR unknown app\n");
            return "ERR unknown app";
        }

        char why[512] = "";
        int rc = run_pkg_verb(install ? "install" : "remove", id, why, sizeof why);
        if (rc != 0) {
            char reply[600];
            snprintf(reply, sizeof reply, "ERR %s\n",
                     why[0] ? why : "the package manager could not run");
            finn_write_str(fd, reply);

            static char result[160];
            snprintf(result, sizeof result, "ERR %s %s",
                     install ? "install" : "remove", id);
            return result;
        }

        char reply[96];
        snprintf(reply, sizeof reply, "OK %s %s\n",
                 install ? "installed" : "removed", id);
        finn_write_str(fd, reply);

        static char result[96];
        snprintf(result, sizeof result, "OK %s %s",
                 install ? "installed" : "removed", id);
        return result;
    }

    /* ---- display + input + machine: proxied to the provider ---- */
    if (!strncmp(verb, "pointer.", 8) || !strncmp(verb, "screen.", 7) ||
        !strncmp(verb, "key.", 4) || !strncmp(verb, "sound.", 6) ||
        !strncmp(verb, "display.", 8) || !strncmp(verb, "radio.", 6) ||
        !strncmp(verb, "appearance.", 11) || !strncmp(verb, "notify.", 7) ||
        !strncmp(verb, "window.", 7) || !strncmp(verb, "setup.", 6) ||
        !strncmp(verb, "settings.", 9) || !strncmp(verb, "files.", 6)) {
        (void)proxy_to_comp(line, fd);  /* the provider's own status is the log entry */
        return g_reply_status;
    }

    finn_write_str(fd, "ERR unknown verb\n");
    return "ERR unknown";
}

/*
 * Serve whatever the peer has already sent, and only that: every complete
 * line is dispatched, a framing error is answered once and skipped, and
 * running out of bytes returns to the caller's poll instead of closing.
 *
 * One connection at a time: a second client waits in the listen backlog
 * until this one is finished. Multiplexing is a larger change; what this
 * fixes is that one connection can carry many requests, and that a request
 * split across packets is framed rather than executed twice.
 *
 * Returns 0 when the connection is still open, -1 when it should be closed
 * (EOF, a truncated line, or a read error).
 */
static int serve(int fd, finn_conn *conn, uid_t uid) {
    char line[FINN_MAX_LINE];

    for (;;) {
        int eof = 0;
        int n = finn_conn_read(conn, fd, line, (int)sizeof line, &eof);

        if (n == 1) {
            /* handle() works on a copy, but the log wants the request as it
             * arrived, so it is kept before the split. */
            char request[FINN_MAX_LINE];
            snprintf(request, sizeof request, "%s", line);
            const char *res = handle(line, fd);
            if (res) log_call(uid, request, res);
            continue;
        }

        if (n < 0) {
            if (errno == EMSGSIZE && !eof) {
                /* the overlong line was consumed whole, so the next request
                 * still frames: answer this one and stay */
                finn_write_str(fd, "ERR line too long\n");
                log_call(uid, "(overlong line)", "ERR line too long");
                continue;
            }
            log_call(uid, "(read)", "ERR read");
            return -1;
        }

        if (eof) return -1;   /* clean end, or truncation already answered */

        return 0;             /* EAGAIN: wait in poll, do not spin */
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    mkdir("/run/finn", 0755);
    mkdir(FINN_STATE_DIR, 0755);
    mkdir(LOG_DIR, 0755);
    log_open();

    struct passwd *pw = getpwnam("finn");
    if (pw) {
        g_finn_uid = pw->pw_uid;
        g_finn_gid = pw->pw_gid;
        if (pw->pw_dir && pw->pw_dir[0] == '/') {
            int n = snprintf(g_finn_home, sizeof g_finn_home, "%s", pw->pw_dir);
            if (n < 0 || (size_t)n >= sizeof g_finn_home) g_finn_home[0] = '\0';
        }
    }

    /* The winget installer runs as Finn and leaves one short state line
     * here. finnd owns the directory, and a job id is validated before it
     * becomes a file name, so a job cannot point the writer somewhere else. */
    if (mkdir(FINN_WINGET_STATE_DIR, 0700) != 0 && errno != EEXIST)
        fprintf(stderr, "finnd: cannot create %s: %s\n",
                FINN_WINGET_STATE_DIR, strerror(errno));
    if (g_finn_uid != (uid_t)-1 &&
        chown(FINN_WINGET_STATE_DIR, g_finn_uid, g_finn_gid) != 0)
        fprintf(stderr, "finnd: cannot give %s to the finn user: %s\n",
                FINN_WINGET_STATE_DIR, strerror(errno));
    if (chmod(FINN_WINGET_STATE_DIR, 0700) != 0)
        fprintf(stderr, "finnd: cannot protect %s: %s\n",
                FINN_WINGET_STATE_DIR, strerror(errno));

    /* An explicit development escape hatch, and only the value "1": a
     * variable that is merely present must not open the plane to every
     * uid. */
    const char *allow_any_env = getenv("FINN_CONTROL_ALLOW_ANY");
    int allow_any = allow_any_env && !strcmp(allow_any_env, "1");
    if (allow_any)
        fprintf(stderr, "finnd: FINN_CONTROL_ALLOW_ANY=1: every uid may use the control plane\n");

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

    /* One connection at a time, held across poll iterations so a peer can
     * ask several questions without being disconnected between them. */
    static finn_conn conn;
    int cfd = -1;
    uid_t cuid = (uid_t)-1;

    while (!g_stop) {
        /* the children of a launch are reaped here: the control plane is
         * long-lived, and a zombie is a debt it would accumulate */
        while (waitpid(-1, NULL, WNOHANG) > 0) { }

        /* While a client is connected the listen socket is not polled: its
         * next client waits in the backlog, which is the cost of not
         * multiplexing. The wait is bounded so an idle client cannot hold
         * the plane forever or starve child reaping. */
        struct pollfd pf = { cfd >= 0 ? cfd : lfd, POLLIN, 0 };
        int pr = poll(&pf, 1, cfd >= 0 ? 5000 : 500);
        if (pr < 0) continue;
        if (pr == 0 && cfd >= 0) {
            close(cfd);
            cfd = -1;
            cuid = (uid_t)-1;
            continue;
        }

        if (cfd < 0) {
            int c = finn_accept(lfd);
            if (c < 0) continue;

            uid_t uid = (uid_t)-1;
            int allowed = 0;
            if (peer_uid(c, &uid) == 0) {
                allowed = (uid == 0) || (g_finn_uid != (uid_t)-1 && uid == g_finn_uid);
                if (!allowed && allow_any) allowed = 1;
            }

            if (!allowed) {
                finn_write_str(c, "ERR not permitted\n");
                log_call(uid, "(connect)", "ERR not permitted");
                close(c);
                continue;
            }

            finn_conn_init(&conn);
            cfd = c;
            cuid = uid;
            continue;
        }

        if (serve(cfd, &conn, cuid) != 0) {
            close(cfd);
            cfd = -1;
            cuid = (uid_t)-1;
        }
    }

    if (cfd >= 0) close(cfd);
    close(lfd);
    unlink(FINN_CONTROL_SOCK);
    fprintf(stderr, "finnd: stopped\n");
    return 0;
}
