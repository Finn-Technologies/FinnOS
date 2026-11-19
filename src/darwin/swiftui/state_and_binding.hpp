// SwiftUI property wrappers, Combine sinks, view modifiers, containers and
// graphics values for the finn-darwin SwiftUI slice.
//
// Swift ABI note
// -------------
// Every entry point a compiled Swift body can reach is a plain C function with
// scalar/pointer arguments and a pointer/integer return.  On aarch64 that is
// the Swift calling convention for such signatures: arguments arrive in
// x0..x7 (doubles in v0..v7) and the result leaves in x0.  Swift appends hidden
// generic-metadata arguments for generic functions; our callees simply ignore
// those extra registers, which is safe because the caller owns them.  No C++
// object with a destructor is ever handed to Swift: only opaque pointers that
// this module allocates and the matching `*_destroy` call releases.
//
// The modifier/container layer works on "view tokens" (opaque handles) that a
// view host binds to a real bridged NSView with `swiftui_bind_view`.  A Swift
// view body passes the token as `self` (x0); we look up the NSView and drive it
// through the Objective-C runtime: NSView `setFrame:` for geometry and the
// CALayer `setBackgroundColor:` / `setCornerRadius:` selectors for colour and
// rounding.  Nothing is returned unless it is backed by that real behaviour.

#ifndef FINN_DARWIN_SWIFTUI_STATE_AND_BINDING_HPP
#define FINN_DARWIN_SWIFTUI_STATE_AND_BINDING_HPP

#include "contract.hpp"

#include "darwin/objc_runtime.hpp"

#include <cstddef>
#include <cstdint>

