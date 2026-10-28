/*
 * finnsettingsapp — the Settings application view inside finn-comp.
 *
 * The app is a window, so its body is already frosted glass over the desktop;
 * this file is the furniture on it. The arrangement is the one a person
 * already knows from macOS System Settings — a translucent sidebar with a
 * search field and grouped rows, and a content column of inset cards — but
 * the material, radii and type are FinnOS's, so it belongs to the shell it
 * lives in rather than looking like a port.
 *
 * Both columns are the same sheet. The sidebar is the body glass set back by
 * a wash of ink; a card is the body glass brought forward by a second frost.
 * Painting the sidebar white instead made the window read as two windows: a
 * flat white page on the left with glass leaking in from the right.
 *
 * Two rules decide most of what follows:
 *
 *   - Everything is a token. A row that hardcodes white in the light
 *     appearance goes white-on-white in the dark one, and that is exactly
 *     what this screen used to do.
 *   - Nothing is faked. A machine without a radio says so and its switch is
 *     inert; there is no refresh rate to report, so none is shown; the power
 *     page says the sleep policy does not exist yet.
 */
#define _GNU_SOURCE
#include "finnsettingsapp.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnvg.h"
#include "finnanim.h"
#include "finnmotion.h"
#include "finnaudio.h"
#include "finnsock.h"
#include "finnproto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <linux/input.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>

/*
 * This app works in the frame's own units: a 250 sidebar, a 30px search
 * field, 12px labels. ui_scale() maps one design unit onto the compositor's
 * pixels, so the frame fills the window it was drawn for and keeps that size
 * when the window is maximised.
 */

/* Round to scaled integer */
static inline int rs(float v, float s) { return (int)lroundf(v * s); }

static inline int font_px(float s, float base) {
    int px = rs(base, s);
    return px < 6 ? 6 : px;
}

/*
 * Sidebar metrics, reference units — the Figma frame's numbers: a 250 sheet,
 * the title at 15,44, a 220x30 search field at 15,70, and 30px rows on a 35px
 * pitch. The frame draws one 10-unit break, between the network rows and the
 * pair below them; it has no group captions, so none are drawn.
 */
#define SB_W        250.0f
#define SB_PAD       15.0f
#define SB_TITLE_Y   44.0f
#define SB_SEARCH_Y  70.0f
#define SB_SEARCH_H  30.0f
#define SB_FIRST_Y  115.0f
#define SB_ROW_H     30.0f
#define SB_ROW_STEP  35.0f
#define SB_GROUP_GAP 10.0f

/* Content metrics: the frame's 20/44 title, then its 70-first-row of cards. */
#define CARD_R        4.0f
#define CARD_PAD     16.0f
#define HEADER_TOP   44.0f
#define CARD_TOP     70.0f
#define CARD_GAP     10.0f
#define CONTENT_PAD_L 20.0f
#define CONTENT_PAD_R 10.0f

/* Card internals, shared by every page so no page invents its own rhythm. */
#define ROW_H        34.0f
#define HERO_H       62.0f
#define SW_W         42.0f
#define SW_H         25.0f
#define TILE_W      112.0f
#define TILE_H       66.0f

/* The System Information frame's composition: a 305x160 gradient hero with a
 * 10 gap to a 305-wide pair of 147.5x75 stat tiles, 9.5 apart and 10 apart
 * vertically. The hero's exact height is what makes the two rows line up. */
#define ABOUT_HERO_W  305.0f
#define ABOUT_HERO_H  160.0f
#define ABOUT_TILE_H   75.0f
#define ABOUT_GAP      10.0f
#define ABOUT_TILE_GAP  9.5f

/* The update page's three cards: four report rows, one action row and a
 * note with its scope line. They use the same rows and radii as every other
 * page, so the page reads as part of Settings rather than as a console
 * bolted onto it. */
#define OTA_STATUS_H 178.0f
#define OTA_ACTIONS_H 74.0f
#define OTA_NOTES_H   96.0f

/*
 * The app draws in the frame's own units: the window opens at 900x500 and
 * the compositor's canonical scale maps one design unit to its pixels. There
 * is no second ratio here — the old one multiplied the frame by 1.24 and
 * then laid 900 units into a 900-unit window, which overflowed every card.
 */
#define CONTENT_MAX    780.0f
static float ui_scale(float scale) {
    return scale > 0.0f ? scale : 1.0f;
}

/* ---------------- fonts & assets ---------------- */

static finn_font g_f_hero;      /* 16 reference: page title */
static finn_font g_f_title;     /* 16 reference: card row title */
static finn_font g_f_label;     /* 12 reference: body */
static finn_font g_f_sub;       /* 10 reference: detail */
static finn_font g_f_micro;     /* 10 reference: caption */
static float     g_font_scale = -1.0f;
static char      g_font_dir[512] = "";
static char      g_icon_dir[512] = "";

static fsvg g_ic_wifi;
static fsvg g_ic_bt;
static fsvg g_ic_sun;
static fsvg g_ic_vol;
static fsvg g_ic_bat;
static fsvg g_ic_about;
static fsvg g_ic_search;
static fsvg g_ic_power;
static fsvg g_ic_refresh;
static fsvg g_ic_chevron;
static fsvg g_ic_display;
static fsvg g_ic_globe;   /* Figma "internet": the Network row's globe */
static fsvg g_ic_brush;   /* Figma "paint-brush-04": the Personalization row */
static fsvg g_ic_info;    /* Figma "info": the System Information row */
static int  g_ready = 0;

/* ---------------- pages metadata ---------------- */

typedef struct {
    const char *id;
    const char *title;
    const char *section;
    fsvg       *icon;
    const char *keywords;
    int visible;
    /* The sidebar chip behind the glyph, as drawn in the Figma Settings
     * frame: blue for the network rows, green for personalization, grey
     * for system information. */
    uint32_t chip;
} settings_page_desc;

static const settings_page_desc g_pages[] = {
    { "wifi", "Wi-Fi", "NETWORK", &g_ic_wifi,
      "wifi wi-fi wireless internet wlan", 1, 0xFF006FFFu },
    { "bluetooth", "Bluetooth", "NETWORK", &g_ic_bt,
      "bluetooth bt wireless accessory accessories device", 1, 0xFF006FFFu },
    { "network", "Network", "NETWORK", &g_ic_globe,
      "network ethernet interface internet connection", 1, 0xFF006FFFu },
    { "personalization", "Personalization", "PERSONALIZATION", &g_ic_brush,
      "personalization theme appearance wallpaper dark light display", 1, 0xFF50FFABu },
    { "display", "Display", "DEVICE", &g_ic_display,
      "display screen monitor resolution scale brightness appearance dark light theme", 0, 0xFF006FFFu },
    { "sound", "Sound", "DEVICE", &g_ic_vol,
      "sound audio volume speaker output mixer mute", 0, 0xFF006FFFu },
    { "power", "Power", "DEVICE", &g_ic_bat,
      "power battery charge mains energy ac charging", 0, 0xFF006FFFu },
    { "about", "System Information", "SYSTEM", &g_ic_info,
      "about system information os kernel version uptime hardware host memory ram storage cpu gpu", 1, 0xFF858585u },
    { "update", "System Update", "SYSTEM", &g_ic_refresh,
      "update ota upgrade release version system", 1, 0xFF006FFFu }
};
#define PAGE_COUNT ((int)(sizeof(g_pages)/sizeof(g_pages[0])))
/* Appended after the legacy pages so their indices never move. */
#define PAGE_UPDATE 8

/* ---------------- interactive state ---------------- */

static int   g_active_page = 0;
static char  g_search_text[64] = "";
static int   g_search_focused = 0;
static float g_caret_timer = 0.0f;
static int   g_caret_visible = 1;

static int   g_dragging_slider = 0; /* 0 = none, 1 = volume, 2 = brightness */
static int   g_cached_screen_w = 1920;
static int   g_cached_screen_h = 1080;
static int   g_cached_wx = 0;
static int   g_cached_wy = 0;
static int   g_cached_ww = 900;
static int   g_cached_wh = 500;

/* Saved volume for mute toggle */
static int   g_prev_volume = 70;

/* ---------------- OTA (System Update) ---------------- */

/*
 * Settings is the only shell surface that has to wait on the network and on
 * finnd, and both can be slow or briefly absent. The compositor draws at
 * 60Hz, so none of that happens on its thread: one worker owns every control
 * socket call, and the drawing code only reads a copy taken under the mutex.
 *
 * There is no progress bar here on purpose. A package job is a systemd unit
 * that reports queued, running, complete or failed; the page repeats those
 * words rather than inventing a percentage from them. When finnd is down —
 * which is what an install looks like for a moment — the page says so and
 * keeps asking, so it recovers by itself instead of freezing on stale data.
 */

#define OTA_JOB_MAX     80
#define OTA_VER_MAX     64
#define OTA_CHECK_MAX   64
#define OTA_STATE_MAX   32
#define OTA_NOTE_MAX    512
#define OTA_ERR_MAX     256
#define OTA_ACT_MAX     48
#define OTA_VERB_MAX    16

enum {
    OTA_OP_NONE = 0,
    OTA_OP_STATUS,
    OTA_OP_CHECK,
    OTA_OP_DOWNLOAD,
    OTA_OP_INSTALL,
    OTA_OP_ROLLBACK,
    OTA_OP_JOB
};

enum {
    OTA_ERR_NONE = 0,
    OTA_ERR_NETWORK,
    OTA_ERR_CHECK,
    OTA_ERR_JOB,
    OTA_ERR_REQUEST
};

typedef struct {
    char current[OTA_VER_MAX];
    char latest[OTA_VER_MAX];
    char last_check[OTA_CHECK_MAX];
    char state[OTA_STATE_MAX];
    char notes[OTA_NOTE_MAX];
    char error[OTA_ERR_MAX];
    char activity[OTA_ACT_MAX];
    char job_id[OTA_JOB_MAX];
    char job_verb[OTA_VERB_MAX];
    char job_state[OTA_STATE_MAX];
    int  available;      /* -1 until a reply says otherwise */
    int  downloaded;
    int  rollback;
    int  reachable;      /* -1 not asked yet, 0 no answer, 1 answered */
    int  have_status;
    int  have_check;
    int  error_kind;
    int  worker_ok;
    int  busy;           /* the worker has a running or queued op */
    int  user_busy;      /* a check or package action is in flight */
} ota_state;

static ota_state g_ota = { .available = -1, .reachable = -1 };
static pthread_mutex_t g_ota_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_ota_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_ota_thread;
static int g_ota_stop = 0;
static int g_ota_thread_live = 0;
static int g_ota_pending = OTA_OP_NONE;
static char g_ota_pending_job[OTA_JOB_MAX] = "";
static char g_ota_pending_verb[OTA_VERB_MAX] = "";
static unsigned g_ota_rev = 1;
static unsigned g_ota_seen_rev = 1;
static int64_t g_ota_next_poll_ms = 0;
static int g_ota_build_page = -1;

static void ota_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

/* A reply is text, but a control character in it must not become a second
 * line on the glass. */
static void ota_copy_clean(char *dst, size_t cap, const char *src) {
    if (!dst || !cap) return;
    size_t n = 0;
    for (; src && *src && n + 1 < cap; src++) {
        unsigned char c = (unsigned char)*src;
        dst[n++] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
    }
    while (n && dst[n - 1] == ' ') n--;
    dst[n] = '\0';
}

/* Find `key=` as a whole token in a one-line reply. */
static const char *ota_kv(const char *line, const char *key) {
    if (!line || !key || !*key) return NULL;
    size_t kl = strlen(key);
    const char *p = line;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == line || isspace((unsigned char)p[-1])) && p[kl] == '=')
            return p + kl + 1;
        p += kl;
    }
    return NULL;
}

/* The next `key=` after a value, so a timestamp or an error sentence may
 * contain spaces without swallowing the fields that follow it. */
static const char *ota_next_kv(const char *p) {
    static const char *const keys[] = {
        "current", "latest", "available", "state", "last_check",
        "error", "notes", "downloaded", "rollback", "previous",
        "url", "sha256", "job", "job_state"
    };
    const char *best = NULL;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        size_t kl = strlen(keys[i]);
        const char *q = p;
        while ((q = strstr(q, keys[i])) != NULL) {
            if ((q == p || isspace((unsigned char)q[-1])) && q[kl] == '=') {
                if (!best || q < best) best = q;
                break;
            }
            q += kl;
        }
    }
    return best;
}

/* Returns 1 when the key exists, even when its value is empty. */
static int ota_kv_value(const char *line, const char *key, char *out, size_t cap) {
    if (!out || !cap) return 0;
    out[0] = '\0';
    const char *v = ota_kv(line, key);
    if (!v) return 0;
    const char *end = ota_next_kv(v);
    if (!end) end = v + strlen(v);
    size_t n = (size_t)(end - v);
    while (n && isspace((unsigned char)*v)) { v++; n--; }
    while (n && isspace((unsigned char)v[n - 1])) n--;
    if (n >= cap) n = cap - 1;
    memcpy(out, v, n);
    out[n] = '\0';
    return 1;
}

/* A job id is one token, even when the reply appends a state to it. */
static int ota_token_value(const char *line, const char *key, char *out, size_t cap) {
    if (!out || !cap) return 0;
    out[0] = '\0';
    const char *v = ota_kv(line, key);
    if (!v) return 0;
    while (*v && isspace((unsigned char)*v)) v++;
    size_t n = 0;
    while (v[n] && !isspace((unsigned char)v[n]) && n + 1 < cap) {
        out[n] = v[n];
        n++;
    }
    out[n] = '\0';
    return 1;
}

static int ota_flag_value(const char *text, int dflt) {
    if (!text || !*text) return dflt;
    /* The daemon's absent value is -1, not a plausible false. Keeping it
     * distinct is what stops a missing field from becoming "not downloaded"
     * and lighting an action that was never reported. */
    if (!strcmp(text, "-1")) return -1;
    if (!strcmp(text, "1") || !strcasecmp(text, "true") ||
        !strcasecmp(text, "yes") || !strcasecmp(text, "on"))
        return 1;
    if (!strcmp(text, "0") || !strcasecmp(text, "false") ||
        !strcasecmp(text, "no") || !strcasecmp(text, "off"))
        return 0;
    return dflt;
}

static void ota_clear_error_locked(void) {
    g_ota.error[0] = '\0';
    g_ota.error_kind = OTA_ERR_NONE;
}

