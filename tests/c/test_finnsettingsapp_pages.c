#include "finnsettingsapp.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *id;
    const char *title;
    int in_navigation;
} expected_page;

static int check(int ok, const char *message) {
    if (!ok) fprintf(stderr, "not ok - %s\n", message);
    else printf("ok - %s\n", message);
    return ok ? 0 : 1;
}

int main(void) {
    static const expected_page pages[] = {
        { "wifi", "Wi-Fi", 1 },
        { "bluetooth", "Bluetooth", 1 },
        { "network", "Network", 1 },
        { "personalization", "Personalization", 1 },
        { "display", "Display", 0 },
        { "sound", "Sound", 0 },
        { "power", "Power", 0 },
        { "about", "System Information", 1 },
        { "update", "System Update", 1 },
    };
    int failed = 0;
    failed += check(finnsettingsapp_page_count() == (int)(sizeof pages / sizeof pages[0]),
                    "page metadata count is stable");

    for (int i = 0; i < (int)(sizeof pages / sizeof pages[0]); i++) {
        finn_settings_page_info info;
        int ok = finnsettingsapp_page_info(i, &info) == 0 &&
                 strcmp(info.id, pages[i].id) == 0 &&
                 strcmp(info.title, pages[i].title) == 0 &&
                 info.in_navigation == pages[i].in_navigation;
        char message[96];
        snprintf(message, sizeof message, "page %s has expected title and navigation visibility",
                 pages[i].id);
        failed += check(ok, message);
    }

    static const struct { const char *name; const char *id; } routes[] = {
        { "Network", "network" },
        { "Personalization", "personalization" },
        { "Display", "display" },
        { "Sound", "sound" },
        { "Power", "power" },
        { "System Information", "about" },
    };
    for (int i = 0; i < (int)(sizeof routes / sizeof routes[0]); i++) {
        int ok = finnsettingsapp_set_page(routes[i].name) == 0 &&
                 strcmp(finnsettingsapp_get_page(), routes[i].id) == 0;
        char message[96];
        snprintf(message, sizeof message, "settings route %s resolves", routes[i].name);
        failed += check(ok, message);
    }
    failed += check(finnsettingsapp_set_page("no-such-page") != 0,
                    "unknown settings route is rejected");

    if (failed) return 1;
    puts("finnsettingsapp_pages: Figma navigation and legacy routes passed");
    return 0;
}
