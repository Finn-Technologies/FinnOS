#include "darwin/foundation.hpp"

#include "darwin/plist.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace finn::darwin {
namespace {

template <typename Fn>
ObjcImp to_imp(Fn fn) {
    ObjcImp out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// --- Backing C++ structs laid out after ObjcObject::isa ---
struct NSStringObj {
    ObjcObject base;
    std::string* value;
};

struct NSArrayObj {
    ObjcObject base;
    std::vector<ObjcId>* items;
};

struct NSDictObj {
    ObjcObject base;
    std::map<std::string, ObjcId>* map;
};

struct NSNumberObj {
    ObjcObject base;
    double dval;
    std::int64_t ival;
    bool is_float;
};

struct NSDataObj {
    ObjcObject base;
    std::vector<std::uint8_t>* bytes;
};

struct NSURLObj {
    ObjcObject base;
    std::string* str;
};

struct NSBundleObj {
    ObjcObject base;
    std::string* path;
    ObjcId info_dict;
};

struct NSNotificationObj {
    ObjcObject base;
    ObjcId name;
    ObjcId object;
    ObjcId user_info;
};

struct NotificationObserver {
    ObjcId observer;
    ObjcSel selector;
    std::string name;
    ObjcId object;
};

ObjcClass* g_cls_nsobject = nullptr;
ObjcClass* g_cls_nsstring = nullptr;
ObjcClass* g_cls_nsmutablestring = nullptr;
ObjcClass* g_cls_nsconststring = nullptr;
ObjcClass* g_cls_nsarray = nullptr;
ObjcClass* g_cls_nsmutablearray = nullptr;
ObjcClass* g_cls_nsdict = nullptr;
ObjcClass* g_cls_nsmutabledict = nullptr;
ObjcClass* g_cls_nsnumber = nullptr;
ObjcClass* g_cls_nsdata = nullptr;
ObjcClass* g_cls_nsurl = nullptr;
ObjcClass* g_cls_nsbundle = nullptr;
ObjcClass* g_cls_nsprocessinfo = nullptr;
ObjcClass* g_cls_nsfilemanager = nullptr;
ObjcClass* g_cls_nsuserdefaults = nullptr;
ObjcClass* g_cls_nsnotification = nullptr;
ObjcClass* g_cls_nsnotificationcenter = nullptr;
ObjcClass* g_cls_nsautoreleasepool = nullptr;

std::string g_main_bundle_path;
ObjcId g_main_bundle_obj = nullptr;
ObjcId g_process_info_obj = nullptr;
ObjcId g_file_manager_obj = nullptr;
ObjcId g_user_defaults_obj = nullptr;
ObjcId g_notification_center_obj = nullptr;
std::map<std::string, ObjcId> g_defaults_store;
std::vector<NotificationObserver> g_observers;

// Placeholder global for Mach-O __DATA,__cfstring constant strings
ObjcClass g_cf_const_string_placeholder;

// --- String formatting helper supporting %@ for Objective-C objects ---
std::string format_nsstring_va(std::string_view fmt, va_list ap) {
    std::string out;
    out.reserve(fmt.size() + 64);
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) {
            out.push_back(fmt[i]);
            continue;
        }
        ++i;
        if (fmt[i] == '%') {
            out.push_back('%');
            continue;
        }
        // Parse optional flags/width/precision/length
        std::string spec = "%";
        while (i < fmt.size() && std::strchr("-+ #0123456789.*hlLzjt", fmt[i])) {
            spec.push_back(fmt[i++]);
        }
        if (i >= fmt.size()) break;
        const char conv = fmt[i];
        if (conv == '@') {
            ObjcId arg = va_arg(ap, ObjcId);
            if (!arg) {
                out += "(null)";
            } else if (arg->isa == g_cls_nsstring || arg->isa == g_cls_nsmutablestring ||
                       arg->isa == g_cls_nsconststring) {
                out += nsstring_to_utf8(arg);
            } else {
                ObjcId desc = objc_send_name<ObjcId>(arg, "description");
                out += desc ? nsstring_to_utf8(desc) : "(object)";
            }
            continue;
        }
        spec.push_back(conv);
        char tmp[128] = {};
        if (conv == 's') {
            const char* s = va_arg(ap, const char*);
            out += s ? s : "(null)";
        } else if (conv == 'd' || conv == 'i') {
            if (spec.find("ll") != std::string::npos) {
                std::snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, long long));
            } else if (spec.find('l') != std::string::npos) {
                std::snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, long));
            } else {
                std::snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, int));
            }
            out += tmp;
        } else if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            if (spec.find("ll") != std::string::npos) {
                std::snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, unsigned long long));
            } else if (spec.find('l') != std::string::npos) {
                std::snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, unsigned long));
            } else {
                std::snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, unsigned int));
            }
            out += tmp;
        } else if (conv == 'f' || conv == 'g' || conv == 'e' || conv == 'F' || conv == 'G') {
            std::snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, double));
            out += tmp;
        } else if (conv == 'c') {
            out.push_back(static_cast<char>(va_arg(ap, int)));
        } else if (conv == 'p') {
            std::snprintf(tmp, sizeof(tmp), "%p", va_arg(ap, void*));
            out += tmp;
        } else {
            out += spec;
        }
    }
    return out;
}

