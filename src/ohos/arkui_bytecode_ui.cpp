#include "ohos/arkui_bytecode_ui.hpp"

#include "ohos/abc_runtime.hpp"
#include "ohos/arkui_bridge.hpp"
#include "ohos/ohos_format.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace finn::ohos {
namespace {

constexpr std::size_t kMaxNodes = 640;
constexpr int kMaxWalkDepth = 16;
constexpr std::size_t kObsWindow = 96;
constexpr std::size_t kMaxPages = 8;
constexpr std::size_t kMaxBuilderWindow = 28;

// ---- small binary readers -------------------------------------------------

std::uint32_t rd32le(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[p]) |
           (static_cast<std::uint32_t>(b[p + 1]) << 8u) |
           (static_cast<std::uint32_t>(b[p + 2]) << 16u) |
           (static_cast<std::uint32_t>(b[p + 3]) << 24u);
}

std::uint32_t rd32be(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 4 > b.size()) return 0;
    return (static_cast<std::uint32_t>(b[p]) << 24u) |
           (static_cast<std::uint32_t>(b[p + 1]) << 16u) |
           (static_cast<std::uint32_t>(b[p + 2]) << 8u) |
           static_cast<std::uint32_t>(b[p + 3]);
}

// resources.index stores most ids as id<<24 | 1. The very first entry of a
// section is the bare value 1, which is id 1 (app_name), not id 0.
unsigned resource_id_from_raw(std::uint32_t raw) {
    return raw == 1u ? 1u : (raw >> 24u);
}

std::uint16_t rd16le(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 2 > b.size()) return 0;
    return static_cast<std::uint16_t>(b[p] |
                                      (static_cast<std::uint16_t>(b[p + 1]) << 8u));
}

bool read_binary(const fs::path& path, std::vector<std::uint8_t>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
    return true;
}

bool read_text(const fs::path& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
    return true;
}

bool valid_utf8(const std::uint8_t* s, std::size_t n) {
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t c = s[i];
        if (c < 0x80u) {
            ++i;
            continue;
        }
        int continuation = 0;
        std::uint32_t cp = 0;
        if ((c & 0xe0u) == 0xc0u) {
            if (c < 0xc2u) return false;
            continuation = 1;
            cp = c & 0x1fu;
        } else if ((c & 0xf0u) == 0xe0u) {
            continuation = 2;
            cp = c & 0x0fu;
        } else if ((c & 0xf8u) == 0xf0u) {
            if (c > 0xf4u) return false;
            continuation = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + static_cast<std::size_t>(continuation) >= n) return false;
        for (int k = 1; k <= continuation; ++k) {
            const std::uint8_t cc = s[i + static_cast<std::size_t>(k)];
            if ((cc & 0xc0u) != 0x80u) return false;
            cp = (cp << 6u) | (cc & 0x3fu);
        }
        if (continuation == 2 && cp < 0x800u) return false;
        if (continuation == 3 && cp < 0x10000u) return false;
        if (cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return false;
        i += static_cast<std::size_t>(continuation) + 1u;
    }
    return true;
}

// ---- resources.index numeric-id decoder ----------------------------------

struct ResourceEntry {
    unsigned type = 0;
    std::string value;
    std::string key;
};

// resources.index contains one or more IDSS sections. Each entry is
// [u32 descriptor offset][u32 id]. The descriptor is
// [u32 size][u32 type][u32 id, big endian][u16 len+1][UTF-8][NUL]
//                                        [u16 len+1][key][NUL].
// The last section is the most specific qualifier (for example zh_CN), so it
// wins; that is the string the device actually shows.
std::size_t decode_resource_ids(const std::vector<std::uint8_t>& bytes,
                                std::map<unsigned, ResourceEntry>* by_id,
                                std::map<std::string, std::string>* by_key) {
    std::size_t sections = 0;
    std::size_t search = 0;
    while (search + 12u <= bytes.size()) {
        const auto found = std::search(bytes.begin() + static_cast<std::ptrdiff_t>(search),
                                       bytes.end(),
                                       std::begin("IDSS"), std::end("IDSS") - 1);
        if (found == bytes.end()) break;
        const std::size_t sec = static_cast<std::size_t>(
            std::distance(bytes.begin(), found));
        const std::uint32_t count = rd32le(bytes, sec + 4u);
        std::size_t p = sec + 8u;
        // The first four bytes of the record area are a section marker.
        if (p + 4u <= bytes.size() && rd32le(bytes, p) == 0x01000000u) p += 4u;
        ++sections;
        for (std::uint32_t i = 0; i < count && p + 8u <= bytes.size(); ++i) {
            const std::uint32_t off = rd32le(bytes, p);
            p += 8u;
            if (off + 16u > bytes.size()) continue;
            const unsigned type = rd32le(bytes, static_cast<std::size_t>(off) + 4u);
            const std::uint32_t raw_id = rd32be(bytes, static_cast<std::size_t>(off) + 8u);
            const unsigned id = resource_id_from_raw(raw_id);
            std::size_t q = static_cast<std::size_t>(off) + 12u;
            const std::uint16_t vlen = rd16le(bytes, q);
            q += 2u;
            if (vlen < 1u || q + vlen > bytes.size()) continue;
            const std::uint8_t* vstart = bytes.data() + q;
            if (!valid_utf8(vstart, vlen - 1u)) continue;
            const std::string value(reinterpret_cast<const char*>(vstart), vlen - 1u);
            q += vlen;
            std::string key;
            if (q + 2u <= bytes.size()) {
                const std::uint16_t klen = rd16le(bytes, q);
                q += 2u;
                if (klen >= 1u && q + klen <= bytes.size()) {
                    const std::uint8_t* kstart = bytes.data() + q;
                    if (valid_utf8(kstart, klen - 1u)) {
                        key.assign(reinterpret_cast<const char*>(kstart), klen - 1u);
                    }
                }
            }
            if (id == 0 || value.empty()) continue;
            ResourceEntry entry;
            entry.type = type;
            entry.value = value;
            entry.key = key;
            (*by_id)[id] = std::move(entry);
            if (!key.empty()) (*by_key)[key] = value;
        }
        search = sec + 12u;
    }
    return sections;
}

