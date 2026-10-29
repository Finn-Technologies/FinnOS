#define _GNU_SOURCE
#include "finncompat.h"
#include "finnproto.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define QUERY_TIMEOUT_MS 15000
#define SYSTEMD_TIMEOUT_MS 5000
#define MAX_CAPTURE_BYTES (1024u * 1024u)

static void set_error(char *dst, size_t cap, const char *text) {
    if (!dst || !cap) return;
    snprintf(dst, cap, "%s", text ? text : "operation failed");
}

static int unit_name_valid(const char *unit) {
    if (!unit || !*unit || strlen(unit) > 128) return 0;
    for (const unsigned char *p = (const unsigned char *)unit; *p; p++) {
        int alpha = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z');
        int digit = *p >= '0' && *p <= '9';
        if (!(alpha || digit || *p == '-' || *p == '_' || *p == '.' || *p == '@'))
            return 0;
    }
    return 1;
}

static long long monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int ascii_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int ascii_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

/*
 * The job id is a unit name, so it is one prefix and two decimal numbers:
 * nothing from a request, no shell, no path. The three families share the
 * rule and differ only in the word after `finn-`.
 */
static int job_id_make(char *dst, size_t cap, const char *prefix,
                       unsigned long pid, unsigned long sequence) {
    if (!dst || cap == 0 || !prefix || pid == 0 || sequence == 0) return -1;
    int n = snprintf(dst, cap, "finn-%s-%lu-%lu", prefix, pid, sequence);
    if (n <= 0 || (size_t)n >= cap) return -1;
    return 0;
}

static int job_id_valid(const char *id, const char *prefix) {
    if (!id || !prefix) return 0;
    size_t plen = strlen(prefix);
    if (strncmp(id, "finn-", 5) != 0) return 0;
    if (strncmp(id + 5, prefix, plen) != 0 || id[5 + plen] != '-') return 0;
    const unsigned char *p = (const unsigned char *)id + 5 + plen + 1;
    if (!ascii_digit(*p)) return 0;
    while (ascii_digit(*p)) p++;
    if (*p++ != '-' || !ascii_digit(*p)) return 0;
    while (ascii_digit(*p)) p++;
    return *p == '\0';
}

int finncompat_flatpak_id_valid(const char *id) {
    if (!id) return 0;
    size_t len = strlen(id);
    if (len < 3 || len > FINN_FLATPAK_ID_MAX) return 0;

    int segments = 0;
    const unsigned char *p = (const unsigned char *)id;
    while (*p) {
        if (!(ascii_alpha(*p) || *p == '_')) return 0;
        segments++;
        p++;
        while (*p && *p != '.') {
            if (!(ascii_alpha(*p) || ascii_digit(*p) || *p == '_' || *p == '-'))
                return 0;
            p++;
        }
        if (*p == '.') {
            p++;
            if (!*p) return 0;
        }
    }
    return segments >= 2;
}

int finncompat_flatpak_remote_valid(const char *remote) {
    if (!remote) return 0;
    size_t len = strlen(remote);
    if (len == 0 || len > 64) return 0;
    const unsigned char *p = (const unsigned char *)remote;
    if (!ascii_alpha(*p) && !ascii_digit(*p)) return 0;
    for (; *p; p++)
        if (!ascii_alpha(*p) && !ascii_digit(*p) && *p != '_' && *p != '-' && *p != '.')
            return 0;
    return 1;
}

int finncompat_flatpak_job_id_make(char *dst, size_t cap,
                                   unsigned long pid, unsigned long sequence) {
    return job_id_make(dst, cap, "flatpak", pid, sequence);
}

int finncompat_flatpak_job_id_valid(const char *id) {
    return job_id_valid(id, "flatpak");
}

int finncompat_winget_id_valid(const char *id) {
    if (!id) return 0;
    size_t len = strlen(id);
    if (len < 1 || len > FINN_WINGET_ID_MAX) return 0;
    const unsigned char *p = (const unsigned char *)id;
    if (!ascii_alpha(*p) && !ascii_digit(*p)) return 0;
    for (; *p; p++)
        if (!ascii_alpha(*p) && !ascii_digit(*p) &&
            *p != '.' && *p != '_' && *p != '-')
            return 0;
    return 1;
}

int finncompat_winget_job_id_make(char *dst, size_t cap,
                                  unsigned long pid, unsigned long sequence) {
    return job_id_make(dst, cap, "winget", pid, sequence);
}

int finncompat_winget_job_id_valid(const char *id) {
    return job_id_valid(id, "winget");
}

int finncompat_ota_job_id_make(char *dst, size_t cap,
                               unsigned long pid, unsigned long sequence) {
    return job_id_make(dst, cap, "ota", pid, sequence);
}

int finncompat_ota_job_id_valid(const char *id) {
    return job_id_valid(id, "ota");
}

static void child_exec(const char *path, char *const argv[], int out_fd) {
    if (out_fd >= 0) {
        dup2(out_fd, STDOUT_FILENO);
        dup2(out_fd, STDERR_FILENO);
        if (out_fd > STDERR_FILENO) close(out_fd);
    }
    setsid();
    execv(path, argv);
    dprintf(STDERR_FILENO, "could not execute %.160s: %s\n", path, strerror(errno));
    _exit(127);
}

/* Run argv without a shell, collecting stdout and stderr into one bounded buffer. */
static int run_capture(const char *path, char *const argv[], int timeout_ms,
                       size_t max_bytes, uint8_t **out, size_t *out_len,
                       char *error, size_t error_cap) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        set_error(error, error_cap, "could not create command pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        set_error(error, error_cap, "could not start command");
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        child_exec(path, argv, pipefd[1]);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    uint8_t *buffer = malloc(max_bytes ? max_bytes : 1);
    if (!buffer) {
        close(pipefd[0]);
        kill(-pid, SIGKILL);
        waitpid(pid, NULL, 0);
        set_error(error, error_cap, "could not allocate command output buffer");
        return -1;
    }

    size_t used = 0;
    int eof = 0, done = 0, timed_out = 0, overflow = 0, status = 0;
    long long deadline = monotonic_ms() + timeout_ms;
    while (!eof || !done) {
        if (!done) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) done = 1;
            else if (w < 0 && errno != EINTR) { done = 1; status = -1; }
        }

        if (!eof) {
            long long left = deadline - monotonic_ms();
            if (left <= 0) {
                timed_out = 1;
                break;
            }
            struct pollfd pf = { pipefd[0], POLLIN | POLLHUP, 0 };
            int wait_ms = left > 100 ? 100 : (int)left;
            int pr = poll(&pf, 1, wait_ms);
            if (pr < 0 && errno != EINTR) break;
            if (pr > 0 && (pf.revents & (POLLIN | POLLHUP))) {
                for (;;) {
                    uint8_t chunk[4096];
                    ssize_t got = read(pipefd[0], chunk, sizeof chunk);
                    if (got > 0) {
                        if ((size_t)got > max_bytes - used) {
                            overflow = 1;
                            break;
                        }
                        memcpy(buffer + used, chunk, (size_t)got);
                        used += (size_t)got;
                        continue;
                    }
                    if (got == 0) eof = 1;
                    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                        eof = 1;
                    break;
                }
                if (overflow) break;
            }
        } else if (!done) {
            usleep(10000);
        }

        if (!done && monotonic_ms() >= deadline) {
            timed_out = 1;
            break;
        }
    }

    if (timed_out || overflow) {
        kill(-pid, SIGTERM);
        usleep(20000);
        kill(-pid, SIGKILL);
    }
    close(pipefd[0]);
    if (!done) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    }

    if (timed_out || overflow || status < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (timed_out) set_error(error, error_cap, "command timed out");
        else if (overflow) set_error(error, error_cap, "command output exceeded its limit");
        else if (used && error && error_cap) {
            size_t n = 0;
            while (n < used && buffer[n] != '\n' && buffer[n] != '\r' && n + 1 < error_cap) {
                unsigned char c = buffer[n];
                error[n] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
                n++;
            }
            error[n] = '\0';
            if (!n) set_error(error, error_cap, "command failed");
        } else set_error(error, error_cap, "command failed");
        free(buffer);
        return -1;
    }

    if (out) *out = buffer;
    else free(buffer);
    if (out_len) *out_len = used;
    return 0;
}

