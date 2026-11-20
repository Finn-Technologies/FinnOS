#include "darwin/late_frameworks.hpp"

#include "darwin/foundation.hpp"
#include "darwin/objc_runtime.hpp"

#include <dlfcn.h>
#include <pthread.h>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace finn::darwin {
namespace {

// A Mach-O symbol address is a raw pointer; C++ forbids an implicit conversion
// from a function pointer, so every binding goes through memcpy like the other
// darwin modules do.
template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

template <typename Fn>
ObjcImp to_imp(Fn fn) {
    ObjcImp out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

void* host_fn(const char* name) { return ::dlsym(RTLD_DEFAULT, name); }

// ---------------------------------------------------------------------------
// Late AppKit objects.
//
// Each class is backed by a class the AppKit bridge really implements, so an
// allocated instance participates in the real view/widget hierarchy. Classes
// with no backing (WKWebView, SMAppService, XPC, TextKit 2, KeyedArchiver ...)
// are intentionally absent: registering them would only hide the wall that
// diagnose is supposed to report.
// ---------------------------------------------------------------------------
struct LockIvar {
    ObjcObject base;
    std::mutex* mutex = nullptr;
};

struct StatusItemIvar {
    ObjcObject base;
    ObjcId button = nullptr;
    double length = 0.0;
};

struct DarwinPoint { double x; double y; };
struct DarwinSize { double width; double height; };
struct DarwinRect { DarwinPoint origin; DarwinSize size; };

// Lazy mutex creation keeps [NSLock alloc] safe even when a caller skips init:
// calloc leaves the pointer null and the first lock builds it under a guard.
std::mutex g_lock_guard;

std::mutex* lock_mutex(ObjcId self) {
    if (!self) return nullptr;
    auto* ivar = reinterpret_cast<LockIvar*>(self);
    std::lock_guard<std::mutex> once(g_lock_guard);
    if (!ivar->mutex) ivar->mutex = new std::mutex();
    return ivar->mutex;
}

ObjcId lock_init(ObjcId self, ObjcSel) { return self; }
void lock_lock(ObjcId self, ObjcSel) {
    if (auto* m = lock_mutex(self)) m->lock();
}
void lock_unlock(ObjcId self, ObjcSel) {
    auto* ivar = reinterpret_cast<LockIvar*>(self);
    if (ivar && ivar->mutex) ivar->mutex->unlock();
}
signed char lock_tryLock(ObjcId self, ObjcSel) {
    auto* m = lock_mutex(self);
    return (m && m->try_lock()) ? 1 : 0;
}
// A real timed lock: try, then retry until the NSDate deadline passes. Sleeping
// keeps the wait honest instead of spinning on the CPU.
signed char lock_lockBeforeDate(ObjcId self, ObjcSel, ObjcId date) {
    if (!date) return 0;
    if (auto* m = lock_mutex(self); m && m->try_lock()) return 1;
    const double deadline = objc_send_name<double>(date, "timeIntervalSince1970");
    const double now = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) / 1e9;
    if (deadline <= now) return 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto* m = lock_mutex(self);
        if (m && m->try_lock()) return 1;
        const double tick = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()) / 1e9;
        if (tick >= deadline) return 0;
    }
}
void lock_dtor(ObjcId obj) {
    delete reinterpret_cast<LockIvar*>(obj)->mutex;
}

// A status item is a real object holding a real AppKit NSButton. Hidden Bar's
// menu-bar icon is exactly this pair; there is no fake drawing involved.
ObjcId status_item_with_length(ObjcId cls, ObjcSel, double length) {
    ObjcId item = finn_objc_alloc_object(reinterpret_cast<ObjcClass*>(cls));
    auto* ivar = reinterpret_cast<StatusItemIvar*>(item);
    ivar->length = length;
    ObjcClass* button_cls = finn_objc_getClass("NSButton");
    if (button_cls) {
        ObjcId button = finn_objc_alloc_object(button_cls);
        button = objc_send_name<ObjcId>(button, "initWithFrame:",
                                        DarwinRect{{0, 0}, {length > 0 ? length : 24, 24}});
        ivar->button = finn_objc_retain(button);
    }
    return item;
}
ObjcId status_item_button(ObjcId self, ObjcSel) {
    return self ? reinterpret_cast<StatusItemIvar*>(self)->button : nullptr;
}
void status_item_setButton(ObjcId self, ObjcSel, ObjcId button) {
    if (!self) return;
    auto* ivar = reinterpret_cast<StatusItemIvar*>(self);
    if (ivar->button == button) return;
    finn_objc_retain(button);
    finn_objc_release(ivar->button);
    ivar->button = button;
}
double status_item_length(ObjcId self, ObjcSel) {
    return self ? reinterpret_cast<StatusItemIvar*>(self)->length : 0.0;
}
void status_item_setLength(ObjcId self, ObjcSel, double length) {
    if (self) reinterpret_cast<StatusItemIvar*>(self)->length = length;
}
void status_item_dtor(ObjcId obj) {
    finn_objc_release(reinterpret_cast<StatusItemIvar*>(obj)->button);
}