std::string reference_key(const std::string& ref) {
    const std::size_t colon = ref.find(':');
    if (colon == std::string::npos || colon + 1u >= ref.size()) return {};
    std::string key = ref.substr(colon + 1u);
    const std::size_t dot = key.rfind('.');
    if (dot != std::string::npos && dot + 1u < key.size()) key = key.substr(dot + 1u);
    return key;
}

// ---- Panda index helpers -------------------------------------------------

const AbcIndexRegion* region_for(const AbcFile& abc, std::uint32_t method_offset) {
    for (const auto& reg : abc.index_regions) {
        if (method_offset >= reg.start && method_offset < reg.end) return &reg;
    }
    return abc.index_regions.empty() ? nullptr : &abc.index_regions.front();
}

std::string id16_at(const AbcFile& abc, const AbcMethod& m,
                    const std::vector<std::uint8_t>& code, std::size_t id_pos) {
    if (id_pos + 2u > code.size()) return {};
    const std::uint16_t id = static_cast<std::uint16_t>(code[id_pos]) |
                             (static_cast<std::uint16_t>(code[id_pos + 1u]) << 8u);
    return abc.resolve_id16_string(m.offset, id);
}

std::uint16_t raw_id16(const std::vector<std::uint8_t>& code, std::size_t id_pos) {
    if (id_pos + 2u > code.size()) return 0;
    return static_cast<std::uint16_t>(code[id_pos]) |
           (static_cast<std::uint16_t>(code[id_pos + 1u]) << 8u);
}

const AbcMethod* method_at_id(const AbcFile& abc, const AbcMethod& m,
                              std::uint16_t id,
                              const std::map<std::uint32_t, const AbcMethod*>& by_offset) {
    const AbcIndexRegion* reg = region_for(abc, m.offset);
    if (!reg || id >= reg->method_idx.size()) return nullptr;
    const auto it = by_offset.find(reg->method_idx[id]);
    return it == by_offset.end() ? nullptr : it->second;
}

// The literal array for a createobjectwithbuffer id16 lives at
// index.method_idx[id16]. Panda literal arrays are [u32 count] followed by
// [u8 tag][4-byte value] records. A tag-2 value whose big-endian id resolves
// to a string resource is the $r(...) argument the ArkTS compiler emitted.
std::string literal_resource_text(const AbcFile& abc, const AbcMethod& m,
                                  const std::vector<std::uint8_t>& code,
                                  std::size_t pc,
                                  const std::map<unsigned, ResourceEntry>& by_id) {
    if (pc + 4u > code.size()) return {};
    const std::uint16_t id = raw_id16(code, pc + 2u);
    const AbcIndexRegion* reg = region_for(abc, m.offset);
    if (!reg || id >= reg->method_idx.size()) return {};
    const std::size_t off = reg->method_idx[id];
    if (off + 4u > abc.raw_bytes.size()) return {};
    const std::uint32_t count = rd32le(abc.raw_bytes, off);
    if (count == 0 || count > 128u) return {};
    std::size_t p = off + 4u;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (p + 5u > abc.raw_bytes.size()) return {};
        const std::uint8_t tag = abc.raw_bytes[p++];
        if (tag == 0x02u) {
            const std::uint32_t raw = rd32be(abc.raw_bytes, p);
            const unsigned resource_id = resource_id_from_raw(raw);
            const auto it = by_id.find(resource_id);
            if (it != by_id.end() && it->second.type == 9u) {
                return it->second.value;
            }
        }
        p += 4u;
    }
    return {};
}

bool printable_text(const std::string& s) {
    if (s.empty() || s.size() > 160u) return false;
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20u && c != '\n' && c != '\t') return false;
        if (uc == 0x7fu) return false;
    }
    return true;
}