// --- NSObject methods ---
ObjcId nsobject_alloc(ObjcId self, ObjcSel) {
    return finn_objc_alloc_object(reinterpret_cast<ObjcClass*>(self));
}
ObjcId nsobject_init(ObjcId self, ObjcSel) { return self; }
ObjcId nsobject_new(ObjcId self, ObjcSel) {
    ObjcId obj = objc_send_name<ObjcId>(self, "alloc");
    return objc_send_name<ObjcId>(obj, "init");
}
ObjcId nsobject_retain(ObjcId self, ObjcSel) { return finn_objc_retain(self); }
void nsobject_release(ObjcId self, ObjcSel) { finn_objc_release(self); }
ObjcId nsobject_autorelease(ObjcId self, ObjcSel) { return finn_objc_autorelease(self); }
void nsobject_dealloc(ObjcId, ObjcSel) {}
ObjcClass* nsobject_class(ObjcId self, ObjcSel) { return finn_object_getClass(self); }
ObjcClass* nsobject_superclass(ObjcId self, ObjcSel) {
    ObjcClass* c = finn_object_getClass(self);
    return c ? c->superclass : nullptr;
}
int nsobject_isKindOfClass(ObjcId self, ObjcSel, ObjcClass* cls) {
    return finn_objc_isKindOfClass(self, cls) ? 1 : 0;
}
int nsobject_respondsToSelector(ObjcId self, ObjcSel, ObjcSel sel) {
    return finn_objc_respondsToSelector(self, sel) ? 1 : 0;
}
ObjcId nsobject_performSelector(ObjcId self, ObjcSel, ObjcSel sel) {
    return objc_send<ObjcId>(self, sel);
}
ObjcId nsobject_performSelectorWithObject(ObjcId self, ObjcSel, ObjcSel sel, ObjcId arg) {
    return objc_send<ObjcId>(self, sel, arg);
}
int nsobject_isEqual(ObjcId self, ObjcSel, ObjcId other) { return self == other ? 1 : 0; }
std::uintptr_t nsobject_hash(ObjcId self, ObjcSel) {
    return reinterpret_cast<std::uintptr_t>(self);
}
ObjcId nsobject_description(ObjcId self, ObjcSel) {
    char buf[128] = {};
    const char* cname = (self && self->isa) ? self->isa->name.c_str() : "NSObject";
    std::snprintf(buf, sizeof(buf), "<%s: %p>", cname, static_cast<void*>(self));
    return nsstring_from_utf8(buf);
}

// --- NSString / NSMutableString / NSConstantString ---
void nsstring_dtor(ObjcId obj) {
    auto* s = reinterpret_cast<NSStringObj*>(obj);
    delete s->value;
    s->value = nullptr;
}

ObjcId make_nsstring(std::string_view text, bool mut = false) {
    auto* s = reinterpret_cast<NSStringObj*>(
        finn_objc_alloc_object(mut ? g_cls_nsmutablestring : g_cls_nsstring,
                               sizeof(NSStringObj)));
    s->value = new std::string(text);
    return reinterpret_cast<ObjcId>(s);
}

ObjcId nsstring_init(ObjcId self, ObjcSel) {
    auto* s = reinterpret_cast<NSStringObj*>(self);
    if (!s->value) s->value = new std::string();
    return self;
}

ObjcId nsstring_stringWithUTF8(ObjcId, ObjcSel, const char* cstr) {
    return make_nsstring(cstr ? cstr : "");
}

ObjcId nsstring_initWithUTF8(ObjcId self, ObjcSel, const char* cstr) {
    auto* s = reinterpret_cast<NSStringObj*>(self);
    delete s->value;
    s->value = new std::string(cstr ? cstr : "");
    return self;
}

ObjcId nsstring_stringWithString(ObjcId self, ObjcSel, ObjcId other) {
    const bool mut = (reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutablestring);
    return make_nsstring(nsstring_to_utf8(other), mut);
}

#if defined(__linux__) && defined(__aarch64__)
ObjcId nsstring_stringWithFormat(ObjcId, ObjcSel, ObjcId fmt_obj,
                                 std::uintptr_t, std::uintptr_t, std::uintptr_t,
                                 std::uintptr_t, std::uintptr_t,
                                 double, double, double, double, double, double, double,
                                 double _finn_va_last, ...) {
    const std::string fmt = nsstring_to_utf8(fmt_obj);
    va_list ap;
    va_start(ap, _finn_va_last);
    const std::string formatted = format_nsstring_va(fmt, ap);
    va_end(ap);
    return make_nsstring(formatted);
}
#else
ObjcId nsstring_stringWithFormat(ObjcId, ObjcSel, ObjcId fmt_obj, ...) {
    const std::string fmt = nsstring_to_utf8(fmt_obj);
    va_list ap;
    va_start(ap, fmt_obj);
    const std::string formatted = format_nsstring_va(fmt, ap);
    va_end(ap);
    return make_nsstring(formatted);
}
#endif

const char* nsstring_utf8String(ObjcId self, ObjcSel) {
    if (!self) return "";
    if (self->isa == g_cls_nsconststring) {
        const auto* lit = reinterpret_cast<const DarwinCFStringLiteral*>(self);
        return lit->data ? lit->data : "";
    }
    const auto* s = reinterpret_cast<const NSStringObj*>(self);
    return s->value ? s->value->c_str() : "";
}

std::size_t nsstring_length(ObjcId self, ObjcSel) {
    return nsstring_to_utf8(self).size();
}

int nsstring_isEqualToString(ObjcId self, ObjcSel, ObjcId other) {
    if (!other) return 0;
    return nsstring_to_utf8(self) == nsstring_to_utf8(other) ? 1 : 0;
}

int nsstring_hasPrefix(ObjcId self, ObjcSel, ObjcId prefix) {
    const std::string a = nsstring_to_utf8(self);
    const std::string b = nsstring_to_utf8(prefix);
    return a.rfind(b, 0) == 0 ? 1 : 0;
}

int nsstring_hasSuffix(ObjcId self, ObjcSel, ObjcId suffix) {
    const std::string a = nsstring_to_utf8(self);
    const std::string b = nsstring_to_utf8(suffix);
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0 ? 1 : 0;
}

ObjcId nsstring_byAppendingString(ObjcId self, ObjcSel, ObjcId other) {
    return make_nsstring(nsstring_to_utf8(self) + nsstring_to_utf8(other));
}

