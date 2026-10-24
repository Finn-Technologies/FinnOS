/*
 * finnfilesapp — the Finn Files view.
 *
 * A browser, not a launchpad: it reads directories, shows what is really
 * there, and previews text. There is no client surface protocol in this
 * build, so the view lives in the compositor alongside Settings rather than
 * in a process of its own.
 *
 * Two decisions shape the file:
 *
 *   - The filesystem is read as the desktop user. The compositor is root
 *     because it owns DRM, but a file manager in it must not inherit that.
 *     Every read goes through finnapp_fs_enter(), so the kernel applies the
 *     user's permissions and /etc/shadow is not a preview.
 *
 *   - The window is read-only, and says so. The compositor's unit mounts the
 *     home directory read-only. A delete button that always failed would be
 *     worse than no delete button.
 */
#define _GNU_SOURCE
#include "finnfilesapp.h"
#include "finnappui.h"
#include "finndraw.h"
#include "finntext.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/input.h>

/* ---------------- metrics, in reference units ---------------- */

#define SIDEBAR_W     176.0f
#define SIDEBAR_PAD    14.0f
#define SIDEBAR_TITLE  42.0f
#define PLACES_CAP     70.0f
#define PLACES_TOP     82.0f
#define PLACE_H        28.0f
#define PLACE_STEP     30.0f

#define MAIN_PAD       14.0f
#define HEADER_TOP     54.0f
#define UP_SIZE        24.0f
#define SEARCH_W      190.0f
#define SEARCH_H       26.0f
#define LIST_TOP       94.0f
#define ROW_H          28.0f
#define FOOTER_H       26.0f
#define PREVIEW_W     300.0f
#define PREVIEW_GAP    12.0f

#define DOUBLE_MS     400

typedef struct {
    char name[256];
    int  is_dir;
    unsigned long long size;
    long long mtime;
} file_entry;

typedef struct {
    const char *label;
    const char *rel;        /* relative to home; NULL means the place is
                             * computed or always present */
    const char *path;       /* absolute override, for Computer */
} place_desc;

static const place_desc g_places[] = {
    { "Home",      "",          NULL },
    { "Desktop",   "Desktop",   NULL },
    { "Documents", "Documents", NULL },
    { "Downloads", "Downloads", NULL },
    { "Pictures",  "Pictures",  NULL },
    { "Music",     "Music",     NULL },
    { "Videos",    "Videos",    NULL },
    { "Computer",  NULL,        "/"  },
};
#define PLACE_COUNT ((int)(sizeof g_places / sizeof g_places[0]))

/* ---------------- state ---------------- */

static file_entry *g_entries = NULL;
static int g_n_entries = 0;
static int g_cap_entries = 0;
static char g_path[PATH_MAX] = "";
static char g_home[PATH_MAX] = "";
static int g_sel = -1;
static int g_scroll = 0;
static char g_filter[64] = "";
static int g_filter_focused = 0;
static char g_error[PATH_MAX + 300] = "";
static int g_truncated = 0;
static unsigned g_rev = 1;

static int g_place_exists[PLACE_COUNT];

static char g_preview_name[256] = "";
static char g_preview[49152];
static int g_preview_len = 0;
static int g_preview_binary = 0;
static int g_preview_loaded = 0;

static finn_font g_f_title, g_f_label, g_f_sub, g_f_micro;
static float g_font_scale = -1.0f;
static char g_font_dir[512] = "";
static int g_ready = 0;

static int g_last_click_entry = -1;
static double g_last_click_ms = 0.0;

static void bump(void) { g_rev++; }

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void set_error(const char *msg) {
    snprintf(g_error, sizeof g_error, "%s", msg ? msg : "");
    bump();
}

static const char *base_name(const char *path) {
    if (!path || !path[0]) return "";
    if (!strcmp(path, "/")) return "Computer";
    const char *slash = strrchr(path, '/');
    if (!slash) return path;
    if (!slash[1]) return path;
    return slash + 1;
}

static int load_fonts(float scale) {
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);

    char p[600];
    snprintf(p, sizeof p, "%s/FixelDisplay-Regular.otf", g_font_dir);
    if (finn_font_load(&g_f_title, p, finnapp_font_px(scale, 15.0f)) != 0) return -1;
    if (finn_font_load(&g_f_label, p, finnapp_font_px(scale, 12.0f)) != 0) return -1;
    if (finn_font_load(&g_f_sub, p, finnapp_font_px(scale, 10.0f)) != 0) return -1;
    if (finn_font_load(&g_f_micro, p, finnapp_font_px(scale, 8.5f)) != 0) return -1;
    return 0;
}

/* ---------------- the listing ---------------- */

static int is_pseudo_path(const char *p) {
    static const char *blocked[] = { "/proc", "/sys", "/dev", "/run" };
    for (size_t i = 0; i < sizeof blocked / sizeof blocked[0]; i++) {
        size_t n = strlen(blocked[i]);
        if (!strncmp(p, blocked[i], n) && (p[n] == '\0' || p[n] == '/'))
            return 1;
    }
    return 0;
}

static int entry_cmp(const void *a, const void *b) {
    const file_entry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    int c = strcasecmp(x->name, y->name);
    if (c) return c;
    return strcmp(x->name, y->name);
}