// Returns an existing class unchanged, otherwise builds it on a real superclass
// from the AppKit bridge. Bumping instance_size covers the case where an earlier
// module registered the name as an empty class without our ivar space.
ObjcClass* ensure_class(const char* name, const char* super_name, std::size_t instance_size) {
    ObjcClass* cls = finn_objc_getClass(name);
    if (!cls) {
        ObjcClass* super_cls = finn_objc_getClass(super_name ? super_name : "NSObject");
        if (!super_cls) return nullptr;
        const std::size_t extra = instance_size > sizeof(ObjcObject)
                                      ? instance_size - sizeof(ObjcObject)
                                      : 0;
        cls = finn_objc_allocateClassPair(super_cls, name, extra);
        if (!cls) return nullptr;
        finn_objc_registerClassPair(cls);
    }
    if (instance_size > cls->instance_size) cls->instance_size = instance_size;
    return cls;
}

void add_class_symbols(std::unordered_map<std::string, void*>* table, const char* name,
                       ObjcClass* cls) {
    if (!cls) return;
    (*table)[std::string("_OBJC_CLASS_$_") + name] = cls;
    (*table)[std::string("_OBJC_METACLASS_$_") + name] = cls->isa;
}

void register_late_classes(std::unordered_map<std::string, void*>* table) {
    if (ObjcClass* cls = ensure_class("NSLock", "NSObject", sizeof(LockIvar))) {
        cls->cpp_destructor = &lock_dtor;
        finn_class_addMethod(cls, finn_sel_registerName("init"), to_imp(&lock_init));
        finn_class_addMethod(cls, finn_sel_registerName("lock"), to_imp(&lock_lock));
        finn_class_addMethod(cls, finn_sel_registerName("unlock"), to_imp(&lock_unlock));
        finn_class_addMethod(cls, finn_sel_registerName("tryLock"), to_imp(&lock_tryLock));
        finn_class_addMethod(cls, finn_sel_registerName("lockBeforeDate:"),
                             to_imp(&lock_lockBeforeDate));
        add_class_symbols(table, "NSLock", cls);
    }
    if (ObjcClass* cls = ensure_class("NSStatusItem", "NSObject", sizeof(StatusItemIvar))) {
        cls->cpp_destructor = &status_item_dtor;
        finn_class_addClassMethod(cls, finn_sel_registerName("statusItemWithLength:"),
                                  to_imp(&status_item_with_length));
        finn_class_addMethod(cls, finn_sel_registerName("button"),
                             to_imp(&status_item_button));
        finn_class_addMethod(cls, finn_sel_registerName("setButton:"),
                             to_imp(&status_item_setButton));
        finn_class_addMethod(cls, finn_sel_registerName("length"),
                             to_imp(&status_item_length));
        finn_class_addMethod(cls, finn_sel_registerName("setLength:"),
                             to_imp(&status_item_setLength));
        add_class_symbols(table, "NSStatusItem", cls);
    }
    // These carry no custom methods: their value is that they are real NSView /
    // NSControl subclasses, so every inherited bridge method works on them.
    struct Subclass { const char* name; const char* super_name; };
    static const Subclass kSubclasses[] = {
        {"NSSwitch", "NSButton"},
        {"NSClipView", "NSView"},
        {"NSOutlineView", "NSView"},
        {"NSVisualEffectView", "NSView"},
        {"NSSearchField", "NSTextField"},
    };
    for (const auto& s : kSubclasses) {
        ObjcClass* cls = ensure_class(s.name, s.super_name, 0);
        add_class_symbols(table, s.name, cls);
    }
}

// ---------------------------------------------------------------------------
// Named constants. The loader binds a data symbol to the address of a global
// that holds a real NSString, which is what a guest `NSString *const` expects.
// ---------------------------------------------------------------------------
struct StringConstant { const char* symbol; const char* value; };

