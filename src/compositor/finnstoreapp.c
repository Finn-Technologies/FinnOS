/*
 * finnstoreapp — the Finn Store view.
 *
 * The Store is five pages under the Settings chrome: Home, Native Apps,
 * Linux Apps, Windows Apps and Library. Each page is one source, and the
 * source is named in the navigation rather than hidden behind a filter, so
 * a package never appears without the place it came from.
 *
 * The rules the view keeps:
 *
 *   - The native catalogue is the OS registry. An app the registry does not
 *     know is not offered, and an entry whose launcher is missing is shown
 *     as unavailable with the reason, never as a button that would fail.
 *
 *   - Installed is the package manager's answer. Native state comes from
 *     finnpkg's install list, Flatpak state from flatpak.list, and Windows
 *     state from wine.list. Nothing is called installed because an install
 *     job exited.
 *
 *   - Every package change and every launch goes through finnd. This view
 *     never edits package state or starts a process itself.
 *
 * Linux Apps uses flatpak.list/search/install/remove/run. Windows Apps
 * searches winget through finnd and installs through the audited job path;
 * the installer itself is never run by the compositor. Library combines the
 * three installed sets and launches each through the verb that owns it.
 *
 * Network and package work runs off the compositor thread. The Store draws
 * what the backends said and says why when they said nothing.
 */
#define _GNU_SOURCE
#include "finnstoreapp.h"
#include "finnappui.h"
#include "finndraw.h"
#include "finntext.h"
#include "finnapps.h"
#include "finnsock.h"
#include "finnproto.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

/* ---------------- metrics, in reference units ---------------- */

/*
 * The chrome is Settings' chrome. The Store is a sibling window, not a
 * different operating system: the same 250-unit sheet, the same 16-unit
 * margin, the title on the same baseline and the search field directly under
 * it. What changes is only what the rail contains.
 */
#define PAD             32.0f   /* content column, as in Settings */
#define NARROW_PAD      20.0f
#define RAIL_W         250.0f
#define RAIL_PAD        16.0f   /* inside the sheet */
#define RAIL_MIN       560.0f   /* narrower than this: tabs over the content */
#define RAIL_MIN_H     380.0f   /* too short for a rail: use tabs instead */

#define CONTENT_MAX    780.0f   /* the point where a maximised column stops */
#define TITLE_TOP       60.0f   /* content header title */
#define SUB_TOP         88.0f   /* content header subtitle */
#define FILTER_W       218.0f   /* rail width less its two margins */
#define FILTER_H        30.0f
#define CONTENT_TOP    104.0f

#define NAV_FIRST_Y    148.0f
#define NAV_ITEM_H      30.0f
#define NAV_ROW_STEP    33.0f
#define NAV_SECTION_GAP  6.0f
#define NAV_SECTION_TOP 10.0f
#define TAB_H           30.0f
#define TAB_GAP          6.0f

#define CARD_R           8.0f
#define CARD_PAD        18.0f
#define CARD_GAP        12.0f
#define HERO_H         132.0f
#define SHELF_CARD_H   128.0f
#define GRID_CARD_H    176.0f
#define WIN_ROW_H      116.0f
#define LIB_ROW_H       68.0f
#define HOME_WELL_H     78.0f
#define LABEL_H         18.0f
#define SECTION_GAP     10.0f

#define HERO_GAP        10.0f
#define CARD_TOP_GAP     5.0f
#define GRID_MIN_W     185.0f
#define SHELF_MIN_W    140.0f

#define ACTION_H        34.0f
#define ACTION_MIN_W    62.0f
#define ACTION_QUIET_W  64.0f

#define DETAIL_BACK_H   28.0f
#define DETAIL_BACK_W   68.0f
#define DETAIL_ICON_W   84.0f
#define DETAIL_ICON_N   64.0f
#define DETAIL_ACTION_W 104.0f

#define LIST_LABEL_H    24.0f
#define LIST_NOTE_H     16.0f
#define LIST_GAP         8.0f
#define REFRESH_W       76.0f

typedef struct {
    fsvg svg;
    int  loaded;
    char id[32];
} store_icon;

typedef struct {
    uint32_t *px;
    int w, h;
    int ox, oy;      /* screen coordinate of pixel 0,0 (frost samples this) */
    float dx, dy;    /* added to every draw coordinate: the rail and header
                        * draw in window coordinates, the page content draws
                        * relative to the content column and is shifted here */
} store_canvas;

enum { STORE_PAGE_HOME = 0, STORE_PAGE_NATIVE, STORE_PAGE_LINUX,
       STORE_PAGE_WINDOWS, STORE_PAGE_LIBRARY, STORE_PAGE_COUNT };
enum { BTN_QUIET = 0, BTN_PRIMARY, BTN_REMOVE, BTN_REMOVE_CONFIRM,
       BTN_DISABLED };
enum { SEL_NONE = 0, SEL_NATIVE, SEL_FLATPAK, SEL_WINE, SEL_WINGET };
/* Library rows carry the same kind as the selection, so one value can name
 * the source of a row and the verb that opens it. */
enum { LIB_NATIVE = SEL_NATIVE, LIB_FLATPAK = SEL_FLATPAK, LIB_WINE = SEL_WINE };

#define FLATPAK_APP_MAX 128
#define FLATPAK_APP_ID_CAP (FINN_FLATPAK_ID_MAX + 1)
#define WINE_APP_MAX 128
#define WINE_APP_ID_CAP 33
#define FLATPAK_APP_REMOTE_CAP 65
#define FLATPAK_APP_NAME_CAP 128
#define FLATPAK_APP_NOTE_CAP 256
#define FLATPAK_JOB_ID_CAP FINN_FLATPAK_JOB_ID_MAX

#define WINGET_RESULT_MAX 64
#define WINGET_INFO_MAX 32
#define WINGET_ID_CAP (FINN_WINGET_ID_MAX + 1)
#define WINGET_NAME_CAP 160
#define WINGET_TEXT_CAP 256
#define WINGET_DESC_CAP 512
#define WINGET_URL_CAP 512
#define WINGET_JOB_ID_CAP FINN_WINGET_JOB_ID_MAX

typedef struct {
    char id[FLATPAK_APP_ID_CAP];
    char name[FLATPAK_APP_NAME_CAP];
    char note[FLATPAK_APP_NOTE_CAP];
    char remote[FLATPAK_APP_REMOTE_CAP];
    int installed;
} flatpak_app;

typedef struct {
    char id[WINE_APP_ID_CAP];
} wine_app;

typedef struct {
    int available;             /* -1 until wine.status has answered */
    int loaded;
    int failed;                /* the status could not be read at all */
    int list_failed;
    char error[WINGET_TEXT_CAP];
    char arch[32];
    char backend[64];
    char version[FINN_WINE_VERSION_MAX];
    char prefix[WINGET_URL_CAP];
    char reason[WINGET_TEXT_CAP];
    wine_app apps[WINE_APP_MAX];
    int count;
} wine_state;

typedef struct {
    char id[WINGET_ID_CAP];
    char name[WINGET_NAME_CAP];
    char version[64];
    char publisher[128];
    char installer_type[64];
} winget_result;

typedef struct {
    char id[WINGET_ID_CAP];
    int present;
    int failed;
    char error[192];
    char name[WINGET_NAME_CAP];
    char version[64];
    char publisher[128];
    char description[WINGET_DESC_CAP];
    char homepage[WINGET_URL_CAP];
    char installer_type[64];
    char installer_url[WINGET_URL_CAP];
    char installer_sha256[65];
    char installer_arch[32];
    char size[32];
} winget_info;

typedef struct {
    int kind;
    int index;
} library_entry;

/* ---------------- worker tasks ---------------- */

typedef struct {
    char query[FINN_FLATPAK_QUERY_MAX + 1];
    flatpak_app installed[FLATPAK_APP_MAX];
    int installed_count;
    flatpak_app results[FLATPAK_APP_MAX];
    int results_count;
    int failed;
    char error[WINGET_TEXT_CAP];
    unsigned generation;
    int done;                  /* published under g_task_mutex */
} flatpak_task;

typedef struct {
    wine_state state;
    unsigned generation;
    int done;                  /* published under g_task_mutex */
} wine_task;

typedef struct {
    char query[FINN_WINGET_QUERY_MAX + 1];
    winget_result results[WINGET_RESULT_MAX];
    int count;
    int failed;
    char error[WINGET_TEXT_CAP];
    unsigned generation;
    int done;
} winget_search_task;

typedef struct {
    char id[WINGET_ID_CAP];
    winget_info info;
    int failed;
    char error[192];
    unsigned generation;
    int done;
} winget_info_task;

typedef struct {
    char job[WINGET_JOB_ID_CAP];
    unsigned generation;
    int request_failed;
    char error[192];
    char state[16];
    int done;
    /* A completed installer has not registered an app; the same worker
     * re-reads the Wine registry so the Library can say what is true. */
    int wine_read;
    wine_state wine;
} winget_job_task;

/* ---------------- state ---------------- */

static char g_font_dir[512] = "";
static char g_icon_dir[512] = "";
static char g_registry[512] = "";

static finn_font g_f_hero, g_f_title, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static fsvg g_ic_home, g_ic_store, g_ic_grid, g_ic_windows, g_ic_library;
static fsvg g_ic_search, g_ic_clear, g_ic_back, g_ic_next, g_ic_refresh;
static int g_ready = 0;
static unsigned g_store_generation = 1;

static store_icon g_icons[FINN_APP_MAX];

static char g_filter[FINN_WINGET_QUERY_MAX + 1] = "";
static int  g_filter_focused = 0;
static int  g_page = STORE_PAGE_HOME;
static int  g_detail = 0;
static int  g_sel_kind = SEL_NONE;
static char g_sel_id[FLATPAK_APP_ID_CAP] = "";
static char g_confirm_id[FLATPAK_APP_ID_CAP] = "";
static int  g_scroll[STORE_PAGE_COUNT] = { 0, 0, 0, 0, 0 };
static int  g_detail_scroll = 0;
static char g_status[512] = "";
static int  g_status_error = 0;
static int  g_load_error = 0;
static unsigned g_rev = 1;
static int  g_reveal_pending = 0;
static int  g_last_cols = 1;

static uint32_t *g_scratch = NULL;
static size_t g_scratch_cap = 0;

static pthread_mutex_t g_task_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Flatpak: the installed set and the last search are kept apart, so a
 * search that is still in flight cannot make Library forget what is on the
 * machine. */
static flatpak_app g_flatpak_installed[FLATPAK_APP_MAX];
static int g_flatpak_installed_count = 0;
static flatpak_app g_flatpak_results[FLATPAK_APP_MAX];
static int g_flatpak_results_count = 0;
static int g_flatpak_search_active = 0;
static int g_flatpak_available = -1;
static int g_flatpak_loaded = 0;
static int g_flatpak_error = 0;
static char g_flatpak_error_text[WINGET_TEXT_CAP] = "";
static char g_flatpak_query[FINN_FLATPAK_QUERY_MAX + 1] = "";
static char g_flatpak_job_id[FLATPAK_JOB_ID_CAP] = "";
static char g_flatpak_job_app[FLATPAK_APP_ID_CAP] = "";
static char g_flatpak_job_name[FLATPAK_APP_NAME_CAP] = "";
static int g_flatpak_job_install = 0;

static flatpak_task g_flatpak_task;
static pthread_t g_flatpak_thread;
static int g_flatpak_active = 0;
static int g_flatpak_pending = 0;
static char g_flatpak_pending_query[FINN_FLATPAK_QUERY_MAX + 1] = "";

/* Windows: one status read feeds the page's caveat and the install button,
 * one list read feeds Library. Both come from the control plane; the
 * compositor never guesses that Wine is present because a binary is in
 * PATH. */
static wine_state g_wine;
static wine_task g_wine_task;
static pthread_t g_wine_thread;
static int g_wine_active = 0;
static int g_wine_pending = 0;

/* winget search results belong to the query that produced them. */
static winget_result g_winget_results[WINGET_RESULT_MAX];
static int g_winget_count = 0;
static char g_winget_query[FINN_WINGET_QUERY_MAX + 1] = "";
static int g_winget_search_active = 0;
static int g_winget_loaded = 0;
static int g_winget_error = 0;
static char g_winget_error_text[WINGET_TEXT_CAP] = "";
static winget_search_task g_winget_search_task;
static pthread_t g_winget_search_thread;
static int g_winget_search_running = 0;
static int g_winget_search_pending = 0;
static char g_winget_pending_query[FINN_WINGET_QUERY_MAX + 1] = "";

static winget_info g_winget_info[WINGET_INFO_MAX];
static int g_winget_info_next = 0;
static winget_info_task g_winget_info_task;
static pthread_t g_winget_info_thread;
static int g_winget_info_running = 0;
static int g_winget_info_pending = 0;
static char g_winget_info_pending_id[WINGET_ID_CAP] = "";

static winget_job_task g_winget_job_task;
static pthread_t g_winget_job_thread;
static int g_winget_job_running = 0;
static char g_winget_job_id[WINGET_JOB_ID_CAP] = "";
static char g_winget_job_app[WINGET_ID_CAP] = "";
static char g_winget_job_name[WINGET_NAME_CAP] = "";
static char g_winget_job_state[16] = "";
static long long g_winget_job_last_ms = 0;

static library_entry g_library[FINN_APP_MAX + FLATPAK_APP_MAX + WINE_APP_MAX];
static int g_library_count = 0;

static void bump(void) { g_rev++; }

static void ensure_selection(void);

static uint32_t with_alpha(uint32_t c, unsigned a) {
    return (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
}

static long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int load_fonts(float scale) {
    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    float s = scale * FINN_APP_REF;
    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    /* Settings' scale, exactly: 22 for the title and a product name, 15 for
     * a card's name, 13 for rows and buttons, 12 for descriptions and 11.5
     * for the smallest captions. A Store one size smaller than the rest of
     * the shell reads as a web page embedded in the OS. */
    if (finn_font_load(&g_f_hero, p, finnapp_font_px(s, 22.0f)) != 0) return -1;
    if (finn_font_load(&g_f_title, p, finnapp_font_px(s, 15.0f)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, finnapp_font_px(s, 13.0f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, finnapp_font_px(s, 12.0f)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, finnapp_font_px(s, 11.5f)) != 0) return -1;
    return 0;
}

static void set_status(const char *text, int error) {
    snprintf(g_status, sizeof g_status, "%s", text ? text : "");
    g_status_error = error ? 1 : 0;
    bump();
}

/* ---------------- native catalogue ---------------- */

static int find_index(const char *id) {
    if (!id || !*id) return -1;
    int n = finn_apps_count();
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (a && !strcmp(a->id, id)) return i;
    }
    return -1;
}

/*
 * A catalogue entry is usable only when its launcher is actually executable.
 * finnpkg refuses to mark an app installed without one, and a Store button
 * that only produces that refusal would be pretending to be a capability.
 */
static int app_available(const finn_app *a) {
    return a && a->command[0] && access(a->command, X_OK) == 0;
}

static int native_matches(int i) {
    int n = finn_apps_count();
    if (i < 0 || i >= n) return 0;
    if (!g_filter[0]) return 1;
    const finn_app *a = finn_apps_at(i);
    if (!a) return 0;
    return strcasestr(a->name, g_filter) != NULL ||
           strcasestr(a->id, g_filter) != NULL ||
           strcasestr(a->note, g_filter) != NULL;
}

static int native_visible_count(void) {
    int n = finn_apps_count(), k = 0;
    for (int i = 0; i < n; i++) if (native_matches(i)) k++;
    return k;
}

static int native_visible_at(int k) {
    int n = finn_apps_count();
    if (k < 0) return -1;
    for (int i = 0; i < n; i++) {
        if (!native_matches(i)) continue;
        if (k-- == 0) return i;
    }
    return -1;
}

static int native_visible_index_of(int i) {
    if (i < 0) return -1;
    int n = finn_apps_count(), k = 0;
    for (int j = 0; j < n; j++) {
        if (!native_matches(j)) continue;
        if (j == i) return k;
        k++;
    }
    return -1;
}

static int native_installed_visible_count(void) {
    int n = finn_apps_count(), k = 0;
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (a && a->installed && native_matches(i)) k++;
    }
    return k;
}

static int native_installed_visible_at(int k) {
    int n = finn_apps_count();
    if (k < 0) return -1;
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a || !a->installed || !native_matches(i)) continue;
        if (k-- == 0) return i;
    }
    return -1;
}

static int native_installed_visible_index_of(int i) {
    if (i < 0) return -1;
    int n = finn_apps_count(), k = 0;
    for (int j = 0; j < n; j++) {
        const finn_app *a = finn_apps_at(j);
        if (!a || !a->installed || !native_matches(j)) continue;
        if (j == i) return k;
        k++;
    }
    return -1;
}

/* ---------------- Flatpak catalogue ---------------- */

static int flatpak_installed_index(const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g_flatpak_installed_count; i++)
        if (!strcmp(g_flatpak_installed[i].id, id)) return i;
    return -1;
}

static int flatpak_result_index(const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g_flatpak_results_count; i++)
        if (!strcmp(g_flatpak_results[i].id, id)) return i;
    return -1;
}

static int flatpak_matches(const flatpak_app *a) {
    if (!a) return 0;
    if (!g_filter[0]) return 1;
    return strcasestr(a->name, g_filter) != NULL ||
           strcasestr(a->id, g_filter) != NULL ||
           strcasestr(a->note, g_filter) != NULL ||
           strcasestr(a->remote, g_filter) != NULL;
}

/* Linux Apps shows search results when a search is current, and the
 * installed set otherwise. A query that no longer matches the field is not
 * shown, because a result list for another word is a lie about this one. */
static int flatpak_search_current(void) {
    return g_flatpak_search_active && g_flatpak_query[0] &&
           !strcmp(g_flatpak_query, g_filter);
}

static int flatpak_linux_count(void) {
    if (flatpak_search_current()) {
        int k = 0;
        for (int i = 0; i < g_flatpak_results_count; i++)
            if (flatpak_matches(&g_flatpak_results[i])) k++;
        return k;
    }
    int k = 0;
    for (int i = 0; i < g_flatpak_installed_count; i++)
        if (flatpak_matches(&g_flatpak_installed[i])) k++;
    return k;
}

static int flatpak_linux_at(int ordinal) {
    if (ordinal < 0) return -1;
    if (flatpak_search_current()) {
        for (int i = 0; i < g_flatpak_results_count; i++) {
            if (!flatpak_matches(&g_flatpak_results[i])) continue;
            if (ordinal-- == 0) return i;
        }
        return -1;
    }
    for (int i = 0; i < g_flatpak_installed_count; i++) {
        if (!flatpak_matches(&g_flatpak_installed[i])) continue;
        if (ordinal-- == 0) return i;
    }
    return -1;
}

static int flatpak_linux_index_of(int index) {
    if (index < 0) return -1;
    int ordinal = 0;
    if (flatpak_search_current()) {
        if (index >= g_flatpak_results_count) return -1;
        for (int i = 0; i < g_flatpak_results_count; i++) {
            if (!flatpak_matches(&g_flatpak_results[i])) continue;
            if (i == index) return ordinal;
            ordinal++;
        }
        return -1;
    }
    if (index >= g_flatpak_installed_count) return -1;
    for (int i = 0; i < g_flatpak_installed_count; i++) {
        if (!flatpak_matches(&g_flatpak_installed[i])) continue;
        if (i == index) return ordinal;
        ordinal++;
    }
    return -1;
}

static const flatpak_app *flatpak_linux_item(int index) {
    if (index < 0) return NULL;
    if (flatpak_search_current())
        return index < g_flatpak_results_count ? &g_flatpak_results[index] : NULL;
    return index < g_flatpak_installed_count ? &g_flatpak_installed[index] : NULL;
}

static void flatpak_copy_field(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = 0;
    while (src && *src && n + 1 < cap) {
        unsigned char c = (unsigned char)*src++;
        dst[n++] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
    }
    while (n && dst[n - 1] == ' ') n--;
    dst[n] = '\0';
}

static void flatpak_first_remote(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    dst[0] = '\0';
    while (src && (*src == ' ' || *src == '\t')) src++;
    size_t n = 0;
    while (src && *src && *src != ',' && *src != ';' &&
           *src != ' ' && *src != '\t' && n + 1 < cap)
        dst[n++] = *src++;
    dst[n] = '\0';
}

static int flatpak_parse_rows(const char *payload, int descriptions,
                              flatpak_app *apps, int cap) {
    if (!payload || !apps || cap <= 0) return 0;
    char *copy = strdup(payload);
    if (!copy) return 0;
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = '\0';
        char *name = strchr(line, '\t');
        if (!name) continue;
        *name++ = '\0';
        char *note = descriptions ? strchr(name, '\t') : NULL;
        char *remote = NULL;
        if (note) {
            *note++ = '\0';
            remote = strchr(note, '\t');
            if (remote) *remote++ = '\0';
        }
        if (!strchr(line, '.') || !*name || count >= cap) continue;
        flatpak_app *a = &apps[count];
        memset(a, 0, sizeof *a);
        flatpak_copy_field(a->id, sizeof a->id, line);
        flatpak_copy_field(a->name, sizeof a->name, name);
        flatpak_copy_field(a->note, sizeof a->note, note ? note : "");
        flatpak_first_remote(a->remote, sizeof a->remote, remote);
        if (!a->id[0]) continue;
        count++;
    }
    free(copy);
    return count;
}

/* ---------------- control-plane plumbing ---------------- */