static void free_entries(void) {
    free(g_entries);
    g_entries = NULL;
    g_n_entries = 0;
    g_cap_entries = 0;
}

static int append_entry(const struct stat *st, const char *name) {
    if (g_n_entries >= FINNFILES_MAX_ENTRIES) return 1;
    if (g_n_entries == g_cap_entries) {
        int cap = g_cap_entries ? g_cap_entries * 2 : 64;
        if (cap > FINNFILES_MAX_ENTRIES) cap = FINNFILES_MAX_ENTRIES;
        file_entry *grown = realloc(g_entries, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        g_entries = grown;
        g_cap_entries = cap;
    }
    file_entry *e = &g_entries[g_n_entries++];
    snprintf(e->name, sizeof e->name, "%s", name);
    e->is_dir = S_ISDIR(st->st_mode);
    e->size = (unsigned long long)st->st_size;
    e->mtime = (long long)st->st_mtime;
    return 0;
}

static void clear_preview(void) {
    g_preview_name[0] = '\0';
    g_preview_len = 0;
    g_preview_binary = 0;
    g_preview_loaded = 0;
}

static void load_preview(void);

static int load_dir(const char *path) {
    char resolved[PATH_MAX];
    struct stat st;

    if (!path || path[0] != '/') { set_error("A path must be absolute"); return -1; }

    finnapp_fs_enter();
    if (!realpath(path, resolved)) {
        char why[PATH_MAX + 300];
        snprintf(why, sizeof why, "%s: %s", path, strerror(errno));
        finnapp_fs_leave();
        set_error(why);
        return -1;
    }
    if (is_pseudo_path(resolved)) {
        finnapp_fs_leave();
        set_error("This part of the system is not browsable");
        return -1;
    }
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) {
        finnapp_fs_leave();
        set_error("That is not a folder");
        return -1;
    }

    DIR *d = opendir(resolved);
    if (!d) {
        char why[PATH_MAX + 300];
        snprintf(why, sizeof why, "%s: %s", resolved, strerror(errno));
        finnapp_fs_leave();
        set_error(why);
        return -1;
    }

    free_entries();
    g_truncated = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (de->d_name[0] == '.') continue;   /* hidden files stay hidden */

        char full[PATH_MAX];
        if (snprintf(full, sizeof full, "%s/%s", resolved, de->d_name) >= (int)sizeof full)
            continue;
        struct stat est;
        if (stat(full, &est) != 0) continue;   /* it vanished, or no access */
        if (append_entry(&est, de->d_name) == 1) { g_truncated = 1; break; }
    }
    closedir(d);
    finnapp_fs_leave();

    qsort(g_entries, (size_t)g_n_entries, sizeof *g_entries, entry_cmp);

    snprintf(g_path, sizeof g_path, "%s", resolved);
    g_filter[0] = '\0';
    g_filter_focused = 0;
    g_scroll = 0;
    g_sel = -1;
    g_error[0] = '\0';
    g_last_click_entry = -1;
    clear_preview();
    bump();
    return 0;
}

/* Does entry i survive the filter? */
static int matches(int i) {
    if (!g_filter[0]) return 1;
    if (i < 0 || i >= g_n_entries) return 0;
    return strcasestr(g_entries[i].name, g_filter) != NULL;
}

static int visible_count(void) {
    int n = 0;
    for (int i = 0; i < g_n_entries; i++) if (matches(i)) n++;
    return n;
}

/* The entry index of the k-th visible row, or -1. */
static int visible_entry(int k) {
    if (k < 0) return -1;
    for (int i = 0; i < g_n_entries; i++) {
        if (!matches(i)) continue;
        if (k-- == 0) return i;
    }
    return -1;
}

static int visible_index_of(int entry) {
    if (entry < 0) return -1;
    int k = 0;
    for (int i = 0; i < g_n_entries; i++) {
        if (!matches(i)) continue;
        if (i == entry) return k;
        k++;
    }
    return -1;
}

static void ensure_selection_visible(void) {
    int k = visible_index_of(g_sel);
    if (k < 0) {
        g_sel = visible_entry(0);
        k = g_sel >= 0 ? 0 : -1;
    }
    if (k < 0) { g_scroll = 0; return; }
    /* The renderer will clamp against its own row count; keeping the
     * selection at least inside the current scroll window here is enough. */
    if (k < g_scroll) g_scroll = k;
}

static void select_entry(int entry) {
    if (entry == g_sel) return;
    g_sel = entry;
    clear_preview();
    bump();
}

static void move_selection(int delta) {
    int n = visible_count();
    if (n == 0) return;
    int k = visible_index_of(g_sel);
    if (k < 0) k = delta > 0 ? -1 : n;
    k += delta;
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    int entry = visible_entry(k);
    if (entry >= 0) select_entry(entry);
}

static int enter_selected(void) {
    if (g_sel < 0 || g_sel >= g_n_entries) return 0;
    if (!g_entries[g_sel].is_dir) return 0;
    char next[PATH_MAX];
    if (snprintf(next, sizeof next, "%s/%s", g_path, g_entries[g_sel].name)
        >= (int)sizeof next)
        return 0;
    return load_dir(next) == 0;
}

