/*
 * finntaskbar — the taskbar pin list.
 *
 * Exercised through the explicit-path forms, like the settings store it sits
 * on. What matters here is the difference between "never set" and "the user
 * pinned nothing", that pin order survives a toggle, and that an id which
 * cannot be an app id is refused before it reaches the store.
 */
#define _GNU_SOURCE
#include "finntaskbar.h"
#include "finnsettings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_failed = 0;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) g_failed = 1;
}

static int has(char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX], int n,
               const char *id) {
    for (int i = 0; i < n; i++)
        if (!strcmp(ids[i], id)) return 1;
    return 0;
}

int main(void) {
    char dir[] = "/tmp/finntaskbar.XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }

    char path[512];
    snprintf(path, sizeof path, "%s/settings", dir);

    char ids[FINNTASKBAR_MAX][FINNTASKBAR_ID_MAX];
    int n = finntaskbar_pins_at(path, ids, FINNTASKBAR_MAX);
    check(n == 3 && has(ids, n, "settings") && has(ids, n, "files") &&
          has(ids, n, "store"), "a store that was never written has the defaults");

    int pinned = -1;
    check(finntaskbar_toggle_at(path, "browser", &pinned) == 0 && pinned == 1,
          "pin a new app");
    n = finntaskbar_pins_at(path, ids, FINNTASKBAR_MAX);
    check(n == 4 && strcmp(ids[3], "browser") == 0,
          "a new pin goes at the end of the list");
    check(finntaskbar_is_pinned_at(path, "browser") == 1,
          "the pinned app reads back as pinned");

    check(finntaskbar_toggle_at(path, "files", &pinned) == 0 && pinned == 0,
          "unpin an app");
    n = finntaskbar_pins_at(path, ids, FINNTASKBAR_MAX);
    check(n == 3 && !has(ids, n, "files") &&
          strcmp(ids[0], "settings") == 0 && strcmp(ids[1], "store") == 0,
          "unpinning preserves the order of the rest");

    /* The store keeps the other keys; a pin write is not a settings reset. */
    check(finn_settings_set_at(path, "volume", "37") == 0, "another setting");
    check(finntaskbar_toggle_at(path, "files", NULL) == 0, "pin again");
    char value[32];
    check(finn_settings_get_at(path, "volume", value, sizeof value) == 0 &&
          strcmp(value, "37") == 0, "pinning leaves other settings alone");

    check(finntaskbar_toggle_at(path, "Bad Id", NULL) == -1,
          "an id that is not an app id is refused");
    check(finntaskbar_toggle_at(path, "", NULL) == -1, "an empty id is refused");

    /* Unpinning everything is a real answer, not a reset to the defaults. */
    n = finntaskbar_pins_at(path, ids, FINNTASKBAR_MAX);
    for (int i = 0; i < n; i++)
        check(finntaskbar_toggle_at(path, ids[i], NULL) == 0, "unpin one");
    n = finntaskbar_pins_at(path, ids, FINNTASKBAR_MAX);
    check(n == 0, "unpinning everything stores an empty list");
    check(finn_settings_get_at(path, "taskbar.pins", value, sizeof value) == 0 &&
          value[0] == 0, "the empty list is written, not an absent key");

    unlink(path);
    rmdir(dir);
    return g_failed ? 1 : 0;
}
