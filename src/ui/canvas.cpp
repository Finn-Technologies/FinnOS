#include "finn/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "raster/finntext.h"
#include "raster/finnvg.h"
}

namespace finn::ui {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp(double value, double low, double high) {
    return std::max(low, std::min(high, value));
}

Rect intersect(const Rect& a, const Rect& b) {
    const double x0 = std::max(a.x, b.x);
    const double y0 = std::max(a.y, b.y);
    const double x1 = std::min(a.x + a.w, b.x + b.w);
    const double y1 = std::min(a.y + a.h, b.y + b.h);
    return Rect{x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
}

double round_rect_distance(double px, double py, const Rect& r, double radius) {
    const double half_w = r.w / 2.0;
    const double half_h = r.h / 2.0;
    const double cx = r.x + half_w;
    const double cy = r.y + half_h;
    const double qx = std::fabs(px - cx) - (half_w - radius);
    const double qy = std::fabs(py - cy) - (half_h - radius);
    const double outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
    return outside + std::min(std::max(qx, qy), 0.0) - radius;
}

void blend_pixel(std::uint32_t& dst, std::uint32_t src, double alpha) {
    const double sa = ((src >> 24) & 0xFFu) / 255.0 * clamp(alpha, 0.0, 1.0);
    if (sa <= 0.0) return;
    const double da = ((dst >> 24) & 0xFFu) / 255.0;
    const double oa = sa + da * (1.0 - sa);
    auto channel = [&](int shift) {
        const double s = static_cast<double>((src >> shift) & 0xFFu);
        const double d = static_cast<double>((dst >> shift) & 0xFFu);
        const double out = (s * sa + d * da * (1.0 - sa)) / oa;
        return static_cast<std::uint32_t>(clamp(out + 0.5, 0.0, 255.0));
    };
    const auto a = static_cast<std::uint32_t>(clamp(oa * 255.0 + 0.5, 0.0, 255.0));
    dst = (a << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

std::string font_path() {
    if (const char* path = std::getenv("FINN_FONT"); path && *path) return path;
    const char* candidates[] = {
        "/usr/share/fonts/OTF/FixelDisplay-Regular.otf",
        "assets/fonts/FixelDisplay/FixelDisplay-Regular.otf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    for (const char* path : candidates) {
        FILE* file = std::fopen(path, "rb");
        if (file != nullptr) {
            std::fclose(file);
            return path;
        }
    }
    return {};
}

std::string resolve_icon(std::string_view name) {
    if (name.empty()) return {};
    if (name.find('/') != std::string_view::npos) return std::string(name);
    if (const char* dir = std::getenv("FINN_ICON_DIR"); dir != nullptr && *dir != '\0') {
        const std::string candidate = std::string(dir) + "/" + std::string(name);
        FILE* file = std::fopen(candidate.c_str(), "rb");
        if (file != nullptr) {
            std::fclose(file);
            return candidate;
        }
    }
    const char* roots[] = {
        "/usr/share/finn/icons/",
        "assets/icons/apps/",
        "assets/icons/shell/",
    };
    for (const char* root : roots) {
        const std::string candidate = std::string(root) + std::string(name);
        FILE* file = std::fopen(candidate.c_str(), "rb");
        if (file != nullptr) {
            std::fclose(file);
            return candidate;
        }
    }
    return {};
}

}  // namespace

struct Canvas::Impl {
    struct WidgetState {
        bool hover = false;
        bool active = false;
    };

    struct Icon {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
    };

    std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    double scale = 1.0;
    Input* input = nullptr;
    const Theme* theme = nullptr;
    std::vector<Rect> clips;
    std::unordered_map<std::string, WidgetState> states;
    std::string focus;
    std::map<int, finn_font> fonts;
    std::unordered_map<std::string, Icon> icons;
    bool font_warned = false;

    ~Impl() {
        for (auto& [size, font] : fonts) {
            (void)size;
            finn_font_free(&font);
        }
    }

    Rect visible() const {
        if (clips.empty()) return Rect{0, 0, static_cast<double>(width), static_cast<double>(height)};
        return clips.back();
    }

    void fill_rect(Rect rect, std::uint32_t color, double radius, double opacity = 1.0) {
        if (pixels == nullptr || rect.w <= 0 || rect.h <= 0) return;
        const Rect clip = visible();
        rect = intersect(rect, clip);
        if (rect.w <= 0 || rect.h <= 0) return;

        const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
        const int x1 = std::min(width, static_cast<int>(std::ceil(rect.x + rect.w)));
        const int y1 = std::min(height, static_cast<int>(std::ceil(rect.y + rect.h)));
        radius = clamp(radius, 0.0, std::min(rect.w, rect.h) / 2.0);

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const double distance =
                    round_rect_distance(x + 0.5, y + 0.5, rect, radius);
                const double coverage = clamp(0.5 - distance, 0.0, 1.0) * opacity;
                if (coverage <= 0.0) continue;
                blend_pixel(pixels[y * width + x], color, coverage);
            }
        }
    }

    void stroke_rect(Rect rect, std::uint32_t color, double radius, double thickness) {
        if (pixels == nullptr || rect.w <= 0 || rect.h <= 0 || thickness <= 0) return;
        const Rect clip = visible();
        const Rect bounds = intersect(
            Rect{rect.x - thickness - 1, rect.y - thickness - 1,
                 rect.w + 2 * thickness + 2, rect.h + 2 * thickness + 2},
            clip);
        if (bounds.w <= 0 || bounds.h <= 0) return;

        const int x0 = std::max(0, static_cast<int>(std::floor(bounds.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(bounds.y)));
        const int x1 = std::min(width, static_cast<int>(std::ceil(bounds.x + bounds.w)));
        const int y1 = std::min(height, static_cast<int>(std::ceil(bounds.y + bounds.h)));
        radius = clamp(radius, 0.0, std::min(rect.w, rect.h) / 2.0);

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const double distance =
                    round_rect_distance(x + 0.5, y + 0.5, rect, radius);
                const double coverage =
                    clamp(0.5 + thickness / 2.0 - std::fabs(distance), 0.0, 1.0);
                if (coverage <= 0.0) continue;
                blend_pixel(pixels[y * width + x], color, coverage);
            }
        }
    }

