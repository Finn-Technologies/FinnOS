#include "darwin/macho_loader.hpp"

#include "darwin/framework_coverage.hpp"

#include "darwin/appkit_bridge.hpp"
#include "darwin/carbon_compat.hpp"
#include "darwin/compat_symbols.hpp"
#include "darwin/coregraphics.hpp"
#include "darwin/foundation.hpp"
#include "darwin/libsystem.hpp"
#include "darwin/macho_format.hpp"
#include "darwin/objc_runtime.hpp"
#include "darwin/plist.hpp"
#include "darwin/swift_runtime.hpp"
#include "darwin/swiftui/contract.hpp"

#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>

namespace fs = std::filesystem;

namespace finn::darwin {
namespace {

std::uint32_t swap32(std::uint32_t v) {
    return ((v & 0xff000000u) >> 24) | ((v & 0x00ff0000u) >> 8) |
           ((v & 0x0000ff00u) << 8) | ((v & 0x000000ffu) << 24);
}

std::uint64_t swap64(std::uint64_t v) {
    return (static_cast<std::uint64_t>(swap32(static_cast<std::uint32_t>(v & 0xffffffffu))) << 32) |
           swap32(static_cast<std::uint32_t>(v >> 32));
}

std::uint32_t host_cputype() {
#if defined(__aarch64__)
    return kCpuTypeArm64;
#elif defined(__x86_64__)
    return kCpuTypeX86_64;
#else
    return 0;
#endif
}

std::string cputype_name(std::uint32_t ct) {
    switch (ct) {
        case kCpuTypeArm64: return "arm64";
        case kCpuTypeX86_64: return "x86_64";
        case kCpuTypeArm: return "arm";
        case kCpuTypeX86: return "x86";
        default: return "unknown";
    }
}

std::string host_arch_name() {
    struct utsname u{};
    if (::uname(&u) == 0) return u.machine;
    return cputype_name(host_cputype());
}

std::size_t align_up(std::size_t v, std::size_t a) {
    return (v + a - 1) & ~(a - 1);
}

std::size_t align_down(std::size_t v, std::size_t a) {
    return v & ~(a - 1);
}

std::uint64_t read_uleb128(const std::uint8_t** p, const std::uint8_t* end) {
    std::uint64_t result = 0;
    int shift = 0;
    while (*p < end) {
        const std::uint8_t b = *(*p)++;
        result |= static_cast<std::uint64_t>(b & 0x7fu) << shift;
        if ((b & 0x80u) == 0) break;
        shift += 7;
        if (shift >= 64) break;
    }
    return result;
}

std::int64_t read_sleb128(const std::uint8_t** p, const std::uint8_t* end) {
    std::int64_t result = 0;
    int shift = 0;
    std::uint8_t b = 0;
    while (*p < end) {
        b = *(*p)++;
        result |= static_cast<std::int64_t>(b & 0x7fu) << shift;
        shift += 7;
        if ((b & 0x80u) == 0) break;
        if (shift >= 64) break;
    }
    if (shift < 64 && (b & 0x40u)) {
        result |= -(static_cast<std::int64_t>(1) << shift);
    }
    return result;
}

std::string fixed_cstr(const char* buf, std::size_t max_len) {
    std::size_t len = 0;
    while (len < max_len && buf[len] != '\0') ++len;
    return std::string(buf, len);
}

bool read_file_bytes(const std::string& path, std::vector<std::uint8_t>* out,
                     std::string* error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        if (error) *error = "unable to open Mach-O file: " + path;
        return false;
    }
    const std::streamoff sz = in.tellg();
    if (sz < 32) {
        if (error) *error = "file smaller than Mach-O header: " + path;
        return false;
    }
    out->resize(static_cast<std::size_t>(sz));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(out->data()), sz);
    if (in.gcount() != sz) {
        if (error) *error = "truncated read of Mach-O file: " + path;
        return false;
    }
    return true;
}

struct SliceSpan {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::uint32_t cputype = 0;
};

bool select_macho_slice(const std::vector<std::uint8_t>& file,
                        std::vector<std::string>* all_arches,
                        SliceSpan* selected,
                        std::string* error) {
    if (file.size() < 8) {
        if (error) *error = "file too small for Mach-O header";
        return false;
    }
    std::uint32_t magic = 0;
    std::memcpy(&magic, file.data(), 4);

    if (magic == kMhMagic64) {
        if (file.size() < sizeof(MachHeader64)) {
            if (error) *error = "truncated Mach-O 64-bit header";
            return false;
        }
        MachHeader64 hdr{};
        std::memcpy(&hdr, file.data(), sizeof(hdr));
        if (all_arches) all_arches->push_back(cputype_name(hdr.cputype));
        selected->offset = 0;
        selected->size = file.size();
        selected->cputype = hdr.cputype;
        return true;
    }

    const std::uint32_t magic_be = swap32(magic);
    if (magic_be == kFatMagic || magic_be == kFatMagic64) {
        const bool fat64 = (magic_be == kFatMagic64);
        std::uint32_t nfat_raw = 0;
        std::memcpy(&nfat_raw, file.data() + 4, 4);
        const std::uint32_t nfat = swap32(nfat_raw);
        if (nfat == 0 || nfat > 16) {
            if (error) *error = "invalid Universal Mach-O slice count";
            return false;
        }
        const std::size_t entry_sz = fat64 ? 32 : 20;
        if (8 + static_cast<std::size_t>(nfat) * entry_sz > file.size()) {
            if (error) *error = "truncated Universal Mach-O header";
            return false;
        }
        const std::uint32_t want = host_cputype();
        bool found_host = false;
        SliceSpan first_valid{};
        bool have_first = false;

        for (std::uint32_t i = 0; i < nfat; ++i) {
            const std::uint8_t* p = file.data() + 8 + static_cast<std::size_t>(i) * entry_sz;
            std::uint32_t ct_raw = 0;
            std::memcpy(&ct_raw, p, 4);
            const std::uint32_t ct = swap32(ct_raw);
            std::uint64_t off = 0;
            std::uint64_t sz = 0;
            if (fat64) {
                std::uint64_t o_raw = 0, s_raw = 0;
                std::memcpy(&o_raw, p + 8, 8);
                std::memcpy(&s_raw, p + 16, 8);
                off = swap64(o_raw);
                sz = swap64(s_raw);
            } else {
                std::uint32_t o_raw = 0, s_raw = 0;
                std::memcpy(&o_raw, p + 8, 4);
                std::memcpy(&s_raw, p + 12, 4);
                off = swap32(o_raw);
                sz = swap32(s_raw);
            }
            if (all_arches) all_arches->push_back(cputype_name(ct));
            if (off + sz <= file.size()) {
                SliceSpan sp{static_cast<std::size_t>(off), static_cast<std::size_t>(sz), ct};
                if (!have_first) {
                    first_valid = sp;
                    have_first = true;
                }
                if (ct == want) {
                    *selected = sp;
                    found_host = true;
                }
            }
        }
        if (found_host) return true;
        if (have_first) {
            *selected = first_valid;
            return true;
        }
        if (error) *error = "no valid slice in Universal Mach-O";
        return false;
    }

    if (error) *error = "unsupported binary magic (expected 64-bit Mach-O or Universal)";
    return false;
}

bool is_builtin_darwin_library(std::string_view dylib) {
    static constexpr const char* kSupported[] = {
        "libSystem.B.dylib",
        "libSystem.dylib",
        "libobjc.A.dylib",
        "libobjc.dylib",
        "Cocoa",
        "CoreFoundation",
        "Foundation",
        "AppKit",
        "CoreGraphics",
        "QuartzCore",
        "CoreText",
        "libswiftCore",
        "libdispatch",
        "libsystem_",
    };
    for (const char* s : kSupported) {
        if (dylib.find(s) != std::string_view::npos) return true;
    }
    return false;
}

