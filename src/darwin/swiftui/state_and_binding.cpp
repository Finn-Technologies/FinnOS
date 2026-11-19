#include "state_and_binding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace finn::darwin::swiftui {

// These crate-private storage structs match the opaque forward declarations in
// the header.  They are defined at namespace scope (not in the anonymous
// namespace) so the public signatures refer to the same types.

struct Binding {
    StateBox* box = nullptr;
};

struct Publisher {
    struct Subscription {
        std::uint64_t id = 0;
        SinkFunction sink = nullptr;
        void* context = nullptr;
        bool active = true;
    };

    std::vector<Subscription> subscribers;
    std::uint64_t next_id = 1;
    std::vector<unsigned char> last_value;
    bool has_last = false;
    bool replay_last = false;
    mutable std::mutex mutex;
};

struct StateBox {
    unsigned char* value = nullptr;
    std::size_t size = 0;
    Binding* projection = nullptr;   // stable, created on first projectedValue read
    Publisher* observer = nullptr;   // owned by the box when non-null
    std::uint64_t generation = 0;
};

namespace {

// ---------------------------------------------------------------------------
// Process-wide registries
// ---------------------------------------------------------------------------

std::mutex& object_publishers_mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<void*, Publisher*>& object_publishers() {
    static std::unordered_map<void*, Publisher*> map;
    return map;
}

struct ViewRecord {
    ObjcId view = nullptr;
};

std::mutex& views_mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<void*, ViewRecord*>& views() {
    static std::unordered_map<void*, ViewRecord*> map;
    return map;
}

// A name is only added when the implementation exists.
void add_symbol(SymbolTable* table, const char* name, void* fn) {
    if (table && fn) (*table)[name] = fn;
}

// ---------------------------------------------------------------------------
// Objective-C helpers
// ---------------------------------------------------------------------------

ObjcClass* lookup_class(const char* name) {
    return finn_objc_getClass(name);
}

ObjcId class_object(ObjcClass* cls) {
    return reinterpret_cast<ObjcId>(cls);
}

bool responds(ObjcId object, const char* selector) {
    return object != nullptr &&
           finn_objc_respondsToSelector(object, finn_sel_registerName(selector));
}

// Make sure the view owns a real CALayer and return it.  The layer is created
// by NSView's own setWantsLayer: implementation; we never fabricate one.  If
// CoreGraphics is not initialised, no layer exists and we report failure.
ObjcId ensure_layer(ObjcId view) {
    if (!responds(view, "setWantsLayer:") || !lookup_class("CALayer")) return nullptr;
    objc_send_name<void>(view, "setWantsLayer:", static_cast<signed char>(1));
    ObjcId layer = objc_send_name<ObjcId>(view, "layer");
    return responds(layer, "setCornerRadius:") ? layer : nullptr;
}

// Allocate a bridged NSView (or subclass) with a real, initialised frame.
ObjcId make_view(const char* class_name, SwiftViewRect frame) {
    ObjcClass* cls = lookup_class(class_name);
    if (!cls) return nullptr;
    ObjcId view = finn_objc_alloc_object(cls, 0);
    if (!view) return nullptr;
    if (!responds(view, "initWithFrame:")) return view;
    return objc_send_name<ObjcId>(view, "initWithFrame:", frame);
}

void set_child_frame(ObjcId child, SwiftViewRect frame) {
    if (responds(child, "setFrame:")) {
        objc_send_name<void>(child, "setFrame:", frame);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// @State / @Binding implementation
// ---------------------------------------------------------------------------

StateBox* swiftui_state_create(const void* initial_value, std::size_t size) {
    auto* box = new (std::nothrow) StateBox();
    if (!box) return nullptr;
    if (size > 0) {
        box->value = static_cast<unsigned char*>(std::malloc(size));
        if (!box->value) {
            delete box;
            return nullptr;
        }
        if (initial_value) {
            std::memcpy(box->value, initial_value, size);
        } else {
            std::memset(box->value, 0, size);
        }
        box->size = size;
    }
    return box;
}

StateBox* swiftui_state_create_empty(std::size_t size) {
    return swiftui_state_create(nullptr, size);
}

void* swiftui_state_get(StateBox* box) {
    return box ? static_cast<void*>(box->value) : nullptr;
}

const void* swiftui_state_get(const StateBox* box) {
    return box ? static_cast<const void*>(box->value) : nullptr;
}

bool swiftui_state_set(StateBox* box, const void* value) {
    if (!box || !value || box->size == 0 || !box->value) return false;
    std::memcpy(box->value, value, box->size);
    ++box->generation;
    if (box->observer) {
        (void)swiftui_publisher_send_copy(box->observer, box->value, box->size);
    }
    return true;
}

std::uint64_t swiftui_state_generation(const StateBox* box) {
    return box ? box->generation : 0;
}

Binding* swiftui_state_projected(StateBox* box) {
    if (!box) return nullptr;
    if (!box->projection) {
        box->projection = new (std::nothrow) Binding();
        if (box->projection) box->projection->box = box;
    }
    return box->projection;
}

void swiftui_state_destroy(StateBox* box) {
    if (!box) return;
    if (box->observer) swiftui_publisher_destroy(box->observer);
    delete box->projection;
    std::free(box->value);
    delete box;
}

void* swiftui_binding_get(Binding* binding) {
    return binding ? swiftui_state_get(binding->box) : nullptr;
}

bool swiftui_binding_set(Binding* binding, const void* value) {
    return binding ? swiftui_state_set(binding->box, value) : false;
}

StateBox* swiftui_binding_box(const Binding* binding) {
    return binding ? binding->box : nullptr;
}

Publisher* swiftui_state_observe(StateBox* box, bool replay_last) {
    if (!box) return nullptr;
    if (!box->observer) box->observer = swiftui_publisher_create(replay_last);
    return box->observer;
}

// ---------------------------------------------------------------------------
// Combine implementation
// ---------------------------------------------------------------------------

Publisher* swiftui_publisher_create(bool replay_last) {
    auto* publisher = new (std::nothrow) Publisher();
    if (publisher) publisher->replay_last = replay_last;
    return publisher;
}

std::uint64_t swiftui_publisher_sink(Publisher* publisher, SinkFunction sink,
                                     void* context) {
    if (!publisher || !sink) return 0;
    Publisher::Subscription sub;
    sub.id = publisher->next_id++;
    sub.sink = sink;
    sub.context = context;

    // A current-value sink is called with the stored value.  Do it outside the
    // lock so a re-entrant send from the sink cannot deadlock.
    bool replay = false;
    {
        std::lock_guard<std::mutex> lock(publisher->mutex);
        publisher->subscribers.push_back(sub);
        replay = publisher->replay_last && publisher->has_last;
    }
    if (replay) {
        sink(context, publisher->last_value.empty() ? nullptr
                                                    : publisher->last_value.data());
    }
    return sub.id;
}

void swiftui_publisher_send(Publisher* publisher, void* value) {
    if (!publisher) return;
    std::vector<Publisher::Subscription> snapshot;
    {
        std::lock_guard<std::mutex> lock(publisher->mutex);
        snapshot = publisher->subscribers;
    }
    for (const Publisher::Subscription& sub : snapshot) {
        if (sub.active && sub.sink) sub.sink(sub.context, value);
    }
}

bool swiftui_publisher_send_copy(Publisher* publisher, const void* value,
                                 std::size_t size) {
    if (!publisher) return false;
    if (publisher->replay_last) {
        std::lock_guard<std::mutex> lock(publisher->mutex);
        if (value && size > 0) {
            const auto* bytes = static_cast<const unsigned char*>(value);
            publisher->last_value.assign(bytes, bytes + size);
            publisher->has_last = true;
        } else {
            publisher->last_value.clear();
            publisher->has_last = false;
        }
    }
    swiftui_publisher_send(publisher, const_cast<void*>(value));
    return true;
}

bool swiftui_publisher_cancel(Publisher* publisher, std::uint64_t subscription) {
    if (!publisher || subscription == 0) return false;
    std::lock_guard<std::mutex> lock(publisher->mutex);
    for (Publisher::Subscription& sub : publisher->subscribers) {
        if (sub.id == subscription) {
            sub.active = false;
            return true;
        }
    }
    return false;
}

std::uint64_t swiftui_publisher_subscriber_count(const Publisher* publisher) {
    if (!publisher) return 0;
    std::lock_guard<std::mutex> lock(publisher->mutex);
    std::uint64_t count = 0;
    for (const Publisher::Subscription& sub : publisher->subscribers) {
        if (sub.active) ++count;
    }
    return count;
}

void swiftui_publisher_destroy(Publisher* publisher) {
    delete publisher;
}

Publisher* swiftui_object_publisher(void* object) {
    if (!object) return nullptr;
    std::lock_guard<std::mutex> lock(object_publishers_mutex());
    auto& map = object_publishers();
    auto it = map.find(object);
    if (it != map.end()) return it->second;
    Publisher* publisher = swiftui_publisher_create(false);
    if (publisher) map.emplace(object, publisher);
    return publisher;
}

void swiftui_object_will_change(void* object, void* value) {
    swiftui_publisher_send(swiftui_object_publisher(object), value);
}

void swiftui_object_remove(void* object) {
    if (!object) return;
    std::lock_guard<std::mutex> lock(object_publishers_mutex());
    auto& map = object_publishers();
    auto it = map.find(object);
    if (it == map.end()) return;
    swiftui_publisher_destroy(it->second);
    map.erase(it);
}

// ---------------------------------------------------------------------------
// View tokens and modifiers
// ---------------------------------------------------------------------------

void* swiftui_view_token_create() {
    return new (std::nothrow) ViewRecord();
}

bool swiftui_bind_view(void* token, ObjcId view) {
    if (!token) return false;
    std::lock_guard<std::mutex> lock(views_mutex());
    auto& map = views();
    auto it = map.find(token);
    ViewRecord* record = (it == map.end()) ? nullptr : it->second;
    if (!record) {
        record = new (std::nothrow) ViewRecord();
        if (!record) return false;
        map.emplace(token, record);
    }
    record->view = view;
    return view != nullptr;
}

ObjcId swiftui_view_for_token(void* token) {
    if (!token) return nullptr;
    std::lock_guard<std::mutex> lock(views_mutex());
    auto it = views().find(token);
    return (it == views().end() || !it->second) ? nullptr : it->second->view;
}

void swiftui_view_token_destroy(void* token) {
    if (!token) return;
    std::lock_guard<std::mutex> lock(views_mutex());
    auto& map = views();
    auto it = map.find(token);
    if (it == map.end()) return;
    delete it->second;
    map.erase(it);
}

SwiftViewRect swiftui_view_frame(ObjcId view) {
    if (!responds(view, "frame")) return {0.0, 0.0, 0.0, 0.0};
    return objc_send_name<SwiftViewRect>(view, "frame");
}

bool swiftui_apply_padding(ObjcId view, double top, double leading,
                           double bottom, double trailing) {
    if (!responds(view, "setFrame:")) return false;
    SwiftViewRect frame = swiftui_view_frame(view);
    frame.x -= leading;
    frame.y -= bottom;
    frame.width += leading + trailing;
    frame.height += top + bottom;
    frame.width = std::max(0.0, frame.width);
    frame.height = std::max(0.0, frame.height);
    objc_send_name<void>(view, "setFrame:", frame);
    return true;
}

bool swiftui_apply_frame(ObjcId view, double width, double height) {
    if (!responds(view, "setFrame:")) return false;
    SwiftViewRect frame = swiftui_view_frame(view);
    if (std::isfinite(width) && width >= 0.0) frame.width = width;
    if (std::isfinite(height) && height >= 0.0) frame.height = height;
    objc_send_name<void>(view, "setFrame:", frame);
    return true;
}

bool swiftui_apply_background(ObjcId view, double red, double green,
                              double blue, double alpha) {
    ObjcId layer = ensure_layer(view);
    ObjcClass* color_class = lookup_class("NSColor");
    if (!layer || !color_class) return false;
    ObjcId color = objc_send_name<ObjcId>(class_object(color_class),
                                          "colorWithRed:green:blue:alpha:",
                                          red, green, blue, alpha);
    if (!color) return false;
    objc_send_name<void>(layer, "setBackgroundColor:", color);
    objc_send_name<void>(layer, "setMasksToBounds:", static_cast<signed char>(1));
    finn_objc_release(color);  // the layer retains it
    return true;
}

bool swiftui_apply_corner_radius(ObjcId view, double radius) {
    ObjcId layer = ensure_layer(view);
    if (!layer) return false;
    objc_send_name<void>(layer, "setCornerRadius:", radius);
    objc_send_name<void>(layer, "setMasksToBounds:", static_cast<signed char>(1));
    return true;
}

void* swiftui_modifier_padding(void* token, double top, double leading,
                               double bottom, double trailing) {
    ObjcId view = swiftui_view_for_token(token);
    return swiftui_apply_padding(view, top, leading, bottom, trailing) ? token : nullptr;
}

void* swiftui_modifier_frame(void* token, double width, double height) {
    ObjcId view = swiftui_view_for_token(token);
    return swiftui_apply_frame(view, width, height) ? token : nullptr;
}

void* swiftui_modifier_background(void* token, double red, double green,
                                  double blue, double alpha) {
    ObjcId view = swiftui_view_for_token(token);
    return swiftui_apply_background(view, red, green, blue, alpha) ? token : nullptr;
}

void* swiftui_modifier_corner_radius(void* token, double radius) {
    ObjcId view = swiftui_view_for_token(token);
    return swiftui_apply_corner_radius(view, radius) ? token : nullptr;
}

// ---------------------------------------------------------------------------
// Graphics: Color / Font
// ---------------------------------------------------------------------------

SwiftUIColor swiftui_color(double red, double green, double blue, double alpha) {
    return {red, green, blue, alpha};
}

SwiftUIColor swiftui_system_color(int index) {
    switch (index) {
        case 0:  return {0.0, 0.0, 0.0, 0.0};     // clear
        case 1:  return {0.0, 0.0, 0.0, 1.0};     // black
        case 2:  return {1.0, 1.0, 1.0, 1.0};     // white
        case 3:  return {1.0, 0.0, 0.0, 1.0};     // red
        case 4:  return {0.0, 1.0, 0.0, 1.0};     // green
        case 5:  return {0.0, 0.0, 1.0, 1.0};     // blue
        case 6:  return {1.0, 0.5, 0.0, 1.0};     // orange
        case 7:  return {1.0, 1.0, 0.0, 1.0};     // yellow
        case 8:  return {1.0, 0.25, 0.5, 1.0};    // pink
        case 9:  return {0.5, 0.0, 1.0, 1.0};     // purple
        default: return {0.5, 0.5, 0.5, 1.0};     // gray
    }
}

SwiftUIFont swiftui_system_font(double size, std::uint32_t traits) {
    return {size, traits};
}

ObjcId swiftui_color_to_nscolor(SwiftUIColor color) {
    ObjcClass* color_class = lookup_class("NSColor");
    if (!color_class) return nullptr;
    return objc_send_name<ObjcId>(class_object(color_class),
                                  "colorWithRed:green:blue:alpha:",
                                  color.red, color.green, color.blue, color.alpha);
}

ObjcId swiftui_font_to_nsfont(SwiftUIFont font) {
    ObjcClass* font_class = lookup_class("NSFont");
    if (!font_class) return nullptr;
    const char* selector = (font.traits & 1u) ? "boldSystemFontOfSize:"
                                              : "systemFontOfSize:";
    return objc_send_name<ObjcId>(class_object(font_class), selector, font.size);
}

// ---------------------------------------------------------------------------
// Containers backed by real bridged views
// ---------------------------------------------------------------------------

void* swiftui_container_empty_view() {
    ObjcId view = make_view("NSView", {0.0, 0.0, 0.0, 0.0});
    if (!view) return nullptr;
    void* token = swiftui_view_token_create();
    return swiftui_bind_view(token, view) ? token : nullptr;
}

void* swiftui_container_spacer() {
    // A spacer has zero intrinsic size; the stack layout assigns its frame.
    return swiftui_container_empty_view();
}

void* swiftui_container_divider() {
    ObjcId view = make_view("NSView", {0.0, 0.0, 1.0, 1.0});
    if (!view) return nullptr;
    (void)swiftui_apply_background(view, 0.6, 0.6, 0.6, 1.0);
    void* token = swiftui_view_token_create();
    return swiftui_bind_view(token, view) ? token : nullptr;
}

void* swiftui_container_group(void* content) {
    // Group is transparent: it contributes no layer of its own.  Reusing the
    // content token is the truthful representation.
    return swiftui_view_for_token(content) ? content : nullptr;
}

void* swiftui_container_scroll_view(void* content) {
    ObjcId content_view = swiftui_view_for_token(content);
    if (!content_view) return nullptr;
    ObjcId scroll = make_view("NSScrollView", {0.0, 0.0, 240.0, 160.0});
    if (!scroll) return nullptr;
    if (responds(scroll, "setDocumentView:")) {
        objc_send_name<void>(scroll, "setDocumentView:", content_view);
    }
    void* token = swiftui_view_token_create();
    return swiftui_bind_view(token, scroll) ? token : nullptr;
}

void* swiftui_container_list(void* const* children, std::size_t count) {
    ObjcId doc = make_view("NSView", {0.0, 0.0, 240.0,
                                      std::max(1.0, static_cast<double>(count) * 24.0)});
    if (!doc) return nullptr;
    double y = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        ObjcId child = swiftui_view_for_token(children[i]);
        if (!child) continue;
        SwiftViewRect frame = swiftui_view_frame(child);
        frame.x = 0.0;
        frame.y = y;
        if (frame.height <= 0.0) frame.height = 24.0;
        set_child_frame(child, frame);
        if (responds(doc, "addSubview:")) {
            objc_send_name<void>(doc, "addSubview:", child);
        }
        y += frame.height;
    }
    ObjcId scroll = make_view("NSScrollView", {0.0, 0.0, 240.0, 160.0});
    if (!scroll) return nullptr;
    if (responds(scroll, "setDocumentView:")) {
        objc_send_name<void>(scroll, "setDocumentView:", doc);
    }
    void* token = swiftui_view_token_create();
    return swiftui_bind_view(token, scroll) ? token : nullptr;
}

void* swiftui_container_zstack(void* const* children, std::size_t count) {
    ObjcId stack = make_view("NSView", {0.0, 0.0, 0.0, 0.0});
    if (!stack) return nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        ObjcId child = swiftui_view_for_token(children[i]);
        if (!child || !responds(stack, "addSubview:")) continue;
        objc_send_name<void>(stack, "addSubview:", child);
    }
    void* token = swiftui_view_token_create();
    return swiftui_bind_view(token, stack) ? token : nullptr;
}

// ---------------------------------------------------------------------------
// Swift-facing thunks.  All take and return scalars/pointers so they follow
// the Swift ABI on aarch64 with no hidden sret or aggregate-return registers.
// ---------------------------------------------------------------------------

extern "C" {

void* finn_swiftui_state_create(void* value, std::uint64_t size) {
    return swiftui_state_create(value, static_cast<std::size_t>(size));
}
void* finn_swiftui_state_get(void* box) {
    return swiftui_state_get(static_cast<StateBox*>(box));
}
void finn_swiftui_state_set(void* box, void* value) {
    (void)swiftui_state_set(static_cast<StateBox*>(box), value);
}
void* finn_swiftui_state_projected(void* box) {
    return swiftui_state_projected(static_cast<StateBox*>(box));
}
std::uint64_t finn_swiftui_state_generation(void* box) {
    return swiftui_state_generation(static_cast<const StateBox*>(box));
}
void finn_swiftui_state_destroy(void* box) {
    swiftui_state_destroy(static_cast<StateBox*>(box));
}
void* finn_swiftui_binding_get(void* binding) {
    return swiftui_binding_get(static_cast<Binding*>(binding));
}
void finn_swiftui_binding_set(void* binding, void* value) {
    (void)swiftui_binding_set(static_cast<Binding*>(binding), value);
}

void* finn_swiftui_publisher_create(std::uint64_t replay) {
    return swiftui_publisher_create(replay != 0);
}
std::uint64_t finn_swiftui_publisher_sink(void* publisher, void* sink, void* context) {
    return swiftui_publisher_sink(static_cast<Publisher*>(publisher),
                                  reinterpret_cast<SinkFunction>(sink), context);
}
void finn_swiftui_publisher_send(void* publisher, void* value) {
    swiftui_publisher_send(static_cast<Publisher*>(publisher), value);
}
std::uint64_t finn_swiftui_publisher_send_copy(void* publisher, void* value,
                                               std::uint64_t size) {
    return swiftui_publisher_send_copy(static_cast<Publisher*>(publisher), value,
                                       static_cast<std::size_t>(size)) ? 1u : 0u;
}
void finn_swiftui_publisher_cancel(void* publisher, std::uint64_t id) {
    (void)swiftui_publisher_cancel(static_cast<Publisher*>(publisher), id);
}
std::uint64_t finn_swiftui_publisher_count(void* publisher) {
    return swiftui_publisher_subscriber_count(static_cast<const Publisher*>(publisher));
}
void finn_swiftui_publisher_destroy(void* publisher) {
    swiftui_publisher_destroy(static_cast<Publisher*>(publisher));
}
void* finn_swiftui_object_publisher(void* object) {
    return swiftui_object_publisher(object);
}
void finn_swiftui_object_will_change(void* object, void* value) {
    swiftui_object_will_change(object, value);
}
void finn_swiftui_object_remove(void* object) {
    swiftui_object_remove(object);
}
void* finn_swiftui_state_observe(void* box, std::uint64_t replay) {
    return swiftui_state_observe(static_cast<StateBox*>(box), replay != 0);
}

void* finn_swiftui_view_token_create() {
    return swiftui_view_token_create();
}
void* finn_swiftui_bind_view(void* token, void* view) {
    return swiftui_bind_view(token, static_cast<ObjcId>(view)) ? token : nullptr;
}
void finn_swiftui_view_token_destroy(void* token) {
    swiftui_view_token_destroy(token);
}

void* finn_swiftui_padding(void* token, double top, double leading,
                           double bottom, double trailing) {
    return swiftui_modifier_padding(token, top, leading, bottom, trailing);
}
void* finn_swiftui_frame(void* token, double width, double height) {
    return swiftui_modifier_frame(token, width, height);
}
void* finn_swiftui_background(void* token, double red, double green,
                              double blue, double alpha) {
    return swiftui_modifier_background(token, red, green, blue, alpha);
}
void* finn_swiftui_corner_radius(void* token, double radius) {
    return swiftui_modifier_corner_radius(token, radius);
}

void* finn_swiftui_color_create(double red, double green, double blue, double alpha) {
    return new (std::nothrow) SwiftUIColor(swiftui_color(red, green, blue, alpha));
}
void* finn_swiftui_system_color(int index) {
    return new (std::nothrow) SwiftUIColor(swiftui_system_color(index));
}
void* finn_swiftui_color_nscolor(void* color) {
    if (!color) return nullptr;
    return swiftui_color_to_nscolor(*static_cast<const SwiftUIColor*>(color));
}
void* finn_swiftui_font_create(double size, std::uint64_t traits) {
    return new (std::nothrow) SwiftUIFont(
        swiftui_system_font(size, static_cast<std::uint32_t>(traits)));
}
void* finn_swiftui_font_nsfont(void* font) {
    if (!font) return nullptr;
    return swiftui_font_to_nsfont(*static_cast<const SwiftUIFont*>(font));
}
void finn_swiftui_color_destroy(void* color) {
    delete static_cast<SwiftUIColor*>(color);
}
void finn_swiftui_font_destroy(void* font) {
    delete static_cast<SwiftUIFont*>(font);
}

void* finn_swiftui_empty_view() { return swiftui_container_empty_view(); }
void* finn_swiftui_spacer() { return swiftui_container_spacer(); }
void* finn_swiftui_divider() { return swiftui_container_divider(); }
void* finn_swiftui_group(void* content) { return swiftui_container_group(content); }
void* finn_swiftui_scroll_view(void* content) {
    return swiftui_container_scroll_view(content);
}
void* finn_swiftui_list(void* const* children, std::uint64_t count) {
    return swiftui_container_list(children, static_cast<std::size_t>(count));
}
void* finn_swiftui_zstack(void* const* children, std::uint64_t count) {
    return swiftui_container_zstack(children, static_cast<std::size_t>(count));
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_state_and_binding(SymbolTable* table) {
    // C-ABI entry points with documented signatures (always real).
    add_symbol(table, "_finn_swiftui_state_create",
               reinterpret_cast<void*>(&finn_swiftui_state_create));
    add_symbol(table, "_finn_swiftui_state_get",
               reinterpret_cast<void*>(&finn_swiftui_state_get));
    add_symbol(table, "_finn_swiftui_state_set",
               reinterpret_cast<void*>(&finn_swiftui_state_set));
    add_symbol(table, "_finn_swiftui_state_projected",
               reinterpret_cast<void*>(&finn_swiftui_state_projected));
    add_symbol(table, "_finn_swiftui_state_generation",
               reinterpret_cast<void*>(&finn_swiftui_state_generation));
    add_symbol(table, "_finn_swiftui_state_destroy",
               reinterpret_cast<void*>(&finn_swiftui_state_destroy));
    add_symbol(table, "_finn_swiftui_binding_get",
               reinterpret_cast<void*>(&finn_swiftui_binding_get));
    add_symbol(table, "_finn_swiftui_binding_set",
               reinterpret_cast<void*>(&finn_swiftui_binding_set));

    // Canonical Swift property accessors, as aliases of the C box API.  `x` is
    // the first generic parameter (State<Value>.wrappedValue and
    // Binding<Value>.wrappedValue); `vg`/`vs` are the standard getter/setter
    // mangling suffixes.  The thunks implement our box-storage semantics with
    // scalar/pointer arguments and a pointer return in x0.  Swift's generic
    // value-lowering for an arbitrary Value, and its dedicated self register
    // (x20), are compiler-level conventions this C++ shim does not reproduce;
    // view bodies in this compatibility layer reach the accessors through the
    // C entry points above.
    add_symbol(table, "_$s7SwiftUI5StateV12wrappedValuexvg",
               reinterpret_cast<void*>(&finn_swiftui_state_get));
    add_symbol(table, "_$s7SwiftUI5StateV12wrappedValuexvs",
               reinterpret_cast<void*>(&finn_swiftui_state_set));
    add_symbol(table, "_$s7SwiftUI5StateV13projectedValueAA7BindingVyxGvg",
               reinterpret_cast<void*>(&finn_swiftui_state_projected));
    add_symbol(table, "_$s7SwiftUI7BindingV12wrappedValuexvg",
               reinterpret_cast<void*>(&finn_swiftui_binding_get));
    add_symbol(table, "_$s7SwiftUI7BindingV12wrappedValuexvs",
               reinterpret_cast<void*>(&finn_swiftui_binding_set));
}

void register_combine(SymbolTable* table) {
    add_symbol(table, "_finn_swiftui_publisher_create",
               reinterpret_cast<void*>(&finn_swiftui_publisher_create));
    add_symbol(table, "_finn_swiftui_publisher_sink",
               reinterpret_cast<void*>(&finn_swiftui_publisher_sink));
    add_symbol(table, "_finn_swiftui_publisher_send",
               reinterpret_cast<void*>(&finn_swiftui_publisher_send));
    add_symbol(table, "_finn_swiftui_publisher_send_copy",
               reinterpret_cast<void*>(&finn_swiftui_publisher_send_copy));
    add_symbol(table, "_finn_swiftui_publisher_cancel",
               reinterpret_cast<void*>(&finn_swiftui_publisher_cancel));
    add_symbol(table, "_finn_swiftui_publisher_count",
               reinterpret_cast<void*>(&finn_swiftui_publisher_count));
    add_symbol(table, "_finn_swiftui_publisher_destroy",
               reinterpret_cast<void*>(&finn_swiftui_publisher_destroy));
    add_symbol(table, "_finn_swiftui_object_publisher",
               reinterpret_cast<void*>(&finn_swiftui_object_publisher));
    add_symbol(table, "_finn_swiftui_object_will_change",
               reinterpret_cast<void*>(&finn_swiftui_object_will_change));
    add_symbol(table, "_finn_swiftui_object_remove",
               reinterpret_cast<void*>(&finn_swiftui_object_remove));
    add_symbol(table, "_finn_swiftui_state_observe",
               reinterpret_cast<void*>(&finn_swiftui_state_observe));

    // ObservableObject.objectWillChange and the subject send(_:) entry points.
    // A subject's `send` has (self, value) -- exactly our publisher_send ABI.
    // Only the pointer-sized payload form is claimed; generic value lowering is
    // a compiler concern and is not faked here.
    add_symbol(table,
               "_$s7Combine16ObservableObjectP15objectWillChangeAA25ObservableObjectPublisherVvg",
               reinterpret_cast<void*>(&finn_swiftui_object_publisher));
    add_symbol(table, "_$s7Combine18PassthroughSubjectV4sendyxF",
               reinterpret_cast<void*>(&finn_swiftui_publisher_send));
    add_symbol(table, "_$s7Combine19CurrentValueSubjectV4sendyxF",
               reinterpret_cast<void*>(&finn_swiftui_publisher_send));
}

void register_modifiers(SymbolTable* table) {
    add_symbol(table, "_finn_swiftui_view_token_create",
               reinterpret_cast<void*>(&finn_swiftui_view_token_create));
    add_symbol(table, "_finn_swiftui_bind_view",
               reinterpret_cast<void*>(&finn_swiftui_bind_view));
    add_symbol(table, "_finn_swiftui_view_token_destroy",
               reinterpret_cast<void*>(&finn_swiftui_view_token_destroy));
    add_symbol(table, "_finn_swiftui_padding",
               reinterpret_cast<void*>(&finn_swiftui_padding));
    add_symbol(table, "_finn_swiftui_frame",
               reinterpret_cast<void*>(&finn_swiftui_frame));
    add_symbol(table, "_finn_swiftui_background",
               reinterpret_cast<void*>(&finn_swiftui_background));
    add_symbol(table, "_finn_swiftui_corner_radius",
               reinterpret_cast<void*>(&finn_swiftui_corner_radius));
}

void register_containers(SymbolTable* table) {
    add_symbol(table, "_finn_swiftui_empty_view",
               reinterpret_cast<void*>(&finn_swiftui_empty_view));
    add_symbol(table, "_finn_swiftui_spacer",
               reinterpret_cast<void*>(&finn_swiftui_spacer));
    add_symbol(table, "_finn_swiftui_divider",
               reinterpret_cast<void*>(&finn_swiftui_divider));
    add_symbol(table, "_finn_swiftui_group",
               reinterpret_cast<void*>(&finn_swiftui_group));
    add_symbol(table, "_finn_swiftui_scroll_view",
               reinterpret_cast<void*>(&finn_swiftui_scroll_view));
    add_symbol(table, "_finn_swiftui_list",
               reinterpret_cast<void*>(&finn_swiftui_list));
    add_symbol(table, "_finn_swiftui_zstack",
               reinterpret_cast<void*>(&finn_swiftui_zstack));
}

void register_graphics(SymbolTable* table) {
    add_symbol(table, "_finn_swiftui_color_create",
               reinterpret_cast<void*>(&finn_swiftui_color_create));
    add_symbol(table, "_finn_swiftui_system_color",
               reinterpret_cast<void*>(&finn_swiftui_system_color));
    add_symbol(table, "_finn_swiftui_color_nscolor",
               reinterpret_cast<void*>(&finn_swiftui_color_nscolor));
    add_symbol(table, "_finn_swiftui_color_destroy",
               reinterpret_cast<void*>(&finn_swiftui_color_destroy));
    add_symbol(table, "_finn_swiftui_font_create",
               reinterpret_cast<void*>(&finn_swiftui_font_create));
    add_symbol(table, "_finn_swiftui_font_nsfont",
               reinterpret_cast<void*>(&finn_swiftui_font_nsfont));
    add_symbol(table, "_finn_swiftui_font_destroy",
               reinterpret_cast<void*>(&finn_swiftui_font_destroy));
}

}  // namespace finn::darwin::swiftui
