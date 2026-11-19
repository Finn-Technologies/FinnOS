#include "darwin/appkit_bridge.hpp"

#include "darwin/foundation.hpp"
#include "darwin/objc_runtime.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef FINN_DARWIN_QT6
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>
#endif

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

struct DarwinPoint { double x; double y; };
struct DarwinSize { double width; double height; };
struct DarwinRect { DarwinPoint origin; DarwinSize size; };

struct NSViewObj {
    ObjcObject base;
    DarwinRect frame;
    ObjcId superview;
    std::vector<ObjcId>* subviews;
    ObjcId target;
    ObjcSel action;
    std::string* title;
#ifdef FINN_DARWIN_QT6
    QWidget* widget;
#else
    void* widget;
#endif
};

struct NSWindowObj {
    ObjcObject base;
    DarwinRect frame;
    std::string* title;
    ObjcId content_view;
    ObjcId delegate;
#ifdef FINN_DARWIN_QT6
    QMainWindow* window;
#else
    void* window;
#endif
};

struct NSAppObj {
    ObjcObject base;
    ObjcId delegate;
    ObjcId main_menu;
    std::vector<ObjcId>* windows;
};

struct NSMenuObj {
    ObjcObject base;
    std::string* title;
    std::vector<ObjcId>* items;
#ifdef FINN_DARWIN_QT6
    QMenu* menu;
#else
    void* menu;
#endif
};

struct NSMenuItemObj {
    ObjcObject base;
    std::string* title;
    ObjcId target;
    ObjcSel action;
    ObjcId submenu;
};

struct NSAlertObj {
    ObjcObject base;
    std::string* message;
    std::string* info;
};

struct NSColorObj {
    ObjcObject base;
    double r, g, b, a;
};

struct NSFontObj {
    ObjcObject base;
    std::string* family;
    double size;
    bool bold;
};

ObjcClass* g_cls_nsapp = nullptr;
ObjcClass* g_cls_nswindow = nullptr;
ObjcClass* g_cls_nsview = nullptr;
ObjcClass* g_cls_nscontrol = nullptr;
ObjcClass* g_cls_nsbutton = nullptr;
ObjcClass* g_cls_nstextfield = nullptr;
ObjcClass* g_cls_nstextview = nullptr;
ObjcClass* g_cls_nsscrollview = nullptr;
ObjcClass* g_cls_nsmenu = nullptr;
ObjcClass* g_cls_nsmenuitem = nullptr;
ObjcClass* g_cls_nsalert = nullptr;
ObjcClass* g_cls_nscolor = nullptr;
ObjcClass* g_cls_nsfont = nullptr;
ObjcClass* g_cls_nsimage = nullptr;
ObjcClass* g_cls_nstimer = nullptr;

ObjcId g_shared_app = nullptr;
int g_qt_argc = 0;
char** g_qt_argv = nullptr;
#ifdef FINN_DARWIN_QT6
QApplication* g_qapp = nullptr;

QApplication* ensure_qapp() {
    if (!g_qapp) {
        if (!QApplication::instance()) {
            g_qapp = new QApplication(g_qt_argc, g_qt_argv);
        }
    }
    return qobject_cast<QApplication*>(QApplication::instance());
}

// Converts macOS bottom-left coordinates into Qt top-left widget geometry.
void sync_qt_geometry(NSViewObj* view, int parent_height) {
    if (!view || !view->widget) return;
    const int x = static_cast<int>(std::lround(view->frame.origin.x));
    const int w = std::max(1, static_cast<int>(std::lround(view->frame.size.width)));
    const int h = std::max(1, static_cast<int>(std::lround(view->frame.size.height)));
    const int y = std::max(0, parent_height - static_cast<int>(std::lround(view->frame.origin.y)) - h);
    view->widget->setGeometry(x, y, w, h);
}
#endif