ObjcId nsstring_byAppendingPathComponent(ObjcId self, ObjcSel, ObjcId comp) {
    const fs::path p = fs::path(nsstring_to_utf8(self)) / nsstring_to_utf8(comp);
    return make_nsstring(p.string());
}

ObjcId nsstring_lastPathComponent(ObjcId self, ObjcSel) {
    return make_nsstring(fs::path(nsstring_to_utf8(self)).filename().string());
}

ObjcId nsstring_pathExtension(ObjcId self, ObjcSel) {
    std::string ext = fs::path(nsstring_to_utf8(self)).extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    return make_nsstring(ext);
}

ObjcId nsstring_lowercase(ObjcId self, ObjcSel) {
    std::string s = nsstring_to_utf8(self);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return make_nsstring(s);
}

ObjcId nsstring_uppercase(ObjcId self, ObjcSel) {
    std::string s = nsstring_to_utf8(self);
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return make_nsstring(s);
}

std::intptr_t nsstring_integerValue(ObjcId self, ObjcSel) {
    return std::strtoll(nsstring_to_utf8(self).c_str(), nullptr, 10);
}

double nsstring_doubleValue(ObjcId self, ObjcSel) {
    return std::strtod(nsstring_to_utf8(self).c_str(), nullptr);
}

void nsmutablestring_appendString(ObjcId self, ObjcSel, ObjcId other) {
    auto* s = reinterpret_cast<NSStringObj*>(self);
    if (!s->value) s->value = new std::string();
    *s->value += nsstring_to_utf8(other);
}

void nsmutablestring_setString(ObjcId self, ObjcSel, ObjcId other) {
    auto* s = reinterpret_cast<NSStringObj*>(self);
    if (!s->value) s->value = new std::string();
    *s->value = nsstring_to_utf8(other);
}

ObjcId nsstring_description(ObjcId self, ObjcSel) { return self; }

// --- NSArray / NSMutableArray ---
void nsarray_dtor(ObjcId obj) {
    auto* a = reinterpret_cast<NSArrayObj*>(obj);
    if (a->items) {
        for (ObjcId item : *a->items) finn_objc_release(item);
        delete a->items;
        a->items = nullptr;
    }
}

ObjcId make_nsarray(bool mut = false) {
    auto* a = reinterpret_cast<NSArrayObj*>(
        finn_objc_alloc_object(mut ? g_cls_nsmutablearray : g_cls_nsarray, sizeof(NSArrayObj)));
    a->items = new std::vector<ObjcId>();
    return reinterpret_cast<ObjcId>(a);
}

ObjcId nsarray_init(ObjcId self, ObjcSel) {
    auto* a = reinterpret_cast<NSArrayObj*>(self);
    if (!a->items) a->items = new std::vector<ObjcId>();
    return self;
}

ObjcId nsarray_array(ObjcId self, ObjcSel) {
    return make_nsarray(reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutablearray);
}

ObjcId nsarray_arrayWithObjectsCount(ObjcId self, ObjcSel, const ObjcId* objs, std::size_t count) {
    ObjcId arr = make_nsarray(reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutablearray);
    auto* a = reinterpret_cast<NSArrayObj*>(arr);
    for (std::size_t i = 0; i < count; ++i) {
        a->items->push_back(finn_objc_retain(objs[i]));
    }
    return arr;
}

std::size_t nsarray_count(ObjcId self, ObjcSel) {
    const auto* a = reinterpret_cast<const NSArrayObj*>(self);
    return (a && a->items) ? a->items->size() : 0;
}

ObjcId nsarray_objectAtIndex(ObjcId self, ObjcSel, std::size_t idx) {
    const auto* a = reinterpret_cast<const NSArrayObj*>(self);
    if (!a || !a->items || idx >= a->items->size()) return nullptr;
    return (*a->items)[idx];
}

ObjcId nsarray_firstObject(ObjcId self, ObjcSel) {
    const auto* a = reinterpret_cast<const NSArrayObj*>(self);
    return (a && a->items && !a->items->empty()) ? a->items->front() : nullptr;
}

ObjcId nsarray_lastObject(ObjcId self, ObjcSel) {
    const auto* a = reinterpret_cast<const NSArrayObj*>(self);
    return (a && a->items && !a->items->empty()) ? a->items->back() : nullptr;
}

void nsarray_addObject(ObjcId self, ObjcSel, ObjcId item) {
    auto* a = reinterpret_cast<NSArrayObj*>(self);
    if (!a->items) a->items = new std::vector<ObjcId>();
    if (item) a->items->push_back(finn_objc_retain(item));
}

void nsarray_removeAllObjects(ObjcId self, ObjcSel) {
    auto* a = reinterpret_cast<NSArrayObj*>(self);
    if (!a->items) return;
    for (ObjcId item : *a->items) finn_objc_release(item);
    a->items->clear();
}

ObjcId nsarray_componentsJoinedByString(ObjcId self, ObjcSel, ObjcId sep) {
    const auto* a = reinterpret_cast<const NSArrayObj*>(self);
    const std::string glue = nsstring_to_utf8(sep);
    std::string out;
    if (a && a->items) {
        for (std::size_t i = 0; i < a->items->size(); ++i) {
            if (i > 0) out += glue;
            ObjcId desc = objc_send_name<ObjcId>((*a->items)[i], "description");
            out += nsstring_to_utf8(desc);
        }
    }
    return make_nsstring(out);
}

// --- NSDictionary / NSMutableDictionary ---
void nsdict_dtor(ObjcId obj) {
    auto* d = reinterpret_cast<NSDictObj*>(obj);
    if (d->map) {
        for (auto& [k, v] : *d->map) finn_objc_release(v);
        delete d->map;
        d->map = nullptr;
    }
}

