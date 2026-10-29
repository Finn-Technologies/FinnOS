/* finnbrowser — the wire between Finn Browser's view and its page engine. */
#define _GNU_SOURCE
#include "finnbrowser.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One decimal field, no sign, no leading '+', no overflow. */
static const char *parse_uint(const char *p, long *out) {
    if (!p || *p < '0' || *p > '9') return NULL;
    long v = 0;
    while (*p >= '0' && *p <= '9') {
        if (v > (LONG_MAX - 9) / 10) return NULL;
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = v;
    return p;
}

static int frame_in_bounds(long tab, long w, long h) {
    if (tab < 1 || tab > 1000000) return 0;
    if (w < 1 || h < 1) return 0;
    if (w > FINN_BROWSER_FRAME_MAX_DIM || h > FINN_BROWSER_FRAME_MAX_DIM) return 0;
    return (unsigned long)w * (unsigned long)h <= FINN_BROWSER_FRAME_MAX_PIXELS;
}

int finn_browser_frame_parse(const char *line, int *tab, int *w, int *h) {
    long t, fw, fh;
    const char *p;
    if (!line || strncmp(line, "FRAME ", 6) != 0) return -1;
    p = parse_uint(line + 6, &t);
    if (!p || *p != ' ') return -1;
    p = parse_uint(p + 1, &fw);
    if (!p || *p != ' ') return -1;
    p = parse_uint(p + 1, &fh);
    if (!p || *p != '\0') return -1;
    if (!frame_in_bounds(t, fw, fh)) return -1;
    if (tab) *tab = (int)t;
    if (w) *w = (int)fw;
    if (h) *h = (int)fh;
    return 0;
}

size_t finn_browser_frame_bytes(int w, int h) {
    if (!frame_in_bounds(1, w, h)) return 0;
    return (size_t)w * (size_t)h * 4u;
}

int finn_browser_frame_header(char *buf, size_t cap, int tab, int w, int h) {
    if (!buf || !frame_in_bounds(tab, w, h)) return -1;
    int n = snprintf(buf, cap, "FRAME %d %d %d\n", tab, w, h);
    if (n <= 0 || (size_t)n >= cap) return -1;
    return n;
}

void finn_browser_text_clean(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
        /* Back off to the start of a UTF-8 sequence so the cut never leaves
         * half a character for the other side to render. */
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) n--;
    }
    memmove(dst, src, n);
    dst[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)dst[i];
        if (c < 0x20 || c == 0x7f) dst[i] = ' ';
    }
}

static int host_char(unsigned char c) {
    return isalnum(c) || c == '-' || c == '.' || c == '_';
}

