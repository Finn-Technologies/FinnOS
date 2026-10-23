#ifndef FINNSETTINGS_H
#define FINNSETTINGS_H

#include <stddef.h>

/*
 * finnsettings — the one versioned store for the settings FinnOS remembers.
 *
 * The file is line-oriented `key=value`, and the first line is always
 * `version=<n>`. That first line is the migration field: a reader that does
 * not know the version reports "no settings" instead of guessing what an
 * older or a newer file meant, and the next write stamps the version this
 * build writes. Every write goes to a temporary file in the same directory
 * and is renamed over the target, so a reader never sees half a file and a
 * crash leaves the previous settings in place.
 *
 * The store never invents a value. A key that is absent is absent, and the
 * caller decides what the default is, because only the caller knows whether
 * "no remembered volume" means "the mixer is the truth" or "draw n/a".
 *
 * Keys are [a-z0-9_.-] and 1..32 bytes; `version` is reserved for the stamp.
 * Values are at most 128 bytes, with no newline, no NUL and no leading or
 * trailing space. Anything else is refused rather than written and found
 * broken later. The store holds 64 keys, which is more than the shell has
 * settings, and a new key past that is refused rather than dropped.
 *
 * Every entry point returns 0 when the key was found or written, and -1 when
 * it is absent or the operation failed. A value that does not fit the
 * caller's buffer counts as absent: it is never truncated.
 */

#define FINN_SETTINGS_PATH    "/var/lib/finn/settings"
#define FINN_SETTINGS_VERSION 1

/* The default-path forms, for the running system. */
int finn_settings_get(const char *key, char *out, size_t cap);
int finn_settings_get_int(const char *key, int *out);
int finn_settings_set(const char *key, const char *value);
int finn_settings_set_int(const char *key, int value);

/* The explicit-path forms, so the store can be tested without /var/lib. */
int finn_settings_get_at(const char *path, const char *key, char *out, size_t cap);
int finn_settings_set_at(const char *path, const char *key, const char *value);

#endif
