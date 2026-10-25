#define _POSIX_C_SOURCE 200809L
#include "../../src/control/finncompat.h"
#include "../../src/core/finnproto.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_dir[] = "/tmp/finncompat-test-XXXXXX";

static void write_script(const char *name, const char *body, char *path, size_t cap) {
    int n = snprintf(path, cap, "%s/%s", g_dir, name);
    assert(n > 0 && (size_t)n < cap);
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs("#!/bin/sh\n", f) >= 0);
    assert(fputs(body, f) >= 0);
    assert(fclose(f) == 0);
    assert(chmod(path, 0700) == 0);
}

static void read_text(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    assert(f != NULL);
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    assert(fclose(f) == 0);
}

static void test_ids(void) {
    char id[FINN_FLATPAK_JOB_ID_MAX];
    assert(finncompat_wine_id_valid("paint-app2"));
    assert(!finncompat_wine_id_valid(""));
    assert(!finncompat_wine_id_valid("../paint"));
    assert(!finncompat_wine_id_valid("Paint"));
    assert(!finncompat_wine_id_valid("paint;touch"));
    assert(finncompat_flatpak_id_valid("org.gnome.Calculator"));
    assert(finncompat_flatpak_id_valid("com.example.App_2"));
    assert(finncompat_flatpak_remote_valid("flathub"));
    assert(finncompat_flatpak_remote_valid("gnome-nightly"));
    assert(!finncompat_flatpak_remote_valid(""));
    assert(!finncompat_flatpak_remote_valid("flathub;reboot"));
    assert(!finncompat_flatpak_remote_valid("has space"));
    assert(!finncompat_flatpak_id_valid(""));
    assert(!finncompat_flatpak_id_valid("Calculator"));
    assert(!finncompat_flatpak_id_valid("org..Calculator"));
    assert(!finncompat_flatpak_id_valid("org.2Calculator"));
    assert(!finncompat_flatpak_id_valid("org.example.App;touch"));
    assert(!finncompat_flatpak_id_valid("org.example.App/run"));

    assert(finncompat_flatpak_job_id_make(id, sizeof id, 401, 8) == 0);
    assert(!strcmp(id, "finn-flatpak-401-8"));
    assert(finncompat_flatpak_job_id_valid(id));
    assert(!finncompat_flatpak_job_id_valid("finn-flatpak-401-8.service"));
    assert(!finncompat_flatpak_job_id_valid("finn-flatpak-401-x"));
    assert(!finncompat_flatpak_job_id_valid("finn-flatpak-401-8;reboot"));
    assert(finncompat_flatpak_job_id_make(id, sizeof id, 0, 8) != 0);
}

static int bytes_contain(const uint8_t *data, size_t len, const char *needle) {
    size_t n = strlen(needle);
    if (n > len) return 0;
    for (size_t i = 0; i + n <= len; i++)
        if (memcmp(data + i, needle, n) == 0) return 1;
    return 0;
}

static void test_query(const char *pkg, const char *log_path) {
    uint8_t *out = NULL;
    size_t len = 0;
    char error[256] = "";
    assert(finncompat_flatpak_query(pkg, "search", "2d drawing",
                                    &out, &len, error, sizeof error) == 0);
    assert(len > 0);
    assert(bytes_contain(out, len, "org.example.Paint"));
    free(out);

    char log[512];
    read_text(log_path, log, sizeof log);
    assert(strstr(log, "flatpak\nsearch\n2d drawing\n") != NULL);

    out = NULL;
    len = 0;
    assert(finncompat_flatpak_query(pkg, "list", NULL,
                                    &out, &len, error, sizeof error) == 0);
    assert(bytes_contain(out, len, "org.example.Paint"));
    free(out);

    assert(finncompat_flatpak_query(pkg, "search", "", &out, &len,
                                    error, sizeof error) != 0);
    assert(strstr(error, "printable search term") != NULL);
    assert(finncompat_flatpak_query(pkg, "remove", "org.example.Paint",
                                    &out, &len, error, sizeof error) != 0);
    assert(strstr(error, "unsupported Flatpak query") != NULL);
}

