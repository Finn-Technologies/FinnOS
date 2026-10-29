/* finntext — Fixel Display text rasterisation for FinnOS. */
#include "finntext.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H

/*
 * The shell redraws the same few hundred glyphs on every frame — a dock
 * label, a window title, a settings row — and FreeType rasterises each one
 * again for each draw. Rasterising is the expensive half of text; the
 * metrics come almost free. So cache the rendered bitmap next to the
 * metrics, keyed by codepoint, and let a draw be a copy and a blend.
 *
 * A direct-mapped table keeps the lookup one multiply and one compare, and
 * a collision costs one re-rasterisation of a glyph that was already on
 * screen. 256 slots cover the Latin labels the shell redraws; a font with
 * more glyphs still renders, it simply misses more often. The table is per
 * font, and a font is one face at one pixel size.
 */
#define GLYPH_SLOTS 256

typedef struct {
    uint32_t cp;       /* codepoint + 1, so 0 means the slot is empty */
    uint8_t  draw;     /* the bitmap is valid, even if it is empty */
    uint8_t  metrics;  /* advance and bearing came from FreeType */
    int      left;     /* bitmap_left */
    int      top;      /* bitmap_top */
    int      w, h;     /* bitmap dimensions */
    float    advance;  /* horizontal advance in pixels */
    float    bearing_y;
    uint8_t *px;       /* w * h coverage, tightly packed, row major */
} glyph;

static unsigned glyph_slot(unsigned cp) {
    /* Knuth's multiplicative hash, top byte: the codepoints in a label are
     * close together, and the multiply spreads them across the table. */
    return (cp * 2654435761u) >> 24;
}

static glyph *glyph_get(const finn_font *f, unsigned cp) {
    glyph *tab = (glyph *)f->glyphs;
    if (!tab) return NULL;
    glyph *g = &tab[glyph_slot(cp)];
    if (g->cp == cp + 1) return g;

    free(g->px);
    memset(g, 0, sizeof *g);
    g->cp = cp + 1;

    FT_Face face = (FT_Face)f->face;
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) {
        /*
         * A glyph that cannot be rendered can still have an advance, and
         * finn_text_width has always counted it. Ask for the metrics so
         * the width sum does not change; draw skips the glyph, as before.
         */
        if (!FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) {
            g->metrics = 1;
            g->advance = face->glyph->advance.x / 64.0f;
            g->bearing_y = face->glyph->metrics.horiBearingY / 64.0f;
        }
        return g;
    }

    FT_GlyphSlot s = face->glyph;
    g->draw = 1;
    g->metrics = 1;
    g->left = s->bitmap_left;
    g->top = s->bitmap_top;
    g->w = (int)s->bitmap.width;
    g->h = (int)s->bitmap.rows;
    g->advance = s->advance.x / 64.0f;
    g->bearing_y = s->metrics.horiBearingY / 64.0f;

    if (g->w > 0 && g->h > 0 && s->bitmap.buffer) {
        g->px = malloc((size_t)g->w * (size_t)g->h);
        if (g->px) {
            /* The face's bitmap is padded to its pitch; the cache is not. */
            for (int row = 0; row < g->h; row++)
                memcpy(g->px + (size_t)row * g->w,
                       s->bitmap.buffer + (size_t)row * s->bitmap.pitch,
                       (size_t)g->w);
        } else {
            /* No room to cache it. Drop the slot so the next call takes
             * the direct path rather than drawing an empty glyph. */
            g->cp = 0;
            return NULL;
        }
    } else {
        g->w = g->h = 0;
    }
    return g;
}

int finn_font_load(finn_font *f, const char *path, int px) {
    FT_Library lib;
    if (FT_Init_FreeType(&lib)) return -1;
    FT_Face face;
    if (FT_New_Face(lib, path, 0, &face)) { FT_Done_FreeType(lib); return -1; }
    if (FT_Set_Pixel_Sizes(face, 0, px)) { FT_Done_Face(face); FT_Done_FreeType(lib); return -1; }
    f->lib = lib;
    f->face = face;
    f->px = px;
    /* A cache that cannot be allocated is not a load failure: the text
     * functions fall back to FreeType and the shell still draws. */
    f->glyphs = calloc(GLYPH_SLOTS, sizeof(glyph));
    return 0;
}

void finn_font_free(finn_font *f) {
    if (f->glyphs) {
        glyph *tab = (glyph *)f->glyphs;
        for (int i = 0; i < GLYPH_SLOTS; i++) free(tab[i].px);
        free(tab);
        f->glyphs = NULL;
    }
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
    float adv = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp = utf8_next(&p);
        const glyph *g = glyph_get(f, cp);
        if (g) {
            /* A glyph FreeType can measure but not render still advances;
             * one it cannot load at all has advance 0, exactly as the old
             * FT_LOAD_CHAR loop behaved. */
            adv += g->advance;
            continue;
        }
        FT_Face face = (FT_Face)f->face;
        if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) continue;
        adv += face->glyph->advance.x / 64.0f;
    }
    return adv;
}

