// Late-framework compatibility for Rectangle, Hidden Bar and Nightfall.
//
// This module closes only the gaps that have a truthful implementation on this
// host: late AppKit objects that map onto the real AppKit-on-Qt6 widget bridge,
// Cocoa/CF string constants with their documented values, and small C entry
// points backed by host libc, Foundation or the documentated Darwin ABI.
//
// It deliberately does NOT register WebKit (no renderer exists in this tree),
// SMAppService (no launchd), or fabricated code-signing results. Those imports
// stay missing so `finn-darwin diagnose` keeps naming them.

#ifndef FINN_DARWIN_LATE_FRAMEWORKS_HPP
#define FINN_DARWIN_LATE_FRAMEWORKS_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin {

// Adds the real late-framework entries to the loader symbol table. Safe to call
// more than once; later calls overwrite with the same values.
void register_late_framework_symbols(std::unordered_map<std::string, void*>* table);

// Exercises the registered entries and prints a line per check. Returns true
// only when every check passes. Intended for a small host harness and tests.
bool late_frameworks_self_test();

}  // namespace finn::darwin

#endif  // FINN_DARWIN_LATE_FRAMEWORKS_HPP
