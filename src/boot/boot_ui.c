#include "boot_ui.h"

/* Compact clean 5x7 uppercase/lowercase/symbol bitmap font (ASCII 32..126),
 * scaled cleanly to 2x (10x14) or 3x (15x21) for crisp HiDPI UEFI GOP text. */
static const uint8_t kFont5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* '~' */
};

static inline uint32_t bgra_px(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16) | 0xFF000000U;
}

static inline void blend_px(uint32_t *bgra, int w, int h, int x, int y,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a == 0) return;
    if (a == 255) {
        bgra[y * w + x] = bgra_px(r, g, b);
        return;
    }
    uint32_t dst = bgra[y * w + x];
    uint32_t db = dst & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t ia = 255 - a;
    uint8_t or_ = (uint8_t)((r * a + dr * ia) / 255);
    uint8_t og_ = (uint8_t)((g * a + dg * ia) / 255);
    uint8_t ob_ = (uint8_t)((b * a + db * ia) / 255);
    bgra[y * w + x] = bgra_px(or_, og_, ob_);
}

static void fill_rect(uint32_t *bgra, int w, int h, int rx, int ry, int rw, int rh,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = rx + rw > w ? w : rx + rw;
    int y1 = ry + rh > h ? h : ry + rh;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            blend_px(bgra, w, h, x, y, r, g, b, a);
        }
    }
}

static void fill_round_rect(uint32_t *bgra, int w, int h,
                            int rx, int ry, int rw, int rh, int rad,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (rad * 2 > rw) rad = rw / 2;
    if (rad * 2 > rh) rad = rh / 2;
    int r2 = rad * rad;
    for (int y = ry; y < ry + rh; ++y) {
        if (y < 0 || y >= h) continue;
        for (int x = rx; x < rx + rw; ++x) {
            if (x < 0 || x >= w) continue;
            int dx = 0, dy = 0;
            if (x < rx + rad) dx = (rx + rad) - x;
            else if (x >= rx + rw - rad) dx = x - (rx + rw - rad - 1);
            if (y < ry + rad) dy = (ry + rad) - y;
            else if (y >= ry + rh - rad) dy = y - (ry + rh - rad - 1);
            if (dx * dx + dy * dy <= r2) {
                blend_px(bgra, w, h, x, y, r, g, b, a);
            }
        }
    }
}

static void stroke_round_rect(uint32_t *bgra, int w, int h,
                              int rx, int ry, int rw, int rh, int rad, int thick,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int outer_r2 = rad * rad;
    int inner_rad = rad > thick ? rad - thick : 0;
    int inner_r2 = inner_rad * inner_rad;
    for (int y = ry; y < ry + rh; ++y) {
        if (y < 0 || y >= h) continue;
        for (int x = rx; x < rx + rw; ++x) {
            if (x < 0 || x >= w) continue;
            int dx = 0, dy = 0;
            if (x < rx + rad) dx = (rx + rad) - x;
            else if (x >= rx + rw - rad) dx = x - (rx + rw - rad - 1);
            if (y < ry + rad) dy = (ry + rad) - y;
            else if (y >= ry + rh - rad) dy = y - (ry + rh - rad - 1);
            if (dx * dx + dy * dy > outer_r2) continue;

            int in_x = (x >= rx + thick && x < rx + rw - thick);
            int in_y = (y >= ry + thick && y < ry + rh - thick);
            if (in_x && in_y) {
                int idx = 0, idy = 0;
                if (x < rx + thick + inner_rad) idx = (rx + thick + inner_rad) - x;
                else if (x >= rx + rw - thick - inner_rad) idx = x - (rx + rw - thick - inner_rad - 1);
                if (y < ry + thick + inner_rad) idy = (ry + thick + inner_rad) - y;
                else if (y >= ry + rh - thick - inner_rad) idy = y - (ry + rh - thick - inner_rad - 1);
                if (idx * idx + idy * idy <= inner_r2) continue;
            }
            blend_px(bgra, w, h, x, y, r, g, b, a);
        }
    }
}

