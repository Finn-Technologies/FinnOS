#include "darwin/foundation.hpp"

#include "darwin/plist.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

extern "C" char** environ;

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

struct DarwinRange {
    std::size_t location;
    std::size_t length;
};

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
    bool is_bool;
};

struct NSDataObj {
    ObjcObject base;
    std::vector<std::uint8_t>* bytes;
};

struct NSDateObj {
    ObjcObject base;
    double since_1970;
};

// Each byte of the backing UTF-8 string carries its own (retained) attributes
// dictionary. This makes range edits and effective ranges straightforward and
// correct; the trade-off is O(length) attribute slots, which is fine for text.
struct NSAttributedStringObj {
    ObjcObject base;
    std::string* text;
    std::vector<ObjcId>* attributes;
};

struct NSDateFormatterObj {
    ObjcObject base;
    std::string* format;
};

// Instances are calloc-zeroed by the ObjC allocator; no C++ constructor runs.
struct NSDateComponentsObj {
    ObjcObject base;
    std::int64_t era;
    std::int64_t year;
    std::int64_t month;
    std::int64_t day;
    std::int64_t hour;
    std::int64_t minute;
    std::int64_t second;
    std::int64_t nanosecond;
    std::int64_t weekday;
};

struct NSCalendarObj {
    ObjcObject base;
    std::string* identifier;
};

struct NSErrorObj {
    ObjcObject base;
    ObjcId domain;
    std::intptr_t code;
    ObjcId user_info;
};

