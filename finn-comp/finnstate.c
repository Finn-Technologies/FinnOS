/* finnstate — the system state the FinnOS shell reflects. */
#include "finnstate.h"
#include "finnaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#define STATE_DIR   "/run/finn/state"
#define PERSIST_DIR "/var/lib/finn"

/* ---------------- small helpers ---------------- */

static int read_int_file(const char *path, int *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v;
    int ok = (fscanf(f, "%d", &v) == 1);
    fclose(f);
    if (ok) *out = v;
    return ok ? 0 : -1;
}

static int write_int_file(const char *path, int v) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", v);
    fclose(f);
    return 0;
}

static void read_first_line(const char *path, char *out, size_t cap) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)cap, f)) {
        char *nl = strchr(out, '\n');
        if (nl) *nl = 0;
    }
    fclose(f);
}

static int path_is_dir(const char *path) {
    struct stat sb;
    return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

/* First entry of /sys/class/<cls> that has a <probe> child (probe NULL means
 * "any entry"), written out as a full path. */
static int sysfs_first(const char *cls, const char *probe, char *out, size_t cap) {
    char dir[256];
    snprintf(dir, sizeof dir, "/sys/class/%s", cls);
    DIR *d = opendir(dir);
    if (!d) return -1;

    struct dirent *e;
    int found = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (probe) {
            char p[600];
            snprintf(p, sizeof p, "%s/%s/%s", dir, e->d_name, probe);
            if (!path_is_dir(p) && access(p, F_OK) != 0) continue;
        }
        if (out) snprintf(out, cap, "%s/%s", dir, e->d_name);
        found = 0;
        break;
    }
    closedir(d);
    return found;
}

/* rfkill is how a radio is switched off without the adapter disappearing. */
static int rfkill_find(const char *type, char *out, size_t cap) {
    static const char dir[] = "/sys/class/rfkill";
    DIR *d = opendir(dir);
    if (!d) return -1;

    struct dirent *e;
    int found = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char tp[600], t[64];
        snprintf(tp, sizeof tp, "%s/%s/type", dir, e->d_name);
        read_first_line(tp, t, sizeof t);
        if (strcmp(t, type)) continue;
        if (out) snprintf(out, cap, "%s/%s", dir, e->d_name);
        found = 0;
        break;
    }
    closedir(d);
    return found;
}

static int rfkill_soft(const char *type, int *out) {
    char d[600];
    if (rfkill_find(type, d, sizeof d) != 0) return -1;
    char p[640];
    snprintf(p, sizeof p, "%s/soft", d);
    return read_int_file(p, out);
}

static int rfkill_soft_set(const char *type, int on) {
    char d[600];
    if (rfkill_find(type, d, sizeof d) != 0) return -1;
    char p[640];
    snprintf(p, sizeof p, "%s/soft", d);
    return write_int_file(p, on ? 0 : 1);
}

/* ---------------- readers ---------------- */

static void read_battery(finn_state *st) {
    int v;
    char path[512];

    if (read_int_file(STATE_DIR "/battery", &v) == 0) {
        st->battery_pct = v;
        if (read_int_file(STATE_DIR "/charging", &v) == 0) st->charging = v;
        return;
    }

    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", e->d_name);
        char type[64];
        read_first_line(path, type, sizeof type);
        if (strncmp(type, "Battery", 7)) continue;

        snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", e->d_name);
        if (read_int_file(path, &v) == 0) st->battery_pct = v;

        snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", e->d_name);
        char s[64];
        read_first_line(path, s, sizeof s);
        if (!strncmp(s, "Charging", 8) || !strncmp(s, "Full", 4)) st->charging = 1;
        break;
    }
    closedir(d);
}

static void read_wifi(finn_state *st) {
    int v, junk;

    if (read_int_file(STATE_DIR "/wifi", &v) == 0) { st->wifi = v; return; }

    /* No wireless hardware at all: report absent, not "zero bars". */
    if (sysfs_first("net", "wireless", NULL, 0) != 0) return;

    st->wifi = 0;

    /* a soft-blocked radio is an adapter that is switched off */
    if (rfkill_soft("wlan", &v) != 0 && rfkill_soft("wifi", &v) != 0) v = 0;
    if (v) return;

    /* /proc/net/wireless:  iface: status link quality level noise ... */
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return;
    char line[512];
    if (fgets(line, sizeof line, f) && fgets(line, sizeof line, f)) {
        while (fgets(line, sizeof line, f)) {
            char *colon = strchr(line, ':');
            if (!colon) continue;
            int link = 0, level = 0;
            if (sscanf(colon + 1, " %d %d %d", &link, &level, &junk) >= 2) {
                int lvl = level ? level : link;
                /* level is dBm (negative); link is 0..70 on older drivers */
                if (level < 0) {
                    if (lvl >= -50) st->wifi = 3;
                    else if (lvl >= -62) st->wifi = 2;
                    else if (lvl >= -75) st->wifi = 1;
                } else {
                    st->wifi = (lvl >= 60) ? 3 : (lvl >= 40) ? 2 : (lvl >= 20) ? 1 : 0;
                }
            }
            break;
        }
    }
    fclose(f);
}

static void read_wired(finn_state *st) {
    int v;
    if (read_int_file(STATE_DIR "/wired", &v) == 0) { st->wired = v; return; }

    DIR *d = opendir("/sys/class/net");
    if (!d) return;

    struct dirent *e;
    int any = 0, up = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo")) continue;

        char p[512];
        snprintf(p, sizeof p, "/sys/class/net/%s/wireless", e->d_name);
        if (path_is_dir(p)) continue;      /* that one is the radio */

        any = 1;
        snprintf(p, sizeof p, "/sys/class/net/%s/operstate", e->d_name);
        char s[32];
        read_first_line(p, s, sizeof s);
        if (s[0] == 'u' && s[1] == 'p') up = 1;   /* up or unknown */
    }
    closedir(d);

    if (any) st->wired = up;
}