std::string framework_short_name(std::string_view dylib) {
    const std::size_t slash = dylib.rfind('/');
    return std::string(slash == std::string_view::npos ? dylib : dylib.substr(slash + 1));
}

struct ParsedMacho {
    MachHeader64 hdr{};
    std::vector<SegmentCommand64> segments;
    std::vector<Section64> sections;
    std::vector<std::string> dylibs;
    std::vector<bool> dylib_weak;
    std::vector<std::string> rpaths;
    std::uint64_t entry_offset = 0;
    bool has_main = false;
    bool has_dyld_info = false;
    DyldInfoCommand dyld_info{};
    bool has_chained_fixups = false;
    LinkeditDataCommand chained_fixups{};
    bool has_exports_trie = false;
    LinkeditDataCommand exports_trie{};
    bool has_symtab = false;
    SymtabCommand symtab{};
};

bool parse_macho_slice(const std::uint8_t* slice, std::size_t size,
                       ParsedMacho* out, std::string* error) {
    if (size < sizeof(MachHeader64)) {
        if (error) *error = "slice smaller than MachHeader64";
        return false;
    }
    std::memcpy(&out->hdr, slice, sizeof(MachHeader64));
    if (out->hdr.magic != kMhMagic64) {
        if (error) *error = "slice is not 64-bit little-endian Mach-O";
        return false;
    }

    std::size_t cursor = sizeof(MachHeader64);
    for (std::uint32_t i = 0; i < out->hdr.ncmds; ++i) {
        if (cursor + sizeof(LoadCommand) > size) {
            if (error) *error = "truncated Mach-O load commands";
            return false;
        }
        LoadCommand lc{};
        std::memcpy(&lc, slice + cursor, sizeof(lc));
        if (lc.cmdsize < sizeof(LoadCommand) || cursor + lc.cmdsize > size) {
            if (error) *error = "invalid Mach-O load command size";
            return false;
        }

        if (lc.cmd == kLcSegment64 && lc.cmdsize >= sizeof(SegmentCommand64)) {
            SegmentCommand64 seg{};
            std::memcpy(&seg, slice + cursor, sizeof(seg));
            out->segments.push_back(seg);
            std::size_t sc = cursor + sizeof(SegmentCommand64);
            for (std::uint32_t s = 0; s < seg.nsects; ++s) {
                if (sc + sizeof(Section64) > cursor + lc.cmdsize) break;
                Section64 sec{};
                std::memcpy(&sec, slice + sc, sizeof(sec));
                out->sections.push_back(sec);
                sc += sizeof(Section64);
            }
        } else if (lc.cmd == kLcMain && lc.cmdsize >= sizeof(EntryPointCommand)) {
            EntryPointCommand ep{};
            std::memcpy(&ep, slice + cursor, sizeof(ep));
            out->entry_offset = ep.entryoff;
            out->has_main = true;
        } else if ((lc.cmd == kLcLoadDylib || lc.cmd == kLcLoadWeakDylib ||
                    lc.cmd == kLcReexportDylib || lc.cmd == kLcLazyLoadDylib) &&
                   lc.cmdsize >= sizeof(DylibCommand)) {
            DylibCommand dc{};
            std::memcpy(&dc, slice + cursor, sizeof(dc));
            if (dc.name_offset < lc.cmdsize) {
                const char* s = reinterpret_cast<const char*>(slice + cursor + dc.name_offset);
                out->dylibs.push_back(fixed_cstr(s, lc.cmdsize - dc.name_offset));
                out->dylib_weak.push_back(lc.cmd == kLcLoadWeakDylib);
            }
        } else if (lc.cmd == kLcRpath && lc.cmdsize >= 12) {
            std::uint32_t name_off = 0;
            std::memcpy(&name_off, slice + cursor + 8, 4);
            if (name_off < lc.cmdsize) {
                const char* s = reinterpret_cast<const char*>(slice + cursor + name_off);
                out->rpaths.push_back(fixed_cstr(s, lc.cmdsize - name_off));
            }
        } else if ((lc.cmd == kLcDyldInfo || lc.cmd == kLcDyldInfoOnly) &&
                   lc.cmdsize >= sizeof(DyldInfoCommand)) {
            std::memcpy(&out->dyld_info, slice + cursor, sizeof(DyldInfoCommand));
            out->has_dyld_info = true;
        } else if (lc.cmd == kLcDyldChainedFixups &&
                   lc.cmdsize >= sizeof(LinkeditDataCommand)) {
            std::memcpy(&out->chained_fixups, slice + cursor, sizeof(LinkeditDataCommand));
            out->has_chained_fixups = true;
        } else if (lc.cmd == kLcDyldExportsTrie &&
                   lc.cmdsize >= sizeof(LinkeditDataCommand)) {
            std::memcpy(&out->exports_trie, slice + cursor, sizeof(LinkeditDataCommand));
            out->has_exports_trie = true;
        } else if (lc.cmd == kLcSymtab && lc.cmdsize >= sizeof(SymtabCommand)) {
            std::memcpy(&out->symtab, slice + cursor, sizeof(SymtabCommand));
            out->has_symtab = true;
        }

        cursor += lc.cmdsize;
    }
    return true;
}

struct ChainedImportEntry {
    std::string name;
    std::int32_t lib_ordinal = 0;
    std::int64_t addend = 0;
    bool weak = false;
};

bool parse_chained_imports(const std::uint8_t* slice, std::size_t slice_size,
                           const LinkeditDataCommand& cmd,
                           std::vector<ChainedImportEntry>* out) {
    if (cmd.dataoff + sizeof(DyldChainedFixupsHeader) > slice_size) return false;
    const std::uint8_t* base = slice + cmd.dataoff;
    const std::size_t lim = std::min<std::size_t>(cmd.datasize, slice_size - cmd.dataoff);

    DyldChainedFixupsHeader hdr{};
    std::memcpy(&hdr, base, sizeof(hdr));
    if (hdr.imports_offset >= lim || hdr.symbols_offset >= lim) return false;

    const char* syms = reinterpret_cast<const char*>(base + hdr.symbols_offset);
    const std::size_t syms_len = lim - hdr.symbols_offset;

    out->reserve(hdr.imports_count);
    for (std::uint32_t i = 0; i < hdr.imports_count; ++i) {
        ChainedImportEntry imp;
        std::uint32_t name_off = 0;
        if (hdr.imports_format == kDyldChainedImport) {
            const std::size_t off = hdr.imports_offset + static_cast<std::size_t>(i) * 4;
            if (off + 4 > lim) break;
            std::uint32_t w = 0;
            std::memcpy(&w, base + off, 4);
            imp.lib_ordinal = static_cast<std::int8_t>(w & 0xffu);
            imp.weak = ((w >> 8) & 1u) != 0;
            name_off = w >> 9;
        } else if (hdr.imports_format == kDyldChainedImportAddend) {
            const std::size_t off = hdr.imports_offset + static_cast<std::size_t>(i) * 8;
            if (off + 8 > lim) break;
            std::uint32_t w = 0;
            std::int32_t add = 0;
            std::memcpy(&w, base + off, 4);
            std::memcpy(&add, base + off + 4, 4);
            imp.lib_ordinal = static_cast<std::int8_t>(w & 0xffu);
            imp.weak = ((w >> 8) & 1u) != 0;
            name_off = w >> 9;
            imp.addend = add;
        } else if (hdr.imports_format == kDyldChainedImportAddend64) {
            const std::size_t off = hdr.imports_offset + static_cast<std::size_t>(i) * 16;
            if (off + 16 > lim) break;
            std::uint64_t w = 0;
            std::int64_t add = 0;
            std::memcpy(&w, base + off, 8);
            std::memcpy(&add, base + off + 8, 8);
            imp.lib_ordinal = static_cast<std::int16_t>(w & 0xffffu);
            imp.weak = ((w >> 16) & 1u) != 0;
            name_off = static_cast<std::uint32_t>((w >> 32) & 0xffffffffu);
            imp.addend = add;
        } else {
            return false;
        }
        if (name_off < syms_len) {
            imp.name = fixed_cstr(syms + name_off, syms_len - name_off);
        }
        out->push_back(std::move(imp));
    }
    return true;
}