struct NSURLObj {
    ObjcObject base;
    std::string* raw_str;
    std::string* scheme;
    std::string* host;
    std::string* path;
    bool is_file_url;
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

// Clang 13+ constant Objective-C literal layouts in __DATA,__objc_* sections
struct DarwinConstIntNumber {
    ObjcObject base;
    const char* encoding;
    std::int64_t value;
};

struct DarwinConstDoubleNumber {
    ObjcObject base;
    double value;
};

struct DarwinConstFloatNumber {
    ObjcObject base;
    float value;
};

struct DarwinConstArray {
    ObjcObject base;
    std::size_t count;
    const ObjcId* objects;
};

struct DarwinConstDict {
    ObjcObject base;
    std::size_t options;
    std::size_t count;
    const ObjcId* keys;
    const ObjcId* objects;
};

ObjcClass* g_cls_nsobject = nullptr;
ObjcClass* g_cls_nsstring = nullptr;
ObjcClass* g_cls_nsmutablestring = nullptr;
ObjcClass* g_cls_nsconststring = nullptr;
ObjcClass* g_cls_nsarray = nullptr;
ObjcClass* g_cls_nsmutablearray = nullptr;
ObjcClass* g_cls_nsconstarray = nullptr;
ObjcClass* g_cls_nsdict = nullptr;
ObjcClass* g_cls_nsmutabledict = nullptr;
ObjcClass* g_cls_nsconstdict = nullptr;
ObjcClass* g_cls_nsnumber = nullptr;
ObjcClass* g_cls_nsconstint = nullptr;
ObjcClass* g_cls_nsconstdouble = nullptr;
ObjcClass* g_cls_nsconstfloat = nullptr;
ObjcClass* g_cls_nsdata = nullptr;
ObjcClass* g_cls_nsmutabledata = nullptr;
ObjcClass* g_cls_nsdate = nullptr;
ObjcClass* g_cls_nsattributedstring = nullptr;
ObjcClass* g_cls_nsmutableattributedstring = nullptr;
ObjcClass* g_cls_nsdateformatter = nullptr;
ObjcClass* g_cls_nscalendar = nullptr;
ObjcClass* g_cls_nsdatecomponents = nullptr;
ObjcClass* g_cls_nserror = nullptr;
ObjcClass* g_cls_nsnull = nullptr;
ObjcClass* g_cls_nsjson = nullptr;
ObjcClass* g_cls_nsurl = nullptr;
ObjcClass* g_cls_nsbundle = nullptr;
ObjcClass* g_cls_nsprocessinfo = nullptr;
ObjcClass* g_cls_nsfilemanager = nullptr;
ObjcClass* g_cls_nsuserdefaults = nullptr;
ObjcClass* g_cls_nsnotification = nullptr;
ObjcClass* g_cls_nsnotificationcenter = nullptr;
ObjcClass* g_cls_nsrunloop = nullptr;
ObjcClass* g_cls_nsautoreleasepool = nullptr;

std::string g_main_bundle_path;
std::string g_main_bundle_id;
std::vector<std::string> g_process_args;

ObjcId g_main_bundle_obj = nullptr;
ObjcId g_process_info_obj = nullptr;
ObjcId g_file_manager_obj = nullptr;
ObjcId g_user_defaults_obj = nullptr;
ObjcId g_notification_center_obj = nullptr;
ObjcId g_null_singleton = nullptr;
ObjcId g_main_runloop_obj = nullptr;
// 16-byte zero prefix ensures objc_runtime::heap_header_of() reads valid memory
// and treats static CFBoolean singletons as immortal under AddressSanitizer.
struct ImmortalNSNumberStorage {
    std::uint64_t non_heap_prefix[2];
    NSNumberObj obj;
};

ImmortalNSNumberStorage g_cf_bool_true_storage{};
ImmortalNSNumberStorage g_cf_bool_false_storage{};
ObjcId g_cf_bool_true = nullptr;
ObjcId g_cf_bool_false = nullptr;

// Exported Foundation string constant pointers
ObjcId g_const_cocoa_error_domain = nullptr;
ObjcId g_const_posix_error_domain = nullptr;
ObjcId g_const_osstatus_error_domain = nullptr;
ObjcId g_const_localized_desc_key = nullptr;
ObjcId g_const_underlying_error_key = nullptr;
ObjcId g_const_default_runloop_mode = nullptr;
ObjcId g_const_common_runloop_modes = nullptr;
ObjcId g_const_nsfont_attribute = nullptr;
ObjcId g_const_nsforeground_attribute = nullptr;
ObjcId g_const_nsbackground_attribute = nullptr;
ObjcId g_const_nsunderline_attribute = nullptr;
ObjcId g_const_nscalendar_gregorian = nullptr;
ObjcId g_gregorian_calendar = nullptr;

void* g_cf_allocator_default = nullptr;
std::uintptr_t g_cf_array_callbacks[6] = {};
std::uintptr_t g_cf_dict_key_callbacks[6] = {};
std::uintptr_t g_cf_dict_val_callbacks[6] = {};

std::map<std::string, ObjcId> g_defaults_store;
std::string g_loaded_defaults_path;
std::vector<NotificationObserver> g_observers;

// Placeholder global for Mach-O __DATA,__cfstring constant strings
ObjcClass g_cf_const_string_placeholder;

// Forward declarations for internal helpers
ObjcId make_nsstring(std::string_view text, bool mut = false);
ObjcId make_nsarray(bool mut = false);
ObjcId make_nsdict(bool mut = false);
ObjcId make_nsnumber_int(std::int64_t v);
ObjcId make_nsnumber_bool(bool v);
ObjcId make_nsnumber_double(double v);
ObjcId nsnull_null(ObjcId, ObjcSel);
bool is_nsstring_obj(ObjcId obj);
void nsarray_addObject(ObjcId self, ObjcSel, ObjcId item);
void nsdict_setObjectForKey(ObjcId self, ObjcSel, ObjcId val, ObjcId key);
ObjcId nsdict_objectForKey(ObjcId self, ObjcSel, ObjcId key);
ObjcId json_parse_bytes(std::string_view input, bool mutable_containers, std::string* err_out);
bool json_serialize_object(ObjcId obj, bool pretty, int indent, std::string* out, std::string* err_out);

double now_epoch_seconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

std::string sanitize_bundle_slug(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '-' || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out.empty() ? "default" : out;
}

bool is_nsstring_obj(ObjcId obj) {
    if (!obj) return false;
    return obj->isa == g_cls_nsstring || obj->isa == g_cls_nsmutablestring ||
           obj->isa == g_cls_nsconststring;
}

bool is_nsarray_obj(ObjcId obj) {
    if (!obj) return false;
    return obj->isa == g_cls_nsarray || obj->isa == g_cls_nsmutablearray ||
           obj->isa == g_cls_nsconstarray;
}

bool is_nsdict_obj(ObjcId obj) {
    if (!obj) return false;
    return obj->isa == g_cls_nsdict || obj->isa == g_cls_nsmutabledict ||
           obj->isa == g_cls_nsconstdict;
}

bool is_nsnumber_obj(ObjcId obj) {
    if (!obj) return false;
    return obj->isa == g_cls_nsnumber || obj->isa == g_cls_nsconstint ||
           obj->isa == g_cls_nsconstdouble || obj->isa == g_cls_nsconstfloat;
}

bool read_nsnumber(ObjcId obj, std::int64_t* ival, double* dval,
                   bool* is_float, bool* is_bool) {
    if (!obj) return false;
    if (obj->isa == g_cls_nsnumber) {
        const auto* n = reinterpret_cast<const NSNumberObj*>(obj);
        if (ival) *ival = n->ival;
        if (dval) *dval = n->dval;
        if (is_float) *is_float = n->is_float;
        if (is_bool) *is_bool = n->is_bool;
        return true;
    }
    if (obj->isa == g_cls_nsconstint) {
        const auto* c = reinterpret_cast<const DarwinConstIntNumber*>(obj);
        if (ival) *ival = c->value;
        if (dval) *dval = static_cast<double>(c->value);
        if (is_float) *is_float = false;
        if (is_bool) *is_bool = false;
        return true;
    }
    if (obj->isa == g_cls_nsconstdouble) {
        const auto* c = reinterpret_cast<const DarwinConstDoubleNumber*>(obj);
        if (ival) *ival = static_cast<std::int64_t>(c->value);
        if (dval) *dval = c->value;
        if (is_float) *is_float = true;
        if (is_bool) *is_bool = false;
        return true;
    }
    if (obj->isa == g_cls_nsconstfloat) {
        const auto* c = reinterpret_cast<const DarwinConstFloatNumber*>(obj);
        if (ival) *ival = static_cast<std::int64_t>(c->value);
        if (dval) *dval = static_cast<double>(c->value);
        if (is_float) *is_float = true;
        if (is_bool) *is_bool = false;
        return true;
    }
    return false;
}

bool is_nsdata_obj(ObjcId obj) {
    if (!obj) return false;
    return obj->isa == g_cls_nsdata || obj->isa == g_cls_nsmutabledata;
}

void set_out_error(ObjcId* err_out, std::string_view domain, std::intptr_t code,
                   std::string_view desc) {
    if (!err_out) return;
    *err_out = finn_objc_autorelease(nserror_create(domain, code, desc));
}

bool write_bytes_to_file(const fs::path& dst, const void* data, std::size_t len, bool atomic,
                         std::string* err_msg) {
    std::error_code ec;
    if (dst.has_parent_path()) {
        fs::create_directories(dst.parent_path(), ec);
    }
    const fs::path target =
        atomic ? fs::path(dst.string() + ".tmp." + std::to_string(::getpid())) : dst;
    {
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (err_msg) *err_msg = "failed to open file for writing: " + dst.string();
            return false;
        }
        if (len > 0 && data) {
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
        }
        if (!out) {
            if (err_msg) *err_msg = "failed to write file: " + dst.string();
            return false;
        }
    }
    if (atomic) {
        fs::rename(target, dst, ec);
        if (ec) {
            fs::remove(target, ec);
            if (err_msg) *err_msg = "failed to atomically replace file: " + dst.string();
            return false;
        }
    }
    return true;
}

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
            } else if (is_nsstring_obj(arg)) {
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
ObjcId nsobject_copy(ObjcId self, ObjcSel) { return finn_objc_retain(self); }
void nsobject_dealloc(ObjcId, ObjcSel) {}
ObjcClass* nsobject_class(ObjcId self, ObjcSel) { return finn_object_getClass(self); }
ObjcClass* nsobject_superclass(ObjcId self, ObjcSel) {
    ObjcClass* c = finn_object_getClass(self);
    return c ? c->superclass : nullptr;
}
signed char nsobject_isKindOfClass(ObjcId self, ObjcSel, ObjcClass* cls) {
    return finn_objc_isKindOfClass(self, cls) ? 1 : 0;
}
signed char nsobject_respondsToSelector(ObjcId self, ObjcSel, ObjcSel sel) {
    return finn_objc_respondsToSelector(self, sel) ? 1 : 0;
}
ObjcId nsobject_performSelector(ObjcId self, ObjcSel, ObjcSel sel) {
    return objc_send<ObjcId>(self, sel);
}
ObjcId nsobject_performSelectorWithObject(ObjcId self, ObjcSel, ObjcSel sel, ObjcId arg) {
    return objc_send<ObjcId>(self, sel, arg);
}
signed char nsobject_isEqual(ObjcId self, ObjcSel, ObjcId other) { return self == other ? 1 : 0; }
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

ObjcId make_nsstring(std::string_view text, bool mut) {
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

ObjcId nsstring_stringWithUTF8(ObjcId self, ObjcSel, const char* cstr) {
    const bool mut = (reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutablestring);
    return make_nsstring(cstr ? cstr : "", mut);
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

ObjcId nsstring_initWithDataEncoding(ObjcId self, ObjcSel, ObjcId data, std::uintptr_t) {
    auto* s = reinterpret_cast<NSStringObj*>(self);
    delete s->value;
    if (is_nsdata_obj(data)) {
        const auto* d = reinterpret_cast<const NSDataObj*>(data);
        if (d->bytes) {
            s->value = new std::string(reinterpret_cast<const char*>(d->bytes->data()),
                                       d->bytes->size());
        } else {
            s->value = new std::string();
        }
    } else {
        s->value = new std::string();
    }
    return self;
}

ObjcId nsstring_dataUsingEncoding(ObjcId self, ObjcSel, std::uintptr_t) {
    const std::string text = nsstring_to_utf8(self);
    return nsdata_from_bytes(text.data(), text.size(), false);
}

ObjcId nsstring_stringWithContentsOfFile(ObjcId self, ObjcSel, ObjcId path_obj,
                                         std::uintptr_t, ObjcId* err_out) {
    const std::string path = nsstring_to_utf8(path_obj);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_out_error(err_out, "NSCocoaErrorDomain", 260,
                      "The file \"" + path + "\" could not be opened.");
        return nullptr;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const bool mut = (reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutablestring);
    return make_nsstring(ss.str(), mut);
}

ObjcId nsstring_initWithContentsOfFile(ObjcId self, ObjcSel, ObjcId path_obj,
                                       std::uintptr_t, ObjcId* err_out) {
    const std::string path = nsstring_to_utf8(path_obj);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_out_error(err_out, "NSCocoaErrorDomain", 260,
                      "The file \"" + path + "\" could not be opened.");
        finn_objc_release(self);
        return nullptr;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto* s = reinterpret_cast<NSStringObj*>(self);
    delete s->value;
    s->value = new std::string(ss.str());
    return self;
}

signed char nsstring_writeToFile(ObjcId self, ObjcSel, ObjcId path_obj, signed char atomically,
                                 std::uintptr_t, ObjcId* err_out) {
    const std::string path = nsstring_to_utf8(path_obj);
    const std::string content = nsstring_to_utf8(self);
    std::string err_msg;
    if (!write_bytes_to_file(fs::path(path), content.data(), content.size(), atomically != 0,
                             &err_msg)) {
        set_out_error(err_out, "NSCocoaErrorDomain", 512, err_msg);
        return 0;
    }
    return 1;
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

signed char nsstring_isEqualToString(ObjcId self, ObjcSel, ObjcId other) {
    if (!other || !is_nsstring_obj(other)) return 0;
    return nsstring_to_utf8(self) == nsstring_to_utf8(other) ? 1 : 0;
}

signed char nsstring_hasPrefix(ObjcId self, ObjcSel, ObjcId prefix) {
    const std::string a = nsstring_to_utf8(self);
    const std::string b = nsstring_to_utf8(prefix);
    return a.rfind(b, 0) == 0 ? 1 : 0;
}

signed char nsstring_hasSuffix(ObjcId self, ObjcSel, ObjcId suffix) {
    const std::string a = nsstring_to_utf8(self);
    const std::string b = nsstring_to_utf8(suffix);
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0 ? 1 : 0;
}

signed char nsstring_containsString(ObjcId self, ObjcSel, ObjcId sub) {
    const std::string a = nsstring_to_utf8(self);
    const std::string b = nsstring_to_utf8(sub);
    if (b.empty()) return 0;
    return a.find(b) != std::string::npos ? 1 : 0;
}

DarwinRange nsstring_rangeOfString(ObjcId self, ObjcSel, ObjcId sub) {
    const std::string a = nsstring_to_utf8(self);
    const std::string b = nsstring_to_utf8(sub);
    if (b.empty()) return {static_cast<std::size_t>(-1), 0};
    const std::size_t pos = a.find(b);
    if (pos == std::string::npos) return {static_cast<std::size_t>(-1), 0};
    return {pos, b.size()};
}

ObjcId nsstring_byAppendingString(ObjcId self, ObjcSel, ObjcId other) {
    return make_nsstring(nsstring_to_utf8(self) + nsstring_to_utf8(other));
}

ObjcId nsstring_byAppendingPathComponent(ObjcId self, ObjcSel, ObjcId comp) {
    const fs::path p = fs::path(nsstring_to_utf8(self)) / nsstring_to_utf8(comp);
    return make_nsstring(p.string());
}

ObjcId nsstring_byDeletingLastPathComponent(ObjcId self, ObjcSel) {
    std::string raw = nsstring_to_utf8(self);
    while (raw.size() > 1 && raw.back() == '/') raw.pop_back();
    if (raw == "/") return make_nsstring("/");
    const fs::path parent = fs::path(raw).parent_path();
    return make_nsstring(parent.string());
}

ObjcId nsstring_byDeletingPathExtension(ObjcId self, ObjcSel) {
    fs::path p(nsstring_to_utf8(self));
    p.replace_extension("");
    return make_nsstring(p.string());
}

ObjcId nsstring_byAppendingPathExtension(ObjcId self, ObjcSel, ObjcId ext) {
    const std::string base = nsstring_to_utf8(self);
    const std::string e = nsstring_to_utf8(ext);
    if (e.empty()) return make_nsstring(base);
    return make_nsstring(base + "." + e);
}

ObjcId nsstring_lastPathComponent(ObjcId self, ObjcSel) {
    std::string raw = nsstring_to_utf8(self);
    while (raw.size() > 1 && raw.back() == '/') raw.pop_back();
    if (raw == "/") return make_nsstring("/");
    return make_nsstring(fs::path(raw).filename().string());
}

ObjcId nsstring_pathExtension(ObjcId self, ObjcSel) {
    std::string ext = fs::path(nsstring_to_utf8(self)).extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    return make_nsstring(ext);
}

ObjcId nsstring_componentsSeparatedByString(ObjcId self, ObjcSel, ObjcId sep_obj) {
    const std::string s = nsstring_to_utf8(self);
    const std::string sep = nsstring_to_utf8(sep_obj);
    ObjcId arr = make_nsarray(false);
    if (sep.empty()) {
        ObjcId item = make_nsstring(s);
        nsarray_addObject(arr, nullptr, item);
        finn_objc_release(item);
        return arr;
    }
    std::size_t pos = 0;
    while (true) {
        const std::size_t hit = s.find(sep, pos);
        if (hit == std::string::npos) {
            ObjcId part = make_nsstring(s.substr(pos));
            nsarray_addObject(arr, nullptr, part);
            finn_objc_release(part);
            break;
        }
        ObjcId part = make_nsstring(s.substr(pos, hit - pos));
        nsarray_addObject(arr, nullptr, part);
        finn_objc_release(part);
        pos = hit + sep.size();
    }
    return arr;
}

ObjcId nsstring_replacingOccurrences(ObjcId self, ObjcSel, ObjcId target_obj, ObjcId repl_obj) {
    std::string s = nsstring_to_utf8(self);
    const std::string target = nsstring_to_utf8(target_obj);
    const std::string repl = nsstring_to_utf8(repl_obj);
    if (target.empty()) return make_nsstring(s);
    std::string out;
    std::size_t pos = 0;
    while (true) {
        const std::size_t hit = s.find(target, pos);
        if (hit == std::string::npos) {
            out.append(s, pos, std::string::npos);
            break;
        }
        out.append(s, pos, hit - pos);
        out.append(repl);
        pos = hit + target.size();
    }
    return make_nsstring(out);
}

ObjcId nsstring_substringFromIndex(ObjcId self, ObjcSel, std::size_t idx) {
    const std::string s = nsstring_to_utf8(self);
    if (idx >= s.size()) return make_nsstring("");
    return make_nsstring(s.substr(idx));
}

ObjcId nsstring_substringToIndex(ObjcId self, ObjcSel, std::size_t idx) {
    const std::string s = nsstring_to_utf8(self);
    return make_nsstring(s.substr(0, std::min(idx, s.size())));
}

ObjcId nsstring_substringWithRange(ObjcId self, ObjcSel, DarwinRange range) {
    const std::string s = nsstring_to_utf8(self);
    if (range.location >= s.size()) return make_nsstring("");
    return make_nsstring(s.substr(range.location, range.length));
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

float nsstring_floatValue(ObjcId self, ObjcSel) {
    return std::strtof(nsstring_to_utf8(self).c_str(), nullptr);
}

signed char nsstring_boolValue(ObjcId self, ObjcSel) {
    const std::string s = nsstring_to_utf8(self);
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '+' || c == '-') continue;
        if (c == '0') continue;
        return (c == 'Y' || c == 'y' || c == 'T' || c == 't' || (c >= '1' && c <= '9')) ? 1 : 0;
    }
    return 0;
}

ObjcId nsstring_mutableCopy(ObjcId self, ObjcSel) {
    return make_nsstring(nsstring_to_utf8(self), true);
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

ObjcId make_nsarray(bool mut) {
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
    if (!self) return 0;
    if (self->isa == g_cls_nsconstarray) {
        return reinterpret_cast<const DarwinConstArray*>(self)->count;
    }
    const auto* a = reinterpret_cast<const NSArrayObj*>(self);
    return a->items ? a->items->size() : 0;
}

ObjcId nsarray_objectAtIndex(ObjcId self, ObjcSel, std::size_t idx) {
    if (!self) return nullptr;
    if (self->isa == g_cls_nsconstarray) {
        const auto* c = reinterpret_cast<const DarwinConstArray*>(self);
        return (c->objects && idx < c->count) ? c->objects[idx] : nullptr;
    }
    const auto* a = reinterpret_cast<const NSArrayObj*>(self);
    if (!a->items || idx >= a->items->size()) return nullptr;
    return (*a->items)[idx];
}

ObjcId nsarray_firstObject(ObjcId self, ObjcSel) {
    return nsarray_count(self, nullptr) > 0 ? nsarray_objectAtIndex(self, nullptr, 0) : nullptr;
}

ObjcId nsarray_lastObject(ObjcId self, ObjcSel) {
    const std::size_t cnt = nsarray_count(self, nullptr);
    return cnt > 0 ? nsarray_objectAtIndex(self, nullptr, cnt - 1) : nullptr;
}

signed char nsarray_containsObject(ObjcId self, ObjcSel, ObjcId target) {
    if (!self || !target) return 0;
    const std::size_t cnt = nsarray_count(self, nullptr);
    for (std::size_t i = 0; i < cnt; ++i) {
        ObjcId item = nsarray_objectAtIndex(self, nullptr, i);
        if (item == target || objc_send_name<signed char>(item, "isEqual:", target) != 0) {
            return 1;
        }
    }
    return 0;
}

void nsarray_addObject(ObjcId self, ObjcSel, ObjcId item) {
    auto* a = reinterpret_cast<NSArrayObj*>(self);
    if (!a->items) a->items = new std::vector<ObjcId>();
    if (item) a->items->push_back(finn_objc_retain(item));
}

void nsarray_removeLastObject(ObjcId self, ObjcSel) {
    auto* a = reinterpret_cast<NSArrayObj*>(self);
    if (!a->items || a->items->empty()) return;
    finn_objc_release(a->items->back());
    a->items->pop_back();
}

void nsarray_removeObjectAtIndex(ObjcId self, ObjcSel, std::size_t idx) {
    auto* a = reinterpret_cast<NSArrayObj*>(self);
    if (!a->items || idx >= a->items->size()) return;
    finn_objc_release((*a->items)[idx]);
    a->items->erase(a->items->begin() + static_cast<std::ptrdiff_t>(idx));
}

void nsarray_removeAllObjects(ObjcId self, ObjcSel) {
    auto* a = reinterpret_cast<NSArrayObj*>(self);
    if (!a->items) return;
    for (ObjcId item : *a->items) finn_objc_release(item);
    a->items->clear();
}

ObjcId nsarray_mutableCopy(ObjcId self, ObjcSel) {
    ObjcId out = make_nsarray(true);
    const std::size_t cnt = nsarray_count(self, nullptr);
    for (std::size_t i = 0; i < cnt; ++i) {
        nsarray_addObject(out, nullptr, nsarray_objectAtIndex(self, nullptr, i));
    }
    return out;
}

ObjcId nsarray_componentsJoinedByString(ObjcId self, ObjcSel, ObjcId sep) {
    const std::string glue = nsstring_to_utf8(sep);
    std::string out;
    const std::size_t cnt = nsarray_count(self, nullptr);
    for (std::size_t i = 0; i < cnt; ++i) {
        if (i > 0) out += glue;
        ObjcId desc = objc_send_name<ObjcId>(nsarray_objectAtIndex(self, nullptr, i), "description");
        out += nsstring_to_utf8(desc);
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

ObjcId make_nsdict(bool mut) {
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
        const std::string k = nsstring_to_utf8(keys[i]);
        if (auto it = d->map->find(k); it != d->map->end()) {
            finn_objc_release(it->second);
        }
        (*d->map)[k] = finn_objc_retain(vals[i]);
    }
    return dict;
}

std::size_t nsdict_count(ObjcId self, ObjcSel) {
    if (!self) return 0;
    if (self->isa == g_cls_nsconstdict) {
        return reinterpret_cast<const DarwinConstDict*>(self)->count;
    }
    const auto* d = reinterpret_cast<const NSDictObj*>(self);
    return d->map ? d->map->size() : 0;
}

ObjcId nsdict_objectForKey(ObjcId self, ObjcSel, ObjcId key) {
    if (!self || !key) return nullptr;
    const std::string want = nsstring_to_utf8(key);
    if (self->isa == g_cls_nsconstdict) {
        const auto* c = reinterpret_cast<const DarwinConstDict*>(self);
        if (!c->keys || !c->objects) return nullptr;
        for (std::size_t i = 0; i < c->count; ++i) {
            if (nsstring_to_utf8(c->keys[i]) == want) return c->objects[i];
        }
        return nullptr;
    }
    const auto* d = reinterpret_cast<const NSDictObj*>(self);
    if (!d->map) return nullptr;
    auto it = d->map->find(want);
    return (it == d->map->end()) ? nullptr : it->second;
}

ObjcId nsdict_allKeys(ObjcId self, ObjcSel) {
    ObjcId arr = make_nsarray(false);
    if (!self) return arr;
    if (self->isa == g_cls_nsconstdict) {
        const auto* c = reinterpret_cast<const DarwinConstDict*>(self);
        if (c->keys) {
            for (std::size_t i = 0; i < c->count; ++i) nsarray_addObject(arr, nullptr, c->keys[i]);
        }
        return arr;
    }
    const auto* d = reinterpret_cast<const NSDictObj*>(self);
    if (d->map) {
        for (const auto& [k, _] : *d->map) {
            ObjcId ks = make_nsstring(k);
            nsarray_addObject(arr, nullptr, ks);
            finn_objc_release(ks);
        }
    }
    return arr;
}

ObjcId nsdict_allValues(ObjcId self, ObjcSel) {
    ObjcId arr = make_nsarray(false);
    if (!self) return arr;
    if (self->isa == g_cls_nsconstdict) {
        const auto* c = reinterpret_cast<const DarwinConstDict*>(self);
        if (c->objects) {
            for (std::size_t i = 0; i < c->count; ++i) nsarray_addObject(arr, nullptr, c->objects[i]);
        }
        return arr;
    }
    const auto* d = reinterpret_cast<const NSDictObj*>(self);
    if (d->map) {
        for (const auto& [_, v] : *d->map) {
            nsarray_addObject(arr, nullptr, v);
        }
    }
    return arr;
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

void nsdict_removeObjectForKey(ObjcId self, ObjcSel, ObjcId key) {
    auto* d = reinterpret_cast<NSDictObj*>(self);
    if (!d || !d->map || !key) return;
    const std::string k = nsstring_to_utf8(key);
    if (auto it = d->map->find(k); it != d->map->end()) {
        finn_objc_release(it->second);
        d->map->erase(it);
    }
}

void nsdict_removeAllObjects(ObjcId self, ObjcSel) {
    auto* d = reinterpret_cast<NSDictObj*>(self);
    if (!d || !d->map) return;
    for (auto& [_, v] : *d->map) finn_objc_release(v);
    d->map->clear();
}

void nsdict_setObjectForKeyedSubscript(ObjcId self, ObjcSel sel, ObjcId val, ObjcId key) {
    nsdict_setObjectForKey(self, sel, val, key);
}

ObjcId nsdict_mutableCopy(ObjcId self, ObjcSel) {
    ObjcId out = make_nsdict(true);
    if (!self) return out;
    auto* out_d = reinterpret_cast<NSDictObj*>(out);
    if (self->isa == g_cls_nsconstdict) {
        const auto* c = reinterpret_cast<const DarwinConstDict*>(self);
        if (c->keys && c->objects) {
            for (std::size_t i = 0; i < c->count; ++i) {
                (*out_d->map)[nsstring_to_utf8(c->keys[i])] = finn_objc_retain(c->objects[i]);
            }
        }
        return out;
    }
    const auto* d = reinterpret_cast<const NSDictObj*>(self);
    if (d->map) {
        for (const auto& [k, v] : *d->map) {
            (*out_d->map)[k] = finn_objc_retain(v);
        }
    }
    return out;
}

// --- NSNumber ---
ObjcId make_nsnumber_int(std::int64_t v) {
    auto* n = reinterpret_cast<NSNumberObj*>(
        finn_objc_alloc_object(g_cls_nsnumber, sizeof(NSNumberObj)));
    n->ival = v;
    n->dval = static_cast<double>(v);
    n->is_float = false;
    n->is_bool = false;
    return reinterpret_cast<ObjcId>(n);
}

ObjcId make_nsnumber_bool(bool v) {
    auto* n = reinterpret_cast<NSNumberObj*>(
        finn_objc_alloc_object(g_cls_nsnumber, sizeof(NSNumberObj)));
    n->ival = v ? 1 : 0;
    n->dval = v ? 1.0 : 0.0;
    n->is_float = false;
    n->is_bool = true;
    return reinterpret_cast<ObjcId>(n);
}

ObjcId make_nsnumber_double(double v) {
    auto* n = reinterpret_cast<NSNumberObj*>(
        finn_objc_alloc_object(g_cls_nsnumber, sizeof(NSNumberObj)));
    n->dval = v;
    n->ival = static_cast<std::int64_t>(v);
    n->is_float = true;
    n->is_bool = false;
    return reinterpret_cast<ObjcId>(n);
}

ObjcId nsnumber_withInt(ObjcId, ObjcSel, int v) { return make_nsnumber_int(v); }
ObjcId nsnumber_withBool(ObjcId, ObjcSel, signed char v) { return make_nsnumber_bool(v != 0); }
ObjcId nsnumber_withInteger(ObjcId, ObjcSel, std::intptr_t v) { return make_nsnumber_int(v); }
ObjcId nsnumber_withDouble(ObjcId, ObjcSel, double v) { return make_nsnumber_double(v); }
ObjcId nsnumber_withFloat(ObjcId, ObjcSel, float v) { return make_nsnumber_double(v); }

int nsnumber_intValue(ObjcId self, ObjcSel) {
    std::int64_t iv = 0;
    (void)read_nsnumber(self, &iv, nullptr, nullptr, nullptr);
    return static_cast<int>(iv);
}
signed char nsnumber_boolValue(ObjcId self, ObjcSel) {
    std::int64_t iv = 0;
    double dv = 0.0;
    bool flt = false;
    if (!read_nsnumber(self, &iv, &dv, &flt, nullptr)) return 0;
    return (flt ? (dv != 0.0) : (iv != 0)) ? 1 : 0;
}
std::intptr_t nsnumber_integerValue(ObjcId self, ObjcSel) {
    std::int64_t iv = 0;
    (void)read_nsnumber(self, &iv, nullptr, nullptr, nullptr);
    return static_cast<std::intptr_t>(iv);
}
long long nsnumber_longLongValue(ObjcId self, ObjcSel) {
    std::int64_t iv = 0;
    (void)read_nsnumber(self, &iv, nullptr, nullptr, nullptr);
    return static_cast<long long>(iv);
}
double nsnumber_doubleValue(ObjcId self, ObjcSel) {
    double dv = 0.0;
    (void)read_nsnumber(self, nullptr, &dv, nullptr, nullptr);
    return dv;
}
float nsnumber_floatValue(ObjcId self, ObjcSel) {
    double dv = 0.0;
    (void)read_nsnumber(self, nullptr, &dv, nullptr, nullptr);
    return static_cast<float>(dv);
}
signed char nsnumber_isEqual(ObjcId self, ObjcSel, ObjcId other) {
    std::int64_t aiv = 0, biv = 0;
    double adv = 0.0, bdv = 0.0;
    bool aflt = false, bflt = false;
    if (!read_nsnumber(self, &aiv, &adv, &aflt, nullptr) ||
        !read_nsnumber(other, &biv, &bdv, &bflt, nullptr)) {
        return 0;
    }
    if (aflt || bflt) return adv == bdv ? 1 : 0;
    return aiv == biv ? 1 : 0;
}
ObjcId nsnumber_stringValue(ObjcId self, ObjcSel) {
    std::int64_t iv = 0;
    double dv = 0.0;
    bool flt = false;
    (void)read_nsnumber(self, &iv, &dv, &flt, nullptr);
    char buf[64] = {};
    if (flt) std::snprintf(buf, sizeof(buf), "%.15g", dv);
    else std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(iv));
    return make_nsstring(buf);
}

// --- NSNull ---
ObjcId nsnull_null(ObjcId, ObjcSel) {
    if (!g_null_singleton) {
        g_null_singleton = finn_objc_alloc_object(g_cls_nsnull);
    }
    return g_null_singleton;
}
ObjcId nsnull_description(ObjcId, ObjcSel) {
    return make_nsstring("<null>");
}

// --- NSData & NSMutableData ---
void nsdata_dtor(ObjcId obj) {
    auto* d = reinterpret_cast<NSDataObj*>(obj);
    delete d->bytes;
    d->bytes = nullptr;
}

ObjcId nsdata_init(ObjcId self, ObjcSel) {
    auto* d = reinterpret_cast<NSDataObj*>(self);
    if (!d->bytes) d->bytes = new std::vector<std::uint8_t>();
    return self;
}

ObjcId nsdata_data(ObjcId self, ObjcSel) {
    const bool mut = (reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutabledata);
    return nsdata_from_bytes(nullptr, 0, mut);
}

ObjcId nsdata_withBytesLength(ObjcId self, ObjcSel, const void* ptr, std::size_t len) {
    const bool mut = (reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutabledata);
    return nsdata_from_bytes(ptr, len, mut);
}

ObjcId nsdata_initWithBytesLength(ObjcId self, ObjcSel, const void* ptr, std::size_t len) {
    auto* d = reinterpret_cast<NSDataObj*>(self);
    delete d->bytes;
    if (ptr && len > 0) {
        d->bytes = new std::vector<std::uint8_t>(
            static_cast<const std::uint8_t*>(ptr),
            static_cast<const std::uint8_t*>(ptr) + len);
    } else {
        d->bytes = new std::vector<std::uint8_t>();
    }
    return self;
}

ObjcId nsdata_withCapacity(ObjcId, ObjcSel, std::size_t cap) {
    auto* d = reinterpret_cast<NSDataObj*>(
        finn_objc_alloc_object(g_cls_nsmutabledata, sizeof(NSDataObj)));
    d->bytes = new std::vector<std::uint8_t>();
    d->bytes->reserve(cap);
    return reinterpret_cast<ObjcId>(d);
}

ObjcId nsdata_initWithCapacity(ObjcId self, ObjcSel, std::size_t cap) {
    auto* d = reinterpret_cast<NSDataObj*>(self);
    if (!d->bytes) d->bytes = new std::vector<std::uint8_t>();
    d->bytes->reserve(cap);
    return self;
}

ObjcId nsdata_withLength(ObjcId, ObjcSel, std::size_t len) {
    auto* d = reinterpret_cast<NSDataObj*>(
        finn_objc_alloc_object(g_cls_nsmutabledata, sizeof(NSDataObj)));
    d->bytes = new std::vector<std::uint8_t>(len, 0);
    return reinterpret_cast<ObjcId>(d);
}

ObjcId nsdata_dataWithContentsOfFile(ObjcId self, ObjcSel, ObjcId path_obj) {
    const std::string path = nsstring_to_utf8(path_obj);
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return nullptr;
    const std::streamoff sz = in.tellg();
    if (sz < 0) return nullptr;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    in.seekg(0, std::ios::beg);
    if (sz > 0) in.read(reinterpret_cast<char*>(buf.data()), sz);
    const bool mut = (reinterpret_cast<ObjcClass*>(self) == g_cls_nsmutabledata);
    return nsdata_from_bytes(buf.data(), buf.size(), mut);
}

ObjcId nsdata_dataWithContentsOfFileOptsErr(ObjcId self, ObjcSel, ObjcId path_obj,
                                            std::uintptr_t, ObjcId* err_out) {
    ObjcId res = nsdata_dataWithContentsOfFile(self, nullptr, path_obj);
    if (!res) {
        set_out_error(err_out, "NSCocoaErrorDomain", 260,
                      "Unable to read file: " + nsstring_to_utf8(path_obj));
    }
    return res;
}

signed char nsdata_writeToFileAtomically(ObjcId self, ObjcSel, ObjcId path_obj,
                                         signed char atomically) {
    const auto* d = reinterpret_cast<const NSDataObj*>(self);
    const void* ptr = (d && d->bytes && !d->bytes->empty()) ? d->bytes->data() : nullptr;
    const std::size_t len = (d && d->bytes) ? d->bytes->size() : 0;
    return write_bytes_to_file(fs::path(nsstring_to_utf8(path_obj)), ptr, len,
                               atomically != 0, nullptr)
               ? 1
               : 0;
}

signed char nsdata_writeToFileOptsErr(ObjcId self, ObjcSel, ObjcId path_obj, std::uintptr_t opts,
                                      ObjcId* err_out) {
    const auto* d = reinterpret_cast<const NSDataObj*>(self);
    const void* ptr = (d && d->bytes && !d->bytes->empty()) ? d->bytes->data() : nullptr;
    const std::size_t len = (d && d->bytes) ? d->bytes->size() : 0;
    std::string err_msg;
    if (!write_bytes_to_file(fs::path(nsstring_to_utf8(path_obj)), ptr, len,
                             (opts & 1u) != 0, &err_msg)) {
        set_out_error(err_out, "NSCocoaErrorDomain", 512, err_msg);
        return 0;
    }
    return 1;
}

const void* nsdata_bytes(ObjcId self, ObjcSel) {
    const auto* d = reinterpret_cast<const NSDataObj*>(self);
    return (d && d->bytes && !d->bytes->empty()) ? d->bytes->data() : nullptr;
}

void* nsdata_mutableBytes(ObjcId self, ObjcSel) {
    auto* d = reinterpret_cast<NSDataObj*>(self);
    return (d && d->bytes && !d->bytes->empty()) ? d->bytes->data() : nullptr;
}

std::size_t nsdata_length(ObjcId self, ObjcSel) {
    const auto* d = reinterpret_cast<const NSDataObj*>(self);
    return (d && d->bytes) ? d->bytes->size() : 0;
}

void nsmutabledata_appendBytesLength(ObjcId self, ObjcSel, const void* ptr, std::size_t len) {
    auto* d = reinterpret_cast<NSDataObj*>(self);
    if (!d->bytes) d->bytes = new std::vector<std::uint8_t>();
    if (ptr && len > 0) {
        const auto* p = static_cast<const std::uint8_t*>(ptr);
        d->bytes->insert(d->bytes->end(), p, p + len);
    }
}

void nsmutabledata_appendData(ObjcId self, ObjcSel, ObjcId other) {
    if (!is_nsdata_obj(other)) return;
    const auto* od = reinterpret_cast<const NSDataObj*>(other);
    if (od && od->bytes && !od->bytes->empty()) {
        nsmutabledata_appendBytesLength(self, nullptr, od->bytes->data(), od->bytes->size());
    }
}

void nsmutabledata_setLength(ObjcId self, ObjcSel, std::size_t len) {
    auto* d = reinterpret_cast<NSDataObj*>(self);
    if (!d->bytes) d->bytes = new std::vector<std::uint8_t>();
    d->bytes->resize(len, 0);
}

signed char nsdata_isEqualToData(ObjcId self, ObjcSel, ObjcId other) {
    if (!is_nsdata_obj(other)) return 0;
    const auto* a = reinterpret_cast<const NSDataObj*>(self);
    const auto* b = reinterpret_cast<const NSDataObj*>(other);
    const std::vector<std::uint8_t> empty;
    const auto& va = a->bytes ? *a->bytes : empty;
    const auto& vb = b->bytes ? *b->bytes : empty;
    return va == vb ? 1 : 0;
}

// --- NSError ---
void nserror_dtor(ObjcId obj) {
    auto* e = reinterpret_cast<NSErrorObj*>(obj);
    finn_objc_release(e->domain);
    finn_objc_release(e->user_info);
    e->domain = nullptr;
    e->user_info = nullptr;
}

ObjcId nserror_initWithDomain(ObjcId self, ObjcSel, ObjcId domain, std::intptr_t code,
                              ObjcId user_info) {
    auto* e = reinterpret_cast<NSErrorObj*>(self);
    finn_objc_release(e->domain);
    finn_objc_release(e->user_info);
    e->domain = finn_objc_retain(domain);
    e->code = code;
    e->user_info = finn_objc_retain(user_info);
    return self;
}

ObjcId nserror_errorWithDomain(ObjcId, ObjcSel, ObjcId domain, std::intptr_t code,
                               ObjcId user_info) {
    ObjcId obj = finn_objc_alloc_object(g_cls_nserror, sizeof(NSErrorObj));
    return nserror_initWithDomain(obj, nullptr, domain, code, user_info);
}

ObjcId nserror_domain(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSErrorObj*>(self)->domain;
}

std::intptr_t nserror_code(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSErrorObj*>(self)->code;
}

ObjcId nserror_userInfo(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSErrorObj*>(self)->user_info;
}

ObjcId nserror_localizedDescription(ObjcId self, ObjcSel) {
    const auto* e = reinterpret_cast<const NSErrorObj*>(self);
    if (e->user_info && is_nsdict_obj(e->user_info)) {
        ObjcId k = make_nsstring("NSLocalizedDescription");
        ObjcId v = nsdict_objectForKey(e->user_info, nullptr, k);
        finn_objc_release(k);
        if (v && is_nsstring_obj(v)) return v;
    }
    const std::string dom = e->domain ? nsstring_to_utf8(e->domain) : "NSError";
    return make_nsstring(dom + " error " + std::to_string(e->code));
}

// --- NSDate ---
ObjcId nsdate_init(ObjcId self, ObjcSel) {
    reinterpret_cast<NSDateObj*>(self)->since_1970 = now_epoch_seconds();
    return self;
}

ObjcId nsdate_date(ObjcId, ObjcSel) {
    auto* d = reinterpret_cast<NSDateObj*>(finn_objc_alloc_object(g_cls_nsdate, sizeof(NSDateObj)));
    d->since_1970 = now_epoch_seconds();
    return reinterpret_cast<ObjcId>(d);
}

ObjcId nsdate_with1970(ObjcId, ObjcSel, double secs) {
    auto* d = reinterpret_cast<NSDateObj*>(finn_objc_alloc_object(g_cls_nsdate, sizeof(NSDateObj)));
    d->since_1970 = secs;
    return reinterpret_cast<ObjcId>(d);
}

ObjcId nsdate_withSinceNow(ObjcId, ObjcSel, double secs) {
    auto* d = reinterpret_cast<NSDateObj*>(finn_objc_alloc_object(g_cls_nsdate, sizeof(NSDateObj)));
    d->since_1970 = now_epoch_seconds() + secs;
    return reinterpret_cast<ObjcId>(d);
}

ObjcId nsdate_initWith1970(ObjcId self, ObjcSel, double secs) {
    reinterpret_cast<NSDateObj*>(self)->since_1970 = secs;
    return self;
}

ObjcId nsdate_initWithSinceNow(ObjcId self, ObjcSel, double secs) {
    reinterpret_cast<NSDateObj*>(self)->since_1970 = now_epoch_seconds() + secs;
    return self;
}

double nsdate_timeIntervalSince1970(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSDateObj*>(self)->since_1970;
}

double nsdate_timeIntervalSinceNow(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSDateObj*>(self)->since_1970 - now_epoch_seconds();
}

double nsdate_timeIntervalSinceDate(ObjcId self, ObjcSel, ObjcId other) {
    if (!other || other->isa != g_cls_nsdate) return 0.0;
    return reinterpret_cast<const NSDateObj*>(self)->since_1970 -
           reinterpret_cast<const NSDateObj*>(other)->since_1970;
}

ObjcId nsdate_description(ObjcId self, ObjcSel) {
    const auto t = static_cast<std::time_t>(reinterpret_cast<const NSDateObj*>(self)->since_1970);
    struct tm tm_utc{};
    ::gmtime_r(&t, &tm_utc);
    char buf[64] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S +0000", &tm_utc);
    return make_nsstring(buf);
}

// --- NSAttributedString / NSMutableAttributedString ---
// Ranges are UTF-8 byte offsets, matching NSString.length in this runtime.
// The string is the source of truth; attribute slots follow its size exactly.
static void attributed_dtor(ObjcId obj) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(obj);
    if (a->attributes) {
        for (ObjcId d : *a->attributes) finn_objc_release(d);
        delete a->attributes;
        a->attributes = nullptr;
    }
    delete a->text;
    a->text = nullptr;
}

static ObjcId attributed_alloc(bool mut) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(finn_objc_alloc_object(
        mut ? g_cls_nsmutableattributedstring : g_cls_nsattributedstring,
        sizeof(NSAttributedStringObj)));
    a->text = new std::string();
    a->attributes = new std::vector<ObjcId>();
    return reinterpret_cast<ObjcId>(a);
}

static DarwinRange attributed_clamp(const std::string& text, DarwinRange r) {
    DarwinRange out{};
    out.location = std::min(r.location, text.size());
    out.length = std::min(r.length, text.size() - out.location);
    return out;
}

static ObjcId attributed_init_with(ObjcId self, ObjcSel, ObjcId str, ObjcId attrs) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    if (!a->text) a->text = new std::string();
    if (!a->attributes) a->attributes = new std::vector<ObjcId>();
    *a->text = nsstring_to_utf8(str);
    for (ObjcId d : *a->attributes) finn_objc_release(d);
    a->attributes->assign(a->text->size(), nullptr);
    if (attrs && is_nsdict_obj(attrs) && nsdict_count(attrs, nullptr) > 0) {
        for (ObjcId& slot : *a->attributes) slot = finn_objc_retain(attrs);
    }
    return self;
}

static ObjcId attributed_init_string(ObjcId self, ObjcSel sel, ObjcId str) {
    return attributed_init_with(self, sel, str, nullptr);
}

static ObjcId attributed_string(ObjcId self, ObjcSel) {
    const auto* a = reinterpret_cast<const NSAttributedStringObj*>(self);
    return make_nsstring(a->text ? *a->text : std::string{});
}

static std::size_t attributed_length(ObjcId self, ObjcSel) {
    const auto* a = reinterpret_cast<const NSAttributedStringObj*>(self);
    return a->text ? a->text->size() : 0;
}

static bool attr_value_equal(ObjcId a, ObjcId b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (is_nsstring_obj(a) && is_nsstring_obj(b)) return nsstring_to_utf8(a) == nsstring_to_utf8(b);
    if (a->isa == g_cls_nsnumber && b->isa == g_cls_nsnumber) {
        const auto* na = reinterpret_cast<const NSNumberObj*>(a);
        const auto* nb = reinterpret_cast<const NSNumberObj*>(b);
        if (na->is_float != nb->is_float || na->is_bool != nb->is_bool) return false;
        return na->is_float ? (na->dval == nb->dval) : (na->ival == nb->ival);
    }
    return false;
}

static bool attr_dict_equal(ObjcId a, ObjcId b) {
    if (a == b) return true;
    if (!a || !b || !is_nsdict_obj(a) || !is_nsdict_obj(b)) return false;
    const std::size_t ca = nsdict_count(a, nullptr);
    if (ca != nsdict_count(b, nullptr)) return false;
    ObjcId keys = nsdict_allKeys(a, nullptr);
    bool equal = true;
    for (std::size_t i = 0; i < ca && equal; ++i) {
        ObjcId key = nsarray_objectAtIndex(keys, nullptr, i);
        equal = attr_value_equal(nsdict_objectForKey(a, nullptr, key),
                                 nsdict_objectForKey(b, nullptr, key));
    }
    finn_objc_release(keys);
    return equal;
}

static ObjcId attributed_attributes_at(ObjcId self, ObjcSel, std::size_t index,
                                        DarwinRange* effective) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    const std::size_t n = a->text->size();
    if (index >= n) {
        if (effective) *effective = {n, 0};
        return make_nsdict(false);
    }
    const ObjcId cur = (*a->attributes)[index];
    std::size_t start = index;
    std::size_t end = index + 1;
    while (start > 0 && attr_dict_equal((*a->attributes)[start - 1], cur)) --start;
    while (end < n && attr_dict_equal((*a->attributes)[end], cur)) ++end;
    if (effective) *effective = {start, end - start};
    return cur ? finn_objc_retain(cur) : make_nsdict(false);
}

static ObjcId attributed_substring(ObjcId self, ObjcSel, DarwinRange range) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    const DarwinRange r = attributed_clamp(*a->text, range);
    ObjcId sub = attributed_alloc(self->isa == g_cls_nsmutableattributedstring);
    auto* s = reinterpret_cast<NSAttributedStringObj*>(sub);
    *s->text = a->text->substr(r.location, r.length);
    s->attributes->assign(r.length, nullptr);
    for (std::size_t i = 0; i < r.length; ++i) {
        (*s->attributes)[i] = finn_objc_retain((*a->attributes)[r.location + i]);
    }
    return sub;
}