int finncompat_start_app(const char *systemd_run, const char *unit,
                         const char *command, unsigned uid, unsigned gid,
                         char *error, size_t error_cap) {
    if (!systemd_run || !unit_name_valid(unit) || !command || command[0] != '/') {
        set_error(error, error_cap, "invalid application launch request");
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)command; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) {
            set_error(error, error_cap, "application path contains a control character");
            return -1;
        }
    }

    char unit_arg[144], uid_arg[32], gid_arg[32], runtime_arg[96];
    int un = snprintf(unit_arg, sizeof unit_arg, "--unit=%s", unit);
    int uin = snprintf(uid_arg, sizeof uid_arg, "--uid=%u", uid);
    int gin = snprintf(gid_arg, sizeof gid_arg, "--gid=%u", gid);
    int rn = snprintf(runtime_arg, sizeof runtime_arg,
                      "--setenv=XDG_RUNTIME_DIR=/run/user/%u", uid);
    if (un <= 0 || (size_t)un >= sizeof unit_arg ||
        uin <= 0 || (size_t)uin >= sizeof uid_arg ||
        gin <= 0 || (size_t)gin >= sizeof gid_arg ||
        rn <= 0 || (size_t)rn >= sizeof runtime_arg) {
        set_error(error, error_cap, "application launch arguments are too long");
        return -1;
    }

    char *argv[] = {
        (char *)systemd_run, "--quiet", "--collect", unit_arg,
        "--service-type=exec", uid_arg, gid_arg,
        "--setenv=HOME=/home/finn", "--setenv=USER=finn",
        "--setenv=LOGNAME=finn", "--setenv=SHELL=/bin/bash",
        "--setenv=PATH=/usr/local/bin:/usr/bin:/bin",
        runtime_arg, "--setenv=WAYLAND_DISPLAY=wayland-0",
        "--setenv=XDG_SESSION_TYPE=wayland", "--setenv=GTK_CSD=0",
        "--property=Restart=no", "--", (char *)command, NULL
    };
    uint8_t *output = NULL;
    size_t output_len = 0;
    int rc = run_capture(systemd_run, argv, SYSTEMD_TIMEOUT_MS,
                         512, &output, &output_len, error, error_cap);
    free(output);
    return rc;
}