ObjcId make_nsdict(bool mut = false) {
    auto* d = reinterpret_cast<NSDictObj*>(
        finn_objc_alloc_object(mut ? g_cls_nsmutabledict : g_cls_nsdict, sizeof(NSDictObj)));
    d->map = new std::map<std::string, ObjcId>();
    return reinterpret_cast<ObjcId>(d);
}

ObjcId nsdict_init(ObjcId self, ObjcSel) {
    auto* d = reinterpret_cast<NSDictObj*>(self);
    if (!d->map) d->map = new std::map<std::string, ObjcId>();
    return self;
}

ObjcId nsdict_dictionary(ObjcId self, ObjcSel) {
    return make_nsdict(reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutabledict);
}

ObjcId nsdict_withObjectsForKeysCount(ObjcId self, ObjcSel, const ObjcId* vals,
                                      const ObjcId* keys, std::size_t count) {
    ObjcId dict = make_nsdict(reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutabledict);
    auto* d = reinterpret_cast<NSDictObj*>(dict);
    for (std::size_t i = 0; i < count; ++i) {
        (*d->map)[nsstring_to_utf8(keys[i])] = finn_objc_retain(vals[i]);
    }
    return dict;
}

std::size_t nsdict_count(ObjcId self, ObjcSel) {
    const auto* d = reinterpret_cast<const NSDictObj*>(self);
    return (d && d->map) ? d->map->size() : 0;
}

ObjcId nsdict_objectForKey(ObjcId self, ObjcSel, ObjcId key) {
    const auto* d = reinterpret_cast<const NSDictObj*>(self);
    if (!d || !d->map || !key) return nullptr;
    auto it = d->map->find(nsstring_to_utf8(key));
    return (it == d->map->end()) ? nullptr : it->second;
}

void nsdict_setObjectForKey(ObjcId self, ObjcSel, ObjcId val, ObjcId key) {
    auto* d = reinterpret_cast<NSDictObj*>(self);
    if (!d->map) d->map = new std::map<std::string, ObjcId>();
    if (!key) return;
    const std::string k = nsstring_to_utf8(key);
    if (auto it = d->map->find(k); it != d->map->end()) {
        finn_objc_release(it->second);
        d->map->erase(it);
    }
    if (val) (*d->map)[k] = finn_objc_retain(val);
}

void nsdict_setObjectForKeyedSubscript(ObjcId self, ObjcSel sel, ObjcId val, ObjcId key) {
    nsdict_setObjectForKey(self, sel, val, key);
}

// --- NSNumber ---
ObjcId nsnumber_withInt(ObjcId, ObjcSel, int v) {
    auto* n = reinterpret_cast<NSNumberObj*>(
        finn_objc_alloc_object(g_cls_nsnumber, sizeof(NSNumberObj)));
    n->ival = v;
    n->dval = static_cast<double>(v);
    n->is_float = false;
    return reinterpret_cast<ObjcId>(n);
}

ObjcId nsnumber_withInteger(ObjcId, ObjcSel, std::intptr_t v) {
    auto* n = reinterpret_cast<NSNumberObj*>(
        finn_objc_alloc_object(g_cls_nsnumber, sizeof(NSNumberObj)));
    n->ival = v;
    n->dval = static_cast<double>(v);
    n->is_float = false;
    return reinterpret_cast<ObjcId>(n);
}

ObjcId nsnumber_withDouble(ObjcId, ObjcSel, double v) {
    auto* n = reinterpret_cast<NSNumberObj*>(
        finn_objc_alloc_object(g_cls_nsnumber, sizeof(NSNumberObj)));
    n->dval = v;
    n->ival = static_cast<std::int64_t>(v);
    n->is_float = true;
    return reinterpret_cast<ObjcId>(n);
}

int nsnumber_intValue(ObjcId self, ObjcSel) {
    return static_cast<int>(reinterpret_cast<const NSNumberObj*>(self)->ival);
}
std::intptr_t nsnumber_integerValue(ObjcId self, ObjcSel) {
    return static_cast<std::intptr_t>(reinterpret_cast<const NSNumberObj*>(self)->ival);
}
double nsnumber_doubleValue(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSNumberObj*>(self)->dval;
}
ObjcId nsnumber_stringValue(ObjcId self, ObjcSel) {
    const auto* n = reinterpret_cast<const NSNumberObj*>(self);
    char buf[64] = {};
    if (n->is_float) std::snprintf(buf, sizeof(buf), "%g", n->dval);
    else std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n->ival));
    return make_nsstring(buf);
}

// --- NSData ---
void nsdata_dtor(ObjcId obj) {
    auto* d = reinterpret_cast<NSDataObj*>(obj);
    delete d->bytes;
    d->bytes = nullptr;
}
ObjcId nsdata_withBytesLength(ObjcId, ObjcSel, const void* ptr, std::size_t len) {
    auto* d = reinterpret_cast<NSDataObj*>(
        finn_objc_alloc_object(g_cls_nsdata, sizeof(NSDataObj)));
    d->bytes = new std::vector<std::uint8_t>(
        static_cast<const std::uint8_t*>(ptr),
        static_cast<const std::uint8_t*>(ptr) + len);
    return reinterpret_cast<ObjcId>(d);
}
const void* nsdata_bytes(ObjcId self, ObjcSel) {
    const auto* d = reinterpret_cast<const NSDataObj*>(self);
    return (d && d->bytes) ? d->bytes->data() : nullptr;
}
std::size_t nsdata_length(ObjcId self, ObjcSel) {
    const auto* d = reinterpret_cast<const NSDataObj*>(self);
    return (d && d->bytes) ? d->bytes->size() : 0;
}