static int control_binary_request(const char *request, char **payload,
                                  size_t *payload_len, char *error,
                                  size_t error_cap) {
    if (!request || !payload || !payload_len) return -1;
    *payload = NULL;
    *payload_len = 0;
    int fd = finn_connect(FINN_CONTROL_SOCK);
    if (fd < 0) {
        snprintf(error, error_cap, "The control plane is not reachable.");
        return -1;
    }

    struct timeval timeout = { 20, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    if (finn_write_str(fd, request) != 0 || finn_write_str(fd, "\n") != 0) {
        snprintf(error, error_cap, "The control plane request could not be sent.");
        close(fd);
        return -1;
    }

    char reply[256];
    int n = finn_read_line(fd, reply, sizeof reply);
    if (n <= 0) {
        snprintf(error, error_cap, "The control plane did not answer.");
        close(fd);
        return -1;
    }
    if (!strncmp(reply, "ERR ", 4)) {
        snprintf(error, error_cap, "%s", reply + 4);
        close(fd);
        return -1;
    }
    if (strncmp(reply, "OK BINARY ", 10)) {
        snprintf(error, error_cap, "The control plane returned an unexpected reply.");
        close(fd);
        return -1;
    }

    char *end = NULL;
    unsigned long long length = strtoull(reply + 10, &end, 10);
    if (!end || end == reply + 10 || *end || length > 1024u * 1024u) {
        snprintf(error, error_cap, "The reply size is invalid.");
        close(fd);
        return -1;
    }

    char *data = malloc((size_t)length + 1);
    if (!data) {
        snprintf(error, error_cap, "The reply could not be allocated.");
        close(fd);
        return -1;
    }
    size_t used = 0;
    while (used < (size_t)length) {
        ssize_t got = read(fd, data + used, (size_t)length - used);
        if (got > 0) used += (size_t)got;
        else if (got < 0 && errno == EINTR) continue;
        else break;
    }
    close(fd);
    if (used != (size_t)length || memchr(data, '\0', used)) {
        free(data);
        snprintf(error, error_cap, "The reply was truncated or malformed.");
        return -1;
    }
    data[used] = '\0';
    *payload = data;
    *payload_len = used;
    if (error && error_cap) error[0] = '\0';
    return 0;
}

static int control_text_request(const char *request, char *reply, size_t cap) {
    if (!request || !reply || !cap) return -1;
    return finn_call(FINN_CONTROL_SOCK, request, reply, (int)cap);
}

static void task_done(int *done) {
    pthread_mutex_lock(&g_task_mutex);
    *done = 1;
    pthread_mutex_unlock(&g_task_mutex);
}

static int task_is_done(int *done) {
    pthread_mutex_lock(&g_task_mutex);
    int v = *done;
    pthread_mutex_unlock(&g_task_mutex);
    return v;
}

/* ---------------- Flatpak source ---------------- */

static void *flatpak_worker(void *arg) {
    flatpak_task *task = arg;
    char *installed_text = NULL;
    size_t installed_len = 0;
    char error[WINGET_TEXT_CAP] = "";

    if (control_binary_request("flatpak.list", &installed_text, &installed_len,
                               error, sizeof error) != 0) {
        task->failed = 1;
        snprintf(task->error, sizeof task->error, "%s", error);
        goto complete;
    }
    (void)installed_len;
    task->installed_count = flatpak_parse_rows(installed_text, 0,
                                               task->installed,
                                               FLATPAK_APP_MAX);
    for (int i = 0; i < task->installed_count; i++)
        task->installed[i].installed = 1;
    free(installed_text);
    installed_text = NULL;

    if (!task->query[0]) goto complete;

    {
        char request[FINN_FLATPAK_QUERY_MAX + 32];
        int n = snprintf(request, sizeof request, "flatpak.search %s", task->query);
        if (n <= 0 || (size_t)n >= sizeof request) {
            task->failed = 1;
            snprintf(task->error, sizeof task->error,
                     "The Flatpak search text is too long.");
            goto complete;
        }

        char *search_text = NULL;
        size_t search_len = 0;
        if (control_binary_request(request, &search_text, &search_len,
                                   error, sizeof error) != 0) {
            task->failed = 1;
            snprintf(task->error, sizeof task->error, "%s", error);
            goto complete;
        }
        (void)search_len;
        task->results_count = flatpak_parse_rows(search_text, 1, task->results,
                                                 FLATPAK_APP_MAX);
        free(search_text);
        for (int i = 0; i < task->results_count; i++) {
            for (int j = 0; j < task->installed_count; j++) {
                if (!strcmp(task->results[i].id, task->installed[j].id)) {
                    task->results[i].installed = 1;
                    break;
                }
            }
        }
    }

complete:
    free(installed_text);
    task_done(&task->done);
    return NULL;
}

static void flatpak_apply_failure(const char *message) {
    g_flatpak_available = 0;
    g_flatpak_loaded = 1;
    g_flatpak_error = 1;
    g_flatpak_installed_count = 0;
    g_flatpak_results_count = 0;
    g_flatpak_search_active = 0;
    snprintf(g_flatpak_error_text, sizeof g_flatpak_error_text, "%s",
             message && *message ? message : "Flatpak is unavailable.");
    ensure_selection();
}

static void flatpak_query_start(const char *query);

static int flatpak_status_is_progress(void) {
    return !strncmp(g_status, "Loading installed Flatpak", 25) ||
           !strncmp(g_status, "Searching Flatpak", 17) ||
           !strncmp(g_status, "Waiting for the current Flatpak", 30);
}

static void flatpak_query_poll(void) {
    if (!g_flatpak_active) return;
    if (!task_is_done(&g_flatpak_task.done)) return;

    pthread_join(g_flatpak_thread, NULL);
    g_flatpak_active = 0;
    char finished_query[FINN_FLATPAK_QUERY_MAX + 1];
    snprintf(finished_query, sizeof finished_query, "%s", g_flatpak_task.query);

    if (g_flatpak_task.generation == g_store_generation && g_ready) {
        if (g_flatpak_task.failed) {
            flatpak_apply_failure(g_flatpak_task.error);
            set_status(g_flatpak_task.error[0] ? g_flatpak_task.error
                                              : "Flatpak could not be read.", 1);
        } else {
            memcpy(g_flatpak_installed, g_flatpak_task.installed,
                   sizeof g_flatpak_installed);
            g_flatpak_installed_count = g_flatpak_task.installed_count;
            g_flatpak_available = 1;
            g_flatpak_loaded = 1;
            g_flatpak_error = 0;
            g_flatpak_error_text[0] = '\0';
            if (finished_query[0] && !strcmp(finished_query, g_filter) &&
                g_page == STORE_PAGE_LINUX) {
                memcpy(g_flatpak_results, g_flatpak_task.results,
                       sizeof g_flatpak_results);
                g_flatpak_results_count = g_flatpak_task.results_count;
                g_flatpak_search_active = 1;
                snprintf(g_flatpak_query, sizeof g_flatpak_query, "%s",
                         finished_query);
            } else if (!finished_query[0]) {
                g_flatpak_results_count = 0;
                g_flatpak_search_active = 0;
                g_flatpak_query[0] = '\0';
            }
            if (flatpak_status_is_progress()) {
                g_status[0] = '\0';
                g_status_error = 0;
            }
            ensure_selection();
            bump();
        }
    }

    int pending = g_flatpak_pending;
    char next_query[FINN_FLATPAK_QUERY_MAX + 1];
    snprintf(next_query, sizeof next_query, "%s", g_flatpak_pending_query);
    g_flatpak_pending = 0;
    g_flatpak_pending_query[0] = '\0';
    if (pending && g_ready && g_page == STORE_PAGE_LINUX &&
        !strcmp(g_filter, next_query))
        flatpak_query_start(next_query);
}

static void flatpak_query_start(const char *query) {
    if (!query) query = "";
    if (g_flatpak_active) {
        if (g_flatpak_task.generation == g_store_generation &&
            !strcmp(query, g_flatpak_task.query)) {
            g_flatpak_pending = 0;
            g_flatpak_pending_query[0] = '\0';
            return;
        }
        snprintf(g_flatpak_pending_query, sizeof g_flatpak_pending_query,
                 "%s", query);
        g_flatpak_pending = 1;
        set_status("Waiting for the current Flatpak request...", 0);
        return;
    }

    memset(&g_flatpak_task, 0, sizeof g_flatpak_task);
    snprintf(g_flatpak_task.query, sizeof g_flatpak_task.query, "%s", query);
    g_flatpak_task.generation = g_store_generation;
    g_flatpak_active = 1;
    int rc = pthread_create(&g_flatpak_thread, NULL, flatpak_worker,
                            &g_flatpak_task);
    if (rc != 0) {
        g_flatpak_active = 0;
        flatpak_apply_failure("Could not start the background Flatpak query.");
        set_status(g_flatpak_error_text, 1);
        return;
    }
    set_status(query[0] ? "Searching Flatpak apps..."
                        : "Loading installed Flatpak apps...", 0);
}

/* ---------------- Wine source ---------------- */

static int wine_id_valid(const char *id) {
    if (!id || id[0] < 'a' || id[0] > 'z') return 0;
    size_t n = 1;
    for (; id[n]; n++) {
        unsigned char c = (unsigned char)id[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }
    return n <= WINE_APP_ID_CAP - 1;
}

static int wine_index(const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g_wine.count; i++)
        if (!strcmp(g_wine.apps[i].id, id)) return i;
    return -1;
}

static int wine_matches(const wine_app *a) {
    if (!a) return 0;
    return !g_filter[0] || strcasestr(a->id, g_filter) != NULL;
}

static int wine_visible_count(void) {
    int k = 0;
    for (int i = 0; i < g_wine.count; i++)
        if (wine_matches(&g_wine.apps[i])) k++;
    return k;
}

static int wine_visible_at(int ordinal) {
    if (ordinal < 0) return -1;
    for (int i = 0; i < g_wine.count; i++) {
        if (!wine_matches(&g_wine.apps[i])) continue;
        if (ordinal-- == 0) return i;
    }
    return -1;
}

static int wine_visible_index_of(int index) {
    if (index < 0 || index >= g_wine.count) return -1;
    int ordinal = 0;
    for (int i = 0; i < g_wine.count; i++) {
        if (!wine_matches(&g_wine.apps[i])) continue;
        if (i == index) return ordinal;
        ordinal++;
    }
    return -1;
}

static int wine_parse_ids(const char *payload, wine_app *apps, int cap) {
    if (!payload || !apps || cap <= 0) return 0;
    char *copy = strdup(payload);
    if (!copy) return 0;
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line && count < cap;
         line = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = '\0';
        char *tab = strchr(line, '\t');
        if (tab) *tab = '\0';
        if (!wine_id_valid(line)) continue;
        int duplicate = 0;
        for (int i = 0; i < count; i++)
            if (!strcmp(apps[i].id, line)) { duplicate = 1; break; }
        if (duplicate) continue;
        snprintf(apps[count++].id, WINE_APP_ID_CAP, "%s", line);
    }
    free(copy);
    return count;
}

/* A key=value field from a one-line status reply. Values are single words in
 * the protocol, so a token scan is enough and cannot be fooled by a prefix
 * that happens to contain the key name. */
static void status_field(const char *reply, const char *key,
                         char *dst, size_t cap) {
    if (!cap) return;
    dst[0] = '\0';
    if (!reply || !key || !*key) return;
    size_t klen = strlen(key);
    const char *p = reply;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *token = p;
        while (*p && *p != ' ') p++;
        size_t tlen = (size_t)(p - token);
        if (tlen > klen && token[klen] == '=' &&
            !strncmp(token, key, klen)) {
            size_t vlen = tlen - klen - 1;
            if (vlen >= cap) vlen = cap - 1;
            memcpy(dst, token + klen + 1, vlen);
            dst[vlen] = '\0';
            return;
        }
    }
}

/*
 * Read the Wine runtime and its registrations as one answer. The status is
 * the authority on availability: a wine binary on PATH is not a runtime,
 * and an install task that exited is not a registration.
 */
static int wine_read_state(wine_state *out, char *error, size_t error_cap) {
    memset(out, 0, sizeof *out);
    out->available = -1;
    out->loaded = 1;
    if (error && error_cap) error[0] = '\0';

    char reply[512];
    if (control_text_request("wine.status", reply, sizeof reply) != 0) {
        out->failed = 1;
        snprintf(out->error, sizeof out->error,
                 "The control plane is not reachable.");
        if (error && error_cap)
            snprintf(error, error_cap, "%s", out->error);
        return -1;
    }
    if (strncmp(reply, "OK wine.status", 14)) {
        out->failed = 1;
        const char *why = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        snprintf(out->error, sizeof out->error, "%s",
                 why[0] ? why : "The Wine runtime status could not be read.");
        if (error && error_cap)
            snprintf(error, error_cap, "%s", out->error);
        return -1;
    }

    status_field(reply, "available", out->reason, sizeof out->reason);
    out->available = !strcmp(out->reason, "1") ? 1 : 0;
    out->reason[0] = '\0';
    status_field(reply, "arch", out->arch, sizeof out->arch);
    status_field(reply, "backend", out->backend, sizeof out->backend);
    status_field(reply, "version", out->version, sizeof out->version);
    status_field(reply, "prefix", out->prefix, sizeof out->prefix);
    status_field(reply, "reason", out->reason, sizeof out->reason);

    if (!out->available) return 0;

    char *payload = NULL;
    size_t payload_len = 0;
    char list_error[WINGET_TEXT_CAP] = "";
    if (control_binary_request("wine.list", &payload, &payload_len,
                               list_error, sizeof list_error) != 0) {
        out->list_failed = 1;
        snprintf(out->error, sizeof out->error, "%s", list_error);
        if (error && error_cap)
            snprintf(error, error_cap, "%s", list_error);
        return 0;
    }
    (void)payload_len;
    out->count = wine_parse_ids(payload, out->apps, WINE_APP_MAX);
    free(payload);
    return 0;
}

static void *wine_worker(void *arg) {
    wine_task *task = arg;
    char error[WINGET_TEXT_CAP] = "";
    wine_read_state(&task->state, error, sizeof error);
    task_done(&task->done);
    return NULL;
}

static void wine_refresh(void);

static int wine_status_is_progress(void) {
    return !strncmp(g_status, "Reading the Wine runtime", 24) ||
           !strncmp(g_status, "Waiting for the current Wine", 28);
}

static void wine_query_poll(void) {
    if (!g_wine_active) return;
    if (!task_is_done(&g_wine_task.done)) return;

    pthread_join(g_wine_thread, NULL);
    g_wine_active = 0;
    if (g_wine_task.generation == g_store_generation && g_ready) {
        g_wine = g_wine_task.state;
        if (wine_status_is_progress()) {
            g_status[0] = '\0';
            g_status_error = 0;
        }
        if (g_wine.failed)
            set_status(g_wine.error[0] ? g_wine.error
                                       : "The Wine runtime could not be read.", 1);
        ensure_selection();
        bump();
    }

    if (g_wine_pending && g_ready) {
        g_wine_pending = 0;
        wine_refresh();
    }
}

static void wine_refresh(void) {
    if (g_wine_active) {
        g_wine_pending = 1;
        set_status("Waiting for the current Wine request...", 0);
        return;
    }
    memset(&g_wine_task, 0, sizeof g_wine_task);
    g_wine_task.generation = g_store_generation;
    g_wine_active = 1;
    int rc = pthread_create(&g_wine_thread, NULL, wine_worker, &g_wine_task);
    if (rc != 0) {
        g_wine_active = 0;
        g_wine.failed = 1;
        g_wine.available = -1;
        snprintf(g_wine.error, sizeof g_wine.error,
                 "Could not start the Wine status request.");
        set_status(g_wine.error, 1);
        return;
    }
    set_status("Reading the Wine runtime and registrations...", 0);
}

/* ---------------- winget source ---------------- */

static int winget_index(const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g_winget_count; i++)
        if (!strcmp(g_winget_results[i].id, id)) return i;
    return -1;
}

static int winget_results_visible(void) {
    return g_winget_search_active && g_winget_query[0] &&
           !strcmp(g_winget_query, g_filter);
}

static int winget_parse_results(const char *payload, winget_result *out,
                                int cap) {
    if (!payload || !out || cap <= 0) return 0;
    char *copy = strdup(payload);
    if (!copy) return 0;
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line && count < cap;
         line = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = '\0';
        char *f[5] = { line, NULL, NULL, NULL, NULL };
        for (int i = 1; i < 5; i++) {
            char *tab = strchr(f[i - 1], '\t');
            if (!tab) break;
            *tab = '\0';
            f[i] = tab + 1;
        }
        if (!f[0] || !f[0][0] || !f[1] || !f[1][0]) continue;
        winget_result *r = &out[count];
        memset(r, 0, sizeof *r);
        flatpak_copy_field(r->id, sizeof r->id, f[0]);
        flatpak_copy_field(r->name, sizeof r->name, f[1]);
        if (f[2]) flatpak_copy_field(r->version, sizeof r->version, f[2]);
        if (f[3]) flatpak_copy_field(r->publisher, sizeof r->publisher, f[3]);
        if (f[4])
            flatpak_copy_field(r->installer_type, sizeof r->installer_type,
                               f[4]);
        if (!r->id[0]) continue;
        count++;
    }
    free(copy);
    return count;
}

static void *winget_search_worker(void *arg) {
    winget_search_task *task = arg;
    char request[FINN_WINGET_QUERY_MAX + 32];
    int n = snprintf(request, sizeof request, "winget.search %s", task->query);
    if (n <= 0 || (size_t)n >= sizeof request) {
        task->failed = 1;
        snprintf(task->error, sizeof task->error,
                 "The winget search text is too long.");
        task_done(&task->done);
        return NULL;
    }

    char *payload = NULL;
    size_t payload_len = 0;
    char error[WINGET_TEXT_CAP] = "";
    if (control_binary_request(request, &payload, &payload_len,
                               error, sizeof error) != 0) {
        task->failed = 1;
        snprintf(task->error, sizeof task->error, "%s", error);
    } else {
        (void)payload_len;
        task->count = winget_parse_results(payload, task->results,
                                           WINGET_RESULT_MAX);
    }
    free(payload);
    task_done(&task->done);
    return NULL;
}

static void winget_search_start(const char *query);

static void winget_search_poll(void) {
    if (!g_winget_search_running) return;
    if (!task_is_done(&g_winget_search_task.done)) return;

    pthread_join(g_winget_search_thread, NULL);
    g_winget_search_running = 0;
    if (g_winget_search_task.generation == g_store_generation && g_ready) {
        g_winget_loaded = 1;
        if (g_winget_search_task.failed) {
            g_winget_error = 1;
            g_winget_search_active = 0;
            g_winget_count = 0;
            snprintf(g_winget_error_text, sizeof g_winget_error_text, "%s",
                     g_winget_search_task.error[0]
                         ? g_winget_search_task.error
                         : "The winget search could not be read.");
            set_status(g_winget_error_text, 1);
        } else {
            memcpy(g_winget_results, g_winget_search_task.results,
                   sizeof g_winget_results);
            g_winget_count = g_winget_search_task.count;
            snprintf(g_winget_query, sizeof g_winget_query, "%s",
                     g_winget_search_task.query);
            g_winget_search_active = 1;
            g_winget_error = 0;
            g_winget_error_text[0] = '\0';
            if (!strncmp(g_status, "Searching winget", 16)) {
                g_status[0] = '\0';
                g_status_error = 0;
            }
            ensure_selection();
        }
        bump();
    }

    if (g_winget_search_pending && g_ready) {
        char next[FINN_WINGET_QUERY_MAX + 1];
        snprintf(next, sizeof next, "%s", g_winget_pending_query);
        g_winget_search_pending = 0;
        g_winget_pending_query[0] = '\0';
        if (next[0] && !strcmp(next, g_filter))
            winget_search_start(next);
    }
}

static void winget_search_start(const char *query) {
    if (!query) query = "";
    if (!query[0]) {
        g_winget_count = 0;
        g_winget_search_active = 0;
        g_winget_query[0] = '\0';
        g_winget_loaded = 1;
        bump();
        return;
    }
    if (g_winget_search_running) {
        if (g_winget_search_task.generation == g_store_generation &&
            !strcmp(query, g_winget_search_task.query))
            return;
        snprintf(g_winget_pending_query, sizeof g_winget_pending_query,
                 "%s", query);
        g_winget_search_pending = 1;
        set_status("Waiting for the current winget search...", 0);
        return;
    }

    memset(&g_winget_search_task, 0, sizeof g_winget_search_task);
    snprintf(g_winget_search_task.query, sizeof g_winget_search_task.query,
             "%s", query);
    g_winget_search_task.generation = g_store_generation;
    g_winget_search_running = 1;
    int rc = pthread_create(&g_winget_search_thread, NULL,
                            winget_search_worker, &g_winget_search_task);
    if (rc != 0) {
        g_winget_search_running = 0;
        g_winget_error = 1;
        snprintf(g_winget_error_text, sizeof g_winget_error_text,
                 "Could not start the background winget search.");
        set_status(g_winget_error_text, 1);
        return;
    }
    set_status("Searching winget...", 0);
}

static winget_info *winget_info_find(const char *id) {
    if (!id || !*id) return NULL;
    for (int i = 0; i < WINGET_INFO_MAX; i++)
        if (g_winget_info[i].present && !strcmp(g_winget_info[i].id, id))
            return &g_winget_info[i];
    return NULL;
}

static void winget_info_parse(const char *payload, winget_info *info) {
    if (!payload || !info) return;
    char *copy = strdup(payload);
    if (!copy) return;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = '\0';
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab++ = '\0';
        if (!strcmp(line, "id"))
            flatpak_copy_field(info->id, sizeof info->id, tab);
        else if (!strcmp(line, "name"))
            flatpak_copy_field(info->name, sizeof info->name, tab);
        else if (!strcmp(line, "version"))
            flatpak_copy_field(info->version, sizeof info->version, tab);
        else if (!strcmp(line, "publisher"))
            flatpak_copy_field(info->publisher, sizeof info->publisher, tab);
        else if (!strcmp(line, "description"))
            flatpak_copy_field(info->description, sizeof info->description,
                               tab);
        else if (!strcmp(line, "homepage"))
            flatpak_copy_field(info->homepage, sizeof info->homepage, tab);
        else if (!strcmp(line, "installer_type"))
            flatpak_copy_field(info->installer_type,
                               sizeof info->installer_type, tab);
        else if (!strcmp(line, "installer_url"))
            flatpak_copy_field(info->installer_url, sizeof info->installer_url,
                               tab);
        else if (!strcmp(line, "installer_sha256"))
            flatpak_copy_field(info->installer_sha256,
                               sizeof info->installer_sha256, tab);
        else if (!strcmp(line, "installer_arch"))
            flatpak_copy_field(info->installer_arch,
                               sizeof info->installer_arch, tab);
        else if (!strcmp(line, "size"))
            flatpak_copy_field(info->size, sizeof info->size, tab);
    }
    free(copy);
}

static void *winget_info_worker(void *arg) {
    winget_info_task *task = arg;
    char request[WINGET_ID_CAP + 24];
    int n = snprintf(request, sizeof request, "winget.info %s", task->id);
    if (n <= 0 || (size_t)n >= sizeof request) {
        task->failed = 1;
        snprintf(task->error, sizeof task->error,
                 "The winget package id is too long.");
        task_done(&task->done);
        return NULL;
    }

    char *payload = NULL;
    size_t payload_len = 0;
    char error[192] = "";
    memset(&task->info, 0, sizeof task->info);
    if (control_binary_request(request, &payload, &payload_len,
                               error, sizeof error) != 0) {
        task->failed = 1;
        snprintf(task->error, sizeof task->error, "%s", error);
    } else {
        (void)payload_len;
        winget_info_parse(payload, &task->info);
        if (!task->info.id[0])
            snprintf(task->info.id, sizeof task->info.id, "%s", task->id);
    }
    free(payload);
    task_done(&task->done);
    return NULL;
}

static void winget_info_start(const char *id);

static void winget_info_poll(void) {
    if (!g_winget_info_running) return;
    if (!task_is_done(&g_winget_info_task.done)) return;

    pthread_join(g_winget_info_thread, NULL);
    g_winget_info_running = 0;
    if (g_winget_info_task.generation == g_store_generation && g_ready) {
        winget_info *slot = NULL;
        winget_info *existing = winget_info_find(g_winget_info_task.id);
        if (existing) {
            slot = existing;
        } else {
            slot = &g_winget_info[g_winget_info_next];
            g_winget_info_next = (g_winget_info_next + 1) % WINGET_INFO_MAX;
        }
        memset(slot, 0, sizeof *slot);
        slot->present = 1;
        if (g_winget_info_task.failed) {
            slot->failed = 1;
            snprintf(slot->id, sizeof slot->id, "%s",
                     g_winget_info_task.id);
            snprintf(slot->error, sizeof slot->error, "%s",
                     g_winget_info_task.error[0]
                         ? g_winget_info_task.error
                         : "The package details could not be read.");
        } else {
            *slot = g_winget_info_task.info;
            slot->present = 1;
        }
        bump();
    }

    if (g_winget_info_pending && g_ready) {
        char next[WINGET_ID_CAP];
        snprintf(next, sizeof next, "%s", g_winget_info_pending_id);
        g_winget_info_pending = 0;
        g_winget_info_pending_id[0] = '\0';
        if (next[0] && g_sel_kind == SEL_WINGET && !strcmp(next, g_sel_id))
            winget_info_start(next);
    }
}

static void winget_info_start(const char *id) {
    if (!id || !*id) return;
    if (winget_info_find(id)) return;
    if (g_winget_info_running) {
        if (!strcmp(id, g_winget_info_task.id)) return;
        snprintf(g_winget_info_pending_id, sizeof g_winget_info_pending_id,
                 "%s", id);
        g_winget_info_pending = 1;
        return;
    }

    memset(&g_winget_info_task, 0, sizeof g_winget_info_task);
    snprintf(g_winget_info_task.id, sizeof g_winget_info_task.id, "%s", id);
    g_winget_info_task.generation = g_store_generation;
    g_winget_info_running = 1;
    int rc = pthread_create(&g_winget_info_thread, NULL, winget_info_worker,
                            &g_winget_info_task);
    if (rc != 0) {
        g_winget_info_running = 0;
        winget_info *slot = &g_winget_info[g_winget_info_next];
        g_winget_info_next = (g_winget_info_next + 1) % WINGET_INFO_MAX;
        memset(slot, 0, sizeof *slot);
        slot->present = 1;
        slot->failed = 1;
        snprintf(slot->id, sizeof slot->id, "%s", id);
        snprintf(slot->error, sizeof slot->error,
                 "Could not start the package-details request.");
        bump();
    }
}

/* ---------------- winget install job ---------------- */

static int winget_job_active(void) { return g_winget_job_id[0] != '\0'; }

static int winget_sha256_valid(const char *sha) {
    if (!sha) return 0;
    size_t n = 0;
    for (; sha[n]; n++) {
        if (!isxdigit((unsigned char)sha[n])) return 0;
    }
    return n == 64;
}