static void ota_set_error_locked(const char *text, int kind) {
    ota_copy_clean(g_ota.error, sizeof g_ota.error,
                   text && *text ? text : "The update request failed.");
    ota_trim(g_ota.error);
    g_ota.error_kind = kind;
}

/* A job is active until the daemon says it is complete or failed. An empty
 * state after a start reply means the first ota.job call has not answered
 * yet, which is still a job in flight. */
static int ota_state_job_active(const ota_state *s) {
    if (!s || !s->job_id[0]) return 0;
    if (!s->job_state[0]) return 1;
    return !strcmp(s->job_state, "queued") || !strcmp(s->job_state, "running");
}

static int ota_state_can_check(const ota_state *s) {
    /* A worker that never started cannot act, so it must not draw a button
     * that would silently drop the click. */
    return s && s->worker_ok && !s->user_busy && !ota_state_job_active(s);
}

static int ota_state_can_download(const ota_state *s) {
    return ota_state_can_check(s) && s->available == 1 && s->downloaded != 1;
}

static int ota_state_install_needed(const ota_state *s) {
    return !s->current[0] || !s->latest[0] || strcmp(s->current, s->latest) != 0;
}

static int ota_state_can_install(const ota_state *s) {
    return ota_state_can_check(s) && s->downloaded == 1 &&
           ota_state_install_needed(s);
}

static int ota_state_can_rollback(const ota_state *s) {
    return ota_state_can_check(s) && s->rollback == 1;
}

static int ota_action_mask(const ota_state *s) {
    int m = 0;
    if (ota_state_can_check(s))    m |= 1;
    if (ota_state_can_download(s)) m |= 2;
    if (ota_state_can_install(s))  m |= 4;
    if (ota_state_can_rollback(s)) m |= 8;
    return m;
}

static int ota_op_is_user(int op) {
    return op == OTA_OP_CHECK || op == OTA_OP_DOWNLOAD ||
           op == OTA_OP_INSTALL || op == OTA_OP_ROLLBACK;
}

static int ota_op_allowed_locked(int op) {
    if (g_ota.user_busy) return 0;
    switch (op) {
    case OTA_OP_CHECK:    return ota_state_can_check(&g_ota);
    case OTA_OP_DOWNLOAD: return ota_state_can_download(&g_ota);
    case OTA_OP_INSTALL:  return ota_state_can_install(&g_ota);
    case OTA_OP_ROLLBACK: return ota_state_can_rollback(&g_ota);
    default:              return 0;
    }
}

/* The words after the verb, for a reply that carries a sentence rather than
 * fields. The prefix is protocol furniture and must not reach the glass. */
static const char *ota_reply_sentence(const char *line) {
    if (!line) return "";
    const char *p = line;
    if (!strcmp(p, "OK")) return "";
    if (!strncmp(p, "OK ", 3)) p += 3;
    const char *sp = strchr(p, ' ');
    if (sp) {
        int verb = 0;
        for (const char *q = p; q < sp; q++)
            if (*q == '.') verb = 1;
        if (verb) p = sp + 1;
    } else if (strchr(p, '.') && !strchr(p, '=')) {
        return "";
    }
    while (*p == ' ') p++;
    return p;
}

static void ota_apply_status_locked(const char *line) {
    char v[OTA_NOTE_MAX];
    int seen = 0;

    if (ota_kv_value(line, "current", v, sizeof v)) {
        ota_copy_clean(g_ota.current, sizeof g_ota.current, v);
        seen = 1;
    }
    if (ota_kv_value(line, "latest", v, sizeof v)) {
        ota_copy_clean(g_ota.latest, sizeof g_ota.latest, v);
        seen = 1;
    }
    if (ota_kv_value(line, "last_check", v, sizeof v)) {
        ota_copy_clean(g_ota.last_check, sizeof g_ota.last_check, v);
        seen = 1;
    }
    if (ota_kv_value(line, "state", v, sizeof v)) {
        ota_copy_clean(g_ota.state, sizeof g_ota.state, v);
        seen = 1;
    }
    if (ota_kv_value(line, "notes", v, sizeof v)) {
        ota_copy_clean(g_ota.notes, sizeof g_ota.notes, v);
        seen = 1;
    }
    if (ota_kv_value(line, "available", v, sizeof v)) {
        g_ota.available = ota_flag_value(v, g_ota.available);
        seen = 1;
    }
    if (ota_kv_value(line, "downloaded", v, sizeof v)) {
        g_ota.downloaded = ota_flag_value(v, g_ota.downloaded);
        seen = 1;
    }
    if (ota_kv_value(line, "rollback", v, sizeof v)) {
        g_ota.rollback = ota_flag_value(v, g_ota.rollback);
        seen = 1;
    }
    if (ota_kv_value(line, "previous", v, sizeof v)) {
        if (v[0]) g_ota.rollback = 1;
        seen = 1;
    }

    if (ota_kv_value(line, "error", v, sizeof v)) {
        seen = 1;
        if (v[0]) ota_set_error_locked(v, OTA_ERR_CHECK);
        else ota_clear_error_locked();
    } else if (g_ota.error_kind == OTA_ERR_NETWORK) {
        /* This reply proves the control plane is back. A failed check or
         * job is not stale just because a later status poll succeeded. */
        ota_clear_error_locked();
    }

    g_ota.reachable = 1;
    if (seen) {
        g_ota.have_status = 1;
    } else if (line && *line) {
        /* A daemon that answers with a sentence rather than fields is
         * telling us something; repeat it instead of turning silence into
         * "no release is available". */
        const char *sentence = ota_reply_sentence(line);
        if (*sentence) {
            ota_copy_clean(g_ota.notes, sizeof g_ota.notes, sentence);
            g_ota.have_status = 1;
        }
    }
    g_ota.activity[0] = '\0';
}

static void ota_apply_check_payload_locked(const char *payload) {
    char line[OTA_NOTE_MAX];
    char extra[OTA_NOTE_MAX] = "";
    int seen_available = 0;
    int seen_downloaded = 0;
    int seen_notes = 0;

    ota_clear_error_locked();
    const char *p = payload;
    while (p && *p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;
            ota_trim(key);
            ota_trim(val);
            /* A daemon that echoes the verb on the same line still reads as
             * key=value: the field name is the last word before the '='. */
            char *sp = strrchr(key, ' ');
            if (sp) key = sp + 1;

            if (!strcmp(key, "current"))
                ota_copy_clean(g_ota.current, sizeof g_ota.current, val);
            else if (!strcmp(key, "latest"))
                ota_copy_clean(g_ota.latest, sizeof g_ota.latest, val);
            else if (!strcmp(key, "last_check"))
                ota_copy_clean(g_ota.last_check, sizeof g_ota.last_check, val);
            else if (!strcmp(key, "state"))
                ota_copy_clean(g_ota.state, sizeof g_ota.state, val);
            else if (!strcmp(key, "notes")) {
                ota_copy_clean(g_ota.notes, sizeof g_ota.notes, val);
                seen_notes = 1;
            } else if (!strcmp(key, "available")) {
                g_ota.available = ota_flag_value(val, g_ota.available);
                seen_available = 1;
            } else if (!strcmp(key, "downloaded")) {
                g_ota.downloaded = ota_flag_value(val, g_ota.downloaded);
                seen_downloaded = 1;
            } else if (!strcmp(key, "rollback")) {
                g_ota.rollback = ota_flag_value(val, g_ota.rollback);
            } else if (!strcmp(key, "previous")) {
                if (*val) g_ota.rollback = 1;
            } else if (!strcmp(key, "error")) {
                if (*val) ota_set_error_locked(val, OTA_ERR_CHECK);
            }
        } else {
            /* Not every daemon field is key=value; a sentence is still the
             * daemon's answer and must not disappear. */
            ota_trim(line);
            if (*line) {
                size_t used = strlen(extra);
                if (used && used + 1 < sizeof extra) extra[used++] = ' ';
                snprintf(extra + used, sizeof extra - used, "%s", line);
            }
        }
        p = e ? e + 1 : p + strlen(p);
    }

    /* A fresh check is the authority on whether the package on disk still
     * belongs to the latest release. */
    if (seen_available && g_ota.available == 0) g_ota.downloaded = 0;
    else if (seen_available && g_ota.available == 1 && !seen_downloaded)
        g_ota.downloaded = 0;

    if (!seen_notes && extra[0])
        ota_copy_clean(g_ota.notes, sizeof g_ota.notes, extra);

    g_ota.reachable = 1;
    g_ota.have_check = 1;
    g_ota.activity[0] = '\0';
}

static const char *ota_job_state_word(const char *line) {
    static const char *const words[] = { "queued", "running", "complete", "failed" };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        size_t wl = strlen(words[i]);
        const char *p = line;
        while ((p = strstr(p, words[i])) != NULL) {
            if ((p == line || isspace((unsigned char)p[-1])) &&
                (!p[wl] || isspace((unsigned char)p[wl])))
                return words[i];
            p += wl;
        }
    }
    return NULL;
}

static int ota_job_id_ok(const char *id) {
    if (!id || !*id) return 0;
    size_t n = strlen(id);
    if (n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }
    return 1;
}

static int ota_parse_job_id(const char *line, char *out, size_t cap) {
    if (!out || !cap) return 0;
    out[0] = '\0';

    char v[OTA_JOB_MAX];
    if (ota_token_value(line, "job", v, sizeof v) && ota_job_id_ok(v)) {
        snprintf(out, cap, "%s", v);
        return 1;
    }

    /* A daemon that echoes the id without a key still gets understood; the
     * last whitespace-separated token is the only thing it can be. */
    const char *last = NULL;
    for (const char *p = line; *p; ) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        last = p;
        while (*p && !isspace((unsigned char)*p)) p++;
    }
    if (last) {
        size_t n = 0;
        while (last[n] && !isspace((unsigned char)last[n]) && n + 1 < sizeof v) {
            v[n] = last[n];
            n++;
        }
        v[n] = '\0';
        if (ota_job_id_ok(v)) {
            snprintf(out, cap, "%s", v);
            return 1;
        }
    }
    return 0;
}

static int ota_apply_job_locked(const char *line, const char *verb) {
    const char *st = ota_job_state_word(line);
    if (!st) return 0;

    ota_copy_clean(g_ota.job_state, sizeof g_ota.job_state, st);
    g_ota.activity[0] = '\0';

    if (!strcmp(st, "queued") || !strcmp(st, "running")) {
        ota_clear_error_locked();
        return 1;
    }
    if (!strcmp(st, "complete")) {
        ota_clear_error_locked();
        if (!strcmp(verb, "download")) {
            g_ota.downloaded = 1;
        } else if (!strcmp(verb, "install")) {
            /* The downloaded package was consumed. Whether the previous
             * runtime was actually kept is the daemon's answer, not this
             * page's assumption, so wait for status to report rollback. */
            g_ota.downloaded = 0;
            g_ota.rollback = 0;
        } else if (!strcmp(verb, "rollback")) {
            g_ota.downloaded = 0;
            g_ota.rollback = 0;
        }
        return 1;
    }
    if (!strcmp(st, "failed")) {
        char msg[OTA_ERR_MAX];
        snprintf(msg, sizeof msg, "The %s job failed.",
                 verb && *verb ? verb : "update");
        ota_set_error_locked(msg, OTA_ERR_JOB);
        return 1;
    }
    return 0;
}

static void ota_now_text(char *out, size_t cap) {
    if (!out || !cap) return;
    time_t t = time(NULL);
    struct tm tmv;
    if (localtime_r(&t, &tmv) && strftime(out, cap, "%Y-%m-%d %H:%M", &tmv) > 0)
        return;
    snprintf(out, cap, "Just now");
}

/* A bounded one-line call. finn_call's two-second budget is right for a
 * question but too tight for finnpkg's status read and a systemd hand-off. */