void collect_classic_bind_symbols(const std::uint8_t* slice, std::size_t slice_size,
                                  std::uint32_t off, std::uint32_t sz,
                                  std::vector<std::string>* out) {
    if (off == 0 || sz == 0 || off + sz > slice_size) return;
    const std::uint8_t* p = slice + off;
    const std::uint8_t* end = p + sz;
    while (p < end) {
        const std::uint8_t b = *p++;
        const std::uint8_t op = b & kBindOpcodeMask;
        switch (op) {
            case kBindOpcodeDone:
            case kBindOpcodeSetDylibOrdinalImm:
            case kBindOpcodeSetDylibSpecialImm:
            case kBindOpcodeSetTypeImm:
            case kBindOpcodeDoBind:
            case kBindOpcodeDoBindAddAddrImmScaled:
                break;
            case kBindOpcodeSetDylibOrdinalUleb:
            case kBindOpcodeAddAddrUleb:
            case kBindOpcodeDoBindAddAddrUleb:
                (void)read_uleb128(&p, end);
                break;
            case kBindOpcodeSetAddendSleb:
                (void)read_sleb128(&p, end);
                break;
            case kBindOpcodeSetSegmentAndOffsetUleb:
                (void)read_uleb128(&p, end);
                break;
            case kBindOpcodeDoBindUlebTimesSkippingUleb:
                (void)read_uleb128(&p, end);
                (void)read_uleb128(&p, end);
                break;
            case kBindOpcodeSetSymbolTrailingFlagsImm: {
                const char* s = reinterpret_cast<const char*>(p);
                while (p < end && *p != 0) ++p;
                if (p < end) ++p;
                out->emplace_back(s);
                break;
            }
            default:
                return;
        }
    }
}

void parse_exports_trie_node(const std::uint8_t* base, const std::uint8_t* end,
                             const std::uint8_t* node, const std::string& prefix,
                             std::uintptr_t image_base,
                             std::unordered_map<std::string, void*>* exports) {
    if (node < base || node >= end) return;
    const std::uint8_t* p = node;
    const std::uint64_t term_size = read_uleb128(&p, end);
    const std::uint8_t* after_term = p + term_size;
    if (after_term > end) return;

    if (term_size > 0) {
        const std::uint64_t flags = read_uleb128(&p, after_term);
        const std::uint64_t kind = flags & 0x3u;
        if (kind == 0 && (flags & 0x08u) == 0) {  // regular export, not re-export
            const std::uint64_t sym_off = read_uleb128(&p, after_term);
            (*exports)[prefix] = reinterpret_cast<void*>(image_base + sym_off);
        }
    }

    p = after_term;
    if (p >= end) return;
    const std::uint8_t child_count = *p++;
    for (std::uint8_t i = 0; i < child_count && p < end; ++i) {
        std::string edge;
        while (p < end && *p != 0) edge.push_back(static_cast<char>(*p++));
        if (p < end) ++p;
        const std::uint64_t child_off = read_uleb128(&p, end);
        if (child_off > 0 && base + child_off < end) {
            parse_exports_trie_node(base, end, base + child_off, prefix + edge,
                                    image_base, exports);
        }
    }
}

// A real .app loads its bundled @rpath dylibs before binding main's imports.
// diagnose must look at the same closure, or it reports symbols as missing that
// load_and_run actually resolves from MASShortcut.framework, Sparkle, etc.
struct BundledClosure {
    std::unordered_set<std::string> exports;
    std::vector<std::string> imports;       // strong imports
    std::vector<std::string> weak_imports;  // weak imports may stay unresolved
};

void collect_bundled_closure(const std::string& path, std::set<std::string>* visited,
                             BundledClosure* out) {
    std::error_code ec;
    const std::string abs = fs::absolute(fs::path(path), ec).string();
    if (!visited->insert(abs).second) return;

    std::vector<std::uint8_t> file;
    std::string error;
    if (!read_file_bytes(abs, &file, &error)) return;
    SliceSpan span{};
    if (!select_macho_slice(file, nullptr, &span, &error)) return;
    const std::uint8_t* slice = file.data() + span.offset;
    ParsedMacho parsed;
    if (!parse_macho_slice(slice, span.size, &parsed, &error)) return;

    if (parsed.has_exports_trie &&
        parsed.exports_trie.dataoff + parsed.exports_trie.datasize <= span.size) {
        const std::uint8_t* eb = slice + parsed.exports_trie.dataoff;
        std::unordered_map<std::string, void*> ex;
        parse_exports_trie_node(eb, eb + parsed.exports_trie.datasize, eb, "", 0, &ex);
        for (const auto& kv : ex) out->exports.insert(kv.first);
    } else if (parsed.has_dyld_info && parsed.dyld_info.export_size > 0 &&
               parsed.dyld_info.export_off + parsed.dyld_info.export_size <= span.size) {
        const std::uint8_t* eb = slice + parsed.dyld_info.export_off;
        std::unordered_map<std::string, void*> ex;
        parse_exports_trie_node(eb, eb + parsed.dyld_info.export_size, eb, "", 0, &ex);
        for (const auto& kv : ex) out->exports.insert(kv.first);
    }

    if (parsed.has_chained_fixups) {
        std::vector<ChainedImportEntry> cimps;
        parse_chained_imports(slice, span.size, parsed.chained_fixups, &cimps);
        for (const auto& ci : cimps) {
            if (ci.name.empty()) continue;
            if (ci.weak) out->weak_imports.push_back(ci.name);
            else out->imports.push_back(ci.name);
        }
    }
    if (parsed.has_dyld_info) {
        collect_classic_bind_symbols(slice, span.size, parsed.dyld_info.bind_off,
                                     parsed.dyld_info.bind_size, &out->imports);
        collect_classic_bind_symbols(slice, span.size, parsed.dyld_info.lazy_bind_off,
                                     parsed.dyld_info.lazy_bind_size, &out->imports);
    }

    const std::string main_dir = fs::path(abs).parent_path().string();
    for (const auto& lib : parsed.dylibs) {
        if (is_builtin_darwin_library(lib)) continue;
        std::string resolved;
        if (lib.rfind("@executable_path/", 0) == 0) {
            resolved = (fs::path(main_dir) / lib.substr(17)).string();
        } else if (lib.rfind("@loader_path/", 0) == 0) {
            resolved = (fs::path(main_dir) / lib.substr(13)).string();
        } else if (lib.rfind("@rpath/", 0) == 0) {
            for (const auto& rp : parsed.rpaths) {
                std::string base = rp;
                if (base.rfind("@executable_path", 0) == 0) base = main_dir + base.substr(16);
                else if (base.rfind("@loader_path", 0) == 0) base = main_dir + base.substr(12);
                const std::string candidate = (fs::path(base) / lib.substr(7)).string();
                if (fs::is_regular_file(candidate, ec)) {
                    resolved = candidate;
                    break;
                }
            }
        }
        if (!resolved.empty() && fs::is_regular_file(resolved, ec)) {
            collect_bundled_closure(resolved, visited, out);
        }
    }
}

