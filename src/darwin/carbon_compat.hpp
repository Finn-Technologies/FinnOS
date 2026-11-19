// Carbon/HIToolbox and late AppKit compatibility for finn-darwin.
//
// Implements the real entry points real macOS menu-bar apps import: global
// hotkey registration, keyboard-layout translation and the small AppKit helper
// classes they rely on.  Every symbol has a real implementation; unsupported
// key/property queries return an honest empty result.

#ifndef FINN_DARWIN_CARBON_COMPAT_HPP
#define FINN_DARWIN_CARBON_COMPAT_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin {

void register_carbon_classes();
void register_carbon_symbols(std::unordered_map<std::string, void*>* table);

// Number of currently registered global hotkeys (unit-test observability).
std::size_t carbon_registered_hotkey_count();

}  // namespace finn::darwin

#endif  // FINN_DARWIN_CARBON_COMPAT_HPP
