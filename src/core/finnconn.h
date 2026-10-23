#ifndef FINNCONN_H
#define FINNCONN_H

/*
 * finnconn — line framing for a connection that carries more than one
 * request.
 *
 * A stream carries bytes, not requests: what a read returns depends on how
 * the sender's write happened to be split, so a reader that treats one
 * read as one request can run a split request twice and can treat a pause
 * as the end of the connection (docs/review.md P1.3). This keeps what has
 * arrived and hands out complete lines only.
 *
 * The fd must be non-blocking. `finn_read_line` in finnsock.h stays the
 * one-shot form: a caller that asks once and closes, with a bounded wait,
 * has no session to buffer.
 */

#define FINN_CONN_CAP 8192

typedef struct {
    char buf[FINN_CONN_CAP];
    int len;
    int discarding;
} finn_conn;

void finn_conn_init(finn_conn *c);

/*
 * Read one line.
 *
 * Returns 1 when one complete line was written to `out` — NUL-terminated,
 * without its newline and without a trailing CR — 0 when more bytes are
 * needed, and -1 on a framing or IO error. `*eof` is set when the peer
 * closed: EOF with an empty buffer is a clean end (returns 0, *eof = 1),
 * EOF with a partial line is truncated input (returns -1, *eof = 1).
 *
 * A line may be empty: the protocol can carry one, so 1 with "" is an
 * answer and not an error.
 *
 * An overlong line is discarded up to and including its newline and then
 * reported as -1 with errno = EMSGSIZE; the line after it still frames,
 * because the discarded bytes were consumed whole. The longest line that
 * can be delivered is FINN_CONN_CAP - 1 bytes; a line longer than `cap - 1`
 * but shorter than that is consumed the same way, because it cannot be
 * handed back without lying about its length.
 *
 * EAGAIN means 0, never a line. Lines already buffered are answered
 * without reading again, so a peer that pipelined two requests costs one
 * read and two calls.
 */
int finn_conn_read(finn_conn *c, int fd, char *out, int cap, int *eof);

#endif