// --- NSApplication ---
ObjcId nsapp_shared(ObjcId, ObjcSel) {
    if (!g_shared_app) {
        auto* a = reinterpret_cast<NSAppObj*>(
            finn_objc_alloc_object(g_cls_nsapp, sizeof(NSAppObj)));
        a->windows = new std::vector<ObjcId>();
        g_shared_app = reinterpret_cast<ObjcId>(a);
#ifdef FINN_DARWIN_QT6
        (void)ensure_qapp();
#endif
    }
    return g_shared_app;
}
void nsapp_setDelegate(ObjcId self, ObjcSel, ObjcId del) {
    reinterpret_cast<NSAppObj*>(self)->delegate = del;
}
ObjcId nsapp_delegate(ObjcId self, ObjcSel) {
    return reinterpret_cast<NSAppObj*>(self)->delegate;
}
int nsapp_setActivationPolicy(ObjcId, ObjcSel, std::intptr_t) { return 1; }
void nsapp_activateIgnoringOtherApps(ObjcId, ObjcSel, int) {}
void nsapp_setMainMenu(ObjcId self, ObjcSel, ObjcId menu) {
    reinterpret_cast<NSAppObj*>(self)->main_menu = menu;
}
void nsapp_run(ObjcId self, ObjcSel) {
    auto* app = reinterpret_cast<NSAppObj*>(self);
    if (app->delegate &&
        finn_objc_respondsToSelector(app->delegate,
                                     finn_sel_registerName("applicationDidFinishLaunching:"))) {
        objc_send_name<void>(app->delegate, "applicationDidFinishLaunching:", nullptr);
    }
#ifdef FINN_DARWIN_QT6
    if (QApplication* qa = ensure_qapp()) {
        qa->exec();
        return;
    }
#endif
    std::fprintf(stderr,
                 "finn-darwin: NSApplication run() invoked without Qt6 display backend.\n");
}
void nsapp_terminate(ObjcId, ObjcSel, ObjcId) {
#ifdef FINN_DARWIN_QT6
    if (auto* qa = QApplication::instance()) qa->quit();
#endif
}

int shim_NSApplicationMain(int, const char**) {
    ObjcId app = nsapp_shared(nullptr, nullptr);
    nsapp_run(app, nullptr);
    return 0;
}

// --- NSView ---
ObjcId nsview_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->frame = frame;
    if (!v->subviews) v->subviews = new std::vector<ObjcId>();
    if (!v->title) v->title = new std::string();
#ifdef FINN_DARWIN_QT6
    if (!v->widget) {
        (void)ensure_qapp();
        v->widget = new QWidget();
        v->widget->resize(std::max(1, static_cast<int>(frame.size.width)),
                          std::max(1, static_cast<int>(frame.size.height)));
    }
#endif
    return self;
}

void nsview_addSubview(ObjcId self, ObjcSel, ObjcId sub) {
    if (!self || !sub) return;
    auto* parent = reinterpret_cast<NSViewObj*>(self);
    auto* child = reinterpret_cast<NSViewObj*>(sub);
    if (!parent->subviews) parent->subviews = new std::vector<ObjcId>();
    parent->subviews->push_back(finn_objc_retain(sub));
    child->superview = self;
#ifdef FINN_DARWIN_QT6
    if (parent->widget && child->widget) {
        child->widget->setParent(parent->widget);
        sync_qt_geometry(child, std::max(1, parent->widget->height()));
        child->widget->show();
    }
#endif
}

void nsview_setFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->frame = frame;
#ifdef FINN_DARWIN_QT6
    if (v->widget) {
        const int ph = v->widget->parentWidget() ? v->widget->parentWidget()->height()
                                                 : static_cast<int>(frame.size.height);
        sync_qt_geometry(v, ph);
    }
#endif
}

DarwinRect nsview_frame(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->frame;
}

void nsview_setHidden(ObjcId self, ObjcSel, int hidden) {
#ifdef FINN_DARWIN_QT6
    auto* v = reinterpret_cast<NSViewObj*>(self);
    if (v->widget) v->widget->setVisible(!hidden);
#else
    (void)self;
    (void)hidden;
#endif
}