static int winget_install_ready(const winget_result *r, const char **why) {
    if (!r) {
        if (why) *why = "No package is selected.";
        return 0;
    }
    if (g_wine.available < 0) {
        if (why) *why = "Wine status has not been read yet.";
        return 0;
    }
    if (g_wine.failed) {
        if (why) *why = "The Wine runtime could not be read.";
        return 0;
    }
    if (!g_wine.available) {
        if (why) *why = g_wine.reason[0] && strcmp(g_wine.reason, "no-runtime")
            ? g_wine.reason
            : "Wine is unavailable on this image, so the installer cannot run.";
        return 0;
    }

    winget_info *info = winget_info_find(r->id);
    if (!info) {
        if (why) *why = "Select the result to check its installer details.";
        return 0;
    }
    if (info->failed) {
        if (why) *why = info->error[0] ? info->error
                                      : "The package details could not be read.";
        return 0;
    }
    if (info->installer_type[0] &&
        (strcasestr(info->installer_type, "msix") ||
         strcasestr(info->installer_type, "appx"))) {
        if (why) *why = "MSIX and AppX packages are not supported by this installer path.";
        return 0;
    }
    if (strncasecmp(info->installer_url, "https://", 8) != 0) {
        if (why) *why = "No verified HTTPS installer is available.";
        return 0;
    }
    if (!winget_sha256_valid(info->installer_sha256)) {
        if (why) *why = "The installer has no verified SHA-256.";
        return 0;
    }
    if (why) *why = "";
    return 1;
}

static void *winget_job_worker(void *arg) {
    winget_job_task *task = arg;
    char request[WINGET_JOB_ID_CAP + 24];
    int n = snprintf(request, sizeof request, "winget.job %s", task->job);
    if (n <= 0 || (size_t)n >= sizeof request) {
        task->request_failed = 1;
        snprintf(task->error, sizeof task->error, "The job id is too long.");
        task_done(&task->done);
        return NULL;
    }

    char reply[256] = "";
    if (control_text_request(request, reply, sizeof reply) != 0 ||
        strncmp(reply, "OK winget.job", 13)) {
        task->request_failed = 1;
        const char *why = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        snprintf(task->error, sizeof task->error, "%s",
                 why[0] ? why : "The install task state could not be read.");
        task_done(&task->done);
        return NULL;
    }

    const char *word = strrchr(reply, ' ');
    snprintf(task->state, sizeof task->state, "%s",
             word && word[1] ? word + 1 : "queued");
    if (!strcmp(task->state, "complete") || !strcmp(task->state, "failed")) {
        char why[WINGET_TEXT_CAP] = "";
        wine_read_state(&task->wine, why, sizeof why);
        task->wine_read = 1;
    }
    task_done(&task->done);
    return NULL;
}

static void winget_job_poll_start(void) {
    if (!winget_job_active() || g_winget_job_running) return;
    long long now = now_ms();
    if (g_winget_job_last_ms && now - g_winget_job_last_ms < 900) return;

    memset(&g_winget_job_task, 0, sizeof g_winget_job_task);
    snprintf(g_winget_job_task.job, sizeof g_winget_job_task.job, "%s",
             g_winget_job_id);
    g_winget_job_task.generation = g_store_generation;
    g_winget_job_running = 1;
    g_winget_job_last_ms = now;
    int rc = pthread_create(&g_winget_job_thread, NULL, winget_job_worker,
                            &g_winget_job_task);
    if (rc != 0) {
        g_winget_job_running = 0;
        set_status("Could not start the install-task poll.", 1);
    }
}

static void winget_job_apply_wine(const winget_job_task *task) {
    if (!task->wine_read) return;
    g_wine = task->wine;
    ensure_selection();
}

static void winget_job_poll(void) {
    if (!g_winget_job_running) return;
    if (!task_is_done(&g_winget_job_task.done)) return;

    pthread_join(g_winget_job_thread, NULL);
    g_winget_job_running = 0;
    if (g_winget_job_task.generation != g_store_generation || !g_ready)
        return;
    if (strcmp(g_winget_job_task.job, g_winget_job_id)) return;

    if (g_winget_job_task.request_failed) {
        set_status(g_winget_job_task.error[0] ? g_winget_job_task.error
                                             : "The install task state could not be read; retrying.", 0);
        bump();
        return;
    }

    if (!strcmp(g_winget_job_task.state, "complete")) {
        char name[WINGET_NAME_CAP];
        snprintf(name, sizeof name, "%s", g_winget_job_name);
        winget_job_apply_wine(&g_winget_job_task);
        g_winget_job_id[0] = '\0';
        g_winget_job_app[0] = '\0';
        g_winget_job_name[0] = '\0';
        g_winget_job_state[0] = '\0';
        char message[320];
        snprintf(message, sizeof message,
                 "The install task for %s finished. Registered Windows apps were re-read; "
                 "an installer exiting is not a registration.", name);
        set_status(message, 0);
        bump();
        return;
    }
    if (!strcmp(g_winget_job_task.state, "failed")) {
        char name[WINGET_NAME_CAP];
        snprintf(name, sizeof name, "%s", g_winget_job_name);
        winget_job_apply_wine(&g_winget_job_task);
        g_winget_job_id[0] = '\0';
        g_winget_job_app[0] = '\0';
        g_winget_job_name[0] = '\0';
        g_winget_job_state[0] = '\0';
        char message[320];
        snprintf(message, sizeof message,
                 "The install task for %s failed; see the control log.", name);
        set_status(message, 1);
        bump();
        return;
    }

    snprintf(g_winget_job_state, sizeof g_winget_job_state, "%s",
             g_winget_job_task.state);
    char message[320];
    snprintf(message, sizeof message, "Installing %s (%s)...",
             g_winget_job_name, g_winget_job_state);
    set_status(message, 0);
    bump();
}

/* ---------------- actions ---------------- */

static void native_launch(int idx) {
    const finn_app *a = finn_apps_at(idx);
    if (!a) return;
    char request[128];
    char reply[256] = "";
    int n = snprintf(request, sizeof request, "app.launch %s", a->id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        set_status("The control plane is not reachable; no app was started.", 1);
        return;
    }
    if (!strncmp(reply, "OK launched ", 12)) {
        char message[256];
        snprintf(message, sizeof message, "Opening %s.", a->name);
        set_status(message, 0);
    } else {
        set_status(reply[0] ? reply : "The app could not start.", 1);
    }
}

static void native_install(int idx) {
    const finn_app *a = finn_apps_at(idx);
    if (!a) return;
    if (!app_available(a)) {
        set_status("That app's launcher is not on this machine, so it cannot be installed.",
                   1);
        return;
    }
    char request[128];
    char reply[512] = "";
    int n = snprintf(request, sizeof request, "app.install %s", a->id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        set_status("The control plane is not reachable, so nothing was changed.", 1);
        return;
    }
    if (strncmp(reply, "OK", 2)) {
        const char *why = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        set_status(why[0] ? why : "The package manager refused the change.", 1);
        return;
    }
    char name[128];
    snprintf(name, sizeof name, "%s", a->name);
    int count = finn_apps_load(g_registry, NULL);
    g_load_error = count < 0;
    ensure_selection();
    char message[256];
    snprintf(message, sizeof message, "Installed %s.", name);
    set_status(message, 0);
    bump();
}

static void native_remove(int idx) {
    const finn_app *a = finn_apps_at(idx);
    if (!a) return;
    if (strcmp(g_confirm_id, a->id)) {
        snprintf(g_confirm_id, sizeof g_confirm_id, "%s", a->id);
        char message[256];
        snprintf(message, sizeof message,
                 "Press Remove again to take %s off this machine.", a->name);
        set_status(message, 0);
        return;
    }

    char request[128];
    char reply[512] = "";
    int n = snprintf(request, sizeof request, "app.remove %s", a->id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        g_confirm_id[0] = '\0';
        set_status("The control plane is not reachable, so nothing was changed.", 1);
        return;
    }
    if (strncmp(reply, "OK", 2)) {
        const char *why = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        g_confirm_id[0] = '\0';
        set_status(why[0] ? why : "The package manager refused the change.", 1);
        return;
    }

    char name[128];
    snprintf(name, sizeof name, "%s", a->name);
    g_confirm_id[0] = '\0';
    int count = finn_apps_load(g_registry, NULL);
    g_load_error = count < 0;
    ensure_selection();
    char message[256];
    snprintf(message, sizeof message, "Removed %s.", name);
    set_status(message, 0);
    bump();
}

static void flatpak_run_app(const flatpak_app *app) {
    if (!app) return;
    char request[FLATPAK_APP_ID_CAP + 24];
    char reply[256] = "";
    int n = snprintf(request, sizeof request, "flatpak.run %s", app->id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        set_status("The control plane is not reachable; no app was started.", 1);
        return;
    }
    if (!strncmp(reply, "OK launched ", 12)) {
        char message[256];
        snprintf(message, sizeof message, "Opening %s.", app->name);
        set_status(message, 0);
    } else {
        set_status(reply[0] ? reply : "The Flatpak app could not start.", 1);
    }
}

static void flatpak_install_app(const flatpak_app *app) {
    if (!app) return;
    if (g_flatpak_job_id[0]) {
        set_status("A Flatpak task is active. Press F5 to check its state.", 0);
        return;
    }
    if (!app->remote[0]) {
        set_status("This search result has no install source; no package change was made.",
                   1);
        return;
    }
    char request[FLATPAK_APP_ID_CAP + FLATPAK_APP_REMOTE_CAP + 32];
    char reply[256] = "";
    int n = snprintf(request, sizeof request, "flatpak.install %s %s",
                     app->id, app->remote);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        set_status("The control plane is not reachable; no package change was made.", 1);
        return;
    }
    if (strncmp(reply, "OK job=", 7)) {
        const char *why = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        set_status(why[0] ? why : "The package manager refused the change.", 1);
        return;
    }
    const char *job = reply + 7;
    if (!*job || strlen(job) >= sizeof g_flatpak_job_id) {
        set_status("The package manager returned an invalid job id.", 1);
        return;
    }
    snprintf(g_flatpak_job_id, sizeof g_flatpak_job_id, "%s", job);
    snprintf(g_flatpak_job_app, sizeof g_flatpak_job_app, "%s", app->id);
    snprintf(g_flatpak_job_name, sizeof g_flatpak_job_name, "%s", app->name);
    g_flatpak_job_install = 1;
    char message[256];
    snprintf(message, sizeof message,
             "Installing %s. Press F5 to check.", app->name);
    set_status(message, 0);
}

static void flatpak_remove_app(const flatpak_app *app) {
    if (!app) return;
    if (g_flatpak_job_id[0]) {
        set_status("A Flatpak task is active. Press F5 to check its state.", 0);
        return;
    }
    if (strcmp(g_confirm_id, app->id)) {
        snprintf(g_confirm_id, sizeof g_confirm_id, "%s", app->id);
        char message[256];
        snprintf(message, sizeof message,
                 "Press Remove again to remove %s. User data is kept.", app->name);
        set_status(message, 0);
        return;
    }

    char request[FLATPAK_APP_ID_CAP + 32];
    char reply[256] = "";
    int n = snprintf(request, sizeof request, "flatpak.remove %s", app->id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        g_confirm_id[0] = '\0';
        set_status("The control plane is not reachable; no package change was made.", 1);
        return;
    }
    if (strncmp(reply, "OK job=", 7)) {
        const char *why = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        g_confirm_id[0] = '\0';
        set_status(why[0] ? why : "The package manager refused the change.", 1);
        return;
    }
    const char *job = reply + 7;
    if (!*job || strlen(job) >= sizeof g_flatpak_job_id) {
        g_confirm_id[0] = '\0';
        set_status("The package manager returned an invalid job id.", 1);
        return;
    }
    snprintf(g_flatpak_job_id, sizeof g_flatpak_job_id, "%s", job);
    snprintf(g_flatpak_job_app, sizeof g_flatpak_job_app, "%s", app->id);
    snprintf(g_flatpak_job_name, sizeof g_flatpak_job_name, "%s", app->name);
    g_flatpak_job_install = 0;
    g_confirm_id[0] = '\0';
    char message[256];
    snprintf(message, sizeof message,
             "Removing %s. Press F5 to check.", app->name);
    set_status(message, 0);
}

static void wine_run_app(const wine_app *app) {
    if (!app) return;
    char request[WINE_APP_ID_CAP + 24];
    char reply[256] = "";
    int n = snprintf(request, sizeof request, "wine.run %s", app->id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        set_status("The control plane is not reachable; no Windows app was started.", 1);
        return;
    }
    if (!strncmp(reply, "OK launched ", 12)) {
        char message[192];
        snprintf(message, sizeof message, "Opening %s with Wine.", app->id);
        set_status(message, 0);
    } else {
        set_status(reply[0] ? reply : "The Windows app could not start.", 1);
    }
}

static void winget_install_result(const winget_result *r) {
    if (!r) return;
    if (winget_job_active()) {
        set_status("A Windows install task is already active.", 0);
        return;
    }
    const char *why = "";
    if (!winget_install_ready(r, &why)) {
        set_status(why[0] ? why : "This package cannot be installed here.", 1);
        return;
    }

    char request[WINGET_ID_CAP + 24];
    char reply[256] = "";
    int n = snprintf(request, sizeof request, "winget.install %s", r->id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0) {
        set_status("The control plane is not reachable; no install was started.", 1);
        return;
    }

    const char *job = NULL;
    if (!strncmp(reply, "OK winget.install job=", 22)) job = reply + 22;
    else if (!strncmp(reply, "OK job=", 7)) job = reply + 7;
    if (!job || !*job || strlen(job) >= sizeof g_winget_job_id) {
        const char *err = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        set_status(err[0] ? err : "The control plane refused the install.", 1);
        return;
    }

    snprintf(g_winget_job_id, sizeof g_winget_job_id, "%s", job);
    snprintf(g_winget_job_app, sizeof g_winget_job_app, "%s", r->id);
    snprintf(g_winget_job_name, sizeof g_winget_job_name, "%s", r->name);
    snprintf(g_winget_job_state, sizeof g_winget_job_state, "queued");
    g_winget_job_last_ms = 0;
    char message[320];
    snprintf(message, sizeof message, "Installing %s (queued)...", r->name);
    set_status(message, 0);
}

static void flatpak_poll_job(void) {
    if (!g_flatpak_job_id[0]) {
        flatpak_query_start(g_filter);
        return;
    }
    char request[FLATPAK_JOB_ID_CAP + 24];
    char reply[256] = "";
    int n = snprintf(request, sizeof request, "flatpak.job %s",
                     g_flatpak_job_id);
    if (n <= 0 || (size_t)n >= sizeof request ||
        control_text_request(request, reply, sizeof reply) != 0 ||
        strncmp(reply, "OK", 2)) {
        const char *why = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
        set_status(why[0] ? why : "The Flatpak task state could not be read.", 1);
        return;
    }

    char state[32] = "";
    const char *field = strstr(reply, "state=");
    if (field) {
        field += 6;
        size_t n2 = 0;
        while (field[n2] && field[n2] != ' ' && n2 + 1 < sizeof state) {
            state[n2] = field[n2];
            n2++;
        }
        state[n2] = '\0';
    } else {
        const char *word = strrchr(reply, ' ');
        snprintf(state, sizeof state, "%s", word && word[1] ? word + 1 : "");
    }

    if (!strcmp(state, "queued") || !strcmp(state, "running") ||
        !strcmp(state, "activating")) {
        char message[256];
        snprintf(message, sizeof message, "%s %s. Press F5 to check.",
                 g_flatpak_job_install ? "Installing" : "Removing",
                 g_flatpak_job_name);
        set_status(message, 0);
        return;
    }

    int installing = g_flatpak_job_install;
    char name[FLATPAK_APP_NAME_CAP];
    snprintf(name, sizeof name, "%s", g_flatpak_job_name);
    g_flatpak_job_id[0] = '\0';
    g_flatpak_job_app[0] = '\0';
    g_flatpak_job_name[0] = '\0';
    g_flatpak_job_install = 0;
    flatpak_query_start(g_filter);

    if (!strcmp(state, "complete")) {
        char message[256];
        snprintf(message, sizeof message, "%s %s%s",
                 installing ? "Installed" : "Removed", name,
                 installing ? "; open it from Library." : ".");
        set_status(message, 0);
    } else {
        char message[256];
        snprintf(message, sizeof message,
                 "%s %s failed; see the control log for details.",
                 installing ? "Install" : "Removal", name);
        set_status(message, 1);
    }
}

/* ---------------- canvas and icons ---------------- */

static void c_fill(store_canvas *c, float x, float y, float w, float h,
                   float r, uint32_t col) {
    finn_fill_rrect(c->px, c->w, c->h, x + c->dx, y + c->dy, w, h, r, col);
}

static void c_stroke(store_canvas *c, float x, float y, float w, float h,
                     float r, float t, uint32_t col) {
    finn_stroke_rrect(c->px, c->w, c->h, x + c->dx, y + c->dy, w, h, r, t,
                      col);
}

static void c_frost(store_canvas *c, float x, float y, float w, float h,
                    float r, uint32_t tint, float alpha) {
    finn_frost_rrect(c->px, c->w, c->h, c->ox, c->oy, x + c->dx, y + c->dy,
                     w, h, r, tint, alpha);
}

static void c_text(store_canvas *c, const finn_font *f, float x, float base,
                   const char *text, uint32_t col) {
    finn_text_draw(f, c->px, c->w, c->h, x + c->dx, base + c->dy, text, col);
}

static void c_icon_ink(store_canvas *c, fsvg *ic, float cx, float cy,
                       float ink, uint32_t active, uint32_t inactive) {
    if (!ic || ic->vw <= 0) return;
    finn_icon_blit_ink(ic, c->px, c->w, c->h,
                       (int)lroundf(cx + c->dx), (int)lroundf(cy + c->dy),
                       (int)lroundf(ink), active, inactive, 0);
}

static void free_icons(void) {
    for (int i = 0; i < FINN_APP_MAX; i++) {
        if (g_icons[i].loaded) fsvg_free(&g_icons[i].svg);
        g_icons[i].loaded = 0;
        g_icons[i].id[0] = '\0';
    }
}

static fsvg *icon_for(int i, const finn_app *a) {
    if (!a || i < 0 || i >= FINN_APP_MAX) return NULL;
    store_icon *ic = &g_icons[i];

    if (ic->loaded && !strcmp(ic->id, a->id)) return &ic->svg;
    if (ic->loaded) { fsvg_free(&ic->svg); ic->loaded = 0; }
    ic->id[0] = '\0';
    if (!a->icon[0] || !g_icon_dir[0]) return NULL;

    char p[600];
    snprintf(p, sizeof p, "%s/%s", g_icon_dir, a->icon);
    if (fsvg_load(p, &ic->svg) != 0) {
        memset(&ic->svg, 0, sizeof ic->svg);
        return NULL;
    }
    ic->loaded = 1;
    snprintf(ic->id, sizeof ic->id, "%s", a->id);
    return &ic->svg;
}

static void draw_app_icon(store_canvas *c, const finn_app *a, int i,
                          float x, float y, float sz) {
    fsvg *ic = icon_for(i, a);
    if (ic && ic->vw > 0) {
        finn_icon_blit(ic, c->px, c->w, c->h,
                       (int)lroundf(x + c->dx), (int)lroundf(y + c->dy),
                       (int)lroundf(sz), 0, 0, 0);
        return;
    }

    /* No asset is not an empty square: the tile still has to say which app it
     * belongs to, so the first letter of the name stands in. */
    c_fill(c, x, y, sz, sz, sz * 0.22f, FINN_TILE_ON);
    char initial[2] = { a && a->name[0] ? a->name[0] : '?', 0 };
    float cap = finn_text_cap_height(&g_f_label);
    float tw = finn_text_width(&g_f_label, initial);
    c_text(c, &g_f_label, x + (sz - tw) * 0.5f,
           y + sz * 0.5f + cap * 0.5f, initial, FINN_INK);
}

static void draw_source_glyph(store_canvas *c, fsvg *ic, float x, float y,
                              float sz) {
    c_fill(c, x, y, sz, sz, sz * 0.22f,
           finn_theme_dark() ? 0x2A2E7BFFu : 0x18006FFFu);
    c_icon_ink(c, ic, x + sz * 0.5f, y + sz * 0.5f, sz * 0.54f,
               FINN_ON, FINN_ON);
}

/* ---------------- layout ---------------- */

typedef struct {
    float pad;
    int   rail;
    float rail_w;
    float rail_y;
    float title_x, title_top;
    float sub_x, sub_top;
    float filter_x, filter_y, filter_w, filter_h;
    float tabs_x, tabs_y, tabs_w, tabs_h;
    float content_x, content_y, content_w, content_bottom;
    float status_x, status_w;
    float scrollbar_x;
    int   grid_cols, shelf_cols;
} store_layout;

static int cols_for(float cw, float s, float minw) {
    float gap = s * CARD_GAP;
    int c = (int)floorf((cw + gap) / (s * minw + gap));
    if (c < 1) c = 1;
    if (c > 3) c = 3;
    return c;
}

static int shelf_cols_for(float cw, float s) {
    float gap = s * CARD_GAP;
    int c = (int)floorf((cw + gap) / (s * SHELF_MIN_W + gap));
    if (c < 1) c = 1;
    if (c > 4) c = 4;
    return c;
}

/*
 * Two arrangements, one rhythm. The rail is the default and is the one the
 * window opens in; a window dragged narrow folds the rail into a row of tabs
 * rather than squeezing a 250-unit sheet into half the width.
 */
static void layout_for(int ww, int wh, float s, store_layout *L) {
    memset(L, 0, sizeof *L);
    float ref_w = (float)ww / s;
    float ref_h = (float)wh / s;

    L->rail = ref_w >= RAIL_MIN && ref_h >= RAIL_MIN_H;
    L->pad = s * (L->rail ? PAD : NARROW_PAD);
    L->filter_h = s * FILTER_H;
    L->tabs_h = s * TAB_H;
    L->rail_y = 0.0f;

    if (L->rail) {
        L->rail_w = s * RAIL_W;
        L->filter_w = s * FILTER_W;
        if (L->filter_w > L->rail_w - s * RAIL_PAD * 2.0f)
            L->filter_w = L->rail_w - s * RAIL_PAD * 2.0f;
        L->filter_x = s * RAIL_PAD;
        L->filter_y = s * SUB_TOP;

        L->title_x = L->rail_w + s * PAD;
        L->title_top = s * TITLE_TOP;
        L->sub_x = L->title_x;
        L->sub_top = s * SUB_TOP;
        L->content_x = L->rail_w + s * PAD;
        L->content_w = (float)ww - L->content_x - L->pad;
        /* A shelf of cards as wide as the screen is not a shop window. Like
         * Settings, the column stops growing once it is readable and leaves
         * the rest of a maximised window to the glass. */
        if (L->content_w > s * CONTENT_MAX) L->content_w = s * CONTENT_MAX;
        L->content_y = s * CONTENT_TOP;
        L->tabs_w = 0.0f;
    } else {
        /* The page title takes the window title's place so a narrow Store
         * does not spend two lines saying where it is. */
        L->title_x = L->pad;
        L->title_top = s * 44.0f;
        L->sub_x = L->pad;
        L->sub_top = s * 68.0f;

        L->filter_w = (float)ww - L->pad * 2.0f;
        L->filter_x = L->pad;
        L->filter_y = s * 88.0f;
        L->tabs_x = L->pad;
        L->tabs_y = s * 126.0f;
        L->tabs_w = (float)ww - L->pad * 2.0f;
        L->content_x = L->pad;
        L->content_w = L->filter_w;
        L->content_y = s * (126.0f + TAB_H + 14.0f);
    }

    if (L->content_w < s * 80.0f) L->content_w = s * 80.0f;
    L->grid_cols = cols_for(L->content_w, s, GRID_MIN_W);
    L->shelf_cols = shelf_cols_for(L->content_w, s);

    L->content_bottom = (float)wh - s * 16.0f;
    if (L->content_bottom < L->content_y) L->content_bottom = L->content_y;
    L->scrollbar_x = L->content_x + L->content_w + s * 5.0f;
    if (L->scrollbar_x > (float)ww - s * 6.0f)
        L->scrollbar_x = (float)ww - s * 6.0f;

    L->status_w = L->content_w * 0.46f;
    if (L->status_w < s * 90.0f) L->status_w = s * 90.0f;
    L->status_x = L->content_x + L->content_w - L->status_w;
}

