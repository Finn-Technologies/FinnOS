#include "darwin/objc_runtime.hpp"

#include "darwin/compat_symbols.hpp"

#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace finn::darwin {
namespace {

// Refcount header stored immediately before every heap-allocated ObjcObject
// created by finn_objc_alloc_object. Magic lets us safely distinguish heap
// objects from static Mach-O __cfstring literals or Class objects.
inline constexpr std::uint64_t kObjcHeapMagic = 0x46494e4e4f424a31ull;  // "FINNOBJ1"

struct ObjcHeapHeader {
    std::uint64_t magic;
    std::atomic<std::int64_t> refcount;
};

struct RuntimeState {
    std::mutex mu;
    bool initialized = false;
    std::unordered_map<std::string, const char*> selectors;
    std::unordered_map<std::string, ObjcClass*> classes;
    std::unordered_map<void*, ObjcClass*> mach_class_map;
    std::unordered_map<std::string, void*> msgsend_stubs;
    std::vector<ObjcClass*> owned_classes;
    std::vector<std::string*> owned_sel_strings;
    ObjcClass* cfstring_class = nullptr;

    // Executable stub pool for _objc_msgSend$<selector> trampolines
    std::uint8_t* stub_page = nullptr;
    std::size_t stub_used = 0;
    std::size_t stub_cap = 0;
};

RuntimeState& rt() {
    static RuntimeState state;
    return state;
}

thread_local std::vector<std::vector<ObjcId>> g_autorelease_pools;

static bool guest_pointer_is_mapped(std::uintptr_t value);

ObjcHeapHeader* heap_header_of(ObjcId obj) {
    if (!obj) return nullptr;
    const auto addr = reinterpret_cast<std::uintptr_t>(obj);
    // Guest code may release a pointer that is not one of our heap objects.
    // Validate both the object and its header page before reading the magic.
    if (!guest_pointer_is_mapped(addr) ||
        !guest_pointer_is_mapped(addr - sizeof(ObjcHeapHeader))) {
        return nullptr;
    }
    auto* hdr = reinterpret_cast<ObjcHeapHeader*>(
        reinterpret_cast<std::uint8_t*>(obj) - sizeof(ObjcHeapHeader));
    if (hdr->magic != kObjcHeapMagic) return nullptr;
    return hdr;
}

// Darwin Mach-O ObjC ABI v2 structs (64-bit)
struct MachObjcClass {
    std::uintptr_t isa;
    std::uintptr_t superclass;
    std::uintptr_t cache_buckets;
    std::uintptr_t cache_mask;
    std::uintptr_t data_bits;  // Low 3 bits are flags; pointer is class_ro_t*
};

struct MachObjcClassRo {
    std::uint32_t flags;
    std::uint32_t instance_start;
    std::uint32_t instance_size;
    std::uint32_t reserved;
    std::uintptr_t ivar_layout;
    const char* name;
    std::uintptr_t base_methods;
    std::uintptr_t base_protocols;
    std::uintptr_t ivars;
    std::uintptr_t weak_ivar_layout;
    std::uintptr_t base_properties;
};

struct MachObjcMethodList {
    std::uint32_t entsize_and_flags;
    std::uint32_t count;
};

struct MachObjcMethod64 {
    const char* name;
    const char* types;
    ObjcImp imp;
};

struct MachObjcRelativeMethod {
    std::int32_t name_offset;
    std::int32_t types_offset;
    std::int32_t imp_offset;
};

struct MachObjcCategory {
    const char* name;
    std::uintptr_t cls;
    std::uintptr_t instance_methods;
    std::uintptr_t class_methods;
    std::uintptr_t protocols;
    std::uintptr_t properties;
};

void import_mach_method_list(ObjcClass* target_cls, std::uintptr_t list_addr) {
    if (!target_cls || list_addr == 0) return;
    const auto* list = reinterpret_cast<const MachObjcMethodList*>(list_addr);
    const std::uint32_t flags = list->entsize_and_flags;
    const bool is_relative = (flags & 0x80000000u) != 0;
    const bool is_direct_sel = (flags & 0x40000000u) != 0;
    const std::uint32_t entsize = flags & 0x0000ffffu;
    const auto* base = reinterpret_cast<const std::uint8_t*>(list + 1);

    for (std::uint32_t i = 0; i < list->count; ++i) {
        const std::uint8_t* elem = base + static_cast<std::size_t>(i) * entsize;
        if (is_relative) {
            const auto* rm = reinterpret_cast<const MachObjcRelativeMethod*>(elem);
            const std::uintptr_t name_field_addr = reinterpret_cast<std::uintptr_t>(&rm->name_offset);
            const std::uintptr_t imp_field_addr = reinterpret_cast<std::uintptr_t>(&rm->imp_offset);
            const std::uintptr_t name_target =
                static_cast<std::uintptr_t>(static_cast<std::intptr_t>(name_field_addr) + rm->name_offset);
            const char* sel_str = nullptr;
            if (is_direct_sel) {
                sel_str = reinterpret_cast<const char*>(name_target);
            } else {
                // Indirect relative method list points to a selref pointer (const char**)
                sel_str = *reinterpret_cast<const char* const*>(name_target);
            }
            const std::uintptr_t imp_target =
                static_cast<std::uintptr_t>(static_cast<std::intptr_t>(imp_field_addr) + rm->imp_offset);
            if (sel_str && imp_target) {
                finn_class_addMethod(target_cls, finn_sel_registerName(sel_str),
                                     reinterpret_cast<ObjcImp>(imp_target));
            }
        } else {
            const auto* m = reinterpret_cast<const MachObjcMethod64*>(elem);
            if (m->name && m->imp) {
                finn_class_addMethod(target_cls, finn_sel_registerName(m->name), m->imp,
                                     m->types ? m->types : "");
            }
        }
    }
}

// A guest Mach-O's ObjC sections can contain pointers whose target was not
// mapped (e.g. a class reference into a weak-framework dylib we don't load).
// Never dereference such a pointer: validate that its page is mapped first.
static bool guest_pointer_is_mapped(std::uintptr_t value) {
    if (value == 0) return false;
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return false;
    const std::uintptr_t page = value & ~static_cast<std::uintptr_t>(page_size - 1);
    return ::msync(reinterpret_cast<void*>(page),
                   static_cast<std::size_t>(page_size), MS_ASYNC) == 0;
}

ObjcClass* lookup_or_realize_mach_class(std::uintptr_t raw_ptr) {
    if (raw_ptr == 0 || !guest_pointer_is_mapped(raw_ptr)) return nullptr;
    auto& state = rt();

    // Already a FinnOS ObjcClass*?
    for (ObjcClass* c : state.owned_classes) {
        if (reinterpret_cast<std::uintptr_t>(c) == raw_ptr) return c;
    }
    if (auto it = state.mach_class_map.find(reinterpret_cast<void*>(raw_ptr));
        it != state.mach_class_map.end()) {
        return it->second;
    }

    const auto* mc = reinterpret_cast<const MachObjcClass*>(raw_ptr);
    const std::uintptr_t ro_ptr = mc->data_bits & ~static_cast<std::uintptr_t>(0x7u);
    if (ro_ptr == 0 || !guest_pointer_is_mapped(ro_ptr)) return nullptr;
    const auto* ro = reinterpret_cast<const MachObjcClassRo*>(ro_ptr);
    const char* cname = (ro->name != 0 &&
                         guest_pointer_is_mapped(reinterpret_cast<std::uintptr_t>(ro->name)))
                            ? reinterpret_cast<const char*>(ro->name)
                            : "UnnamedMachClass";

    ObjcClass* super_cls = nullptr;
    if (mc->superclass != 0) {
        super_cls = lookup_or_realize_mach_class(mc->superclass);
    }
    if (!super_cls && std::strcmp(cname, "NSObject") != 0) {
        super_cls = finn_objc_getClass("NSObject");
    }

    ObjcClass* cls = finn_objc_allocateClassPair(super_cls, cname, 0);
    cls->instance_size = std::max<std::size_t>(sizeof(ObjcObject), ro->instance_size);
    cls->mach_class_ptr = reinterpret_cast<void*>(raw_ptr);
    state.mach_class_map[reinterpret_cast<void*>(raw_ptr)] = cls;

    import_mach_method_list(cls, ro->base_methods);

    if (mc->isa != 0) {
        const auto* meta_mc = reinterpret_cast<const MachObjcClass*>(mc->isa);
        const std::uintptr_t meta_ro_ptr =
            meta_mc->data_bits & ~static_cast<std::uintptr_t>(0x7u);
        if (meta_ro_ptr != 0) {
            const auto* meta_ro = reinterpret_cast<const MachObjcClassRo*>(meta_ro_ptr);
            import_mach_method_list(cls->isa, meta_ro->base_methods);
        }
    }

    finn_objc_registerClassPair(cls);
    return cls;
}

// --- Blocks runtime shims ---
int g_concrete_global_block_isa = 0;
int g_concrete_stack_block_isa = 0;
int g_concrete_malloc_block_isa = 0;
std::uintptr_t g_objc_empty_cache[4] = {};
std::uintptr_t g_objc_empty_vtable[4] = {};

void* shim_block_copy(const void* aBlock) {
    if (!aBlock) return nullptr;
    const auto* blk = reinterpret_cast<const DarwinBlockLayout*>(aBlock);
    if (blk->isa == &g_concrete_global_block_isa ||
        blk->isa == &g_concrete_malloc_block_isa) {
        return const_cast<void*>(aBlock);
    }
    // Copy stack block to heap (descriptor->size is at offset 8 of descriptor)
    std::size_t size = sizeof(DarwinBlockLayout);
    if (blk->descriptor) {
        const auto* desc = reinterpret_cast<const std::uintptr_t*>(blk->descriptor);
        if (desc[1] >= sizeof(DarwinBlockLayout) && desc[1] < 65536) {
            size = static_cast<std::size_t>(desc[1]);
        }
    }
    auto* copy = reinterpret_cast<DarwinBlockLayout*>(std::malloc(size));
    if (!copy) return nullptr;
    std::memcpy(copy, blk, size);
    copy->isa = &g_concrete_malloc_block_isa;
    return copy;
}

void shim_block_release(const void*) {}
void shim_block_object_assign(void* dst, const void* src, int) {
    if (dst) *reinterpret_cast<const void**>(dst) = src;
}
void shim_block_object_dispose(const void*, int) {}

ObjcClass* resolve_class_ptr(void* raw) {
    if (!raw) return nullptr;
    auto& state = rt();
    if (auto it = state.mach_class_map.find(raw); it != state.mach_class_map.end()) {
        return it->second;
    }
    return reinterpret_cast<ObjcClass*>(raw);
}

// --- ObjC C ABI Shims ---
ObjcId shim_objc_alloc(ObjcClass* cls) {
    cls = resolve_class_ptr(cls);
    if (!cls) return nullptr;
    return objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "alloc");
}

