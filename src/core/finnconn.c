/* finnconn — line framing for a connection that carries many requests. */
#include "finnconn.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

void finn_conn_init(finn_conn *c) {
    c->len = 0;
    c->discarding = 0;
}

/* Drop everything up to and including the newline at `nl`, keeping the
 * bytes that follow it: they are the next request. */
static void take_line(finn_conn *c, int nl) {
    int rest = c->len - nl - 1;
    if (rest > 0) memmove(c->buf, c->buf + nl + 1, (size_t)rest);
    c->len = rest;
}

int finn_conn_read(finn_conn *c, int fd, char *out, int cap, int *eof) {
    if (eof) *eof = 0;
    if (!c || !out || cap < 1) { errno = EINVAL; return -1; }

    for (;;) {
        int nl = -1;
        for (int i = 0; i < c->len; i++) {
            if (c->buf[i] == '\n') { nl = i; break; }
        }

        if (c->discarding) {
            if (nl < 0) {
                c->len = 0;             /* no newline yet: throw the bytes away */
            } else {
                take_line(c, nl);
                c->discarding = 0;
                errno = EMSGSIZE;
                return -1;
            }
        } else if (nl >= 0) {
            int n = nl;
            /* A peer that ends lines with CRLF sent the same request. */
            if (n > 0 && c->buf[n - 1] == '\r') n--;
            if (n > cap - 1) {
                take_line(c, nl);
                errno = EMSGSIZE;
                return -1;
            }
            memcpy(out, c->buf, (size_t)n);
            out[n] = 0;
            take_line(c, nl);
            return 1;
        }

        /* A full buffer with no newline in it is a line longer than the
         * framing cap. Swallow it up to its newline so the connection can
         * carry on with the request after it. */
        if (c->len >= FINN_CONN_CAP) {
            c->len = 0;
            c->discarding = 1;
        }

        ssize_t r = read(fd, c->buf + c->len, (size_t)(FINN_CONN_CAP - c->len));
        if (r > 0) {
            c->len += (int)r;
            continue;
        }
        if (r == 0) {
            if (eof) *eof = 1;
            if (c->discarding) {
                c->discarding = 0;
                c->len = 0;
                errno = EMSGSIZE;
                return -1;
            }
            if (c->len > 0) {
                /* Truncated input: half a request is not a request, and
                 * saying it is whole is how a partial verb gets run. */
                c->len = 0;
                errno = EPROTO;
                return -1;
            }
            return 0;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
}