static void fill_circle(uint32_t *bgra, int w, int h, int cx, int cy, int rad,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int r2 = rad * rad;
    for (int y = cy - rad; y <= cy + rad; ++y) {
        for (int x = cx - rad; x <= cx + rad; ++x) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r2) {
                blend_px(bgra, w, h, x, y, r, g, b, a);
            }
        }
    }
}

static int text_width_px(const char *s, int scale) {
    int n = 0;
    while (s && s[n]) ++n;
    if (n == 0) return 0;
    return n * 6 * scale - scale;
}

static void draw_text(uint32_t *bgra, int w, int h, int x, int y,
                      const char *s, int scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int cx = x;
    for (int i = 0; s && s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126) c = '?';
        const uint8_t *cols = kFont5x7[c - 32];
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = cols[col];
            for (int row = 0; row < 7; ++row) {
                if (bits & (1U << row)) {
                    fill_rect(bgra, w, h, cx + col * scale, y + row * scale, scale, scale, r, g, b, a);
                }
            }
        }
        cx += 6 * scale;
    }
}

static void draw_text_centered(uint32_t *bgra, int w, int h, int cx, int y,
                               const char *s, int scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int tw = text_width_px(s, scale);
    draw_text(bgra, w, h, cx - tw / 2, y, s, scale, r, g, b, a);
}

static void draw_finn_mark(uint32_t *bgra, int w, int h, int cx, int cy, int dot_r,
                           uint8_t r, uint8_t g, uint8_t b) {
    int off = dot_r * 2 + 2;
    /* 2x2 grid of 3 circles + bottom-right 4-point sparkle (NAMING.md visual lockup) */
    fill_circle(bgra, w, h, cx - off / 2, cy - off / 2, dot_r, r, g, b, 255);
    fill_circle(bgra, w, h, cx + off / 2, cy - off / 2, dot_r, r, g, b, 255);
    fill_circle(bgra, w, h, cx - off / 2, cy + off / 2, dot_r, r, g, b, 255);
    /* Diamond sparkle at bottom-right */
    int sx = cx + off / 2, sy = cy + off / 2, sr = dot_r + 2;
    for (int dy = -sr; dy <= sr; ++dy) {
        int span = sr - (dy < 0 ? -dy : dy);
        for (int dx = -span; dx <= span; ++dx) {
            blend_px(bgra, w, h, sx + dx, sy + dy, r, g, b, 255);
        }
    }
}