ObjcId shim_objc_alloc_init(ObjcClass* cls) {
    ObjcId obj = shim_objc_alloc(cls);
    if (!obj) return nullptr;
    return objc_send_name<ObjcId>(obj, "init");
}

ObjcId shim_objc_allocWithZone(ObjcClass* cls) {
    return finn_objc_alloc_object(resolve_class_ptr(cls));
}

ObjcId shim_objc_opt_new(ObjcClass* cls) {
    return shim_objc_alloc_init(cls);
}

ObjcId shim_objc_opt_self(ObjcId obj) {
    return obj;
}

ObjcClass* shim_objc_opt_class(ObjcId obj) {
    return finn_object_getClass(obj);
}

int shim_objc_opt_isKindOfClass(ObjcId obj, ObjcClass* cls) {
    return finn_objc_isKindOfClass(obj, cls) ? 1 : 0;
}

int shim_objc_opt_respondsToSelector(ObjcId obj, ObjcSel sel) {
    return finn_objc_respondsToSelector(obj, sel) ? 1 : 0;
}

void shim_objc_storeStrong(ObjcId* location, ObjcId obj) {
    if (!location) return;
    ObjcId prev = *location;
    if (prev == obj) return;
    finn_objc_retain(obj);
    *location = obj;
    finn_objc_release(prev);
}