std::vector<LoadedMachoImage*>& loaded_images() {
    static std::vector<LoadedMachoImage*> images;
    return images;
}

void shim_missing_symbol_trap() {
    std::fprintf(stderr, "finn-darwin: called an unimplemented macOS symbol (aborting cleanly)\n");
    std::exit(78);
}

void* missing_symbol_trap_ptr() {
    void* out = nullptr;
    auto fn = &shim_missing_symbol_trap;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

}  // namespace

MachoLoader::MachoLoader() = default;

MachoLoader::~MachoLoader() {
    unmap_all();
}

void MachoLoader::unmap_all() {
    for (LoadedMachoImage* img : images_) {
        if (img->map_base && img->map_size > 0) {
            ::munmap(img->map_base, img->map_size);
        }
        delete img;
    }
    images_.clear();
    loaded_images().clear();
}

void* find_loaded_symbol_containing(const std::string& substring) {
    for (const LoadedMachoImage* img : loaded_images()) {
        for (const auto& entry : img->symbols) {
            if (entry.first.find(substring) != std::string::npos) return entry.second;
        }
    }
    return nullptr;
}

std::string MachoLoader::resolve_target_binary(const std::string& path,
                                               std::string* bundle_root_out,
                                               std::string* error) {
    std::error_code ec;
    const fs::path p(path);
    if (fs::is_directory(p, ec)) {
        const fs::path plist_path = p / "Contents" / "Info.plist";
        if (!fs::is_regular_file(plist_path, ec)) {
            if (error) *error = "directory is not a macOS .app bundle (missing Contents/Info.plist)";
            return {};
        }
        auto plist = parse_plist_file(plist_path.string(), error);
        if (!plist) return {};
        const std::string exe_name = plist->get_string("CFBundleExecutable");
        if (exe_name.empty()) {
            if (error) *error = "Info.plist missing CFBundleExecutable";
            return {};
        }
        const fs::path exe = p / "Contents" / "MacOS" / exe_name;
        if (!fs::is_regular_file(exe, ec)) {
            if (error) *error = "bundle executable not found: " + exe.string();
            return {};
        }
        if (bundle_root_out) *bundle_root_out = p.string();
        return exe.string();
    }
    if (!fs::is_regular_file(p, ec)) {
        if (error) *error = "target binary does not exist: " + path;
        return {};
    }
    // Check if binary resides inside <App>.app/Contents/MacOS/<bin>
    if (bundle_root_out) {
        const fs::path macos_dir = p.parent_path();
        if (macos_dir.filename() == "MacOS" &&
            macos_dir.parent_path().filename() == "Contents") {
            *bundle_root_out = macos_dir.parent_path().parent_path().string();
        }
    }
    return p.string();
}

void MachoLoader::initialize_runtime(int argc, char** argv, const std::string& target_path) {
    std::string bundle_root;
    std::string resolved = resolve_target_binary(target_path, &bundle_root);
    if (resolved.empty()) resolved = target_path;

    set_darwin_process_args(argc, argv, resolved);
    set_foundation_process_args(argc, argv);
    if (!runtime_initialized_) {
        init_objc_runtime();
        init_foundation(bundle_root);
        init_coregraphics();
        init_appkit(argc, argv);
        register_carbon_classes();
        register_libsystem_symbols(&symbols_);
        register_objc_symbols(&symbols_);
        register_foundation_symbols(&symbols_);
        register_coregraphics_symbols(&symbols_);
        register_appkit_symbols(&symbols_);
        register_swift_runtime_symbols(&symbols_);
        register_compat_symbols(&symbols_);
        register_carbon_symbols(&symbols_);
        finn::darwin::swiftui::register_all_swiftui_symbols(&symbols_);
        runtime_initialized_ = true;
    } else {
        init_foundation(bundle_root);
    }
}

void* MachoLoader::lookup_symbol(const std::string& name, bool allow_stub,
                                 std::vector<std::string>* missing_out) {
    if (auto it = symbols_.find(name); it != symbols_.end()) {
        return it->second;
    }
    for (const LoadedMachoImage* img : images_) {
        if (auto it = img->exports.find(name); it != img->exports.end()) {
            return it->second;
        }
    }
    if (void* dyn = resolve_objc_dynamic_symbol(name)) {
        return dyn;
    }
    if (missing_out) {
        if (std::find(missing_out->begin(), missing_out->end(), name) == missing_out->end()) {
            missing_out->push_back(name);
        }
    }
    return allow_stub ? missing_symbol_trap_ptr() : nullptr;
}

