/*
 * finnvg — minimal SVG subset rasteriser for FinnOS iconography.
 * See finnvg.h for the contract.
 */
#include "finnvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* geometry primitives                                                 */
/* ------------------------------------------------------------------ */

typedef struct { float a, b, c, d, e, f; } fmat;

static const fmat MAT_ID = { 1, 0, 0, 1, 0, 0 };

static fv2 mapply(const fmat *m, float x, float y) {
    fv2 r = { m->a * x + m->c * y + m->e, m->b * x + m->d * y + m->f };
    return r;
}

static fmat mmul(const fmat *l, const fmat *r) {
    fmat o;
    o.a = l->a * r->a + l->c * r->b;
    o.b = l->b * r->a + l->d * r->b;
    o.c = l->a * r->c + l->c * r->d;
    o.d = l->b * r->c + l->d * r->d;
    o.e = l->a * r->e + l->c * r->f + l->e;
    o.f = l->b * r->e + l->d * r->f + l->f;
    return o;
}

static void poly_push(fpoly *q, float x, float y) {
    if (q->n == q->cap) {
        q->cap = q->cap ? q->cap * 2 : 64;
        q->p = realloc(q->p, sizeof(fv2) * q->cap);
    }
    q->p[q->n].x = x;
    q->p[q->n].y = y;
    q->n++;
}

static fpoly *shape_newsub(fshape *sh) {
    if (sh->npolys == sh->cpolys) {
        sh->cpolys = sh->cpolys ? sh->cpolys * 2 : 4;
        sh->polys = realloc(sh->polys, sizeof(fpoly) * sh->cpolys);
    }
    fpoly *q = &sh->polys[sh->npolys++];
    memset(q, 0, sizeof *q);
    q->vis = 1;
    return q;
}

static void poly_bbox(const fpoly *q, float *x0, float *y0, float *x1, float *y1) {
    for (int i = 0; i < q->n; i++) {
        if (q->p[i].x < *x0) *x0 = q->p[i].x;
        if (q->p[i].y < *y0) *y0 = q->p[i].y;
        if (q->p[i].x > *x1) *x1 = q->p[i].x;
        if (q->p[i].y > *y1) *y1 = q->p[i].y;
    }
}

/* flatteners take already-transformed control points */
static void flatten_cubic(fpoly *q, fv2 p0, fv2 p1, fv2 p2, fv2 p3) {
    float len = 0;
    fv2 prev = p0;
    fv2 cp[3] = { p1, p2, p3 };
    for (int i = 0; i < 3; i++) {
        len += hypotf(cp[i].x - prev.x, cp[i].y - prev.y);
        prev = cp[i];
    }
    int n = (int)ceilf(len / 2.0f);
    if (n < 8) n = 8;
    if (n > 96) n = 96;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / n, u = 1.0f - t;
        float w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
        poly_push(q,
                  w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x,
                  w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y);
    }
}

static void flatten_quad(fpoly *q, fv2 p0, fv2 p1, fv2 p2) {
    /* elevate to cubic */
    fv2 c1 = { p0.x + 2.0f / 3.0f * (p1.x - p0.x), p0.y + 2.0f / 3.0f * (p1.y - p0.y) };
    fv2 c2 = { p2.x + 2.0f / 3.0f * (p1.x - p2.x), p2.y + 2.0f / 3.0f * (p1.y - p2.y) };
    flatten_cubic(q, p0, c1, c2, p2);
}

/* ------------------------------------------------------------------ */
/* value parsing                                                       */
/* ------------------------------------------------------------------ */

static const char *skip_sep(const char *p) {
    while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
    return p;
}

static int read_num(const char **pp, float *out) {
    const char *p = skip_sep(*pp);
    if (!*p) return 0;
    char *end = NULL;
    float v = strtof(p, &end);
    if (end == p) return 0;
    *out = v;
    *pp = end;
    return 1;
}

