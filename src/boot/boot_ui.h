/* Pure freestanding C macOS Startup Manager-inspired GOP framebuffer renderer
 * for FinnOS graphical UEFI bootloader (finn-boot).
 * Renders into a 32-bit BGRA pixel buffer (matches EFI_GRAPHICS_OUTPUT_BLT_PIXEL).
 */

#ifndef FINN_BOOT_UI_H
#define FINN_BOOT_UI_H

#include "bls_parser.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   selected_index;
    int   hover_index;       /* -1 if none */
    int   countdown_ms;      /* >0 when auto-boot timer active, 0 when cancelled */
    int   countdown_total_ms;
    int   pointer_active;
    int   pointer_x;
    int   pointer_y;
    int   editing_options;   /* 1 when live kernel cmdline editor overlay is open */
    const char *status_banner;
} BootUiState;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int arrow_cx;
    int arrow_cy;
    int arrow_r;
} BootCardLayout;

void boot_ui_compute_layout(int fb_w, int fb_h, int count,
                            BootCardLayout *out_cards);

/* Hit-test pointer click: returns card index >= 0, or -1; sets *hit_boot_arrow=1 if click is on the circular arrow. */
int boot_ui_hit_test(int fb_w, int fb_h, int count, int selected_index,
                     int px, int py, int *hit_boot_arrow);

/* Render complete Startup Manager frame into BGRA buffer (stride == fb_w). */
void boot_ui_render(uint32_t *bgra, int fb_w, int fb_h,
                    const BootEntry *entries, int count,
                    const BootUiState *state);

#ifdef __cplusplus
}
#endif

#endif /* FINN_BOOT_UI_H */