static int go_up(void) {
    if (!strcmp(g_path, "/")) return 0;
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", g_path);
    char *slash = strrchr(parent, '/');
    if (!slash) return 0;
    if (slash == parent) parent[1] = '\0';
    else *slash = '\0';
    return load_dir(parent) == 0;
}

static void refresh_places(void) {
    char home[PATH_MAX];
    finnapp_home(home, sizeof home);
    snprintf(g_home, sizeof g_home, "%s", home);

    for (int i = 0; i < PLACE_COUNT; i++) {
        if (g_places[i].path) { g_place_exists[i] = 1; continue; }
        char p[PATH_MAX * 2];
        if (!g_places[i].rel[0]) snprintf(p, sizeof p, "%s", g_home);
        else snprintf(p, sizeof p, "%s/%s", g_home, g_places[i].rel);

        finnapp_fs_enter();
        struct stat st;
        g_place_exists[i] = (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
        finnapp_fs_leave();
    }
}

static const char *place_path(int i, char *buf, size_t cap) {
    if (g_places[i].path) { snprintf(buf, cap, "%s", g_places[i].path); return buf; }
    if (!g_places[i].rel[0]) snprintf(buf, cap, "%s", g_home);
    else snprintf(buf, cap, "%s/%s", g_home, g_places[i].rel);
    return buf;
}

/* A place is active when the current path is that folder or inside it. */
static int place_active(int i) {
    char p[PATH_MAX * 2];
    place_path(i, p, sizeof p);
    size_t n = strlen(p);
    if (!strcmp(p, "/")) return 1;
    if (strncmp(g_path, p, n)) return 0;
    return g_path[n] == '\0' || g_path[n] == '/';
}

/* ---------------- preview ---------------- */

static int looks_binary(const char *buf, int len) {
    int odd = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0) return 1;
        if (c < 7 || (c > 13 && c < 32)) odd++;
    }
    return len > 0 && odd * 100 > len * 10;
}

static void load_preview(void) {
    if (g_sel < 0 || g_sel >= g_n_entries) return;
    file_entry *e = &g_entries[g_sel];
    if (!strcmp(g_preview_name, e->name)) return;

    clear_preview();
    snprintf(g_preview_name, sizeof g_preview_name, "%s", e->name);
    if (e->is_dir) { g_preview_loaded = 1; return; }
    if (e->size > 1024ULL * 1024ULL) {
        g_preview_loaded = 1;
        g_preview_binary = 1;      /* too large to preview, not a binary */
        return;
    }

    char full[PATH_MAX];
    if (snprintf(full, sizeof full, "%s/%s", g_path, e->name) >= (int)sizeof full) {
        g_preview_loaded = 1;
        return;
    }

    finnapp_fs_enter();
    FILE *f = fopen(full, "rb");
    if (f) {
        g_preview_len = (int)fread(g_preview, 1, sizeof g_preview - 1, f);
        fclose(f);
        g_preview[g_preview_len] = '\0';
        g_preview_binary = looks_binary(g_preview, g_preview_len);
    }
    finnapp_fs_leave();
    g_preview_loaded = 1;
}

/* ---------------- layout ---------------- */

typedef struct {
    float sb_w, main_x, main_w;
    float up_x, up_y;
    float title_x, title_base;
    float search_x, search_y, search_w;
    float list_x, list_y, list_w, list_h;
    float prev_x, prev_y, prev_w, prev_h;
    float foot_y;
    int   rows;
    int   show_preview;
    int   show_date;
    int   up;
} files_layout;

static void layout_for(int ww, int wh, float s, files_layout *L) {
    memset(L, 0, sizeof *L);
    L->sb_w = s * SIDEBAR_W;
    if (L->sb_w > (float)ww * 0.45f) L->sb_w = (float)ww * 0.45f;
    L->main_x = L->sb_w;
    L->main_w = (float)ww - L->main_x;
    L->up = L->main_w > s * 300.0f;
    L->up_x = L->main_x + s * MAIN_PAD;
    L->up_y = s * HEADER_TOP;

    float up_sz = s * UP_SIZE;
    L->title_x = L->up ? L->up_x + up_sz + s * 10.0f : L->up_x;
    L->title_base = s * HEADER_TOP + up_sz * 0.78f;

    L->search_w = s * SEARCH_W;
    float max_search = L->main_w - (L->title_x - L->main_x) - s * (MAIN_PAD * 2.0f);
    if (L->search_w > max_search) L->search_w = max_search;
    if (L->search_w < s * 80.0f) L->search_w = 0.0f;
    L->search_x = L->main_x + L->main_w - s * MAIN_PAD - L->search_w;
    L->search_y = s * HEADER_TOP;

    L->list_x = L->main_x + s * MAIN_PAD;
    L->list_y = s * LIST_TOP;
    L->list_w = L->main_w - s * MAIN_PAD * 2.0f;
    L->list_h = (float)wh - L->list_y - s * FOOTER_H;
    if (L->list_h < s * 40.0f) L->list_h = s * 40.0f;

    L->show_preview = L->list_w >= s * 560.0f;
    if (L->show_preview) {
        L->prev_w = s * PREVIEW_W;
        if (L->prev_w > L->list_w * 0.46f) L->prev_w = L->list_w * 0.46f;
        L->prev_x = L->list_x + L->list_w - L->prev_w;
        L->prev_y = L->list_y;
        L->prev_h = L->list_h;
        L->list_w -= L->prev_w + s * PREVIEW_GAP;
    }

    L->show_date = L->list_w >= s * 320.0f;
    L->rows = (int)(L->list_h / (s * ROW_H));
    if (L->rows < 1) L->rows = 1;
    L->foot_y = (float)wh - s * 20.0f;
}