static int find_attr(const char *tag, const char *name, char *out, int n) {
    size_t nl = strlen(name);
    const char *p = tag;
    while ((p = strstr(p, name))) {
        const char *q = p + nl;
        if (p != tag && !isspace((unsigned char)p[-1])) { p = q; continue; }
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '=') { p = q; continue; }
        q++;
        while (*q == ' ' || *q == '\t') q++;
        char quote = (*q == '"' || *q == '\'') ? *q++ : 0;
        const char *e = q;
        if (quote) while (*e && *e != quote) e++;
        else while (*e && !isspace((unsigned char)*e) && *e != '>' && *e != '/') e++;
        int len = (int)(e - q);
        if (len > n - 1) len = n - 1;
        memcpy(out, q, len);
        out[len] = 0;
        return 1;
    }
    return 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* returns packed 0xAARRGGBB, or 0 for none */
static uint32_t parse_color(const char *s) {
    if (!s || !*s) return 0;
    if (!strcmp(s, "none")) return 0;
    if (!strcmp(s, "black")) return 0xFF000000u;
    if (!strcmp(s, "white")) return 0xFFFFFFFFu;
    if (!strcmp(s, "transparent")) return 0;

    if (s[0] == '#') {
        size_t n = strlen(s + 1);
        if (n >= 6) {
            int r = hexval(s[1]) * 16 + hexval(s[2]);
            int g = hexval(s[3]) * 16 + hexval(s[4]);
            int b = hexval(s[5]) * 16 + hexval(s[6]);
            if (r < 0 || g < 0 || b < 0) return 0;
            return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
        if (n >= 3) {
            int r = hexval(s[1]), g = hexval(s[2]), b = hexval(s[3]);
            if (r < 0 || g < 0 || b < 0) return 0;
            r = r * 16 + r; g = g * 16 + g; b = b * 16 + b;
            return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
        return 0;
    }
    if (!strncmp(s, "rgb", 3)) {
        const char *p = strchr(s, '(');
        if (!p) return 0;
        float c[3] = { 0, 0, 0 };
        p++;
        for (int i = 0; i < 3; i++) {
            if (!read_num(&p, &c[i])) return 0;
        }
        uint32_t r = (uint32_t)fmaxf(0, fminf(255, c[0]));
        uint32_t g = (uint32_t)fmaxf(0, fminf(255, c[1]));
        uint32_t b = (uint32_t)fmaxf(0, fminf(255, c[2]));
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return 0;
}

static void parse_transform(const char *s, fmat *m) {
    *m = MAT_ID;
    if (!s) return;
    const char *p = s;
    while (*p) {
        while (*p && !isalpha((unsigned char)*p)) p++;
        if (!*p) break;
        const char *name = p;
        while (isalpha((unsigned char)*p)) p++;
        size_t nl = (size_t)(p - name);
        while (*p && *p != '(') p++;
        if (*p != '(') break;
        p++;
        float a[6] = { 0 };
        int n = 0;
        while (*p && *p != ')' && n < 6) {
            if (!read_num(&p, &a[n])) break;
            n++;
        }
        while (*p && *p != ')') p++;
        if (*p == ')') p++;

        fmat t = MAT_ID;
        if (nl == 6 && !strncmp(name, "matrix", 6) && n == 6) {
            t.a = a[0]; t.b = a[1]; t.c = a[2]; t.d = a[3]; t.e = a[4]; t.f = a[5];
        } else if (nl == 9 && !strncmp(name, "translate", 9) && n >= 1) {
            t.e = a[0]; t.f = (n > 1) ? a[1] : 0;
        } else if (nl == 5 && !strncmp(name, "scale", 5) && n >= 1) {
            t.a = a[0]; t.d = (n > 1) ? a[1] : a[0];
        } else if (nl == 6 && !strncmp(name, "rotate", 6) && n >= 1) {
            float rad = a[0] * (float)M_PI / 180.0f;
            float cs = cosf(rad), sn = sinf(rad);
            fmat rot = { cs, sn, -sn, cs, 0, 0 };
            if (n >= 3) {
                fmat to = { 1, 0, 0, 1, a[1], a[2] };
                fmat back = { 1, 0, 0, 1, -a[1], -a[2] };
                fmat rb = mmul(&rot, &back);
                t = mmul(&to, &rb);
            } else {
                t = rot;
            }
        }
        *m = mmul(m, &t);
    }
}

/* ------------------------------------------------------------------ */
/* path data                                                           */
/* ------------------------------------------------------------------ */

static void parse_path(fshape *sh, const char *d, const fmat *m) {
    fpoly *q = NULL;
    float cx = 0, cy = 0;          /* current point            */
    float sx = 0, sy = 0;          /* subpath start            */
    float lc_x = 0, lc_y = 0;      /* last cubic control       */
    float lq_x = 0, lq_y = 0;      /* last quad control        */
    char prev = 0;
    const char *p = d;

    while (*p) {
        p = skip_sep(p);
        if (!*p) break;
        char c = *p;
        if (isalpha((unsigned char)c)) {
            p++;
        } else if (prev == 'M') { c = 'L';
        } else if (prev == 'm') { c = 'l';
        } else if (prev == 'C' || prev == 'c' || prev == 'S' || prev == 's') { c = prev == 'C' ? 'C' : (prev == 'c' ? 'c' : (prev == 'S' ? 'S' : 's'));
        } else { c = prev; }

        if (c == 'Z' || c == 'z') {
            if (q) {
                q->closed = 1;
                poly_push(q, sx, sy);
            }
            cx = sx; cy = sy;
            prev = c;
            continue;
        }

        float a[7] = { 0 };
        int want = 0;
        switch (c) {
        case 'M': case 'm': case 'L': case 'l': case 'T': case 't': want = 2; break;
        case 'H': case 'h': case 'V': case 'v': want = 1; break;
        case 'C': case 'c': want = 6; break;
        case 'S': case 's': case 'Q': case 'q': want = 4; break;
        default: /* unsupported (A/a) — stop cleanly */
            return;
        }
        for (int i = 0; i < want; i++)
            if (!read_num(&p, &a[i])) return;

        int rel = islower((unsigned char)c);
        float x1, y1, x2, y2, x3, y3;

        switch (c) {
        case 'M': case 'm': {
            x1 = rel ? cx + a[0] : a[0];
            y1 = rel ? cy + a[1] : a[1];
            cx = sx = x1; cy = sy = y1;
            q = shape_newsub(sh);
            {
                fv2 t = mapply(m, x1, y1);
                poly_push(q, t.x, t.y);
            }
            break;
        }
        case 'L': case 'l': {
            x1 = rel ? cx + a[0] : a[0];
            y1 = rel ? cy + a[1] : a[1];
            if (!q) { q = shape_newsub(sh); sx = cx; sy = cy;
                      { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); } }
            { fv2 t = mapply(m, x1, y1); poly_push(q, t.x, t.y); }
            cx = x1; cy = y1;
            break;
        }
        case 'H': case 'h': {
            x1 = rel ? cx + a[0] : a[0];
            y1 = cy;
            if (!q) { q = shape_newsub(sh); sx = cx; sy = cy;
                      { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); } }
            { fv2 t = mapply(m, x1, y1); poly_push(q, t.x, t.y); }
            cx = x1;
            break;
        }
        case 'V': case 'v': {
            x1 = cx;
            y1 = rel ? cy + a[0] : a[0];
            if (!q) { q = shape_newsub(sh); sx = cx; sy = cy;
                      { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); } }
            { fv2 t = mapply(m, x1, y1); poly_push(q, t.x, t.y); }
            cy = y1;
            break;
        }
        case 'C': case 'c': {
            x2 = rel ? cx + a[0] : a[0];
            y2 = rel ? cy + a[1] : a[1];
            x3 = rel ? cx + a[2] : a[2];
            y3 = rel ? cy + a[3] : a[3];
            x1 = rel ? cx + a[4] : a[4];
            y1 = rel ? cy + a[5] : a[5];
            if (!q) { q = shape_newsub(sh); sx = cx; sy = cy;
                      { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); } }
            { fv2 p0 = mapply(m, cx, cy), p1 = mapply(m, x2, y2),
                    p2 = mapply(m, x3, y3), p3 = mapply(m, x1, y1);
              flatten_cubic(q, p0, p1, p2, p3); }
            lc_x = x3; lc_y = y3;
            cx = x1; cy = y1;
            break;
        }
        case 'S': case 's': {
            float rx, ry;
            if (prev == 'C' || prev == 'c' || prev == 'S' || prev == 's') {
                rx = 2 * cx - lc_x; ry = 2 * cy - lc_y;
            } else { rx = cx; ry = cy; }
            x2 = rel ? cx + a[0] : a[0];
            y2 = rel ? cy + a[1] : a[1];
            x1 = rel ? cx + a[2] : a[2];
            y1 = rel ? cy + a[3] : a[3];
            if (!q) { q = shape_newsub(sh); sx = cx; sy = cy;
                      { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); } }
            { fv2 p0 = mapply(m, cx, cy), p1 = mapply(m, rx, ry),
                    p2 = mapply(m, x2, y2), p3 = mapply(m, x1, y1);
              flatten_cubic(q, p0, p1, p2, p3); }
            lc_x = x2; lc_y = y2;
            cx = x1; cy = y1;
            break;
        }
        case 'Q': case 'q': {
            x2 = rel ? cx + a[0] : a[0];
            y2 = rel ? cy + a[1] : a[1];
            x1 = rel ? cx + a[2] : a[2];
            y1 = rel ? cy + a[3] : a[3];
            if (!q) { q = shape_newsub(sh); sx = cx; sy = cy;
                      { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); } }
            { fv2 p0 = mapply(m, cx, cy), p1 = mapply(m, x2, y2), p2 = mapply(m, x1, y1);
              flatten_quad(q, p0, p1, p2); }
            lq_x = x2; lq_y = y2;
            cx = x1; cy = y1;
            break;
        }
        case 'T': case 't': {
            float rx, ry;
            if (prev == 'Q' || prev == 'q' || prev == 'T' || prev == 't') {
                rx = 2 * cx - lq_x; ry = 2 * cy - lq_y;
            } else { rx = cx; ry = cy; }
            x1 = rel ? cx + a[0] : a[0];
            y1 = rel ? cy + a[1] : a[1];
            if (!q) { q = shape_newsub(sh); sx = cx; sy = cy;
                      { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); } }
            { fv2 p0 = mapply(m, cx, cy), p1 = mapply(m, rx, ry), p2 = mapply(m, x1, y1);
              flatten_quad(q, p0, p1, p2); }
            lq_x = rx; lq_y = ry;
            cx = x1; cy = y1;
            break;
        }
        default: return;
        }
        prev = c;
    }
}

