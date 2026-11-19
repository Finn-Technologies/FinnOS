// Shared contract for the SwiftUI compatibility modules.
//
// Each module owns its own files under src/darwin/swiftui/ and exposes exactly
// one registration function declared here. The aggregator (registry.cpp) is
// the only place that calls them, and macho_loader.cpp calls the aggregator.
// This lets many modules be built and tested independently.
//
// NEVER FAKE A CAPABILITY: a symbol registered here must have real behaviour
// for the ABI it claims to implement. If a behaviour cannot be implemented,
// do not register the symbol; leave it to finn-darwin diagnose to report.

#ifndef FINN_DARWIN_SWIFTUI_CONTRACT_HPP
#define FINN_DARWIN_SWIFTUI_CONTRACT_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin::swiftui {

using SymbolTable = std::unordered_map<std::string, void*>;

// One function per module. Implementations live in the matching .cpp file.
void register_abi_metadata(SymbolTable* table);
void register_swift_runtime(SymbolTable* table);
void register_view_protocol(SymbolTable* table);
void register_app_scene(SymbolTable* table);
void register_text_view(SymbolTable* table);
void register_button_view(SymbolTable* table);
void register_stack_layout(SymbolTable* table);
void register_scene_builder(SymbolTable* table);
void register_graphics(SymbolTable* table);
void register_state_and_binding(SymbolTable* table);
void register_combine(SymbolTable* table);
void register_modifiers(SymbolTable* table);
void register_containers(SymbolTable* table);

// Calls every module above. Defined in registry.cpp (integration owner).
void register_all_swiftui_symbols(SymbolTable* table);

// True when at least one SwiftUI/Swift symbol was registered.
bool swiftui_available();

}  // namespace finn::darwin::swiftui

#endif
