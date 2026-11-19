#include "darwin/framework_coverage.hpp"

#include <algorithm>

namespace finn::darwin {
namespace {

const std::vector<FrameworkGap>& framework_table() {
    static const std::vector<FrameworkGap> table = {
        {"SwiftUI",
         "partial SwiftUI: Text/VStack/Button/WindowGroup materialisation, ABI "
         "metadata and App.main are implemented on the AppKit-on-Qt6 bridge; "
         "modifiers, property wrappers and the full render graph are not"},
        {"Swift",
         "partial Swift: core HeapObject/ARC/once ABI plus mangled-name type "
         "lookup, witness-table resolution and basic stdlib entry points; "
         "generics, concurrency and full collections are not implemented"},
        {"Metal", "no Metal device, command queue, pipeline state or shader compiler"},
        {"MetalKit", "no MTKView or Metal texture bridging"},
        {"WebKit", "no web renderer and no WKWebView"},
        {"CoreData", "no managed object model, context or persistent store coordinator"},
        {"Combine", "no publishers/subscribers or scheduler operators"},
        {"Security", "no Keychain, certificate, trust or code-signing services"},
        {"CloudKit", "no iCloud container or record-database service"},
        {"StoreKit", "no App Store receipts, purchases or subscriptions"},
        {"AVFoundation", "no media capture, playback or audio session services"},
        {"MapKit", "no map renderer or geocoding service"},
        {"HealthKit", "no health data store"},
    };
    return table;
}

const std::vector<SymbolGap>& symbol_table() {
    static const std::vector<SymbolGap> table = {
        {"_swift_getTypeByMangledName",
         "Swift runtime: mangled-name type metadata lookup is not implemented; "
         "this blocks ordinary compiled Swift binaries"},
        {"_swift_getTypeByMangledNameInContext",
         "Swift runtime: mangled-name type metadata lookup is not implemented; "
         "this blocks ordinary compiled Swift binaries"},
        {"_swift_getTypeByMangledNameInContext2",
         "Swift runtime: mangled-name type metadata lookup is not implemented; "
         "this blocks ordinary compiled Swift binaries"},
        {"_swift_getWitnessTable",
         "Swift runtime: protocol witness table lookup is not implemented"},
        {"_swift_allocError", "Swift runtime: error existentials are not implemented"},
        {"_swift_willThrow", "Swift runtime: error existentials are not implemented"},
        {"_swift_errorRetain", "Swift runtime: error existentials are not implemented"},
        {"_swift_errorRelease", "Swift runtime: error existentials are not implemented"},
        {"_swift_getFunctionTypeMetadata",
         "Swift runtime: function type metadata is not implemented"},
        {"_swift_task_create", "Swift concurrency runtime is not implemented"},
        {"_MTLCreateSystemDefaultDevice", "Metal is not implemented"},
        {"_NSManagedObjectContextInitialize", "CoreData is not implemented"},
    };
    return table;
}

bool starts_with(const std::string& s, const char* prefix) {
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

}  // namespace

const std::vector<FrameworkGap>& unimplemented_frameworks() { return framework_table(); }

const std::vector<SymbolGap>& unimplemented_symbols() { return symbol_table(); }

std::string framework_reason(const std::string& short_name) {
    for (const auto& f : framework_table()) {
        if (f.name == short_name) return f.reason;
    }
    return {};
}

std::string symbol_reason(const std::string& symbol) {
    // Exact entries first (including the leading Mach-O underscore where present).
    for (const auto& s : symbol_table()) {
        if (s.name == symbol) return s.reason;
    }
    // Swift mangled symbols carry their module name, e.g. $s7SwiftUI4TextV...
    if (symbol.find("SwiftUI") != std::string::npos) return framework_reason("SwiftUI");
    if (symbol.find("Swift.") != std::string::npos) return framework_reason("Swift");
    if (symbol.find("Metal") != std::string::npos) return framework_reason("Metal");
    if (symbol.find("WebKit") != std::string::npos) return framework_reason("WebKit");
    if (symbol.find("CoreData") != std::string::npos) return framework_reason("CoreData");
    if (symbol.find("Combine") != std::string::npos) return framework_reason("Combine");

    // Objective-C class families exported by unimplemented frameworks.
    if (starts_with(symbol, "_NSManagedObject") ||
        starts_with(symbol, "_NSPersistentStore") ||
        starts_with(symbol, "OBJC_CLASS_$_NSManagedObject")) {
        return framework_reason("CoreData");
    }
    if (starts_with(symbol, "_MTL")) return framework_reason("Metal");
    if (starts_with(symbol, "_WK")) return framework_reason("WebKit");

    // Any other Swift-mangled symbol is standard-library metadata we do not
    // materialize (String metadata, print(), array storage, mangled module names).
    if (starts_with(symbol, "_$s") || starts_with(symbol, "$s")) {
        return framework_reason("Swift");
    }

    // Bare (non-underscored) C/Swift names that correspond to a table entry.
    if (!symbol.empty() && symbol[0] != '_') {
        const std::string underscored = "_" + symbol;
        for (const auto& s : symbol_table()) {
            if (s.name == underscored) return s.reason;
        }
    }
    return {};
}

std::string coverage_report() {
    std::string out;
    for (const auto& f : framework_table()) {
        out += "  - " + f.name + ": " + f.reason + "\n";
    }
    return out;
}

}  // namespace finn::darwin