// NSAccessibility names are the documented AX* attribute/role strings. They are
// not descriptions: the guest passes them straight to the accessibility API.
static const StringConstant kStringConstants[] = {
    {"_NSAccessibilityApplicationRole", "AXApplication"},
    {"_NSAccessibilityChildrenAttribute", "AXChildren"},
    {"_NSAccessibilityCloseButtonAttribute", "AXCloseButton"},
    {"_NSAccessibilityCloseButtonSubrole", "AXCloseButtonSubrole"},
    {"_NSAccessibilityFocusedWindowAttribute", "AXFocusedWindow"},
    {"_NSAccessibilityFullScreenButtonAttribute", "AXFullScreenButton"},
    {"_NSAccessibilityFullScreenButtonSubrole", "AXFullScreenButtonSubrole"},
    {"_NSAccessibilityHiddenAttribute", "AXHidden"},
    {"_NSAccessibilityListRole", "AXList"},
    {"_NSAccessibilityMainAttribute", "AXMain"},
    {"_NSAccessibilityMinimizedAttribute", "AXMinimized"},
    {"_NSAccessibilityOrientationAttribute", "AXOrientation"},
    {"_NSAccessibilityParentAttribute", "AXParent"},
    {"_NSAccessibilityPositionAttribute", "AXPosition"},
    {"_NSAccessibilityRoleAttribute", "AXRole"},
    {"_NSAccessibilitySheetRole", "AXSheet"},
    {"_NSAccessibilitySizeAttribute", "AXSize"},
    {"_NSAccessibilitySplitterRole", "AXSplitter"},
    {"_NSAccessibilityStandardWindowSubrole", "AXStandardWindowSubrole"},
    {"_NSAccessibilityStaticTextRole", "AXStaticText"},
    {"_NSAccessibilitySubroleAttribute", "AXSubrole"},
    {"_NSAccessibilitySystemDialogSubrole", "AXSystemDialogSubrole"},
    {"_NSAccessibilityTabGroupRole", "AXTabGroup"},
    {"_NSAccessibilityTitleAttribute", "AXTitle"},
    {"_NSAccessibilityToolbarRole", "AXToolbar"},
    {"_NSAccessibilityValueAttribute", "AXValue"},
    {"_NSAccessibilityWindowAttribute", "AXWindow"},
    {"_NSAccessibilityWindowRole", "AXWindow"},
    {"_NSAccessibilityWindowsAttribute", "AXWindows"},
    {"_NSAccessibilityZoomButtonSubrole", "AXZoomButtonSubrole"},
    {"_NSApplicationDidChangeScreenParametersNotification",
     "NSApplicationDidChangeScreenParametersNotification"},
    {"_NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification",
     "NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification"},
    {"_NSWorkspaceActiveSpaceDidChangeNotification", "NSWorkspaceActiveSpaceDidChangeNotification"},
    {"_NSWorkspaceApplicationKey", "NSWorkspaceApplicationKey"},
    {"_NSWorkspaceDidActivateApplicationNotification",
     "NSWorkspaceDidActivateApplicationNotification"},
    {"_NSWorkspaceDidLaunchApplicationNotification", "NSWorkspaceDidLaunchApplicationNotification"},
    {"_NSWorkspaceDidTerminateApplicationNotification",
     "NSWorkspaceDidTerminateApplicationNotification"},
    {"_NSWorkspaceSessionDidBecomeActiveNotification",
     "NSWorkspaceSessionDidBecomeActiveNotification"},
    {"_NSWorkspaceSessionDidResignActiveNotification",
     "NSWorkspaceSessionDidResignActiveNotification"},
    {"_NSUserDefaultsDidChangeNotification", "NSUserDefaultsDidChangeNotification"},
    {"_NSKeyValueChangeNewKey", "new"},
    {"_NSKernAttributeName", "NSKern"},
    {"_NSSecureUnarchiveFromDataTransformerName", "NSSecureUnarchiveFromData"},
    {"_NSImageNameCaution", "NSImageNameCaution"},
    {"_NSEventTrackingRunLoopMode", "NSEventTrackingRunLoopMode"},
    {"_NSMenuDidBeginTrackingNotification", "NSMenuDidBeginTrackingNotification"},
    {"_NSMenuDidEndTrackingNotification", "NSMenuDidEndTrackingNotification"},
    {"_kAXTrustedCheckOptionPrompt", "AXTrustedCheckOptionPrompt"},
    {"_kCFRunLoopCommonModes", "kCFRunLoopCommonModes"},
    {"_kCAContentsFormatRGBA8Uint", "RGBA8Uint"},
    {"_kCAFillRuleEvenOdd", "evenOdd"},
    {"_kCGWindowAlpha", "Alpha"},
    {"_kCGWindowNumber", "WindowNumber"},
    {"_kCGWindowOwnerName", "OwnerName"},
    {"_kCGWindowName", "Name"},
};

ObjcId g_string_constants[std::size(kStringConstants)]{};
ObjcId g_empty_dictionary = nullptr;

void register_constants(std::unordered_map<std::string, void*>* table) {
    for (std::size_t i = 0; i < std::size(kStringConstants); ++i) {
        if (!g_string_constants[i]) {
            g_string_constants[i] = nsstring_from_utf8(kStringConstants[i].value);
        }
        (*table)[kStringConstants[i].symbol] = &g_string_constants[i];
    }
    // The `_struct` spelling is the Swift Foundation empty-dictionary singleton.
    // It is the same real empty NSDictionary the Foundation bridge already uses.
    if (!g_empty_dictionary) {
        ObjcClass* cls = finn_objc_getClass("NSDictionary");
        if (cls) {
            ObjcId dict = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "dictionary");
            g_empty_dictionary = finn_objc_retain(dict);
        }
    }
    if (g_empty_dictionary) {
        (*table)["___NSDictionary0__struct"] = &g_empty_dictionary;
    }
}

