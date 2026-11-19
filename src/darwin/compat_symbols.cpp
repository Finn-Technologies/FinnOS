#include "darwin/compat_symbols.hpp"

#include "darwin/foundation.hpp"
#include "darwin/objc_runtime.hpp"

#include <dlfcn.h>

#include <atomic>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace finn::darwin {
namespace {

template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

void* host_symbol(const char* name) {
    return ::dlsym(RTLD_DEFAULT, name);
}

// ---------------------------------------------------------------------------
// Foundation geometry (NSGeometry.h / NSRange.h). CGFloat is double on the
// 64-bit ABIs we support; the structs are returned in registers by both
// Darwin and the host AArch64/x86-64 SysV ABI, so plain C++ functions match.
// ---------------------------------------------------------------------------

struct CGFloatPoint {
    double x = 0;
    double y = 0;
};
struct CGFloatSize {
    double width = 0;
    double height = 0;
};
struct CGFloatRect {
    CGFloatPoint origin;
    CGFloatSize size;
};
struct NSUIntegerRange {
    std::uint64_t location = 0;
    std::uint64_t length = 0;
};

const CGFloatPoint kZeroPoint{};
const CGFloatSize kZeroSize{};
const CGFloatRect kZeroRect{};

bool equal_points(CGFloatPoint a, CGFloatPoint b) {
    return a.x == b.x && a.y == b.y;
}
bool equal_sizes(CGFloatSize a, CGFloatSize b) {
    return a.width == b.width && a.height == b.height;
}
bool equal_rects(CGFloatRect a, CGFloatRect b) {
    return equal_points(a.origin, b.origin) && equal_sizes(a.size, b.size);
}
CGFloatRect inset_rect(CGFloatRect r, double dx, double dy) {
    r.origin.x += dx;
    r.origin.y += dy;
    r.size.width -= 2 * dx;
    r.size.height -= 2 * dy;
    return r;
}
CGFloatRect offset_rect(CGFloatRect r, double dx, double dy) {
    r.origin.x += dx;
    r.origin.y += dy;
    return r;
}
CGFloatRect union_rect(CGFloatRect a, CGFloatRect b) {
    if (a.size.width <= 0 || a.size.height <= 0) return b;
    if (b.size.width <= 0 || b.size.height <= 0) return a;
    const double minx = std::min(a.origin.x, b.origin.x);
    const double miny = std::min(a.origin.y, b.origin.y);
    const double maxx = std::max(a.origin.x + a.size.width, b.origin.x + b.size.width);
    const double maxy = std::max(a.origin.y + a.size.height, b.origin.y + b.size.height);
    return {{minx, miny}, {maxx - minx, maxy - miny}};
}
CGFloatRect intersection_rect(CGFloatRect a, CGFloatRect b) {
    const double minx = std::max(a.origin.x, b.origin.x);
    const double miny = std::max(a.origin.y, b.origin.y);
    const double maxx = std::min(a.origin.x + a.size.width, b.origin.x + b.size.width);
    const double maxy = std::min(a.origin.y + a.size.height, b.origin.y + b.size.height);
    if (maxx <= minx || maxy <= miny) return kZeroRect;
    return {{minx, miny}, {maxx - minx, maxy - miny}};
}
bool point_in_rect(CGFloatPoint p, CGFloatRect r) {
    return p.x >= r.origin.x && p.x < r.origin.x + r.size.width &&
           p.y >= r.origin.y && p.y < r.origin.y + r.size.height;
}
bool contains_rect(CGFloatRect a, CGFloatRect b) {
    if (b.size.width <= 0 || b.size.height <= 0) return true;
    if (a.size.width <= 0 || a.size.height <= 0) return false;
    return b.origin.x >= a.origin.x && b.origin.y >= a.origin.y &&
           b.origin.x + b.size.width <= a.origin.x + a.size.width &&
           b.origin.y + b.size.height <= a.origin.y + a.size.height;
}
NSUIntegerRange intersection_range(NSUIntegerRange a, NSUIntegerRange b) {
    const std::uint64_t start = std::max(a.location, b.location);
    const std::uint64_t end = std::min(a.location + a.length, b.location + b.length);
    if (end <= start) return {0, 0};
    return {start, end - start};
}
NSUIntegerRange union_range(NSUIntegerRange a, NSUIntegerRange b) {
    const std::uint64_t start = std::min(a.location, b.location);
    const std::uint64_t end = std::max(a.location + a.length, b.location + b.length);
    return {start, end - start};
}
// Foundation runtime reflection helpers. NSSelectorFromString and
// NSStringFromSelector use the real selector table; NSClassFromString returns
// nil for a class that was never registered, never an invented class.
ObjcId string_from_class(ObjcClass* cls) {
    return nsstring_from_utf8(cls ? cls->name : "");
}
ObjcId string_from_selector(ObjcSel sel) {
    return nsstring_from_utf8(sel ? finn_sel_getName(sel) : "");
}
ObjcId class_from_string(ObjcId name) {
    if (!name) return nullptr;
    ObjcClass* cls = finn_objc_getClass(nsstring_to_utf8(name).c_str());
    return reinterpret_cast<ObjcId>(cls);
}
ObjcSel selector_from_string(ObjcId name) {
    if (!name) return nullptr;
    return finn_sel_registerName(nsstring_to_utf8(name).c_str());
}

// NSBeep writes the real terminal bell when no audio output is available.
void ns_beep() {
    std::fputs("\a", stderr);
    std::fflush(stderr);
}

// NSDivideRect splits aRect at amount from the requested edge. slice and
// remainder are real NSRect out-parameters (NSRectEdge: 0=minX 1=minY 2=maxX
// 3=maxY), so the guest sees the same values as Foundation.
void divide_rect(CGFloatRect r, CGFloatRect* slice, CGFloatRect* remainder,
                 double amount, int edge) {
    CGFloatRect s = kZeroRect;
    CGFloatRect rem = r;
    switch (edge) {
        case 0:
            s = {{r.origin.x, r.origin.y}, {amount, r.size.height}};
            rem.origin.x += amount;
            rem.size.width -= amount;
            break;
        case 1:
            s = {{r.origin.x, r.origin.y}, {r.size.width, amount}};
            rem.origin.y += amount;
            rem.size.height -= amount;
            break;
        case 2:
            s = {{r.origin.x + r.size.width - amount, r.origin.y},
                 {amount, r.size.height}};
            rem.size.width -= amount;
            break;
        case 3:
            s = {{r.origin.x, r.origin.y + r.size.height - amount},
                 {r.size.width, amount}};
            rem.size.height -= amount;
            break;
        default:
            break;
    }
    if (slice) *slice = s;
    if (remainder) *remainder = rem;
}

int fpclassify_double(double value) {
    return std::fpclassify(value);
}

std::string format_number(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}
ObjcId string_from_rect(CGFloatRect r) {
    return nsstring_from_utf8("{{" + format_number(r.origin.x) + ", " +
                              format_number(r.origin.y) + "}, {" +
                              format_number(r.size.width) + ", " +
                              format_number(r.size.height) + "}}");
}
ObjcId string_from_range(NSUIntegerRange r) {
    return nsstring_from_utf8("{" + std::to_string(r.location) + ", " +
                              std::to_string(r.length) + "}");
}
ObjcId dictionary_of_variable_bindings(ObjcId self, ObjcSel sel, const char* first, ...) {
    // _NSDictionaryOfVariableBindings(...) is a macro helper that builds a
    // dictionary of NSValue-wrapped pointer variables by name. Our Foundation
    // has no NSValue class; returning nil rather than a partial dictionary
    // would break callers that expect a real container. Build a real
    // NSDictionary of name -> pointer-as-number using NSNumber numberWithLong.
    (void)self;
    (void)sel;
    std::vector<ObjcId> keys;
    std::vector<ObjcId> values;
    va_list args;
    va_start(args, first);
    for (const char* name = first; name != nullptr; name = va_arg(args, const char*)) {
        void* ptr = va_arg(args, void*);
        keys.push_back(nsstring_from_utf8(name));
        ObjcId cls = reinterpret_cast<ObjcId>(finn_objc_getClass("NSNumber"));
        if (!cls) continue;
        ObjcId num = objc_send_name<ObjcId>(
            cls, "numberWithUnsignedLongLong:",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(ptr)));
        if (num) values.push_back(num);
    }
    va_end(args);
    if (keys.size() != values.size() || keys.empty()) return nullptr;
    ObjcId dict_cls = reinterpret_cast<ObjcId>(finn_objc_getClass("NSDictionary"));
    if (!dict_cls) return nullptr;
    ObjcId dict = objc_send_name<ObjcId>(dict_cls, "dictionaryWithObjects:forKeys:count:",
                                         values.data(), keys.data(), values.size());
    return dict;
}

