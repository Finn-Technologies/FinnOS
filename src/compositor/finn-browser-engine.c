/*
 * finn-browser-engine — the out-of-process page engine helper for Finn Browser.
 *
 * Runs headless Chromium in a separate process boundary and manages tabs,
 * navigations, screenshots, and input via the Chrome DevTools Protocol (CDP)
 * over descriptors 3 and 4.
 *
 * Protocol over FINN_BROWSER_SOCK connects to finn-comp's native browser window.
 * JSON parsing is strictly restricted to this helper file and never crosses
 * into finn-comp or finnd.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <png.h>
#include <jpeglib.h>

#include "finnbrowser.h"
#include "finnproto.h"
#include "finnsock.h"

#define FINN_BROWSER_MAX_TABS 6
#define CDP_PENDING_MAX 128
#define CDP_BUF_INIT (64 * 1024)
#define CDP_BUF_MAX (16 * 1024 * 1024)

enum engine_type {
    ENGINE_NONE = 0,
    ENGINE_BIN,
    ENGINE_FLATPAK
};

enum cdp_cmd_kind {
    CDP_CMD_NONE = 0,
    CDP_CMD_CREATE_TARGET,
    CDP_CMD_ATTACH_TARGET,
    CDP_CMD_NAV_HISTORY,
    CDP_CMD_CAPTURE_SCREENSHOT,
    CDP_CMD_RUNTIME_EVAL_FIND,
    CDP_CMD_OTHER
};

typedef struct {
    int cdp_id;
    int cmd_kind;
    int tab_id;
} pending_cdp_cmd;

typedef struct {
    int id;                                /* 1..FINN_BROWSER_MAX_TABS */
    int used;
    char target_id[128];
    char session_id[128];
    char url[FINN_BROWSER_URL_MAX];
    char title[FINN_BROWSER_TITLE_MAX];
    int loading;
    int can_back;
    int can_forward;
    int attached;
    char pending_nav_url[FINN_BROWSER_URL_MAX];
    char find_query[FINN_BROWSER_FIND_MAX];
    int find_seq;
    int prev_history_id;
    int next_history_id;
} browser_tab;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
} png_mem_src;

struct jpeg_custom_error {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static volatile sig_atomic_t g_stop = 0;
static pid_t g_child_pid = -1;
static char g_profile_dir[PATH_MAX];
static browser_tab g_tabs[FINN_BROWSER_MAX_TABS];
static pending_cdp_cmd g_pending_ring[CDP_PENDING_MAX];
static int g_next_cdp_id = 1;
static int g_active_tab_id = 1;
static int g_ready_sent = 0;
static int g_vp_w = 1000;
static int g_vp_h = 700;
static int g_viewport_received = 0;

static int g_screenshot_in_flight = 0;
static int64_t g_screenshot_due_ms = 0;
static int64_t g_followup_due_ms = 0;

static void sig_handler(int sig) {
    (void)sig;
    g_stop = 1;
}

static int64_t get_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}

static void schedule_screenshot(int delay_ms) {
    int64_t due = get_monotonic_ms() + delay_ms;
    if (g_screenshot_due_ms == 0 || due < g_screenshot_due_ms) {
        g_screenshot_due_ms = due;
    }
}

static void schedule_followup(int delay_ms) {
    int64_t due = get_monotonic_ms() + delay_ms;
    if (g_followup_due_ms == 0 || due < g_followup_due_ms) {
        g_followup_due_ms = due;
    }
}

static browser_tab *get_tab(int id) {
    if (id >= 1 && id <= FINN_BROWSER_MAX_TABS && g_tabs[id - 1].used) {
        return &g_tabs[id - 1];
    }
    return NULL;
}

static browser_tab *get_active_tab(void) {
    browser_tab *tab = get_tab(g_active_tab_id);
    if (!tab) {
        for (int i = 0; i < FINN_BROWSER_MAX_TABS; i++) {
            if (g_tabs[i].used) {
                g_active_tab_id = g_tabs[i].id;
                return &g_tabs[i];
            }
        }
    }
    return tab;
}

static browser_tab *find_tab_by_session(const char *session_id) {
    if (!session_id || !*session_id) return NULL;
    for (int i = 0; i < FINN_BROWSER_MAX_TABS; i++) {
        if (g_tabs[i].used && strcmp(g_tabs[i].session_id, session_id) == 0) {
            return &g_tabs[i];
        }
    }
    return NULL;
}

static browser_tab *find_tab_by_target(const char *target_id) {
    if (!target_id || !*target_id) return NULL;
    for (int i = 0; i < FINN_BROWSER_MAX_TABS; i++) {
        if (g_tabs[i].used && strcmp(g_tabs[i].target_id, target_id) == 0) {
            return &g_tabs[i];
        }
    }
    return NULL;
}

static void cleanup_profile_dir(const char *dir) {
    if (!dir || !*dir) return;
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    char sub[PATH_MAX];
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        snprintf(sub, sizeof(sub), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (lstat(sub, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                cleanup_profile_dir(sub);
            } else {
                unlink(sub);
            }
        }
    }
    closedir(d);
    rmdir(dir);
}

static void json_escape_string(const char *src, char *dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return;
    dst[0] = '\0';
    if (!src) return;

    size_t o = 0;
    for (size_t i = 0; src[i] && o + 8 < dst_cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"') {
            dst[o++] = '\\'; dst[o++] = '"';
        } else if (c == '\\') {
            dst[o++] = '\\'; dst[o++] = '\\';
        } else if (c == '\n') {
            dst[o++] = '\\'; dst[o++] = 'n';
        } else if (c == '\r') {
            dst[o++] = '\\'; dst[o++] = 'r';
        } else if (c == '\t') {
            dst[o++] = '\\'; dst[o++] = 't';
        } else if (c == '\b') {
            dst[o++] = '\\'; dst[o++] = 'b';
        } else if (c == '\f') {
            dst[o++] = '\\'; dst[o++] = 'f';
        } else if (c < 0x20) {
            o += snprintf(dst + o, dst_cap - o, "\\u%04x", c);
        } else {
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

static const char *json_find_field(const char *json, const char *key) {
    if (!json || !key) return NULL;
    size_t klen = strlen(key);
    const char *p = json;

    while (*p) {
        if (*p == '"') {
            p++;
            if (strncmp(p, key, klen) == 0 && p[klen] == '"') {
                const char *after = p + klen + 1;
                while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') {
                    after++;
                }
                if (*after == ':') {
                    after++;
                    while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') {
                        after++;
                    }
                    return after;
                }
            }
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1)) p++;
                p++;
            }
            if (*p == '"') p++;
        } else {
            p++;
        }
    }
    return NULL;
}