int finncompat_wine_id_valid(const char *id) {
    if (!id || !(id[0] >= 'a' && id[0] <= 'z')) return 0;
    size_t n = 1;
    for (; id[n]; n++) {
        unsigned char c = (unsigned char)id[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }
    return n <= 32;
}

static int executable_file(const char *path) {
    struct stat st;
    return path && path[0] == '/' && stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

static int join_bin(const char *dir, const char *name, char *out, size_t cap) {
    int n = snprintf(out, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

static int wine_glob_find(const char *pattern, char *out, size_t cap) {
    glob_t matches;
    memset(&matches, 0, sizeof matches);
    int found = 0;
    if (glob(pattern, GLOB_NOSORT, NULL, &matches) == 0) {
        for (size_t i = 0; i < matches.gl_pathc; i++) {
            if (!executable_file(matches.gl_pathv[i])) continue;
            int n = snprintf(out, cap, "%s", matches.gl_pathv[i]);
            if (n > 0 && (size_t)n < cap) found = 1;
            break;
        }
    }
    globfree(&matches);
    return found;
}

/*
 * Proton is a compatibility tool, not a command: it only counts when a
 * real tool's wine binary is there. A directory that merely looks like
 * Proton is not a runtime the Store can start.
 */
static int wine_proton_find(const char *home, char *out, size_t cap) {
    static const char *const system_patterns[] = {
        "/usr/share/steam/compatibilitytools.d/*/files/bin/wine",
        "/usr/local/share/steam/compatibilitytools.d/*/files/bin/wine",
        "/usr/share/steam/steamapps/common/Proton*/files/bin/wine",
        "/usr/local/share/steam/steamapps/common/Proton*/files/bin/wine",
    };
    for (size_t i = 0; i < sizeof system_patterns / sizeof system_patterns[0]; i++)
        if (wine_glob_find(system_patterns[i], out, cap)) return 1;

    if (home && home[0] == '/') {
        static const char *const home_suffixes[] = {
            "/.steam/steam/compatibilitytools.d/*/files/bin/wine",
            "/.local/share/Steam/compatibilitytools.d/*/files/bin/wine",
            "/.steam/steam/steamapps/common/Proton*/files/bin/wine",
            "/.local/share/Steam/steamapps/common/Proton*/files/bin/wine",
        };
        for (size_t i = 0; i < sizeof home_suffixes / sizeof home_suffixes[0]; i++) {
            char pattern[PATH_MAX + 96];
            int n = snprintf(pattern, sizeof pattern, "%s%s", home, home_suffixes[i]);
            if (n > 0 && (size_t)n < sizeof pattern &&
                wine_glob_find(pattern, out, cap))
                return 1;
        }
    }
    return 0;
}

static void wine_backend_set(finn_wine_runtime *rt, const char *backend,
                             const char *binary, const char *wrapper) {
    snprintf(rt->backend, sizeof rt->backend, "%s", backend);
    snprintf(rt->binary, sizeof rt->binary, "%s", binary ? binary : "");
    snprintf(rt->wrapper, sizeof rt->wrapper, "%s", wrapper ? wrapper : "");
    rt->available = binary && binary[0] ? 1 : 0;
    snprintf(rt->reason, sizeof rt->reason, "%s",
             rt->available ? "" : "no-runtime");
}

static void wine_version_read(const finn_wine_runtime *rt, char *dst, size_t cap) {
    snprintf(dst, cap, "unknown");
    char *argv[4];
    int argc = 0;
    if (rt->wrapper[0]) argv[argc++] = rt->wrapper;
    argv[argc++] = rt->binary;
    argv[argc++] = "--version";
    argv[argc] = NULL;

    uint8_t *output = NULL;
    size_t len = 0;
    char why[128] = "";
    if (run_capture(argv[0], argv, 2500, 512, &output, &len, why, sizeof why) != 0) {
        free(output);
        return;
    }

    size_t n = 0;
    while (n < len && output[n] != '\n' && output[n] != '\r' &&
           output[n] != ' ' && output[n] != '\t')
        n++;
    char raw[FINN_WINE_VERSION_MAX];
    if (n >= sizeof raw) n = sizeof raw - 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = output[i];
        raw[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    raw[n] = '\0';
    free(output);
    if (!n) return;

    const char *v = raw;
    if (!strncmp(v, "wine-", 5)) v += 5;
    if (*v) snprintf(dst, cap, "%s", v);
}

int finncompat_wine_status(const char *bin_dir, const char *home,
                           finn_wine_runtime *out,
                           char *error, size_t error_cap) {
    if (!out) {
        set_error(error, error_cap, "invalid Wine status request");
        return -1;
    }
    memset(out, 0, sizeof *out);
    if (error && error_cap) error[0] = '\0';
    snprintf(out->backend, sizeof out->backend, "none");
    snprintf(out->reason, sizeof out->reason, "no-runtime");
    snprintf(out->version, sizeof out->version, "unknown");

    struct utsname un;
    if (uname(&un) == 0 && un.machine[0])
        snprintf(out->arch, sizeof out->arch, "%s", un.machine);
    else
        snprintf(out->arch, sizeof out->arch, "unknown");

    const char *home_ok = home && home[0] == '/' ? home : NULL;
    if (home_ok)
        snprintf(out->prefix, sizeof out->prefix, "%s%s",
                 home_ok, FINN_WINE_DATA_SUFFIX);
    else
        snprintf(out->prefix, sizeof out->prefix, "unknown");

    int arm = !strcmp(out->arch, "aarch64") || !strcmp(out->arch, "arm64");
    if (!arm && strcmp(out->arch, "x86_64")) {
        snprintf(out->reason, sizeof out->reason, "unsupported-arch");
        return 0;
    }

    const char *dir = bin_dir && bin_dir[0] == '/' ? bin_dir : "/usr/bin";
    char hangover[PATH_MAX], staging[PATH_MAX], wine[PATH_MAX], box64[PATH_MAX];
    int have_hangover = join_bin(dir, "hangover", hangover, sizeof hangover) &&
                        executable_file(hangover);
    int have_staging = join_bin(dir, "wine-staging", staging, sizeof staging) &&
                       executable_file(staging);
    int have_wine = join_bin(dir, "wine", wine, sizeof wine) &&
                    executable_file(wine);
    int have_box64 = join_bin(dir, "box64", box64, sizeof box64) &&
                     executable_file(box64);

    char proton[PATH_MAX];
    if (have_hangover) {
        wine_backend_set(out, "hangover", hangover, NULL);
    } else if (have_staging) {
        wine_backend_set(out, "wine-staging", staging, NULL);
    } else if (have_wine) {
        /* On ARM an x86_64 Wine is a Windows runtime only with box64 in
         * front of it. Without the wrapper it is a file, not a backend. */
        if (arm && have_box64) {
            wine_backend_set(out, "box64", wine, box64);
        } else if (arm) {
            snprintf(out->backend, sizeof out->backend, "wine");
            snprintf(out->binary, sizeof out->binary, "%s", wine);
            out->available = 0;
            snprintf(out->reason, sizeof out->reason, "needs-box64");
        } else {
            wine_backend_set(out, "wine", wine, NULL);
        }
    } else if (wine_proton_find(home_ok, proton, sizeof proton)) {
        wine_backend_set(out, "proton", proton, NULL);
    } else if (have_box64) {
        wine_backend_set(out, "box64", NULL, NULL);
        snprintf(out->reason, sizeof out->reason, "no-wine");
    } else {
        wine_backend_set(out, "none", NULL, NULL);
    }

    if (out->available)
        wine_version_read(out, out->version, sizeof out->version);
    return 0;
}

static int wine_executable_valid(const char *path) {
    if (!path || path[0] != '/') return 0;
    size_t n = strlen(path);
    if (n < 5 || n >= PATH_MAX) return 0;
    const char *suffix = path + n - 4;
    if (tolower((unsigned char)suffix[0]) != '.' ||
        tolower((unsigned char)suffix[1]) != 'e' ||
        tolower((unsigned char)suffix[2]) != 'x' ||
        tolower((unsigned char)suffix[3]) != 'e') return 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p < 0x20 || *p == 0x7f) return 0;
    return 1;
}

static int private_owned_dir(int fd, unsigned uid) {
    struct stat st;
    return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) &&
           st.st_uid == (uid_t)uid && (st.st_mode & 077) == 0;
}

static int wine_app_read_at(int root_fd, const char *wine_root, const char *id,
                            unsigned uid, char *executable, size_t executable_cap,
                            char *prefix, size_t prefix_cap,
                            char *error, size_t error_cap) {
    int app_fd = openat(root_fd, id, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (app_fd < 0 || !private_owned_dir(app_fd, uid)) {
        if (app_fd >= 0) close(app_fd);
        set_error(error, error_cap, "Wine app directory is not private to Finn");
        return -1;
    }

    int executable_fd = openat(app_fd, "executable", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat executable_stat;
    char stored[PATH_MAX];
    ssize_t used = -1;
    if (executable_fd >= 0 && fstat(executable_fd, &executable_stat) == 0 &&
        S_ISREG(executable_stat.st_mode) && executable_stat.st_uid == (uid_t)uid &&
        (executable_stat.st_mode & 077) == 0 && executable_stat.st_size > 1 &&
        executable_stat.st_size < (off_t)sizeof stored) {
        size_t total = 0, expected = (size_t)executable_stat.st_size;
        while (total < expected) {
            ssize_t got = read(executable_fd, stored + total, expected - total);
            if (got > 0) total += (size_t)got;
            else if (got < 0 && errno == EINTR) continue;
            else break;
        }
        if (total == expected) used = (ssize_t)total;
    }
    if (executable_fd >= 0) close(executable_fd);
    if (used <= 1 || stored[used - 1] != '\n' || memchr(stored, '\0', (size_t)used)) {
        close(app_fd);
        set_error(error, error_cap, "registered Wine executable metadata is invalid");
        return -1;
    }
    stored[used - 1] = '\0';
    struct stat app_executable_stat;
    if (!wine_executable_valid(stored) || strlen(stored) + 1 > executable_cap ||
        stat(stored, &app_executable_stat) != 0 ||
        !S_ISREG(app_executable_stat.st_mode) || !(app_executable_stat.st_mode & 0444)) {
        close(app_fd);
        set_error(error, error_cap, "registered Wine executable is missing or invalid");
        return -1;
    }

    int prefix_fd = openat(app_fd, "prefix", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (prefix_fd < 0 || !private_owned_dir(prefix_fd, uid)) {
        if (prefix_fd >= 0) close(prefix_fd);
        close(app_fd);
        set_error(error, error_cap, "Wine prefix is not private to Finn");
        return -1;
    }
    close(prefix_fd);

    size_t root_len = strlen(wine_root);
    int pn = snprintf(prefix, prefix_cap, "%s%s%s/prefix", wine_root,
                      root_len && wine_root[root_len - 1] == '/' ? "" : "/", id);
    if (pn <= 0 || (size_t)pn >= prefix_cap) {
        close(app_fd);
        set_error(error, error_cap, "Wine prefix path is too long");
        return -1;
    }
    snprintf(executable, executable_cap, "%s", stored);
    close(app_fd);
    return 0;
}

static int wine_root_valid(const char *wine_root, unsigned uid,
                           char *error, size_t error_cap) {
    if (!wine_root || wine_root[0] != '/' || uid == 0) {
        set_error(error, error_cap, "invalid Wine data root");
        return 0;
    }
    for (const unsigned char *p = (const unsigned char *)wine_root; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) {
            set_error(error, error_cap, "Wine data path contains a control character");
            return 0;
        }
    }
    return 1;
}

static int wine_root_open(const char *wine_root, unsigned uid,
                          char *error, size_t error_cap) {
    int root_fd = open(wine_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root_fd < 0 || !private_owned_dir(root_fd, uid)) {
        if (root_fd >= 0) close(root_fd);
        set_error(error, error_cap, "Wine data root is not private to Finn");
        return -1;
    }
    return root_fd;
}

int finncompat_wine_app_read(const char *wine_root, const char *id, unsigned uid,
                             char *executable, size_t executable_cap,
                             char *prefix, size_t prefix_cap,
                             char *error, size_t error_cap) {
    if (error && error_cap) error[0] = '\0';
    if (!wine_root_valid(wine_root, uid, error, error_cap) ||
        !finncompat_wine_id_valid(id) || !executable || !executable_cap ||
        !prefix || !prefix_cap) {
        if (!error || !error_cap || !error[0])
            set_error(error, error_cap, "invalid registered Wine app request");
        return -1;
    }
    int root_fd = wine_root_open(wine_root, uid, error, error_cap);
    if (root_fd < 0) return -1;
    int rc = wine_app_read_at(root_fd, wine_root, id, uid, executable,
                              executable_cap, prefix, prefix_cap, error, error_cap);
    close(root_fd);
    return rc;
}

#define MAX_WINE_LIST_BYTES (64u * 1024u)
#define MAX_WINE_LIST_ENTRIES 4096u

int finncompat_wine_list(const char *wine_root, unsigned uid,
                         uint8_t **out, size_t *out_len,
                         char *error, size_t error_cap) {
    if (error && error_cap) error[0] = '\0';
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!out || !out_len || !wine_root_valid(wine_root, uid, error, error_cap)) {
        if ((!error || !error_cap || !error[0]) && (out == NULL || out_len == NULL))
            set_error(error, error_cap, "invalid Wine app list request");
        return -1;
    }

    int root_fd = open(wine_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root_fd < 0 && errno == ENOENT) {
        *out = calloc(1, 1);
        if (!*out) {
            set_error(error, error_cap, "could not allocate Wine app list");
            return -1;
        }
        return 0;
    }
    if (root_fd < 0 || !private_owned_dir(root_fd, uid)) {
        if (root_fd >= 0) close(root_fd);
        set_error(error, error_cap, "Wine data root is not private to Finn");
        return -1;
    }

    int scan_fd = dup(root_fd);
    DIR *dir = scan_fd >= 0 ? fdopendir(scan_fd) : NULL;
    if (!dir) {
        if (scan_fd >= 0) close(scan_fd);
        close(root_fd);
        set_error(error, error_cap, "could not enumerate Wine app registrations");
        return -1;
    }
    uint8_t *ids = malloc(MAX_WINE_LIST_BYTES);
    if (!ids) {
        closedir(dir);
        close(root_fd);
        set_error(error, error_cap, "could not allocate Wine app list");
        return -1;
    }

    size_t used = 0, scanned = 0;
    int failed = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno != 0) failed = 1;
            break;
        }
        if (++scanned > MAX_WINE_LIST_ENTRIES) {
            failed = 1;
            break;
        }
        if (!finncompat_wine_id_valid(entry->d_name)) continue;
        char executable[PATH_MAX], prefix[PATH_MAX], why[160] = "";
        if (wine_app_read_at(root_fd, wine_root, entry->d_name, uid,
                             executable, sizeof executable, prefix, sizeof prefix,
                             why, sizeof why) != 0)
            continue;

        size_t id_len = strlen(entry->d_name);
        if (id_len + 1 > MAX_WINE_LIST_BYTES - used) {
            failed = 1;
            break;
        }
        memcpy(ids + used, entry->d_name, id_len);
        used += id_len;
        ids[used++] = '\n';
    }
    closedir(dir);
    close(root_fd);
    if (failed) {
        free(ids);
        set_error(error, error_cap, "Wine app list exceeded its bound or could not be read");
        return -1;
    }
    *out = ids;
    *out_len = used;
    return 0;
}

int finncompat_wine_run_wrapped(const char *systemd_run, const char *wine,
                                const char *wrapper, const char *unit,
                                const char *home, const char *executable,
                                const char *prefix, unsigned uid, unsigned gid,
                                char *error, size_t error_cap) {
    if (!systemd_run || systemd_run[0] != '/' || !wine || wine[0] != '/' ||
        !unit_name_valid(unit) || !home || home[0] != '/' ||
        !wine_executable_valid(executable) || !prefix || prefix[0] != '/' ||
        uid == 0 || gid == 0 ||
        (wrapper && wrapper[0] &&
         (wrapper[0] != '/' || strlen(wrapper) >= PATH_MAX))) {
        set_error(error, error_cap, "invalid Wine launch request");
        return -1;
    }
    const char *paths[] = { home, prefix, wrapper && wrapper[0] ? wrapper : NULL };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        if (!paths[i]) continue;
        for (const unsigned char *p = (const unsigned char *)paths[i]; *p; p++) {
            if (*p < 0x20 || *p == 0x7f) {
                set_error(error, error_cap, "Wine launch path contains a control character");
                return -1;
            }
        }
    }

    char unit_arg[144], uid_arg[32], gid_arg[32], home_arg[PATH_MAX + 32];
    char runtime_arg[96], prefix_arg[PATH_MAX + 32];
    int un = snprintf(unit_arg, sizeof unit_arg, "--unit=%s", unit);
    int uin = snprintf(uid_arg, sizeof uid_arg, "--uid=%u", uid);
    int gin = snprintf(gid_arg, sizeof gid_arg, "--gid=%u", gid);
    int hn = snprintf(home_arg, sizeof home_arg, "--setenv=HOME=%s", home);
    int rn = snprintf(runtime_arg, sizeof runtime_arg,
                      "--setenv=XDG_RUNTIME_DIR=/run/user/%u", uid);
    int pn = snprintf(prefix_arg, sizeof prefix_arg, "--setenv=WINEPREFIX=%s", prefix);
    if (un <= 0 || (size_t)un >= sizeof unit_arg ||
        uin <= 0 || (size_t)uin >= sizeof uid_arg ||
        gin <= 0 || (size_t)gin >= sizeof gid_arg ||
        hn <= 0 || (size_t)hn >= sizeof home_arg ||
        rn <= 0 || (size_t)rn >= sizeof runtime_arg ||
        pn <= 0 || (size_t)pn >= sizeof prefix_arg) {
        set_error(error, error_cap, "Wine launch arguments are too long");
        return -1;
    }

    char *argv[32];
    int a = 0;
    argv[a++] = (char *)systemd_run;
    argv[a++] = "--quiet";
    argv[a++] = "--collect";
    argv[a++] = unit_arg;
    argv[a++] = "--service-type=exec";
    argv[a++] = uid_arg;
    argv[a++] = gid_arg;
    argv[a++] = home_arg;
    argv[a++] = "--setenv=USER=finn";
    argv[a++] = "--setenv=LOGNAME=finn";
    argv[a++] = "--setenv=SHELL=/bin/bash";
    argv[a++] = "--setenv=PATH=/usr/local/bin:/usr/bin:/bin";
    argv[a++] = runtime_arg;
    argv[a++] = "--setenv=WAYLAND_DISPLAY=wayland-0";
    argv[a++] = "--setenv=XDG_SESSION_TYPE=wayland";
    argv[a++] = prefix_arg;
    argv[a++] = "--setenv=GTK_CSD=0";
    argv[a++] = "--property=Restart=no";
    argv[a++] = "--";
    if (wrapper && wrapper[0]) argv[a++] = (char *)wrapper;
    argv[a++] = (char *)wine;
    argv[a++] = (char *)executable;
    argv[a] = NULL;

    uint8_t *output = NULL;
    size_t output_len = 0;
    int rc = run_capture(systemd_run, argv, SYSTEMD_TIMEOUT_MS, 2048,
                         &output, &output_len, error, error_cap);
    free(output);
    return rc;
}