/* ------------------------------------------------------------------ */
/* shape constructors                                                  */
/* ------------------------------------------------------------------ */

static void shape_finish(fshape *sh) {
    sh->bx0 = sh->by0 = 1e30f;
    sh->bx1 = sh->by1 = -1e30f;
    for (int i = 0; i < sh->npolys; i++)
        poly_bbox(&sh->polys[i], &sh->bx0, &sh->by0, &sh->bx1, &sh->by1);
    if (sh->npolys == 0) { sh->bx0 = sh->by0 = sh->bx1 = sh->by1 = 0; }
    sh->clip_x = 1e30f;
    sh->visible = 1;
}

static void make_rect(fshape *sh, float x, float y, float w, float h,
                      float rx, const fmat *m) {
    if (rx > w * 0.5f) rx = w * 0.5f;
    if (rx > h * 0.5f) rx = h * 0.5f;
    if (rx < 0) rx = 0;
    fpoly *q = shape_newsub(sh);
    const int SEG = 10;
    if (rx <= 0.01f) {
        fv2 pts[4] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
        for (int i = 0; i < 4; i++) { fv2 t = mapply(m, pts[i].x, pts[i].y); poly_push(q, t.x, t.y); }
    } else {
        struct { float cx, cy, a0; } corners[4] = {
            { x + rx,     y + rx,     180 }, { x + w - rx, y + rx,     270 },
            { x + w - rx, y + h - rx,   0 }, { x + rx,     y + h - rx,  90 },
        };
        for (int c = 0; c < 4; c++) {
            for (int i = 0; i <= SEG; i++) {
                float a = (corners[c].a0 + 90.0f * i / SEG) * (float)M_PI / 180.0f;
                float px_ = corners[c].cx + cosf(a) * rx;
                float py_ = corners[c].cy + sinf(a) * rx;
                fv2 t = mapply(m, px_, py_);
                poly_push(q, t.x, t.y);
            }
        }
    }
    q->closed = 1;
}