// Panda literal arrays are a [u32 count] followed by [tag][4-byte value]
// records; tag 5 is a string reference whose value is an offset into the
// .abc string area. Scanning the bounded array region for that pattern is
// more robust than assuming a fixed element width (debug arrays mix tags).
std::vector<std::string> literal_array_strings(
    const AbcFile& abc, const AbcMethod& m,
    const std::vector<std::uint8_t>& code, std::size_t pc) {
    std::vector<std::string> out;
    if (pc + 4u > code.size()) return out;
    const std::uint16_t id = raw_id16(code, pc + 2u);
    const AbcIndexRegion* reg = region_for(abc, m.offset);
    if (!reg || id >= reg->method_idx.size()) return out;
    const std::uint32_t off = reg->method_idx[id];
    if (off + 4u > abc.raw_bytes.size()) return out;
    const std::uint32_t count = rd32le(abc.raw_bytes, off);
    if (count == 0u || count > 512u) return out;
    // The literalarray index is not sorted and can point into the middle of
    // another array, so bound the scan by this array's declared element count
    // plus slack instead of by the next index entry.
    std::size_t limit = off + 4u + static_cast<std::size_t>(count) * 8u + 64u;
    limit = std::min(limit, abc.raw_bytes.size());
    for (std::size_t p = off + 4u; p + 5u <= limit; ++p) {
        if (abc.raw_bytes[p] != 0x05u) continue;
        const std::uint32_t str_off = rd32le(abc.raw_bytes, p + 1u);
        if (str_off == 0u || str_off >= abc.raw_bytes.size()) continue;
        const std::string s = abc.read_mutf8_string(str_off);
        if (printable_text(s)) out.push_back(s);
        p += 4u;
    }
    return out;
}

std::string class_leaf(std::string name) {
    if (!name.empty() && name.front() == 'L' && name.back() == ';') {
        name = name.substr(1u, name.size() - 2u);
    }
    const std::size_t slash = name.rfind('/');
    if (slash != std::string::npos) name = name.substr(slash + 1u);
    return name;
}

// A custom ArkUI component is instantiated as ViewPU.create(<literal array>),
// where the array carries the component class name (for example
// "TitleBarComponent"). Resolve it to the real class in the same .abc.
const AbcClass* custom_class_for_method(
    const AbcFile& abc, const AbcMethod& m,
    const std::map<std::string, const AbcClass*>& by_leaf) {
    const auto& code = m.bytecode;
    for (std::size_t pc = 0; pc + 4u <= code.size(); ++pc) {
        if (code[pc] != 0x07u) continue;
        for (const std::string& s : literal_array_strings(abc, m, code, pc)) {
            const std::string leaf = class_leaf(s);
            const auto it = by_leaf.find(leaf);
            if (it != by_leaf.end()) return it->second;
        }
    }
    return nullptr;
}

bool is_arkui_component(const std::string& name);
std::size_t property_id_pos(std::uint8_t op);

// Older `observeComponentCreation(builder)` lowers the constructor into the
// builder body (`X.create()`), not the observation site. Recover that real
// component type from the builder.
std::string component_from_builder(const AbcFile& abc, const AbcMethod& m) {
    const auto& code = m.bytecode;
    std::string last_global;
    for (std::size_t pc = 0; pc + 4u <= code.size();) {
        const std::uint8_t op = code[pc];
        if (op == 0x42u || op == 0x90u) {
            const std::size_t rel = (op == 0x42u) ? 2u : 3u;
            const std::string prop = id16_at(abc, m, code, pc + rel);
            if ((prop == "create" || prop == "createWithChild" ||
                 prop == "createWithLabel") &&
                is_arkui_component(last_global)) {
                return last_global;
            }
            pc += rel + 2u;
            continue;
        }
        if (op == 0x3fu || op == 0x8cu) {
            const std::size_t rel = (op == 0x3fu) ? 2u : 3u;
            last_global = id16_at(abc, m, code, pc + rel);
            pc += rel + 2u;
            continue;
        }
        ++pc;
    }
    return {};
}

// True when a method really constructs UI (used to avoid following ordinary
// getters and event handlers through property loads).
bool has_ui_calls(const AbcFile& abc, const AbcMethod& m) {
    const auto& code = m.bytecode;
    std::string last_global;
    for (std::size_t pc = 0; pc < code.size();) {
        const std::uint8_t op = code[pc];
        if (op == 0x42u || op == 0x90u || op == 0x43u || op == 0x7au) {
            const std::size_t rel = property_id_pos(op);
            if (rel != std::string::npos && pc + rel + 2u <= code.size()) {
                const std::string prop = id16_at(abc, m, code, pc + rel);
                if (prop == "observeComponentCreation" ||
                    prop == "observeComponentCreation2" ||
                    prop == "forEachUpdateFunction" ||
                    prop == "ifElseBranchUpdateFunction") {
                    return true;
                }
                if ((prop == "create" || prop == "createWithChild" ||
                     prop == "createWithLabel") &&
                    is_arkui_component(last_global)) {
                    return true;
                }
            }
            const bool is_store = (op == 0x43u || op == 0x7au || op == 0x91u || op == 0xccu);
            pc += rel == std::string::npos ? 1u : rel + 2u + (is_store ? 1u : 0u);
            continue;
        }
        if (op == 0x3fu || op == 0x8cu) {
            const std::size_t rel = (op == 0x3fu) ? 2u : 3u;
            if (pc + rel + 2u <= code.size()) {
                const std::string name = id16_at(abc, m, code, pc + rel);
                if (is_arkui_component(name) || name == "ViewPU") last_global = name;
            }
            pc += rel + 2u;
            continue;
        }
        ++pc;
    }
    return false;
}

