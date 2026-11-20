// Swift Foundation overlay ABI for the clean-room Darwin runtime.
//
// The symbols registered here are the out-of-line entry points that a compiled
// Swift binary imports from the Foundation *Swift overlay* (the `_$s10Foundation...`
// symbols), plus the Swift runtime entry points they depend on (Hasher, weak
// references, generic enum tags).  Every registered symbol has a real
// implementation: the Hasher uses the same SipHash-2-4 state machine as the
// Swift standard library; URL/Date/UUID/Data/Locale/CharacterSet are backed by
// real value representations whose layouts were derived from `MemoryLayout`
// probes and the Swift ABI; the enum helpers implement the documented
// single/multi-payload tag algorithms.
//
// Services that genuinely do not exist on this platform (ServiceManagement
// login items, KVO change observation driven by KeyPath witness tables, generic
// Codable dispatch) are deliberately NOT registered.  `finn-darwin diagnose`
// keeps reporting them instead of pretending they work.

#ifndef FINN_DARWIN_SWIFTUI_FOUNDATION_OVERLAY_HPP
#define FINN_DARWIN_SWIFTUI_FOUNDATION_OVERLAY_HPP

#include "contract.hpp"

namespace finn::darwin::swiftui {

// Populate `table` with the real Swift Foundation overlay entry points.
void register_swift_foundation_overlay(SymbolTable* table);

// Self-contained checks of the pure-value parts of this overlay (SipHash,
// Date arithmetic, UUID equality, URL path parsing, CharacterSet classes and
// enum tag round-trips).  Returns true when every check passes.
bool swift_foundation_overlay_self_test();

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_FOUNDATION_OVERLAY_HPP