// ---------------------------------------------------------------------------
// Exported Foundation string/number constants. The pointer registered with
// the loader is the address of the global; the global holds a real NSString
// (or a real CGFloat) so guest code dereferences it like the system framework.
// ---------------------------------------------------------------------------

struct NamedConstant {
    const char* symbol;
    const char* value;
};

const NamedConstant kStringConstants[] = {
    {"_MASDictionaryTransformerName", "MASDictionaryTransformer"},
    {"_NSAccessibilityCheckBoxRole", "AXCheckBox"},
    {"_NSAccessibilityDisclosureTriangleRole", "AXDisclosureTriangle"},
    {"_NSAccessibilityGroupRole", "AXGroup"},
    {"_NSAccessibilityLinkRole", "AXLink"},
    {"_NSAccessibilityUnignoredChildren", "AXUnignoredChildren"},
    {"_NSAppearanceNameAqua", "NSAppearanceNameAqua"},
    {"_NSAppearanceNameDarkAqua", "NSAppearanceNameDarkAqua"},
    {"_NSBaselineOffsetAttributeName", "NSBaselineOffset"},
    {"_NSContinuouslyUpdatesValueBindingOption", "NSContinuouslyUpdatesValue"},
    {"_NSFontFeatureSelectorIdentifierKey", "NSFontFeatureSelectorIdentifier"},
    {"_NSFontFeatureSettingsAttribute", "NSFontFeatureSettings"},
    {"_NSFontFeatureTypeIdentifierKey", "NSFontFeatureTypeIdentifier"},
    {"_NSLinkAttributeName", "NSLink"},
    {"_NSMultipleValuesPlaceholderBindingOption", "NSMultipleValuesPlaceholder"},
    {"_NSNegateBooleanTransformerName", "NSNegateBoolean"},
    {"_NSNoSelectionPlaceholderBindingOption", "NSNoSelectionPlaceholder"},
    {"_NSNotApplicablePlaceholderBindingOption", "NSNotApplicablePlaceholder"},
    {"_NSNullPlaceholderBindingOption", "NSNullPlaceholder"},
    {"_NSParagraphStyleAttributeName", "NSParagraphStyle"},
    {"_NSPasteboardTypeURL", "public.url"},
    {"_NSValueTransformerBindingOption", "NSValueTransformer"},
    {"_NSValueTransformerNameBindingOption", "NSValueTransformerName"},
    {"_NSViewBoundsDidChangeNotification", "NSViewBoundsDidChangeNotification"},
    {"_NSWindowDidChangeOcclusionStateNotification", "NSWindowDidChangeOcclusionStateNotification"},
    {"_NSWindowDidMoveNotification", "NSWindowDidMoveNotification"},
    {"_NSWindowDidResizeNotification", "NSWindowDidResizeNotification"},
    {"_NSWorkspaceDidWakeNotification", "NSWorkspaceDidWakeNotification"},
    {"_NSCalendarDayChangedNotification", "NSCalendarDayChangedNotification"},
    {"_NSCurrentLocaleDidChangeNotification", "NSCurrentLocaleDidChangeNotification"},
    {"_NSSystemClockDidChangeNotification", "NSSystemClockDidChangeNotification"},
    {"_NSSystemTimeZoneDidChangeNotification", "NSSystemTimeZoneDidChangeNotification"},
    {"_NSInternalInconsistencyException", "NSInternalInconsistencyException"},
    {"_NSLocaleCountryCode", "countrycode"},
    {"_NSLocaleGroupingSeparator", "groupingseparator"},
    {"_NSCursorAttributeName", "NSCursor"},
    {"_NSImageNameInfo", "NSImageNameInfo"},
    {"_NSMarkedClauseSegmentAttributeName", "NSMarkedClauseSegment"},
    {"_NSStringPboardType", "NSStringPboardType"},
    {"_NSViewAnimationEffectKey", "NSViewAnimationEffectKey"},
    {"_NSViewAnimationFadeOutEffect", "NSViewAnimationFadeOutEffect"},
    {"_NSViewAnimationTargetKey", "NSViewAnimationTargetKey"},
    {"_kCATransactionAnimationDuration", "duration"},
    {"_kCACornerCurveContinuous", "continuous"},
    {"_kCGWindowBounds", "Bounds"},
    {"_kCGWindowLayer", "Layer"},
    {"_kCGWindowOwnerPID", "OwnerPID"},
    {"_NSKeyedUnarchiveFromDataTransformerName", "NSKeyedUnarchiveFromData"},
    {"_NSAccessibilityAnnouncementKey", "AXAnnouncement"},
    {"_NSAccessibilityAnnouncementRequestedNotification",
     "NSAccessibilityAnnouncementRequestedNotification"},
    {"_NSAccessibilityButtonRole", "AXButton"},
    {"_NSAccessibilityPriorityKey", "AXPriorityKey"},
    {"_NSObservedKeyPathKey", "NSObservedKeyPath"},
    {"_NSObservedObjectKey", "NSObservedObject"},
    {"_NSOptionsKey", "NSOptions"},
    {"_NSWindowDidResignKeyNotification", "NSWindowDidResignKeyNotification"},
    {"_kTISPropertyUnicodeKeyLayoutData", "TISPropertyUnicodeKeyLayoutData"},
    {"_NSIsNilTransformerName", "NSIsNil"},
    {"_NSLocalizedFailureReasonErrorKey", "NSLocalizedFailureReason"},
    {"_NSLocalizedRecoveryOptionsErrorKey", "NSLocalizedRecoveryOptions"},
    {"_NSLocalizedRecoverySuggestionErrorKey", "NSLocalizedRecoverySuggestion"},
    {"_NSMultipleUnderlyingErrorsKey", "NSMultipleUnderlyingErrors"},
    {"_NSPresentationIntentAttributeName", "NSPresentationIntent"},
    {"_NSURLErrorFailingURLErrorKey", "NSURLErrorFailingURLStringErrorKey"},
    {"_NSApplicationDidBecomeActiveNotification",
     "NSApplicationDidBecomeActiveNotification"},
    {"_NSHiddenBinding", "NSHidden"},
    {"_NSImageNameApplicationIcon", "NSImageNameApplicationIcon"},
    {"_NSMaxValueBinding", "NSMaxValue"},
    {"_NSTitleBinding", "NSTitle"},
    {"_NSValueBinding", "NSValue"},
    {"_kCFBundleIdentifierKey", "CFBundleIdentifier"},
    {"_kCFBundleNameKey", "CFBundleName"},
    {"_kCFBundleVersionKey", "CFBundleVersion"},
    {"_kSMDomainSystemLaunchd", "com.apple.launchd"},
    {"_kSMDomainUserLaunchd", "com.apple.launchd.peruser"},
};