static void make_circle(fshape *sh, float cx, float cy, float r, const fmat *m) {
    fpoly *q = shape_newsub(sh);
    const int SEG = 64;
    for (int i = 0; i < SEG; i++) {
        float a = (float)i / SEG * 2.0f * (float)M_PI;
        fv2 t = mapply(m, cx + cosf(a) * r, cy + sinf(a) * r);
        poly_push(q, t.x, t.y);
    }
    q->closed = 1;
}

/* ------------------------------------------------------------------ */
/* rasteriser                                                          */
/* ------------------------------------------------------------------ */

#define SS 4                       /* supersample factor */

static inline uint32_t blend_over(uint32_t dst, uint32_t src, float a) {
    if (a <= 0.0f) return dst;
    float sa = a * ((src >> 24) & 0xFF) / 255.0f;
    if (sa <= 0.0f) return dst;
    if (sa >= 1.0f) return (src & 0x00FFFFFFu) | 0xFF000000u;
    uint32_t d0 = dst & 0xFF, d1 = (dst >> 8) & 0xFF, d2 = (dst >> 16) & 0xFF;
    uint32_t s0 = src & 0xFF, s1 = (src >> 8) & 0xFF, s2 = (src >> 16) & 0xFF;
    uint32_t r0 = (uint32_t)(s0 * sa + d0 * (1 - sa));
    uint32_t r1 = (uint32_t)(s1 * sa + d1 * (1 - sa));
    uint32_t r2 = (uint32_t)(s2 * sa + d2 * (1 - sa));
    return 0xFF000000u | (r2 << 16) | (r1 << 8) | r0;
}