static const char *nav_section_for(int i) {
    if (i <= 3) return "Browse";
    return "Your apps";
}

static float nav_row_top(int i) {
    float y = NAV_FIRST_Y;
    for (int k = 0; k < i; k++) {
        y += NAV_ROW_STEP;
        if (k == 3) y += NAV_SECTION_GAP + NAV_SECTION_TOP;
    }
    return y;
}

static void nav_item_rect(const store_layout *L, int i, float s,
                          float *x, float *y, float *w, float *h) {
    if (L->rail) {
        *x = s * RAIL_PAD;
        *w = L->rail_w - s * RAIL_PAD * 2.0f;
        *y = s * nav_row_top(i);
        *h = s * NAV_ITEM_H;
    } else {
        float gap = s * TAB_GAP;
        *w = (L->tabs_w - gap * 4.0f) / 5.0f;
        *x = L->tabs_x + (float)i * (*w + gap);
        *y = L->tabs_y;
        *h = L->tabs_h;
    }
}

static void filter_clear_rect(const store_layout *L, float s,
                              float *x, float *y, float *w, float *h) {
    *w = s * 24.0f;
    *h = s * 24.0f;
    *x = L->filter_x + L->filter_w - *w - s * 3.0f;
    *y = L->filter_y + (L->filter_h - *h) * 0.5f;
}

/* Content-relative like the cards: the caller draws the page with the canvas
 * shifted to the content column, so this names a rect inside that column. */
static void refresh_rect(const store_layout *L, float s, float top,
                         float *x, float *y, float *w, float *h) {
    *w = s * REFRESH_W;
    *h = s * LIST_LABEL_H;
    *x = L->content_w - *w;
    *y = s * top;
}

static float list_top_for(int page) {
    float top = LIST_LABEL_H + LIST_GAP;
    if (page == STORE_PAGE_LINUX || page == STORE_PAGE_WINDOWS)
        top += LIST_NOTE_H;
    return top;
}

/* ---------------- drawing helpers ---------------- */

static void card_bg(store_canvas *c, float x, float y, float w, float h,
                    float s, int selected) {
    float r = s * CARD_R;
    c_frost(c, x, y, w, h, r, FINN_GLASS, 0.72f);
    if (finn_theme_dark())
        c_fill(c, x, y, w, h, r, 0x0AFFFFFFu);
    if (selected) {
        c_fill(c, x, y, w, h, r,
               finn_theme_dark() ? 0x1C0A84FFu : 0x10006FFFu);
        c_stroke(c, x, y, w, h, r, 1.5f, FINN_ON);
    } else {
        c_stroke(c, x, y, w, h, r, 1.0f, FINN_HAIR);
    }
}

/* The quiet well a page shows instead of a list when it has nothing to list. */
static void quiet_well(store_canvas *c, float x, float y, float w, float h,
                       float s, const char *line, const char *sub) {
    c_fill(c, x, y, w, h, s * CARD_R,
           finn_theme_dark() ? 0x1AFFFFFFu : 0x0D000000u);
    float tx = x + s * CARD_PAD;
    float maxw = w - s * CARD_PAD * 2.0f;
    char fitted[320];
    float base = sub && sub[0] ? y + h * 0.5f - s * 2.0f
                               : y + h * 0.5f + finn_text_cap_height(&g_f_sub) * 0.5f;
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, line, maxw);
    c_text(c, &g_f_sub, tx, base, fitted, FINN_DIM);
    if (sub && sub[0]) {
        finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, sub, maxw);
        c_text(c, &g_f_micro, tx, base + s * 17.0f, fitted, FINN_MUTE);
    }
}

static void draw_state_pill(store_canvas *c, float x, float y, float s,
                            const char *label, int installed, int available) {
    float tw = finn_text_width(&g_f_micro, label);
    float dot_w = installed ? s * 10.0f : 0.0f;
    float ph = s * 24.0f;
    float pw = tw + s * 20.0f + dot_w;

    uint32_t bg, ink;
    if (installed) {
        bg = finn_theme_dark() ? 0x2E0A84FFu : 0x14006FFFu;
        ink = FINN_INK;
    } else if (available) {
        bg = finn_theme_dark() ? 0x1AFFFFFFu : 0x0E000000u;
        ink = FINN_DIM;
    } else {
        bg = finn_theme_dark() ? 0x12FFFFFFu : 0x08000000u;
        ink = FINN_MUTE;
    }
    c_fill(c, x, y, pw, ph, ph * 0.5f, bg);
    if (installed)
        c_fill(c, x + s * 10.0f, y + ph * 0.5f - s * 2.0f, s * 4.0f, s * 4.0f,
               s * 2.0f, FINN_ON);
    c_text(c, &g_f_micro, x + s * 10.0f + dot_w,
           y + ph * 0.5f + finn_text_cap_height(&g_f_micro) * 0.5f,
           label, ink);
}

/* A status as a dot and a word, for a card's bottom line. */
static void draw_status_line(store_canvas *c, float x, float base, float s,
                             const char *label, int installed, int available) {
    uint32_t ink = installed ? FINN_INK : (available ? FINN_DIM : FINN_MUTE);
    float cap = finn_text_cap_height(&g_f_micro);
    c_fill(c, x, base - cap * 0.5f - s * 1.5f, s * 4.0f, s * 4.0f,
           s * 2.0f, installed ? FINN_ON : FINN_MUTE);
    c_text(c, &g_f_micro, x + s * 10.0f, base, label, ink);
}

static void draw_button(store_canvas *c, float x, float y, float w, float h,
                        float s, const char *label, int kind) {
    float r = h * 0.5f;
    uint32_t fill, ink;
    int stroked = 0;
    uint32_t stroke = FINN_HAIR;

    switch (kind) {
    case BTN_PRIMARY:
        fill = FINN_ON;
        ink = 0xFFFFFFFFu;
        break;
    case BTN_REMOVE:
        fill = finn_theme_dark() ? 0x1AFFFFFFu : 0x0C000000u;
        ink = FINN_INK;
        stroked = 1;
        break;
    case BTN_REMOVE_CONFIRM:
        fill = finn_theme_dark() ? 0x1AFFFFFFu : 0x0C000000u;
        ink = FINN_INK;
        stroked = 1;
        stroke = FINN_ON;
        break;
    case BTN_QUIET:
        fill = finn_theme_dark() ? 0x14FFFFFFu : 0x08000000u;
        ink = FINN_DIM;
        stroked = 1;
        break;
    default:
        fill = finn_theme_dark() ? 0x10FFFFFFu : 0x08000000u;
        ink = FINN_MUTE;
        break;
    }

    c_fill(c, x, y, w, h, r, fill);
    if (stroked) c_stroke(c, x, y, w, h, r, 1.0f, stroke);

    char fitted[96];
    finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, label, w - s * 8.0f);
    float tw = finn_text_width(&g_f_label, fitted);
    c_text(c, &g_f_label, x + (w - tw) * 0.5f,
           y + h * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
           fitted, ink);
}

/* A word-wrapped paragraph, drawn from its top. */
static int draw_wrapped(store_canvas *c, const finn_font *f, const char *text,
                        float x, float top, float maxw, float line_h,
                        uint32_t col) {
    if (!text || !*text) return 0;
    char line[320] = "";
    const char *p = text;
    int drawn = 0;
    float y = top;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *word = p;
        while (*p && *p != ' ') p++;
        size_t wl = (size_t)(p - word);

        char trial[320];
        if (line[0]) snprintf(trial, sizeof trial, "%s %.*s", line, (int)wl, word);
        else snprintf(trial, sizeof trial, "%.*s", (int)wl, word);

        if (line[0] && finn_text_width(f, trial) > maxw) {
            float cap = finn_text_cap_height(f);
            c_text(c, f, x, y + cap, line, col);
            y += line_h;
            drawn++;
            snprintf(line, sizeof line, "%.*s", (int)wl, word);
        } else if (!line[0] && finn_text_width(f, trial) > maxw) {
            char fitted[320];
            finnapp_ellipsis(f, fitted, sizeof fitted, trial, maxw);
            float cap = finn_text_cap_height(f);
            c_text(c, f, x, y + cap, fitted, col);
            y += line_h;
            drawn++;
            line[0] = '\0';
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }

    if (line[0]) {
        char fitted[320];
        finnapp_ellipsis(f, fitted, sizeof fitted, line, maxw);
        float cap = finn_text_cap_height(f);
        c_text(c, f, x, y + cap, fitted, col);
        drawn++;
    }
    return drawn;
}

static int wrap_lines(const finn_font *f, const char *text, float maxw) {
    if (!text || !*text) return 0;
    char line[320] = "";
    const char *p = text;
    int lines = 0;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *word = p;
        while (*p && *p != ' ') p++;
        size_t wl = (size_t)(p - word);

        char trial[320];
        if (line[0]) snprintf(trial, sizeof trial, "%s %.*s", line, (int)wl, word);
        else snprintf(trial, sizeof trial, "%.*s", (int)wl, word);

        if (line[0] && finn_text_width(f, trial) > maxw) {
            lines++;
            snprintf(line, sizeof line, "%.*s", (int)wl, word);
        } else if (!line[0] && finn_text_width(f, trial) > maxw) {
            lines++;
            line[0] = '\0';
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }
    if (line[0]) lines++;
    return lines;
}

/* ---------------- header, rail and labels ---------------- */

static const char *search_placeholder(void) {
    if (g_page == STORE_PAGE_LINUX) return "Search Flatpak";
    if (g_page == STORE_PAGE_WINDOWS) return "Search winget";
    if (g_page == STORE_PAGE_LIBRARY) return "Search library";
    return "Search apps";
}

static void draw_filter(store_canvas *c, const store_layout *L, float s,
                        int focused) {
    float x = L->filter_x, y = L->filter_y;
    float w = L->filter_w, h = L->filter_h;
    float r = s * 4.0f;

    /* Settings' field, not a web search bar: the same wash of ink, the same
     * 4-unit corners and the same 13px type as the row labels beside it. */
    c_fill(c, x, y, w, h, r,
           finn_theme_dark() ? 0x33FFFFFFu : 0x40C7C7C7u);
    if (focused)
        c_stroke(c, x, y, w, h, r, 1.5f, FINN_ON);
    else
        c_stroke(c, x, y, w, h, r, 1.0f, FINN_HAIR);

    c_icon_ink(c, &g_ic_search, x + s * 15.0f, y + h * 0.5f,
               s * 16.0f, FINN_DIM, FINN_MUTE);

    float tx = x + s * 36.0f;
    float clear_x = x + w;
    int has_clear = g_filter[0] != '\0';
    float text_right = x + w - (has_clear ? s * 36.0f : s * 12.0f);

    if (!g_filter[0]) {
        c_text(c, &g_f_label, tx, y + h * 0.5f +
               finn_text_cap_height(&g_f_label) * 0.5f,
               search_placeholder(), FINN_DIM);
    } else {
        char fitted[FINN_WINGET_QUERY_MAX + 1];
        finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, g_filter,
                         text_right - tx);
        c_text(c, &g_f_label, tx, y + h * 0.5f +
               finn_text_cap_height(&g_f_label) * 0.5f,
               fitted, FINN_INK);
        if (focused) {
            float caret_x = tx + finn_text_width(&g_f_label, fitted);
            if (caret_x > text_right) caret_x = text_right;
            float cap = finn_text_cap_height(&g_f_label);
            c_fill(c, caret_x + s * 2.0f, y + h * 0.5f - cap * 0.575f,
                   s * 1.5f, cap * 1.15f, s * 0.75f, FINN_ON);
        }
    }

    if (has_clear) {
        float cx = clear_x - s * 15.0f;
        float cy = y + h * 0.5f;
        c_fill(c, cx - s * 9.0f, cy - s * 9.0f, s * 18.0f, s * 18.0f,
               s * 9.0f, finn_theme_dark() ? 0x1AFFFFFFu : 0x0E000000u);
        c_icon_ink(c, &g_ic_clear, cx, cy, s * 9.0f, FINN_MUTE, FINN_MUTE);
    }
}

static const char *page_title(void) {
    if (g_detail) return "App details";
    switch (g_page) {
    case STORE_PAGE_NATIVE:  return "Native Apps";
    case STORE_PAGE_LINUX:   return "Linux Apps";
    case STORE_PAGE_WINDOWS: return "Windows Apps";
    case STORE_PAGE_LIBRARY: return "Library";
    default:                 return "Home";
    }
}

static void page_subtitle(char *out, size_t cap) {
    int total = finn_apps_count();
    int installed = finn_apps_installed_count();

    if (g_detail) {
        snprintf(out, cap, "Catalogue entry, install state and launcher");
        return;
    }

    switch (g_page) {
    case STORE_PAGE_NATIVE:
        if (g_load_error) {
            snprintf(out, cap, "The app catalogue could not be read");
        } else if (g_filter[0]) {
            snprintf(out, cap, "%d of %d apps match \"%.64s\"",
                     native_visible_count(), total, g_filter);
        } else {
            snprintf(out, cap, "%d app%s in the catalogue, %d installed",
                     total, total == 1 ? "" : "s", installed);
        }
        break;
    case STORE_PAGE_LINUX:
        if (g_flatpak_error) {
            snprintf(out, cap, "Flatpak catalogue unavailable");
        } else if (flatpak_search_current()) {
            snprintf(out, cap, "%d Flatpak result%s for \"%.64s\"",
                     flatpak_linux_count(),
                     flatpak_linux_count() == 1 ? "" : "s", g_filter);
        } else if (!g_flatpak_loaded) {
            snprintf(out, cap, "Reading installed Flatpak apps...");
        } else {
            snprintf(out, cap, "%d installed Flatpak app%s; press Enter to search",
                     g_flatpak_installed_count,
                     g_flatpak_installed_count == 1 ? "" : "s");
        }
        break;
    case STORE_PAGE_WINDOWS:
        if (winget_results_visible()) {
            snprintf(out, cap, "%d winget result%s for \"%.64s\"",
                     g_winget_count, g_winget_count == 1 ? "" : "s",
                     g_winget_query);
        } else if (g_winget_error && g_filter[0]) {
            snprintf(out, cap, "winget search unavailable");
        } else if (g_wine.available == 1) {
            snprintf(out, cap, "%d registered Windows app%s; search winget to install",
                     g_wine.count, g_wine.count == 1 ? "" : "s");
        } else if (g_wine.failed) {
            snprintf(out, cap, "The Wine runtime could not be read");
        } else if (g_wine.available == 0) {
            snprintf(out, cap, "Wine is unavailable; Windows apps cannot run");
        } else {
            snprintf(out, cap, "Reading the Wine runtime...");
        }
        break;
    case STORE_PAGE_LIBRARY:
        snprintf(out, cap, "%d installed app%s across native, Flatpak and Wine",
                 g_library_count, g_library_count == 1 ? "" : "s");
        break;
    default: {
        int flat = g_flatpak_installed_count;
        int win = g_wine.count;
        if (!g_flatpak_loaded || g_wine.available < 0) {
            snprintf(out, cap, "%d native app%s, %d installed; reading other sources",
                     total, total == 1 ? "" : "s", installed);
        } else {
            snprintf(out, cap,
                     "%d native apps, %d installed · %d Flatpak · %d Windows",
                     total, installed, flat, win);
        }
        break;
    }
    }
}

static void draw_header(store_canvas *c, const store_layout *L, float s) {
    c_text(c, &g_f_hero, L->title_x,
           L->title_top + finn_text_cap_height(&g_f_hero),
           page_title(), FINN_INK);

    char sub[320], fitted[320];
    page_subtitle(sub, sizeof sub);
    float sub_max = L->content_w;
    if (g_status[0]) sub_max -= L->status_w + s * 16.0f;
    if (sub_max < s * 40.0f) sub_max = s * 40.0f;
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, sub, sub_max);
    c_text(c, &g_f_sub, L->sub_x,
           L->sub_top + finn_text_cap_height(&g_f_sub), fitted, FINN_DIM);

    if (g_status[0]) {
        finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, g_status,
                         L->status_w);
        float tw = finn_text_width(&g_f_micro, fitted);
        c_text(c, &g_f_micro, L->status_x + L->status_w - tw,
               L->sub_top + finn_text_cap_height(&g_f_micro), fitted,
               g_status_error ? FINN_INK : FINN_MUTE);
    }
}

static const char *nav_label(int i, int narrow) {
    static const char *full[5] = {
        "Home", "Native Apps", "Linux Apps", "Windows Apps", "Library"
    };
    static const char *short_names[5] = {
        "Home", "Native", "Linux", "Windows", "Library"
    };
    if (i < 0 || i >= 5) return "";
    return narrow ? short_names[i] : full[i];
}

static fsvg *nav_icon(int i) {
    switch (i) {
    case 0: return &g_ic_home;
    case 1: return &g_ic_store;
    case 2: return &g_ic_grid;
    case 3: return &g_ic_windows;
    default: return &g_ic_library;
    }
}

static void draw_rail(store_canvas *c, const store_layout *L, float s) {
    float h = (float)c->h - L->rail_y;
    if (h <= 0.0f) return;

    /* The same 0.92 sheet as Settings: wallpaper light, no wallpaper shape,
     * and rounded only where the window itself is round. */
    float wr = s * 8.0f;
    c_frost(c, 0.0f, L->rail_y, L->rail_w, h, wr, FINN_GLASS, 0.92f);
    c_frost(c, L->rail_w - wr, L->rail_y, wr, h, 0.0f, FINN_GLASS, 0.92f);
    c_fill(c, L->rail_w - 1.0f, L->rail_y, 1.0f, h, 0.0f, FINN_HAIR);

    c_text(c, &g_f_hero, s * RAIL_PAD,
           s * TITLE_TOP + finn_text_cap_height(&g_f_hero),
           "Finn Store", FINN_INK);

    draw_filter(c, L, s, g_filter_focused);

    for (int i = 0; i < 5; i++) {
        float x, y, w, hgt;
        nav_item_rect(L, i, s, &x, &y, &w, &hgt);

        const char *section = nav_section_for(i);
        if (i == 0 || strcmp(section, nav_section_for(i - 1)) != 0) {
            c_text(c, &g_f_micro, x,
                   y - s * NAV_SECTION_TOP +
                   finn_text_cap_height(&g_f_micro),
                   section, FINN_MUTE);
        }

        int active = (i == g_page);
        if (active) {
            c_fill(c, x, y, w, hgt, s * 4.0f,
                   finn_theme_dark() ? 0x38FFFFFFu : 0x26006FFFu);
            c_stroke(c, x, y, w, hgt, s * 4.0f, 1.0f,
                     finn_theme_dark() ? 0x2EFFFFFFu : 0x24006FFFu);
        }

        /* Settings' circular chip, in the Store's own blue. */
        float chip = s * 20.0f;
        float chip_x = x + s * 6.0f;
        float chip_y = y + (hgt - chip) * 0.5f;
        c_fill(c, chip_x, chip_y, chip, chip, chip * 0.5f,
               active ? FINN_ON
                      : (finn_theme_dark() ? 0xFF4A4F59u : 0xFF006FFFu));
        c_icon_ink(c, nav_icon(i), chip_x + chip * 0.5f, chip_y + chip * 0.5f,
                   s * 13.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);

        c_text(c, &g_f_label, chip_x + chip + s * 10.0f,
               y + hgt * 0.5f + finn_text_cap_height(&g_f_label) * 0.5f,
               nav_label(i, 0), FINN_INK);
    }
}

static void draw_tabs(store_canvas *c, const store_layout *L, float s) {
    for (int i = 0; i < 5; i++) {
        float x, y, w, h;
        nav_item_rect(L, i, s, &x, &y, &w, &h);
        int active = (i == g_page);
        if (active) {
            c_fill(c, x, y, w, h, s * 4.0f,
                   finn_theme_dark() ? 0x38FFFFFFu : 0x26006FFFu);
            c_stroke(c, x, y, w, h, s * 4.0f, 1.0f,
                     finn_theme_dark() ? 0x2EFFFFFFu : 0x24006FFFu);
        }

        const char *label = nav_label(i, 1);
        float tw = finn_text_width(&g_f_label, label);
        float icon = s * 14.0f;
        float group = icon + s * 7.0f + tw;
        float cy = y + h * 0.5f;
        if (group <= w - s * 10.0f) {
            float gx = x + (w - group) * 0.5f;
            c_icon_ink(c, nav_icon(i), gx + icon * 0.5f, cy, icon,
                       active ? FINN_ON : FINN_MUTE,
                       active ? FINN_ON : FINN_MUTE);
            c_text(c, &g_f_label, gx + icon + s * 7.0f,
                   cy + finn_text_cap_height(&g_f_label) * 0.5f,
                   label, active ? FINN_INK : FINN_DIM);
        } else {
            char fitted[32];
            finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, label,
                             w - s * 10.0f);
            float fw = finn_text_width(&g_f_label, fitted);
            c_text(c, &g_f_label, x + (w - fw) * 0.5f,
                   cy + finn_text_cap_height(&g_f_label) * 0.5f,
                   fitted, active ? FINN_INK : FINN_DIM);
        }
    }
}

static void draw_list_label(store_canvas *c, const store_layout *L, float s,
                            float top, float scroll_px, const char *label,
                            int refresh) {
    c_text(c, &g_f_micro, 0.0f,
           s * top - scroll_px +
           finn_text_cap_height(&g_f_micro),
           label, FINN_MUTE);

    if (!refresh) return;
    float x, y, w, h;
    refresh_rect(L, s, top, &x, &y, &w, &h);
    y -= scroll_px;
    float cap = finn_text_cap_height(&g_f_micro);
    float tw = finn_text_width(&g_f_micro, "Refresh");
    float icon = s * 12.0f;
    float group = icon + s * 6.0f + tw;
    float gx = x + w - group;
    c_icon_ink(c, &g_ic_refresh, gx + icon * 0.5f, y + h * 0.5f, icon,
               FINN_MUTE, FINN_MUTE);
    c_text(c, &g_f_micro, gx + icon + s * 6.0f,
           y + h * 0.5f + cap * 0.5f, "Refresh", FINN_DIM);
}

static void draw_list_note(store_canvas *c, const store_layout *L, float s,
                           float top, float scroll_px, const char *text) {
    if (!text || !*text) return;
    char fitted[320];
    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, text,
                     L->content_w);
    c_text(c, &g_f_micro, L->content_x,
           L->content_y + s * (top + LIST_LABEL_H) - scroll_px +
           finn_text_cap_height(&g_f_micro),
           fitted, FINN_MUTE);
}

static void draw_scrollbar(store_canvas *c, const store_layout *L, float s,
                           float vy, float vh, int scroll, int max) {
    if (max <= 0 || vh <= 0.0f) return;
    float thumb_h = vh * vh / (vh + (float)max * s);
    if (thumb_h < s * 24.0f) thumb_h = s * 24.0f;
    if (thumb_h > vh) thumb_h = vh;
    float t = (float)scroll / (float)max;
    float ty = vy + (vh - thumb_h) * t;
    c_fill(c, L->scrollbar_x, ty, s * 3.0f, thumb_h, s * 1.5f,
           with_alpha(FINN_MUTE, 0x66));
}

static uint32_t *scratch_begin(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    size_t need = (size_t)w * (size_t)h;
    if (need > g_scratch_cap) {
        uint32_t *p = realloc(g_scratch, need * sizeof(uint32_t));
        if (!p) return NULL;
        g_scratch = p;
        g_scratch_cap = need;
    }
    memset(g_scratch, 0, need * sizeof(uint32_t));
    return g_scratch;
}

/*
 * An offscreen buffer has to start as the surface it is going to replace,
 * or the glass drawn into it frosts the wallpaper instead of the window and
 * the cards read as stickers on a different material. The window body has
 * already been painted into the layer by the time a view renders, so the
 * scratch borrows exactly those pixels and puts them back under the new
 * content.
 */
static void copy_from_layer(uint32_t *dst, int dw, int dh,
                            const uint32_t *src, int sw, int sh,
                            int ox, int oy) {
    for (int y = 0; y < dh; y++) {
        int sy = oy + y;
        if (sy < 0 || sy >= sh) continue;
        int x0 = ox < 0 ? -ox : 0;
        int x1 = dw;
        if (ox + x1 > sw) x1 = sw - ox;
        if (x1 <= x0) continue;
        memcpy(dst + (size_t)y * dw + x0, src + (size_t)sy * sw + ox + x0,
               (size_t)(x1 - x0) * sizeof(uint32_t));
    }
}