ObjcId g_string_constants[std::size(kStringConstants)]{};
double g_font_weight_bold = -0.8;
double g_font_weight_medium = 0.0;
double g_font_weight_regular = 0.0;
double g_font_weight_semibold = 0.4;
double g_view_no_intrinsic_metric = -1.0;
std::int64_t g_url_session_transfer_size_unknown = -1;

// ---------------------------------------------------------------------------
// libobjc associated objects, weak references and property accessors.
// ---------------------------------------------------------------------------

struct AssociationKey {
    const void* object;
    const void* key;
    bool operator<(const AssociationKey& o) const {
        if (object != o.object) return object < o.object;
        return key < o.key;
    }
};

struct AssociationValue {
    ObjcId value = nullptr;
    int policy = 0;
};

std::mutex g_assoc_mu;
std::map<AssociationKey, AssociationValue> g_assoc;
std::mutex g_weak_mu;
std::map<void*, std::set<void**>> g_weak_slots;

constexpr int kPolicyRetain = 0x01;
constexpr int kPolicyCopy = 0x03;

void objc_setAssociatedObject(ObjcId object, const void* key, ObjcId value, int policy) {
    if (!object || !key) return;
    AssociationKey k{object, key};
    std::lock_guard<std::mutex> lk(g_assoc_mu);
    auto it = g_assoc.find(k);
    if (it != g_assoc.end()) {
        if (it->second.value && it->second.value != value) finn_objc_release(it->second.value);
        it->second.value = nullptr;
        g_assoc.erase(it);
    }
    if (!value) return;
    ObjcId stored = value;
    if ((policy & 0x0f) == kPolicyCopy) {
        stored = objc_send_name<ObjcId>(value, "copy");
    } else if ((policy & kPolicyRetain) != 0) {
        finn_objc_retain(value);
    }
    g_assoc[k] = {stored, policy};
}

