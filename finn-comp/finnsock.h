#ifndef FINNSOCK_H
#define FINNSOCK_H

#include <stddef.h>

/*
 * finnsock — tiny unix-socket helpers shared by finnd, finn-comp and finnctl.
 *
 * All calls are blocking unless noted. Reads are line-oriented; binary
 * payloads are handled explicitly by the caller.
 */

/* Server side */
int finn_listen(const char *path, int mode);          /* non-blocking accept */
int finn_accept(int listen_fd);                        /* blocking */

/* Client side */
int finn_connect(const char *path);

/*
 * One-shot request: connect, write one line, read one reply line, close.
 *
 * This is the shape almost every control-plane call wants — a question and
 * an answer, with no session in between — so it lives here rather than in
 * each caller. The wait is bounded: a peer that never answers must not be
 * able to hold the caller still forever, and the reply is left empty when
 * the call times out.
 *
 * Returns 0 when a reply was read, -1 otherwise. The reply never carries
 * its newline.
 */
int finn_call(const char *path, const char *req, char *reply, int cap);

/* Line I/O. finn_read_line returns the length excluding the newline, 0 on
 * EOF, -1 on error. */
int finn_read_line(int fd, char *buf, int cap);
int finn_write_all(int fd, const void *buf, size_t n);
int finn_write_str(int fd, const char *s);

#endif
