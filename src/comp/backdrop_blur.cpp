#include "comp/backdrop_blur.hpp"

#include "comp/internal.hpp"

#include <drm_fourcc.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace finn::comp::backdrop_blur {
namespace {

bool is_glass(const char* ns) {
    return ns &&
           (std::strncmp(ns, "finn-shell-", 11) == 0 ||
            std::strncmp(ns, "finn-phone-statusbar", 20) == 0 ||
            std::strncmp(ns, "finn-notifications", 18) == 0);
}

// A Figma blur radius r corresponds to sigma ~= 0.43*r. Three square box
// passes of half-width b yield sigma ~= b + 0.5, so b = 0.43*r matches the
// token's visual strength.
struct BlurPlan {
    int box_r;
    int margin;
};

BlurPlan plan_for(int radius) {
    const int box_r = std::max(2, static_cast<int>(radius * 0.43));
    return {box_r, 3 * box_r};
}

// Correct edge-clamped box blur via an integral image; three passes converge
// on a Gaussian. Input/output are packed ARGB8888.
void box_blur_integral(const uint8_t* src, uint8_t* dst, int w, int h, int r) {
    const size_t plane = static_cast<size_t>(w + 1) * (h + 1);
    std::vector<int> integral(plane * 3, 0);
    auto at = [&](int x, int y, int c) -> int& {
        return integral[(static_cast<size_t>(y) * (w + 1) + x) * 3 + c];
    };
    for (int y = 0; y < h; ++y) {
        int row[3] = {0, 0, 0};
        for (int x = 0; x < w; ++x) {
            const uint8_t* px = src + (static_cast<size_t>(y) * w + x) * 4;
            for (int c = 0; c < 3; ++c) {
                row[c] += px[c];
                at(x + 1, y + 1, c) = at(x + 1, y, c) + row[c];
            }
        }
    }
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - r);
        const int y1 = std::min(h - 1, y + r);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - r);
            const int x1 = std::min(w - 1, x + r);
            const int count = (x1 - x0 + 1) * (y1 - y0 + 1);
            uint8_t* out = dst + (static_cast<size_t>(y) * w + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const int sum = at(x1 + 1, y1 + 1, c) - at(x0, y1 + 1, c) -
                                at(x1 + 1, y0, c) + at(x0, y0, c);
                out[c] = static_cast<uint8_t>(sum / count);
            }
            out[3] = 255;
        }
    }
}

// Read the real backdrop around `region`, blur it, then mask it with the glass
// surface's own alpha. The layer surface buffer contains the pill/card shapes
// on a fully transparent background (with antialiased, rounded edges), so its
// alpha is exactly the mask we need: the backdrop is blurred only inside the
// visible glass, with the correct corner geometry, and nowhere else.
std::vector<uint8_t> masked_blurred_region(
    const uint8_t* data, uint32_t format, size_t stride, int buf_w, int buf_h,
    const wlr_box& region, const BlurPlan& plan,
    const uint8_t* mask, size_t mask_stride, int mask_x, int mask_y) {
    std::vector<uint8_t> empty;
    if (format != DRM_FORMAT_ARGB8888 && format != DRM_FORMAT_XRGB8888) return empty;
    const int m = plan.margin;
    const int x0 = std::max(0, region.x - m);
    const int y0 = std::max(0, region.y - m);
    const int x1 = std::min(buf_w, region.x + region.width + m);
    const int y1 = std::min(buf_h, region.y + region.height + m);
    if (x1 <= x0 || y1 <= y0) return empty;
    const int w = x1 - x0;
    const int h = y1 - y0;

    std::vector<uint8_t> a(static_cast<size_t>(w) * h * 4);
    std::vector<uint8_t> b(a.size());
    for (int y = 0; y < h; ++y) {
        std::memcpy(&a[static_cast<size_t>(y) * w * 4],
                    data + static_cast<size_t>(y0 + y) * stride + x0 * 4,
                    static_cast<size_t>(w) * 4);
    }
    for (int pass = 0; pass < 3; ++pass) {
        box_blur_integral(a.data(), b.data(), w, h, plan.box_r);
        a.swap(b);
    }

    const int cx = region.x - x0;
    const int cy = region.y - y0;
    std::vector<uint8_t> out(static_cast<size_t>(region.width) * region.height * 4);
    for (int y = 0; y < region.height; ++y) {
        uint8_t* dst = &out[static_cast<size_t>(y) * region.width * 4];
        const uint8_t* blurred = &a[static_cast<size_t>(cy + y) * w * 4 + cx * 4];
        const uint8_t* alpha_row = mask
            ? mask + static_cast<size_t>(mask_y + y) * mask_stride + mask_x * 4
            : nullptr;
        for (int x = 0; x < region.width; ++x) {
            const int alpha = alpha_row ? alpha_row[x * 4 + 3] : 255;
            dst[x * 4 + 3] = static_cast<uint8_t>(alpha);
            for (int c = 0; c < 3; ++c) {
                dst[x * 4 + c] = static_cast<uint8_t>(blurred[x * 4 + c] * alpha / 255);
            }
        }
    }
    return out;
}

