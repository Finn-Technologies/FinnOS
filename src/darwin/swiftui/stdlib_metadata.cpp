// Generic Swift standard-library metadata materializer.
//
// See stdlib_metadata.hpp for the honesty contract.  All ABI constants below
// were extracted from a linked Swift 6.4.2 libswiftCore.dylib on the host:
//   dlsym("$sSHMp") -> TargetProtocolDescriptor header + trailing requirement
//   records; dlsym("$sSH9hashValueSivgTq") lands on Hashable.req[1], which is
//   exactly what the `Tq` symbol means in Swift's reflection metadata.
//
// We deliberately do NOT reuse abi_metadata.hpp's simplified 16-byte protocol
// descriptor: compiled apps read the real 24-byte TargetProtocolDescriptor
// layout, so this module defines and emits that layout itself.

#include "stdlib_metadata.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace finn::darwin::swiftui {
namespace {

// ---------------------------------------------------------------------------
// Swift 6.4 ABI layouts (64-bit, little endian, no pointer authentication on
// the Linux target).  The pointer-auth signature bits present in the host
// dylib's requirement flags live in the high 16 bits; the low 16 bits are the
// real ProtocolRequirementFlags we reproduce here.
// ---------------------------------------------------------------------------

struct TargetProtocolDescriptorHeader {
    std::uint32_t Flags;                 // 0x00 ContextDescriptorFlags
    std::int32_t Parent;                 // 0x04 relative direct (0 = null)
    std::int32_t Name;                   // 0x08 relative direct, C string
    std::uint32_t NumRequirementsInSignature;  // 0x0c
    std::uint32_t NumRequirements;       // 0x10
    std::int32_t AssociatedTypeNames;    // 0x14 relative direct (0 = null)
};
static_assert(sizeof(TargetProtocolDescriptorHeader) == 24);

struct TargetGenericRequirement {
    std::uint32_t Flags;                 // 0x00 GenericRequirementFlags
    std::int32_t Param;                  // 0x04 relative direct, mangled name
    std::uint32_t Extra;                 // 0x08 union; 0 = no constraint value
};
static_assert(sizeof(TargetGenericRequirement) == 12);

struct TargetProtocolRequirement {
    std::uint32_t Flags;                 // 0x00 ProtocolRequirementFlags
    std::int32_t DefaultImplementation;  // 0x04 relative direct (0 = none)
};
static_assert(sizeof(TargetProtocolRequirement) == 8);

// Real TargetProtocolConformanceDescriptor: protocol, typeRef, witness-table
// pattern, flags.  Every field is a relative pointer measured from itself.
struct TargetConformanceDescriptor {
    std::int32_t Protocol;
    std::int32_t TypeRef;
    std::int32_t WitnessTablePattern;
    std::uint32_t Flags;
};
static_assert(sizeof(TargetConformanceDescriptor) == 16);

// A witness table starts with a pointer to its conformance descriptor.
struct TargetWitnessTable {
    const TargetConformanceDescriptor* Description;
    void* Witnesses[8];
};

// Real value witness table: eight function slots, four data slots.
struct TargetValueWitnessTable {
    void* InitializeBufferWithCopyOfBuffer;
    void* Destroy;
    void* InitializeWithCopy;
    void* AssignWithCopy;
    void* InitializeWithTake;
    void* AssignWithTake;
    void* GetEnumTagSinglePayload;
    void* StoreEnumTagSinglePayload;
    std::uintptr_t Size;
    std::uintptr_t Stride;
    std::uint32_t Flags;                 // low 3 bits: alignment mask
    std::uint32_t ExtraInhabitantCount;
};
static_assert(sizeof(TargetValueWitnessTable) == 88);

// Minimal real nominal context descriptor used as the conformance typeRef.
struct TargetNominalDescriptor {
    std::uint32_t Flags;                 // 0x51 = struct, Swift version 1
    std::int32_t Parent;
    std::int32_t Name;
    std::int32_t AccessFunction;
    std::int32_t Fields;
};
static_assert(sizeof(TargetNominalDescriptor) == 20);

// ---------------------------------------------------------------------------
// Table-driven specs.  Generated from host libswiftCore 6.4.2.
// ---------------------------------------------------------------------------

struct SignatureSpec {
    std::uint32_t flags;
    const char* param;
    std::uint32_t extra;  // constraint value from the host; we emit 0 (absent)
};

struct ProtocolSpec {
    const char* symbol;                  // e.g. "$sSHMp"
    std::uint32_t flags;
    std::uint32_t num_sig;
    std::uint32_t num_req;
    const char* assoc_names;             // nullptr when the protocol has none
    const char* name;                    // plain name stored in the descriptor
    SignatureSpec sig[2];
    std::uint32_t req[21];
};

static const ProtocolSpec kProtocols[] = {
{"$sSHMp", 0x00030043u, 2u, 4u, nullptr, "Hashable", {{0x0080u, "x", 0}, {0x0005u, "x", 0}}, {0x0000u, 0x0013u, 0x0011u, 0x0011u}},
{"$sSQMp", 0x00030043u, 1u, 1u, nullptr, "Equatable", {{0x0005u, "x", 0}}, {0x0001u}},
{"$sSLMp", 0x00030043u, 2u, 5u, nullptr, "Comparable", {{0x0080u, "x", 0}, {0x0005u, "x", 0}}, {0x0000u, 0x0001u, 0x0001u, 0x0001u, 0x0001u}},
{"$sSYMp", 0x00030043u, 0u, 3u, "RawValue", "RawRepresentable", {}, {0x0007u, 0x0002u, 0x0013u}},
{"$sSEMp", 0x00030043u, 0u, 1u, nullptr, "Encodable", {}, {0x0011u}},
{"$sSeMp", 0x00030043u, 0u, 1u, nullptr, "Decodable", {}, {0x0002u}},
{"$ss10SetAlgebraMp", 0x00030043u, 2u, 21u, "Element", "SetAlgebra", {{0x0080u, "x", 0}, {0x0080u, "x", 0}}, {0x0000u, 0x0000u, 0x0007u, 0x0002u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0011u, 0x0013u, 0x0002u, 0x0011u}},
{"$ss12CaseIterableMp", 0x00030043u, 2u, 3u, "AllCases", "CaseIterable", {{0x0001u, "x", 0}, {0x0080u, "$ss12CaseIterableP8AllCasesQz", 0}}, {0x0008u, 0x0007u, 0x0003u}},
{"$ss12IdentifiableMp", 0x00030043u, 1u, 3u, "ID", "Identifiable", {{0x0080u, "x", 0}}, {0x0008u, 0x0007u, 0x0013u}},
{"$ss20_SwiftNewtypeWrapperMp", 0x00030043u, 2u, 2u, nullptr, "_SwiftNewtypeWrapper", {{0x0080u, "x", 0}, {0x0080u, "x", 0}}, {0x0000u, 0x0000u}},
{"$ss21_ObjectiveCBridgeableMp", 0x00030043u, 1u, 5u, "_ObjectiveCType", "_ObjectiveCBridgeable", {{0x001fu, "x", 0}}, {0x0007u, 0x0011u, 0x0001u, 0x0001u, 0x0001u}},
{"$ss23CustomStringConvertibleMp", 0x00030043u, 0u, 1u, nullptr, "CustomStringConvertible", {}, {0x0013u}},
{"$ss25ExpressibleByArrayLiteralMp", 0x00030043u, 0u, 2u, "ArrayLiteralElement", "ExpressibleByArrayLiteral", {}, {0x0007u, 0x0002u}},
{"$ss28CustomDebugStringConvertibleMp", 0x00030043u, 0u, 1u, nullptr, "CustomDebugStringConvertible", {}, {0x0013u}},
{"$ss35_HasCustomAnyHashableRepresentationMp", 0x00030043u, 0u, 1u, nullptr, "_HasCustomAnyHashableRepresentation", {}, {0x0011u}},
{"$ss5ErrorMp", 0x00070043u, 0u, 4u, nullptr, "Error", {}, {0x0013u, 0x0013u, 0x0013u, 0x0011u}},
{"$ss7CVarArgMp", 0x00030043u, 0u, 1u, nullptr, "CVarArg", {}, {0x0013u}},
{"$ss9CodingKeyMp", 0x00030043u, 2u, 6u, nullptr, "CodingKey", {{0x0080u, "x", 0}, {0x0080u, "x", 0}}, {0x0000u, 0x0000u, 0x0013u, 0x0002u, 0x0013u, 0x0002u}},
{"$ss9OptionSetMp", 0x00030043u, 2u, 3u, nullptr, "OptionSet", {{0x0080u, "x", 0}, {0x0080u, "x", 0}}, {0x0000u, 0x0000u, 0x0002u}},
// Swift.Observable from libswiftObservation.dylib (0 requirements).
{"$s11Observation10ObservableMp", 0x00030043u, 0u, 0u, nullptr, "Observable", {}, {}},
};

struct RequirementRow {
    const char* symbol;
    std::uint32_t protocol;              // index into kProtocols
    std::uint32_t index;                 // requirement slot
    std::uint32_t flags;                 // verified against the spec
};

// Tq/Tb symbols are requirement records, not functions.  Each row's address
// is set to &block.req[index] so the pointer identity matches the real stdlib.
static const RequirementRow kRequirements[] = {
    {"_$sSE6encode2toys7Encoder_p_tKFTq", 4, 0u, 0x0011u},
    {"_$sSH13_rawHashValue4seedS2i_tFTq", 0, 3u, 0x0011u},
    {"_$sSH4hash4intoys6HasherVz_tFTq", 0, 2u, 0x0011u},
    {"_$sSH9hashValueSivgTq", 0, 1u, 0x0013u},
    {"_$sSHSQTb", 0, 0u, 0x0000u},
    {"_$sSL1goiySbx_xtFZTq", 2, 4u, 0x0001u},
    {"_$sSL1loiySbx_xtFZTq", 2, 1u, 0x0001u},
    {"_$sSL2geoiySbx_xtFZTq", 2, 3u, 0x0001u},
    {"_$sSL2leoiySbx_xtFZTq", 2, 2u, 0x0001u},
    {"_$sSLSQTb", 2, 0u, 0x0000u},
    {"_$sSQ2eeoiySbx_xtFZTq", 1, 0u, 0x0001u},
    {"_$sSY8rawValue03RawB0QzvgTq", 3, 2u, 0x0013u},
    {"_$sSY8rawValuexSg03RawB0Qz_tcfCTq", 3, 1u, 0x0002u},
    {"_$sSe4fromxs7Decoder_p_tKcfCTq", 5, 0u, 0x0002u},
    {"_$ss10SetAlgebraP10isDisjoint4withSbx_tFTq", 6, 16u, 0x0011u},
    {"_$ss10SetAlgebraP10isSuperset2ofSbx_tFTq", 6, 17u, 0x0011u},
    {"_$ss10SetAlgebraP11subtractingyxxFTq", 6, 14u, 0x0011u},
    {"_$ss10SetAlgebraP12intersectionyxxFTq", 6, 6u, 0x0011u},
    {"_$ss10SetAlgebraP16formIntersectionyyxFTq", 6, 12u, 0x0011u},
    {"_$ss10SetAlgebraP19symmetricDifferenceyxxnFTq", 6, 7u, 0x0011u},
    {"_$ss10SetAlgebraP23formSymmetricDifferenceyyxnFTq", 6, 13u, 0x0011u},
    {"_$ss10SetAlgebraP5unionyxxnFTq", 6, 5u, 0x0011u},
    {"_$ss10SetAlgebraP6insertySb8inserted_7ElementQz17memberAfterInserttAFnFTq", 6, 8u, 0x0011u},
    {"_$ss10SetAlgebraP6removey7ElementQzSgAEFTq", 6, 9u, 0x0011u},
    {"_$ss10SetAlgebraP6update4with7ElementQzSgAFn_tFTq", 6, 10u, 0x0011u},
    {"_$ss10SetAlgebraP7isEmptySbvgTq", 6, 18u, 0x0013u},
    {"_$ss10SetAlgebraP8containsySb7ElementQzFTq", 6, 4u, 0x0011u},
    {"_$ss10SetAlgebraP8isSubset2ofSbx_tFTq", 6, 15u, 0x0011u},
    {"_$ss10SetAlgebraP8subtractyyxFTq", 6, 20u, 0x0011u},
    {"_$ss10SetAlgebraP9formUnionyyxnFTq", 6, 11u, 0x0011u},
    {"_$ss10SetAlgebraPSQTb", 6, 0u, 0x0000u},
    {"_$ss10SetAlgebraPs25ExpressibleByArrayLiteralTb", 6, 1u, 0x0000u},
    {"_$ss10SetAlgebraPxycfCTq", 6, 3u, 0x0002u},
    {"_$ss10SetAlgebraPyxqd__ncSTRd__7ElementQyd__ACRtzlufCTq", 6, 19u, 0x0002u},
    {"_$ss12CaseIterableP8allCases03AllD0QzvgZTq", 7, 2u, 0x0003u},
    {"_$ss12IdentifiableP2id2IDQzvgTq", 8, 2u, 0x0013u},
    {"_$ss20_SwiftNewtypeWrapperPSYTb", 9, 0u, 0x0000u},
    {"_$ss20_SwiftNewtypeWrapperPs35_HasCustomAnyHashableRepresentationTb", 9, 1u, 0x0000u},
    {"_$ss21_ObjectiveCBridgeableP016_forceBridgeFromA1C_6resulty01_A5CTypeQz_xSgztFZTq", 10, 2u, 0x0001u},
    {"_$ss21_ObjectiveCBridgeableP024_conditionallyBridgeFromA1C_6resultSb01_A5CTypeQz_xSgztFZTq", 10, 3u, 0x0001u},
    {"_$ss21_ObjectiveCBridgeableP026_unconditionallyBridgeFromA1Cyx01_A5CTypeQzSgFZTq", 10, 4u, 0x0001u},
    {"_$ss21_ObjectiveCBridgeableP09_bridgeToA1C01_A5CTypeQzyFTq", 10, 1u, 0x0011u},
    {"_$ss23CustomStringConvertibleP11descriptionSSvgTq", 11, 0u, 0x0013u},
    {"_$ss25ExpressibleByArrayLiteralP05arrayD0x0cD7ElementQzd_tcfCTq", 12, 1u, 0x0002u},
    {"_$ss28CustomDebugStringConvertibleP16debugDescriptionSSvgTq", 13, 0u, 0x0013u},
    {"_$ss35_HasCustomAnyHashableRepresentationP03_tobcD0s0cD0VSgyFTq", 14, 0u, 0x0011u},
    {"_$ss9CodingKeyP11stringValueSSvgTq", 17, 2u, 0x0013u},
    {"_$ss9CodingKeyP11stringValuexSgSS_tcfCTq", 17, 3u, 0x0002u},
    {"_$ss9CodingKeyP8intValueSiSgvgTq", 17, 4u, 0x0013u},
    {"_$ss9CodingKeyP8intValuexSgSi_tcfCTq", 17, 5u, 0x0002u},
    {"_$ss9CodingKeyPs23CustomStringConvertibleTb", 17, 1u, 0x0000u},
    {"_$ss9CodingKeyPs28CustomDebugStringConvertibleTb", 17, 0u, 0x0000u},
    {"_$ss9OptionSetP8rawValuex03RawD0Qz_tcfCTq", 18, 2u, 0x0002u},
    {"_$ss9OptionSetPSYTb", 18, 0u, 0x0000u},
    {"_$ss9OptionSetPs0B7AlgebraTb", 18, 1u, 0x0000u},
    // Associated-type / associated-conformance records (same layout, same
    // requirement arrays; verified by dlsym address identity on Swift 6.4.2).
    {"_$s8RawValueSYTl", 3, 0u, 0x0007u},
    {"_$s7Elements10SetAlgebraPTl", 6, 2u, 0x0007u},
    {"_$s8AllCasess12CaseIterablePTl", 7, 1u, 0x0007u},
    {"_$ss12CaseIterableP8AllCasesAB_SlTn", 7, 0u, 0x0008u},
    {"_$s2IDs12IdentifiablePTl", 8, 1u, 0x0007u},
    {"_$ss12IdentifiableP2IDAB_SHTn", 8, 0u, 0x0008u},
    {"_$s19ArrayLiteralElements013ExpressibleByaB0PTl", 12, 0u, 0x0007u},
    {"_$s15_ObjectiveCTypes01_A11CBridgeablePTl", 10, 0u, 0x0007u},
};

// Associated-type and associated-conformance reflection symbols (`Tl`/`Tn`)
// are also TargetProtocolRequirement records -- dlsym address identity checked.
// Add them to the same table by listing them here before construction.
// (the Tl/Tn rows are appended directly to kRequirements above)

// Nominal type descriptors copied byte-for-byte (flags, name, trailing words)
// from the host Swift 6.4.2 / CoreGraphics dylibs.  Parent, access function and
// reflection field descriptor are deliberately absent (zero) in our copies.
struct NominalSpec {
    const char* symbol;                  // "ss5Int32VMn"; full symbol is "_$"+symbol
    std::uint32_t flags;
    const char* name;
    std::uint32_t trailing0;
    std::uint32_t trailing1;
};

static const NominalSpec kNominals[] = {
{"s12CoreGraphics7CGFloatVMn", 0x00100051u, "CGFloat", 0x00000001u, 0x00000002u},
{"ss5Int32VMn", 0x00000051u, "Int32", 0x00000001u, 0x00000002u},
{"ss6UInt32VMn", 0x00000051u, "UInt32", 0x00000001u, 0x00000002u},
{"ss5Int64VMn", 0x00000051u, "Int64", 0x00000001u, 0x00000002u},
{"ss6UInt64VMn", 0x00000051u, "UInt64", 0x00000001u, 0x00000002u},
{"ss5UInt8VMn", 0x00000051u, "UInt8", 0x00000001u, 0x00000002u},
{"sSiMn", 0x00000051u, "Int", 0x00000001u, 0x00000002u},
{"sSbMn", 0x00000051u, "Bool", 0x00000001u, 0x00000002u},
{"sSSMn", 0x00000051u, "String", 0x00000001u, 0x00000002u},
{"ss6ResultOMn", 0x000000f2u, "Result", 0x05000002u, 0x00000000u},
{"ss11AnyHashableVMn", 0x00000051u, "AnyHashable", 0x00000001u, 0x00000002u},
{"ss5NeverOMn", 0x00000052u, "Never", 0x00000000u, 0x00000000u},
{"ss12StaticStringVMn", 0x00000051u, "StaticString", 0x00000003u, 0x00000002u},
{"ss13OpaquePointerVMn", 0x00000051u, "OpaquePointer", 0x00000001u, 0x00000002u},
{"ss11_SetStorageCMn", 0xc00800d0u, "_SetStorage", 0xfffef13cu, 0x00000003u},
{"ss18_DictionaryStorageCMn", 0xc00800d0u, "_DictionaryStorage", 0xfffeecfau, 0x00000003u},
{"ss22KeyedDecodingContainerVMn", 0x000000d1u, "KeyedDecodingContainer", 0x00000001u, 0x00000004u},
{"ss22KeyedEncodingContainerVMn", 0x000000d1u, "KeyedEncodingContainer", 0x00000001u, 0x00000004u},
{"s10Foundation11JSONEncoderC16OutputFormattingVMn", 0x00100051u, "OutputFormatting", 0x00000001u, 0x00000002u},
{"s10Foundation13URLComponentsVMn", 0x00100051u, "URLComponents", 0x00000001u, 0x00000002u},
{"s10Foundation24FloatingPointFormatStyleVMn", 0x001000d1u, "FloatingPointFormatStyle", 0x00000002u, 0x00000004u},
{"s10Foundation4DataVMn", 0x00100051u, "Data", 0x00000001u, 0x00000002u},
{"sSS10FoundationE8EncodingVMn", 0x00100051u, "Encoding", 0x00000001u, 0x00000002u},
{"s11Observation0A9RegistrarVMn", 0x00000051u, "ObservationRegistrar", 0x00000001u, 0x00000002u},
{"s8Dispatch0A13WorkItemFlagsVMn", 0x00100051u, "DispatchWorkItemFlags", 0x00000001u, 0x00000002u},
{"s8Dispatch0A8WorkItemCMn", 0x80000050u, "DispatchWorkItem", 0x00000000u, 0x00000003u},
{"s8Dispatch0A12TimeIntervalOMn", 0x00000052u, "DispatchTimeInterval", 0x00000004u, 0x00000001u},
{"sSo20NSNotificationCenterC10FoundationE9PublisherVMn", 0x00100051u, "Publisher", 0x00000003u, 0x00000002u},
{"sSo17OS_dispatch_queueC8DispatchE10AttributesVMn", 0x00100051u, "Attributes", 0x00000001u, 0x00000002u},
{"sSo9NSRunLoopC10FoundationE16SchedulerOptionsVMn", 0x00000051u, "SchedulerOptions", 0x00000000u, 0x00000002u},
{"ss7UnicodeO6ScalarV10PropertiesVMn", 0x00000051u, "Properties", 0x00000001u, 0x00000002u},
};

// Type metadata we can back with a genuine value witness table (POD values).
struct MetadataSpec {
    const char* symbol;                  // "...VN"
    const char* nominal;                 // key in kNominals
    std::size_t size;
};

static const MetadataSpec kMetadatas[] = {
    {"ss5Int32VN", "ss5Int32VMn", 4},
    {"ss6UInt32VN", "ss6UInt32VMn", 4},
    {"ss5Int64VN", "ss5Int64VMn", 8},
    {"ss6UInt64VN", "ss6UInt64VMn", 8},
    {"s12CoreGraphics7CGFloatVN", "s12CoreGraphics7CGFloatVMn", 8},
};

struct UnimplementedRow {
    const char* symbol;
    const char* reason;
};

static const UnimplementedRow kUnimplemented[] = {
    {"_$s10Foundation11JSONEncoderC16OutputFormattingVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s10Foundation11JSONEncoderC16OutputFormattingVs10SetAlgebraAAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s10Foundation12URLQueryItemVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s10Foundation13URLComponentsVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s10Foundation24FloatingPointFormatStyleVyxGAA09ParseabledE0AAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s10Foundation3URLVs23CustomStringConvertibleAAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s10Foundation4UUIDVSHAAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s10Foundation4UUIDVSQAAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s10Foundation6LocaleV8LanguageVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s10NSViewType7SwiftUI0A13RepresentablePTl", "reflection associated-type descriptor layout not embedded"},
    {"_$s11Coordinator7SwiftUI19NSViewRepresentablePTl", "reflection associated-type descriptor layout not embedded"},
    {"_$s11Observation0A9RegistrarVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s12CoreGraphics7CGFloatVSEAAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s12CoreGraphics7CGFloatVSeAAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s14CoreFoundation9_CFObjectMp", "protocol descriptor not derivable from host Swift 6.4.2 dylibs"},
    {"_$s14CoreFoundation9_CFObjectPSHTb", "base conformance record in a framework outside the host Swift stdlib"},
    {"_$s22UniformTypeIdentifiers6UTTypeVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s22UniformTypeIdentifiers6UTTypeVMn", "nominal type descriptor not backed by a real field layout"},
    {"_$s25ObjectWillChangePublisher7Combine010ObservableA0PTl", "reflection associated-type descriptor layout not embedded"},
    {"_$s7SwiftUI19NSViewRepresentablePAA4ViewTb", "base conformance record in a framework outside the host Swift stdlib"},
    {"_$s8Dispatch0A12TimeIntervalOMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s8Dispatch0A13WorkItemFlagsVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s8Dispatch0A13WorkItemFlagsVs10SetAlgebraAAMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$s8Dispatch0A3QoSV0B6SClassOMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s8Dispatch0A3QoSVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s8Dispatch0A4TimeVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$s8Dispatch0A8WorkItemCMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSDyxq_GSEsSERzSER_rlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSDyxq_GSesSeRzSeR_rlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSS10FoundationE17LocalizationValueV19StringInterpolationVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSS10FoundationE17LocalizationValueVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSS10FoundationE8EncodingVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSSSEsWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSSSTsWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSSSesWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSSSysMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSSs25LosslessStringConvertiblesWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSSs7CVarArg10FoundationMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSayxGSEsSERzlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSayxGSKsMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSayxGSTsMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSayxGSesSeRzlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSayxGSksMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSayxGSlsMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sScMMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sScMScAsMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sScPMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSdSBsMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSds7CVarArgsWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSfSBsMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sShyxGSEsSERzrlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sShyxGSesSeRzrlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSi10Foundation18_FormatSpecifiableAAWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSi7SwiftUI18_FormatSpecifiableAAWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSiSZsMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSiSxsWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSis23CustomStringConvertiblesWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSis7CVarArgsWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSnyxGSksSxRzSZ6StrideRpzrlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSo17OS_dispatch_queueC8DispatchE10AttributesVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSo17OS_dispatch_queueC8DispatchE10AttributesVs10SetAlgebraACMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSo17OS_dispatch_queueC8DispatchE20AutoreleaseFrequencyOMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSo20NSNotificationCenterC10FoundationE9PublisherV7CombineAdCMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSo20NSNotificationCenterC10FoundationE9PublisherVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSo8NSObjectC10Foundation27_KeyValueCodingAndObservingACWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$sSo9NSRunLoopC10FoundationE16SchedulerOptionsVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sSo9NSRunLoopC7Combine9Scheduler10FoundationMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sSqMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$ss11AnyHashableVN", "metadata object not backed by a real implementation"},
    {"_$ss11AnyHashableVSHsWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$ss11_StringGutsVN", "metadata object not backed by a real implementation"},
    {"_$ss26DefaultStringInterpolationVN", "metadata object not backed by a real implementation"},
    {"_$ss26DefaultStringInterpolationVs16TextOutputStreamsWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$ss28__ContiguousArrayStorageBaseCMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$ss5Int32Vs23CustomStringConvertiblesWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$ss5NeverO7SwiftUI4ViewACWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$ss6UInt32Vs23CustomStringConvertiblesWP", "concrete witness semantics require unimplemented framework behaviour"},
    {"_$ss7UnicodeO6ScalarV10PropertiesVMa", "nominal type metadata accessor not backed by a real metadata object"},
    {"_$sxSgSEsSERzlMc", "generic or framework conformance needs a full witness-table pattern"},
    {"_$sxSgSHsSHRzlMc", "generic or framework conformance needs a full witness-table pattern"},
};

// ---------------------------------------------------------------------------
// Allocation and relative-pointer helpers.  All objects are process-lifetime.
// ---------------------------------------------------------------------------

std::int32_t rel_to(std::size_t field_off, std::size_t target_off) {
    const auto delta = static_cast<std::ptrdiff_t>(target_off) -
                       static_cast<std::ptrdiff_t>(field_off);
    return static_cast<std::int32_t>(delta);
}

std::int32_t rel_abs(const void* field, const void* target) {
    const auto delta = reinterpret_cast<std::uintptr_t>(target) -
                       reinterpret_cast<std::uintptr_t>(field);
    return static_cast<std::int32_t>(delta);
}

std::uint8_t* alloc_zero(std::size_t n) {
    return static_cast<std::uint8_t*>(std::calloc(1, n));
}

struct FullNominalDescriptor;

struct Block {
    std::uint8_t* bytes = nullptr;
    TargetProtocolDescriptorHeader* header = nullptr;
    TargetProtocolRequirement* req = nullptr;
    TargetGenericRequirement* sig = nullptr;
};

struct Registration {
    std::mutex mu;
    std::vector<std::uint8_t*> allocations;
    std::vector<Block> blocks;
    std::unordered_map<std::string, void*> symbols;
    std::unordered_map<std::string, const FullNominalDescriptor*> nominals;
    std::size_t registered = 0;
};

Registration& registration() {
    static Registration r;
    return r;
}

// ---------------------------------------------------------------------------
// Reuse the SipHash implementation already registered by foundation_overlay.
// We never axiomatise a hash: if the real shims are absent, no Hashable
// witness table is registered at all.
// ---------------------------------------------------------------------------

using HashU64Fn = std::intptr_t (*)(std::intptr_t, std::uint64_t);
using HashCombineFn = std::intptr_t (*)(void*, std::uintptr_t);

HashU64Fn g_hash_u64 = nullptr;
HashCombineFn g_hash_combine = nullptr;

template <typename Fn>
void load_function(const SymbolTable& table, const char* symbol, Fn& out) {
    auto it = table.find(symbol);
    if (it == table.end() || it->second == nullptr) return;
    std::memcpy(&out, &it->second, sizeof(Fn));
}

template <std::size_t N>
std::uint64_t load_uint(const void* p) {
    std::uint64_t v = 0;
    if (p != nullptr) std::memcpy(&v, p, N);
    return v;
}

// ---------------------------------------------------------------------------
// Real witness functions.  Swift's generic calling convention appends hidden
// metadata/witness-table arguments; a C function reading only the leading
// arguments ignores the rest, which is safe on AAPCS64 and x86-64.
// ---------------------------------------------------------------------------

template <std::size_t N>
bool witness_equal(const void* a, const void* b, const void*, const void*) {
    if (a == nullptr || b == nullptr) return false;
    return load_uint<N>(a) == load_uint<N>(b);
}

template <std::size_t N>
std::intptr_t witness_hash_value(const void* self, const void*, const void*) {
    return g_hash_u64 != nullptr
               ? g_hash_u64(0, load_uint<N>(self))
               : 0;
}

template <std::size_t N>
void witness_hash_into(void* self, void* hasher, const void*, const void*) {
    if (g_hash_combine != nullptr && hasher != nullptr) {
        (void)g_hash_combine(hasher,
                             static_cast<std::uintptr_t>(load_uint<N>(self)));
    }
}

template <std::size_t N>
std::intptr_t witness_raw_hash(const void* self, std::intptr_t seed,
                               const void*, const void*) {
    return g_hash_u64 != nullptr
               ? g_hash_u64(seed, load_uint<N>(self))
               : 0;
}

template <typename Fn>
void* fn_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// Witness slot arrays.  Hashable's slot 0 is the inherited Equatable witness
// table pointer; the remaining three slots are the hash requirements.
const void* const* witness_slots_bool_eq() {
    static const void* const slots[] = {fn_ptr(&witness_equal<1>)};
    return slots;
}

const void* const* witness_slots_int_eq() {
    static const void* const slots[] = {fn_ptr(&witness_equal<8>)};
    return slots;
}

const void* const* witness_slots_int_hash() {
    static const void* const slots[] = {
        nullptr,  // base Equatable witness table, linked at construction
        fn_ptr(&witness_hash_into<8>),
        fn_ptr(&witness_hash_value<8>),
        fn_ptr(&witness_raw_hash<8>),
    };
    return slots;
}

const void* const* witness_slots_i32_hash() {
    static const void* const slots[] = {
        nullptr,  // no Int32: Equatable witness table registered here
        fn_ptr(&witness_hash_into<4>),
        fn_ptr(&witness_hash_value<4>),
        fn_ptr(&witness_raw_hash<4>),
    };
    return slots;
}

const void* const* witness_slots_u32_hash() {
    static const void* const slots[] = {
        nullptr,
        fn_ptr(&witness_hash_into<4>),
        fn_ptr(&witness_hash_value<4>),
        fn_ptr(&witness_raw_hash<4>),
    };
    return slots;
}

// Deterministic stand-ins used by the self-test only, never by production
// registration (which reuses foundation_overlay's real SipHash shims).
std::intptr_t test_hash_u64(std::intptr_t seed, std::uint64_t value) {
    return static_cast<std::intptr_t>(
        (value * 0x9e3779b97f4a7c15ull) ^ static_cast<std::uint64_t>(seed));
}

std::intptr_t test_hash_combine(void*, std::uintptr_t) {
    return 0;
}

// Reference-count stubs used only by the self-test to exercise the VWT wiring.
void* test_retain(void* p) { return p; }
void test_release(void*) {}
std::uintptr_t test_bridge_retain(std::uintptr_t bits) { return bits; }
void test_bridge_release(std::uintptr_t) {}

// ---------------------------------------------------------------------------
// Trivial bitwise value witnesses for Builtin.Int32 / Builtin.Int64.
// ---------------------------------------------------------------------------

template <std::size_t N>
void* vwt_copy(void* dest, void* src, const void*) {
    if (dest != nullptr && src != nullptr) std::memcpy(dest, src, N);
    return dest;
}

template <std::size_t N>
void vwt_destroy(void*, const void*) {}

// A 4/8-byte integer has no extra inhabitants: every payload bit pattern is a
// valid value, so the single-payload tag is always case zero and the payload
// bytes are never used to store a non-zero tag.
template <std::size_t N>
unsigned vwt_get_tag(const void*, unsigned, const void*) {
    return 0;
}

template <std::size_t N>
void vwt_set_tag(void*, unsigned which_case, unsigned, const void*) {
    (void)which_case;
}

// ---------------------------------------------------------------------------
// Descriptor builders.
// ---------------------------------------------------------------------------

Block make_protocol(const ProtocolSpec& spec, Registration& reg) {
    Block block;
    const std::size_t name_len = std::strlen(spec.name) + 1;
    const std::size_t assoc_len = spec.assoc_names != nullptr
                                      ? std::strlen(spec.assoc_names) + 1 : 0;
    std::size_t sig_param_len = 0;
    for (std::uint32_t i = 0; i < spec.num_sig; ++i) {
        sig_param_len += std::strlen(spec.sig[i].param) + 1;
    }

    const std::size_t sig_off = sizeof(TargetProtocolDescriptorHeader);
    const std::size_t req_off = sig_off + spec.num_sig * sizeof(TargetGenericRequirement);
    const std::size_t name_off = req_off + spec.num_req * sizeof(TargetProtocolRequirement);
    const std::size_t assoc_off = name_off + name_len;
    const std::size_t params_off = assoc_off + assoc_len;
    const std::size_t total = params_off + sig_param_len;

    std::uint8_t* bytes = alloc_zero(total);
    if (bytes == nullptr) return block;

    auto* header = reinterpret_cast<TargetProtocolDescriptorHeader*>(bytes);
    header->Flags = spec.flags;
    header->Parent = 0;  // no Swift module descriptor in this slice
    header->NumRequirementsInSignature = spec.num_sig;
    header->NumRequirements = spec.num_req;
    header->Name = rel_to(offsetof(TargetProtocolDescriptorHeader, Name), name_off);
    header->AssociatedTypeNames = spec.assoc_names != nullptr
        ? rel_to(offsetof(TargetProtocolDescriptorHeader, AssociatedTypeNames), assoc_off)
        : 0;
    std::memcpy(bytes + name_off, spec.name, name_len);
    if (spec.assoc_names != nullptr) {
        std::memcpy(bytes + assoc_off, spec.assoc_names, assoc_len);
    }

    auto* sig = reinterpret_cast<TargetGenericRequirement*>(bytes + sig_off);
    std::size_t param_cursor = params_off;
    for (std::uint32_t i = 0; i < spec.num_sig; ++i) {
        sig[i].Flags = spec.sig[i].flags;
        sig[i].Extra = 0;
        const std::size_t len = std::strlen(spec.sig[i].param) + 1;
        sig[i].Param = rel_to(sig_off + i * sizeof(TargetGenericRequirement) + 4, param_cursor);
        std::memcpy(bytes + param_cursor, spec.sig[i].param, len);
        param_cursor += len;
    }

    auto* req = reinterpret_cast<TargetProtocolRequirement*>(bytes + req_off);
    for (std::uint32_t i = 0; i < spec.num_req; ++i) {
        req[i].Flags = spec.req[i];
        req[i].DefaultImplementation = 0;  // no default implementations claimed
    }

    block.bytes = bytes;
    block.header = header;
    block.sig = sig;
    block.req = req;
    reg.allocations.push_back(bytes);
    reg.blocks.push_back(block);
    return block;
}

std::uint8_t* make_nominal(const char* mangled, Registration& reg) {
    const std::size_t len = std::strlen(mangled) + 1;
    std::uint8_t* bytes = alloc_zero(sizeof(TargetNominalDescriptor) + len);
    if (bytes == nullptr) return nullptr;
    auto* desc = reinterpret_cast<TargetNominalDescriptor*>(bytes);
    desc->Flags = 0x51u;  // struct, Swift version 1 (derived from a 6.4 snippet)
    desc->Parent = 0;
    desc->AccessFunction = 0;
    desc->Fields = 0;
    desc->Name = rel_to(offsetof(TargetNominalDescriptor, Name), sizeof(TargetNominalDescriptor));
    std::memcpy(bytes + sizeof(TargetNominalDescriptor), mangled, len);
    reg.allocations.push_back(bytes);
    return bytes;
}

// ---------------------------------------------------------------------------
// Nominal type descriptors copied from the host dylibs.  These are real
// TargetTypeContextDescriptor prefixes (20 bytes) plus the host's two trailing
// words; Parent/AccessFunction/Fields are absent (zero) in our copies.
// ---------------------------------------------------------------------------

struct FullNominalDescriptor {
    std::uint32_t Flags;
    std::int32_t Parent;
    std::int32_t Name;
    std::int32_t AccessFunction;
    std::int32_t Fields;
    std::uint32_t Trailing0;
    std::uint32_t Trailing1;
};
static_assert(sizeof(FullNominalDescriptor) == 28);

const FullNominalDescriptor* make_full_nominal(const NominalSpec& spec,
                                               Registration& reg) {
    const std::size_t len = std::strlen(spec.name) + 1;
    std::uint8_t* bytes = alloc_zero(sizeof(FullNominalDescriptor) + len);
    if (bytes == nullptr) return nullptr;
    auto* desc = reinterpret_cast<FullNominalDescriptor*>(bytes);
    desc->Flags = spec.flags;
    desc->Parent = 0;
    desc->AccessFunction = 0;
    desc->Fields = 0;
    desc->Trailing0 = spec.trailing0;
    desc->Trailing1 = spec.trailing1;
    desc->Name = rel_to(offsetof(FullNominalDescriptor, Name),
                        sizeof(FullNominalDescriptor));
    std::memcpy(bytes + sizeof(FullNominalDescriptor), spec.name, len);
    reg.allocations.push_back(bytes);
    return desc;
}

// A real type metadata object: the value witness table lives at -8, the
// metadata kind and nominal descriptor pointer at +0/+8.
struct RealTypeMetadata {
    const TargetValueWitnessTable* Vwt;
    std::uintptr_t Kind;
    const void* Description;
    std::uintptr_t Reserved[6];
};

const void* make_type_metadata(const TargetValueWitnessTable* vwt,
                               const FullNominalDescriptor* desc,
                               Registration& reg) {
    auto* full = static_cast<RealTypeMetadata*>(static_cast<void*>(
        alloc_zero(sizeof(RealTypeMetadata))));
    if (full == nullptr) return nullptr;
    reg.allocations.push_back(reinterpret_cast<std::uint8_t*>(full));
    full->Vwt = vwt;
    full->Kind = (desc->Flags & 0x1fu) == 18u ? 0x201u : 0x200u;
    full->Description = desc;
    return &full->Kind;
}

// ---------------------------------------------------------------------------
// Reference-counted value witnesses.  The retain/release entry points come
// from the already-registered Swift runtime; without them no object VWT is
// claimed.  The single-payload enum-tag slots are absent (null) because a
// reference payload has no out-of-line tag we can safely materialise.
// ---------------------------------------------------------------------------

using RetainFn = void* (*)(void*);
using ReleaseFn = void (*)(void*);
using BridgeRetainFn = std::uintptr_t (*)(std::uintptr_t);
using BridgeReleaseFn = void (*)(std::uintptr_t);

RetainFn g_retain = nullptr;
ReleaseFn g_release = nullptr;
RetainFn g_unknown_retain = nullptr;
ReleaseFn g_unknown_release = nullptr;
BridgeRetainFn g_bridge_retain = nullptr;
BridgeReleaseFn g_bridge_release = nullptr;

struct NativeRefPolicy {
    static void* retain(void* p) { return g_retain != nullptr ? g_retain(p) : p; }
    static void release(void* p) { if (g_release != nullptr) g_release(p); }
};

struct UnknownRefPolicy {
    static void* retain(void* p) {
        return g_unknown_retain != nullptr ? g_unknown_retain(p) : p;
    }
    static void release(void* p) {
        if (g_unknown_release != nullptr) g_unknown_release(p);
    }
};

struct BridgeRefPolicy {
    static void* retain(void* p) {
        const auto bits = reinterpret_cast<std::uintptr_t>(p);
        return reinterpret_cast<void*>(g_bridge_retain != nullptr
                                           ? g_bridge_retain(bits) : bits);
    }
    static void release(void* p) {
        if (g_bridge_release != nullptr) {
            g_bridge_release(reinterpret_cast<std::uintptr_t>(p));
        }
    }
};

template <typename Policy>
void* ref_init_copy(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    void* p = src != nullptr ? *static_cast<void**>(src) : nullptr;
    *static_cast<void**>(dest) = Policy::retain(p);
    return dest;
}

template <typename Policy>
void* ref_assign_copy(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    void* p = src != nullptr ? *static_cast<void**>(src) : nullptr;
    void* held = Policy::retain(p);
    void* old = *static_cast<void**>(dest);
    *static_cast<void**>(dest) = held;
    Policy::release(old);
    return dest;
}

template <typename Policy>
void* ref_init_take(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    *static_cast<void**>(dest) =
        src != nullptr ? *static_cast<void**>(src) : nullptr;
    return dest;
}

template <typename Policy>
void* ref_assign_take(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    void* old = *static_cast<void**>(dest);
    *static_cast<void**>(dest) =
        src != nullptr ? *static_cast<void**>(src) : nullptr;
    Policy::release(old);
    return dest;
}

template <typename Policy>
void ref_destroy(void* object, const void*) {
    if (object != nullptr) Policy::release(*static_cast<void**>(object));
}

template <typename Policy>
TargetValueWitnessTable* make_ref_vwt(Registration& reg) {
    auto* vwt = static_cast<TargetValueWitnessTable*>(
        static_cast<void*>(alloc_zero(sizeof(TargetValueWitnessTable))));
    if (vwt == nullptr) return nullptr;
    reg.allocations.push_back(reinterpret_cast<std::uint8_t*>(vwt));
    vwt->InitializeBufferWithCopyOfBuffer = fn_ptr(&ref_init_copy<Policy>);
    vwt->InitializeWithCopy = fn_ptr(&ref_init_copy<Policy>);
    vwt->AssignWithCopy = fn_ptr(&ref_assign_copy<Policy>);
    vwt->InitializeWithTake = fn_ptr(&ref_init_take<Policy>);
    vwt->AssignWithTake = fn_ptr(&ref_assign_take<Policy>);
    vwt->Destroy = fn_ptr(&ref_destroy<Policy>);
    vwt->Size = 8;
    vwt->Stride = 8;
    vwt->Flags = 7u;
    vwt->ExtraInhabitantCount = 0;  // enum-tag slots stay null (reported)
    return vwt;
}

// ---------------------------------------------------------------------------
// String representation and SipHash-2-4 byte streaming.  The String layout is
// the one documented in foundation_overlay.cpp; SipHash constants and seed are
// the same, but a local byte-streaming implementation is needed because the
// registered Hasher shims only accept whole UInt64 chunks.
// ---------------------------------------------------------------------------

struct SwiftStringBits {
    std::uint64_t word0;
    std::uint64_t word1;
};

bool read_swift_string(const void* self, std::string& out) {
    if (self == nullptr) return false;
    SwiftStringBits bits;
    std::memcpy(&bits, self, sizeof(bits));
    const auto tag1 = static_cast<std::uint8_t>(bits.word1 >> 56);
    if ((tag1 & 0xe0u) == 0xe0u) {  // small/inline form
        const std::size_t n = tag1 & 0x1fu;
        out.assign(n, '\0');
        std::memcpy(&out[0], &bits.word0, std::min<std::size_t>(8, n));
        if (n > 8) std::memcpy(&out[8], &bits.word1, n - 8);
        return true;
    }
    const auto tag0 = static_cast<std::uint8_t>(bits.word0 >> 56);
    if (tag0 == 0xd0u || tag0 == 0x90u) {  // native / ASCII heap form
        const std::size_t n = static_cast<std::size_t>(bits.word0 & 0xffffffffu);
        const char* ptr = reinterpret_cast<const char*>(bits.word1);
        if (ptr == nullptr || n > (1u << 24)) return false;
        out.assign(ptr, n);
        return true;
    }
    return false;  // unknown representation: never invent content
}

constexpr std::uint64_t kSipC0 = 0x736f6d6570736575ull;
constexpr std::uint64_t kSipC1 = 0x646f72616e646f6dull;
constexpr std::uint64_t kSipC2 = 0x6c7967656e657261ull;
constexpr std::uint64_t kSipC3 = 0x7465646279746573ull;
constexpr std::uint64_t kExecSeed0 = 0x9e3779b97f4a7c15ull;
constexpr std::uint64_t kExecSeed1 = 0xbf58476d1ce4e5b9ull;

std::uint64_t rotl64(std::uint64_t x, unsigned b) {
    return (x << b) | (x >> (64 - b));
}

void sip_round(std::uint64_t* v) {
    v[0] += v[1]; v[1] = rotl64(v[1], 13); v[1] ^= v[0]; v[0] = rotl64(v[0], 32);
    v[2] += v[3]; v[3] = rotl64(v[3], 16); v[3] ^= v[2];
    v[0] += v[3]; v[3] = rotl64(v[3], 21); v[3] ^= v[0];
    v[2] += v[1]; v[1] = rotl64(v[1], 17); v[1] ^= v[2]; v[2] = rotl64(v[2], 32);
}

void sip_compress(std::uint64_t* v, std::uint64_t m) {
    v[3] ^= m;
    sip_round(v);
    v[0] ^= m;
}

std::intptr_t sip_hash_bytes(const std::string& bytes, std::uint64_t seed) {
    std::uint64_t v[4] = {
        kSipC0 ^ (kExecSeed0 ^ seed),
        kSipC1 ^ kExecSeed1,
        kSipC2 ^ (kExecSeed0 ^ seed),
        kSipC3 ^ kExecSeed1,
    };
    std::size_t pos = 0;
    for (; pos + 8 <= bytes.size(); pos += 8) {
        std::uint64_t m = 0;
        std::memcpy(&m, bytes.data() + pos, 8);
        sip_compress(v, m);
    }
    const std::size_t tail = bytes.size() - pos;
    std::uint64_t last = static_cast<std::uint64_t>(tail) << 56;
    for (std::size_t i = 0; i < tail; ++i) {
        last |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[pos + i]))
                << (i * 8);
    }
    sip_compress(v, last);
    v[2] ^= 0xff;
    for (int i = 0; i < 3; ++i) sip_round(v);
    return static_cast<std::intptr_t>(v[0] ^ v[1] ^ v[2] ^ v[3]);
}