static int all_digits(const char *s, size_t n) {
    if (n == 0) return 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static int is_ipv4(const char *h, size_t n) {
    int parts = 0;
    size_t i = 0;
    while (i < n) {
        size_t s = i;
        while (i < n && h[i] >= '0' && h[i] <= '9') i++;
        if (i == s || i - s > 3) return 0;
        long v = strtol(h + s, NULL, 10);
        if (v > 255) return 0;
        parts++;
        if (i < n) {
            if (h[i] != '.') return 0;
            i++;
            if (i == n) return 0;
        }
    }
    return parts == 4;
}

int finn_browser_address(const char *typed, char *out, size_t cap) {
    if (!typed || !out || cap < 16) return FINN_BROWSER_ADDR_INVALID;
    out[0] = '\0';

    while (*typed == ' ' || *typed == '\t') typed++;
    size_t n = strlen(typed);
    while (n > 0 && (typed[n - 1] == ' ' || typed[n - 1] == '\t')) n--;
    if (n == 0) return FINN_BROWSER_ADDR_INVALID;
    if (n >= FINN_BROWSER_URL_MAX - 16 || n + 16 >= cap)
        return FINN_BROWSER_ADDR_INVALID;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)typed[i];
        if (c < 0x20 || c == 0x7f) return FINN_BROWSER_ADDR_INVALID;
    }

    char text[FINN_BROWSER_URL_MAX];
    memcpy(text, typed, n);
    text[n] = '\0';

    /* An explicit scheme is taken at its word, but only the ones a page
     * view should load. "javascript:" and "data:" typed into an address
     * bar are an injection, not a destination. */
    size_t scheme_len = 0;
    while (scheme_len < n && (isalnum((unsigned char)text[scheme_len]) ||
                              text[scheme_len] == '+' || text[scheme_len] == '-' ||
                              text[scheme_len] == '.'))
        scheme_len++;
    if (scheme_len > 0 && scheme_len < n && text[scheme_len] == ':' &&
        isalpha((unsigned char)text[0])) {
        const char *rest = text + scheme_len + 1;
        char scheme[16];
        if (scheme_len < sizeof scheme) {
            for (size_t i = 0; i < scheme_len; i++)
                scheme[i] = (char)tolower((unsigned char)text[i]);
            scheme[scheme_len] = '\0';
        } else {
            scheme[0] = '\0';
        }
        int has_slashes = rest[0] == '/' && rest[1] == '/';
        /* "localhost:8080" is a host and a port, not a scheme. */
        size_t digits = 0;
        while (rest[digits] >= '0' && rest[digits] <= '9') digits++;
        int is_port = digits > 0 && (rest[digits] == '\0' || rest[digits] == '/' ||
                                     rest[digits] == '?' || rest[digits] == '#');
        if (!is_port) {
            if (strchr(text, ' ')) return FINN_BROWSER_ADDR_NOT_URL;
            if (!strcmp(scheme, "about")) {
                if (!*rest) return FINN_BROWSER_ADDR_INVALID;
                snprintf(out, cap, "about:%s", rest);
                return FINN_BROWSER_ADDR_URL;
            }
            if ((!strcmp(scheme, "http") || !strcmp(scheme, "https")) &&
                has_slashes && rest[2]) {
                snprintf(out, cap, "%s:%s", scheme, rest);
                return FINN_BROWSER_ADDR_URL;
            }
            if (!strcmp(scheme, "file") && has_slashes && rest[2] == '/') {
                snprintf(out, cap, "file:%s", rest);
                return FINN_BROWSER_ADDR_URL;
            }
            return FINN_BROWSER_ADDR_INVALID;
        }
    }

    /* No scheme: it is an address only if it looks like a host. Words with
     * spaces, or a single word with no dot, are a search — and there is no
     * search provider to send them to. */
    if (strchr(text, ' ') || strchr(text, '\t')) return FINN_BROWSER_ADDR_NOT_URL;

    size_t host_end = 0;
    while (host_end < n && text[host_end] != '/' && text[host_end] != '?' &&
           text[host_end] != '#')
        host_end++;
    size_t port_at = host_end;
    for (size_t i = 0; i < host_end; i++)
        if (text[i] == ':') { port_at = i; break; }
    size_t host_len = port_at;
    if (host_len == 0) return FINN_BROWSER_ADDR_NOT_URL;
    if (port_at < host_end &&
        !all_digits(text + port_at + 1, host_end - port_at - 1))
        return FINN_BROWSER_ADDR_NOT_URL;
    for (size_t i = 0; i < host_len; i++)
        if (!host_char((unsigned char)text[i])) return FINN_BROWSER_ADDR_NOT_URL;

    int local = (host_len == 9 && !strncasecmp(text, "localhost", 9)) ||
                is_ipv4(text, host_len);
    int dotted = memchr(text, '.', host_len) != NULL &&
                 text[0] != '.' && text[host_len - 1] != '.';
    if (!local && !dotted) return FINN_BROWSER_ADDR_NOT_URL;

    snprintf(out, cap, "%s://%s%s", local ? "http" : "https", text,
             host_end == n ? "/" : "");
    return FINN_BROWSER_ADDR_URL;
}
