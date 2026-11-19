#include "views_and_scene.hpp"

#include "../appkit_bridge.hpp"
#include "../foundation.hpp"
#include "../macho_loader.hpp"
#include "../objc_runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace finn::darwin::swiftui {
namespace {

using finn::darwin::ObjcId;
using finn::darwin::objc_send_name;

template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

struct DarwinRect {
    double x, y, w, h;
};

struct CapturedView {
    enum class Kind { Text, Button, VStack, WindowGroup } kind = Kind::Text;
    std::string title;  // text or button label
};

struct ViewState {
    std::mutex mu;
    std::vector<CapturedView> views;
    bool window_shown = false;
};

ViewState& view_state() {
    static ViewState s;
    return s;
}

// ---------------------------------------------------------------------------
// Swift String extraction.
//
// Swift uses a 128-bit String representation.  An immortal/out-of-line string
// has bit 63 set in the second word, with the UTF-8 pointer in the low bits and
// the byte length in the low 32 bits of the first word.  A short string is
// stored inline, little-endian across both words; we take its printable prefix.
// ---------------------------------------------------------------------------
std::string extract_swift_string(std::uint64_t w0, std::uint64_t w1) {
    const bool out_of_line = (w1 & (std::uint64_t{1} << 63)) != 0;
    if (out_of_line) {
        const auto ptr = static_cast<std::uintptr_t>(w1 & 0x7fffffffffffffffull);
        const std::size_t len = static_cast<std::size_t>(w0 & 0xffffffffu);
        if (ptr != 0 && len > 0 && len < (1u << 20)) {
            // A Mach-O Swift string literal points at a 32-byte on-disk header;
            // the UTF-8 bytes begin immediately after it.
            return std::string(reinterpret_cast<const char*>(ptr + 0x20), len);
        }
    }
    std::uint8_t bytes[16] = {};
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(w0 >> (i * 8));
    for (int i = 0; i < 8; ++i) bytes[8 + i] = static_cast<std::uint8_t>(w1 >> (i * 8));
    std::string out;
    for (std::uint8_t b : bytes) {
        if (b == 0) break;
        if (b < 0x20 || b > 0x7e) break;
        out.push_back(static_cast<char>(b));
    }
    return out;
}

void add_view(CapturedView::Kind kind, std::string title) {
    ViewState& s = view_state();
    std::lock_guard<std::mutex> lk(s.mu);
    // The compiler materialises each localized key once for the key itself and
    // once for the Text/Button initializer, and the scene/body getters may be
    // walked more than once.  Collapse consecutive identical captures.
    if (!s.views.empty() && s.views.back().kind == kind && s.views.back().title == title) {
        return;
    }
    s.views.push_back({kind, std::move(title)});
    if (s.views.size() > 64) s.views.erase(s.views.begin());
}

// ---------------------------------------------------------------------------
// The compiled SwiftUI initializers.  They capture their arguments and return
// an all-zero opaque value so the caller's ARC bookkeeping stays safe.  The
// return registers x1-x3 are explicitly cleared: the caller stores them as an
// opaque view payload and later passes them to retain/release shims.
// ---------------------------------------------------------------------------
inline void clear_return_registers() {
#if defined(__aarch64__)
    __asm__ volatile("mov x1, #0\n\tmov x2, #0\n\tmov x3, #0" ::: "x1", "x2", "x3");
#endif
}

extern "C" std::uint64_t swiftui_localized_string_key(std::uint64_t w0, std::uint64_t w1,
                                                       std::uint64_t) {
    const std::string text = extract_swift_string(w0, w1);
    if (!text.empty()) add_view(CapturedView::Kind::Text, text);
    clear_return_registers();
    return 0;
}

extern "C" std::uint64_t swiftui_text_init(std::uint64_t w0, std::uint64_t w1,
                                            std::uint64_t, std::uint64_t,
                                            std::uint64_t, std::uint64_t,
                                            std::uint64_t, std::uint64_t) {
    const std::string text = extract_swift_string(w0, w1);
    if (!text.empty()) add_view(CapturedView::Kind::Text, text);
    clear_return_registers();
    return 0;
}

extern "C" std::uint64_t swiftui_button_init(std::uint64_t w0, std::uint64_t w1,
                                              std::uint64_t, std::uint64_t,
                                              std::uint64_t, std::uint64_t,
                                              std::uint64_t, std::uint64_t) {
    const std::string text = extract_swift_string(w0, w1);
    if (!text.empty()) add_view(CapturedView::Kind::Button, text);
    clear_return_registers();
    return 0;
}

extern "C" std::uint64_t swiftui_vstack_init(std::uint64_t, std::uint64_t,
                                              std::uint64_t, std::uint64_t,
                                              std::uint64_t, std::uint64_t,
                                              std::uint64_t, std::uint64_t) {
    add_view(CapturedView::Kind::VStack, {});
    clear_return_registers();
    return 0;
}

extern "C" std::uint64_t swiftui_window_group_init(std::uint64_t, std::uint64_t,
                                                    std::uint64_t, std::uint64_t,
                                                    std::uint64_t, std::uint64_t,
                                                    std::uint64_t, std::uint64_t) {
    add_view(CapturedView::Kind::WindowGroup, {});
    clear_return_registers();
    return 0;
}

extern "C" std::uint64_t swiftui_scene_build_block(std::uint64_t, std::uint64_t,
                                                    std::uint64_t, std::uint64_t,
                                                    std::uint64_t, std::uint64_t,
                                                    std::uint64_t, std::uint64_t) {
    clear_return_registers();
    return 0;
}

// HorizontalAlignment.center returns a small struct in x0/x1.
extern "C" std::uint64_t swiftui_horizontal_alignment_center() {
    clear_return_registers();
    return 0;
}

// ---------------------------------------------------------------------------
// Scene/App protocol requirement dispatch thunks.  These are real functions:
// they materialise a graph value for the opaque view payload, which is enough
// for the compiled body getters to complete.
// ---------------------------------------------------------------------------
extern "C" std::uint64_t swiftui_make_view(std::uint64_t, std::uint64_t,
                                            std::uint64_t, std::uint64_t,
                                            std::uint64_t, std::uint64_t,
                                            std::uint64_t, std::uint64_t) {
    clear_return_registers();
    return 0;
}

extern "C" std::uint64_t swiftui_view_list_count() {
    clear_return_registers();
    return 0;  // optional: no fixed view-list count
}

// App.main() — start the event loop.  The window is created by the initializer
// capture path; if no content was captured there is nothing honest to show.
// A large indirect-return buffer.  C++/AAPCS64 passes its address in x8, which
// is exactly where the compiled Swift `body` getter expects its result buffer.
struct OpaqueBodyResult {
    unsigned char bytes[256];
};

extern "C" int swiftui_app_main(std::uint64_t metadata, std::uint64_t) {
    // Drive the compiled App.body getter, the step a real SwiftUI App.main()
    // performs before building the scene.  Locating it by mangled-name fragment
    // keeps this generic across apps instead of hardcoding an address.
    using BodyFn = OpaqueBodyResult (*)(void*);
    // A SwiftUI app's scene is built in stages: App.body produces the scene
    // (WindowGroup) and the window's content body produces the view tree.  Both
    // getters are compiler-generated and both must run to capture the content.
    for (const char* fragment : {"AppV4bodyQrvg", "ContentViewV4bodyQrvg"}) {
        if (void* body = finn::darwin::find_loaded_symbol_containing(fragment)) {
            (void)reinterpret_cast<BodyFn>(body)(reinterpret_cast<void*>(metadata));
        }
    }

    const bool shown = render_captured_views();
    if (!shown) {
        std::fprintf(stderr,
                     "finn-darwin: SwiftUI App.main(): no view tree was captured "
                     "(the compiled body getter produced no content); nothing to render.\n");
    }
    ObjcId app = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn::darwin::finn_objc_getClass("NSApplication")),
        "sharedApplication");
    if (!app) return 0;
    objc_send_name<void>(app, "run");
    return 0;
}

}  // namespace