ObjcId shim_objc_initWeak(ObjcId* location, ObjcId obj) {
    if (location) *location = obj;
    return obj;
}

ObjcId shim_objc_storeWeak(ObjcId* location, ObjcId obj) {
    if (location) *location = obj;
    return obj;
}

ObjcId shim_objc_loadWeakRetained(ObjcId* location) {
    if (!location || !*location) return nullptr;
    return finn_objc_retain(*location);
}

void shim_objc_destroyWeak(ObjcId* location) {
    if (location) *location = nullptr;
}

ObjcId shim_objc_retainAutorelease(ObjcId obj) {
    return finn_objc_autorelease(finn_objc_retain(obj));
}

ObjcId shim_objc_retainAutoreleaseReturnValue(ObjcId obj) {
    return finn_objc_autorelease(finn_objc_retain(obj));
}

ObjcId shim_objc_autoreleaseReturnValue(ObjcId obj) {
    return finn_objc_autorelease(obj);
}

ObjcId shim_objc_retainAutoreleasedReturnValue(ObjcId obj) {
    return finn_objc_retain(obj);
}

void shim_objc_enumerationMutation(ObjcId) {
    std::fprintf(stderr, "finn-darwin: Objective-C collection mutated during enumeration\n");
}

const char* shim_class_getName(ObjcClass* cls) {
    return cls ? cls->name.c_str() : "nil";
}