static signed char attributed_is_equal(ObjcId self, ObjcSel, ObjcId other) {
    if (!other || (other->isa != g_cls_nsattributedstring &&
                   other->isa != g_cls_nsmutableattributedstring)) {
        return 0;
    }
    const auto* a = reinterpret_cast<const NSAttributedStringObj*>(self);
    const auto* b = reinterpret_cast<const NSAttributedStringObj*>(other);
    if (*a->text != *b->text || a->attributes->size() != b->attributes->size()) return 0;
    for (std::size_t i = 0; i < a->attributes->size(); ++i) {
        if (!attr_dict_equal((*a->attributes)[i], (*b->attributes)[i])) return 0;
    }
    return 1;
}

static ObjcId attributed_description(ObjcId self, ObjcSel) {
    return attributed_string(self, nullptr);
}

static ObjcId attr_mutable_copy(ObjcId src) {
    ObjcId out = make_nsdict(true);
    if (!src || !is_nsdict_obj(src)) return out;
    ObjcId keys = nsdict_allKeys(src, nullptr);
    const std::size_t count = nsarray_count(keys, nullptr);
    for (std::size_t i = 0; i < count; ++i) {
        ObjcId key = nsarray_objectAtIndex(keys, nullptr, i);
        nsdict_setObjectForKey(out, nullptr, nsdict_objectForKey(src, nullptr, key), key);
    }
    finn_objc_release(keys);
    return out;
}

static void attributed_replace_slots(NSAttributedStringObj* a, DarwinRange r, ObjcId attrs) {
    r = attributed_clamp(*a->text, r);
    for (std::size_t i = r.location; i < r.location + r.length; ++i) {
        finn_objc_release((*a->attributes)[i]);
        (*a->attributes)[i] = attrs ? finn_objc_retain(attrs) : nullptr;
    }
}

static void attributed_set_attributes(ObjcId self, ObjcSel, ObjcId attrs, DarwinRange range) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    ObjcId stored = nullptr;
    if (attrs && is_nsdict_obj(attrs) && nsdict_count(attrs, nullptr) > 0) stored = attrs;
    attributed_replace_slots(a, range, stored);
}

static void attributed_add_attribute(ObjcId self, ObjcSel, ObjcId key, ObjcId value,
                                     DarwinRange range) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    const std::string k = nsstring_to_utf8(key);
    if (k.empty()) return;
    const DarwinRange r = attributed_clamp(*a->text, range);
    for (std::size_t i = r.location; i < r.location + r.length; ++i) {
        ObjcId cur = (*a->attributes)[i];
        ObjcId dict = attr_mutable_copy(cur);
        finn_objc_release(cur);
        ObjcId key_obj = make_nsstring(k);
        if (value) nsdict_setObjectForKey(dict, nullptr, value, key_obj);
        else nsdict_removeObjectForKey(dict, nullptr, key_obj);
        finn_objc_release(key_obj);
        (*a->attributes)[i] = nsdict_count(dict, nullptr) ? finn_objc_retain(dict) : nullptr;
        finn_objc_release(dict);
    }
}

static void attributed_add_attributes(ObjcId self, ObjcSel, ObjcId attrs, DarwinRange range) {
    if (!attrs || !is_nsdict_obj(attrs)) return;
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    const DarwinRange r = attributed_clamp(*a->text, range);
    ObjcId keys = nsdict_allKeys(attrs, nullptr);
    const std::size_t count = nsarray_count(keys, nullptr);
    for (std::size_t i = r.location; i < r.location + r.length; ++i) {
        ObjcId cur = (*a->attributes)[i];
        ObjcId dict = attr_mutable_copy(cur);
        finn_objc_release(cur);
        for (std::size_t j = 0; j < count; ++j) {
            ObjcId key = nsarray_objectAtIndex(keys, nullptr, j);
            nsdict_setObjectForKey(dict, nullptr, nsdict_objectForKey(attrs, nullptr, key), key);
        }
        (*a->attributes)[i] = nsdict_count(dict, nullptr) ? finn_objc_retain(dict) : nullptr;
        finn_objc_release(dict);
    }
    finn_objc_release(keys);
}

static void attributed_remove_attribute(ObjcId self, ObjcSel, ObjcId key, DarwinRange range) {
    attributed_add_attribute(self, nullptr, key, nullptr, range);
}

static void attributed_replace_characters(ObjcId self, ObjcSel, DarwinRange range, ObjcId str) {
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    const DarwinRange r = attributed_clamp(*a->text, range);
    const std::string replacement = nsstring_to_utf8(str);
    a->text->replace(r.location, r.length, replacement);
    a->attributes->erase(a->attributes->begin() + static_cast<std::ptrdiff_t>(r.location),
                         a->attributes->begin() + static_cast<std::ptrdiff_t>(r.location + r.length));
    a->attributes->insert(a->attributes->begin() + static_cast<std::ptrdiff_t>(r.location),
                          replacement.size(), nullptr);
}

static void attributed_delete_characters(ObjcId self, ObjcSel, DarwinRange range) {
    attributed_replace_characters(self, nullptr, range, nsstring_from_utf8(""));
}

static void attributed_append(ObjcId self, ObjcSel, ObjcId other) {
    if (!other || (other->isa != g_cls_nsattributedstring &&
                      other->isa != g_cls_nsmutableattributedstring)) {
        return;
    }
    auto* a = reinterpret_cast<NSAttributedStringObj*>(self);
    const auto* b = reinterpret_cast<const NSAttributedStringObj*>(other);
    *a->text += *b->text;
    for (ObjcId d : *b->attributes) a->attributes->push_back(finn_objc_retain(d));
}

// --- NSDateFormatter / NSCalendar / NSDateComponents ---
// The formatter supports the common strftime-compatible date-format tokens
// below and formats in UTC. Unsupported tokens are passed through literally;
// named time zones and locale-sensitive ordering are not implemented.
static std::string date_format_to_strftime(std::string_view fmt) {
    static const struct {
        const char* token;
        const char* spec;
    } kTokens[] = {
        {"yyyy", "%Y"}, {"yy", "%y"}, {"MMMM", "%B"}, {"MMM", "%b"}, {"MM", "%m"},
        {"dd", "%d"}, {"HH", "%H"}, {"hh", "%I"}, {"mm", "%M"}, {"ss", "%S"},
        {"EEEE", "%A"}, {"EEE", "%a"}, {"a", "%p"}, {"M", "%m"}, {"d", "%d"},
        {"H", "%H"}, {"h", "%I"}, {"m", "%M"}, {"s", "%S"}, {"Y", "%Y"},
    };
    std::string out;
    for (std::size_t i = 0; i < fmt.size();) {
        bool matched = false;
        for (const auto& t : kTokens) {
            const std::size_t len = std::strlen(t.token);
            if (fmt.compare(i, len, t.token) == 0) {
                out += t.spec;
                i += len;
                matched = true;
                break;
            }
        }
        if (!matched) out.push_back(fmt[i++]);
    }
    return out;
}

static ObjcId dateformatter_init(ObjcId self, ObjcSel) {
    auto* f = reinterpret_cast<NSDateFormatterObj*>(self);
    if (!f->format) f->format = new std::string("yyyy-MM-dd HH:mm:ss");
    return self;
}

static ObjcId dateformatter_set_format(ObjcId self, ObjcSel, ObjcId fmt) {
    auto* f = reinterpret_cast<NSDateFormatterObj*>(self);
    if (!f->format) f->format = new std::string();
    *f->format = nsstring_to_utf8(fmt);
    return self;
}

static ObjcId dateformatter_format(ObjcId self, ObjcSel) {
    const auto* f = reinterpret_cast<const NSDateFormatterObj*>(self);
    return make_nsstring(f->format ? *f->format : std::string{});
}

static ObjcId dateformatter_string_from_date(ObjcId self, ObjcSel, ObjcId date) {
    if (!date || date->isa != g_cls_nsdate) return nullptr;
    const auto* f = reinterpret_cast<const NSDateFormatterObj*>(self);
    const auto t = static_cast<std::time_t>(
        reinterpret_cast<const NSDateObj*>(date)->since_1970);
    struct tm tm_utc{};
    if (!::gmtime_r(&t, &tm_utc)) return nullptr;
    const std::string spec = date_format_to_strftime(f->format ? *f->format : std::string{});
    char buf[512] = {};
    const std::size_t written = ::strftime(buf, sizeof(buf), spec.c_str(), &tm_utc);
    return written ? make_nsstring(buf) : nullptr;
}

static ObjcId dateformatter_date_from_string(ObjcId self, ObjcSel, ObjcId str) {
    if (!str || !is_nsstring_obj(str)) return nullptr;
    const auto* f = reinterpret_cast<const NSDateFormatterObj*>(self);
    const std::string spec = date_format_to_strftime(f->format ? *f->format : std::string{});
    struct tm tm_val{};
    tm_val.tm_mday = 1;
    tm_val.tm_year = 70;
    const std::string input = nsstring_to_utf8(str);
    if (!::strptime(input.c_str(), spec.c_str(), &tm_val)) return nullptr;
    const std::time_t t = ::timegm(&tm_val);
    auto* d = reinterpret_cast<NSDateObj*>(
        finn_objc_alloc_object(g_cls_nsdate, sizeof(NSDateObj)));
    d->since_1970 = static_cast<double>(t);
    return reinterpret_cast<ObjcId>(d);
}