namespace finn::darwin::swiftui {

// Layout-compatible with the bridge's DarwinRect and with Cocoa NSRect: four
// consecutive doubles (origin.x, origin.y, size.width, size.height).  It is an
// HFA on aarch64, so both C++ and Swift pass it in v0..v3 identically.
struct SwiftViewRect {
    double x;
    double y;
    double width;
    double height;
};

// ---------------------------------------------------------------------------
// @State and @Binding
// ---------------------------------------------------------------------------

struct StateBox;
struct Binding;

// Allocate a state box and copy `size` bytes from `initial_value` into it.
// Copies of the wrapper share this box, which is what makes @State survive a
// view re-render.  Returns nullptr when allocation fails.
StateBox* swiftui_state_create(const void* initial_value, std::size_t size);
StateBox* swiftui_state_create_empty(std::size_t size);

// Pointer to the stored bytes (never owned by the caller).  nullptr for an
// empty value.
void* swiftui_state_get(StateBox* box);
const void* swiftui_state_get(const StateBox* box);

// Copy `size` bytes in, bump the generation and notify any attached publisher.
bool swiftui_state_set(StateBox* box, const void* value);

// Monotonically increasing count of writes through the box or its binding.
std::uint64_t swiftui_state_generation(const StateBox* box);

// The projected binding is created once and stays stable for the box lifetime.
Binding* swiftui_state_projected(StateBox* box);

void swiftui_state_destroy(StateBox* box);

// @Binding is a projection, not an independent value: reads and writes go to
// the originating StateBox.  Two bindings to one box stay in sync.
void* swiftui_binding_get(Binding* binding);
bool swiftui_binding_set(Binding* binding, const void* value);
StateBox* swiftui_binding_box(const Binding* binding);

// ---------------------------------------------------------------------------
// Combine-style publisher / subscriber
// ---------------------------------------------------------------------------

struct Publisher;

using SinkFunction = void (*)(void* context, void* value);

// `replay_last` gives CurrentValueSubject semantics; false gives a
// PassthroughSubject.  A sink installed on a current-value publisher is called
// immediately with the stored value.
Publisher* swiftui_publisher_create(bool replay_last);
std::uint64_t swiftui_publisher_sink(Publisher* publisher, SinkFunction sink,
                                     void* context);
void swiftui_publisher_send(Publisher* publisher, void* value);

// Deliver an opaque value and, for a current-value publisher, retain a real
// copy so a sink installed later receives the most recent value.  The size is
// supplied by the producer (e.g. @State knows its own storage size).
bool swiftui_publisher_send_copy(Publisher* publisher, const void* value,
                                 std::size_t size);
bool swiftui_publisher_cancel(Publisher* publisher, std::uint64_t subscription);
std::uint64_t swiftui_publisher_subscriber_count(const Publisher* publisher);
void swiftui_publisher_destroy(Publisher* publisher);

// ObservableObject: one lazily created publisher per observed object.  Sending
// `objectWillChange` notifies every sink registered for that object.
Publisher* swiftui_object_publisher(void* object);
void swiftui_object_will_change(void* object, void* value);
void swiftui_object_remove(void* object);

// Attach a publisher to @State so a write through wrappedValue or a projected
// binding is observable.  Returns the attached publisher (owned by the box).
Publisher* swiftui_state_observe(StateBox* box, bool replay_last);

// ---------------------------------------------------------------------------
// Opaque view tokens and the bridged NSView behind them
// ---------------------------------------------------------------------------

// Allocate a fresh token with no view attached.
void* swiftui_view_token_create();

// Associate a token with a real NSView.  The view is not retained here: its
// lifetime belongs to the view host that created it.
bool swiftui_bind_view(void* token, ObjcId view);

// Returns the NSView for a token, or nullptr when the token was never bound.
ObjcId swiftui_view_for_token(void* token);

void swiftui_view_token_destroy(void* token);

// The four modifiers.  Each returns `token` when the backing view exists and
// was changed, nullptr otherwise.  `frame` treats a negative or non-finite
// dimension as "leave that axis alone".
void* swiftui_modifier_padding(void* token, double top, double leading,
                               double bottom, double trailing);
void* swiftui_modifier_frame(void* token, double width, double height);
void* swiftui_modifier_background(void* token, double red, double green,
                                  double blue, double alpha);
void* swiftui_modifier_corner_radius(void* token, double radius);

// NSView-level variants, useful to a view host that materialises a view
// directly.  They return false when the required ObjC classes are absent, and
// never invent a geometry or colour that could not be applied.
bool swiftui_apply_padding(ObjcId view, double top, double leading,
                           double bottom, double trailing);
bool swiftui_apply_frame(ObjcId view, double width, double height);
bool swiftui_apply_background(ObjcId view, double red, double green,
                              double blue, double alpha);
bool swiftui_apply_corner_radius(ObjcId view, double radius);

SwiftViewRect swiftui_view_frame(ObjcId view);

// ---------------------------------------------------------------------------
// Graphics: Color / Font
// ---------------------------------------------------------------------------

// Plain SwiftUI-style value structs.  Their layout is ours, not Apple's
// private representation, so Text and friends must build them with these
// factories instead of treating imported bytes as a Color/Font.
struct SwiftUIColor {
    double red;
    double green;
    double blue;
    double alpha;
};

struct SwiftUIFont {
    double size;
    std::uint32_t traits;  // bit 0 bold, bit 1 italic, bit 2 monospaced
};

SwiftUIColor swiftui_color(double red, double green, double blue, double alpha);
SwiftUIColor swiftui_system_color(int index);
SwiftUIFont swiftui_system_font(double size, std::uint32_t traits);

// Real NSColor / NSFont objects from the AppKit bridge.  nullptr when the ObjC
// classes have not been initialised; callers must check before using them.
ObjcId swiftui_color_to_nscolor(SwiftUIColor color);
ObjcId swiftui_font_to_nsfont(SwiftUIFont font);

// ---------------------------------------------------------------------------
// Containers backed by real NSViews
// ---------------------------------------------------------------------------

// Each returns a new bound token, or nullptr when AppKit is unavailable.
// `group` is transparent: it reuses its content's view instead of inventing a
// layer.  `list` builds a vertical stack inside an NSScrollView.
void* swiftui_container_empty_view();
void* swiftui_container_spacer();
void* swiftui_container_divider();
void* swiftui_container_group(void* content);
void* swiftui_container_scroll_view(void* content);
void* swiftui_container_list(void* const* children, std::size_t count);
void* swiftui_container_zstack(void* const* children, std::size_t count);

// ---------------------------------------------------------------------------
// Loader registration (declared in contract.hpp)
// ---------------------------------------------------------------------------

void register_state_and_binding(SymbolTable* table);
void register_combine(SymbolTable* table);
void register_modifiers(SymbolTable* table);
void register_containers(SymbolTable* table);
void register_graphics(SymbolTable* table);

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_STATE_AND_BINDING_HPP