static int ota_sock_call(const char *req, char *reply, int cap, int timeout_sec) {
    if (!req || !reply || cap <= 0) return -1;
    reply[0] = '\0';
    int fd = finn_connect(FINN_CONTROL_SOCK);
    if (fd < 0) return -1;

    struct timeval tv = { timeout_sec, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    int rc = -1;
    if (finn_write_str(fd, req) == 0 && finn_write_str(fd, "\n") == 0) {
        if (finn_read_line(fd, reply, cap) > 0) rc = 0;
    }
    close(fd);
    return rc;
}

static int ota_read_exact(int fd, char *buf, size_t len) {
    size_t used = 0;
    while (used < len) {
        ssize_t got = read(fd, buf + used, len - used);
        if (got > 0) used += (size_t)got;
        else if (got < 0 && errno == EINTR) continue;
        else break;
    }
    return used == len ? 0 : -1;
}

/*
 * ota.check is the one request that can wait on GitHub, so it gets its own
 * longer budget and reads the BINARY payload itself. Returns -2 when the
 * control plane could not be reached, -1 when it accepted the connection but
 * refused, closed, or sent a malformed reply, and 0 with an allocated payload
 * on success.
 */
static int ota_check_call(char **out, char *err, size_t err_cap) {
    if (out) *out = NULL;
    if (err && err_cap) err[0] = '\0';

    int fd = finn_connect(FINN_CONTROL_SOCK);
    if (fd < 0) {
        if (err) snprintf(err, err_cap, "The control plane is not reachable.");
        return -2;
    }

    /* finnpkg's check is bounded at thirty seconds; leave room for the
     * daemon to hand the answer back instead of timing out beside it. */
    struct timeval tv = { 35, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (finn_write_str(fd, "ota.check") != 0 || finn_write_str(fd, "\n") != 0) {
        close(fd);
        if (err) snprintf(err, err_cap, "The control plane did not accept the update check.");
        return -1;
    }

    char line[512];
    int n = finn_read_line(fd, line, sizeof line);
    if (n <= 0) {
        close(fd);
        if (err) snprintf(err, err_cap, "The control plane did not answer the update check.");
        return -1;
    }
    if (!strncmp(line, "ERR ", 4)) {
        if (err) snprintf(err, err_cap, "%s", line + 4);
        close(fd);
        return -1;
    }

    if (!strncmp(line, "OK BINARY ", 10)) {
        char *end = NULL;
        unsigned long long len = strtoull(line + 10, &end, 10);
        if (!end || end == line + 10 || *end || len > 256u * 1024u) {
            if (err) snprintf(err, err_cap, "The update check reply has an invalid size.");
            close(fd);
            return -1;
        }
        char *data = malloc((size_t)len + 1);
        if (!data) {
            if (err) snprintf(err, err_cap, "The update check reply could not be allocated.");
            close(fd);
            return -1;
        }
        if (ota_read_exact(fd, data, (size_t)len) != 0 ||
            memchr(data, '\0', (size_t)len) != NULL) {
            free(data);
            if (err) snprintf(err, err_cap, "The update check reply was truncated or malformed.");
            close(fd);
            return -1;
        }
        data[len] = '\0';
        close(fd);
        *out = data;
        return 0;
    }

    if (!strncmp(line, "OK", 2)) {
        const char *p = line + 2;
        while (*p == ' ') p++;
        size_t len = strlen(p);
        char *data = malloc(len + 1);
        if (!data) {
            if (err) snprintf(err, err_cap, "The update check reply could not be allocated.");
            close(fd);
            return -1;
        }
        memcpy(data, p, len + 1);
        close(fd);
        *out = data;
        return 0;
    }

    close(fd);
    if (err) snprintf(err, err_cap, "The control plane returned an unexpected update reply.");
    return -1;
}

static void ota_run_status(void) {
    char reply[FINN_MAX_LINE];
    /* finnpkg's own status budget is three seconds; the socket must outlast
     * the command it is waiting for, or a working daemon reads as absent. */
    int rc = ota_sock_call("ota.status", reply, sizeof reply, 5);

    pthread_mutex_lock(&g_ota_mutex);
    g_ota.activity[0] = '\0';
    if (rc == 0) {
        g_ota.reachable = 1;
        if (!strncmp(reply, "ERR ", 4))
            ota_set_error_locked(reply + 4, OTA_ERR_REQUEST);
        else
            ota_apply_status_locked(reply);
    } else {
        g_ota.reachable = 0;
        ota_set_error_locked("The control plane is not reachable.", OTA_ERR_NETWORK);
    }
    pthread_mutex_unlock(&g_ota_mutex);
}

static void ota_run_check(void) {
    char *payload = NULL;
    char err[OTA_ERR_MAX] = "";

    pthread_mutex_lock(&g_ota_mutex);
    snprintf(g_ota.activity, sizeof g_ota.activity, "Checking for updates…");
    pthread_mutex_unlock(&g_ota_mutex);

    int rc = ota_check_call(&payload, err, sizeof err);

    pthread_mutex_lock(&g_ota_mutex);
    g_ota.activity[0] = '\0';
    if (rc == 0) {
        g_ota.reachable = 1;
        ota_now_text(g_ota.last_check, sizeof g_ota.last_check);
        ota_apply_check_payload_locked(payload);
    } else if (rc == -2) {
        g_ota.reachable = 0;
        ota_set_error_locked("The control plane is not reachable.", OTA_ERR_NETWORK);
    } else {
        g_ota.reachable = 1;
        ota_set_error_locked(err[0] ? err : "The update check failed.", OTA_ERR_CHECK);
    }
    pthread_mutex_unlock(&g_ota_mutex);

    free(payload);
}

static void ota_run_job(const char *job, const char *verb) {
    if (!job || !*job) return;

    char req[128];
    char reply[FINN_MAX_LINE];
    snprintf(req, sizeof req, "ota.job %s", job);
    /* systemd's own probe gets five seconds; the socket must outlast it
     * rather than reporting a working daemon as absent. */
    int rc = ota_sock_call(req, reply, sizeof reply, 7);
    int refresh = 0;

    pthread_mutex_lock(&g_ota_mutex);
    if (rc != 0) {
        g_ota.reachable = 0;
        ota_set_error_locked("The control plane is not reachable.", OTA_ERR_NETWORK);
        snprintf(g_ota.activity, sizeof g_ota.activity, "Waiting for the control plane…");
    } else {
        g_ota.reachable = 1;
        if (!strncmp(reply, "ERR ", 4)) {
            /* finnd forgot the unit, or the id is no longer valid. Holding
             * the id would poll a job that cannot answer forever, so drop
             * it and let status say what the machine actually has. */
            ota_set_error_locked(reply + 4, OTA_ERR_JOB);
            g_ota.job_id[0] = '\0';
            g_ota.job_verb[0] = '\0';
            g_ota.job_state[0] = '\0';
            refresh = 1;
        } else if (!ota_apply_job_locked(reply, verb)) {
            ota_set_error_locked("The control plane returned an unknown job state.",
                                 OTA_ERR_JOB);
            g_ota.job_id[0] = '\0';
            g_ota.job_verb[0] = '\0';
            g_ota.job_state[0] = '\0';
            refresh = 1;
        } else if (!strcmp(g_ota.job_state, "complete") ||
                   !strcmp(g_ota.job_state, "failed")) {
            refresh = 1;
        }
    }
    pthread_mutex_unlock(&g_ota_mutex);

    /* A complete install may have restarted finnd, so the status is read
     * again rather than leaving the page on the pre-install version. */
    if (refresh) ota_run_status();
}

static void ota_run_start(const char *verb) {
    char req[64];
    char reply[FINN_MAX_LINE];
    snprintf(req, sizeof req, "ota.%s", verb);

    pthread_mutex_lock(&g_ota_mutex);
    snprintf(g_ota.activity, sizeof g_ota.activity, "Starting %s…", verb);
    pthread_mutex_unlock(&g_ota_mutex);

    int rc = ota_sock_call(req, reply, sizeof reply, 7);
    char job[OTA_JOB_MAX] = "";
    int started = 0;

    pthread_mutex_lock(&g_ota_mutex);
    g_ota.activity[0] = '\0';
    if (rc != 0) {
        g_ota.reachable = 0;
        ota_set_error_locked("The control plane is not reachable.", OTA_ERR_NETWORK);
    } else {
        g_ota.reachable = 1;
        if (!strncmp(reply, "ERR ", 4)) {
            ota_set_error_locked(reply + 4, OTA_ERR_REQUEST);
        } else if (!ota_parse_job_id(reply, job, sizeof job)) {
            ota_set_error_locked("The control plane did not return a job id.", OTA_ERR_REQUEST);
        } else {
            snprintf(g_ota.job_id, sizeof g_ota.job_id, "%s", job);
            snprintf(g_ota.job_verb, sizeof g_ota.job_verb, "%s", verb);
            g_ota.job_state[0] = '\0';
            ota_clear_error_locked();
            if (!strcmp(verb, "download")) g_ota.downloaded = 0;
            snprintf(g_ota.activity, sizeof g_ota.activity,
                     "Waiting for the %s job…", verb);
            started = 1;
        }
    }
    pthread_mutex_unlock(&g_ota_mutex);

    if (started) ota_run_job(job, verb);
}

static void ota_run_op(int op, const char *job, const char *verb) {
    switch (op) {
    case OTA_OP_STATUS:   ota_run_status(); break;
    case OTA_OP_CHECK:    ota_run_check(); break;
    case OTA_OP_DOWNLOAD: ota_run_start("download"); break;
    case OTA_OP_INSTALL:  ota_run_start("install"); break;
    case OTA_OP_ROLLBACK: ota_run_start("rollback"); break;
    case OTA_OP_JOB:      ota_run_job(job, verb); break;
    default: break;
    }
}

static void *ota_worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_ota_mutex);
        while (!g_ota_stop && g_ota_pending == OTA_OP_NONE)
            pthread_cond_wait(&g_ota_cv, &g_ota_mutex);
        if (g_ota_stop) {
            pthread_mutex_unlock(&g_ota_mutex);
            break;
        }

        int op = g_ota_pending;
        char job[OTA_JOB_MAX];
        char verb[OTA_VERB_MAX];
        snprintf(job, sizeof job, "%s", g_ota_pending_job);
        snprintf(verb, sizeof verb, "%s", g_ota_pending_verb);
        g_ota_pending = OTA_OP_NONE;
        g_ota_pending_job[0] = '\0';
        g_ota_pending_verb[0] = '\0';
        pthread_mutex_unlock(&g_ota_mutex);

        ota_run_op(op, job, verb);

        pthread_mutex_lock(&g_ota_mutex);
        if (ota_op_is_user(op) && !ota_op_is_user(g_ota_pending))
            g_ota.user_busy = 0;
        if (g_ota_pending == OTA_OP_NONE)
            g_ota.busy = 0;
        g_ota_rev++;
        pthread_mutex_unlock(&g_ota_mutex);
    }

    pthread_mutex_lock(&g_ota_mutex);
    g_ota_thread_live = 0;
    pthread_mutex_unlock(&g_ota_mutex);
    return NULL;
}

static int ota_start_worker(void) {
    pthread_mutex_lock(&g_ota_mutex);
    if (g_ota.worker_ok) {
        pthread_mutex_unlock(&g_ota_mutex);
        return 0;
    }
    /* A detached worker can still be finishing a thirty-second check when
     * the app is freed. Starting a second one would give two owners of the
     * same pending slot, so wait for the old one to leave instead. The page
     * retries from its poll rather than failing for the rest of the session. */
    if (g_ota_thread_live) {
        pthread_mutex_unlock(&g_ota_mutex);
        return -1;
    }
    g_ota_stop = 0;
    g_ota_thread_live = 1;
    int rc = pthread_create(&g_ota_thread, NULL, ota_worker, NULL);
    if (rc == 0) {
        pthread_detach(g_ota_thread);
        g_ota.worker_ok = 1;
        g_ota.reachable = -1;
        g_ota_rev++;
    } else {
        g_ota_thread_live = 0;
        g_ota.worker_ok = 0;
        ota_set_error_locked("The update worker could not start.", OTA_ERR_REQUEST);
        g_ota_rev++;
    }
    pthread_mutex_unlock(&g_ota_mutex);
    return rc == 0 ? 0 : -1;
}

static void ota_stop_worker(void) {
    pthread_mutex_lock(&g_ota_mutex);
    g_ota_stop = 1;
    g_ota.worker_ok = 0;
    g_ota_pending = OTA_OP_NONE;
    g_ota_pending_job[0] = '\0';
    g_ota_pending_verb[0] = '\0';
    g_ota.busy = 0;
    g_ota.user_busy = 0;
    pthread_cond_broadcast(&g_ota_cv);
    pthread_mutex_unlock(&g_ota_mutex);
}

static void ota_snapshot(ota_state *out) {
    if (!out) return;
    pthread_mutex_lock(&g_ota_mutex);
    memcpy(out, &g_ota, sizeof *out);
    pthread_mutex_unlock(&g_ota_mutex);
}

static int ota_enqueue(int op, int user) {
    pthread_mutex_lock(&g_ota_mutex);
    if (!g_ota.worker_ok || g_ota_stop) {
        pthread_mutex_unlock(&g_ota_mutex);
        return 0;
    }
    if (g_ota_pending != OTA_OP_NONE) {
        /* A poll that has not been picked up yet is not worth more than
         * the click that just happened, so the person's action replaces
         * it. A second user action still waits for the first. */
        if (!user || ota_op_is_user(g_ota_pending)) {
            pthread_mutex_unlock(&g_ota_mutex);
            return 0;
        }
    }
    /* A background poll yields to a user action but never queues behind
     * another poll; one worker call is enough to keep the page current. */
    if (g_ota.busy && !user) {
        pthread_mutex_unlock(&g_ota_mutex);
        return 0;
    }
    if (user && !ota_op_allowed_locked(op)) {
        pthread_mutex_unlock(&g_ota_mutex);
        return 0;
    }

    if (op == OTA_OP_JOB) {
        snprintf(g_ota_pending_job, sizeof g_ota_pending_job, "%s", g_ota.job_id);
        snprintf(g_ota_pending_verb, sizeof g_ota_pending_verb, "%s", g_ota.job_verb);
    } else {
        g_ota_pending_job[0] = '\0';
        g_ota_pending_verb[0] = '\0';
    }

    g_ota_pending = op;
    g_ota.busy = 1;
    if (user) {
        g_ota.user_busy = 1;
        ota_clear_error_locked();
        g_ota.job_id[0] = '\0';
        g_ota.job_verb[0] = '\0';
        g_ota.job_state[0] = '\0';
        if (op == OTA_OP_CHECK)
            snprintf(g_ota.activity, sizeof g_ota.activity, "Checking for updates…");
        else if (op == OTA_OP_DOWNLOAD)
            snprintf(g_ota.activity, sizeof g_ota.activity, "Starting download…");
        else if (op == OTA_OP_INSTALL)
            snprintf(g_ota.activity, sizeof g_ota.activity, "Starting install…");
        else if (op == OTA_OP_ROLLBACK)
            snprintf(g_ota.activity, sizeof g_ota.activity, "Starting rollback…");
    } else if (op == OTA_OP_STATUS && !g_ota.have_status) {
        snprintf(g_ota.activity, sizeof g_ota.activity, "Reading update status…");
    }

    g_ota_rev++;
    pthread_cond_signal(&g_ota_cv);
    pthread_mutex_unlock(&g_ota_mutex);
    return 1;
}

static int64_t ota_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * Polling lives here rather than in finnsettingsapp_tick because the build
 * key is only reached while a Settings window exists: a closed window must
 * not keep waking finnd for a page nobody is looking at. Returning to the
 * page forces an immediate read instead of waiting for the next interval.
 */
static void ota_poll_due(void) {
    if (g_active_page != PAGE_UPDATE) {
        g_ota_build_page = -1;
        g_ota_next_poll_ms = 0;
        return;
    }
    if (g_ota_build_page != PAGE_UPDATE) {
        g_ota_build_page = PAGE_UPDATE;
        g_ota_next_poll_ms = 0;
    }

    int64_t now = ota_now_ms();
    if (now <= 0) now = 1;
    if (g_ota_next_poll_ms && now < g_ota_next_poll_ms) return;

    int worker = 0, have_job = 0, fast = 0;
    pthread_mutex_lock(&g_ota_mutex);
    worker = g_ota.worker_ok;
    have_job = ota_state_job_active(&g_ota);
    fast = !g_ota.have_status || g_ota.reachable != 1;
    pthread_mutex_unlock(&g_ota_mutex);

    if (!worker) {
        /* A worker left over from a previous Settings window may still be
         * finishing a long check. Retry here, or the page would stay unable
         * to act for the rest of the session. */
        if (ota_start_worker() != 0) {
            g_ota_next_poll_ms = now + 2000;
            return;
        }
        fast = 1;
    }

    int op = have_job ? OTA_OP_JOB : OTA_OP_STATUS;
    if (ota_enqueue(op, 0))
        g_ota_next_poll_ms = now + ((have_job || fast) ? 1000 : 5000);
    else
        g_ota_next_poll_ms = now + 1000;
}