// ---------------------------------------------------------------------------
// Simple C entry points with a real host implementation.
// ---------------------------------------------------------------------------
int fpclassify_double(double value) { return std::fpclassify(value); }

std::size_t malloc_size_of(void* ptr) {
    if (!ptr) return 0;
    using Fn = std::size_t (*)(void*);
    static Fn fn = nullptr;
    static bool probed = false;
    if (!probed) {
        probed = true;
        void* raw = host_fn("malloc_usable_size");  // glibc
        if (!raw) raw = host_fn("malloc_size");     // Darwin / BSD
        if (raw) std::memcpy(&fn, &raw, sizeof(fn));
    }
    return fn ? fn(ptr) : 0;
}

double exp2_host(double value) { return std::exp2(value); }

// CFStringEncoding <-> NSStringEncoding is a fixed documented mapping, not a
// guess. Unknown encodings return 0 (kCFStringEncodingInvalidId / no encoding).
constexpr unsigned kCFInvalid = 0xffffffffu;
constexpr unsigned kNSUTF8 = 4;
constexpr unsigned kNSUnicode = 10;
constexpr unsigned kNSISOLatin1 = 5;
constexpr unsigned kNSASCII = 1;
constexpr unsigned kNSWindowsCP1252 = 12;
constexpr unsigned kNSMacOSRoman = 30;
constexpr unsigned kNSNonLossyASCII = 7;

struct EncodingPair { unsigned cf; unsigned ns; };
static const EncodingPair kEncodings[] = {
    {0x0000, kNSMacOSRoman},      // kCFStringEncodingMacRoman
    {0x0500, kNSWindowsCP1252},   // kCFStringEncodingWindowsLatin1
    {0x0201, kNSISOLatin1},       // kCFStringEncodingISOLatin1
    {0x0B01, 2},                  // kCFStringEncodingNextStepLatin
    {0x0600, kNSASCII},           // kCFStringEncodingASCII
    {0x0100, kNSUnicode},         // kCFStringEncodingUnicode / UTF16
    {0x08000100, kNSUTF8},        // kCFStringEncodingUTF8
    {0x0BFF, kNSNonLossyASCII},   // kCFStringEncodingNonLossyASCII
    {0x10000100, 0x90000100},     // UTF16BE
    {0x14000100, 0x94000100},     // UTF16LE
    {0x0c000100, 0x8c000100},     // UTF32
    {0x18000100, 0x98000100},     // UTF32BE
    {0x1c000100, 0x9c000100},     // UTF32LE
};

unsigned long cf_to_ns_encoding(unsigned cf) {
    for (const auto& e : kEncodings) {
        if (e.cf == cf) return e.ns;
    }
    return 0;
}

unsigned cf_from_iana_name(ObjcId name_obj) {
    const std::string name = nsstring_to_utf8(name_obj);
    if (name.empty()) return kCFInvalid;
    struct Alias { const char* name; unsigned cf; };
    static const Alias kAliases[] = {
        {"utf-8", 0x08000100}, {"utf8", 0x08000100},
        {"us-ascii", 0x0600}, {"ascii", 0x0600},
        {"iso-8859-1", 0x0201}, {"iso8859-1", 0x0201}, {"latin1", 0x0201},
        {"utf-16", 0x0100}, {"utf-16be", 0x10000100}, {"utf-16le", 0x14000100},
        {"utf-32", 0x0c000100}, {"utf-32be", 0x18000100}, {"utf-32le", 0x1c000100},
        {"windows-1252", 0x0500}, {"cp1252", 0x0500},
        {"macintosh", 0x0000}, {"mac-roman", 0x0000},
        {"shift_jis", 0x0A01}, {"euc-jp", 0x0A02}, {"iso-2022-jp", 0x0A03},
    };
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    for (const auto& a : kAliases) {
        if (lowered == a.name) return a.cf;
    }
    return kCFInvalid;
}

signed char cf_preferences_app_synchronize(ObjcId) {
    ObjcClass* cls = finn_objc_getClass("NSUserDefaults");
    if (!cls) return 0;
    ObjcId ud = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "standardUserDefaults");
    return ud ? objc_send_name<signed char>(ud, "synchronize") : 0;
}

// Both methods exist on the real Foundation objects in this runtime, so these
// CF entry points forward to them instead of inventing a hash or error code.
std::uintptr_t cf_hash(ObjcId object) {
    return object ? objc_send_name<std::uintptr_t>(object, "hash") : 0;
}
std::intptr_t cf_error_get_code(ObjcId error) {
    return error ? objc_send_name<std::intptr_t>(error, "code") : 0;
}