static void clamp_scroll(const files_layout *L) {
    int n = visible_count();
    int max = n - L->rows;
    if (max < 0) max = 0;
    if (g_scroll > max) g_scroll = max;
    if (g_scroll < 0) g_scroll = 0;
}

/* ---------------- drawing helpers ---------------- */

static void folder_glyph(uint32_t *L, int lw, int lh, float x, float y,
                         float sz, uint32_t col) {
    float tab_w = sz * 0.45f, tab_h = sz * 0.22f;
    finn_fill_rrect(L, lw, lh, x + sz * 0.08f, y + sz * 0.18f,
                    tab_w, tab_h * 1.4f, sz * 0.09f, col);
    finn_fill_rrect(L, lw, lh, x, y + sz * 0.30f, sz, sz * 0.60f,
                    sz * 0.14f, col);
}

static void file_glyph(uint32_t *L, int lw, int lh, float x, float y,
                       float sz, uint32_t col) {
    float w = sz * 0.72f, h = sz * 0.92f;
    finn_fill_rrect(L, lw, lh, x + (sz - w) * 0.5f, y + sz * 0.04f,
                    w, h, sz * 0.10f, col);
    /* a folded corner in the window's own glass colour */
    finn_fill_rrect(L, lw, lh, x + (sz - w) * 0.5f + w * 0.55f, y + sz * 0.04f,
                    w * 0.45f, h * 0.30f, sz * 0.06f, FINN_GLASS);
}

static void draw_chip(uint32_t *L, int lw, int lh, const files_layout *lay,
                      const char *text, float s, uint32_t ink) {
    float cap = finn_text_cap_height(&g_f_sub);
    finn_fill_rrect(L, lw, lh, lay->search_x, lay->search_y, lay->search_w, s * SEARCH_H,
                    s * 7.0f, finn_theme_dark() ? 0x14FFFFFFu : 0x0C000000u);
    finn_stroke_rrect(L, lw, lh, lay->search_x, lay->search_y, lay->search_w,
                      s * SEARCH_H, s * 7.0f, 1.0f, FINN_HAIR);
    finn_text_draw(&g_f_sub, L, lw, lh, lay->search_x + s * 10.0f,
                   lay->search_y + s * SEARCH_H * 0.5f + cap * 0.5f, text, ink);
}

/* ---------------- render ---------------- */