// --- NSControl / NSButton / NSTextField ---
void nscontrol_setTarget(ObjcId self, ObjcSel, ObjcId target) {
    reinterpret_cast<NSViewObj*>(self)->target = target;
}
void nscontrol_setAction(ObjcId self, ObjcSel, ObjcSel action) {
    reinterpret_cast<NSViewObj*>(self)->action = action;
}
void nscontrol_setEnabled(ObjcId self, ObjcSel, int enabled) {
#ifdef FINN_DARWIN_QT6
    auto* v = reinterpret_cast<NSViewObj*>(self);
    if (v->widget) v->widget->setEnabled(enabled != 0);
#else
    (void)self;
    (void)enabled;
#endif
}

ObjcId nsbutton_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->frame = frame;
    v->subviews = new std::vector<ObjcId>();
    v->title = new std::string();
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    auto* btn = new QPushButton();
    v->widget = btn;
    QObject::connect(btn, &QPushButton::clicked, [self]() {
        auto* bv = reinterpret_cast<NSViewObj*>(self);
        if (bv->target && bv->action) {
            objc_send<void>(bv->target, bv->action, self);
        }
    });
#endif
    return self;
}

ObjcId nsbutton_buttonWithTitleTargetAction(ObjcId, ObjcSel, ObjcId title,
                                            ObjcId target, ObjcSel action) {
    ObjcId btn = finn_objc_alloc_object(g_cls_nsbutton, sizeof(NSViewObj));
    nsbutton_initWithFrame(btn, nullptr, {{0, 0}, {120, 32}});
    objc_send_name<void>(btn, "setTitle:", title);
    nscontrol_setTarget(btn, nullptr, target);
    nscontrol_setAction(btn, nullptr, action);
    return btn;
}

void nsbutton_setTitle(ObjcId self, ObjcSel, ObjcId title) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    const std::string text = nsstring_to_utf8(title);
    if (!v->title) v->title = new std::string();
    *v->title = text;
#ifdef FINN_DARWIN_QT6
    if (auto* btn = qobject_cast<QPushButton*>(v->widget)) {
        btn->setText(QString::fromStdString(text));
    }
#endif
}

ObjcId nsbutton_title(ObjcId self, ObjcSel) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    return nsstring_from_utf8(v->title ? *v->title : "");
}

ObjcId nstextfield_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->frame = frame;
    v->subviews = new std::vector<ObjcId>();
    v->title = new std::string();
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    v->widget = new QLineEdit();
#endif
    return self;
}

ObjcId nstextfield_labelWithString(ObjcId, ObjcSel, ObjcId text) {
    ObjcId tf = finn_objc_alloc_object(g_cls_nstextfield, sizeof(NSViewObj));
    auto* v = reinterpret_cast<NSViewObj*>(tf);
    v->frame = {{0, 0}, {220, 24}};
    v->subviews = new std::vector<ObjcId>();
    v->title = new std::string(nsstring_to_utf8(text));
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    v->widget = new QLabel(QString::fromStdString(*v->title));
#endif
    return tf;
}

void nstextfield_setStringValue(ObjcId self, ObjcSel, ObjcId text) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    const std::string s = nsstring_to_utf8(text);
    if (!v->title) v->title = new std::string();
    *v->title = s;
#ifdef FINN_DARWIN_QT6
    if (auto* le = qobject_cast<QLineEdit*>(v->widget)) {
        le->setText(QString::fromStdString(s));
    } else if (auto* lbl = qobject_cast<QLabel*>(v->widget)) {
        lbl->setText(QString::fromStdString(s));
    }
#endif
}

ObjcId nstextfield_stringValue(ObjcId self, ObjcSel) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
#ifdef FINN_DARWIN_QT6
    if (auto* le = qobject_cast<QLineEdit*>(v->widget)) {
        return nsstring_from_utf8(le->text().toStdString());
    }
    if (auto* lbl = qobject_cast<QLabel*>(v->widget)) {
        return nsstring_from_utf8(lbl->text().toStdString());
    }
#endif
    return nsstring_from_utf8(v->title ? *v->title : "");
}

