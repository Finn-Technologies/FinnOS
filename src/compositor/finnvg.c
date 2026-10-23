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

static fmat minv(const fmat *m) {
    float det = m->a * m->d - m->b * m->c;
    if (fabsf(det) < 1e-9f) return MAT_ID;
    float inv_det = 1.0f / det;
    fmat o;
    o.a =  m->d * inv_det;
    o.b = -m->b * inv_det;
    o.c = -m->c * inv_det;
    o.d =  m->a * inv_det;
    o.e = (m->c * m->f - m->d * m->e) * inv_det;
    o.f = (m->b * m->e - m->a * m->f) * inv_det;
    return o;
}

static void poly_push(fpoly *q, float x, float y) {
    if (!q) return;
    if (q->n == q->cap) {
        int ncap = q->cap ? q->cap * 2 : 64;
        fv2 *np = realloc(q->p, sizeof(fv2) * (size_t)ncap);
        if (!np) return;
        q->p = np;
        q->cap = ncap;
    }
    q->p[q->n].x = x;
    q->p[q->n].y = y;
    q->n++;
}

static fpoly *shape_newsub(fshape *sh) {
    if (!sh) return NULL;
    if (sh->npolys == sh->cpolys) {
        int ncap = sh->cpolys ? sh->cpolys * 2 : 4;
        fpoly *np = realloc(sh->polys, sizeof(fpoly) * (size_t)ncap);
        if (!np) return NULL;
        sh->polys = np;
        sh->cpolys = ncap;
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
    fv2 c1 = { p0.x + 2.0f / 3.0f * (p1.x - p0.x), p0.y + 2.0f / 3.0f * (p1.y - p0.y) };
    fv2 c2 = { p2.x + 2.0f / 3.0f * (p1.x - p2.x), p2.y + 2.0f / 3.0f * (p1.y - p2.y) };
    flatten_cubic(q, p0, c1, c2, p2);
}

static void add_arc_to_poly(fpoly *q, float x1, float y1,
                            float rx, float ry, float x_axis_rotation_deg,
                            int large_arc_flag, int sweep_flag,
                            float x2, float y2, const fmat *m) {
    if (x1 == x2 && y1 == y2) return;
    rx = fabsf(rx);
    ry = fabsf(ry);
    if (rx < 1e-6f || ry < 1e-6f) {
        fv2 t = mapply(m, x2, y2);
        poly_push(q, t.x, t.y);
        return;
    }

    float phi = x_axis_rotation_deg * (float)M_PI / 180.0f;
    float cos_phi = cosf(phi);
    float sin_phi = sinf(phi);

    float dx2 = (x1 - x2) * 0.5f;
    float dy2 = (y1 - y2) * 0.5f;

    float x1p = cos_phi * dx2 + sin_phi * dy2;
    float y1p = -sin_phi * dx2 + cos_phi * dy2;

    float prx = rx * rx;
    float pry = ry * ry;
    float px1p = x1p * x1p;
    float py1p = y1p * y1p;

    float check = px1p / prx + py1p / pry;
    if (check > 1.0f) {
        float s = sqrtf(check);
        rx *= s;
        ry *= s;
        prx = rx * rx;
        pry = ry * ry;
    }

    float sign = (large_arc_flag == sweep_flag) ? -1.0f : 1.0f;
    float num = prx * pry - prx * py1p - pry * px1p;
    float den = prx * py1p + pry * px1p;
    float sq = (den > 1e-9f && num > 0.0f) ? num / den : 0.0f;
    float coef = sign * sqrtf(sq);

    float cxp = coef * ((rx * y1p) / ry);
    float cyp = coef * (-(ry * x1p) / rx);

    float cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) * 0.5f;
    float cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) * 0.5f;

    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;

    float theta1 = atan2f(uy, ux);
    float dtheta = atan2f(ux * vy - uy * vx, ux * vx + uy * vy);

    if (!sweep_flag && dtheta > 0.0f) dtheta -= 2.0f * (float)M_PI;
    else if (sweep_flag && dtheta < 0.0f) dtheta += 2.0f * (float)M_PI;

    int nsegs = (int)ceilf(fabsf(dtheta) / ((float)M_PI / 16.0f));
    if (nsegs < 4) nsegs = 4;
    if (nsegs > 64) nsegs = 64;

    for (int i = 1; i <= nsegs; i++) {
        float th = theta1 + dtheta * ((float)i / (float)nsegs);
        float ct = cosf(th), st = sinf(th);
        float px = cos_phi * rx * ct - sin_phi * ry * st + cx;
        float py = sin_phi * rx * ct + cos_phi * ry * st + cy;
        fv2 t = mapply(m, px, py);
        poly_push(q, t.x, t.y);
    }
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

