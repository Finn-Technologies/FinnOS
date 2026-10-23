/* finnsettings — the one versioned store for the settings FinnOS remembers. */
#include "finnsettings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define KEY_MAX    32
#define VALUE_MAX  128
#define MAX_KEYS   64

typedef struct {
    char key[KEY_MAX + 1];
    char value[VALUE_MAX + 1];
} setting;

/* ---------------- validation ---------------- */

static int key_valid(const char *key) {
    if (!key) return 0;
    size_t n = strlen(key);
    if (n < 1 || n > KEY_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = key[i];
        if (c >= 'a' && c <= 'z') continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '_' || c == '.' || c == '-') continue;
        return 0;
    }
    /* The stamp owns this name; a setting called "version" would be read
     * back as the migration field. */
    return strcmp(key, "version") != 0;
}

static int value_valid(const char *value) {
    if (!value) return 0;
    size_t n = strlen(value);
    if (n > VALUE_MAX) return 0;
    if (strchr(value, '\n') || strchr(value, '\r')) return 0;
    if (n > 0 && (value[0] == ' ' || value[n - 1] == ' ')) return 0;
    return 1;
}

/* ---------------- file ---------------- */

/*
 * Loads the store into `out`. Anything that is not a file this build wrote is
 * "no settings": a missing file, a missing or unknown version line, or a file
 * that is not ours at all. The caller then starts from empty, and the next
 * write stamps the current version over it.
 */
static int settings_load(const char *path, setting *out, int cap) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[KEY_MAX + VALUE_MAX + 4];
    if (!fgets(line, sizeof line, f)) { fclose(f); return -1; }
    char *nl = strchr(line, '\n');
    if (nl) *nl = 0;
    char stamp[32];
    snprintf(stamp, sizeof stamp, "version=%d", FINN_SETTINGS_VERSION);
    if (strcmp(line, stamp)) { fclose(f); return -1; }

    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *e = strchr(line, '\n');
        if (!e && !feof(f)) {
            /* Longer than a legal line: not something we wrote. Drop the rest
             * of it rather than treating the tail as another key. */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') { }
            continue;
        }
        if (e) *e = 0;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (!key_valid(line) || !value_valid(eq + 1)) continue;
        if (n >= cap) break;
        snprintf(out[n].key, sizeof out[n].key, "%s", line);
        snprintf(out[n].value, sizeof out[n].value, "%s", eq + 1);
        n++;
    }
    fclose(f);
    return n;
}

static int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += w;
        n -= (size_t)w;
    }
    return 0;
}

static void split_path(const char *path, char *dir, size_t dcap,
                       char *base, size_t bcap) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(dir, dcap, ".");
        snprintf(base, bcap, "%s", path);
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1;                      /* the root directory */
    if (n >= dcap) n = dcap - 1;
    memcpy(dir, path, n);
    dir[n] = 0;
    snprintf(base, bcap, "%s", slash + 1);
}

/*
 * Writes the whole store to a temporary file beside the target, flushes it to
 * the disk and renames it over the target. The rename is the only step a
 * reader can observe, so the file is either the old one or the new one.
 */
static int write_atomic(const char *path, const setting *s, int n) {
    char dir[512], base[128], tmp[640];
    split_path(path, dir, sizeof dir, base, sizeof base);
    if (!base[0]) return -1;

    int len = snprintf(tmp, sizeof tmp, "%s/.%s.tmp.XXXXXX", dir, base);
    if (len < 0 || (size_t)len >= sizeof tmp) return -1;

    int fd = mkstemp(tmp);
    if (fd < 0) return -1;
    /* mkstemp already makes 0600; say it again so the mode does not depend on
     * the host's idea of a default. */
    if (fchmod(fd, 0600) != 0) { close(fd); unlink(tmp); return -1; }

    char line[KEY_MAX + VALUE_MAX + 16];
    int ok = 1;

    len = snprintf(line, sizeof line, "version=%d\n", FINN_SETTINGS_VERSION);
    if (write_all(fd, line, (size_t)len) != 0) ok = 0;

    for (int i = 0; ok && i < n; i++) {
        len = snprintf(line, sizeof line, "%s=%s\n", s[i].key, s[i].value);
        if (write_all(fd, line, (size_t)len) != 0) ok = 0;
    }

    if (ok && fsync(fd) != 0) ok = 0;
    if (close(fd) != 0) ok = 0;

    if (!ok) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }

    /* The rename has already happened; fsyncing the directory only makes it
     * survive a power cut, and a failure here cannot undo it — so it is not
     * reported as a failed write. */
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
    return 0;
}

/* ---------------- public ---------------- */

int finn_settings_get_at(const char *path, const char *key, char *out, size_t cap) {
    if (!path || !key_valid(key) || !out || cap == 0) return -1;

    setting s[MAX_KEYS];
    int n = settings_load(path, s, MAX_KEYS);
    if (n < 0) return -1;

    for (int i = 0; i < n; i++) {
        if (strcmp(s[i].key, key)) continue;
        size_t len = strlen(s[i].value);
        if (len + 1 > cap) return -1;       /* absent, never truncated */
        memcpy(out, s[i].value, len + 1);
        return 0;
    }
    return -1;
}

int finn_settings_set_at(const char *path, const char *key, const char *value) {
    if (!path || !key_valid(key) || !value_valid(value)) return -1;

    setting s[MAX_KEYS];
    int n = settings_load(path, s, MAX_KEYS);
    if (n < 0) n = 0;                       /* absent or unknown version */

    int slot = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(s[i].key, key)) { slot = i; break; }

    if (slot < 0) {
        if (n >= MAX_KEYS) return -1;
        slot = n++;
    }
    snprintf(s[slot].key, sizeof s[slot].key, "%s", key);
    snprintf(s[slot].value, sizeof s[slot].value, "%s", value);

    return write_atomic(path, s, n);
}

int finn_settings_get(const char *key, char *out, size_t cap) {
    return finn_settings_get_at(FINN_SETTINGS_PATH, key, out, cap);
}

int finn_settings_get_int(const char *key, int *out) {
    if (!out) return -1;

    char buf[VALUE_MAX + 1];
    if (finn_settings_get(key, buf, sizeof buf) != 0) return -1;

    char *end = NULL;
    errno = 0;
    long v = strtol(buf, &end, 10);
    if (errno || !end || end == buf || *end) return -1;
    if (v < -2147483647L || v > 2147483647L) return -1;
    *out = (int)v;
    return 0;
}

int finn_settings_set(const char *key, const char *value) {
    /* /var/lib/finn belongs to the system, not to the caller; the store is the
     * one place that knows where its file lives, so it makes the directory
     * rather than making every writer repeat the path. */
    char dir[512], base[128];
    split_path(FINN_SETTINGS_PATH, dir, sizeof dir, base, sizeof base);
    mkdir(dir, 0755);
    return finn_settings_set_at(FINN_SETTINGS_PATH, key, value);
}

int finn_settings_set_int(const char *key, int value) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", value);
    return finn_settings_set(key, buf);
}
