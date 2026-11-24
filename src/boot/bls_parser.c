#include "bls_parser.h"

static void str_copy(char *dst, size_t cap, const char *src, size_t src_len) {
    if (cap == 0) return;
    size_t n = src_len < cap - 1 ? src_len : cap - 1;
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
    dst[n] = '\0';
}

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static char to_lower_ch(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (to_lower_ch(*a) != to_lower_ch(*b)) return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int str_contains_ci(const char *hay, const char *needle) {
    size_t nlen = str_len(needle);
    if (nlen == 0) return 1;
    size_t hlen = str_len(hay);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i + nlen <= hlen; ++i) {
        size_t j = 0;
        while (j < nlen && to_lower_ch(hay[i + j]) == to_lower_ch(needle[j])) ++j;
        if (j == nlen) return 1;
    }
    return 0;
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void mem_zero(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; ++i) b[i] = 0;
}

static int parse_nonneg_int(const char *s, size_t len, int *out) {
    if (len == 0) return 0;
    int v = 0;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 1;
}

void bls_config_init(BootLoaderConfig *cfg) {
    mem_zero(cfg, sizeof(*cfg));
    cfg->timeout_sec = 3;
    cfg->editor_allowed = 1;
    cfg->auto_entries = 1;
    cfg->auto_firmware = 1;
    cfg->console_text_only = 0;
}

void bls_parse_loader_conf(const char *text, size_t len, BootLoaderConfig *cfg) {
    size_t i = 0;
    while (i < len) {
        while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
        size_t line_start = i;
        while (i < len && text[i] != '\n') ++i;
        size_t line_end = i;
        while (line_end > line_start && (text[line_end - 1] == '\r' || text[line_end - 1] == ' ' || text[line_end - 1] == '\t')) {
            --line_end;
        }
        if (line_start >= line_end || text[line_start] == '#') continue;

        size_t k_end = line_start;
        while (k_end < line_end && text[k_end] != ' ' && text[k_end] != '\t') ++k_end;
        size_t v_start = k_end;
        while (v_start < line_end && (text[v_start] == ' ' || text[v_start] == '\t')) ++v_start;

        char key[32];
        char val[FINN_BOOT_MAX_STR];
        str_copy(key, sizeof(key), text + line_start, k_end - line_start);
        str_copy(val, sizeof(val), text + v_start, line_end - v_start);

        if (str_eq_ci(key, "default")) {
            str_copy(cfg->default_pattern, sizeof(cfg->default_pattern), val, str_len(val));
        } else if (str_eq_ci(key, "timeout")) {
            int sec = 0;
            if (str_eq_ci(val, "menu-force")) {
                cfg->timeout_sec = -1;
            } else if (str_eq_ci(val, "menu-hidden")) {
                cfg->timeout_sec = 0;
            } else if (parse_nonneg_int(val, str_len(val), &sec)) {
                cfg->timeout_sec = sec;
            }
        } else if (str_eq_ci(key, "editor")) {
            cfg->editor_allowed = !(str_eq_ci(val, "no") || str_eq_ci(val, "0") || str_eq_ci(val, "false"));
        } else if (str_eq_ci(key, "auto-entries")) {
            cfg->auto_entries = !(str_eq_ci(val, "no") || str_eq_ci(val, "0") || str_eq_ci(val, "false"));
        } else if (str_eq_ci(key, "auto-firmware")) {
            cfg->auto_firmware = !(str_eq_ci(val, "no") || str_eq_ci(val, "0") || str_eq_ci(val, "false"));
        } else if (str_eq_ci(key, "console-mode")) {
            if (str_eq_ci(val, "text") || str_eq_ci(val, "0")) {
                cfg->console_text_only = 1;
            }
        }
    }
}