int finncompat_wine_run(const char *systemd_run, const char *wine,
                        const char *unit, const char *home,
                        const char *executable, const char *prefix,
                        unsigned uid, unsigned gid,
                        char *error, size_t error_cap) {
    return finncompat_wine_run_wrapped(systemd_run, wine, NULL, unit, home,
                                       executable, prefix, uid, gid,
                                       error, error_cap);
}

static const char WINE_REG_HEADER[] =
    "WINE REGISTRY Version 2\n"
    ";; All keys relative to \\User\\S-1-5-21-0-0-0-1000\n\n";

static const char FINN_MANAGED_BEGIN[] = ";; FINNOS-MANAGED-BEGIN\n";
static const char FINN_MANAGED_END[] = ";; FINNOS-MANAGED-END\n";

/*
 * Wine needs the Wayland driver and zero window metrics so Finn's compositor
 * can manage titlebars and shadows without simulated Windows frame borders.
 */
static const char FINN_WINE_SETTINGS_COMMON[] =
    "[Software\\Wine\\Drivers]\n"
    "\"Graphics\"=\"wayland\"\n\n"
    "[Software\\Wine\\Wayland Driver]\n"
    "\"Decorated\"=\"N\"\n"
    "\"Managed\"=\"Y\"\n\n"
    "[Software\\Wine\\X11 Driver]\n"
    "\"Decorated\"=\"N\"\n"
    "\"Managed\"=\"Y\"\n\n"
    "[Control Panel\\Desktop\\WindowMetrics]\n"
    "\"BorderWidth\"=\"0\"\n"
    "\"PaddedBorderWidth\"=\"0\"\n\n";

