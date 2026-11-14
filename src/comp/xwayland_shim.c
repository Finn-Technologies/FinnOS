#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif

#include <wlr/xwayland.h>

const char* finn_xwayland_surface_class(const struct wlr_xwayland_surface* surface) {
    return surface ? surface->class : NULL;
}