    finn_font* font(double px) {
        const int size = std::max(1, static_cast<int>(std::lround(px)));
        auto found = fonts.find(size);
        if (found != fonts.end()) return &found->second;
        const std::string path = font_path();
        if (path.empty()) {
            if (!font_warned) {
                std::fprintf(stderr, "libfinnui: no font found; text will be empty\n");
                font_warned = true;
            }
            return nullptr;
        }
        auto [it, inserted] = fonts.try_emplace(size);
        if (inserted && finn_font_load(&it->second, path.c_str(), size) != 0) {
            if (!font_warned) {
                std::fprintf(stderr, "libfinnui: cannot load %s; text will be empty\n",
                             path.c_str());
                font_warned = true;
            }
            fonts.erase(it);
            return nullptr;
        }
        return &it->second;
    }

    void draw_text(double x, double baseline, std::string_view value, double px,
                   Color color) {
        if (value.empty() || pixels == nullptr) return;
        finn_font* face = font(px);
        if (face == nullptr) return;
        const Rect clip = visible();
        if (baseline < clip.y - px || baseline > clip.y + clip.h + px) return;
        std::string text(value);
        finn_text_draw(face, pixels, width, height, x, baseline, text.c_str(),
                       color.argb);
    }

    const Icon& icon_for(std::string_view name, int w, int h, Color color,
                         bool tinted) {
        static const Icon empty;
        const std::string path = resolve_icon(name);
        if (path.empty()) return empty;
        char key[512];
        std::snprintf(key, sizeof key, "%s|%dx%d|%s|%08X", path.c_str(), w, h,
                      tinted ? "tint" : "raw", color.argb);
        auto found = icons.find(key);
        if (found != icons.end()) return found->second;

        fsvg svg{};
        if (fsvg_load(path.c_str(), &svg) != 0) return empty;
        Icon icon;
        icon.width = w;
        icon.height = h;
        icon.pixels.assign(static_cast<std::size_t>(w) * h, 0);
        std::uint32_t ink = color.argb;
        fsvg_raster(&svg, icon.pixels.data(), w, h, tinted ? fsvg_mono : nullptr,
                    tinted ? &ink : nullptr);
        fsvg_free(&svg);
        auto [it, inserted] = icons.emplace(key, std::move(icon));
        (void)inserted;
        return it->second;
    }

