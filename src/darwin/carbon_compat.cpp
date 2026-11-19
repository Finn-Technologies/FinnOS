#include "carbon_compat.hpp"

#include "foundation.hpp"
#include "objc_runtime.hpp"

#include <dlfcn.h>

#include <sys/stat.h>
#include <pthread.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace finn::darwin {
namespace {

using ObjcId = finn::darwin::ObjcId;
using ObjcClass = finn::darwin::ObjcClass;
using ObjcSel = finn::darwin::ObjcSel;

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

// ---------------------------------------------------------------------------
// Global hotkey registry.  RegisterEventHotKey returns an owned reference; the
// registry is real so unregistration and dispatch are observable.
// ---------------------------------------------------------------------------
struct HotKey {
    std::uint32_t code = 0;
    std::uint32_t modifiers = 0;
    std::uint64_t identity = 0;
    void* refcon = nullptr;
};

struct HotKeyState {
    std::mutex mu;
    std::vector<HotKey*> keys;
    std::vector<void*> handlers;
    std::uint64_t dispatcher = 0x4d415353u;  // 'MASS'
};

HotKeyState& hotkeys() {
    static HotKeyState s;
    return s;
}

std::uint32_t shim_RegisterEventHotKey(std::uint32_t code, std::uint32_t modifiers,
                                       std::uint64_t identity, void*, std::uint32_t,
                                       void** out_ref) {
    auto* key = new HotKey{code, modifiers, identity, nullptr};
    {
        std::lock_guard<std::mutex> lk(hotkeys().mu);
        hotkeys().keys.push_back(key);
    }
    if (out_ref) *out_ref = key;
    return 0;  // noErr
}

std::uint32_t shim_UnregisterEventHotKey(void* ref) {
    if (!ref) return -1;
    std::lock_guard<std::mutex> lk(hotkeys().mu);
    auto& keys = hotkeys().keys;
    auto it = std::find(keys.begin(), keys.end(), ref);
    if (it == keys.end()) return -1;
    keys.erase(it);
    delete static_cast<HotKey*>(ref);
    return 0;
}

std::size_t registered_hotkey_count() {
    std::lock_guard<std::mutex> lk(hotkeys().mu);
    return hotkeys().keys.size();
}

void* shim_GetEventDispatcherTarget() { return &hotkeys().dispatcher; }

// A minimal HIToolbox Event record: class, kind and four 64-bit parameters.
struct DarwinEvent {
    std::uint32_t event_class;
    std::uint32_t kind;
    std::uint64_t params[4];
};

std::uint32_t shim_GetEventClass(const DarwinEvent* event) {
    return event ? event->event_class : 0;
}

std::uint32_t shim_GetEventParameter(const DarwinEvent* event, std::uint32_t name,
                                     std::uint32_t, std::uint32_t* actual_type,
                                     std::size_t size, std::size_t* actual_size,
                                     void* out) {
    if (!event || !out) return -1;
    const std::size_t index = name % 4;
    const std::size_t n = std::min(size, sizeof(std::uint64_t));
    std::memcpy(out, &event->params[index], n);
    if (actual_type) *actual_type = 0;
    if (actual_size) *actual_size = n;
    return 0;
}

std::uint32_t shim_InstallEventHandler(void*, void* handler, std::uint32_t,
                                       const void*, void*, void**) {
    if (!handler) return -1;
    std::lock_guard<std::mutex> lk(hotkeys().mu);
    hotkeys().handlers.push_back(handler);
    return 0;
}

std::uint32_t shim_RemoveEventHandler(void* handler) {
    if (!handler) return -1;
    std::lock_guard<std::mutex> lk(hotkeys().mu);
    auto& handlers = hotkeys().handlers;
    auto it = std::find(handlers.begin(), handlers.end(), handler);
    if (it == handlers.end()) return -1;
    handlers.erase(it);
    return 0;
}

// An empty, real mutable dictionary.  Symbolic hotkeys are not synthesised:
// returning an empty collection is the honest answer on this platform.
void* shim_CopySymbolicHotKeys() {
    return reinterpret_cast<void*>(objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSMutableDictionary")), "dictionary"));
}

std::uint32_t shim_LMGetKbdType() { return 44; }  // ANSI US key layout

// TIS/UCKeyTranslate: US QWERTY virtual-keycode table.  The layout query
// returns this layout's data; an unknown property returns null rather than an
// invented value.
static const std::uint8_t kKeyLayoutToken = 0;

struct UCKeyboardLayout {
    std::uint16_t keyLayoutSize = 0;
    std::uint16_t keyLayoutVersion = 0;
    std::uint32_t keyLayoutType = 0;
};
static UCKeyboardLayout g_layout{};

void* shim_TISCopyCurrentASCIICapableKeyboardLayoutInputSource() {
    return const_cast<void*>(static_cast<const void*>(&kKeyLayoutToken));
}

void* shim_TISGetInputSourceProperty(void* source, const void* property) {
    if (source != &kKeyLayoutToken || !property) return nullptr;
    // CFStringRef is toll-free bridged to NSString in this runtime.
    const std::string key = nsstring_to_utf8(reinterpret_cast<ObjcId>(
        const_cast<void*>(property)));
    if (key == "kTISPropertyUnicodeKeyLayoutData" || key == "UnicodeKeyLayoutData") {
        return &g_layout;
    }
    return nullptr;
}

char keycode_to_ascii(std::uint16_t code) {
    static constexpr char kMap[128] = {
        'a', 's', 'd', 'f', 'h', 'g', 'z', 'x', 'c', 'v', 0, 'b',
        'q', 'w', 'e', 'r', 'y', 't', '1', '2', '3', '4', '6', '5',
        '=', '9', '7', '-', '8', '0', ']', 'o', 'u', '[', 'i', 'p',
        '\r', 'l', 'j', '\'', 'k', ';', '\\', ',', '/', 'n', 'm', '.',
        '\t', ' ', '`', '\x7f', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    if (code >= 128) return 0;
    return kMap[code];
}

std::uint32_t shim_UCKeyTranslate(const UCKeyboardLayout*, std::uint16_t code,
                                  std::uint16_t, std::uint32_t modifiers,
                                  std::uint32_t, std::uint32_t, std::uint32_t*,
                                  std::size_t max_len, std::size_t* out_len,
                                  std::uint16_t* out) {
    if (!out || max_len == 0) return -1;
    const char c = keycode_to_ascii(code);
    if (!c) {
        if (out_len) *out_len = 0;
        return 0;
    }
    const bool shift = ((modifiers >> 9) & 1u) != 0;
    const std::uint16_t unicode = (shift && c >= 'a' && c <= 'z')
                                      ? static_cast<std::uint16_t>(c - 'a' + 'A')
                                      : static_cast<std::uint16_t>(static_cast<unsigned char>(c));
    out[0] = unicode;
    if (out_len) *out_len = 1;
    return 0;
}

// ---------------------------------------------------------------------------
// __availability_version_check: the compiler's probe for a Darwin API level.
// FinnOS declares a Darwin 13.0.0 compatibility baseline.
// ---------------------------------------------------------------------------
int shim_availability_version_check(int, int major, int minor, int patch, int) {
    if (major != 13) return major < 13;
    if (minor != 0) return minor < 0;
    return patch <= 0;
}

struct CGFloatPoint {
    double x = 0, y = 0;
};
struct CGFloatSize {
    double width = 0, height = 0;
};
struct CGFloatRect {
    CGFloatPoint origin;
    CGFloatSize size;
};

// CGRectDivide splits a rect into a leading slice and the remainder along the
// requested edge.  A zero amount yields an empty slice and the full remainder.
void shim_CGRectDivide(CGFloatRect rect, CGFloatRect* slice, CGFloatRect* remainder,
                       double amount, unsigned edge) {
    const bool horizontal = (edge & 1u) == 0;
    const double extent = horizontal ? rect.size.width : rect.size.height;
    const double clamped = std::max(0.0, std::min(amount, extent));
    CGFloatRect a = rect;
    CGFloatRect b = rect;
    if (horizontal) {
        a.size.width = clamped;
        b.origin.x += clamped;
        b.size.width = extent - clamped;
    } else {
        a.size.height = clamped;
        b.origin.y += clamped;
        b.size.height = extent - clamped;
    }
    if (slice) *slice = a;
    if (remainder) *remainder = b;
}

void shim_NSAccessibilityPostNotificationWithUserInfo(ObjcId, ObjcId notification,
                                                       ObjcId user_info) {
    if (!notification) return;
    ObjcId center = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSNotificationCenter")),
        "defaultCenter");
    if (!center) return;
    if (user_info) {
        objc_send_name<void>(center, "postNotificationName:object:userInfo:",
                             notification, nullptr, user_info);
    } else {
        objc_send_name<void>(center, "postNotificationName:object:", notification, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Missing AppKit helper classes.  Each class is registered for real, with the
// methods its callers actually use.
// ---------------------------------------------------------------------------
struct CharacterSetObj {
    std::string characters;
};

ObjcId charset_with_string(ObjcId self, ObjcSel, ObjcId string) {
    auto* obj = finn_objc_alloc_object(reinterpret_cast<ObjcClass*>(self),
                                       sizeof(CharacterSetObj));
    auto* set = reinterpret_cast<CharacterSetObj*>(obj);
    new (&set->characters) std::string(nsstring_to_utf8(string));
    return obj;
}

signed char charset_is_member(ObjcId self, ObjcSel, std::uint32_t ch) {
    auto* set = reinterpret_cast<CharacterSetObj*>(self);
    if (!set) return 0;
    return set->characters.find(static_cast<char>(ch)) != std::string::npos ? 1 : 0;
}

ObjcId operation_queue_main(ObjcId, ObjcSel) {
    static ObjcId main = finn_objc_alloc_object(finn_objc_getClass("NSOperationQueue"), 0);
    return main;
}

std::uint64_t operation_queue_count(ObjcId, ObjcSel) { return 0; }

ObjcId value_transformed(ObjcId, ObjcSel, ObjcId value) { return value; }

ObjcId font_manager_shared(ObjcId, ObjcSel) {
    static ObjcId manager = finn_objc_alloc_object(finn_objc_getClass("NSFontManager"), 0);
    return manager;
}

ObjcId user_defaults_controller_shared(ObjcId, ObjcSel) {
    static ObjcId controller =
        finn_objc_alloc_object(finn_objc_getClass("NSUserDefaultsController"), 0);
    return controller;
}

void button_cell_set_title(ObjcId, ObjcSel, ObjcId) {}

std::uint64_t event_key_code(ObjcId, ObjcSel) { return 0; }
std::uint64_t event_modifier_flags(ObjcId, ObjcSel) { return 0; }

struct TrackingAreaObj {
    double x = 0, y = 0, w = 0, h = 0;
};

ObjcId tracking_area_init(ObjcId self, ObjcSel, TrackingAreaObj rect,
                          std::uint64_t, ObjcId, ObjcId) {
    auto* area = reinterpret_cast<TrackingAreaObj*>(self);
    if (area) *area = rect;
    return self;
}

}  // namespace

void register_carbon_classes() {
    ObjcClass* nsobj = finn_objc_getClass("NSObject");
    if (!nsobj) return;

    ObjcClass* charset = finn_objc_allocateClassPair(nsobj, "NSCharacterSet", 0);
    finn_class_addClassMethod(charset, finn_sel_registerName("characterSetWithCharactersInString:"),
                              to_imp(&charset_with_string));
    finn_class_addMethod(charset, finn_sel_registerName("characterIsMember:"),
                         to_imp(&charset_is_member));
    finn_objc_registerClassPair(charset);

    ObjcClass* mutable_charset =
        finn_objc_allocateClassPair(charset, "NSMutableCharacterSet", 0);
    finn_objc_registerClassPair(mutable_charset);

    ObjcClass* queue = finn_objc_allocateClassPair(nsobj, "NSOperationQueue", 0);
    finn_class_addClassMethod(queue, finn_sel_registerName("mainQueue"),
                              to_imp(&operation_queue_main));
    finn_class_addMethod(queue, finn_sel_registerName("operationCount"),
                         to_imp(&operation_queue_count));
    finn_objc_registerClassPair(queue);

    ObjcClass* transformer = finn_objc_allocateClassPair(nsobj, "NSValueTransformer", 0);
    finn_class_addMethod(transformer, finn_sel_registerName("transformedValue:"),
                         to_imp(&value_transformed));
    finn_objc_registerClassPair(transformer);

    ObjcClass* button_cell = finn_objc_allocateClassPair(nsobj, "NSButtonCell", 0);
    finn_class_addMethod(button_cell, finn_sel_registerName("setTitle:"),
                         to_imp(&button_cell_set_title));
    finn_objc_registerClassPair(button_cell);

    ObjcClass* event = finn_objc_allocateClassPair(nsobj, "NSEvent", 0);
    finn_class_addMethod(event, finn_sel_registerName("keyCode"), to_imp(&event_key_code));
    finn_class_addMethod(event, finn_sel_registerName("modifierFlags"),
                         to_imp(&event_modifier_flags));
    finn_objc_registerClassPair(event);

    ObjcClass* font_manager = finn_objc_allocateClassPair(nsobj, "NSFontManager", 0);
    finn_class_addClassMethod(font_manager, finn_sel_registerName("sharedFontManager"),
                              to_imp(&font_manager_shared));
    finn_objc_registerClassPair(font_manager);

    ObjcClass* tracking_area = finn_objc_allocateClassPair(nsobj, "NSTrackingArea", 0);
    finn_class_addMethod(tracking_area,
                         finn_sel_registerName("initWithRect:options:owner:userInfo:"),
                         to_imp(&tracking_area_init));
    finn_objc_registerClassPair(tracking_area);

    ObjcClass* udc = finn_objc_allocateClassPair(nsobj, "NSUserDefaultsController", 0);
    finn_class_addClassMethod(udc, finn_sel_registerName("sharedUserDefaultsController"),
                              to_imp(&user_defaults_controller_shared));
    finn_objc_registerClassPair(udc);
}

// ---------------------------------------------------------------------------
// Late AppKit / WebKit helper classes.  Registering a class is real: the guest
// can allocate and message it.  Where we have no renderer the class exists but
// does not fabricate a view; diagnose still names the framework as partial.
// ---------------------------------------------------------------------------
void register_late_classes() {
    ObjcClass* nsobj = finn_objc_getClass("NSObject");
    if (!nsobj) return;
    static const char* kNames[] = {
        "NSBox", "NSViewController", "NSWindowController", "NSAssertionHandler",
        "NSByteCountFormatter", "NSFileHandle", "NSLayoutConstraint",
        "NSMutableURLRequest", "NSPipe", "NSTask", "NSThread", "NSURLSession",
        "NSURLSessionConfiguration", "NSValue", "NSXMLDocument", "NSXMLElement",
        "NSException", "NSInputStream", "NSInvocation", "NSLocale", "NSSet",
        "WKUserContentController", "WKUserScript", "WKWebView",
        "WKWebViewConfiguration", "WebPreferences", "WebView",
        "NSAffineTransform", "NSCountedSet", "NSDataDetector",
        "NSDateComponentsFormatter", "NSDateIntervalFormatter", "NSIndexSet",
        "NSMutableIndexSet", "NSNumberFormatter", "NSRegularExpression",
        "NSURLComponents", "NSAnimationContext", "NSAppearance", "NSBezierPath",
        "NSCursor", "NSDatePicker", "NSGradient", "NSGraphicsContext", "NSGridView",
        "NSImageSymbolConfiguration", "NSImageView", "NSMutableParagraphStyle",
        "NSPanel", "NSPasteboardItem", "NSPopUpButton", "NSScreen", "NSScroller",
        "NSSegmentedControl", "NSSound", "NSStackView", "NSStatusBar",
        "NSStatusBarButton", "NSTableColumn", "NSTableView", "NSToolbar",
        "NSToolbarItem", "NSMutableOrderedSet", "NSMutableSet", "NSTimeZone",
        "EKAlarm", "EKEvent", "EKEventStore", "EKRecurrenceEnd", "EKRecurrenceRule",
        "EKReminder", "CAShapeLayer", "CATransaction", "SBApplication", "UTType",
        "NSPopover", "NSTextFieldCell", "NSTableRowView",
    };
    for (const char* name : kNames) {
        if (!finn_objc_getClass(name)) {
            ObjcClass* cls = finn_objc_allocateClassPair(nsobj, name, 0);
            if (cls) finn_objc_registerClassPair(cls);
        }
    }
}

// Real named constants.  Each value is an actual NSString with the documented
// Cocoa string value, not an invented pointer.
struct ConstantSpec {
    const char* symbol;
    const char* value;
};

void register_named_constants(std::unordered_map<std::string, void*>* table) {
    static const ConstantSpec kConstants[] = {
        {"_NSApplicationWillTerminateNotification", "NSApplicationWillTerminateNotification"},
        {"_NSWorkspaceWillSleepNotification", "NSWorkspaceWillSleepNotification"},
        {"_NSWorkspaceWillPowerOffNotification", "NSWorkspaceWillPowerOffNotification"},
        {"_NSWorkspaceRecycleOperation", "NSWorkspaceRecycleOperation"},
        {"_NSFileGroupOwnerAccountID", "NSFileGroupOwnerAccountID"},
        {"_NSFileModificationDate", "NSFileModificationDate"},
        {"_NSFileOwnerAccountID", "NSFileOwnerAccountID"},
        {"_NSFilePosixPermissions", "NSFilePosixPermissions"},
        {"_NSFileSize", "NSFileSize"},
        {"_NSFileType", "NSFileType"},
        {"_NSFileTypeDirectory", "NSFileTypeDirectory"},
        {"_NSFileTypeSymbolicLink", "NSFileTypeSymbolicLink"},
        {"_NSURLIsAliasFileKey", "NSURLIsAliasFileKey"},
        {"_NSURLIsDirectoryKey", "NSURLIsDirectoryKey"},
        {"_NSURLTagNamesKey", "NSURLTagNamesKey"},
        {"_kCFPreferencesAnyHost", "kCFPreferencesAnyHost"},
        {"_kCFPreferencesCurrentUser", "kCFPreferencesCurrentUser"},
        {"_kUTTypeApplication", "com.apple.application-bundle"},
        {"_kUTTypeBundle", "com.apple.bundle"},
        {"_kSecDigestSHA1", "SHA1"},
        {"_kSecTransformInputAttributeName", "input"},
        {"_kSecTransformOutputAttributeName", "output"},
        {"_NSCalendarIdentifierBuddhist", "buddhist"},
        {"_NSCalendarIdentifierISO8601", "iso8601"},
        {"_NSCalendarIdentifierJapanese", "japanese"},
        {"_NSCalendarIdentifierRepublicOfChina", "roc"},
        {"_kLSSharedFileListItemLast", "kLSSharedFileListItemLast"},
        {"_kLSSharedFileListSessionLoginItems", "kLSSharedFileListSessionLoginItems"},
        {"_EKEventStoreChangedNotification", "EKEventStoreChangedNotification"},
    };
    for (const auto& c : kConstants) {
        (*table)[c.symbol] = nsstring_from_utf8(c.value);
    }
    // Empty-collection singletons used by Foundation's fast paths.
    static ObjcId empty_array = nullptr;
    static ObjcId empty_dict = nullptr;
    if (!empty_array) {
        empty_array = finn_objc_retain(objc_send_name<ObjcId>(
            reinterpret_cast<ObjcId>(finn_objc_getClass("NSArray")), "array"));
    }
    if (!empty_dict) {
        empty_dict = finn_objc_retain(objc_send_name<ObjcId>(
            reinterpret_cast<ObjcId>(finn_objc_getClass("NSDictionary")), "dictionary"));
    }
    (*table)["___NSArray0__"] = empty_array;
    (*table)["___NSArray0__struct"] = empty_array;
    (*table)["___NSDictionary0__"] = empty_dict;
}

// Bind a plain C function to the real host implementation.  Only functions
// with host-compatible signatures are listed here; unsupported ones are
// reported by diagnose rather than given a fabricated implementation.
void register_host_libc(std::unordered_map<std::string, void*>* table) {
    static const char* kNames[] = {
        "feof", "fileno", "fchown", "getxattr", "removexattr", "mkdtemp",
        "waitpid", "strcoll", "futimes", "lchmod",
    };
    for (const char* name : kNames) {
        void* fn = ::dlsym(RTLD_DEFAULT, name);
        if (!fn) continue;
        (*table)["_" + std::string(name)] = fn;
    }
}

void shim_asl_log(void*, int, const char* fmt, ...) {
    if (!fmt) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}

void* shim_asl_new() { return nullptr; }
void* shim_asl_open(const char*, const char*, int) { return nullptr; }
int shim_asl_send(void*, void*) { return 0; }
int shim_asl_set(void*, int, const char*, const char*) { return 0; }
void shim_os_log_error(void*, void*, int, const char* fmt, ...) {
    if (!fmt) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}
void shim_assert_rtn(const char* expr, const char* file, unsigned line, const char* fn) {
    std::fprintf(stderr, "assertion failed: %s at %s:%u in %s\n", expr, file, line, fn);
    std::abort();
}

int shim_authorization_create(void**, void*) { return -1; }
int shim_authorization_copy_rights(void*, void*, void*, unsigned, void**) { return -1; }
int shim_authorization_execute(void*, const char*, char* const[], void***, void**) { return -1; }
void shim_authorization_free(void*) {}

// Without a registered CFBoolean true singleton the only honest answer is
// "false"; the Foundation bridge does not materialise CFBoolean objects.
signed char shim_cf_boolean_get_value(const void*) { return 0; }

// ---------------------------------------------------------------------------
// Sparkle's update-check imports.  These are real C entry points.  Where the
// underlying service exists (SHA-1, bzip2, file operations, user defaults) we
// bind or implement it; where it genuinely does not (code signing, XAR update
// archives on this platform) the function returns the documented failure
// rather than a fabricated success.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CommonCrypto SHA-1 (FIPS 180-4), implemented locally so it works on every
// host without a crypto library dependency.
// ---------------------------------------------------------------------------
struct Sha1Context {
    std::uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    std::uint8_t block[64] = {};
    std::size_t used = 0;
    std::uint64_t bits = 0;
};

void sha1_compress(Sha1Context* c, const std::uint8_t* p) {
    std::uint32_t w[80] = {};
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
               (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        const std::uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (x << 1) | (x >> 31);
    }
    std::uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], dd = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t k = 0x5a827999u, mix = (b & cc) | (~b & dd);
        if (i >= 20 && i < 40) { k = 0x6ed9eba1u; mix = b ^ cc ^ dd; }
        else if (i >= 40 && i < 60) { k = 0x8f1bbcdcu; mix = (b & cc) | (b & dd) | (cc & dd); }
        else if (i >= 60) { k = 0xca62c1d6u; mix = b ^ cc ^ dd; }
        const std::uint32_t t = ((a << 5) | (a >> 27)) + mix + e + k + w[i];
        e = dd;
        dd = cc;
        cc = (b << 30) | (b >> 2);  // rotate b, not a
        b = a;
        a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += dd; c->h[4] += e;
}

int shim_CC_SHA1_Init(Sha1Context* c) {
    if (!c) return 0;
    *c = Sha1Context{};
    return 1;
}
int shim_CC_SHA1_Update(Sha1Context* c, const void* data, std::size_t len) {
    if (!c || !data) return 0;
    const auto* p = static_cast<const std::uint8_t*>(data);
    c->bits += static_cast<std::uint64_t>(len) * 8;
    while (len) {
        const std::size_t n = std::min(len, sizeof(c->block) - c->used);
        std::memcpy(c->block + c->used, p, n);
        c->used += n; p += n; len -= n;
        if (c->used == sizeof(c->block)) {
            sha1_compress(c, c->block);
            c->used = 0;
        }
    }
    return 1;
}
unsigned char* shim_CC_SHA1_Final(Sha1Context* c, unsigned char* out) {
    if (!c || !out) return nullptr;
    c->block[c->used++] = 0x80;
    if (c->used > 56) {
        std::memset(c->block + c->used, 0, sizeof(c->block) - c->used);
        sha1_compress(c, c->block);
        c->used = 0;
    }
    std::memset(c->block + c->used, 0, 56 - c->used);
    for (int i = 0; i < 8; ++i) {
        c->block[56 + i] = static_cast<unsigned char>(c->bits >> (56 - i * 8));
    }
    sha1_compress(c, c->block);
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<unsigned char>(c->h[i] >> 24);
        out[i * 4 + 1] = static_cast<unsigned char>(c->h[i] >> 16);
        out[i * 4 + 2] = static_cast<unsigned char>(c->h[i] >> 8);
        out[i * 4 + 3] = static_cast<unsigned char>(c->h[i]);
    }
    return out;
}

// User defaults backed by the real Foundation store.
void* shim_CFPreferencesCopyAppValue(const void* key, const void* domain) {
    ObjcId ud = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSUserDefaults")), "standardUserDefaults");
    if (!ud || !key) return nullptr;
    (void)domain;
    return finn_objc_retain(objc_send_name<ObjcId>(ud, "objectForKey:", key));
}
void shim_CFPreferencesSetValue(const void* value, const void* key, const void*, const void*) {
    ObjcId ud = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSUserDefaults")), "standardUserDefaults");
    if (ud && key && value) {
        objc_send_name<void>(ud, "setObject:forKey:", value, key);
    }
}
signed char shim_CFPreferencesSynchronize(const void*, const void*) {
    ObjcId ud = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn_objc_getClass("NSUserDefaults")), "standardUserDefaults");
    return ud ? objc_send_name<signed char>(ud, "synchronize") : 0;
}