static void draw_os_icon(uint32_t *bgra, int w, int h, int cx, int cy, BootOsIcon icon) {
    int sz = 68;
    int ix = cx - sz / 2;
    int iy = cy - sz / 2;
    switch (icon) {
        case OS_ICON_FINNOS:
            fill_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 0x10, 0x2A, 0x52, 255);
            stroke_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 2, 0x7A, 0xB8, 0xFF, 220);
            draw_finn_mark(bgra, w, h, cx, cy, 8, 0x7A, 0xB8, 0xFF);
            break;
        case OS_ICON_WINDOWS:
            fill_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 0x0C, 0x2A, 0x42, 255);
            stroke_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 2, 0x00, 0xA4, 0xEF, 200);
            fill_round_rect(bgra, w, h, cx - 18, cy - 18, 16, 16, 2, 0x00, 0xA4, 0xEF, 255);
            fill_round_rect(bgra, w, h, cx + 2,  cy - 18, 16, 16, 2, 0x38, 0xBD, 0xF8, 255);
            fill_round_rect(bgra, w, h, cx - 18, cy + 2,  16, 16, 2, 0x00, 0x78, 0xD4, 255);
            fill_round_rect(bgra, w, h, cx + 2,  cy + 2,  16, 16, 2, 0x00, 0xA4, 0xEF, 255);
            break;
        case OS_ICON_MACOS:
            fill_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 0x22, 0x26, 0x34, 255);
            stroke_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 2, 0xC4, 0xCC, 0xDC, 200);
            fill_round_rect(bgra, w, h, cx - 18, cy - 16, 18, 32, 5, 0x4B, 0x9C, 0xF9, 255);
            fill_round_rect(bgra, w, h, cx,      cy - 16, 18, 32, 5, 0xE2, 0xE8, 0xF0, 255);
            fill_circle(bgra, w, h, cx - 8, cy - 5, 2, 0x0F, 0x17, 0x2A, 255);
            fill_circle(bgra, w, h, cx + 8, cy - 5, 2, 0x0F, 0x17, 0x2A, 255);
            fill_rect(bgra, w, h, cx - 9, cy + 7, 18, 2, 0x0F, 0x17, 0x2A, 255);
            break;
        case OS_ICON_LINUX:
            fill_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 0x12, 0x2D, 0x20, 255);
            stroke_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 2, 0x22, 0xC5, 0x5E, 200);
            draw_text_centered(bgra, w, h, cx, cy - 10, ">_", 3, 0x4A, 0xDE, 0x80, 255);
            break;
        case OS_ICON_RECOVERY:
            fill_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 0x33, 0x22, 0x10, 255);
            stroke_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 2, 0xF5, 0x9E, 0x0B, 210);
            fill_circle(bgra, w, h, cx, cy, 18, 0xF5, 0x9E, 0x0B, 255);
            fill_circle(bgra, w, h, cx, cy, 9,  0x33, 0x22, 0x10, 255);
            fill_rect(bgra, w, h, cx - 3, cy - 20, 6, 40, 0xFE, 0xF3, 0xC7, 255);
            fill_rect(bgra, w, h, cx - 20, cy - 3, 40, 6, 0xFE, 0xF3, 0xC7, 255);
            fill_circle(bgra, w, h, cx, cy, 8,  0x33, 0x22, 0x10, 255);
            break;
        case OS_ICON_FIRMWARE:
        default:
            fill_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 0x1F, 0x24, 0x30, 255);
            stroke_round_rect(bgra, w, h, ix, iy, sz, sz, 16, 2, 0x94, 0xA3, 0xB8, 200);
            draw_text_centered(bgra, w, h, cx, cy - 10, "EFI", 2, 0xE2, 0xE8, 0xF0, 255);
            break;
    }
}

void boot_ui_compute_layout(int fb_w, int fb_h, int count, BootCardLayout *out_cards) {
    if (count <= 0) return;
    int card_w = 164;
    int card_h = 188;
    int gap = 22;
    int max_row_w = fb_w - 80;
    while (count > 1 && (count * card_w + (count - 1) * gap) > max_row_w && card_w > 110) {
        card_w -= 10;
        gap = gap > 12 ? gap - 2 : 12;
    }
    int total_w = count * card_w + (count - 1) * gap;
    int start_x = (fb_w - total_w) / 2;
    int card_y = (fb_h - card_h) / 2 - 12;
    for (int i = 0; i < count; ++i) {
        out_cards[i].x = start_x + i * (card_w + gap);
        out_cards[i].y = card_y;
        out_cards[i].w = card_w;
        out_cards[i].h = card_h;
        out_cards[i].arrow_cx = out_cards[i].x + card_w / 2;
        out_cards[i].arrow_cy = card_y + card_h + 34;
        out_cards[i].arrow_r = 18;
    }
}