bool string_equal(const void* a, const void* b, const void*, const void*) {
    std::string x, y;
    if (!read_swift_string(a, x) || !read_swift_string(b, y)) return false;
    return x == y;
}

std::intptr_t string_hash_value(const void* self, const void*, const void*) {
    std::string s;
    if (!read_swift_string(self, s)) return 0;
    return sip_hash_bytes(s, 0);
}

std::intptr_t string_raw_hash(const void* self, std::intptr_t seed,
                              const void*, const void*) {
    std::string s;
    if (!read_swift_string(self, s)) return 0;
    return sip_hash_bytes(s, static_cast<std::uint64_t>(seed));
}

// The registered Hasher shim only accepts whole UInt64 words, so the final
// short chunk is zero-padded.  This stays deterministic and equality-consistent;
// it is not byte-for-byte Swift's own String hash(into:) stream.
void string_hash_into(void* self, void* hasher, const void*, const void*) {
    std::string s;
    if (!read_swift_string(self, s) || hasher == nullptr || g_hash_combine == nullptr) {
        return;
    }
    for (std::size_t pos = 0; pos < s.size(); pos += 8) {
        std::uint64_t chunk = 0;
        const std::size_t n = std::min<std::size_t>(8, s.size() - pos);
        std::memcpy(&chunk, s.data() + pos, n);
        (void)g_hash_combine(hasher, static_cast<std::uintptr_t>(chunk));
    }
}