static void test_job_start(const char *systemd_run, const char *log_path) {
    char error[256] = "";
    assert(finncompat_flatpak_start(systemd_run, "/usr/local/bin/finnpkg",
                                    "finn-flatpak-401-8", "install",
                                    "org.gnome.Calculator", "flathub", error,
                                    sizeof error) == 0);
    char log[1024];
    read_text(log_path, log, sizeof log);
    assert(strstr(log, "--quiet\n--system\n--no-block\n") != NULL);
    assert(strstr(log, "--unit=finn-flatpak-401-8\n") != NULL);
    assert(strstr(log, "/usr/local/bin/finnpkg\nflatpak\ninstall\norg.gnome.Calculator\nflathub\n") != NULL);

    assert(finncompat_flatpak_start(systemd_run, "/usr/local/bin/finnpkg",
                                    "finn-flatpak-401-8", "remove",
                                    "org.gnome.Calculator", NULL, error,
                                    sizeof error) == 0);
    read_text(log_path, log, sizeof log);
    assert(strstr(log, "/usr/local/bin/finnpkg\nflatpak\nremove\norg.gnome.Calculator\n") != NULL);

    assert(finncompat_flatpak_start(systemd_run, "/usr/local/bin/finnpkg",
                                    "finn-flatpak-401-8", "install",
                                    "org.gnome.Calculator", "flathub;reboot", error,
                                    sizeof error) != 0);
    assert(strstr(error, "invalid Flatpak package operation") != NULL);
    assert(finncompat_flatpak_start(systemd_run, "/usr/local/bin/finnpkg",
                                    "finn-flatpak-401-8.service", "install",
                                    "org.gnome.Calculator", "flathub", error,
                                    sizeof error) != 0);
    assert(strstr(error, "invalid Flatpak package operation") != NULL);
}

static void test_app_start(const char *systemd_run, const char *log_path) {
    char command[1024], injected[512], error[256] = "";
    int n = snprintf(injected, sizeof injected, "%s/injected", g_dir);
    assert(n > 0 && (size_t)n < sizeof injected);
    n = snprintf(command, sizeof command, "%s/pretend-app;touch %s", g_dir, injected);
    assert(n > 0 && (size_t)n < sizeof command);

    assert(finncompat_start_app(systemd_run, "finn-app-test-401", command,
                                1000, 1000, error, sizeof error) == 0);
    assert(access(injected, F_OK) != 0);

    char log[2048];
    read_text(log_path, log, sizeof log);
    assert(strstr(log, "--quiet\n--collect\n--unit=finn-app-test-401\n") != NULL);
    assert(strstr(log, "--uid=1000\n--gid=1000\n") != NULL);
    assert(strstr(log, "--setenv=XDG_RUNTIME_DIR=/run/user/1000\n") != NULL);
    assert(strstr(log, "--setenv=WAYLAND_DISPLAY=wayland-0\n") != NULL);
    assert(strstr(log, "--property=Restart=no\n--\n") != NULL);
    assert(strstr(log, command) != NULL);

    assert(finncompat_start_app(systemd_run, "bad;unit", "/usr/bin/example",
                                1000, 1000, error, sizeof error) != 0);
    assert(strstr(error, "invalid application launch request") != NULL);
    assert(finncompat_start_app(systemd_run, "finn-app-test-401", "relative-app",
                                1000, 1000, error, sizeof error) != 0);
    assert(strstr(error, "invalid application launch request") != NULL);
}

static void test_flatpak_run(const char *systemd_run, const char *log_path) {
    char error[256] = "";
    assert(finncompat_flatpak_run(systemd_run, "/usr/bin/flatpak",
                                  "finn-flatpak-run-401-8",
                                  "org.gnome.Calculator", 1000, 1000,
                                  error, sizeof error) == 0);
    char log[2048];
    read_text(log_path, log, sizeof log);
    assert(strstr(log, "--setenv=XDG_RUNTIME_DIR=/run/user/1000\n") != NULL);
    assert(strstr(log, "--setenv=WAYLAND_DISPLAY=wayland-0\n") != NULL);
    assert(strstr(log, "/usr/bin/dbus-run-session\n--\n/usr/bin/flatpak\nrun\n--system\n") != NULL);
    assert(strstr(log, "--socket=wayland\n--socket=session-bus\norg.gnome.Calculator\n") != NULL);

    assert(finncompat_flatpak_run(systemd_run, "/usr/bin/flatpak",
                                  "bad;unit", "org.gnome.Calculator",
                                  1000, 1000, error, sizeof error) != 0);
    assert(strstr(error, "invalid Flatpak launch request") != NULL);
    assert(finncompat_flatpak_run(systemd_run, "/usr/bin/flatpak",
                                  "finn-flatpak-run-401-8",
                                  "org.gnome.Calculator;bad", 1000, 1000,
                                  error, sizeof error) != 0);
}