int boot_ui_hit_test(int fb_w, int fb_h, int count, int selected_index,
                     int px, int py, int *hit_boot_arrow) {
    if (hit_boot_arrow) *hit_boot_arrow = 0;
    if (count <= 0 || count > FINN_BOOT_MAX_ENTRIES) return -1;
    BootCardLayout cards[FINN_BOOT_MAX_ENTRIES];
    boot_ui_compute_layout(fb_w, fb_h, count, cards);
    if (selected_index >= 0 && selected_index < count) {
        int dx = px - cards[selected_index].arrow_cx;
        int dy = py - cards[selected_index].arrow_cy;
        int r = cards[selected_index].arrow_r + 4;
        if (dx * dx + dy * dy <= r * r) {
            if (hit_boot_arrow) *hit_boot_arrow = 1;
            return selected_index;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (px >= cards[i].x && px < cards[i].x + cards[i].w &&
            py >= cards[i].y && py < cards[i].y + cards[i].h) {
            return i;
        }
    }
    return -1;
}

void boot_ui_render(uint32_t *bgra, int fb_w, int fb_h,
                    const BootEntry *entries, int count,
                    const BootUiState *state) {
    if (!bgra || fb_w <= 0 || fb_h <= 0) return;

    /* 1. Deep FinnOS obsidian canvas (#0B0E14) with subtle top radial glow */
    for (int y = 0; y < fb_h; ++y) {
        int gy = (y < fb_h / 2) ? (12 * (fb_h / 2 - y) / (fb_h / 2)) : 0;
        uint32_t row_c = bgra_px((uint8_t)(0x0B + gy / 3), (uint8_t)(0x0E + gy / 2), (uint8_t)(0x14 + gy));
        uint32_t *row = bgra + y * fb_w;
        for (int x = 0; x < fb_w; ++x) row[x] = row_c;
    }

    /* 2. Centered top FinnOS Boot Mark & wordmark */
    int header_y = fb_h / 7;
    if (header_y < 52) header_y = 52;
    draw_finn_mark(bgra, fb_w, fb_h, fb_w / 2 - 52, header_y, 5, 0x7A, 0xB8, 0xFF);
    draw_text(bgra, fb_w, fb_h, fb_w / 2 - 28, header_y - 9, "FinnOS", 3, 0xF2, 0xF4, 0xF8, 255);
    draw_text_centered(bgra, fb_w, fb_h, fb_w / 2, header_y + 24,
                       "Startup Manager", 2, 0x88, 0x91, 0xA0, 255);

    /* 3. Centered Horizontal OS Icon Cards */
    if (count > 0 && count <= FINN_BOOT_MAX_ENTRIES) {
        BootCardLayout cards[FINN_BOOT_MAX_ENTRIES];
        boot_ui_compute_layout(fb_w, fb_h, count, cards);

        for (int i = 0; i < count; ++i) {
            int sel = (state && state->selected_index == i);
            int hov = (state && state->hover_index == i && !sel);
            const BootCardLayout *c = &cards[i];

            if (sel) {
                fill_round_rect(bgra, fb_w, fb_h, c->x - 4, c->y - 4, c->w + 8, c->h + 8, 20,
                                0x00, 0x7B, 0xFF, 45);
                fill_round_rect(bgra, fb_w, fb_h, c->x, c->y, c->w, c->h, 16,
                                0x1A, 0x22, 0x33, 255);
                stroke_round_rect(bgra, fb_w, fb_h, c->x, c->y, c->w, c->h, 16, 2,
                                  0x7A, 0xB8, 0xFF, 255);
            } else if (hov) {
                fill_round_rect(bgra, fb_w, fb_h, c->x, c->y, c->w, c->h, 16,
                                0x16, 0x1B, 0x26, 255);
                stroke_round_rect(bgra, fb_w, fb_h, c->x, c->y, c->w, c->h, 16, 1,
                                  0x52, 0x62, 0x7A, 220);
            } else {
                fill_round_rect(bgra, fb_w, fb_h, c->x, c->y, c->w, c->h, 16,
                                0x12, 0x16, 0x20, 255);
                stroke_round_rect(bgra, fb_w, fb_h, c->x, c->y, c->w, c->h, 16, 1,
                                  0x28, 0x30, 0x40, 200);
            }

            int cx = c->x + c->w / 2;
            draw_os_icon(bgra, fb_w, fb_h, cx, c->y + 64, entries[i].icon);

            /* Truncate title neatly to fit card width */
            char short_title[18];
            int max_chars = (c->w - 16) / 12;
            if (max_chars > 16) max_chars = 16;
            if (max_chars < 4) max_chars = 4;
            int ti = 0;
            while (entries[i].title[ti] && ti < max_chars) {
                short_title[ti] = entries[i].title[ti];
                ++ti;
            }
            short_title[ti] = '\0';
            draw_text_centered(bgra, fb_w, fb_h, cx, c->y + 122, short_title, 2,
                               sel ? 0xFF : 0xD4, sel ? 0xFF : 0xDC, sel ? 0xFF : 0xE8, 255);

            const char *sub = entries[i].version[0] ? entries[i].version
                            : (entries[i].kind == ENTRY_KIND_UKI ? "UKI"
                            : (entries[i].kind == ENTRY_KIND_EFI_CHAINLOAD ? "UEFI"
                            : (entries[i].kind == ENTRY_KIND_FIRMWARE_SETUP ? "Setup" : "BLS")));
            draw_text_centered(bgra, fb_w, fb_h, cx, c->y + 150, sub, 1,
                               0x88, 0x91, 0xA0, 255);

            /* macOS Startup Manager circular up-arrow boot pill beneath selected card */
            if (sel) {
                fill_circle(bgra, fb_w, fb_h, c->arrow_cx, c->arrow_cy, c->arrow_r, 0x00, 0x7B, 0xFF, 255);
                /* White upward arrow */
                fill_rect(bgra, fb_w, fb_h, c->arrow_cx - 1, c->arrow_cy - 6, 3, 14, 0xFF, 0xFF, 0xFF, 255);
                for (int k = 0; k < 6; ++k) {
                    fill_rect(bgra, fb_w, fb_h, c->arrow_cx - k, c->arrow_cy - 7 + k, 2, 2, 0xFF, 0xFF, 0xFF, 255);
                    fill_rect(bgra, fb_w, fb_h, c->arrow_cx + k - 1, c->arrow_cy - 7 + k, 2, 2, 0xFF, 0xFF, 0xFF, 255);
                }
            }
        }
    }

    /* 4. Auto-boot countdown progress bar (when active) */
    if (state && state->countdown_ms > 0 && state->countdown_total_ms > 0) {
        int bar_w = 240;
        int bar_x = (fb_w - bar_w) / 2;
        int bar_y = fb_h - 92;
        fill_round_rect(bgra, fb_w, fb_h, bar_x, bar_y, bar_w, 6, 3, 0x26, 0x2E, 0x3D, 255);
        int fill_w = (bar_w * state->countdown_ms) / state->countdown_total_ms;
        if (fill_w > 6) {
            fill_round_rect(bgra, fb_w, fb_h, bar_x, bar_y, fill_w, 6, 3, 0x7A, 0xB8, 0xFF, 255);
        }
    }

    /* 5. Optional status / cmdline editor bar */
    if (state && state->status_banner && state->status_banner[0]) {
        fill_round_rect(bgra, fb_w, fb_h, fb_w / 2 - 280, fb_h - 134, 560, 28, 8, 0x16, 0x1B, 0x26, 235);
        draw_text_centered(bgra, fb_w, fb_h, fb_w / 2, fb_h - 124, state->status_banner, 1, 0xE2, 0xE8, 0xF0, 255);
    }

    /* 6. Quiet bottom keyboard hint bar */
    draw_text_centered(bgra, fb_w, fb_h, fb_w / 2, fb_h - 48,
                       "Left/Right: Select    Enter: Boot    E: Options    T: Text Mode",
                       1, 0x88, 0x91, 0xA0, 255);

    /* 7. Pointer cursor (when UEFI Simple Pointer is active) */
    if (state && state->pointer_active) {
        int px = state->pointer_x;
        int py = state->pointer_y;
        for (int row = 0; row < 16; ++row) {
            int span = (row < 12) ? (row + 1) : (16 - row);
            for (int col = 0; col < span; ++col) {
                int edge = (col == 0 || col == span - 1 || row == 15);
                blend_px(bgra, fb_w, fb_h, px + col, py + row,
                         edge ? 0x0B : 0xFF, edge ? 0x0E : 0xFF, edge ? 0x14 : 0xFF, 255);
            }
        }
    }
}