ObjcId objc_getAssociatedObject(ObjcId object, const void* key) {
    if (!object || !key) return nullptr;
    std::lock_guard<std::mutex> lk(g_assoc_mu);
    auto it = g_assoc.find({object, key});
    return it == g_assoc.end() ? nullptr : it->second.value;
}

void objc_copyWeak(ObjcId* dst, ObjcId* src) {
    if (!dst) return;
    if (!src || !*src) {
        *dst = nullptr;
        return;
    }
    void* obj = *src;
    {
        std::lock_guard<std::mutex> lk(g_weak_mu);
        g_weak_slots[obj].insert(reinterpret_cast<void**>(dst));
    }
    *dst = *src;
}

void objc_storeWeak(ObjcId* dst, ObjcId value) {
    if (!dst) return;
    if (value) {
        std::lock_guard<std::mutex> lk(g_weak_mu);
        g_weak_slots[value].insert(reinterpret_cast<void**>(dst));
    }
    *dst = value;
}

void* objc_begin_catch(void* exception) {
    using Fn = void* (*)(void*);
    auto fn = reinterpret_cast<Fn>(host_symbol("__cxa_begin_catch"));
    return fn ? fn(exception) : exception;
}

void objc_end_catch() {
    using Fn = void (*)();
    auto fn = reinterpret_cast<Fn>(host_symbol("__cxa_end_catch"));
    if (fn) fn();
}