static int nonzero_fill_hit(const fshape *sh, float x, float y) {
    int wind = 0;
    for (int s = 0; s < sh->npolys; s++) {
        const fpoly *q = &sh->polys[s];
        if (!q->vis) continue;
        for (int i = 0; i < q->n; i++) {
            fv2 a = q->p[i];
            fv2 b = q->p[(i + 1) % q->n];
            if (a.y <= y) {
                if (b.y > y) {
                    float cr = (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y);
                    if (cr > 0) wind++;
                }
            } else {
                if (b.y <= y) {
                    float cr = (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y);
                    if (cr < 0) wind--;
                }
            }
        }
    }
    return wind != 0;
}

static float seg_dist(float px_, float py_, fv2 a, fv2 b) {
    float vx = b.x - a.x, vy = b.y - a.y;
    float wx = px_ - a.x, wy = py_ - a.y;
    float vv = vx * vx + vy * vy;
    float t = vv > 1e-9f ? (wx * vx + wy * vy) / vv : 0.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float dx = wx - vx * t, dy = wy - vy * t;
    return sqrtf(dx * dx + dy * dy);
}

void fsvg_raster(const fsvg *s, uint32_t *dst, int w, int h,
                 fsvg_tint_fn tint, void *ud) {
    if (w <= 0 || h <= 0) return;
    int W = w * SS, H = h * SS;
    uint32_t *hi = calloc((size_t)W * H, sizeof(uint32_t));
    if (!hi) return;

    float sx = (float)W / s->vw;
    float sy = (float)H / s->vh;

    for (int si = 0; si < s->n; si++) {
        const fshape *sh = &s->shapes[si];
        if (!sh->visible || sh->npolys == 0) continue;

        uint32_t fill = sh->fill, stroke = sh->stroke;
        if (tint) { fill = fill ? tint(fill, ud) : 0; stroke = stroke ? tint(stroke, ud) : 0; }
        float clip_dev = sh->clip_x * sx;

        /* ---- fill ---- */
        if ((fill >> 24) && sh->fill) {
            int x0 = (int)floorf(sh->bx0 * sx) - 1, x1 = (int)ceilf(sh->bx1 * sx) + 1;
            int y0 = (int)floorf(sh->by0 * sy) - 1, y1 = (int)ceilf(sh->by1 * sy) + 1;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > W) x1 = W;
            if (y1 > H) y1 = H;
            for (int py_ = y0; py_ < y1; py_++) {
                for (int px_ = x0; px_ < x1; px_++) {
                    if ((float)px_ > clip_dev) continue;
                    if (nonzero_fill_hit(sh, (px_ + 0.5f) / sx, (py_ + 0.5f) / sy))
                        hi[(size_t)py_ * W + px_] = blend_over(hi[(size_t)py_ * W + px_], fill, 1.0f);
                }
            }
        }

        /* ---- stroke: union of round-capped capsules ---- */
        if ((stroke >> 24) && sh->stroke && sh->stroke_w > 0) {
            float r = sh->stroke_w * 0.5f * (sx + sy) * 0.5f;
            for (int sp = 0; sp < sh->npolys; sp++) {
                const fpoly *q = &sh->polys[sp];
                if (!q->vis) continue;
                uint32_t sc = q->col ? q->col : stroke;
                if (!(sc >> 24)) continue;
                int segs = q->closed ? q->n : q->n - 1;
                for (int i = 0; i < segs; i++) {
                    fv2 a0 = q->p[i];
                    fv2 b0 = q->p[(i + 1) % q->n];
                    /* bring endpoints into device space */
                    fv2 a = { a0.x * sx, a0.y * sy }, b = { b0.x * sx, b0.y * sy };
                    int x0 = (int)floorf(fminf(a.x, b.x) - r - 1);
                    int x1 = (int)ceilf(fmaxf(a.x, b.x) + r + 1);
                    int y0 = (int)floorf(fminf(a.y, b.y) - r - 1);
                    int y1 = (int)ceilf(fmaxf(a.y, b.y) + r + 1);
                    if (x0 < 0) x0 = 0;
                    if (y0 < 0) y0 = 0;
                    if (x1 > W) x1 = W;
                    if (y1 > H) y1 = H;
                    for (int py_ = y0; py_ < y1; py_++)
                        for (int px_ = x0; px_ < x1; px_++) {
                            float d = seg_dist(px_ + 0.5f, py_ + 0.5f, a, b);
                            float cov = 0.5f + (r - d);
                            if (cov <= 0.0f) continue;
                            if (cov > 1.0f) cov = 1.0f;
                            hi[(size_t)py_ * W + px_] = blend_over(hi[(size_t)py_ * W + px_], sc, cov);
                        }
                }
            }
        }
    }

    /* ---- box downsample SS x SS ---- */
    int n = SS * SS;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t a = 0, r = 0, g = 0, b = 0, acc = 0;
            for (int j = 0; j < SS; j++)
                for (int i = 0; i < SS; i++) {
                    uint32_t c = hi[(size_t)(y * SS + j) * W + (x * SS + i)];
                    uint32_t ca = (c >> 24) & 0xFF;
                    r += ((c >> 16) & 0xFF) * ca;
                    g += ((c >> 8) & 0xFF) * ca;
                    b += (c & 0xFF) * ca;
                    acc += ca;
                    a += ca;
                }
            if (acc == 0) { dst[(size_t)y * w + x] = 0; continue; }
            uint32_t oa = a / n;
            dst[(size_t)y * w + x] = (oa << 24) |
                (((r / acc) & 0xFF) << 16) | (((g / acc) & 0xFF) << 8) | ((b / acc) & 0xFF);
        }
    }
    free(hi);
}