/*
 * Standard Win32 controls read system metrics from Control Panel\\Colors;
 * populate both variants to match the active FinnOS dark/light theme palette.
 */
static const char FINN_WINE_COLORS_LIGHT[] =
    "[Control Panel\\Colors]\n"
    "\"ActiveBorder\"=\"249 249 249\"\n"
    "\"ActiveTitle\"=\"249 249 249\"\n"
    "\"AppWorkSpace\"=\"242 242 242\"\n"
    "\"Background\"=\"249 249 249\"\n"
    "\"ButtonDkShadow\"=\"160 160 160\"\n"
    "\"ButtonFace\"=\"249 249 249\"\n"
    "\"ButtonHilight\"=\"255 255 255\"\n"
    "\"ButtonLight\"=\"255 255 255\"\n"
    "\"ButtonShadow\"=\"215 215 215\"\n"
    "\"ButtonText\"=\"0 0 0\"\n"
    "\"Control\"=\"249 249 249\"\n"
    "\"GrayText\"=\"150 150 150\"\n"
    "\"Hilight\"=\"0 111 255\"\n"
    "\"HilightText\"=\"255 255 255\"\n"
    "\"InactiveBorder\"=\"249 249 249\"\n"
    "\"InactiveTitle\"=\"242 242 242\"\n"
    "\"InactiveTitleText\"=\"110 110 110\"\n"
    "\"InfoText\"=\"0 0 0\"\n"
    "\"InfoWindow\"=\"255 255 255\"\n"
    "\"Menu\"=\"249 249 249\"\n"
    "\"MenuBar\"=\"249 249 249\"\n"
    "\"MenuHilight\"=\"0 111 255\"\n"
    "\"MenuText\"=\"0 0 0\"\n"
    "\"Scrollbar\"=\"242 242 242\"\n"
    "\"TitleText\"=\"0 0 0\"\n"
    "\"Window\"=\"255 255 255\"\n"
    "\"WindowFrame\"=\"215 215 215\"\n"
    "\"WindowText\"=\"0 0 0\"\n";

static const char FINN_WINE_COLORS_DARK[] =
    "[Control Panel\\Colors]\n"
    "\"ActiveBorder\"=\"47 51 60\"\n"
    "\"ActiveTitle\"=\"47 51 60\"\n"
    "\"AppWorkSpace\"=\"26 28 35\"\n"
    "\"Background\"=\"35 38 46\"\n"
    "\"ButtonDkShadow\"=\"18 20 26\"\n"
    "\"ButtonFace\"=\"47 51 60\"\n"
    "\"ButtonHilight\"=\"68 74 86\"\n"
    "\"ButtonLight\"=\"58 63 74\"\n"
    "\"ButtonShadow\"=\"26 28 35\"\n"
    "\"ButtonText\"=\"244 240 232\"\n"
    "\"Control\"=\"47 51 60\"\n"
    "\"GrayText\"=\"139 144 153\"\n"
    "\"Hilight\"=\"0 111 255\"\n"
    "\"HilightText\"=\"244 240 232\"\n"
    "\"InactiveBorder\"=\"47 51 60\"\n"
    "\"InactiveTitle\"=\"35 38 46\"\n"
    "\"InactiveTitleText\"=\"139 144 153\"\n"
    "\"InfoText\"=\"244 240 232\"\n"
    "\"InfoWindow\"=\"47 51 60\"\n"
    "\"Menu\"=\"47 51 60\"\n"
    "\"MenuBar\"=\"47 51 60\"\n"
    "\"MenuHilight\"=\"0 111 255\"\n"
    "\"MenuText\"=\"244 240 232\"\n"
    "\"Scrollbar\"=\"35 38 46\"\n"
    "\"TitleText\"=\"244 240 232\"\n"
    "\"Window\"=\"35 38 46\"\n"
    "\"WindowFrame\"=\"58 63 74\"\n"
    "\"WindowText\"=\"244 240 232\"\n";