ObjcClass* shim_class_getSuperclass(ObjcClass* cls) {
    return cls ? cls->superclass : nullptr;
}

ObjcId shim_unimplemented_sel(ObjcId self, ObjcSel sel) {
    const char* cls_name = (self && self->isa) ? self->isa->name.c_str() : "nil";
    std::fprintf(stderr, "finn-darwin: unimplemented selector [%s %s]\n",
                 cls_name, sel ? sel : "<null>");
    return nullptr;
}

}  // namespace

extern "C" {

ObjcImp finn_objc_lookup_imp(ObjcId self, ObjcSel sel) {
    if (!self || !sel) return nullptr;
    // Handle direct class messages sent to a raw Mach-O class_t pointer
    ObjcClass* cls = nullptr;
    auto& state = rt();
    if (auto it = state.mach_class_map.find(self); it != state.mach_class_map.end()) {
        cls = it->second->isa;
    } else {
        cls = self->isa;
    }
    if (!cls) return nullptr;
    // Fast pointer lookup first (selectors are interned), with strcmp fallback
    for (ObjcClass* cur = cls; cur != nullptr; cur = cur->superclass) {
        if (auto it = cur->methods.find(sel); it != cur->methods.end()) {
            return it->second;
        }
        for (const auto& [k, imp] : cur->methods) {
            if (std::strcmp(k, sel) == 0) return imp;
        }
    }
    return reinterpret_cast<ObjcImp>(&shim_unimplemented_sel);
}

ObjcImp finn_objc_lookup_super_imp(ObjcSuper* super_info, ObjcSel sel) {
    if (!super_info || !super_info->receiver || !sel) return nullptr;
    ObjcClass* cls = super_info->current_class ? super_info->current_class->superclass : nullptr;
    for (ObjcClass* cur = cls; cur != nullptr; cur = cur->superclass) {
        if (auto it = cur->methods.find(sel); it != cur->methods.end()) {
            return it->second;
        }
        for (const auto& [k, imp] : cur->methods) {
            if (std::strcmp(k, sel) == 0) return imp;
        }
    }
    return reinterpret_cast<ObjcImp>(&shim_unimplemented_sel);
}

}  // extern "C"

void init_objc_runtime() {
    auto& state = rt();
    std::lock_guard<std::mutex> lk(state.mu);
    state.initialized = true;
}

ObjcSel finn_sel_registerName(const char* name) {
    if (!name) return nullptr;
    auto& state = rt();
    std::lock_guard<std::mutex> lk(state.mu);
    const std::string key(name);
    if (auto it = state.selectors.find(key); it != state.selectors.end()) {
        return it->second;
    }
    auto* owned = new std::string(key);
    state.owned_sel_strings.push_back(owned);
    state.selectors[key] = owned->c_str();
    return owned->c_str();
}

const char* finn_sel_getName(ObjcSel sel) {
    return sel ? sel : "";
}