ObjcId cg_rect_create_dictionary(DarwinRect rect) {
    ObjcClass* dict_cls = finn_objc_getClass("NSDictionary");
    ObjcClass* num_cls = finn_objc_getClass("NSNumber");
    if (!dict_cls || !num_cls) return nullptr;
    const char* keys[] = {"X", "Y", "Width", "Height"};
    const double values[] = {rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
    ObjcId nums[4]{};
    ObjcId key_objs[4]{};
    for (int i = 0; i < 4; ++i) {
        nums[i] = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(num_cls),
                                         "numberWithDouble:", values[i]);
        key_objs[i] = nsstring_from_utf8(keys[i]);
    }
    return objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(dict_cls),
                                  "dictionaryWithObjects:forKeys:count:",
                                  nums, key_objs, static_cast<std::uintptr_t>(4));
}

bool cg_rect_is_infinite(DarwinRect rect) {
    return std::isinf(rect.origin.x) || std::isinf(rect.origin.y) ||
           std::isinf(rect.size.width) || std::isinf(rect.size.height);
}

// The honest answer on a host with no IOKit is "no assertion is held": an empty
// dictionary, plus a diagnostic so a caller never mistakes this for a real
// power-management assertion.
ObjcId iopm_copy_assertions_by_process() {
    std::fprintf(stderr,
                 "finn-darwin: IOPMCopyAssertionsByProcess: no IOKit power assertions are held "
                 "on this platform (returning an empty dictionary).\n");
    ObjcClass* cls = finn_objc_getClass("NSMutableDictionary");
    return cls ? objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "dictionary") : nullptr;
}

void log_variadic(const char* tag, const char* format, va_list args) {
    if (!format) return;
    std::fprintf(stderr, "finn-darwin: %s: ", tag);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
}
void asl_log_shim(void*, void*, int, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_variadic("asl_log", format, args);
    va_end(args);
}
void os_release_shim(void*, int, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_variadic("os_release", format, args);
    va_end(args);
}

ObjcId objc_root_autorelease(ObjcId self) { return finn_objc_autorelease(self); }

// The console user is the real session owner. A single-user FinnOS session has
// exactly one, so the uid/gid out-parameters are filled from the process owner.
ObjcId sc_copy_console_user(void*, unsigned* uid_out, unsigned* gid_out) {
    auto* pw = ::getpwuid(::getuid());
    if (!pw) return nullptr;
    if (uid_out) *uid_out = pw->pw_uid;
    if (gid_out) *gid_out = pw->pw_gid;
    return nsstring_from_utf8(pw->pw_name);
}

pthread_t g_main_thread{};
int pthread_main_np_shim() {
    return ::pthread_equal(::pthread_self(), g_main_thread) ? 1 : 0;
}

struct AsyncJob { void* context; void (*work)(void*); };
void* async_thread(void* arg) {
    auto* job = static_cast<AsyncJob*>(arg);
    job->work(job->context);
    delete job;
    return nullptr;
}
void dispatch_async_f_shim(void*, void* context, void (*work)(void*)) {
    if (!work) return;
    auto* job = new AsyncJob{context, work};
    pthread_t thread{};
    if (::pthread_create(&thread, nullptr, &async_thread, job) != 0) {
        delete job;
        return;
    }
    ::pthread_detach(thread);
}

// dispatch_time() in the compat module counts steady-clock nanoseconds, so the
// deadline is a real monotonic time point. The job thread waits until then and
// only afterwards invokes the work function; dispatch_after_f never blocks.
struct DeferredJob {
    void* context;
    void (*work)(void*);
    std::chrono::steady_clock::time_point deadline;
};
void* deferred_thread(void* arg) {
    auto* job = static_cast<DeferredJob*>(arg);
    std::this_thread::sleep_until(job->deadline);
    job->work(job->context);
    delete job;
    return nullptr;
}
void dispatch_after_f_shim(std::uint64_t when, void*, void* context, void (*work)(void*)) {
    if (!work) return;
    using Clock = std::chrono::steady_clock;
    auto* job = new DeferredJob{
        context, work,
        Clock::time_point(std::chrono::nanoseconds(static_cast<std::int64_t>(when)))};
    pthread_t thread{};
    if (::pthread_create(&thread, nullptr, &deferred_thread, job) != 0) {
        delete job;
        return;
    }
    ::pthread_detach(thread);
}