static int write_all(int fd, const char *data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t written = write(fd, data + total, len - total);
        if (written > 0) {
            total += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int finncompat_validate_wine_prefix(const char *prefix, unsigned uid,
                                           char *error, size_t error_cap) {
    if (!prefix || prefix[0] != '/' || strlen(prefix) >= PATH_MAX || uid == 0) {
        set_error(error, error_cap, "invalid Wine prefix");
        return 0;
    }
    for (const unsigned char *p = (const unsigned char *)prefix; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) {
            set_error(error, error_cap, "Wine prefix path contains a control character");
            return 0;
        }
    }
    return 1;
}

int finncompat_wine_configure_prefix(const char *prefix,
                                     unsigned uid, unsigned gid,
                                     int dark,
                                     char *error, size_t error_cap) {
    if (!finncompat_validate_wine_prefix(prefix, uid, error, error_cap)) {
        return -1;
    }
    if (gid == 0) {
        set_error(error, error_cap, "invalid Wine group id");
        return -1;
    }

    int dir_fd = open(prefix, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir_fd < 0) {
        set_error(error, error_cap, "could not open Wine prefix directory");
        return -1;
    }

    struct stat st;
    if (fstat(dir_fd, &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != (uid_t)uid || (st.st_mode & 0077) != 0) {
        close(dir_fd);
        set_error(error, error_cap, "Wine prefix is not private to Finn");
        return -1;
    }

    char *existing = NULL;
    size_t existing_len = 0;
    int reg_fd = openat(dir_fd, "user.reg", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (reg_fd >= 0) {
        struct stat reg_st;
        if (fstat(reg_fd, &reg_st) != 0 || !S_ISREG(reg_st.st_mode)) {
            close(reg_fd);
            close(dir_fd);
            set_error(error, error_cap, "invalid Wine user.reg file");
            return -1;
        }
        if (reg_st.st_size > (off_t)(2 * 1024 * 1024)) {
            close(reg_fd);
            close(dir_fd);
            set_error(error, error_cap, "Wine user.reg is too large");
            return -1;
        }
        size_t cap = (size_t)reg_st.st_size;
        existing = malloc(cap + 1);
        if (!existing) {
            close(reg_fd);
            close(dir_fd);
            set_error(error, error_cap, "could not allocate registry buffer");
            return -1;
        }
        while (existing_len < cap) {
            ssize_t got = read(reg_fd, existing + existing_len, cap - existing_len);
            if (got > 0) {
                existing_len += (size_t)got;
            } else if (got < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        close(reg_fd);
        existing[existing_len] = '\0';
    } else if (errno != ENOENT) {
        close(dir_fd);
        set_error(error, error_cap, "could not read Wine user.reg");
        return -1;
    }

    /*
     * Strip any previous Finn-managed block so new driver and theme keys overwrite
     * cleanly while preserving user- or installer-generated keys in the prefix.
     */
    char *stripped = NULL;
    size_t stripped_len = 0;
    if (existing && existing_len > 0) {
        stripped = malloc(existing_len + 1);
        if (!stripped) {
            free(existing);
            close(dir_fd);
            set_error(error, error_cap, "could not allocate registry buffer");
            return -1;
        }
        const char *p = existing;
        const char *end_buf = existing + existing_len;
        const char *begin_tag = ";; FINNOS-MANAGED-BEGIN";
        const char *end_tag = ";; FINNOS-MANAGED-END";
        size_t end_tag_len = strlen(end_tag);

        while (p < end_buf) {
            const char *begin = strstr(p, begin_tag);
            if (!begin) {
                size_t tail = (size_t)(end_buf - p);
                memcpy(stripped + stripped_len, p, tail);
                stripped_len += tail;
                break;
            }
            if (begin > p) {
                size_t chunk = (size_t)(begin - p);
                memcpy(stripped + stripped_len, p, chunk);
                stripped_len += chunk;
            }
            const char *end = strstr(begin, end_tag);
            if (!end) break;
            p = end + end_tag_len;
            while (p < end_buf && (*p == '\r' || *p == '\n')) {
                p++;
            }
        }
        while (stripped_len > 0 &&
               (stripped[stripped_len - 1] == '\r' || stripped[stripped_len - 1] == '\n')) {
            stripped_len--;
        }
        stripped[stripped_len] = '\0';
    }

    int tmp_fd = openat(dir_fd, "user.reg.finn.tmp",
                        O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (tmp_fd < 0) {
        free(existing);
        free(stripped);
        close(dir_fd);
        set_error(error, error_cap, "could not create temporary Wine registry file");
        return -1;
    }

    /*
     * A valid Wine registry starts with the version header; ensure it appears
     * first whether creating a fresh registry or updating an existing one.
     */
    int has_header = stripped && stripped_len >= 23 &&
                     strncmp(stripped, "WINE REGISTRY Version 2", 23) == 0;
    if (!has_header) {
        if (write_all(tmp_fd, WINE_REG_HEADER, strlen(WINE_REG_HEADER)) != 0) {
            goto write_err;
        }
    }
    if (stripped && stripped_len > 0) {
        if (write_all(tmp_fd, stripped, stripped_len) != 0) {
            goto write_err;
        }
        if (write_all(tmp_fd, "\n\n", 2) != 0) {
            goto write_err;
        }
    }

    if (write_all(tmp_fd, FINN_MANAGED_BEGIN, strlen(FINN_MANAGED_BEGIN)) != 0 ||
        write_all(tmp_fd, FINN_WINE_SETTINGS_COMMON, strlen(FINN_WINE_SETTINGS_COMMON)) != 0 ||
        write_all(tmp_fd, dark ? FINN_WINE_COLORS_DARK : FINN_WINE_COLORS_LIGHT,
                  strlen(dark ? FINN_WINE_COLORS_DARK : FINN_WINE_COLORS_LIGHT)) != 0 ||
        write_all(tmp_fd, FINN_MANAGED_END, strlen(FINN_MANAGED_END)) != 0) {
        goto write_err;
    }

    if (fchmod(tmp_fd, 0600) != 0) {
        set_error(error, error_cap, "could not set Wine registry permissions");
        goto clean_err;
    }
    if (fchown(tmp_fd, (uid_t)uid, (gid_t)gid) != 0) {
        if (geteuid() == 0 || errno != EPERM) {
            set_error(error, error_cap, "could not set Wine registry ownership");
            goto clean_err;
        }
    }
    if (close(tmp_fd) != 0) {
        tmp_fd = -1;
        set_error(error, error_cap, "could not flush Wine registry file");
        goto clean_err;
    }
    tmp_fd = -1;

    if (renameat(dir_fd, "user.reg.finn.tmp", dir_fd, "user.reg") != 0) {
        set_error(error, error_cap, "could not install Wine registry file");
        goto clean_err;
    }

    free(existing);
    free(stripped);
    close(dir_fd);
    return 0;

write_err:
    set_error(error, error_cap, "could not write Wine registry file");
clean_err:
    if (tmp_fd >= 0) close(tmp_fd);
    unlinkat(dir_fd, "user.reg.finn.tmp", 0);
    close(dir_fd);
    free(existing);
    free(stripped);
    return -1;
}

int finncompat_flatpak_run(const char *systemd_run, const char *flatpak,
                           const char *unit, const char *app_id,
                           unsigned uid, unsigned gid,
                           char *error, size_t error_cap) {
    if (!systemd_run || !flatpak || flatpak[0] != '/' ||
        !unit_name_valid(unit) || !finncompat_flatpak_id_valid(app_id) ||
        uid == 0 || gid == 0) {
        set_error(error, error_cap, "invalid Flatpak launch request");
        return -1;
    }

    char unit_arg[144], uid_arg[32], gid_arg[32], runtime_arg[96];
    int un = snprintf(unit_arg, sizeof unit_arg, "--unit=%s", unit);
    int uin = snprintf(uid_arg, sizeof uid_arg, "--uid=%u", uid);
    int gin = snprintf(gid_arg, sizeof gid_arg, "--gid=%u", gid);
    int rn = snprintf(runtime_arg, sizeof runtime_arg,
                      "--setenv=XDG_RUNTIME_DIR=/run/user/%u", uid);
    if (un <= 0 || (size_t)un >= sizeof unit_arg ||
        uin <= 0 || (size_t)uin >= sizeof uid_arg ||
        gin <= 0 || (size_t)gin >= sizeof gid_arg ||
        rn <= 0 || (size_t)rn >= sizeof runtime_arg) {
        set_error(error, error_cap, "Flatpak launch arguments are too long");
        return -1;
    }

    char *argv[] = {
        (char *)systemd_run, "--quiet", "--collect", unit_arg,
        "--service-type=exec", uid_arg, gid_arg,
        "--setenv=HOME=/home/finn", "--setenv=USER=finn",
        "--setenv=LOGNAME=finn", "--setenv=SHELL=/bin/bash",
        "--setenv=PATH=/usr/local/bin:/usr/bin:/bin",
        runtime_arg, "--setenv=WAYLAND_DISPLAY=wayland-0",
        "--setenv=XDG_SESSION_TYPE=wayland", "--setenv=GTK_CSD=0",
        "--property=Restart=no", "--",
        "/usr/bin/dbus-run-session", "--", (char *)flatpak, "run", "--system",
        "--env=GTK_CSD=0", "--socket=wayland", "--socket=session-bus",
        (char *)app_id, NULL
    };
    uint8_t *output = NULL;
    size_t output_len = 0;
    int rc = run_capture(systemd_run, argv, SYSTEMD_TIMEOUT_MS, 2048,
                         &output, &output_len, error, error_cap);
    free(output);
    return rc;
}

static int query_text_valid(const char *query) {
    if (!query || !*query || strlen(query) > FINN_FLATPAK_QUERY_MAX) return 0;
    for (const unsigned char *p = (const unsigned char *)query; *p; p++)
        if (*p < 0x20 || *p == 0x7f) return 0;
    return 1;
}

int finncompat_flatpak_query(const char *finnpkg, const char *action,
                             const char *query, uint8_t **out, size_t *out_len,
                             char *error, size_t error_cap) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!finnpkg || !action || !out || !out_len) {
        set_error(error, error_cap, "invalid Flatpak query");
        return -1;
    }

    char *argv[5] = { (char *)finnpkg, "flatpak", (char *)action, NULL, NULL };
    if (!strcmp(action, "list")) {
        if (query && *query) {
            set_error(error, error_cap, "flatpak list takes no search term");
            return -1;
        }
    } else if (!strcmp(action, "search")) {
        if (!query_text_valid(query)) {
            set_error(error, error_cap, "flatpak search needs a printable search term (up to 128 bytes)");
            return -1;
        }
        argv[3] = (char *)query;
    } else {
        set_error(error, error_cap, "unsupported Flatpak query");
        return -1;
    }

    return run_capture(finnpkg, argv, QUERY_TIMEOUT_MS, MAX_CAPTURE_BYTES,
                       out, out_len, error, error_cap);
}

int finncompat_flatpak_start(const char *systemd_run, const char *finnpkg,
                             const char *job_id, const char *action,
                             const char *app_id, const char *remote,
                             char *error, size_t error_cap) {
    int install = action && !strcmp(action, "install");
    int remove = action && !strcmp(action, "remove");
    if (!systemd_run || !finnpkg || !action ||
        !finncompat_flatpak_job_id_valid(job_id) ||
        !finncompat_flatpak_id_valid(app_id) ||
        (!install && !remove) ||
        (install && !finncompat_flatpak_remote_valid(remote)) ||
        (remove && remote)) {
        set_error(error, error_cap, "invalid Flatpak package operation");
        return -1;
    }

    char unit_arg[FINN_FLATPAK_JOB_ID_MAX + 16];
    int n = snprintf(unit_arg, sizeof unit_arg, "--unit=%s", job_id);
    if (n <= 0 || (size_t)n >= sizeof unit_arg) {
        set_error(error, error_cap, "Flatpak job id is too long");
        return -1;
    }
    char *argv[] = {
        (char *)systemd_run, "--quiet", "--system", "--no-block", unit_arg,
        "--property=Type=exec", "--property=Restart=no",
        (char *)finnpkg, "flatpak", (char *)action, (char *)app_id,
        (char *)remote, NULL
    };
    if (!remote) argv[11] = NULL;
    uint8_t *output = NULL;
    size_t output_len = 0;
    int rc = run_capture(systemd_run, argv, SYSTEMD_TIMEOUT_MS, 2048,
                         &output, &output_len, error, error_cap);
    free(output);
    return rc;
}

static int winget_query_text_valid(const char *query) {
    if (!query || !*query || strlen(query) > FINN_WINGET_QUERY_MAX) return 0;
    for (const unsigned char *p = (const unsigned char *)query; *p; p++)
        if (*p < 0x20 || *p == 0x7f) return 0;
    return 1;
}

int finncompat_winget_query(const char *finnpkg, const char *action,
                            const char *argument,
                            uint8_t **out, size_t *out_len,
                            char *error, size_t error_cap) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!finnpkg || !action || !argument || !out || !out_len) {
        set_error(error, error_cap, "invalid winget query");
        return -1;
    }

    char *argv[5] = { (char *)finnpkg, "winget", (char *)action, NULL, NULL };
    int timeout_ms;
    if (!strcmp(action, "search")) {
        if (!winget_query_text_valid(argument)) {
            set_error(error, error_cap,
                      "winget search needs a printable search term (up to 128 bytes)");
            return -1;
        }
        argv[3] = (char *)argument;
        /* The index is remote; the cache makes a repeat search instant. */
        timeout_ms = 20000;
    } else if (!strcmp(action, "info")) {
        if (!finncompat_winget_id_valid(argument)) {
            set_error(error, error_cap, "winget info needs a valid package id");
            return -1;
        }
        argv[3] = (char *)argument;
        /* info reads the version list and three manifests from GitHub. */
        timeout_ms = 30000;
    } else {
        set_error(error, error_cap, "unsupported winget query");
        return -1;
    }

    return run_capture(finnpkg, argv, timeout_ms, MAX_CAPTURE_BYTES,
                       out, out_len, error, error_cap);
}

int finncompat_winget_start(const char *systemd_run, const char *finnpkg,
                            const char *job_id, const char *id,
                            unsigned uid, unsigned gid, const char *home,
                            char *error, size_t error_cap) {
    if (!systemd_run || systemd_run[0] != '/' || !finnpkg || finnpkg[0] != '/' ||
        !finncompat_winget_job_id_valid(job_id) ||
        !finncompat_winget_id_valid(id) ||
        !home || home[0] != '/' || uid == 0 || gid == 0) {
        set_error(error, error_cap, "invalid winget install request");
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)home; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) {
            set_error(error, error_cap, "winget home contains a control character");
            return -1;
        }
    }

    char unit_arg[FINN_WINGET_JOB_ID_MAX + 16], uid_arg[32], gid_arg[32];
    char home_arg[PATH_MAX + 32], runtime_arg[96], job_arg[FINN_WINGET_JOB_ID_MAX + 32];
    int un = snprintf(unit_arg, sizeof unit_arg, "--unit=%s", job_id);
    int uin = snprintf(uid_arg, sizeof uid_arg, "--uid=%u", uid);
    int gin = snprintf(gid_arg, sizeof gid_arg, "--gid=%u", gid);
    int hn = snprintf(home_arg, sizeof home_arg, "--setenv=HOME=%s", home);
    int rn = snprintf(runtime_arg, sizeof runtime_arg,
                      "--setenv=XDG_RUNTIME_DIR=/run/user/%u", uid);
    int jn = snprintf(job_arg, sizeof job_arg, "--setenv=FINN_WINGET_JOB=%s", job_id);
    if (un <= 0 || (size_t)un >= sizeof unit_arg ||
        uin <= 0 || (size_t)uin >= sizeof uid_arg ||
        gin <= 0 || (size_t)gin >= sizeof gid_arg ||
        hn <= 0 || (size_t)hn >= sizeof home_arg ||
        rn <= 0 || (size_t)rn >= sizeof runtime_arg ||
        jn <= 0 || (size_t)jn >= sizeof job_arg) {
        set_error(error, error_cap, "winget install arguments are too long");
        return -1;
    }

    char *argv[] = {
        (char *)systemd_run, "--quiet", "--system", "--no-block", unit_arg,
        "--property=Type=exec", "--property=Restart=no",
        uid_arg, gid_arg, home_arg,
        "--setenv=USER=finn", "--setenv=LOGNAME=finn",
        "--setenv=PATH=/usr/local/bin:/usr/bin:/bin",
        runtime_arg, job_arg,
        (char *)finnpkg, "winget", "install", (char *)id, NULL
    };
    uint8_t *output = NULL;
    size_t output_len = 0;
    int rc = run_capture(systemd_run, argv, SYSTEMD_TIMEOUT_MS, 2048,
                         &output, &output_len, error, error_cap);
    free(output);
    return rc;
}

