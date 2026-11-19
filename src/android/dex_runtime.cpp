#include "android/dex_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace finn::android {
namespace {

std::string proto_signature(const DexFile& dex, std::uint32_t proto_idx) {
    if (proto_idx >= dex.protos.size()) return {};
    const DexProto& p = dex.protos[proto_idx];
    std::string out = "(";
    for (const auto& param : p.parameters) out += param;
    out += ")" + p.return_type;
    return out;
}

std::int32_t sign_extend(std::uint32_t value, unsigned bits) {
    if (bits >= 32u) return static_cast<std::int32_t>(value);
    const std::uint32_t sign = 1u << (bits - 1u);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

std::int64_t combine32(std::uint16_t lo, std::uint16_t hi) {
    return static_cast<std::int64_t>(static_cast<std::uint32_t>(lo) |
                                     (static_cast<std::uint32_t>(hi) << 16));
}

std::string reg_name(std::uint16_t r) {
    return "v" + std::to_string(r);
}

const std::array<DexOpcodeInfo, 256>& opcode_table() {
    static const std::array<DexOpcodeInfo, 256> table = [] {
        std::array<DexOpcodeInfo, 256> t{};
        auto add = [&](std::uint16_t op, const char* name, DexFormat fmt, std::uint8_t w) {
            t[op] = {name, fmt, w};
        };
        add(0x00, "nop", DexFormat::F10x, 1);
        add(0x01, "move", DexFormat::F12x, 1);
        add(0x02, "move/from16", DexFormat::F22x, 2);
        add(0x03, "move/16", DexFormat::F32x, 3);
        add(0x04, "move-wide", DexFormat::F12x, 1);
        add(0x05, "move-wide/from16", DexFormat::F22x, 2);
        add(0x06, "move-wide/16", DexFormat::F32x, 3);
        add(0x07, "move-object", DexFormat::F12x, 1);
        add(0x08, "move-object/from16", DexFormat::F22x, 2);
        add(0x09, "move-object/16", DexFormat::F32x, 3);
        add(0x0a, "move-result", DexFormat::F11x, 1);
        add(0x0b, "move-result-wide", DexFormat::F11x, 1);
        add(0x0c, "move-result-object", DexFormat::F11x, 1);
        add(0x0d, "move-exception", DexFormat::F11x, 1);
        add(0x0e, "return-void", DexFormat::F10x, 1);
        add(0x0f, "return", DexFormat::F11x, 1);
        add(0x10, "return-wide", DexFormat::F11x, 1);
        add(0x11, "return-object", DexFormat::F11x, 1);
        add(0x12, "const/4", DexFormat::F11n, 1);
        add(0x13, "const/16", DexFormat::F21s, 2);
        add(0x14, "const", DexFormat::F31i, 3);
        add(0x15, "const/high16", DexFormat::F21h, 2);
        add(0x16, "const-wide/16", DexFormat::F21s, 2);
        add(0x17, "const-wide/32", DexFormat::F31i, 3);
        add(0x18, "const-wide", DexFormat::F51i, 5);
        add(0x19, "const-wide/high16", DexFormat::F21h, 2);
        add(0x1a, "const-string", DexFormat::F21c, 2);
        add(0x1b, "const-string/jumbo", DexFormat::F31c, 3);
        add(0x1c, "const-class", DexFormat::F21c, 2);
        add(0x1d, "monitor-enter", DexFormat::F11x, 1);
        add(0x1e, "monitor-exit", DexFormat::F11x, 1);
        add(0x1f, "check-cast", DexFormat::F21c, 2);
        add(0x20, "instance-of", DexFormat::F22c, 2);
        add(0x21, "array-length", DexFormat::F12x, 1);
        add(0x22, "new-instance", DexFormat::F21c, 2);
        add(0x23, "new-array", DexFormat::F22c, 2);
        add(0x24, "filled-new-array", DexFormat::F35c, 3);
        add(0x25, "filled-new-array/range", DexFormat::F3rc, 3);
        add(0x26, "fill-array-data", DexFormat::F31t, 3);
        add(0x27, "throw", DexFormat::F11x, 1);
        add(0x28, "goto", DexFormat::F10t, 1);
        add(0x29, "goto/16", DexFormat::F20t, 2);
        add(0x2a, "goto/32", DexFormat::F30t, 3);
        add(0x2b, "packed-switch", DexFormat::F31t, 3);
        add(0x2c, "sparse-switch", DexFormat::F31t, 3);
        add(0x2d, "cmpl-float", DexFormat::F23x, 2);
        add(0x2e, "cmpg-float", DexFormat::F23x, 2);
        add(0x2f, "cmpl-double", DexFormat::F23x, 2);
        add(0x30, "cmpg-double", DexFormat::F23x, 2);
        add(0x31, "cmp-long", DexFormat::F23x, 2);
        static constexpr const char* kIf[] = {"if-eq", "if-ne", "if-lt", "if-ge", "if-gt", "if-le"};
        for (int i = 0; i < 6; ++i) add(0x32u + i, kIf[i], DexFormat::F22t, 2);
        static constexpr const char* kIfz[] = {"if-eqz", "if-nez", "if-ltz", "if-gez", "if-gtz", "if-lez"};
        for (int i = 0; i < 6; ++i) add(0x38u + i, kIfz[i], DexFormat::F21t, 2);
        static constexpr const char* kAget[] = {"aget", "aget-wide", "aget-object",
                                                "aget-boolean", "aget-byte", "aget-char", "aget-short"};
        for (int i = 0; i < 7; ++i) add(0x44u + i, kAget[i], DexFormat::F23x, 2);
        static constexpr const char* kAput[] = {"aput", "aput-wide", "aput-object",
                                                "aput-boolean", "aput-byte", "aput-char", "aput-short"};
        for (int i = 0; i < 7; ++i) add(0x4bu + i, kAput[i], DexFormat::F23x, 2);
        static constexpr const char* kIget[] = {"iget", "iget-wide", "iget-object",
                                                "iget-boolean", "iget-byte", "iget-char", "iget-short"};
        for (int i = 0; i < 7; ++i) add(0x52u + i, kIget[i], DexFormat::F22c, 2);
        static constexpr const char* kIput[] = {"iput", "iput-wide", "iput-object",
                                                "iput-boolean", "iput-byte", "iput-char", "iput-short"};
        for (int i = 0; i < 7; ++i) add(0x59u + i, kIput[i], DexFormat::F22c, 2);
        static constexpr const char* kSget[] = {"sget", "sget-wide", "sget-object",
                                                "sget-boolean", "sget-byte", "sget-char", "sget-short"};
        for (int i = 0; i < 7; ++i) add(0x60u + i, kSget[i], DexFormat::F21c, 2);
        static constexpr const char* kSput[] = {"sput", "sput-wide", "sput-object",
                                                "sput-boolean", "sput-byte", "sput-char", "sput-short"};
        for (int i = 0; i < 7; ++i) add(0x67u + i, kSput[i], DexFormat::F21c, 2);
        static constexpr const char* kInvoke[] = {"invoke-virtual", "invoke-super",
                                                  "invoke-direct", "invoke-static", "invoke-interface"};
        static constexpr const char* kInvokeRange[] = {"invoke-virtual/range", "invoke-super/range",
                                                       "invoke-direct/range", "invoke-static/range",
                                                       "invoke-interface/range"};
        for (int i = 0; i < 5; ++i) {
            add(0x6eu + i, kInvoke[i], DexFormat::F35c, 3);
            add(0x74u + i, kInvokeRange[i], DexFormat::F3rc, 3);
        }
        static constexpr const char* kIntOp[] = {"add-int", "sub-int", "mul-int", "div-int",
                                                 "rem-int", "and-int", "or-int", "xor-int",
                                                 "shl-int", "shr-int", "ushr-int"};
        for (int i = 0; i < 11; ++i) add(0x90u + i, kIntOp[i], DexFormat::F23x, 2);
        static constexpr const char* kLongOp[] = {"add-long", "sub-long", "mul-long", "div-long",
                                                  "rem-long", "and-long", "or-long", "xor-long",
                                                  "shl-long", "shr-long", "ushr-long"};
        for (int i = 0; i < 11; ++i) add(0x9bu + i, kLongOp[i], DexFormat::F23x, 2);
        static constexpr const char* kFloatOp[] = {"add-float", "sub-float", "mul-float",
                                                   "div-float", "rem-float"};
        for (int i = 0; i < 5; ++i) add(0xa6u + i, kFloatOp[i], DexFormat::F23x, 2);
        static constexpr const char* kDoubleOp[] = {"add-double", "sub-double", "mul-double",
                                                    "div-double", "rem-double"};
        for (int i = 0; i < 5; ++i) add(0xabu + i, kDoubleOp[i], DexFormat::F23x, 2);
        static constexpr const char* kIntAddr[] = {
            "add-int/2addr", "sub-int/2addr", "mul-int/2addr", "div-int/2addr",
            "rem-int/2addr", "and-int/2addr", "or-int/2addr", "xor-int/2addr",
            "shl-int/2addr", "shr-int/2addr", "ushr-int/2addr"};
        static constexpr const char* kLongAddr[] = {
            "add-long/2addr", "sub-long/2addr", "mul-long/2addr", "div-long/2addr",
            "rem-long/2addr", "and-long/2addr", "or-long/2addr", "xor-long/2addr",
            "shl-long/2addr", "shr-long/2addr", "ushr-long/2addr"};
        static constexpr const char* kFloatAddr[] = {"add-float/2addr", "sub-float/2addr",
            "mul-float/2addr", "div-float/2addr", "rem-float/2addr"};
        static constexpr const char* kDoubleAddr[] = {"add-double/2addr", "sub-double/2addr",
            "mul-double/2addr", "div-double/2addr", "rem-double/2addr"};
        for (int i = 0; i < 11; ++i) {
            add(0xb0u + i, kIntAddr[i], DexFormat::F12x, 1);
            add(0xbbu + i, kLongAddr[i], DexFormat::F12x, 1);
        }
        for (int i = 0; i < 5; ++i) {
            add(0xc6u + i, kFloatAddr[i], DexFormat::F12x, 1);
            add(0xcbu + i, kDoubleAddr[i], DexFormat::F12x, 1);
        }
        static constexpr const char* kLit8[] = {"add-int/lit8", "rsub-int/lit8", "mul-int/lit8",
                                                "div-int/lit8", "rem-int/lit8", "and-int/lit8",
                                                "or-int/lit8", "xor-int/lit8", "shl-int/lit8",
                                                "shr-int/lit8", "ushr-int/lit8"};
        for (int i = 0; i < 11; ++i) add(0xd0u + i, kLit8[i], DexFormat::F22b, 2);
        static constexpr const char* kLit16[] = {"add-int/lit16", "rsub-int/lit16",
                                                 "mul-int/lit16", "div-int/lit16",
                                                 "rem-int/lit16", "and-int/lit16",
                                                 "or-int/lit16", "xor-int/lit16"};
        for (int i = 0; i < 8; ++i) add(0xdbu + i, kLit16[i], DexFormat::F22s, 2);
        add(0xfa, "invoke-polymorphic", DexFormat::F45cc, 4);
        add(0xfb, "invoke-polymorphic/range", DexFormat::F4rcc, 4);
        add(0xfc, "invoke-custom", DexFormat::F35c, 3);
        add(0xfd, "invoke-custom/range", DexFormat::F3rc, 3);
        add(0xfe, "const-method-handle", DexFormat::F21c, 2);
        add(0xff, "const-method-type", DexFormat::F21c, 2);
        return t;
    }();
    return table;
}

}  // namespace

std::string DexFile::string_at(std::uint32_t idx) const {
    return idx < strings.size() ? strings[idx] : std::string();
}

std::string DexFile::type_at(std::uint32_t idx) const {
    return idx < types.size() ? types[idx].descriptor : std::string();
}

const DexMethod* DexFile::find_method(std::string_view class_name,
                                      std::string_view method_name) const {
    for (const auto& m : methods) {
        if (m.name == method_name &&
            (m.class_name == class_name ||
             m.class_name == "L" + std::string(class_name) + ";")) {
            return &m;
        }
    }
    return nullptr;
}

const DexMethod* DexFile::find_method_by_full_name(std::string_view full_name) const {
    for (const auto& m : methods) {
        if (m.full_name() == full_name) return &m;
    }
    return nullptr;
}

std::size_t DexFile::total_method_count() const { return methods.size(); }

std::size_t DexFile::code_method_count() const {
    std::size_t n = 0;
    for (const auto& c : classes) {
        for (const auto& m : c.direct_methods) if (m.has_code) ++n;
        for (const auto& m : c.virtual_methods) if (m.has_code) ++n;
    }
    return n;
}

DexFile parse_dex_bytes(const std::uint8_t* data, std::size_t size) {
    DexFile dex;
    dex.verification = verify_dex_header(data, size);
    if (!dex.verification.valid) {
        dex.error = dex.verification.error.empty() ? "invalid DEX header"
                                                    : dex.verification.error;
        return dex;
    }
    dex.raw.assign(data, data + size);
    const DexHeaderInfo& h = dex.verification.header;
    const std::uint8_t* p = data;
    auto in_range = [size](std::uint32_t off, std::uint32_t len) {
        return static_cast<std::uint64_t>(off) + len <= static_cast<std::uint64_t>(size);
    };

    for (std::uint32_t i = 0; i < h.string_ids_size; ++i) {
        const std::uint32_t off = read_le32(p + h.string_ids_off + i * 4u);
        if (off >= size) {
            dex.strings.emplace_back();
            continue;
        }
        std::size_t pos = off;
        std::uint64_t len = 0;
        if (!read_uleb128(p, size, &pos, &len) || len > size - pos) {
            dex.strings.emplace_back();
            continue;
        }
        std::size_t q = 0;
        std::string s;
        // decode_mutf8 takes an index into the buffer it is given; pass a local
        // cursor starting at zero and require forward progress every iteration.
        while (q < len) {
            std::size_t consumed = 0;
            s += decode_mutf8(p + pos + q, static_cast<std::size_t>(len) - q, &consumed);
            if (consumed == 0u) break;
            q += consumed;
        }
        dex.strings.push_back(std::move(s));
    }

    for (std::uint32_t i = 0; i < h.type_ids_size; ++i) {
        DexType t;
        t.descriptor_idx = read_le32(p + h.type_ids_off + i * 4u);
        t.descriptor = dex.string_at(t.descriptor_idx);
        dex.types.push_back(std::move(t));
    }

    for (std::uint32_t i = 0; i < h.proto_ids_size; ++i) {
        const std::uint8_t* e = p + h.proto_ids_off + static_cast<std::size_t>(i) * 12u;
        DexProto proto;
        proto.shorty_idx = read_le32(e + 0);
        proto.return_type_idx = read_le32(e + 4);
        proto.parameters_off = read_le32(e + 8);
        proto.shorty = dex.string_at(proto.shorty_idx);
        proto.return_type = dex.string_at(proto.return_type_idx);
        if (proto.parameters_off != 0u && in_range(proto.parameters_off, 4u)) {
            const std::uint32_t count = read_le32(p + proto.parameters_off);
            if (static_cast<std::uint64_t>(proto.parameters_off) + 4u +
                    static_cast<std::uint64_t>(count) * 2u <= size) {
                for (std::uint32_t k = 0; k < count; ++k) {
                    const std::uint16_t ti = read_le16(p + proto.parameters_off + 4u + k * 2u);
                    proto.parameters.push_back(dex.type_at(ti));
                }
            }
        }
        dex.protos.push_back(std::move(proto));
    }

    for (std::uint32_t i = 0; i < h.field_ids_size; ++i) {
        const std::uint8_t* e = p + h.field_ids_off + static_cast<std::size_t>(i) * 8u;
        DexField f;
        f.class_idx = read_le16(e + 0);
        f.type_idx = read_le16(e + 2);
        f.name_idx = read_le32(e + 4);
        f.class_name = dex.type_at(f.class_idx);
        f.type_name = dex.type_at(f.type_idx);
        f.name = dex.string_at(f.name_idx);
        dex.fields.push_back(std::move(f));
    }

    for (std::uint32_t i = 0; i < h.method_ids_size; ++i) {
        const std::uint8_t* e = p + h.method_ids_off + static_cast<std::size_t>(i) * 8u;
        DexMethod m;
        m.class_idx = read_le16(e + 0);
        m.proto_idx = read_le16(e + 2);
        m.name_idx = read_le32(e + 4);
        m.class_name = dex.type_at(m.class_idx);
        m.name = dex.string_at(m.name_idx);
        m.signature = proto_signature(dex, m.proto_idx);
        dex.methods.push_back(std::move(m));
    }

    for (std::uint32_t i = 0; i < h.class_defs_size; ++i) {
        const std::uint8_t* e = p + h.class_defs_off + static_cast<std::size_t>(i) * 32u;
        DexClass cls;
        cls.class_idx = read_le32(e + 0);
        cls.access_flags = read_le32(e + 4);
        cls.superclass_idx = read_le32(e + 8);
        cls.source_file_idx = read_le32(e + 16);
        cls.class_data_off = read_le32(e + 24);
        cls.name = dex.type_at(cls.class_idx);
        cls.superclass = dex.type_at(cls.superclass_idx);

        if (cls.class_data_off != 0u && in_range(cls.class_data_off, 4u)) {
            std::size_t pos = cls.class_data_off;
            std::uint64_t nsf = 0, nif = 0, ndm = 0, nvm = 0;
            if (read_uleb128(p, size, &pos, &nsf) && read_uleb128(p, size, &pos, &nif) &&
                read_uleb128(p, size, &pos, &ndm) && read_uleb128(p, size, &pos, &nvm)) {
                auto read_fields = [&](std::uint64_t count,
                                       std::vector<DexEncodedField>* dst) {
                    std::uint64_t idx = 0;
                    for (std::uint64_t k = 0; k < count; ++k) {
                        std::uint64_t diff = 0, flags = 0;
                        if (!read_uleb128(p, size, &pos, &diff) ||
                            !read_uleb128(p, size, &pos, &flags)) return false;
                        idx += diff;
                        if (idx >= dex.fields.size()) return false;
                        dst->push_back({static_cast<std::uint32_t>(idx),
                                        static_cast<std::uint32_t>(flags)});
                    }
                    return true;
                };
                if (!read_fields(nsf, &cls.static_fields) ||
                    !read_fields(nif, &cls.instance_fields)) {
                    cls.static_fields.clear();
                    cls.instance_fields.clear();
                }
                auto read_methods = [&](std::uint64_t count,
                                        std::vector<DexEncodedMethod>* dst) {
                    std::uint64_t idx = 0;
                    for (std::uint64_t k = 0; k < count; ++k) {
                        std::uint64_t diff = 0, flags = 0, code_off = 0;
                        if (!read_uleb128(p, size, &pos, &diff) ||
                            !read_uleb128(p, size, &pos, &flags) ||
                            !read_uleb128(p, size, &pos, &code_off)) return false;
                        idx += diff;
                        if (idx >= dex.methods.size()) return false;
                        DexEncodedMethod method;
                        method.method_idx = static_cast<std::uint32_t>(idx);
                        method.access_flags = static_cast<std::uint32_t>(flags);
                        method.code_off = static_cast<std::uint32_t>(code_off);
                        method.name = dex.methods[idx].name;
                        method.class_name = cls.name;
                        if (code_off != 0u && in_range(static_cast<std::uint32_t>(code_off), 16u)) {
                            const std::uint8_t* c = p + code_off;
                            method.has_code = true;
                            method.code.registers_size = read_le16(c + 0);
                            method.code.ins_size = read_le16(c + 2);
                            method.code.outs_size = read_le16(c + 4);
                            method.code.tries_size = read_le16(c + 6);
                            method.code.debug_info_off = read_le32(c + 8);
                            method.code.insns_size = read_le32(c + 12);
                            const std::uint64_t insns_bytes =
                                static_cast<std::uint64_t>(method.code.insns_size) * 2u;
                            if (static_cast<std::uint64_t>(code_off) + 16u + insns_bytes <= size) {
                                method.code.insns.resize(method.code.insns_size);
                                std::memcpy(method.code.insns.data(), c + 16,
                                            static_cast<std::size_t>(insns_bytes));
                            } else {
                                method.has_code = false;
                                method.code.insns_size = 0;
                            }
                        }
                        dst->push_back(std::move(method));
                    }
                    return true;
                };
                if (!read_methods(ndm, &cls.direct_methods) ||
                    !read_methods(nvm, &cls.virtual_methods)) {
                    cls.direct_methods.clear();
                    cls.virtual_methods.clear();
                }
            }
        }
        dex.classes.push_back(std::move(cls));
    }
    dex.valid = true;
    return dex;
}

DexFile parse_dex_bytes(const std::vector<std::uint8_t>& bytes) {
    return parse_dex_bytes(bytes.data(), bytes.size());
}

DexFile parse_dex_file(const std::string& path) {
    DexFile dex;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        dex.error = "unable to open DEX file: " + path;
        return dex;
    }
    const std::streamoff sz = in.tellg();
    if (sz <= 0) {
        dex.error = "DEX file is empty";
        return dex;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(buf.data()), sz);
    return parse_dex_bytes(buf.data(), buf.size());
}