/* ---------------- Home ---------------- */

typedef struct {
    int hero;
    int top_apps[FINN_APP_MAX];
    int top_n;
    int nvis;
    float top_label_y, top_y;
    float linux_label_y, linux_y, linux_h;
    float win_label_y, win_y, win_h;
    float content_h;
} home_geom;

static float link_width(const char *label, float s) {
    return finn_text_width(&g_f_micro, label) + s * 18.0f;
}

static void link_rect(const store_layout *L, const char *label, float s,
                      float label_y, float *x, float *y, float *w, float *h) {
    *w = link_width(label, s);
    *x = L->content_w - *w;
    *y = label_y * s;
    *h = s * LABEL_H;
}

static int hero_index(void) {
    int sel = g_sel_kind == SEL_NATIVE ? find_index(g_sel_id) : -1;
    if (sel >= 0 && native_matches(sel)) return sel;

    int n = finn_apps_count();
    int first_avail = -1, first = -1;
    for (int i = 0; i < n; i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a || !native_matches(i)) continue;
        if (first < 0) first = i;
        if (a->installed) return i;
        if (first_avail < 0 && app_available(a)) first_avail = i;
    }
    return first_avail >= 0 ? first_avail : first;
}

static void home_build(const store_layout *L, float s, home_geom *H) {
    (void)L; (void)s;
    memset(H, 0, sizeof *H);
    H->nvis = native_visible_count();

    if (H->nvis == 0) {
        H->hero = -1;
        H->top_label_y = 0.0f;
        H->top_y = 0.0f;
        H->top_n = 0;
        H->linux_label_y = 106.0f;
    } else {
        H->hero = hero_index();
        int sel = g_sel_kind == SEL_NATIVE ? native_visible_index_of(find_index(g_sel_id))
                                          : -1;
        if (sel < 0) sel = 0;
        int cap = L->shelf_cols;
        int start = sel >= cap ? sel - cap + 1 : 0;
        int maxstart = H->nvis - cap;
        if (maxstart < 0) maxstart = 0;
        if (start > maxstart) start = maxstart;
        H->top_n = H->nvis - start;
        if (H->top_n > cap) H->top_n = cap;
        for (int k = 0; k < H->top_n; k++)
            H->top_apps[k] = native_visible_at(start + k);

        H->top_label_y = HERO_H + HERO_GAP;
        H->top_y = H->top_label_y + LABEL_H + CARD_TOP_GAP;
        H->linux_label_y = H->top_y + SHELF_CARD_H + SECTION_GAP;
    }

    H->linux_y = H->linux_label_y + LABEL_H + CARD_TOP_GAP;
    H->linux_h = HOME_WELL_H;
    H->win_label_y = H->linux_y + H->linux_h + SECTION_GAP;
    H->win_y = H->win_label_y + LABEL_H + CARD_TOP_GAP;
    H->win_h = HOME_WELL_H;
    H->content_h = H->win_y + H->win_h;
}

static void draw_link(store_canvas *c, const store_layout *L, const char *label,
                      float s, float label_y, float scroll_px) {
    float x, y, w, h;
    link_rect(L, label, s, label_y, &x, &y, &w, &h);
    y -= scroll_px;
    float cap = finn_text_cap_height(&g_f_micro);
    c_text(c, &g_f_micro, x, y + cap, label, FINN_DIM);
    c_icon_ink(c, &g_ic_next, x + w - s * 5.0f, y + h * 0.5f,
               s * 10.0f, FINN_MUTE, FINN_MUTE);
}

static void draw_hero(store_canvas *c, const store_layout *L, float s,
                      float y, const finn_app *a, int idx) {
    float w = L->content_w;
    card_bg(c, 0.0f, y, w, s * HERO_H, s, 0);
    c_fill(c, 0.0f, y, w, s * HERO_H, s * CARD_R,
           finn_theme_dark() ? 0x0E0A84FFu : 0x07006FFFu);

    float icon = s * 64.0f;
    draw_app_icon(c, a, idx, s * CARD_PAD,
                  y + (s * HERO_H - icon) * 0.5f, icon);

    float tx = s * CARD_PAD + icon + s * 18.0f;
    float view_w = s * 64.0f, view_h = s * 30.0f;
    float view_x = w - s * CARD_PAD - view_w;
    float view_y = y + (s * HERO_H - view_h) * 0.5f;
    draw_button(c, view_x, view_y, view_w, view_h, s, "View", BTN_QUIET);

    float maxw = view_x - s * 16.0f - tx;
    if (maxw < s * 40.0f) maxw = s * 40.0f;

    c_text(c, &g_f_micro, tx, y + s * 26.0f,
           "FEATURED APP", FINN_MUTE);

    char fitted[192];
    finnapp_ellipsis(&g_f_hero, fitted, sizeof fitted, a->name, maxw);
    c_text(c, &g_f_hero, tx, y + s * 58.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                     a->note[0] ? a->note : a->id, maxw);
    c_text(c, &g_f_sub, tx, y + s * 82.0f, fitted, FINN_DIM);

    draw_state_pill(c, tx, y + s * 90.0f, s,
                    a->installed ? "Installed"
                                 : (app_available(a) ? "Available" : "Unavailable"),
                    a->installed, app_available(a));
}

static void draw_shelf_card(store_canvas *c, float x, float y, float w,
                            float s, const finn_app *a, int idx, int selected) {
    float h = s * SHELF_CARD_H;
    card_bg(c, x, y, w, h, s, selected);
    draw_app_icon(c, a, idx, x + s * CARD_PAD, y + s * 16.0f, s * 40.0f);

    float maxw = w - s * CARD_PAD * 2.0f;
    char fitted[192];
    finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, a->name, maxw);
    c_text(c, &g_f_title, x + s * CARD_PAD, y + s * 76.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                     a->note[0] ? a->note : a->id, maxw);
    c_text(c, &g_f_sub, x + s * CARD_PAD, y + s * 96.0f, fitted, FINN_MUTE);

    draw_status_line(c, x + s * CARD_PAD, y + s * 114.0f, s,
                     a->installed ? "Installed"
                                  : (app_available(a) ? "Available" : "Unavailable"),
                     a->installed, app_available(a));
}

static void draw_home_well(store_canvas *c, const store_layout *L, float s,
                           float y, const char *line, const char *sub) {
    quiet_well(c, 0.0f, y, L->content_w, s * HOME_WELL_H, s, line, sub);
}

static void draw_home(store_canvas *c, const store_layout *L, float s,
                      const home_geom *H, float scroll_px) {
    float sy = -scroll_px;

    if (H->nvis == 0) {
        if (g_load_error)
            quiet_well(c, 0.0f, sy, L->content_w, s * 96.0f, s,
                       "The app catalogue could not be read.",
                       "The registry is missing or unreadable.");
        else if (g_filter[0])
            quiet_well(c, 0.0f, sy, L->content_w, s * 96.0f, s,
                       "No native app matches that search.",
                       "Clear the search to see the catalogue.");
        else
            quiet_well(c, 0.0f, sy, L->content_w, s * 96.0f, s,
                       "The native catalogue is empty.",
                       "This machine's registry has no entries.");
    } else {
        const finn_app *hero = H->hero >= 0 ? finn_apps_at(H->hero) : NULL;
        if (hero) draw_hero(c, L, s, sy, hero, H->hero);

        float y = H->top_label_y * s + sy;
        float cap = finn_text_cap_height(&g_f_micro);
        c_text(c, &g_f_micro, 0.0f, y + cap, "NATIVE APPS", FINN_MUTE);
        draw_link(c, L, "Native Apps", s, H->top_label_y, scroll_px);

        float gap = s * CARD_GAP;
        float slot = (L->content_w - gap * (float)(L->shelf_cols - 1)) /
                     (float)L->shelf_cols;
        for (int k = 0; k < H->top_n; k++) {
            int idx = H->top_apps[k];
            const finn_app *a = finn_apps_at(idx);
            if (!a) continue;
            float cx = (float)k * (slot + gap);
            int selected = g_sel_kind == SEL_NATIVE && !strcmp(g_sel_id, a->id);
            draw_shelf_card(c, cx, H->top_y * s + sy, slot, s, a, idx, selected);
        }
    }

    float y = H->linux_label_y * s + sy;
    float cap = finn_text_cap_height(&g_f_micro);
    c_text(c, &g_f_micro, 0.0f, y + cap, "LINUX APPS", FINN_MUTE);
    draw_link(c, L, "Linux Apps", s, H->linux_label_y, scroll_px);

    char line[192], sub[320];
    if (g_flatpak_error) {
        snprintf(line, sizeof line, "Flatpak could not be read.");
        snprintf(sub, sizeof sub, "%s",
                 g_flatpak_error_text[0] ? g_flatpak_error_text
                                         : "Check the Flatpak runtime and sources.");
    } else if (!g_flatpak_loaded) {
        snprintf(line, sizeof line, "Reading installed Flatpak apps...");
        snprintf(sub, sizeof sub, "The list comes from flatpak.list.");
    } else if (g_flatpak_installed_count == 0) {
        snprintf(line, sizeof line, "No Flatpak apps are installed.");
        snprintf(sub, sizeof sub, "Search for packages on Linux Apps.");
    } else {
        snprintf(line, sizeof line, "%d Flatpak app%s installed",
                 g_flatpak_installed_count,
                 g_flatpak_installed_count == 1 ? "" : "s");
        snprintf(sub, sizeof sub,
                 "Apps run through FinnOS Wayland; some protocols are unsupported.");
    }
    draw_home_well(c, L, s, H->linux_y * s + sy, line, sub);

    y = H->win_label_y * s + sy;
    c_text(c, &g_f_micro, 0.0f, y + cap, "WINDOWS APPS", FINN_MUTE);
    draw_link(c, L, "Windows Apps", s, H->win_label_y, scroll_px);

    if (g_wine.failed) {
        snprintf(line, sizeof line, "The Wine runtime could not be read.");
        snprintf(sub, sizeof sub, "%s",
                 g_wine.error[0] ? g_wine.error
                                 : "Check the control plane and Wine status.");
    } else if (g_wine.available < 0) {
        snprintf(line, sizeof line, "Reading the Wine runtime...");
        snprintf(sub, sizeof sub, "wine.status names the backend that exists.");
    } else if (!g_wine.available) {
        snprintf(line, sizeof line, "Wine is unavailable on this image.");
        snprintf(sub, sizeof sub, "%s",
                 g_wine.reason[0] ? g_wine.reason
                                  : "Windows apps require an x86_64 Wine runtime.");
    } else {
        snprintf(line, sizeof line, "%d Windows app%s registered with Wine",
                 g_wine.count, g_wine.count == 1 ? "" : "s");
        snprintf(sub, sizeof sub, "Open Windows Apps to search winget or launch one.");
    }
    draw_home_well(c, L, s, H->win_y * s + sy, line, sub);
}

/* ---------------- Native Apps ---------------- */

typedef struct {
    int nvis;
    float content_h;
} native_geom;

static void native_build(const store_layout *L, native_geom *G) {
    G->nvis = native_visible_count();
    if (G->nvis == 0) {
        G->content_h = list_top_for(STORE_PAGE_NATIVE) + 78.0f;
        return;
    }
    int rows = (G->nvis + L->grid_cols - 1) / L->grid_cols;
    G->content_h = list_top_for(STORE_PAGE_NATIVE) +
                   (float)rows * GRID_CARD_H +
                   (float)(rows - 1) * CARD_GAP;
}

static float grid_slot(const store_layout *L, float s) {
    int cols = L->grid_cols;
    return (L->content_w - s * CARD_GAP * (float)(cols - 1)) / (float)cols;
}

static void grid_rect(const store_layout *L, float s, int k, float top,
                      float *x, float *y, float *w, float *h) {
    float slot = grid_slot(L, s);
    float gap = s * CARD_GAP;
    int row = k / L->grid_cols;
    int col = k % L->grid_cols;
    *x = (float)col * (slot + gap);
    *y = (s * top) + (float)row * (s * GRID_CARD_H + gap);
    *w = slot;
    *h = s * GRID_CARD_H;
}

static const char *native_primary_label(const finn_app *a) {
    if (!a) return "";
    if (a->installed) return "Open";
    return app_available(a) ? "Install" : "Unavailable";
}

static int native_primary_kind(const finn_app *a) {
    if (!a) return BTN_DISABLED;
    if (a->installed) return BTN_PRIMARY;
    return app_available(a) ? BTN_PRIMARY : BTN_DISABLED;
}

static float action_width(const finn_font *f, const char *label, float s,
                          float minw, float maxw) {
    float w = finn_text_width(f, label) + s * 22.0f;
    if (w < s * minw) w = s * minw;
    if (w > s * maxw) w = s * maxw;
    return w;
}

static void native_card_primary_rect(const store_layout *L, float s, int k,
                                     const finn_app *a, float *x, float *y,
                                     float *w, float *h) {
    float cx, cy, cw, ch;
    grid_rect(L, s, k, list_top_for(STORE_PAGE_NATIVE), &cx, &cy, &cw, &ch);
    *w = action_width(&g_f_label, native_primary_label(a), s, ACTION_MIN_W, 104.0f);
    *h = s * 30.0f;
    *x = cx + cw - s * CARD_PAD - *w;
    *y = cy + s * 134.0f;
}

static void native_card_remove_rect(const store_layout *L, float s, int k,
                                    const finn_app *a, float *x, float *y,
                                    float *w, float *h) {
    float cx, cy, cw, ch;
    grid_rect(L, s, k, list_top_for(STORE_PAGE_NATIVE), &cx, &cy, &cw, &ch);
    const char *label = a && !strcmp(g_confirm_id, a->id) ? "Remove?" : "Remove";
    *w = action_width(&g_f_label, label, s, ACTION_QUIET_W, 82.0f);
    *h = s * 30.0f;
    *x = cx + cw - s * CARD_PAD - *w;
    *y = cy + s * 134.0f;
    float px, py, pw, ph;
    native_card_primary_rect(L, s, k, a, &px, &py, &pw, &ph);
    *x = px - s * 8.0f - *w;
}

static void draw_native_card(store_canvas *c, const store_layout *L, float s,
                             int k, const finn_app *a, int idx, int selected) {
    float x, y, w, h;
    grid_rect(L, s, k, list_top_for(STORE_PAGE_NATIVE), &x, &y, &w, &h);
    card_bg(c, x, y, w, h, s, selected);
    draw_app_icon(c, a, idx, x + s * CARD_PAD, y + s * 16.0f, s * 48.0f);

    float maxw = w - s * CARD_PAD * 2.0f;
    char fitted[192];
    finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, a->name, maxw);
    c_text(c, &g_f_title, x + s * CARD_PAD, y + s * 84.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                     a->note[0] ? a->note : a->id, maxw);
    c_text(c, &g_f_sub, x + s * CARD_PAD, y + s * 104.0f, fitted, FINN_MUTE);

    c_fill(c, x + s * CARD_PAD, y + s * 118.0f,
           w - s * CARD_PAD * 2.0f, 1.0f, 0.0f, FINN_HAIR);

    float ax, ay, aw, ah;
    native_card_primary_rect(L, s, k, a, &ax, &ay, &aw, &ah);
    if (a->installed) {
        float rx, ry, rw, rh;
        native_card_remove_rect(L, s, k, a, &rx, &ry, &rw, &rh);
        draw_button(c, rx, ry, rw, rh, s,
                    !strcmp(g_confirm_id, a->id) ? "Remove?" : "Remove",
                    !strcmp(g_confirm_id, a->id) ? BTN_REMOVE_CONFIRM
                                                 : BTN_REMOVE);
    } else {
        draw_status_line(c, x + s * CARD_PAD,
                         ay + ah * 0.5f +
                         finn_text_cap_height(&g_f_micro) * 0.5f,
                         s,
                         app_available(a) ? "Available" : "Launcher missing",
                         a->installed, app_available(a));
    }
    draw_button(c, ax, ay, aw, ah, s, native_primary_label(a),
                native_primary_kind(a));
}

static void draw_native(store_canvas *c, const store_layout *L, float s,
                        const native_geom *G, float scroll_px) {
    draw_list_label(c, L, s, 0.0f, scroll_px, "CATALOGUE", 1);
    float top = list_top_for(STORE_PAGE_NATIVE);
    if (G->nvis == 0) {
        float y = top * s - scroll_px;
        if (g_load_error)
            quiet_well(c, 0.0f, y, L->content_w, s * 78.0f, s,
                       "The app catalogue could not be read.",
                       "The registry is missing or unreadable.");
        else if (g_filter[0])
            quiet_well(c, 0.0f, y, L->content_w, s * 78.0f, s,
                       "No app matches that search.",
                       "Clear the search to see the catalogue.");
        else
            quiet_well(c, 0.0f, y, L->content_w, s * 78.0f, s,
                       "The catalogue is empty.",
                       "This machine's registry has no entries.");
        return;
    }

    for (int k = 0; k < G->nvis; k++) {
        int idx = native_visible_at(k);
        const finn_app *a = finn_apps_at(idx);
        if (!a) continue;
        float x, y, w, h;
        grid_rect(L, s, k, top, &x, &y, &w, &h);
        draw_native_card(c, L, s, k, a, idx,
                         g_sel_kind == SEL_NATIVE && !strcmp(g_sel_id, a->id));
        (void)h;
    }
}

/* ---------------- native detail ---------------- */

typedef struct {
    float back_x, back_y, back_w, back_h;
    float action_x, action_y, action_w, action_h;
    float remove_x, remove_w;
    float vp_y, vp_h;
} detail_geom;

static int detail_stacked(const store_layout *L, float s) {
    return L->content_w < s * 420.0f;
}

static void detail_metrics(const store_layout *L, float s,
                           const finn_app *a, detail_geom *G) {
    memset(G, 0, sizeof *G);
    G->back_w = s * DETAIL_BACK_W;
    G->back_h = s * DETAIL_BACK_H;
    G->back_x = 0.0f;
    G->back_y = 0.0f;

    G->action_h = s * ACTION_H;
    G->action_w = action_width(&g_f_label, native_primary_label(a), s,
                               DETAIL_ACTION_W, 132.0f);
    if (G->action_w > L->content_w) G->action_w = L->content_w;
    G->action_x = L->content_w - G->action_w;

    if (a && a->installed) {
        const char *label = !strcmp(g_confirm_id, a->id) ? "Remove?" : "Remove";
        G->remove_w = action_width(&g_f_label, label, s, ACTION_QUIET_W, 96.0f);
        G->remove_x = G->action_x - s * 8.0f - G->remove_w;
    }

    G->action_y = (L->content_bottom - L->content_y) - G->action_h;
    G->vp_y = G->back_h + s * 10.0f;
    G->vp_h = G->action_y - s * 10.0f - G->vp_y;
    if (G->vp_h < 0.0f) G->vp_h = 0.0f;
}

static const char *detail_explain(const finn_app *a) {
    if (!a) return "";
    if (a->installed)
        return "This app is installed. Open starts it through finnd; Remove "
               "takes it off this machine after a second press.";
    if (app_available(a))
        return "This app is in the catalogue but is not installed. Installing "
               "marks it available to the Start Menu.";
    return "This app is in the catalogue, but its launcher is not on this "
           "machine. It cannot be installed here.";
}

#define DETAIL_NOTE \
    "Finn Store installs from the catalogue that ships with FinnOS. " \
    "This build does not download packages."

static float detail_content_h(const store_layout *L, float s,
                              const finn_app *a) {
    int stacked = detail_stacked(L, s);
    float block = stacked ? 164.0f : DETAIL_ICON_W;
    int lines = wrap_lines(&g_f_sub, detail_explain(a), L->content_w);
    int flines = wrap_lines(&g_f_micro, DETAIL_NOTE, L->content_w);
    float divider = block + 16.0f;
    float about_top = divider + 16.0f;
    float body_top = about_top + 22.0f;
    float body_end = body_top + (float)lines * 17.0f;
    float meta_y = body_end + 16.0f;
    float foot_y = meta_y + 90.0f + 12.0f;
    return foot_y + (float)flines * 16.0f + 8.0f;
}

static void draw_meta_row(store_canvas *c, float x, float y, float w,
                          float s, const char *label, const char *value) {
    float cap_l = finn_text_cap_height(&g_f_sub);
    float cap_v = finn_text_cap_height(&g_f_label);
    float middle = y + s * 15.0f;
    c_text(c, &g_f_sub, x + s * CARD_PAD, middle + cap_l * 0.5f,
           label, FINN_MUTE);

    float lw = finn_text_width(&g_f_sub, label);
    float maxw = w - s * CARD_PAD * 2.0f - lw - s * 12.0f;
    char fitted[320];
    finnapp_ellipsis(&g_f_label, fitted, sizeof fitted, value,
                     maxw > s * 20.0f ? maxw : s * 20.0f);
    float vw = finn_text_width(&g_f_label, fitted);
    c_text(c, &g_f_label, x + w - s * CARD_PAD - vw,
           middle + cap_v * 0.5f, fitted, FINN_INK);
}

static void draw_detail_content(store_canvas *c, const store_layout *L,
                                float s, const finn_app *a, int idx,
                                int stacked, float scroll_px) {
    float sy = -scroll_px;
    float block;

    if (stacked) {
        draw_app_icon(c, a, idx, 0.0f, sy, s * DETAIL_ICON_N);
        float tx = 0.0f;
        float ty = sy + s * 90.0f;
        char fitted[192];
        finnapp_ellipsis(&g_f_hero, fitted, sizeof fitted, a->name,
                         L->content_w);
        c_text(c, &g_f_hero, tx, ty, fitted, FINN_INK);
        finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                         a->note[0] ? a->note : a->id, L->content_w);
        c_text(c, &g_f_sub, tx, ty + s * 24.0f, fitted, FINN_DIM);
        draw_state_pill(c, tx, ty + s * 32.0f, s,
                        a->installed ? "Installed"
                                     : (app_available(a) ? "Available" : "Unavailable"),
                        a->installed, app_available(a));
        block = 164.0f;
    } else {
        draw_app_icon(c, a, idx, 0.0f, sy, s * DETAIL_ICON_W);
        float tx = s * 100.0f;
        float far = L->content_w - tx;
        char fitted[192];
        finnapp_ellipsis(&g_f_hero, fitted, sizeof fitted, a->name, far);
        c_text(c, &g_f_hero, tx, sy + s * 30.0f, fitted, FINN_INK);
        finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                         a->note[0] ? a->note : a->id, far);
        c_text(c, &g_f_sub, tx, sy + s * 54.0f, fitted, FINN_DIM);
        draw_state_pill(c, tx, sy + s * 62.0f, s,
                        a->installed ? "Installed"
                                     : (app_available(a) ? "Available" : "Unavailable"),
                        a->installed, app_available(a));
        block = DETAIL_ICON_W;
    }

    float divider = block + 16.0f;
    c_fill(c, 0.0f, sy + s * divider, L->content_w, 1.0f, 0.0f, FINN_HAIR);

    float about_top = divider + 16.0f;
    c_text(c, &g_f_micro, 0.0f,
           sy + s * about_top + finn_text_cap_height(&g_f_micro),
           "ABOUT THIS APP", FINN_MUTE);

    float body_top = about_top + 22.0f;
    int lines = draw_wrapped(c, &g_f_sub, detail_explain(a), 0.0f,
                             sy + s * body_top, L->content_w, s * 17.0f,
                             FINN_DIM);

    float meta_y = body_top + (float)lines * 17.0f + 16.0f;
    float meta_h = 90.0f;
    c_fill(c, 0.0f, sy + s * meta_y, L->content_w, s * meta_h,
           s * CARD_R, finn_theme_dark() ? 0x1AFFFFFFu : 0x0D000000u);
    c_stroke(c, 0.0f, sy + s * meta_y, L->content_w, s * meta_h,
             s * CARD_R, 1.0f, FINN_HAIR);

    draw_meta_row(c, 0.0f, sy + s * meta_y, L->content_w, s,
                  "Catalogue id", a->id);
    c_fill(c, s * CARD_PAD, sy + s * (meta_y + 30.0f),
           L->content_w - s * CARD_PAD * 2.0f, 1.0f, 0.0f, FINN_HAIR);
    draw_meta_row(c, 0.0f, sy + s * (meta_y + 30.0f), L->content_w, s,
                  "Launcher", a->command[0] ? a->command : "None");
    c_fill(c, s * CARD_PAD, sy + s * (meta_y + 60.0f),
           L->content_w - s * CARD_PAD * 2.0f, 1.0f, 0.0f, FINN_HAIR);
    draw_meta_row(c, 0.0f, sy + s * (meta_y + 60.0f), L->content_w, s,
                  "Source", "Built-in catalogue");

    float foot_y = meta_y + meta_h + 12.0f;
    draw_wrapped(c, &g_f_micro, DETAIL_NOTE, 0.0f, sy + s * foot_y,
                 L->content_w, s * 16.0f, FINN_MUTE);
}