int finncompat_ota_query(const char *finnpkg, const char *action,
                         uint8_t **out, size_t *out_len,
                         char *error, size_t error_cap) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!finnpkg || !action || !out || !out_len) {
        set_error(error, error_cap, "invalid OTA query");
        return -1;
    }

    char *argv[5] = { (char *)finnpkg, "update", (char *)action, NULL, NULL };
    int timeout_ms;
    size_t max_bytes;
    if (!strcmp(action, "status")) {
        /* status reads two small local files; it must not wait on a network. */
        timeout_ms = 3000;
        max_bytes = 8192;
    } else if (!strcmp(action, "check")) {
        /* One GitHub release lookup, bounded so a stalled feed cannot hold
         * the single control connection. */
        timeout_ms = 30000;
        max_bytes = 65536;
    } else {
        set_error(error, error_cap, "unsupported OTA query");
        return -1;
    }

    return run_capture(finnpkg, argv, timeout_ms, max_bytes,
                       out, out_len, error, error_cap);
}

int finncompat_ota_start(const char *systemd_run, const char *finnpkg,
                         const char *job_id, const char *action,
                         char *error, size_t error_cap) {
    if (!systemd_run || systemd_run[0] != '/' || !finnpkg || finnpkg[0] != '/' ||
        !finncompat_ota_job_id_valid(job_id) || !action ||
        (strcmp(action, "download") && strcmp(action, "install") &&
         strcmp(action, "rollback"))) {
        set_error(error, error_cap, "invalid OTA job request");
        return -1;
    }

    char unit_arg[FINN_OTA_JOB_ID_MAX + 16];
    int n = snprintf(unit_arg, sizeof unit_arg, "--unit=%s", job_id);
    if (n <= 0 || (size_t)n >= sizeof unit_arg) {
        set_error(error, error_cap, "OTA job id is too long");
        return -1;
    }
    char *argv[] = {
        (char *)systemd_run, "--quiet", "--system", "--no-block", unit_arg,
        "--property=Type=exec", "--property=Restart=no",
        (char *)finnpkg, "update", (char *)action, NULL
    };
    uint8_t *output = NULL;
    size_t output_len = 0;
    int rc = run_capture(systemd_run, argv, SYSTEMD_TIMEOUT_MS, 2048,
                         &output, &output_len, error, error_cap);
    free(output);
    return rc;
}

