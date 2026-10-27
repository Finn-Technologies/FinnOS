/*
 * vgtest — host-side harness for the vector rasteriser.
 *
 * Rasterises every icon in the set and writes a sheet, so that a change to
 * finnvg can be seen without a VM, a DRM device or a compositor running.
 * The icons are drawn by their ink, at the size the shell draws them, over
 * the same bone the dock sits on.
 *
 * The directory may be the icon root, which holds shell/ and apps/, or a
 * single directory of SVGs. The shell and the catalogue keep their icons
 * apart; the installed system puts both in one directory.
 *
 *   make -C src/tools vgtest
 *   ./vgtest ../../assets/icons sheet.ppm
 */
#include "finnvg.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#define CELL 96      /* the cell an icon is drawn in, in 1080p units */
#define PAD    8

/* Find an icon by name under the root: flat first, then the two source
 * directories. This keeps the documented invocation working whether the
 * caller names assets/icons or assets/icons/shell. */
static int icon_path(char *path, size_t len, const char *dir,
                     const char *name) {
    static const char *const subdirs[] = { "", "shell", "apps" };
    for (size_t i = 0; i < sizeof subdirs / sizeof subdirs[0]; i++) {
        if (subdirs[i][0])
            snprintf(path, len, "%s/%s/%s.svg", dir, subdirs[i], name);
        else
            snprintf(path, len, "%s/%s.svg", dir, name);
        if (access(path, R_OK) == 0)
            return 0;
    }
    return -1;
}

/* Draw one icon at `S` times the shell's own size. */
static void draw_one(uint32_t *buf, int W, const char *path, int S,
                     int ox, int oy) {
    fsvg svg;
    if (fsvg_load(path, &svg) != 0) {
        fprintf(stderr, "vgtest: cannot load %s\n", path);
        return;
    }

    int sz = CELL * S;
    uint32_t *ic = malloc((size_t)sz * sz * 4);
    if (!ic) { fsvg_free(&svg); return; }
    memset(ic, 0, (size_t)sz * sz * 4);

    uint32_t ink = 0xFF1A1D24u;
    if (strstr(path, "avatar") || strstr(path, "settings-app")) {
        fsvg_raster(&svg, ic, sz, sz, NULL, NULL);
    } else {
        fsvg_raster(&svg, ic, sz, sz, fsvg_mono, &ink);
    }

    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++) {
            uint32_t c = ic[(size_t)y * sz + x];
            uint32_t *d = &buf[(size_t)(oy + y) * W + (ox + x)];
            *d = blend_over(*d, c, 1.0f);
        }

    free(ic);
    fsvg_free(&svg);
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "../../assets/icons";
    const char *out = (argc > 2) ? argv[2] : "sheet.ppm";
    int S = (argc > 3) ? atoi(argv[3]) : 2;
    if (S < 1) S = 1;

    /* the shell's own glyphs, in the order they appear in the shell */
    static const char *const shell_icons[] = {
        "boot-mark", "search-01", "home-09", "view-open-windows",
        "volume-02", "wifi-02", "battery",
        "bluetooth", "sun-01", "sun-03", "edit-01", "settings-02",
        "power", "refresh", "avatar", "chevron-right",
        "expand", "minus-sign", "x", "cursor"
    };
    /* the catalogue's app icons, one per tile the Store can draw */
    static const char *const app_icons[] = {
        "app-grid", "browser", "chevron-left", "files", "library",
        "settings-app", "store", "windows"
    };
    const int NS = (int)(sizeof shell_icons / sizeof shell_icons[0]);
    const int NA = (int)(sizeof app_icons / sizeof app_icons[0]);
    const int N = NS + NA;

    int pad = PAD * S;
    int cw = CELL * S + pad;
    int W = cw * N + pad, H = CELL * S + 2 * pad;

    uint32_t *buf = malloc((size_t)W * H * 4);
    if (!buf) { fprintf(stderr, "vgtest: out of memory\n"); return 1; }

    /* the bone the dock sits on, so the sheet shows what the shell shows */
    for (size_t i = 0; i < (size_t)W * H; i++) buf[i] = 0xFFF6F1E7u;

    for (int i = 0; i < N; i++) {
        const char *name = (i < NS) ? shell_icons[i] : app_icons[i - NS];
        char path[512];
        if (icon_path(path, sizeof path, dir, name) != 0) {
            fprintf(stderr, "vgtest: no icon named %s under %s\n", name, dir);
            free(buf);
            return 1;
        }
        draw_one(buf, W, path, S, pad + i * cw, pad);
    }

    FILE *f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "vgtest: cannot write %s\n", out); free(buf); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (size_t i = 0; i < (size_t)W * H; i++) {
        uint8_t px[3] = { (uint8_t)((buf[i] >> 16) & 0xFF),
                          (uint8_t)((buf[i] >> 8) & 0xFF),
                          (uint8_t)(buf[i] & 0xFF) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    free(buf);

    printf("vgtest: wrote %s (%dx%d, %d icons)\n", out, W, H, N);
    return 0;
}
