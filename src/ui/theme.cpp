// Color palette and theme definition mapping for libfinnui.
// Populates light and dark theme structures from generated design tokens
// to ensure color consistency across all native system applications.

#include "finn/ui.hpp"

#include "generated_tokens.hpp"

namespace finn::ui {
namespace c = colors;

Theme Theme::light() {
    Theme t;
    t.dark = false;
    t.bone = Color::rgba(c::bone_light);
    t.tile = Color::rgba(c::tile_light);
    t.tile_on = Color::rgba(c::tile_on_light);
    t.hair = Color::rgba(c::hair_light);
    t.ink = Color::rgba(c::ink_light);
    t.mute = Color::rgba(c::mute_light);
    t.dim = Color::rgba(c::dim_light);
    t.accent = Color::rgba(c::accent_light);
    t.on = Color::rgba(c::on_light);
    t.track = Color::rgba(c::track_light);
    t.knob = Color::rgba(c::knob_light);
    t.glass = Color::rgba(c::glass_light);
    return t;
}

Theme Theme::dark_theme() {
    Theme t;
    t.dark = true;
    t.bone = Color::rgba(c::bone_dark);
    t.tile = Color::rgba(c::tile_dark);
    t.tile_on = Color::rgba(c::tile_on_dark);
    t.hair = Color::rgba(c::hair_dark);
    t.ink = Color::rgba(c::ink_dark);
    t.mute = Color::rgba(c::mute_dark);
    t.dim = Color::rgba(c::dim_dark);
    t.accent = Color::rgba(c::accent_dark);
    t.on = Color::rgba(c::on_dark);
    t.track = Color::rgba(c::track_dark);
    t.knob = Color::rgba(c::knob_dark);
    t.glass = Color::rgba(c::glass_dark);
    return t;
}

}  // namespace finn::ui