// --- NSURL ---
void nsurl_dtor(ObjcId obj) {
    delete reinterpret_cast<NSURLObj*>(obj)->str;
}
ObjcId nsurl_fileURLWithPath(ObjcId, ObjcSel, ObjcId path) {
    auto* u = reinterpret_cast<NSURLObj*>(
        finn_objc_alloc_object(g_cls_nsurl, sizeof(NSURLObj)));
    u->str = new std::string(nsstring_to_utf8(path));
    return reinterpret_cast<ObjcId>(u);
}
ObjcId nsurl_path(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    return make_nsstring((u && u->str) ? *u->str : "");
}

// --- NSBundle ---
ObjcId plist_to_objc(const PlistValue& v) {
    switch (v.kind) {
        case PlistValue::Kind::String: return make_nsstring(v.string_val);
        case PlistValue::Kind::Int: return nsnumber_withInteger(nullptr, nullptr, v.int_val);
        case PlistValue::Kind::Bool: return nsnumber_withInt(nullptr, nullptr, v.bool_val ? 1 : 0);
        case PlistValue::Kind::Real: return nsnumber_withDouble(nullptr, nullptr, v.real_val);
        case PlistValue::Kind::Array: {
            ObjcId arr = make_nsarray(false);
            for (const auto& e : v.array_val) nsarray_addObject(arr, nullptr, plist_to_objc(e));
            return arr;
        }
        case PlistValue::Kind::Dict: {
            ObjcId d = make_nsdict(false);
            for (const auto& [k, val] : v.dict_val) {
                nsdict_setObjectForKey(d, nullptr, plist_to_objc(val), make_nsstring(k));
            }
            return d;
        }
        default: return nullptr;
    }
}

ObjcId nsbundle_mainBundle(ObjcId, ObjcSel) {
    if (!g_main_bundle_obj) {
        auto* b = reinterpret_cast<NSBundleObj*>(
            finn_objc_alloc_object(g_cls_nsbundle, sizeof(NSBundleObj)));
        b->path = new std::string(g_main_bundle_path);
        b->info_dict = make_nsdict(false);
        if (!g_main_bundle_path.empty()) {
            const fs::path plist_path = fs::path(g_main_bundle_path) / "Contents" / "Info.plist";
            if (auto p = parse_plist_file(plist_path.string())) {
                b->info_dict = plist_to_objc(*p);
            }
        }
        g_main_bundle_obj = reinterpret_cast<ObjcId>(b);
    }
    return g_main_bundle_obj;
}

ObjcId nsbundle_bundlePath(ObjcId self, ObjcSel) {
    const auto* b = reinterpret_cast<const NSBundleObj*>(self);
    return make_nsstring((b && b->path) ? *b->path : "");
}

ObjcId nsbundle_infoDictionary(ObjcId self, ObjcSel) {
    const auto* b = reinterpret_cast<const NSBundleObj*>(self);
    return b ? b->info_dict : nullptr;
}

ObjcId nsbundle_objectForInfoKey(ObjcId self, ObjcSel, ObjcId key) {
    const auto* b = reinterpret_cast<const NSBundleObj*>(self);
    return (b && b->info_dict) ? nsdict_objectForKey(b->info_dict, nullptr, key) : nullptr;
}

// --- NSProcessInfo / NSFileManager / NSUserDefaults / NSNotificationCenter ---
ObjcId nsprocessinfo_processInfo(ObjcId, ObjcSel) {
    if (!g_process_info_obj) {
        g_process_info_obj = finn_objc_alloc_object(g_cls_nsprocessinfo);
    }
    return g_process_info_obj;
}
ObjcId nsprocessinfo_osVersionString(ObjcId, ObjcSel) {
    return make_nsstring("Version 14.0 (FinnOS Darwin Compatibility)");
}
std::size_t nsprocessinfo_processorCount(ObjcId, ObjcSel) {
    return std::max<std::size_t>(1, std::thread::hardware_concurrency());
}

ObjcId nsfilemanager_defaultManager(ObjcId, ObjcSel) {
    if (!g_file_manager_obj) {
        g_file_manager_obj = finn_objc_alloc_object(g_cls_nsfilemanager);
    }
    return g_file_manager_obj;
}
int nsfilemanager_fileExists(ObjcId, ObjcSel, ObjcId path) {
    std::error_code ec;
    return fs::exists(nsstring_to_utf8(path), ec) ? 1 : 0;
}
ObjcId nsfilemanager_currentDir(ObjcId, ObjcSel) {
    std::error_code ec;
    return make_nsstring(fs::current_path(ec).string());
}

ObjcId nsuserdefaults_standard(ObjcId, ObjcSel) {
    if (!g_user_defaults_obj) {
        g_user_defaults_obj = finn_objc_alloc_object(g_cls_nsuserdefaults);
    }
    return g_user_defaults_obj;
}
ObjcId nsuserdefaults_objectForKey(ObjcId, ObjcSel, ObjcId key) {
    auto it = g_defaults_store.find(nsstring_to_utf8(key));
    return (it == g_defaults_store.end()) ? nullptr : it->second;
}
void nsuserdefaults_setObjectForKey(ObjcId, ObjcSel, ObjcId val, ObjcId key) {
    const std::string k = nsstring_to_utf8(key);
    if (val) g_defaults_store[k] = finn_objc_retain(val);
    else g_defaults_store.erase(k);
}
int nsuserdefaults_synchronize(ObjcId, ObjcSel) { return 1; }

