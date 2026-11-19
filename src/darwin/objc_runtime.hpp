// Clean-room Objective-C 2.0 runtime (libobjc.A.dylib) for FinnOS finn-darwin.
// Supports Mach-O ABI v2 class/metaclass/category/selector/cfstring realization,
// ARC reference counting, blocks, and dynamic objc_msgSend$<sel> stubs.

#ifndef FINN_DARWIN_OBJC_RUNTIME_HPP
#define FINN_DARWIN_OBJC_RUNTIME_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace finn::darwin {

struct ObjcClass;
struct ObjcObject;

using ObjcId = ObjcObject*;
using ObjcSel = const char*;
using ObjcImp = void (*)();

// All Objective-C instances start with an isa pointer (matches Darwin ABI).
struct ObjcObject {
    ObjcClass* isa = nullptr;
};

// Matches Darwin's struct objc_super passed to objc_msgSendSuper2.
struct ObjcSuper {
    ObjcId receiver = nullptr;
    ObjcClass* current_class = nullptr;
};

// Matches Darwin's __DATA,__cfstring constant NSString binary layout (32 bytes on 64-bit).
struct DarwinCFStringLiteral {
    void* isa;
    std::uintptr_t flags;
    const char* data;
    std::intptr_t length;
};

// Matches Darwin's Block_layout header.
struct DarwinBlockLayout {
    void* isa;
    int flags;
    int reserved;
    void (*invoke)(void*, ...);
    void* descriptor;
};

// Runtime class record. The first field is `isa` (metaclass pointer) so a Class
// is itself a valid ObjcObject for class-method dispatch `[MyClass alloc]`.
struct ObjcClass {
    ObjcClass* isa = nullptr;          // Metaclass
    ObjcClass* superclass = nullptr;
    std::string name;
    std::size_t instance_size = sizeof(ObjcObject);
    bool is_meta = false;
    std::unordered_map<const char*, ObjcImp> methods;
    void (*cpp_destructor)(ObjcId) = nullptr;
    void* mach_class_ptr = nullptr;    // Original Mach-O class_t pointer when realized from image
};

// Assembly trampolines in objc_msgsend.S
extern "C" {
void finn_objc_msgSend();
void finn_objc_msgSendSuper2();
#if defined(__x86_64__)
void finn_objc_msgSend_fpret();
#endif
ObjcImp finn_objc_lookup_imp(ObjcId self, ObjcSel sel);
ObjcImp finn_objc_lookup_super_imp(ObjcSuper* super_info, ObjcSel sel);
}

// Runtime initialization & symbol registration
void init_objc_runtime();
void register_objc_symbols(std::unordered_map<std::string, void*>* table);

// Core ObjC C API used by Foundation, AppKit, and loaded Mach-O binaries
ObjcSel finn_sel_registerName(const char* name);
const char* finn_sel_getName(ObjcSel sel);
ObjcClass* finn_objc_allocateClassPair(ObjcClass* super_cls, const char* name,
                                       std::size_t extra_bytes = 0);
void finn_objc_registerClassPair(ObjcClass* cls);
ObjcClass* finn_objc_getClass(const char* name);
ObjcClass* finn_objc_getMetaClass(const char* name);
bool finn_class_addMethod(ObjcClass* cls, ObjcSel sel, ObjcImp imp, const char* types = "");
bool finn_class_addClassMethod(ObjcClass* cls, ObjcSel sel, ObjcImp imp, const char* types = "");
ObjcClass* finn_object_getClass(ObjcId obj);
bool finn_objc_isKindOfClass(ObjcId obj, ObjcClass* cls);
bool finn_objc_respondsToSelector(ObjcId obj, ObjcSel sel);

// Object allocation and ARC memory management
ObjcId finn_objc_alloc_object(ObjcClass* cls, std::size_t total_size = 0);
ObjcId finn_objc_retain(ObjcId obj);
void finn_objc_release(ObjcId obj);
ObjcId finn_objc_autorelease(ObjcId obj);
void* finn_objc_autoreleasePoolPush();
void finn_objc_autoreleasePoolPop(void* token);

// Typed helper for invoking Objective-C methods from C++ framework code
template <typename Ret, typename... Args>
Ret objc_send(ObjcId self, ObjcSel sel, Args... args) {
    using Fn = Ret (*)(ObjcId, ObjcSel, Args...);
    auto fn = reinterpret_cast<Fn>(&finn_objc_msgSend);
    return fn(self, sel, args...);
}

template <typename Ret, typename... Args>
Ret objc_send_name(ObjcId self, const char* sel_name, Args... args) {
    return objc_send<Ret>(self, finn_sel_registerName(sel_name), args...);
}

// Synthesizes a native executable trampoline for Clang 14+ `_objc_msgSend$<selector>` symbols.
void* get_or_create_msgsend_stub(std::string_view selector_name);

// Realizes Objective-C ABI v2 sections mapped from a Mach-O image in memory.
struct ObjcSectionSpan {
    std::uint8_t* addr = nullptr;
    std::size_t size = 0;
};

struct ObjcImageSections {
    ObjcSectionSpan selrefs;
    ObjcSectionSpan classlist;
    ObjcSectionSpan classrefs;
    ObjcSectionSpan superrefs;
    ObjcSectionSpan catlist;
    ObjcSectionSpan cfstring;
    std::uintptr_t image_slide = 0;
};

void realize_objc_sections(const ObjcImageSections& sections,
                           const std::unordered_map<std::string, void*>& symbols);

// Registers the NSConstantString class pointer used by __DATA,__cfstring literals.
void set_cfstring_constant_class(ObjcClass* cls);
ObjcClass* get_cfstring_constant_class();

// Helper for resolving external class symbols like `OBJC_CLASS_$_NSObject`.
void* resolve_objc_dynamic_symbol(std::string_view sym_name);

}  // namespace finn::darwin

#endif