void finnfilesapp_render(uint32_t *layer, int lw, int lh,
                         int lx0, int ly0, int wx, int wy, int ww, int wh,
                         float scale, const finn_state *st, int focused) {
    (void)st;
    (void)focused;
    if (!g_ready) return;

    if (g_font_scale != scale) {
        if (load_fonts(scale) != 0) return;
        g_font_scale = scale;
    }
    if (!g_path[0]) {
        if (load_dir(g_home[0] ? g_home : "/") != 0) g_path[0] = '\0';
        refresh_places();
    }

    float s = scale * FINN_APP_REF;
    files_layout lay;
    layout_for(ww, wh, s, &lay);

    /* Keep the selection on screen. A control-plane select or a keyboard
     * move can land below the current window of rows, and a selected file
     * the person cannot see is not selected as far as they are concerned. */
    int sel_row = visible_index_of(g_sel);
    if (sel_row >= 0) {
        if (sel_row < g_scroll) g_scroll = sel_row;
        if (sel_row >= g_scroll + lay.rows) g_scroll = sel_row - lay.rows + 1;
    }
    clamp_scroll(&lay);

    float ox = (float)(wx - lx0);
    float oy = (float)(wy - ly0);

    /* The layer coordinates below are window-local, offset by the window's
     * place in the layer. */
    #define LX(v) ((v) + ox)
    #define LY(v) ((v) + oy)

    finn_frost_rrect(layer, lw, lh, lx0, ly0, LX(0.0f), LY(0.0f),
                     lay.sb_w, (float)wh, s * 8.0f, FINN_GLASS, 0.86f);
    finn_fill_rrect(layer, lw, lh, LX(lay.sb_w - 1.0f), LY(0.0f),
                    1.0f, (float)wh, 0.0f, FINN_HAIR);

    float cap_h = finn_text_cap_height(&g_f_title);
    finn_text_draw(&g_f_title, layer, lw, lh,
                   LX(s * SIDEBAR_PAD), LY(s * SIDEBAR_TITLE + cap_h),
                   "Finn Files", FINN_INK);

    finn_text_draw(&g_f_micro, layer, lw, lh,
                   LX(s * SIDEBAR_PAD), LY(s * PLACES_CAP), "PLACES", FINN_MUTE);

    int row = 0;
    for (int i = 0; i < PLACE_COUNT; i++) {
        if (!g_place_exists[i]) continue;
        float y = s * PLACES_TOP + (float)row * s * PLACE_STEP;
        float h = s * PLACE_H;
        int act = place_active(i);
        if (act)
            finn_fill_rrect(layer, lw, lh, LX(s * 6.0f), LY(y),
                            lay.sb_w - s * 12.0f, h, s * 5.0f,
                            0x24006FFFu);
        folder_glyph(layer, lw, lh, LX(s * SIDEBAR_PAD), LY(y + s * 7.0f),
                     s * 14.0f, act ? FINN_ON : FINN_MUTE);
        float lc = finn_text_cap_height(&g_f_label);
        finn_text_draw(&g_f_label, layer, lw, lh,
                       LX(s * (SIDEBAR_PAD + 24.0f)),
                       LY(y + h * 0.5f + lc * 0.45f),
                       g_places[i].label, act ? FINN_INK : FINN_DIM);
        row++;
    }

    /* Header: up, title, path, filter. */
    if (lay.up) {
        float ux = lay.up_x, uy = lay.up_y, us = s * UP_SIZE;
        finn_frost_rrect(layer, lw, lh, lx0, ly0, LX(ux), LY(uy), us, us,
                         us * 0.5f, FINN_GLASS, FINN_GLASS_TILE);
        finn_stroke_rrect(layer, lw, lh, LX(ux), LY(uy), us, us, us * 0.5f,
                          1.0f, FINN_HAIR);
        float ac = finn_text_cap_height(&g_f_label);
        finn_text_draw(&g_f_label, layer, lw, lh, LX(ux + us * 0.5f - s * 3.0f),
                       LY(uy + us * 0.5f + ac * 0.45f), "\xE2\x86\x91", FINN_DIM);
    }

    {
        const char *title = base_name(g_path);
        float tw = lay.search_x - lay.title_x - s * 10.0f;
        if (tw < s * 40.0f) tw = s * 40.0f;
        char fitted[256];
        finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, title, tw);
        float tc = finn_text_cap_height(&g_f_title);
        finn_text_draw(&g_f_title, layer, lw, lh, LX(lay.title_x),
                       LY(lay.title_base + tc * 0.1f), fitted, FINN_INK);

        if (lay.search_w > 0.0f) {
            draw_chip(layer, lw, lh, &lay, g_filter[0] ? g_filter : "Filter this folder",
                      s, g_filter[0] ? FINN_INK : FINN_MUTE);
            if (g_filter_focused) {
                float tw2 = finn_text_width(&g_f_sub, g_filter);
                float cx = lay.search_x + s * 10.0f + tw2 + s * 2.0f;
                finn_fill_rrect(layer, lw, lh, LX(cx),
                                LY(lay.search_y + s * 7.0f), 1.0f,
                                s * (SEARCH_H - 14.0f), 0.0f, FINN_ON);
            }
        }
    }

    /* The listing. */
    finn_fill_rrect(layer, lw, lh, LX(lay.list_x), LY(lay.list_y),
                    lay.list_w, lay.list_h, s * 8.0f,
                    finn_theme_dark() ? 0x08FFFFFFu : 0x06000000u);

    float rh = s * ROW_H;
    int nvis = visible_count();
    for (int k = 0; k < lay.rows; k++) {
        int entry = visible_entry(g_scroll + k);
        if (entry < 0) break;
        const file_entry *e = &g_entries[entry];
        float y = lay.list_y + (float)k * rh;
        int selected = entry == g_sel;

        if (selected)
            finn_fill_rrect(layer, lw, lh, LX(lay.list_x + s * 4.0f), LY(y + s * 1.0f),
                            lay.list_w - s * 8.0f, rh - s * 2.0f, s * 5.0f,
                            0x2E006FFFu);

        float icon_x = lay.list_x + s * 10.0f;
        if (e->is_dir)
            folder_glyph(layer, lw, lh, LX(icon_x), LY(y + rh * 0.5f - s * 7.0f),
                         s * 14.0f, selected ? FINN_ON : FINN_DIM);
        else
            file_glyph(layer, lw, lh, LX(icon_x), LY(y + rh * 0.5f - s * 7.0f),
                       s * 14.0f, selected ? FINN_ON : FINN_MUTE);

        float name_x = lay.list_x + s * 30.0f;
        float size_right = lay.list_x + lay.list_w - s * 10.0f;
        if (lay.show_date) size_right -= s * 104.0f;
        float name_max = size_right - name_x - s * 8.0f;
        if (name_max < s * 40.0f) name_max = s * 40.0f;
        char name[256];
        finnapp_ellipsis(&g_f_label, name, sizeof name, e->name, name_max);
        float lc = finn_text_cap_height(&g_f_label);
        finn_text_draw(&g_f_label, layer, lw, lh, LX(name_x),
                       LY(y + rh * 0.5f + lc * 0.45f), name,
                       selected ? FINN_INK : FINN_DIM);

        if (lay.show_date) {
            char date[32];
            finnapp_format_time(e->mtime, date, sizeof date);
            float dw = finn_text_width(&g_f_micro, date);
            finn_text_draw(&g_f_micro, layer, lw, lh,
                           LX(lay.list_x + lay.list_w - s * 10.0f - dw),
                           LY(y + rh * 0.5f + lc * 0.35f), date, FINN_MUTE);
        }

        char size[32];
        if (e->is_dir) snprintf(size, sizeof size, "%s", "\xE2\x80\x94");
        else finnapp_format_size(e->size, size, sizeof size);
        float sw = finn_text_width(&g_f_micro, size);
        finn_text_draw(&g_f_micro, layer, lw, lh,
                       LX(size_right - sw),
                       LY(y + rh * 0.5f + lc * 0.35f), size,
                       selected ? FINN_DIM : FINN_MUTE);
    }

    if (nvis == 0) {
        float lc = finn_text_cap_height(&g_f_label);
        const char *line = g_filter[0] ? "Nothing here matches that filter"
                                       : "This folder is empty";
        finn_text_draw(&g_f_label, layer, lw, lh,
                       LX(lay.list_x + lay.list_w * 0.5f - finn_text_width(&g_f_label, line) * 0.5f),
                       LY(lay.list_y + lay.list_h * 0.5f + lc * 0.5f),
                       line, FINN_DIM);
    } else if (nvis > lay.rows) {
        /* A restrained scroll indicator, not a full scrollbar. */
        float track_h = lay.list_h - s * 8.0f;
        float th = track_h * (float)lay.rows / (float)nvis;
        if (th < s * 24.0f) th = s * 24.0f;
        float maxscroll = (float)(nvis - lay.rows);
        float t = maxscroll > 0 ? (float)g_scroll / maxscroll : 0.0f;
        float ty = lay.list_y + s * 4.0f + (track_h - th) * t;
        finn_fill_rrect(layer, lw, lh, LX(lay.list_x + lay.list_w - s * 5.0f),
                        LY(ty), s * 3.0f, th, s * 1.5f, FINN_MUTE);
    }

    /* Preview. */
    if (lay.show_preview) {
        finn_frost_rrect(layer, lw, lh, lx0, ly0, LX(lay.prev_x), LY(lay.prev_y),
                         lay.prev_w, lay.prev_h, s * 8.0f, FINN_GLASS, 0.72f);
        finn_stroke_rrect(layer, lw, lh, LX(lay.prev_x), LY(lay.prev_y),
                          lay.prev_w, lay.prev_h, s * 8.0f, 1.0f, FINN_HAIR);

        float px = lay.prev_x + s * 16.0f;
        float py = lay.prev_y + s * 22.0f;
        float pw = lay.prev_w - s * 32.0f;

        if (g_sel < 0 || g_sel >= g_n_entries) {
            float lc = finn_text_cap_height(&g_f_sub);
            finn_text_draw(&g_f_sub, layer, lw, lh, LX(px), LY(py + lc),
                           "Select something to look at it.", FINN_MUTE);
        } else {
            const file_entry *e = &g_entries[g_sel];
            char fitted[256];
            finnapp_ellipsis(&g_f_title, fitted, sizeof fitted, e->name, pw);
            float tc = finn_text_cap_height(&g_f_title);
            finn_text_draw(&g_f_title, layer, lw, lh, LX(px), LY(py + tc), fitted, FINN_INK);

            char meta[128], date[32], size[32];
            finnapp_format_time(e->mtime, date, sizeof date);
            if (e->is_dir) snprintf(size, sizeof size, "Folder");
            else finnapp_format_size(e->size, size, sizeof size);
            snprintf(meta, sizeof meta, "%s \xC2\xB7 %s", size, date);
            float mc = finn_text_cap_height(&g_f_micro);
            finn_text_draw(&g_f_micro, layer, lw, lh, LX(px),
                           LY(py + tc + s * 16.0f + mc), meta, FINN_MUTE);

            float body_y = py + tc + s * 40.0f;
            float body_h = lay.prev_y + lay.prev_h - body_y - s * 14.0f;
            if (e->is_dir) {
                float lc = finn_text_cap_height(&g_f_label);
                finn_text_draw(&g_f_label, layer, lw, lh, LX(px),
                               LY(body_y + lc), "A folder", FINN_DIM);
            } else {
                load_preview();
                if (!g_preview_loaded || g_preview_len == 0) {
                    float lc = finn_text_cap_height(&g_f_label);
                    finn_text_draw(&g_f_label, layer, lw, lh, LX(px),
                                   LY(body_y + lc), "Nothing to preview", FINN_DIM);
                } else if (g_preview_binary) {
                    float lc = finn_text_cap_height(&g_f_label);
                    finn_text_draw(&g_f_label, layer, lw, lh, LX(px),
                                   LY(body_y + lc), "No text preview for this file",
                                   FINN_DIM);
                } else {
                    float line_h = s * 15.0f;
                    float ly = body_y;
                    char line[512];
                    size_t used = 0;
                    for (int i = 0; i <= g_preview_len; i++) {
                        char c = i < g_preview_len ? g_preview[i] : '\n';
                        if (c != '\n' && c != '\r' && used + 1 < sizeof line) {
                            line[used++] = c;
                            continue;
                        }
                        if (c == '\r') continue;
                        line[used] = '\0';
                        used = 0;
                        if (ly + line_h > body_y + body_h) break;

                        /* Tabs are four spaces, because the preview font has
                         * no tab stop of its own. */
                        char expanded[512];
                        size_t w = 0;
                        for (const char *q = line; *q && w + 4 < sizeof expanded; q++) {
                            if (*q == '\t') { for (int t = 0; t < 4; t++) expanded[w++] = ' '; }
                            else expanded[w++] = *q;
                        }
                        expanded[w] = '\0';

                        char fitted_line[512];
                        finnapp_ellipsis(&g_f_sub, fitted_line, sizeof fitted_line,
                                         expanded, pw);
                        finn_text_draw(&g_f_sub, layer, lw, lh, LX(px), LY(ly),
                                       fitted_line, FINN_DIM);
                        ly += line_h;
                    }
                }
            }
        }
    }

    /* Footer. */
    {
        char left[PATH_MAX + 300];
        if (g_error[0]) snprintf(left, sizeof left, "%s", g_error);
        else if (g_truncated) snprintf(left, sizeof left, "%d items, first 512 shown", nvis);
        else snprintf(left, sizeof left, "%d item%s", nvis, nvis == 1 ? "" : "s");

        const char *note = "Read-only";
        float nw = finn_text_width(&g_f_micro, note);
        float avail = lay.list_w - nw - s * 16.0f;
        char fitted[PATH_MAX + 300];
        finnapp_ellipsis(&g_f_micro, fitted, sizeof fitted, left, avail);

        float lc = finn_text_cap_height(&g_f_micro);
        finn_text_draw(&g_f_micro, layer, lw, lh, LX(lay.list_x),
                       LY(lay.foot_y + lc), fitted,
                       g_error[0] ? FINN_INK : FINN_MUTE);
        finn_text_draw(&g_f_micro, layer, lw, lh,
                       LX(lay.list_x + lay.list_w - nw),
                       LY(lay.foot_y + lc), note, FINN_MUTE);
    }

    #undef LX
    #undef LY
}