// Item/child builders are passed as DefineFunc arguments to create(),
// createWithChild(), forEachUpdateFunction() and as object properties such
// as bindSheet({ builder: ... }).
bool is_item_builder_property(const std::string& prop) {
    return prop == "create" || prop == "createWithChild" ||
           prop == "createWithLabel" || prop == "forEachUpdateFunction" ||
           prop == "ifElseBranchUpdateFunction" || prop == "builder" ||
           prop == "itemBuilder";
}

// Position of the id16 operand of a property load/store opcode, or npos.
std::size_t property_id_pos(std::uint8_t op) {
    switch (op) {
        case 0x42u: case 0x43u: case 0x7au: return 2u;
        case 0x90u: case 0x91u: case 0xccu: return 3u;
        default: return std::string::npos;
    }
}

// ---- component recognition ------------------------------------------------

bool is_arkui_component(const std::string& name) {
    if (name.empty() || name.size() > 32u) return false;
    static const std::set<std::string> kExtra = {
        "If", "ForEach", "Canvas", "Panel", "CustomDialog", "AbilityComponent",
        "FormLink", "LocalStorage", "NodeContainer", "XComponent", "Video",
        "Web", "RichEditor", "DataPanel", "Gauge", "Rating", "Select",
        "Checkbox", "CheckboxGroup", "Radio", "Picker", "DatePicker",
        "TimePicker", "TextPicker", "Menu", "MenuItem", "Popup", "Span",
        "Navigation", "NavDestination", "SideBarContainer", "SplitLayout",
    };
    if (kExtra.count(name) != 0) return true;
    const ArkComponentKind kind = parse_ark_component_kind(name);
    return kind != ArkComponentKind::Custom || name == "Custom";
}

bool is_text_component(const std::string& name) {
    return name == "Text" || name == "Button" || name == "TextInput" ||
           name == "Span" || name == "ListItem";
}

bool looks_like_style_literal(const std::string& s) {
    if (s.empty() || s.size() > 180u) return true;
    if (s.find("px") != std::string::npos) return true;
    if (s.find('%') != std::string::npos) return true;
    if (s[0] == '#') return true;
    if (s == "function" || s == "object" || s == "undefined") return true;
    bool all_digits = true;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            all_digits = false;
            break;
        }
    }
    return all_digits;
}

// ---- bytecode walker ------------------------------------------------------

const AbcMethod* find_initial_render(const AbcClass& cls);

struct NodeRec {
    ArkUiNode node;
    NodeRec* parent = nullptr;
    std::vector<NodeRec*> children;
};

class Walker {
public:
    Walker(const AbcFile& abc,
           const std::map<std::uint32_t, const AbcMethod*>& by_offset,
           const std::map<unsigned, ResourceEntry>& by_id,
           const std::map<std::uint32_t, const AbcClass*>& custom_class_by_method,
           const std::set<std::uint32_t>& item_builder_offsets,
           const std::map<std::string, std::map<std::string, const AbcMethod*>>& class_methods,
           const std::set<std::uint32_t>& ui_method_offsets)
        : abc_(abc), by_offset_(by_offset), by_id_(by_id),
          custom_class_by_method_(custom_class_by_method),
          item_builder_offsets_(item_builder_offsets),
          class_methods_(class_methods), ui_method_offsets_(ui_method_offsets) {}