static ObjcId datecomponents_init(ObjcId self, ObjcSel) { return self; }

static ObjcId datecomponents_new() {
    return finn_objc_alloc_object(g_cls_nsdatecomponents, sizeof(NSDateComponentsObj));
}

static const std::int64_t* components_field(ObjcId obj, std::uintptr_t unit) {
    auto* c = reinterpret_cast<const NSDateComponentsObj*>(obj);
    switch (unit) {
        case 1u << 1: return &c->era;
        case 1u << 2: return &c->year;
        case 1u << 3: return &c->month;
        case 1u << 4: return &c->day;
        case 1u << 5: return &c->hour;
        case 1u << 6: return &c->minute;
        case 1u << 7: return &c->second;
        case 1u << 8: return &c->weekday;
        case 1u << 14: return &c->nanosecond;
        default: return nullptr;
    }
}

static std::int64_t components_get(ObjcId self, ObjcSel, std::uintptr_t unit) {
    const std::int64_t* p = components_field(self, unit);
    return p ? *p : 0;
}

// One tiny setter per NSCalendarUnit; each writes exactly one field.
#define FINN_COMPONENT_SETTER(name, field)                                     \
    static void components_set_##name(ObjcId self, ObjcSel, std::int64_t v) {  \
        reinterpret_cast<NSDateComponentsObj*>(self)->field = v;               \
    }
FINN_COMPONENT_SETTER(era, era)
FINN_COMPONENT_SETTER(year, year)
FINN_COMPONENT_SETTER(month, month)
FINN_COMPONENT_SETTER(day, day)
FINN_COMPONENT_SETTER(hour, hour)
FINN_COMPONENT_SETTER(minute, minute)
FINN_COMPONENT_SETTER(second, second)
FINN_COMPONENT_SETTER(nanosecond, nanosecond)
FINN_COMPONENT_SETTER(weekday, weekday)
#undef FINN_COMPONENT_SETTER

#define FINN_COMPONENT_GETTER(name, field)                                     \
    static std::int64_t components_get_##name(ObjcId self, ObjcSel) {          \
        return reinterpret_cast<const NSDateComponentsObj*>(self)->field;      \
    }
FINN_COMPONENT_GETTER(era, era)
FINN_COMPONENT_GETTER(year, year)
FINN_COMPONENT_GETTER(month, month)
FINN_COMPONENT_GETTER(day, day)
FINN_COMPONENT_GETTER(hour, hour)
FINN_COMPONENT_GETTER(minute, minute)
FINN_COMPONENT_GETTER(second, second)
FINN_COMPONENT_GETTER(nanosecond, nanosecond)
FINN_COMPONENT_GETTER(weekday, weekday)
#undef FINN_COMPONENT_GETTER

static ObjcId calendar_current(ObjcId, ObjcSel) {
    if (!g_gregorian_calendar) {
        auto* c = reinterpret_cast<NSCalendarObj*>(
            finn_objc_alloc_object(g_cls_nscalendar, sizeof(NSCalendarObj)));
        c->identifier = new std::string("NSGregorianCalendar");
        g_gregorian_calendar = reinterpret_cast<ObjcId>(c);
    }
    return g_gregorian_calendar;
}

static ObjcId calendar_with_identifier(ObjcId, ObjcSel, ObjcId ident) {
    const std::string id = nsstring_to_utf8(ident);
    // Only the Gregorian calendar is implemented. Anything else returns nil
    // rather than silently behaving like a Gregorian calendar.
    if (id != "NSGregorianCalendar" && id != "gregory" && id != "NSCalendarIdentifierGregorian") {
        return nullptr;
    }
    return calendar_current(nullptr, nullptr);
}

static ObjcId calendar_identifier(ObjcId self, ObjcSel) {
    const auto* c = reinterpret_cast<const NSCalendarObj*>(self);
    return make_nsstring(c->identifier ? *c->identifier : std::string{});
}

static void date_to_tm(ObjcId date, struct tm* out) {
    const auto t = static_cast<std::time_t>(
        reinterpret_cast<const NSDateObj*>(date)->since_1970);
    if (!::gmtime_r(&t, out)) *out = {};
}

static std::int64_t calendar_component(ObjcId, ObjcSel, std::uintptr_t unit, ObjcId date) {
    if (!date || date->isa != g_cls_nsdate) return 0;
    struct tm t{};
    date_to_tm(date, &t);
    switch (unit) {
        case 1u << 1: return t.tm_year / 100 + 1;                 // era
        case 1u << 2: return t.tm_year + 1900;                    // year
        case 1u << 3: return t.tm_mon + 1;                        // month
        case 1u << 4: return t.tm_mday;                           // day
        case 1u << 5: return t.tm_hour;                           // hour
        case 1u << 6: return t.tm_min;                            // minute
        case 1u << 7: return t.tm_sec;                            // second
        case 1u << 8: return t.tm_wday + 1;                       // weekday (Sunday = 1)
        default: return 0;  // weeks, quarters, nanoseconds not implemented
    }
}

static ObjcId calendar_components(ObjcId self, ObjcSel, std::uintptr_t units, ObjcId date) {
    ObjcId comps = datecomponents_new();
    static constexpr std::uintptr_t kUnits[] = {
        1u << 1, 1u << 2, 1u << 3, 1u << 4, 1u << 5, 1u << 6, 1u << 7, 1u << 8,
    };
    for (std::uintptr_t unit : kUnits) {
        if ((units & unit) != 0) {
            const std::int64_t v = calendar_component(self, nullptr, unit, date);
            std::int64_t* field = const_cast<std::int64_t*>(
                components_field(comps, unit));
            if (field) *field = v;
        }
    }
    return comps;
}

static ObjcId calendar_date_from_components(ObjcId, ObjcSel, ObjcId comps) {
    if (!comps || comps->isa != g_cls_nsdatecomponents) return nullptr;
    const auto* c = reinterpret_cast<const NSDateComponentsObj*>(comps);
    struct tm t{};
    t.tm_year = static_cast<int>(c->year ? c->year - 1900 : 70);
    t.tm_mon = static_cast<int>(c->month ? c->month - 1 : 0);
    t.tm_mday = static_cast<int>(c->day ? c->day : 1);
    t.tm_hour = static_cast<int>(c->hour);
    t.tm_min = static_cast<int>(c->minute);
    t.tm_sec = static_cast<int>(c->second);
    const std::time_t epoch = ::timegm(&t);
    auto* d = reinterpret_cast<NSDateObj*>(
        finn_objc_alloc_object(g_cls_nsdate, sizeof(NSDateObj)));
    d->since_1970 = static_cast<double>(epoch) + c->nanosecond / 1e9;
    return reinterpret_cast<ObjcId>(d);
}

static ObjcId calendar_add_unit(ObjcId self, ObjcSel, std::uintptr_t unit,
                                std::int64_t value, ObjcId date, std::uintptr_t) {
    (void)self;
    if (!date || date->isa != g_cls_nsdate) return nullptr;
    struct tm t{};
    date_to_tm(date, &t);
    switch (unit) {
        case 1u << 2: t.tm_year += static_cast<int>(value); break;  // year
        case 1u << 3: t.tm_mon += static_cast<int>(value); break;   // month
        case 1u << 4: t.tm_mday += static_cast<int>(value); break;  // day
        case 1u << 5: t.tm_hour += static_cast<int>(value); break;  // hour
        case 1u << 6: t.tm_min += static_cast<int>(value); break;   // minute
        case 1u << 7: t.tm_sec += static_cast<int>(value); break;   // second
        default: return nullptr;  // unsupported unit: report, do not fake
    }
    const std::time_t epoch = ::timegm(&t);
    auto* d = reinterpret_cast<NSDateObj*>(
        finn_objc_alloc_object(g_cls_nsdate, sizeof(NSDateObj)));
    d->since_1970 = static_cast<double>(epoch);
    return reinterpret_cast<ObjcId>(d);
}

// --- NSJSONSerialization (RFC 8259 parser & serializer) ---
class JsonParser {
public:
    JsonParser(std::string_view input, bool mut) : s_(input), mut_(mut) {}

    ObjcId parse(std::string* err) {
        skip_ws();
        ObjcId val = parse_value(0);
        if (!val) {
            if (err) *err = err_.empty() ? "invalid JSON input" : err_;
            return nullptr;
        }
        skip_ws();
        if (pos_ != s_.size()) {
            finn_objc_release(val);
            if (err) *err = "trailing characters after JSON value";
            return nullptr;
        }
        return val;
    }

private:
    void skip_ws() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos_;
            else break;
        }
    }

    static void append_utf8(std::string* out, std::uint32_t cp) {
        if (cp <= 0x7fu) {
            out->push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ffu) {
            out->push_back(static_cast<char>(0xc0u | (cp >> 6)));
            out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else if (cp <= 0xffffu) {
            out->push_back(static_cast<char>(0xe0u | (cp >> 12)));
            out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else {
            out->push_back(static_cast<char>(0xf0u | (cp >> 18)));
            out->push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        }
    }

    bool parse_hex4(std::uint32_t* out) {
        if (pos_ + 4 > s_.size()) return false;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        *out = v;
        return true;
    }

    bool parse_raw_string(std::string* out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') {
            err_ = "expected '\"'";
            return false;
        }
        ++pos_;
        out->clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20u) {
                err_ = "unescaped control character in JSON string";
                return false;
            }
            if (c == '\\') {
                if (pos_ >= s_.size()) { err_ = "unterminated escape"; return false; }
                const char esc = s_[pos_++];
                switch (esc) {
                    case '"': out->push_back('"'); break;
                    case '\\': out->push_back('\\'); break;
                    case '/': out->push_back('/'); break;
                    case 'b': out->push_back('\b'); break;
                    case 'f': out->push_back('\f'); break;
                    case 'n': out->push_back('\n'); break;
                    case 'r': out->push_back('\r'); break;
                    case 't': out->push_back('\t'); break;
                    case 'u': {
                        std::uint32_t cp = 0;
                        if (!parse_hex4(&cp)) { err_ = "invalid \\u escape"; return false; }
                        if (cp >= 0xd800u && cp <= 0xdbffu && pos_ + 2 <= s_.size() &&
                            s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            std::uint32_t low = 0;
                            if (!parse_hex4(&low) || low < 0xdc00u || low > 0xdfffu) {
                                err_ = "invalid UTF-16 surrogate pair";
                                return false;
                            }
                            cp = 0x10000u + (((cp - 0xd800u) << 10) | (low - 0xdc00u));
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:
                        err_ = "invalid JSON escape sequence";
                        return false;
                }
            } else {
                out->push_back(c);
            }
        }
        err_ = "unterminated JSON string";
        return false;
    }

    ObjcId parse_value(int depth) {
        if (depth > 128) { err_ = "JSON nesting too deep"; return nullptr; }
        skip_ws();
        if (pos_ >= s_.size()) { err_ = "unexpected end of JSON"; return nullptr; }
        const char c = s_[pos_];
        if (c == '{') return parse_object(depth + 1);
        if (c == '[') return parse_array(depth + 1);
        if (c == '"') {
            std::string str;
            if (!parse_raw_string(&str)) return nullptr;
            return make_nsstring(str, false);
        }
        if (s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return make_nsnumber_bool(true);
        }
        if (s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return make_nsnumber_bool(false);
        }
        if (s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return finn_objc_retain(nsnull_null(nullptr, nullptr));
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        err_ = "unexpected character in JSON";
        return nullptr;
    }

    ObjcId parse_object(int depth) {
        ++pos_;  // '{'
        ObjcId dict = make_nsdict(mut_);
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return dict;
        }
        while (pos_ < s_.size()) {
            skip_ws();
            std::string key;
            if (!parse_raw_string(&key)) { finn_objc_release(dict); return nullptr; }
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') {
                err_ = "expected ':' after JSON key";
                finn_objc_release(dict);
                return nullptr;
            }
            ++pos_;
            ObjcId val = parse_value(depth);
            if (!val) { finn_objc_release(dict); return nullptr; }
            ObjcId key_obj = make_nsstring(key);
            nsdict_setObjectForKey(dict, nullptr, val, key_obj);
            finn_objc_release(key_obj);
            finn_objc_release(val);
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return dict;
            }
            err_ = "expected ',' or '}' in JSON object";
            finn_objc_release(dict);
            return nullptr;
        }
        err_ = "unterminated JSON object";
        finn_objc_release(dict);
        return nullptr;
    }

    ObjcId parse_array(int depth) {
        ++pos_;  // '['
        ObjcId arr = make_nsarray(mut_);
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return arr;
        }
        while (pos_ < s_.size()) {
            ObjcId val = parse_value(depth);
            if (!val) { finn_objc_release(arr); return nullptr; }
            nsarray_addObject(arr, nullptr, val);
            finn_objc_release(val);
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return arr;
            }
            err_ = "expected ',' or ']' in JSON array";
            finn_objc_release(arr);
            return nullptr;
        }
        err_ = "unterminated JSON array";
        finn_objc_release(arr);
        return nullptr;
    }

    ObjcId parse_number() {
        const std::size_t start = pos_;
        bool is_float = false;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
            err_ = "invalid JSON number";
            return nullptr;
        }
        if (s_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            is_float = true;
            ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                err_ = "invalid fractional JSON number";
                return nullptr;
            }
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                err_ = "invalid exponent in JSON number";
                return nullptr;
            }
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        const std::string token(s_.substr(start, pos_ - start));
        if (is_float) {
            return make_nsnumber_double(std::strtod(token.c_str(), nullptr));
        }
        char* endp = nullptr;
        const long long iv = std::strtoll(token.c_str(), &endp, 10);
        return make_nsnumber_int(static_cast<std::int64_t>(iv));
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    bool mut_ = false;
    std::string err_;
};

ObjcId json_parse_bytes(std::string_view input, bool mutable_containers, std::string* err_out) {
    JsonParser parser(input, mutable_containers);
    return parser.parse(err_out);
}

void json_escape_string(std::string_view s, std::string* out) {
    out->push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  *out += "\\\""; break;
            case '\\': *out += "\\\\"; break;
            case '\b': *out += "\\b"; break;
            case '\f': *out += "\\f"; break;
            case '\n': *out += "\\n"; break;
            case '\r': *out += "\\r"; break;
            case '\t': *out += "\\t"; break;
            default:
                if (c < 0x20u) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    *out += buf;
                } else {
                    out->push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out->push_back('"');
}

bool json_serialize_object(ObjcId obj, bool pretty, int indent, std::string* out,
                           std::string* err_out) {
    if (!obj || obj->isa == g_cls_nsnull) {
        *out += "null";
        return true;
    }
    if (is_nsstring_obj(obj)) {
        json_escape_string(nsstring_to_utf8(obj), out);
        return true;
    }
    if (is_nsnumber_obj(obj)) {
        std::int64_t ival = 0;
        double dval = 0.0;
        bool is_float = false;
        bool is_bool = false;
        read_nsnumber(obj, &ival, &dval, &is_float, &is_bool);
        if (is_bool) {
            *out += (ival != 0) ? "true" : "false";
            return true;
        }
        if (is_float) {
            if (!std::isfinite(dval)) {
                if (err_out) *err_out = "JSON cannot encode NaN or Infinity";
                return false;
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.15g", dval);
            *out += buf;
            return true;
        }
        *out += std::to_string(ival);
        return true;
    }
    if (is_nsarray_obj(obj)) {
        out->push_back('[');
        const std::size_t cnt = nsarray_count(obj, nullptr);
        if (cnt > 0) {
            for (std::size_t i = 0; i < cnt; ++i) {
                if (i > 0) out->push_back(',');
                if (pretty) {
                    out->push_back('\n');
                    out->append(static_cast<std::size_t>(indent + 2), ' ');
                }
                if (!json_serialize_object(nsarray_objectAtIndex(obj, nullptr, i),
                                           pretty, indent + 2, out, err_out)) {
                    return false;
                }
            }
            if (pretty) {
                out->push_back('\n');
                out->append(static_cast<std::size_t>(indent), ' ');
            }
        }
        out->push_back(']');
        return true;
    }
    if (is_nsdict_obj(obj)) {
        out->push_back('{');
        std::map<std::string, ObjcId> ordered;
        if (obj->isa == g_cls_nsconstdict) {
            const auto* c = reinterpret_cast<const DarwinConstDict*>(obj);
            if (c->keys && c->objects) {
                for (std::size_t i = 0; i < c->count; ++i) {
                    ordered[nsstring_to_utf8(c->keys[i])] = c->objects[i];
                }
            }
        } else {
            const auto* d = reinterpret_cast<const NSDictObj*>(obj);
            if (d->map) ordered = *d->map;
        }
        if (!ordered.empty()) {
            bool first = true;
            for (const auto& [k, v] : ordered) {
                if (!first) out->push_back(',');
                first = false;
                if (pretty) {
                    out->push_back('\n');
                    out->append(static_cast<std::size_t>(indent + 2), ' ');
                }
                json_escape_string(k, out);
                *out += pretty ? ": " : ":";
                if (!json_serialize_object(v, pretty, indent + 2, out, err_out)) {
                    return false;
                }
            }
            if (pretty) {
                out->push_back('\n');
                out->append(static_cast<std::size_t>(indent), ' ');
            }
        }
        out->push_back('}');
        return true;
    }
    if (err_out) *err_out = "unsupported object type for JSON serialization";
    return false;
}

ObjcId nsjson_objectWithData(ObjcId, ObjcSel, ObjcId data, std::uintptr_t opts, ObjcId* err_out) {
    if (!is_nsdata_obj(data)) {
        set_out_error(err_out, "NSCocoaErrorDomain", 3840, "JSON input is not NSData");
        return nullptr;
    }
    const auto* d = reinterpret_cast<const NSDataObj*>(data);
    if (!d->bytes || d->bytes->empty()) {
        set_out_error(err_out, "NSCocoaErrorDomain", 3840, "Empty JSON data");
        return nullptr;
    }
    const std::string_view sv(reinterpret_cast<const char*>(d->bytes->data()), d->bytes->size());
    std::string err;
    ObjcId res = json_parse_bytes(sv, (opts & 1u) != 0, &err);
    if (!res) {
        set_out_error(err_out, "NSCocoaErrorDomain", 3840, err);
        return nullptr;
    }
    return finn_objc_autorelease(res);
}

ObjcId nsjson_dataWithObject(ObjcId, ObjcSel, ObjcId obj, std::uintptr_t opts, ObjcId* err_out) {
    if (!is_nsdict_obj(obj) && !is_nsarray_obj(obj)) {
        set_out_error(err_out, "NSCocoaErrorDomain", 3840,
                      "Top-level JSON object must be NSArray or NSDictionary");
        return nullptr;
    }
    std::string json;
    std::string err;
    if (!json_serialize_object(obj, (opts & 1u) != 0, 0, &json, &err)) {
        set_out_error(err_out, "NSCocoaErrorDomain", 3840, err);
        return nullptr;
    }
    return finn_objc_autorelease(nsdata_from_bytes(json.data(), json.size(), false));
}

signed char nsjson_isValidObject(ObjcId, ObjcSel, ObjcId obj) {
    if (!is_nsdict_obj(obj) && !is_nsarray_obj(obj)) return 0;
    std::string dummy;
    std::string err;
    return json_serialize_object(obj, false, 0, &dummy, &err) ? 1 : 0;
}

// --- NSURL ---
void nsurl_dtor(ObjcId obj) {
    auto* u = reinterpret_cast<NSURLObj*>(obj);
    delete u->raw_str;
    delete u->scheme;
    delete u->host;
    delete u->path;
    u->raw_str = nullptr;
    u->scheme = nullptr;
    u->host = nullptr;
    u->path = nullptr;
}

ObjcId make_nsurl_parsed(std::string_view raw, bool force_file) {
    auto* u = reinterpret_cast<NSURLObj*>(
        finn_objc_alloc_object(g_cls_nsurl, sizeof(NSURLObj)));
    u->raw_str = new std::string();
    u->scheme = new std::string();
    u->host = new std::string();
    u->path = new std::string();
    u->is_file_url = force_file;

    if (force_file) {
        *u->scheme = "file";
        *u->path = std::string(raw);
        *u->raw_str = "file://" + std::string(raw);
        return reinterpret_cast<ObjcId>(u);
    }

    *u->raw_str = std::string(raw);
    const std::size_t sep = raw.find("://");
    if (sep != std::string_view::npos) {
        *u->scheme = std::string(raw.substr(0, sep));
        const std::string_view rest = raw.substr(sep + 3);
        const std::size_t qpos = rest.find_first_of("?#");
        const std::string_view auth_and_path =
            (qpos == std::string_view::npos) ? rest : rest.substr(0, qpos);
        if (*u->scheme == "file") {
            u->is_file_url = true;
            *u->path = std::string(auth_and_path);
        } else {
            const std::size_t slash = auth_and_path.find('/');
            std::string_view host_part =
                (slash == std::string_view::npos) ? auth_and_path : auth_and_path.substr(0, slash);
            // Strip optional :port from host
            const std::size_t colon = host_part.rfind(':');
            if (colon != std::string_view::npos) host_part = host_part.substr(0, colon);
            *u->host = std::string(host_part);
            *u->path = (slash == std::string_view::npos)
                           ? std::string{}
                           : std::string(auth_and_path.substr(slash));
        }
    } else {
        *u->path = std::string(raw);
    }
    return reinterpret_cast<ObjcId>(u);
}

ObjcId nsurl_fileURLWithPath(ObjcId, ObjcSel, ObjcId path) {
    return make_nsurl_parsed(nsstring_to_utf8(path), true);
}

ObjcId nsurl_URLWithString(ObjcId, ObjcSel, ObjcId str) {
    const std::string s = nsstring_to_utf8(str);
    if (s.empty()) return nullptr;
    return make_nsurl_parsed(s, false);
}

ObjcId nsurl_path(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    return make_nsstring((u && u->path) ? *u->path : "");
}

ObjcId nsurl_absoluteString(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    return make_nsstring((u && u->raw_str) ? *u->raw_str : "");
}

ObjcId nsurl_scheme(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    if (!u || !u->scheme || u->scheme->empty()) return nullptr;
    return make_nsstring(*u->scheme);
}

ObjcId nsurl_host(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    if (!u || !u->host || u->host->empty()) return nullptr;
    return make_nsstring(*u->host);
}

ObjcId nsurl_lastPathComponent(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    std::string p = (u && u->path) ? *u->path : "";
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    if (p == "/") return make_nsstring("/");
    return make_nsstring(fs::path(p).filename().string());
}

ObjcId nsurl_pathExtension(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    std::string ext = fs::path((u && u->path) ? *u->path : "").extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    return make_nsstring(ext);
}

ObjcId nsurl_byAppendingPathComponent(ObjcId self, ObjcSel, ObjcId comp_obj) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    const std::string comp = nsstring_to_utf8(comp_obj);
    if (!u) return nullptr;
    if (u->is_file_url) {
        const fs::path joined = fs::path(u->path ? *u->path : "") / comp;
        return make_nsurl_parsed(joined.string(), true);
    }
    std::string base = u->raw_str ? *u->raw_str : "";
    if (!base.empty() && base.back() != '/' && !comp.empty() && comp.front() != '/') {
        base.push_back('/');
    }
    base += comp;
    return make_nsurl_parsed(base, false);
}