/* ---------------- input ---------------- */

int finnfilesapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                     float scale, int *out_sub) {
    if (out_sub) *out_sub = 0;
    if (px < wx || py < wy || px >= wx + ww || py >= wy + wh) return FINNFILES_HIT_NONE;

    float s = scale * FINN_APP_REF;
    files_layout lay;
    layout_for(ww, wh, s, &lay);

    float x = (float)(px - wx);
    float y = (float)(py - wy);

    /* Leave the top strip to the window manager: it is the titlebar, and a
     * drag there has to move the window. */
    if (y < s * 48.0f) return FINNFILES_HIT_NONE;

    if (x < lay.sb_w) {
        int row = 0;
        for (int i = 0; i < PLACE_COUNT; i++) {
            if (!g_place_exists[i]) continue;
            float ry = s * PLACES_TOP + (float)row * s * PLACE_STEP;
            if (y >= ry && y < ry + s * PLACE_H) {
                if (out_sub) *out_sub = i;
                return FINNFILES_HIT_PLACE;
            }
            row++;
        }
        return FINNFILES_HIT_NONE;
    }

    if (lay.up) {
        float us = s * UP_SIZE;
        if (x >= lay.up_x && x < lay.up_x + us &&
            y >= lay.up_y && y < lay.up_y + us)
            return FINNFILES_HIT_UP;
    }
    if (lay.search_w > 0 &&
        x >= lay.search_x && x < lay.search_x + lay.search_w &&
        y >= lay.search_y && y < lay.search_y + s * SEARCH_H)
        return FINNFILES_HIT_FILTER;

    if (x >= lay.list_x && x < lay.list_x + lay.list_w &&
        y >= lay.list_y && y < lay.list_y + lay.list_h) {
        int k = (int)((y - lay.list_y) / (s * ROW_H));
        int entry = visible_entry(g_scroll + k);
        if (entry >= 0) {
            if (out_sub) *out_sub = entry;
            return FINNFILES_HIT_ROW;
        }
    }

    return FINNFILES_HIT_NONE;
}