static int json_get_string(const char *json, const char *key, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return -1;
    out[0] = '\0';

    const char *val = json_find_field(json, key);
    if (!val || *val != '"') return -1;
    val++;

    size_t o = 0;
    while (*val && *val != '"') {
        if (*val == '\\') {
            val++;
            if (!*val) break;
            if (*val == 'u' && isxdigit((unsigned char)val[1]) && isxdigit((unsigned char)val[2]) &&
                isxdigit((unsigned char)val[3]) && isxdigit((unsigned char)val[4])) {
                char hex[5] = { val[1], val[2], val[3], val[4], '\0' };
                unsigned long cp = strtoul(hex, NULL, 16);
                val += 4;
                if (cp < 0x80) {
                    if (o + 1 < out_cap) out[o++] = (char)cp;
                } else if (cp < 0x800) {
                    if (o + 2 < out_cap) {
                        out[o++] = (char)(0xC0 | (cp >> 6));
                        out[o++] = (char)(0x80 | (cp & 0x3F));
                    }
                } else {
                    if (o + 3 < out_cap) {
                        out[o++] = (char)(0xE0 | (cp >> 12));
                        out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[o++] = (char)(0x80 | (cp & 0x3F));
                    }
                }
            } else {
                char c = *val;
                if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c == 'b') c = '\b';
                else if (c == 'f') c = '\f';
                if (o + 1 < out_cap) out[o++] = c;
            }
        } else {
            if (o + 1 < out_cap) out[o++] = *val;
        }
        val++;
    }
    out[o] = '\0';
    return (*val == '"') ? 0 : -1;
}

static int json_get_int(const char *json, const char *key, int *out) {
    const char *val = json_find_field(json, key);
    if (!val) return -1;

    char *end = NULL;
    long v = strtol(val, &end, 10);
    if (end == val) return -1;
    if (out) *out = (int)v;
    return 0;
}

static const char *json_get_data_slice(const char *json, size_t *out_len) {
    const char *val = json_find_field(json, "data");
    if (!val || *val != '"') return NULL;
    val++;

    /* Base64 payloads contain no escaped characters or internal quotes */
    const char *end = strchr(val, '"');
    if (!end) return NULL;
    if (out_len) *out_len = (size_t)(end - val);
    return val;
}

static int b64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint8_t *base64_decode(const char *src, size_t src_len, size_t *out_len) {
    if (!src || src_len == 0) return NULL;
    size_t alloc_size = (src_len * 3) / 4 + 4;
    uint8_t *out = malloc(alloc_size);
    if (!out) return NULL;

    size_t o = 0;
    int val = 0, valb = -8;
    for (size_t i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == '=') break;
        int d = b64_char_value(c);
        if (d < 0) continue;
        val = (val << 6) | d;
        valb += 6;
        if (valb >= 0) {
            out[o++] = (uint8_t)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    if (out_len) *out_len = o;
    return out;
}

static int cdp_exec(int cmd_w, int cmd_kind, int tab_id, const char *session_id,
                    const char *method, const char *params_json) {
    int id = g_next_cdp_id++;
    if (g_next_cdp_id <= 0) g_next_cdp_id = 1;

    g_pending_ring[id % CDP_PENDING_MAX] = (pending_cdp_cmd){
        .cdp_id = id,
        .cmd_kind = cmd_kind,
        .tab_id = tab_id
    };

    size_t need = 128 + (session_id ? strlen(session_id) : 0) +
                  strlen(method) + (params_json ? strlen(params_json) : 0);
    char stack_buf[2048];
    char *buf = stack_buf;
    if (need >= sizeof(stack_buf)) {
        buf = malloc(need + 64);
        if (!buf) return -1;
    }

    int n;
    if (session_id && session_id[0]) {
        if (params_json && params_json[0]) {
            n = sprintf(buf, "{\"id\":%d,\"sessionId\":\"%s\",\"method\":\"%s\",\"params\":%s}",
                        id, session_id, method, params_json);
        } else {
            n = sprintf(buf, "{\"id\":%d,\"sessionId\":\"%s\",\"method\":\"%s\"}",
                        id, session_id, method);
        }
    } else {
        if (params_json && params_json[0]) {
            n = sprintf(buf, "{\"id\":%d,\"method\":\"%s\",\"params\":%s}",
                        id, method, params_json);
        } else {
            n = sprintf(buf, "{\"id\":%d,\"method\":\"%s\"}",
                        id, method);
        }
    }

    /* Out-of-process CDP pipe frames are JSON followed by a single null byte */
    int r = (n > 0) ? finn_write_all(cmd_w, buf, (size_t)n + 1) : -1;
    if (buf != stack_buf) free(buf);
    return r;
}

static void send_tab_state(int sock_fd, const browser_tab *tab, int is_active) {
    if (!tab || !tab->used) return;

    char url_clean[FINN_BROWSER_URL_MAX];
    char title_clean[FINN_BROWSER_TITLE_MAX];
    finn_browser_text_clean(url_clean, sizeof(url_clean),
                            tab->url[0] ? tab->url : "about:blank");
    finn_browser_text_clean(title_clean, sizeof(title_clean),
                            tab->title[0] ? tab->title : "New Tab");

    char line[FINN_BROWSER_LINE_MAX];
    snprintf(line, sizeof(line), "TAB %d %d %d %d %d %s\t%s\n",
             tab->id, is_active ? 1 : 0, tab->loading ? 1 : 0,
             tab->can_back ? 1 : 0, tab->can_forward ? 1 : 0,
             url_clean, title_clean);
    finn_write_all(sock_fd, line, strlen(line));
}

static void png_mem_read_cb(png_structp png_ptr, png_bytep data, png_size_t length) {
    png_mem_src *src = (png_mem_src *)png_get_io_ptr(png_ptr);
    if (!src || src->offset + length > src->size) {
        png_error(png_ptr, "Read overflow in memory PNG stream");
        return;
    }
    memcpy(data, src->data + src->offset, length);
    src->offset += length;
}

static int decode_png_and_send(int sock_fd, int tab_id, const uint8_t *raw, size_t raw_len) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return -1;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        return -1;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        return -1;
    }

    png_mem_src src = { .data = raw, .size = raw_len, .offset = 0 };
    png_set_read_fn(png, &src, png_mem_read_cb);
    png_read_info(png, info);

    png_uint_32 w = 0, h = 0;
    int bit_depth = 0, color_type = 0;
    png_get_IHDR(png, info, &w, &h, &bit_depth, &color_type, NULL, NULL, NULL);

    size_t frame_bytes = finn_browser_frame_bytes((int)w, (int)h);
    if (frame_bytes == 0) {
        png_destroy_read_struct(&png, &info, NULL);
        return -1;
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    /* Little-endian native-endian 0xFFRRGGBB matches memory order B, G, R, A */
    png_set_bgr(png);
    png_read_update_info(png, info);

    uint32_t *pixels = malloc(frame_bytes);
    if (!pixels) {
        png_destroy_read_struct(&png, &info, NULL);
        return -1;
    }

    png_bytep *row_pointers = malloc(h * sizeof(png_bytep));
    if (!row_pointers) {
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        return -1;
    }

    for (png_uint_32 y = 0; y < h; y++) {
        row_pointers[y] = (png_bytep)(pixels + (size_t)y * w);
    }

    png_read_image(png, row_pointers);
    png_read_end(png, NULL);

    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);

    size_t total_px = (size_t)w * h;
    for (size_t i = 0; i < total_px; i++) {
        pixels[i] |= 0xFF000000u;
    }

    char hdr[64];
    int hlen = finn_browser_frame_header(hdr, sizeof(hdr), tab_id, (int)w, (int)h);
    int ret = 0;
    if (hlen > 0) {
        if (finn_write_all(sock_fd, hdr, (size_t)hlen) != 0 ||
            finn_write_all(sock_fd, pixels, frame_bytes) != 0) {
            ret = -1;
        }
    } else {
        ret = -1;
    }

    free(pixels);
    return ret;
}