ObjcId nsnotifcenter_default(ObjcId, ObjcSel) {
    if (!g_notification_center_obj) {
        g_notification_center_obj = finn_objc_alloc_object(g_cls_nsnotificationcenter);
    }
    return g_notification_center_obj;
}
void nsnotifcenter_addObserver(ObjcId, ObjcSel, ObjcId obs, ObjcSel sel,
                               ObjcId name, ObjcId obj) {
    g_observers.push_back({obs, sel, nsstring_to_utf8(name), obj});
}
void nsnotifcenter_post(ObjcId, ObjcSel, ObjcId name, ObjcId obj, ObjcId user_info) {
    const std::string n = nsstring_to_utf8(name);
    auto* notif = reinterpret_cast<NSNotificationObj*>(
        finn_objc_alloc_object(g_cls_nsnotification, sizeof(NSNotificationObj)));
    notif->name = name;
    notif->object = obj;
    notif->user_info = user_info;
    for (const auto& o : g_observers) {
        if ((o.name.empty() || o.name == n) && (!o.object || o.object == obj)) {
            objc_send<void>(o.observer, o.selector, reinterpret_cast<ObjcId>(notif));
        }
    }
    finn_objc_release(reinterpret_cast<ObjcId>(notif));
}

// --- C Foundation / CoreFoundation APIs ---
#if defined(__linux__) && defined(__aarch64__)
void shim_NSLog(ObjcId fmt_obj,
                std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                std::uintptr_t, std::uintptr_t, std::uintptr_t,
                double, double, double, double, double, double, double,
                double _finn_va_last, ...) {
    const std::string fmt = nsstring_to_utf8(fmt_obj);
    va_list ap;
    va_start(ap, _finn_va_last);
    const std::string msg = format_nsstring_va(fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "%s\n", msg.c_str());
}
#else
void shim_NSLog(ObjcId fmt_obj, ...) {
    const std::string fmt = nsstring_to_utf8(fmt_obj);
    va_list ap;
    va_start(ap, fmt_obj);
    const std::string msg = format_nsstring_va(fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "%s\n", msg.c_str());
}
#endif

ObjcId shim_CFStringCreateWithCString(void*, const char* cstr, std::uint32_t) {
    return make_nsstring(cstr ? cstr : "");
}
const char* shim_CFStringGetCStringPtr(ObjcId str, std::uint32_t) {
    return nsstring_utf8String(str, nullptr);
}
std::intptr_t shim_CFStringGetLength(ObjcId str) {
    return static_cast<std::intptr_t>(nsstring_length(str, nullptr));
}
ObjcId shim_CFRetain(ObjcId obj) { return finn_objc_retain(obj); }
void shim_CFRelease(ObjcId obj) { finn_objc_release(obj); }

}  // namespace

ObjcId nsstring_from_utf8(std::string_view text) {
    return make_nsstring(text, false);
}

std::string nsstring_to_utf8(ObjcId str_obj) {
    if (!str_obj) return {};
    if (str_obj->isa == g_cls_nsconststring) {
        const auto* lit = reinterpret_cast<const DarwinCFStringLiteral*>(str_obj);
        if (!lit->data) return {};
        return (lit->length > 0) ? std::string(lit->data, static_cast<std::size_t>(lit->length))
                                 : std::string(lit->data);
    }
    if (str_obj->isa == g_cls_nsstring || str_obj->isa == g_cls_nsmutablestring) {
        const auto* s = reinterpret_cast<const NSStringObj*>(str_obj);
        return s->value ? *s->value : std::string{};
    }
    return {};
}