int finnfilesapp_press(int hit, int sub, int px, int py, float scale) {
    (void)px; (void)py; (void)scale;
    switch (hit) {
    case FINNFILES_HIT_PLACE: {
        if (sub < 0 || sub >= PLACE_COUNT || !g_place_exists[sub]) return 0;
        char p[PATH_MAX];
        place_path(sub, p, sizeof p);
        g_filter[0] = '\0';
        g_filter_focused = 0;
        return load_dir(p) == 0;
    }
    case FINNFILES_HIT_UP:
        g_filter[0] = '\0';
        g_filter_focused = 0;
        return go_up();
    case FINNFILES_HIT_FILTER:
        g_filter_focused = 1;
        bump();
        return 1;
    case FINNFILES_HIT_ROW: {
        if (sub < 0 || sub >= g_n_entries) return 0;
        g_filter_focused = 0;
        double t = now_ms();
        int was_double = (g_last_click_entry == sub &&
                          t - g_last_click_ms <= (double)DOUBLE_MS);
        g_last_click_entry = sub;
        g_last_click_ms = t;

        if (sub != g_sel) select_entry(sub);

        if (was_double && g_entries[sub].is_dir) {
            g_last_click_entry = -1;
            return enter_selected();
        }
        return sub == g_sel ? 1 : 0;
    }
    default:
        return 0;
    }
}

void finnfilesapp_release(void) {
    /* Nothing is held between press and release: the rows act on the press,
     * because selecting a file is not irreversible. */
}