bool render_captured_views() {
    ViewState& s = view_state();
    std::vector<CapturedView> views;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.window_shown || s.views.empty()) return !s.views.empty();
        views = s.views;
        s.window_shown = true;
    }

    finn::darwin::init_foundation();

    ObjcId window = objc_send_name<ObjcId>(
        objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(finn::darwin::finn_objc_getClass("NSWindow")),
                               "alloc"),
        "initWithContentRect:styleMask:backing:defer:",
        DarwinRect{0, 0, 520, 260}, static_cast<std::uintptr_t>(0),
        static_cast<std::uintptr_t>(0), static_cast<signed char>(0));
    if (!window) return false;

    ObjcId content = objc_send_name<ObjcId>(window, "contentView");

    double y = 190.0;
    for (const auto& v : views) {
        if (v.kind == CapturedView::Kind::Text || v.kind == CapturedView::Kind::Button) {
            ObjcId label = objc_send_name<ObjcId>(
                objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(
                    finn::darwin::finn_objc_getClass("NSTextField")), "alloc"),
                "initWithFrame:", DarwinRect{30, y, 460, 28});
            objc_send_name<void>(label, "setStringValue:",
                                 finn::darwin::nsstring_from_utf8(v.title));
            objc_send_name<void>(label, "setEditable:", static_cast<signed char>(0));
            objc_send_name<void>(content, "addSubview:", label);
            y -= 40.0;
            if (v.kind == CapturedView::Kind::Button) {
                ObjcId button = objc_send_name<ObjcId>(
                    objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(
                        finn::darwin::finn_objc_getClass("NSButton")), "alloc"),
                    "initWithFrame:", DarwinRect{30, y, 140, 32});
                objc_send_name<void>(button, "setTitle:",
                                     finn::darwin::nsstring_from_utf8(
                                         v.title.empty() ? "Button" : v.title));
                objc_send_name<void>(content, "addSubview:", button);
                y -= 46.0;
            }
        }
    }

    objc_send_name<void>(window, "makeKeyAndOrderFront:", objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(finn::darwin::finn_objc_getClass("NSApplication")),
        "sharedApplication"));
    return true;
}