/* ---------------- font loader ---------------- */

static int load_fonts(float s) {
    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char regular[600];
    snprintf(regular, sizeof regular, "%s/FixelDisplay-Regular.otf", g_font_dir);

    /* The frame's type, at the frame's sizes: 16 for the two titles and the
     * brand, 12 for rows, values and the search field, 10 for every quiet
     * line — card labels, qualifiers and details. */
    int ok = 1;
    ok &= (finn_font_load(&g_f_hero,  regular, font_px(s, 16.0f)) == 0);
    ok &= (finn_font_load(&g_f_title, regular, font_px(s, 16.0f)) == 0);
    ok &= (finn_font_load(&g_f_label, regular, font_px(s, 12.0f)) == 0);
    ok &= (finn_font_load(&g_f_sub,   regular, font_px(s, 10.0f)) == 0);
    ok &= (finn_font_load(&g_f_micro, regular, font_px(s, 10.0f)) == 0);

    if (!ok) {
        fprintf(stderr, "finnsettingsapp: cannot load fonts from %s\n", g_font_dir);
        return -1;
    }
    return 0;
}

int finnsettingsapp_init(const char *icon_dir, const char *font_dir) {
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir ? icon_dir : "");
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");

    char p[600];
    int ok = 1;
    snprintf(p, sizeof p, "%s/wifi-02.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_wifi) == 0);
    snprintf(p, sizeof p, "%s/bluetooth.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_bt) == 0);
    snprintf(p, sizeof p, "%s/sun-01.svg", g_icon_dir);         ok &= (fsvg_load(p, &g_ic_sun) == 0);
    snprintf(p, sizeof p, "%s/volume-02.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_vol) == 0);
    snprintf(p, sizeof p, "%s/battery.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_bat) == 0);
    snprintf(p, sizeof p, "%s/boot-mark.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_about) == 0);
    snprintf(p, sizeof p, "%s/search-01.svg", g_icon_dir);      ok &= (fsvg_load(p, &g_ic_search) == 0);
    snprintf(p, sizeof p, "%s/power.svg", g_icon_dir);          ok &= (fsvg_load(p, &g_ic_power) == 0);
    snprintf(p, sizeof p, "%s/refresh.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_refresh) == 0);
    snprintf(p, sizeof p, "%s/chevron-right.svg", g_icon_dir);  ok &= (fsvg_load(p, &g_ic_chevron) == 0);
    snprintf(p, sizeof p, "%s/display.svg", g_icon_dir);        ok &= (fsvg_load(p, &g_ic_display) == 0);
    snprintf(p, sizeof p, "%s/internet.svg", g_icon_dir);       ok &= (fsvg_load(p, &g_ic_globe) == 0);
    snprintf(p, sizeof p, "%s/paint-brush.svg", g_icon_dir);    ok &= (fsvg_load(p, &g_ic_brush) == 0);
    snprintf(p, sizeof p, "%s/info.svg", g_icon_dir);           ok &= (fsvg_load(p, &g_ic_info) == 0);

    g_ready = ok;
    if (ok) ota_start_worker();
    return ok ? 0 : -1;
}

void finnsettingsapp_free(void) {
    ota_stop_worker();

    fsvg_free(&g_ic_wifi);
    fsvg_free(&g_ic_bt);
    fsvg_free(&g_ic_sun);
    fsvg_free(&g_ic_vol);
    fsvg_free(&g_ic_bat);
    fsvg_free(&g_ic_about);
    fsvg_free(&g_ic_search);
    fsvg_free(&g_ic_power);
    fsvg_free(&g_ic_refresh);
    fsvg_free(&g_ic_chevron);
    fsvg_free(&g_ic_display);
    fsvg_free(&g_ic_globe);
    fsvg_free(&g_ic_brush);
    fsvg_free(&g_ic_info);

    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    g_font_scale = -1.0f;
    g_ready = 0;
}

/* ---------------- timer & key tracking ---------------- */

int finnsettingsapp_tick(float dt) {
    int dirty = 0;
    if (g_search_focused) {
        g_caret_timer += dt;
        if (g_caret_timer >= 0.5f) {
            g_caret_timer = 0.0f;
            g_caret_visible = !g_caret_visible;
            dirty = 1;
        }
    }

    /* The worker owns the network and the socket; the frame only needs to
     * know that its copy is stale, and the build key carries the same
     * revision so the body cannot miss the change. */
    unsigned rev;
    pthread_mutex_lock(&g_ota_mutex);
    rev = g_ota_rev;
    pthread_mutex_unlock(&g_ota_mutex);
    if (rev != g_ota_seen_rev) {
        g_ota_seen_rev = rev;
        dirty = 1;
    }
    return dirty;
}

uint32_t finnsettingsapp_build_key(float scale, const finn_state *st) {
    ota_poll_due();

    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(scale * 1000.0f));
    MIX(g_active_page);
    MIX(g_search_focused);
    MIX(g_caret_visible);
    MIX(st->dark);
    MIX(st->wifi);
    MIX(st->bluetooth);
    MIX(st->volume);
    MIX(st->brightness);
    MIX(st->battery_pct);
    MIX(st->charging);
    MIX(st->wired);
    for (const char *p = g_search_text; *p; p++) MIX(*p);

    /* The update page's pixels come from a worker, so its revision is part
     * of the identity of the surface, not a detail of the page draw. */
    pthread_mutex_lock(&g_ota_mutex);
    MIX(g_ota_rev);
    MIX(g_ota.busy);
    MIX(g_ota.user_busy);
    MIX(g_ota.available);
    MIX(g_ota.downloaded);
    MIX(g_ota.rollback);
    MIX(g_ota.reachable);
    pthread_mutex_unlock(&g_ota_mutex);

    #undef MIX
    return h ? h : 1u;
}

/* ---------------- search filtering ---------------- */

static int page_matches_search(int idx) {
    if (!g_search_text[0]) return 1;
    if (strcasestr(g_pages[idx].title, g_search_text) != NULL) return 1;
    if (strcasestr(g_pages[idx].keywords, g_search_text) != NULL) return 1;
    return 0;
}

/* ---------------- drawing helpers ---------------- */

/* A card: the frame's opaque paper on the content sheet. It carries no
 * hairline — the colour step from #FFFFFF to #F9F9F9 is the whole edge — and
 * every card in the app goes through here, so no page invents its own. */
static void card_bg(uint32_t *L, int lw, int lh,
                    float x, float y, float w, float h, float r, int accent) {
    finn_fill_rrect(L, lw, lh, x, y, w, h, r,
                    finn_theme_dark() ? 0xFF2A2D33u : 0xFFF9F9F9u);
    if (accent)
        finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 2.0f, FINN_ON);
}

/* A circular icon chip with the accent behind it. */
static void icon_disc(uint32_t *L, int lw, int lh, float cx, float cy,
                      float d, float s, fsvg *ic, int lit) {
    uint32_t bg = lit ? FINN_ON : (finn_theme_dark() ? 0xFF3A3F49u : 0xFFE7E7EAu);
    uint32_t fg = lit ? 0xFFFFFFFFu : FINN_DIM;
    finn_fill_rrect(L, lw, lh, cx - d * 0.5f, cy - d * 0.5f, d, d, d * 0.5f, bg);
    if (ic && ic->vw > 0)
        finn_icon_blit_ink(ic, L, lw, lh, (int)lroundf(cx), (int)lroundf(cy),
                           rs(d * 0.56f / s, s), fg, fg, 0);
}

/* A macOS-style switch: a pill track and a knob, inert when unavailable. */
static void switch_ctrl(uint32_t *L, int lw, int lh, float x, float y,
                        float w, float h, int on, int enabled) {
    float r = h * 0.5f;
    uint32_t track = !enabled ? (finn_theme_dark() ? 0xFF343841u : 0x1F000000u)
                   : (on ? FINN_ON : (finn_theme_dark() ? 0xFF4A4F59u : 0xFFD6D6DAu));
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, track);

    float knob = h - 4.0f;
    float kx = on ? (x + w - 2.0f - knob) : (x + 2.0f);
    uint32_t kc = !enabled ? (finn_theme_dark() ? 0x66FFFFFFu : 0x66FFFFFFu)
                 : (on ? 0xFFFFFFFFu : (finn_theme_dark() ? 0xFFB9BCC4u : 0xFFFFFFFFu));
    finn_fill_rrect(L, lw, lh, kx, y + 2.0f, knob, knob, knob * 0.5f, kc);
}

/* A capsule action button. */
static void pill_button(uint32_t *L, int lw, int lh, float x, float y,
                        float w, float h, const char *label,
                        const finn_font *f, int filled, int enabled) {
    float r = h * 0.5f;
    uint32_t bg = !enabled ? (finn_theme_dark() ? 0x1AFFFFFFu : 0x14000000u)
                : (filled ? FINN_ON : (finn_theme_dark() ? 0x24FFFFFFu : 0x18000000u));
    uint32_t fg = !enabled ? FINN_MUTE : (filled ? 0xFFFFFFFFu : FINN_INK);
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, bg);
    if (!filled && enabled)
        finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 1.0f,
                          finn_theme_dark() ? 0x26FFFFFFu : 0x1A000000u);
    float tw = finn_text_width(f, label);
    finn_text_draw(f, L, lw, lh, x + (w - tw) * 0.5f,
                   y + h * 0.5f + finn_text_cap_height(f) * 0.5f, label, fg);
}

/* A slider with a knob, drawn for the settings cards. */
static void slider_ctrl(uint32_t *L, int lw, int lh, float x, float y,
                        float w, float h, int pct, fsvg *icon, int enabled) {
    float r = h * 0.5f;
    uint32_t track = finn_theme_dark() ? 0x33FFFFFFu : 0x33000000u;
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, track);

    if (!enabled) {
        if (icon && icon->vw > 0)
            finn_icon_blit_ink(icon, L, lw, lh, (int)lroundf(x + h * 0.5f),
                               (int)lroundf(y + h * 0.5f),
                               (int)lroundf(h * 0.48f),
                               FINN_MUTE, FINN_MUTE, 0);
        return;
    }

    float fw = 0.0f;
    if (pct > 0) {
        fw = w * (float)pct / 100.0f;
        if (fw > w) fw = w;
        /*
         * The accent, not the shell's white fill: this slider sits on a card
         * that is itself near-white, where a white bar reads as a hole in the
         * track. The Control Center keeps the white fill because it is glass
         * over the desktop; paper needs ink.
         */
        finn_fill_bar(L, lw, lh, x, y, w, h, r, fw, FINN_ON);
    }

    if (icon && icon->vw > 0) {
        int covered = fw >= h * 0.5f;
        uint32_t ink = covered ? 0xFFFFFFFFu : FINN_INK;
        finn_icon_blit_ink(icon, L, lw, lh, (int)lroundf(x + h * 0.5f),
                           (int)lroundf(y + h * 0.5f),
                           (int)lroundf(h * 0.52f), ink, ink, 0);
    }
}

/* ---------------- sidebar geometry ---------------- */

typedef struct {
    int page_idx;
    float y_ref;      /* top of the row, reference units */
} sidebar_layout_item;

static int get_sidebar_layout(sidebar_layout_item *items, int max_items) {
    int count = 0;
    float cur_y = SB_FIRST_Y;

    for (int i = 0; i < PAGE_COUNT && count < max_items; i++) {
        if (!g_pages[i].visible && !g_search_text[0]) continue;
        if (!page_matches_search(i)) continue;

        /*
         * The frame draws one 10-unit break, between the three network rows
         * and Personalization. It is the only group break in the list, and it
         * belongs to the full list rather than to a filtered result set, so a
         * search compacts the rows instead of leaving a hole.
         */
        if (!g_search_text[0] && count > 0 &&
            strcmp(g_pages[i].section, "PERSONALIZATION") == 0)
            cur_y += SB_GROUP_GAP;

        if (count < max_items) {
            items[count].page_idx = i;
            items[count].y_ref = cur_y;
            count++;
        }
        cur_y += SB_ROW_STEP;
    }
    return count;
}

/* Where the content column starts and how wide it is, in scaled pixels.
 * Render and hit-testing both go through this, so a card cannot move without
 * its control moving with it. */
static void content_box(int wx, int ww, float s, float *cx, float *cw) {
    float sb_w = s * SB_W;
    if (sb_w > (float)ww) sb_w = (float)ww;
    float pad_l = s * CONTENT_PAD_L;
    float pad_r = s * CONTENT_PAD_R;
    float avail = (float)ww - sb_w - pad_l - pad_r;
    if (avail < 80.0f) avail = 80.0f;

    /* A maximised window gets a wider column, but only up to a point: a
     * paragraph of settings rows as wide as the screen is unreadable. Past
     * that the column stops growing and stays where the sidebar put it, so
     * the eye still reads it as one page rather than a floating island. */
    float w = s * CONTENT_MAX;
    if (avail < w) w = avail;
    *cx = (float)wx + sb_w + pad_l;
    *cw = w;
}

/* ---------------- rendering ---------------- */

/*
 * The sidebar is the Figma frame, drawn: 250 reference units of sheet, the
 * 16px title at 15,44, a 30px search field at 15,70, then 30px rows on a 35px
 * pitch with a 22px circular accent chip in front of every row. Nothing here
 * is an interpretation of the design; the numbers are the frame's numbers.
 */
