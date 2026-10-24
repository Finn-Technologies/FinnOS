/*
 * finnappui — shared furniture for the in-compositor application views.
 */
#define _GNU_SOURCE
#include "finnappui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/fsuid.h>

void finnapp_ellipsis(const finn_font *f, char *dst, size_t cap,
                      const char *src, float maxw) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;

    if (finn_text_width(f, src) <= maxw) {
        snprintf(dst, cap, "%s", src);
        return;
    }

    /* The ellipsis itself has to fit, or a narrow column would cut it too. */
    const char *ell = "\xE2\x80\xA6";
    float ew = finn_text_width(f, ell);
    if (ew > maxw) {
        dst[0] = '\0';
        return;
    }

    char candidate[512];
    size_t i = 0;
    while (src[i] && i + 1 < sizeof candidate) {
        unsigned char c = (unsigned char)src[i];
        size_t clen = 1;
        if ((c & 0xE0) == 0xC0) clen = 2;
        else if ((c & 0xF0) == 0xE0) clen = 3;
        else if ((c & 0xF8) == 0xF0) clen = 4;

        if (i + clen >= sizeof candidate) break;
        for (size_t k = 0; k < clen; k++)
            if (!src[i + k]) { clen = k; break; }
        if (clen == 0) break;

        memcpy(candidate + i, src + i, clen);
        candidate[i + clen] = '\0';

        char with_ell[520];
        snprintf(with_ell, sizeof with_ell, "%s%s", candidate, ell);
        if (finn_text_width(f, with_ell) > maxw) {
            candidate[i] = '\0';
            break;
        }
        i += clen;
    }
    candidate[i] = '\0';
    snprintf(dst, cap, "%s%s", candidate, ell);
}

void finnapp_format_size(unsigned long long bytes, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (bytes < 1000ULL) {
        snprintf(out, cap, "%llu B", bytes);
    } else if (bytes < 1000ULL * 1000ULL) {
        snprintf(out, cap, "%.1f kB", (double)bytes / 1000.0);
    } else if (bytes < 1000ULL * 1000ULL * 1000ULL) {
        snprintf(out, cap, "%.1f MB", (double)bytes / 1000000.0);
    } else {
        snprintf(out, cap, "%.2f GB", (double)bytes / 1000000000.0);
    }
}

void finnapp_format_time(long long mtime, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (mtime <= 0) { snprintf(out, cap, "unknown"); return; }

    time_t t = (time_t)mtime;
    struct tm tm;
    if (!localtime_r(&t, &tm)) { snprintf(out, cap, "unknown"); return; }
    if (strftime(out, cap, "%e %b %Y %H:%M", &tm) == 0)
        snprintf(out, cap, "unknown");
}

uid_t finnapp_desktop_uid(void) {
    struct passwd *pw = getpwuid(1000);
    if (!pw) pw = getpwnam("finn");
    return pw ? pw->pw_uid : (uid_t)-1;
}

gid_t finnapp_desktop_gid(void) {
    struct passwd *pw = getpwuid(1000);
    if (!pw) pw = getpwnam("finn");
    return pw ? pw->pw_gid : (gid_t)-1;
}

const char *finnapp_home(char *buf, size_t cap) {
    if (!buf || cap == 0) return "/";
    struct passwd *pw = getpwuid(1000);
    if (!pw) pw = getpwnam("finn");
    if (pw && pw->pw_dir && pw->pw_dir[0]) {
        snprintf(buf, cap, "%s", pw->pw_dir);
        return buf;
    }
    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(buf, cap, "%s", home);
        return buf;
    }
    snprintf(buf, cap, "/");
    return buf;
}

static int g_fs_depth = 0;
static uid_t g_saved_uid = 0;
static gid_t g_saved_gid = 0;

void finnapp_fs_enter(void) {
    if (g_fs_depth++ > 0) return;

    /* setfsuid returns the previous value; 0 is the only safe thing to put
     * back, because the compositor is a root service by contract. */
    g_saved_uid = (uid_t)setfsuid(0);
    g_saved_gid = (gid_t)setfsgid(0);

    uid_t uid = finnapp_desktop_uid();
    gid_t gid = finnapp_desktop_gid();
    if (uid != (uid_t)-1) setfsuid(uid);
    if (gid != (gid_t)-1) setfsgid(gid);
}

void finnapp_fs_leave(void) {
    if (g_fs_depth == 0) return;
    if (--g_fs_depth > 0) return;
    setfsuid(g_saved_uid);
    setfsgid(g_saved_gid);
}