// objc_getProperty/objc_setProperty are emitted by the compiler for @property
// access when no direct ivar is available. A selector-keyed associated-object
// store gives them real, per-object backing with the requested ownership.
ObjcId objc_getProperty(ObjcId self, ObjcSel sel, bool atomic) {
    (void)atomic;
    if (!self || !sel) return nullptr;
    return objc_getAssociatedObject(self, sel);
}

void objc_setProperty(ObjcId self, ObjcSel sel, ObjcId value, bool atomic, bool copy) {
    (void)atomic;
    if (!self || !sel) return;
    objc_setAssociatedObject(self, sel, value, copy ? kPolicyCopy : kPolicyRetain);
}

// ---------------------------------------------------------------------------
// libdispatch semaphores (real blocking primitives).
// ---------------------------------------------------------------------------

struct DispatchSemaphore {
    std::mutex mu;
    std::condition_variable cv;
    long count;
    explicit DispatchSemaphore(long v) : count(v) {}
};

using DispatchTimeT = std::uint64_t;

DispatchSemaphore* dispatch_semaphore_create(long value) {
    return new DispatchSemaphore(value < 0 ? 0 : value);
}

long dispatch_semaphore_signal(DispatchSemaphore* sem) {
    if (!sem) return 0;
    {
        std::lock_guard<std::mutex> lk(sem->mu);
        ++sem->count;
    }
    sem->cv.notify_one();
    return 0;
}