uint32_t fsvg_mono(uint32_t svg_color, void *ud) {
    uint32_t ink = *(uint32_t *)ud;
    return (ink & 0x00FFFFFFu) | (((svg_color >> 24) & 0xFF) << 24);
}

/* ------------------------------------------------------------------ */
/* loader                                                              */
/* ------------------------------------------------------------------ */

static fshape *svg_newshape(fsvg *s) {
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->shapes = realloc(s->shapes, sizeof(fshape) * s->cap);
    }
    fshape *sh = &s->shapes[s->n++];
    memset(sh, 0, sizeof *sh);
    return sh;
}

int fsvg_load(const char *path, fsvg *s) {
    memset(s, 0, sizeof *s);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); free(buf); return -1; }
    buf[n] = 0;
    fclose(f);

    s->vw = s->vh = 24.0f;

    /* viewBox */
    {
        char vb[128];
        if (find_attr(buf, "viewBox", vb, sizeof vb)) {
            const char *p = vb;
            float a[4];
            int k = 0;
            while (k < 4 && read_num(&p, &a[k])) k++;
            if (k == 4 && a[2] > 0 && a[3] > 0) { s->vw = a[2]; s->vh = a[3]; }
        }
    }

    fmat stack[8];
    int depth = 0;
    stack[depth] = MAT_ID;

    const char *p = buf;
    while ((p = strchr(p, '<'))) {
        p++;
        if (*p == '/') {                       /* closing tag */
            const char *e = strchr(p, '>');
            if (!e) break;
            if (p[1] == 'g' && depth > 0) depth--;
            p = e + 1;
            continue;
        }
        if (*p == '!' || *p == '?') { const char *e = strchr(p, '>'); if (!e) break; p = e + 1; continue; }

        const char *tag_end = strchr(p, '>');
        if (!tag_end) break;
        int selfclose = (tag_end > p && tag_end[-1] == '/');

        /* <defs> holds clip paths and gradients, never drawable geometry */
        if (!selfclose && (!strncmp(p, "defs", 4) || !strncmp(p, "clipPath", 8))) {
            const char *close = strstr(tag_end, "</defs>");
            if (close) { p = close; continue; }
        }

        /* tag text is [p, tag_end) */
        size_t taglen = (size_t)(tag_end - p);
        char *tag = malloc(taglen + 1);
        memcpy(tag, p, taglen);
        tag[taglen] = 0;

        if (!strncmp(tag, "g", 1) && !isalpha((unsigned char)tag[1])) {
            char tf[256];
            fmat m = MAT_ID;
            if (find_attr(tag, "transform", tf, sizeof tf)) parse_transform(tf, &m);
            if (depth < 7) { depth++; stack[depth] = mmul(&stack[depth - 1], &m); }
            if (!selfclose) { free(tag); p = tag_end + 1; continue; }
        } else if (!strncmp(tag, "path", 4)) {
            char d[8192];
            if (find_attr(tag, "d", d, sizeof d)) {
                fshape *sh = svg_newshape(s);
                char v[128];
                sh->fill = find_attr(tag, "fill", v, sizeof v) ? parse_color(v) : 0;
                if (find_attr(tag, "stroke", v, sizeof v)) sh->stroke = parse_color(v);
                sh->stroke_w = find_attr(tag, "stroke-width", v, sizeof v) ? strtof(v, NULL) : (sh->stroke ? 1.0f : 0.0f);
                fmat m = MAT_ID;
                if (find_attr(tag, "transform", v, sizeof v)) parse_transform(v, &m);
                fmat full = mmul(&stack[depth], &m);
                parse_path(sh, d, &full);
                shape_finish(sh);
                if (sh->npolys == 0) s->n--;
            }
        } else if (!strncmp(tag, "rect", 4)) {
            char v[128];
            float x = find_attr(tag, "x", v, sizeof v) ? strtof(v, NULL) : 0;
            float y = find_attr(tag, "y", v, sizeof v) ? strtof(v, NULL) : 0;
            float rw = find_attr(tag, "width", v, sizeof v) ? strtof(v, NULL) : 0;
            float rh = find_attr(tag, "height", v, sizeof v) ? strtof(v, NULL) : 0;
            float rx = find_attr(tag, "rx", v, sizeof v) ? strtof(v, NULL) : 0;
            fshape *sh = svg_newshape(s);
            sh->fill = find_attr(tag, "fill", v, sizeof v) ? parse_color(v) : 0;
            if (find_attr(tag, "stroke", v, sizeof v)) sh->stroke = parse_color(v);
            sh->stroke_w = find_attr(tag, "stroke-width", v, sizeof v) ? strtof(v, NULL) : 0;
            fmat m = MAT_ID;
            if (find_attr(tag, "transform", v, sizeof v)) parse_transform(v, &m);
            fmat full = mmul(&stack[depth], &m);
            make_rect(sh, x, y, rw, rh, rx, &full);
            shape_finish(sh);
        } else if (!strncmp(tag, "circle", 6)) {
            char v[128];
            float cx = find_attr(tag, "cx", v, sizeof v) ? strtof(v, NULL) : 0;
            float cy = find_attr(tag, "cy", v, sizeof v) ? strtof(v, NULL) : 0;
            float r = find_attr(tag, "r", v, sizeof v) ? strtof(v, NULL) : 0;
            fshape *sh = svg_newshape(s);
            sh->fill = find_attr(tag, "fill", v, sizeof v) ? parse_color(v) : 0;
            if (find_attr(tag, "stroke", v, sizeof v)) sh->stroke = parse_color(v);
            sh->stroke_w = find_attr(tag, "stroke-width", v, sizeof v) ? strtof(v, NULL) : 0;
            fmat m = MAT_ID;
            if (find_attr(tag, "transform", v, sizeof v)) parse_transform(v, &m);
            fmat full = mmul(&stack[depth], &m);
            make_circle(sh, cx, cy, r, &full);
            shape_finish(sh);
        }

        free(tag);
        p = tag_end + 1;
    }
    free(buf);
    return 0;
}

