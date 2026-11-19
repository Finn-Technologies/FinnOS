// Real backdrop blur for FinnOS glass layer surfaces.
//
// wlroots 0.20 has no blur render pass, so this module implements the blur on
// the CPU over the *actual* composited backdrop. On each frame the glass layer
// surfaces are temporarily hidden, the scene is rendered into the output
// buffer, the pixels under each glass rect are blurred in place, and the glass
// surfaces are composited back on top. The result is a genuine blur of the
// wallpaper and any windows behind the glass — not a translucent placeholder.
//
// Radii come from the authoritative tokens in ui/_tokens.fui.

#ifndef FINN_COMP_BACKDROP_BLUR_HPP
#define FINN_COMP_BACKDROP_BLUR_HPP

#include "ui/generated_tokens.hpp"

namespace finn::comp::backdrop_blur {

inline constexpr int kGlassRadius = finn::ui::blur::blur_glass;       // 35 px
inline constexpr int kNotificationRadius = finn::ui::blur::blur_notification;  // 15 px

// Returns the blur radius for a layer-shell namespace, or 0 when the surface
// is not glass.
int radius_for_namespace(const char* name_space);

// One-time capability report for the startup log.
void report_once();

}  // namespace finn::comp::backdrop_blur

#endif