void init_foundation(const std::string& bundle_path) {
    g_main_bundle_path = bundle_path;
    if (g_cls_nsobject) return;

    init_objc_runtime();

    // NSObject
    g_cls_nsobject = finn_objc_allocateClassPair(nullptr, "NSObject");
    finn_class_addClassMethod(g_cls_nsobject, finn_sel_registerName("alloc"), to_imp(&nsobject_alloc));
    finn_class_addClassMethod(g_cls_nsobject, finn_sel_registerName("new"), to_imp(&nsobject_new));
    finn_class_addClassMethod(g_cls_nsobject, finn_sel_registerName("class"), to_imp(&nsobject_class));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("init"), to_imp(&nsobject_init));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("retain"), to_imp(&nsobject_retain));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("release"), to_imp(&nsobject_release));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("autorelease"), to_imp(&nsobject_autorelease));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("dealloc"), to_imp(&nsobject_dealloc));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("class"), to_imp(&nsobject_class));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("superclass"), to_imp(&nsobject_superclass));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("isKindOfClass:"), to_imp(&nsobject_isKindOfClass));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("respondsToSelector:"), to_imp(&nsobject_respondsToSelector));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("performSelector:"), to_imp(&nsobject_performSelector));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("performSelector:withObject:"), to_imp(&nsobject_performSelectorWithObject));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("isEqual:"), to_imp(&nsobject_isEqual));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("hash"), to_imp(&nsobject_hash));
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("description"), to_imp(&nsobject_description));
    finn_objc_registerClassPair(g_cls_nsobject);

    // NSString / NSMutableString / NSConstantString
    g_cls_nsstring = finn_objc_allocateClassPair(g_cls_nsobject, "NSString", sizeof(std::string*));
    g_cls_nsstring->cpp_destructor = &nsstring_dtor;
    finn_class_addClassMethod(g_cls_nsstring, finn_sel_registerName("stringWithUTF8String:"), to_imp(&nsstring_stringWithUTF8));
    finn_class_addClassMethod(g_cls_nsstring, finn_sel_registerName("stringWithString:"), to_imp(&nsstring_stringWithString));
    finn_class_addClassMethod(g_cls_nsstring, finn_sel_registerName("stringWithFormat:"), to_imp(&nsstring_stringWithFormat));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("init"), to_imp(&nsstring_init));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("initWithUTF8String:"), to_imp(&nsstring_initWithUTF8));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("UTF8String"), to_imp(&nsstring_utf8String));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("cStringUsingEncoding:"), to_imp(&nsstring_utf8String));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("length"), to_imp(&nsstring_length));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("isEqualToString:"), to_imp(&nsstring_isEqualToString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("isEqual:"), to_imp(&nsstring_isEqualToString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("hasPrefix:"), to_imp(&nsstring_hasPrefix));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("hasSuffix:"), to_imp(&nsstring_hasSuffix));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByAppendingString:"), to_imp(&nsstring_byAppendingString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByAppendingPathComponent:"), to_imp(&nsstring_byAppendingPathComponent));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("lastPathComponent"), to_imp(&nsstring_lastPathComponent));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("pathExtension"), to_imp(&nsstring_pathExtension));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("lowercaseString"), to_imp(&nsstring_lowercase));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("uppercaseString"), to_imp(&nsstring_uppercase));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("integerValue"), to_imp(&nsstring_integerValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("intValue"), to_imp(&nsstring_integerValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("doubleValue"), to_imp(&nsstring_doubleValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("description"), to_imp(&nsstring_description));
    finn_objc_registerClassPair(g_cls_nsstring);

    g_cls_nsmutablestring = finn_objc_allocateClassPair(g_cls_nsstring, "NSMutableString");
    g_cls_nsmutablestring->cpp_destructor = &nsstring_dtor;
    finn_class_addMethod(g_cls_nsmutablestring, finn_sel_registerName("appendString:"), to_imp(&nsmutablestring_appendString));
    finn_class_addMethod(g_cls_nsmutablestring, finn_sel_registerName("setString:"), to_imp(&nsmutablestring_setString));
    finn_objc_registerClassPair(g_cls_nsmutablestring);

    g_cls_nsconststring = finn_objc_allocateClassPair(g_cls_nsstring, "__NSCFConstantString");
    finn_objc_registerClassPair(g_cls_nsconststring);
    set_cfstring_constant_class(g_cls_nsconststring);

    // NSArray / NSMutableArray
    g_cls_nsarray = finn_objc_allocateClassPair(g_cls_nsobject, "NSArray", sizeof(void*));
    g_cls_nsarray->cpp_destructor = &nsarray_dtor;
    finn_class_addClassMethod(g_cls_nsarray, finn_sel_registerName("array"), to_imp(&nsarray_array));
    finn_class_addClassMethod(g_cls_nsarray, finn_sel_registerName("arrayWithObjects:count:"), to_imp(&nsarray_arrayWithObjectsCount));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("init"), to_imp(&nsarray_init));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("count"), to_imp(&nsarray_count));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("objectAtIndex:"), to_imp(&nsarray_objectAtIndex));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("objectAtIndexedSubscript:"), to_imp(&nsarray_objectAtIndex));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("firstObject"), to_imp(&nsarray_firstObject));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("lastObject"), to_imp(&nsarray_lastObject));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("componentsJoinedByString:"), to_imp(&nsarray_componentsJoinedByString));
    finn_objc_registerClassPair(g_cls_nsarray);

    g_cls_nsmutablearray = finn_objc_allocateClassPair(g_cls_nsarray, "NSMutableArray");
    g_cls_nsmutablearray->cpp_destructor = &nsarray_dtor;
    finn_class_addMethod(g_cls_nsmutablearray, finn_sel_registerName("addObject:"), to_imp(&nsarray_addObject));
    finn_class_addMethod(g_cls_nsmutablearray, finn_sel_registerName("removeAllObjects"), to_imp(&nsarray_removeAllObjects));
    finn_objc_registerClassPair(g_cls_nsmutablearray);

    // NSDictionary / NSMutableDictionary
    g_cls_nsdict = finn_objc_allocateClassPair(g_cls_nsobject, "NSDictionary", sizeof(void*));
    g_cls_nsdict->cpp_destructor = &nsdict_dtor;
    finn_class_addClassMethod(g_cls_nsdict, finn_sel_registerName("dictionary"), to_imp(&nsdict_dictionary));
    finn_class_addClassMethod(g_cls_nsdict, finn_sel_registerName("dictionaryWithObjects:forKeys:count:"), to_imp(&nsdict_withObjectsForKeysCount));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("init"), to_imp(&nsdict_init));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("count"), to_imp(&nsdict_count));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("objectForKey:"), to_imp(&nsdict_objectForKey));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("objectForKeyedSubscript:"), to_imp(&nsdict_objectForKey));
    finn_objc_registerClassPair(g_cls_nsdict);

    g_cls_nsmutabledict = finn_objc_allocateClassPair(g_cls_nsdict, "NSMutableDictionary");
    g_cls_nsmutabledict->cpp_destructor = &nsdict_dtor;
    finn_class_addMethod(g_cls_nsmutabledict, finn_sel_registerName("setObject:forKey:"), to_imp(&nsdict_setObjectForKey));
    finn_class_addMethod(g_cls_nsmutabledict, finn_sel_registerName("setObject:forKeyedSubscript:"), to_imp(&nsdict_setObjectForKeyedSubscript));
    finn_objc_registerClassPair(g_cls_nsmutabledict);

    // NSNumber
    g_cls_nsnumber = finn_objc_allocateClassPair(g_cls_nsobject, "NSNumber", sizeof(NSNumberObj));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithInt:"), to_imp(&nsnumber_withInt));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithBool:"), to_imp(&nsnumber_withInt));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithInteger:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithUnsignedInteger:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithUnsignedLong:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithLongLong:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithDouble:"), to_imp(&nsnumber_withDouble));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("intValue"), to_imp(&nsnumber_intValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("boolValue"), to_imp(&nsnumber_intValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("integerValue"), to_imp(&nsnumber_integerValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("doubleValue"), to_imp(&nsnumber_doubleValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("stringValue"), to_imp(&nsnumber_stringValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("description"), to_imp(&nsnumber_stringValue));
    finn_objc_registerClassPair(g_cls_nsnumber);

    // NSData
    g_cls_nsdata = finn_objc_allocateClassPair(g_cls_nsobject, "NSData", sizeof(void*));
    g_cls_nsdata->cpp_destructor = &nsdata_dtor;
    finn_class_addClassMethod(g_cls_nsdata, finn_sel_registerName("dataWithBytes:length:"), to_imp(&nsdata_withBytesLength));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("bytes"), to_imp(&nsdata_bytes));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("length"), to_imp(&nsdata_length));
    finn_objc_registerClassPair(g_cls_nsdata);

    // NSURL
    g_cls_nsurl = finn_objc_allocateClassPair(g_cls_nsobject, "NSURL", sizeof(void*));
    g_cls_nsurl->cpp_destructor = &nsurl_dtor;
    finn_class_addClassMethod(g_cls_nsurl, finn_sel_registerName("fileURLWithPath:"), to_imp(&nsurl_fileURLWithPath));
    finn_class_addClassMethod(g_cls_nsurl, finn_sel_registerName("URLWithString:"), to_imp(&nsurl_fileURLWithPath));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("path"), to_imp(&nsurl_path));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("absoluteString"), to_imp(&nsurl_path));
    finn_objc_registerClassPair(g_cls_nsurl);

    // NSBundle
    g_cls_nsbundle = finn_objc_allocateClassPair(g_cls_nsobject, "NSBundle", sizeof(NSBundleObj));
    finn_class_addClassMethod(g_cls_nsbundle, finn_sel_registerName("mainBundle"), to_imp(&nsbundle_mainBundle));
    finn_class_addMethod(g_cls_nsbundle, finn_sel_registerName("bundlePath"), to_imp(&nsbundle_bundlePath));
    finn_class_addMethod(g_cls_nsbundle, finn_sel_registerName("infoDictionary"), to_imp(&nsbundle_infoDictionary));
    finn_class_addMethod(g_cls_nsbundle, finn_sel_registerName("objectForInfoDictionaryKey:"), to_imp(&nsbundle_objectForInfoKey));
    finn_objc_registerClassPair(g_cls_nsbundle);

    // NSProcessInfo, NSFileManager, NSUserDefaults, NSNotificationCenter, NSAutoreleasePool
    g_cls_nsprocessinfo = finn_objc_allocateClassPair(g_cls_nsobject, "NSProcessInfo");
    finn_class_addClassMethod(g_cls_nsprocessinfo, finn_sel_registerName("processInfo"), to_imp(&nsprocessinfo_processInfo));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("operatingSystemVersionString"), to_imp(&nsprocessinfo_osVersionString));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("processorCount"), to_imp(&nsprocessinfo_processorCount));
    finn_objc_registerClassPair(g_cls_nsprocessinfo);

    g_cls_nsfilemanager = finn_objc_allocateClassPair(g_cls_nsobject, "NSFileManager");
    finn_class_addClassMethod(g_cls_nsfilemanager, finn_sel_registerName("defaultManager"), to_imp(&nsfilemanager_defaultManager));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("fileExistsAtPath:"), to_imp(&nsfilemanager_fileExists));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("currentDirectoryPath"), to_imp(&nsfilemanager_currentDir));
    finn_objc_registerClassPair(g_cls_nsfilemanager);

    g_cls_nsuserdefaults = finn_objc_allocateClassPair(g_cls_nsobject, "NSUserDefaults");
    finn_class_addClassMethod(g_cls_nsuserdefaults, finn_sel_registerName("standardUserDefaults"), to_imp(&nsuserdefaults_standard));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("objectForKey:"), to_imp(&nsuserdefaults_objectForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("stringForKey:"), to_imp(&nsuserdefaults_objectForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("setObject:forKey:"), to_imp(&nsuserdefaults_setObjectForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("synchronize"), to_imp(&nsuserdefaults_synchronize));
    finn_objc_registerClassPair(g_cls_nsuserdefaults);

    g_cls_nsnotification = finn_objc_allocateClassPair(g_cls_nsobject, "NSNotification", sizeof(NSNotificationObj));
    finn_objc_registerClassPair(g_cls_nsnotification);

    g_cls_nsnotificationcenter = finn_objc_allocateClassPair(g_cls_nsobject, "NSNotificationCenter");
    finn_class_addClassMethod(g_cls_nsnotificationcenter, finn_sel_registerName("defaultCenter"), to_imp(&nsnotifcenter_default));
    finn_class_addMethod(g_cls_nsnotificationcenter, finn_sel_registerName("addObserver:selector:name:object:"), to_imp(&nsnotifcenter_addObserver));
    finn_class_addMethod(g_cls_nsnotificationcenter, finn_sel_registerName("postNotificationName:object:userInfo:"), to_imp(&nsnotifcenter_post));
    finn_objc_registerClassPair(g_cls_nsnotificationcenter);

    g_cls_nsautoreleasepool = finn_objc_allocateClassPair(g_cls_nsobject, "NSAutoreleasePool");
    finn_class_addMethod(g_cls_nsautoreleasepool, finn_sel_registerName("drain"), to_imp(&nsobject_release));
    finn_objc_registerClassPair(g_cls_nsautoreleasepool);
}

void register_foundation_symbols(std::unordered_map<std::string, void*>* table) {
    (*table)["___CFConstantStringClassReference"] =
        g_cls_nsconststring ? static_cast<void*>(g_cls_nsconststring)
                            : static_cast<void*>(&g_cf_const_string_placeholder);
    (*table)["_NSLog"] = to_ptr(&shim_NSLog);
    (*table)["_CFStringCreateWithCString"] = to_ptr(&shim_CFStringCreateWithCString);
    (*table)["_CFStringGetCStringPtr"] = to_ptr(&shim_CFStringGetCStringPtr);
    (*table)["_CFStringGetLength"] = to_ptr(&shim_CFStringGetLength);
    (*table)["_CFRetain"] = to_ptr(&shim_CFRetain);
    (*table)["_CFRelease"] = to_ptr(&shim_CFRelease);
}

}  // namespace finn::darwin
