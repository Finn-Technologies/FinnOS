/*
 * A compile-only stand-in for Linux's <sys/sysinfo.h>, for the host build of
 * the Settings page test on macOS.
 *
 * finnsettingsapp.c reads total and free memory for its System Information
 * page. The test exercises the page metadata, not the reading, and macOS has
 * no sysinfo(2), so the call reports "unavailable" and the caller takes its
 * absent path.
 */
#ifndef FINN_TEST_COMPAT_SYS_SYSINFO_H
#define FINN_TEST_COMPAT_SYS_SYSINFO_H

struct sysinfo {
    unsigned long uptime;
    unsigned long totalram;
    unsigned long freeram;
    unsigned long mem_unit;
};

static inline int sysinfo(struct sysinfo *info) {
    (void)info;
    return -1;
}

#endif