static void jpeg_error_exit_cb(j_common_ptr cinfo) {
    struct jpeg_custom_error *myerr = (struct jpeg_custom_error *)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

static int decode_jpeg_and_send(int sock_fd, int tab_id, const uint8_t *raw, size_t raw_len) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_custom_error jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit_cb;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)raw, raw_len);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;
    size_t frame_bytes = finn_browser_frame_bytes(w, h);
    if (frame_bytes == 0) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    uint32_t *pixels = malloc(frame_bytes);
    if (!pixels) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    JSAMPARRAY row_buf = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr)&cinfo, JPOOL_IMAGE, w * 3, 1);

    uint32_t *dst = pixels;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, row_buf, 1);
        const unsigned char *rgb = row_buf[0];
        for (int x = 0; x < w; x++) {
            uint32_t r = rgb[x * 3 + 0];
            uint32_t g = rgb[x * 3 + 1];
            uint32_t b = rgb[x * 3 + 2];
            *dst++ = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    char hdr[64];
    int hlen = finn_browser_frame_header(hdr, sizeof(hdr), tab_id, w, h);
    int ret = 0;
    if (hlen > 0) {
        if (finn_write_all(sock_fd, hdr, (size_t)hlen) != 0 ||
            finn_write_all(sock_fd, pixels, frame_bytes) != 0) {
            ret = -1;
        }
    } else {
        ret = -1;
    }

    free(pixels);
    return ret;
}

static int decode_and_send_frame(int sock_fd, int tab_id, const uint8_t *raw, size_t raw_len) {
    if (!raw || raw_len < 8) return -1;

    if (raw[0] == 0x89 && raw[1] == 'P' && raw[2] == 'N' && raw[3] == 'G' &&
        raw[4] == 0x0D && raw[5] == 0x0A && raw[6] == 0x1A && raw[7] == 0x0A) {
        return decode_png_and_send(sock_fd, tab_id, raw, raw_len);
    }

    if (raw[0] == 0xFF && raw[1] == 0xD8) {
        return decode_jpeg_and_send(sock_fd, tab_id, raw, raw_len);
    }

    return -1;
}

static void request_screenshot(int cmd_w, browser_tab *tab) {
    if (!tab || !tab->attached || !tab->session_id[0]) return;
    cdp_exec(cmd_w, CDP_CMD_CAPTURE_SCREENSHOT, tab->id, tab->session_id,
             "Page.captureScreenshot", "{\"format\":\"png\"}");
}

static int create_tab(int cmd_w, int sock_fd, const char *url) {
    int free_slot = -1;
    for (int i = 0; i < FINN_BROWSER_MAX_TABS; i++) {
        if (!g_tabs[i].used) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        finn_write_str(sock_fd, "ERROR 0 Tab limit reached\n");
        return -1;
    }

    browser_tab *tab = &g_tabs[free_slot];
    memset(tab, 0, sizeof(*tab));
    tab->id = free_slot + 1;
    tab->used = 1;
    tab->loading = 1;
    if (url && url[0]) {
        strncpy(tab->pending_nav_url, url, sizeof(tab->pending_nav_url) - 1);
        strncpy(tab->url, url, sizeof(tab->url) - 1);
    } else {
        strncpy(tab->pending_nav_url, "https://example.com/", sizeof(tab->pending_nav_url) - 1);
        strncpy(tab->url, "https://example.com/", sizeof(tab->url) - 1);
    }
    strncpy(tab->title, "New Tab", sizeof(tab->title) - 1);

    cdp_exec(cmd_w, CDP_CMD_CREATE_TARGET, tab->id, NULL,
             "Target.createTarget", "{\"url\":\"about:blank\"}");
    return tab->id;
}

static void on_tab_attached(int cmd_w, int sock_fd, browser_tab *tab, const char *session_id) {
    if (!tab || tab->attached) return;
    tab->attached = 1;
    strncpy(tab->session_id, session_id, sizeof(tab->session_id) - 1);

    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Page.enable", NULL);
    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Runtime.enable", NULL);

    char metrics[128];
    snprintf(metrics, sizeof(metrics),
             "{\"width\":%d,\"height\":%d,\"deviceScaleFactor\":1,\"mobile\":false}",
             g_vp_w, g_vp_h);
    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id,
             "Emulation.setDeviceMetricsOverride", metrics);

    if (tab->pending_nav_url[0]) {
        char esc[FINN_BROWSER_URL_MAX * 2];
        json_escape_string(tab->pending_nav_url, esc, sizeof(esc));
        char pnav[FINN_BROWSER_URL_MAX * 2 + 32];
        snprintf(pnav, sizeof(pnav), "{\"url\":\"%s\"}", esc);
        cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Page.navigate", pnav);
        tab->pending_nav_url[0] = '\0';
    }

    char sc[128];
    snprintf(sc, sizeof(sc),
             "{\"format\":\"png\",\"everyNthFrame\":1,\"maxWidth\":%d,\"maxHeight\":%d}",
             g_vp_w, g_vp_h);
    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Page.startScreencast", sc);

    if (!g_ready_sent) {
        g_ready_sent = 1;
        finn_write_str(sock_fd, "READY\n");
    }

    send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
    if (tab->id == g_active_tab_id) {
        char act[32];
        snprintf(act, sizeof(act), "ACTIVE %d\n", tab->id);
        finn_write_str(sock_fd, act);
    }

    schedule_screenshot(40);
}

static void close_tab(int cmd_w, int sock_fd, int tab_id) {
    browser_tab *tab = get_tab(tab_id);
    if (!tab) return;

    if (tab->target_id[0]) {
        char p[256];
        snprintf(p, sizeof(p), "{\"targetId\":\"%s\"}", tab->target_id);
        cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, NULL, "Target.closeTarget", p);
    }

    char line[64];
    snprintf(line, sizeof(line), "TABCLOSED %d\n", tab_id);
    finn_write_all(sock_fd, line, strlen(line));
    snprintf(line, sizeof(line), "CLOSED %d\n", tab_id);
    finn_write_all(sock_fd, line, strlen(line));

    memset(tab, 0, sizeof(*tab));

    if (g_active_tab_id == tab_id) {
        browser_tab *next = get_active_tab();
        if (next) {
            g_active_tab_id = next->id;
            snprintf(line, sizeof(line), "ACTIVE %d\n", g_active_tab_id);
            finn_write_all(sock_fd, line, strlen(line));
            send_tab_state(sock_fd, next, 1);
            schedule_screenshot(0);
        }
    }
}