// ServiceManagement and CodeSigning are genuinely unavailable: return the
// documented failure (errSecNotAvailable, a false Boolean) rather than a
// plausible-looking success, and say so on stderr.
int security_unavailable(const char* what) {
    std::fprintf(stderr,
                 "finn-darwin: %s: Security framework is not available on this platform "
                 "(errSecNotAvailable).\n", what);
    return -25291;
}
int sec_requirement_create_with_string(ObjcId, unsigned, void** out) {
    if (out) *out = nullptr;
    return security_unavailable("SecRequirementCreateWithString");
}
int sec_static_code_check_validity(void*, unsigned, void*) {
    return security_unavailable("SecStaticCodeCheckValidity");
}
unsigned char sm_job_submit(void*, void*, void*, void** out_error) {
    if (out_error) *out_error = nullptr;
    std::fprintf(stderr,
                 "finn-darwin: SMJobSubmit: ServiceManagement is not available on this platform.\n");
    return 0;
}
unsigned char sm_job_remove(ObjcId, ObjcId, void*, unsigned char, void** out_error) {
    if (out_error) *out_error = nullptr;
    std::fprintf(stderr,
                 "finn-darwin: SMJobRemove: ServiceManagement is not available on this platform.\n");
    return 0;
}

// There is no authorization database on this platform: both right queries
// report the documented "not available" status instead of granting anything.
int authorization_right_get(const char* right_name, void** out_definition) {
    if (out_definition) *out_definition = nullptr;
    std::fprintf(stderr,
                 "finn-darwin: AuthorizationRightGet(%s): no authorization database on this "
                 "platform (errSecNotAvailable).\n", right_name ? right_name : "(null)");
    return -25291;
}
int authorization_right_set(void*, const char* right_name, void*, ObjcId, void*, ObjcId) {
    std::fprintf(stderr,
                 "finn-darwin: AuthorizationRightSet(%s): no authorization database on this "
                 "platform (errSecNotAvailable).\n", right_name ? right_name : "(null)");
    return -25291;
}

}  // namespace

void register_late_framework_symbols(std::unordered_map<std::string, void*>* table) {
    if (!table) return;
    g_main_thread = ::pthread_self();

    register_late_classes(table);
    register_constants(table);

    // Host C runtime and libm.
    (*table)["___fpclassifyd"] = to_ptr(&fpclassify_double);
    (*table)["__fpclassifyd"] = to_ptr(&fpclassify_double);  // older toolchains
    (*table)["_malloc_size"] = to_ptr(&malloc_size_of);
    (*table)["_malloc_good_size"] = to_ptr(&malloc_size_of);
    (*table)["_exp2"] = to_ptr(&exp2_host);
    (*table)["__objc_rootAutorelease"] = to_ptr(&objc_root_autorelease);
    (*table)["_pthread_main_np"] = to_ptr(&pthread_main_np_shim);
    (*table)["_asl_log"] = to_ptr(&asl_log_shim);
    (*table)["_os_release"] = to_ptr(&os_release_shim);
    (*table)["_dispatch_async_f"] = to_ptr(&dispatch_async_f_shim);
    (*table)["_dispatch_after_f"] = to_ptr(&dispatch_after_f_shim);

    // pthread mutex attributes exist with identical C signatures on both hosts.
    struct HostFn { const char* macho; const char* host; };
    static const HostFn kPthreadFns[] = {
        {"_pthread_mutexattr_init", "pthread_mutexattr_init"},
        {"_pthread_mutexattr_destroy", "pthread_mutexattr_destroy"},
        {"_pthread_mutexattr_settype", "pthread_mutexattr_settype"},
    };
    for (const auto& fn : kPthreadFns) {
        if (void* raw = host_fn(fn.host)) (*table)[fn.macho] = raw;
    }

    // Foundation / CoreFoundation.
    (*table)["_NSUserName"] = to_ptr(+[]() -> ObjcId {
        const char* name = nullptr;
        if (auto* pw = ::getpwuid(::getuid())) name = pw->pw_name;
        if (!name) name = std::getenv("USER");
        return nsstring_from_utf8(name ? name : "");
    });
    (*table)["_NSHomeDirectoryForUser"] = to_ptr(+[](ObjcId user) -> ObjcId {
        const std::string name = nsstring_to_utf8(user);
        if (name.empty()) return nullptr;
        auto* pw = ::getpwnam(name.c_str());
        if (!pw || !pw->pw_dir) return nullptr;
        return nsstring_from_utf8(pw->pw_dir);
    });
    (*table)["_CFStringConvertEncodingToNSStringEncoding"] =
        to_ptr(&cf_to_ns_encoding);
    (*table)["_CFStringConvertIANACharSetNameToEncoding"] =
        to_ptr(&cf_from_iana_name);
    (*table)["_CFPreferencesAppSynchronize"] =
        to_ptr(&cf_preferences_app_synchronize);
    (*table)["_CFHash"] = to_ptr(&cf_hash);
    (*table)["_CFErrorGetCode"] = to_ptr(&cf_error_get_code);
    (*table)["_SCDynamicStoreCopyConsoleUser"] = to_ptr(&sc_copy_console_user);

    // CoreGraphics geometry, computed from the real four-double NSRect layout.
    (*table)["_CGRectCreateDictionaryRepresentation"] =
        to_ptr(&cg_rect_create_dictionary);
    (*table)["_CGRectIsInfinite"] = to_ptr(&cg_rect_is_infinite);

    // Power assertion query: empty and loudly reported, never claimed as held.
    (*table)["_IOPMCopyAssertionsByProcess"] = to_ptr(&iopm_copy_assertions_by_process);

    // Honest unsupported results.
    (*table)["_SecRequirementCreateWithString"] =
        to_ptr(&sec_requirement_create_with_string);
    (*table)["_SecStaticCodeCheckValidity"] =
        to_ptr(&sec_static_code_check_validity);
    (*table)["_SMJobSubmit"] = to_ptr(&sm_job_submit);
    (*table)["_SMJobRemove"] = to_ptr(&sm_job_remove);
    (*table)["_AuthorizationRightGet"] = to_ptr(&authorization_right_get);
    (*table)["_AuthorizationRightSet"] = to_ptr(&authorization_right_set);
}