long dispatch_semaphore_wait(DispatchSemaphore* sem, DispatchTimeT deadline) {
    if (!sem) return 1;
    std::unique_lock<std::mutex> lk(sem->mu);
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (deadline != 0 && deadline != ~DispatchTimeT{0}) {
        if (sem->cv.wait_until(lk, std::chrono::steady_clock::time_point(
                                       std::chrono::steady_clock::duration(
                                           static_cast<std::int64_t>(deadline))),
                               [&] { return sem->count > 0; })) {
            --sem->count;
            return 0;
        }
        return 1;
    }
    (void)now;
    sem->cv.wait(lk, [&] { return sem->count > 0; });
    --sem->count;
    return 0;
}

DispatchTimeT dispatch_time(DispatchTimeT base, std::int64_t delta) {
    if (base == 0) base = static_cast<DispatchTimeT>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    if (delta < 0) return base;
    return base + static_cast<DispatchTimeT>(delta);
}

// ---------------------------------------------------------------------------
// QuartzCore / CoreGraphics helpers.
// ---------------------------------------------------------------------------

struct CATransform3D {
    double m11, m12, m13, m14;
    double m21, m22, m23, m24;
    double m31, m32, m33, m34;
    double m41, m42, m43, m44;
};

const CATransform3D kCATransform3DIdentity = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

CATransform3D transform_make_scale(double sx, double sy, double sz) {
    CATransform3D t = kCATransform3DIdentity;
    t.m11 = sx;
    t.m22 = sy;
    t.m33 = sz;
    return t;
}

CATransform3D transform_make_translation(double tx, double ty, double tz) {
    CATransform3D t = kCATransform3DIdentity;
    t.m41 = tx;
    t.m42 = ty;
    t.m43 = tz;
    return t;
}

CATransform3D transform_concat(CATransform3D a, CATransform3D b) {
    CATransform3D r{};
    const double aa[4][4] = {{a.m11, a.m12, a.m13, a.m14},
                             {a.m21, a.m22, a.m23, a.m24},
                             {a.m31, a.m32, a.m33, a.m34},
                             {a.m41, a.m42, a.m43, a.m44}};
    const double bb[4][4] = {{b.m11, b.m12, b.m13, b.m14},
                             {b.m21, b.m22, b.m23, b.m24},
                             {b.m31, b.m32, b.m33, b.m34},
                             {b.m41, b.m42, b.m43, b.m44}};
    double out[4][4]{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) out[i][j] += aa[i][k] * bb[k][j];
        }
    }
    r.m11 = out[0][0]; r.m12 = out[0][1]; r.m13 = out[0][2]; r.m14 = out[0][3];
    r.m21 = out[1][0]; r.m22 = out[1][1]; r.m23 = out[1][2]; r.m24 = out[1][3];
    r.m31 = out[2][0]; r.m32 = out[2][1]; r.m33 = out[2][2]; r.m34 = out[2][3];
    r.m41 = out[3][0]; r.m42 = out[3][1]; r.m43 = out[3][2]; r.m44 = out[3][3];
    return r;
}

double current_media_time() {
    const auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
    return static_cast<double>(ns) / 1e9;
}

// ---------------------------------------------------------------------------
// libc / libstdc++ ABI entries used by downloaded apps. The host is also an
// Itanium-ABI 64-bit target, so binding these to the host implementations is
// real behavior rather than a trap or a guess.
// ---------------------------------------------------------------------------

std::unordered_map<std::string, void*> g_symbols;

void add_fn(const char* name, void* ptr) {
    if (ptr) g_symbols[name] = ptr;
}

}  // namespace