static void render_sidebar(uint32_t *L, int lw, int lh,
                           float sb_x, float sb_y, float sb_w, float sb_h,
                           float s, const finn_state *st) {
    (void)st;

    /* The frame's sidebar is the window's own sheet: it draws no fill, so the
     * wallpaper's light comes through exactly as it does through the body.
     * Only the seam marks where the opaque content column begins. */
    finn_fill_rrect(L, lw, lh, sb_x + sb_w - 1.0f, sb_y, 1.0f, sb_h, 0.0f,
                    FINN_HAIR);

    float pad = s * SB_PAD;
    float title_x = sb_x + pad;

    float t_cap = finn_text_cap_height(&g_f_hero);
    finn_text_draw(&g_f_hero, L, lw, lh, title_x, sb_y + s * SB_TITLE_Y + t_cap,
                   "Settings", FINN_INK);

    /* Search field: the frame's 220x30 field at 15,70, radius 4. */
    float sf_x = title_x;
    float sf_y = sb_y + s * SB_SEARCH_Y;
    float sf_w = sb_w - pad * 2.0f;
    float sf_h = s * SB_SEARCH_H;
    float sf_r = s * 4.0f;
    finn_fill_rrect(L, lw, lh, sf_x, sf_y, sf_w, sf_h, sf_r,
                    finn_theme_dark() ? 0x33FFFFFFu : 0x40C7C7C7u);
    if (g_search_focused)
        finn_stroke_rrect(L, lw, lh, sf_x, sf_y, sf_w, sf_h, sf_r, 1.5f, FINN_ON);

    if (g_ic_search.vw > 0)
        finn_icon_blit_ink(&g_ic_search, L, lw, lh,
                           (int)lroundf(sf_x + s * 15.0f),
                           (int)lroundf(sf_y + sf_h * 0.5f),
                           rs(18.0f, s), FINN_INK, FINN_MUTE, 0);

    float stext_x = sf_x + s * 34.0f;
    float stext_y = sf_y + sf_h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f;
    if (g_search_text[0] == '\0') {
        finn_text_draw(&g_f_label, L, lw, lh, stext_x, stext_y,
                       "Type here to search",
                       finn_theme_dark() ? FINN_MUTE : 0xFF555555u);
    } else {
        finn_text_draw(&g_f_label, L, lw, lh, stext_x, stext_y,
                       g_search_text, FINN_INK);
    }
    if (g_search_focused && g_caret_visible) {
        float tw = finn_text_width(&g_f_label, g_search_text);
        float ch = finn_text_cap_height(&g_f_label) * 1.15f;
        finn_fill_rrect(L, lw, lh, stext_x + tw + 2.0f,
                        sf_y + (sf_h - ch) * 0.5f, 1.5f, ch, 0.75f, FINN_ON);
    }

    /* Rows, on the frame's 35px pitch: 30px high, 22px chip at 4, label at 34. */
    sidebar_layout_item items[24];
    int n_items = get_sidebar_layout(items, 24);

    for (int k = 0; k < n_items; k++) {
        float y = sb_y + s * items[k].y_ref;

        int pidx = items[k].page_idx;
        float rw = sf_w;
        float rh = s * SB_ROW_H;
        float rr = s * 4.14f;
        int active = (pidx == g_active_page);

        if (active)
            finn_fill_rrect(L, lw, lh, title_x, y, rw, rh, rr,
                            finn_theme_dark() ? 0x38FFFFFFu : 0xFFEFEFEFu);

        /* The frame's chip: a 22px circle in the row's own colour, the glyph
         * white on top. The selected row keeps its colour. */
        float chip = s * 22.0f;
        float chip_x = title_x + s * 4.0f;
        float chip_y = y + (rh - chip) * 0.5f;
        uint32_t chip_col = g_pages[pidx].chip ? g_pages[pidx].chip : FINN_ON;
        finn_fill_rrect(L, lw, lh, chip_x, chip_y, chip, chip, chip * 0.5f,
                        chip_col);

        fsvg *ic = g_pages[pidx].icon;
        if (ic && ic->vw > 0)
            finn_icon_blit_ink(ic, L, lw, lh,
                               (int)lroundf(chip_x + chip * 0.5f),
                               (int)lroundf(chip_y + chip * 0.5f),
                               rs(18.0f, s), 0xFFFFFFFFu, 0xFFFFFFFFu, 0);

        float tx = title_x + s * 34.0f;
        float ty = y + rh * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f;
        finn_text_draw(&g_f_label, L, lw, lh, tx, ty, g_pages[pidx].title, FINN_INK);
    }

    if (n_items == 0) {
        finn_text_draw(&g_f_label, L, lw, lh, title_x,
                       sb_y + s * (SB_FIRST_Y + 10.0f), "No matching settings", FINN_MUTE);
    }
}

/* ---------------- cards ----------------
 *
 * A page is a column of cards. Every card is the same paper, every caption
 * sits on the same baseline, and every row is the same 34 units tall with its
 * hairline inset by the card's padding. The rhythm is the whole point: a page
 * that invents one number looks like a different app.
 */

/* A caption row at the top of a card: 10px, tracked by the face, quiet. */
static void card_head(uint32_t *L, int lw, int lh, float x, float y,
                      const char *label, float s) {
    finn_text_draw(&g_f_micro, L, lw, lh, x + s * CARD_PAD,
                   y + s * 21.0f, label, FINN_MUTE);
}

/* One label/value line, value flush right. */
static void card_row(uint32_t *L, int lw, int lh, float x, float y, float w,
                     const char *label, const char *value, float s) {
    float cap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, L, lw, lh, x + s * CARD_PAD, y + s * 20.0f + cap,
                   label, FINN_INK);
    if (value && value[0]) {
        float vw = finn_text_width(&g_f_label, value);
        finn_text_draw(&g_f_label, L, lw, lh, x + w - s * CARD_PAD - vw,
                       y + s * 20.0f + cap, value, FINN_DIM);
    }
}

/* The hairline between two rows, inset so it stops short of the card edge. */
static void card_div(uint32_t *L, int lw, int lh, float x, float y, float w, float s) {
    finn_fill_rrect(L, lw, lh, x + s * CARD_PAD, y, w - s * CARD_PAD * 2.0f,
                    1.0f, 0.0f, FINN_HAIR);
}

/*
 * A quiet well inside a card: the sentence a page has instead of a list when
 * there is nothing to list. It is a recess, not another card, so it takes a
 * wash of ink rather than a frost and never a hairline of its own.
 */
static void card_well(uint32_t *L, int lw, int lh, float x, float y, float w,
                      float h, const char *line, const char *sub, float s) {
    float wn = w - s * CARD_PAD * 2.0f;
    float wx = x + s * CARD_PAD;
    finn_fill_rrect(L, lw, lh, wx, y, wn, h, s * 6.0f,
                    finn_theme_dark() ? 0x1AFFFFFFu : 0x0D000000u);
    float cap = finn_text_cap_height(&g_f_label);
    finn_text_draw(&g_f_label, L, lw, lh, wx + s * 14.0f, y + s * 24.0f + cap,
                   line, FINN_DIM);
    if (sub && sub[0])
        finn_text_draw(&g_f_sub, L, lw, lh, wx + s * 14.0f, y + s * 46.0f + cap,
                       sub, FINN_MUTE);
}

/* The status card every connectivity page opens with: chip, state, switch. */
static void card_status(uint32_t *L, int lw, int lh,
                        float x, float y, float w, float s, fsvg *icon,
                        const char *title, const char *sub, int on, int enabled) {
    float h = s * HERO_H;
    float r = s * CARD_R;
    card_bg(L, lw, lh, x, y, w, h, r, 0);

    float disc = s * 34.0f;
    icon_disc(L, lw, lh, x + s * CARD_PAD + disc * 0.5f, y + h * 0.5f,
              disc, s, icon, on);

    float tx = x + s * CARD_PAD + disc + s * 12.0f;
    finn_text_draw(&g_f_title, L, lw, lh, tx, y + s * 26.0f, title, FINN_INK);
    finn_text_draw(&g_f_sub, L, lw, lh, tx, y + s * 44.0f, sub, FINN_DIM);

    switch_ctrl(L, lw, lh, x + w - s * CARD_PAD - s * SW_W,
                y + (h - s * SW_H) * 0.5f, s * SW_W, s * SW_H, on, enabled);
}

/* ---------------- pages ---------------- */

static void page_wifi(uint32_t *L, int lw, int lh,
                      float cx, float card_y, float cw, float s,
                      const finn_state *st) {
    float r = s * CARD_R;
    int ok = st->wifi >= 0;

    const char *title = !ok ? "No wireless adapter"
                     : (st->wifi == 0 ? "Wi-Fi is off" : "Wi-Fi is on");
    const char *sub = !ok ? "This machine has no Wi-Fi hardware"
                    : (st->wifi == 0 ? "Turn it on to join a network"
                                     : "No network joined yet");
    card_status(L, lw, lh, cx, card_y, cw, s, &g_ic_wifi,
                title, sub, ok && st->wifi > 0, ok);

    /* Known networks: the list, or the reason there is no list. */
    float y = card_y + s * HERO_H + s * CARD_GAP;
    float h = s * 110.0f;
    card_bg(L, lw, lh, cx, y, cw, h, r, 0);
    card_head(L, lw, lh, cx, y, "KNOWN NETWORKS", s);

    if (!ok)
        card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
                  "No wireless hardware to scan with.",
                  "The compositor found no station interface.", s);
    else if (st->wifi <= 0)
        card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
                  "Wi-Fi is turned off.",
                  "Networks are listed once the radio is on.", s);
    else
        card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
                  "No networks in range.",
                  "Association is not implemented in this build.", s);

    /* The interface, reported rather than guessed. */
    y += h + s * CARD_GAP;
    card_bg(L, lw, lh, cx, y, cw, s * 110.0f, r, 0);
    card_head(L, lw, lh, cx, y, "INTERFACE", s);
    card_row(L, lw, lh, cx, y + s * ROW_H, cw, "Interface",
             ok ? "wlan0" : "None", s);
    card_div(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, "State",
             !ok ? "Unavailable" : (st->wifi > 0 ? "Ready" : "Off"), s);
}

static void page_bluetooth(uint32_t *L, int lw, int lh,
                           float cx, float card_y, float cw, float s,
                           const finn_state *st) {
    float r = s * CARD_R;
    int ok = st->bluetooth >= 0;

    const char *title = !ok ? "No Bluetooth adapter"
                     : (st->bluetooth == 0 ? "Bluetooth is off" : "Bluetooth is on");
    const char *sub = !ok ? "This machine has no Bluetooth controller"
                    : (st->bluetooth == 0 ? "Turn it on to pair an accessory"
                                          : "Discoverable as finnos");
    card_status(L, lw, lh, cx, card_y, cw, s, &g_ic_bt,
                title, sub, ok && st->bluetooth > 0, ok);

    float y = card_y + s * HERO_H + s * CARD_GAP;
    float h = s * 110.0f;
    card_bg(L, lw, lh, cx, y, cw, h, r, 0);
    card_head(L, lw, lh, cx, y, "MY DEVICES", s);
    card_well(L, lw, lh, cx, y + s * 30.0f, cw, s * 70.0f,
              (!ok || st->bluetooth <= 0) ? "No accessories connected."
                                          : "No accessories paired yet.",
              "Pairing is not implemented in this build.", s);

    y += h + s * CARD_GAP;
    card_bg(L, lw, lh, cx, y, cw, s * 110.0f, r, 0);
    card_head(L, lw, lh, cx, y, "CONTROLLER", s);
    card_row(L, lw, lh, cx, y + s * ROW_H, cw, "Adapter",
             ok ? "hci0" : "None", s);
    card_div(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, y + s * (ROW_H*2.0f), cw, "State",
             !ok ? "Unavailable" : (st->bluetooth > 0 ? "Ready" : "Off"), s);
}

/* One of the two appearance previews: a miniature desktop, not a screenshot. */
static void appearance_tile(uint32_t *L, int lw, int lh, float x, float y,
                            float w, float h, float s, int dark, int selected) {
    float r = s * 8.0f;

    uint32_t sky  = dark ? 0xFF1A1D25u : 0xFFE4F0FEu;
    uint32_t deep = dark ? 0xFF101319u : 0xFFA9CDF2u;
    finn_fill_rrect(L, lw, lh, x, y, w, h, r, sky);
    finn_fill_rrect(L, lw, lh, x, y + h * 0.58f, w, h * 0.42f, 0.0f, deep);

    float win_w = w * 0.58f, win_h = h * 0.48f;
    float wx = x + (w - win_w) * 0.5f;
    float wy = y + h * 0.16f;
    finn_fill_rrect(L, lw, lh, wx, wy, win_w, win_h, s * 4.0f,
                    dark ? 0xCC2B3039u : 0xD9FFFFFFu);
    finn_fill_rrect(L, lw, lh, wx, wy, win_w * 0.3f, win_h, s * 4.0f,
                    dark ? 0xE0222630u : 0xE6F1F3F7u);
    finn_fill_rrect(L, lw, lh, wx + win_w - s * 9.0f, wy + s * 3.5f,
                    s * 6.0f, s * 2.6f, s * 1.3f,
                    dark ? 0x66FFFFFFu : 0x40000000u);

    float dock_w = w * 0.42f, dock_h = s * 6.0f;
    finn_fill_rrect(L, lw, lh, x + (w - dock_w) * 0.5f, y + h - dock_h - s * 6.0f,
                    dock_w, dock_h, dock_h * 0.5f,
                    dark ? 0xC03C414Cu : 0xC0FFFFFFu);

    finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 1.0f,
                      dark ? 0x30FFFFFFu : 0x24000000u);

    if (selected) {
        finn_stroke_rrect(L, lw, lh, x, y, w, h, r, 2.0f, FINN_ON);
        float check = s * 16.0f;
        float cxp = x + w - check - s * 6.0f;
        float cyp = y + s * 6.0f;
        finn_fill_rrect(L, lw, lh, cxp, cyp, check, check, check * 0.5f, FINN_ON);
        float cx0 = cxp + check * 0.30f, cy0 = cyp + check * 0.52f;
        float cx1 = cxp + check * 0.45f, cy1 = cyp + check * 0.68f;
        float cx2 = cxp + check * 0.72f, cy2 = cyp + check * 0.36f;
        for (float t = 0.0f; t <= 1.0f; t += 0.08f) {
            float ax = cx0 + (cx1 - cx0) * t, ay = cy0 + (cy1 - cy0) * t;
            float bx = cx1 + (cx2 - cx1) * t, by = cy1 + (cy2 - cy1) * t;
            finn_fill_rrect(L, lw, lh, ax - s * 0.9f, ay - s * 0.9f,
                            s * 1.8f, s * 1.8f, s * 0.9f, 0xFFFFFFFFu);
            finn_fill_rrect(L, lw, lh, bx - s * 0.9f, by - s * 0.9f,
                            s * 1.8f, s * 1.8f, s * 0.9f, 0xFFFFFFFFu);
        }
    }

    float cap = finn_text_cap_height(&g_f_sub);
    float label_w = finn_text_width(&g_f_sub, dark ? "Dark" : "Light");
    finn_text_draw(&g_f_sub, L, lw, lh, x + (w - label_w) * 0.5f,
                   y + h + s * 14.0f + cap, dark ? "Dark" : "Light", FINN_DIM);
}