static void draw_detail_fixed(store_canvas *c, const store_layout *L,
                              float s, const detail_geom *G,
                              const finn_app *a) {
    /* Back is a quiet pill rather than a bare arrow: it stays visible over
     * the bright icon artwork at the top of the page. */
    float bx = G->back_x, by = G->back_y;
    float bh = G->back_h, bw = G->back_w;
    c_fill(c, bx, by, bw, bh, bh * 0.5f,
           finn_theme_dark() ? 0x1AFFFFFFu : 0x0A000000u);
    c_stroke(c, bx, by, bw, bh, bh * 0.5f, 1.0f, FINN_HAIR);
    c_icon_ink(c, &g_ic_back, bx + s * 13.0f, by + bh * 0.5f,
               s * 11.0f, FINN_DIM, FINN_DIM);
    c_text(c, &g_f_sub, bx + s * 24.0f,
           by + bh * 0.5f + finn_text_cap_height(&g_f_sub) * 0.5f,
           "Back", FINN_DIM);

    c_fill(c, G->back_x, G->action_y - s * 10.0f, L->content_w, 1.0f,
           0.0f, FINN_HAIR);

    if (a && a->installed) {
        float maxw = G->remove_x - s * 14.0f - G->back_x;
        if (maxw > s * 40.0f && !strcmp(g_confirm_id, a->id)) {
            char fitted[192];
            finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                             "Press Remove again to confirm.", maxw);
            c_text(c, &g_f_sub, G->back_x,
                   G->action_y + G->action_h * 0.5f +
                   finn_text_cap_height(&g_f_sub) * 0.5f,
                   fitted, FINN_DIM);
        }
        draw_button(c, G->remove_x, G->action_y, G->remove_w, G->action_h, s,
                    !strcmp(g_confirm_id, a->id) ? "Remove?" : "Remove",
                    !strcmp(g_confirm_id, a->id) ? BTN_REMOVE_CONFIRM
                                                 : BTN_REMOVE);
    }

    draw_button(c, G->action_x, G->action_y, G->action_w, G->action_h, s,
                native_primary_label(a), native_primary_kind(a));
}

/* ---------------- Linux Apps ---------------- */

typedef struct {
    int nvis;
    float content_h;
} linux_geom;

static void linux_build(const store_layout *L, linux_geom *G) {
    G->nvis = flatpak_linux_count();
    float top = list_top_for(STORE_PAGE_LINUX);
    if (G->nvis == 0) {
        G->content_h = top + 78.0f;
        return;
    }
    int rows = (G->nvis + L->grid_cols - 1) / L->grid_cols;
    G->content_h = top + (float)rows * GRID_CARD_H +
                   (float)(rows - 1) * CARD_GAP;
}

static const char *flatpak_primary_label(const flatpak_app *a) {
    if (!a) return "";
    if (g_flatpak_job_app[0] && !strcmp(g_flatpak_job_app, a->id))
        return "Working";
    if (a->installed) return "Open";
    return a->remote[0] ? "Install" : "Unavailable";
}

static int flatpak_primary_kind(const flatpak_app *a) {
    if (!a) return BTN_DISABLED;
    if (g_flatpak_job_app[0] && !strcmp(g_flatpak_job_app, a->id))
        return BTN_DISABLED;
    if (a->installed) return BTN_PRIMARY;
    return a->remote[0] ? BTN_PRIMARY : BTN_DISABLED;
}

static int flatpak_show_remove(const flatpak_app *a) {
    return a && a->installed &&
           !(g_flatpak_job_app[0] && !strcmp(g_flatpak_job_app, a->id));
}

static void flatpak_card_primary_rect(const store_layout *L, float s, int k,
                                      const flatpak_app *a, float *x, float *y,
                                      float *w, float *h) {
    float cx, cy, cw, ch;
    grid_rect(L, s, k, list_top_for(STORE_PAGE_LINUX), &cx, &cy, &cw, &ch);
    *w = action_width(&g_f_label, flatpak_primary_label(a), s, ACTION_MIN_W, 104.0f);
    *h = s * 30.0f;
    *x = cx + cw - s * CARD_PAD - *w;
    *y = cy + s * 134.0f;
}

static void flatpak_card_remove_rect(const store_layout *L, float s, int k,
                                     const flatpak_app *a, float *x, float *y,
                                     float *w, float *h) {
    float cx, cy, cw, ch;
    grid_rect(L, s, k, list_top_for(STORE_PAGE_LINUX), &cx, &cy, &cw, &ch);
    const char *label = a && !strcmp(g_confirm_id, a->id) ? "Remove?" : "Remove";
    *w = action_width(&g_f_label, label, s, ACTION_QUIET_W, 82.0f);
    *h = s * 30.0f;
    *x = cx + cw - s * CARD_PAD - *w;
    *y = cy + s * 134.0f;
    float px, py, pw, ph;
    flatpak_card_primary_rect(L, s, k, a, &px, &py, &pw, &ph);
    *x = px - s * 8.0f - *w;
}

static void draw_flatpak_card(store_canvas *c, const store_layout *L, float s,
                              int k, const flatpak_app *a, int selected) {
    float x, y, w, h;
    grid_rect(L, s, k, list_top_for(STORE_PAGE_LINUX), &x, &y, &w, &h);
    card_bg(c, x, y, w, h, s, selected);

    float icon = s * 42.0f;
    draw_source_glyph(c, &g_ic_store, x + s * CARD_PAD, y + s * 16.0f, icon);

    float tx = x + s * CARD_PAD;
    float text_w = w - s * CARD_PAD * 2.0f;
    char fitted[FLATPAK_APP_NOTE_CAP];
    finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, a->name, text_w);
    c_text(c, &g_f_title, tx, y + s * 78.0f, fitted, FINN_INK);
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, a->id, text_w);
    c_text(c, &g_f_sub, tx, y + s * 98.0f, fitted, FINN_MUTE);

    char state[FLATPAK_APP_REMOTE_CAP + 64];
    if (a->installed)
        snprintf(state, sizeof state, "Installed%s%s",
                 a->remote[0] ? " · " : "",
                 a->remote[0] ? a->remote : "");
    else if (a->remote[0])
        snprintf(state, sizeof state, "From %s; not installed", a->remote);
    else
        snprintf(state, sizeof state, "No install source available");
    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, state, text_w);
    c_text(c, &g_f_micro, tx, y + s * 120.0f, fitted, FINN_DIM);

    c_fill(c, tx, y + s * 126.0f, text_w, 1.0f, 0.0f, FINN_HAIR);

    float ax, ay, aw, ah;
    flatpak_card_primary_rect(L, s, k, a, &ax, &ay, &aw, &ah);
    if (flatpak_show_remove(a)) {
        float rx, ry, rw, rh;
        flatpak_card_remove_rect(L, s, k, a, &rx, &ry, &rw, &rh);
        draw_button(c, rx, ry, rw, rh, s,
                    !strcmp(g_confirm_id, a->id) ? "Remove?" : "Remove",
                    !strcmp(g_confirm_id, a->id) ? BTN_REMOVE_CONFIRM
                                                 : BTN_REMOVE);
    }
    draw_button(c, ax, ay, aw, ah, s, flatpak_primary_label(a),
                flatpak_primary_kind(a));
}

static void draw_linux(store_canvas *c, const store_layout *L, float s,
                       const linux_geom *G, float scroll_px) {
    int searching = flatpak_search_current();
    draw_list_label(c, L, s, 0.0f, scroll_px,
                    searching ? "SEARCH RESULTS" : "INSTALLED FLATPAK APPS", 1);
    draw_list_note(c, L, s, 0.0f, scroll_px,
                   "Apps run through FinnOS Wayland; apps needing unsupported protocols may not run.");

    float top = list_top_for(STORE_PAGE_LINUX);
    if (g_flatpak_error) {
        quiet_well(c, 0.0f, top * s - scroll_px, L->content_w, s * 78.0f, s,
                   g_flatpak_error_text[0] ? g_flatpak_error_text
                                           : "Flatpak could not be read.",
                   "Check the Flatpak runtime and configured sources.");
        return;
    }
    if (!g_flatpak_loaded) {
        quiet_well(c, 0.0f, top * s - scroll_px, L->content_w, s * 78.0f, s,
                   "Reading installed Flatpak apps...",
                   "The list comes from flatpak.list through finnd.");
        return;
    }
    if (G->nvis == 0) {
        const char *line, *sub;
        if (searching) {
            line = "No Flatpak app matches that search.";
            sub = "Try another name or application id.";
        } else if (g_filter[0]) {
            line = "No installed Flatpak app matches that search.";
            sub = "Press Enter to search Flathub instead.";
        } else {
            line = "No Flatpak apps are installed.";
            sub = "Type a search above and press Enter to browse.";
        }
        quiet_well(c, 0.0f, top * s - scroll_px, L->content_w, s * 78.0f, s,
                   line, sub);
        return;
    }

    for (int k = 0; k < G->nvis; k++) {
        int idx = flatpak_linux_at(k);
        const flatpak_app *a = flatpak_linux_item(idx);
        if (!a) continue;
        draw_flatpak_card(c, L, s, k, a,
                          g_sel_kind == SEL_FLATPAK && !strcmp(g_sel_id, a->id));
    }
}

/* ---------------- Windows Apps ---------------- */

typedef struct {
    int results_visible;
    int result_count;
    float results_top;
    float registered_label_y;
    float wine_top;
    int wine_count;
    float content_h;
} windows_geom;

static void wine_note(char *out, size_t cap) {
    if (g_wine.failed) {
        snprintf(out, cap, "The Wine runtime could not be read: %.200s",
                 g_wine.error[0] ? g_wine.error : "unknown error");
    } else if (g_wine.available < 0) {
        snprintf(out, cap, "Reading the Wine runtime...");
    } else if (!g_wine.available) {
        snprintf(out, cap, "Wine is unavailable: %.200s",
                 g_wine.reason[0] ? g_wine.reason : "no runtime");
    } else {
        char prefix[160];
        snprintf(prefix, sizeof prefix, "%s", g_wine.prefix);
        snprintf(out, cap, "Wine %s · %s · %s · prefix %s",
                 g_wine.version[0] ? g_wine.version : "version unknown",
                 g_wine.backend[0] ? g_wine.backend : "backend unknown",
                 g_wine.arch[0] ? g_wine.arch : "arch unknown",
                 prefix[0] ? prefix : "unknown");
    }
}

static void windows_build(const store_layout *L, windows_geom *G) {
    (void)L;
    memset(G, 0, sizeof *G);
    G->results_visible = winget_results_visible();
    G->result_count = G->results_visible ? g_winget_count : 0;
    G->results_top = list_top_for(STORE_PAGE_WINDOWS);

    float y = G->results_top;
    if (!G->results_visible || g_winget_error || G->result_count == 0) {
        y += 78.0f + SECTION_GAP;
    } else {
        y += (float)G->result_count * (WIN_ROW_H + CARD_GAP) - CARD_GAP +
             SECTION_GAP;
    }

    G->registered_label_y = y;
    G->wine_top = y + LIST_LABEL_H;
    G->wine_count = (g_wine.available == 1) ? wine_visible_count() : 0;
    if (G->wine_count > 0)
        G->content_h = G->wine_top + (float)G->wine_count *
                       (LIB_ROW_H + CARD_GAP) - CARD_GAP;
    else
        G->content_h = G->wine_top + 78.0f;
}

static void winget_row_rect(const store_layout *L, float s, int k,
                            float *x, float *y, float *w, float *h) {
    *x = 0.0f;
    *y = s * list_top_for(STORE_PAGE_WINDOWS) +
         (float)k * (s * WIN_ROW_H + s * CARD_GAP);
    *w = L->content_w;
    *h = s * WIN_ROW_H;
}

static float winget_action_width(const winget_result *r, float s) {
    const char *label = "Install";
    if (g_winget_job_app[0] && r && !strcmp(g_winget_job_app, r->id))
        label = "Working";
    else {
        const char *why = "";
        if (!winget_install_ready(r, &why)) label = "Unavailable";
    }
    return action_width(&g_f_label, label, s, 76.0f, 104.0f);
}

static void winget_row_action_rect(const store_layout *L, float s, int k,
                                   const winget_result *r, float *x, float *y,
                                   float *w, float *h) {
    float rx, ry, rw, rh;
    winget_row_rect(L, s, k, &rx, &ry, &rw, &rh);
    *w = winget_action_width(r, s);
    *h = s * 30.0f;
    *x = rx + rw - s * CARD_PAD - *w;
    *y = ry + (rh - *h) * 0.5f;
}

static void winget_meta(const winget_result *r, char *out, size_t cap) {
    out[0] = '\0';
    size_t used = 0;
    const char *parts[3] = { r->version, r->publisher, r->installer_type };
    for (int i = 0; i < 3; i++) {
        if (!parts[i][0]) continue;
        int n = snprintf(out + used, cap - used, "%s%s", used ? " · " : "",
                         parts[i]);
        if (n <= 0 || (size_t)n >= cap - used) break;
        used += (size_t)n;
    }
    if (!out[0]) snprintf(out, cap, "Version, publisher and installer type unavailable");
}

static void winget_row_state(const winget_result *r, char *out, size_t cap) {
    if (g_winget_job_app[0] && !strcmp(g_winget_job_app, r->id)) {
        snprintf(out, cap, "Install task %s.",
                 g_winget_job_state[0] ? g_winget_job_state : "queued");
        return;
    }
    winget_info *info = winget_info_find(r->id);
    if (!info) {
        snprintf(out, cap, "Select to check installer details.");
        return;
    }
    if (info->failed) {
        snprintf(out, cap, "%s", info->error[0] ? info->error
                                               : "Package details unavailable.");
        return;
    }
    const char *why = "";
    if (winget_install_ready(r, &why)) {
        snprintf(out, cap, "Verified HTTPS installer with SHA-256%s%s.",
                 info->installer_arch[0] ? " · " : "",
                 info->installer_arch[0] ? info->installer_arch : "");
    } else {
        snprintf(out, cap, "%s", why[0] ? why : "Install is unavailable.");
    }
}

static void draw_winget_row(store_canvas *c, const store_layout *L, float s,
                            int k, const winget_result *r, int selected) {
    float x, y, w, h;
    winget_row_rect(L, s, k, &x, &y, &w, &h);
    card_bg(c, x, y, w, h, s, selected);

    float icon = s * 40.0f;
    draw_source_glyph(c, &g_ic_windows, x + s * CARD_PAD,
                      y + (h - icon) * 0.5f, icon);

    float ax, ay, aw, ah;
    winget_row_action_rect(L, s, k, r, &ax, &ay, &aw, &ah);
    float tx = x + s * CARD_PAD + icon + s * 14.0f;
    float text_w = ax - s * 12.0f - tx;
    if (text_w < s * 40.0f) text_w = s * 40.0f;

    char fitted[WINGET_DESC_CAP];
    finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, r->name, text_w);
    c_text(c, &g_f_title, tx, y + s * 28.0f, fitted, FINN_INK);

    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, r->id, text_w);
    c_text(c, &g_f_sub, tx, y + s * 48.0f, fitted, FINN_MUTE);

    char meta[384];
    winget_meta(r, meta, sizeof meta);
    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, meta, text_w);
    c_text(c, &g_f_micro, tx, y + s * 68.0f, fitted, FINN_DIM);

    winget_info *info = winget_info_find(r->id);
    const char *description = info && !info->failed && info->description[0]
        ? info->description
        : (info && info->failed
           ? "Package details could not be read."
           : "Installer details are read from the winget manifest when selected.");
    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, description, text_w);
    c_text(c, &g_f_micro, tx, y + s * 88.0f, fitted, FINN_MUTE);

    char state[WINGET_DESC_CAP];
    winget_row_state(r, state, sizeof state);
    finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, state, text_w);
    c_text(c, &g_f_micro, tx, y + s * 106.0f, fitted,
           winget_install_ready(r, NULL) ? FINN_DIM : FINN_MUTE);

    const char *label = "Install";
    int kind = BTN_PRIMARY;
    if (g_winget_job_app[0] && !strcmp(g_winget_job_app, r->id)) {
        label = "Working";
        kind = BTN_DISABLED;
    } else if (!winget_install_ready(r, NULL)) {
        label = "Unavailable";
        kind = BTN_DISABLED;
    }
    draw_button(c, ax, ay, aw, ah, s, label, kind);
}

static void draw_wine_row(store_canvas *c, const store_layout *L, float s,
                          float top, int k, const wine_app *app,
                          int selected) {
    float x = 0.0f;
    float y = s * top + (float)k * (s * LIB_ROW_H + s * CARD_GAP);
    float w = L->content_w;
    float h = s * LIB_ROW_H;
    card_bg(c, x, y, w, h, s, selected);

    float icon = s * 40.0f;
    draw_source_glyph(c, &g_ic_windows, x + s * CARD_PAD,
                      y + (h - icon) * 0.5f, icon);

    float aw = s * 72.0f, ah = s * 30.0f;
    float ax = x + w - s * CARD_PAD - aw;
    float ay = y + (h - ah) * 0.5f;
    float tx = x + s * CARD_PAD + icon + s * 14.0f;
    float text_w = ax - s * 12.0f - tx;
    if (text_w < s * 30.0f) text_w = s * 30.0f;

    char fitted[WINE_APP_ID_CAP + 32];
    finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, app->id, text_w);
    c_text(c, &g_f_title, tx, y + s * 30.0f, fitted, FINN_INK);
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted,
                     "Registered Windows app · Wine", text_w);
    c_text(c, &g_f_sub, tx, y + s * 50.0f, fitted, FINN_MUTE);
    draw_button(c, ax, ay, aw, ah, s, "Open", BTN_PRIMARY);
}

static void draw_windows(store_canvas *c, const store_layout *L, float s,
                         const windows_geom *G, float scroll_px) {
    draw_list_label(c, L, s, 0.0f, scroll_px,
                    G->results_visible ? "WINGET RESULTS" : "SEARCH WINGET", 1);

    char note[384];
    wine_note(note, sizeof note);
    draw_list_note(c, L, s, 0.0f, scroll_px, note);

    float top = G->results_top;
    if (!G->results_visible) {
        const char *line = g_filter[0]
            ? "Press Enter to search winget for this name."
            : "Search winget for Windows apps.";
        const char *sub = g_filter[0]
            ? "Results are checked against the winget manifest before install."
            : "Type a name above and press Enter. Installers run through Wine.";
        quiet_well(c, 0.0f, top * s - scroll_px, L->content_w, s * 78.0f, s,
                   line, sub);
    } else if (g_winget_error) {
        quiet_well(c, 0.0f, top * s - scroll_px, L->content_w, s * 78.0f, s,
                   g_winget_error_text[0] ? g_winget_error_text
                                          : "The winget search could not be read.",
                   "Check the control plane and the winget helper.");
    } else if (G->result_count == 0) {
        quiet_well(c, 0.0f, top * s - scroll_px, L->content_w, s * 78.0f, s,
                   "No winget package matches that search.",
                   "Try a publisher or package id.");
    } else {
        for (int k = 0; k < G->result_count; k++) {
            const winget_result *r = &g_winget_results[k];
            draw_winget_row(c, L, s, k, r,
                            g_sel_kind == SEL_WINGET && !strcmp(g_sel_id, r->id));
        }
    }

    float y = G->registered_label_y * s - scroll_px;
    float cap = finn_text_cap_height(&g_f_micro);
    char label[96];
    snprintf(label, sizeof label, "REGISTERED WITH WINE (%d)", wine_visible_count());
    c_text(c, &g_f_micro, 0.0f, y + cap, label, FINN_MUTE);

    if (G->wine_count == 0) {
        const char *line, *sub;
        if (g_wine.failed) {
            line = "The Wine registry could not be read.";
            sub = g_wine.error[0] ? g_wine.error : "Check the control plane.";
        } else if (g_wine.available < 0) {
            line = "Reading registered Windows apps...";
            sub = "The list comes from wine.list.";
        } else if (!g_wine.available) {
            line = "Wine is unavailable on this image.";
            sub = g_wine.reason[0] ? g_wine.reason
                                   : "Windows apps require an x86_64 Wine runtime.";
        } else if (g_wine.list_failed) {
            line = "Registered Windows apps could not be read.";
            sub = g_wine.error[0] ? g_wine.error : "Check the Wine registry.";
        } else if (g_filter[0]) {
            line = "No registered Windows app matches that search.";
            sub = "Clear the search to see the registry.";
        } else {
            line = "No Windows apps are registered yet.";
            sub = "A winget install task that finishes is re-read here; exiting is not registering.";
        }
        quiet_well(c, 0.0f, G->wine_top * s - scroll_px, L->content_w,
                   s * 78.0f, s, line, sub);
        return;
    }

    for (int k = 0; k < G->wine_count; k++) {
        int idx = wine_visible_at(k);
        if (idx < 0) continue;
        draw_wine_row(c, L, s, G->wine_top, k, &g_wine.apps[idx],
                      g_sel_kind == SEL_WINE &&
                      !strcmp(g_sel_id, g_wine.apps[idx].id));
    }
}

/* ---------------- Library ---------------- */

typedef struct {
    int n;
    float content_h;
} library_geom;

static void library_build_entries(void) {
    g_library_count = 0;

    int n = finn_apps_count();
    for (int i = 0; i < n && g_library_count < (int)(sizeof g_library /
                                                      sizeof g_library[0]); i++) {
        const finn_app *a = finn_apps_at(i);
        if (!a || !a->installed || !native_matches(i)) continue;
        g_library[g_library_count].kind = LIB_NATIVE;
        g_library[g_library_count].index = i;
        g_library_count++;
    }

    for (int i = 0; i < g_flatpak_installed_count &&
                    g_library_count < (int)(sizeof g_library / sizeof g_library[0]);
         i++) {
        if (!flatpak_matches(&g_flatpak_installed[i])) continue;
        g_library[g_library_count].kind = LIB_FLATPAK;
        g_library[g_library_count].index = i;
        g_library_count++;
    }

    for (int i = 0; i < g_wine.count &&
                    g_library_count < (int)(sizeof g_library / sizeof g_library[0]);
         i++) {
        if (!wine_matches(&g_wine.apps[i])) continue;
        g_library[g_library_count].kind = LIB_WINE;
        g_library[g_library_count].index = i;
        g_library_count++;
    }
}

static int library_find(int kind, const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g_library_count; i++) {
        if (g_library[i].kind != kind) continue;
        const char *entry_id = NULL;
        if (kind == LIB_NATIVE) {
            const finn_app *a = finn_apps_at(g_library[i].index);
            entry_id = a ? a->id : NULL;
        } else if (kind == LIB_FLATPAK) {
            entry_id = g_flatpak_installed[g_library[i].index].id;
        } else if (kind == LIB_WINE) {
            entry_id = g_wine.apps[g_library[i].index].id;
        }
        if (entry_id && !strcmp(entry_id, id)) return i;
    }
    return -1;
}

static void library_build(const store_layout *L, library_geom *G) {
    (void)L;
    library_build_entries();
    G->n = g_library_count;
    float top = list_top_for(STORE_PAGE_LIBRARY);
    if (G->n == 0) {
        G->content_h = top + 78.0f;
        return;
    }
    G->content_h = top + (float)G->n * (LIB_ROW_H + CARD_GAP) - CARD_GAP;
}

static void library_row_rect(const store_layout *L, float s, int k,
                             float *x, float *y, float *w, float *h) {
    *x = 0.0f;
    *y = s * list_top_for(STORE_PAGE_LIBRARY) +
         (float)k * (s * LIB_ROW_H + s * CARD_GAP);
    *w = L->content_w;
    *h = s * LIB_ROW_H;
}

