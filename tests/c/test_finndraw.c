#include "finndraw.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message) {
    if (condition) return 1;
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
}

static void reference_opaque_rounded_bottom(uint32_t *dst, int dw, int dh,
                                             const uint32_t *src, int sw, int sh,
                                             int dx, int dy, int radius) {
    for (int y = 0; y < sh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= dh) continue;
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= dw) continue;
            if (radius > 0 && y >= sh - radius) {
                int corner_x = -1;
                if (x < radius && x < sw / 2) corner_x = radius - x;
                else if (x >= sw - radius && x >= sw / 2)
                    corner_x = x - (sw - radius - 1);
                if (corner_x >= 0) {
                    int corner_y = y - (sh - radius - 1);
                    if ((int64_t)corner_x * corner_x +
                        (int64_t)corner_y * corner_y >
                        (int64_t)radius * radius)
                        continue;
                }
            }
            dst[(size_t)ty * dw + tx] = src[(size_t)y * sw + x];
        }
    }
}

static uint32_t test_random(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

int main(void) {
    uint32_t pixels[64 * 64] = {0};
    finn_shadow_rrect(pixels, 64, 64, 16.0f, 16.0f, 32.0f, 32.0f,
                      4.0f, 8.0f, 0.0f, 4.0f, 0.03f);

    uint32_t inside = pixels[36 * 64 + 32];
    uint32_t outside = pixels[52 * 64 + 32];
    uint32_t distant = pixels[63 * 64 + 32];
    int ok = 1;
    ok &= check(((inside >> 24) & 0xFFu) >= 7 &&
                (inside & 0x00FFFFFFu) == 0,
                "shadow paints a low-opacity black silhouette");
    ok &= check(((outside >> 24) & 0xFFu) > 0 &&
                ((outside >> 24) & 0xFFu) < ((inside >> 24) & 0xFFu),
                "shadow fades beyond the offset rounded rectangle");
    ok &= check(distant == 0,
                "shadow is clipped at the three-sigma support");

    uint32_t surface[6 * 5] = {
        0xFF000001u, 0xFF000002u, 0xFF000003u, 0xFF000004u, 0xFF000005u, 0xFF000006u,
        0xFF000007u, 0xFF000008u, 0xFF000009u, 0xFF00000Au, 0xFF00000Bu, 0xFF00000Cu,
        0xFF00000Du, 0xFF00000Eu, 0xFF00000Fu, 0xFF000010u, 0xFF000011u, 0xFF000012u,
        0xFF000013u, 0xFF000014u, 0xFF000015u, 0xFF000016u, 0xFF000017u, 0xFF000018u,
        0xFF000019u, 0xFF00001Au, 0xFF00001Bu, 0xFF00001Cu, 0xFF00001Du, 0xFF00001Eu
    };
    uint32_t surface_dst[8 * 7];
    for (size_t i = 0; i < sizeof surface_dst / sizeof surface_dst[0]; i++)
        surface_dst[i] = 0xFFABCDEFu;
    finn_blit_opaque_rounded_bottom(surface_dst, 8, 7, surface, 6, 5,
                                    1, 1, 2);
    ok &= check(surface_dst[1 * 8 + 1] == surface[0] &&
                surface_dst[1 * 8 + 6] == surface[5] &&
                surface_dst[4 * 8 + 1] == 0xFFABCDEFu &&
                surface_dst[4 * 8 + 2] == surface[3 * 6 + 1] &&
                surface_dst[4 * 8 + 5] == surface[3 * 6 + 4],
                "opaque surface blit keeps rectangular top and rounded bottom corners");

    uint32_t clipped_dst[4 * 4];
    for (size_t i = 0; i < sizeof clipped_dst / sizeof clipped_dst[0]; i++)
        clipped_dst[i] = 0xFFABCDEFu;
    finn_blit_opaque_rounded_bottom(clipped_dst, 4, 4, surface, 6, 5,
                                    -2, -1, 0);
    ok &= check(clipped_dst[0] == surface[1 * 6 + 2] &&
                clipped_dst[3] == surface[1 * 6 + 5] &&
                clipped_dst[3 * 4 + 0] == surface[4 * 6 + 2] &&
                clipped_dst[3 * 4 + 3] == surface[4 * 6 + 5],
                "opaque surface blit clips safely at negative origins");

    uint32_t source[23 * 19];
    uint32_t optimized[29 * 25], reference[29 * 25];
    uint32_t random = 0x81f2e13bu;
    for (size_t i = 0; i < sizeof source / sizeof source[0]; i++)
        source[i] = test_random(&random) | 0xFF000000u;
    for (int trial = 0; trial < 100; trial++) {
        for (size_t i = 0; i < sizeof optimized / sizeof optimized[0]; i++)
            optimized[i] = reference[i] = test_random(&random);
        int sw = 3 + (int)(test_random(&random) % 21u);
        int sh = 3 + (int)(test_random(&random) % 17u);
        int dx = (int)(test_random(&random) % 35u) - 5;
        int dy = (int)(test_random(&random) % 31u) - 4;
        int radius = (int)(test_random(&random) % 10u);
        if (radius > sw / 2) radius = sw / 2;
        if (radius > sh / 2) radius = sh / 2;
        finn_blit_opaque_rounded_bottom(optimized, 29, 25, source, sw, sh,
                                        dx, dy, radius);
        reference_opaque_rounded_bottom(reference, 29, 25, source, sw, sh,
                                         dx, dy, radius);
        if (memcmp(optimized, reference, sizeof optimized) != 0) {
            ok &= check(0, "opaque span blit matches the pixel reference under clipping");
            break;
        }
    }

    uint32_t unchanged = 0xFFFFFFFFu;
    finn_shadow_rrect(&unchanged, 1, 1, 0.0f, 0.0f, 1.0f, 1.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.03f);
    ok &= check(unchanged == 0xFFFFFFFFu,
                "zero blur leaves the destination unchanged");
    if (!ok) return 1;
    puts("finndraw shadow: OK");
    return 0;
}