static void network_interface_state(const char *name, char *out, size_t cap) {
    if (!cap) return;
    snprintf(out, cap, "Unknown");
    char path[256];
    int n = snprintf(path, sizeof path, "/sys/class/net/%s/operstate", name);
    if (n <= 0 || (size_t)n >= sizeof path) return;
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char state[32];
    if (fgets(state, sizeof state, fp)) {
        state[strcspn(state, "\r\n")] = '\0';
        if (!strcmp(state, "up")) snprintf(out, cap, "Connected");
        else if (!strcmp(state, "down")) snprintf(out, cap, "Disconnected");
        else if (!strcmp(state, "lowerlayerdown")) snprintf(out, cap, "No link");
        else if (!strcmp(state, "dormant")) snprintf(out, cap, "Dormant");
        else snprintf(out, cap, "Unknown");
    }
    fclose(fp);
}

static void page_network(uint32_t *L, int lw, int lh,
                         float cx, float card_y, float cw, float s,
                         const finn_state *st) {
    (void)st;
    float r = s * CARD_R;
    float c1_h = s * 202.0f;
    card_bg(L, lw, lh, cx, card_y, cw, c1_h, r, 0);
    card_head(L, lw, lh, cx, card_y, "NETWORK INTERFACES", s);

    DIR *dir = opendir("/sys/class/net");
    int rows = 0;
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && rows < 4) {
            if (entry->d_name[0] == '.' || !strcmp(entry->d_name, "lo")) continue;
            char state[32];
            network_interface_state(entry->d_name, state, sizeof state);
            float row_y = card_y + s * (32.0f + rows * 36.0f);
            card_row(L, lw, lh, cx, row_y, cw, entry->d_name, state, s);
            if (rows < 3) card_div(L, lw, lh, cx, row_y + s * 34.0f, cw, s);
            rows++;
        }
        closedir(dir);
    }
    if (!rows) {
        card_well(L, lw, lh, cx, card_y + s * 36.0f, cw, s * 102.0f,
                  "No network interfaces detected",
                  "Interfaces appear here when the system detects them.", s);
    }

    float c2_y = card_y + c1_h + s * CARD_GAP;
    float c2_h = s * 100.0f;
    card_bg(L, lw, lh, cx, c2_y, cw, c2_h, r, 0);
    card_head(L, lw, lh, cx, c2_y, "NETWORK MANAGEMENT", s);
    card_well(L, lw, lh, cx, c2_y + s * 34.0f, cw, s * 54.0f,
              "Link state is read-only",
              "Network configuration is not available in this build.", s);
}

static void page_display(uint32_t *L, int lw, int lh,
                         float cx, float card_y, float cw, float s,
                         const finn_state *st) {
    float r = s * CARD_R;

    /* Output facts. Everything here is read from the running device. */
    float c1_h = s * 144.0f;
    card_bg(L, lw, lh, cx, card_y, cw, c1_h, r, 0);
    card_head(L, lw, lh, cx, card_y, "DISPLAY", s);

    char res[64], scale_s[64];
    snprintf(res, sizeof res, "%d × %d", g_cached_screen_w, g_cached_screen_h);
    snprintf(scale_s, sizeof scale_s, "%.2f×", s);

    card_row(L, lw, lh, cx, card_y + s * ROW_H, cw, "Resolution", res, s);
    card_div(L, lw, lh, cx, card_y + s * (ROW_H * 2.0f), cw, s);
    card_row(L, lw, lh, cx, card_y + s * (ROW_H * 2.0f), cw, "UI scale", scale_s, s);
    card_div(L, lw, lh, cx, card_y + s * (ROW_H * 3.0f), cw, s);
    card_row(L, lw, lh, cx, card_y + s * (ROW_H * 3.0f), cw, "Presentation", "Direct DRM/KMS", s);

    /* Appearance: two previews, side by side. */
    float c2_y = card_y + c1_h + s * CARD_GAP;
    float c2_h = s * 158.0f;
    card_bg(L, lw, lh, cx, c2_y, cw, c2_h, r, 0);
    card_head(L, lw, lh, cx, c2_y, "APPEARANCE", s);

    float tw = s * TILE_W, th = s * TILE_H;
    float gap = s * 16.0f;
    float tx = cx + s * CARD_PAD;
    float ty = c2_y + s * 38.0f;
    appearance_tile(L, lw, lh, tx, ty, tw, th, s, 0, !st->dark);
    appearance_tile(L, lw, lh, tx + tw + gap, ty, tw, th, s, 1, st->dark);

    finn_text_draw(&g_f_sub, L, lw, lh, tx, c2_y + c2_h - s * 14.0f,
                   "Saved to /var/lib/finn/settings and applied to every surface.",
                   FINN_MUTE);
}

static void page_sound(uint32_t *L, int lw, int lh,
                       float cx, float card_y, float cw, float s,
                       const finn_state *st) {
    float r = s * CARD_R;
    float c1_h = s * 132.0f;

    card_bg(L, lw, lh, cx, card_y, cw, c1_h, r, 0);
    card_head(L, lw, lh, cx, card_y, "OUTPUT VOLUME", s);

    if (st->volume >= 0) {
        char vol_s[16];
        snprintf(vol_s, sizeof vol_s, "%d%%", st->volume);
        float vw = finn_text_width(&g_f_sub, vol_s);
        finn_text_draw(&g_f_sub, L, lw, lh, cx + cw - s * CARD_PAD - vw,
                       card_y + s * 22.0f, vol_s, FINN_DIM);

        float sl_x = cx + s * CARD_PAD;
        float sl_y = card_y + s * 40.0f;
        float sl_w = cw - s * CARD_PAD * 2.0f;
        float sl_h = s * 26.0f;
        slider_ctrl(L, lw, lh, sl_x, sl_y, sl_w, sl_h, st->volume, &g_ic_vol, 1);

        float mb_w = s * 72.0f, mb_h = s * 28.0f;
        pill_button(L, lw, lh, cx + s * CARD_PAD, card_y + s * 80.0f,
                    mb_w, mb_h, st->volume == 0 ? "Unmute" : "Mute",
                    &g_f_label, 0, 1);
    } else {
        card_well(L, lw, lh, cx, card_y + s * 30.0f, cw, s * 70.0f,
                  "No audio mixer on this machine",
                  "Volume is absent, not zero: there is no ALSA element.", s);
    }

    float c2_y = card_y + c1_h + s * CARD_GAP;
    float c2_h = s * 110.0f;
    card_bg(L, lw, lh, cx, c2_y, cw, c2_h, r, 0);
    card_head(L, lw, lh, cx, c2_y, "AUDIO CONTROLLER", s);

    const char *elem = finn_audio_element();
    card_row(L, lw, lh, cx, c2_y + s * ROW_H, cw, "Mixer element",
             elem ? elem : "None", s);
    card_div(L, lw, lh, cx, c2_y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, c2_y + s * 68.0f, cw, "Driver", "ALSA", s);
}

static void page_power(uint32_t *L, int lw, int lh,
                       float cx, float card_y, float cw, float s,
                       const finn_state *st) {
    float r = s * CARD_R;
    float hero_h = s * 92.0f;

    card_bg(L, lw, lh, cx, card_y, cw, hero_h, r, 0);

    float disc = s * 34.0f;
    icon_disc(L, lw, lh, cx + s * CARD_PAD + disc * 0.5f,
              card_y + s * 46.0f, disc, s, &g_ic_bat, 1);

    float tx = cx + s * CARD_PAD + disc + s * 12.0f;
    float ty = card_y + s * 38.0f, dy = card_y + s * 58.0f;

    if (st->battery_pct >= 0) {
        char t[64];
        snprintf(t, sizeof t, "Battery %d%%", st->battery_pct);
        finn_text_draw(&g_f_title, L, lw, lh, tx, ty, t, FINN_INK);
        finn_text_draw(&g_f_sub, L, lw, lh, tx, dy,
                       st->charging ? "Charging · plugged in"
                                    : "On battery · unplugged", FINN_DIM);

        float gm_x = cx + s * CARD_PAD;
        float gm_y = card_y + s * 74.0f;
        float gm_w = cw - s * CARD_PAD * 2.0f;
        float gm_h = s * 8.0f;
        finn_fill_rrect(L, lw, lh, gm_x, gm_y, gm_w, gm_h, gm_h * 0.5f,
                        finn_theme_dark() ? 0x3DFFFFFFu : 0x24000000u);
        float gw = gm_w * (float)st->battery_pct / 100.0f;
        if (gw < gm_h) gw = gm_h;
        finn_fill_rrect(L, lw, lh, gm_x, gm_y, gw, gm_h, gm_h * 0.5f, FINN_ON);
    } else {
        finn_text_draw(&g_f_title, L, lw, lh, tx, ty, "Power source: mains", FINN_INK);
        finn_text_draw(&g_f_sub, L, lw, lh, tx, dy,
                       "No battery on this machine", FINN_DIM);
    }

    float c2_y = card_y + hero_h + s * CARD_GAP;
    float c2_h = s * 148.0f;
    card_bg(L, lw, lh, cx, c2_y, cw, c2_h, r, 0);
    card_head(L, lw, lh, cx, c2_y, "POWER SUPPLY", s);

    card_row(L, lw, lh, cx, c2_y + s * ROW_H, cw, "Battery",
             st->battery_pct >= 0 ? "Present" : "Not present", s);
    card_div(L, lw, lh, cx, c2_y + s * (ROW_H*2.0f), cw, s);
    card_row(L, lw, lh, cx, c2_y + s * 68.0f, cw, "External power",
             st->battery_pct < 0 || st->charging ? "Connected" : "Disconnected", s);
    card_div(L, lw, lh, cx, c2_y + s * (ROW_H*3.0f), cw, s);
    card_row(L, lw, lh, cx, c2_y + s * (ROW_H*3.0f), cw, "Sleep policy",
             "Not implemented yet", s);
}

static void system_text_fit(char *out, size_t cap, const char *text,
                            const finn_font *font, float max_width) {
    if (!cap) return;
    if (cap < 4) { out[0] = '\0'; return; }
    snprintf(out, cap, "%s", text ? text : "");
    size_t n = strlen(out);
    if (finn_text_width(font, out) <= max_width) return;
    if (n > cap - 4) n = cap - 4;
    while (n > 0) {
        n--;
        out[n] = '.';
        out[n + 1] = '.';
        out[n + 2] = '.';
        out[n + 3] = '\0';
        if (finn_text_width(font, out) <= max_width) return;
    }
    snprintf(out, cap, "...");
}

static void system_info_tile(uint32_t *L, int lw, int lh,
                             float x, float y, float w, float h, float s,
                             const char *title, const char *value,
                             const char *detail) {
    card_bg(L, lw, lh, x, y, w, h, s * CARD_R, 0);

    /* The frame's stat tile: category at 5,8, qualifier at 5,42, value at
     * 5,59 — the quiet line above the reading, never the other way round. */
    float pad = s * 5.0f;
    float text_x = x + pad;
    float max_width = w - pad * 2.0f;
    char fitted[160];

    finn_text_draw(&g_f_micro, L, lw, lh, text_x,
                   y + s * 8.0f + finn_text_cap_height(&g_f_micro),
                   title, FINN_DIM);

    system_text_fit(fitted, sizeof fitted, detail, &g_f_micro, max_width);
    finn_text_draw(&g_f_micro, L, lw, lh, text_x,
                   y + s * 42.0f + finn_text_cap_height(&g_f_micro),
                   fitted, FINN_DIM);

    system_text_fit(fitted, sizeof fitted, value, &g_f_label, max_width);
    finn_text_draw(&g_f_label, L, lw, lh, text_x,
                   y + s * 59.0f + finn_text_cap_height(&g_f_label),
                   fitted, FINN_INK);
}

static void system_cpu_model(char *out, size_t cap, const char *fallback) {
    snprintf(out, cap, "%s", fallback ? fallback : "");
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof line, fp)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *key = line;
        while (*key && isspace((unsigned char)*key)) key++;
        char *key_end = key + strlen(key);
        while (key_end > key && isspace((unsigned char)key_end[-1])) *--key_end = '\0';
        if (strcmp(key, "model name") && strcmp(key, "Hardware") &&
            strcmp(key, "Processor")) continue;

        char *value = colon + 1;
        while (*value && isspace((unsigned char)*value)) value++;
        value[strcspn(value, "\r\n")] = '\0';
        if (*value) snprintf(out, cap, "%s", value);
        break;
    }
    fclose(fp);
}

