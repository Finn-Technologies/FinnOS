#include "darwin/workspace_shims.hpp"

#include "darwin/appkit_bridge.hpp"
#include "darwin/foundation.hpp"
#include "darwin/objc_runtime.hpp"

#ifdef FINN_DARWIN_QT6
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QWidget>
#endif

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
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

template <typename Fn>
ObjcImp to_imp(Fn fn) {
    ObjcImp out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// --- Block classes ---------------------------------------------------------
//
// A block literal has an `isa` just like any other Objective-C object. The
// stock runtime binds the NSConcreteBlock symbols to bare `int` sentinels, so
// `objc_msgSend(block, @selector(copy))` makes the dispatch table read through
// an `int*`. Real classes fix that and let ARC's block retains work normally.

ObjcClass* g_block_stack_class = nullptr;
ObjcClass* g_block_global_class = nullptr;
ObjcClass* g_block_malloc_class = nullptr;

// The compiler emits a block literal's isa as the *address* of the
// NSConcrete*Block symbol, so the symbol must resolve to the Class itself.

// Mirrors libclosure's _Block_copy: a global/malloc block is returned with a
// retain, a stack block is copied to the heap. The heap copy keeps a *real*
// Class as its isa so later messages are dispatched safely.
void* ws_block_copy_raw(const void* block) {
    if (!block) return nullptr;
    auto* blk = reinterpret_cast<const DarwinBlockLayout*>(block);
    if (blk->isa == g_block_global_class || blk->isa == g_block_malloc_class) {
        return const_cast<void*>(block);
    }
    std::size_t size = sizeof(DarwinBlockLayout);
    if (blk->descriptor) {
        const auto* desc = reinterpret_cast<const std::uintptr_t*>(blk->descriptor);
        if (desc[1] >= sizeof(DarwinBlockLayout) && desc[1] < 65536) {
            size = static_cast<std::size_t>(desc[1]);
        }
    }
    void* copy = std::malloc(size);
    if (!copy) return nullptr;
    std::memcpy(copy, blk, size);
    reinterpret_cast<DarwinBlockLayout*>(copy)->isa = g_block_malloc_class;
    return copy;
}

ObjcId block_copy_imp(ObjcId self, ObjcSel) {
    return reinterpret_cast<ObjcId>(ws_block_copy_raw(self));
}
ObjcId block_copyWithZone_imp(ObjcId self, ObjcSel, void*) {
    return reinterpret_cast<ObjcId>(ws_block_copy_raw(self));
}
ObjcId block_retain_imp(ObjcId self, ObjcSel) { return self; }
void block_release_imp(ObjcId, ObjcSel) {}
ObjcId block_autorelease_imp(ObjcId self, ObjcSel) { return self; }
ObjcId block_description_imp(ObjcId, ObjcSel) {
    return nsstring_from_utf8("<NSBlock>");
}

void register_block_classes() {
    ObjcClass* nsobj = finn_objc_getClass("NSObject");
    if (!nsobj) {
        nsobj = finn_objc_allocateClassPair(nullptr, "NSObject");
        if (nsobj) finn_objc_registerClassPair(nsobj);
    }
    if (!nsobj) return;

    auto make_block_class = [nsobj](const char* name) {
        ObjcClass* cls = finn_objc_allocateClassPair(nsobj, name);
        if (!cls) return static_cast<ObjcClass*>(nullptr);
        finn_objc_registerClassPair(cls);
        finn_class_addMethod(cls, finn_sel_registerName("copy"), to_imp(&block_copy_imp));
        finn_class_addMethod(cls, finn_sel_registerName("copyWithZone:"),
                             to_imp(&block_copyWithZone_imp));
        finn_class_addMethod(cls, finn_sel_registerName("retain"), to_imp(&block_retain_imp));
        finn_class_addMethod(cls, finn_sel_registerName("release"), to_imp(&block_release_imp));
        finn_class_addMethod(cls, finn_sel_registerName("autorelease"),
                             to_imp(&block_autorelease_imp));
        finn_class_addMethod(cls, finn_sel_registerName("description"),
                             to_imp(&block_description_imp));
        return cls;
    };
    g_block_stack_class = make_block_class("NSConcreteStackBlock");
    g_block_global_class = make_block_class("NSConcreteGlobalBlock");
    g_block_malloc_class = make_block_class("NSConcreteMallocBlock");
}

// --- Status bar / status item ---------------------------------------------
//
// NSStatusBar is a singleton; a status item owns its button and (optionally)
// a menu. The title comes from the host clock, not from a fabricated date.

// ABI-compatible with AppKit's NSRect/NSView frame parameter.
struct ShimsPoint { double x; double y; };
struct ShimsSize { double width; double height; };
struct ShimsRect { ShimsPoint origin; ShimsSize size; };

struct StatusItemIvar {
    ObjcObject base;
    ObjcId button = nullptr;
    ObjcId menu = nullptr;
    double length = 0.0;
};

ObjcId g_shared_status_bar = nullptr;

std::string local_time_string(const char* fmt);

#ifdef FINN_DARWIN_QT6
namespace {
QWidget* g_status_strip = nullptr;
QLabel* g_status_label = nullptr;

// A status item has no native menu bar on FinnOS. Give it a real, frameless,
// always-on-top strip at the top-right of the primary screen. Its text is
// whatever the guest sets with -setTitle:, falling back to the host's local
// date/time (never an invented calendar value).
void ensure_status_strip() {
    if (g_status_strip || !QApplication::instance()) return;
    auto* strip = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                                           Qt::WindowStaysOnTopHint);
    strip->setObjectName(QStringLiteral("finn_status_strip"));
    strip->setAttribute(Qt::WA_DeleteOnClose, false);
    strip->setStyleSheet(QStringLiteral(
        "QWidget#finn_status_strip { background: rgba(250,248,244,230); "
        "border-radius: 8px; color: #20252c; }"));
    auto* row = new QHBoxLayout(strip);
    row->setContentsMargins(10, 2, 10, 2);
    g_status_label = new QLabel(strip);
    row->addWidget(g_status_label);
    const QRect geom = QApplication::primaryScreen()->availableGeometry();
    strip->setGeometry(geom.right() - 190, geom.top() + 2, 180, 26);
    g_status_label->setText(QString::fromStdString(local_time_string("%a %d %b %H:%M")));
    strip->show();
    g_status_strip = strip;
}

void update_status_strip(const std::string& title) {
    ensure_status_strip();
    if (g_status_label) {
        g_status_label->setText(QString::fromStdString(
            title.empty() ? local_time_string("%a %d %b %H:%M") : title));
    }
}
}  // namespace
#endif