void nstextfield_setEditable(ObjcId self, ObjcSel, int editable) {
#ifdef FINN_DARWIN_QT6
    auto* v = reinterpret_cast<NSViewObj*>(self);
    if (auto* le = qobject_cast<QLineEdit*>(v->widget)) {
        le->setReadOnly(editable == 0);
    }
#else
    (void)self;
    (void)editable;
#endif
}

// --- NSWindow ---
ObjcId nswindow_init(ObjcId self, ObjcSel, DarwinRect rect, std::uintptr_t,
                     std::uintptr_t, int) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    w->frame = rect;
    w->title = new std::string();
    ObjcId content = finn_objc_alloc_object(g_cls_nsview, sizeof(NSViewObj));
    nsview_initWithFrame(content, nullptr, {{0, 0}, rect.size});
    w->content_view = content;
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    w->window = new QMainWindow();
    w->window->resize(std::max(120, static_cast<int>(rect.size.width)),
                      std::max(80, static_cast<int>(rect.size.height)));
    auto* cv = reinterpret_cast<NSViewObj*>(content);
    if (cv->widget) w->window->setCentralWidget(cv->widget);
#endif
    return self;
}

void nswindow_setTitle(ObjcId self, ObjcSel, ObjcId title) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    const std::string s = nsstring_to_utf8(title);
    if (!w->title) w->title = new std::string();
    *w->title = s;
#ifdef FINN_DARWIN_QT6
    if (w->window) w->window->setWindowTitle(QString::fromStdString(s));
#endif
}
ObjcId nswindow_title(ObjcId self, ObjcSel) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    return nsstring_from_utf8(w->title ? *w->title : "");
}
ObjcId nswindow_contentView(ObjcId self, ObjcSel) {
    return reinterpret_cast<NSWindowObj*>(self)->content_view;
}
void nswindow_setContentView(ObjcId self, ObjcSel, ObjcId view) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    w->content_view = view;
#ifdef FINN_DARWIN_QT6
    auto* v = reinterpret_cast<NSViewObj*>(view);
    if (w->window && v && v->widget) w->window->setCentralWidget(v->widget);
#endif
}
void nswindow_makeKeyAndOrderFront(ObjcId self, ObjcSel, ObjcId) {
#ifdef FINN_DARWIN_QT6
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    if (w->window) {
        w->window->show();
        w->window->raise();
        w->window->activateWindow();
    }
#else
    (void)self;
#endif
}
void nswindow_center(ObjcId, ObjcSel) {}
void nswindow_close(ObjcId self, ObjcSel) {
#ifdef FINN_DARWIN_QT6
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    if (w->window) w->window->close();
#else
    (void)self;
#endif
}

// --- NSAlert / NSColor / NSFont ---
void nsalert_setMessageText(ObjcId self, ObjcSel, ObjcId txt) {
    auto* a = reinterpret_cast<NSAlertObj*>(self);
    if (!a->message) a->message = new std::string();
    *a->message = nsstring_to_utf8(txt);
}
void nsalert_setInformativeText(ObjcId self, ObjcSel, ObjcId txt) {
    auto* a = reinterpret_cast<NSAlertObj*>(self);
    if (!a->info) a->info = new std::string();
    *a->info = nsstring_to_utf8(txt);
}
std::intptr_t nsalert_runModal(ObjcId self, ObjcSel) {
    auto* a = reinterpret_cast<NSAlertObj*>(self);
    const std::string msg = a->message ? *a->message : "";
    const std::string info = a->info ? *a->info : "";
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    QMessageBox box;
    box.setText(QString::fromStdString(msg));
    if (!info.empty()) box.setInformativeText(QString::fromStdString(info));
    box.exec();
#else
    std::fprintf(stderr, "NSAlert: %s — %s\n", msg.c_str(), info.c_str());
#endif
    return 1000;  // NSAlertFirstButtonReturn
}