    void draw_icon(std::string_view name, Rect rect, Color color,
                   bool tinted = true) {
        const int w = std::max(1, static_cast<int>(std::lround(rect.w)));
        const int h = std::max(1, static_cast<int>(std::lround(rect.h)));
        const Icon& icon = icon_for(name, w, h, color, tinted);
        if (icon.pixels.empty()) return;
        const int ox = static_cast<int>(std::lround(rect.x));
        const int oy = static_cast<int>(std::lround(rect.y));
        const Rect clip = visible();
        for (int y = 0; y < h; ++y) {
            const int py = oy + y;
            if (py < 0 || py >= height) continue;
            if (py < clip.y || py >= clip.y + clip.h) continue;
            for (int x = 0; x < w; ++x) {
                const int px = ox + x;
                if (px < 0 || px >= width) continue;
                if (px < clip.x || px >= clip.x + clip.w) continue;
                const std::uint32_t src = icon.pixels[static_cast<std::size_t>(y) * w + x];
                if ((src >> 24) == 0) continue;
                blend_pixel(pixels[py * width + px], src, 1.0);
            }
        }
    }

    WidgetState& state_for(std::string_view id) {
        return states[std::string(id)];
    }

    bool pressed_in(const Rect& rect) const {
        return input != nullptr && input->pointer_pressed && rect.contains(input->x, input->y);
    }

    bool released_in(const Rect& rect) const {
        return input != nullptr && input->pointer_released && rect.contains(input->x, input->y);
    }
};

Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}
Canvas::~Canvas() = default;

Canvas::Canvas(std::uint32_t* pixels, int width, int height, double scale,
               Input& input, const Theme& theme)
    : impl_(std::make_unique<Impl>()) {
    begin(pixels, width, height, scale, input, theme);
}

void Canvas::begin(std::uint32_t* pixels, int width, int height, double scale,
                   Input& input, const Theme& theme) {
    impl_->pixels = pixels;
    impl_->width = width;
    impl_->height = height;
    impl_->scale = scale;
    impl_->input = &input;
    impl_->theme = &theme;
    impl_->clips.clear();
    impl_->clips.push_back(Rect{0, 0, static_cast<double>(width),
                                static_cast<double>(height)});
}

void Canvas::end() {
    impl_->pixels = nullptr;
    impl_->input = nullptr;
    impl_->theme = nullptr;
    impl_->clips.clear();
}

void Canvas::clear_transient() {}

void Canvas::clear(Color color) {
    if (impl_->pixels == nullptr) return;
    const std::size_t count = static_cast<std::size_t>(impl_->width) * impl_->height;
    if ((color.argb >> 24) == 0) {
        std::memset(impl_->pixels, 0, count * sizeof(std::uint32_t));
        return;
    }
    std::fill(impl_->pixels, impl_->pixels + count, color.argb);
}

void Canvas::fill(Rect rect, Color color, double radius) {
    impl_->fill_rect(rect, color.argb, radius);
}

void Canvas::stroke(Rect rect, Color color, double radius, double width) {
    impl_->stroke_rect(rect, color.argb, radius, width);
}