DexOpcodeInfo dex_opcode_info(std::uint16_t opcode) {
    return opcode_table()[opcode & 0xffu];
}

bool decode_dex_instruction(const std::uint16_t* insns, std::size_t size,
                             std::size_t pos, DexInstruction* out) {
    if (!insns || !out || pos >= size) return false;
    const DexOpcodeInfo info = dex_opcode_info(insns[pos]);
    *out = DexInstruction{};
    out->offset = static_cast<std::uint32_t>(pos);
    out->opcode = static_cast<std::uint16_t>(insns[pos] & 0xffu);
    out->mnemonic = info.mnemonic ? info.mnemonic : "unknown";
    out->format = dex_format_name(info.format);
    out->width = info.width_units;
    if (!info.mnemonic || info.width_units == 0u || info.width_units > size - pos) {
        out->supported = false;
        out->reason = !info.mnemonic
                          ? "opcode is outside the decoded Dalvik subset"
                          : "instruction operands are truncated";
        return false;
    }
    out->supported = true;

    // Dalvik puts the opcode in the low byte of the first code unit; all other
    // fields live in the high byte or in following units. Nibble positions
    // below follow the published instruction-format tables exactly.
    const auto u = [&](std::size_t i) -> std::uint16_t {
        return (i < size) ? insns[pos + i] : 0u;
    };
    const auto hi = [&](std::size_t i) -> unsigned { return (u(i) >> 8) & 0xffu; };
    const auto lo = [&](std::size_t i) -> unsigned { return u(i) & 0xffu; };
    std::ostringstream ops;
    auto branch = [&](std::int64_t target, unsigned reg, bool has_reg) {
        out->has_target = true;
        out->target_offset = target;
        if (has_reg) out->registers.push_back(static_cast<std::uint16_t>(reg));
        if (has_reg) ops << reg_name(static_cast<std::uint16_t>(reg)) << ", ";
        ops << (target >= 0 ? "+" : "") << target;
    };

    switch (info.format) {
        case DexFormat::F10x:
            break;
        case DexFormat::F10t:
            branch(sign_extend(static_cast<std::uint32_t>(hi(0)), 8), 0, false);
            break;
        case DexFormat::F11n: {
            const unsigned reg = hi(0) & 0x0fu;
            const int lit = sign_extend(hi(0) >> 4u, 4);
            out->registers.push_back(static_cast<std::uint16_t>(reg));
            out->literal = lit;
            out->has_literal = true;
            ops << reg_name(static_cast<std::uint16_t>(reg)) << ", #" << lit;
            break;
        }
        case DexFormat::F11x:
            out->registers.push_back(static_cast<std::uint16_t>(hi(0)));
            ops << reg_name(static_cast<std::uint16_t>(hi(0)));
            break;
        case DexFormat::F12x: {
            const unsigned a = hi(0) & 0x0fu;
            const unsigned b = hi(0) >> 4u;
            out->registers.push_back(static_cast<std::uint16_t>(a));
            out->registers.push_back(static_cast<std::uint16_t>(b));
            ops << reg_name(static_cast<std::uint16_t>(a)) << ", "
                << reg_name(static_cast<std::uint16_t>(b));
            break;
        }
        case DexFormat::F20t:
            branch(static_cast<std::int16_t>(static_cast<std::uint8_t>(lo(1))), 0, false);
            break;
        case DexFormat::F21c:
        case DexFormat::F31c: {
            const unsigned reg = hi(0);
            out->registers.push_back(static_cast<std::uint16_t>(reg));
            out->index = (info.format == DexFormat::F21c)
                             ? u(1)
                             : static_cast<std::uint32_t>(combine32(u(1), u(2)));
            out->has_index = true;
            ops << reg_name(static_cast<std::uint16_t>(reg)) << ", @" << out->index;
            break;
        }
        case DexFormat::F21h: {
            const unsigned reg = hi(0);
            out->registers.push_back(static_cast<std::uint16_t>(reg));
            out->literal = static_cast<std::int64_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(u(1)))) << 16;
            out->has_literal = true;
            ops << reg_name(static_cast<std::uint16_t>(reg)) << ", #" << out->literal;
            break;
        }
        case DexFormat::F21s: {
            const unsigned reg = hi(0);
            out->registers.push_back(static_cast<std::uint16_t>(reg));
            out->literal = static_cast<std::int16_t>(u(1));
            out->has_literal = true;
            ops << reg_name(static_cast<std::uint16_t>(reg)) << ", #" << out->literal;
            break;
        }
        case DexFormat::F21t:
            branch(static_cast<std::int16_t>(u(1)), hi(0), true);
            break;
        case DexFormat::F22b:
        case DexFormat::F22c:
        case DexFormat::F22s:
        case DexFormat::F22t: {
            const unsigned a = hi(0) & 0x0fu;
            const unsigned b = hi(0) >> 4u;
            out->registers.push_back(static_cast<std::uint16_t>(a));
            out->registers.push_back(static_cast<std::uint16_t>(b));
            if (info.format == DexFormat::F22b) {
                out->literal = static_cast<std::int8_t>(static_cast<std::uint8_t>(lo(1)));
                out->has_literal = true;
            } else if (info.format == DexFormat::F22s) {
                out->literal = static_cast<std::int16_t>(u(1));
                out->has_literal = true;
            } else if (info.format == DexFormat::F22c) {
                out->index = u(1);
                out->has_index = true;
            }
            ops << reg_name(static_cast<std::uint16_t>(a)) << ", "
                << reg_name(static_cast<std::uint16_t>(b));
            if (out->has_literal) ops << ", #" << out->literal;
            if (out->has_index) ops << ", @" << out->index;
            if (info.format == DexFormat::F22t) {
                const std::int64_t target = static_cast<std::int16_t>(u(1));
                out->has_target = true;
                out->target_offset = target;
                ops << ", " << (target >= 0 ? "+" : "") << target;
            }
            break;
        }
        case DexFormat::F22x: {
            const unsigned a = hi(0);
            const unsigned b = u(1);
            out->registers.push_back(static_cast<std::uint16_t>(a));
            out->registers.push_back(static_cast<std::uint16_t>(b));
            ops << reg_name(static_cast<std::uint16_t>(a)) << ", "
                << reg_name(static_cast<std::uint16_t>(b));
            break;
        }
        case DexFormat::F23x: {
            const unsigned a = hi(0);
            const unsigned b = lo(1);
            const unsigned c = hi(1);
            out->registers.push_back(static_cast<std::uint16_t>(a));
            out->registers.push_back(static_cast<std::uint16_t>(b));
            out->registers.push_back(static_cast<std::uint16_t>(c));
            ops << reg_name(static_cast<std::uint16_t>(a)) << ", "
                << reg_name(static_cast<std::uint16_t>(b)) << ", "
                << reg_name(static_cast<std::uint16_t>(c));
            break;
        }
        case DexFormat::F30t:
            branch(combine32(u(1), u(2)), 0, false);
            break;
        case DexFormat::F31i: {
            const unsigned reg = hi(0);
            out->registers.push_back(static_cast<std::uint16_t>(reg));
            out->literal = combine32(u(1), u(2));
            out->has_literal = true;
            ops << reg_name(static_cast<std::uint16_t>(reg)) << ", #" << out->literal;
            break;
        }
        case DexFormat::F31t:
            branch(combine32(u(1), u(2)), hi(0), true);
            break;
        case DexFormat::F32x: {
            const unsigned a = u(1);
            const unsigned b = u(2);
            out->registers.push_back(static_cast<std::uint16_t>(a));
            out->registers.push_back(static_cast<std::uint16_t>(b));
            ops << reg_name(static_cast<std::uint16_t>(a)) << ", "
                << reg_name(static_cast<std::uint16_t>(b));
            break;
        }
        case DexFormat::F35c:
        case DexFormat::F45cc:
        case DexFormat::F4rcc: {
            const unsigned count = hi(0) & 0x0fu;
            const unsigned g = hi(0) >> 4u;
            out->index = u(1);
            out->has_index = true;
            const unsigned regs[4] = {u(2) & 0x0fu, (u(2) >> 4u) & 0x0fu,
                                      (u(2) >> 8u) & 0x0fu, (u(2) >> 12u) & 0x0fu};
            for (unsigned i = 0; i < count && i < 5u; ++i) {
                const unsigned r = (i < 4u) ? regs[i] : g;
                out->registers.push_back(static_cast<std::uint16_t>(r));
            }
            ops << "{";
            for (std::size_t i = 0; i < out->registers.size(); ++i) {
                if (i) ops << ", ";
                ops << reg_name(out->registers[i]);
            }
            if (count > out->registers.size()) ops << ", ...";
            ops << "}, @" << out->index;
            if (info.format == DexFormat::F45cc || info.format == DexFormat::F4rcc) {
                ops << ", proto @" << u(3);
            }
            break;
        }
        case DexFormat::F3rc: {
            const unsigned count = hi(0);
            const unsigned first = u(2);
            out->index = u(1);
            out->has_index = true;
            const unsigned n = std::min<unsigned>(count, 16u);
            for (unsigned i = 0; i < n; ++i) {
                out->registers.push_back(static_cast<std::uint16_t>(first + i));
            }
            ops << "{v" << first;
            if (n > 1u) ops << "..v" << (first + n - 1u);
            ops << "}, @" << out->index;
            break;
        }
        case DexFormat::F51i: {
            const unsigned reg = hi(0);
            out->registers.push_back(static_cast<std::uint16_t>(reg));
            out->literal = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(u(1)) |
                (static_cast<std::uint64_t>(u(2)) << 16u) |
                (static_cast<std::uint64_t>(u(3)) << 32u) |
                (static_cast<std::uint64_t>(u(4)) << 48u));
            out->has_literal = true;
            ops << reg_name(static_cast<std::uint16_t>(reg)) << ", #" << out->literal;
            break;
        }
        case DexFormat::Unknown:
            out->supported = false;
            out->reason = "unknown instruction format";
            return false;
    }
    out->operands = ops.str();
    return true;
}

DexDisassembly disassemble_dex_code(const DexCodeItem& code) {
    DexDisassembly d;
    if (code.insns.empty()) {
        d.complete = true;
        return d;
    }
    d.instructions.reserve(code.insns_size);
    std::size_t pos = 0;
    while (pos < code.insns.size()) {
        DexInstruction ins{};
        if (!decode_dex_instruction(code.insns.data(), code.insns.size(), pos, &ins)) {
            d.unsupported.emplace_back(static_cast<std::uint32_t>(pos), ins.reason);
            d.error = "unsupported opcode at code unit " + std::to_string(pos) +
                      ": " + ins.reason;
            break;
        }
        d.instructions.push_back(std::move(ins));
        ++d.opcode_counts[ins.opcode];
        if (ins.has_target) {
            const std::int64_t at = static_cast<std::int64_t>(pos) + ins.target_offset;
            if (at >= 0 && at < static_cast<std::int64_t>(code.insns.size())) {
                d.branch_targets.push_back(static_cast<std::uint32_t>(at));
            }
        }
        pos += ins.width;
    }
    d.complete = d.unsupported.empty();
    return d;
}

}  // namespace finn::android
