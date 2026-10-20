#ifndef FINNTEXT_H
#define FINNTEXT_H

#include <stdint.h>

/*
 * finntext — Fixel Display text rasterisation for FinnOS.
 *
 * Fixel Display ships as CFF/OTF, so stb_truetype cannot load it; FreeType
 * is used directly rather than pulling in a full toolkit. Colours are
 * 0xAARRGGBB, matching finnvg and the DRM XRGB8888 scanout buffer.
 */

typedef struct {
    void *lib;      /* FT_Library */
    void *face;     /* FT_Face    */
    int   px;
} finn_font;

int  finn_font_load(finn_font *f, const char *path, int px);
void finn_font_free(finn_font *f);

float finn_text_width(const finn_font *f, const char *s);
/* Cap height in pixels — use this to optically centre caps and digits. */
float finn_text_cap_height(const finn_font *f);

/* y is the baseline. */
void finn_text_draw(const finn_font *f, uint32_t *buf, int w, int h,
                    float x, float y, const char *s, uint32_t color);

#endif