ObjcId nsurl_byDeletingLastPathComponent(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    if (!u) return nullptr;
    if (u->is_file_url) {
        std::string p = u->path ? *u->path : "";
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        return make_nsurl_parsed(fs::path(p).parent_path().string(), true);
    }
    std::string base = u->raw_str ? *u->raw_str : "";
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    const std::size_t slash = base.rfind('/');
    if (slash != std::string::npos) base.resize(slash);
    return make_nsurl_parsed(base, false);
}

signed char nsurl_isFileURL(ObjcId self, ObjcSel) {
    const auto* u = reinterpret_cast<const NSURLObj*>(self);
    return (u && u->is_file_url) ? 1 : 0;
}

// --- NSBundle ---
ObjcId plist_to_objc(const PlistValue& v) {
    switch (v.kind) {
        case PlistValue::Kind::String: return make_nsstring(v.string_val);
        case PlistValue::Kind::Int: return make_nsnumber_int(v.int_val);
        case PlistValue::Kind::Bool: return make_nsnumber_bool(v.bool_val);
        case PlistValue::Kind::Real: return make_nsnumber_double(v.real_val);
        case PlistValue::Kind::Array: {
            ObjcId arr = make_nsarray(false);
            for (const auto& e : v.array_val) {
                ObjcId child = plist_to_objc(e);
                nsarray_addObject(arr, nullptr, child);
                finn_objc_release(child);
            }
            return arr;
        }
        case PlistValue::Kind::Dict: {
            ObjcId d = make_nsdict(false);
            for (const auto& [k, val] : v.dict_val) {
                ObjcId ks = make_nsstring(k);
                ObjcId vo = plist_to_objc(val);
                nsdict_setObjectForKey(d, nullptr, vo, ks);
                finn_objc_release(ks);
                finn_objc_release(vo);
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
                finn_objc_release(b->info_dict);
                b->info_dict = plist_to_objc(*p);
                g_main_bundle_id = p->get_string("CFBundleIdentifier");
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

ObjcId nsbundle_bundleIdentifier(ObjcId self, ObjcSel) {
    const auto* b = reinterpret_cast<const NSBundleObj*>(self);
    if (!b || !b->info_dict) return nullptr;
    ObjcId k = make_nsstring("CFBundleIdentifier");
    ObjcId v = nsdict_objectForKey(b->info_dict, nullptr, k);
    finn_objc_release(k);
    return v;
}

ObjcId nsbundle_infoDictionary(ObjcId self, ObjcSel) {
    const auto* b = reinterpret_cast<const NSBundleObj*>(self);
    return b ? b->info_dict : nullptr;
}

ObjcId nsbundle_objectForInfoKey(ObjcId self, ObjcSel, ObjcId key) {
    const auto* b = reinterpret_cast<const NSBundleObj*>(self);
    return (b && b->info_dict) ? nsdict_objectForKey(b->info_dict, nullptr, key) : nullptr;
}

// --- NSProcessInfo ---
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

int nsprocessinfo_pid(ObjcId, ObjcSel) {
    return static_cast<int>(::getpid());
}

ObjcId nsprocessinfo_processName(ObjcId, ObjcSel) {
    if (!g_process_args.empty() && !g_process_args[0].empty()) {
        return make_nsstring(fs::path(g_process_args[0]).filename().string());
    }
    return make_nsstring("finn-darwin");
}

ObjcId nsprocessinfo_arguments(ObjcId, ObjcSel) {
    ObjcId arr = make_nsarray(false);
    for (const auto& arg : g_process_args) {
        ObjcId s = make_nsstring(arg);
        nsarray_addObject(arr, nullptr, s);
        finn_objc_release(s);
    }
    return finn_objc_autorelease(arr);
}

ObjcId nsprocessinfo_environment(ObjcId, ObjcSel) {
    ObjcId dict = make_nsdict(false);
    if (environ) {
        for (char** ep = environ; *ep != nullptr; ++ep) {
            const std::string_view kv(*ep);
            const std::size_t eq = kv.find('=');
            if (eq == std::string_view::npos) continue;
            ObjcId k = make_nsstring(kv.substr(0, eq));
            ObjcId v = make_nsstring(kv.substr(eq + 1));
            nsdict_setObjectForKey(dict, nullptr, v, k);
            finn_objc_release(k);
            finn_objc_release(v);
        }
    }
    return finn_objc_autorelease(dict);
}

ObjcId nsprocessinfo_hostName(ObjcId, ObjcSel) {
    char buf[256] = {};
    if (::gethostname(buf, sizeof(buf) - 1) == 0) return make_nsstring(buf);
    return make_nsstring("localhost");
}

// --- NSFileManager ---
ObjcId nsfilemanager_defaultManager(ObjcId, ObjcSel) {
    if (!g_file_manager_obj) {
        g_file_manager_obj = finn_objc_alloc_object(g_cls_nsfilemanager);
    }
    return g_file_manager_obj;
}

signed char nsfilemanager_fileExists(ObjcId, ObjcSel, ObjcId path) {
    std::error_code ec;
    return fs::exists(nsstring_to_utf8(path), ec) ? 1 : 0;
}

signed char nsfilemanager_fileExistsIsDir(ObjcId, ObjcSel, ObjcId path, signed char* is_dir) {
    std::error_code ec;
    const fs::path p(nsstring_to_utf8(path));
    if (!fs::exists(p, ec)) {
        if (is_dir) *is_dir = 0;
        return 0;
    }
    if (is_dir) *is_dir = fs::is_directory(p, ec) ? 1 : 0;
    return 1;
}

ObjcId nsfilemanager_currentDir(ObjcId, ObjcSel) {
    std::error_code ec;
    return make_nsstring(fs::current_path(ec).string());
}

signed char nsfilemanager_changeCurrentDir(ObjcId, ObjcSel, ObjcId path) {
    std::error_code ec;
    fs::current_path(nsstring_to_utf8(path), ec);
    return ec ? 0 : 1;
}

ObjcId nsfilemanager_contentsOfDirectory(ObjcId, ObjcSel, ObjcId path_obj, ObjcId* err_out) {
    const std::string path = nsstring_to_utf8(path_obj);
    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        set_out_error(err_out, "NSCocoaErrorDomain", 260,
                      "Directory does not exist: " + path);
        return nullptr;
    }
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) {
            set_out_error(err_out, "NSPOSIXErrorDomain", ec.value(), ec.message());
            return nullptr;
        }
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    ObjcId arr = make_nsarray(false);
    for (const auto& n : names) {
        ObjcId s = make_nsstring(n);
        nsarray_addObject(arr, nullptr, s);
        finn_objc_release(s);
    }
    return finn_objc_autorelease(arr);
}

signed char nsfilemanager_createDirectory(ObjcId, ObjcSel, ObjcId path_obj,
                                          signed char intermediates, ObjcId, ObjcId* err_out) {
    const fs::path p(nsstring_to_utf8(path_obj));
    std::error_code ec;
    if (intermediates != 0) {
        fs::create_directories(p, ec);
    } else {
        fs::create_directory(p, ec);
    }
    if (ec) {
        set_out_error(err_out, "NSCocoaErrorDomain", 512,
                      "Failed to create directory " + p.string() + ": " + ec.message());
        return 0;
    }
    return 1;
}

signed char nsfilemanager_removeItem(ObjcId, ObjcSel, ObjcId path_obj, ObjcId* err_out) {
    const fs::path p(nsstring_to_utf8(path_obj));
    std::error_code ec;
    if (!fs::exists(p, ec) && !fs::is_symlink(p, ec)) {
        set_out_error(err_out, "NSCocoaErrorDomain", 4, "No such file or directory: " + p.string());
        return 0;
    }
    fs::remove_all(p, ec);
    if (ec) {
        set_out_error(err_out, "NSCocoaErrorDomain", 512,
                      "Failed to remove item " + p.string() + ": " + ec.message());
        return 0;
    }
    return 1;
}

signed char nsfilemanager_copyItem(ObjcId, ObjcSel, ObjcId src_obj, ObjcId dst_obj,
                                   ObjcId* err_out) {
    std::error_code ec;
    fs::copy(nsstring_to_utf8(src_obj), nsstring_to_utf8(dst_obj),
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) {
        set_out_error(err_out, "NSCocoaErrorDomain", 512, ec.message());
        return 0;
    }
    return 1;
}

signed char nsfilemanager_moveItem(ObjcId, ObjcSel, ObjcId src_obj, ObjcId dst_obj,
                                   ObjcId* err_out) {
    std::error_code ec;
    fs::rename(nsstring_to_utf8(src_obj), nsstring_to_utf8(dst_obj), ec);
    if (ec) {
        set_out_error(err_out, "NSCocoaErrorDomain", 512, ec.message());
        return 0;
    }
    return 1;
}

ObjcId nsfilemanager_homeDirectory(ObjcId, ObjcSel) {
    const char* home = std::getenv("HOME");
    return finn_objc_autorelease(make_nsurl_parsed((home && *home) ? home : "/", true));
}

ObjcId nsfilemanager_tempDirectory(ObjcId, ObjcSel) {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec);
    return finn_objc_autorelease(make_nsurl_parsed(tmp.empty() ? "/tmp" : tmp.string(), true));
}

// --- NSUserDefaults (persisted to ~/.local/share/finn/macos/defaults/<bundle>.conf) ---
void clear_defaults_store() {
    for (auto& [_, v] : g_defaults_store) finn_objc_release(v);
    g_defaults_store.clear();
}

void ensure_defaults_loaded() {
    const std::string path = user_defaults_file_path();
    if (path == g_loaded_defaults_path) return;
    g_loaded_defaults_path = path;
    clear_defaults_store();

    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();
    if (content.empty()) return;

    std::string err;
    ObjcId parsed = json_parse_bytes(content, false, &err);
    if (parsed && is_nsdict_obj(parsed)) {
        const auto* d = reinterpret_cast<const NSDictObj*>(parsed);
        if (d->map) {
            for (const auto& [k, v] : *d->map) {
                g_defaults_store[k] = finn_objc_retain(v);
            }
        }
    }
    finn_objc_release(parsed);
}

ObjcId nsuserdefaults_standard(ObjcId, ObjcSel) {
    if (!g_user_defaults_obj) {
        g_user_defaults_obj = finn_objc_alloc_object(g_cls_nsuserdefaults);
    }
    ensure_defaults_loaded();
    return g_user_defaults_obj;
}

ObjcId nsuserdefaults_objectForKey(ObjcId, ObjcSel, ObjcId key) {
    ensure_defaults_loaded();
    auto it = g_defaults_store.find(nsstring_to_utf8(key));
    return (it == g_defaults_store.end()) ? nullptr : it->second;
}

ObjcId nsuserdefaults_stringForKey(ObjcId, ObjcSel, ObjcId key) {
    ObjcId val = nsuserdefaults_objectForKey(nullptr, nullptr, key);
    if (!val) return nullptr;
    if (is_nsstring_obj(val)) return val;
    if (is_nsnumber_obj(val)) return nsnumber_stringValue(val, nullptr);
    return nullptr;
}

std::intptr_t nsuserdefaults_integerForKey(ObjcId, ObjcSel, ObjcId key) {
    ObjcId val = nsuserdefaults_objectForKey(nullptr, nullptr, key);
    if (!val) return 0;
    if (is_nsnumber_obj(val)) return nsnumber_integerValue(val, nullptr);
    if (is_nsstring_obj(val)) return nsstring_integerValue(val, nullptr);
    return 0;
}

signed char nsuserdefaults_boolForKey(ObjcId, ObjcSel, ObjcId key) {
    ObjcId val = nsuserdefaults_objectForKey(nullptr, nullptr, key);
    if (!val) return 0;
    if (is_nsnumber_obj(val)) return nsnumber_boolValue(val, nullptr);
    if (is_nsstring_obj(val)) return nsstring_boolValue(val, nullptr);
    return 0;
}

double nsuserdefaults_doubleForKey(ObjcId, ObjcSel, ObjcId key) {
    ObjcId val = nsuserdefaults_objectForKey(nullptr, nullptr, key);
    if (!val) return 0.0;
    if (is_nsnumber_obj(val)) return nsnumber_doubleValue(val, nullptr);
    if (is_nsstring_obj(val)) return nsstring_doubleValue(val, nullptr);
    return 0.0;
}

void nsuserdefaults_setObjectForKey(ObjcId, ObjcSel, ObjcId val, ObjcId key) {
    ensure_defaults_loaded();
    const std::string k = nsstring_to_utf8(key);
    if (auto it = g_defaults_store.find(k); it != g_defaults_store.end()) {
        finn_objc_release(it->second);
        g_defaults_store.erase(it);
    }
    if (val) g_defaults_store[k] = finn_objc_retain(val);
}

void nsuserdefaults_setIntegerForKey(ObjcId self, ObjcSel, std::intptr_t v, ObjcId key) {
    ObjcId num = make_nsnumber_int(v);
    nsuserdefaults_setObjectForKey(self, nullptr, num, key);
    finn_objc_release(num);
}

void nsuserdefaults_setBoolForKey(ObjcId self, ObjcSel, signed char v, ObjcId key) {
    ObjcId num = make_nsnumber_bool(v != 0);
    nsuserdefaults_setObjectForKey(self, nullptr, num, key);
    finn_objc_release(num);
}

void nsuserdefaults_setDoubleForKey(ObjcId self, ObjcSel, double v, ObjcId key) {
    ObjcId num = make_nsnumber_double(v);
    nsuserdefaults_setObjectForKey(self, nullptr, num, key);
    finn_objc_release(num);
}

void nsuserdefaults_removeObjectForKey(ObjcId, ObjcSel, ObjcId key) {
    ensure_defaults_loaded();
    const std::string k = nsstring_to_utf8(key);
    if (auto it = g_defaults_store.find(k); it != g_defaults_store.end()) {
        finn_objc_release(it->second);
        g_defaults_store.erase(it);
    }
}

signed char nsuserdefaults_synchronize(ObjcId, ObjcSel) {
    ensure_defaults_loaded();
    ObjcId dict = make_nsdict(false);
    auto* d = reinterpret_cast<NSDictObj*>(dict);
    for (const auto& [k, v] : g_defaults_store) {
        (*d->map)[k] = finn_objc_retain(v);
    }
    std::string json;
    std::string err;
    const bool serialized = json_serialize_object(dict, true, 0, &json, &err);
    finn_objc_release(dict);
    if (!serialized) return 0;
    json.push_back('\n');
    const std::string path = user_defaults_file_path();
    g_loaded_defaults_path = path;
    return write_bytes_to_file(fs::path(path), json.data(), json.size(), true, nullptr) ? 1 : 0;
}

// --- NSNotificationCenter & NSRunLoop ---
ObjcId nsnotifcenter_default(ObjcId, ObjcSel) {
    if (!g_notification_center_obj) {
        g_notification_center_obj = finn_objc_alloc_object(g_cls_nsnotificationcenter);
    }
    return g_notification_center_obj;
}
void nsnotifcenter_addObserver(ObjcId, ObjcSel, ObjcId obs, ObjcSel sel,
                               ObjcId name, ObjcId obj) {
    if (!obs) return;
    // A registration owns its observer and observed object. Without these
    // retains an autorelease-pool drain can free either one while it is still
    // in the table, and posting would then dispatch through a dangling IMP.
    g_observers.push_back(
        {finn_objc_retain(obs), sel, nsstring_to_utf8(name), finn_objc_retain(obj)});
}
void nsnotifcenter_removeObserver(ObjcId, ObjcSel, ObjcId obs) {
    const auto first = std::remove_if(
        g_observers.begin(), g_observers.end(),
        [obs](const NotificationObserver& o) { return o.observer == obs; });
    for (auto it = first; it != g_observers.end(); ++it) {
        finn_objc_release(it->observer);
        finn_objc_release(it->object);
    }
    g_observers.erase(first, g_observers.end());
}
void nsnotifcenter_removeObserverNameObject(ObjcId, ObjcSel, ObjcId obs, ObjcId name,
                                            ObjcId obj) {
    const std::string n = nsstring_to_utf8(name);
    const bool has_name = (name != nullptr);
    const auto first = std::remove_if(
        g_observers.begin(), g_observers.end(),
        [obs, &n, has_name, obj](const NotificationObserver& o) {
            if (o.observer != obs) return false;
            if (has_name && o.name != n) return false;
            if (obj && o.object != obj) return false;
            return true;
        });
    for (auto it = first; it != g_observers.end(); ++it) {
        finn_objc_release(it->observer);
        finn_objc_release(it->object);
    }
    g_observers.erase(first, g_observers.end());
}
static void nsnotification_dtor(ObjcId obj) {
    auto* n = reinterpret_cast<NSNotificationObj*>(obj);
    finn_objc_release(n->name);
    finn_objc_release(n->object);
    finn_objc_release(n->user_info);
}

static ObjcId nsnotification_with_name(ObjcId, ObjcSel, ObjcId name, ObjcId obj,
                                       ObjcId user_info) {
    auto* n = reinterpret_cast<NSNotificationObj*>(
        finn_objc_alloc_object(g_cls_nsnotification, sizeof(NSNotificationObj)));
    n->name = finn_objc_retain(name);
    n->object = finn_objc_retain(obj);
    n->user_info = finn_objc_retain(user_info);
    return reinterpret_cast<ObjcId>(n);
}

ObjcId nsnotification_name(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSNotificationObj*>(self)->name;
}
ObjcId nsnotification_object(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSNotificationObj*>(self)->object;
}
ObjcId nsnotification_userinfo(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSNotificationObj*>(self)->user_info;
}
void nsnotifcenter_post_notification(ObjcId, ObjcSel, ObjcId notification) {
    if (!notification || notification->isa != g_cls_nsnotification) return;
    auto* notif = reinterpret_cast<NSNotificationObj*>(notification);
    const std::string n = nsstring_to_utf8(notif->name);
    const auto snapshot = g_observers;
    for (const auto& o : snapshot) {
        if ((o.name.empty() || o.name == n) && (!o.object || o.object == notif->object)) {
            objc_send<void>(o.observer, o.selector, notification);
        }
    }
}
void nsnotifcenter_post(ObjcId, ObjcSel, ObjcId name, ObjcId obj, ObjcId user_info) {
    const std::string n = nsstring_to_utf8(name);
    auto* notif = reinterpret_cast<NSNotificationObj*>(
        finn_objc_alloc_object(g_cls_nsnotification, sizeof(NSNotificationObj)));
    notif->name = finn_objc_retain(name);
    notif->object = finn_objc_retain(obj);
    notif->user_info = finn_objc_retain(user_info);
    const auto snapshot = g_observers;
    for (const auto& o : snapshot) {
        if ((o.name.empty() || o.name == n) && (!o.object || o.object == obj)) {
            objc_send<void>(o.observer, o.selector, reinterpret_cast<ObjcId>(notif));
        }
    }
    // Foundation posts an autoreleased notification; observers may retain it.
    finn_objc_autorelease(reinterpret_cast<ObjcId>(notif));
}

ObjcId nsrunloop_main(ObjcId, ObjcSel) {
    if (!g_main_runloop_obj) {
        g_main_runloop_obj = finn_objc_alloc_object(g_cls_nsrunloop);
    }
    return g_main_runloop_obj;
}
void nsrunloop_runUntilDate(ObjcId, ObjcSel, ObjcId date) {
    if (date && date->isa == g_cls_nsdate) {
        const double delta = nsdate_timeIntervalSinceNow(date, nullptr);
        if (delta > 0.0 && delta < 3600.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(delta));
        }
    }
}
signed char nsrunloop_runModeBeforeDate(ObjcId self, ObjcSel, ObjcId, ObjcId date) {
    nsrunloop_runUntilDate(self, nullptr, date);
    return 1;
}
void nsrunloop_addTimer(ObjcId, ObjcSel, ObjcId, ObjcId) {}

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