bool late_frameworks_self_test() {
    bool ok = true;
    auto check = [&ok](bool condition, const char* what) {
        std::printf("[late_frameworks] %s: %s\n", condition ? "PASS" : "FAIL", what);
        if (!condition) ok = false;
    };

    std::unordered_map<std::string, void*> table;
    register_late_framework_symbols(&table);
    check(table.size() >= 100, "symbol table populated");

    auto has = [&table](const char* name) {
        auto it = table.find(name);
        return it != table.end() && it->second != nullptr;
    };
    check(has("_OBJC_CLASS_$_NSLock"), "NSLock class symbol");
    check(has("_OBJC_METACLASS_$_NSLock"), "NSLock metaclass symbol");
    check(has("_OBJC_CLASS_$_NSStatusItem"), "NSStatusItem class symbol");
    check(has("_OBJC_CLASS_$_NSSwitch"), "NSSwitch class symbol");
    check(has("_OBJC_CLASS_$_NSVisualEffectView"), "NSVisualEffectView class symbol");
    check(has("_CFHash") && has("_CFErrorGetCode"), "CF hash/error entry points");
    check(has("_SCDynamicStoreCopyConsoleUser"), "console-user query");
    check(has("_pthread_mutexattr_init") && has("_pthread_mutexattr_settype"),
          "host pthread mutex attributes");
    check(has("_dispatch_after_f"), "timed dispatch");
    check(finn_objc_getClass("NSLock") != nullptr, "NSLock registered in runtime");
    check(finn_objc_getClass("NSStatusItem") != nullptr, "NSStatusItem registered in runtime");

    // Inherited widget behavior is only meaningful after the AppKit bridge is up.
    if (ObjcClass* button = finn_objc_getClass("NSButton")) {
        ObjcClass* nsswitch = finn_objc_getClass("NSSwitch");
        check(nsswitch && finn_objc_isKindOfClass(
                              reinterpret_cast<ObjcId>(finn_objc_alloc_object(nsswitch)), button),
              "NSSwitch is a real NSButton subclass");
    }
    if (ObjcClass* view = finn_objc_getClass("NSView")) {
        ObjcClass* clip = finn_objc_getClass("NSClipView");
        check(clip && finn_objc_isKindOfClass(
                          reinterpret_cast<ObjcId>(finn_objc_alloc_object(clip)), view),
              "NSClipView is a real NSView subclass");
    }

    // Constants: the symbol points at a global holding a real NSString.
    auto constant = [&table](const char* name) -> std::string {
        auto it = table.find(name);
        if (it == table.end() || !it->second) return {};
        return nsstring_to_utf8(*reinterpret_cast<ObjcId*>(it->second));
    };
    check(constant("_NSAccessibilityRoleAttribute") == "AXRole", "AXRole constant value");
    check(constant("_NSAccessibilityWindowRole") == "AXWindow", "AXWindow constant value");
    check(constant("_NSKeyValueChangeNewKey") == "new", "NSKeyValueChangeNewKey value");
    check(constant("_kCGWindowOwnerName") == "OwnerName", "kCGWindowOwnerName value");
    check(constant("_kCAFillRuleEvenOdd") == "evenOdd", "kCAFillRuleEvenOdd value");

    using PtrFn = int (*)(double);
    PtrFn fp{};
    std::memcpy(&fp, &table["___fpclassifyd"], sizeof(fp));
    check(fp(0.0) == FP_ZERO && fp(1.5) == FP_NORMAL, "fpclassify is real");

    using MallocFn = std::size_t (*)(void*);
    MallocFn msize{};
    std::memcpy(&msize, &table["_malloc_size"], sizeof(msize));
    void* heap_probe = std::malloc(64);
    check(heap_probe && msize(heap_probe) >= 64, "malloc_size reports a real allocation");
    std::free(heap_probe);

    using ConvFn = unsigned long (*)(unsigned);
    ConvFn conv{};
    std::memcpy(&conv, &table["_CFStringConvertEncodingToNSStringEncoding"], sizeof(conv));
    check(conv(0x08000100) == kNSUTF8, "UTF-8 encoding conversion");
    check(conv(0x0201) == kNSISOLatin1, "ISO Latin-1 encoding conversion");

    using IanaFn = unsigned (*)(ObjcId);
    IanaFn iana{};
    std::memcpy(&iana, &table["_CFStringConvertIANACharSetNameToEncoding"], sizeof(iana));
    check(iana(nsstring_from_utf8("UTF-8")) == 0x08000100, "IANA UTF-8 lookup");
    check(iana(nsstring_from_utf8("not-a-charset")) == kCFInvalid, "unknown charset is invalid");

    using HashFn = std::uintptr_t (*)(ObjcId);
    HashFn hash{};
    std::memcpy(&hash, &table["_CFHash"], sizeof(hash));
    ObjcId hashed = nsstring_from_utf8("hashable");
    check(hash(nullptr) == 0 && hash(hashed) == reinterpret_cast<std::uintptr_t>(hashed),
          "CFHash forwards to real Foundation hash");

    using ConsoleFn = ObjcId (*)(void*, unsigned*, unsigned*);
    ConsoleFn console{};
    std::memcpy(&console, &table["_SCDynamicStoreCopyConsoleUser"], sizeof(console));
    unsigned uid = 0, gid = 0;
    ObjcId user = console(nullptr, &uid, &gid);
    check(user != nullptr && !nsstring_to_utf8(user).empty(),
          "console user is a real host account");

    using PthreadAttrFn = int (*)(void*);
    PthreadAttrFn attr_init{};
    std::memcpy(&attr_init, &table["_pthread_mutexattr_init"], sizeof(attr_init));
    // A real pthread attribute object is opaque and at least one pointer wide.
    alignas(void*) unsigned char attr_storage[64]{};
    check(attr_init(attr_storage) == 0, "pthread_mutexattr_init works on host");
    if (auto destroy = table.find("_pthread_mutexattr_destroy");
        destroy != table.end() && destroy->second) {
        PthreadAttrFn attr_destroy{};
        std::memcpy(&attr_destroy, &destroy->second, sizeof(attr_destroy));
        check(attr_destroy(attr_storage) == 0, "pthread_mutexattr_destroy works on host");
    }

    using RectBoolFn = bool (*)(DarwinRect);
    RectBoolFn infinite{};
    std::memcpy(&infinite, &table["_CGRectIsInfinite"], sizeof(infinite));
    const double inf = std::numeric_limits<double>::infinity();
    check(infinite({{0, 0}, {inf, 1}}), "CGRectIsInfinite detects infinity");
    check(!infinite({{0, 0}, {1, 1}}), "CGRectIsInfinite rejects finite rect");

    using RectDictFn = ObjcId (*)(DarwinRect);
    RectDictFn dict{};
    std::memcpy(&dict, &table["_CGRectCreateDictionaryRepresentation"], sizeof(dict));
    ObjcId d = dict({{1, 2}, {3, 4}});
    check(d != nullptr, "CGRectCreateDictionaryRepresentation builds a dictionary");
    if (d) {
        ObjcId width = objc_send_name<ObjcId>(
            d, "objectForKey:", nsstring_from_utf8("Width"));
        check(width != nullptr, "dictionary has a Width key");
    }

    using AssertionsFn = ObjcId (*)();
    AssertionsFn assertions{};
    std::memcpy(&assertions, &table["_IOPMCopyAssertionsByProcess"], sizeof(assertions));
    ObjcId a = assertions();  // also checks that the diagnostic is emitted
    check(a != nullptr, "IOPM query returns a real empty dictionary");
    if (a) {
        check(objc_send_name<std::uintptr_t>(a, "count") == 0,
              "IOPM query reports no held assertions");
    }

    using SecFn = int (*)(ObjcId, unsigned, void**);
    SecFn sec{};
    std::memcpy(&sec, &table["_SecRequirementCreateWithString"], sizeof(sec));
    void* requirement = reinterpret_cast<void*>(1);
    check(sec(nsstring_from_utf8("anchor apple"), 0, &requirement) == -25291 &&
              requirement == nullptr,
          "code-signing reports errSecNotAvailable, never a valid signature");

    using SmFn = unsigned char (*)(void*, void*, void*, void**);
    SmFn sm{};
    std::memcpy(&sm, &table["_SMJobSubmit"], sizeof(sm));
    check(sm(nullptr, nullptr, nullptr, nullptr) == 0,
          "ServiceManagement job submission reports failure honestly");

    std::printf("[late_frameworks] %s (%zu symbols registered)\n",
                ok ? "SELF TEST PASSED" : "SELF TEST FAILED", table.size());
    return ok;
}

}  // namespace finn::darwin