void Canvas::line(double x1, double y1, double x2, double y2, Color color,
                  double width) {
    if (impl_->pixels == nullptr) return;
    const Rect clip = impl_->visible();
    const double half = std::max(0.5, width / 2.0);
    const double min_x = std::max(clip.x, std::min(x1, x2) - half - 1);
    const double min_y = std::max(clip.y, std::min(y1, y2) - half - 1);
    const double max_x = std::min(clip.x + clip.w, std::max(x1, x2) + half + 1);
    const double max_y = std::min(clip.y + clip.h, std::max(y1, y2) + half + 1);
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double length_sq = dx * dx + dy * dy;
    for (int y = static_cast<int>(std::floor(min_y));
         y < static_cast<int>(std::ceil(max_y)); ++y) {
        if (y < 0 || y >= impl_->height) continue;
        for (int x = static_cast<int>(std::floor(min_x));
             x < static_cast<int>(std::ceil(max_x)); ++x) {
            if (x < 0 || x >= impl_->width) continue;
            const double px = x + 0.5;
            const double py = y + 0.5;
            double t = 0.0;
            if (length_sq > 0.0) {
                t = clamp(((px - x1) * dx + (py - y1) * dy) / length_sq, 0.0, 1.0);
            }
            const double cx = x1 + t * dx;
            const double cy = y1 + t * dy;
            const double distance = std::hypot(px - cx, py - cy);
            const double coverage = clamp(0.5 + half - distance, 0.0, 1.0);
            if (coverage <= 0.0) continue;
            blend_pixel(impl_->pixels[y * impl_->width + x], color.argb, coverage);
        }
    }
}

void Canvas::text(double x, double baseline, std::string_view value, double px,
                  Color color) {
    impl_->draw_text(x, baseline, value, px, color);
}

double Canvas::text_width(std::string_view value, double px) const {
    finn_font* face = const_cast<Impl*>(impl_.get())->font(px);
    if (face == nullptr || value.empty()) return 0;
    std::string text(value);
    return finn_text_width(face, text.c_str());
}

void Canvas::text_centered(Rect rect, std::string_view value, double px,
                           Color color) {
    const double width = text_width(value, px);
    finn_font* face = impl_->font(px);
    const double cap = face != nullptr ? finn_text_cap_height(face) : px * 0.7;
    const double x = rect.x + (rect.w - width) / 2.0;
    const double baseline = rect.y + (rect.h + cap) / 2.0;
    text(x, baseline, value, px, color);
}

void Canvas::icon(std::string_view path, Rect rect, Color color) {
    impl_->draw_icon(path, rect, color);
}

void Canvas::app_icon(std::string_view path, Rect rect) {
    impl_->draw_icon(path, rect, Color{0}, false);
}

void Canvas::push_clip(Rect rect) {
    if (impl_->clips.empty()) {
        impl_->clips.push_back(rect);
    } else {
        impl_->clips.push_back(intersect(impl_->clips.back(), rect));
    }
}

void Canvas::pop_clip() {
    if (impl_->clips.size() > 1) impl_->clips.pop_back();
}

const Input& Canvas::input() const { return *impl_->input; }
const Theme& Canvas::theme() const { return *impl_->theme; }
double Canvas::width() const { return impl_->width; }
double Canvas::height() const { return impl_->height; }

bool Canvas::button(std::string_view id, Rect rect, std::string_view label,
                    bool enabled) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = impl_->input->x >= rect.x && impl_->input->x < rect.x + rect.w &&
                         impl_->input->y >= rect.y && impl_->input->y < rect.y + rect.h;
    if (enabled && hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = hovered;
        state.active = false;
    }

    const Color background = !enabled
        ? impl_->theme->tile.with_alpha(120)
        : (state.active ? impl_->theme->tile_on : (hovered ? impl_->theme->tile_on : impl_->theme->tile));
    impl_->fill_rect(rect, background.argb, 8);
    impl_->stroke_rect(rect, impl_->theme->hair.argb, 8, 1);
    const Color ink = enabled ? impl_->theme->ink : impl_->theme->mute;
    text_centered(rect, label, 14, ink);
    return clicked;
}