static void test_wine_app_read(void) {
    char wine_root[512], app_dir[512], prefix_dir[512], metadata[512];
    char executable_path[512], executable[512], prefix[512], error[256] = "";
    int n = snprintf(wine_root, sizeof wine_root, "%s/wine", g_dir);
    assert(n > 0 && (size_t)n < sizeof wine_root);
    n = snprintf(app_dir, sizeof app_dir, "%s/paint", wine_root);
    assert(n > 0 && (size_t)n < sizeof app_dir);
    n = snprintf(prefix_dir, sizeof prefix_dir, "%s/prefix", app_dir);
    assert(n > 0 && (size_t)n < sizeof prefix_dir);
    n = snprintf(metadata, sizeof metadata, "%s/executable", app_dir);
    assert(n > 0 && (size_t)n < sizeof metadata);
    n = snprintf(executable_path, sizeof executable_path, "%s/Portable Paint.exe", g_dir);
    assert(n > 0 && (size_t)n < sizeof executable_path);

    assert(mkdir(wine_root, 0700) == 0);
    assert(mkdir(app_dir, 0700) == 0);
    assert(mkdir(prefix_dir, 0700) == 0);
    FILE *f = fopen(metadata, "w");
    assert(f != NULL);
    assert(fprintf(f, "%s\n", executable_path) > 0);
    assert(fclose(f) == 0);
    assert(chmod(metadata, 0600) == 0);
    f = fopen(executable_path, "w");
    assert(f != NULL);
    assert(fputs("portable app", f) >= 0);
    assert(fclose(f) == 0);

    assert(finncompat_wine_app_read(wine_root, "paint", (unsigned)getuid(),
                                    executable, sizeof executable,
                                    prefix, sizeof prefix, error,
                                    sizeof error) == 0);
    assert(!strcmp(executable, executable_path));
    assert(!strcmp(prefix, prefix_dir));

    uint8_t *ids = NULL;
    size_t ids_len = 0;
    assert(finncompat_wine_list(wine_root, (unsigned)getuid(), &ids, &ids_len,
                                error, sizeof error) == 0);
    assert(ids_len == sizeof "paint\n" - 1);
    assert(memcmp(ids, "paint\n", ids_len) == 0);
    free(ids);

    char stray_dir[512];
    n = snprintf(stray_dir, sizeof stray_dir, "%s/stray", wine_root);
    assert(n > 0 && (size_t)n < sizeof stray_dir);
    assert(mkdir(stray_dir, 0700) == 0); /* An app-looking directory is not a registration. */
    ids = NULL;
    ids_len = 0;
    assert(finncompat_wine_list(wine_root, (unsigned)getuid(), &ids, &ids_len,
                                error, sizeof error) == 0);
    assert(ids_len == sizeof "paint\n" - 1);
    assert(memcmp(ids, "paint\n", ids_len) == 0);
    free(ids);

    char missing_root[512];
    n = snprintf(missing_root, sizeof missing_root, "%s/no-wine-data", g_dir);
    assert(n > 0 && (size_t)n < sizeof missing_root);
    assert(finncompat_wine_list(missing_root, (unsigned)getuid(), &ids, &ids_len,
                                error, sizeof error) == 0);
    assert(ids_len == 0);
    free(ids);
    assert(chmod(wine_root, 0755) == 0);
    assert(finncompat_wine_list(wine_root, (unsigned)getuid(), &ids, &ids_len,
                                error, sizeof error) != 0);
    assert(strstr(error, "not private") != NULL);
    assert(chmod(wine_root, 0700) == 0);
    assert(finncompat_wine_app_read(wine_root, "../paint", (unsigned)getuid(),
                                    executable, sizeof executable,
                                    prefix, sizeof prefix, error,
                                    sizeof error) != 0);
    assert(unlink(executable_path) == 0);
    assert(finncompat_wine_app_read(wine_root, "paint", (unsigned)getuid(),
                                    executable, sizeof executable,
                                    prefix, sizeof prefix, error,
                                    sizeof error) != 0);
    f = fopen(executable_path, "w");
    assert(f != NULL);
    assert(fputs("portable app", f) >= 0);
    assert(fclose(f) == 0);

    f = fopen(metadata, "w");
    assert(f != NULL);
    assert(fwrite(executable_path, 1, strlen(executable_path), f) == strlen(executable_path));
    assert(fputc('\0', f) != EOF);
    assert(fputs("ignored\n", f) >= 0);
    assert(fclose(f) == 0);
    assert(chmod(metadata, 0600) == 0);
    assert(finncompat_wine_app_read(wine_root, "paint", (unsigned)getuid(),
                                    executable, sizeof executable,
                                    prefix, sizeof prefix, error,
                                    sizeof error) != 0);
    f = fopen(metadata, "w");
    assert(f != NULL);
    assert(fputs("relative.exe\n", f) >= 0);
    assert(fclose(f) == 0);
    assert(chmod(metadata, 0600) == 0);
    assert(finncompat_wine_app_read(wine_root, "paint", (unsigned)getuid(),
                                    executable, sizeof executable,
                                    prefix, sizeof prefix, error,
                                    sizeof error) != 0);
    ids = NULL;
    ids_len = 0;
    assert(finncompat_wine_list(wine_root, (unsigned)getuid(), &ids, &ids_len,
                                error, sizeof error) == 0);
    assert(ids_len == 0); /* Invalid registration metadata is not exposed. */
    free(ids);
}