static void parse_nav_history_response(int sock_fd, browser_tab *tab, const char *msg) {
    int cur_idx = 0;
    json_get_int(msg, "currentIndex", &cur_idx);

    const char *arr = json_find_field(msg, "entries");
    if (!arr || *arr != '[') return;

    int entry_count = 0;
    int prev_entry_id = -1, next_entry_id = -1;
    char cur_url[FINN_BROWSER_URL_MAX] = {0};
    char cur_title[FINN_BROWSER_TITLE_MAX] = {0};

    const char *p = arr + 1;
    while (*p && *p != ']') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == '{') {
            const char *obj_start = p;
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '"') {
                    p++;
                    while (*p && *p != '"') {
                        if (*p == '\\' && *(p + 1)) p++;
                        p++;
                    }
                    if (*p == '"') p++;
                } else {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            }
            size_t obj_len = (size_t)(p - obj_start);
            char obj_buf[4096];
            if (obj_len < sizeof(obj_buf)) {
                memcpy(obj_buf, obj_start, obj_len);
                obj_buf[obj_len] = '\0';

                int eid = -1;
                json_get_int(obj_buf, "id", &eid);
                if (entry_count == cur_idx) {
                    json_get_string(obj_buf, "url", cur_url, sizeof(cur_url));
                    json_get_string(obj_buf, "title", cur_title, sizeof(cur_title));
                } else if (entry_count == cur_idx - 1) {
                    prev_entry_id = eid;
                } else if (entry_count == cur_idx + 1) {
                    next_entry_id = eid;
                }
            }
            entry_count++;
        } else {
            if (*p && *p != ']') p++;
        }
    }

    tab->can_back = (cur_idx > 0);
    tab->can_forward = (cur_idx + 1 < entry_count);
    tab->prev_history_id = prev_entry_id;
    tab->next_history_id = next_entry_id;

    if (cur_url[0]) {
        strncpy(tab->url, cur_url, sizeof(tab->url) - 1);
    }
    if (cur_title[0]) {
        strncpy(tab->title, cur_title, sizeof(tab->title) - 1);
    }

    send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
}

static void handle_back(int cmd_w, int tab_id) {
    browser_tab *tab = get_tab(tab_id ? tab_id : g_active_tab_id);
    if (!tab || !tab->attached) return;
    if (tab->prev_history_id > 0) {
        char p[64];
        snprintf(p, sizeof(p), "{\"entryId\":%d}", tab->prev_history_id);
        cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Page.navigateToHistoryEntry", p);
    } else {
        cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Runtime.evaluate",
                 "{\"expression\":\"window.history.back()\"}");
    }
    cdp_exec(cmd_w, CDP_CMD_NAV_HISTORY, tab->id, tab->session_id, "Page.getNavigationHistory", NULL);
    schedule_screenshot(40);
    schedule_followup(250);
}

static void handle_forward(int cmd_w, int tab_id) {
    browser_tab *tab = get_tab(tab_id ? tab_id : g_active_tab_id);
    if (!tab || !tab->attached) return;
    if (tab->next_history_id > 0) {
        char p[64];
        snprintf(p, sizeof(p), "{\"entryId\":%d}", tab->next_history_id);
        cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Page.navigateToHistoryEntry", p);
    } else {
        cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Runtime.evaluate",
                 "{\"expression\":\"window.history.forward()\"}");
    }
    cdp_exec(cmd_w, CDP_CMD_NAV_HISTORY, tab->id, tab->session_id, "Page.getNavigationHistory", NULL);
    schedule_screenshot(40);
    schedule_followup(250);
}

static void handle_mouse(int cmd_w, const char *line) {
    char tok0[32] = {0}, tok1[32] = {0}, tok2[32] = {0}, tok3[32] = {0};
    char tok4[32] = {0}, tok5[32] = {0}, tok6[32] = {0}, tok7[32] = {0};
    int count = sscanf(line, "%31s %31s %31s %31s %31s %31s %31s %31s",
                       tok0, tok1, tok2, tok3, tok4, tok5, tok6, tok7);
    if (count < 6) return;

    int tab_id = g_active_tab_id;
    const char *type_str = tok1;
    int x = 0, y = 0;
    const char *btn_str = "none";
    int buttons = 0, clicks = 1;

    if (isdigit((unsigned char)tok1[0]) && count >= 7) {
        tab_id = atoi(tok1);
        type_str = tok2;
        x = atoi(tok3);
        y = atoi(tok4);
        btn_str = tok5;
        clicks = atoi(tok6);
        if (strcmp(btn_str, "left") == 0) buttons = 1;
        else if (strcmp(btn_str, "right") == 0) buttons = 2;
        else if (strcmp(btn_str, "middle") == 0) buttons = 4;
    } else {
        type_str = tok1;
        x = atoi(tok2);
        y = atoi(tok3);
        btn_str = tok4;
        buttons = atoi(tok5);
        if (count >= 7) clicks = atoi(tok6);
    }

    browser_tab *tab = get_tab(tab_id);
    if (!tab || !tab->attached) return;

    const char *cdp_type = "mouseMoved";
    if (strcmp(type_str, "down") == 0 || strcmp(type_str, "press") == 0) {
        cdp_type = "mousePressed";
    } else if (strcmp(type_str, "up") == 0 || strcmp(type_str, "release") == 0) {
        cdp_type = "mouseReleased";
    }

    const char *cdp_btn = "none";
    if (strcmp(btn_str, "left") == 0) cdp_btn = "left";
    else if (strcmp(btn_str, "right") == 0) cdp_btn = "right";
    else if (strcmp(btn_str, "middle") == 0) cdp_btn = "middle";

    char p[256];
    snprintf(p, sizeof(p),
             "{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"button\":\"%s\",\"buttons\":%d,\"clickCount\":%d}",
             cdp_type, x, y, cdp_btn, buttons, clicks);
    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Input.dispatchMouseEvent", p);

    if (strcmp(cdp_type, "mousePressed") == 0 || strcmp(cdp_type, "mouseReleased") == 0) {
        schedule_screenshot(40);
    }
}

static void handle_wheel(int cmd_w, const char *line) {
    char tok0[32] = {0}, tok1[32] = {0}, tok2[32] = {0}, tok3[32] = {0};
    char tok4[32] = {0}, tok5[32] = {0};
    int count = sscanf(line, "%31s %31s %31s %31s %31s %31s",
                       tok0, tok1, tok2, tok3, tok4, tok5);
    if (count < 5) return;

    int tab_id = g_active_tab_id;
    int x = 0, y = 0, dx = 0, dy = 0;

    if (count >= 6) {
        tab_id = atoi(tok1);
        x = atoi(tok2);
        y = atoi(tok3);
        dx = atoi(tok4);
        dy = atoi(tok5);
    } else {
        x = atoi(tok1);
        y = atoi(tok2);
        dx = atoi(tok3);
        dy = atoi(tok4);
    }

    browser_tab *tab = get_tab(tab_id);
    if (!tab || !tab->attached) return;

    char p[256];
    snprintf(p, sizeof(p),
             "{\"type\":\"mouseWheel\",\"x\":%d,\"y\":%d,\"deltaX\":%d,\"deltaY\":%d}",
             x, y, dx, dy);
    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Input.dispatchMouseEvent", p);

    if (dx != 0 || dy != 0) {
        char scr[128];
        snprintf(scr, sizeof(scr),
                 "{\"expression\":\"window.scrollBy(%d,%d);\"}", dx, dy);
        cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Runtime.evaluate", scr);
    }

    schedule_screenshot(40);
}