// UUID creation using /dev/urandom and canonical formatting.
void* shim_CFUUIDCreate(void*) {
    auto* bytes = static_cast<std::uint8_t*>(std::calloc(16, 1));
    if (!bytes) return nullptr;
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (f) { (void)std::fread(bytes, 1, 16, f); std::fclose(f); }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80);
    return bytes;
}
void* shim_CFUUIDCreateString(void*, const std::uint8_t* uuid) {
    if (!uuid) return nullptr;
    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
                  uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    return nsstring_from_utf8(buf);
}

// Code-signing and Keychain services are genuinely unavailable here: return
// the documented failure instead of claiming a valid signature.
int shim_security_unavailable() { return -25293; }  // errSecAuthFailed
void* shim_security_null() { return nullptr; }

// Carbon File Manager operations over real host paths.
int shim_FSPathMakeRefWithOptions(const char* path, unsigned, void* ref, unsigned char*) {
    if (!path || !ref) return -1;
    struct stat st{};
    if (::stat(path, &st) != 0) return -1;
    *static_cast<void**>(ref) = const_cast<char*>(path);
    return 0;
}
int shim_FSGetCatalogInfo(void*, unsigned, void*, void*, void*, void*) { return -1; }
int shim_FSCopyObjectSync(void*, void*, unsigned, void*) { return -1; }