static void test_wine_run(const char *systemd_run, const char *log_path) {
    char error[256] = "", exe[1024], injected[512];
    int n = snprintf(injected, sizeof injected, "%s/injected.exe", g_dir);
    assert(n > 0 && (size_t)n < sizeof injected);
    n = snprintf(exe, sizeof exe, "/home/finn/Portable App;touch %s", injected);
    assert(n > 0 && (size_t)n < sizeof exe);
    const char *prefix = "/home/finn/.local/share/finn/wine/paint/prefix";
    assert(finncompat_wine_run(systemd_run, "/usr/bin/wine",
                               "finn-wine-paint-401-8", "/home/finn",
                               exe, prefix, 1000, 1000,
                               error, sizeof error) == 0);
    char log[4096];
    read_text(log_path, log, sizeof log);
    assert(strstr(log, "--unit=finn-wine-paint-401-8\n") != NULL);
    assert(strstr(log, "--uid=1000\n--gid=1000\n") != NULL);
    assert(strstr(log, "--setenv=HOME=/home/finn\n") != NULL);
    assert(strstr(log, "--setenv=WINEPREFIX=/home/finn/.local/share/finn/wine/paint/prefix\n") != NULL);
    assert(strstr(log, "--setenv=WAYLAND_DISPLAY=wayland-0\n") != NULL);
    assert(strstr(log, "--property=Restart=no\n--\n/usr/bin/wine\n") != NULL);
    assert(strstr(log, exe) != NULL);
    assert(access(injected, F_OK) != 0);

    assert(finncompat_wine_run(systemd_run, "/usr/bin/wine", "bad;unit",
                               "/home/finn", exe, prefix, 1000, 1000,
                               error, sizeof error) != 0);
    assert(finncompat_wine_run(systemd_run, "/usr/bin/wine",
                               "finn-wine-paint-401-8", "/home/finn",
                               "relative.exe", prefix, 1000, 1000,
                               error, sizeof error) != 0);
    assert(finncompat_wine_run(systemd_run, "/usr/bin/wine",
                               "finn-wine-paint-401-8", "/home/finn",
                               exe, "relative-prefix", 1000, 1000,
                               error, sizeof error) != 0);
}