static void handle_key(int cmd_w, const char *line) {
    char tok0[32] = {0}, tok1[32] = {0}, tok2[32] = {0}, tok3[64] = {0};
    char tok4[64] = {0}, tok5[32] = {0}, tok6[32] = {0};
    int count = sscanf(line, "%31s %31s %31s %63s %63s %31s %31s",
                       tok0, tok1, tok2, tok3, tok4, tok5, tok6);
    if (count < 6) return;

    int tab_id = g_active_tab_id;
    const char *type_str = "down";
    int vk = 0, mods = 0;
    const char *key = "";
    const char *code = "";

    if (isdigit((unsigned char)tok1[0]) && count >= 7) {
        tab_id = atoi(tok1);
        type_str = tok2;
        key = tok3;
        code = tok4;
        vk = atoi(tok5);
        mods = atoi(tok6);
    } else {
        type_str = tok1;
        vk = atoi(tok2);
        mods = atoi(tok3);
        key = tok4;
        code = tok5;
    }

    browser_tab *tab = get_tab(tab_id);
    if (!tab || !tab->attached) return;

    const char *cdp_type = (strcmp(type_str, "up") == 0) ? "keyUp" : "rawKeyDown";
    char esc_key[128], esc_code[128];
    json_escape_string(key, esc_key, sizeof(esc_key));
    json_escape_string(code, esc_code, sizeof(esc_code));

    char p[512];
    snprintf(p, sizeof(p),
             "{\"type\":\"%s\",\"windowsVirtualKeyCode\":%d,\"nativeVirtualKeyCode\":%d,"
             "\"modifiers\":%d,\"key\":\"%s\",\"code\":\"%s\"}",
             cdp_type, vk, vk, mods, esc_key, esc_code);
    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Input.dispatchKeyEvent", p);

    schedule_screenshot(40);
}

static void handle_text(int cmd_w, const char *line) {
    const char *p = line + 4;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return;

    int tab_id = g_active_tab_id;
    if (isdigit((unsigned char)*p)) {
        char *end = NULL;
        long tid = strtol(p, &end, 10);
        if (end && *end == ' ') {
            tab_id = (int)tid;
            p = end + 1;
            while (*p == ' ' || *p == '\t') p++;
        }
    }

    browser_tab *tab = get_tab(tab_id);
    if (!tab || !tab->attached) return;

    char esc_text[FINN_BROWSER_LINE_MAX * 2];
    json_escape_string(p, esc_text, sizeof(esc_text));

    char params[FINN_BROWSER_LINE_MAX * 2 + 32];
    snprintf(params, sizeof(params), "{\"text\":\"%s\"}", esc_text);
    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Input.insertText", params);

    schedule_screenshot(40);
}

static void handle_find(int cmd_w, const char *line) {
    char tok0[32] = {0}, tok1[32] = {0}, tok2[32] = {0}, tok3[32] = {0};
    int count = sscanf(line, "%31s %31s %31s %31s", tok0, tok1, tok2, tok3);
    if (count < 3) return;

    int tab_id = atoi(tok1);
    browser_tab *tab = get_tab(tab_id ? tab_id : g_active_tab_id);
    if (!tab || !tab->attached) return;

    int forward_val = 1;
    if (strcmp(tok2, "0") == 0 || strcasecmp(tok2, "backward") == 0) {
        forward_val = 0;
    }

    int reset_val = 0;
    const char *query = NULL;
    if (count >= 4 && (strcmp(tok3, "0") == 0 || strcmp(tok3, "1") == 0)) {
        reset_val = atoi(tok3);
        const char *p = strstr(line, tok3);
        if (p) {
            p += strlen(tok3);
            while (*p == ' ' || *p == '\t') p++;
            query = p;
        }
    } else {
        const char *p = strstr(line, tok2);
        if (p) {
            p += strlen(tok2);
            while (*p == ' ' || *p == '\t') p++;
            query = p;
        }
    }

    if (!query) query = "";

    char esc_q[FINN_BROWSER_FIND_MAX * 2];
    json_escape_string(query, esc_q, sizeof(esc_q));

    char js[2048];
    snprintf(js, sizeof(js),
        "(()=>{"
        "var q=\"%s\",fwd=%d,reset=%d;"
        "if(!q||!q.length){if(window.__finnFindState)delete window.__finnFindState;return \"0 0\";}"
        "var text=(document.body&&document.body.innerText)?document.body.innerText:\"\";"
        "var total=0,idx=0,lq=q.toLowerCase(),lt=text.toLowerCase();"
        "while((idx=lt.indexOf(lq,idx))!==-1){total++;idx+=lq.length;}"
        "if(total===0){if(window.__finnFindState)delete window.__finnFindState;return \"0 0\";}"
        "if(!window.__finnFindState||window.__finnFindState.query!==q||reset){"
        "window.__finnFindState={query:q,active:0,total:total};"
        "window.getSelection().removeAllRanges();"
        "}"
        "var found=window.find(q,false,!fwd,true,false,true,false);"
        "if(found){"
        "if(fwd){window.__finnFindState.active++;if(window.__finnFindState.active>total)window.__finnFindState.active=1;}"
        "else{window.__finnFindState.active--;if(window.__finnFindState.active<1)window.__finnFindState.active=total;}"
        "}else{window.__finnFindState.active=0;}"
        "return window.__finnFindState.active+\" \"+total;"
        "})()",
        esc_q, forward_val, reset_val);

    char esc_js[4096];
    json_escape_string(js, esc_js, sizeof(esc_js));
    char p[4096];
    snprintf(p, sizeof(p), "{\"expression\":\"%s\",\"returnByValue\":true}", esc_js);
    cdp_exec(cmd_w, CDP_CMD_RUNTIME_EVAL_FIND, tab->id, tab->session_id, "Runtime.evaluate", p);
}

static void handle_findend(int cmd_w, int sock_fd, const char *line) {
    int tab_id = g_active_tab_id;
    (void)sscanf(line, "FINDEND %d", &tab_id);
    browser_tab *tab = get_tab(tab_id ? tab_id : g_active_tab_id);
    if (!tab || !tab->attached) return;

    cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Runtime.evaluate",
             "{\"expression\":\"window.getSelection().removeAllRanges();"
             "if(window.__finnFindState)delete window.__finnFindState;\"}");

    char out[64];
    snprintf(out, sizeof(out), "FOUND %d 0 0\n", tab->id);
    finn_write_all(sock_fd, out, strlen(out));
    schedule_screenshot(0);
}