int bls_parse_boot_count(const char *filename, int *tries_left, int *tries_done) {
    size_t len = str_len(filename);
    if (len < 7) return 0;
    if (!str_eq_ci(filename + len - 5, ".conf")) return 0;
    size_t stem_end = len - 5;
    size_t plus = stem_end;
    for (size_t i = stem_end; i > 0; --i) {
        if (filename[i - 1] == '+') { plus = i - 1; break; }
    }
    if (plus >= stem_end) return 0;

    size_t dash = stem_end;
    for (size_t i = plus + 1; i < stem_end; ++i) {
        if (filename[i] == '-') { dash = i; break; }
    }
    int left = 0, done = 0;
    if (!parse_nonneg_int(filename + plus + 1, dash - (plus + 1), &left)) return 0;
    if (dash < stem_end) {
        if (!parse_nonneg_int(filename + dash + 1, stem_end - (dash + 1), &done)) return 0;
    }
    if (tries_left) *tries_left = left;
    if (tries_done) *tries_done = done;
    return 1;
}

int bls_format_decremented_filename(const char *filename, char *out, size_t out_cap) {
    int left = 0, done = 0;
    if (!bls_parse_boot_count(filename, &left, &done)) return 0;
    if (left <= 0) return 0;
    size_t len = str_len(filename);
    size_t stem_end = len - 5;
    size_t plus = stem_end;
    for (size_t i = stem_end; i > 0; --i) {
        if (filename[i - 1] == '+') { plus = i - 1; break; }
    }
    left -= 1;
    done += 1;

    size_t pos = 0;
    if (plus + 20 >= out_cap) return 0;
    for (size_t i = 0; i < plus; ++i) out[pos++] = filename[i];
    out[pos++] = '+';

    char tmp[16];
    int n = 0;
    int v = left;
    if (v == 0) tmp[n++] = '0';
    else { while (v > 0 && n < 15) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } }
    for (int k = n - 1; k >= 0; --k) out[pos++] = tmp[k];
    out[pos++] = '-';
    n = 0;
    v = done;
    if (v == 0) tmp[n++] = '0';
    else { while (v > 0 && n < 15) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } }
    for (int k = n - 1; k >= 0; --k) out[pos++] = tmp[k];

    const char *ext = ".conf";
    for (int k = 0; k < 5 && pos + 1 < out_cap; ++k) out[pos++] = ext[k];
    out[pos] = '\0';
    return 1;
}

BootOsIcon bls_classify_icon(const BootEntry *e) {
    if (e->kind == ENTRY_KIND_FIRMWARE_SETUP) return OS_ICON_FIRMWARE;
    if (str_contains_ci(e->title, "recovery") || str_contains_ci(e->id, "recovery") ||
        str_contains_ci(e->options, "finn.recovery") || str_contains_ci(e->options, "finn.install")) {
        return OS_ICON_RECOVERY;
    }
    if (str_contains_ci(e->title, "windows") || str_contains_ci(e->efi_path, "microsoft") ||
        str_contains_ci(e->efi_path, "bootmgfw")) {
        return OS_ICON_WINDOWS;
    }
    if (str_contains_ci(e->title, "macos") || str_contains_ci(e->title, "mac os") ||
        str_contains_ci(e->efi_path, "apple") || str_contains_ci(e->efi_path, "coreServices")) {
        return OS_ICON_MACOS;
    }
    if (str_contains_ci(e->title, "finnos") || str_contains_ci(e->id, "finnos") ||
        str_contains_ci(e->sort_key, "finnos")) {
        return OS_ICON_FINNOS;
    }
    return OS_ICON_LINUX;
}