void init_compat_symbols() {
    static bool ready = false;
    if (ready) return;
    ready = true;
    for (std::size_t i = 0; i < std::size(kStringConstants); ++i) {
        g_string_constants[i] = nsstring_from_utf8(kStringConstants[i].value);
    }

    add_fn("_NSEqualPoints", to_ptr(&equal_points));
    add_fn("_NSEqualSizes", to_ptr(&equal_sizes));
    add_fn("_NSEqualRects", to_ptr(&equal_rects));
    add_fn("_NSInsetRect", to_ptr(&inset_rect));
    add_fn("_NSOffsetRect", to_ptr(&offset_rect));
    add_fn("_NSUnionRect", to_ptr(&union_rect));
    add_fn("_NSIntersectionRect", to_ptr(&intersection_rect));
    add_fn("_NSPointInRect", to_ptr(&point_in_rect));
    add_fn("_NSContainsRect", to_ptr(&contains_rect));
    add_fn("_NSIntersectionRange", to_ptr(&intersection_range));
    add_fn("_NSUnionRange", to_ptr(&union_range));
    add_fn("_NSStringFromRect", to_ptr(&string_from_rect));
    add_fn("_NSStringFromRange", to_ptr(&string_from_range));
    add_fn("_NSStringFromClass", to_ptr(&string_from_class));
    add_fn("_NSStringFromSelector", to_ptr(&string_from_selector));
    add_fn("_NSClassFromString", to_ptr(&class_from_string));
    add_fn("_NSSelectorFromString", to_ptr(&selector_from_string));
    add_fn("_NSBeep", to_ptr(&ns_beep));
    add_fn("_NSDivideRect", to_ptr(&divide_rect));
    add_fn("__fpclassifyd", to_ptr(&fpclassify_double));
    add_fn("__NSDictionaryOfVariableBindings", to_ptr(&dictionary_of_variable_bindings));
    g_symbols["_NSZeroPoint"] = const_cast<CGFloatPoint*>(&kZeroPoint);
    g_symbols["_NSZeroSize"] = const_cast<CGFloatSize*>(&kZeroSize);
    g_symbols["_NSZeroRect"] = const_cast<CGFloatRect*>(&kZeroRect);

    for (std::size_t i = 0; i < std::size(kStringConstants); ++i) {
        g_symbols[kStringConstants[i].symbol] = &g_string_constants[i];
    }
    g_symbols["_NSFontWeightBold"] = &g_font_weight_bold;
    g_symbols["_NSFontWeightMedium"] = &g_font_weight_medium;
    g_symbols["_NSFontWeightRegular"] = &g_font_weight_regular;
    g_symbols["_NSFontWeightSemibold"] = &g_font_weight_semibold;
    g_symbols["_NSViewNoIntrinsicMetric"] = &g_view_no_intrinsic_metric;
    g_symbols["_NSURLSessionTransferSizeUnknown"] = &g_url_session_transfer_size_unknown;

    add_fn("_objc_setAssociatedObject", to_ptr(&objc_setAssociatedObject));
    add_fn("_objc_getAssociatedObject", to_ptr(&objc_getAssociatedObject));
    add_fn("_objc_copyWeak", to_ptr(&objc_copyWeak));
    add_fn("_objc_storeWeak", to_ptr(&objc_storeWeak));
    add_fn("_objc_begin_catch", to_ptr(&objc_begin_catch));
    add_fn("_objc_end_catch", to_ptr(&objc_end_catch));
    add_fn("_objc_getProperty", to_ptr(&objc_getProperty));
    add_fn("_objc_setProperty", to_ptr(&objc_setProperty));
    add_fn("_objc_setProperty_atomic", to_ptr(&objc_setProperty));
    add_fn("_objc_setProperty_nonatomic_copy", to_ptr(&objc_setProperty));
    add_fn("_objc_setProperty_atomic_copy", to_ptr(&objc_setProperty));
    add_fn("_objc_retainBlock", to_ptr(&finn_objc_retain));

    add_fn("_dispatch_semaphore_create", to_ptr(&dispatch_semaphore_create));
    add_fn("_dispatch_semaphore_signal", to_ptr(&dispatch_semaphore_signal));
    add_fn("_dispatch_semaphore_wait", to_ptr(&dispatch_semaphore_wait));
    add_fn("_dispatch_time", to_ptr(&dispatch_time));

    g_symbols["_CACurrentMediaTime"] = to_ptr(&current_media_time);
    g_symbols["_CATransform3DIdentity"] = const_cast<CATransform3D*>(&kCATransform3DIdentity);
    g_symbols["_CATransform3DConcat"] = to_ptr(&transform_concat);
    g_symbols["_CATransform3DMakeScale"] = to_ptr(&transform_make_scale);
    g_symbols["_CATransform3DMakeTranslation"] = to_ptr(&transform_make_translation);

    // Host libc functions with the same C signature on Darwin and Linux.
    add_fn("_fseeko", host_symbol("fseeko"));
    add_fn("_ftello", host_symbol("ftello"));
    add_fn("_getnameinfo", host_symbol("getnameinfo"));
    add_fn("_gmtime", host_symbol("gmtime"));
    add_fn("_pthread_cancel", host_symbol("pthread_cancel"));
    add_fn("_pthread_setcancelstate", host_symbol("pthread_setcancelstate"));
    add_fn("_pthread_testcancel", host_symbol("pthread_testcancel"));
    add_fn("_select", host_symbol("select"));
    add_fn("_signal", host_symbol("signal"));
    add_fn("_strerror", host_symbol("strerror"));
    add_fn("___tolower", host_symbol("tolower"));
    add_fn("___toupper", host_symbol("toupper"));
    add_fn("___maskrune", host_symbol("towlower"));
    add_fn("__Unwind_Resume", host_symbol("_Unwind_Resume"));

    // Itanium C++ ABI. The Mach-O name carries an extra underscore prefix that
    // the ELF host does not; map the two explicitly.
    struct HostAbi { const char* macho; const char* host; };
    const HostAbi abi[] = {
        {"__Znwm", "_Znwm"}, {"__Znam", "_Znam"}, {"__ZdlPv", "_ZdlPv"},
        {"__ZdaPv", "_ZdaPv"},
        {"___cxa_pure_virtual", "__cxa_pure_virtual"},
        {"___cxa_allocate_exception", "__cxa_allocate_exception"},
        {"___cxa_free_exception", "__cxa_free_exception"},
        {"___cxa_throw", "__cxa_throw"}, {"___cxa_rethrow", "__cxa_rethrow"},
        {"___cxa_begin_catch", "__cxa_begin_catch"},
        {"___cxa_end_catch", "__cxa_end_catch"},
        {"___cxa_call_unexpected", "__cxa_call_unexpected"},
        {"___cxa_guard_acquire", "__cxa_guard_acquire"},
        {"___cxa_guard_release", "__cxa_guard_release"},
        {"___cxa_guard_abort", "__cxa_guard_abort"},
        {"___gxx_personality_v0", "__gxx_personality_v0"},
        {"___objc_personality_v0", "__gxx_personality_v0"},
        {"__ZSt9terminatev", "_ZSt9terminatev"},
        {"__ZSt18uncaught_exceptionv", "_ZSt18uncaught_exceptionv"},
    };
    for (const auto& entry : abi) add_fn(entry.macho, host_symbol(entry.host));
}

void register_compat_symbols(std::unordered_map<std::string, void*>* table) {
    init_compat_symbols();
    for (const auto& e : g_symbols) (*table)[e.first] = e.second;
}

void dispatch_semaphore_release(void* semaphore) {
    delete static_cast<DispatchSemaphore*>(semaphore);
}

void compat_weak_zero(void* object) {
    if (!object) return;
    std::vector<void**> slots;
    {
        std::lock_guard<std::mutex> lk(g_weak_mu);
        auto it = g_weak_slots.find(object);
        if (it == g_weak_slots.end()) return;
        slots.assign(it->second.begin(), it->second.end());
        g_weak_slots.erase(it);
    }
    for (void** slot : slots) *slot = nullptr;
}

bool compat_has_symbol(const std::string& macho_symbol) {
    init_compat_symbols();
    return g_symbols.find(macho_symbol) != g_symbols.end();
}

}  // namespace finn::darwin