void fsvg_free(fsvg *s) {
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->shapes[i].npolys; j++)
            free(s->shapes[i].polys[j].p);
    free(s->shapes);
    memset(s, 0, sizeof *s);
}

void fsvg_ink(const fsvg *s, float *x0, float *y0, float *x1, float *y1) {
    float a = 1e30f, b = 1e30f, c = -1e30f, d = -1e30f;

    for (int i = 0; s && i < s->n; i++) {
        const fshape *sh = &s->shapes[i];
        if (sh->npolys == 0) continue;

        /* a stroked outline reaches half its width past the polyline */
        float e = (sh->stroke >> 24) ? sh->stroke_w * 0.5f : 0.0f;
        float qx0 = sh->bx0 - e, qy0 = sh->by0 - e;
        float qx1 = sh->bx1 + e, qy1 = sh->by1 + e;

        if (qx0 < a) a = qx0;
        if (qy0 < b) b = qy0;
        if (qx1 > c) c = qx1;
        if (qy1 > d) d = qy1;
    }

    if (a > c || b > d) {          /* nothing drawable: fall back to the box */
        a = b = 0.0f;
        c = s && s->vw > 0 ? s->vw : 0.0f;
        d = s && s->vh > 0 ? s->vh : 0.0f;
    }
    *x0 = a; *y0 = b; *x1 = c; *y1 = d;
}