static void page_about(uint32_t *L, int lw, int lh,
                       float cx, float card_y, float cw, float s,
                       const finn_state *st) {
    (void)st;
    struct utsname un;
    memset(&un, 0, sizeof un);
    int have_uname = uname(&un) == 0;

    struct sysinfo si;
    memset(&si, 0, sizeof si);
    int have_sysinfo = sysinfo(&si) == 0;

    char ram_value[64] = "Unavailable";
    char ram_detail[64] = "Memory is not reported";
    if (have_sysinfo && si.mem_unit) {
        double total_gb = (double)si.totalram * (double)si.mem_unit /
                          (1024.0 * 1024.0 * 1024.0);
        double free_gb = (double)si.freeram * (double)si.mem_unit /
                         (1024.0 * 1024.0 * 1024.0);
        snprintf(ram_value, sizeof ram_value, "%.1f GB", total_gb);
        snprintf(ram_detail, sizeof ram_detail, "%.1f GB free", free_gb);
    }

    char storage_value[64] = "Unavailable";
    char storage_detail[64] = "Root storage is not reported";
    struct statvfs fs;
    if (statvfs("/", &fs) == 0) {
        unsigned long block_size = fs.f_frsize ? fs.f_frsize : fs.f_bsize;
        if (block_size) {
            double total_gb = (double)fs.f_blocks * (double)block_size /
                              (1024.0 * 1024.0 * 1024.0);
            double free_gb = (double)fs.f_bavail * (double)block_size /
                             (1024.0 * 1024.0 * 1024.0);
            snprintf(storage_value, sizeof storage_value, "%.0f GB", total_gb);
            snprintf(storage_detail, sizeof storage_detail,
                     "%.0f GB free on /", free_gb);
        }
    }

    char cpu_model[160] = "";
    system_cpu_model(cpu_model, sizeof cpu_model, "");
    const char *cpu_arch = have_uname && un.machine[0] ? un.machine : "Unknown";
    const char *cpu_value = cpu_model[0] ? cpu_model : "Not reported";

    /* The frame's composition: identity at left in a mint-washed hero, four
     * hardware readings at right. Values are read from the host where
     * available; GPU details stay explicitly absent until the compositor
     * exposes renderer telemetry to Settings. */
    float gap = s * ABOUT_GAP;
    float tile_gap = s * ABOUT_TILE_GAP;
    float tile_h = s * ABOUT_TILE_H;
    float hero_h = s * ABOUT_HERO_H;
    float hero_w = s * ABOUT_HERO_W;

    float right_x = cx + hero_w + gap;
    float right_w = cw - hero_w - gap;
    float tile_w = (right_w - tile_gap) * 0.5f;

    int stacked = 0;
    if (right_w < s * 200.0f) {
        /* Too narrow for the frame's two columns: the hero keeps its height
         * and the tiles fall below it in the same two-column grid, rather
         * than shrinking into unreadable slivers. */
        stacked = 1;
        hero_w = cw;
        tile_w = (cw - tile_gap) * 0.5f;
        right_x = cx;
    }

    uint32_t hero_top = finn_theme_dark() ? 0xFF2A2D33u : 0xFFF9F9F9u;
    uint32_t hero_bottom = finn_theme_dark() ? 0xFF17322Fu : 0xFFE7FFFBu;
    finn_fill_rrect_grad(L, lw, lh, cx, card_y, hero_w, hero_h,
                         s * CARD_R, hero_top, hero_bottom);

    /* The frame's mark: 20 units at (70,70) inside the hero, which is its
     * vertical centre; the brand starts 15 units after the mark. */
    float icon_px = s * 20.0f;
    float icon_cx = cx + s * 80.0f;
    float icon_cy = card_y + s * 80.0f;
    if (g_ic_about.vw > 0)
        finn_icon_blit_ink(&g_ic_about, L, lw, lh,
                           (int)lroundf(icon_cx), (int)lroundf(icon_cy),
                           (int)lroundf(icon_px), FINN_INK, FINN_INK, 0);

    float brand_x = cx + s * 105.0f;
    float brand_base = card_y + s * 80.0f +
                       finn_text_cap_height(&g_f_title) * 0.5f;
    const char *brand = "FinnOS";
    const char *brand_tail = " One Beta";
    finn_text_draw(&g_f_title, L, lw, lh, brand_x, brand_base, brand, FINN_INK);
    finn_text_draw(&g_f_title, L, lw, lh,
                   brand_x + finn_text_width(&g_f_title, brand), brand_base,
                   brand_tail, finn_theme_dark() ? 0xB3FFFFFFu : 0xA6000000u);

    float tiles_y = stacked ? card_y + hero_h + gap : card_y;
    float tiles_x = stacked ? cx : right_x;
    system_info_tile(L, lw, lh, tiles_x, tiles_y,
                     tile_w, tile_h, s, "RAM", ram_value, ram_detail);
    system_info_tile(L, lw, lh, tiles_x + tile_w + tile_gap, tiles_y,
                     tile_w, tile_h, s, "Storage",
                     storage_value, storage_detail);
    system_info_tile(L, lw, lh, tiles_x,
                     tiles_y + tile_h + gap, tile_w, tile_h, s, "CPU",
                     cpu_value, cpu_arch);
    system_info_tile(L, lw, lh, tiles_x + tile_w + tile_gap,
                     tiles_y + tile_h + gap, tile_w, tile_h, s, "GPU",
                     "Not reported", "No GPU telemetry");
}

/* ---------------- System Update ---------------- */

/* The daemon's words, in the shell's sentence case. Unknown values are
 * passed through rather than guessed at. */
static const char *ota_state_word(const char *state) {
    if (!state || !*state) return NULL;
    if (!strcasecmp(state, "idle")) return "Idle";
    if (!strcasecmp(state, "checking")) return "Checking";
    if (!strcasecmp(state, "downloading")) return "Downloading";
    if (!strcasecmp(state, "installing")) return "Installing";
    if (!strcasecmp(state, "rollback") || !strcasecmp(state, "rolling-back"))
        return "Rolling back";
    if (!strcasecmp(state, "downloaded") || !strcasecmp(state, "ready"))
        return "Downloaded";
    if (!strcasecmp(state, "complete")) return "Complete";
    if (!strcasecmp(state, "failed")) return "Failed";
    if (!strcasecmp(state, "unknown")) return "Unknown";
    return NULL;
}

static const char *ota_verb_word(const char *verb) {
    if (!verb || !*verb) return "Update";
    if (!strcasecmp(verb, "download")) return "Download";
    if (!strcasecmp(verb, "install")) return "Install";
    if (!strcasecmp(verb, "rollback")) return "Roll back";
    return verb;
}

static const char *ota_job_word(const char *state) {
    if (!state || !*state) return "Waiting";
    if (!strcasecmp(state, "queued")) return "Queued";
    if (!strcasecmp(state, "running")) return "Running";
    if (!strcasecmp(state, "complete")) return "Complete";
    if (!strcasecmp(state, "failed")) return "Failed";
    return state;
}

static void ota_state_line(const ota_state *v, char *out, size_t cap) {
    if (!out || !cap) return;
    if (!v->worker_ok) {
        snprintf(out, cap, "Update worker unavailable");
        return;
    }
    if (v->activity[0]) {
        snprintf(out, cap, "%s", v->activity);
        return;
    }
    if (v->reachable == 0) {
        snprintf(out, cap, "Control plane unreachable");
        return;
    }
    if (v->reachable < 0) {
        snprintf(out, cap, "Not checked");
        return;
    }
    if (v->job_id[0] && ota_state_job_active(v)) {
        snprintf(out, cap, "%s · %s", ota_verb_word(v->job_verb),
                 ota_job_word(v->job_state));
        return;
    }
    const char *word = ota_state_word(v->state);
    if (word) {
        snprintf(out, cap, "%s", word);
        return;
    }
    if (v->state[0]) {
        snprintf(out, cap, "%s", v->state);
        return;
    }
    /* A finished job is history. The daemon's state above is the machine
     * now; the job's word is only a fallback when status reported none. */
    if (v->job_id[0]) {
        snprintf(out, cap, "%s · %s", ota_verb_word(v->job_verb),
                 ota_job_word(v->job_state));
        return;
    }
    snprintf(out, cap, "Unknown");
}

static int ota_version_known(const char *v) {
    return v && *v && strcasecmp(v, "unknown") != 0;
}

/* One line, and it is the daemon's line: an error outranks release notes,
 * because a failed check must not look like a successful one. */
static void ota_note_line(const ota_state *v, char *out, size_t cap) {
    if (!out || !cap) return;
    if (v->error[0]) {
        snprintf(out, cap, "%s", v->error);
        return;
    }
    if (v->notes[0]) {
        snprintf(out, cap, "%s", v->notes);
        return;
    }
    if (v->available == 0) {
        if (ota_version_known(v->current) && ota_version_known(v->latest) &&
            !strcmp(v->latest, v->current))
            snprintf(out, cap, "FinnOS %s is up to date.", v->current);
        else if (ota_version_known(v->current))
            snprintf(out, cap, "No newer release than %s is available.",
                     v->current);
        else
            snprintf(out, cap, "No newer release is available.");
        return;
    }
    if (v->reachable == 0) {
        snprintf(out, cap, "The control plane is not reachable.");
        return;
    }
    if (!v->have_check) {
        snprintf(out, cap, "Check for updates to read the release notes.");
        return;
    }
    snprintf(out, cap, "No release notes provided.");
}

/*
 * The action row. The buttons that are honest right now are the buttons
 * that are drawn: Check is always available when no job is in flight,
 * Download only when the daemon reports an update that is not on disk,
 * Install only after a download this page saw complete, and Roll back only
 * when the daemon says a previous package exists.
 */
static void ota_actions_layout(float cx, float cw, float s, int mask,
                               float xs[4], float ws[4], int ids[4],
                               int *count) {
    static const int all[4] = {
        FINNSETTINGS_HIT_OTA_CHECK, FINNSETTINGS_HIT_OTA_DOWNLOAD,
        FINNSETTINGS_HIT_OTA_INSTALL, FINNSETTINGS_HIT_OTA_ROLLBACK
    };
    int n = 0;
    for (int i = 0; i < 4; i++) if (mask & (1 << i)) n++;

    float pad = s * CARD_PAD;
    float gap = s * 8.0f;
    float avail = cw - pad * 2.0f;
    float bw = n > 0 ? (avail - gap * (float)(n - 1)) / (float)n : 0.0f;
    if (bw > s * 132.0f) bw = s * 132.0f;
    if (bw < s * 64.0f) bw = s * 64.0f;

    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (!(mask & (1 << i))) continue;
        xs[k] = cx + pad + (float)k * (bw + gap);
        ws[k] = bw;
        ids[k] = all[i];
        k++;
    }
    if (count) *count = k;
}

static const char *ota_action_label(int id) {
    switch (id) {
    case FINNSETTINGS_HIT_OTA_CHECK:    return "Check";
    case FINNSETTINGS_HIT_OTA_DOWNLOAD: return "Download";
    case FINNSETTINGS_HIT_OTA_INSTALL:  return "Install";
    case FINNSETTINGS_HIT_OTA_ROLLBACK: return "Roll back";
    default:                            return "";
    }
}

static void page_update(uint32_t *L, int lw, int lh,
                        float cx, float card_y, float cw, float s,
                        const finn_state *st) {
    (void)st;

    ota_state v;
    ota_snapshot(&v);

    float r = s * CARD_R;
    float pad = s * CARD_PAD;
    float cap_label = finn_text_cap_height(&g_f_label);
    float cap_micro = finn_text_cap_height(&g_f_micro);
    float value_max = cw * 0.58f;
    char fitted[OTA_NOTE_MAX + 64];

    float c1_h = s * OTA_STATUS_H;
    card_bg(L, lw, lh, cx, card_y, cw, c1_h, r, 0);
    card_head(L, lw, lh, cx, card_y, "UPDATE STATUS", s);

    char current[OTA_VER_MAX + 16];
    char latest[OTA_VER_MAX + 24];
    char last[OTA_CHECK_MAX + 8];
    char state_line[96];

    const char *cur = v.current[0] ? v.current : "Unknown";
    if (!strcasecmp(cur, "unknown")) cur = "Unknown";
    snprintf(current, sizeof current, "%s", cur);

    const char *lat;
    if (v.latest[0]) {
        lat = !strcasecmp(v.latest, "unknown") ? "Unknown" : v.latest;
    } else if (v.available == 1) {
        lat = "Unknown";
    } else if (v.available == 0) {
        lat = "None";
    } else if (v.have_check || v.have_status) {
        lat = "Not reported";
    } else {
        lat = "Not checked";
    }
    snprintf(latest, sizeof latest, "%s", lat);

    const char *lastv = v.last_check[0] ? v.last_check : "Never";
    if (!strcasecmp(lastv, "never")) lastv = "Never";
    snprintf(last, sizeof last, "%s", lastv);
    ota_state_line(&v, state_line, sizeof state_line);

    float row = card_y + s * ROW_H;
    system_text_fit(fitted, sizeof fitted, current, &g_f_label, value_max);
    card_row(L, lw, lh, cx, row, cw, "Installed version", fitted, s);
    card_div(L, lw, lh, cx, row + s * ROW_H, cw, s);

    system_text_fit(fitted, sizeof fitted, latest, &g_f_label, value_max);
    card_row(L, lw, lh, cx, row + s * ROW_H, cw, "Latest version", fitted, s);
    card_div(L, lw, lh, cx, row + s * ROW_H * 2.0f, cw, s);

    system_text_fit(fitted, sizeof fitted, last, &g_f_label, value_max);
    card_row(L, lw, lh, cx, row + s * ROW_H * 2.0f, cw, "Last checked", fitted, s);
    card_div(L, lw, lh, cx, row + s * ROW_H * 3.0f, cw, s);

    system_text_fit(fitted, sizeof fitted, state_line, &g_f_label, value_max);
    card_row(L, lw, lh, cx, row + s * ROW_H * 3.0f, cw, "Update state", fitted, s);

    float c2_y = card_y + c1_h + s * CARD_GAP;
    card_bg(L, lw, lh, cx, c2_y, cw, s * OTA_ACTIONS_H,
            r, 0);
    card_head(L, lw, lh, cx, c2_y, "ACTIONS", s);

    int mask = ota_action_mask(&v);
    float xs[4], ws[4];
    int ids[4], n = 0;
    ota_actions_layout(cx, cw, s, mask, xs, ws, ids, &n);
    if (n == 0) {
        const char *line = v.user_busy
            ? "Waiting for the current update request to finish."
            : (ota_state_job_active(&v)
                ? "Waiting for the current update job to finish."
                : "No update action is available.");
        finn_text_draw(&g_f_label, L, lw, lh, cx + pad,
                       c2_y + s * 44.0f + cap_label, line, FINN_DIM);
    } else {
        int primary = ids[0];
        if (mask & 4) primary = FINNSETTINGS_HIT_OTA_INSTALL;
        else if (mask & 2) primary = FINNSETTINGS_HIT_OTA_DOWNLOAD;
        else if (mask & 8) primary = FINNSETTINGS_HIT_OTA_ROLLBACK;

        for (int k = 0; k < n; k++) {
            pill_button(L, lw, lh, xs[k], c2_y + s * 34.0f, ws[k], s * 28.0f,
                        ota_action_label(ids[k]), &g_f_label,
                        ids[k] == primary, 1);
        }
    }

    float c3_y = c2_y + s * OTA_ACTIONS_H + s * CARD_GAP;
    card_bg(L, lw, lh, cx, c3_y, cw, s * OTA_NOTES_H,
            r, 0);
    card_head(L, lw, lh, cx, c3_y, "RELEASE NOTES", s);

    char note[OTA_NOTE_MAX + 64];
    ota_note_line(&v, note, sizeof note);
    system_text_fit(fitted, sizeof fitted, note, &g_f_label, cw - pad * 2.0f);
    finn_text_draw(&g_f_label, L, lw, lh, cx + pad,
                   c3_y + s * 44.0f + cap_label, fitted,
                   v.error[0] ? FINN_INK : FINN_DIM);

    /* The update path is the runtime package, not the Arch base system.
     * Saying so here is cheaper than a user assuming pacman just ran. */
    const char *scope =
        "FinnOS runtime and assets only; Arch base packages are not updated here.";
    system_text_fit(fitted, sizeof fitted, scope, &g_f_micro, cw - pad * 2.0f);
    finn_text_draw(&g_f_micro, L, lw, lh, cx + pad,
                   c3_y + s * 66.0f + cap_micro, fitted, FINN_MUTE);
}

