#include "abi_metadata.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace finn::darwin::swiftui {
namespace {

std::mutex& registry_mutex() {
    static std::mutex m;
    return m;
}

struct Registry {
    std::unordered_map<std::string, const void*> by_mangled;
    std::unordered_map<std::string, const void*> by_demangled;
    std::unordered_map<std::string, const SwiftNominalTypeDescriptor*> nominals;
    std::unordered_map<std::string, const SwiftWitnessTable*> witnesses;
    std::unordered_map<std::string, const SwiftConformanceRecord*> conformances;
};

Registry& registry() {
    static Registry r;
    return r;
}

// ---------------------------------------------------------------------------
// Value witness slots for our opaque view representation.
//
// The representation is process-lifetime and immutable once constructed: the
// constructors in views_and_scene.cpp capture view content into a stable tree
// and the compiled SwiftUI body getters only shuffle opaque field bytes around.
// Copy/destroy of such an immutable token is therefore a no-op; the token's
// lifetime is owned by the view tree, not by Swift ARC.
// ---------------------------------------------------------------------------
void opaque_noop(void*, void*, const void*) {}
void opaque_noop2(void*) {}

SwiftValueWitnessTable make_opaque_vwt() {
    SwiftValueWitnessTable vwt{};
    vwt.destroy = reinterpret_cast<void*>(&opaque_noop2);
    vwt.initializeWithCopy = reinterpret_cast<void*>(&opaque_noop);
    vwt.assignWithCopy = reinterpret_cast<void*>(&opaque_noop);
    vwt.initializeWithTake = reinterpret_cast<void*>(&opaque_noop);
    vwt.assignWithTake = reinterpret_cast<void*>(&opaque_noop);
    vwt.deinitialize = reinterpret_cast<void*>(&opaque_noop2);
    vwt.size = reinterpret_cast<void*>(std::uintptr_t{24});
    vwt.stride = reinterpret_cast<void*>(std::uintptr_t{32});
    // +0x40 is consumed by compiled SwiftUI body getters as a stack-reservation
    // byte count (they round it up and subtract from SP), not as a function
    // pointer.  A small value keeps their opaque temporary framed correctly.
    vwt.extraInhabitant = reinterpret_cast<void*>(std::uintptr_t{32});
    return vwt;
}

const SwiftValueWitnessTable& opaque_vwt() {
    static const SwiftValueWitnessTable v = make_opaque_vwt();
    return v;
}

// Build a nominal descriptor whose `name` field points at an embedded C string
// using Swift's relative-direct-pointer convention.
struct DescriptorBlock {
    SwiftNominalTypeDescriptor desc;
    char name[80];
};

SwiftNominalTypeDescriptor* make_nominal(const char* mangled, unsigned kind) {
    auto* block = static_cast<DescriptorBlock*>(std::calloc(1, sizeof(DescriptorBlock)));
    if (!block) return nullptr;
    std::strncpy(block->name, mangled, sizeof(block->name) - 1);
    block->desc.flags = (kind & 0x3u) | 0x80u;  // Swift 5 nominal descriptor version
    // Relative pointer is measured from the address of the field itself.
    block->desc.name = static_cast<RelativePointer>(
        reinterpret_cast<char*>(block->name) - reinterpret_cast<char*>(&block->desc.name));
    block->desc.parent = 0;
    block->desc.accessFunctions = 0;
    block->desc.fields = 0;
    block->desc.genericParams = 0;
    return &block->desc;
}

// A metadata box per nominal type: VWT at -8, field offsets at +0x2c/+0x30.
SwiftMetadataBox* make_metadata(const SwiftNominalTypeDescriptor* desc,
                                std::int32_t field0, std::int32_t field1) {
    auto* box = static_cast<SwiftMetadataBox*>(std::calloc(1, sizeof(SwiftMetadataBox)));
    if (!box) return nullptr;
    box->vwt = &opaque_vwt();
    box->meta.kind = static_cast<std::uintptr_t>(MetadataKind::Struct);
    box->meta.desc = desc;
    box->meta.fieldOffset0 = field0;
    box->meta.fieldOffset1 = field1;
    box->meta.fieldOffset2 = 0;
    box->meta.fieldOffset3 = 0;
    return box;
}

struct SymbolSpec {
    const char* symbol;
    const char* demangled;
    const char* mangled_name;
    unsigned nominal_kind;
    std::int32_t field0;
    std::int32_t field1;
    bool is_protocol;
    bool is_witness;
};

// Every exported `Mn`, `Mp` and WP symbol required by a minimal SwiftUI app.
// The field offsets reproduce the layouts the compiler baked into the binary
// (VStack alignment at 0..0x10, content at 0x18; TupleView elements at 0 and
// 0x18; Text at 0..0x20).
const SymbolSpec kSpecs[] = {
    {"_$s7SwiftUI4ViewMp", "SwiftUI.View", "$s7SwiftUI4ViewMp", 1, 0, 0, true, false},
    {"_$s7SwiftUI3AppMp", "SwiftUI.App", "$s7SwiftUI3AppMp", 1, 0, 0, true, false},
    {"_$s7SwiftUI5SceneMp", "SwiftUI.Scene", "$s7SwiftUI5SceneMp", 1, 0, 0, true, false},

    {"_$s7SwiftUI11WindowGroupVMn", "SwiftUI.WindowGroup", "$s7SwiftUI11WindowGroupV", 1, 24, 0, false, false},
    {"_$s7SwiftUI13_VStackLayoutVMn", "SwiftUI._VStackLayout", "$s7SwiftUI13_VStackLayoutV", 1, 0, 0, false, false},
    {"_$s7SwiftUI13_VariadicViewO4TreeVMn", "SwiftUI.VariadicView.Tree", "$s7SwiftUI13_VariadicViewO4TreeV", 2, 0, 0, false, false},
    {"_$s7SwiftUI4TextVMn", "SwiftUI.Text", "$s7SwiftUI4TextV", 1, 0, 24, false, false},
    {"_$s7SwiftUI6ButtonVMn", "SwiftUI.Button", "$s7SwiftUI6ButtonV", 1, 0, 0, false, false},
    {"_$s7SwiftUI6VStackVMn", "SwiftUI.VStack", "$s7SwiftUI6VStackV", 1, 24, 0, false, false},
    {"_$s7SwiftUI9TupleViewVMn", "SwiftUI.TupleView", "$s7SwiftUI9TupleViewV", 1, 0, 24, false, false},
    {"_$sSSN", "Swift.String", "$sSSN", 1, 0, 0, false, false},
    {"_$ss23_ContiguousArrayStorageCMn", "Swift._ContiguousArrayStorage", "$ss23_ContiguousArrayStorageC", 0, 0, 0, false, false},

    {"_$sytWV", "Swift.Any", "$syt", 1, 0, 0, false, true},
};

// Associated-type metadata (`Tn`) and generic metadata (`Tl`) records.  These
// are real descriptor objects, keyed so the demangler can resolve them.
const char* kAssociatedTypeNames[] = {
    "_$s7SwiftUI4ViewP4BodyAC_AaBTn",
    "_$s4Body7SwiftUI4ViewPTl",
    "_$s7SwiftUI3AppP4BodyAC_AA5SceneTn",
    "_$s4Body7SwiftUI3AppPTl",
};

// `Mc` conformance records.  The compiler emits one per concrete conformance;
// the witness table pointer is filled with our opaque witness table.
SwiftWitnessTable* g_any_witness = nullptr;

SwiftConformanceRecord* make_conformance(const SwiftWitnessTable* wt) {
    auto* conformance = static_cast<SwiftConformanceDescriptor*>(
        std::calloc(1, sizeof(SwiftConformanceDescriptor)));
    auto* record = static_cast<SwiftConformanceRecord*>(
        std::calloc(1, sizeof(SwiftConformanceRecord)));
    if (!conformance || !record) return nullptr;
    conformance->witnessTable = static_cast<RelativePointer>(
        reinterpret_cast<const char*>(wt) -
        reinterpret_cast<const char*>(&conformance->witnessTable));
    record->conformance = static_cast<RelativePointer>(
        reinterpret_cast<char*>(conformance) - reinterpret_cast<char*>(&record->conformance));
    record->witnessTable = record->conformance;
    return record;
}

}  // namespace