    void walk(const AbcMethod& method) {
        if (depth_ >= kMaxWalkDepth || nodes_.size() >= kMaxNodes) return;
        if (!visited_.insert(method.offset).second) return;
        ++depth_;

        const auto& code = method.bytecode;
        std::string last_global;
        std::string last_property;
        std::string pending_text;
        // Item builders create their components with direct X.create() calls
        // rather than observeComponentCreation; those calls are real widgets.
        const bool direct_create = item_builder_offsets_.count(method.offset) != 0u;
        std::size_t pc = 0;
        while (pc < code.size()) {
            const std::uint8_t op = code[pc];

            // observeComponentCreation2(builder, Component) in the current DSL
            // and observeComponentCreation(builder) in the older DSL are the
            // structural primitives; both carry a real builder and a real
            // component constructor.
            bool is_obs = false;
            if (op == 0x42u && pc + 3u <= code.size()) {
                const std::string prop = id16_at(abc_, method, code, pc + 2u);
                is_obs = prop == "observeComponentCreation2" ||
                         prop == "observeComponentCreation";
            } else if (op == 0x90u && pc + 4u <= code.size()) {
                const std::string prop = id16_at(abc_, method, code, pc + 3u);
                is_obs = prop == "observeComponentCreation2" ||
                         prop == "observeComponentCreation";
            }
            if (is_obs) {
                const AbcMethod* builder = nullptr;
                std::string component;
                std::size_t end = std::min(code.size(), pc + kObsWindow);
                std::size_t call_end = end;
                for (std::size_t q = pc + 1u; q < end; ++q) {
                    if (code[q] == 0x33u && q + 4u <= code.size()) {  // definefunc
                        if (!builder) builder = method_at_id(abc_, method, raw_id16(code, q + 2u), by_offset_);
                    } else if (code[q] == 0x3fu && q + 3u <= code.size()) {
                        const std::string name = id16_at(abc_, method, code, q + 2u);
                        if (component.empty() && is_arkui_component(name)) component = name;
                    } else if (code[q] == 0x8cu && q + 4u <= code.size()) {
                        const std::string name = id16_at(abc_, method, code, q + 3u);
                        if (component.empty() && is_arkui_component(name)) component = name;
                    } else if (code[q] >= 0x2du && code[q] <= 0x30u) {
                        if (call_end == end) call_end = q + 1u;
                    }
                }
                const auto custom = builder
                                        ? custom_class_by_method_.find(builder->offset)
                                        : custom_class_by_method_.end();
                if (custom != custom_class_by_method_.end()) {
                    // A literal-array class descriptor is stronger evidence
                    // than a neighbouring global seen in the scan window.
                    component = class_leaf(custom->second->name);
                } else if (component.empty() && builder) {
                    component = component_from_builder(abc_, *builder);
                }
                if (!component.empty()) {
                    NodeRec* parent = open_.empty() ? nullptr : open_.back();
                    NodeRec* rec = add(component, parent);
                    open_.push_back(rec);
                    if (builder && depth_ < kMaxWalkDepth) {
                        const AbcMethod* body = builder;
                        if (custom != custom_class_by_method_.end()) {
                            if (const AbcMethod* render = find_initial_render(*custom->second)) {
                                body = render;
                            }
                        }
                        walk(*body);
                    }
                }
                pc = call_end;
                continue;
            }

            // Item builders arrive as DefineFunc arguments to create()/
            // createWithChild()/forEachUpdateFunction(). Follow the real
            // method so a list gains the children the bytecode constructs.
            if (op == 0x33u && pc + 5u <= code.size()) {
                const AbcMethod* target = method_at_id(
                    abc_, method, raw_id16(code, pc + 2u), by_offset_);
                if (target && item_builder_offsets_.count(target->offset) != 0u &&
                    depth_ < kMaxWalkDepth) {
                    walk(*target);
                }
                pc += 5u;
                continue;
            }

            // Component.pop() closes the component most recently opened.
            if ((op == 0x42u || op == 0x90u)) {
                const std::size_t p = (op == 0x42u) ? pc + 2u : pc + 3u;
                const std::string prop = id16_at(abc_, method, code, p);
                last_property = prop;
                if (direct_create && is_item_builder_property(prop) &&
                    is_arkui_component(last_global) && nodes_.size() < kMaxNodes) {
                    NodeRec* parent = open_.empty() ? nullptr : open_.back();
                    NodeRec* rec = add(last_global, parent);
                    open_.push_back(rec);
                }
                // A builder stored as a method property (`this.myBuilder.call()`)
                // is followed when the named method really contains UI calls.
                const auto cm = class_methods_.find(method.class_name);
                if (cm != class_methods_.end()) {
                    const auto mi = cm->second.find(prop);
                    if (mi != cm->second.end() &&
                        ui_method_offsets_.count(mi->second->offset) != 0u &&
                        depth_ < kMaxWalkDepth) {
                        walk(*mi->second);
                    }
                }
                if (prop == "pop" && is_arkui_component(last_global) && !open_.empty()) {
                    open_.pop_back();
                    last_global.clear();
                }
                pc += (op == 0x42u) ? 4u : 5u;
                continue;
            }

            if (op == 0x3fu || op == 0x8cu) {  // tryldglobalbyname
                const std::size_t p = (op == 0x3fu) ? pc + 2u : pc + 3u;
                const std::string name = id16_at(abc_, method, code, p);
                if (is_arkui_component(name)) last_global = name;
                pc += (op == 0x3fu) ? 4u : 5u;
                continue;
            }

            if (op == 0x3eu) {  // lda.str — a real literal in the method
                const std::string text = id16_at(abc_, method, code, pc + 1u);
                if (!looks_like_style_literal(text) &&
                    (last_property == "create" || last_property == "createWithLabel" ||
                     last_property == "createWithChild")) {
                    pending_text = text;
                }
                pc += 3u;
                continue;
            }

            if (op == 0x07u) {  // createobjectwithbuffer — $r(<id>)
                const std::string text = literal_resource_text(abc_, method, code, pc, by_id_);
                if (!text.empty() && !looks_like_style_literal(text)) pending_text = text;
                pc += 4u;
                continue;
            }

            if ((op >= 0x29u && op <= 0x30u) && !pending_text.empty()) {
                if (!open_.empty() && open_.back()->node.text.empty() &&
                    is_text_component(open_.back()->node.type_name)) {
                    open_.back()->node.text = pending_text;
                }
                pending_text.clear();
            }
            ++pc;
        }
        --depth_;
    }

    std::vector<NodeRec*> roots() const { return roots_; }
    std::size_t node_count() const { return nodes_.size(); }
    std::size_t text_count() const {
        std::size_t n = 0;
        for (const auto& rec : nodes_) {
            if (!rec->node.text.empty()) ++n;
        }
        return n;
    }

    ArkUiNode materialize(NodeRec* rec, int& counter) const {
        ArkUiNode node = rec->node;
        node.id = "panda_" + node.type_name + "_" + std::to_string(counter++);
        node.kind = parse_ark_component_kind(node.type_name);
        for (NodeRec* child : rec->children) {
            node.children.push_back(materialize(child, counter));
        }
        return node;
    }