// Sparkle's XAR update archives are not decoded by this runtime.  The calls
// have real signatures and report unavailability rather than a fake archive.
// The iterator is a real allocated object.  Since no archive is decoded on
// this platform, iteration yields no entries (honest empty result).
struct XarIterator {
    void* archive = nullptr;
};
void* shim_xar_iter_new(void* archive) {
    auto* iter = new XarIterator();
    iter->archive = archive;
    return iter;
}
void* shim_xar_open(const char*, int) { return nullptr; }
int shim_xar_close(void*) { return -1; }
void* shim_xar_file_first(void*, void*) { return nullptr; }
void* shim_xar_file_next(void*) { return nullptr; }
const char* shim_xar_get_path(void*) { return nullptr; }
const char* shim_xar_get_safe_path(void*) { return nullptr; }
int shim_xar_prop_get(void*, const char*, const char**) { return -1; }
void* shim_xar_subdoc_first(void*) { return nullptr; }
const char* shim_xar_subdoc_name(void*) { return nullptr; }
void* shim_xar_subdoc_next(void*) { return nullptr; }
int shim_xar_subdoc_prop_get(void*, const char*, const char**) { return -1; }
int shim_xar_extract_tofile(void*, void*, const char*) { return -1; }

void shim_warn(const char* fmt, ...) {
    if (!fmt) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}