ObjcId nscolor_rgba(ObjcId, ObjcSel, double r, double g, double b, double a) {
    auto* c = reinterpret_cast<NSColorObj*>(
        finn_objc_alloc_object(g_cls_nscolor, sizeof(NSColorObj)));
    c->r = r; c->g = g; c->b = b; c->a = a;
    return reinterpret_cast<ObjcId>(c);
}
ObjcId nscolor_white(ObjcId s, ObjcSel sel) { return nscolor_rgba(s, sel, 1, 1, 1, 1); }
ObjcId nscolor_black(ObjcId s, ObjcSel sel) { return nscolor_rgba(s, sel, 0, 0, 0, 1); }
ObjcId nscolor_clear(ObjcId s, ObjcSel sel) { return nscolor_rgba(s, sel, 0, 0, 0, 0); }

ObjcId nsfont_system(ObjcId, ObjcSel, double size) {
    auto* f = reinterpret_cast<NSFontObj*>(
        finn_objc_alloc_object(g_cls_nsfont, sizeof(NSFontObj)));
    f->family = new std::string("Fixel Display");
    f->size = size;
    f->bold = false;
    return reinterpret_cast<ObjcId>(f);
}

}  // namespace

bool appkit_qt_backend_available() {
#ifdef FINN_DARWIN_QT6
    return true;
#else
    return false;
#endif
}

