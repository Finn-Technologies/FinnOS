// Clean-room DEX parser and Dalvik bytecode decoder for finn-android.
//
// The decoder implements a stated subset of the Dalvik instruction set with
// real operand widths. An opcode outside that subset is reported with its
// offset and reason; decoding stops rather than guessing a width and
// desynchronising the stream.

#ifndef FINN_ANDROID_DEX_RUNTIME_HPP
#define FINN_ANDROID_DEX_RUNTIME_HPP

#include "android/android_format.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace finn::android {

struct DexType {
    std::uint32_t descriptor_idx = 0;
    std::string descriptor;
};

struct DexProto {
    std::uint32_t shorty_idx = 0;
    std::uint32_t return_type_idx = 0;
    std::uint32_t parameters_off = 0;
    std::string shorty;
    std::string return_type;
    std::vector<std::string> parameters;
};

struct DexField {
    std::uint16_t class_idx = 0;
    std::uint16_t type_idx = 0;
    std::uint32_t name_idx = 0;
    std::string class_name;
    std::string type_name;
    std::string name;
};

struct DexMethod {
    std::uint16_t class_idx = 0;
    std::uint16_t proto_idx = 0;
    std::uint32_t name_idx = 0;
    std::string class_name;
    std::string name;
    std::string signature;
    std::string full_name() const { return class_name + "->" + name + signature; }
};

struct DexEncodedField {
    std::uint32_t field_idx = 0;
    std::uint32_t access_flags = 0;
};

struct DexCodeItem {
    std::uint16_t registers_size = 0;
    std::uint16_t ins_size = 0;
    std::uint16_t outs_size = 0;
    std::uint16_t tries_size = 0;
    std::uint32_t debug_info_off = 0;
    std::uint32_t insns_size = 0;
    std::vector<std::uint16_t> insns;
};

struct DexEncodedMethod {
    std::uint32_t method_idx = 0;
    std::uint32_t access_flags = 0;
    std::uint32_t code_off = 0;
    bool has_code = false;
    std::string name;
    std::string class_name;
    DexCodeItem code;
};

struct DexClass {
    std::uint16_t class_idx = 0;
    std::uint32_t access_flags = 0;
    std::uint32_t superclass_idx = 0;
    std::uint32_t source_file_idx = 0;
    std::uint32_t class_data_off = 0;
    std::string name;
    std::string superclass;
    std::vector<DexEncodedField> static_fields;
    std::vector<DexEncodedField> instance_fields;
    std::vector<DexEncodedMethod> direct_methods;
    std::vector<DexEncodedMethod> virtual_methods;
};

struct DexFile {
    bool valid = false;
    DexVerification verification;
    std::vector<std::string> strings;
    std::vector<DexType> types;
    std::vector<DexProto> protos;
    std::vector<DexField> fields;
    std::vector<DexMethod> methods;
    std::vector<DexClass> classes;
    std::vector<std::uint8_t> raw;
    std::string error;

    std::string string_at(std::uint32_t idx) const;
    std::string type_at(std::uint32_t idx) const;
    const DexMethod* find_method(std::string_view class_name,
                                 std::string_view method_name) const;
    const DexMethod* find_method_by_full_name(std::string_view full_name) const;
    std::size_t total_method_count() const;
    std::size_t code_method_count() const;
};

DexFile parse_dex_bytes(const std::uint8_t* data, std::size_t size);
DexFile parse_dex_bytes(const std::vector<std::uint8_t>& bytes);
DexFile parse_dex_file(const std::string& path);

enum class DexFormat {
    Unknown,
    F10x, F10t, F11n, F11x, F12x,
    F20t, F21c, F21h, F21s, F21t, F22b, F22c, F22s, F22t, F22x, F23x,
    F30t, F31c, F31i, F31t, F32x, F35c, F3rc,
    F45cc, F4rcc, F51i,
};

inline const char* dex_format_name(DexFormat f) {
    switch (f) {
        case DexFormat::F10x: return "10x";
        case DexFormat::F10t: return "10t";
        case DexFormat::F11n: return "11n";
        case DexFormat::F11x: return "11x";
        case DexFormat::F12x: return "12x";
        case DexFormat::F20t: return "20t";
        case DexFormat::F21c: return "21c";
        case DexFormat::F21h: return "21h";
        case DexFormat::F21s: return "21s";
        case DexFormat::F21t: return "21t";
        case DexFormat::F22b: return "22b";
        case DexFormat::F22c: return "22c";
        case DexFormat::F22s: return "22s";
        case DexFormat::F22t: return "22t";
        case DexFormat::F22x: return "22x";
        case DexFormat::F23x: return "23x";
        case DexFormat::F30t: return "30t";
        case DexFormat::F31c: return "31c";
        case DexFormat::F31i: return "31i";
        case DexFormat::F31t: return "31t";
        case DexFormat::F32x: return "32x";
        case DexFormat::F35c: return "35c";
        case DexFormat::F3rc: return "3rc";
        case DexFormat::F45cc: return "45cc";
        case DexFormat::F4rcc: return "4rcc";
        case DexFormat::F51i: return "51i";
        case DexFormat::Unknown: break;
    }
    return "unknown";
}

struct DexOpcodeInfo {
    const char* mnemonic = nullptr;
    DexFormat format = DexFormat::Unknown;
    std::uint8_t width_units = 0;
};

DexOpcodeInfo dex_opcode_info(std::uint16_t opcode);

struct DexInstruction {
    std::uint32_t offset = 0;       // code units from the start of insns
    std::uint16_t opcode = 0;
    std::uint8_t width = 0;
    bool supported = false;
    std::string mnemonic;
    std::string format;
    std::string operands;
    std::uint32_t index = 0;        // method/field/type/string index, when present
    bool has_index = false;
    std::int64_t target_offset = 0; // relative branch target, when present
    bool has_target = false;
    std::int64_t literal = 0;
    bool has_literal = false;
    std::vector<std::uint16_t> registers;
    std::string reason;             // why an opcode is not decoded
};

struct DexDisassembly {
    bool complete = false;
    std::vector<DexInstruction> instructions;
    std::map<std::uint16_t, std::uint32_t> opcode_counts;
    std::vector<std::pair<std::uint32_t, std::string>> unsupported;
    std::vector<std::uint32_t> branch_targets;
    std::string error;
};

DexDisassembly disassemble_dex_code(const DexCodeItem& code);
// Decodes one instruction at `pos`; returns false when the opcode is outside
// the supported subset or the operands do not fit in the code item.
bool decode_dex_instruction(const std::uint16_t* insns, std::size_t size,
                            std::size_t pos, DexInstruction* out);

}  // namespace finn::android

#endif  // FINN_ANDROID_DEX_RUNTIME_HPP