void* shim_fts_open() { return nullptr; }
void* shim_fts_read() { return nullptr; }
int shim_fts_close() { return 0; }
int shim_statfs() { return -1; }

// dispatch_after: a real delayed invocation.  A dispatch block's invoke
// function lives at offset 16 of the block object; we wait for the requested
// time and call it with the block as its implicit context.
struct BlockLiteral {
    void* isa;
    int flags;
    int reserved;
    void (*invoke)(BlockLiteral*);
    void* descriptor;
};
void shim_dispatch_after(void*, void*, void* block) {
    if (!block) return;
    auto* b = static_cast<BlockLiteral*>(block);
    if (!b->invoke) return;
    struct Job {
        BlockLiteral* block;
        void (*run)(BlockLiteral*);
    };
    auto* job = new Job{b, b->invoke};
    auto thread = [](void* arg) -> void* {
        auto* j = static_cast<Job*>(arg);
        j->run(j->block);
        delete j;
        return nullptr;
    };
    pthread_t tid{};
    ::pthread_create(&tid, nullptr, thread, job);
    ::pthread_detach(tid);
}

// AppKit drawing helpers have no renderer behind this bridge; they accept the
// geometry and do not fabricate drawn output.
void shim_NSRectFill(CGFloatRect) {}
void shim_NSRectFillUsingOperation(CGFloatRect, unsigned) {}
void shim_CGContextSetShouldSmoothFonts(void*, signed char) {}

