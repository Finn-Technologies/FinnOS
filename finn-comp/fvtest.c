/* host-side harness: rasterise the icon set and dump a PPM sheet */
#include "finnvg.c"
#include <stdio.h>

static void sheet(const char *name, int S, const char *out) {
    static const char *files[] = {
        "start_button", "volume-02", "wifi-02", "battery"
    };
    const int N = 4;
    int pad = 8 * S;
    int cw = 96 * S + pad;
    int W = cw * N + pad, H = 96 * S + 2 * pad;

    uint32_t *buf = malloc((size_t)W * H * 4);
    /* background: bone, so we see what the dock will look like */
    for (size_t i = 0; i < (size_t)W * H; i++) buf[i] = 0xFFF6F1E7u;

    for (int i = 0; i < N; i++) {
        char path[256];
        snprintf(path, sizeof path, "icons/%s.svg", files[i]);
        fsvg svg;
        if (fsvg_load(path, &svg) != 0) { fprintf(stderr, "load fail %s\n", path); continue; }
        int sz = 96 * S;
        uint32_t *ic = malloc((size_t)sz * sz * 4);
        memset(ic, 0, (size_t)sz * sz * 4);
        uint32_t ink = 0xFF1A1D24u;
        fsvg_raster(&svg, ic, sz, sz, fsvg_mono, &ink);
        int ox = pad + i * cw, oy = pad;
        for (int y = 0; y < sz; y++)
            for (int x = 0; x < sz; x++) {
                uint32_t c = ic[(size_t)y * sz + x];
                uint32_t *d = &buf[(size_t)(oy + y) * W + (ox + x)];
                *d = blend_over(*d, c, 1.0f);
            }
        free(ic);
        fsvg_free(&svg);
    }

    FILE *f = fopen(out, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (size_t i = 0; i < (size_t)W * H; i++) {
        uint8_t px[3] = { (uint8_t)((buf[i] >> 16) & 0xFF),
                          (uint8_t)((buf[i] >> 8) & 0xFF),
                          (uint8_t)(buf[i] & 0xFF) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    free(buf);
    printf("wrote %s (%dx%d)\n", out, W, H);
}

int main(void) {
    sheet("default", 2, "sheet.ppm");
    return 0;
}
