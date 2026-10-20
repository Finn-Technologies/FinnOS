/* finntext — Fixel Display text rasterisation for FinnOS. */
#include "finntext.h"

#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H

int finn_font_load(finn_font *f, const char *path, int px) {
    FT_Library lib;
    if (FT_Init_FreeType(&lib)) return -1;
    FT_Face face;
    if (FT_New_Face(lib, path, 0, &face)) { FT_Done_FreeType(lib); return -1; }
    if (FT_Set_Pixel_Sizes(face, 0, px)) { FT_Done_Face(face); FT_Done_FreeType(lib); return -1; }
    f->lib = lib;
    f->face = face;
    f->px = px;
    return 0;
}

void finn_font_free(finn_font *f) {
    if (f->face) FT_Done_Face((FT_Face)f->face);
    if (f->lib) FT_Done_FreeType((FT_Library)f->lib);
    f->face = f->lib = NULL;
}

/*
 * Text reaches the shell as UTF-8 and the face's charmap is Unicode, so a
 * byte is not a character: feeding bytes straight in renders every non-ASCII
 * string as latin-1 mojibake (a middle dot becomes "Â·"). Decode first, then
 * ask FreeType for the codepoint. Malformed bytes become '?' and are consumed,
 * which keeps the loop total.
 */
static unsigned utf8_next(const unsigned char **pp) {
    const unsigned char *p = *pp;
    unsigned c = *p++;
    if (c < 0x80) { *pp = p; return c; }

    int n;
    unsigned v;
    if ((c & 0xE0) == 0xC0)      { n = 1; v = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { n = 2; v = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { n = 3; v = c & 0x07u; }
    else { *pp = p; return '?'; }

    for (int i = 0; i < n; i++) {
        if ((*p & 0xC0) != 0x80) { *pp = p; return '?'; }
        v = (v << 6) | (unsigned)(*p++ & 0x3F);
    }
    *pp = p;
    return v;
}

float finn_text_width(const finn_font *f, const char *s) {
    FT_Face face = (FT_Face)f->face;
    float adv = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp = utf8_next(&p);
        if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) continue;
        adv += face->glyph->advance.x / 64.0f;
    }
    return adv;
}

float finn_text_cap_height(const finn_font *f) {
    FT_Face face = (FT_Face)f->face;
    if (FT_Load_Char(face, 'H', FT_LOAD_DEFAULT)) return f->px * 0.7f;
    return face->glyph->metrics.horiBearingY / 64.0f;
}

void finn_text_draw(const finn_font *f, uint32_t *buf, int w, int h,
                    float x, float y, const char *s, uint32_t color) {
    FT_Face face = (FT_Face)f->face;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp = utf8_next(&p);
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) continue;
        FT_GlyphSlot g = face->glyph;
        int ox = (int)floorf(x) + g->bitmap_left;
        int oy = (int)floorf(y) - g->bitmap_top;
        for (unsigned row = 0; row < g->bitmap.rows; row++) {
            int py = oy + (int)row;
            if (py < 0 || py >= h) continue;
            for (unsigned col = 0; col < g->bitmap.width; col++) {
                int px = ox + (int)col;
                if (px < 0 || px >= w) continue;
                uint8_t a = g->bitmap.buffer[row * g->bitmap.pitch + col];
                if (!a) continue;
                uint32_t *d = &buf[(size_t)py * w + px];
                float sa = a / 255.0f;
                /* Composite in straight alpha: the shell surfaces are
                 * transparent layers, so text that wrote an opaque result
                 * would punch black holes through them. */
                float da = (float)((*d >> 24) & 0xFF) / 255.0f;
                float ra = sa + da * (1.0f - sa);
                if (ra <= 0.0001f) { *d = 0; continue; }
                float k = da * (1.0f - sa), inv = 1.0f / ra;
                float s0 = (float)(color & 0xFF), s1 = (float)((color >> 8) & 0xFF),
                      s2 = (float)((color >> 16) & 0xFF);
                float d0 = (float)(*d & 0xFF), d1 = (float)((*d >> 8) & 0xFF),
                      d2 = (float)((*d >> 16) & 0xFF);
                uint32_t r0 = (uint32_t)((s0 * sa + d0 * k) * inv + 0.5f);
                uint32_t r1 = (uint32_t)((s1 * sa + d1 * k) * inv + 0.5f);
                uint32_t r2 = (uint32_t)((s2 * sa + d2 * k) * inv + 0.5f);
                uint32_t ra8 = (uint32_t)(ra * 255.0f + 0.5f);
                *d = (ra8 << 24) | (r2 << 16) | (r1 << 8) | r0;
            }
        }
        x += g->advance.x / 64.0f;
    }
}