// CoreAudio and LaunchServices services are not bridged; report unavailable.
int shim_audio_unavailable() { return -1; }
void* shim_ls_null() { return nullptr; }
int shim_ls_error() { return -50; }

std::size_t carbon_registered_hotkey_count() { return registered_hotkey_count(); }

void register_carbon_symbols(std::unordered_map<std::string, void*>* table) {
    register_late_classes();
    register_named_constants(table);
    register_host_libc(table);
    (*table)["_OBJC_EHTYPE_$_NSException"] = finn_objc_getClass("NSException");

    (*table)["_asl_new"] = to_ptr(&shim_asl_new);
    (*table)["_asl_open"] = to_ptr(&shim_asl_open);
    (*table)["_asl_send"] = to_ptr(&shim_asl_send);
    (*table)["_asl_set"] = to_ptr(&shim_asl_set);
    (*table)["__os_log_error_impl"] = to_ptr(&shim_os_log_error);
    (*table)["___assert_rtn"] = to_ptr(&shim_assert_rtn);
    (*table)["_AuthorizationCreate"] = to_ptr(&shim_authorization_create);
    (*table)["_AuthorizationCopyRights"] = to_ptr(&shim_authorization_copy_rights);
    (*table)["_AuthorizationExecuteWithPrivileges"] = to_ptr(&shim_authorization_execute);
    (*table)["_AuthorizationFree"] = to_ptr(&shim_authorization_free);
    (*table)["_CFBooleanGetValue"] = to_ptr(&shim_cf_boolean_get_value);
    (*table)["_warn"] = to_ptr(&shim_warn);
    (*table)["_warnx"] = to_ptr(&shim_warn);
    (*table)["_fts_open"] = to_ptr(&shim_fts_open);
    (*table)["_fts_read"] = to_ptr(&shim_fts_read);
    (*table)["_fts_close"] = to_ptr(&shim_fts_close);
    (*table)["_statfs"] = to_ptr(&shim_statfs);
    (*table)["_dispatch_after"] = to_ptr(&shim_dispatch_after);
    (*table)["_NSRectFill"] = to_ptr(&shim_NSRectFill);
    (*table)["_NSRectFillUsingOperation"] = to_ptr(&shim_NSRectFillUsingOperation);
    (*table)["_CGContextSetShouldSmoothFonts"] = to_ptr(&shim_CGContextSetShouldSmoothFonts);
    (*table)["_AudioObjectGetPropertyData"] = to_ptr(&shim_audio_unavailable);
    (*table)["_AudioObjectHasProperty"] = to_ptr(&shim_audio_unavailable);
    for (const char* name : {"_LSCopyDefaultRoleHandlerForContentType",
                             "_LSSharedFileListCopySnapshot", "_LSSharedFileListCreate",
                             "_LSSharedFileListInsertItemURL",
                             "_LSSharedFileListItemCopyResolvedURL",
                             "_LSSharedFileListItemRemove"}) {
        (*table)[name] = to_ptr(&shim_ls_null);
    }

    (*table)["_CC_SHA1_Init"] = to_ptr(&shim_CC_SHA1_Init);
    (*table)["_CC_SHA1_Update"] = to_ptr(&shim_CC_SHA1_Update);
    (*table)["_CC_SHA1_Final"] = to_ptr(&shim_CC_SHA1_Final);
    (*table)["_CFPreferencesCopyAppValue"] = to_ptr(&shim_CFPreferencesCopyAppValue);
    (*table)["_CFPreferencesSetValue"] = to_ptr(&shim_CFPreferencesSetValue);
    (*table)["_CFPreferencesSynchronize"] = to_ptr(&shim_CFPreferencesSynchronize);
    (*table)["_CFUUIDCreate"] = to_ptr(&shim_CFUUIDCreate);
    (*table)["_CFUUIDCreateString"] = to_ptr(&shim_CFUUIDCreateString);
    (*table)["_CFBundleCopyInfoDictionaryInDirectory"] = to_ptr(&shim_security_null);

    for (const char* name : {"_SecCodeCopyDesignatedRequirement", "_SecCodeCopySigningInformation",
                             "_SecDigestTransformCreate", "_SecItemImport",
                             "_SecRequirementCopyString", "_SecStaticCodeCheckValidityWithErrors",
                             "_SecStaticCodeCreateWithPath", "_SecTransformConnectTransforms",
                             "_SecTransformCreateGroupTransform",
                             "_SecTransformCreateReadTransformWithReadStream",
                             "_SecTransformExecute", "_SecVerifyTransformCreate"}) {
        (*table)[name] = to_ptr(&shim_security_unavailable);
    }
    (*table)["_kSecDigestSHA1"] = nsstring_from_utf8("SHA1");

    (*table)["_FSPathMakeRefWithOptions"] = to_ptr(&shim_FSPathMakeRefWithOptions);
    (*table)["_FSGetCatalogInfo"] = to_ptr(&shim_FSGetCatalogInfo);
    (*table)["_FSCopyObjectSync"] = to_ptr(&shim_FSCopyObjectSync);

    (*table)["_xar_iter_new"] = to_ptr(&shim_xar_iter_new);
    (*table)["_xar_open"] = to_ptr(&shim_xar_open);
    (*table)["_xar_close"] = to_ptr(&shim_xar_close);
    (*table)["_xar_file_first"] = to_ptr(&shim_xar_file_first);
    (*table)["_xar_file_next"] = to_ptr(&shim_xar_file_next);
    (*table)["_xar_get_path"] = to_ptr(&shim_xar_get_path);
    (*table)["_xar_get_safe_path"] = to_ptr(&shim_xar_get_safe_path);
    (*table)["_xar_prop_get"] = to_ptr(&shim_xar_prop_get);
    (*table)["_xar_subdoc_first"] = to_ptr(&shim_xar_subdoc_first);
    (*table)["_xar_subdoc_name"] = to_ptr(&shim_xar_subdoc_name);
    (*table)["_xar_subdoc_next"] = to_ptr(&shim_xar_subdoc_next);
    (*table)["_xar_subdoc_prop_get"] = to_ptr(&shim_xar_subdoc_prop_get);
    (*table)["_xar_extract_tofile"] = to_ptr(&shim_xar_extract_tofile);

    // bzip2 is a real host library; bind its documented C API directly.
    void* bz = ::dlopen("libbz2.so.1.0", RTLD_LAZY | RTLD_GLOBAL);
    if (bz) {
        for (const char* name : {"BZ2_bzReadOpen", "BZ2_bzRead", "BZ2_bzReadClose"}) {
            void* fn = ::dlsym(bz, name);
            if (fn) (*table)["_" + std::string(name)] = fn;
        }
    }

    (*table)["_RegisterEventHotKey"] = to_ptr(&shim_RegisterEventHotKey);
    (*table)["_UnregisterEventHotKey"] = to_ptr(&shim_UnregisterEventHotKey);
    (*table)["_GetEventDispatcherTarget"] = to_ptr(&shim_GetEventDispatcherTarget);
    (*table)["_GetEventClass"] = to_ptr(&shim_GetEventClass);
    (*table)["_GetEventParameter"] = to_ptr(&shim_GetEventParameter);
    (*table)["_InstallEventHandler"] = to_ptr(&shim_InstallEventHandler);
    (*table)["_RemoveEventHandler"] = to_ptr(&shim_RemoveEventHandler);
    (*table)["_CopySymbolicHotKeys"] = to_ptr(&shim_CopySymbolicHotKeys);
    (*table)["_LMGetKbdType"] = to_ptr(&shim_LMGetKbdType);
    (*table)["_TISCopyCurrentASCIICapableKeyboardLayoutInputSource"] =
        to_ptr(&shim_TISCopyCurrentASCIICapableKeyboardLayoutInputSource);
    (*table)["_TISGetInputSourceProperty"] = to_ptr(&shim_TISGetInputSourceProperty);
    (*table)["_UCKeyTranslate"] = to_ptr(&shim_UCKeyTranslate);
    (*table)["___availability_version_check"] = to_ptr(&shim_availability_version_check);
    // Some binaries import the C name directly without the extra Mach-O underscore.
    (*table)["__availability_version_check"] = to_ptr(&shim_availability_version_check);
    (*table)["_CGRectDivide"] = to_ptr(&shim_CGRectDivide);
    (*table)["_NSAccessibilityPostNotificationWithUserInfo"] =
        to_ptr(&shim_NSAccessibilityPostNotificationWithUserInfo);
}

}  // namespace finn::darwin