static void read_bluetooth(finn_state *st) {
    int v;
    if (read_int_file(STATE_DIR "/bluetooth", &v) == 0) { st->bluetooth = v; return; }

    if (sysfs_first("bluetooth", NULL, NULL, 0) != 0) return;   /* stays -1 */

    st->bluetooth = 1;
    if (rfkill_soft("bluetooth", &v) == 0 && v) st->bluetooth = 0;
}

static void read_brightness(finn_state *st) {
    int v, maxv;
    char d[512];

    if (read_int_file(STATE_DIR "/brightness", &v) == 0) { st->brightness = v; return; }
    if (sysfs_first("backlight", "brightness", d, sizeof d) != 0) return;

    char cp[600], mp[600];
    snprintf(cp, sizeof cp, "%s/brightness", d);
    snprintf(mp, sizeof mp, "%s/max_brightness", d);
    if (read_int_file(cp, &v) != 0 || read_int_file(mp, &maxv) != 0 || maxv <= 0) return;

    int pct = v * 100 / maxv;
    st->brightness = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

static void read_dark(finn_state *st) {
    int v;
    if (read_int_file(STATE_DIR "/dark", &v) == 0) { st->dark = v ? 1 : 0; return; }

    char s[32];
    read_first_line("/etc/finn/appearance", s, sizeof s);
    st->dark = (strncmp(s, "dark", 4) == 0);
}

static void read_volume(finn_state *st) {
    int v;
    if (read_int_file(STATE_DIR "/volume", &v) == 0) { st->volume = v; return; }

    v = finn_audio_get();                  /* the real mixer, when there is one */
    if (v >= 0) { st->volume = v; return; }

    if (read_int_file(PERSIST_DIR "/volume", &v) == 0) { st->volume = v; return; }
    st->volume = 50;
}

/* ---------------- public ---------------- */

void finn_state_read(finn_state *st) {
    st->battery_pct = -1;
    st->charging    = 0;
    st->wifi        = -1;
    st->volume      = -1;
    st->brightness  = -1;
    st->bluetooth   = -1;
    st->wired       = -1;
    st->dark        = 0;

    read_battery(st);
    read_wifi(st);
    read_wired(st);
    read_bluetooth(st);
    read_brightness(st);
    read_volume(st);
    read_dark(st);
}

int finn_state_differs(const finn_state *a, const finn_state *b) {
    return a->battery_pct != b->battery_pct || a->charging != b->charging ||
           a->wifi        != b->wifi        || a->volume   != b->volume   ||
           a->brightness  != b->brightness  || a->bluetooth!= b->bluetooth||
           a->wired       != b->wired       || a->dark     != b->dark;
}

static const char *const OVERRIDES[] = {
    "battery", "charging", "wifi", "volume",
    "brightness", "bluetooth", "wired", "dark", NULL
};

int finn_state_set(const char *name, int value) {
    int known = 0;
    for (int i = 0; OVERRIDES[i]; i++)
        if (!strcmp(name, OVERRIDES[i])) { known = 1; break; }
    if (!known) return -1;

    mkdir("/run/finn", 0755);
    mkdir(STATE_DIR, 0755);

    char path[512];
    snprintf(path, sizeof path, STATE_DIR "/%s", name);
    return write_int_file(path, value);
}

void finn_state_clear(void) {
    for (int i = 0; OVERRIDES[i]; i++) {
        char path[512];
        snprintf(path, sizeof path, STATE_DIR "/%s", OVERRIDES[i]);
        unlink(path);
    }
}

/* ---------------- actuators ---------------- */

/*
 * Volume is a level the OS owns, and it is pushed to the mixer whenever a
 * mixer exists. The persisted level is what the shell shows on a machine
 * whose audio hardware has no usable control (and, until the session audio
 * daemon lands, what keeps the level across a reboot).
 */
int finn_state_set_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    finn_audio_set(pct);

    mkdir("/run/finn", 0755);
    mkdir(STATE_DIR, 0755);
    mkdir(PERSIST_DIR, 0755);
    write_int_file(PERSIST_DIR "/volume", pct);
    return write_int_file(STATE_DIR "/volume", pct);
}

int finn_state_set_brightness(int pct) {
    char d[512];
    if (sysfs_first("backlight", "brightness", d, sizeof d) != 0) return -1;

    char cp[600], mp[600];
    snprintf(cp, sizeof cp, "%s/brightness", d);
    snprintf(mp, sizeof mp, "%s/max_brightness", d);

    int maxv;
    if (read_int_file(mp, &maxv) != 0 || maxv <= 0) return -1;

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int v = maxv * pct / 100;
    if (v < 1 && pct > 0) v = 1;          /* never go fully black */
    return write_int_file(cp, v);
}

int finn_state_set_wifi(int on) { return rfkill_soft_set("wlan", on); }
int finn_state_set_bluetooth(int on) { return rfkill_soft_set("bluetooth", on); }

int finn_state_set_dark(int on) {
    mkdir("/run/finn", 0755);
    mkdir(STATE_DIR, 0755);
    mkdir("/etc/finn", 0755);

    FILE *f = fopen("/etc/finn/appearance", "w");
    if (f) { fprintf(f, "%s\n", on ? "dark" : "light"); fclose(f); }

    char path[512];
    snprintf(path, sizeof path, STATE_DIR "/dark");
    return write_int_file(path, on ? 1 : 0);
}