// --- NSPathUtilities ---
// NSSearchPathDirectory values from NSPathUtilities.h. Only the directories
// below are mapped onto the host filesystem; every other constant yields an
// empty array rather than an invented path.
static std::string path_home() {
    const char* h = std::getenv("HOME");
    return h ? h : std::string{};
}

static std::string path_temp() {
    const char* t = std::getenv("TMPDIR");
    return (t && *t) ? t : std::string("/tmp");
}

static ObjcId make_path_array(const std::vector<std::string>& paths) {
    ObjcId arr = make_nsarray(false);
    for (const auto& p : paths) {
        ObjcId s = make_nsstring(p);
        nsarray_addObject(arr, nullptr, s);
        finn_objc_release(s);
    }
    return arr;
}

ObjcId shim_NSSearchPathForDirectoriesInDomains(std::uintptr_t directory,
                                                std::uintptr_t domain_mask,
                                                signed char /*expand_tilde*/) {
    const bool user = (domain_mask & (1u << 0)) != 0;   // NSUserDomainMask
    const bool local = (domain_mask & (1u << 1)) != 0;  // NSLocalDomainMask
    const bool system = (domain_mask & (1u << 2)) != 0; // NSSystemDomainMask
    const bool all = (domain_mask & 0xffffu) == 0xffffu; // NSAllDomainsMask
    const std::string home = path_home();
    std::vector<std::string> out;

    auto join = [&home](const char* suffix) { return home + suffix; };
    switch (directory) {
        case 1:  // NSApplicationDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Applications"));
            if (local || system || all) out.push_back("/Applications");
            break;
        case 5:  // NSLibraryDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Library"));
            if (local || system || all) out.push_back("/Library");
            break;
        case 7:  // NSUserDirectory
            if ((user || all) && !home.empty()) out.push_back(home);
            break;
        case 9:  // NSDocumentDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Documents"));
            break;
        case 12: // NSDesktopDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Desktop"));
            break;
        case 13: // NSCachesDirectory
            if (user || all) {
                const char* xdg = std::getenv("XDG_CACHE_HOME");
                if (xdg && *xdg) out.push_back(std::string(xdg));
                else if (!home.empty()) out.push_back(join("/.cache"));
            }
            if (local || system) out.push_back("/Library/Caches");
            break;
        case 14: // NSApplicationSupportDirectory
            if (user || all) {
                const char* xdg = std::getenv("XDG_DATA_HOME");
                if (xdg && *xdg) out.push_back(std::string(xdg));
                else if (!home.empty()) out.push_back(join("/.local/share"));
            }
            if (local || system) out.push_back("/Library/Application Support");
            break;
        case 15: // NSDownloadsDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Downloads"));
            break;
        case 17: // NSMoviesDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Movies"));
            break;
        case 18: // NSMusicDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Music"));
            break;
        case 19: // NSPicturesDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Pictures"));
            break;
        case 21: // NSSharedPublicDirectory
            if ((user || all) && !home.empty()) out.push_back(join("/Public"));
            break;
        case 99: // NSItemReplacementDirectory
            if (user || all || local || system) out.push_back(path_temp());
            break;
        default:
            break;  // honestly unsupported: no paths invented
    }
    return make_path_array(out);
}

const char* shim_NSHomeDirectory() {
    static std::string value;
    value = path_home();
    return value.c_str();
}

const char* shim_NSTemporaryDirectory() {
    static std::string value;
    value = path_temp();
    return value.c_str();
}

// CFString
ObjcId shim_CFStringCreateWithCString(void*, const char* cstr, std::uint32_t) {
    return make_nsstring(cstr ? cstr : "");
}
ObjcId shim_CFStringCreateCopy(void*, ObjcId str) {
    return make_nsstring(nsstring_to_utf8(str));
}
const char* shim_CFStringGetCStringPtr(ObjcId str, std::uint32_t) {
    return nsstring_utf8String(str, nullptr);
}
unsigned char shim_CFStringGetCString(ObjcId str, char* buf, std::intptr_t buf_sz, std::uint32_t) {
    if (!buf || buf_sz <= 0) return 0;
    const std::string s = nsstring_to_utf8(str);
    if (static_cast<std::intptr_t>(s.size()) >= buf_sz) return 0;
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return 1;
}
std::intptr_t shim_CFStringGetLength(ObjcId str) {
    return static_cast<std::intptr_t>(nsstring_length(str, nullptr));
}
std::intptr_t shim_CFStringCompare(ObjcId a, ObjcId b, std::uintptr_t) {
    const std::string sa = nsstring_to_utf8(a);
    const std::string sb = nsstring_to_utf8(b);
    if (sa < sb) return -1;
    if (sa > sb) return 1;
    return 0;
}

// CFArray (toll-free bridged to NSArray/NSMutableArray)
ObjcId shim_CFArrayCreate(void*, const void** values, std::intptr_t count, const void*) {
    ObjcId arr = make_nsarray(false);
    if (values && count > 0) {
        for (std::intptr_t i = 0; i < count; ++i) {
            nsarray_addObject(arr, nullptr,
                              reinterpret_cast<ObjcId>(const_cast<void*>(values[i])));
        }
    }
    return arr;
}
ObjcId shim_CFArrayCreateMutable(void*, std::intptr_t, const void*) {
    return make_nsarray(true);
}
std::intptr_t shim_CFArrayGetCount(ObjcId arr) {
    return static_cast<std::intptr_t>(nsarray_count(arr, nullptr));
}
const void* shim_CFArrayGetValueAtIndex(ObjcId arr, std::intptr_t idx) {
    if (idx < 0) return nullptr;
    return nsarray_objectAtIndex(arr, nullptr, static_cast<std::size_t>(idx));
}
void shim_CFArrayAppendValue(ObjcId arr, const void* val) {
    nsarray_addObject(arr, nullptr, reinterpret_cast<ObjcId>(const_cast<void*>(val)));
}
void shim_CFArrayRemoveAllValues(ObjcId arr) {
    nsarray_removeAllObjects(arr, nullptr);
}

// CFDictionary (toll-free bridged to NSDictionary/NSMutableDictionary)
ObjcId shim_CFDictionaryCreate(void*, const void** keys, const void** values,
                               std::intptr_t count, const void*, const void*) {
    ObjcId dict = make_nsdict(false);
    if (keys && values && count > 0) {
        for (std::intptr_t i = 0; i < count; ++i) {
            nsdict_setObjectForKey(
                dict, nullptr,
                reinterpret_cast<ObjcId>(const_cast<void*>(values[i])),
                reinterpret_cast<ObjcId>(const_cast<void*>(keys[i])));
        }
    }
    return dict;
}
ObjcId shim_CFDictionaryCreateMutable(void*, std::intptr_t, const void*, const void*) {
    return make_nsdict(true);
}
std::intptr_t shim_CFDictionaryGetCount(ObjcId dict) {
    return static_cast<std::intptr_t>(nsdict_count(dict, nullptr));
}
const void* shim_CFDictionaryGetValue(ObjcId dict, const void* key) {
    return nsdict_objectForKey(dict, nullptr, reinterpret_cast<ObjcId>(const_cast<void*>(key)));
}
void shim_CFDictionarySetValue(ObjcId dict, const void* key, const void* val) {
    nsdict_setObjectForKey(dict, nullptr,
                           reinterpret_cast<ObjcId>(const_cast<void*>(val)),
                           reinterpret_cast<ObjcId>(const_cast<void*>(key)));
}
void shim_CFDictionaryRemoveValue(ObjcId dict, const void* key) {
    nsdict_removeObjectForKey(dict, nullptr, reinterpret_cast<ObjcId>(const_cast<void*>(key)));
}
unsigned char shim_CFDictionaryContainsKey(ObjcId dict, const void* key) {
    return shim_CFDictionaryGetValue(dict, key) != nullptr ? 1 : 0;
}

// CFData (toll-free bridged to NSData/NSMutableData)
ObjcId shim_CFDataCreate(void*, const std::uint8_t* bytes, std::intptr_t len) {
    return nsdata_from_bytes(bytes, len > 0 ? static_cast<std::size_t>(len) : 0, false);
}
ObjcId shim_CFDataCreateMutable(void*, std::intptr_t cap) {
    ObjcId d = nsdata_from_bytes(nullptr, 0, true);
    if (cap > 0) reinterpret_cast<NSDataObj*>(d)->bytes->reserve(static_cast<std::size_t>(cap));
    return d;
}
const std::uint8_t* shim_CFDataGetBytePtr(ObjcId data) {
    return static_cast<const std::uint8_t*>(nsdata_bytes(data, nullptr));
}
std::uint8_t* shim_CFDataGetMutableBytePtr(ObjcId data) {
    return static_cast<std::uint8_t*>(nsdata_mutableBytes(data, nullptr));
}
std::intptr_t shim_CFDataGetLength(ObjcId data) {
    return static_cast<std::intptr_t>(nsdata_length(data, nullptr));
}
void shim_CFDataAppendBytes(ObjcId data, const std::uint8_t* bytes, std::intptr_t len) {
    if (len > 0) nsmutabledata_appendBytesLength(data, nullptr, bytes, static_cast<std::size_t>(len));
}
void shim_CFDataGetBytes(ObjcId data, DarwinRange range, std::uint8_t* buf) {
    if (!buf || !is_nsdata_obj(data)) return;
    const auto* d = reinterpret_cast<const NSDataObj*>(data);
    if (!d->bytes || range.location >= d->bytes->size()) return;
    const std::size_t avail = std::min(range.length, d->bytes->size() - range.location);
    std::memcpy(buf, d->bytes->data() + range.location, avail);
}

// CFNumber (toll-free bridged to NSNumber)
// Apple CFNumberType enum values:
// 1=SInt8, 2=SInt16, 3=SInt32, 4=SInt64, 5=Float32, 6=Float64,
// 7=Char, 8=Short, 9=Int, 10=Long, 11=LongLong, 12=Float, 13=Double,
// 14=CFIndex, 15=NSInteger, 16=CGFloat
ObjcId shim_CFNumberCreate(void*, std::intptr_t type, const void* value_ptr) {
    if (!value_ptr) return nullptr;
    switch (type) {
        case 1: case 7: { std::int8_t v = 0; std::memcpy(&v, value_ptr, 1); return make_nsnumber_int(v); }
        case 2: case 8: { std::int16_t v = 0; std::memcpy(&v, value_ptr, 2); return make_nsnumber_int(v); }
        case 3: case 9: { std::int32_t v = 0; std::memcpy(&v, value_ptr, 4); return make_nsnumber_int(v); }
        case 4: case 10: case 11: case 14: case 15: {
            std::int64_t v = 0;
            std::memcpy(&v, value_ptr, 8);
            return make_nsnumber_int(v);
        }
        case 5: case 12: { float v = 0; std::memcpy(&v, value_ptr, 4); return make_nsnumber_double(v); }
        case 6: case 13: case 16: {
            double v = 0;
            std::memcpy(&v, value_ptr, 8);
            return make_nsnumber_double(v);
        }
        default: return nullptr;
    }
}

unsigned char shim_CFNumberGetValue(ObjcId num, std::intptr_t type, void* out_ptr) {
    std::int64_t ival = 0;
    double dval = 0.0;
    if (!out_ptr || !read_nsnumber(num, &ival, &dval, nullptr, nullptr)) return 0;
    switch (type) {
        case 1: case 7: { const auto v = static_cast<std::int8_t>(ival); std::memcpy(out_ptr, &v, 1); return 1; }
        case 2: case 8: { const auto v = static_cast<std::int16_t>(ival); std::memcpy(out_ptr, &v, 2); return 1; }
        case 3: case 9: { const auto v = static_cast<std::int32_t>(ival); std::memcpy(out_ptr, &v, 4); return 1; }
        case 4: case 10: case 11: case 14: case 15: {
            std::memcpy(out_ptr, &ival, 8);
            return 1;
        }
        case 5: case 12: { const auto v = static_cast<float>(dval); std::memcpy(out_ptr, &v, 4); return 1; }
        case 6: case 13: case 16: {
            std::memcpy(out_ptr, &dval, 8);
            return 1;
        }
        default: return 0;
    }
}

std::intptr_t shim_CFNumberGetType(ObjcId num) {
    bool is_float = false;
    if (!read_nsnumber(num, nullptr, nullptr, &is_float, nullptr)) return 0;
    return is_float ? 6 : 4;
}

unsigned char shim_CFNumberIsFloatType(ObjcId num) {
    bool is_float = false;
    if (!read_nsnumber(num, nullptr, nullptr, &is_float, nullptr)) return 0;
    return is_float ? 1 : 0;
}

ObjcId shim_CFRetain(ObjcId obj) { return finn_objc_retain(obj); }
void shim_CFRelease(ObjcId obj) { finn_objc_release(obj); }
unsigned char shim_CFEqual(ObjcId a, ObjcId b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    return objc_send_name<signed char>(a, "isEqual:", b) != 0 ? 1 : 0;
}
void shim_CFShow(ObjcId obj) {
    ObjcId desc = obj ? objc_send_name<ObjcId>(obj, "description") : nullptr;
    std::fprintf(stderr, "%s\n", desc ? nsstring_to_utf8(desc).c_str() : "(null)");
}

}  // namespace

void set_foundation_process_args(int argc, char** argv) {
    g_process_args.clear();
    if (argv && argc > 0) {
        g_process_args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            g_process_args.emplace_back(argv[i] ? argv[i] : "");
        }
    }
}

std::string user_defaults_file_path() {
    std::string home = "/tmp";
    if (const char* h = std::getenv("HOME"); h && *h) home = h;
    std::string bundle = g_main_bundle_id;
    if (bundle.empty() && !g_main_bundle_path.empty()) {
        const fs::path plist_path = fs::path(g_main_bundle_path) / "Contents" / "Info.plist";
        if (auto p = parse_plist_file(plist_path.string())) {
            bundle = p->get_string("CFBundleIdentifier");
        }
        if (bundle.empty()) bundle = fs::path(g_main_bundle_path).stem().string();
    }
    return home + "/.local/share/finn/macos/defaults/" + sanitize_bundle_slug(bundle) + ".conf";
}

void reload_user_defaults() {
    g_loaded_defaults_path.clear();
    ensure_defaults_loaded();
}

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

ObjcId nsdata_from_bytes(const void* ptr, std::size_t len, bool mut) {
    auto* d = reinterpret_cast<NSDataObj*>(
        finn_objc_alloc_object(mut ? g_cls_nsmutabledata : g_cls_nsdata, sizeof(NSDataObj)));
    if (ptr && len > 0) {
        d->bytes = new std::vector<std::uint8_t>(
            static_cast<const std::uint8_t*>(ptr),
            static_cast<const std::uint8_t*>(ptr) + len);
    } else {
        d->bytes = new std::vector<std::uint8_t>();
    }
    return reinterpret_cast<ObjcId>(d);
}

ObjcId nsurl_from_path(std::string_view path) {
    return make_nsurl_parsed(path, true);
}

ObjcId nserror_create(std::string_view domain, std::intptr_t code, std::string_view description) {
    ObjcId dom = make_nsstring(domain);
    ObjcId info = make_nsdict(false);
    if (!description.empty()) {
        ObjcId k = make_nsstring("NSLocalizedDescription");
        ObjcId v = make_nsstring(description);
        nsdict_setObjectForKey(info, nullptr, v, k);
        finn_objc_release(k);
        finn_objc_release(v);
    }
    ObjcId err = nserror_errorWithDomain(nullptr, nullptr, dom, code, info);
    finn_objc_release(dom);
    finn_objc_release(info);
    return err;
}

std::string main_bundle_path() { return g_main_bundle_path; }