static void handle_compositor_cmd(int cmd_w, int sock_fd, const char *line) {
    if (!line || !*line) return;

    if (strncmp(line, "VIEWPORT ", 9) == 0) {
        int w = 0, h = 0;
        if (sscanf(line + 9, "%d %d", &w, &h) >= 2 && w > 0 && h > 0) {
            g_vp_w = w;
            g_vp_h = h;
            g_viewport_received = 1;
            for (int i = 0; i < FINN_BROWSER_MAX_TABS; i++) {
                if (g_tabs[i].used && g_tabs[i].attached) {
                    char m[128];
                    snprintf(m, sizeof(m),
                             "{\"width\":%d,\"height\":%d,\"deviceScaleFactor\":1,\"mobile\":false}",
                             g_vp_w, g_vp_h);
                    cdp_exec(cmd_w, CDP_CMD_OTHER, g_tabs[i].id, g_tabs[i].session_id,
                             "Emulation.setDeviceMetricsOverride", m);
                }
            }
            schedule_screenshot(0);
        }
    } else if (strncmp(line, "NEW", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
        const char *u = line + 3;
        while (*u == ' ') u++;
        create_tab(cmd_w, sock_fd, u);
    } else if (strncmp(line, "CLOSE ", 6) == 0) {
        int tid = 0;
        if (sscanf(line + 6, "%d", &tid) == 1) {
            close_tab(cmd_w, sock_fd, tid);
        }
    } else if (strncmp(line, "ACTIVATE ", 9) == 0) {
        int tid = 0;
        if (sscanf(line + 9, "%d", &tid) == 1) {
            browser_tab *tab = get_tab(tid);
            if (tab) {
                g_active_tab_id = tid;
                char act[32];
                snprintf(act, sizeof(act), "ACTIVE %d\n", tid);
                finn_write_str(sock_fd, act);
                send_tab_state(sock_fd, tab, 1);
                schedule_screenshot(0);
            }
        }
    } else if (strncmp(line, "NAVIGATE ", 9) == 0) {
        int tid = 0;
        char url[FINN_BROWSER_URL_MAX] = {0};
        const char *p = line + 9;
        if (isdigit((unsigned char)*p)) {
            tid = atoi(p);
            p = strchr(p, ' ');
            if (p) {
                while (*p == ' ') p++;
                strncpy(url, p, sizeof(url) - 1);
            }
        } else {
            tid = g_active_tab_id;
            strncpy(url, p, sizeof(url) - 1);
        }
        browser_tab *tab = get_tab(tid ? tid : g_active_tab_id);
        if (tab && url[0]) {
            tab->loading = 1;
            strncpy(tab->url, url, sizeof(tab->url) - 1);
            send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
            if (tab->attached) {
                char esc[FINN_BROWSER_URL_MAX * 2];
                json_escape_string(url, esc, sizeof(esc));
                char pnav[FINN_BROWSER_URL_MAX * 2 + 32];
                snprintf(pnav, sizeof(pnav), "{\"url\":\"%s\"}", esc);
                cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Page.navigate", pnav);
            } else {
                strncpy(tab->pending_nav_url, url, sizeof(tab->pending_nav_url) - 1);
            }
            schedule_screenshot(40);
            schedule_followup(250);
        }
    } else if (strncmp(line, "BACK", 4) == 0) {
        int tid = 0;
        (void)sscanf(line, "BACK %d", &tid);
        handle_back(cmd_w, tid);
    } else if (strncmp(line, "FORWARD", 7) == 0) {
        int tid = 0;
        (void)sscanf(line, "FORWARD %d", &tid);
        handle_forward(cmd_w, tid);
    } else if (strncmp(line, "RELOAD", 6) == 0) {
        int tid = 0;
        (void)sscanf(line, "RELOAD %d", &tid);
        browser_tab *tab = get_tab(tid ? tid : g_active_tab_id);
        if (tab && tab->attached) {
            tab->loading = 1;
            send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
            cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id,
                     "Page.reload", "{\"ignoreCache\":false}");
            schedule_screenshot(40);
            schedule_followup(250);
        }
    } else if (strncmp(line, "STOP", 4) == 0) {
        int tid = 0;
        (void)sscanf(line, "STOP %d", &tid);
        browser_tab *tab = get_tab(tid ? tid : g_active_tab_id);
        if (tab && tab->attached) {
            tab->loading = 0;
            send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
            cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id, "Page.stopLoading", NULL);
        }
    } else if (strncmp(line, "MOUSE ", 6) == 0) {
        handle_mouse(cmd_w, line);
    } else if (strncmp(line, "WHEEL ", 6) == 0) {
        handle_wheel(cmd_w, line);
    } else if (strncmp(line, "KEY ", 4) == 0) {
        handle_key(cmd_w, line);
    } else if (strncmp(line, "TEXT ", 5) == 0) {
        handle_text(cmd_w, line);
    } else if (strncmp(line, "FIND ", 5) == 0) {
        handle_find(cmd_w, line);
    } else if (strncmp(line, "FINDEND", 7) == 0) {
        handle_findend(cmd_w, sock_fd, line);
    } else if (strcmp(line, "QUIT") == 0) {
        g_stop = 1;
    }
}

static void handle_cdp_message(int cmd_w, int sock_fd, const char *msg) {
    int id = 0;
    if (json_get_int(msg, "id", &id) == 0 && id > 0) {
        pending_cdp_cmd *cmd = &g_pending_ring[id % CDP_PENDING_MAX];
        if (cmd->cdp_id == id) {
            int kind = cmd->cmd_kind;
            int tab_id = cmd->tab_id;
            cmd->cmd_kind = CDP_CMD_NONE;

            if (kind == CDP_CMD_CREATE_TARGET) {
                char target_id[128];
                if (json_get_string(msg, "targetId", target_id, sizeof(target_id)) == 0) {
                    browser_tab *tab = get_tab(tab_id);
                    if (tab) {
                        strncpy(tab->target_id, target_id, sizeof(tab->target_id) - 1);
                        char params[256];
                        snprintf(params, sizeof(params),
                                 "{\"targetId\":\"%s\",\"flatten\":true}", target_id);
                        cdp_exec(cmd_w, CDP_CMD_ATTACH_TARGET, tab->id, NULL,
                                 "Target.attachToTarget", params);
                    }
                }
            } else if (kind == CDP_CMD_ATTACH_TARGET) {
                char session_id[128];
                if (json_get_string(msg, "sessionId", session_id, sizeof(session_id)) == 0) {
                    browser_tab *tab = get_tab(tab_id);
                    if (tab) {
                        on_tab_attached(cmd_w, sock_fd, tab, session_id);
                    }
                }
            } else if (kind == CDP_CMD_NAV_HISTORY) {
                browser_tab *tab = get_tab(tab_id);
                if (tab) {
                    parse_nav_history_response(sock_fd, tab, msg);
                }
            } else if (kind == CDP_CMD_CAPTURE_SCREENSHOT) {
                g_screenshot_in_flight = 0;
                size_t data_len = 0;
                const char *b64 = json_get_data_slice(msg, &data_len);
                if (b64 && data_len > 0) {
                    size_t raw_len = 0;
                    uint8_t *raw = base64_decode(b64, data_len, &raw_len);
                    if (raw) {
                        decode_and_send_frame(sock_fd, tab_id, raw, raw_len);
                        free(raw);
                    }
                }
            } else if (kind == CDP_CMD_RUNTIME_EVAL_FIND) {
                browser_tab *tab = get_tab(tab_id);
                if (tab) {
                    char val[128];
                    if (json_get_string(msg, "value", val, sizeof(val)) == 0) {
                        int active = 0, total = 0;
                        if (sscanf(val, "%d %d", &active, &total) == 2) {
                            char line[64];
                            snprintf(line, sizeof(line), "FOUND %d %d %d\n",
                                     tab->id, active, total);
                            finn_write_all(sock_fd, line, strlen(line));
                            schedule_screenshot(0);
                        }
                    }
                }
            }
        }
        return;
    }

    char method[128];
    if (json_get_string(msg, "method", method, sizeof(method)) == 0) {
        char session_id[128];
        json_get_string(msg, "sessionId", session_id, sizeof(session_id));
        browser_tab *tab = session_id[0] ? find_tab_by_session(session_id) : NULL;

        if (strcmp(method, "Target.attachedToTarget") == 0) {
            char target_id[128];
            json_get_string(msg, "targetId", target_id, sizeof(target_id));
            char sess[128];
            json_get_string(msg, "sessionId", sess, sizeof(sess));
            if (!tab && target_id[0]) {
                tab = find_tab_by_target(target_id);
            }
            if (tab && sess[0]) {
                on_tab_attached(cmd_w, sock_fd, tab, sess);
            }
        } else if (strcmp(method, "Page.loadEventFired") == 0) {
            if (tab) {
                tab->loading = 0;
                send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
                cdp_exec(cmd_w, CDP_CMD_NAV_HISTORY, tab->id, tab->session_id,
                         "Page.getNavigationHistory", NULL);
                schedule_screenshot(40);
                schedule_followup(250);
            }
        } else if (strcmp(method, "Page.frameStoppedLoading") == 0) {
            if (tab) {
                tab->loading = 0;
                send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
                schedule_screenshot(40);
            }
        } else if (strcmp(method, "Page.domContentEventFired") == 0) {
            schedule_screenshot(40);
        } else if (strcmp(method, "Page.frameNavigated") == 0 ||
                   strcmp(method, "Page.navigatedWithinDocument") == 0) {
            if (tab) {
                char furl[FINN_BROWSER_URL_MAX];
                if (json_get_string(msg, "url", furl, sizeof(furl)) == 0 && furl[0]) {
                    strncpy(tab->url, furl, sizeof(tab->url) - 1);
                }
                tab->loading = (strcmp(method, "Page.frameNavigated") == 0);
                send_tab_state(sock_fd, tab, (tab->id == g_active_tab_id));
                cdp_exec(cmd_w, CDP_CMD_NAV_HISTORY, tab->id, tab->session_id,
                         "Page.getNavigationHistory", NULL);
                schedule_screenshot(40);
            }
        } else if (strcmp(method, "Page.screencastFrame") == 0) {
            size_t data_len = 0;
            const char *b64 = json_get_data_slice(msg, &data_len);
            if (tab && b64 && data_len > 0) {
                size_t raw_len = 0;
                uint8_t *raw = base64_decode(b64, data_len, &raw_len);
                if (raw) {
                    decode_and_send_frame(sock_fd, tab->id, raw, raw_len);
                    free(raw);
                }
            }
            int sc_sess_id = 0;
            if (json_get_int(msg, "sessionId", &sc_sess_id) == 0 && tab) {
                char ack_params[64];
                snprintf(ack_params, sizeof(ack_params), "{\"sessionId\":%d}", sc_sess_id);
                cdp_exec(cmd_w, CDP_CMD_OTHER, tab->id, tab->session_id,
                         "Page.screencastFrameAck", ack_params);
            }
        } else if (strcmp(method, "Inspector.targetCrashed") == 0) {
            if (tab) {
                finn_write_str(sock_fd, "ERROR 0 Page crashed\n");
            }
        }
    }
}