ObjcClass* finn_objc_allocateClassPair(ObjcClass* super_cls, const char* name,
                                       std::size_t extra_bytes) {
    auto& state = rt();
    std::lock_guard<std::mutex> lk(state.mu);
    if (auto it = state.classes.find(name); it != state.classes.end()) {
        return it->second;
    }

    auto* cls = new ObjcClass();
    auto* meta = new ObjcClass();
    cls->name = name ? name : "";
    cls->superclass = super_cls;
    cls->isa = meta;
    cls->instance_size =
        (super_cls ? super_cls->instance_size : sizeof(ObjcObject)) + extra_bytes;
    cls->is_meta = false;

    meta->name = cls->name + " (meta)";
    meta->superclass = super_cls ? super_cls->isa : cls;
    meta->isa = meta;
    meta->instance_size = sizeof(ObjcClass);
    meta->is_meta = true;

    state.owned_classes.push_back(cls);
    state.owned_classes.push_back(meta);
    return cls;
}

void finn_objc_registerClassPair(ObjcClass* cls) {
    if (!cls) return;
    auto& state = rt();
    std::lock_guard<std::mutex> lk(state.mu);
    state.classes[cls->name] = cls;
}

ObjcClass* finn_objc_getClass(const char* name) {
    if (!name) return nullptr;
    auto& state = rt();
    std::lock_guard<std::mutex> lk(state.mu);
    auto it = state.classes.find(name);
    return (it == state.classes.end()) ? nullptr : it->second;
}

ObjcClass* finn_objc_getMetaClass(const char* name) {
    ObjcClass* cls = finn_objc_getClass(name);
    return cls ? cls->isa : nullptr;
}

bool finn_class_addMethod(ObjcClass* cls, ObjcSel sel, ObjcImp imp, const char*) {
    if (!cls || !sel || !imp) return false;
    ObjcSel interned = finn_sel_registerName(sel);
    cls->methods[interned] = imp;
    return true;
}

bool finn_class_addClassMethod(ObjcClass* cls, ObjcSel sel, ObjcImp imp, const char* types) {
    if (!cls || !cls->isa) return false;
    return finn_class_addMethod(cls->isa, sel, imp, types);
}

ObjcClass* finn_object_getClass(ObjcId obj) {
    return obj ? obj->isa : nullptr;
}

bool finn_objc_isKindOfClass(ObjcId obj, ObjcClass* target) {
    target = resolve_class_ptr(target);
    if (!obj || !target) return false;
    for (ObjcClass* cur = obj->isa; cur != nullptr; cur = cur->superclass) {
        if (cur == target) return true;
    }
    return false;
}

bool finn_objc_respondsToSelector(ObjcId obj, ObjcSel sel) {
    if (!obj || !sel) return false;
    for (ObjcClass* cur = obj->isa; cur != nullptr; cur = cur->superclass) {
        if (cur->methods.find(sel) != cur->methods.end()) return true;
        for (const auto& [k, imp] : cur->methods) {
            if (std::strcmp(k, sel) == 0) return true;
        }
    }
    return false;
}

ObjcId finn_objc_alloc_object(ObjcClass* cls, std::size_t total_size) {
    if (!cls) return nullptr;
    const std::size_t body = std::max(total_size, cls->instance_size);
    const std::size_t alloc_size = sizeof(ObjcHeapHeader) + body;
    auto* raw = static_cast<std::uint8_t*>(std::calloc(1, alloc_size));
    if (!raw) return nullptr;
    auto* hdr = reinterpret_cast<ObjcHeapHeader*>(raw);
    hdr->magic = kObjcHeapMagic;
    hdr->refcount.store(1, std::memory_order_relaxed);
    auto* obj = reinterpret_cast<ObjcId>(raw + sizeof(ObjcHeapHeader));
    obj->isa = cls;
    return obj;
}

ObjcId finn_objc_retain(ObjcId obj) {
    if (auto* hdr = heap_header_of(obj)) {
        hdr->refcount.fetch_add(1, std::memory_order_relaxed);
    }
    return obj;
}

void finn_objc_release(ObjcId obj) {
    auto* hdr = heap_header_of(obj);
    if (!hdr) return;
    if (hdr->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // A weak reference must read as nil after the last strong release.
        compat_weak_zero(obj);
        if (obj->isa && obj->isa->cpp_destructor) {
            obj->isa->cpp_destructor(obj);
        }
        hdr->magic = 0;
        std::free(hdr);
    }
}