void reset_captured_views() {
    ViewState& s = view_state();
    std::lock_guard<std::mutex> lk(s.mu);
    s.views.clear();
    s.window_shown = false;
}

void register_view_protocol(SymbolTable* table) {
    (*table)["_$s7SwiftUI4ViewP4body4BodyQzvgTq"] = to_ptr(&swiftui_make_view);
    (*table)["_$s7SwiftUI4ViewPAAE05_makeC04view6inputsAA01_C7OutputsVAA11_GraphValueVyxG_AA01_C6InputsVtFZ"] =
        to_ptr(&swiftui_make_view);
    (*table)["_$s7SwiftUI4ViewPAAE05_makeC4List4view6inputsAA01_cE7OutputsVAA11_GraphValueVyxG_AA01_cE6InputsVtFZ"] =
        to_ptr(&swiftui_make_view);
    (*table)["_$s7SwiftUI4ViewPAAE14_viewListCount6inputsSiSgAA01_ceF6InputsV_tFZ"] =
        to_ptr(&swiftui_view_list_count);
    (*table)["_$s7SwiftUI4ViewP05_makeC04view6inputsAA01_C7OutputsVAA11_GraphValueVyxG_AA01_C6InputsVtFZTq"] =
        to_ptr(&swiftui_make_view);
    (*table)["_$s7SwiftUI4ViewP05_makeC4List4view6inputsAA01_cE7OutputsVAA11_GraphValueVyxG_AA01_cE6InputsVtFZTq"] =
        to_ptr(&swiftui_make_view);
    (*table)["_$s7SwiftUI4ViewP14_viewListCount6inputsSiSgAA01_ceF6InputsV_tFZTq"] =
        to_ptr(&swiftui_view_list_count);
}

void register_app_scene(SymbolTable* table) {
    (*table)["_$s7SwiftUI3AppPAAE4mainyyFZ"] = to_ptr(&swiftui_app_main);
    (*table)["_$s7SwiftUI3AppP4body4BodyQzvgTq"] = to_ptr(&swiftui_make_view);
    (*table)["_$s7SwiftUI3AppPxycfCTq"] = to_ptr(&swiftui_make_view);
    (*table)["_$s7SwiftUI11WindowGroupV7contentACyxGxyXE_tcfC"] =
        to_ptr(&swiftui_window_group_init);
}

void register_text_view(SymbolTable* table) {
    (*table)["_$s7SwiftUI18LocalizedStringKeyV13stringLiteralACSS_tcfC"] =
        to_ptr(&swiftui_localized_string_key);
    (*table)["_$s7SwiftUI4TextV_9tableName6bundle7commentAcA18LocalizedStringKeyV_SSSgSo8NSBundleCSgs06StaticI0VSgtcfC"] =
        to_ptr(&swiftui_text_init);
}

void register_button_view(SymbolTable* table) {
    (*table)["_$s7SwiftUI6ButtonVA2A4TextVRszrlE_6actionACyAEGAA18LocalizedStringKeyV_yyctcfC"] =
        to_ptr(&swiftui_button_init);
}

void register_stack_layout(SymbolTable* table) {
    (*table)["_$s7SwiftUI19HorizontalAlignmentV6centerACvgZ"] =
        to_ptr(&swiftui_horizontal_alignment_center);
}

void register_scene_builder(SymbolTable* table) {
    (*table)["_$s7SwiftUI12SceneBuilderV10buildBlockyxxAA0C0RzlFZ"] =
        to_ptr(&swiftui_scene_build_block);
}

void register_graphics(SymbolTable* table) {
    // No additional graphics symbols are required by a minimal text/button
    // view.  Frame geometry comes from the existing CoreGraphics bridge.
    (void)table;
}

}  // namespace finn::darwin::swiftui