// Find the largest scene buffer under a layer surface tree. Qt composites a
// whole layer-shell surface into one buffer, so this is the glass surface's
// own pixels (and its alpha mask).
void find_main_buffer(wlr_scene_node* node, wlr_scene_buffer** best, int* best_area) {
    if (!node->enabled) return;
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        wlr_scene_buffer* sb = wlr_scene_buffer_from_node(node);
        if (sb && sb->buffer) {
            const int area = sb->buffer->width * sb->buffer->height;
            if (area > *best_area) {
                *best_area = area;
                *best = sb;
            }
        }
        return;
    }
    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_node* child;
        wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
            find_main_buffer(child, best, best_area);
        }
    }
}

// Re-draw a hidden glass subtree into a render pass. The scene's
// surface->current.buffer is not populated while the node is hidden, so the
// wlr_buffer held by each wlr_scene_buffer node is used instead.
void composite_scene_node(wlr_render_pass* pass, wlr_scene_node* node,
                          wlr_renderer* renderer, wlr_scene_output* out,
                          std::vector<wlr_texture*>& keepalive) {
    if (!node->enabled) return;
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        wlr_scene_buffer* sb = wlr_scene_buffer_from_node(node);
        if (!sb || !sb->buffer) return;
        wlr_texture* tex = wlr_texture_from_buffer(renderer, sb->buffer);
        if (!tex) return;
        int sx = 0, sy = 0;
        wlr_scene_node_coords(&sb->node, &sx, &sy);
        wlr_render_texture_options opts{};
        opts.texture = tex;
        opts.dst_box = wlr_box{
            sx - out->x, sy - out->y,
            sb->dst_width > 0 ? sb->dst_width : sb->buffer->width,
            sb->dst_height > 0 ? sb->dst_height : sb->buffer->height,
        };
        opts.alpha = &sb->opacity;
        wlr_render_pass_add_texture(pass, &opts);
        keepalive.push_back(tex);  // the pass reads textures at submit time
        return;
    }
    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_node* child;
        wl_list_for_each(child, &wlr_scene_tree_from_node(node)->children, link) {
            composite_scene_node(pass, child, renderer, out, keepalive);
        }
    }
}

}  // namespace

int radius_for_namespace(const char* name_space) {
    if (!is_glass(name_space)) return 0;
    if (std::strncmp(name_space, "finn-notifications", 18) == 0) {
        return kNotificationRadius;
    }
    return kGlassRadius;
}

void report_once() {
    static bool reported = false;
    if (reported) return;
    reported = true;
    std::fprintf(stderr,
                 "finn-comp: CPU backdrop blur active: glass %d px, notification %d px\n",
                 kGlassRadius, kNotificationRadius);
}

}  // namespace finn::comp::backdrop_blur

