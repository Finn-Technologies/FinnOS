#ifndef FINNCOMPAT_H
#define FINNCOMPAT_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "finnproto.h"

/*
 * What is really installed for Windows programs.
 *
 * A backend is reported only when its executable is present and runnable:
 * the status verb exists so the Store can disable an install instead of
 * discovering the truth after a download. `wrapper` is the one case where
 * two programs make a backend (box64 running an x86_64 Wine on ARM); it is
 * empty when the backend is a single executable.
 */
typedef struct {
    char arch[16];
    int available;
    char backend[16];
    char binary[PATH_MAX];
    char wrapper[PATH_MAX];
    char version[FINN_WINE_VERSION_MAX];
    char prefix[PATH_MAX];
    char reason[64];
} finn_wine_runtime;

int finncompat_flatpak_id_valid(const char *id);
int finncompat_flatpak_remote_valid(const char *remote);
int finncompat_flatpak_job_id_make(char *dst, size_t cap,
                                   unsigned long pid, unsigned long sequence);
int finncompat_flatpak_job_id_valid(const char *id);

/* Start one absolute-path application as the FinnOS user via systemd-run. */
int finncompat_start_app(const char *systemd_run, const char *unit,
                         const char *command, unsigned uid, unsigned gid,
                         char *error, size_t error_cap);

/* Wine app ids intentionally share finnpkg's lowercase word shape. */
int finncompat_wine_id_valid(const char *id);
/*
 * Detect a real Wine runtime under `bin_dir` (normally /usr/bin) and a real
 * Proton compatibility tool below `home`. `out` is filled even when nothing
 * is installed: available=0, backend=none, reason=no-runtime. This never
 * claims a backend it could not execute.
 */
int finncompat_wine_status(const char *bin_dir, const char *home,
                           finn_wine_runtime *out,
                           char *error, size_t error_cap);
/* Read a registered app and its isolated prefix from Finn's private data tree. */
int finncompat_wine_app_read(const char *wine_root, const char *id, unsigned uid,
                             char *executable, size_t executable_cap,
                             char *prefix, size_t prefix_cap,
                             char *error, size_t error_cap);
/* Return newline-separated IDs of valid, Finn-owned private registrations. */
int finncompat_wine_list(const char *wine_root, unsigned uid,
                         uint8_t **out, size_t *out_len,
                         char *error, size_t error_cap);
/* Launch a registered Windows executable as Finn with its own WINEPREFIX. */
int finncompat_wine_run(const char *systemd_run, const char *wine,
                        const char *unit, const char *home,
                        const char *executable, const char *prefix,
                        unsigned uid, unsigned gid,
                        char *error, size_t error_cap);
/* The same launch with an optional wrapper in front of Wine (box64 on ARM). */
int finncompat_wine_run_wrapped(const char *systemd_run, const char *wine,
                                const char *wrapper, const char *unit,
                                const char *home, const char *executable,
                                const char *prefix, unsigned uid, unsigned gid,
                                char *error, size_t error_cap);
/* Launch one validated system-installed Flatpak app in the FinnOS Wayland
 * session. The app id is passed as one argv element; no shell is involved. */
int finncompat_flatpak_run(const char *systemd_run, const char *flatpak,
                           const char *unit, const char *app_id,
                           unsigned uid, unsigned gid,
                           char *error, size_t error_cap);

/*
 * Run a read-only finnpkg Flatpak query and capture its complete text output.
 * The returned buffer is owned by the caller. A bounded timeout and output
 * limit keep a remote catalogue from holding the single control connection
 * indefinitely or exhausting the daemon's memory.
 */
int finncompat_flatpak_query(const char *finnpkg, const char *action,
                             const char *query, uint8_t **out, size_t *out_len,
                             char *error, size_t error_cap);

/* Start a long package operation as a tracked systemd service. */
int finncompat_flatpak_start(const char *systemd_run, const char *finnpkg,
                             const char *job_id, const char *action,
                             const char *app_id, const char *remote,
                             char *error, size_t error_cap);

/* Return state=queued|running|complete|failed from systemd's unit state. */
int finncompat_flatpak_status(const char *systemctl, const char *job_id,
                              char *state, size_t state_cap,
                              char *error, size_t error_cap);

/* winget package ids are the catalogue's own shape, not a path or a word. */
int finncompat_winget_id_valid(const char *id);
int finncompat_winget_job_id_make(char *dst, size_t cap,
                                  unsigned long pid, unsigned long sequence);
int finncompat_winget_job_id_valid(const char *id);

/* The runtime OTA uses the same job naming rule as Flatpak and winget. */
int finncompat_ota_job_id_make(char *dst, size_t cap,
                               unsigned long pid, unsigned long sequence);
int finncompat_ota_job_id_valid(const char *id);

/*
 * Run one read-only finnpkg winget query and capture its text. The id (for
 * info) and the search term are validated here as well as in finnpkg: they
 * reach a remote path and a cache file, and the control plane is the door.
 */
int finncompat_winget_query(const char *finnpkg, const char *action,
                            const char *argument,
                            uint8_t **out, size_t *out_len,
                            char *error, size_t error_cap);

/* Start `finnpkg winget install <id>` as Finn in a tracked unit. */
int finncompat_winget_start(const char *systemd_run, const char *finnpkg,
                            const char *job_id, const char *id,
                            unsigned uid, unsigned gid, const char *home,
                            char *error, size_t error_cap);

/*
 * `status` is a local read of the cached check and must stay fast;
 * `check` is the one bounded network call the control plane makes for OTA.
 */
int finncompat_ota_query(const char *finnpkg, const char *action,
                         uint8_t **out, size_t *out_len,
                         char *error, size_t error_cap);

/* Start `finnpkg update download|install|rollback` as a root systemd unit. */
int finncompat_ota_start(const char *systemd_run, const char *finnpkg,
                         const char *job_id, const char *action,
                         char *error, size_t error_cap);

/* One word from systemd, for winget.job and ota.job. */
int finncompat_job_word(const char *systemctl, const char *job_id,
                        char *word, size_t word_cap,
                        char *error, size_t error_cap);

#endif
