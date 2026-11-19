// Honest, data-driven coverage record for frameworks and ABI symbols that
// finn-darwin does not implement. This is a *reporting* table only: nothing here
// installs a stub or pretends a capability exists. `finn-darwin diagnose`,
// `status`, and the loader's launch-failure message all read from it so that a
// binary which needs SwiftUI, Metal, WebKit, CoreData or the full Swift runtime
// fails before main() with a precise reason.

#ifndef FINN_DARWIN_FRAMEWORK_COVERAGE_HPP
#define FINN_DARWIN_FRAMEWORK_COVERAGE_HPP

#include <string>
#include <vector>

namespace finn::darwin {

struct FrameworkGap {
    std::string name;
    std::string reason;
    bool linked = false;  // set by MachoLoader::diagnose when the binary links it
};

struct SymbolGap {
    std::string name;
    std::string reason;
};

// Static, immutable descriptions of what is genuinely not implemented.
const std::vector<FrameworkGap>& unimplemented_frameworks();
const std::vector<SymbolGap>& unimplemented_symbols();

// Returns a non-empty explanation when the framework or symbol is known to be
// unimplemented, and an empty string otherwise. Prefix/mangling rules cover
// Swift ($s7SwiftUI...) and Objective-C class families (NSManagedObject...).
std::string framework_reason(const std::string& short_name);
std::string symbol_reason(const std::string& symbol);

// Human-readable multi-line list used by `finn-darwin status`.
std::string coverage_report();

}  // namespace finn::darwin

#endif