MachoDiagnosis MachoLoader::diagnose(const std::string& binary_or_app_path) {
    MachoDiagnosis diag;
    diag.host_arch = host_arch_name();

    std::string bundle_root;
    const std::string bin = resolve_target_binary(binary_or_app_path, &bundle_root, &diag.error);
    if (bin.empty()) return diag;

    char* fake_argv[] = {const_cast<char*>(bin.c_str()), nullptr};
    initialize_runtime(1, fake_argv, binary_or_app_path);

    std::vector<std::uint8_t> file;
    if (!read_file_bytes(bin, &file, &diag.error)) return diag;

    SliceSpan slice{};
    if (!select_macho_slice(file, &diag.binary_arches, &slice, &diag.error)) return diag;

    diag.selected_arch = cputype_name(slice.cputype);
    diag.arch_compatible = (slice.cputype == host_cputype());

    ParsedMacho parsed;
    if (!parse_macho_slice(file.data() + slice.offset, slice.size, &parsed, &diag.error)) {
        return diag;
    }

    diag.valid = true;
    diag.linked_dylibs = parsed.dylibs;

    // Count ObjC classes declared by the binary
    for (const auto& sec : parsed.sections) {
        if (fixed_cstr(sec.sectname, 16) == "__objc_classlist") {
            diag.objc_class_count = static_cast<std::size_t>(sec.size / sizeof(void*));
        }
    }

    // Collect non-builtin frameworks that are not bundled inside the .app
    const fs::path exec_dir = fs::path(bin).parent_path();
    for (std::size_t i = 0; i < parsed.dylibs.size(); ++i) {
        const std::string& lib = parsed.dylibs[i];
        if (is_builtin_darwin_library(lib)) continue;
        if (lib.rfind("@executable_path/", 0) == 0 || lib.rfind("@loader_path/", 0) == 0 ||
            lib.rfind("@rpath/", 0) == 0) {
            continue;
        }
        const std::string fname = framework_short_name(lib);
        if (std::find(diag.missing_frameworks.begin(), diag.missing_frameworks.end(), fname) ==
            diag.missing_frameworks.end()) {
            diag.missing_frameworks.push_back(fname);
        }
    }
    (void)exec_dir;

    // Collect all imported symbols from chained fixups and/or classic dyld info
    std::vector<std::string> raw_imports;
    std::set<std::string> weak_imports;
    if (parsed.has_chained_fixups) {
        std::vector<ChainedImportEntry> cimps;
        parse_chained_imports(file.data() + slice.offset, slice.size,
                              parsed.chained_fixups, &cimps);
        for (const auto& ci : cimps) {
            if (ci.name.empty()) continue;
            if (ci.weak) weak_imports.insert(ci.name);
            else raw_imports.push_back(ci.name);
        }
    }
    if (parsed.has_dyld_info) {
        collect_classic_bind_symbols(file.data() + slice.offset, slice.size,
                                     parsed.dyld_info.bind_off, parsed.dyld_info.bind_size,
                                     &raw_imports);
        collect_classic_bind_symbols(file.data() + slice.offset, slice.size,
                                     parsed.dyld_info.lazy_bind_off, parsed.dyld_info.lazy_bind_size,
                                     &raw_imports);
        std::vector<std::string> weak_binds;
        collect_classic_bind_symbols(file.data() + slice.offset, slice.size,
                                     parsed.dyld_info.weak_bind_off, parsed.dyld_info.weak_bind_size,
                                     &weak_binds);
        weak_imports.insert(weak_binds.begin(), weak_binds.end());
    }

    // Include the imports of bundled dylibs and treat their exports as
    // resolvable, exactly as load_and_run does when it maps those frameworks.
    std::set<std::string> visited;
    BundledClosure bundled;
    collect_bundled_closure(bin, &visited, &bundled);
    raw_imports.insert(raw_imports.end(), bundled.imports.begin(), bundled.imports.end());
    weak_imports.insert(bundled.weak_imports.begin(), bundled.weak_imports.end());

    std::set<std::string> seen;
    for (const auto& sym : raw_imports) {
        if (!seen.insert(sym).second) continue;
        diag.imported_symbols.push_back(sym);
        if (bundled.exports.count(sym) != 0) continue;
        (void)lookup_symbol(sym, false, &diag.missing_symbols);
    }
    for (const auto& sym : weak_imports) {
        if (!seen.insert(sym).second) continue;
        diag.imported_symbols.push_back(sym);
        // A weak import is allowed to stay unresolved; load_image binds it to
        // null without recording a failure, so diagnose must do the same.
    }

    // Explain every gap we can name. A framework may link cleanly as a dylib
    // name yet still be unimplemented — report it by name rather than silently
    // binding its imports to the missing-symbol trap.
    for (const auto& lib : parsed.dylibs) {
        const std::string short_name = framework_short_name(lib);
        const std::string why = framework_reason(short_name);
        if (why.empty()) continue;
        const auto it = std::find_if(diag.framework_gaps.begin(), diag.framework_gaps.end(),
                                     [&](const MachoDiagnosis::Gap& g) { return g.name == short_name; });
        // A direct LC_LOAD_DYLIB of an unimplemented framework counts as linked.
        if (it == diag.framework_gaps.end()) {
            diag.framework_gaps.push_back({short_name, why});
            diag.framework_gaps.back().linked = true;
        } else {
            it->linked = true;
        }
    }
    for (const auto& name : diag.missing_frameworks) {
        const std::string why = framework_reason(name);
        if (why.empty()) continue;
        const auto it = std::find_if(diag.framework_gaps.begin(), diag.framework_gaps.end(),
                                     [&](const MachoDiagnosis::Gap& g) { return g.name == name; });
        if (it == diag.framework_gaps.end()) diag.framework_gaps.push_back({name, why});
        else it->linked = true;
    }
    for (const auto& sym : diag.missing_symbols) {
        const std::string why = symbol_reason(sym);
        if (why.empty()) continue;
        diag.symbol_gaps.push_back({sym, why});
    }

    diag.runnable = diag.arch_compatible && diag.missing_symbols.empty();
    if (!diag.arch_compatible) {
        diag.summary = "Architecture mismatch: binary is " + diag.selected_arch +
                       ", host is " + diag.host_arch + ".";
    } else if (diag.missing_symbols.empty()) {
        diag.summary = "Ready to execute natively on " + diag.host_arch + " (" +
                       std::to_string(diag.imported_symbols.size()) +
                       " imported symbols resolved by finn-darwin).";
    } else {
        diag.summary = "Missing " + std::to_string(diag.missing_symbols.size()) +
                       " macOS symbol(s) across " +
                       std::to_string(std::max<std::size_t>(1, diag.missing_frameworks.size())) +
                       " unimplemented framework(s) — cannot run cleanly yet.";
        if (!diag.framework_gaps.empty()) {
            diag.summary += " Linked but not implemented:";
            for (const auto& g : diag.framework_gaps) {
                if (g.linked) diag.summary += " " + g.name + ",";
            }
            if (!diag.summary.empty() && diag.summary.back() == ',') diag.summary.pop_back();
            diag.summary += ".";
        }
    }
    return diag;
}