bool Canvas::icon_button(std::string_view id, Rect rect, std::string_view icon,
                         Color ink) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = hovered;
        state.active = false;
    }
    if (state.active || hovered) {
        impl_->fill_rect(rect, impl_->theme->tile_on.argb, 8);
    }
    const double inset = std::min(rect.w, rect.h) * 0.22;
    impl_->draw_icon(icon, rect.inset(inset), ink);
    return clicked;
}

bool Canvas::app_icon_button(std::string_view id, Rect rect,
                             std::string_view icon) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = hovered;
        state.active = false;
    }
    if (state.active || hovered) {
        impl_->fill_rect(rect, impl_->theme->tile_on.argb, 8);
    }
    const double inset = std::min(rect.w, rect.h) * 0.08;
    impl_->draw_icon(icon, rect.inset(inset), Color{0}, false);
    return clicked;
}

bool Canvas::toggle(std::string_view id, Rect rect, std::string_view label,
                    bool& value) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (hovered && impl_->input->pointer_pressed) state.active = true;
    bool changed = false;
    if (state.active && impl_->input->pointer_released) {
        if (hovered) {
            value = !value;
            changed = true;
        }
        state.active = false;
    }

    const double switch_w = 44;
    const double switch_h = 24;
    const Rect track{rect.x + rect.w - switch_w, rect.y + (rect.h - switch_h) / 2,
                     switch_w, switch_h};
    impl_->fill_rect(track, (value ? impl_->theme->accent : impl_->theme->track).argb,
                     switch_h / 2);
    const double knob = switch_h - 6;
    const double knob_x = value ? track.x + track.w - knob - 3 : track.x + 3;
    impl_->fill_rect(Rect{knob_x, track.y + 3, knob, knob}, impl_->theme->knob.argb,
                     knob / 2);
    finn_font* face = impl_->font(14);
    const double cap = face != nullptr ? finn_text_cap_height(face) : 10;
    impl_->draw_text(rect.x, rect.y + (rect.h + cap) / 2, label, 14,
                     impl_->theme->ink);
    return changed;
}

bool Canvas::slider(std::string_view id, Rect rect, double& value, double min,
                    double max) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (hovered && impl_->input->pointer_pressed) state.active = true;
    if (state.active && !impl_->input->pointer_down && impl_->input->pointer_released) {
        state.active = false;
    }
    const double track_h = 4;
    const Rect track{rect.x, rect.y + (rect.h - track_h) / 2, rect.w, track_h};
    const double span = std::max(0.0001, max - min);
    double normalized = clamp((value - min) / span, 0.0, 1.0);
    bool changed = false;
    if (state.active && impl_->input->pointer_down) {
        normalized = clamp((impl_->input->x - rect.x) / rect.w, 0.0, 1.0);
        const double next = min + normalized * span;
        if (std::fabs(next - value) > 0.0001) {
            value = next;
            changed = true;
        }
    }
    impl_->fill_rect(track, impl_->theme->track.argb, track_h / 2);
    impl_->fill_rect(Rect{track.x, track.y, track.w * normalized, track_h},
                     impl_->theme->accent.argb, track_h / 2);
    const double knob = 16;
    const double knob_x = rect.x + normalized * (rect.w - knob);
    impl_->fill_rect(Rect{knob_x, rect.y + (rect.h - knob) / 2, knob, knob},
                     impl_->theme->knob.argb, knob / 2);
    return changed;
}

