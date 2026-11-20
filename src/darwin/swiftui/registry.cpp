// Aggregator for the SwiftUI compatibility modules.  This is the only file
// that calls the per-module registration functions; macho_loader.cpp calls
// register_all_swiftui_symbols once during runtime initialisation.

#include "contract.hpp"

#include "abi_metadata.hpp"
#include "state_and_combine.hpp"
#include "swift_runtime_ext.hpp"
#include "views_and_scene.hpp"
#include "foundation_overlay.hpp"
#include "render_graph.hpp"

namespace finn::darwin::swiftui {

void register_all_swiftui_symbols(SymbolTable* table) {
    register_abi_metadata(table);
    register_swift_runtime(table);
    register_view_protocol(table);
    register_app_scene(table);
    register_text_view(table);
    register_button_view(table);
    register_stack_layout(table);
    register_scene_builder(table);
    register_graphics(table);
    register_state_and_binding(table);
    register_combine(table);
    register_modifiers(table);
    register_containers(table);
    register_swift_foundation_overlay(table);
    register_swiftui_render_graph(table);
}

bool swiftui_available() { return !all_metadata().empty(); }

}  // namespace finn::darwin::swiftui