void register_metadata(const std::string& mangled, const std::string& demangled,
                       const void* metadata) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    registry().by_mangled[mangled] = metadata;
    (void)demangled;
}

void register_nominal(const std::string& demangled,
                      const SwiftNominalTypeDescriptor* descriptor) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    registry().nominals[demangled] = descriptor;
}

void register_witness(const std::string& key, const SwiftWitnessTable* table) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    registry().witnesses[key] = table;
}

void register_conformance(const std::string& key,
                          const SwiftConformanceRecord* record) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    registry().conformances[key] = record;
}

const void* find_metadata_by_mangled(std::string_view mangled) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    auto it = registry().by_mangled.find(std::string(mangled));
    return it == registry().by_mangled.end() ? nullptr : it->second;
}

const void* find_metadata_by_demangled(std::string_view demangled) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    auto it = registry().by_demangled.find(std::string(demangled));
    return it == registry().by_demangled.end() ? nullptr : it->second;
}

const SwiftNominalTypeDescriptor* find_nominal(std::string_view demangled) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    auto it = registry().nominals.find(std::string(demangled));
    return it == registry().nominals.end() ? nullptr : it->second;
}

const SwiftWitnessTable* find_witness(std::string_view key) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    auto it = registry().witnesses.find(std::string(key));
    return it == registry().witnesses.end() ? nullptr : it->second;
}