static void test_job_status(const char *systemctl) {
    char state[192], error[256] = "";
    assert(setenv("FINN_TEST_SYSTEMD_STATE", "running", 1) == 0);
    assert(finncompat_flatpak_status(systemctl, "finn-flatpak-401-8",
                                     state, sizeof state, error,
                                     sizeof error) == 0);
    assert(!strcmp(state, "state=running"));

    assert(setenv("FINN_TEST_SYSTEMD_STATE", "queued", 1) == 0);
    assert(finncompat_flatpak_status(systemctl, "finn-flatpak-401-8",
                                     state, sizeof state, error,
                                     sizeof error) == 0);
    assert(!strcmp(state, "state=queued"));

    assert(setenv("FINN_TEST_SYSTEMD_STATE", "complete", 1) == 0);
    assert(finncompat_flatpak_status(systemctl, "finn-flatpak-401-8",
                                     state, sizeof state, error,
                                     sizeof error) == 0);
    assert(!strcmp(state, "state=complete"));

    assert(setenv("FINN_TEST_SYSTEMD_STATE", "failed", 1) == 0);
    assert(finncompat_flatpak_status(systemctl, "finn-flatpak-401-8",
                                     state, sizeof state, error,
                                     sizeof error) == 0);
    assert(strstr(state, "state=failed result=exit-code exit=1") != NULL);

    assert(setenv("FINN_TEST_SYSTEMD_STATE", "missing", 1) == 0);
    assert(finncompat_flatpak_status(systemctl, "finn-flatpak-401-8",
                                     state, sizeof state, error,
                                     sizeof error) != 0);
    assert(strstr(error, "not found") != NULL);
}

int main(void) {
    int temp_fd = mkstemp(g_dir);
    assert(temp_fd >= 0);
    close(temp_fd);
    assert(unlink(g_dir) == 0);
    assert(mkdir(g_dir, 0700) == 0);
    char pkg[512], systemd_run[512], systemctl[512];
    char query_log[512], run_log[512];
    snprintf(query_log, sizeof query_log, "%s/query.log", g_dir);
    snprintf(run_log, sizeof run_log, "%s/run.log", g_dir);

    write_script("finnpkg", "printf '%s\\n' \"$@\" > \"$FINN_TEST_QUERY_LOG\"\n"
                 "printf 'org.example.Paint\\tPaint\\n'\n", pkg, sizeof pkg);
    write_script("systemd-run", "printf '%s\\n' \"$@\" > \"$FINN_TEST_RUN_LOG\"\n",
                 systemd_run, sizeof systemd_run);
    write_script("systemctl", "case \"$FINN_TEST_SYSTEMD_STATE\" in\n"
                 "running) printf 'LoadState=loaded\\nActiveState=active\\nResult=success\\nExecMainStatus=0\\nExecMainStartTimestampMonotonic=100\\n' ;;\n"
                 "queued) printf 'LoadState=loaded\\nActiveState=inactive\\nResult=success\\nExecMainStatus=0\\nExecMainStartTimestampMonotonic=0\\n' ;;\n"
                 "complete) printf 'LoadState=loaded\\nActiveState=inactive\\nResult=success\\nExecMainStatus=0\\nExecMainStartTimestampMonotonic=100\\n' ;;\n"
                 "failed) printf 'LoadState=loaded\\nActiveState=failed\\nResult=exit-code\\nExecMainStatus=1\\nExecMainStartTimestampMonotonic=100\\n' ;;\n"
                 "missing) printf 'LoadState=not-found\\nActiveState=inactive\\nResult=success\\nExecMainStatus=0\\nExecMainStartTimestampMonotonic=0\\n' ;;\n"
                 "esac\n", systemctl, sizeof systemctl);

    assert(setenv("FINN_TEST_QUERY_LOG", query_log, 1) == 0);
    assert(setenv("FINN_TEST_RUN_LOG", run_log, 1) == 0);
    test_ids();
    test_query(pkg, query_log);
    test_job_start(systemd_run, run_log);
    test_app_start(systemd_run, run_log);
    test_flatpak_run(systemd_run, run_log);
    test_wine_app_read();
    test_wine_run(systemd_run, run_log);
    test_job_status(systemctl);

    unlink(pkg);
    unlink(systemd_run);
    unlink(systemctl);
    unlink(query_log);
    unlink(run_log);
    rmdir(g_dir);
    puts("finncompat: Flatpak and Wine validation, argv launch and systemd jobs passed");
    return 0;
}