    ArkUiNode materialize_forest() const {
        ArkUiNode root;
        int counter = 1;
        if (roots_.size() == 1u) return materialize(roots_.front(), counter);
        root.kind = ArkComponentKind::Column;
        root.type_name = "Column";
        root.id = "panda_root";
        root.padding = 12.0;
        for (NodeRec* rec : roots_) root.children.push_back(materialize(rec, counter));
        return root;
    }

private:
    NodeRec* add(const std::string& type, NodeRec* parent) {
        auto rec = std::make_unique<NodeRec>();
        rec->node.kind = parse_ark_component_kind(type);
        rec->node.type_name = type;
        rec->node.id = "panda_" + type;
        rec->parent = parent;
        NodeRec* raw = rec.get();
        nodes_.push_back(std::move(rec));
        if (parent) parent->children.push_back(raw);
        else roots_.push_back(raw);
        return raw;
    }

    const AbcFile& abc_;
    const std::map<std::uint32_t, const AbcMethod*>& by_offset_;
    const std::map<unsigned, ResourceEntry>& by_id_;
    std::vector<std::unique_ptr<NodeRec>> nodes_;
    std::vector<NodeRec*> roots_;
    std::vector<NodeRec*> open_;
    std::set<std::uint32_t> visited_;
    const std::map<std::uint32_t, const AbcClass*>& custom_class_by_method_;
    const std::set<std::uint32_t>& item_builder_offsets_;
    const std::map<std::string, std::map<std::string, const AbcMethod*>>& class_methods_;
    const std::set<std::uint32_t>& ui_method_offsets_;
    int depth_ = 0;
};

const AbcMethod* find_initial_render(const AbcClass& cls) {
    for (const auto& m : cls.methods) {
        if (m.name.find("initialRender") != std::string::npos) return &m;
    }
    return nullptr;
}

std::vector<std::string> page_names(const fs::path& root) {
    std::vector<std::string> pages;
    std::string text;
    const fs::path profile = root / "resources" / "base" / "profile" / "main_pages.json";
    if (!read_text(profile, &text)) return pages;
    std::string error;
    const auto doc = parse_json5(text, &error);
    if (!doc || doc->kind != JsonValue::Kind::Object) return pages;
    const JsonValue* src = doc->get("src");
    if (!src || src->kind != JsonValue::Kind::Array) return pages;
    for (const JsonValue& entry : src->array_val) {
        if (entry.kind != JsonValue::Kind::String || entry.str_val.empty()) continue;
        if (pages.size() >= kMaxPages) break;
        pages.push_back(entry.str_val);
    }
    return pages;
}

std::string page_label(const fs::path& root,
                       const std::map<std::string, std::string>& by_key,
                       std::string* bundle_out) {
    std::string text;
    if (!read_text(root / "module.json", &text)) return {};
    std::string error;
    const auto doc = parse_json5(text, &error);
    if (!doc || doc->kind != JsonValue::Kind::Object) return {};
    const JsonValue* app = doc->get("app");
    if (!app || app->kind != JsonValue::Kind::Object) return {};
    *bundle_out = app->get_string("bundleName");
    const std::string ref = app->get_string("label");
    if (ref.empty()) return {};
    if (ref[0] != '$') return ref;
    const std::string key = reference_key(ref);
    const auto it = by_key.find(key);
    return it == by_key.end() ? std::string() : it->second;
}

}  // namespace

