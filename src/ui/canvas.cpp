// Software rendering canvas and immediate-mode widget toolkit for libfinnui.
// Rasterizes antialiased shapes, lines, glyphs, and WebP images with subpixel
// blending, while animating interactive controls via smooth approach physics.

#include "finn/ui.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <webp/decode.h>

extern "C" {
#include "raster/finntext.h"
#include "raster/finnvg.h"
}

namespace finn::ui {
namespace {

double clamp(double value, double low, double high) {
    return std::max(low, std::min(high, value));
}

Color mix(Color a, Color b, double t) {
    t = clamp(t, 0.0, 1.0);
    auto channel = [&](int shift) {
        const double av = static_cast<double>((a.argb >> shift) & 0xFFu);
        const double bv = static_cast<double>((b.argb >> shift) & 0xFFu);
        return static_cast<std::uint32_t>(av + (bv - av) * t + 0.5);
    };
    return Color{(channel(24) << 24) | (channel(16) << 16) |
                 (channel(8) << 8) | channel(0)};
}

Rect intersect(const Rect& a, const Rect& b) {
    const double x0 = std::max(a.x, b.x);
    const double y0 = std::max(a.y, b.y);
    const double x1 = std::min(a.x + a.w, b.x + b.w);
    const double y1 = std::min(a.y + a.h, b.y + b.h);
    return Rect{x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
}

// Computes signed distance from point to rounded rectangle boundary, allowing
// high-quality analytic coverage calculation for anti-aliased corners.
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

// Performs premultiplied-aware alpha compositing for RGBA/ARGB pixel buffers,
// ensuring smooth anti-aliased edge falloff against existing canvas contents.
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

std::string resolve_wallpaper() {
    if (const char* env = std::getenv("FINN_WALLPAPER"); env != nullptr && *env != '\0') {
        FILE* file = std::fopen(env, "rb");
        if (file != nullptr) {
            std::fclose(file);
            return env;
        }
    }
    const char* candidates[] = {
        "/usr/share/finn/wallpaper/default.webp",
        "assets/wallpaper/default.webp",
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

}  // namespace

struct Canvas::Impl {
    struct WidgetState {
        bool active = false;
        double hover = 0.0;
        double press = 0.0;
        double value = 0.0;
        double focus = 0.0;
    };

    struct Icon {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
    };

    struct Image {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
    };

    std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    double scale = 1.0;
    double delta = 0.016;
    double last_frame = 0.0;
    bool animating = false;
    Input* input = nullptr;
    const Theme* theme = nullptr;
    std::vector<Rect> clips;
    std::unordered_map<std::string, WidgetState> states;
    std::string focus;
    std::map<int, finn_font> fonts;
    std::unordered_map<std::string, Icon> icons;
    std::unordered_map<std::string, Image> images;
    std::unordered_map<std::string, std::vector<std::uint32_t>> blur_cache;
    double screen_w = 0;
    double screen_h = 0;
    double surface_x = 0;
    double surface_y = 0;
    bool font_warned = false;
    bool wallpaper_warned = false;

    void track(double& value, double target, double duration) {
        const double next = approach(value, target, delta, duration);
        if (std::fabs(next - value) > 0.002) animating = true;
        value = next;
    }

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

    const Image& image_for(std::string_view path) {
        static const Image empty;
        const std::string key(path);
        auto found = images.find(key);
        if (found != images.end()) return found->second;

        std::ifstream file(key, std::ios::binary);
        if (!file) return empty;
        std::vector<std::uint8_t> data(
            (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        int width = 0;
        int height = 0;
        if (data.empty() ||
            WebPGetInfo(data.data(), data.size(), &width, &height) == 0 ||
            width <= 0 || height <= 0) {
            return empty;
        }
        std::vector<std::uint8_t> rgba(
            static_cast<std::size_t>(width) * height * 4);
        if (WebPDecodeRGBAInto(data.data(), data.size(), rgba.data(), rgba.size(),
                               width * 4) == nullptr) {
            return empty;
        }
        Image image;
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<std::size_t>(width) * height);
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
            const std::uint32_t r = rgba[i * 4 + 0];
            const std::uint32_t g = rgba[i * 4 + 1];
            const std::uint32_t b = rgba[i * 4 + 2];
            const std::uint32_t a = rgba[i * 4 + 3];
            image.pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        auto [it, inserted] = images.emplace(key, std::move(image));
        (void)inserted;
        return it->second;
    }

    bool draw_image(std::string_view path, Rect rect, bool cover) {
        const Image& image = image_for(path);
        if (image.pixels.empty()) return false;
        const Rect clip = visible();
        rect = intersect(rect, clip);
        if (rect.w <= 0 || rect.h <= 0) return true;

        const double cover_scale = cover
            ? std::max(rect.w / image.width, rect.h / image.height)
            : std::min(rect.w / image.width, rect.h / image.height);
        const double src_w = rect.w / cover_scale;
        const double src_h = rect.h / cover_scale;
        const double src_x = (image.width - src_w) / 2.0;
        const double src_y = (image.height - src_h) / 2.0;

        const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
        const int x1 = std::min(width, static_cast<int>(std::ceil(rect.x + rect.w)));
        const int y1 = std::min(height, static_cast<int>(std::ceil(rect.y + rect.h)));
        for (int y = y0; y < y1; ++y) {
            const double v = src_y + (y - rect.y) / rect.h * src_h;
            const int sy = std::max(0, std::min(image.height - 1,
                                                 static_cast<int>(v)));
            for (int x = x0; x < x1; ++x) {
                const double u = src_x + (x - rect.x) / rect.w * src_w;
                const int sx = std::max(0, std::min(image.width - 1,
                                                    static_cast<int>(u)));
                const std::uint32_t src =
                    image.pixels[static_cast<std::size_t>(sy) * image.width + sx];
                blend_pixel(pixels[y * width + x], src, 1.0);
            }
        }
        return true;
    }

    void render_glass(Rect rect, double radius, Color tint, double blur_radius) {
        if (pixels == nullptr || rect.w <= 0 || rect.h <= 0) return;
        const double s = scale > 0.0 ? scale : 1.0;

        const std::string wp_path = resolve_wallpaper();
        const Image* img = nullptr;
        if (!wp_path.empty()) {
            const Image& loaded = image_for(wp_path);
            if (!loaded.pixels.empty()) {
                img = &loaded;
            }
        }

        if (img == nullptr) {
            if (!wallpaper_warned) {
                std::fprintf(stderr,
                             "libfinnui: cannot load wallpaper for backdrop blur; drawing tint only\n");
                wallpaper_warned = true;
            }
            fill_rect(Rect{rect.x * s, rect.y * s, rect.w * s, rect.h * s},
                      tint.argb, radius * s);
            return;
        }

        const double sw = screen_w > 0 ? screen_w : (width / s);
        const double sh = screen_h > 0 ? screen_h : (height / s);
        const double screen_rx = surface_x + rect.x;
        const double screen_ry = surface_y + rect.y;

        const int crop_w = std::max(1, static_cast<int>(std::lround(rect.w * s)));
        const int crop_h = std::max(1, static_cast<int>(std::lround(rect.h * s)));
        const int r_dev = std::max(1, static_cast<int>(std::lround(blur_radius * s)));
        const bool dark = theme != nullptr && theme->dark;

        char key[256];
        std::snprintf(key, sizeof key, "%dx%d|%.1f,%.1f|%.1f,%.1f|%s|%d|%d|%d",
                      crop_w, crop_h, screen_rx, screen_ry, sw, sh,
                      wp_path.c_str(), dark ? 1 : 0, static_cast<int>(s * 1000), r_dev);

        auto found = blur_cache.find(key);
        if (found == blur_cache.end()) {
            const double cover_scale =
                std::max(sw / img->width, sh / img->height);
            const double src_w = sw / cover_scale;
            const double src_h = sh / cover_scale;
            const double src_x = (img->width - src_w) / 2.0;
            const double src_y = (img->height - src_h) / 2.0;

            const int pad = std::min(r_dev * 3, 45);
            const int pw = crop_w + 2 * pad;
            const int ph = crop_h + 2 * pad;
            std::vector<std::uint32_t> buf(static_cast<std::size_t>(pw) * ph);

            for (int py = 0; py < ph; ++py) {
                const double sy = screen_ry + (py - pad + 0.5) / s;
                const double v = src_y + sy / cover_scale;
                const int iv = std::max(0, std::min(img->height - 1, static_cast<int>(std::floor(v))));
                const std::size_t row_offset = static_cast<std::size_t>(py) * pw;
                for (int px = 0; px < pw; ++px) {
                    const double sx = screen_rx + (px - pad + 0.5) / s;
                    const double u = src_x + sx / cover_scale;
                    const int iu = std::max(0, std::min(img->width - 1, static_cast<int>(std::floor(u))));
                    std::uint32_t p = img->pixels[static_cast<std::size_t>(iv) * img->width + iu];
                    if (dark) {
                        const std::uint32_t r = (((p >> 16) & 0xFFu) * 153u + 127u) / 255u;
                        const std::uint32_t g = (((p >> 8) & 0xFFu) * 153u + 127u) / 255u;
                        const std::uint32_t b = ((p & 0xFFu) * 153u + 127u) / 255u;
                        p = 0xFF000000u | (r << 16) | (g << 8) | b;
                    }
                    buf[row_offset + px] = p;
                }
            }

            std::vector<std::uint32_t> temp(buf.size());
            const int r = r_dev;
            const int div = 2 * r + 1;

            for (int pass = 0; pass < 3; ++pass) {
                // Horizontal pass: buf -> temp
                for (int y = 0; y < ph; ++y) {
                    const std::uint32_t* src_row = &buf[static_cast<std::size_t>(y) * pw];
                    std::uint32_t* dst_row = &temp[static_cast<std::size_t>(y) * pw];
                    std::int32_t sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
                    for (int i = -r; i <= r; ++i) {
                        const int xi = std::max(0, std::min(pw - 1, i));
                        const std::uint32_t p = src_row[xi];
                        sum_a += static_cast<std::int32_t>((p >> 24) & 0xFFu);
                        sum_r += static_cast<std::int32_t>((p >> 16) & 0xFFu);
                        sum_g += static_cast<std::int32_t>((p >> 8) & 0xFFu);
                        sum_b += static_cast<std::int32_t>(p & 0xFFu);
                    }
                    for (int x = 0; x < pw; ++x) {
                        const std::uint32_t a = static_cast<std::uint32_t>((sum_a + div / 2) / div);
                        const std::uint32_t r_ch = static_cast<std::uint32_t>((sum_r + div / 2) / div);
                        const std::uint32_t g = static_cast<std::uint32_t>((sum_g + div / 2) / div);
                        const std::uint32_t b = static_cast<std::uint32_t>((sum_b + div / 2) / div);
                        dst_row[x] = (a << 24) | (r_ch << 16) | (g << 8) | b;

                        const int x_left = std::max(0, std::min(pw - 1, x - r));
                        const int x_right = std::max(0, std::min(pw - 1, x + r + 1));
                        const std::uint32_t p_left = src_row[x_left];
                        const std::uint32_t p_right = src_row[x_right];
                        sum_a += static_cast<std::int32_t>((p_right >> 24) & 0xFFu) - static_cast<std::int32_t>((p_left >> 24) & 0xFFu);
                        sum_r += static_cast<std::int32_t>((p_right >> 16) & 0xFFu) - static_cast<std::int32_t>((p_left >> 16) & 0xFFu);
                        sum_g += static_cast<std::int32_t>((p_right >> 8) & 0xFFu) - static_cast<std::int32_t>((p_left >> 8) & 0xFFu);
                        sum_b += static_cast<std::int32_t>(p_right & 0xFFu) - static_cast<std::int32_t>(p_left & 0xFFu);
                    }
                }
                // Vertical pass: temp -> buf
                for (int x = 0; x < pw; ++x) {
                    std::int32_t sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
                    for (int i = -r; i <= r; ++i) {
                        const int yi = std::max(0, std::min(ph - 1, i));
                        const std::uint32_t p = temp[static_cast<std::size_t>(yi) * pw + x];
                        sum_a += static_cast<std::int32_t>((p >> 24) & 0xFFu);
                        sum_r += static_cast<std::int32_t>((p >> 16) & 0xFFu);
                        sum_g += static_cast<std::int32_t>((p >> 8) & 0xFFu);
                        sum_b += static_cast<std::int32_t>(p & 0xFFu);
                    }
                    for (int y = 0; y < ph; ++y) {
                        const std::uint32_t a = static_cast<std::uint32_t>((sum_a + div / 2) / div);
                        const std::uint32_t r_ch = static_cast<std::uint32_t>((sum_r + div / 2) / div);
                        const std::uint32_t g = static_cast<std::uint32_t>((sum_g + div / 2) / div);
                        const std::uint32_t b = static_cast<std::uint32_t>((sum_b + div / 2) / div);
                        buf[static_cast<std::size_t>(y) * pw + x] =
                            (a << 24) | (r_ch << 16) | (g << 8) | b;

                        const int y_left = std::max(0, std::min(ph - 1, y - r));
                        const int y_right = std::max(0, std::min(ph - 1, y + r + 1));
                        const std::uint32_t p_left = temp[static_cast<std::size_t>(y_left) * pw + x];
                        const std::uint32_t p_right = temp[static_cast<std::size_t>(y_right) * pw + x];
                        sum_a += static_cast<std::int32_t>((p_right >> 24) & 0xFFu) - static_cast<std::int32_t>((p_left >> 24) & 0xFFu);
                        sum_r += static_cast<std::int32_t>((p_right >> 16) & 0xFFu) - static_cast<std::int32_t>((p_left >> 16) & 0xFFu);
                        sum_g += static_cast<std::int32_t>((p_right >> 8) & 0xFFu) - static_cast<std::int32_t>((p_left >> 8) & 0xFFu);
                        sum_b += static_cast<std::int32_t>(p_right & 0xFFu) - static_cast<std::int32_t>(p_left & 0xFFu);
                    }
                }
            }

            std::vector<std::uint32_t> crop(static_cast<std::size_t>(crop_w) * crop_h);
            for (int y = 0; y < crop_h; ++y) {
                std::memcpy(&crop[static_cast<std::size_t>(y) * crop_w],
                            &buf[static_cast<std::size_t>(y + pad) * pw + pad],
                            static_cast<std::size_t>(crop_w) * sizeof(std::uint32_t));
            }
            auto res = blur_cache.emplace(key, std::move(crop));
            found = res.first;
        }

        const auto& blurred = found->second;
        const Rect rect_dev{rect.x * s, rect.y * s, rect.w * s, rect.h * s};
        const double rad_dev = clamp(radius * s, 0.0, std::min(rect_dev.w, rect_dev.h) / 2.0);
        const Rect clip = visible();
        const Rect bound = intersect(rect_dev, clip);
        if (bound.w <= 0 || bound.h <= 0) return;

        const int x0 = std::max(0, static_cast<int>(std::floor(bound.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(bound.y)));
        const int x1 = std::min(width, static_cast<int>(std::ceil(bound.x + bound.w)));
        const int y1 = std::min(height, static_cast<int>(std::ceil(bound.y + bound.h)));

        for (int y = y0; y < y1; ++y) {
            const int cy = y - static_cast<int>(std::floor(rect_dev.y));
            if (cy < 0 || cy >= crop_h) continue;
            for (int x = x0; x < x1; ++x) {
                const int cx = x - static_cast<int>(std::floor(rect_dev.x));
                if (cx < 0 || cx >= crop_w) continue;
                const double distance = round_rect_distance(x + 0.5, y + 0.5, rect_dev, rad_dev);
                const double coverage = clamp(0.5 - distance, 0.0, 1.0);
                if (coverage <= 0.0) continue;
                std::uint32_t pixel = blurred[static_cast<std::size_t>(cy) * crop_w + cx];
                blend_pixel(pixel, tint.argb, 1.0);
                blend_pixel(pixels[y * width + x], pixel, coverage);
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
    impl_->scale = scale > 0.0 ? scale : 1.0;
    impl_->input = &input;
    impl_->theme = &theme;
    impl_->clips.clear();
    impl_->clips.push_back(Rect{0, 0, static_cast<double>(width),
                                static_cast<double>(height)});

    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    impl_->delta = impl_->last_frame > 0.0
        ? clamp(now - impl_->last_frame, 0.0, 0.05)
        : 0.016;
    impl_->last_frame = now;
    impl_->animating = false;
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
    const double s = impl_->scale;
    impl_->fill_rect(Rect{rect.x * s, rect.y * s, rect.w * s, rect.h * s},
                     color.argb, radius * s);
}

void Canvas::stroke(Rect rect, Color color, double radius, double width) {
    const double s = impl_->scale;
    impl_->stroke_rect(Rect{rect.x * s, rect.y * s, rect.w * s, rect.h * s},
                       color.argb, radius * s, width * s);
}

void Canvas::line(double x1, double y1, double x2, double y2, Color color,
                  double width) {
    if (impl_->pixels == nullptr) return;
    const double s = impl_->scale;
    x1 *= s;
    y1 *= s;
    x2 *= s;
    y2 *= s;
    width *= s;
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
    const double s = impl_->scale;
    impl_->draw_text(x * s, baseline * s, value, px * s, color);
}

double Canvas::text_width(std::string_view value, double px) const {
    const double s = impl_->scale > 0.0 ? impl_->scale : 1.0;
    finn_font* face = const_cast<Impl*>(impl_.get())->font(px * s);
    if (face == nullptr || value.empty()) return 0;
    std::string text(value);
    return finn_text_width(face, text.c_str()) / s;
}

void Canvas::text_centered(Rect rect, std::string_view value, double px,
                           Color color) {
    const double width = text_width(value, px);
    const double s = impl_->scale > 0.0 ? impl_->scale : 1.0;
    finn_font* face = impl_->font(px * s);
    const double cap = face != nullptr ? (finn_text_cap_height(face) / s) : px * 0.7;
    const double x = rect.x + (rect.w - width) / 2.0;
    const double baseline = rect.y + (rect.h + cap) / 2.0;
    text(x, baseline, value, px, color);
}

void Canvas::icon(std::string_view path, Rect rect, Color color) {
    const double s = impl_->scale;
    impl_->draw_icon(path, Rect{rect.x * s, rect.y * s, rect.w * s, rect.h * s},
                     color, true);
}

void Canvas::app_icon(std::string_view path, Rect rect) {
    const double s = impl_->scale;
    impl_->draw_icon(path, Rect{rect.x * s, rect.y * s, rect.w * s, rect.h * s},
                     Color{0}, false);
}

bool Canvas::image(std::string_view path, Rect rect, bool cover) {
    const double s = impl_->scale;
    return impl_->draw_image(path, Rect{rect.x * s, rect.y * s, rect.w * s, rect.h * s},
                            cover);
}

void Canvas::glass(Rect rect, double radius, Color tint, double blur_radius) {
    impl_->render_glass(rect, radius, tint, blur_radius);
}

void Canvas::set_screen_geometry(double screen_w, double screen_h, double surface_x,
                                 double surface_y) {
    impl_->screen_w = screen_w;
    impl_->screen_h = screen_h;
    impl_->surface_x = surface_x;
    impl_->surface_y = surface_y;
}

void Canvas::push_clip(Rect rect) {
    const double s = impl_->scale;
    const Rect px_rect{rect.x * s, rect.y * s, rect.w * s, rect.h * s};
    if (impl_->clips.empty()) {
        impl_->clips.push_back(px_rect);
    } else {
        impl_->clips.push_back(intersect(impl_->clips.back(), px_rect));
    }
}

void Canvas::pop_clip() {
    if (impl_->clips.size() > 1) impl_->clips.pop_back();
}

const Input& Canvas::input() const { return *impl_->input; }
const Theme& Canvas::theme() const { return *impl_->theme; }
double Canvas::width() const {
    const double s = impl_->scale > 0.0 ? impl_->scale : 1.0;
    return static_cast<double>(impl_->width) / s;
}
double Canvas::height() const {
    const double s = impl_->scale > 0.0 ? impl_->scale : 1.0;
    return static_cast<double>(impl_->height) / s;
}
double Canvas::delta_time() const { return impl_->delta; }
bool Canvas::animating() const { return impl_->animating; }

// Tracks pointer press, hover, and release states to trigger clicks on button-up,
// animating subtle scale and accent color changes through approach tracking.
bool Canvas::button(std::string_view id, Rect rect, std::string_view label,
                    bool enabled) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (enabled && hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = enabled && hovered;
        state.active = false;
    }

    impl_->track(state.hover, enabled && hovered ? 1.0 : 0.0, kMotionFast);
    impl_->track(state.press, state.active ? 1.0 : 0.0, kMotionFast);
    const Rect body = rect.inset(state.press * 0.8);
    Color background = enabled ? mix(impl_->theme->tile, impl_->theme->tile_on,
                                     state.hover)
                               : impl_->theme->tile.with_alpha(120);
    if (enabled) {
        background = mix(background, impl_->theme->accent, state.press * 0.08);
    }
    fill(body, background, 8);
    stroke(body, impl_->theme->hair, 8, 1);
    const Color ink = enabled ? impl_->theme->ink : impl_->theme->mute;
    text_centered(body, label, 14, ink);
    return clicked;
}

bool Canvas::icon_button(std::string_view id, Rect rect, std::string_view icon_name,
                         Color ink) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = hovered;
        state.active = false;
    }
    impl_->track(state.hover, hovered ? 1.0 : 0.0, kMotionFast);
    impl_->track(state.press, state.active ? 1.0 : 0.0, kMotionFast);
    const double amount = std::max(state.hover, state.press);
    if (amount > 0.01) {
        fill(rect, impl_->theme->tile_on.with_alpha(
                       static_cast<std::uint8_t>(225 * amount)), 8);
    }
    const double inset = std::min(rect.w, rect.h) * 0.22;
    Rect glyph = rect.inset(inset);
    glyph.y -= state.hover * 1.0;
    icon(icon_name, glyph, ink);
    return clicked;
}

bool Canvas::app_icon_button(std::string_view id, Rect rect,
                             std::string_view icon_name) {
    Impl::WidgetState& state = impl_->state_for(id);
    const bool hovered = rect.contains(impl_->input->x, impl_->input->y);
    if (hovered && impl_->input->pointer_pressed) state.active = true;
    bool clicked = false;
    if (state.active && impl_->input->pointer_released) {
        clicked = hovered;
        state.active = false;
    }
    impl_->track(state.hover, hovered ? 1.0 : 0.0, kMotionFast);
    impl_->track(state.press, state.active ? 1.0 : 0.0, kMotionFast);
    const double amount = std::max(state.hover, state.press);
    if (amount > 0.01) {
        fill(rect, impl_->theme->tile_on.with_alpha(
                       static_cast<std::uint8_t>(225 * amount)), 8);
    }
    const double inset = std::min(rect.w, rect.h) * 0.08;
    Rect glyph = rect.inset(inset);
    glyph.y -= state.hover * 1.5;
    app_icon(icon_name, glyph);
    return clicked;
}

// Animates toggle knob translation and track color transitions smoothly across
// state flips rather than snapping instantly between boolean values.
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
    impl_->track(state.hover, hovered ? 1.0 : 0.0, kMotionFast);
    impl_->track(state.value, value ? 1.0 : 0.0, kMotionFast);
    if (state.hover > 0.01) {
        fill(rect.inset(4),
             impl_->theme->tile_on.with_alpha(
                 static_cast<std::uint8_t>(45 * state.hover)),
             8);
    }
    const Rect track{rect.x + rect.w - switch_w, rect.y + (rect.h - switch_h) / 2,
                     switch_w, switch_h};
    fill(track, mix(impl_->theme->track, impl_->theme->accent,
                    state.value), switch_h / 2);
    const double knob = switch_h - 6;
    const double knob_x = track.x + 3 + state.value * (track.w - knob - 6);
    fill(Rect{knob_x, track.y + 3, knob, knob}, impl_->theme->knob,
         knob / 2);
    const double s = impl_->scale > 0.0 ? impl_->scale : 1.0;
    finn_font* face = impl_->font(14 * s);
    const double cap = face != nullptr ? (finn_text_cap_height(face) / s) : 10;
    text(rect.x, rect.y + (rect.h + cap) / 2, label, 14,
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
    impl_->track(state.hover, hovered ? 1.0 : 0.0, kMotionFast);
    impl_->track(state.value, normalized, kMotionFast);
    if (state.active && impl_->input->pointer_down) state.value = normalized;
    const double shown = state.value;
    fill(track, impl_->theme->track, track_h / 2);
    fill(Rect{track.x, track.y, track.w * shown, track_h},
         impl_->theme->accent, track_h / 2);
    const double knob = 16 + state.hover * 2;
    const double knob_x = rect.x + shown * (rect.w - knob);
    fill(Rect{knob_x, rect.y + (rect.h - knob) / 2, knob, knob},
         impl_->theme->knob, knob / 2);
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

    fill(rect, impl_->theme->tile, 8);
    impl_->track(state.focus, focused ? 1.0 : 0.0, kMotionFast);
    stroke(rect, mix(impl_->theme->hair, impl_->theme->accent,
                     state.focus),
           8, 1 + state.focus);
    const Rect inner = rect.inset(12);
    const double px = 14;
    if (value.empty() && !focused) {
        text(inner.x, rect.y + rect.h / 2 + 5, placeholder, px,
             impl_->theme->mute);
    } else {
        push_clip(inner);
        text(inner.x, rect.y + rect.h / 2 + 5, value, px,
             impl_->theme->ink);
        if (focused) {
            const double caret = inner.x + text_width(value, px) + 1;
            fill(Rect{caret, rect.y + 12, 1.5, rect.h - 24},
                 impl_->theme->accent, 0);
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
    impl_->track(state.hover, hovered ? 1.0 : 0.0, kMotionFast);
    if (selected || state.hover > 0.01) {
        fill(rect, mix(impl_->theme->tile, impl_->theme->tile_on,
                       selected ? 1.0 : state.hover), 8);
    }
    text(rect.x + 12, rect.y + 24, title, 15, impl_->theme->ink);
    if (!detail.empty()) {
        text(rect.x + 12, rect.y + 44, detail, 11, impl_->theme->mute);
    }
    return clicked;
}

bool Canvas::list_item_icon(std::string_view id, Rect rect, std::string_view icon_name,
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
    impl_->track(state.hover, enabled && hovered ? 1.0 : 0.0, kMotionFast);
    if (selected || (enabled && state.hover > 0.01)) {
        fill(rect, mix(impl_->theme->tile, impl_->theme->tile_on,
                       selected ? 1.0 : state.hover), 8);
    }
    const Color ink = enabled ? impl_->theme->ink : impl_->theme->mute;
    icon(icon_name, Rect{rect.x + 12, rect.y + (rect.h - 26) / 2, 26, 26}, ink);
    text(rect.x + 50, rect.y + 23, title, 14, ink);
    if (!detail.empty()) {
        text(rect.x + 50, rect.y + 40, detail, 10,
             enabled ? impl_->theme->mute : impl_->theme->mute.with_alpha(120));
    }
    return clicked;
}

bool Canvas::app_list_item(std::string_view id, Rect rect, std::string_view icon_name,
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
    impl_->track(state.hover, enabled && hovered ? 1.0 : 0.0, kMotionFast);
    if (enabled && state.hover > 0.01) {
        fill(rect, mix(impl_->theme->tile, impl_->theme->tile_on,
                       state.hover), 8);
    }
    app_icon(icon_name, Rect{rect.x + 12, rect.y + (rect.h - 34) / 2, 34, 34});
    text(rect.x + 56, rect.y + 25, title, 14,
         enabled ? impl_->theme->ink : impl_->theme->mute);
    if (!detail.empty()) {
        text(rect.x + 56, rect.y + 42, detail, 10, impl_->theme->mute);
    }
    return clicked;
}

}  // namespace finn::ui