// Function values are two words: an entry point and a reference-counted
// closure context.  The context uses the unknown-object ARC entry points,
// which handle both native Swift and ObjC closure contexts.
void* fnv_init_copy(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    auto* d = static_cast<void**>(dest);
    auto* s = static_cast<void**>(src);
    d[0] = s != nullptr ? s[0] : nullptr;
    d[1] = s != nullptr ? UnknownRefPolicy::retain(s[1]) : nullptr;
    return dest;
}

void* fnv_assign_copy(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    auto* d = static_cast<void**>(dest);
    auto* s = static_cast<void**>(src);
    void* held = s != nullptr ? UnknownRefPolicy::retain(s[1]) : nullptr;
    void* old = d[1];
    d[0] = s != nullptr ? s[0] : nullptr;
    d[1] = held;
    UnknownRefPolicy::release(old);
    return dest;
}

void* fnv_init_take(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    std::memcpy(dest, src, 16);
    return dest;
}

void* fnv_assign_take(void* dest, void* src, const void*) {
    if (dest == nullptr) return dest;
    auto* d = static_cast<void**>(dest);
    UnknownRefPolicy::release(d[1]);
    std::memcpy(dest, src, 16);
    return dest;
}

void fnv_destroy(void* object, const void*) {
    if (object != nullptr) {
        UnknownRefPolicy::release(static_cast<void**>(object)[1]);
    }
}

