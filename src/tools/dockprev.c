/*
 * dockprev — host-side preview of the FinnOS dock.
 *
 * Links the real finndock/finnvg code and renders the chrome over a flat
 * background, writing a PPM. This is the fast iteration loop: no VM, no
 * DRM, no rebuild-and-boot cycle.
 *
 *   make -C src/tools
 *   src/tools/dockprev out.ppm <screen_h> <zoom> <battery> <charging> <wifi> <volume>
 *
 * battery: 0..100, or -1 for mains-only
 * wifi:    0..3, or -1 for no interface
 * volume:  0..100
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "finndock.h"
#include "finnblur.h"

int main(int argc, char **argv) {
    if (argc < 8) {
        fprintf(stderr,
                "usage: %s out.ppm screen_h zoom battery charging wifi volume\n"
                "  battery 0..100 or -1 (mains); wifi 0..3 or -1; volume 0..100\n",
                argv[0]);
        return 2;
    }
    const char *out = argv[1];
    int screen_h = atoi(argv[2]);
    int zoom = atoi(argv[3]);
    finn_state st;
    memset(&st, 0, sizeof st);
    st.battery_pct = atoi(argv[4]);
    st.charging = atoi(argv[5]);
    st.wifi = atoi(argv[6]);
    st.volume = atoi(argv[7]);
    st.brightness = -1;
    st.bluetooth = -1;
    st.wired = -1;

    /* The icons and the face are found relative to the tree, so the preview
     * can be run from anywhere in it. */
    if (finndock_init("../../assets/icons/shell", "../../assets/fonts/FixelDisplay") != 0) {
        fprintf(stderr, "dockprev: cannot load the icons and the face\n"
                        "           (run it from src/tools/)\n");
        return 1;
    }

    float scale = fmaxf(0.85f, fminf((float)screen_h / 1080.0f, 2.0f));

    /* a canvas wide enough for the dock, tall enough to show the radius */
    int W = (int)(scale * 520);
    int H = (int)(scale * 200);
    uint32_t *buf = malloc((size_t)W * H * 4);
    if (!buf) return 1;

    /* stand-in for the fluted wallpaper gradient */
    for (int y = 0; y < H; y++) {
        float t = (float)y / H;
        int r = (int)(0xE8 + (0xFF - 0xE8) * t);
        int g = (int)(0xB8 + (0xC0 - 0xB8) * t);
        int b = (int)(0xD8 + (0x9A - 0xD8) * t);
        for (int x = 0; x < W; x++)
            buf[(size_t)y * W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    /* the preview's backdrop is a stand-in, but the glass samples the real
     * blur path so the material is what ships */
    finnblur_wall(buf, W, H);

    finndock_render(W, H, scale, &st);
    finndock_blit(buf, W, H, 0, 0);

    /* crop to the dock band and zoom for inspection */
    int band_h = (int)(scale * 96);
    int oy = H - band_h;
    int OW = W * zoom, OH = band_h * zoom;

    FILE *f = fopen(out, "wb");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", OW, OH);
    for (int y = 0; y < OH; y++)
        for (int x = 0; x < OW; x++) {
            uint32_t c = buf[(size_t)(oy + y / zoom) * W + (x / zoom)];
            uint8_t p[3] = { (uint8_t)((c >> 16) & 0xFF),
                             (uint8_t)((c >> 8) & 0xFF),
                             (uint8_t)(c & 0xFF) };
            fwrite(p, 1, 3, f);
        }
    fclose(f);
    printf("%s  (%dx%d, scale %.2f)\n", out, OW, OH, scale);
    finndock_free();
    free(buf);
    return 0;
}
