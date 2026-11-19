#include "comp/internal.hpp"
#include "comp/backdrop_blur.hpp"

#include <algorithm>
#include <array>

namespace finn {
namespace {

bool layer_valid(uint32_t layer) { return layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; }

}  // namespace

void Server::arrange_layers() {
    wlr_output* output = primary_output();
    if (!output) return;

    int out_w = 0, out_h = 0;
    wlr_output_effective_resolution(output, &out_w, &out_h);
    wlr_box full_area{0, 0, out_w, out_h};
    wlr_box usable = full_area;

    static constexpr std::array<uint32_t, 4> kOrder = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };

    for (bool exclusive_only : {true, false}) {
        for (uint32_t target_layer : kOrder) {
            for (const auto& layer : layers_) {
                if (!layer->layer_surface || !layer->scene_layer) continue;
                const auto& st = layer->layer_surface->current;
                if (st.layer != target_layer) continue;
                const bool is_excl = st.exclusive_zone > 0;
                if (is_excl != exclusive_only) continue;
                wlr_scene_layer_surface_v1_configure(layer->scene_layer, &full_area,
                                                     &usable);
            }
        }
    }
    usable_area_ = usable;
    process_cursor_motion(monotonic_msec());
}

wlr_box Server::usable_area_for(wlr_output* output) const {
    if (usable_area_.width > 0 && usable_area_.height > 0) return usable_area_;
    wlr_output* out = output ? output : primary_output();
    if (!out) return wlr_box{0, 0, 1440, 900};
    int w = 0, h = 0;
    wlr_output_effective_resolution(out, &w, &h);
    return wlr_box{0, 0, w, h};
}

wlr_scene_tree* Server::parent_tree_for_xdg(wlr_surface* parent) const {
    if (!parent) return nullptr;
    if (wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(parent)) {
        return static_cast<wlr_scene_tree*>(xdg->data);
    }
    if (wlr_layer_surface_v1* l = wlr_layer_surface_v1_try_from_wlr_surface(parent)) {
        auto* layer = static_cast<LayerSurface*>(l->data);
        return layer && layer->scene_layer ? layer->scene_layer->tree : nullptr;
    }
    return nullptr;
}

void Server::create_popup(wlr_xdg_popup* popup, wlr_scene_tree* parent_tree) {
    if (!popup || !popup->base || !parent_tree) return;

    auto node = std::make_unique<PopupSurface>();
    PopupSurface* p = node.get();
    p->server = this;
    p->popup = popup;
    p->parent_tree = parent_tree;
    p->scene_tree = wlr_scene_xdg_surface_create(parent_tree, popup->base);
    popup->base->data = p->scene_tree;

    popups_.push_back(std::move(node));

    attach_listener(
        popup->base->surface->events.commit, p->commit_l,
        [](wl_listener* l, void*) {
            PopupSurface* ps = wl_container_of(l, ps, commit_l);
            if (ps->popup->base->initial_commit) {
                wlr_output* out = ps->server->primary_output();
                if (out) {
                    int w = 0, h = 0;
                    wlr_output_effective_resolution(out, &w, &h);
                    int lx = 0, ly = 0;
                    wlr_scene_node_coords(&ps->parent_tree->node, &lx, &ly);
                    wlr_box box{-lx, -ly, w, h};
                    wlr_xdg_popup_unconstrain_from_box(ps->popup, &box);
                }
                wlr_xdg_surface_schedule_configure(ps->popup->base);
            }
        });

    attach_listener(popup->events.destroy, p->destroy_l,
                    [](wl_listener* l, void*) {
                        PopupSurface* ps = wl_container_of(l, ps, destroy_l);
                        Server* s = ps->server;
                        s->popups_.remove_if(
                            [ps](const std::unique_ptr<PopupSurface>& it) {
                                return it.get() == ps;
                            });
                    });
}