TargetValueWitnessTable* make_function_vwt(Registration& reg) {
    auto* vwt = static_cast<TargetValueWitnessTable*>(
        static_cast<void*>(alloc_zero(sizeof(TargetValueWitnessTable))));
    if (vwt == nullptr) return nullptr;
    reg.allocations.push_back(reinterpret_cast<std::uint8_t*>(vwt));
    vwt->InitializeBufferWithCopyOfBuffer = fn_ptr(&fnv_init_copy);
    vwt->InitializeWithCopy = fn_ptr(&fnv_init_copy);
    vwt->AssignWithCopy = fn_ptr(&fnv_assign_copy);
    vwt->InitializeWithTake = fn_ptr(&fnv_init_take);
    vwt->AssignWithTake = fn_ptr(&fnv_assign_take);
    vwt->Destroy = fn_ptr(&fnv_destroy);
    vwt->Size = 16;
    vwt->Stride = 16;
    vwt->Flags = 7u;
    return vwt;
}

TargetWitnessTable* make_witness_table(const void* protocol_mp,
                                       const char* nominal_mangled,
                                       const void* const* slots, std::size_t count,
                                       const TargetWitnessTable* base,
                                       Registration& reg) {
    std::uint8_t* nominal = make_nominal(nominal_mangled, reg);
    if (nominal == nullptr) return nullptr;

    auto* cd = static_cast<TargetConformanceDescriptor*>(
        static_cast<void*>(alloc_zero(sizeof(TargetConformanceDescriptor))));
    auto* wt = static_cast<TargetWitnessTable*>(
        static_cast<void*>(alloc_zero(sizeof(TargetWitnessTable))));
    if (cd == nullptr || wt == nullptr) {
        std::free(cd);
        std::free(wt);
        return nullptr;
    }
    reg.allocations.push_back(reinterpret_cast<std::uint8_t*>(cd));
    reg.allocations.push_back(reinterpret_cast<std::uint8_t*>(wt));

    cd->Protocol = rel_abs(&cd->Protocol, protocol_mp);
    cd->TypeRef = rel_abs(&cd->TypeRef, nominal);
    cd->WitnessTablePattern = rel_abs(&cd->WitnessTablePattern, wt);
    cd->Flags = 0;  // DirectTypeDescriptor, no conditional requirements
    wt->Description = cd;
    for (std::size_t i = 0; i < count && i < 8; ++i) {
        wt->Witnesses[i] = const_cast<void*>(slots[i]);
    }
    if (base != nullptr) wt->Witnesses[0] = const_cast<TargetWitnessTable*>(base);
    return wt;
}