static enum engine_type discover_engine(char *out_path, size_t out_path_cap) {
    if (access("/usr/bin/chromium", X_OK) == 0) {
        snprintf(out_path, out_path_cap, "/usr/bin/chromium");
        return ENGINE_BIN;
    }
    if (access("/usr/lib/chromium/chromium", X_OK) == 0) {
        snprintf(out_path, out_path_cap, "/usr/lib/chromium/chromium");
        return ENGINE_BIN;
    }
    if (access("/usr/bin/flatpak", X_OK) == 0) {
        if (access("/var/lib/flatpak/app/org.chromium.Chromium/current/active/files/bin/chromium", F_OK) == 0) {
            return ENGINE_FLATPAK;
        }
        int r = system("/usr/bin/flatpak info org.chromium.Chromium >/dev/null 2>&1");
        if (r == 0) {
            return ENGINE_FLATPAK;
        }
    }
    return ENGINE_NONE;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    /* Launch the compositor window if not already active */
    char open_reply[FINN_MAX_LINE];
    (void)finn_call(FINN_CONTROL_SOCK, "window.open app=browser", open_reply, sizeof(open_reply));

    /* Connect to FINN_BROWSER_SOCK with retries as compositor binds the socket */
    int sock_fd = -1;
    for (int i = 0; i < 30; i++) {
        sock_fd = finn_connect(FINN_BROWSER_SOCK);
        if (sock_fd >= 0) break;
        struct timespec req = { 0, 50000000L };
        nanosleep(&req, NULL);
    }
    if (sock_fd < 0) {
        fprintf(stderr, "finn-browser: failed to connect to %s: %s\n",
                FINN_BROWSER_SOCK, strerror(errno));
        return 1;
    }

    struct timeval tv = { 2, 0 };
    setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    finn_write_str(sock_fd, "HELLO 1\n");

    char initial_url[FINN_BROWSER_URL_MAX] = "https://example.com/";
    if (argc > 1 && argv[1][0] != '-') {
        finn_browser_text_clean(initial_url, sizeof(initial_url), argv[1]);
    }

