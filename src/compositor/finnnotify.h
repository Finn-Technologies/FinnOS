#ifndef FINNNOTIFY_H
#define FINNNOTIFY_H

#include <time.h>

/*
 * finnnotify — the shell's notification queue.
 *
 * Newest first, in memory, and only as deep as the page can show. A
 * notification here is a thing that happened during this session, not a log:
 * anything that wants history writes it somewhere that survives a reboot.
 * That is also why the queue starts empty on every start — the page can then
 * say "nothing has happened" and mean it.
 *
 * Posting is open to any part of the system: the compositor posts hardware
 * transitions it observes, and the control plane can post on behalf of a
 * service (notify.post). The queue does not care who wrote it.
 */

/* Queue depth matches the 4 slots drawn by finnclock.c (NOTE_SLOTS) */
#define FINN_NOTIFY_MAX 4

typedef struct {
    char   title[64];
    char   body[160];
    time_t at;              /* when it was posted, for the page's clock time */
} finn_note;

void finn_notify_post(const char *title, const char *body);

int  finn_notify_count(void);
const finn_note *finn_notify_at(int i);     /* 0 is the newest */

void finn_notify_clear(void);

/* Moves whenever the list changes, so a surface can key its raster on it. */
unsigned finn_notify_rev(void);

#endif