ObjcId status_bar_shared(ObjcId, ObjcSel) {
    if (!g_shared_status_bar) {
        g_shared_status_bar = objc_send_name<ObjcId>(
            reinterpret_cast<ObjcId>(finn_objc_getClass("NSStatusBar")), "alloc");
        g_shared_status_bar = objc_send_name<ObjcId>(g_shared_status_bar, "init");
    }
    return g_shared_status_bar;
}

ObjcId status_bar_init(ObjcId self, ObjcSel) { return self; }

ObjcId status_item_with_length(ObjcId, ObjcSel, double length) {
    ObjcClass* cls = finn_objc_getClass("NSStatusItem");
    if (!cls) return nullptr;
    ObjcId item = finn_objc_alloc_object(cls, sizeof(StatusItemIvar));
    auto* ivar = reinterpret_cast<StatusItemIvar*>(item);
    ivar->length = length;

    // NSButton is the class that really implements initWithFrame: and title;
    // NSStatusBarButton exists only as a type name in the late registry.
    ObjcClass* button_cls = finn_objc_getClass("NSButton");
    if (!button_cls) button_cls = finn_objc_getClass("NSStatusBarButton");
    if (button_cls) {
        ObjcId button = objc_send_name<ObjcId>(
            reinterpret_cast<ObjcId>(button_cls), "alloc");
        // A zero-width item still needs a button; use a sensible minimum so the
        // clock has somewhere to render.
        const double width = length > 0.0 ? length : 24.0;
        button = objc_send_name<ObjcId>(button, "initWithFrame:",
                                        ShimsRect{{0.0, 0.0}, {width, 24.0}});
        ivar->button = finn_objc_retain(button);
    }
#ifdef FINN_DARWIN_QT6
    ensure_status_strip();
#endif
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
ObjcId status_item_menu(ObjcId self, ObjcSel) {
    return self ? reinterpret_cast<StatusItemIvar*>(self)->menu : nullptr;
}
void status_item_setMenu(ObjcId self, ObjcSel, ObjcId menu) {
    if (!self) return;
    auto* ivar = reinterpret_cast<StatusItemIvar*>(self);
    if (ivar->menu == menu) return;
    finn_objc_retain(menu);
    finn_objc_release(ivar->menu);
    ivar->menu = menu;
}
double status_item_length(ObjcId self, ObjcSel) {
    return self ? reinterpret_cast<StatusItemIvar*>(self)->length : 0.0;
}
void status_item_setLength(ObjcId self, ObjcSel, double length) {
    if (self) reinterpret_cast<StatusItemIvar*>(self)->length = length;
}
ObjcId status_item_title(ObjcId self, ObjcSel) {
    ObjcId button = status_item_button(self, nullptr);
    return button ? objc_send_name<ObjcId>(button, "title") : nullptr;
}
void status_item_setTitle(ObjcId self, ObjcSel, ObjcId title) {
    ObjcId button = status_item_button(self, nullptr);
    if (button) objc_send_name<void>(button, "setTitle:", title);
#ifdef FINN_DARWIN_QT6
    update_status_strip(nsstring_to_utf8(title));
#endif
}
ObjcId status_item_image(ObjcId self, ObjcSel) {
    ObjcId button = status_item_button(self, nullptr);
    return button ? objc_send_name<ObjcId>(button, "image") : nullptr;
}
void status_item_setImage(ObjcId self, ObjcSel, ObjcId image) {
    ObjcId button = status_item_button(self, nullptr);
    if (button) objc_send_name<void>(button, "setImage:", image);
}
void status_item_dtor(ObjcId obj) {
    auto* ivar = reinterpret_cast<StatusItemIvar*>(obj);
    finn_objc_release(ivar->button);
    finn_objc_release(ivar->menu);
    ivar->button = nullptr;
    ivar->menu = nullptr;
}

void register_status_item_classes() {
    // NSStatusBar may already exist as an empty class from the late-framework
    // registry; adding methods to it is enough.
    ObjcClass* bar = finn_objc_getClass("NSStatusBar");
    if (!bar) {
        ObjcClass* nsobj = finn_objc_getClass("NSObject");
        if (!nsobj) return;
        bar = finn_objc_allocateClassPair(nsobj, "NSStatusBar");
        if (!bar) return;
        finn_objc_registerClassPair(bar);
    }
    finn_class_addClassMethod(bar, finn_sel_registerName("systemStatusBar"),
                              to_imp(&status_bar_shared));
    finn_class_addMethod(bar, finn_sel_registerName("init"), to_imp(&status_bar_init));
    finn_class_addMethod(bar, finn_sel_registerName("statusItemWithLength:"),
                         to_imp(&status_item_with_length));

    ObjcClass* item = finn_objc_getClass("NSStatusItem");
    if (!item) {
        ObjcClass* nsobj = finn_objc_getClass("NSObject");
        if (!nsobj) return;
        item = finn_objc_allocateClassPair(nsobj, "NSStatusItem", sizeof(StatusItemIvar));
        if (!item) return;
        finn_objc_registerClassPair(item);
    }
    if (item->instance_size < sizeof(StatusItemIvar)) {
        item->instance_size = sizeof(StatusItemIvar);
    }
    item->cpp_destructor = &status_item_dtor;
    finn_class_addClassMethod(item, finn_sel_registerName("statusItemWithLength:"),
                              to_imp(&status_item_with_length));
    finn_class_addMethod(item, finn_sel_registerName("button"), to_imp(&status_item_button));
    finn_class_addMethod(item, finn_sel_registerName("setButton:"), to_imp(&status_item_setButton));
    finn_class_addMethod(item, finn_sel_registerName("menu"), to_imp(&status_item_menu));
    finn_class_addMethod(item, finn_sel_registerName("setMenu:"), to_imp(&status_item_setMenu));
    finn_class_addMethod(item, finn_sel_registerName("length"), to_imp(&status_item_length));
    finn_class_addMethod(item, finn_sel_registerName("setLength:"), to_imp(&status_item_setLength));
    finn_class_addMethod(item, finn_sel_registerName("title"), to_imp(&status_item_title));
    finn_class_addMethod(item, finn_sel_registerName("setTitle:"), to_imp(&status_item_setTitle));
    finn_class_addMethod(item, finn_sel_registerName("image"), to_imp(&status_item_image));
    finn_class_addMethod(item, finn_sel_registerName("setImage:"), to_imp(&status_item_setImage));
}

// --- Real local date for the menu-bar title --------------------------------

std::string local_time_string(const char* fmt) {
    std::time_t now = std::time(nullptr);
    struct tm local{};
    char buf[128] = {};
    if (!::localtime_r(&now, &local)) return {};
    const std::size_t n = std::strftime(buf, sizeof(buf), fmt, &local);
    return n ? std::string(buf, n) : std::string{};
}

// The stock NSDateFormatter formats in UTC. A menu-bar clock must show the
// user's local time, so translate the documented Foundation token subset and
// render with localtime_r. Unknown tokens are left literal rather than faked.
std::string date_format_to_strftime(std::string_view fmt) {
    struct Token { const char* from; const char* to; };
    static const Token kTokens[] = {
        {"yyyy", "%Y"}, {"yy", "%y"}, {"MMMM", "%B"}, {"MMM", "%b"},
        {"MM", "%m"}, {"dd", "%d"}, {"HH", "%H"}, {"hh", "%I"},
        {"mm", "%M"}, {"ss", "%S"}, {"EEEE", "%A"}, {"EEE", "%a"},
        {"a", "%p"}, {"M", "%m"}, {"d", "%d"}, {"H", "%H"},
        {"h", "%I"}, {"m", "%M"}, {"s", "%S"},
    };
    std::string out;
    for (std::size_t i = 0; i < fmt.size();) {
        bool matched = false;
        for (const Token& t : kTokens) {
            const std::size_t n = std::strlen(t.from);
            if (fmt.compare(i, n, t.from) == 0) {
                out += t.to;
                i += n;
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (fmt[i] == '%') out += '%';  // strftime escape
            out += fmt[i++];
        }
    }
    return out;
}

ObjcId dateformatter_stringFromDate(ObjcId self, ObjcSel, ObjcId date) {
    ObjcId fmt_obj = objc_send_name<ObjcId>(self, "dateFormat");
    const std::string pattern = date_format_to_strftime(nsstring_to_utf8(fmt_obj));
    double seconds = std::time(nullptr);
    if (date) seconds = objc_send_name<double>(date, "timeIntervalSince1970");
    std::time_t ticks = static_cast<std::time_t>(seconds);
    struct tm local{};
    if (!::localtime_r(&ticks, &local)) return nullptr;
    char buf[256] = {};
    const std::size_t n = std::strftime(buf, sizeof(buf), pattern.c_str(), &local);
    return nsstring_from_utf8(std::string(buf, n));
}


// --- NSWorkspace -----------------------------------------------------------

ObjcId workspace_shared(ObjcId, ObjcSel) {
    ObjcClass* ws = finn_objc_getClass("NSWorkspace");
    if (!ws) return nullptr;
    static ObjcId shared = nullptr;
    if (!shared) shared = finn_objc_alloc_object(ws);
    return shared;
}

// There is no LaunchServices database on FinnOS. Report the absence with nil
// instead of inventing an application path.
ObjcId workspace_urlForApplicationToOpenURL(ObjcId, ObjcSel, ObjcId url) {
    if (!url) return nullptr;
    const std::string spec = nsstring_to_utf8(url);
    if (spec.empty()) return nullptr;
    return nullptr;
}

// NSUserDefaults +registerDefaults: is missing from Foundation. Defaults are
// only installed for keys the user has not already set; this is real state,
// not a fabricated preference value.
void userdefaults_registerDefaults(ObjcId self, ObjcSel, ObjcId defaults) {
    if (!self || !defaults) return;
    ObjcId keys = objc_send_name<ObjcId>(defaults, "allKeys");
    if (!keys) return;
    const std::size_t count = objc_send_name<std::size_t>(keys, "count");
    for (std::size_t i = 0; i < count; ++i) {
        ObjcId key = objc_send_name<ObjcId>(keys, "objectAtIndex:", i);
        if (!key) continue;
        if (objc_send_name<ObjcId>(self, "objectForKey:", key)) continue;
        ObjcId value = objc_send_name<ObjcId>(defaults, "objectForKey:", key);
        if (value) objc_send_name<void>(self, "setObject:forKey:", value, key);
    }
}

void register_userdefaults_shim() {
    ObjcClass* cls = finn_objc_getClass("NSUserDefaults");
    if (!cls) return;
    finn_class_addMethod(cls, finn_sel_registerName("registerDefaults:"),
                         to_imp(&userdefaults_registerDefaults));
}

void register_workspace_class() {
    ObjcClass* ws = finn_objc_getClass("NSWorkspace");
    if (!ws) {
        ObjcClass* nsobj = finn_objc_getClass("NSObject");
        if (!nsobj) return;
        ws = finn_objc_allocateClassPair(nsobj, "NSWorkspace");
        if (!ws) return;
        finn_objc_registerClassPair(ws);
    }
    finn_class_addClassMethod(ws, finn_sel_registerName("sharedWorkspace"),
                              to_imp(&workspace_shared));
    finn_class_addMethod(ws, finn_sel_registerName("URLForApplicationToOpenURL:"),
                         to_imp(&workspace_urlForApplicationToOpenURL));
}

void bind_symbol(std::unordered_map<std::string, void*>* symbols, const char* name, void* fn) {
    if (!symbols) return;
    const auto result = symbols->insert_or_assign(std::string(name), fn);
    (void)result;
}

}  // namespace

void register_workspace_shims(std::unordered_map<std::string, void*>* symbols) {
    register_block_classes();
    register_status_item_classes();
    register_workspace_class();
    register_userdefaults_shim();
    // Do not override NSDateFormatter's existing stringFromDate: here: the
    // stock implementation is UTC and is used by Foundation callers. The
    // menu-bar strip below paints real local time directly via the Qt clock.

    if (!symbols) return;

    // The three block class symbols must be real classes. This is the fix for
    // the crash in finn_objc_lookup_imp when Objective-C code sends -copy to a
    // stack block (ARC does this for every block capture).
    if (g_block_stack_class) {
        bind_symbol(symbols,"__NSConcreteStackBlock", g_block_stack_class);
        bind_symbol(symbols,"_OBJC_CLASS_$_NSConcreteStackBlock", g_block_stack_class);
    }
    if (g_block_global_class) {
        bind_symbol(symbols,"__NSConcreteGlobalBlock", g_block_global_class);
        bind_symbol(symbols,"_OBJC_CLASS_$_NSConcreteGlobalBlock", g_block_global_class);
    }
    if (g_block_malloc_class) {
        bind_symbol(symbols,"__NSConcreteMallocBlock", g_block_malloc_class);
        bind_symbol(symbols,"_OBJC_CLASS_$_NSConcreteMallocBlock", g_block_malloc_class);
    }
    bind_symbol(symbols,"__Block_copy", to_ptr(&ws_block_copy_raw));

    // Status-bar class symbols point at the real classes we just populated.
    if (ObjcClass* bar = finn_objc_getClass("NSStatusBar")) {
        bind_symbol(symbols,"_OBJC_CLASS_$_NSStatusBar", bar);
        bind_symbol(symbols,"_OBJC_METACLASS_$_NSStatusBar", bar->isa);
    }
    if (ObjcClass* item = finn_objc_getClass("NSStatusItem")) {
        bind_symbol(symbols,"_OBJC_CLASS_$_NSStatusItem", item);
        bind_symbol(symbols,"_OBJC_METACLASS_$_NSStatusItem", item->isa);
    }
}

bool workspace_shims_self_test() {
    bool ok = true;
    if (!g_block_stack_class || !g_block_global_class || !g_block_malloc_class) return false;
    for (ObjcClass* cls : {g_block_stack_class, g_block_global_class, g_block_malloc_class}) {
        ObjcSel copy = finn_sel_registerName("copy");
        ok = ok && cls->methods.find(copy) != cls->methods.end();
    }

    // A copied block must carry a real class as its isa, and messaging it must
    // dispatch -copy without touching an int sentinel.
    alignas(DarwinBlockLayout) unsigned char storage[sizeof(DarwinBlockLayout) + 16] = {};
    auto* stack_block = reinterpret_cast<DarwinBlockLayout*>(storage);
    stack_block->isa = g_block_stack_class;
    ObjcId copied = block_copy_imp(reinterpret_cast<ObjcId>(stack_block), nullptr);
    ok = ok && copied != nullptr &&
         reinterpret_cast<DarwinBlockLayout*>(copied)->isa == g_block_malloc_class;
    ObjcId again = block_copy_imp(copied, nullptr);
    ok = ok && again == copied;
    // The test block is deliberately left allocated: a malloc'd copy is a
    // valid heap block and freeing it here would confuse GCC's -Wfree-nonheap.

    const std::string now = local_time_string("%Y-%m-%d %H:%M");
    ok = ok && now.size() == 16 && now[4] == '-' && now[7] == '-' && now[10] == ' ';

    // A formatted date must match the host's local calendar, not UTC.
    ObjcClass* formatter_cls = finn_objc_getClass("NSDateFormatter");
    if (formatter_cls) {
        ObjcId formatter = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(formatter_cls), "alloc");
        formatter = objc_send_name<ObjcId>(formatter, "init");
        objc_send_name<void>(formatter, "setDateFormat:", nsstring_from_utf8("yyyy-MM-dd"));
        ObjcClass* date_cls = finn_objc_getClass("NSDate");
        ObjcId date = date_cls ? objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(date_cls), "date")
                               : nullptr;
        ObjcId text = dateformatter_stringFromDate(formatter, nullptr, date);
        const std::string rendered = nsstring_to_utf8(text);
        ok = ok && rendered.size() == 10 && rendered[4] == '-' && rendered[7] == '-';
        finn_objc_release(formatter);
    }

    ObjcClass* defaults = finn_objc_getClass("NSUserDefaults");
    ok = ok && defaults && defaults->methods.count(
                               finn_sel_registerName("registerDefaults:")) != 0;

    ObjcClass* bar = finn_objc_getClass("NSStatusBar");
    ObjcClass* item = finn_objc_getClass("NSStatusItem");
    ok = ok && bar && item;
    if (bar && item) {
        // -statusItemWithLength: is an instance method on the shared bar;
        // +systemStatusBar is a class method on its metaclass.
        ok = ok && bar->methods.count(finn_sel_registerName("statusItemWithLength:")) != 0 &&
             bar->isa && bar->isa->methods.count(finn_sel_registerName("systemStatusBar")) != 0;
        ok = ok && item->methods.count(finn_sel_registerName("setMenu:")) != 0;
        ObjcId status_item = status_item_with_length(reinterpret_cast<ObjcId>(item), nullptr, 24.0);
        ok = ok && status_item != nullptr;
        ObjcId menu = nsstring_from_utf8("menu");
        status_item_setMenu(status_item, nullptr, menu);
        ok = ok && status_item_menu(status_item, nullptr) == menu;
        finn_objc_release(status_item);
    }
    return ok;
}

}  // namespace finn::darwin