TargetValueWitnessTable* make_integer_vwt(std::size_t bytes, Registration& reg) {
    auto* vwt = static_cast<TargetValueWitnessTable*>(
        static_cast<void*>(alloc_zero(sizeof(TargetValueWitnessTable))));
    if (vwt == nullptr) return nullptr;
    reg.allocations.push_back(reinterpret_cast<std::uint8_t*>(vwt));

    if (bytes == 8) {
        vwt->InitializeBufferWithCopyOfBuffer = fn_ptr(&vwt_copy<8>);
        vwt->Destroy = fn_ptr(&vwt_destroy<8>);
        vwt->InitializeWithCopy = fn_ptr(&vwt_copy<8>);
        vwt->AssignWithCopy = fn_ptr(&vwt_copy<8>);
        vwt->InitializeWithTake = fn_ptr(&vwt_copy<8>);
        vwt->AssignWithTake = fn_ptr(&vwt_copy<8>);
        vwt->GetEnumTagSinglePayload = fn_ptr(&vwt_get_tag<8>);
        vwt->StoreEnumTagSinglePayload = fn_ptr(&vwt_set_tag<8>);
        vwt->Flags = 7u;  // 8-byte alignment mask
    } else {
        vwt->InitializeBufferWithCopyOfBuffer = fn_ptr(&vwt_copy<4>);
        vwt->Destroy = fn_ptr(&vwt_destroy<4>);
        vwt->InitializeWithCopy = fn_ptr(&vwt_copy<4>);
        vwt->AssignWithCopy = fn_ptr(&vwt_copy<4>);
        vwt->InitializeWithTake = fn_ptr(&vwt_copy<4>);
        vwt->AssignWithTake = fn_ptr(&vwt_copy<4>);
        vwt->GetEnumTagSinglePayload = fn_ptr(&vwt_get_tag<4>);
        vwt->StoreEnumTagSinglePayload = fn_ptr(&vwt_set_tag<4>);
        vwt->Flags = 3u;  // 4-byte alignment mask
    }
    vwt->Size = bytes;
    vwt->Stride = bytes;
    vwt->ExtraInhabitantCount = 0;
    return vwt;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

void register_swift_stdlib_metadata(SymbolTable* table) {
    if (table == nullptr) return;

    // Never inherit function pointers left over from a self-test or an earlier
    // registration: no shim in the table means no capability is claimed.
    g_hash_u64 = nullptr;
    g_hash_combine = nullptr;
    g_retain = nullptr;
    g_release = nullptr;
    g_unknown_retain = nullptr;
    g_unknown_release = nullptr;
    g_bridge_retain = nullptr;
    g_bridge_release = nullptr;
    load_function(*table, "_$ss6HasherV5_hash4seed_S2i_s6UInt64VtFZ", g_hash_u64);
    load_function(*table, "_$ss6HasherV8_combineyySuF", g_hash_combine);

    Registration& reg = registration();
    {
        std::lock_guard<std::mutex> lk(reg.mu);
        reg.symbols.clear();
        reg.nominals.clear();
        reg.registered = 0;
    }

    for (const auto& spec : kProtocols) {
        Block block = make_protocol(spec, reg);
        if (block.header == nullptr) continue;
        const std::string symbol = std::string("_") + spec.symbol;
        (*table)[symbol] = block.header;
        reg.symbols[symbol] = block.header;
        ++reg.registered;
    }

    for (const auto& row : kRequirements) {
        if (row.protocol >= sizeof(kProtocols) / sizeof(kProtocols[0])) continue;
        const ProtocolSpec& spec = kProtocols[row.protocol];
        if (row.index >= spec.num_req) continue;
        if (spec.req[row.index] != row.flags) continue;  // never guess a slot
        if (row.protocol >= reg.blocks.size()) continue;
        void* requirement = &reg.blocks[row.protocol].req[row.index];
        (*table)[row.symbol] = requirement;
        reg.symbols[row.symbol] = requirement;
        ++reg.registered;
    }

    // Concrete witness tables with fully implementable semantics.  All four
    // integer Hashable tables need the real SipHash shims from the foundation
    // overlay; if those were not registered, we leave the WP symbols absent.
    const void* eq_mp = reg.symbols["_$sSQMp"];
    const void* hash_mp = reg.symbols["_$sSHMp"];
    if (g_hash_u64 != nullptr && g_hash_combine != nullptr &&
        eq_mp != nullptr && hash_mp != nullptr) {
        TargetWitnessTable* bool_eq = make_witness_table(
            eq_mp, "$sSb", witness_slots_bool_eq(), 1, nullptr, reg);
        TargetWitnessTable* int_eq = make_witness_table(
            eq_mp, "$sSi", witness_slots_int_eq(), 1, nullptr, reg);
        TargetWitnessTable* int_hash = make_witness_table(
            hash_mp, "$sSi", witness_slots_int_hash(), 4, int_eq, reg);
        TargetWitnessTable* i32_hash = make_witness_table(
            hash_mp, "$ss5Int32V", witness_slots_i32_hash(), 4, nullptr, reg);
        TargetWitnessTable* u32_hash = make_witness_table(
            hash_mp, "$ss6UInt32V", witness_slots_u32_hash(), 4, nullptr, reg);

        struct WpRow { const char* symbol; TargetWitnessTable* wt; };
        const WpRow rows[] = {
            {"_$sSbSQsWP", bool_eq},
            {"_$sSiSQsWP", int_eq},
            {"_$sSiSHsWP", int_hash},
            {"_$ss5Int32VSHsWP", i32_hash},
            {"_$ss6UInt32VSHsWP", u32_hash},
        };
        for (const auto& r : rows) {
            if (r.wt == nullptr) continue;
            (*table)[r.symbol] = r.wt;
            reg.symbols[r.symbol] = r.wt;
            ++reg.registered;
        }
    }

    TargetValueWitnessTable* vwt64 = make_integer_vwt(8, reg);
    TargetValueWitnessTable* vwt32 = make_integer_vwt(4, reg);
    if (vwt64 != nullptr) {
        (*table)["_$sBi64_WV"] = vwt64;
        reg.symbols["_$sBi64_WV"] = vwt64;
        ++reg.registered;
    }
    if (vwt32 != nullptr) {
        (*table)["_$sBi32_WV"] = vwt32;
        reg.symbols["_$sBi32_WV"] = vwt32;
        ++reg.registered;
    }

    // Nominal type descriptors copied from the linked host dylibs.
    for (const auto& spec : kNominals) {
        const FullNominalDescriptor* desc = make_full_nominal(spec, reg);
        if (desc == nullptr) continue;
        const std::string symbol = std::string("_$") + spec.symbol;
        (*table)[symbol] = const_cast<FullNominalDescriptor*>(desc);
        reg.symbols[symbol] = const_cast<FullNominalDescriptor*>(desc);
        reg.nominals[spec.symbol] = desc;
        ++reg.registered;
    }

    // Type metadata for the POD types above, with a genuine VWT at -8.
    for (const auto& spec : kMetadatas) {
        auto it = reg.nominals.find(spec.nominal);
        if (it == reg.nominals.end() || it->second == nullptr) continue;
        TargetValueWitnessTable* vwt = make_integer_vwt(spec.size, reg);
        if (vwt == nullptr) continue;
        const void* meta = make_type_metadata(vwt, it->second, reg);
        if (meta == nullptr) continue;
        const std::string symbol = std::string("_$") + spec.symbol;
        (*table)[symbol] = const_cast<void*>(meta);
        reg.symbols[symbol] = const_cast<void*>(meta);
        ++reg.registered;
    }

    // Reference-counted value witness tables.  These need the real runtime
    // entry points; without them the WV symbols stay unregistered.
    load_function(*table, "_swift_retain", g_retain);
    load_function(*table, "_swift_release", g_release);
    load_function(*table, "_swift_unknownObjectRetain", g_unknown_retain);
    load_function(*table, "_swift_unknownObjectRelease", g_unknown_release);
    load_function(*table, "_swift_bridgeObjectRetain", g_bridge_retain);
    load_function(*table, "_swift_bridgeObjectRelease", g_bridge_release);
    if (g_retain != nullptr && g_release != nullptr &&
        g_unknown_retain != nullptr && g_unknown_release != nullptr &&
        g_bridge_retain != nullptr && g_bridge_release != nullptr) {
        struct WvRow {
            const char* symbol;
            TargetValueWitnessTable* vwt;
        };
        const WvRow rows[] = {
            {"_$sBoWV", make_ref_vwt<NativeRefPolicy>(reg)},
            {"_$sBOWV", make_ref_vwt<UnknownRefPolicy>(reg)},
            {"_$sBbWV", make_ref_vwt<BridgeRefPolicy>(reg)},
            {"_$syycWV", make_function_vwt(reg)},
        };
        for (const auto& row : rows) {
            if (row.vwt == nullptr) continue;
            (*table)[row.symbol] = row.vwt;
            reg.symbols[row.symbol] = row.vwt;
            ++reg.registered;
        }
    }

    // String: Hashable with a real equality witness and SipHash-backed hash
    // witnesses.  The inherited Equatable witness table is built first and
    // linked into slot 0 of the Hashable table.
    const void* eq_mp_for_string = reg.symbols["_$sSQMp"];
    const void* hash_mp_for_string = reg.symbols["_$sSHMp"];
    if (eq_mp_for_string != nullptr && hash_mp_for_string != nullptr) {
        const void* const eq_slots[] = {fn_ptr(&string_equal)};
        TargetWitnessTable* string_eq = make_witness_table(
            eq_mp_for_string, "$sSS", eq_slots, 1, nullptr, reg);
        const void* const hash_slots[] = {
            nullptr,  // inherited Equatable table, linked below
            fn_ptr(&string_hash_into),
            fn_ptr(&string_hash_value),
            fn_ptr(&string_raw_hash),
        };
        TargetWitnessTable* string_hash = make_witness_table(
            hash_mp_for_string, "$sSS", hash_slots, 4, string_eq, reg);
        if (string_hash != nullptr) {
            (*table)["_$sSSSHsWP"] = string_hash;
            reg.symbols["_$sSSSHsWP"] = string_hash;
            ++reg.registered;
        }
    }
}

std::size_t swift_stdlib_metadata_registered_symbol_count() {
    Registration& reg = registration();
    std::lock_guard<std::mutex> lk(reg.mu);
    return reg.registered;
}

const std::vector<std::pair<std::string, std::string>>&
swift_stdlib_metadata_unimplemented() {
    static const std::vector<std::pair<std::string, std::string>> rows = [] {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& row : kUnimplemented) {
            out.emplace_back(row.symbol, row.reason);
        }
        return out;
    }();
    return rows;
}