bool Canvas::text_field(std::string_view id, Rect rect, std::string& value,
                        std::string_view placeholder) {
    const std::string key(id);
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (impl_->input->pointer_pressed) {
        if (hovered) {
            impl_->focus = key;
            state.active = true;
        } else if (impl_->focus == key) {
            impl_->focus.clear();
            state.active = false;
        }
    }
    const bool focused = impl_->focus == key;
    if (focused) {
        if (!impl_->input->text.empty()) {
            value += impl_->input->text;
            impl_->input->text.clear();
        }
        for (const KeyEvent& event : impl_->input->keys) {
            if (event.key == Key::backspace && !value.empty()) {
                while (!value.empty() &&
                       (static_cast<unsigned char>(value.back()) & 0xC0u) == 0x80u) {
                    value.pop_back();
                }
                if (!value.empty()) value.pop_back();
            } else if (event.key == Key::enter) {
                return true;
            }
        }
    }

    impl_->fill_rect(rect, impl_->theme->tile.argb, 8);
    impl_->stroke_rect(rect, focused ? impl_->theme->accent.argb : impl_->theme->hair.argb,
                       8, focused ? 2 : 1);
    const Rect inner = rect.inset(12);
    const double px = 14;
    if (value.empty() && !focused) {
        impl_->draw_text(inner.x, rect.y + rect.h / 2 + 5, placeholder, px,
                         impl_->theme->mute);
    } else {
        push_clip(inner);
        impl_->draw_text(inner.x, rect.y + rect.h / 2 + 5, value, px,
                         impl_->theme->ink);
        if (focused) {
            const double caret = inner.x + text_width(value, px) + 1;
            impl_->fill_rect(Rect{caret, rect.y + 12, 1.5, rect.h - 24},
                             impl_->theme->accent.argb, 0);
        }
        pop_clip();
    }
    return false;
}

bool Canvas::list_item(std::string_view id, Rect rect, std::string_view title,
                       std::string_view detail, bool selected) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = hovered;
        state.active = false;
    }
    if (selected || hovered || state.active) {
        impl_->fill_rect(rect, (selected ? impl_->theme->tile_on : impl_->theme->tile).argb, 8);
    }
    impl_->draw_text(rect.x + 12, rect.y + 24, title, 15, impl_->theme->ink);
    if (!detail.empty()) {
        impl_->draw_text(rect.x + 12, rect.y + 44, detail, 11, impl_->theme->mute);
    }
    return clicked;
}

bool Canvas::list_item_icon(std::string_view id, Rect rect, std::string_view icon,
                            std::string_view title, std::string_view detail,
                            bool selected, bool enabled) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (enabled && hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = enabled && hovered;
        state.active = false;
    }
    if (selected || (enabled && (hovered || state.active))) {
        impl_->fill_rect(rect, (selected ? impl_->theme->tile_on : impl_->theme->tile).argb, 8);
    }
    const Color ink = enabled ? impl_->theme->ink : impl_->theme->mute;
    impl_->draw_icon(icon, Rect{rect.x + 12, rect.y + (rect.h - 26) / 2, 26, 26}, ink);
    impl_->draw_text(rect.x + 50, rect.y + 23, title, 14, ink);
    if (!detail.empty()) {
        impl_->draw_text(rect.x + 50, rect.y + 40, detail, 10,
                         enabled ? impl_->theme->mute : impl_->theme->mute.with_alpha(120));
    }
    return clicked;
}

bool Canvas::app_list_item(std::string_view id, Rect rect, std::string_view icon,
                           std::string_view title, std::string_view detail,
                           bool enabled) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (enabled && hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = enabled && hovered;
        state.active = false;
    }
    if (enabled && (hovered || state.active)) {
        impl_->fill_rect(rect, impl_->theme->tile_on.argb, 8);
    }
    impl_->draw_icon(icon, Rect{rect.x + 12, rect.y + (rect.h - 34) / 2, 34, 34},
                     Color{0}, false);
    impl_->draw_text(rect.x + 56, rect.y + 25, title, 14,
                     enabled ? impl_->theme->ink : impl_->theme->mute);
    if (!detail.empty()) {
        impl_->draw_text(rect.x + 56, rect.y + 42, detail, 10, impl_->theme->mute);
    }
    return clicked;
}

}  // namespace finn::ui
