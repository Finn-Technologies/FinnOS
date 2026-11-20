// A real SwiftUI render graph for the finn-darwin SwiftUI slice.
//
// This module closes the descriptor gap of larger SwiftUI apps (Nightfall,
// Rectangle) without claiming Apple's private render graph.  It registers real,
// ABI-shaped metadata objects for the compiler-emitted SwiftUI/Combine symbols
// that those apps import, and real requirement thunks/view primitives for the
// parts of the graph we can actually build on the AppKit-on-Qt6 bridge.
//
// NEVER FAKE A CAPABILITY
// -----------------------
// A nominal/protocol/method descriptor here is a genuine descriptor object: its
// name field points at the real mangled name, its flags carry the real kind.
// A witness slot is either a real thunk with real behaviour or null — a null
// slot is reported as such, never filled with a plausible-looking function.
// A view primitive returns a bound token only when a real NSView was created;
// when AppKit is unavailable it returns nullptr and the caller sees that.
//
// Derived ABI facts (Swift 6.4, Xcode toolchain, compiled snippets)
// ---------------------------------------------------------------
// Nominal descriptor flags are 0x50 | kind (0x51 struct, 0x52 enum, 0x50
// class); protocol descriptor flags are 0x00010043.  These were read from a
// compiled `swiftc -emit-object` snippet.  The repository's existing
// abi_metadata.cpp uses the 0x80 version for the minimal slice; this module
// uses its own objects and never overwrites symbols the minimal slice owns.

#ifndef FINN_DARWIN_SWIFTUI_RENDER_GRAPH_HPP
#define FINN_DARWIN_SWIFTUI_RENDER_GRAPH_HPP

#include "contract.hpp"

#include <cstddef>

namespace finn::darwin::swiftui {

// Register every symbol this module can back with real behaviour.  Callers add
// the result to the same SymbolTable the loader uses; symbols already owned by
// the minimal slice are never overwritten.
void register_swiftui_render_graph(SymbolTable* table);

// Self-check of the registered descriptors, thunks and render primitives.
// Prints a short report to stderr and returns true when every assertion that
// does not require a live AppKit bridge passed.  Render assertions are reported
// as skipped (not passed) when the bridge classes are absent.
bool swiftui_render_graph_self_test();

// ---------------------------------------------------------------------------
// Real compositing primitives.  Each takes view tokens produced by the shared
// state_and_binding bridge (swiftui_view_token_create / swiftui_bind_view) and
// returns a new bound token, or nullptr when no real NSView could be made.
// ---------------------------------------------------------------------------

// A stack of real subviews laid out left-to-right (horizontal) or bottom-to-top
// (vertical) with `spacing` pixels between adjacent children.
void* swiftui_rg_hstack(void* const* children, std::size_t count, double spacing);
void* swiftui_rg_vstack(void* const* children, std::size_t count, double spacing);
void* swiftui_rg_zstack(void* const* children, std::size_t count);

// Modifiers backed by real NSView frame / CALayer operations.
void* swiftui_rg_frame(void* content, double width, double height);
void* swiftui_rg_padding(void* content, double top, double leading,
                         double bottom, double trailing);
void* swiftui_rg_background(void* content, double red, double green,
                            double blue, double alpha);
void* swiftui_rg_corner_radius(void* content, double radius);

// ForEach-style repeated children.  `prototype` must be a real bound token;
// `count` real subviews are laid out vertically.  When a Swift ForEach closure
// cannot be invoked (its ABI is not derivable here) the caller supplies the
// materialised children through this API rather than receiving fabricated ones.
void* swiftui_rg_foreach(void* const* children, std::size_t count);

// Number of symbols this module registered (real count, for reporting/tests).
std::size_t swiftui_rg_registered_symbol_count();

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_RENDER_GRAPH_HPP