static void filter_changed(void) {
    g_scroll = 0;
    ensure_selection_visible();
    clear_preview();
    bump();
}

int finnfilesapp_key(const finn_key_event *e) {
    if (!e->pressed) return 0;

    if (g_filter_focused) {
        if (e->code == KEY_ESC) {
            if (g_filter[0]) g_filter[0] = '\0';
            else g_filter_focused = 0;
            filter_changed();
            return 1;
        }
        if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
            g_filter_focused = 0;
            bump();
            return 1;
        }
        if (e->code == KEY_BACKSPACE) {
            size_t len = strlen(g_filter);
            if (len) g_filter[len - 1] = '\0';
            filter_changed();
            return 1;
        }
        if (e->code == KEY_UP)    { move_selection(-1); return 1; }
        if (e->code == KEY_DOWN)  { move_selection(1);  return 1; }
        if (e->keysym >= 32 && e->keysym <= 126 &&
            strlen(g_filter) + 1 < sizeof g_filter) {
            size_t len = strlen(g_filter);
            g_filter[len] = (char)e->keysym;
            g_filter[len + 1] = '\0';
            filter_changed();
            return 1;
        }
        return 1;
    }

    switch (e->code) {
    case KEY_UP:        move_selection(-1); return 1;
    case KEY_DOWN:      move_selection(1);  return 1;
    case KEY_PAGEUP:    g_scroll -= 6; if (g_scroll < 0) g_scroll = 0; bump(); return 1;
    case KEY_PAGEDOWN:  g_scroll += 6; bump(); return 1;
    case KEY_HOME:      g_sel = visible_entry(0); g_scroll = 0; clear_preview(); bump(); return 1;
    case KEY_END:       g_sel = visible_entry(visible_count() - 1); clear_preview(); bump(); return 1;
    case KEY_ENTER:
    case KEY_KPENTER:   return enter_selected();
    case KEY_BACKSPACE:
        if (g_filter[0]) {
            size_t len = strlen(g_filter);
            g_filter[len - 1] = '\0';
            filter_changed();
            return 1;
        }
        return go_up();
    case KEY_ESC:
        if (g_filter[0]) {
            g_filter[0] = '\0';
            filter_changed();
            return 1;
        }
        return 0;
    case KEY_F5:
        return load_dir(g_path) == 0;
    default:
        if (e->keysym >= 32 && e->keysym <= 126 &&
            strlen(g_filter) + 1 < sizeof g_filter) {
            size_t len = strlen(g_filter);
            g_filter[len] = (char)e->keysym;
            g_filter[len + 1] = '\0';
            g_filter_focused = 1;
            filter_changed();
            return 1;
        }
        return 0;
    }
}

int finnfilesapp_scroll(int clicks, int px, int py, float scale) {
    (void)px; (void)py; (void)scale;
    if (!clicks) return 0;
    int before = g_scroll;
    g_scroll -= clicks * 3;
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll != before) { bump(); return 1; }
    return 0;
}

/* ---------------- control-plane navigation ---------------- */

int finnfilesapp_open_path(const char *path) {
    if (!path || !*path) return -1;
    return load_dir(path);
}

int finnfilesapp_select(const char *name) {
    if (!name || !*name) return -1;
    g_filter[0] = '\0';
    g_filter_focused = 0;
    for (int i = 0; i < g_n_entries; i++) {
        if (!strcmp(g_entries[i].name, name)) {
            g_sel = i;
            clear_preview();
            ensure_selection_visible();
            bump();
            return 0;
        }
    }
    set_error("No such item in this folder");
    return -1;
}

const char *finnfilesapp_path(void) { return g_path; }

const char *finnfilesapp_selection(void) {
    if (g_sel < 0 || g_sel >= g_n_entries) return "";
    return g_entries[g_sel].name;
}

const char *finnfilesapp_error(void) { return g_error; }

uint32_t finnfilesapp_build_key(float scale) {
    uint32_t h = 2166136261u;
    #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    MIX((int)(scale * 1000.0f));
    MIX(g_rev);
    for (const char *p = g_path; *p; p++) MIX(*p);
    for (const char *p = g_filter; *p; p++) MIX(*p);
    MIX(g_sel + 2); MIX(g_scroll); MIX(g_filter_focused);
    for (const char *p = g_error; *p; p++) MIX(*p);
    #undef MIX
    return h ? h : 1u;
}

int finnfilesapp_init(const char *icon_dir, const char *font_dir) {
    (void)icon_dir;
    snprintf(g_font_dir, sizeof g_font_dir, "%s", font_dir ? font_dir : "");
    finnapp_home(g_home, sizeof g_home);
    refresh_places();

    /* Start in the home folder. The first render can load it too, but doing
     * it here means the control plane can answer files.get before the window
     * has ever been drawn. */
    if (load_dir(g_home) != 0) {
        if (load_dir("/") != 0) {
            g_path[0] = '\0';
        }
    }
    g_ready = 1;
    return 0;
}

void finnfilesapp_free(void) {
    free_entries();
    finn_font_free(&g_f_title);
    finn_font_free(&g_f_label);
    finn_font_free(&g_f_sub);
    finn_font_free(&g_f_micro);
    g_font_scale = -1.0f;
    g_ready = 0;
}
