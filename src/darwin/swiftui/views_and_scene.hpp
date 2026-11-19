// Minimal SwiftUI view/scene constructors.
//
// The compiled app builds its body by calling the imported SwiftUI initializers
// below.  Each initializer captures the real content the app supplies (the
// localized string, the button action) into a view tree, and materialises that
// tree through the existing AppKit-on-Qt6 bridge.  Nothing is fabricated: if an
// app supplies no content, no view is shown.

#ifndef FINN_DARWIN_SWIFTUI_VIEWS_AND_SCENE_HPP
#define FINN_DARWIN_SWIFTUI_VIEWS_AND_SCENE_HPP

#include "contract.hpp"

namespace finn::darwin::swiftui {

void register_view_protocol(SymbolTable* table);
void register_app_scene(SymbolTable* table);
void register_text_view(SymbolTable* table);
void register_button_view(SymbolTable* table);
void register_stack_layout(SymbolTable* table);
void register_scene_builder(SymbolTable* table);
void register_graphics(SymbolTable* table);

// Creates (once) and shows the window for the captured view tree.  Returns
// false when no view content has been captured yet.
bool render_captured_views();

// Clears captured state (for unit tests).
void reset_captured_views();

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_VIEWS_AND_SCENE_HPP