/* ---------------- rendering ---------------- */

void finnsettingsapp_render(uint32_t *layer, int lw, int lh,
                            int lx0, int ly0, int wx, int wy, int ww, int wh,
                            int screen_w, int screen_h,
                            float scale, const finn_state *st, int focused) {
    (void)focused;
    if (!g_ready) return;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }

    float s = ui_scale(scale);
    g_cached_screen_w = screen_w;
    g_cached_screen_h = screen_h;
    g_cached_wx = wx;
    g_cached_wy = wy;
    g_cached_ww = ww;
    g_cached_wh = wh;

    /* Ensure the active page survives a new filter. */
    if (!page_matches_search(g_active_page)) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            if (page_matches_search(i)) {
                g_active_page = i;
                break;
            }
        }
    }

    float sb_w = s * SB_W;
    if (sb_w > (float)ww) sb_w = (float)ww;
    float sb_x = (float)(wx - lx0);
    float sb_y = (float)(wy - ly0);
    float sb_h = (float)wh;
    float win_r = s * 8.0f;

    render_sidebar(layer, lw, lh, sb_x, sb_y, sb_w, sb_h, s, st);

    /*
     * The frame's content column is opaque paper: the sidebar keeps the
     * window's glass, the content does not. It is square at the seam and
     * round only where the window itself is round.
     */
    float cbx = sb_x + sb_w;
    float cbw = (float)ww - sb_w;
    uint32_t paper = finn_theme_dark() ? 0xFF1E2128u : 0xFFFFFFFFu;
    finn_fill_rrect(layer, lw, lh, cbx, sb_y, cbw, sb_h, win_r, paper);
    finn_fill_rrect(layer, lw, lh, cbx, sb_y, win_r, sb_h, 0.0f, paper);

    /* Content column. */
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    cx -= (float)lx0;
    float cy = sb_y;

    float pt_y = cy + s * HEADER_TOP + finn_text_cap_height(&g_f_hero);
    finn_text_draw(&g_f_hero, layer, lw, lh, cx, pt_y,
                   g_pages[g_active_page].title, FINN_INK);

    float card_y = cy + s * CARD_TOP;

    switch (g_active_page) {
    case 0: page_wifi(layer, lw, lh, cx, card_y, cw, s, st); break;
    case 1: page_bluetooth(layer, lw, lh, cx, card_y, cw, s, st); break;
    case 2: page_network(layer, lw, lh, cx, card_y, cw, s, st); break;
    case 3: page_display(layer, lw, lh, cx, card_y, cw, s, st); break;
    case 4: page_display(layer, lw, lh, cx, card_y, cw, s, st); break;
    case 5: page_sound(layer, lw, lh, cx, card_y, cw, s, st); break;
    case 6: page_power(layer, lw, lh, cx, card_y, cw, s, st); break;
    case PAGE_UPDATE:
        page_update(layer, lw, lh, cx, card_y, cw, s, st);
        break;
    default: page_about(layer, lw, lh, cx, card_y, cw, s, st); break;
    }
}

/* ---------------- hit testing & interaction ---------------- */

/* The switch in the first card of the Wi-Fi and Bluetooth pages. */
static int toggle_rect(int wx, int wy, int ww, float s,
                       float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    float card_y = (float)wy + s * CARD_TOP;
    float sw_w = s * SW_W, sw_h = s * SW_H;
    float sw_x = cx + cw - s * CARD_PAD - sw_w;
    float sw_y = card_y + (s * HERO_H - sw_h) * 0.5f;
    if (bx) *bx = sw_x;
    if (by) *by = sw_y;
    if (bw) *bw = sw_w;
    if (bh) *bh = sw_h;
    return 1;
}

static void appearance_rect(int wx, int wy, int ww, float s, int which,
                            float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    (void)cw;
    float card_y = (float)wy + s * CARD_TOP + s * 144.0f + s * CARD_GAP;
    float tw = s * TILE_W, th = s * TILE_H;
    float tx = cx + s * CARD_PAD + which * (tw + s * 16.0f);
    float ty = card_y + s * 38.0f;
    if (bx) *bx = tx;
    if (by) *by = ty;
    if (bw) *bw = tw;
    if (bh) *bh = th;
}

static void volume_rect(int wx, int wy, int ww, float s,
                        float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    float card_y = (float)wy + s * CARD_TOP;
    if (bx) *bx = cx + s * CARD_PAD;
    if (by) *by = card_y + s * 40.0f;
    if (bw) *bw = cw - s * CARD_PAD * 2.0f;
    if (bh) *bh = s * 26.0f;
}

static void mute_rect(int wx, int wy, int ww, float s,
                      float *bx, float *by, float *bw, float *bh) {
    float cx, cw;
    content_box(wx, ww, s, &cx, &cw);
    (void)cw;
    float card_y = (float)wy + s * CARD_TOP;
    if (bx) *bx = cx + s * CARD_PAD;
    if (by) *by = card_y + s * 80.0f;
    if (bw) *bw = s * 72.0f;
    if (bh) *bh = s * 28.0f;
}

static int in_box(int px, int py, float x, float y, float w, float h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

int finnsettingsapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                         float scale, int *out_sub) {
    if (out_sub) *out_sub = 0;
    if (px < wx || py < wy || px >= wx + ww || py >= wy + wh)
        return FINNSETTINGS_HIT_NONE;

    g_cached_wx = wx;
    g_cached_wy = wy;
    g_cached_ww = ww;
    g_cached_wh = wh;

    float s = ui_scale(scale);
    float sb_w = s * SB_W;
    if (sb_w > (float)ww) sb_w = (float)ww;

    /* Inside the sidebar. */
    if (px < wx + (int)sb_w) {
        if (py < wy + rs(24.0f, s))
            return FINNSETTINGS_HIT_TITLEBAR;

        float sf_x = (float)wx + s * SB_PAD;
        float sf_y = (float)wy + s * SB_SEARCH_Y;
        float sf_w = sb_w - s * SB_PAD * 2.0f;
        float sf_h = s * SB_SEARCH_H;

        if (in_box(px, py, sf_x, sf_y, sf_w, sf_h))
            return FINNSETTINGS_HIT_SEARCH;

        sidebar_layout_item items[20];
        int n_items = get_sidebar_layout(items, 20);
        for (int k = 0; k < n_items; k++) {
            float ry = (float)wy + s * items[k].y_ref;
            float rh = s * SB_ROW_H;
            if (in_box(px, py, sf_x, ry, sf_w, rh)) {
                if (out_sub) *out_sub = items[k].page_idx;
                return FINNSETTINGS_HIT_ROW;
            }
        }
        return FINNSETTINGS_HIT_BODY;
    }

    /* The content titlebar drag strip, left of the window options pill. */
    if (py < wy + rs(28.0f, s) && px < wx + ww - rs(90.0f, s))
        return FINNSETTINGS_HIT_TITLEBAR;

    float bx, by, bw, bh;

    if (g_active_page == 0) {
        toggle_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_WIFI_TOGGLE;
    } else if (g_active_page == 1) {
        toggle_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_BT_TOGGLE;
    } else if (g_active_page == 3 || g_active_page == 4) {
        appearance_rect(wx, wy, ww, s, 0, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_LIGHT_THEME;
        appearance_rect(wx, wy, ww, s, 1, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_DARK_THEME;
    } else if (g_active_page == 5) {
        volume_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_VOL_SLIDER;
        mute_rect(wx, wy, ww, s, &bx, &by, &bw, &bh);
        if (in_box(px, py, bx, by, bw, bh)) return FINNSETTINGS_HIT_MUTE_TOGGLE;
    } else if (g_active_page == PAGE_UPDATE) {
        float cx, cw;
        content_box(wx, ww, s, &cx, &cw);

        ota_state v;
        ota_snapshot(&v);
        int mask = ota_action_mask(&v);
        float xs[4], ws[4];
        int ids[4], n = 0;
        ota_actions_layout(cx, cw, s, mask, xs, ws, ids, &n);

        float card_y = (float)wy + s * CARD_TOP + s * OTA_STATUS_H + s * CARD_GAP;
        for (int k = 0; k < n; k++) {
            if (in_box(px, py, xs[k], card_y + s * 34.0f, ws[k], s * 28.0f))
                return ids[k];
        }
    }

    return FINNSETTINGS_HIT_BODY;
}

void finnsettingsapp_press(int hit, int sub, int px, int py, float scale,
                           const finn_state *st) {
    (void)py;
    float s = ui_scale(scale);

    if (hit == FINNSETTINGS_HIT_SEARCH) {
        g_search_focused = 1;
        g_caret_timer = 0.0f;
        g_caret_visible = 1;
        return;
    }

    /* A click anywhere else takes the caret away from the search field. */
    g_search_focused = 0;

    if (hit == FINNSETTINGS_HIT_ROW) {
        if (sub >= 0 && sub < PAGE_COUNT) g_active_page = sub;
        return;
    }

    if (hit == FINNSETTINGS_HIT_WIFI_TOGGLE) {
        if (st->wifi >= 0) finn_state_set_wifi(st->wifi == 0 ? 1 : 0);
        return;
    }

    if (hit == FINNSETTINGS_HIT_BT_TOGGLE) {
        if (st->bluetooth >= 0) finn_state_set_bluetooth(st->bluetooth == 0 ? 1 : 0);
        return;
    }

    if (hit == FINNSETTINGS_HIT_LIGHT_THEME) { finn_state_set_dark(0); return; }
    if (hit == FINNSETTINGS_HIT_DARK_THEME)  { finn_state_set_dark(1); return; }

    if (hit == FINNSETTINGS_HIT_VOL_SLIDER) {
        float bx, by, bw, bh;
        volume_rect(g_cached_wx, g_cached_wy, g_cached_ww, s, &bx, &by, &bw, &bh);
        float rel = (float)px - bx;
        int pct = (int)lroundf((rel / bw) * 100.0f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        finn_state_set_volume(pct);
        g_dragging_slider = 1;
        return;
    }

    if (hit == FINNSETTINGS_HIT_MUTE_TOGGLE) {
        if (st->volume > 0) {
            g_prev_volume = st->volume;
            finn_state_set_volume(0);
        } else {
            finn_state_set_volume(g_prev_volume > 0 ? g_prev_volume : 50);
        }
        return;
    }

    if (hit == FINNSETTINGS_HIT_OTA_CHECK)    { ota_enqueue(OTA_OP_CHECK, 1); return; }
    if (hit == FINNSETTINGS_HIT_OTA_DOWNLOAD) { ota_enqueue(OTA_OP_DOWNLOAD, 1); return; }
    if (hit == FINNSETTINGS_HIT_OTA_INSTALL)  { ota_enqueue(OTA_OP_INSTALL, 1); return; }
    if (hit == FINNSETTINGS_HIT_OTA_ROLLBACK) { ota_enqueue(OTA_OP_ROLLBACK, 1); return; }
}

int finnsettingsapp_drag(int px, int py, float scale, const finn_state *st) {
    (void)py; (void)st;
    if (!g_dragging_slider) return 0;

    float s = ui_scale(scale);
    float bx, by, bw, bh;
    volume_rect(g_cached_wx, g_cached_wy, g_cached_ww, s, &bx, &by, &bw, &bh);

    float rel = (float)px - bx;
    int pct = (int)lroundf((rel / bw) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    if (g_dragging_slider == 1) {
        finn_state_set_volume(pct);
        return 1;
    }
    return 0;
}

void finnsettingsapp_release(void) {
    g_dragging_slider = 0;
}

int finnsettingsapp_scroll(int wx, int wy, int ww, int wh, int px, int py,
                           int clicks, float scale, const finn_state *st) {
    if (!clicks || !st || st->volume < 0) return 0;
    int sub = 0;
    int hit = finnsettingsapp_hit(wx, wy, ww, wh, px, py, scale, &sub);
    if (hit == FINNSETTINGS_HIT_VOL_SLIDER) {
        int pct = st->volume + clicks * 5;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        return finn_state_set_volume(pct) == 0;
    }
    return 0;
}

/* ---------------- keyboard input for search field ---------------- */

int finnsettingsapp_key(const finn_key_event *e) {
    if (!g_search_focused) return 0;
    if (!e->pressed) return 1; /* consume key releases while focused */

    if (e->code == KEY_ESC) {
        if (g_search_text[0]) g_search_text[0] = '\0';
        else                  g_search_focused = 0;
        return 1;
    }

    if (e->code == KEY_BACKSPACE) {
        size_t len = strlen(g_search_text);
        if (len > 0) {
            g_search_text[len - 1] = '\0';
            g_caret_timer = 0.0f;
            g_caret_visible = 1;
        }
        return 1;
    }

    if ((e->mods & FINN_MOD_CTRL) && (e->code == KEY_A)) {
        g_search_text[0] = '\0';
        g_caret_timer = 0.0f;
        g_caret_visible = 1;
        return 1;
    }

    if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
        g_search_focused = 0;
        return 1;
    }

    if (e->keysym >= 32 && e->keysym <= 126) {
        size_t len = strlen(g_search_text);
        if (len + 1 < sizeof(g_search_text)) {
            g_search_text[len] = (char)e->keysym;
            g_search_text[len + 1] = '\0';
            g_caret_timer = 0.0f;
            g_caret_visible = 1;
        }
        return 1;
    }

    return 1;
}

/* ---------------- control plane navigation verbs ---------------- */

int finnsettingsapp_set_page(const char *name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (strcasecmp(name, g_pages[i].id) == 0 ||
            strcasecmp(name, g_pages[i].title) == 0) {
            g_active_page = i;
            return 0;
        }
    }
    return -1;
}

const char *finnsettingsapp_get_page(void) {
    if (g_active_page >= 0 && g_active_page < PAGE_COUNT)
        return g_pages[g_active_page].id;
    return "wifi";
}

int finnsettingsapp_page_count(void) { return PAGE_COUNT; }

int finnsettingsapp_page_info(int index, finn_settings_page_info *out) {
    if (!out || index < 0 || index >= PAGE_COUNT) return -1;
    out->id = g_pages[index].id;
    out->title = g_pages[index].title;
    out->section = g_pages[index].section;
    out->keywords = g_pages[index].keywords;
    out->in_navigation = g_pages[index].visible;
    return 0;
}