void Server::on_new_layer_surface(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_layer_surface_);
    auto* layer_surface = static_cast<wlr_layer_surface_v1*>(data);

    if (!layer_surface->output) {
        layer_surface->output = server->primary_output();
        if (!layer_surface->output) {
            wlr_layer_surface_v1_destroy(layer_surface);
            return;
        }
    }

    const uint32_t raw_layer = static_cast<uint32_t>(layer_surface->pending.layer);
    const uint32_t layer_idx =
        layer_valid(raw_layer) ? raw_layer
                               : static_cast<uint32_t>(ZWLR_LAYER_SHELL_V1_LAYER_TOP);

    auto node = std::make_unique<LayerSurface>();
    LayerSurface* l = node.get();
    l->server = server;
    l->layer_surface = layer_surface;
    l->current_layer = layer_idx;
    l->scene_layer = wlr_scene_layer_surface_v1_create(
        server->layer_trees_[layer_idx], layer_surface);
    // Glass layer surfaces are picked up by the CPU backdrop blur pass in
    // backdrop_blur.cpp; radii are authoritative in backdrop_blur.hpp.
    comp::backdrop_blur::report_once();
    layer_surface->data = l;
    server->layers_.push_back(std::move(node));

    attach_listener(
        layer_surface->surface->events.commit, l->commit_l,
        [](wl_listener* cl, void*) {
            LayerSurface* ls = wl_container_of(cl, ls, commit_l);
            const auto& cur = ls->layer_surface->current;
            const uint32_t cur_layer = static_cast<uint32_t>(cur.layer);
            const uint32_t target =
                layer_valid(cur_layer) ? cur_layer
                                       : static_cast<uint32_t>(ZWLR_LAYER_SHELL_V1_LAYER_TOP);
            if (target != ls->current_layer) {
                wlr_scene_node_reparent(&ls->scene_layer->tree->node,
                                        ls->server->layer_trees_[target]);
                ls->current_layer = target;
            }
            if (ls->layer_surface->initial_commit || cur.committed != 0) {
                ls->server->arrange_layers();
            }
        });

    attach_listener(
        layer_surface->surface->events.map, l->map_l,
        [](wl_listener* ml, void*) {
            LayerSurface* ls = wl_container_of(ml, ls, map_l);
            ls->mapped = true;
            ls->server->arrange_layers();
            if (ls->layer_surface->current.keyboard_interactive ==
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
                wlr_keyboard* kb = wlr_seat_get_keyboard(ls->server->seat());
                if (kb) {
                    wlr_seat_keyboard_notify_enter(ls->server->seat(),
                                                   ls->layer_surface->surface,
                                                   kb->keycodes, kb->num_keycodes,
                                                   &kb->modifiers);
                }
            }
            ls->server->process_cursor_motion(Server::monotonic_msec());
        });

    attach_listener(
        layer_surface->surface->events.unmap, l->unmap_l,
        [](wl_listener* ul, void*) {
            LayerSurface* ls = wl_container_of(ul, ls, unmap_l);
            ls->mapped = false;
            ls->server->arrange_layers();
            if (ls->server->seat()->keyboard_state.focused_surface ==
                ls->layer_surface->surface) {
                ls->server->focus_top_window();
            }
            ls->server->process_cursor_motion(Server::monotonic_msec());
        });

    attach_listener(
        layer_surface->events.new_popup, l->new_popup_l,
        [](wl_listener* pl, void* pdata) {
            LayerSurface* ls = wl_container_of(pl, ls, new_popup_l);
            ls->server->create_popup(static_cast<wlr_xdg_popup*>(pdata),
                                     ls->scene_layer->tree);
        });

    attach_listener(
        layer_surface->events.destroy, l->destroy_l,
        [](wl_listener* dl, void*) {
            LayerSurface* ls = wl_container_of(dl, ls, destroy_l);
            Server* s = ls->server;
            const bool was_focused =
                s->seat()->keyboard_state.focused_surface == ls->layer_surface->surface;
            s->layers_.remove_if(
                [ls](const std::unique_ptr<LayerSurface>& it) { return it.get() == ls; });
            s->arrange_layers();
            if (was_focused) s->focus_top_window();
        });
}

}  // namespace finn