int bls_parse_entry_conf(const char *id, const char *text, size_t len, BootEntry *out) {
    mem_zero(out, sizeof(*out));
    str_copy(out->id, sizeof(out->id), id, str_len(id));
    out->has_boot_count = bls_parse_boot_count(id, &out->tries_left, &out->tries_done);

    size_t i = 0;
    while (i < len) {
        while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
        size_t line_start = i;
        while (i < len && text[i] != '\n') ++i;
        size_t line_end = i;
        while (line_end > line_start && (text[line_end - 1] == '\r' || text[line_end - 1] == ' ' || text[line_end - 1] == '\t')) {
            --line_end;
        }
        if (line_start >= line_end || text[line_start] == '#') continue;

        size_t k_end = line_start;
        while (k_end < line_end && text[k_end] != ' ' && text[k_end] != '\t') ++k_end;
        size_t v_start = k_end;
        while (v_start < line_end && (text[v_start] == ' ' || text[v_start] == '\t')) ++v_start;

        char key[32];
        str_copy(key, sizeof(key), text + line_start, k_end - line_start);
        const char *val = text + v_start;
        size_t vlen = line_end - v_start;

        if (str_eq_ci(key, "title")) {
            str_copy(out->title, sizeof(out->title), val, vlen);
        } else if (str_eq_ci(key, "version")) {
            str_copy(out->version, sizeof(out->version), val, vlen);
        } else if (str_eq_ci(key, "sort-key")) {
            str_copy(out->sort_key, sizeof(out->sort_key), val, vlen);
        } else if (str_eq_ci(key, "linux")) {
            str_copy(out->linux_path, sizeof(out->linux_path), val, vlen);
            out->kind = ENTRY_KIND_BLS_LINUX;
        } else if (str_eq_ci(key, "efi")) {
            str_copy(out->efi_path, sizeof(out->efi_path), val, vlen);
            out->kind = ENTRY_KIND_EFI_CHAINLOAD;
        } else if (str_eq_ci(key, "initrd")) {
            if (out->initrd_count < FINN_BOOT_MAX_INITRDS) {
                str_copy(out->initrd_paths[out->initrd_count], FINN_BOOT_MAX_STR, val, vlen);
                out->initrd_count++;
            }
        } else if (str_eq_ci(key, "options")) {
            size_t cur = str_len(out->options);
            if (cur > 0 && cur + 1 < FINN_BOOT_MAX_OPTS) {
                out->options[cur++] = ' ';
            }
            if (cur < FINN_BOOT_MAX_OPTS - 1) {
                str_copy(out->options + cur, FINN_BOOT_MAX_OPTS - cur, val, vlen);
            }
        }
    }

    if (out->linux_path[0] == '\0' && out->efi_path[0] == '\0') return 0;
    if (out->title[0] == '\0') {
        str_copy(out->title, sizeof(out->title), id, str_len(id));
    }
    out->icon = bls_classify_icon(out);
    return 1;
}

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void parse_osrel_section(const char *txt, size_t len, BootEntry *out) {
    size_t i = 0;
    while (i < len) {
        size_t s = i;
        while (i < len && txt[i] != '\n' && txt[i] != '\0') ++i;
        size_t e = i;
        if (i < len && txt[i] == '\n') ++i;
        while (e > s && (txt[e - 1] == '\r' || txt[e - 1] == ' ')) --e;
        if (e <= s) continue;

        size_t eq = s;
        while (eq < e && txt[eq] != '=') ++eq;
        if (eq >= e) continue;
        size_t vs = eq + 1;
        size_t ve = e;
        if (ve > vs + 1 && txt[vs] == '"' && txt[ve - 1] == '"') { ++vs; --ve; }

        char key[32];
        str_copy(key, sizeof(key), txt + s, eq - s);
        if (str_eq_ci(key, "PRETTY_NAME")) {
            str_copy(out->title, sizeof(out->title), txt + vs, ve - vs);
        } else if (str_eq_ci(key, "VERSION_ID")) {
            str_copy(out->version, sizeof(out->version), txt + vs, ve - vs);
        } else if (str_eq_ci(key, "ID")) {
            str_copy(out->sort_key, sizeof(out->sort_key), txt + vs, ve - vs);
        }
    }
}

