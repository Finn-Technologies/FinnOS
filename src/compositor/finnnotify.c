#include "finnnotify.h"

#include <stdio.h>
#include <string.h>

static finn_note g_note[FINN_NOTIFY_MAX];
static int       g_n = 0;
static unsigned  g_rev = 1;

void finn_notify_post(const char *title, const char *body) {
    if (!title || !*title) return;

    /* Drop the oldest rather than refusing the newest: the queue exists to
     * show what just happened, so the newest entry is the one worth keeping. */
    if (g_n == FINN_NOTIFY_MAX) {
        memmove(&g_note[0], &g_note[1], sizeof g_note[0] * (FINN_NOTIFY_MAX - 1));
        g_n--;
    }

    finn_note *n = &g_note[g_n++];
    memset(n, 0, sizeof *n);
    snprintf(n->title, sizeof n->title, "%s", title);
    snprintf(n->body, sizeof n->body, "%s", body ? body : "");
    n->at = time(NULL);

    g_rev++;
    fprintf(stderr, "finnnotify: %s — %s\n", n->title, n->body);
}

int finn_notify_count(void) { return g_n; }

const finn_note *finn_notify_at(int i) {
    /* newest first, without moving anything */
    if (i < 0 || i >= g_n) return NULL;
    return &g_note[g_n - 1 - i];
}

void finn_notify_clear(void) {
    if (g_n == 0) return;
    g_n = 0;
    g_rev++;
}

unsigned finn_notify_rev(void) { return g_rev; }