    char engine_bin[PATH_MAX] = {0};
    enum engine_type engine = discover_engine(engine_bin, sizeof(engine_bin));
    if (engine == ENGINE_NONE) {
        finn_write_str(sock_fd, "UNAVAILABLE Chromium engine is not installed\n");
        struct pollfd pfd = { .fd = sock_fd, .events = POLLIN };
        while (!g_stop) {
            int pr = poll(&pfd, 1, 500);
            if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
                char buf[256];
                int n = finn_read_line(sock_fd, buf, sizeof(buf));
                if (n <= 0) break;
                if (strcmp(buf, "QUIT") == 0) break;
            }
        }
        close(sock_fd);
        return 0;
    }

    /* Ensure descriptors 0..4 are open so pipe() descriptors land on >= 5 */
    int opened_placeholders[5] = {0};
    for (int fd = 0; fd <= 4; fd++) {
        if (fcntl(fd, F_GETFD) < 0 && errno == EBADF) {
            int null_fd = open("/dev/null", O_RDWR);
            if (null_fd >= 0) {
                if (null_fd != fd) {
                    dup2(null_fd, fd);
                    close(null_fd);
                }
                opened_placeholders[fd] = 1;
            }
        }
    }

    int cmd_pipe[2], resp_pipe[2];
    if (pipe(cmd_pipe) < 0 || pipe(resp_pipe) < 0) {
        perror("pipe");
        close(sock_fd);
        return 1;
    }

    snprintf(g_profile_dir, sizeof(g_profile_dir),
             "/tmp/finn-browser-profile-%u", (unsigned)getuid());
    mkdir(g_profile_dir, 0700);

    g_child_pid = fork();
    if (g_child_pid < 0) {
        perror("fork");
        cleanup_profile_dir(g_profile_dir);
        close(sock_fd);
        return 1;
    }

    if (g_child_pid == 0) {
        setpgid(0, 0);
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
        dup2(cmd_pipe[0], 3);
        dup2(resp_pipe[1], 4);
        fcntl(3, F_SETFD, 0);
        fcntl(4, F_SETFD, 0);

        int null_in = open("/dev/null", O_RDONLY);
        int null_out = open("/dev/null", O_WRONLY);
        if (null_in >= 0) {
            dup2(null_in, 0);
            if (null_in > 4) close(null_in);
        }
        if (null_out >= 0) {
            dup2(null_out, 1);
            dup2(null_out, 2);
            if (null_out > 4) close(null_out);
        }

        long max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0 || max_fd > 65536) max_fd = 1024;
        for (int fd = 5; fd < max_fd; fd++) {
            close(fd);
        }

        char user_data_arg[256];
        snprintf(user_data_arg, sizeof(user_data_arg), "--user-data-dir=%s", g_profile_dir);

        if (engine == ENGINE_BIN) {
            char *argv_bin[] = {
                (char *)engine_bin,
                "--headless=new",
                "--ozone-platform=headless",
                "--disable-gpu",
                "--no-sandbox",
                "--disable-dev-shm-usage",
                "--disable-background-networking",
                "--disable-sync",
                "--disable-translate",
                "--disable-extensions",
                "--no-first-run",
                "--no-default-browser-check",
                "--hide-scrollbars",
                "--remote-debugging-pipe",
                user_data_arg,
                "--window-size=1000,700",
                "about:blank",
                NULL
            };
            execv(engine_bin, argv_bin);
        } else {
            char *argv_flatpak[] = {
                "/usr/bin/flatpak",
                "run",
                "--filesystem=/tmp",
                "--command=chromium",
                "org.chromium.Chromium",
                "--headless=new",
                "--ozone-platform=headless",
                "--disable-gpu",
                "--no-sandbox",
                "--disable-dev-shm-usage",
                "--disable-background-networking",
                "--disable-sync",
                "--disable-translate",
                "--disable-extensions",
                "--no-first-run",
                "--no-default-browser-check",
                "--hide-scrollbars",
                "--remote-debugging-pipe",
                user_data_arg,
                "--window-size=1000,700",
                "about:blank",
                NULL
            };
            execv("/usr/bin/flatpak", argv_flatpak);
        }
        _exit(127);
    }

    close(cmd_pipe[0]);
    close(resp_pipe[1]);
    if (opened_placeholders[3]) close(3);
    if (opened_placeholders[4]) close(4);

    int cmd_w = cmd_pipe[1];
    int resp_r = resp_pipe[0];

    int fl = fcntl(resp_r, F_GETFL, 0);
    fcntl(resp_r, F_SETFL, fl | O_NONBLOCK);

    char *cdp_rx_buf = malloc(CDP_BUF_INIT);
    size_t cdp_rx_cap = CDP_BUF_INIT;
    size_t cdp_rx_len = 0;

    char rx_sock_buf[FINN_BROWSER_LINE_MAX * 2];
    size_t rx_sock_len = 0;

    int64_t startup_ms = get_monotonic_ms();
    int startup_tab_created = 0;

    struct pollfd pfds[2];
    pfds[0].fd = sock_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd = resp_r;
    pfds[1].events = POLLIN;

    while (!g_stop) {
        int64_t now = get_monotonic_ms();
        int timeout_ms = 1000;

        if (!startup_tab_created) {
            int64_t diff = 150 - (now - startup_ms);
            if (diff <= 0 || g_viewport_received) {
                startup_tab_created = 1;
                create_tab(cmd_w, sock_fd, initial_url);
                g_active_tab_id = 1;
            } else if (diff < timeout_ms) {
                timeout_ms = (int)diff;
            }
        }

        if (g_screenshot_due_ms > 0) {
            int64_t diff = g_screenshot_due_ms - now;
            if (diff <= 0) {
                timeout_ms = 0;
            } else if (diff < timeout_ms) {
                timeout_ms = (int)diff;
            }
        }

        if (g_followup_due_ms > 0) {
            int64_t diff = g_followup_due_ms - now;
            if (diff <= 0) {
                timeout_ms = 0;
            } else if (diff < timeout_ms) {
                timeout_ms = (int)diff;
            }
        }

        int pr = poll(pfds, 2, timeout_ms);
        if (pr < 0 && errno == EINTR) continue;
        if (pr < 0) break;

        now = get_monotonic_ms();

        if (!startup_tab_created && (g_viewport_received || now - startup_ms >= 150)) {
            startup_tab_created = 1;
            create_tab(cmd_w, sock_fd, initial_url);
            g_active_tab_id = 1;
        }

        if (g_screenshot_due_ms > 0 && now >= g_screenshot_due_ms) {
            if (!g_screenshot_in_flight) {
                browser_tab *active = get_active_tab();
                if (active && active->attached) {
                    request_screenshot(cmd_w, active);
                    g_screenshot_in_flight = 1;
                    g_screenshot_due_ms = 0;
                }
            }
        }

        if (g_followup_due_ms > 0 && now >= g_followup_due_ms) {
            if (!g_screenshot_in_flight) {
                browser_tab *active = get_active_tab();
                if (active && active->attached) {
                    request_screenshot(cmd_w, active);
                    g_screenshot_in_flight = 1;
                }
            }
            g_followup_due_ms = 0;
        }

        if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(sock_fd, rx_sock_buf + rx_sock_len,
                             sizeof(rx_sock_buf) - 1 - rx_sock_len);
            if (n > 0) {
                rx_sock_len += (size_t)n;
                rx_sock_buf[rx_sock_len] = '\0';
                size_t start = 0;
                for (size_t i = 0; i < rx_sock_len; i++) {
                    if (rx_sock_buf[i] == '\n') {
                        rx_sock_buf[i] = '\0';
                        if (i > start && rx_sock_buf[i - 1] == '\r') {
                            rx_sock_buf[i - 1] = '\0';
                        }
                        handle_compositor_cmd(cmd_w, sock_fd, rx_sock_buf + start);
                        start = i + 1;
                    }
                }
                if (start > 0) {
                    memmove(rx_sock_buf, rx_sock_buf + start, rx_sock_len - start);
                    rx_sock_len -= start;
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                g_stop = 1;
            }
        }

        if (pfds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (cdp_rx_len + 65536 > cdp_rx_cap) {
                size_t new_cap = cdp_rx_cap * 2;
                if (new_cap > CDP_BUF_MAX) new_cap = CDP_BUF_MAX;
                if (cdp_rx_len + 65536 <= new_cap) {
                    char *nb = realloc(cdp_rx_buf, new_cap);
                    if (nb) {
                        cdp_rx_buf = nb;
                        cdp_rx_cap = new_cap;
                    }
                }
            }
            if (cdp_rx_cap > cdp_rx_len) {
                ssize_t n = read(resp_r, cdp_rx_buf + cdp_rx_len, cdp_rx_cap - cdp_rx_len);
                if (n > 0) {
                    cdp_rx_len += (size_t)n;
                    size_t start = 0;
                    for (size_t i = 0; i < cdp_rx_len; i++) {
                        if (cdp_rx_buf[i] == '\0') {
                            const char *msg = cdp_rx_buf + start;
                            handle_cdp_message(cmd_w, sock_fd, msg);
                            start = i + 1;
                        }
                    }
                    if (start > 0) {
                        memmove(cdp_rx_buf, cdp_rx_buf + start, cdp_rx_len - start);
                        cdp_rx_len -= start;
                    }
                } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    g_stop = 1;
                }
            }
        }

        if (g_child_pid > 0) {
            int status = 0;
            pid_t p = waitpid(g_child_pid, &status, WNOHANG);
            if (p == g_child_pid) {
                finn_write_str(sock_fd, "BYE Chromium exited\n");
                g_child_pid = -1;
                g_stop = 1;
            }
        }
    }

    if (g_child_pid > 0) {
        kill(-g_child_pid, SIGTERM);
        kill(g_child_pid, SIGTERM);
        int status;
        for (int i = 0; i < 20; i++) {
            pid_t p = waitpid(g_child_pid, &status, WNOHANG);
            if (p == g_child_pid || (p < 0 && errno == ECHILD)) break;
            struct timespec ts = { 0, 20000000L };
            nanosleep(&ts, NULL);
        }
        kill(-g_child_pid, SIGKILL);
        kill(g_child_pid, SIGKILL);
        waitpid(g_child_pid, NULL, WNOHANG);
    }

    free(cdp_rx_buf);
    close(cmd_w);
    close(resp_r);
    close(sock_fd);

    cleanup_profile_dir(g_profile_dir);
    return 0;
}