int bls_parse_uki_pe(const char *id, const char *efi_path,
                     const uint8_t *pe_buf, size_t pe_len, BootEntry *out) {
    if (!pe_buf || pe_len < 0x80 || pe_buf[0] != 'M' || pe_buf[1] != 'Z') return 0;
    uint32_t pe_off = rd_u32(pe_buf + 0x3C);
    if (pe_off + 24 > pe_len) return 0;
    if (pe_buf[pe_off] != 'P' || pe_buf[pe_off + 1] != 'E' ||
        pe_buf[pe_off + 2] != 0 || pe_buf[pe_off + 3] != 0) return 0;

    uint16_t n_secs = rd_u16(pe_buf + pe_off + 6);
    uint16_t opt_sz = rd_u16(pe_buf + pe_off + 20);
    size_t sec_table = (size_t)pe_off + 24 + (size_t)opt_sz;
    if (sec_table + (size_t)n_secs * 40 > pe_len) return 0;

    mem_zero(out, sizeof(*out));
    str_copy(out->id, sizeof(out->id), id, str_len(id));
    str_copy(out->efi_path, sizeof(out->efi_path), efi_path, str_len(efi_path));
    out->kind = ENTRY_KIND_UKI;

    int found_osrel = 0;
    for (uint16_t s = 0; s < n_secs; ++s) {
        const uint8_t *sh = pe_buf + sec_table + (size_t)s * 40;
        char sname[9];
        for (int k = 0; k < 8; ++k) sname[k] = (char)sh[k];
        sname[8] = '\0';
        uint32_t vsize = rd_u32(sh + 8);
        uint32_t rsize = rd_u32(sh + 16);
        uint32_t roff  = rd_u32(sh + 20);
        uint32_t use_sz = (vsize > 0 && vsize < rsize) ? vsize : rsize;
        if ((size_t)roff + (size_t)use_sz > pe_len) continue;

        if (str_eq_ci(sname, ".osrel")) {
            parse_osrel_section((const char *)(pe_buf + roff), use_sz, out);
            found_osrel = 1;
        } else if (str_eq_ci(sname, ".cmdline")) {
            size_t clen = use_sz;
            while (clen > 0 && (pe_buf[roff + clen - 1] == 0 || pe_buf[roff + clen - 1] == '\n' || pe_buf[roff + clen - 1] == '\r')) {
                --clen;
            }
            str_copy(out->options, sizeof(out->options), (const char *)(pe_buf + roff), clen);
        }
    }

    if (out->title[0] == '\0') {
        str_copy(out->title, sizeof(out->title), id, str_len(id));
    }
    out->icon = bls_classify_icon(out);
    return found_osrel ? 1 : 1;
}

int bls_glob_match(const char *pattern, const char *text) {
    if (!pattern || !text) return 0;
    const char *star_p = NULL;
    const char *star_t = NULL;
    while (*text) {
        if (*pattern == '*') {
            star_p = pattern++;
            star_t = text;
        } else if (*pattern == '?' || to_lower_ch(*pattern) == to_lower_ch(*text)) {
            ++pattern;
            ++text;
        } else if (star_p) {
            pattern = star_p + 1;
            text = ++star_t;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

static int entry_rank(const BootEntry *e) {
    switch (e->icon) {
        case OS_ICON_FINNOS:   return 0;
        case OS_ICON_WINDOWS:  return 1;
        case OS_ICON_MACOS:    return 2;
        case OS_ICON_LINUX:    return 3;
        case OS_ICON_RECOVERY: return 4;
        case OS_ICON_FIRMWARE: return 5;
        default:               return 6;
    }
}

void bls_sort_entries(BootEntry *entries, int count) {
    for (int i = 1; i < count; ++i) {
        BootEntry key = entries[i];
        int j = i - 1;
        while (j >= 0) {
            int ra = entry_rank(&entries[j]);
            int rb = entry_rank(&key);
            int swap = 0;
            if (ra > rb) swap = 1;
            else if (ra == rb) {
                const char *ka = entries[j].sort_key[0] ? entries[j].sort_key : entries[j].id;
                const char *kb = key.sort_key[0] ? key.sort_key : key.id;
                if (str_cmp(ka, kb) > 0) swap = 1;
            }
            if (!swap) break;
            entries[j + 1] = entries[j];
            --j;
        }
        entries[j + 1] = key;
    }
}
