// Swift runtime ABI extensions required by a compiled SwiftUI hello-world.
//
// Every symbol registered here has a real implementation: a mangled-name
// parser plus metadata lookup, witness-table resolution, a stack probe, the
// Swift `print` entry point for String existentials, and an OS-version probe.

#ifndef FINN_DARWIN_SWIFTUI_SWIFT_RUNTIME_EXT_HPP
#define FINN_DARWIN_SWIFTUI_SWIFT_RUNTIME_EXT_HPP

#include "contract.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace finn::darwin::swiftui {

// Parse a Swift mangled type name into its "Module.Type" canonical form.
// Supports the length-prefixed identifier grammar and the common one-letter
// shorthands (`s` = Swift, `SS` = String).  Returns an empty string when the
// mangling is outside this documented subset (never a guessed type).
std::string demangle_swift_type(std::string_view mangled);

// Resolve a mangled name to real type metadata registered by abi_metadata.
const void* resolve_type_metadata(const char* name, std::size_t length);

void register_swift_runtime(SymbolTable* table);

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_SWIFT_RUNTIME_EXT_HPP
