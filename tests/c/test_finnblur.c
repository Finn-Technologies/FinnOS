/*
 * test_finnblur — the live frosted backdrop.
 *
 * The compositor cannot be tested without a display, but the backdrop can:
 * it takes a buffer and answers colours, which is exactly the part that is
 * easy to get wrong once windows start baking themselves into it.
 *
 * Verifies that:
 * 1. A wallpaper builds a ready backdrop with an opaque colour everywhere.
 * 2. reset() puts the live backdrop back to the wallpaper.
 * 3. patch() bakes a drawn window into the backdrop, so a surface over it
 *    samples the window rather than the wallpaper.
 * 4. live() moves the generation when the desktop changed and not when it
 *    did not, because the generation is every surface's cue to redraw.
 * 5. Calling any of it before or without a wallpaper is safe.
 */
#include <stdint.h>
#include <stdio.h>

#include "finnblur.h"

static int g_failed = 0;

static void ok(const char *what, int cond) {
    if (cond) printf("ok - %s\n", what);
    else { printf("FAIL - %s\n", what); g_failed = 1; }
    fflush(stdout);
}

static uint32_t rgb(int r, int g, int b) {
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

int main(void) {
    enum { W = 320, H = 240 };

    /* Nothing built yet: every entry point has to be safe. */
    ok("no backdrop before a wallpaper", !finnblur_ready());
    ok("no colour before a wallpaper", finnblur_at(10, 10) == 0);
    finnblur_reset();
    finnblur_patch(NULL, 0, 0, 0, 0, 0, 0, 10, 10);
    ok("still no backdrop after empty calls", !finnblur_ready());

    static uint32_t wall[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            /* a graded blue so a uniform answer cannot pass by accident */
            wall[y * W + x] = rgb(20, 60 + y / 4, 160 + x / 8);

    finnblur_wall(wall, W, H);
    ok("wallpaper builds a backdrop", finnblur_ready());
    unsigned gen0 = finnblur_generation();
    ok("the generation starts above zero", gen0 > 0);

    uint32_t c = finnblur_at(W / 2, H / 2);
    ok("the backdrop answers an opaque colour", (c >> 24) == 0xFF);

    /* The centre of a red window drawn over the backdrop. */
    finnblur_reset();
    uint32_t before = finnblur_at(W / 2, H / 2);

    enum { LW = 120, LH = 90 };
    static uint32_t layer[LW * LH];
    for (int i = 0; i < LW * LH; i++) layer[i] = rgb(220, 30, 30);

    finnblur_patch(layer, LW, LH, 100, 60, 100, 60, LW, LH);

    uint32_t after = finnblur_at(100 + LW / 2, 60 + LH / 2);
    int ar = (int)(after & 0xFFu);
    int ag = (int)((after >> 8) & 0xFFu);
    int ab = (int)((after >> 16) & 0xFFu);
    ok("the window changed what the glass sees", after != before);
    ok("the baked window dominates its own rect",
       ar > ag && ar > ab && ar > 120);

    uint32_t outside = finnblur_at(20, 20);
    ok("outside the patch the wallpaper remains",
       (int)(outside & 0xFFu) < 80);

    /* The live desktop: generation moves on change, not on a repeat. */
    static uint32_t desktop[W * H];
    for (int i = 0; i < W * H; i++) desktop[i] = wall[i];

    finnblur_live(desktop, W, H);
    unsigned gen1 = finnblur_generation();
    ok("a new desktop moves the generation", gen1 != gen0);

    finnblur_live(desktop, W, H);
    ok("an unchanged desktop does not", finnblur_generation() == gen1);

    for (int i = 0; i < W * H; i++) desktop[i] = rgb(240, 240, 240);
    finnblur_live(desktop, W, H);
    ok("a changed desktop moves it again", finnblur_generation() != gen1);

    /* reset after a live build is the wallpaper again, not the desktop. */
    finnblur_reset();
    uint32_t back = finnblur_at(W / 2, H / 2);
    ok("reset restores the wallpaper", back != 0xFFF0F0F0u);

    finnblur_free();
    ok("free leaves nothing ready", !finnblur_ready());

    printf("%s\n", g_failed ? "FAILED" : "PASSED");
    return g_failed;
}
