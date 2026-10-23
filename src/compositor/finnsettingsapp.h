#ifndef FINNSETTINGSAPP_H
#define FINNSETTINGSAPP_H

#include <stdint.h>
#include <stddef.h>
#include "finnstate.h"
#include "finnkbd.h"

/*
 * finnsettingsapp — the Settings application view inside finn-comp (Figma 14:330).
 *
 * Renders the Settings window contents:
 *   - Sidebar (250x500 at left, solid white with r=8 outer corners):
 *     - "Settings" title at (15, 44) (Fixel Display 16px)
 *     - Search field at (15, 70), 220x30 r=4 with search icon and placeholder
 *     - Section labels: "NETWORK", "DEVICE", "SYSTEM" (grey 8px caps)
 *     - Interactive rows: "Wi-Fi", "Bluetooth", "Display", "Sound", "Power", "About"
 *       (220x30 r=4.14 with 18px blue circular badge and selected-row highlight)
 *   - Content area (width 650 at right over window frosted glass):
 *     - Real state and working controls:
 *       - Wi-Fi: hardware presence, connection status, enable/disable toggle
 *       - Bluetooth: adapter state, enable/disable toggle
 *       - Display: resolution, scale factor, graphics backend, working light/dark appearance toggle
 *       - Sound: master volume slider bound to ALSA mixer, mute toggle
 *       - Power/Battery: charge percentage, AC mains status, battery meter
 *       - About: FinnOS version, kernel release, hostname, system uptime, memory
 */

enum {
    FINNSETTINGS_HIT_NONE = 0,
    FINNSETTINGS_HIT_BODY,
    FINNSETTINGS_HIT_TITLEBAR,
    FINNSETTINGS_HIT_SEARCH,
    FINNSETTINGS_HIT_ROW,           /* out_sub = page index */
    FINNSETTINGS_HIT_WIFI_TOGGLE,
    FINNSETTINGS_HIT_BT_TOGGLE,
    FINNSETTINGS_HIT_LIGHT_THEME,
    FINNSETTINGS_HIT_DARK_THEME,
    FINNSETTINGS_HIT_VOL_SLIDER,
    FINNSETTINGS_HIT_MUTE_TOGGLE,
    FINNSETTINGS_HIT_BRIGHT_SLIDER
};

int  finnsettingsapp_init(const char *icon_dir, const char *font_dir);
void finnsettingsapp_free(void);

int  finnsettingsapp_tick(float dt);
uint32_t finnsettingsapp_build_key(float scale, const finn_state *st);

void finnsettingsapp_render(uint32_t *layer, int lw, int lh,
                            int lx0, int ly0, int wx, int wy, int ww, int wh,
                            int screen_w, int screen_h,
                            float scale, const finn_state *st, int focused);

int  finnsettingsapp_hit(int wx, int wy, int ww, int wh, int px, int py,
                         float scale, int *out_sub);
void finnsettingsapp_press(int hit, int sub, int px, int py, float scale,
                           const finn_state *st);
int  finnsettingsapp_drag(int px, int py, float scale, const finn_state *st);
void finnsettingsapp_release(void);

/* Keyboard delivery for the search field */
int  finnsettingsapp_key(const finn_key_event *e);

/* Scripted navigation for control protocol (settings.page) */
int  finnsettingsapp_set_page(const char *name);
const char *finnsettingsapp_get_page(void);

#endif