bool build_arkui_from_panda_bytecode(const std::string& install_dir,
                                     ArkUiEngine* engine,
                                     std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (!engine) return fail("ArkUiEngine is null");

    const fs::path root(install_dir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return fail("install directory does not exist: " + install_dir);
    }
    const fs::path abc_path = root / "ets" / "modules.abc";
    if (!fs::is_regular_file(abc_path, ec)) {
        return fail("bytecode rebuild needs ets/modules.abc in " + install_dir);
    }
    const AbcFile abc = parse_abc_file(abc_path.string());
    if (!abc.valid) {
        return fail("cannot parse " + abc_path.string() + ": " + abc.error);
    }

    std::map<unsigned, ResourceEntry> by_id;
    std::map<std::string, std::string> by_key;
    std::vector<std::uint8_t> index_bytes;
    if (read_binary(root / "resources.index", &index_bytes)) {
        (void)decode_resource_ids(index_bytes, &by_id, &by_key);
    }

    std::string bundle;
    std::string label = page_label(root, by_key, &bundle);
    if (label.empty()) label = bundle.empty() ? "OpenHarmony App" : bundle;

    std::map<std::uint32_t, const AbcMethod*> by_offset;
    std::map<std::string, const AbcClass*> classes_by_leaf;
    std::map<std::string, std::map<std::string, const AbcMethod*>> class_methods;
    for (const auto& cls : abc.classes) {
        classes_by_leaf.emplace(class_leaf(cls.name), &cls);
        for (const auto& m : cls.methods) {
            by_offset[m.offset] = &m;
            class_methods[m.class_name][m.name] = &m;
        }
    }

    // Precompute which builder instantiates which custom component, and which
    // methods are item/child builders passed to a create-like call.
    std::map<std::uint32_t, const AbcClass*> custom_class_by_method;
    std::set<std::uint32_t> item_builder_offsets;
    std::set<std::uint32_t> ui_method_offsets;
    for (const auto& cls : abc.classes) {
        for (const auto& m : cls.methods) {
            if (has_ui_calls(abc, m)) ui_method_offsets.insert(m.offset);
            if (const AbcClass* custom =
                    custom_class_for_method(abc, m, classes_by_leaf)) {
                custom_class_by_method[m.offset] = custom;
            }
            const auto& code = m.bytecode;
            for (std::size_t pc = 0; pc + 4u <= code.size(); ++pc) {
                if (code[pc] != 0x33u) continue;  // definefunc
                const AbcMethod* target = method_at_id(
                    abc, m, raw_id16(code, pc + 2u), by_offset);
                if (!target) continue;
                bool create_context = false;
                const std::size_t back = pc > kMaxBuilderWindow
                                             ? pc - kMaxBuilderWindow : 0u;
                const std::size_t fwd = std::min(code.size(), pc + kMaxBuilderWindow);
                for (std::size_t q = back; q < fwd && !create_context; ++q) {
                    const std::size_t rel = property_id_pos(code[q]);
                    if (rel == std::string::npos) continue;
                    const std::size_t idp = q + rel;
                    if (idp + 2u > code.size()) continue;
                    if (is_item_builder_property(id16_at(abc, m, code, idp))) {
                        create_context = true;
                    }
                }
                if (create_context) item_builder_offsets.insert(target->offset);
            }
        }
    }

    const std::vector<std::string> pages = page_names(root);
    std::vector<const AbcClass*> candidates;
    for (const std::string& page : pages) {
        const std::string leaf = fs::path(page).filename().string();
        for (const auto& cls : abc.classes) {
            if (cls.name.find(page) != std::string::npos ||
                (!leaf.empty() && cls.name.find(leaf) != std::string::npos)) {
                if (find_initial_render(cls)) candidates.push_back(&cls);
            }
        }
    }
    if (candidates.empty()) {
        for (const auto& cls : abc.classes) {
            if (find_initial_render(cls)) candidates.push_back(&cls);
        }
    }
    if (candidates.empty()) {
        return fail("no ArkUI page class with initialRender in " + abc_path.string());
    }

    std::string last_error;
    ArkUiNode best_root;
    std::size_t best_score = 0;
    for (const AbcClass* cls : candidates) {
        const AbcMethod* render = find_initial_render(*cls);
        if (!render) continue;
        Walker walker(abc, by_offset, by_id, custom_class_by_method,
                      item_builder_offsets, class_methods, ui_method_offsets);
        walker.walk(*render);
        const std::size_t score = walker.node_count() + 3u * walker.text_count();
        if (walker.node_count() == 0u) {
            last_error = "no observeComponentCreation/observeComponentCreation2 "
                         "component calls in " + cls->name;
            continue;
        }
        if (score > best_score) {
            best_score = score;
            best_root = walker.materialize_forest();
        }
    }
    if (best_score == 0u) {
        return fail(last_error.empty()
                        ? "no reconstructible ArkUI widget tree in " + abc_path.string()
                        : last_error);
    }

    engine->set_resource_root(install_dir);
    engine->set_title(label);
    engine->set_root(std::move(best_root));
    engine->mount();
    if (error) error->clear();
    return true;
}