static void library_action_rect(const store_layout *L, float s, int k,
                                float *x, float *y, float *w, float *h) {
    float rx, ry, rw, rh;
    library_row_rect(L, s, k, &rx, &ry, &rw, &rh);
    *w = s * 72.0f;
    *h = s * 30.0f;
    *x = rx + rw - s * CARD_PAD - *w;
    *y = ry + (rh - *h) * 0.5f;
}

static void library_open(const library_entry *e) {
    if (!e) return;
    if (e->kind == LIB_NATIVE) {
        native_launch(e->index);
    } else if (e->kind == LIB_FLATPAK) {
        if (e->index >= 0 && e->index < g_flatpak_installed_count)
            flatpak_run_app(&g_flatpak_installed[e->index]);
    } else if (e->kind == LIB_WINE) {
        if (e->index >= 0 && e->index < g_wine.count)
            wine_run_app(&g_wine.apps[e->index]);
    }
}

static void draw_library_row(store_canvas *c, const store_layout *L, float s,
                             int k, const library_entry *e, int selected) {
    float x, y, w, h;
    library_row_rect(L, s, k, &x, &y, &w, &h);
    card_bg(c, x, y, w, h, s, selected);

    float icon = s * 40.0f;
    float ix = x + s * CARD_PAD;
    float iy = y + (h - icon) * 0.5f;
    const char *name = "";
    const char *meta = "";
    char idbuf[FLATPAK_APP_ID_CAP] = "";

    if (e->kind == LIB_NATIVE) {
        const finn_app *a = finn_apps_at(e->index);
        if (!a) return;
        draw_app_icon(c, a, e->index, ix, iy, icon);
        name = a->name;
        meta = "Native · FinnOS catalogue";
        snprintf(idbuf, sizeof idbuf, "%s", a->id);
    } else if (e->kind == LIB_FLATPAK) {
        const flatpak_app *a = &g_flatpak_installed[e->index];
        draw_source_glyph(c, &g_ic_store, ix, iy, icon);
        name = a->name;
        meta = a->remote[0] ? "Flatpak · remote" : "Flatpak";
        if (a->remote[0]) {
            static char metabuf[FLATPAK_APP_REMOTE_CAP + 16];
            snprintf(metabuf, sizeof metabuf, "Flatpak · %s", a->remote);
            meta = metabuf;
        }
        snprintf(idbuf, sizeof idbuf, "%s", a->id);
    } else {
        const wine_app *a = &g_wine.apps[e->index];
        draw_source_glyph(c, &g_ic_windows, ix, iy, icon);
        name = a->id;
        meta = "Windows · registered with Wine";
        snprintf(idbuf, sizeof idbuf, "%s", a->id);
    }

    float ax, ay, aw, ah;
    library_action_rect(L, s, k, &ax, &ay, &aw, &ah);
    float tx = ix + icon + s * 14.0f;
    float text_w = ax - s * 12.0f - tx;
    if (text_w < s * 30.0f) text_w = s * 30.0f;

    char fitted[FLATPAK_APP_NOTE_CAP];
    finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, name, text_w);
    c_text(c, &g_f_title, tx, y + s * 30.0f, fitted, FINN_INK);
    finnapp_ellipsis(&g_f_sub, fitted, sizeof fitted, meta, text_w);
    c_text(c, &g_f_sub, tx, y + s * 50.0f, fitted, FINN_MUTE);
    (void)idbuf;

    draw_button(c, ax, ay, aw, ah, s, "Open", BTN_PRIMARY);
}

static void draw_library(store_canvas *c, const store_layout *L, float s,
                         const library_geom *G, float scroll_px) {
    draw_list_label(c, L, s, 0.0f, scroll_px, "INSTALLED APPS", 1);
    float top = list_top_for(STORE_PAGE_LIBRARY);
    if (G->n == 0) {
        const char *line, *sub;
        if (g_filter[0]) {
            line = "No installed app matches that search.";
            sub = "Clear the search to see every source.";
        } else if (g_load_error) {
            line = "The native catalogue could not be read.";
            sub = "Flatpak and Wine entries are still listed when they exist.";
        } else {
            line = "Nothing is installed yet.";
            sub = "Install from Native Apps, Linux Apps or Windows Apps.";
        }
        quiet_well(c, 0.0f, top * s - scroll_px, L->content_w, s * 78.0f, s,
                   line, sub);
        return;
    }

    for (int k = 0; k < G->n; k++) {
        const library_entry *e = &g_library[k];
        draw_library_row(c, L, s, k, e,
                         g_sel_kind != SEL_NONE &&
                         library_find(g_sel_kind, g_sel_id) == k);
    }
}

/* ---------------- navigation and selection ---------------- */

static const char *library_entry_id(const library_entry *e) {
    if (!e) return NULL;
    if (e->kind == LIB_NATIVE) {
        const finn_app *a = finn_apps_at(e->index);
        return a ? a->id : NULL;
    }
    if (e->kind == LIB_FLATPAK)
        return g_flatpak_installed[e->index].id;
    if (e->kind == LIB_WINE)
        return g_wine.apps[e->index].id;
    return NULL;
}

static int native_selection_visible(void) {
    int idx = find_index(g_sel_id);
    return idx >= 0 && native_matches(idx);
}

static int flatpak_selection_visible(void) {
    if (g_sel_kind != SEL_FLATPAK) return 0;
    int idx = flatpak_search_current() ? flatpak_result_index(g_sel_id)
                                       : flatpak_installed_index(g_sel_id);
    return idx >= 0 && flatpak_linux_index_of(idx) >= 0;
}

static int winget_selection_visible(void) {
    if (g_sel_kind != SEL_WINGET) return 0;
    if (!winget_results_visible()) return 0;
    return winget_index(g_sel_id) >= 0;
}

static int wine_selection_visible(void) {
    if (g_sel_kind != SEL_WINE) return 0;
    int idx = wine_index(g_sel_id);
    return idx >= 0 && wine_matches(&g_wine.apps[idx]);
}

static int library_selection_visible(void) {
    if (g_sel_kind == SEL_NONE) return 0;
    library_build_entries();
    return library_find(g_sel_kind, g_sel_id) >= 0;
}

static int selection_visible(void) {
    switch (g_page) {
    case STORE_PAGE_NATIVE:
    case STORE_PAGE_HOME:
        return g_sel_kind == SEL_NATIVE && native_selection_visible();
    case STORE_PAGE_LINUX:
        return flatpak_selection_visible();
    case STORE_PAGE_WINDOWS:
        return winget_selection_visible() || wine_selection_visible();
    case STORE_PAGE_LIBRARY:
        return library_selection_visible();
    default:
        return 0;
    }
}

static void choose_selection(int kind, const char *id) {
    if (kind == SEL_NONE || !id || !*id) {
        if (g_sel_kind == SEL_NONE && !g_sel_id[0]) return;
        g_sel_kind = SEL_NONE;
        g_sel_id[0] = '\0';
        g_confirm_id[0] = '\0';
        g_reveal_pending = 1;
        bump();
        return;
    }
    if (kind == g_sel_kind && !strcmp(id, g_sel_id)) return;
    g_sel_kind = kind;
    snprintf(g_sel_id, sizeof g_sel_id, "%s", id);
    g_confirm_id[0] = '\0';
    g_reveal_pending = 1;
    bump();
}

/*
 * The selection is always a source and an application id, never an index:
 * package lists can be refreshed under the view and an index would then
 * name another app.
 */
static void ensure_selection(void) {
    if (selection_visible()) return;

    int kind = SEL_NONE;
    const char *id = NULL;

    switch (g_page) {
    case STORE_PAGE_HOME: {
        int first = -1;
        int n = finn_apps_count();
        for (int i = 0; i < n; i++) {
            const finn_app *a = finn_apps_at(i);
            if (a && a->installed && native_matches(i)) { first = i; break; }
        }
        if (first < 0) first = native_visible_at(0);
        if (first >= 0) {
            const finn_app *a = finn_apps_at(first);
            if (a) { kind = SEL_NATIVE; id = a->id; }
        }
        break;
    }
    case STORE_PAGE_NATIVE: {
        int first = native_visible_at(0);
        const finn_app *a = first >= 0 ? finn_apps_at(first) : NULL;
        if (a) { kind = SEL_NATIVE; id = a->id; }
        break;
    }
    case STORE_PAGE_LINUX: {
        const flatpak_app *a = flatpak_linux_item(flatpak_linux_at(0));
        if (a) { kind = SEL_FLATPAK; id = a->id; }
        break;
    }
    case STORE_PAGE_WINDOWS:
        if (winget_results_visible() && g_winget_count > 0) {
            kind = SEL_WINGET;
            id = g_winget_results[0].id;
        } else {
            int idx = wine_visible_at(0);
            if (idx >= 0) { kind = SEL_WINE; id = g_wine.apps[idx].id; }
        }
        break;
    case STORE_PAGE_LIBRARY:
        library_build_entries();
        if (g_library_count > 0) {
            kind = g_library[0].kind;
            id = library_entry_id(&g_library[0]);
        }
        break;
    default:
        break;
    }

    choose_selection(kind, id);
}

static void select_page_item(int k) {
    int kind = SEL_NONE;
    const char *id = NULL;

    switch (g_page) {
    case STORE_PAGE_HOME:
    case STORE_PAGE_NATIVE: {
        int idx = native_visible_at(k);
        const finn_app *a = idx >= 0 ? finn_apps_at(idx) : NULL;
        if (a) { kind = SEL_NATIVE; id = a->id; }
        break;
    }
    case STORE_PAGE_LINUX: {
        const flatpak_app *a = flatpak_linux_item(flatpak_linux_at(k));
        if (a) { kind = SEL_FLATPAK; id = a->id; }
        break;
    }
    case STORE_PAGE_WINDOWS:
        if (winget_results_visible()) {
            if (k >= 0 && k < g_winget_count) {
                kind = SEL_WINGET;
                id = g_winget_results[k].id;
            }
        } else {
            int idx = wine_visible_at(k);
            if (idx >= 0) { kind = SEL_WINE; id = g_wine.apps[idx].id; }
        }
        break;
    case STORE_PAGE_LIBRARY:
        library_build_entries();
        if (k >= 0 && k < g_library_count) {
            kind = g_library[k].kind;
            id = library_entry_id(&g_library[k]);
        }
        break;
    default:
        break;
    }

    choose_selection(kind, id);
}

static int page_count_for(int page) {
    switch (page) {
    case STORE_PAGE_HOME:
    case STORE_PAGE_NATIVE:
        return native_visible_count();
    case STORE_PAGE_LINUX:
        return flatpak_linux_count();
    case STORE_PAGE_WINDOWS:
        return winget_results_visible() ? g_winget_count : wine_visible_count();
    case STORE_PAGE_LIBRARY:
        library_build_entries();
        return g_library_count;
    default:
        return 0;
    }
}

static int page_index_of(int page, int kind, const char *id) {
    switch (page) {
    case STORE_PAGE_HOME:
    case STORE_PAGE_NATIVE:
        if (kind != SEL_NATIVE) return -1;
        return native_visible_index_of(find_index(id));
    case STORE_PAGE_LINUX: {
        if (kind != SEL_FLATPAK) return -1;
        int idx = flatpak_search_current() ? flatpak_result_index(id)
                                           : flatpak_installed_index(id);
        return flatpak_linux_index_of(idx);
    }
    case STORE_PAGE_WINDOWS:
        if (kind == SEL_WINGET)
            return winget_results_visible() ? winget_index(id) : -1;
        if (kind == SEL_WINE)
            return wine_visible_index_of(wine_index(id));
        return -1;
    case STORE_PAGE_LIBRARY:
        if (kind == SEL_NONE) return -1;
        library_build_entries();
        return library_find(kind, id);
    default:
        return -1;
    }
}

static void open_detail(int idx) {
    const finn_app *a = finn_apps_at(idx);
    if (!a) return;

    g_sel_kind = SEL_NATIVE;
    snprintf(g_sel_id, sizeof g_sel_id, "%s", a->id);
    g_detail = 1;
    g_detail_scroll = 0;
    g_filter_focused = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    bump();
}

static void close_detail(void) {
    if (!g_detail) return;
    g_detail = 0;
    g_detail_scroll = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    g_reveal_pending = 1;
    bump();
}

static void reload_catalogue(void) {
    int n = finn_apps_load(g_registry, NULL);
    g_load_error = n < 0;
    if (find_index(g_confirm_id) < 0) g_confirm_id[0] = '\0';
    ensure_selection();
    bump();
}

static void winget_info_clear(void) {
    memset(g_winget_info, 0, sizeof g_winget_info);
    g_winget_info_next = 0;
}

static void refresh_page(int page) {
    switch (page) {
    case STORE_PAGE_HOME:
        reload_catalogue();
        if (!g_flatpak_loaded) flatpak_query_start("");
        if (g_wine.available < 0 || g_wine.failed) wine_refresh();
        break;
    case STORE_PAGE_NATIVE:
        reload_catalogue();
        break;
    case STORE_PAGE_LINUX:
        flatpak_query_start(g_filter);
        break;
    case STORE_PAGE_WINDOWS:
        winget_info_clear();
        wine_refresh();
        if (g_filter[0]) winget_search_start(g_filter);
        else {
            g_winget_count = 0;
            g_winget_search_active = 0;
            g_winget_query[0] = '\0';
            bump();
        }
        break;
    case STORE_PAGE_LIBRARY:
        reload_catalogue();
        flatpak_query_start("");
        wine_refresh();
        break;
    default:
        break;
    }
}

static void set_page(int page) {
    if (page < 0 || page >= STORE_PAGE_COUNT) return;

    g_page = page;
    g_detail = 0;
    g_detail_scroll = 0;
    g_filter[0] = '\0';
    g_filter_focused = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;
    g_sel_kind = SEL_NONE;
    g_sel_id[0] = '\0';
    ensure_selection();
    g_reveal_pending = 1;
    bump();

    switch (page) {
    case STORE_PAGE_HOME:
        if (!g_flatpak_loaded) flatpak_query_start("");
        if (g_wine.available < 0) wine_refresh();
        break;
    case STORE_PAGE_LINUX:
        flatpak_query_start("");
        break;
    case STORE_PAGE_WINDOWS:
        wine_refresh();
        break;
    case STORE_PAGE_LIBRARY:
        flatpak_query_start("");
        wine_refresh();
        break;
    default:
        break;
    }
}

static void cycle_page(int delta) {
    int page = (g_page + delta + STORE_PAGE_COUNT) % STORE_PAGE_COUNT;
    set_page(page);
}

static void filter_changed(void) {
    /*
     * Typing is a request to look through the page that is open, so a
     * product page gets out of the way. A search on Linux Apps or Windows
     * Apps is only sent on Enter: the field is a query, not a keystroke
     * stream to a network service.
     */
    if (g_detail) {
        g_detail = 0;
        g_detail_scroll = 0;
    }

    g_scroll[g_page] = 0;
    g_confirm_id[0] = '\0';
    g_status[0] = '\0';
    g_status_error = 0;

    if (g_page == STORE_PAGE_LINUX) {
        if (!g_filter[0]) {
            g_flatpak_search_active = 0;
            g_flatpak_query[0] = '\0';
            g_flatpak_results_count = 0;
        } else if (!flatpak_search_current()) {
            snprintf(g_status, sizeof g_status,
                     "Press Enter to search Flatpak apps.");
        }
    } else if (g_page == STORE_PAGE_WINDOWS) {
        if (!g_filter[0]) {
            g_winget_search_active = 0;
            g_winget_query[0] = '\0';
            g_winget_count = 0;
        } else if (!winget_results_visible()) {
            snprintf(g_status, sizeof g_status,
                     "Press Enter to search winget.");
        }
    }

    ensure_selection();
    g_reveal_pending = 1;
    bump();
}

static void filter_append(uint32_t keysym) {
    size_t len = strlen(g_filter);
    if (len + 1 >= sizeof g_filter) return;
    g_filter[len] = (char)keysym;
    g_filter[len + 1] = '\0';
    g_filter_focused = 1;
    filter_changed();
}

static int filter_backspace(void) {
    size_t len = strlen(g_filter);
    if (!len) return 0;
    g_filter[len - 1] = '\0';
    g_filter_focused = 1;
    filter_changed();
    return 1;
}

static void move_selection(int delta) {
    if (!delta) return;
    int n = page_count_for(g_page);
    if (n == 0) return;

    int k = page_index_of(g_page, g_sel_kind, g_sel_id);
    if (k < 0) k = delta > 0 ? -1 : n;
    k += delta;
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    select_page_item(k);
}

static void jump_selection(int to_end) {
    int n = page_count_for(g_page);
    if (n == 0) return;
    select_page_item(to_end ? n - 1 : 0);
    g_reveal_pending = 1;
    bump();
}

/* ---------------- the window's door ---------------- */

/* The last window size the view drew at. Press, key and scroll do not get
 * one, so they reuse it; hit resolves positions to stable ids, so press
 * never needs geometry at all. */
static int g_view_ww = 0, g_view_wh = 0;

static void load_nav_icon(fsvg *ic, const char *name) {
    if (!ic || !name || !g_icon_dir[0]) return;
    char p[600];
    snprintf(p, sizeof p, "%s/%s", g_icon_dir, name);
    if (fsvg_load(p, ic) != 0) memset(ic, 0, sizeof *ic);
}

static void free_nav_icons(void) {
    fsvg *icons[] = { &g_ic_home, &g_ic_store, &g_ic_grid, &g_ic_windows,
                      &g_ic_library, &g_ic_search, &g_ic_clear, &g_ic_back,
                      &g_ic_next, &g_ic_refresh };
    for (size_t i = 0; i < sizeof icons / sizeof icons[0]; i++) {
        fsvg_free(icons[i]);
        memset(icons[i], 0, sizeof *icons[i]);
    }
}

static void worker_stop(pthread_t *th, int *active, int *done) {
    if (!th || !active || !done || !*active) return;
    *active = 0;
    /* A finished worker is joined so its stack is reaped; one still
     * talking to the control plane is detached rather than waited on.
     * Either way its generation is stale, so it applies nothing. */
    if (task_is_done(done)) pthread_join(*th, NULL);
    else pthread_detach(*th);
}

int finnstoreapp_init(const char *icon_dir, const char *font_dir,
                      const char *registry) {
    if (g_ready) return 1;
    if (icon_dir) snprintf(g_icon_dir, sizeof g_icon_dir, "%s", icon_dir);
    if (font_dir) snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir);
    if (registry) snprintf(g_registry, sizeof g_registry, "%s", registry);

    load_nav_icon(&g_ic_home, "home-09.svg");
    load_nav_icon(&g_ic_store, "store.svg");
    load_nav_icon(&g_ic_grid, "app-grid.svg");
    load_nav_icon(&g_ic_windows, "windows.svg");
    load_nav_icon(&g_ic_library, "library.svg");
    load_nav_icon(&g_ic_search, "search-01.svg");
    load_nav_icon(&g_ic_clear, "x.svg");
    load_nav_icon(&g_ic_back, "chevron-left.svg");
    load_nav_icon(&g_ic_next, "chevron-right.svg");
    load_nav_icon(&g_ic_refresh, "refresh.svg");

    g_store_generation++;
    reload_catalogue();
    set_page(STORE_PAGE_HOME);
    g_ready = 1;
    return 1;
}

void finnstoreapp_free(void) {
    g_ready = 0;
    g_store_generation++;
    g_flatpak_pending = 0;
    g_wine_pending = 0;
    g_winget_search_pending = 0;
    g_winget_info_pending = 0;
    worker_stop(&g_flatpak_thread, &g_flatpak_active, &g_flatpak_task.done);
    worker_stop(&g_wine_thread, &g_wine_active, &g_wine_task.done);
    worker_stop(&g_winget_search_thread, &g_winget_search_running,
                &g_winget_search_task.done);
    worker_stop(&g_winget_info_thread, &g_winget_info_running,
                &g_winget_info_task.done);
    worker_stop(&g_winget_job_thread, &g_winget_job_running,
                &g_winget_job_task.done);
    finn_font_free(&g_f_hero);
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
    free_icons();
    free_nav_icons();
    free(g_scratch);
    g_scratch = NULL;
    g_scratch_cap = 0;
    g_font_scale = -1.0f;
    g_view_ww = g_view_wh = 0;
}

/* The tallest the page's own geometry runs, in reference units. */
static float page_content_h(const store_layout *L, float s) {
    switch (g_page) {
    case STORE_PAGE_HOME: {
        home_geom H;
        home_build(L, s, &H);
        return H.content_h;
    }
    case STORE_PAGE_NATIVE: {
        native_geom G;
        native_build(L, &G);
        return G.content_h;
    }
    case STORE_PAGE_LINUX: {
        linux_geom G;
        linux_build(L, &G);
        return G.content_h;
    }
    case STORE_PAGE_WINDOWS: {
        windows_geom G;
        windows_build(L, &G);
        return G.content_h;
    }
    case STORE_PAGE_LIBRARY: {
        library_geom G;
        library_build(L, &G);
        return G.content_h;
    }
    default:
        return 0.0f;
    }
}

static int scroll_max_for(float content_h, float s, float vph) {
    float m = content_h * s - vph;
    return m > 0.0f ? (int)(m + 0.5f) : 0;
}

void finnstoreapp_render(uint32_t *layer, int lw, int lh,
                         int lx0, int ly0, int wx, int wy, int ww, int wh,
                         float scale, const finn_state *st, int focused) {
    (void)st;
    (void)focused;
    if (!g_ready || !layer || ww <= 0 || wh <= 0) return;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }
    g_view_ww = ww;
    g_view_wh = wh;

    /* The polls only promote finished workers, so a frame never waits on
     * the network or the package manager. */
    flatpak_query_poll();
    wine_query_poll();
    winget_search_poll();
    winget_info_poll();
    winget_job_poll();
    flatpak_poll_job();

    float s = scale * FINN_APP_REF;
    store_layout L;
    layout_for(ww, wh, s, &L);

    int ox = wx - lx0, oy = wy - ly0;
    store_canvas c = { layer, lw, lh, wx, wy, 0.0f, 0.0f };

    if (L.rail) draw_rail(&c, &L, s);
    else {
        draw_filter(&c, &L, s, g_filter_focused);
        draw_tabs(&c, &L, s);
    }
    draw_header(&c, &L, s);

    /* The scrolled page draws into the content column's own buffer, which
     * starts as the glass underneath it: cards that move under the header
     * are clipped by the buffer's edge instead of painting over it, and
     * the rounded corners keep sampling the surface they sit on. */
    float vpw = L.content_w;
    float vph = L.content_bottom - L.content_y;
    if (vpw < 1.0f || vph < 1.0f) return;
    int bw = (int)(vpw + 0.5f);
    int bh = (int)(vph + 0.5f);
    int ix = (int)(L.content_x + 0.5f);
    int iy = (int)(L.content_y + 0.5f);
    uint32_t *buf = scratch_begin(bw, bh);
    if (!buf) return;
    copy_from_layer(buf, bw, bh, layer, lw, lh, ox + ix, oy + iy);
    store_canvas cc = { buf, bw, bh, wx + ix, wy + iy,
                        (float)ix, (float)iy };

    if (g_detail) {
        const finn_app *a = finn_apps_at(find_index(g_sel_id));
        if (!a) close_detail();
        else {
            detail_geom DG;
            detail_metrics(&L, s, a, &DG);
            float vp = DG.vp_h > 0.0f ? DG.vp_h : vph;
            int max = scroll_max_for(detail_content_h(&L, s, a), s, vp);
            if (g_detail_scroll > max) g_detail_scroll = max;
            if (g_detail_scroll < 0) g_detail_scroll = 0;
            /* The body starts below the Back pill: the viewport offset is
             * folded into the scroll so the icon never paints under it. */
            draw_detail_content(&cc, &L, s, a, find_index(g_sel_id),
                                detail_stacked(&L, s),
                                (float)g_detail_scroll - DG.vp_y);
            draw_detail_fixed(&cc, &L, s, &DG, a);
            draw_scrollbar(&c, &L, s, L.content_y, vph, g_detail_scroll,
                           max);
        }
    } else if (g_detail) {
        close_detail();
    }
    if (!g_detail) {
        float ch = page_content_h(&L, s);
        int max = scroll_max_for(ch, s, vph);
        if (g_scroll[g_page] > max) g_scroll[g_page] = max;
        if (g_scroll[g_page] < 0) g_scroll[g_page] = 0;
        float scroll_px = (float)g_scroll[g_page];
        switch (g_page) {
        case STORE_PAGE_HOME: {
            home_geom H;
            home_build(&L, s, &H);
            draw_home(&cc, &L, s, &H, scroll_px);
            break;
        }
        case STORE_PAGE_NATIVE: {
            native_geom G;
            native_build(&L, &G);
            draw_native(&cc, &L, s, &G, scroll_px);
            break;
        }
        case STORE_PAGE_LINUX: {
            linux_geom G;
            linux_build(&L, &G);
            draw_linux(&cc, &L, s, &G, scroll_px);
            break;
        }
        case STORE_PAGE_WINDOWS: {
            windows_geom G;
            windows_build(&L, &G);
            draw_windows(&cc, &L, s, &G, scroll_px);
            break;
        }
        case STORE_PAGE_LIBRARY: {
            library_geom G;
            library_build(&L, &G);
            draw_library(&cc, &L, s, &G, scroll_px);
            break;
        }
        default:
            break;
        }
        draw_scrollbar(&c, &L, s, L.content_y, vph, g_scroll[g_page],
                       max);
    }

    /* The buffer holds exactly the viewport, so the whole of it goes back
     * where it was borrowed from. */
    for (int y = 0; y < bh; y++) {
        int sy = oy + iy + y;
        if (sy < 0 || sy >= lh) continue;
        int x0 = ox + ix < 0 ? -(ox + ix) : 0;
        int x1 = bw;
        if (ox + ix + x1 > lw) x1 = lw - ox - ix;
        if (x1 <= x0) continue;
        memcpy(layer + (size_t)sy * lw + ox + ix + x0,
               buf + (size_t)y * bw + x0,
               (size_t)(x1 - x0) * sizeof(uint32_t));
    }
}