void init_foundation(const std::string& bundle_path) {
    if (bundle_path != g_main_bundle_path) {
        g_main_bundle_path = bundle_path;
        g_main_bundle_id.clear();
        if (g_main_bundle_obj) {
            auto* b = reinterpret_cast<NSBundleObj*>(g_main_bundle_obj);
            delete b->path;
            finn_objc_release(b->info_dict);
            finn_objc_release(g_main_bundle_obj);
            g_main_bundle_obj = nullptr;
        }
        g_loaded_defaults_path.clear();
    }
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
    finn_class_addMethod(g_cls_nsobject, finn_sel_registerName("copy"), to_imp(&nsobject_copy));
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
    finn_class_addClassMethod(g_cls_nsstring, finn_sel_registerName("stringWithContentsOfFile:encoding:error:"), to_imp(&nsstring_stringWithContentsOfFile));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("init"), to_imp(&nsstring_init));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("initWithUTF8String:"), to_imp(&nsstring_initWithUTF8));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("initWithData:encoding:"), to_imp(&nsstring_initWithDataEncoding));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("initWithContentsOfFile:encoding:error:"), to_imp(&nsstring_initWithContentsOfFile));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("writeToFile:atomically:encoding:error:"), to_imp(&nsstring_writeToFile));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("dataUsingEncoding:"), to_imp(&nsstring_dataUsingEncoding));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("UTF8String"), to_imp(&nsstring_utf8String));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("cStringUsingEncoding:"), to_imp(&nsstring_utf8String));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("length"), to_imp(&nsstring_length));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("isEqualToString:"), to_imp(&nsstring_isEqualToString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("isEqual:"), to_imp(&nsstring_isEqualToString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("hasPrefix:"), to_imp(&nsstring_hasPrefix));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("hasSuffix:"), to_imp(&nsstring_hasSuffix));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("containsString:"), to_imp(&nsstring_containsString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("rangeOfString:"), to_imp(&nsstring_rangeOfString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByAppendingString:"), to_imp(&nsstring_byAppendingString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByAppendingPathComponent:"), to_imp(&nsstring_byAppendingPathComponent));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByDeletingLastPathComponent"), to_imp(&nsstring_byDeletingLastPathComponent));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByDeletingPathExtension"), to_imp(&nsstring_byDeletingPathExtension));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByAppendingPathExtension:"), to_imp(&nsstring_byAppendingPathExtension));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("lastPathComponent"), to_imp(&nsstring_lastPathComponent));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("pathExtension"), to_imp(&nsstring_pathExtension));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("componentsSeparatedByString:"), to_imp(&nsstring_componentsSeparatedByString));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("stringByReplacingOccurrencesOfString:withString:"), to_imp(&nsstring_replacingOccurrences));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("substringFromIndex:"), to_imp(&nsstring_substringFromIndex));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("substringToIndex:"), to_imp(&nsstring_substringToIndex));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("substringWithRange:"), to_imp(&nsstring_substringWithRange));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("lowercaseString"), to_imp(&nsstring_lowercase));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("uppercaseString"), to_imp(&nsstring_uppercase));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("integerValue"), to_imp(&nsstring_integerValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("intValue"), to_imp(&nsstring_integerValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("longLongValue"), to_imp(&nsstring_integerValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("doubleValue"), to_imp(&nsstring_doubleValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("floatValue"), to_imp(&nsstring_floatValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("boolValue"), to_imp(&nsstring_boolValue));
    finn_class_addMethod(g_cls_nsstring, finn_sel_registerName("mutableCopy"), to_imp(&nsstring_mutableCopy));
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
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("containsObject:"), to_imp(&nsarray_containsObject));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("componentsJoinedByString:"), to_imp(&nsarray_componentsJoinedByString));
    finn_class_addMethod(g_cls_nsarray, finn_sel_registerName("mutableCopy"), to_imp(&nsarray_mutableCopy));
    finn_objc_registerClassPair(g_cls_nsarray);

    g_cls_nsmutablearray = finn_objc_allocateClassPair(g_cls_nsarray, "NSMutableArray");
    g_cls_nsmutablearray->cpp_destructor = &nsarray_dtor;
    finn_class_addMethod(g_cls_nsmutablearray, finn_sel_registerName("addObject:"), to_imp(&nsarray_addObject));
    finn_class_addMethod(g_cls_nsmutablearray, finn_sel_registerName("removeLastObject"), to_imp(&nsarray_removeLastObject));
    finn_class_addMethod(g_cls_nsmutablearray, finn_sel_registerName("removeObjectAtIndex:"), to_imp(&nsarray_removeObjectAtIndex));
    finn_class_addMethod(g_cls_nsmutablearray, finn_sel_registerName("removeAllObjects"), to_imp(&nsarray_removeAllObjects));
    finn_objc_registerClassPair(g_cls_nsmutablearray);

    g_cls_nsconstarray = finn_objc_allocateClassPair(g_cls_nsarray, "NSConstantArray");
    finn_objc_registerClassPair(g_cls_nsconstarray);

    // NSDictionary / NSMutableDictionary / NSConstantDictionary
    g_cls_nsdict = finn_objc_allocateClassPair(g_cls_nsobject, "NSDictionary", sizeof(void*));
    g_cls_nsdict->cpp_destructor = &nsdict_dtor;
    finn_class_addClassMethod(g_cls_nsdict, finn_sel_registerName("dictionary"), to_imp(&nsdict_dictionary));
    finn_class_addClassMethod(g_cls_nsdict, finn_sel_registerName("dictionaryWithObjects:forKeys:count:"), to_imp(&nsdict_withObjectsForKeysCount));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("init"), to_imp(&nsdict_init));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("count"), to_imp(&nsdict_count));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("objectForKey:"), to_imp(&nsdict_objectForKey));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("objectForKeyedSubscript:"), to_imp(&nsdict_objectForKey));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("allKeys"), to_imp(&nsdict_allKeys));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("allValues"), to_imp(&nsdict_allValues));
    finn_class_addMethod(g_cls_nsdict, finn_sel_registerName("mutableCopy"), to_imp(&nsdict_mutableCopy));
    finn_objc_registerClassPair(g_cls_nsdict);

    g_cls_nsmutabledict = finn_objc_allocateClassPair(g_cls_nsdict, "NSMutableDictionary");
    g_cls_nsmutabledict->cpp_destructor = &nsdict_dtor;
    finn_class_addMethod(g_cls_nsmutabledict, finn_sel_registerName("setObject:forKey:"), to_imp(&nsdict_setObjectForKey));
    finn_class_addMethod(g_cls_nsmutabledict, finn_sel_registerName("setObject:forKeyedSubscript:"), to_imp(&nsdict_setObjectForKeyedSubscript));
    finn_class_addMethod(g_cls_nsmutabledict, finn_sel_registerName("removeObjectForKey:"), to_imp(&nsdict_removeObjectForKey));
    finn_class_addMethod(g_cls_nsmutabledict, finn_sel_registerName("removeAllObjects"), to_imp(&nsdict_removeAllObjects));
    finn_objc_registerClassPair(g_cls_nsmutabledict);

    g_cls_nsconstdict = finn_objc_allocateClassPair(g_cls_nsdict, "NSConstantDictionary");
    finn_objc_registerClassPair(g_cls_nsconstdict);

    // NSNumber / NSConstantIntegerNumber / NSConstantDoubleNumber / NSConstantFloatNumber
    g_cls_nsnumber = finn_objc_allocateClassPair(g_cls_nsobject, "NSNumber", sizeof(NSNumberObj));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithInt:"), to_imp(&nsnumber_withInt));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithUnsignedInt:"), to_imp(&nsnumber_withInt));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithBool:"), to_imp(&nsnumber_withBool));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithInteger:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithUnsignedInteger:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithLong:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithUnsignedLong:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithLongLong:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithUnsignedLongLong:"), to_imp(&nsnumber_withInteger));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithDouble:"), to_imp(&nsnumber_withDouble));
    finn_class_addClassMethod(g_cls_nsnumber, finn_sel_registerName("numberWithFloat:"), to_imp(&nsnumber_withFloat));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("intValue"), to_imp(&nsnumber_intValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("boolValue"), to_imp(&nsnumber_boolValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("integerValue"), to_imp(&nsnumber_integerValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("unsignedIntegerValue"), to_imp(&nsnumber_integerValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("longLongValue"), to_imp(&nsnumber_longLongValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("doubleValue"), to_imp(&nsnumber_doubleValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("floatValue"), to_imp(&nsnumber_floatValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("isEqualToNumber:"), to_imp(&nsnumber_isEqual));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("isEqual:"), to_imp(&nsnumber_isEqual));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("stringValue"), to_imp(&nsnumber_stringValue));
    finn_class_addMethod(g_cls_nsnumber, finn_sel_registerName("description"), to_imp(&nsnumber_stringValue));
    finn_objc_registerClassPair(g_cls_nsnumber);

    g_cls_nsconstint = finn_objc_allocateClassPair(g_cls_nsnumber, "NSConstantIntegerNumber");
    finn_objc_registerClassPair(g_cls_nsconstint);
    g_cls_nsconstdouble = finn_objc_allocateClassPair(g_cls_nsnumber, "NSConstantDoubleNumber");
    finn_objc_registerClassPair(g_cls_nsconstdouble);
    g_cls_nsconstfloat = finn_objc_allocateClassPair(g_cls_nsnumber, "NSConstantFloatNumber");
    finn_objc_registerClassPair(g_cls_nsconstfloat);

    // NSNull
    g_cls_nsnull = finn_objc_allocateClassPair(g_cls_nsobject, "NSNull");
    finn_class_addClassMethod(g_cls_nsnull, finn_sel_registerName("null"), to_imp(&nsnull_null));
    finn_class_addMethod(g_cls_nsnull, finn_sel_registerName("description"), to_imp(&nsnull_description));
    finn_objc_registerClassPair(g_cls_nsnull);

    // NSData / NSMutableData
    g_cls_nsdata = finn_objc_allocateClassPair(g_cls_nsobject, "NSData", sizeof(void*));
    g_cls_nsdata->cpp_destructor = &nsdata_dtor;
    finn_class_addClassMethod(g_cls_nsdata, finn_sel_registerName("data"), to_imp(&nsdata_data));
    finn_class_addClassMethod(g_cls_nsdata, finn_sel_registerName("dataWithBytes:length:"), to_imp(&nsdata_withBytesLength));
    finn_class_addClassMethod(g_cls_nsdata, finn_sel_registerName("dataWithContentsOfFile:"), to_imp(&nsdata_dataWithContentsOfFile));
    finn_class_addClassMethod(g_cls_nsdata, finn_sel_registerName("dataWithContentsOfFile:options:error:"), to_imp(&nsdata_dataWithContentsOfFileOptsErr));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("init"), to_imp(&nsdata_init));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("initWithBytes:length:"), to_imp(&nsdata_initWithBytesLength));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("bytes"), to_imp(&nsdata_bytes));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("length"), to_imp(&nsdata_length));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("writeToFile:atomically:"), to_imp(&nsdata_writeToFileAtomically));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("writeToFile:options:error:"), to_imp(&nsdata_writeToFileOptsErr));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("isEqualToData:"), to_imp(&nsdata_isEqualToData));
    finn_class_addMethod(g_cls_nsdata, finn_sel_registerName("isEqual:"), to_imp(&nsdata_isEqualToData));
    finn_objc_registerClassPair(g_cls_nsdata);

    g_cls_nsmutabledata = finn_objc_allocateClassPair(g_cls_nsdata, "NSMutableData");
    g_cls_nsmutabledata->cpp_destructor = &nsdata_dtor;
    finn_class_addClassMethod(g_cls_nsmutabledata, finn_sel_registerName("dataWithCapacity:"), to_imp(&nsdata_withCapacity));
    finn_class_addClassMethod(g_cls_nsmutabledata, finn_sel_registerName("dataWithLength:"), to_imp(&nsdata_withLength));
    finn_class_addMethod(g_cls_nsmutabledata, finn_sel_registerName("initWithCapacity:"), to_imp(&nsdata_initWithCapacity));
    finn_class_addMethod(g_cls_nsmutabledata, finn_sel_registerName("appendBytes:length:"), to_imp(&nsmutabledata_appendBytesLength));
    finn_class_addMethod(g_cls_nsmutabledata, finn_sel_registerName("appendData:"), to_imp(&nsmutabledata_appendData));
    finn_class_addMethod(g_cls_nsmutabledata, finn_sel_registerName("mutableBytes"), to_imp(&nsdata_mutableBytes));
    finn_class_addMethod(g_cls_nsmutabledata, finn_sel_registerName("setLength:"), to_imp(&nsmutabledata_setLength));
    finn_objc_registerClassPair(g_cls_nsmutabledata);

    // NSError
    g_cls_nserror = finn_objc_allocateClassPair(g_cls_nsobject, "NSError", sizeof(NSErrorObj));
    g_cls_nserror->cpp_destructor = &nserror_dtor;
    finn_class_addClassMethod(g_cls_nserror, finn_sel_registerName("errorWithDomain:code:userInfo:"), to_imp(&nserror_errorWithDomain));
    finn_class_addMethod(g_cls_nserror, finn_sel_registerName("initWithDomain:code:userInfo:"), to_imp(&nserror_initWithDomain));
    finn_class_addMethod(g_cls_nserror, finn_sel_registerName("domain"), to_imp(&nserror_domain));
    finn_class_addMethod(g_cls_nserror, finn_sel_registerName("code"), to_imp(&nserror_code));
    finn_class_addMethod(g_cls_nserror, finn_sel_registerName("userInfo"), to_imp(&nserror_userInfo));
    finn_class_addMethod(g_cls_nserror, finn_sel_registerName("localizedDescription"), to_imp(&nserror_localizedDescription));
    finn_class_addMethod(g_cls_nserror, finn_sel_registerName("description"), to_imp(&nserror_localizedDescription));
    finn_objc_registerClassPair(g_cls_nserror);

    // NSDate
    g_cls_nsdate = finn_objc_allocateClassPair(g_cls_nsobject, "NSDate", sizeof(NSDateObj));
    finn_class_addClassMethod(g_cls_nsdate, finn_sel_registerName("date"), to_imp(&nsdate_date));
    finn_class_addClassMethod(g_cls_nsdate, finn_sel_registerName("dateWithTimeIntervalSince1970:"), to_imp(&nsdate_with1970));
    finn_class_addClassMethod(g_cls_nsdate, finn_sel_registerName("dateWithTimeIntervalSinceNow:"), to_imp(&nsdate_withSinceNow));
    finn_class_addMethod(g_cls_nsdate, finn_sel_registerName("init"), to_imp(&nsdate_init));
    finn_class_addMethod(g_cls_nsdate, finn_sel_registerName("initWithTimeIntervalSince1970:"), to_imp(&nsdate_initWith1970));
    finn_class_addMethod(g_cls_nsdate, finn_sel_registerName("initWithTimeIntervalSinceNow:"), to_imp(&nsdate_initWithSinceNow));
    finn_class_addMethod(g_cls_nsdate, finn_sel_registerName("timeIntervalSince1970"), to_imp(&nsdate_timeIntervalSince1970));
    finn_class_addMethod(g_cls_nsdate, finn_sel_registerName("timeIntervalSinceNow"), to_imp(&nsdate_timeIntervalSinceNow));
    finn_class_addMethod(g_cls_nsdate, finn_sel_registerName("timeIntervalSinceDate:"), to_imp(&nsdate_timeIntervalSinceDate));
    finn_class_addMethod(g_cls_nsdate, finn_sel_registerName("description"), to_imp(&nsdate_description));
    finn_objc_registerClassPair(g_cls_nsdate);

    // NSAttributedString / NSMutableAttributedString
    g_cls_nsattributedstring = finn_objc_allocateClassPair(
        g_cls_nsobject, "NSAttributedString", sizeof(NSAttributedStringObj));
    g_cls_nsattributedstring->cpp_destructor = &attributed_dtor;
    finn_class_addMethod(g_cls_nsattributedstring,
                         finn_sel_registerName("initWithString:"),
                         to_imp(&attributed_init_string));
    finn_class_addMethod(g_cls_nsattributedstring,
                         finn_sel_registerName("initWithString:attributes:"),
                         to_imp(&attributed_init_with));
    finn_class_addMethod(g_cls_nsattributedstring, finn_sel_registerName("string"),
                         to_imp(&attributed_string));
    finn_class_addMethod(g_cls_nsattributedstring, finn_sel_registerName("length"),
                         to_imp(&attributed_length));
    finn_class_addMethod(g_cls_nsattributedstring,
                         finn_sel_registerName("attributesAtIndex:effectiveRange:"),
                         to_imp(&attributed_attributes_at));
    finn_class_addMethod(g_cls_nsattributedstring,
                         finn_sel_registerName("attributedSubstringFromRange:"),
                         to_imp(&attributed_substring));
    finn_class_addMethod(g_cls_nsattributedstring,
                         finn_sel_registerName("isEqualToAttributedString:"),
                         to_imp(&attributed_is_equal));
    finn_class_addMethod(g_cls_nsattributedstring, finn_sel_registerName("description"),
                         to_imp(&attributed_description));
    finn_objc_registerClassPair(g_cls_nsattributedstring);

    g_cls_nsmutableattributedstring = finn_objc_allocateClassPair(
        g_cls_nsattributedstring, "NSMutableAttributedString", sizeof(NSAttributedStringObj));
    g_cls_nsmutableattributedstring->cpp_destructor = &attributed_dtor;
    finn_class_addMethod(g_cls_nsmutableattributedstring,
                         finn_sel_registerName("setAttributes:range:"),
                         to_imp(&attributed_set_attributes));
    finn_class_addMethod(g_cls_nsmutableattributedstring,
                         finn_sel_registerName("addAttribute:value:range:"),
                         to_imp(&attributed_add_attribute));
    finn_class_addMethod(g_cls_nsmutableattributedstring,
                         finn_sel_registerName("addAttributes:range:"),
                         to_imp(&attributed_add_attributes));
    finn_class_addMethod(g_cls_nsmutableattributedstring,
                         finn_sel_registerName("removeAttribute:range:"),
                         to_imp(&attributed_remove_attribute));
    finn_class_addMethod(g_cls_nsmutableattributedstring,
                         finn_sel_registerName("replaceCharactersInRange:withString:"),
                         to_imp(&attributed_replace_characters));
    finn_class_addMethod(g_cls_nsmutableattributedstring,
                         finn_sel_registerName("deleteCharactersInRange:"),
                         to_imp(&attributed_delete_characters));
    finn_class_addMethod(g_cls_nsmutableattributedstring,
                         finn_sel_registerName("appendAttributedString:"),
                         to_imp(&attributed_append));
    finn_objc_registerClassPair(g_cls_nsmutableattributedstring);

    // NSDateFormatter
    g_cls_nsdateformatter = finn_objc_allocateClassPair(
        g_cls_nsobject, "NSDateFormatter", sizeof(NSDateFormatterObj));
    g_cls_nsdateformatter->cpp_destructor = [](ObjcId obj) {
        delete reinterpret_cast<NSDateFormatterObj*>(obj)->format;
    };
    finn_class_addMethod(g_cls_nsdateformatter, finn_sel_registerName("init"),
                         to_imp(&dateformatter_init));
    finn_class_addMethod(g_cls_nsdateformatter, finn_sel_registerName("setDateFormat:"),
                         to_imp(&dateformatter_set_format));
    finn_class_addMethod(g_cls_nsdateformatter, finn_sel_registerName("dateFormat"),
                         to_imp(&dateformatter_format));
    finn_class_addMethod(g_cls_nsdateformatter, finn_sel_registerName("stringFromDate:"),
                         to_imp(&dateformatter_string_from_date));
    finn_class_addMethod(g_cls_nsdateformatter, finn_sel_registerName("dateFromString:"),
                         to_imp(&dateformatter_date_from_string));
    finn_objc_registerClassPair(g_cls_nsdateformatter);

    // NSCalendar / NSDateComponents (Gregorian, UTC only)
    g_cls_nsdatecomponents = finn_objc_allocateClassPair(
        g_cls_nsobject, "NSDateComponents", sizeof(NSDateComponentsObj));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("init"),
                         to_imp(&datecomponents_init));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("valueForComponent:"),
                         to_imp(&components_get));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("era"),
                         to_imp(&components_get_era));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("year"),
                         to_imp(&components_get_year));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("month"),
                         to_imp(&components_get_month));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("day"),
                         to_imp(&components_get_day));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("hour"),
                         to_imp(&components_get_hour));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("minute"),
                         to_imp(&components_get_minute));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("second"),
                         to_imp(&components_get_second));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("nanosecond"),
                         to_imp(&components_get_nanosecond));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("weekday"),
                         to_imp(&components_get_weekday));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setEra:"),
                         to_imp(&components_set_era));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setYear:"),
                         to_imp(&components_set_year));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setMonth:"),
                         to_imp(&components_set_month));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setDay:"),
                         to_imp(&components_set_day));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setHour:"),
                         to_imp(&components_set_hour));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setMinute:"),
                         to_imp(&components_set_minute));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setSecond:"),
                         to_imp(&components_set_second));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setNanosecond:"),
                         to_imp(&components_set_nanosecond));
    finn_class_addMethod(g_cls_nsdatecomponents, finn_sel_registerName("setWeekday:"),
                         to_imp(&components_set_weekday));
    finn_objc_registerClassPair(g_cls_nsdatecomponents);

    g_cls_nscalendar = finn_objc_allocateClassPair(
        g_cls_nsobject, "NSCalendar", sizeof(NSCalendarObj));
    g_cls_nscalendar->cpp_destructor = [](ObjcId obj) {
        delete reinterpret_cast<NSCalendarObj*>(obj)->identifier;
    };
    finn_class_addClassMethod(g_cls_nscalendar, finn_sel_registerName("currentCalendar"),
                              to_imp(&calendar_current));
    finn_class_addClassMethod(g_cls_nscalendar, finn_sel_registerName("calendarWithIdentifier:"),
                              to_imp(&calendar_with_identifier));
    finn_class_addMethod(g_cls_nscalendar, finn_sel_registerName("calendarIdentifier"),
                         to_imp(&calendar_identifier));
    finn_class_addMethod(g_cls_nscalendar, finn_sel_registerName("component:fromDate:"),
                         to_imp(&calendar_component));
    finn_class_addMethod(g_cls_nscalendar, finn_sel_registerName("components:fromDate:"),
                         to_imp(&calendar_components));
    finn_class_addMethod(g_cls_nscalendar, finn_sel_registerName("dateFromComponents:"),
                         to_imp(&calendar_date_from_components));
    finn_class_addMethod(g_cls_nscalendar,
                         finn_sel_registerName("dateByAddingUnit:value:toDate:options:"),
                         to_imp(&calendar_add_unit));
    finn_objc_registerClassPair(g_cls_nscalendar);

    // NSJSONSerialization
    g_cls_nsjson = finn_objc_allocateClassPair(g_cls_nsobject, "NSJSONSerialization");
    finn_class_addClassMethod(g_cls_nsjson, finn_sel_registerName("JSONObjectWithData:options:error:"), to_imp(&nsjson_objectWithData));
    finn_class_addClassMethod(g_cls_nsjson, finn_sel_registerName("dataWithJSONObject:options:error:"), to_imp(&nsjson_dataWithObject));
    finn_class_addClassMethod(g_cls_nsjson, finn_sel_registerName("isValidJSONObject:"), to_imp(&nsjson_isValidObject));
    finn_objc_registerClassPair(g_cls_nsjson);

    // NSURL
    g_cls_nsurl = finn_objc_allocateClassPair(g_cls_nsobject, "NSURL", sizeof(NSURLObj));
    g_cls_nsurl->cpp_destructor = &nsurl_dtor;
    finn_class_addClassMethod(g_cls_nsurl, finn_sel_registerName("fileURLWithPath:"), to_imp(&nsurl_fileURLWithPath));
    finn_class_addClassMethod(g_cls_nsurl, finn_sel_registerName("URLWithString:"), to_imp(&nsurl_URLWithString));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("path"), to_imp(&nsurl_path));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("absoluteString"), to_imp(&nsurl_absoluteString));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("scheme"), to_imp(&nsurl_scheme));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("host"), to_imp(&nsurl_host));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("lastPathComponent"), to_imp(&nsurl_lastPathComponent));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("pathExtension"), to_imp(&nsurl_pathExtension));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("URLByAppendingPathComponent:"), to_imp(&nsurl_byAppendingPathComponent));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("URLByDeletingLastPathComponent"), to_imp(&nsurl_byDeletingLastPathComponent));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("isFileURL"), to_imp(&nsurl_isFileURL));
    finn_class_addMethod(g_cls_nsurl, finn_sel_registerName("description"), to_imp(&nsurl_absoluteString));
    finn_objc_registerClassPair(g_cls_nsurl);

    // NSBundle
    g_cls_nsbundle = finn_objc_allocateClassPair(g_cls_nsobject, "NSBundle", sizeof(NSBundleObj));
    finn_class_addClassMethod(g_cls_nsbundle, finn_sel_registerName("mainBundle"), to_imp(&nsbundle_mainBundle));
    finn_class_addMethod(g_cls_nsbundle, finn_sel_registerName("bundlePath"), to_imp(&nsbundle_bundlePath));
    finn_class_addMethod(g_cls_nsbundle, finn_sel_registerName("bundleIdentifier"), to_imp(&nsbundle_bundleIdentifier));
    finn_class_addMethod(g_cls_nsbundle, finn_sel_registerName("infoDictionary"), to_imp(&nsbundle_infoDictionary));
    finn_class_addMethod(g_cls_nsbundle, finn_sel_registerName("objectForInfoDictionaryKey:"), to_imp(&nsbundle_objectForInfoKey));
    finn_objc_registerClassPair(g_cls_nsbundle);

    // NSProcessInfo
    g_cls_nsprocessinfo = finn_objc_allocateClassPair(g_cls_nsobject, "NSProcessInfo");
    finn_class_addClassMethod(g_cls_nsprocessinfo, finn_sel_registerName("processInfo"), to_imp(&nsprocessinfo_processInfo));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("operatingSystemVersionString"), to_imp(&nsprocessinfo_osVersionString));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("processorCount"), to_imp(&nsprocessinfo_processorCount));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("activeProcessorCount"), to_imp(&nsprocessinfo_processorCount));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("processName"), to_imp(&nsprocessinfo_processName));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("arguments"), to_imp(&nsprocessinfo_arguments));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("environment"), to_imp(&nsprocessinfo_environment));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("processIdentifier"), to_imp(&nsprocessinfo_pid));
    finn_class_addMethod(g_cls_nsprocessinfo, finn_sel_registerName("hostName"), to_imp(&nsprocessinfo_hostName));
    finn_objc_registerClassPair(g_cls_nsprocessinfo);

    // NSFileManager
    g_cls_nsfilemanager = finn_objc_allocateClassPair(g_cls_nsobject, "NSFileManager");
    finn_class_addClassMethod(g_cls_nsfilemanager, finn_sel_registerName("defaultManager"), to_imp(&nsfilemanager_defaultManager));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("fileExistsAtPath:"), to_imp(&nsfilemanager_fileExists));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("fileExistsAtPath:isDirectory:"), to_imp(&nsfilemanager_fileExistsIsDir));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("currentDirectoryPath"), to_imp(&nsfilemanager_currentDir));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("changeCurrentDirectoryPath:"), to_imp(&nsfilemanager_changeCurrentDir));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("contentsOfDirectoryAtPath:error:"), to_imp(&nsfilemanager_contentsOfDirectory));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("createDirectoryAtPath:withIntermediateDirectories:attributes:error:"), to_imp(&nsfilemanager_createDirectory));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("removeItemAtPath:error:"), to_imp(&nsfilemanager_removeItem));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("copyItemAtPath:toPath:error:"), to_imp(&nsfilemanager_copyItem));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("moveItemAtPath:toPath:error:"), to_imp(&nsfilemanager_moveItem));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("homeDirectoryForCurrentUser"), to_imp(&nsfilemanager_homeDirectory));
    finn_class_addMethod(g_cls_nsfilemanager, finn_sel_registerName("temporaryDirectory"), to_imp(&nsfilemanager_tempDirectory));
    finn_objc_registerClassPair(g_cls_nsfilemanager);

    // NSUserDefaults
    g_cls_nsuserdefaults = finn_objc_allocateClassPair(g_cls_nsobject, "NSUserDefaults");
    finn_class_addClassMethod(g_cls_nsuserdefaults, finn_sel_registerName("standardUserDefaults"), to_imp(&nsuserdefaults_standard));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("objectForKey:"), to_imp(&nsuserdefaults_objectForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("stringForKey:"), to_imp(&nsuserdefaults_stringForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("integerForKey:"), to_imp(&nsuserdefaults_integerForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("boolForKey:"), to_imp(&nsuserdefaults_boolForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("doubleForKey:"), to_imp(&nsuserdefaults_doubleForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("setObject:forKey:"), to_imp(&nsuserdefaults_setObjectForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("setInteger:forKey:"), to_imp(&nsuserdefaults_setIntegerForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("setBool:forKey:"), to_imp(&nsuserdefaults_setBoolForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("setDouble:forKey:"), to_imp(&nsuserdefaults_setDoubleForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("removeObjectForKey:"), to_imp(&nsuserdefaults_removeObjectForKey));
    finn_class_addMethod(g_cls_nsuserdefaults, finn_sel_registerName("synchronize"), to_imp(&nsuserdefaults_synchronize));
    finn_objc_registerClassPair(g_cls_nsuserdefaults);

    // NSNotification / NSNotificationCenter
    g_cls_nsnotification = finn_objc_allocateClassPair(g_cls_nsobject, "NSNotification", sizeof(NSNotificationObj));
    g_cls_nsnotification->cpp_destructor = &nsnotification_dtor;
    finn_class_addClassMethod(g_cls_nsnotification,
                              finn_sel_registerName("notificationWithName:object:userInfo:"),
                              to_imp(&nsnotification_with_name));
    finn_class_addMethod(g_cls_nsnotification, finn_sel_registerName("name"),
                         to_imp(&nsnotification_name));
    finn_class_addMethod(g_cls_nsnotification, finn_sel_registerName("object"),
                         to_imp(&nsnotification_object));
    finn_class_addMethod(g_cls_nsnotification, finn_sel_registerName("userInfo"),
                         to_imp(&nsnotification_userinfo));
    finn_objc_registerClassPair(g_cls_nsnotification);

    g_cls_nsnotificationcenter = finn_objc_allocateClassPair(g_cls_nsobject, "NSNotificationCenter");
    finn_class_addClassMethod(g_cls_nsnotificationcenter, finn_sel_registerName("defaultCenter"), to_imp(&nsnotifcenter_default));
    finn_class_addMethod(g_cls_nsnotificationcenter, finn_sel_registerName("addObserver:selector:name:object:"), to_imp(&nsnotifcenter_addObserver));
    finn_class_addMethod(g_cls_nsnotificationcenter, finn_sel_registerName("removeObserver:"), to_imp(&nsnotifcenter_removeObserver));
    finn_class_addMethod(g_cls_nsnotificationcenter, finn_sel_registerName("removeObserver:name:object:"), to_imp(&nsnotifcenter_removeObserverNameObject));
    finn_class_addMethod(g_cls_nsnotificationcenter, finn_sel_registerName("postNotificationName:object:userInfo:"), to_imp(&nsnotifcenter_post));
    finn_class_addMethod(g_cls_nsnotificationcenter, finn_sel_registerName("postNotification:"), to_imp(&nsnotifcenter_post_notification));
    finn_objc_registerClassPair(g_cls_nsnotificationcenter);

    // NSRunLoop
    g_cls_nsrunloop = finn_objc_allocateClassPair(g_cls_nsobject, "NSRunLoop");
    finn_class_addClassMethod(g_cls_nsrunloop, finn_sel_registerName("currentRunLoop"), to_imp(&nsrunloop_main));
    finn_class_addClassMethod(g_cls_nsrunloop, finn_sel_registerName("mainRunLoop"), to_imp(&nsrunloop_main));
    finn_class_addMethod(g_cls_nsrunloop, finn_sel_registerName("runUntilDate:"), to_imp(&nsrunloop_runUntilDate));
    finn_class_addMethod(g_cls_nsrunloop, finn_sel_registerName("runMode:beforeDate:"), to_imp(&nsrunloop_runModeBeforeDate));
    finn_class_addMethod(g_cls_nsrunloop, finn_sel_registerName("addTimer:forMode:"), to_imp(&nsrunloop_addTimer));
    finn_objc_registerClassPair(g_cls_nsrunloop);

    // NSAutoreleasePool
    g_cls_nsautoreleasepool = finn_objc_allocateClassPair(g_cls_nsobject, "NSAutoreleasePool");
    finn_class_addMethod(g_cls_nsautoreleasepool, finn_sel_registerName("drain"), to_imp(&nsobject_release));
    finn_objc_registerClassPair(g_cls_nsautoreleasepool);

    // Constant singletons (static NSNumberObj so ___kCFBooleanTrue / False work as direct objects)
    g_cf_bool_true_storage.non_heap_prefix[0] = 0;
    g_cf_bool_true_storage.non_heap_prefix[1] = 0;
    g_cf_bool_true_storage.obj.base.isa = g_cls_nsnumber;
    g_cf_bool_true_storage.obj.ival = 1;
    g_cf_bool_true_storage.obj.dval = 1.0;
    g_cf_bool_true_storage.obj.is_float = false;
    g_cf_bool_true_storage.obj.is_bool = true;
    g_cf_bool_true = reinterpret_cast<ObjcId>(&g_cf_bool_true_storage.obj);

    g_cf_bool_false_storage.non_heap_prefix[0] = 0;
    g_cf_bool_false_storage.non_heap_prefix[1] = 0;
    g_cf_bool_false_storage.obj.base.isa = g_cls_nsnumber;
    g_cf_bool_false_storage.obj.ival = 0;
    g_cf_bool_false_storage.obj.dval = 0.0;
    g_cf_bool_false_storage.obj.is_float = false;
    g_cf_bool_false_storage.obj.is_bool = true;
    g_cf_bool_false = reinterpret_cast<ObjcId>(&g_cf_bool_false_storage.obj);

    g_null_singleton = finn_objc_alloc_object(g_cls_nsnull);
    g_const_cocoa_error_domain = make_nsstring("NSCocoaErrorDomain");
    g_const_posix_error_domain = make_nsstring("NSPOSIXErrorDomain");
    g_const_osstatus_error_domain = make_nsstring("NSOSStatusErrorDomain");
    g_const_localized_desc_key = make_nsstring("NSLocalizedDescription");
    g_const_underlying_error_key = make_nsstring("NSUnderlyingError");
    g_const_default_runloop_mode = make_nsstring("kCFRunLoopDefaultMode");
    g_const_common_runloop_modes = make_nsstring("kCFRunLoopCommonModes");
    g_const_nsfont_attribute = make_nsstring("NSFont");
    g_const_nsforeground_attribute = make_nsstring("NSColor");
    g_const_nsbackground_attribute = make_nsstring("NSBackgroundColor");
    g_const_nsunderline_attribute = make_nsstring("NSUnderline");
    g_const_nscalendar_gregorian = make_nsstring("NSGregorianCalendar");
}

void register_foundation_symbols(std::unordered_map<std::string, void*>* table) {
    (*table)["___CFConstantStringClassReference"] =
        g_cls_nsconststring ? static_cast<void*>(g_cls_nsconststring)
                            : static_cast<void*>(&g_cf_const_string_placeholder);

    // Foundation exported string & CF data symbols
    (*table)["_NSCocoaErrorDomain"] = &g_const_cocoa_error_domain;
    (*table)["_NSPOSIXErrorDomain"] = &g_const_posix_error_domain;
    (*table)["_NSOSStatusErrorDomain"] = &g_const_osstatus_error_domain;
    (*table)["_NSLocalizedDescriptionKey"] = &g_const_localized_desc_key;
    (*table)["_NSUnderlyingErrorKey"] = &g_const_underlying_error_key;
    (*table)["_NSDefaultRunLoopMode"] = &g_const_default_runloop_mode;
    (*table)["_NSRunLoopCommonModes"] = &g_const_common_runloop_modes;
    (*table)["_NSFontAttributeName"] = &g_const_nsfont_attribute;
    (*table)["_NSForegroundColorAttributeName"] = &g_const_nsforeground_attribute;
    (*table)["_NSBackgroundColorAttributeName"] = &g_const_nsbackground_attribute;
    (*table)["_NSUnderlineStyleAttributeName"] = &g_const_nsunderline_attribute;
    (*table)["_NSCalendarIdentifierGregorian"] = &g_const_nscalendar_gregorian;
    (*table)["_kCFAllocatorDefault"] = &g_cf_allocator_default;
    (*table)["_kCFBooleanTrue"] = &g_cf_bool_true;
    (*table)["_kCFBooleanFalse"] = &g_cf_bool_false;
    (*table)["___kCFBooleanTrue"] = &g_cf_bool_true_storage.obj;
    (*table)["___kCFBooleanFalse"] = &g_cf_bool_false_storage.obj;
    (*table)["_kCFNull"] = &g_null_singleton;
    (*table)["_kCFTypeArrayCallBacks"] = &g_cf_array_callbacks;
    (*table)["_kCFTypeDictionaryKeyCallBacks"] = &g_cf_dict_key_callbacks;
    (*table)["_kCFTypeDictionaryValueCallBacks"] = &g_cf_dict_val_callbacks;

    // Foundation & CoreFoundation C APIs
    (*table)["_NSLog"] = to_ptr(&shim_NSLog);
    (*table)["_NSSearchPathForDirectoriesInDomains"] =
        to_ptr(&shim_NSSearchPathForDirectoriesInDomains);
    (*table)["_NSHomeDirectory"] = to_ptr(&shim_NSHomeDirectory);
    (*table)["_NSTemporaryDirectory"] = to_ptr(&shim_NSTemporaryDirectory);
    (*table)["_CFRetain"] = to_ptr(&shim_CFRetain);
    (*table)["_CFRelease"] = to_ptr(&shim_CFRelease);
    (*table)["_CFEqual"] = to_ptr(&shim_CFEqual);
    (*table)["_CFShow"] = to_ptr(&shim_CFShow);

    (*table)["_CFStringCreateWithCString"] = to_ptr(&shim_CFStringCreateWithCString);
    (*table)["_CFStringCreateCopy"] = to_ptr(&shim_CFStringCreateCopy);
    (*table)["_CFStringGetCStringPtr"] = to_ptr(&shim_CFStringGetCStringPtr);
    (*table)["_CFStringGetCString"] = to_ptr(&shim_CFStringGetCString);
    (*table)["_CFStringGetLength"] = to_ptr(&shim_CFStringGetLength);
    (*table)["_CFStringCompare"] = to_ptr(&shim_CFStringCompare);

    (*table)["_CFArrayCreate"] = to_ptr(&shim_CFArrayCreate);
    (*table)["_CFArrayCreateMutable"] = to_ptr(&shim_CFArrayCreateMutable);
    (*table)["_CFArrayGetCount"] = to_ptr(&shim_CFArrayGetCount);
    (*table)["_CFArrayGetValueAtIndex"] = to_ptr(&shim_CFArrayGetValueAtIndex);
    (*table)["_CFArrayAppendValue"] = to_ptr(&shim_CFArrayAppendValue);
    (*table)["_CFArrayRemoveAllValues"] = to_ptr(&shim_CFArrayRemoveAllValues);

    (*table)["_CFDictionaryCreate"] = to_ptr(&shim_CFDictionaryCreate);
    (*table)["_CFDictionaryCreateMutable"] = to_ptr(&shim_CFDictionaryCreateMutable);
    (*table)["_CFDictionaryGetCount"] = to_ptr(&shim_CFDictionaryGetCount);
    (*table)["_CFDictionaryGetValue"] = to_ptr(&shim_CFDictionaryGetValue);
    (*table)["_CFDictionarySetValue"] = to_ptr(&shim_CFDictionarySetValue);
    (*table)["_CFDictionaryRemoveValue"] = to_ptr(&shim_CFDictionaryRemoveValue);
    (*table)["_CFDictionaryContainsKey"] = to_ptr(&shim_CFDictionaryContainsKey);

    (*table)["_CFDataCreate"] = to_ptr(&shim_CFDataCreate);
    (*table)["_CFDataCreateMutable"] = to_ptr(&shim_CFDataCreateMutable);
    (*table)["_CFDataGetBytePtr"] = to_ptr(&shim_CFDataGetBytePtr);
    (*table)["_CFDataGetMutableBytePtr"] = to_ptr(&shim_CFDataGetMutableBytePtr);
    (*table)["_CFDataGetLength"] = to_ptr(&shim_CFDataGetLength);
    (*table)["_CFDataAppendBytes"] = to_ptr(&shim_CFDataAppendBytes);
    (*table)["_CFDataGetBytes"] = to_ptr(&shim_CFDataGetBytes);

    (*table)["_CFNumberCreate"] = to_ptr(&shim_CFNumberCreate);
    (*table)["_CFNumberGetValue"] = to_ptr(&shim_CFNumberGetValue);
    (*table)["_CFNumberGetType"] = to_ptr(&shim_CFNumberGetType);
    (*table)["_CFNumberIsFloatType"] = to_ptr(&shim_CFNumberIsFloatType);
}

}  // namespace finn::darwin