void init_appkit(int argc, char** argv) {
    g_qt_argc = argc;
    g_qt_argv = argv;
    if (g_cls_nsapp) return;

    ObjcClass* nsobj = finn_objc_getClass("NSObject");

    g_cls_nsapp = finn_objc_allocateClassPair(nsobj, "NSApplication", sizeof(NSAppObj));
    finn_class_addClassMethod(g_cls_nsapp, finn_sel_registerName("sharedApplication"), to_imp(&nsapp_shared));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("setDelegate:"), to_imp(&nsapp_setDelegate));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("delegate"), to_imp(&nsapp_delegate));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("setActivationPolicy:"), to_imp(&nsapp_setActivationPolicy));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("activateIgnoringOtherApps:"), to_imp(&nsapp_activateIgnoringOtherApps));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("setMainMenu:"), to_imp(&nsapp_setMainMenu));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("run"), to_imp(&nsapp_run));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("terminate:"), to_imp(&nsapp_terminate));
    finn_objc_registerClassPair(g_cls_nsapp);

    g_cls_nsview = finn_objc_allocateClassPair(nsobj, "NSView", sizeof(NSViewObj));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("initWithFrame:"), to_imp(&nsview_initWithFrame));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("addSubview:"), to_imp(&nsview_addSubview));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("setFrame:"), to_imp(&nsview_setFrame));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("frame"), to_imp(&nsview_frame));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("setHidden:"), to_imp(&nsview_setHidden));
    finn_objc_registerClassPair(g_cls_nsview);

    g_cls_nscontrol = finn_objc_allocateClassPair(g_cls_nsview, "NSControl");
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("setTarget:"), to_imp(&nscontrol_setTarget));
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("setAction:"), to_imp(&nscontrol_setAction));
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("setEnabled:"), to_imp(&nscontrol_setEnabled));
    finn_objc_registerClassPair(g_cls_nscontrol);

    g_cls_nsbutton = finn_objc_allocateClassPair(g_cls_nscontrol, "NSButton");
    finn_class_addClassMethod(g_cls_nsbutton, finn_sel_registerName("buttonWithTitle:target:action:"), to_imp(&nsbutton_buttonWithTitleTargetAction));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("initWithFrame:"), to_imp(&nsbutton_initWithFrame));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("setTitle:"), to_imp(&nsbutton_setTitle));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("title"), to_imp(&nsbutton_title));
    finn_objc_registerClassPair(g_cls_nsbutton);

    g_cls_nstextfield = finn_objc_allocateClassPair(g_cls_nscontrol, "NSTextField");
    finn_class_addClassMethod(g_cls_nstextfield, finn_sel_registerName("labelWithString:"), to_imp(&nstextfield_labelWithString));
    finn_class_addClassMethod(g_cls_nstextfield, finn_sel_registerName("textFieldWithString:"), to_imp(&nstextfield_labelWithString));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("initWithFrame:"), to_imp(&nstextfield_initWithFrame));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("setStringValue:"), to_imp(&nstextfield_setStringValue));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("stringValue"), to_imp(&nstextfield_stringValue));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("setEditable:"), to_imp(&nstextfield_setEditable));
    finn_objc_registerClassPair(g_cls_nstextfield);

    g_cls_nstextview = finn_objc_allocateClassPair(g_cls_nsview, "NSTextView");
    finn_objc_registerClassPair(g_cls_nstextview);
    g_cls_nsscrollview = finn_objc_allocateClassPair(g_cls_nsview, "NSScrollView");
    finn_objc_registerClassPair(g_cls_nsscrollview);

    g_cls_nswindow = finn_objc_allocateClassPair(nsobj, "NSWindow", sizeof(NSWindowObj));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("initWithContentRect:styleMask:backing:defer:"), to_imp(&nswindow_init));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("setTitle:"), to_imp(&nswindow_setTitle));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("title"), to_imp(&nswindow_title));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("contentView"), to_imp(&nswindow_contentView));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("setContentView:"), to_imp(&nswindow_setContentView));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("makeKeyAndOrderFront:"), to_imp(&nswindow_makeKeyAndOrderFront));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("orderFront:"), to_imp(&nswindow_makeKeyAndOrderFront));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("center"), to_imp(&nswindow_center));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("close"), to_imp(&nswindow_close));
    finn_objc_registerClassPair(g_cls_nswindow);

    g_cls_nsmenu = finn_objc_allocateClassPair(nsobj, "NSMenu", sizeof(NSMenuObj));
    finn_objc_registerClassPair(g_cls_nsmenu);
    g_cls_nsmenuitem = finn_objc_allocateClassPair(nsobj, "NSMenuItem", sizeof(NSMenuItemObj));
    finn_objc_registerClassPair(g_cls_nsmenuitem);

    g_cls_nsalert = finn_objc_allocateClassPair(nsobj, "NSAlert", sizeof(NSAlertObj));
    finn_class_addMethod(g_cls_nsalert, finn_sel_registerName("setMessageText:"), to_imp(&nsalert_setMessageText));
    finn_class_addMethod(g_cls_nsalert, finn_sel_registerName("setInformativeText:"), to_imp(&nsalert_setInformativeText));
    finn_class_addMethod(g_cls_nsalert, finn_sel_registerName("runModal"), to_imp(&nsalert_runModal));
    finn_objc_registerClassPair(g_cls_nsalert);

    g_cls_nscolor = finn_objc_allocateClassPair(nsobj, "NSColor", sizeof(NSColorObj));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("colorWithRed:green:blue:alpha:"), to_imp(&nscolor_rgba));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("whiteColor"), to_imp(&nscolor_white));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("blackColor"), to_imp(&nscolor_black));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("clearColor"), to_imp(&nscolor_clear));
    finn_objc_registerClassPair(g_cls_nscolor);

    g_cls_nsfont = finn_objc_allocateClassPair(nsobj, "NSFont", sizeof(NSFontObj));
    finn_class_addClassMethod(g_cls_nsfont, finn_sel_registerName("systemFontOfSize:"), to_imp(&nsfont_system));
    finn_class_addClassMethod(g_cls_nsfont, finn_sel_registerName("boldSystemFontOfSize:"), to_imp(&nsfont_system));
    finn_objc_registerClassPair(g_cls_nsfont);

    g_cls_nsimage = finn_objc_allocateClassPair(nsobj, "NSImage");
    finn_objc_registerClassPair(g_cls_nsimage);
    g_cls_nstimer = finn_objc_allocateClassPair(nsobj, "NSTimer");
    finn_objc_registerClassPair(g_cls_nstimer);
}

void register_appkit_symbols(std::unordered_map<std::string, void*>* table) {
    (*table)["_NSApplicationMain"] = to_ptr(&shim_NSApplicationMain);
    (*table)["_NSApp"] = &g_shared_app;
}

}  // namespace finn::darwin