/* In the content column's own coordinates, with the scroll added back: the
 * draw code subtracts scroll_px, so a press lands on the rect it drew. */
static int content_point(const store_layout *L, float s, int ww, int wh,
                         int px, int py, int wx, int wy, float scroll_px,
                         float *cx, float *sy) {
    (void)s; (void)ww; (void)wh;
    *cx = (float)(px - wx) - L->content_x;
    *sy = (float)(py - wy) - L->content_y + scroll_px;
    return *cx >= 0.0f && *cx < L->content_w;
}

static int in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int finnstoreapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                     float scale, int *out_sub) {
    if (out_sub) *out_sub = 0;
    if (px < wx || py < wy || px >= wx + ww || py >= wy + wh) return 0;

    float s = scale * FINN_APP_REF;
    store_layout L;
    layout_for(ww, wh, s, &L);

    float x = (float)(px - wx);
    float y = (float)(py - wy);

    for (int i = 0; i < STORE_PAGE_COUNT; i++) {
        float nx, ny, nw, nh;
        nav_item_rect(&L, i, s, &nx, &ny, &nw, &nh);
        if (in_rect(x, y, nx, ny, nw, nh)) {
            if (out_sub) *out_sub = i;
            return FINNSTORE_HIT_NAV;
        }
    }

    if (in_rect(x, y, L.filter_x, L.filter_y, L.filter_w, L.filter_h)) {
        if (g_filter[0]) {
            float fx, fy, fw, fh;
            filter_clear_rect(&L, s, &fx, &fy, &fw, &fh);
            if (in_rect(x, y, fx, fy, fw, fh)) return FINNSTORE_HIT_FILTER_CLEAR;
        }
        return FINNSTORE_HIT_FILTER;
    }

    if (g_detail) {
        const finn_app *a = finn_apps_at(find_index(g_sel_id));
        if (a) {
            detail_geom DG;
            detail_metrics(&L, s, a, &DG);
            float bx = DG.back_x, by = DG.back_y;
            if (in_rect(x - L.content_x, y - L.content_y, bx, by,
                        DG.back_w, DG.back_h))
                return FINNSTORE_HIT_BACK;
            /* Fixed chrome is not scrolled; the body is. */
            float ax = DG.action_x, ay = DG.action_y;
            if (in_rect(x - L.content_x, y - L.content_y, ax, ay,
                        DG.action_w, DG.action_h))
                return FINNSTORE_HIT_DETAIL_ACTION;
            if (a->installed &&
                in_rect(x - L.content_x, y - L.content_y, DG.remove_x, ay,
                        DG.remove_w, DG.action_h))
                return FINNSTORE_HIT_DETAIL_REMOVE;
        }
        return FINNSTORE_HIT_BODY;
    }

    float scroll_px = (float)g_scroll[g_page];
    float cx, cy;
    if (!content_point(&L, s, ww, wh, px, py, wx, wy, scroll_px, &cx, &cy))
        return FINNSTORE_HIT_BODY;

    if (g_page == STORE_PAGE_HOME) {
        home_geom H;
        home_build(&L, s, &H);
        const char *links[3] = { "Native Apps", "Linux Apps", "Windows Apps" };
        float label_ys[3] = { H.top_label_y, H.linux_label_y, H.win_label_y };
        int pages[3] = { STORE_PAGE_NATIVE, STORE_PAGE_LINUX,
                         STORE_PAGE_WINDOWS };
        for (int i = 0; i < 3; i++) {
            float lx, ly, lw, lh;
            link_rect(&L, links[i], s, label_ys[i], &lx, &ly, &lw, &lh);
            if (in_rect(cx, cy, lx, ly, lw, lh)) {
                if (out_sub) *out_sub = pages[i];
                return FINNSTORE_HIT_SEE_ALL;
            }
        }
        if (H.hero >= 0 && in_rect(cx, cy, 0.0f, 0.0f, L.content_w,
                                   s * HERO_H)) {
            if (out_sub) *out_sub = H.hero;
            return FINNSTORE_HIT_CARD;
        }
        float gap = s * CARD_GAP;
        float slot = (L.content_w - gap * (float)(L.shelf_cols - 1)) /
                     (float)L.shelf_cols;
        for (int k = 0; k < H.top_n; k++) {
            float kx = (float)k * (slot + gap);
            if (in_rect(cx, cy, kx, H.top_y * s, slot, s * SHELF_CARD_H)) {
                if (out_sub) *out_sub = H.top_apps[k];
                return FINNSTORE_HIT_CARD;
            }
        }
        return FINNSTORE_HIT_BODY;
    }

    if (g_page == STORE_PAGE_NATIVE || g_page == STORE_PAGE_LINUX) {
        int native = g_page == STORE_PAGE_NATIVE;
        int nvis = native ? native_visible_count() : flatpak_linux_count();
        float top = list_top_for(
            native ? STORE_PAGE_NATIVE : STORE_PAGE_LINUX);
        float rx, ry, rw, rh;
        refresh_rect(&L, s, 0.0f, &rx, &ry, &rw, &rh);
        if (in_rect(cx, cy, rx, ry, rw, rh)) return FINNSTORE_HIT_REFRESH;
        for (int k = 0; k < nvis; k++) {
            int idx = native ? native_visible_at(k)
                             : flatpak_linux_at(k);
            float gx, gy, gw, gh;
            grid_rect(&L, s, k, top, &gx, &gy, &gw, &gh);
            if (!in_rect(cx, cy, gx, gy, gw, gh)) continue;
            float ax, ay, aw, ah;
            if (native) {
                const finn_app *a = finn_apps_at(idx);
                native_card_primary_rect(&L, s, k, a, &ax, &ay, &aw, &ah);
                if (in_rect(cx, cy, ax, ay, aw, ah)) {
                    if (out_sub) *out_sub = idx;
                    return FINNSTORE_HIT_CARD_ACTION;
                }
                if (a && a->installed) {
                    native_card_remove_rect(&L, s, k, a, &ax, &ay, &aw,
                                            &ah);
                    if (in_rect(cx, cy, ax, ay, aw, ah)) {
                        if (out_sub) *out_sub = idx;
                        return FINNSTORE_HIT_CARD_REMOVE;
                    }
                }
            } else {
                const flatpak_app *a =
                    flatpak_linux_item(flatpak_linux_at(k));
                flatpak_card_primary_rect(&L, s, k, a, &ax, &ay, &aw, &ah);
                if (in_rect(cx, cy, ax, ay, aw, ah)) {
                    if (out_sub) *out_sub = k;
                    return FINNSTORE_HIT_CARD_ACTION;
                }
                if (a && a->installed) {
                    flatpak_card_remove_rect(&L, s, k, a, &ax, &ay, &aw,
                                             &ah);
                    if (in_rect(cx, cy, ax, ay, aw, ah)) {
                        if (out_sub) *out_sub = k;
                        return FINNSTORE_HIT_CARD_ACTION;
                    }
                }
            }
            if (out_sub) *out_sub = native ? idx : k;
            return FINNSTORE_HIT_CARD;
        }
        return FINNSTORE_HIT_BODY;
    }

    if (g_page == STORE_PAGE_WINDOWS) {
        float rx, ry, rw, rh;
        refresh_rect(&L, s, 0.0f, &rx, &ry, &rw, &rh);
        if (in_rect(cx, cy, rx, ry, rw, rh)) return FINNSTORE_HIT_REFRESH;
        if (winget_results_visible()) {
            for (int k = 0; k < g_winget_count; k++) {
                float qx, qy, qw, qh;
                winget_row_rect(&L, s, k, &qx, &qy, &qw, &qh);
                if (!in_rect(cx, cy, qx, qy, qw, qh)) continue;
                float ax, ay, aw, ah;
                winget_row_action_rect(&L, s, k, &g_winget_results[k],
                                       &ax, &ay, &aw, &ah);
                if (out_sub) *out_sub = k;
                if (in_rect(cx, cy, ax, ay, aw, ah))
                    return FINNSTORE_HIT_CARD_ACTION;
                return FINNSTORE_HIT_CARD;
            }
        }
        windows_geom G;
        windows_build(&L, &G);
        for (int k = 0; k < G.wine_count; k++) {
            float qy = s * G.wine_top +
                     (float)k * (s * LIB_ROW_H + s * CARD_GAP);
            if (!in_rect(cx, cy, 0.0f, qy, L.content_w, s * LIB_ROW_H))
                continue;
            float aw = s * 72.0f, ah = s * 30.0f;
            float ax = L.content_w - s * CARD_PAD - aw;
            float ay = qy + (s * LIB_ROW_H - ah) * 0.5f;
            if (out_sub) *out_sub = 1000 + k;
            if (in_rect(cx, cy, ax, ay, aw, ah))
                return FINNSTORE_HIT_CARD_ACTION;
            return FINNSTORE_HIT_CARD;
        }
        return FINNSTORE_HIT_BODY;
    }

    if (g_page == STORE_PAGE_LIBRARY) {
        float rx, ry, rw, rh;
        refresh_rect(&L, s, 0.0f, &rx, &ry, &rw, &rh);
        if (in_rect(cx, cy, rx, ry, rw, rh)) return FINNSTORE_HIT_REFRESH;
        library_build_entries();
        for (int k = 0; k < g_library_count; k++) {
            float qx, qy, qw, qh;
            library_row_rect(&L, s, k, &qx, &qy, &qw, &qh);
            if (!in_rect(cx, cy, qx, qy, qw, qh)) continue;
            float ax, ay, aw, ah;
            library_action_rect(&L, s, k, &ax, &ay, &aw, &ah);
            if (out_sub) *out_sub = k;
            if (in_rect(cx, cy, ax, ay, aw, ah))
                return FINNSTORE_HIT_CARD_ACTION;
            return FINNSTORE_HIT_CARD;
        }
        return FINNSTORE_HIT_BODY;
    }

    return FINNSTORE_HIT_BODY;
}

/* The primary button for whatever card the hit resolved. Linux and Windows
 * rows arrive as ordinals; native rows arrive as registry indices, because
 * the grid is filtered and an ordinal would not survive a redraw. */
static int press_card_action(int sub) {
    if (g_page == STORE_PAGE_NATIVE) {
        const finn_app *a = finn_apps_at(sub);
        if (!a) return 0;
        choose_selection(SEL_NATIVE, a->id);
        if (a->installed) native_launch(sub);
        else if (app_available(a)) native_install(sub);
        else set_status("That app's launcher is missing; it cannot be installed here.", 1);
        return 1;
    }
    if (g_page == STORE_PAGE_LINUX) {
        const flatpak_app *a = flatpak_linux_item(flatpak_linux_at(sub));
        if (!a) return 0;
        choose_selection(SEL_FLATPAK, a->id);
        if (a->installed) flatpak_run_app(a);
        else flatpak_install_app(a);
        return 1;
    }
    if (g_page == STORE_PAGE_WINDOWS) {
        if (sub < 1000) {
            if (sub < 0 || sub >= g_winget_count) return 0;
            choose_selection(SEL_WINGET, g_winget_results[sub].id);
            winget_install_result(&g_winget_results[sub]);
            return 1;
        }
        int k = sub - 1000;
        int idx = wine_visible_at(k);
        if (idx < 0) return 0;
        choose_selection(SEL_WINE, g_wine.apps[idx].id);
        wine_run_app(&g_wine.apps[idx]);
        return 1;
    }
    if (g_page == STORE_PAGE_LIBRARY) {
        library_build_entries();
        if (sub < 0 || sub >= g_library_count) return 0;
        choose_selection(g_library[sub].kind,
                         library_entry_id(&g_library[sub]));
        library_open(&g_library[sub]);
        return 1;
    }
    return 0;
}

static int press_card(int sub) {
    g_filter_focused = 0;
    if (g_page == STORE_PAGE_HOME || g_page == STORE_PAGE_NATIVE) {
        const finn_app *a = finn_apps_at(sub);
        if (!a) return 0;
        choose_selection(SEL_NATIVE, a->id);
        open_detail(sub);
        return 1;
    }
    if (g_page == STORE_PAGE_LINUX) {
        const flatpak_app *a = flatpak_linux_item(flatpak_linux_at(sub));
        if (!a) return 0;
        choose_selection(SEL_FLATPAK, a->id);
        return 1;
    }
    if (g_page == STORE_PAGE_WINDOWS) {
        if (sub < 1000) {
            if (sub < 0 || sub >= g_winget_count) return 0;
            choose_selection(SEL_WINGET, g_winget_results[sub].id);
            winget_info_start(g_winget_results[sub].id);
            return 1;
        }
        int k = sub - 1000;
        int idx = wine_visible_at(k);
        if (idx < 0) return 0;
        choose_selection(SEL_WINE, g_wine.apps[idx].id);
        return 1;
    }
    if (g_page == STORE_PAGE_LIBRARY) {
        library_build_entries();
        if (sub < 0 || sub >= g_library_count) return 0;
        choose_selection(g_library[sub].kind,
                         library_entry_id(&g_library[sub]));
        return 1;
    }
    return 0;
}

static int press_remove(int sub) {
    const finn_app *a = NULL;
    const flatpak_app *f = NULL;
    const char *id = NULL;
    if (g_page == STORE_PAGE_NATIVE) {
        a = finn_apps_at(sub);
        if (!a || !a->installed) return 0;
        id = a->id;
    } else if (g_page == STORE_PAGE_LINUX) {
        f = flatpak_linux_item(flatpak_linux_at(sub));
        if (!f || !f->installed) return 0;
        id = f->id;
    } else {
        return 0;
    }
    if (strcmp(g_confirm_id, id) != 0) {
        snprintf(g_confirm_id, sizeof g_confirm_id, "%s", id);
        set_status("Press Remove again to confirm.", 0);
        return 1;
    }
    g_confirm_id[0] = '\0';
    if (a) native_remove(sub);
    else flatpak_remove_app(f);
    return 1;
}

int finnstoreapp_press(int hit, int sub, int px, int py, float scale) {
    (void)px; (void)py; (void)scale;
    if (!g_ready) return 0;
    switch (hit) {
    case FINNSTORE_HIT_NAV:
        if (sub != g_page) set_page(sub);
        else g_filter_focused = 0;
        return 1;
    case FINNSTORE_HIT_FILTER:
        g_filter_focused = 1;
        bump();
        return 1;
    case FINNSTORE_HIT_FILTER_CLEAR:
        g_filter[0] = '\0';
        g_filter_focused = 0;
        filter_changed();
        return 1;
    case FINNSTORE_HIT_CARD:
        return press_card(sub);
    case FINNSTORE_HIT_CARD_ACTION:
        return press_card_action(sub);
    case FINNSTORE_HIT_CARD_REMOVE:
        return press_remove(sub);
    case FINNSTORE_HIT_SEE_ALL:
        set_page(sub);
        return 1;
    case FINNSTORE_HIT_REFRESH:
        refresh_page(g_page);
        return 1;
    case FINNSTORE_HIT_BACK:
        close_detail();
        return 1;
    case FINNSTORE_HIT_DETAIL_ACTION: {
        int idx = find_index(g_sel_id);
        const finn_app *a = finn_apps_at(idx);
        if (!a) { close_detail(); return 1; }
        if (a->installed) native_launch(idx);
        else if (app_available(a)) native_install(idx);
        else set_status("That app's launcher is missing; it cannot be installed here.", 1);
        return 1;
    }
    case FINNSTORE_HIT_DETAIL_REMOVE: {
        int idx = find_index(g_sel_id);
        const finn_app *a = finn_apps_at(idx);
        if (!a || !a->installed) return 0;
        if (strcmp(g_confirm_id, a->id) != 0) {
            snprintf(g_confirm_id, sizeof g_confirm_id, "%s", a->id);
            set_status("Press Remove again to confirm.", 0);
            return 1;
        }
        g_confirm_id[0] = '\0';
        native_remove(idx);
        return 1;
    }
    case FINNSTORE_HIT_BODY:
        if (g_filter_focused) {
            g_filter_focused = 0;
            bump();
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

/* Enter runs the search the field is asking for, or the selected item's
 * primary action when there is nothing to search. */
static int activate_selection(void) {
    int k = page_index_of(g_page, g_sel_kind, g_sel_id);
    if (k < 0) {
        if (page_count_for(g_page) > 0) {
            select_page_item(0);
            return 1;
        }
        return 0;
    }
    if (g_page == STORE_PAGE_HOME || g_page == STORE_PAGE_NATIVE) {
        int idx = native_visible_at(k);
        if (idx < 0) return 0;
        choose_selection(SEL_NATIVE, g_sel_id);
        open_detail(idx);
        return 1;
    }
    if (g_page == STORE_PAGE_LINUX) {
        int idx = flatpak_linux_at(k);
        const flatpak_app *a = flatpak_linux_item(idx);
        if (!a) return 0;
        if (a->installed) flatpak_run_app(a);
        else flatpak_install_app(a);
        return 1;
    }
    if (g_page == STORE_PAGE_WINDOWS) {
        if (g_sel_kind == SEL_WINGET) {
            int i = winget_index(g_sel_id);
            if (i < 0) return 0;
            winget_install_result(&g_winget_results[i]);
            return 1;
        }
        int idx = wine_index(g_sel_id);
        if (idx < 0) return 0;
        wine_run_app(&g_wine.apps[idx]);
        return 1;
    }
    if (g_page == STORE_PAGE_LIBRARY) {
        library_build_entries();
        int i = library_find(g_sel_kind, g_sel_id);
        if (i < 0) return 0;
        library_open(&g_library[i]);
        return 1;
    }
    return 0;
}

int finnstoreapp_key(const finn_key_event *e) {
    if (!g_ready || !e || !e->pressed) return 0;

    if (e->code == KEY_ESC) {
        if (g_detail) { close_detail(); return 1; }
        if (g_filter[0]) {
            g_filter[0] = '\0';
            filter_changed();
            return 1;
        }
        if (g_filter_focused) {
            g_filter_focused = 0;
            bump();
            return 1;
        }
        return 0;
    }

    if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
        if (g_page == STORE_PAGE_LINUX && g_filter[0] &&
            !flatpak_search_current()) {
            flatpak_query_start(g_filter);
            return 1;
        }
        if (g_page == STORE_PAGE_WINDOWS && g_filter[0] &&
            !winget_results_visible()) {
            winget_search_start(g_filter);
            return 1;
        }
        g_filter_focused = 0;
        return activate_selection();
    }

    if (e->code == KEY_BACKSPACE) {
        if (g_filter[0] || g_filter_focused) return filter_backspace();
        if (g_detail) { close_detail(); return 1; }
        return 0;
    }
    if (e->code == KEY_UP)         { move_selection(-1); return 1; }
    if (e->code == KEY_DOWN)       { move_selection(1);  return 1; }
    if (e->code == KEY_LEFT)       { move_selection(-1); return 1; }
    if (e->code == KEY_RIGHT)      { move_selection(1);  return 1; }
    if (e->code == KEY_PAGEUP)     { move_selection(-6); return 1; }
    if (e->code == KEY_PAGEDOWN)   { move_selection(6);  return 1; }
    if (e->code == KEY_HOME)       { jump_selection(0);  return 1; }
    if (e->code == KEY_END)        { jump_selection(1);  return 1; }
    if (e->code == KEY_F5)         { refresh_page(g_page); return 1; }
    if (e->code == KEY_TAB)        { cycle_page(1);      return 1; }

    if (e->keysym >= 32 && e->keysym <= 126 &&
        strlen(g_filter) + 1 < sizeof g_filter) {
        filter_append((uint32_t)e->keysym);
        return 1;
    }
    return 0;
}

int finnstoreapp_scroll(int clicks, float scale) {
    if (!g_ready || !clicks || !g_view_ww || !scale) return 0;
    float s = scale * FINN_APP_REF;
    store_layout L;
    layout_for(g_view_ww, g_view_wh, s, &L);
    float vph = L.content_bottom - L.content_y;
    int max, *slot;
    if (g_detail) {
        const finn_app *a = finn_apps_at(find_index(g_sel_id));
        if (!a) return 0;
        detail_geom DG;
        detail_metrics(&L, s, a, &DG);
        float vp = DG.vp_h > 0.0f ? DG.vp_h : vph;
        max = scroll_max_for(detail_content_h(&L, s, a), s, vp);
        slot = &g_detail_scroll;
    } else {
        max = scroll_max_for(page_content_h(&L, s), s, vph);
        slot = &g_scroll[g_page];
    }
    int before = *slot;
    *slot -= (int)((float)clicks * s * 48.0f);
    if (*slot < 0) *slot = 0;
    if (*slot > max) *slot = max;
    if (*slot != before) { bump(); return 1; }
    return 0;
}

uint32_t finnstoreapp_build_key(float scale) {
    uint32_t h = 2166136261u;
#define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(scale * 1000.0f));
    MIX(g_rev);
    MIX(g_store_generation);
    MIX(g_page);
    MIX(g_detail);
    MIX(g_sel_kind);
    for (const char *p = g_sel_id; *p; p++) MIX(*p);
    for (const char *p = g_filter; *p; p++) MIX(*p);
    for (const char *p = g_confirm_id; *p; p++) MIX(*p);
    for (const char *p = g_status; *p; p++) MIX(*p);
    for (int i = 0; i < STORE_PAGE_COUNT; i++) MIX(g_scroll[i]);
    MIX(g_detail_scroll);
    MIX(g_filter_focused);
    MIX(g_flatpak_installed_count);
    MIX(g_flatpak_results_count);
    MIX(g_flatpak_loaded);
    MIX(g_flatpak_error);
    MIX(g_winget_count);
    MIX(g_winget_loaded);
    MIX(g_winget_error);
    MIX(g_wine.count);
    MIX(g_wine.available);
    MIX(g_wine.loaded);
    MIX(g_library_count);
    for (const char *p = g_flatpak_job_id; *p; p++) MIX(*p);
    for (const char *p = g_flatpak_job_app; *p; p++) MIX(*p);
    for (const char *p = g_winget_job_id; *p; p++) MIX(*p);
    for (const char *p = g_winget_job_app; *p; p++) MIX(*p);
    for (const char *p = g_winget_job_state; *p; p++) MIX(*p);
#undef MIX
    return h ? h : 1u;
}

const char *finnstoreapp_selection(void) {
    if (g_sel_kind == SEL_NONE || !selection_visible()) return "";
    return g_sel_id;
}

const char *finnstoreapp_status(void) {
    return g_status;
}