bool swift_stdlib_metadata_self_test() {
    SymbolTable table;
    // Deterministic stand-ins installed ONLY in this test table, so the
    // registration path still exercises the real foundation-overlay lookup.
    table["_$ss6HasherV5_hash4seed_S2i_s6UInt64VtFZ"] = fn_ptr(&test_hash_u64);
    table["_$ss6HasherV8_combineyySuF"] = fn_ptr(&test_hash_combine);
    table["_swift_retain"] = fn_ptr(&test_retain);
    table["_swift_release"] = fn_ptr(&test_release);
    table["_swift_unknownObjectRetain"] = fn_ptr(&test_retain);
    table["_swift_unknownObjectRelease"] = fn_ptr(&test_release);
    table["_swift_bridgeObjectRetain"] = fn_ptr(&test_bridge_retain);
    table["_swift_bridgeObjectRelease"] = fn_ptr(&test_bridge_release);
    register_swift_stdlib_metadata(&table);
    table.erase("_$ss6HasherV5_hash4seed_S2i_s6UInt64VtFZ");
    table.erase("_$ss6HasherV8_combineyySuF");
    table.erase("_swift_retain");
    table.erase("_swift_release");
    table.erase("_swift_unknownObjectRetain");
    table.erase("_swift_unknownObjectRelease");
    table.erase("_swift_bridgeObjectRetain");
    table.erase("_swift_bridgeObjectRelease");

    std::size_t failed = 0;
    std::size_t checked = 0;
    Registration& reg = registration();

    const auto check = [&](bool ok, const char* what) {
        ++checked;
        if (!ok) {
            std::fprintf(stderr, "stdlib_metadata self-test: FAIL %s\n", what);
            ++failed;
        }
    };

    check(swift_stdlib_metadata_registered_symbol_count() == table.size(),
          "registered count matches table size");
    check(table.size() == 131, "expected 131 symbols with real shims");

    for (std::size_t p = 0; p < sizeof(kProtocols) / sizeof(kProtocols[0]); ++p) {
        const ProtocolSpec& spec = kProtocols[p];
        const auto it = table.find(std::string("_") + spec.symbol);
        check(it != table.end() && it->second != nullptr, "Mp registered");
        if (it == table.end() || it->second == nullptr || p >= reg.blocks.size()) continue;
        const Block& block = reg.blocks[p];
        const auto* h = static_cast<const TargetProtocolDescriptorHeader*>(it->second);
        check(h == block.header, "Mp points at its block header");
        check(h->Flags == spec.flags, "Mp flags");
        check(h->NumRequirements == spec.num_req, "Mp requirement count");
        check(h->NumRequirementsInSignature == spec.num_sig, "Mp signature count");
        // Swift relative pointers are added to the address of the field itself.
        const char* name = reinterpret_cast<const char*>(&h->Name) + h->Name;
        check(std::strcmp(name, spec.name) == 0, "Mp name resolves");
        if (spec.assoc_names != nullptr) {
            const char* assoc = reinterpret_cast<const char*>(&h->AssociatedTypeNames) +
                                h->AssociatedTypeNames;
            check(std::strcmp(assoc, spec.assoc_names) == 0, "Mp associated type names");
        }
        for (std::uint32_t i = 0; i < spec.num_req; ++i) {
            check(block.req[i].Flags == spec.req[i], "requirement flags");
            check(block.req[i].DefaultImplementation == 0, "no fake default impl");
        }
    }

    for (const auto& row : kRequirements) {
        const auto it = table.find(row.symbol);
        check(it != table.end() && it->second != nullptr, "Tq/Tb registered");
        if (it == table.end() || it->second == nullptr ||
            row.protocol >= reg.blocks.size() || row.index >= kProtocols[row.protocol].num_req) {
            continue;
        }
        check(it->second == &reg.blocks[row.protocol].req[row.index],
              "Tq/Tb points at its protocol requirement slot");
        check(static_cast<const TargetProtocolRequirement*>(it->second)->Flags == row.flags,
              "Tq/Tb flags match dlsym extraction");
    }

    // Real behaviour checks on the witness functions.
    const std::uint64_t a = 42, b = 42, c = 43;
    check(witness_equal<8>(&a, &b, nullptr, nullptr), "Int eq(42,42)");
    check(!witness_equal<8>(&a, &c, nullptr, nullptr), "Int eq(42,43) false");
    check(witness_hash_value<8>(&a, nullptr, nullptr) != 0,
          "hash uses the registered hasher shim");
    check(witness_hash_value<8>(&a, nullptr, nullptr) ==
              witness_hash_value<8>(&b, nullptr, nullptr), "hash deterministic");
    check(witness_raw_hash<8>(&a, 7, nullptr, nullptr) ==
              witness_raw_hash<8>(&b, 7, nullptr, nullptr), "raw hash deterministic");
    const std::uint8_t ba = 1, bb = 1, bc = 0;
    check(witness_equal<1>(&ba, &bb, nullptr, nullptr), "Bool eq(1,1)");
    check(!witness_equal<1>(&ba, &bc, nullptr, nullptr), "Bool eq(1,0) false");

    // Value witness functions really move bytes.
    std::uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::uint8_t dst[8] = {};
    check(vwt_copy<8>(dst, src, nullptr) == dst, "VWT copy returns dest");
    check(std::memcmp(src, dst, 8) == 0, "VWT copy moves all bytes");
    check(vwt_get_tag<8>(src, 1, nullptr) == 0, "VWT tag for no-extra-inhabitant type");

    auto* int_eq_wt = static_cast<TargetWitnessTable*>(table["_$sSiSQsWP"]);
    auto* int_hash_wt = static_cast<TargetWitnessTable*>(table["_$sSiSHsWP"]);
    check(int_eq_wt != nullptr && int_eq_wt->Description != nullptr &&
              int_eq_wt->Witnesses[0] == fn_ptr(&witness_equal<8>),
          "Int:Equatable WP has a real equality witness");
    check(int_hash_wt != nullptr && int_hash_wt->Description != nullptr &&
              int_hash_wt->Witnesses[0] == int_eq_wt &&
              int_hash_wt->Witnesses[1] == fn_ptr(&witness_hash_into<8>) &&
              int_hash_wt->Witnesses[2] == fn_ptr(&witness_hash_value<8>) &&
              int_hash_wt->Witnesses[3] == fn_ptr(&witness_raw_hash<8>),
          "Int:Hashable WP has real hash witnesses and inherited Equatable");

    const auto* vwt64 = static_cast<const TargetValueWitnessTable*>(table["_$sBi64_WV"]);
    const auto* vwt32 = static_cast<const TargetValueWitnessTable*>(table["_$sBi32_WV"]);
    check(vwt64 != nullptr && vwt64->Size == 8 && vwt64->Stride == 8, "Int64 VWT layout");
    check(vwt32 != nullptr && vwt32->Size == 4 && vwt32->Stride == 4, "Int32 VWT layout");

    // Nominal descriptors and type metadata copied from the host dylibs.
    for (const auto& spec : kNominals) {
        const std::string symbol = std::string("_$") + spec.symbol;
        const auto it = table.find(symbol);
        check(it != table.end() && it->second != nullptr, "Mn registered");
        if (it == table.end() || it->second == nullptr) continue;
        const auto* desc = static_cast<const FullNominalDescriptor*>(it->second);
        check(desc->Flags == spec.flags, "Mn flags match host dylib");
        const char* name = reinterpret_cast<const char*>(&desc->Name) + desc->Name;
        check(std::strcmp(name, spec.name) == 0, "Mn name resolves");
        check(desc->Trailing0 == spec.trailing0 && desc->Trailing1 == spec.trailing1,
              "Mn trailing words match host dylib");
    }
    for (const auto& spec : kMetadatas) {
        const std::string symbol = std::string("_$") + spec.symbol;
        const auto it = table.find(symbol);
        check(it != table.end() && it->second != nullptr, "VN registered");
        if (it == table.end() || it->second == nullptr) continue;
        const auto* meta = reinterpret_cast<const RealTypeMetadata*>(
            static_cast<const char*>(it->second) - 8);
        check(meta->Vwt != nullptr, "VN has a real VWT");
        check(meta->Kind == 0x200u, "VN metadata kind is struct");
        check(meta->Vwt->Size == spec.size, "VN value size");
        check(reg.nominals.count(spec.nominal) == 1 &&
                  meta->Description == reg.nominals[spec.nominal],
              "VN description is the copied nominal descriptor");
    }

    // Reference-counted VWTs use real retain/release; the enum-tag slots are
    // honestly null.
    for (const char* symbol : {"_$sBoWV", "_$sBOWV", "_$sBbWV"}) {
        const auto* wt = static_cast<const TargetValueWitnessTable*>(table[symbol]);
        check(wt != nullptr && wt->Size == 8 && wt->Stride == 8, "object VWT layout");
        if (wt == nullptr) continue;
        check(wt->InitializeWithCopy != nullptr && wt->AssignWithCopy != nullptr &&
                  wt->InitializeWithTake != nullptr && wt->AssignWithTake != nullptr &&
                  wt->Destroy != nullptr,
              "object VWT has real copy/destroy witnesses");
        check(wt->GetEnumTagSinglePayload == nullptr &&
                  wt->StoreEnumTagSinglePayload == nullptr,
              "object VWT reports absent enum-tag witnesses");
    }

    // String: Hashable uses real read/SipHash witnesses.
    auto* string_hash_wt = static_cast<TargetWitnessTable*>(table["_$sSSSHsWP"]);
    check(string_hash_wt != nullptr && string_hash_wt->Witnesses[0] != nullptr &&
              string_hash_wt->Witnesses[1] == fn_ptr(&string_hash_into) &&
              string_hash_wt->Witnesses[2] == fn_ptr(&string_hash_value) &&
              string_hash_wt->Witnesses[3] == fn_ptr(&string_raw_hash),
          "String:Hashable WP has real witnesses");
    SwiftStringBits sa{}, sb{}, sc{};
    const char text[] = "abc";
    std::memcpy(&sa, text, 3);
    sb = sa;
    std::memcpy(&sc, "abd", 3);
    sa.word1 |= static_cast<std::uint64_t>(0xe0u + 3u) << 56;
    sb.word1 |= static_cast<std::uint64_t>(0xe0u + 3u) << 56;
    sc.word1 |= static_cast<std::uint64_t>(0xe0u + 3u) << 56;
    check(string_equal(&sa, &sb, nullptr, nullptr), "small String equality");
    check(!string_equal(&sa, &sc, nullptr, nullptr), "small String inequality");
    check(string_hash_value(&sa, nullptr, nullptr) == string_hash_value(&sb, nullptr, nullptr),
          "String hash deterministic");
    check(string_raw_hash(&sa, 9, nullptr, nullptr) != 0, "String raw hash non-zero");

    const auto& missing = swift_stdlib_metadata_unimplemented();
    for (const auto& row : missing) {
        check(table.find(row.first) == table.end(), "unregistered symbol stays absent");
    }

    std::fprintf(stderr,
                 "stdlib_metadata self-test: registered=%zu checked=%zu failed=%zu "
                 "deliberately_unregistered=%zu\n",
                 table.size(), checked, failed, missing.size());
    return failed == 0;
}

}  // namespace finn::darwin::swiftui