bool arkui_bytecode_ui_self_test() {
    std::cout << "== arkui_bytecode_ui_self_test ==\n";
    bool ok = true;
    std::error_code ec;
    const auto check = [&ok](bool condition, const std::string& what) {
        std::cout << (condition ? "  ok   " : "  FAIL ") << what << "\n";
        if (!condition) ok = false;
    };

    // Fixture 1: decode a real-shaped resources.index (two IDSS sections).
    std::vector<std::uint8_t> index_bytes;

    // Build the fixture explicitly: "IDSS", count, marker, two 8-byte records,
    // then two descriptors. The last section must win (localized strings).
    const auto emit_section = [&index_bytes](const std::string& v2,
                                              const std::string& v3) {
        for (const char c : std::string("IDSS")) index_bytes.push_back(static_cast<std::uint8_t>(c));
        index_bytes.push_back(2);
        index_bytes.resize(index_bytes.size() + 3u, 0);  // count = 2
        index_bytes.insert(index_bytes.end(), {0, 0, 0, 1});  // marker
        const std::size_t recs = index_bytes.size();
        index_bytes.resize(recs + 16u, 0);  // two records, filled below
        const auto emit_desc = [&](const std::string& value, unsigned id) {
            const std::uint32_t off = static_cast<std::uint32_t>(index_bytes.size());
            std::vector<std::uint8_t> desc;
            const auto p32 = [&desc](std::uint32_t v) {
                for (int k = 0; k < 4; ++k) desc.push_back(static_cast<std::uint8_t>(v >> (k * 8u)));
            };
            const auto p16 = [&desc](std::uint16_t v) {
                desc.push_back(static_cast<std::uint8_t>(v & 0xffu));
                desc.push_back(static_cast<std::uint8_t>(v >> 8u));
            };
            p32(12u + static_cast<std::uint32_t>(value.size() + 1u) + 8u);
            p32(9u);
            const std::uint32_t rid = (id << 24u) | 1u;
            desc.push_back(static_cast<std::uint8_t>(rid >> 24u));
            desc.push_back(static_cast<std::uint8_t>(rid >> 16u));
            desc.push_back(static_cast<std::uint8_t>(rid >> 8u));
            desc.push_back(static_cast<std::uint8_t>(rid));
            p16(static_cast<std::uint16_t>(value.size() + 1u));
            desc.insert(desc.end(), value.begin(), value.end());
            desc.push_back(0);
            const std::string key = "fixture_key_" + std::to_string(id);
            p16(static_cast<std::uint16_t>(key.size() + 1u));
            desc.insert(desc.end(), key.begin(), key.end());
            desc.push_back(0);
            std::copy(desc.begin(), desc.end(), std::back_inserter(index_bytes));
            return off;
        };
        const std::uint32_t off2 = emit_desc(v2, 2u);
        const std::uint32_t off3 = emit_desc(v3, 3u);
        for (int k = 0; k < 4; ++k) {
            index_bytes[recs + k] = static_cast<std::uint8_t>(off2 >> (k * 8u));
            index_bytes[recs + 8u + k] = static_cast<std::uint8_t>(off3 >> (k * 8u));
        }
    };
    emit_section("Allow", "Deny");
    emit_section("允许", "不允许");

    std::map<unsigned, ResourceEntry> by_id;
    std::map<std::string, std::string> by_key;
    const std::size_t sections = decode_resource_ids(index_bytes, &by_id, &by_key);
    check(sections == 2u, "decoder reads both IDSS sections");
    check(by_id[2].value == "允许" && by_id[3].value == "不允许",
          "last (localized) resource section wins: " + by_id[2].value + " / " + by_id[3].value);
    check(by_key["fixture_key_2"] == "允许", "resource key lookup works");

    // Fixture 2: a synthetic .abc index + literal array holding resource id 3.
    AbcFile fake;
    fake.valid = true;
    AbcMethod method;
    method.offset = 1000u;
    method.bytecode = {0x07u, 0x00u, 0x00u, 0x00u};  // createobjectwithbuffer id16=0
    fake.raw_bytes.resize(64u, 0);
    fake.raw_bytes[0] = 1;  // literal array count = 1
    fake.raw_bytes[4] = 0x02u;
    fake.raw_bytes[5] = 0x03u;
    fake.raw_bytes[6] = 0x00u;
    fake.raw_bytes[7] = 0x00u;
    fake.raw_bytes[8] = 0x01u;
    AbcIndexRegion reg;
    reg.start = 0u;
    reg.end = 2000u;
    reg.method_idx.push_back(0u);
    fake.index_regions.push_back(std::move(reg));
    check(literal_resource_text(fake, method, method.bytecode, 0u, by_id) == "不允许",
          "literal array resource id resolves to a real package string");

    ArkUiEngine failure_engine;
    std::string failure_error;
    const bool rejected = build_arkui_from_panda_bytecode(
        (fs::temp_directory_path() / "finn_arkui_bytecode_missing").string(),
        &failure_engine, &failure_error);
    check(!rejected && failure_error.find("install directory") != std::string::npos,
          "missing install dir is rejected: " + failure_error);

    std::vector<fs::path> real_dirs;
    if (const char* single = std::getenv("FINN_OHOS_INSTALL_DIR")) {
        if (*single) real_dirs.emplace_back(single);
    } else {
        std::string home;
        if (const char* h = std::getenv("HOME")) home = h;
        const fs::path apps = fs::path(home) / ".local" / "share" / "finn" / "ohos" / "apps";
        if (fs::is_directory(apps, ec)) {
            for (fs::directory_iterator it(apps, ec), end;
                 it != end && !ec; it.increment(ec)) {
                if (it->is_directory(ec)) real_dirs.push_back(it->path());
            }
        }
    }
    std::sort(real_dirs.begin(), real_dirs.end());

    std::size_t built = 0;
    std::size_t labelled = 0;
    struct RealResult {
        std::string name;
        std::string summary;
        bool has_text = false;
    };
    std::vector<RealResult> results;
    for (const fs::path& dir : real_dirs) {
        if (!fs::is_regular_file(dir / "ets" / "modules.abc", ec)) continue;
        ArkUiEngine engine;
        std::string error;
        if (!build_arkui_from_panda_bytecode(dir.string(), &engine, &error)) {
            std::cout << "  ---- " << dir.filename().string() << ": " << error << "\n";
            continue;
        }
        ++built;
        const std::string summary = engine.render_headless_summary();
        const bool has_text = summary.find("text=\"") != std::string::npos;
        if (has_text) ++labelled;
        results.push_back({dir.filename().string(), summary, has_text});
    }
    std::stable_sort(results.begin(), results.end(),
                     [](const RealResult& a, const RealResult& b) {
                         return a.has_text && !b.has_text;
                     });
    for (std::size_t i = 0; i < results.size() && i < 3u; ++i) {
        std::cout << "  -- real " << results[i].name << " --\n" << results[i].summary;
    }
    if (!real_dirs.empty()) {
        check(built > 0u, "at least one real HAP produced a bytecode-derived tree");
        check(labelled > 0u, "at least one real tree carries a real package string");
    } else {
        std::cout << "  (no installed real HAPs: set FINN_OHOS_INSTALL_DIR to verify one)\n";
    }

    std::cout << (ok ? "SELFTEST: PASS\n" : "SELFTEST: FAIL\n");
    return ok;
}

}  // namespace finn::ohos
