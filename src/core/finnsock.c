/* finnsock — tiny unix-socket helpers shared by finnd, finn-comp and finnctl. */
#define _GNU_SOURCE
#include "finnsock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

#include "finnproto.h"

int finn_listen(const char *path, int mode) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    /* A stale socket from a previous run would block bind(). */
    unlink(path);

    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof a.sun_path) { close(fd); errno = ENAMETOOLONG; return -1; }
    strcpy(a.sun_path, path);

    mode_t old = umask(0);
    int r = bind(fd, (struct sockaddr *)&a, sizeof a);
    umask(old);
    if (r < 0) { close(fd); return -1; }

    chmod(path, mode);

    if (listen(fd, 16) < 0) { close(fd); return -1; }

    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    return fd;
}

int finn_accept(int listen_fd) {
    int fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
    if (fd < 0) return -1;
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    return fd;
}

int finn_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof a.sun_path) { close(fd); errno = ENAMETOOLONG; return -1; }
    strcpy(a.sun_path, path);

    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    return fd;
}

/*
 * One-shot request. The wait is bounded on both ends: a peer that accepts
 * the connection and then says nothing must not be able to hold the caller
 * still forever, and a caller that cannot be heard must not hang either.
 */
int finn_call(const char *path, const char *req, char *reply, int cap) {
    if (reply && cap > 0) reply[0] = 0;

    int fd = finn_connect(path);
    if (fd < 0) return -1;

    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (finn_write_str(fd, req) != 0 || finn_write_str(fd, "\n") != 0) {
        close(fd);
        return -1;
    }

    char line[FINN_MAX_LINE];
    int n = finn_read_line(fd, line, (int)sizeof line);
    close(fd);
    if (n <= 0) return -1;

    if (reply && cap > 0) snprintf(reply, (size_t)cap, "%s", line);
    return 0;
}

int finn_write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { usleep(200); continue; }
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

int finn_write_str(int fd, const char *s) {
    return finn_write_all(fd, s, strlen(s));
}

/* Line-oriented read with a short internal retry budget so a slow or partial
 * write from the peer does not spin the caller. */
int finn_read_line(int fd, char *buf, int cap) {
    int n = 0;
    for (;;) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 1) {
            if (c == '\n') { buf[n] = 0; return n; }
            if (n < cap - 1) buf[n++] = c;
            continue;
        }
        if (r == 0) { buf[n] = 0; return n ? n : 0; }   /* EOF */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (n) { buf[n] = 0; return n; }            /* partial line, deliver it */
            return -1;
        }
        return -1;
    }
}