static int read_flag(const char **pp, float *out) {
    const char *p = skip_sep(*pp);
    if (!*p) return 0;
    if (*p == '0' || *p == '1') {
        *out = (float)(*p - '0');
        *pp = p + 1;
        return 1;
    }
    return read_num(pp, out);
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
        } else if (prev == 'C' || prev == 'c' || prev == 'S' || prev == 's') { c = prev;
        } else if (prev == 'A' || prev == 'a') { c = prev;
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
        case 'A': case 'a': want = 7; break;
        default: return;
        }
        for (int i = 0; i < want; i++) {
            int ok = ((c == 'A' || c == 'a') && (i == 3 || i == 4))
                   ? read_flag(&p, &a[i])
                   : read_num(&p, &a[i]);
            if (!ok) return;
        }

        int rel = islower((unsigned char)c);
        float x1, y1, x2, y2, x3, y3;

        switch (c) {
        case 'M': case 'm': {
            x1 = rel ? cx + a[0] : a[0];
            y1 = rel ? cy + a[1] : a[1];
            cx = sx = x1; cy = sy = y1;
            q = shape_newsub(sh);
            if (!q) break;
            {
                fv2 t = mapply(m, x1, y1);
                poly_push(q, t.x, t.y);
            }
            break;
        }
        case 'L': case 'l': {
            x1 = rel ? cx + a[0] : a[0];
            y1 = rel ? cy + a[1] : a[1];
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
            { fv2 t = mapply(m, x1, y1); poly_push(q, t.x, t.y); }
            cx = x1; cy = y1;
            break;
        }
        case 'H': case 'h': {
            x1 = rel ? cx + a[0] : a[0];
            y1 = cy;
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
            { fv2 t = mapply(m, x1, y1); poly_push(q, t.x, t.y); }
            cx = x1;
            break;
        }
        case 'V': case 'v': {
            x1 = cx;
            y1 = rel ? cy + a[0] : a[0];
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
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
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
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
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
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
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
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
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
            { fv2 p0 = mapply(m, cx, cy), p1 = mapply(m, rx, ry), p2 = mapply(m, x1, y1);
              flatten_quad(q, p0, p1, p2); }
            lq_x = rx; lq_y = ry;
            cx = x1; cy = y1;
            break;
        }
        case 'A': case 'a': {
            float rx = a[0], ry = a[1], rot = a[2];
            int laf = (int)a[3], sf = (int)a[4];
            x1 = rel ? cx + a[5] : a[5];
            y1 = rel ? cy + a[6] : a[6];
            if (!q) {
                q = shape_newsub(sh);
                if (!q) break;
                sx = cx; sy = cy;
                { fv2 t = mapply(m, cx, cy); poly_push(q, t.x, t.y); }
            }
            add_arc_to_poly(q, cx, cy, rx, ry, rot, laf, sf, x1, y1, m);
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
    if (!q) return;
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

static void make_ellipse(fshape *sh, float cx, float cy, float rx, float ry, const fmat *m) {
    if (rx <= 0.0f || ry <= 0.0f) return;
    fpoly *q = shape_newsub(sh);
    if (!q) return;
    const int SEG = 64;
    for (int i = 0; i < SEG; i++) {
        float a = (float)i / SEG * 2.0f * (float)M_PI;
        fv2 t = mapply(m, cx + cosf(a) * rx, cy + sinf(a) * ry);
        poly_push(q, t.x, t.y);
    }
    q->closed = 1;
}

static void make_circle(fshape *sh, float cx, float cy, float r, const fmat *m) {
    make_ellipse(sh, cx, cy, r, r, m);
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

static int clip_hit(const fclip *clip, float x, float y) {
    if (!clip || clip->is_viewbox || clip->npolys == 0) return 1;
    int wind = 0;
    for (int s = 0; s < clip->npolys; s++) {
        const fpoly *q = &clip->polys[s];
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

static int shape_fill_hit(const fshape *sh, float x, float y) {
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
    if (sh->fill_rule == 1) return (wind & 1) != 0;
    return wind != 0;
}

static uint32_t eval_grad(const fsvg *s, int gidx, const fshape *sh, float x, float y) {
    if (gidx < 0 || gidx >= s->ngrads) return sh->fill;
    const fgrad *g = &s->grads[gidx];
    if (g->nstops == 0) return 0;
    if (g->nstops == 1) return g->stops[0].color;

    fv2 p = mapply(&g->inv_transform, x, y);

    float gx1 = g->x1, gy1 = g->y1;
    float gx2 = g->x2, gy2 = g->y2;
    if (g->is_object_bbox) {
        float bw = sh->bx1 - sh->bx0;
        float bh = sh->by1 - sh->by0;
        if (bw < 1e-4f) bw = 1.0f;
        if (bh < 1e-4f) bh = 1.0f;
        gx1 = sh->bx0 + g->x1 * bw;
        gy1 = sh->by0 + g->y1 * bh;
        gx2 = sh->bx0 + g->x2 * bw;
        gy2 = sh->by0 + g->y2 * bh;
    }

    float vx = gx2 - gx1, vy = gy2 - gy1;
    float vlen2 = vx * vx + vy * vy;
    float t = 0.0f;
    if (vlen2 > 1e-9f) {
        t = ((p.x - gx1) * vx + (p.y - gy1) * vy) / vlen2;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    if (t <= g->stops[0].offset) return g->stops[0].color;
    if (t >= g->stops[g->nstops - 1].offset) return g->stops[g->nstops - 1].color;

    for (int i = 0; i < g->nstops - 1; i++) {
        if (t >= g->stops[i].offset && t <= g->stops[i + 1].offset) {
            float span = g->stops[i + 1].offset - g->stops[i].offset;
            float f = (span > 1e-6f) ? (t - g->stops[i].offset) / span : 0.0f;
            uint32_t c0 = g->stops[i].color;
            uint32_t c1 = g->stops[i + 1].color;
            float a0 = (float)((c0 >> 24) & 0xFF), a1 = (float)((c1 >> 24) & 0xFF);
            float r0 = (float)((c0 >> 16) & 0xFF), r1 = (float)((c1 >> 16) & 0xFF);
            float g0 = (float)((c0 >> 8) & 0xFF),  g1 = (float)((c1 >> 8) & 0xFF);
            float b0 = (float)(c0 & 0xFF),         b1 = (float)(c1 & 0xFF);
            uint32_t a = (uint32_t)lroundf(a0 + (a1 - a0) * f);
            uint32_t r = (uint32_t)lroundf(r0 + (r1 - r0) * f);
            uint32_t gr = (uint32_t)lroundf(g0 + (g1 - g0) * f);
            uint32_t b = (uint32_t)lroundf(b0 + (b1 - b0) * f);
            return (a << 24) | (r << 16) | (gr << 8) | b;
        }
    }
    return g->stops[g->nstops - 1].color;
}

static float stroke_dist(float px_, float py_, fv2 a, fv2 b, float r,
                         int is_start, int is_end, int linecap, int linejoin) {
    (void)linejoin;
    float vx = b.x - a.x, vy = b.y - a.y;
    float wx = px_ - a.x, wy = py_ - a.y;
    float vv = vx * vx + vy * vy;
    if (vv < 1e-9f) return hypotf(wx, wy);

    float len = sqrtf(vv);
    float t = (wx * vx + wy * vy) / vv;
    float perp_d = fabsf(wx * vy - wy * vx) / len;

    if (t < 0.0f) {
        float d_past = -t * len;
        if (is_start) {
            if (linecap == 0) { /* BUTT */
                float dx = d_past;
                float dy = fmaxf(0.0f, perp_d - r);
                return r + hypotf(dx, dy);
            } else if (linecap == 2) { /* SQUARE */
                float dx = fmaxf(0.0f, d_past - r);
                float dy = fmaxf(0.0f, perp_d - r);
                return r + hypotf(dx, dy);
            } else { /* ROUND */
                return hypotf(wx, wy);
            }
        } else {
            return hypotf(wx, wy);
        }
    } else if (t > 1.0f) {
        float d_past = (t - 1.0f) * len;
        if (is_end) {
            if (linecap == 0) { /* BUTT */
                float dx = d_past;
                float dy = fmaxf(0.0f, perp_d - r);
                return r + hypotf(dx, dy);
            } else if (linecap == 2) { /* SQUARE */
                float dx = fmaxf(0.0f, d_past - r);
                float dy = fmaxf(0.0f, perp_d - r);
                return r + hypotf(dx, dy);
            } else { /* ROUND */
                return hypotf(px_ - b.x, py_ - b.y);
            }
        } else {
            return hypotf(px_ - b.x, py_ - b.y);
        }
    } else {
        return perp_d;
    }
}

void fsvg_raster(const fsvg *s, uint32_t *dst, int w, int h,
                 fsvg_tint_fn tint, void *ud) {
    /* 4096 is four times the 4K scanout budget and prevents W*H supersample overflow */
    if (!s || !dst || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
    int W = w * SS, H = h * SS;
    uint32_t *hi = calloc((size_t)W * H, sizeof(uint32_t));
    if (!hi) return;

    float sx = (float)W / s->vw;
    float sy = (float)H / s->vh;

    for (int si = 0; si < s->n; si++) {
        const fshape *sh = &s->shapes[si];
        if (!sh->visible || sh->npolys == 0) continue;

        float clip_dev = sh->clip_x * sx;
        int has_fill = (sh->grad_idx >= 0) || ((sh->fill >> 24) != 0);
        int has_stroke = ((sh->stroke >> 24) != 0) && (sh->stroke_w > 0.0f);

        /* ---- fill ---- */
        if (has_fill) {
            int x0 = (int)floorf(sh->bx0 * sx) - 1, x1 = (int)ceilf(sh->bx1 * sx) + 1;
            int y0 = (int)floorf(sh->by0 * sy) - 1, y1 = (int)ceilf(sh->by1 * sy) + 1;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > W) x1 = W;
            if (y1 > H) y1 = H;
            for (int py_ = y0; py_ < y1; py_++) {
                for (int px_ = x0; px_ < x1; px_++) {
                    if ((float)px_ > clip_dev) continue;
                    float svg_x = (px_ + 0.5f) / sx;
                    float svg_y = (py_ + 0.5f) / sy;
                    if (sh->clip && !clip_hit(sh->clip, svg_x, svg_y)) continue;
                    if (shape_fill_hit(sh, svg_x, svg_y)) {
                        uint32_t col = sh->fill;
                        if (sh->grad_idx >= 0) {
                            col = eval_grad(s, sh->grad_idx, sh, svg_x, svg_y);
                        }
                        if (tint) col = tint(col, ud);
                        if (col >> 24) {
                            hi[(size_t)py_ * W + px_] = blend_over(hi[(size_t)py_ * W + px_], col, 1.0f);
                        }
                    }
                }
            }
        }

        /* ---- stroke ---- */
        if (has_stroke) {
            float r = sh->stroke_w * 0.5f * (sx + sy) * 0.5f;
            for (int sp = 0; sp < sh->npolys; sp++) {
                const fpoly *q = &sh->polys[sp];
                if (!q->vis) continue;
                uint32_t sc = q->col ? q->col : sh->stroke;
                if (tint) sc = tint(sc, ud);
                if (!(sc >> 24)) continue;
                int segs = q->closed ? q->n : q->n - 1;
                for (int i = 0; i < segs; i++) {
                    fv2 a0 = q->p[i];
                    fv2 b0 = q->p[(i + 1) % q->n];
                    fv2 a = { a0.x * sx, a0.y * sy }, b = { b0.x * sx, b0.y * sy };
                    int x0 = (int)floorf(fminf(a.x, b.x) - r - 1);
                    int x1 = (int)ceilf(fmaxf(a.x, b.x) + r + 1);
                    int y0 = (int)floorf(fminf(a.y, b.y) - r - 1);
                    int y1 = (int)ceilf(fmaxf(a.y, b.y) + r + 1);
                    if (x0 < 0) x0 = 0;
                    if (y0 < 0) y0 = 0;
                    if (x1 > W) x1 = W;
                    if (y1 > H) y1 = H;
                    int is_start = (!q->closed && i == 0);
                    int is_end = (!q->closed && i == segs - 1);
                    for (int py_ = y0; py_ < y1; py_++) {
                        for (int px_ = x0; px_ < x1; px_++) {
                            float svg_x = (px_ + 0.5f) / sx;
                            float svg_y = (py_ + 0.5f) / sy;
                            if (sh->clip && !clip_hit(sh->clip, svg_x, svg_y)) continue;
                            float d = stroke_dist(px_ + 0.5f, py_ + 0.5f, a, b, r,
                                                  is_start, is_end, sh->linecap, sh->linejoin);
                            float cov = 0.5f + (r - d);
                            if (cov <= 0.0f) continue;
                            if (cov > 1.0f) cov = 1.0f;
                            hi[(size_t)py_ * W + px_] = blend_over(hi[(size_t)py_ * W + px_], sc, cov);
                        }
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
    if (!s) return NULL;
    if (s->n == s->cap) {
        int ncap = s->cap ? s->cap * 2 : 8;
        fshape *ns = realloc(s->shapes, sizeof(fshape) * (size_t)ncap);
        if (!ns) return NULL;
        s->shapes = ns;
        s->cap = ncap;
    }
    fshape *sh = &s->shapes[s->n++];
    memset(sh, 0, sizeof *sh);
    sh->grad_idx = -1;
    return sh;
}

static int find_grad_by_id(const fsvg *s, const char *id) {
    for (int i = 0; i < s->ngrads; i++) {
        if (!strcmp(s->grads[i].id, id)) return i;
    }
    return -1;
}

static const fclip *find_clip_by_id(const fsvg *s, const char *id) {
    for (int i = 0; i < s->nclips; i++) {
        if (!strcmp(s->clips[i].id, id)) return &s->clips[i];
    }
    return NULL;
}

static void parse_defs(fsvg *s, const char *buf) {
    const char *p = buf;
    while ((p = strchr(p, '<'))) {
        p++;
        if (*p == '!' || *p == '?') {
            const char *e = strchr(p, '>');
            if (!e) break;
            p = e + 1;
            continue;
        }
        const char *tag_end = strchr(p, '>');
        if (!tag_end) break;
        int selfclose = (tag_end > p && tag_end[-1] == '/');
        size_t taglen = (size_t)(tag_end - p);
        char *tag = malloc(taglen + 1);
        memcpy(tag, p, taglen);
        tag[taglen] = 0;

        if (!strncmp(tag, "linearGradient", 14)) {
            if (s->ngrads == s->cgrads) {
                int ncap = s->cgrads ? s->cgrads * 2 : 4;
                fgrad *ng = realloc(s->grads, sizeof(fgrad) * (size_t)ncap);
                if (ng) { s->grads = ng; s->cgrads = ncap; }
            }
            if (s->ngrads < s->cgrads) {
                fgrad *g = &s->grads[s->ngrads++];
                memset(g, 0, sizeof *g);
                g->transform = MAT_ID;
                g->inv_transform = MAT_ID;
                g->is_object_bbox = 1;
                g->x1 = 0; g->y1 = 0; g->x2 = 1; g->y2 = 0;
                find_attr(tag, "id", g->id, sizeof g->id);
                char v[256];
                if (find_attr(tag, "gradientUnits", v, sizeof v)) {
                    if (!strcmp(v, "userSpaceOnUse")) g->is_object_bbox = 0;
                }
                if (find_attr(tag, "gradientTransform", v, sizeof v)) {
                    parse_transform(v, &g->transform);
                    g->inv_transform = minv(&g->transform);
                }
                if (find_attr(tag, "x1", v, sizeof v)) g->x1 = strtof(v, NULL);
                if (find_attr(tag, "y1", v, sizeof v)) g->y1 = strtof(v, NULL);
                if (find_attr(tag, "x2", v, sizeof v)) g->x2 = strtof(v, NULL);
                if (find_attr(tag, "y2", v, sizeof v)) g->y2 = strtof(v, NULL);

                /* parse stops until </linearGradient> */
                if (!selfclose) {
                    const char *sp = tag_end + 1;
                    while ((sp = strchr(sp, '<'))) {
                        sp++;
                        if (*sp == '/') {
                            const char *se = strchr(sp, '>');
                            if (!se) break;
                            if (!strncmp(sp + 1, "linearGradient", 14)) { p = se + 1; break; }
                            sp = se + 1;
                            continue;
                        }
                        const char *se = strchr(sp, '>');
                        if (!se) break;
                        size_t slen = (size_t)(se - sp);
                        char *stag = malloc(slen + 1);
                        memcpy(stag, sp, slen);
                        stag[slen] = 0;
                        if (!strncmp(stag, "stop", 4) && g->nstops < FSVG_MAX_STOPS) {
                            fgrad_stop *st = &g->stops[g->nstops++];
                            st->offset = 0.0f;
                            st->color = 0xFF000000u;
                            char sv[128];
                            if (find_attr(stag, "offset", sv, sizeof sv)) {
                                float off = strtof(sv, NULL);
                                if (strchr(sv, '%')) off *= 0.01f;
                                st->offset = off;
                            }
                            float stop_op = 1.0f;
                            if (find_attr(stag, "stop-opacity", sv, sizeof sv)) {
                                stop_op = strtof(sv, NULL);
                            }
                            if (find_attr(stag, "stop-color", sv, sizeof sv)) {
                                st->color = parse_color(sv);
                            }
                            uint32_t a = (uint32_t)lroundf(((st->color >> 24) & 0xFF) * stop_op);
                            st->color = (st->color & 0x00FFFFFF) | (a << 24);
                        }
                        free(stag);
                        sp = se + 1;
                    }
                    free(tag);
                    continue;
                }
            }
        } else if (!strncmp(tag, "clipPath", 8)) {
            if (s->nclips == s->cclips) {
                int ncap = s->cclips ? s->cclips * 2 : 4;
                fclip *nc = realloc(s->clips, sizeof(fclip) * (size_t)ncap);
                if (nc) { s->clips = nc; s->cclips = ncap; }
            }
            if (s->nclips < s->cclips) {
                fclip *cl = &s->clips[s->nclips++];
                memset(cl, 0, sizeof *cl);
                find_attr(tag, "id", cl->id, sizeof cl->id);
                cl->bx0 = cl->by0 = 1e30f;
                cl->bx1 = cl->by1 = -1e30f;

                if (!selfclose) {
                    const char *cp = tag_end + 1;
                    while ((cp = strchr(cp, '<'))) {
                        cp++;
                        if (*cp == '/') {
                            const char *ce = strchr(cp, '>');
                            if (!ce) break;
                            if (!strncmp(cp + 1, "clipPath", 8)) { p = ce + 1; break; }
                            cp = ce + 1;
                            continue;
                        }
                        const char *ce = strchr(cp, '>');
                        if (!ce) break;
                        size_t clen = (size_t)(ce - cp);
                        char *ctag = malloc(clen + 1);
                        memcpy(ctag, cp, clen);
                        ctag[clen] = 0;

                        fshape tmp;
                        memset(&tmp, 0, sizeof tmp);
                        char cv[256];
                        fmat cm = MAT_ID;
                        if (find_attr(ctag, "transform", cv, sizeof cv)) parse_transform(cv, &cm);

                        if (!strncmp(ctag, "rect", 4)) {
                            float x = find_attr(ctag, "x", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float y = find_attr(ctag, "y", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float w = find_attr(ctag, "width", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float h = find_attr(ctag, "height", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float rx = find_attr(ctag, "rx", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            if (rx <= 0.01f && cm.a == 1 && cm.b == 0 && cm.c == 0 && cm.d == 1 &&
                                cm.e == 0 && cm.f == 0 && x <= 0.01f && y <= 0.01f &&
                                w >= s->vw - 0.01f && h >= s->vh - 0.01f) {
                                cl->is_viewbox = 1;
                            }
                            make_rect(&tmp, x, y, w, h, rx, &cm);
                        } else if (!strncmp(ctag, "circle", 6)) {
                            float cx = find_attr(ctag, "cx", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float cy = find_attr(ctag, "cy", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float r = find_attr(ctag, "r", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            make_circle(&tmp, cx, cy, r, &cm);
                        } else if (!strncmp(ctag, "ellipse", 7)) {
                            float cx = find_attr(ctag, "cx", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float cy = find_attr(ctag, "cy", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float rx = find_attr(ctag, "rx", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            float ry = find_attr(ctag, "ry", cv, sizeof cv) ? strtof(cv, NULL) : 0;
                            make_ellipse(&tmp, cx, cy, rx, ry, &cm);
                        } else if (!strncmp(ctag, "path", 4)) {
                            char d[8192];
                            if (find_attr(ctag, "d", d, sizeof d)) parse_path(&tmp, d, &cm);
                        }
                        shape_finish(&tmp);

                        if (tmp.npolys > 0) {
                            int ncap = cl->npolys + tmp.npolys;
                            fpoly *ncp = realloc(cl->polys, sizeof(fpoly) * (size_t)ncap);
                            if (ncp) {
                                cl->polys = ncp;
                                for (int i = 0; i < tmp.npolys; i++) {
                                    cl->polys[cl->npolys++] = tmp.polys[i];
                                }
                            }
                            if (tmp.bx0 < cl->bx0) cl->bx0 = tmp.bx0;
                            if (tmp.by0 < cl->by0) cl->by0 = tmp.by0;
                            if (tmp.bx1 > cl->bx1) cl->bx1 = tmp.bx1;
                            if (tmp.by1 > cl->by1) cl->by1 = tmp.by1;
                            free(tmp.polys);
                        }

                        free(ctag);
                        cp = ce + 1;
                    }
                    free(tag);
                    continue;
                }
            }
        }
        free(tag);
        p = tag_end + 1;
    }
}

typedef struct {
    fmat m;
    float opacity;
    uint32_t fill;
    int has_fill;
    int grad_idx;
    uint32_t stroke;
    int has_stroke;
    float stroke_w;
    int has_stroke_w;
    int linecap;
    int linejoin;
    int fill_rule;
    const fclip *clip;
} fgroup_state;

int fsvg_load(const char *path, fsvg *s) {
    memset(s, 0, sizeof *s);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    /* 16 MiB bound guards against corrupt/huge files overflowing n+1 or exhausting memory */
    if (n < 0 || n > 16 * 1024 * 1024) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return -1; }
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

    parse_defs(s, buf);

    fgroup_state stack[16];
    int depth = 0;
    memset(&stack[0], 0, sizeof stack[0]);
    stack[0].m = MAT_ID;
    stack[0].opacity = 1.0f;
    stack[0].grad_idx = -1;
    stack[0].linecap = 0; /* BUTT */
    stack[0].linejoin = 0; /* MITER */

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

        /* skip <defs> and <clipPath> in Pass 2 */
        if (!selfclose && !strncmp(p, "defs", 4)) {
            const char *close = strstr(tag_end, "</defs>");
            if (close) { p = close; continue; }
        }
        if (!selfclose && !strncmp(p, "clipPath", 8)) {
            const char *close = strstr(tag_end, "</clipPath>");
            if (close) { p = close; continue; }
        }

        /* tag text is [p, tag_end) */
        size_t taglen = (size_t)(tag_end - p);
        char *tag = malloc(taglen + 1);
        memcpy(tag, p, taglen);
        tag[taglen] = 0;

        if (!strncmp(tag, "g", 1) && !isalpha((unsigned char)tag[1])) {
            if (depth < 15) {
                depth++;
                stack[depth] = stack[depth - 1];
                char v[256];
                if (find_attr(tag, "transform", v, sizeof v)) {
                    fmat m = MAT_ID;
                    parse_transform(v, &m);
                    stack[depth].m = mmul(&stack[depth - 1].m, &m);
                }
                if (find_attr(tag, "opacity", v, sizeof v)) {
                    stack[depth].opacity = stack[depth - 1].opacity * strtof(v, NULL);
                }
                if (find_attr(tag, "fill-rule", v, sizeof v)) {
                    stack[depth].fill_rule = !strcmp(v, "evenodd") ? 1 : 0;
                }
                if (find_attr(tag, "stroke-linecap", v, sizeof v)) {
                    if (!strcmp(v, "round")) stack[depth].linecap = 1;
                    else if (!strcmp(v, "square")) stack[depth].linecap = 2;
                    else stack[depth].linecap = 0;
                }
                if (find_attr(tag, "stroke-linejoin", v, sizeof v)) {
                    if (!strcmp(v, "round")) stack[depth].linejoin = 1;
                    else if (!strcmp(v, "bevel")) stack[depth].linejoin = 2;
                    else stack[depth].linejoin = 0;
                }
                if (find_attr(tag, "clip-path", v, sizeof v)) {
                    const char *idstart = strchr(v, '#');
                    if (idstart) {
                        idstart++;
                        char cid[64];
                        int ci = 0;
                        while (*idstart && *idstart != ')' && *idstart != '\'' && *idstart != '"' && ci < 63) {
                            cid[ci++] = *idstart++;
                        }
                        cid[ci] = 0;
                        stack[depth].clip = find_clip_by_id(s, cid);
                    }
                }
                if (find_attr(tag, "fill", v, sizeof v)) {
                    if (!strncmp(v, "url(#", 5)) {
                        char gid[64];
                        int gi = 0;
                        const char *gp = v + 5;
                        while (*gp && *gp != ')' && *gp != '\'' && *gp != '"' && gi < 63) gid[gi++] = *gp++;
                        gid[gi] = 0;
                        stack[depth].grad_idx = find_grad_by_id(s, gid);
                        stack[depth].has_fill = 1;
                        stack[depth].fill = 0xFFFFFFFFu;
                    } else {
                        stack[depth].fill = parse_color(v);
                        stack[depth].has_fill = 1;
                        stack[depth].grad_idx = -1;
                    }
                }
                if (find_attr(tag, "stroke", v, sizeof v)) {
                    stack[depth].stroke = parse_color(v);
                    stack[depth].has_stroke = 1;
                }
                if (find_attr(tag, "stroke-width", v, sizeof v)) {
                    stack[depth].stroke_w = strtof(v, NULL);
                    stack[depth].has_stroke_w = 1;
                }
            }
            if (!selfclose) { free(tag); p = tag_end + 1; continue; }
        } else if (!strncmp(tag, "path", 4) || !strncmp(tag, "rect", 4) ||
                   !strncmp(tag, "circle", 6) || !strncmp(tag, "ellipse", 7)) {
            char v[256];
            fshape *sh = svg_newshape(s);
            if (sh) {
                sh->grad_idx = stack[depth].grad_idx;
                sh->fill = stack[depth].fill;
                sh->stroke = stack[depth].stroke;
                sh->stroke_w = stack[depth].has_stroke_w ? stack[depth].stroke_w : (sh->stroke ? 1.0f : 0.0f);
                sh->linecap = stack[depth].linecap;
                sh->linejoin = stack[depth].linejoin;
                sh->fill_rule = stack[depth].fill_rule;
                sh->clip = stack[depth].clip;

                if (find_attr(tag, "fill-rule", v, sizeof v)) {
                    sh->fill_rule = !strcmp(v, "evenodd") ? 1 : 0;
                }
                if (find_attr(tag, "stroke-linecap", v, sizeof v)) {
                    if (!strcmp(v, "round")) sh->linecap = 1;
                    else if (!strcmp(v, "square")) sh->linecap = 2;
                    else sh->linecap = 0;
                }
                if (find_attr(tag, "stroke-linejoin", v, sizeof v)) {
                    if (!strcmp(v, "round")) sh->linejoin = 1;
                    else if (!strcmp(v, "bevel")) sh->linejoin = 2;
                    else sh->linejoin = 0;
                }
                if (find_attr(tag, "clip-path", v, sizeof v)) {
                    const char *idstart = strchr(v, '#');
                    if (idstart) {
                        idstart++;
                        char cid[64];
                        int ci = 0;
                        while (*idstart && *idstart != ')' && *idstart != '\'' && *idstart != '"' && ci < 63) {
                            cid[ci++] = *idstart++;
                        }
                        cid[ci] = 0;
                        sh->clip = find_clip_by_id(s, cid);
                    }
                }

                if (find_attr(tag, "fill", v, sizeof v)) {
                    if (!strncmp(v, "url(#", 5)) {
                        char gid[64];
                        int gi = 0;
                        const char *gp = v + 5;
                        while (*gp && *gp != ')' && *gp != '\'' && *gp != '"' && gi < 63) gid[gi++] = *gp++;
                        gid[gi] = 0;
                        sh->grad_idx = find_grad_by_id(s, gid);
                        sh->fill = 0xFFFFFFFFu;
                    } else {
                        sh->fill = parse_color(v);
                        sh->grad_idx = -1;
                    }
                }
                if (find_attr(tag, "stroke", v, sizeof v)) {
                    sh->stroke = parse_color(v);
                }
                if (find_attr(tag, "stroke-width", v, sizeof v)) {
                    sh->stroke_w = strtof(v, NULL);
                } else if (sh->stroke && !stack[depth].has_stroke_w) {
                    sh->stroke_w = 1.0f;
                }

                float op = 1.0f, fill_op = 1.0f, stroke_op = 1.0f;
                if (find_attr(tag, "opacity", v, sizeof v)) op = strtof(v, NULL);
                if (find_attr(tag, "fill-opacity", v, sizeof v)) fill_op = strtof(v, NULL);
                if (find_attr(tag, "stroke-opacity", v, sizeof v)) stroke_op = strtof(v, NULL);

                float total_fill_op = stack[depth].opacity * op * fill_op;
                float total_stroke_op = stack[depth].opacity * op * stroke_op;

                if (sh->fill) {
                    uint32_t a = (uint32_t)lroundf(((sh->fill >> 24) & 0xFF) * total_fill_op);
                    sh->fill = (sh->fill & 0x00FFFFFF) | (a << 24);
                }
                if (sh->stroke) {
                    uint32_t a = (uint32_t)lroundf(((sh->stroke >> 24) & 0xFF) * total_stroke_op);
                    sh->stroke = (sh->stroke & 0x00FFFFFF) | (a << 24);
                }

                fmat m = MAT_ID;
                if (find_attr(tag, "transform", v, sizeof v)) parse_transform(v, &m);
                fmat full = mmul(&stack[depth].m, &m);

                if (!strncmp(tag, "path", 4)) {
                    char d[8192];
                    if (find_attr(tag, "d", d, sizeof d)) parse_path(sh, d, &full);
                } else if (!strncmp(tag, "rect", 4)) {
                    float x = find_attr(tag, "x", v, sizeof v) ? strtof(v, NULL) : 0;
                    float y = find_attr(tag, "y", v, sizeof v) ? strtof(v, NULL) : 0;
                    float rw = find_attr(tag, "width", v, sizeof v) ? strtof(v, NULL) : 0;
                    float rh = find_attr(tag, "height", v, sizeof v) ? strtof(v, NULL) : 0;
                    float rx = find_attr(tag, "rx", v, sizeof v) ? strtof(v, NULL) : 0;
                    make_rect(sh, x, y, rw, rh, rx, &full);
                } else if (!strncmp(tag, "circle", 6)) {
                    float cx = find_attr(tag, "cx", v, sizeof v) ? strtof(v, NULL) : 0;
                    float cy = find_attr(tag, "cy", v, sizeof v) ? strtof(v, NULL) : 0;
                    float r = find_attr(tag, "r", v, sizeof v) ? strtof(v, NULL) : 0;
                    make_circle(sh, cx, cy, r, &full);
                } else if (!strncmp(tag, "ellipse", 7)) {
                    float cx = find_attr(tag, "cx", v, sizeof v) ? strtof(v, NULL) : 0;
                    float cy = find_attr(tag, "cy", v, sizeof v) ? strtof(v, NULL) : 0;
                    float rx = find_attr(tag, "rx", v, sizeof v) ? strtof(v, NULL) : 0;
                    float ry = find_attr(tag, "ry", v, sizeof v) ? strtof(v, NULL) : 0;
                    make_ellipse(sh, cx, cy, rx, ry, &full);
                }

                shape_finish(sh);
                if (sh->npolys == 0) s->n--;
            }
        }

        free(tag);
        p = tag_end + 1;
    }
    free(buf);
    return 0;
}

void fsvg_free(fsvg *s) {
    if (!s) return;
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->shapes[i].npolys; j++)
            free(s->shapes[i].polys[j].p);
    free(s->shapes);

    for (int i = 0; i < s->nclips; i++) {
        for (int j = 0; j < s->clips[i].npolys; j++)
            free(s->clips[i].polys[j].p);
        free(s->clips[i].polys);
    }
    free(s->clips);
    free(s->grads);
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

        if (sh->clip && !sh->clip->is_viewbox) {
            if (qx0 < sh->clip->bx0) qx0 = sh->clip->bx0;
            if (qy0 < sh->clip->by0) qy0 = sh->clip->by0;
            if (qx1 > sh->clip->bx1) qx1 = sh->clip->bx1;
            if (qy1 > sh->clip->by1) qy1 = sh->clip->by1;
        }

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