ObjcId finn_objc_autorelease(ObjcId obj) {
    if (!obj) return nullptr;
    if (!g_autorelease_pools.empty()) {
        g_autorelease_pools.back().push_back(obj);
    }
    return obj;
}

void* finn_objc_autoreleasePoolPush() {
    g_autorelease_pools.emplace_back();
    return reinterpret_cast<void*>(g_autorelease_pools.size());
}

void finn_objc_autoreleasePoolPop(void* token) {
    const std::size_t target_depth = reinterpret_cast<std::size_t>(token);
    while (!g_autorelease_pools.empty() && g_autorelease_pools.size() >= target_depth) {
        auto pool = std::move(g_autorelease_pools.back());
        g_autorelease_pools.pop_back();
        for (auto it = pool.rbegin(); it != pool.rend(); ++it) {
            finn_objc_release(*it);
        }
        if (target_depth == 0) break;
    }
}

void set_cfstring_constant_class(ObjcClass* cls) {
    rt().cfstring_class = cls;
}

ObjcClass* get_cfstring_constant_class() {
    return rt().cfstring_class;
}

void* get_or_create_msgsend_stub(std::string_view selector_name) {
    auto& state = rt();
    std::lock_guard<std::mutex> lk(state.mu);
    const std::string key(selector_name);
    if (auto it = state.msgsend_stubs.find(key); it != state.msgsend_stubs.end()) {
        return it->second;
    }

    // Intern selector without deadlocking on state.mu
    const char* sel_ptr = nullptr;
    if (auto sit = state.selectors.find(key); sit != state.selectors.end()) {
        sel_ptr = sit->second;
    } else {
        auto* owned = new std::string(key);
        state.owned_sel_strings.push_back(owned);
        state.selectors[key] = owned->c_str();
        sel_ptr = owned->c_str();
    }

    constexpr std::size_t kStubBytes = 32;
    if (!state.stub_page || state.stub_used + kStubBytes > state.stub_cap) {
        const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
        const std::size_t cap = std::max<std::size_t>(page, 16384);
        int flags = MAP_PRIVATE | MAP_ANON;
#if defined(__APPLE__) && defined(MAP_JIT)
        flags |= MAP_JIT;
#endif
        void* mem = ::mmap(nullptr, cap, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
        if (mem == MAP_FAILED) return nullptr;
        state.stub_page = static_cast<std::uint8_t*>(mem);
        state.stub_used = 0;
        state.stub_cap = cap;
    }

    std::uint8_t* dst = state.stub_page + state.stub_used;
    state.stub_used += kStubBytes;

#if defined(__APPLE__) && defined(__aarch64__)
    ::pthread_jit_write_protect_np(0);
#endif

    const std::uintptr_t sel_u = reinterpret_cast<std::uintptr_t>(sel_ptr);
    const std::uintptr_t msg_u = reinterpret_cast<std::uintptr_t>(&finn_objc_msgSend);

#if defined(__aarch64__)
    const std::uint32_t insns[4] = {
        0x58000081u,  // ldr x1, [pc, #16]
        0x580000b0u,  // ldr x16, [pc, #20]
        0xd61f0200u,  // br x16
        0xd503201fu,  // nop
    };
    std::memcpy(dst, insns, sizeof(insns));
    std::memcpy(dst + 16, &sel_u, sizeof(sel_u));
    std::memcpy(dst + 24, &msg_u, sizeof(msg_u));
#elif defined(__x86_64__)
    dst[0] = 0x48;
    dst[1] = 0xbe;  // movabs imm64, %rsi
    std::memcpy(dst + 2, &sel_u, sizeof(sel_u));
    dst[10] = 0x49;
    dst[11] = 0xbb;  // movabs imm64, %r11
    std::memcpy(dst + 12, &msg_u, sizeof(msg_u));
    dst[20] = 0x41;
    dst[21] = 0xff;
    dst[22] = 0xe3;  // jmp *%r11
    dst[23] = 0x90;  // nop
#endif

#if defined(__APPLE__) && defined(__aarch64__)
    ::pthread_jit_write_protect_np(1);
    ::sys_icache_invalidate(dst, kStubBytes);
#else
    __builtin___clear_cache(reinterpret_cast<char*>(dst),
                            reinterpret_cast<char*>(dst + kStubBytes));
#endif

    state.msgsend_stubs[key] = dst;
    return dst;
}

void realize_objc_sections(const ObjcImageSections& sections,
                           const std::unordered_map<std::string, void*>&) {
    // 1. Intern every selector reference in __DATA,__objc_selrefs
    if (sections.selrefs.addr && sections.selrefs.size >= sizeof(void*)) {
        const std::size_t count = sections.selrefs.size / sizeof(void*);
        auto* slots = reinterpret_cast<const char**>(sections.selrefs.addr);
        for (std::size_t i = 0; i < count; ++i) {
            if (slots[i]) slots[i] = finn_sel_registerName(slots[i]);
        }
    }

    // 2. Realize every Mach-O class in __DATA,__objc_classlist
    if (sections.classlist.addr && sections.classlist.size >= sizeof(void*)) {
        const std::size_t count = sections.classlist.size / sizeof(void*);
        auto* slots = reinterpret_cast<std::uintptr_t*>(sections.classlist.addr);
        for (std::size_t i = 0; i < count; ++i) {
            if (slots[i]) (void)lookup_or_realize_mach_class(slots[i]);
        }
    }

    // 3. Patch __DATA,__objc_classrefs and __objc_superrefs to FinnOS ObjcClass*
    auto patch_class_refs = [](ObjcSectionSpan span) {
        if (!span.addr || span.size < sizeof(void*)) return;
        const std::size_t count = span.size / sizeof(void*);
        auto* slots = reinterpret_cast<std::uintptr_t*>(span.addr);
        for (std::size_t i = 0; i < count; ++i) {
            if (slots[i] == 0) continue;
            if (ObjcClass* resolved = lookup_or_realize_mach_class(slots[i])) {
                slots[i] = reinterpret_cast<std::uintptr_t>(resolved);
            }
        }
    };
    patch_class_refs(sections.classrefs);
    patch_class_refs(sections.superrefs);

    // 4. Attach categories in __DATA,__objc_catlist
    if (sections.catlist.addr && sections.catlist.size >= sizeof(void*)) {
        const std::size_t count = sections.catlist.size / sizeof(void*);
        auto* slots = reinterpret_cast<std::uintptr_t*>(sections.catlist.addr);
        for (std::size_t i = 0; i < count; ++i) {
            if (!slots[i]) continue;
            const auto* cat = reinterpret_cast<const MachObjcCategory*>(slots[i]);
            if (ObjcClass* target = lookup_or_realize_mach_class(cat->cls)) {
                import_mach_method_list(target, cat->instance_methods);
                import_mach_method_list(target->isa, cat->class_methods);
            }
        }
    }

    // 5. Patch constant NSString literals in __DATA,__cfstring
    if (sections.cfstring.addr &&
        sections.cfstring.size >= sizeof(DarwinCFStringLiteral) &&
        get_cfstring_constant_class() != nullptr) {
        const std::size_t count = sections.cfstring.size / sizeof(DarwinCFStringLiteral);
        auto* lits = reinterpret_cast<DarwinCFStringLiteral*>(sections.cfstring.addr);
        for (std::size_t i = 0; i < count; ++i) {
            lits[i].isa = get_cfstring_constant_class();
        }
    }
}

void* resolve_objc_dynamic_symbol(std::string_view sym_name) {
    // Modern Xcode 14+ msgSend stubs: _objc_msgSend$<selector>
    constexpr std::string_view kStubPrefix = "_objc_msgSend$";
    if (sym_name.rfind(kStubPrefix, 0) == 0) {
        return get_or_create_msgsend_stub(sym_name.substr(kStubPrefix.size()));
    }
    // Objective-C class symbols: _OBJC_CLASS_$_<Name> / _OBJC_METACLASS_$_<Name>
    constexpr std::string_view kClsPrefix = "_OBJC_CLASS_$_";
    if (sym_name.rfind(kClsPrefix, 0) == 0) {
        const std::string cname(sym_name.substr(kClsPrefix.size()));
        return finn_objc_getClass(cname.c_str());
    }
    constexpr std::string_view kMetaPrefix = "_OBJC_METACLASS_$_";
    if (sym_name.rfind(kMetaPrefix, 0) == 0) {
        const std::string cname(sym_name.substr(kMetaPrefix.size()));
        return finn_objc_getMetaClass(cname.c_str());
    }
    return nullptr;
}

void register_objc_symbols(std::unordered_map<std::string, void*>* table) {
    auto add = [&](const char* name, auto ptr) {
        void* raw = nullptr;
        static_assert(sizeof(raw) == sizeof(ptr));
        std::memcpy(&raw, &ptr, sizeof(raw));
        (*table)[name] = raw;
    };

    add("_objc_msgSend", &finn_objc_msgSend);
    add("_objc_msgSendSuper2", &finn_objc_msgSendSuper2);
#if defined(__x86_64__)
    add("_objc_msgSend_fpret", &finn_objc_msgSend_fpret);
    add("_objc_msgSend_stret", &finn_objc_msgSend);
#endif
    add("_objc_alloc", &shim_objc_alloc);
    add("_objc_alloc_init", &shim_objc_alloc_init);
    add("_objc_allocWithZone", &shim_objc_allocWithZone);
    add("_objc_opt_new", &shim_objc_opt_new);
    add("_objc_opt_self", &shim_objc_opt_self);
    add("_objc_opt_class", &shim_objc_opt_class);
    add("_objc_opt_isKindOfClass", &shim_objc_opt_isKindOfClass);
    add("_objc_opt_respondsToSelector", &shim_objc_opt_respondsToSelector);
    add("_objc_retain", &finn_objc_retain);
    add("_objc_release", &finn_objc_release);
    add("_objc_autorelease", &finn_objc_autorelease);
    add("_objc_autoreleaseReturnValue", &shim_objc_autoreleaseReturnValue);
    add("_objc_retainAutoreleasedReturnValue", &shim_objc_retainAutoreleasedReturnValue);
    add("_objc_retainAutorelease", &shim_objc_retainAutorelease);
    add("_objc_retainAutoreleaseReturnValue", &shim_objc_retainAutoreleaseReturnValue);
    add("_objc_claimAutoreleasedReturnValue", &shim_objc_retainAutoreleasedReturnValue);
    add("_objc_unsafeClaimAutoreleasedReturnValue", &shim_objc_retainAutoreleasedReturnValue);
    add("_objc_storeStrong", &shim_objc_storeStrong);
    add("_objc_initWeak", &shim_objc_initWeak);
    add("_objc_storeWeak", &shim_objc_storeWeak);
    add("_objc_loadWeakRetained", &shim_objc_loadWeakRetained);
    add("_objc_destroyWeak", &shim_objc_destroyWeak);
    add("_objc_autoreleasePoolPush", &finn_objc_autoreleasePoolPush);
    add("_objc_autoreleasePoolPop", &finn_objc_autoreleasePoolPop);
    add("_objc_enumerationMutation", &shim_objc_enumerationMutation);
    add("_objc_getClass", &finn_objc_getClass);
    add("_objc_lookUpClass", &finn_objc_getClass);
    add("_objc_getMetaClass", &finn_objc_getMetaClass);
    add("_objc_allocateClassPair", &finn_objc_allocateClassPair);
    add("_objc_registerClassPair", &finn_objc_registerClassPair);
    add("_class_addMethod", &finn_class_addMethod);
    add("_class_getName", &shim_class_getName);
    add("_class_getSuperclass", &shim_class_getSuperclass);
    add("_object_getClass", &finn_object_getClass);
    add("_sel_registerName", &finn_sel_registerName);
    add("_sel_getName", &finn_sel_getName);

    // Blocks runtime & ObjC static class cache symbols
    add("__objc_empty_cache", &g_objc_empty_cache);
    add("__objc_empty_vtable", &g_objc_empty_vtable);
    add("__NSConcreteGlobalBlock", &g_concrete_global_block_isa);
    add("__NSConcreteStackBlock", &g_concrete_stack_block_isa);
    add("__NSConcreteMallocBlock", &g_concrete_malloc_block_isa);
    add("__Block_copy", &shim_block_copy);
    add("__Block_release", &shim_block_release);
    add("__Block_object_assign", &shim_block_object_assign);
    add("__Block_object_dispose", &shim_block_object_dispose);
}

}  // namespace finn::darwin