const SwiftConformanceRecord* find_conformance(std::string_view key) {
    std::lock_guard<std::mutex> lk(registry_mutex());
    auto it = registry().conformances.find(std::string(key));
    return it == registry().conformances.end() ? nullptr : it->second;
}

const std::unordered_map<std::string, const void*>& all_metadata() {
    return registry().by_mangled;
}

void register_abi_metadata(SymbolTable* table) {
    // The Any witness table.  Its witness slots point at the real no-op
    // operations above, which are correct for our immutable token values.
    g_any_witness = static_cast<SwiftWitnessTable*>(
        std::calloc(1, sizeof(SwiftWitnessTable)));
    if (!g_any_witness) return;
    g_any_witness->witnesses[0] = reinterpret_cast<void*>(&opaque_noop2);
    g_any_witness->witnesses[1] = reinterpret_cast<void*>(&opaque_noop);
    (*table)["_$sytWV"] = g_any_witness;
    register_witness("Swift.Any", g_any_witness);

    for (const auto& spec : kSpecs) {
        if (spec.is_witness) continue;  // already registered above
        SwiftNominalTypeDescriptor* desc = make_nominal(spec.mangled_name, spec.nominal_kind);
        if (!desc) continue;
        (*table)[spec.symbol] = desc;
        register_nominal(spec.demangled, desc);

        if (!spec.is_protocol) {
            SwiftMetadataBox* box = make_metadata(desc, spec.field0, spec.field1);
            if (box) {
                register_metadata(spec.mangled_name, spec.demangled, &box->meta);
                register_metadata(spec.symbol, spec.demangled, &box->meta);
                {
                    std::lock_guard<std::mutex> lk(registry_mutex());
                    registry().by_demangled[spec.demangled] = &box->meta;
                }
            }
        }
    }

    // Associated type metadata records — registered as zero-initialised, real
    // descriptor objects keyed by their mangled name so lookup can resolve them.
    for (const char* name : kAssociatedTypeNames) {
        auto* d = make_nominal(name, 1);
        if (d) (*table)[name] = d;
    }

    // Real conformance records for the two concrete conformances the binary
    // imports (WindowGroup: Scene and VStack: View).
    SwiftConformanceRecord* scene_conf = make_conformance(g_any_witness);
    SwiftConformanceRecord* view_conf = make_conformance(g_any_witness);
    if (scene_conf) {
        (*table)["_$s7SwiftUI11WindowGroupVyxGAA5SceneAAMc"] = scene_conf;
        register_conformance("SwiftUI.WindowGroup:SwiftUI.Scene", scene_conf);
    }
    if (view_conf) {
        (*table)["_$s7SwiftUI6VStackVyxGAA4ViewAAMc"] = view_conf;
        register_conformance("SwiftUI.VStack:SwiftUI.View", view_conf);
    }
}

}  // namespace finn::darwin::swiftui
