/* finnapps — the FinnOS application registry. */
#define _GNU_SOURCE
#include "finnapps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static finn_app g_apps[FINN_APP_MAX];
static int g_count = 0;

/* trim in place: the catalogue is written by hand, so it carries spaces the
 * machine should not see */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static void copy_field(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

/* Split a line into at most `n` pipe-separated fields, in place. */
static int split(char *line, char **out, int n) {
    int k = 0;
    char *p = line;
    while (k < n) {
        out[k++] = p;
        char *bar = strchr(p, '|');
        if (!bar) break;
        *bar = 0;
        p = bar + 1;
    }
    for (int i = 0; i < k; i++) out[i] = trim(out[i]);
    return k;
}

/*
 * The install list: one id per line. A list the package manager has never
 * written is not an error — a machine with nothing installed simply has no
 * apps marked, and the shell says "not installed" for all of them.
 */
static void load_installed(const char *path) {
    if (!path) return;
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *id = trim(line);
        if (!*id || *id == '#') continue;
        for (int i = 0; i < g_count; i++)
            if (!strcmp(g_apps[i].id, id)) g_apps[i].installed = 1;
    }
    fclose(f);
}

int finn_apps_load(const char *catalogue, const char *installed) {
    g_count = 0;

    const char *path = catalogue ? catalogue : FINN_APPS_CATALOGUE;
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512];
    while (fgets(line, sizeof line, f) && g_count < FINN_APP_MAX) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;

        char *fld[5];
        int n = split(p, fld, 5);
        if (n < 3) continue;                     /* id, name and icon at least */
        if (!*fld[0]) continue;

        finn_app *a = &g_apps[g_count++];
        memset(a, 0, sizeof *a);
        copy_field(a->id, sizeof a->id, fld[0]);
        copy_field(a->name, sizeof a->name, n > 1 ? fld[1] : fld[0]);
        copy_field(a->icon, sizeof a->icon, n > 2 ? fld[2] : "");
        copy_field(a->command, sizeof a->command, n > 3 ? fld[3] : "");
        copy_field(a->note, sizeof a->note, n > 4 ? fld[4] : "");
        a->installed = 0;
    }
    fclose(f);

    load_installed(installed ? installed : FINN_APPS_INSTALLED);
    return g_count;
}

void finn_apps_free(void) {
    memset(g_apps, 0, sizeof g_apps);
    g_count = 0;
}

int finn_apps_count(void) { return g_count; }

const finn_app *finn_apps_at(int i) {
    if (i < 0 || i >= g_count) return NULL;
    return &g_apps[i];
}

const finn_app *finn_apps_find(const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < g_count; i++)
        if (!strcmp(g_apps[i].id, id)) return &g_apps[i];
    return NULL;
}

int finn_apps_installed_count(void) {
    int n = 0;
    for (int i = 0; i < g_count; i++) if (g_apps[i].installed) n++;
    return n;
}
