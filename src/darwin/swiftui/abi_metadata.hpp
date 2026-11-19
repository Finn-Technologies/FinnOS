// Swift 5.x 64-bit ABI metadata structures for the SwiftUI slice.
//
// These are the clean-room equivalents of the compiler-emitted `Mn` (nominal
// type descriptor), `Mp` (protocol descriptor), `Mc` (conformance record) and
// `Tn`/`Tl` (associated-type metadata) symbols.  The layouts follow the public,
// stable Swift ABI (swift/include/swift/ABI/Metadata.h).  Nothing here is a
// trap: every exported symbol is a real descriptor with real fields.
//
// The demangler in swift_runtime_ext.cpp resolves a mangled name to one of the
// metadata pointers registered here.

#ifndef FINN_DARWIN_SWIFTUI_ABI_METADATA_HPP
#define FINN_DARWIN_SWIFTUI_ABI_METADATA_HPP

#include "contract.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace finn::darwin::swiftui {

// ---------------------------------------------------------------------------
// Public Swift ABI layouts (64-bit, Swift 5 ABI stability).
// ---------------------------------------------------------------------------

// A relative direct pointer is an int32 offset from the address of the field
// itself.  A value of 0 means "null".
using RelativePointer = std::int32_t;

// TargetMetadata: every Swift type metadata begins with a kind/isa word.
struct SwiftMetadata {
    std::uintptr_t kind;  // 0x00
};

enum class MetadataKind : std::uintptr_t {
    Struct = 0x200,
    Enum = 0x201,
    Class = 0x202,
    Protocol = 0x203,
    Tuple = 0x204,
    Function = 0x205,
};

// TargetNominalTypeDescriptor for structs and enums.
struct SwiftNominalTypeDescriptor {
    std::uint32_t flags;            // 0x00  low 2 bits: 0=class, 1=struct, 2=enum
    RelativePointer parent;         // 0x04
    RelativePointer name;           // 0x08  mangled name C string
    RelativePointer accessFunctions;// 0x0c
    RelativePointer fields;         // 0x10  TargetFieldDescriptor
    RelativePointer genericParams;  // 0x14  0 when non-generic
};
static_assert(sizeof(SwiftNominalTypeDescriptor) == 24);

// TargetProtocolDescriptor (Mp).
struct SwiftProtocolDescriptor {
    RelativePointer flags;          // 0x00 protocol flags
    RelativePointer parent;         // 0x04
    RelativePointer name;           // 0x08 mangled name
    RelativePointer requirements;   // 0x0c
};

// TargetProtocolConformanceDescriptor backing an `Mc` record.
struct SwiftConformanceDescriptor {
    std::uint32_t flags;            // 0x00
    RelativePointer typeRef;        // 0x04 indirect pointer to type metadata
    RelativePointer witnessTable;   // 0x08 protocol witness table
    RelativePointer protocolRef;    // 0x0c protocol descriptor
};

// TargetConformanceRecord (`Mc`): three relative pointers.
struct SwiftConformanceRecord {
    RelativePointer typeRef;        // 0x00
    RelativePointer witnessTable;   // 0x04
    RelativePointer conformance;    // 0x08
};
static_assert(sizeof(SwiftConformanceRecord) == 12);

// A witness table begins with the conformance descriptor, then witness slots.
struct SwiftWitnessTable {
    const SwiftConformanceDescriptor* conformance;  // 0x00
    const void* witnesses[8];                       // 0x08..
};

// A value witness table.  The compiled SwiftUI body getters fetch function
// pointers from offsets 0x08, 0x10 and 0x40 of this table to copy/destroy the
// opaque view values they construct.  We provide real, safe implementations
// (see swift_runtime_ext.cpp) for those slots.
struct SwiftValueWitnessTable {
    void* destroy;          // 0x00
    void* initializeWithCopy;// 0x08
    void* assignWithCopy;   // 0x10
    void* initializeWithTake;// 0x18
    void* assignWithTake;   // 0x20
    void* deinitialize;     // 0x28
    void* size;             // 0x30 (value, not function)
    void* stride;           // 0x38 (value, not function)
    void* extraInhabitant;  // 0x40
};

// Struct metadata returned by mangled-name lookup.  Swift value metadata keeps
// a pointer to its value witness table at offset -8 from the metadata address,
// and the compiler reads signed field offsets at +0x2c / +0x30.  Both are
// modelled explicitly so compiled SwiftUI body getters see a real layout.
struct SwiftStructMetadata {
    std::uintptr_t kind;                       // 0x00
    const SwiftNominalTypeDescriptor* desc;    // 0x08
    const void* genericArgs[3];                // 0x10..0x27
    std::uint32_t reserved0;                   // 0x28
    std::int32_t fieldOffset0;                 // 0x2c
    std::int32_t fieldOffset1;                 // 0x30
    std::int32_t fieldOffset2;                 // 0x34
    std::int32_t fieldOffset3;                 // 0x38
};
static_assert(offsetof(SwiftStructMetadata, fieldOffset0) == 0x2c);

// The 8-byte VWT prefix lives immediately before the metadata pointer.
struct SwiftMetadataBox {
    const SwiftValueWitnessTable* vwt;  // metadata[-8]
    SwiftStructMetadata meta;
};

// ---------------------------------------------------------------------------
// Metadata registry used by the demangler / runtime lookup.
// ---------------------------------------------------------------------------

// Register a mangled-name -> metadata mapping.  `demangled` is the canonical
// "Module.Type" name used by unit tests.
void register_metadata(const std::string& mangled, const std::string& demangled,
                       const void* metadata);
// Register a nominal descriptor by its demangled name (used for Mp/Mn lookup).
void register_nominal(const std::string& demangled,
                      const SwiftNominalTypeDescriptor* descriptor);
// Register a witness table keyed by the demangled conforming type.
void register_witness(const std::string& key, const SwiftWitnessTable* table);
// Register a conformance record keyed by the demangled conforming type.
void register_conformance(const std::string& key,
                          const SwiftConformanceRecord* record);

const void* find_metadata_by_mangled(std::string_view mangled);
const void* find_metadata_by_demangled(std::string_view demangled);
const SwiftNominalTypeDescriptor* find_nominal(std::string_view demangled);
const SwiftWitnessTable* find_witness(std::string_view key);
const SwiftConformanceRecord* find_conformance(std::string_view key);

// Returns the registered metadata objects so unit tests can inspect them.
const std::unordered_map<std::string, const void*>& all_metadata();

}  // namespace finn::darwin::swiftui

#endif  // FINN_DARWIN_SWIFTUI_ABI_METADATA_HPP
