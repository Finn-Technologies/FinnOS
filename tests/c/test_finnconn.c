/*
 * test_finnconn — the framing module, against a socket pair.
 *
 * The defect it guards is docs/review.md P1.3: a request split across two
 * writes was delivered as two requests, the second half executed as a new
 * verb. Plain C with no framework, because it has to run wherever the
 * compiler does.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../src/core/finnconn.h"

static int g_failed = 0;

static void ok(const char *what, int cond) {
    if (cond) {
        printf("ok - %s\n", what);
    } else {
        printf("FAIL - %s\n", what);
        g_failed = 1;
    }
    fflush(stdout);
}

/* A socket pair with the reading end non-blocking: finnconn is defined on a
 * non-blocking fd, and EAGAIN is part of what is being tested. */
static int make_pair(int *rd, int *wr) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;

    int flags = fcntl(sv[0], F_GETFL, 0);
    if (flags < 0 || fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    *rd = sv[0];
    *wr = sv[1];
    return 0;
}

static int send_all(int fd, const char *s, size_t n) {
    while (n) {
        ssize_t w = write(fd, s, n);
        if (w > 0) {
            s += w;
            n -= (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(1000);
            continue;
        }
        return -1;
    }
    return 0;
}

/* A request split across two writes must not be dispatched by halves. */
static void test_split_line(void) {
    int rd, wr;
    if (make_pair(&rd, &wr) != 0) { ok("socketpair", 0); return; }

    finn_conn c;
    finn_conn_init(&c);
    char line[256];
    int eof = 0;

    send_all(wr, "state.set bat", 13);
    int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("a line split before its newline is not delivered", r == 0 && eof == 0);

    send_all(wr, "tery=1\n", 7);
    r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("...and is delivered whole when the newline arrives",
       r == 1 && strcmp(line, "state.set battery=1") == 0);

    close(rd);
    close(wr);
}

/* Two requests in one write are two requests, answered from one read. */
static void test_pipelined_lines(void) {
    int rd, wr;
    if (make_pair(&rd, &wr) != 0) { ok("socketpair", 0); return; }

    finn_conn c;
    finn_conn_init(&c);
    char line[256];
    int eof = 0;

    send_all(wr, "ping\nversion\n", 13);

    int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("the first of two pipelined lines is delivered",
       r == 1 && strcmp(line, "ping") == 0);

    /* no write here: the second line is already in the buffer */
    r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("the second is delivered without reading again",
       r == 1 && strcmp(line, "version") == 0);

    r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("and then there is nothing pending", r == 0 && eof == 0);

    close(rd);
    close(wr);
}

/* An overlong line is refused, dropped, and does not poison the stream. */
static void test_overlong_line(void) {
    int rd, wr;
    if (make_pair(&rd, &wr) != 0) { ok("socketpair", 0); return; }

    finn_conn c;
    finn_conn_init(&c);
    char line[256];
    int eof = 0;

    static char big[FINN_CONN_CAP + 1024];
    memset(big, 'a', sizeof big);
    big[sizeof big - 1] = '\n';

    /* The line is larger than the kernel's socket buffer, so both ends have
     * to make progress: the writer is non-blocking and the reader swallows
     * what it can between writes, exactly as a real peer would. */
    int flags = fcntl(wr, F_GETFL, 0);
    if (flags < 0 || fcntl(wr, F_SETFL, flags | O_NONBLOCK) != 0) {
        ok("non-blocking writer", 0);
        close(rd);
        close(wr);
        return;
    }

    int refused = 0;
    size_t off = 0;
    while (off < sizeof big) {
        ssize_t w = write(wr, big + off, sizeof big - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;

        int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
        if (r == -1 && errno == EMSGSIZE) { refused = 1; break; }
        if (r != 0) break;      /* a line before the newline is a failure */
    }

    if (!refused) {
        /* everything is queued now, so one read reaches the newline */
        int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
        if (r == -1 && errno == EMSGSIZE) refused = 1;
    }
    ok("an overlong line is refused with EMSGSIZE", refused && eof == 0);

    send_all(wr, "ping\n", 5);
    int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("the connection continues with the line after it",
       r == 1 && strcmp(line, "ping") == 0);

    close(rd);
    close(wr);
}

/* EOF with nothing pending is how a peer says goodbye. */
static void test_clean_eof(void) {
    int rd, wr;
    if (make_pair(&rd, &wr) != 0) { ok("socketpair", 0); return; }

    finn_conn c;
    finn_conn_init(&c);
    char line[256];
    int eof = 0;

    close(wr);
    int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("a clean EOF is the end and not an error", r == 0 && eof == 1);

    close(rd);
}

/* Half a request is not a request. */
static void test_truncated_eof(void) {
    int rd, wr;
    if (make_pair(&rd, &wr) != 0) { ok("socketpair", 0); return; }

    finn_conn c;
    finn_conn_init(&c);
    char line[256];
    int eof = 0;

    send_all(wr, "state.g", 7);
    close(wr);

    int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("EOF in the middle of a line is an error", r == -1 && eof == 1);

    close(rd);
}

/* The protocol is text, so a CRLF peer sent the same request. */
static void test_crlf(void) {
    int rd, wr;
    if (make_pair(&rd, &wr) != 0) { ok("socketpair", 0); return; }

    finn_conn c;
    finn_conn_init(&c);
    char line[256];
    int eof = 0;

    send_all(wr, "ping\r\n", 6);
    int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("CRLF is delivered without the CR",
       r == 1 && strcmp(line, "ping") == 0 && strlen(line) == 4);

    close(rd);
    close(wr);
}

/* An empty line is a line: the protocol can carry one. */
static void test_empty_line(void) {
    int rd, wr;
    if (make_pair(&rd, &wr) != 0) { ok("socketpair", 0); return; }

    finn_conn c;
    finn_conn_init(&c);
    char line[256];
    int eof = 0;

    send_all(wr, "\n", 1);
    int r = finn_conn_read(&c, rd, line, (int)sizeof line, &eof);
    ok("an empty line is delivered as one", r == 1 && line[0] == 0);

    close(rd);
    close(wr);
}

int main(void) {
    test_split_line();
    test_pipelined_lines();
    test_overlong_line();
    test_clean_eof();
    test_truncated_eof();
    test_crlf();
    test_empty_line();

    if (g_failed) {
        printf("test_finnconn: FAILED\n");
        return 1;
    }
    printf("test_finnconn: ok\n");
    return 0;
}
