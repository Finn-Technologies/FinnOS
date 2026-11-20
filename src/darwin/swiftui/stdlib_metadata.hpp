// Generic materializer for Swift standard-library protocol metadata.
//
// This module closes the stdlib half of the missing-symbol gap for real apps
// (Hidden Bar, Rectangle).  It recognises the mangled families emitted by the
// Swift 6.4 compiler -- Mp (protocol descriptor), Tq/Tb (protocol requirement
// records), WP (witness table), Mc (conformance record) and WV (value witness
// table) -- and builds self-contained, ABI-correct objects for the ones whose
// semantics we can actually honour on this runtime.
//
// NEVER FAKE A CAPABILITY
// -----------------------
// * A descriptor is real: its flags, counts, names, requirement flags and
//   witness-table slot order were read out of a *linked Swift 6.4.2
//   libswiftCore.dylib* with dlsym, not guessed from an image.  Relative
//   pointers are genuine Swift relative-direct pointers into the allocation.
// * A Tq/Tb symbol resolves to the actual TargetProtocolRequirement record
//   inside the matching descriptor, exactly as it does in the real stdlib.
// * A witness slot is either a real function (SipHash-backed integer hashing,
//   integer equality, trivial bitwise value witnesses) or absent.  Absent
//   behaviour is never registered; `unimplemented()` reports it precisely.
// * Protocol requirement records carry no default implementation.  Swift's
//   stdlib does provide some; we do not pretend to, and the absence is
//   reported rather than filled with a plausible-looking thunk.

#ifndef FINN_DARWIN_SWIFTUI_STDLIB_METADATA_HPP
#define FINN_DARWIN_SWIFTUI_STDLIB_METADATA_HPP

#include "contract.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace finn::darwin::swiftui {

// Register every stdlib metadata symbol with real behaviour.  Symbols whose
// behaviour cannot be implemented are NOT inserted into `table`.
void register_swift_stdlib_metadata(SymbolTable* table);

// Verifies descriptor layout, pointer arithmetic, requirement indices and the
// behaviour of the real witness functions.  Prints a short report to stderr.
bool swift_stdlib_metadata_self_test();

// Real counts for reporting and for the integration owner's tests.
std::size_t swift_stdlib_metadata_registered_symbol_count();

// Symbols seen in the captured app diagnose lists that this module deliberately
// does not register, each with the precise reason it is absent.
const std::vector<std::pair<std::string, std::string>>&
swift_stdlib_metadata_unimplemented();

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_STDLIB_METADATA_HPP