bool finn::Server::commit_output_with_backdrop_blur(
    finn::Server::OutputDevice* od) {
    namespace bb = finn::comp::backdrop_blur;

    // Step 1: isolate the glass layer surfaces.
    std::vector<LayerSurface*> glass;
    for (const auto& ls : layers_) {
        LayerSurface* l = ls.get();
        if (!l->mapped || !l->scene_layer || !l->layer_surface ||
            !l->layer_surface->surface) continue;
        if (bb::radius_for_namespace(l->layer_surface->finn_wlr_namespace_) == 0) {
            continue;
        }
        glass.push_back(l);
    }
    if (glass.empty()) return wlr_scene_output_commit(od->scene_output, nullptr);

    // Step 2: render the scene with the glass removed. The output buffer now
    // holds the true backdrop: wallpaper, lower layers and app windows.
    for (LayerSurface* l : glass) {
        wlr_scene_node_set_enabled(&l->scene_layer->tree->node, false);
    }
    wlr_output_state state;
    wlr_output_state_init(&state);
    const bool built = wlr_scene_output_build_state(od->scene_output, &state, nullptr);
    for (LayerSurface* l : glass) {
        wlr_scene_node_set_enabled(&l->scene_layer->tree->node, true);
    }
    if (!built || !state.buffer) {
        wlr_output_state_finish(&state);
        return wlr_scene_output_commit(od->scene_output, nullptr);
    }

    // Step 3: for each glass surface, read its own alpha mask (the pill/card
    // shape) and blur only those pixels of the real backdrop.
    struct Blurred {
        wlr_box rect;
        std::vector<uint8_t> pixels;
    };
    std::vector<Blurred> blurred;
    const int buf_w = state.buffer->width;
    const int buf_h = state.buffer->height;
    pixman_image_t* out_image = wlr_renderer_is_pixman(renderer_)
        ? wlr_pixman_renderer_get_buffer_image(renderer_, state.buffer)
        : nullptr;
    if (out_image) {
        const uint8_t* backdrop =
            reinterpret_cast<const uint8_t*>(pixman_image_get_data(out_image));
        const size_t backdrop_stride =
            static_cast<size_t>(pixman_image_get_stride(out_image));
        const auto pf = pixman_image_get_format(out_image);
        const uint32_t format = (pf == PIXMAN_a8r8g8b8) ? DRM_FORMAT_ARGB8888
                                                        : DRM_FORMAT_XRGB8888;
        for (LayerSurface* l : glass) {
            wlr_scene_buffer* sb = nullptr;
            int area = 0;
            finn::comp::backdrop_blur::find_main_buffer(
                &l->scene_layer->tree->node, &sb, &area);
            if (!sb || !sb->buffer) continue;

            pixman_image_t* mask_image =
                wlr_pixman_renderer_get_buffer_image(renderer_, sb->buffer);
            if (!mask_image) continue;
            const uint8_t* mask =
                reinterpret_cast<const uint8_t*>(pixman_image_get_data(mask_image));
            const size_t mask_stride =
                static_cast<size_t>(pixman_image_get_stride(mask_image));

            int sx = 0, sy = 0;
            wlr_scene_node_coords(&sb->node, &sx, &sy);
            const wlr_box surface_rect{
                sx - od->scene_output->x, sy - od->scene_output->y,
                sb->dst_width > 0 ? sb->dst_width : sb->buffer->width,
                sb->dst_height > 0 ? sb->dst_height : sb->buffer->height,
            };
            wlr_box region = surface_rect;
            region.x = std::max(0, std::min(region.x, buf_w - 1));
            region.y = std::max(0, std::min(region.y, buf_h - 1));
            region.width = std::min(region.width, buf_w - region.x);
            region.height = std::min(region.height, buf_h - region.y);
            if (region.width <= 0 || region.height <= 0) continue;

            const int radius =
                bb::radius_for_namespace(l->layer_surface->finn_wlr_namespace_);
            auto image = finn::comp::backdrop_blur::masked_blurred_region(
                backdrop, format, backdrop_stride, buf_w, buf_h, region,
                bb::plan_for(radius), mask, mask_stride,
                region.x - surface_rect.x, region.y - surface_rect.y);
            if (!image.empty()) blurred.push_back({region, std::move(image)});
        }
    }

    // Step 4: paint the masked, blurred backdrop back and then re-draw the
    // glass surfaces themselves on top, both in one render pass.
    if (!blurred.empty()) {
        wlr_render_pass* pass =
            wlr_renderer_begin_buffer_pass(renderer_, state.buffer, nullptr);
        std::vector<wlr_texture*> keepalive;
        if (pass) {
            for (const Blurred& b : blurred) {
                wlr_texture* tex = wlr_texture_from_pixels(
                    renderer_, DRM_FORMAT_ARGB8888,
                    static_cast<uint32_t>(b.rect.width) * 4,
                    b.rect.width, b.rect.height, b.pixels.data());
                if (!tex) continue;
                wlr_render_texture_options opts{};
                opts.texture = tex;
                opts.dst_box = b.rect;
                wlr_render_pass_add_texture(pass, &opts);
                keepalive.push_back(tex);
            }
            for (LayerSurface* l : glass) {
                finn::comp::backdrop_blur::composite_scene_node(
                    pass, &l->scene_layer->tree->node, renderer_, od->scene_output,
                    keepalive);
            }
            wlr_render_pass_submit(pass);
        }
        for (wlr_texture* tex : keepalive) wlr_texture_destroy(tex);
    }

    const bool committed = wlr_output_commit_state(od->output, &state);
    wlr_output_state_finish(&state);
    bb::report_once();
    return committed;
}