bool MachoLoader::load_image(const std::string& path, bool is_main,
                             bool allow_missing_symbols,
                             std::string* error,
                             LoadedMachoImage** out_img) {
    std::vector<std::uint8_t> file;
    if (!read_file_bytes(path, &file, error)) return false;

    SliceSpan slice_span{};
    if (!select_macho_slice(file, nullptr, &slice_span, error)) return false;
    if (slice_span.cputype != host_cputype()) {
        if (error) {
            *error = "cannot execute " + cputype_name(slice_span.cputype) +
                     " Mach-O binary on " + host_arch_name() + " host";
        }
        return false;
    }

    const std::uint8_t* slice = file.data() + slice_span.offset;
    const std::size_t slice_size = slice_span.size;

    ParsedMacho parsed;
    if (!parse_macho_slice(slice, slice_size, &parsed, error)) return false;

    if (is_main) {
        main_exec_dir_ = fs::path(path).parent_path().string();
        for (const auto& rp : parsed.rpaths) rpaths_.push_back(rp);
    }

    // Load bundled non-system dylibs first so their exports are available for binding
    const fs::path loader_dir = fs::path(path).parent_path();
    for (std::size_t i = 0; i < parsed.dylibs.size(); ++i) {
        const std::string& lib = parsed.dylibs[i];
        if (is_builtin_darwin_library(lib)) continue;

        auto resolve_bundled = [&](const std::string& raw) -> std::string {
            std::error_code ec;
            if (raw.rfind("@executable_path/", 0) == 0) {
                const fs::path p = fs::path(main_exec_dir_) / raw.substr(17);
                if (fs::is_regular_file(p, ec)) return p.string();
            } else if (raw.rfind("@loader_path/", 0) == 0) {
                const fs::path p = loader_dir / raw.substr(13);
                if (fs::is_regular_file(p, ec)) return p.string();
            } else if (raw.rfind("@rpath/", 0) == 0) {
                const std::string tail = raw.substr(7);
                for (const auto& rp : rpaths_) {
                    std::string base = rp;
                    if (base.rfind("@executable_path", 0) == 0) {
                        base = main_exec_dir_ + base.substr(16);
                    } else if (base.rfind("@loader_path", 0) == 0) {
                        base = loader_dir.string() + base.substr(12);
                    }
                    const fs::path p = fs::path(base) / tail;
                    if (fs::is_regular_file(p, ec)) return p.string();
                }
            }
            return {};
        };

        const std::string bundled_path = resolve_bundled(lib);
        if (!bundled_path.empty()) {
            bool already = false;
            for (const auto* existing : images_) {
                if (existing->path == bundled_path) { already = true; break; }
            }
            if (!already) {
                LoadedMachoImage* dep = nullptr;
                if (!load_image(bundled_path, false, allow_missing_symbols, error, &dep)) {
                    return false;
                }
            }
        }
    }

    // Compute virtual address span across all loadable segments (skip __PAGEZERO)
    std::uint64_t min_vm = ~std::uint64_t{0};
    std::uint64_t max_vm = 0;
    for (const auto& seg : parsed.segments) {
        const std::string sname = fixed_cstr(seg.segname, 16);
        if (sname == "__PAGEZERO" || seg.vmsize == 0) continue;
        min_vm = std::min(min_vm, seg.vmaddr);
        max_vm = std::max(max_vm, seg.vmaddr + seg.vmsize);
    }
    if (min_vm >= max_vm) {
        if (error) *error = "Mach-O image has no loadable segments";
        return false;
    }

    const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const std::size_t map_size = align_up(static_cast<std::size_t>(max_vm - min_vm), page);

    void* reserved = nullptr;
#if defined(__APPLE__) && defined(__aarch64__)
    // Map the whole contiguous virtual span with MAP_JIT first (Apple Silicon forbids
    // MAP_JIT + MAP_FIXED), then remap non-executable data segments with plain MAP_FIXED
    // RW memory so __TEXT executes under W^X while __DATA remains writable.
    reserved = ::mmap(nullptr, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
#else
    reserved = ::mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
    if (reserved == MAP_FAILED) {
        if (error) *error = "mmap failed for Mach-O image";
        return false;
    }

    auto* img = new LoadedMachoImage();
    img->path = path;
    img->map_base = static_cast<std::uint8_t*>(reserved);
    img->map_size = map_size;
    img->preferred_base = min_vm;
    img->slide = static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(img->map_base) - min_vm);
    img->entry_offset = parsed.entry_offset;

    // Copy each segment into its exact relative VM offset
    for (const auto& seg : parsed.segments) {
        const std::string sname = fixed_cstr(seg.segname, 16);
        if (sname == "__PAGEZERO" || seg.vmsize == 0) continue;
        const std::size_t rel = static_cast<std::size_t>(seg.vmaddr - min_vm);
        std::uint8_t* dst = img->map_base + rel;

#if defined(__APPLE__) && defined(__aarch64__)
        const std::size_t seg_span = align_up(static_cast<std::size_t>(seg.vmsize), page);
        const bool is_exec = (seg.initprot & kVmProtExecute) != 0;
        if (!is_exec) {
            if (::mmap(dst, seg_span, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) {
                delete img;
                ::munmap(reserved, map_size);
                if (error) *error = "MAP_FIXED failed for data segment " + sname;
                return false;
            }
        } else {
            ::pthread_jit_write_protect_np(0);
        }
#endif
        if (seg.filesize > 0 && seg.fileoff + seg.filesize <= slice_size) {
            std::memcpy(dst, slice + seg.fileoff, static_cast<std::size_t>(seg.filesize));
        }
#if defined(__APPLE__) && defined(__aarch64__)
        if (is_exec) ::pthread_jit_write_protect_np(1);
#endif
    }

    // Parse exports trie for dylib symbol resolution
    if (parsed.has_exports_trie &&
        parsed.exports_trie.dataoff + parsed.exports_trie.datasize <= slice_size) {
        const std::uint8_t* eb = slice + parsed.exports_trie.dataoff;
        parse_exports_trie_node(eb, eb + parsed.exports_trie.datasize, eb, "",
                                reinterpret_cast<std::uintptr_t>(img->map_base),
                                &img->exports);
    } else if (parsed.has_dyld_info && parsed.dyld_info.export_size > 0 &&
               parsed.dyld_info.export_off + parsed.dyld_info.export_size <= slice_size) {
        const std::uint8_t* eb = slice + parsed.dyld_info.export_off;
        parse_exports_trie_node(eb, eb + parsed.dyld_info.export_size, eb, "",
                                reinterpret_cast<std::uintptr_t>(img->map_base),
                                &img->exports);
    }

    // Apply modern LC_DYLD_CHAINED_FIXUPS (rebases + symbol binds)
    std::vector<std::string> missing_syms;
    if (parsed.has_chained_fixups &&
        parsed.chained_fixups.dataoff + sizeof(DyldChainedFixupsHeader) <= slice_size) {
        std::vector<ChainedImportEntry> imports;
        parse_chained_imports(slice, slice_size, parsed.chained_fixups, &imports);

        std::vector<std::uintptr_t> resolved_imports(imports.size(), 0);
        for (std::size_t i = 0; i < imports.size(); ++i) {
            void* addr = lookup_symbol(imports[i].name,
                                       allow_missing_symbols || imports[i].weak,
                                       imports[i].weak ? nullptr : &missing_syms);
            resolved_imports[i] =
                reinterpret_cast<std::uintptr_t>(addr) +
                static_cast<std::uintptr_t>(imports[i].addend);
        }

        const std::uint8_t* fbase = slice + parsed.chained_fixups.dataoff;
        const std::size_t flim =
            std::min<std::size_t>(parsed.chained_fixups.datasize,
                                  slice_size - parsed.chained_fixups.dataoff);
        DyldChainedFixupsHeader hdr{};
        std::memcpy(&hdr, fbase, sizeof(hdr));

        if (hdr.starts_offset + 4 <= flim) {
            const std::uint8_t* starts_img = fbase + hdr.starts_offset;
            std::uint32_t seg_count = 0;
            std::memcpy(&seg_count, starts_img, 4);

            auto walk_chain = [&](std::uint8_t* ptr, std::uint16_t fmt) {
                for (int guard = 0; guard < 1000000; ++guard) {
                    if (ptr < img->map_base || ptr + 8 > img->map_base + img->map_size) break;
                    std::uint64_t raw = 0;
                    std::memcpy(&raw, ptr, 8);
                    std::uint64_t next = 0;
                    std::size_t stride = 4;
                    std::uint64_t patched = 0;

                    if (fmt == kDyldChainedPtr64 || fmt == kDyldChainedPtr64Offset) {
                        const bool is_bind = ((raw >> 63) & 1u) != 0;
                        next = (raw >> 51) & 0xfffu;
                        stride = 4;
                        if (is_bind) {
                            const std::uint32_t ord = static_cast<std::uint32_t>(raw & 0xffffffu);
                            const std::int8_t add = static_cast<std::int8_t>((raw >> 24) & 0xffu);
                            patched = (ord < resolved_imports.size())
                                          ? static_cast<std::uint64_t>(resolved_imports[ord] + add)
                                          : 0;
                        } else {
                            const std::uint64_t target = raw & 0xfffffffffull;
                            if (fmt == kDyldChainedPtr64Offset) {
                                patched = reinterpret_cast<std::uintptr_t>(img->map_base) + target;
                            } else {
                                patched = static_cast<std::uint64_t>(
                                    static_cast<std::int64_t>(target) + img->slide);
                            }
                        }
                    } else if (fmt == kDyldChainedPtrArm64e ||
                               fmt == kDyldChainedPtrArm64eUserland ||
                               fmt == kDyldChainedPtrArm64eUserland24) {
                        const bool is_auth = ((raw >> 63) & 1u) != 0;
                        const bool is_bind = ((raw >> 62) & 1u) != 0;
                        next = (raw >> 51) & 0x7ffu;
                        stride = (fmt == kDyldChainedPtrArm64e) ? 8 : 4;
                        if (is_bind) {
                            const std::uint32_t ord =
                                (fmt == kDyldChainedPtrArm64eUserland24)
                                    ? static_cast<std::uint32_t>(raw & 0xffffffu)
                                    : static_cast<std::uint32_t>(raw & 0xffffu);
                            patched = (ord < resolved_imports.size()) ? resolved_imports[ord] : 0;
                        } else if (is_auth) {
                            const std::uint64_t target = raw & 0xffffffffu;
                            patched = reinterpret_cast<std::uintptr_t>(img->map_base) + target;
                        } else {
                            const std::uint64_t target = raw & 0x7ffffffffffull;
                            if (fmt == kDyldChainedPtrArm64e) {
                                patched = static_cast<std::uint64_t>(
                                    static_cast<std::int64_t>(target) + img->slide);
                            } else {
                                patched = reinterpret_cast<std::uintptr_t>(img->map_base) + target;
                            }
                        }
                    } else {
                        break;
                    }

                    std::memcpy(ptr, &patched, 8);
                    if (next == 0) break;
                    ptr += next * stride;
                }
            };

            for (std::uint32_t s = 0; s < seg_count; ++s) {
                std::uint32_t seg_off = 0;
                if (hdr.starts_offset + 4 + (s + 1) * 4 > flim) break;
                std::memcpy(&seg_off, starts_img + 4 + s * 4, 4);
                if (seg_off == 0) continue;
                const std::uint8_t* seg_info_p = starts_img + seg_off;
                if (seg_info_p + sizeof(DyldChainedStartsInSegment) > fbase + flim) continue;
                DyldChainedStartsInSegment si{};
                std::memcpy(&si, seg_info_p, sizeof(si));
                const auto* page_starts = reinterpret_cast<const std::uint16_t*>(
                    seg_info_p + offsetof(DyldChainedStartsInSegment, page_start));
                for (std::uint16_t p = 0; p < si.page_count; ++p) {
                    const std::uint16_t ps = page_starts[p];
                    if (ps == kDyldChainedPtrStartNone) continue;
                    std::uint8_t* page_base =
                        img->map_base + si.segment_offset + static_cast<std::size_t>(p) * si.page_size;
                    if ((ps & kDyldChainedPtrStartMulti) != 0) {
                        std::size_t idx = ps & 0x7fffu;
                        while (true) {
                            const std::uint16_t entry = page_starts[si.page_count + idx];
                            walk_chain(page_base + (entry & 0x7fffu), si.pointer_format);
                            if ((entry & 0x8000u) != 0) break;
                            ++idx;
                        }
                    } else {
                        walk_chain(page_base + ps, si.pointer_format);
                    }
                }
            }
        }
    }

    // Apply classic LC_DYLD_INFO_ONLY rebases and binds if present
    if (parsed.has_dyld_info) {
        auto apply_rebases = [&](std::uint32_t off, std::uint32_t sz) {
            if (off == 0 || sz == 0 || off + sz > slice_size) return;
            const std::uint8_t* p = slice + off;
            const std::uint8_t* end = p + sz;
            std::size_t seg_idx = 0;
            std::uint64_t seg_off = 0;
            auto do_one = [&]() {
                if (seg_idx >= parsed.segments.size()) return;
                const std::uint64_t vm = parsed.segments[seg_idx].vmaddr + seg_off;
                std::uint8_t* slot = img->map_base + (vm - min_vm);
                if (slot >= img->map_base && slot + 8 <= img->map_base + img->map_size) {
                    std::uint64_t v = 0;
                    std::memcpy(&v, slot, 8);
                    v = static_cast<std::uint64_t>(static_cast<std::int64_t>(v) + img->slide);
                    std::memcpy(slot, &v, 8);
                }
            };
            while (p < end) {
                const std::uint8_t b = *p++;
                const std::uint8_t op = b & kRebaseOpcodeMask;
                const std::uint8_t imm = b & kRebaseImmediateMask;
                switch (op) {
                    case kRebaseOpcodeDone: return;
                    case kRebaseOpcodeSetTypeImm: break;
                    case kRebaseOpcodeSetSegmentAndOffsetUleb:
                        seg_idx = imm;
                        seg_off = read_uleb128(&p, end);
                        break;
                    case kRebaseOpcodeAddAddrUleb:
                        seg_off += read_uleb128(&p, end);
                        break;
                    case kRebaseOpcodeAddAddrImmScaled:
                        seg_off += static_cast<std::uint64_t>(imm) * 8;
                        break;
                    case kRebaseOpcodeDoRebaseImmTimes:
                        for (int i = 0; i < imm; ++i) { do_one(); seg_off += 8; }
                        break;
                    case kRebaseOpcodeDoRebaseUlebTimes: {
                        const std::uint64_t cnt = read_uleb128(&p, end);
                        for (std::uint64_t i = 0; i < cnt; ++i) { do_one(); seg_off += 8; }
                        break;
                    }
                    case kRebaseOpcodeDoRebaseAddAddrUleb:
                        do_one();
                        seg_off += 8 + read_uleb128(&p, end);
                        break;
                    case kRebaseOpcodeDoRebaseUlebTimesSkippingUleb: {
                        const std::uint64_t cnt = read_uleb128(&p, end);
                        const std::uint64_t skip = read_uleb128(&p, end);
                        for (std::uint64_t i = 0; i < cnt; ++i) { do_one(); seg_off += 8 + skip; }
                        break;
                    }
                    default: return;
                }
            }
        };

        auto apply_binds = [&](std::uint32_t off, std::uint32_t sz, bool is_weak) {
            if (off == 0 || sz == 0 || off + sz > slice_size) return;
            const std::uint8_t* p = slice + off;
            const std::uint8_t* end = p + sz;
            std::size_t seg_idx = 0;
            std::uint64_t seg_off = 0;
            std::int64_t addend = 0;
            std::string sym_name;
            auto do_bind = [&]() {
                if (seg_idx >= parsed.segments.size() || sym_name.empty()) return;
                const std::uint64_t vm = parsed.segments[seg_idx].vmaddr + seg_off;
                std::uint8_t* slot = img->map_base + (vm - min_vm);
                if (slot >= img->map_base && slot + 8 <= img->map_base + img->map_size) {
                    void* addr = lookup_symbol(sym_name, allow_missing_symbols || is_weak,
                                               is_weak ? nullptr : &missing_syms);
                    const std::uint64_t v =
                        reinterpret_cast<std::uintptr_t>(addr) + static_cast<std::uint64_t>(addend);
                    std::memcpy(slot, &v, 8);
                }
            };
            while (p < end) {
                const std::uint8_t b = *p++;
                const std::uint8_t op = b & kBindOpcodeMask;
                const std::uint8_t imm = b & kBindImmediateMask;
                switch (op) {
                    case kBindOpcodeDone: break;
                    case kBindOpcodeSetDylibOrdinalImm:
                    case kBindOpcodeSetDylibSpecialImm:
                    case kBindOpcodeSetTypeImm:
                        break;
                    case kBindOpcodeSetDylibOrdinalUleb:
                        (void)read_uleb128(&p, end);
                        break;
                    case kBindOpcodeSetSymbolTrailingFlagsImm: {
                        const char* s = reinterpret_cast<const char*>(p);
                        while (p < end && *p != 0) ++p;
                        if (p < end) ++p;
                        sym_name = s;
                        break;
                    }
                    case kBindOpcodeSetAddendSleb:
                        addend = read_sleb128(&p, end);
                        break;
                    case kBindOpcodeSetSegmentAndOffsetUleb:
                        seg_idx = imm;
                        seg_off = read_uleb128(&p, end);
                        break;
                    case kBindOpcodeAddAddrUleb:
                        seg_off += read_uleb128(&p, end);
                        break;
                    case kBindOpcodeDoBind:
                        do_bind();
                        seg_off += 8;
                        break;
                    case kBindOpcodeDoBindAddAddrUleb:
                        do_bind();
                        seg_off += 8 + read_uleb128(&p, end);
                        break;
                    case kBindOpcodeDoBindAddAddrImmScaled:
                        do_bind();
                        seg_off += 8 + static_cast<std::uint64_t>(imm) * 8;
                        break;
                    case kBindOpcodeDoBindUlebTimesSkippingUleb: {
                        const std::uint64_t cnt = read_uleb128(&p, end);
                        const std::uint64_t skip = read_uleb128(&p, end);
                        for (std::uint64_t i = 0; i < cnt; ++i) { do_bind(); seg_off += 8 + skip; }
                        break;
                    }
                    default: return;
                }
            }
        };

        apply_rebases(parsed.dyld_info.rebase_off, parsed.dyld_info.rebase_size);
        apply_binds(parsed.dyld_info.bind_off, parsed.dyld_info.bind_size, false);
        apply_binds(parsed.dyld_info.lazy_bind_off, parsed.dyld_info.lazy_bind_size, false);
        apply_binds(parsed.dyld_info.weak_bind_off, parsed.dyld_info.weak_bind_size, true);
    }

    if (!missing_syms.empty() && !allow_missing_symbols) {
        if (error) {
            // Name every unresolved import. A six-symbol sample hid the real
            // ABI gap and made the diagnosis different from `diagnose`.
            *error = "cannot launch: unresolved macOS symbols (" +
                     std::to_string(missing_syms.size()) + "): ";
            for (std::size_t i = 0; i < missing_syms.size(); ++i) {
                if (i > 0) *error += ", ";
                *error += missing_syms[i];
                const std::string why = symbol_reason(missing_syms[i]);
                if (!why.empty()) *error += " [" + why + "]";
            }
        }
        ::munmap(img->map_base, img->map_size);
        delete img;
        return false;
    }

    // Collect ObjC sections and C++/__mod_init_func initializers
    ObjcImageSections objc_secs{};
    objc_secs.image_slide = static_cast<std::uintptr_t>(img->slide);
    for (const auto& sec : parsed.sections) {
        const std::string sname = fixed_cstr(sec.sectname, 16);
        if (sec.addr < min_vm || sec.size == 0) continue;
        std::uint8_t* ptr = img->map_base + (sec.addr - min_vm);
        const std::size_t len = static_cast<std::size_t>(sec.size);

        if (sname == "__objc_selrefs") objc_secs.selrefs = {ptr, len};
        else if (sname == "__objc_classlist") objc_secs.classlist = {ptr, len};
        else if (sname == "__objc_classrefs") objc_secs.classrefs = {ptr, len};
        else if (sname == "__objc_superrefs") objc_secs.superrefs = {ptr, len};
        else if (sname == "__objc_catlist") objc_secs.catlist = {ptr, len};
        else if (sname == "__cfstring") objc_secs.cfstring = {ptr, len};

        const std::uint32_t stype = sec.flags & kSectionTypeMask;
        if (stype == kSModInitFuncPointers) {
            const std::size_t cnt = len / sizeof(std::uint64_t);
            const auto* addrs = reinterpret_cast<const std::uint64_t*>(ptr);
            for (std::size_t i = 0; i < cnt; ++i) {
                if (addrs[i] != 0) img->mod_init_addrs.push_back(addrs[i]);
            }
        } else if (stype == kSInitFuncOffsets) {
            const std::size_t cnt = len / sizeof(std::uint32_t);
            const auto* offs = reinterpret_cast<const std::uint32_t*>(ptr);
            for (std::size_t i = 0; i < cnt; ++i) {
                if (offs[i] != 0) {
                    img->mod_init_addrs.push_back(
                        reinterpret_cast<std::uintptr_t>(img->map_base) + offs[i]);
                }
            }
        }
    }

    // Realize Objective-C classes, selectors, categories, and __cfstring literals
    realize_objc_sections(objc_secs, symbols_);

    // Apply final segment page protections and flush instruction cache
    for (const auto& seg : parsed.segments) {
        const std::string sname = fixed_cstr(seg.segname, 16);
        if (sname == "__PAGEZERO" || seg.vmsize == 0) continue;
        const std::size_t rel = align_down(static_cast<std::size_t>(seg.vmaddr - min_vm), page);
        const std::size_t span = align_up(static_cast<std::size_t>(seg.vmsize), page);
        std::uint8_t* addr = img->map_base + rel;

#if defined(__APPLE__) && defined(__aarch64__)
        if ((seg.initprot & kVmProtExecute) != 0) {
            ::sys_icache_invalidate(addr, span);
        }
#else
        int prot = 0;
        if (seg.initprot & kVmProtRead) prot |= PROT_READ;
        if (seg.initprot & kVmProtWrite) prot |= PROT_WRITE;
        if (seg.initprot & kVmProtExecute) prot |= PROT_EXEC;
        (void)::mprotect(addr, span, prot);
        if (prot & PROT_EXEC) {
            __builtin___clear_cache(reinterpret_cast<char*>(addr),
                                    reinterpret_cast<char*>(addr + span));
        }
#endif
    }

    // Index the full symbol table so compat runtimes can locate compiler-
    // generated entry points (Swift `body` getters) by mangled-name fragment.
    if (parsed.has_symtab && parsed.symtab.nsyms > 0 &&
        parsed.symtab.symoff + static_cast<std::uint64_t>(parsed.symtab.nsyms) * sizeof(Nlist64) <=
            slice_size &&
        parsed.symtab.stroff + parsed.symtab.strsize <= slice_size) {
        const auto* nlist = reinterpret_cast<const Nlist64*>(slice + parsed.symtab.symoff);
        const char* strtab = reinterpret_cast<const char*>(slice + parsed.symtab.stroff);
        for (std::uint32_t i = 0; i < parsed.symtab.nsyms; ++i) {
            if (nlist[i].n_strx >= parsed.symtab.strsize) continue;
            const char* name = strtab + nlist[i].n_strx;
            if (!*name) continue;
            const std::uint64_t value = nlist[i].n_value;
            if (value >= min_vm && value < max_vm) {
                img->symbols[name] = img->map_base + (value - min_vm);
            }
        }
    }

    images_.push_back(img);
    loaded_images().push_back(img);
    if (out_img) *out_img = img;
    return true;
}

int MachoLoader::load_and_run(const std::string& binary_or_app_path,
                              int argc, char** argv,
                              std::string* error,
                              bool allow_missing_symbols) {
    std::string bundle_root;
    const std::string bin = resolve_target_binary(binary_or_app_path, &bundle_root, error);
    if (bin.empty()) return 1;

    initialize_runtime(argc, argv, binary_or_app_path);

    LoadedMachoImage* main_img = nullptr;
    if (!load_image(bin, true, allow_missing_symbols, error, &main_img)) {
        return 1;
    }

    if (main_img->entry_offset == 0) {
        if (error) *error = "Mach-O image missing LC_MAIN entry point";
        return 1;
    }

    // Run static constructors (__mod_init_func / __init_offsets) across loaded images
    using InitFn = void (*)(int, char**, char**, char**);
    char* empty_env[] = {nullptr};
    char* apple_vec[] = {const_cast<char*>(bin.c_str()), nullptr};

    for (const LoadedMachoImage* img : images_) {
        for (std::uint64_t addr : img->mod_init_addrs) {
            auto fn = reinterpret_cast<InitFn>(static_cast<std::uintptr_t>(addr));
            fn(argc, argv, empty_env, apple_vec);
        }
    }

    void* pool = finn_objc_autoreleasePoolPush();
    using MainFn = int (*)(int, char**, char**, char**);
    auto entry = reinterpret_cast<MainFn>(main_img->map_base + main_img->entry_offset);
    const int rc = entry(argc, argv, empty_env, apple_vec);
    finn_objc_autoreleasePoolPop(pool);
    return rc;
}

}  // namespace finn::darwin
