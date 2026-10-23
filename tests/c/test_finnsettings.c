/*
 * finnsettings — the one versioned settings store.
 *
 * The store is exercised through its explicit-path forms, so the test needs
 * no /var/lib and no root: a temporary directory is the whole world. What it
 * asserts is the contract the shell depends on — a key comes back exactly as
 * it went in, an unknown file is empty rather than guessed, bad input is
 * refused, and a write either replaces the file whole or leaves it alone.
 *
 * Run it with `cc -I src/core tests/c/test_finnsettings.c src/core/finnsettings.c`.
 */
#define _GNU_SOURCE
#include "finnsettings.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int g_failed = 0;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) g_failed = 1;
}

static int read_file(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    return 0;
}

static int write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fputs(text, f) >= 0;
    fclose(f);
    return ok ? 0 : -1;
}

/* A temporary file left behind by a crash must not be mistaken for the store. */
static int count_temps(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' && strstr(e->d_name, ".settings.tmp")) n++;
    }
    closedir(d);
    return n;
}

int main(void) {
    char dir[] = "/tmp/finnsettings.XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }

    char path[512];
    snprintf(path, sizeof path, "%s/settings", dir);

    char buf[256];

    /* A store that has never been written is absent, not full of defaults. */
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == -1,
          "missing file reads as absent");

    check(finn_settings_set_at(path, "volume", "37") == 0, "write a string");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == 0 &&
          strcmp(buf, "37") == 0, "read the string back unchanged");
    check(finn_settings_get_at(path, "appearance", buf, sizeof buf) == -1,
          "a key that was never set is absent");

    check(finn_settings_set_at(path, "brightness", "42") == 0, "write an int");
    check(finn_settings_get_at(path, "brightness", buf, sizeof buf) == 0 &&
          strcmp(buf, "42") == 0, "read the int back unchanged");

    /* A value that does not fit is absent; truncating it would be a lie. */
    check(finn_settings_get_at(path, "volume", buf, 2) == -1,
          "a value that does not fit the buffer reads as absent");

    /* Every write stamps the version and ends with a newline. */
    char raw[1024];
    check(read_file(path, raw, sizeof raw) == 0, "the store file is readable");
    check(strncmp(raw, "version=1\n", 10) == 0, "the first line is the version");
    size_t raw_len = strlen(raw);
    check(raw_len > 0 && raw[raw_len - 1] == '\n', "the file ends with a newline");

    /* Bad keys and bad values are refused, and the file is not touched. */
    check(finn_settings_set_at(path, "Volume", "1") == -1, "an uppercase key is refused");
    check(finn_settings_set_at(path, "", "1") == -1, "an empty key is refused");
    check(finn_settings_set_at(path, "a b", "1") == -1, "a key with a space is refused");
    check(finn_settings_set_at(path, "version", "2") == -1, "the version key is reserved");
    check(finn_settings_set_at(path, "abcdefghijklmnopqrstuvwxyz0123456", "1") == -1,
          "a key longer than 32 bytes is refused");
    check(finn_settings_set_at(path, "ok", "a\nb") == -1, "a value with a newline is refused");
    check(finn_settings_set_at(path, "ok", " a") == -1, "a value with a leading space is refused");
    check(finn_settings_set_at(path, "ok", "a ") == -1, "a value with a trailing space is refused");

    char longval[132];
    memset(longval, 'x', 129);
    longval[129] = 0;
    check(finn_settings_set_at(path, "ok", longval) == -1,
          "a value longer than 128 bytes is refused");
    memset(longval, 'x', 128);
    longval[128] = 0;
    check(finn_settings_set_at(path, "long", longval) == 0, "a 128-byte value is accepted");

    char before[1024];
    check(read_file(path, before, sizeof before) == 0, "the store is readable after a refusal");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == 0 &&
          strcmp(buf, "37") == 0, "a refused write leaves the old value");

    /* A set over an existing file replaces it whole and keeps the other keys. */
    check(finn_settings_set_at(path, "volume", "55") == 0, "overwrite an existing key");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == 0 &&
          strcmp(buf, "55") == 0, "the overwritten value is the new one");
    check(finn_settings_get_at(path, "brightness", buf, sizeof buf) == 0 &&
          strcmp(buf, "42") == 0, "the other keys survive the overwrite");
    check(finn_settings_get_at(path, "long", buf, sizeof buf) == 0 &&
          strlen(buf) == 128, "the long value survives too");
    check(count_temps(dir) == 0, "a completed write leaves no temporary file");

    /* An unknown version is "no settings", and the next write re-stamps. */
    check(write_file(path, "version=99\nvolume=9\n") == 0, "plant a future-version file");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == -1,
          "an unknown version reads as empty");
    check(finn_settings_set_at(path, "appearance", "dark") == 0,
          "a write over an unknown version succeeds");
    check(finn_settings_get_at(path, "appearance", buf, sizeof buf) == 0 &&
          strcmp(buf, "dark") == 0, "the write is readable");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == -1,
          "the old file's keys are not adopted");
    check(read_file(path, raw, sizeof raw) == 0 && strncmp(raw, "version=1\n", 10) == 0,
          "the migration stamp is the current version");

    /* A missing version line is empty in the same way. */
    check(write_file(path, "volume=9\n") == 0, "plant a file with no version");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == -1,
          "a file with no version reads as empty");

    /* The mode is private: a setting is not world-readable. */
    check(finn_settings_set_at(path, "volume", "20") == 0, "write before the mode check");
    struct stat sb;
    check(stat(path, &sb) == 0 && (sb.st_mode & 0777) == 0600,
          "the store is mode 0600");

    /* A crash can leave a temp file; it must not become the store. */
    char stale[512];
    snprintf(stale, sizeof stale, "%s/.settings.tmp.stale", dir);
    check(write_file(stale, "version=1\nvolume=99\n") == 0, "plant a leftover temp file");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == 0 &&
          strcmp(buf, "20") == 0, "a leftover temp file is not read");
    check(finn_settings_set_at(path, "volume", "21") == 0,
          "a write still works with a leftover temp file");
    check(finn_settings_get_at(path, "volume", buf, sizeof buf) == 0 &&
          strcmp(buf, "21") == 0, "the write after a leftover is the new value");

    /* An explicit path never invents the directory it was given. */
    char nowhere[512];
    snprintf(nowhere, sizeof nowhere, "%s/missing/settings", dir);
    check(finn_settings_set_at(nowhere, "volume", "1") == -1,
          "an explicit path with no directory is refused");

    unlink(stale);
    unlink(path);
    rmdir(dir);

    printf("%s\n", g_failed ? "finnsettings: FAILED" : "finnsettings: all ok");
    return g_failed;
}
