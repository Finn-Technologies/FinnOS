/* finntaskbar — the apps the user has pinned to the taskbar. */
#include "finntaskbar.h"
#include "finnsettings.h"

#include <stdio.h>
#include <string.h>

#define PIN_KEY "taskbar.pins"

/* The store's value limit. A list longer than this is refused rather than
 * silently truncated: a pin that disappears on the next boot is worse than a
 * pin that was never accepted. */
#define PIN_VALUE_MAX 128

static const char *const g_defaults[] = { "settings", "files", "store" };
#define DEFAULT_COUNT ((int)(sizeof(g_defaults) / sizeof(g_defaults[0])))

static int id_ok(const char *id) {
    if (!id) return 0;
    size_t n = strlen(id);
    if (n < 1 || n >= FINNTASKBAR_ID_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.')
            continue;
        return 0;
    }
    return 1;
}

int finntaskbar_defaults(char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int max) {
    if (!ids || max <= 0) return 0;
    int n = DEFAULT_COUNT < max ? DEFAULT_COUNT : max;
    for (int i = 0; i < n; i++)
        snprintf(ids[i], FINNTASKBAR_ID_MAX, "%s", g_defaults[i]);
    return n;
}

/* Parse a stored list. A token that is not a legal id is skipped rather than
 * allowed to shadow the rest of the list: the file is user-editable, and one
 * bad character should not hide every pin after it. */
static int parse(const char *value,
                 char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int max) {
    if (!value || !*value) return 0;
    int n = 0;
    const char *p = value;
    while (*p && n < max) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0 && len < FINNTASKBAR_ID_MAX) {
            char id[FINNTASKBAR_ID_MAX];
            memcpy(id, p, len);
            id[len] = 0;
            if (id_ok(id))
                snprintf(ids[n++], FINNTASKBAR_ID_MAX, "%s", id);
        }
        if (!comma) break;
        p = comma + 1;
    }
    return n;
}

int finntaskbar_pins_at(const char *path,
                        char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int max) {
    if (!path || !ids || max <= 0) return 0;

    char value[PIN_VALUE_MAX + 1];
    if (finn_settings_get_at(path, PIN_KEY, value, sizeof value) != 0)
        return finntaskbar_defaults(ids, max);

    /* A key with an empty value is a real answer: the user unpinned
     * everything. It is not the same as the key being absent. */
    return parse(value, ids, max);
}

int finntaskbar_pins(char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int max) {
    return finntaskbar_pins_at(FINN_SETTINGS_PATH, ids, max);
}

static int pinned_in(const char *id,
                     char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int n) {
    for (int i = 0; i < n; i++)
        if (!strcmp(ids[i], id)) return 1;
    return 0;
}

int finntaskbar_is_pinned_at(const char *path, const char *app_id) {
    if (!id_ok(app_id)) return 0;
    char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX];
    int n = finntaskbar_pins_at(path, ids, FINNTASKBAR_MAX);
    if (n < 0) return -1;
    return pinned_in(app_id, ids, n);
}

int finntaskbar_is_pinned(const char *app_id) {
    return finntaskbar_is_pinned_at(FINN_SETTINGS_PATH, app_id);
}

int finntaskbar_toggle_at(const char *path, const char *app_id, int *now_pinned) {
    if (!path || !id_ok(app_id)) return -1;

    char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX];
    int n = finntaskbar_pins_at(path, ids, FINNTASKBAR_MAX);
    if (n < 0) return -1;

    int at = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(ids[i], app_id)) { at = i; break; }

    if (at >= 0) {
        for (int i = at; i < n - 1; i++)
            memmove(ids[i], ids[i + 1], strlen(ids[i + 1]) + 1);
        n--;
        if (now_pinned) *now_pinned = 0;
    } else {
        if (n >= FINNTASKBAR_MAX) return -1;
        snprintf(ids[n], FINNTASKBAR_ID_MAX, "%s", app_id);
        n++;
        if (now_pinned) *now_pinned = 1;
    }

    char value[PIN_VALUE_MAX + 1];
    size_t used = 0;
    value[0] = 0;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(ids[i]);
        if (used + len + (i ? 1 : 0) > PIN_VALUE_MAX) return -1;
        if (i) value[used++] = ',';
        memcpy(value + used, ids[i], len);
        used += len;
        value[used] = 0;
    }

    if (finn_settings_set_at(path, PIN_KEY, value) != 0) return -1;
    return 0;
}

int finntaskbar_toggle(const char *app_id, int *now_pinned) {
    return finntaskbar_toggle_at(FINN_SETTINGS_PATH, app_id, now_pinned);
}