float finn_text_cap_height(const finn_font *f) {
    const glyph *g = glyph_get(f, 'H');
    if (g) return g->metrics ? g->bearing_y : f->px * 0.7f;
    FT_Face face = (FT_Face)f->face;
    if (FT_Load_Char(face, 'H', FT_LOAD_DEFAULT)) return f->px * 0.7f;
    return face->glyph->metrics.horiBearingY / 64.0f;
}

/*
 * One coverage bitmap onto one straight-alpha buffer. The clip is computed
 * once per glyph instead of once per pixel: a glyph is mostly inside its
 * target, and the branch in the inner loop was the larger cost.
 */
static void draw_bitmap(uint32_t *buf, int w, int h, int ox, int oy,
                        const uint8_t *src, int pitch, int gw, int gh,
                        uint32_t color, float ca, int solid) {
    if (!src || gw <= 0 || gh <= 0) return;

    int row0 = oy < 0 ? -oy : 0;
    int row1 = h - oy;
    if (row1 > gh) row1 = gh;
    int col0 = ox < 0 ? -ox : 0;
    int col1 = w - ox;
    if (col1 > gw) col1 = gw;
    if (row0 >= row1 || col0 >= col1) return;

    float s0 = (float)(color & 0xFF);
    float s1 = (float)((color >> 8) & 0xFF);
    float s2 = (float)((color >> 16) & 0xFF);
    uint32_t rgb = color & 0x00FFFFFFu;

    for (int row = row0; row < row1; row++) {
        const uint8_t *sp = src + (ptrdiff_t)row * pitch + col0;
        uint32_t *dp = buf + (size_t)(oy + row) * w + (ox + col0);
        for (int col = col0; col < col1; col++, sp++, dp++) {
            uint8_t a = *sp;
            if (!a) continue;
            /* Fully opaque ink over any destination lands on exactly this
             * value: the general formula reduces to it, but only after six
             * float multiplies and a divide. */
            if (solid && a == 255) { *dp = 0xFF000000u | rgb; continue; }
            float sa = (a / 255.0f) * ca;
            if (sa <= 0.0f) continue;
            /* Composite in straight alpha: the shell surfaces are
             * transparent layers, so text that wrote an opaque result
             * would punch black holes through them. */
            uint32_t d = *dp;
            float da = (float)((d >> 24) & 0xFF) / 255.0f;
            float ra = sa + da * (1.0f - sa);
            if (ra <= 0.0001f) { *dp = 0; continue; }
            float k = da * (1.0f - sa), inv = 1.0f / ra;
            float d0 = (float)(d & 0xFF), d1 = (float)((d >> 8) & 0xFF),
                  d2 = (float)((d >> 16) & 0xFF);
            uint32_t r0 = (uint32_t)((s0 * sa + d0 * k) * inv + 0.5f);
            uint32_t r1 = (uint32_t)((s1 * sa + d1 * k) * inv + 0.5f);
            uint32_t r2 = (uint32_t)((s2 * sa + d2 * k) * inv + 0.5f);
            uint32_t ra8 = (uint32_t)(ra * 255.0f + 0.5f);
            *dp = (ra8 << 24) | (r2 << 16) | (r1 << 8) | r0;
        }
    }
}

void finn_text_draw(const finn_font *f, uint32_t *buf, int w, int h,
                    float x, float y, const char *s, uint32_t color) {
    const unsigned char *p = (const unsigned char *)s;

    /* The token's own alpha is part of the ink: a label that is fading out
     * fades by its colour, not by a second mechanism. Every token the shell
     * draws with is opaque, so this changes nothing until something asks it
     * to. */
    float ca = (float)((color >> 24) & 0xFFu) / 255.0f;
    int solid = ((color >> 24) & 0xFFu) == 255u;

    while (*p) {
        unsigned cp = utf8_next(&p);
        const glyph *g = glyph_get(f, cp);
        if (g) {
            if (!g->draw) continue;
            int ox = (int)floorf(x) + g->left;
            int oy = (int)floorf(y) - g->top;
            draw_bitmap(buf, w, h, ox, oy, g->px, g->w, g->w, g->h,
                        color, ca, solid);
            x += g->advance;
            continue;
        }

        /* No cache (or the slot could not be filled): rasterise directly. */
        FT_Face face = (FT_Face)f->face;
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) continue;
        FT_GlyphSlot slot = face->glyph;
        int ox = (int)floorf(x) + slot->bitmap_left;
        int oy = (int)floorf(y) - slot->bitmap_top;
        draw_bitmap(buf, w, h, ox, oy, slot->bitmap.buffer,
                    slot->bitmap.pitch, (int)slot->bitmap.width,
                    (int)slot->bitmap.rows, color, ca, solid);
        x += slot->advance.x / 64.0f;
    }
}