static const char *property(const char *text, const char *name,
                            char *value, size_t cap) {
    size_t name_len = strlen(name);
    const char *p = text;
    while (p && *p) {
        const char *end = strchr(p, '\n');
        size_t line_len = end ? (size_t)(end - p) : strlen(p);
        if (line_len > name_len && !memcmp(p, name, name_len) && p[name_len] == '=') {
            size_t n = line_len - name_len - 1;
            if (n >= cap) n = cap - 1;
            memcpy(value, p + name_len + 1, n);
            value[n] = '\0';
            return value;
        }
        p = end ? end + 1 : NULL;
    }
    return NULL;
}

struct job_state {
    char load[32];
    char active[32];
    char result[64];
    char exit_status[32];
    char started[32];
};

/*
 * One `systemctl show` for every job family. The unit name is the id plus
 * `.service` and nothing else; systemd, not this daemon, is the record of
 * what a job did.
 */
static int job_probe(const char *systemctl, const char *job_id,
                     const char *label, struct job_state *st,
                     char *error, size_t error_cap) {
    if (!systemctl || !st || !job_id ||
        strlen(job_id) > FINN_OTA_JOB_ID_MAX) {
        set_error(error, error_cap, "invalid job id");
        return -1;
    }

    char unit[FINN_OTA_JOB_ID_MAX + 16];
    int n = snprintf(unit, sizeof unit, "%s.service", job_id);
    if (n <= 0 || (size_t)n >= sizeof unit) {
        set_error(error, error_cap, "job id is too long");
        return -1;
    }
    char *argv[] = {
        (char *)systemctl, "show", "--no-pager", "--property=LoadState",
        "--property=ActiveState", "--property=Result",
        "--property=ExecMainStatus", "--property=ExecMainStartTimestampMonotonic",
        unit, NULL
    };
    uint8_t *output = NULL;
    size_t output_len = 0;
    if (run_capture(systemctl, argv, SYSTEMD_TIMEOUT_MS, 4096,
                    &output, &output_len, error, error_cap) != 0)
        return -1;

    char *text = malloc(output_len + 1);
    if (!text) {
        free(output);
        set_error(error, error_cap, "could not allocate job status");
        return -1;
    }
    memcpy(text, output, output_len);
    text[output_len] = '\0';
    free(output);

    memset(st, 0, sizeof *st);
    property(text, "LoadState", st->load, sizeof st->load);
    property(text, "ActiveState", st->active, sizeof st->active);
    property(text, "Result", st->result, sizeof st->result);
    property(text, "ExecMainStatus", st->exit_status, sizeof st->exit_status);
    property(text, "ExecMainStartTimestampMonotonic", st->started, sizeof st->started);
    free(text);

    if (!strcmp(st->load, "not-found") || !*st->load) {
        char message[96];
        snprintf(message, sizeof message, "%s job was not found or has expired", label);
        set_error(error, error_cap, message);
        return -1;
    }
    return 0;
}

static const char *job_word_from_state(const struct job_state *st) {
    if (!strcmp(st->active, "active") || !strcmp(st->active, "activating"))
        return "running";
    if (!strcmp(st->active, "failed") ||
        (strcmp(st->active, "inactive") && *st->active) ||
        (*st->result && strcmp(st->result, "success")))
        return "failed";
    if (strcmp(st->started, "0") != 0 && *st->started && !strcmp(st->result, "success"))
        return "complete";
    return "queued";
}

int finncompat_flatpak_status(const char *systemctl, const char *job_id,
                              char *state, size_t state_cap,
                              char *error, size_t error_cap) {
    if (!systemctl || !state || !state_cap ||
        !finncompat_flatpak_job_id_valid(job_id)) {
        set_error(error, error_cap, "invalid Flatpak job id");
        return -1;
    }

    struct job_state st;
    if (job_probe(systemctl, job_id, "Flatpak", &st, error, error_cap) != 0)
        return -1;
    const char *word = job_word_from_state(&st);
    if (!strcmp(word, "failed"))
        snprintf(state, state_cap, "state=failed result=%s exit=%s", st.result, st.exit_status);
    else
        snprintf(state, state_cap, "state=%s", word);
    return 0;
}

int finncompat_job_word(const char *systemctl, const char *job_id,
                        char *word, size_t word_cap,
                        char *error, size_t error_cap) {
    if (!systemctl || !word || !word_cap ||
        !(finncompat_winget_job_id_valid(job_id) ||
          finncompat_ota_job_id_valid(job_id))) {
        set_error(error, error_cap, "invalid job id");
        return -1;
    }

    struct job_state st;
    if (job_probe(systemctl, job_id, "job", &st, error, error_cap) != 0)
        return -1;
    snprintf(word, word_cap, "%s", job_word_from_state(&st));
    return 0;
}
