// Clean-room Swift core runtime ABI shims (libswiftCore) for finn-darwin.
// Implements core Swift HeapObject allocation, ARC reference counting,
// bridged/unknown object retention, exclusivity access markers, swift_once,
// slowAlloc/slowDealloc, and ObjC class interop. Does not fake SwiftUI or
// unimplemented Swift standard library symbols.

#ifndef FINN_DARWIN_SWIFT_RUNTIME_HPP
#define FINN_DARWIN_SWIFT_RUNTIME_HPP

#include <cstddef>
#include <string>
#include <unordered_map>

namespace finn::darwin {

// Populates the loader symbol table with core Swift runtime ABI symbols.
void register_swift_runtime_symbols(std::unordered_map<std::string, void*>* table);

// Diagnostic/test helper returning the number of currently live Swift HeapObjects.
std::size_t swift_live_object_count();

}  // namespace finn::darwin

#endif
