#include "darwin/appkit_bridge.hpp"

#include "compat/desktop_entry.hpp"
#include "darwin/foundation.hpp"
#include "darwin/objc_runtime.hpp"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifdef FINN_DARWIN_QT6
#include "compat/finn_qt_theme.hpp"
#include "darwin/foundation.hpp"
#include "darwin/plist.hpp"
#include "ui/finn/window.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QRegion>
#include <QVBoxLayout>
#include <QWindow>
#include <QFileDialog>
#include <QFont>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QString>
#include <QTimer>
#include <QWidget>
#endif

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
    ObjcId document_view;
    ObjcId layer;
    double min_val;
    double max_val;
    double cur_val;
    std::intptr_t button_state;
    std::uintptr_t button_type;
    std::uintptr_t bezel_style;
    bool editable;
    bool hidden;
    bool enabled;
    bool has_vscroller;
    bool has_hscroller;
    bool indeterminate;
    bool animating;
    bool wants_layer;
#ifdef FINN_DARWIN_QT6
    QWidget* widget;
#else
    void* widget;
#endif
};

struct NSWindowObj {
    ObjcObject base;
    DarwinRect frame;
    DarwinSize min_size;
    std::string* title;
    ObjcId content_view;
    ObjcId delegate;
    bool visible;
    bool close_notified;
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
    std::string* key_equiv;
    ObjcId target;
    ObjcSel action;
    ObjcId submenu;
    bool enabled;
    bool is_separator;
#ifdef FINN_DARWIN_QT6
    QAction* qaction;
#else
    void* qaction;
#endif
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

struct NSImageObj {
    ObjcObject base;
    std::string* path;
    DarwinSize size;
    bool valid;
};

struct NSTimerObj {
    ObjcObject base;
    double interval;
    ObjcId target;
    ObjcSel selector;
    ObjcId user_info;
    bool repeats;
    bool valid;
#ifdef FINN_DARWIN_QT6
    QTimer* qtimer;
#else
    void* qtimer;
#endif
};

struct NSSavePanelObj {
    ObjcObject base;
    std::string* title;
    std::string* default_name;
    std::string* directory;
    std::string* selected_path;
    bool can_choose_files;
    bool can_choose_dirs;
    bool allows_multiple;
};

struct NSPasteboardObj {
    ObjcObject base;
    std::map<std::string, std::string>* entries;
    std::intptr_t change_count;
};

ObjcClass* g_cls_nsapp = nullptr;
ObjcClass* g_cls_nswindow = nullptr;
ObjcClass* g_cls_nsview = nullptr;
ObjcClass* g_cls_nscontrol = nullptr;
ObjcClass* g_cls_nsbutton = nullptr;
ObjcClass* g_cls_nstextfield = nullptr;
ObjcClass* g_cls_nstextview = nullptr;
ObjcClass* g_cls_nsscrollview = nullptr;
ObjcClass* g_cls_nsslider = nullptr;
ObjcClass* g_cls_nsprogress = nullptr;
ObjcClass* g_cls_nsmenu = nullptr;
ObjcClass* g_cls_nsmenuitem = nullptr;
ObjcClass* g_cls_nsalert = nullptr;
ObjcClass* g_cls_nssavepanel = nullptr;
ObjcClass* g_cls_nsopenpanel = nullptr;
ObjcClass* g_cls_nspasteboard = nullptr;
ObjcClass* g_cls_nsworkspace = nullptr;
ObjcClass* g_cls_nscolor = nullptr;
ObjcClass* g_cls_nsfont = nullptr;
ObjcClass* g_cls_nsimage = nullptr;
ObjcClass* g_cls_nstimer = nullptr;

ObjcId g_shared_app = nullptr;
ObjcId g_general_pasteboard = nullptr;
ObjcId g_shared_workspace = nullptr;
ObjcId g_pasteboard_type_string = nullptr;

int g_qt_argc = 0;
char** g_qt_argv = nullptr;

#ifdef FINN_DARWIN_QT6
QApplication* g_qapp = nullptr;

QApplication* ensure_qapp() {
    if (!g_qapp) {
        if (!QApplication::instance()) {
            g_qapp = new QApplication(g_qt_argc, g_qt_argv);
            // A guest macOS app must look like a native FinnOS app: same live
            // palette, font, Finn widget style and FinnOS window surfaces.
            finn::compat::apply_finn_qt_theme(g_qapp);
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

class WindowCloseFilter : public QObject {
public:
    explicit WindowCloseFilter(NSWindowObj* win, QObject* parent = nullptr)
        : QObject(parent), win_(win) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Close && win_ && !win_->close_notified) {
            win_->visible = false;
            win_->close_notified = true;
            if (win_->delegate &&
                finn_objc_respondsToSelector(win_->delegate,
                                             finn_sel_registerName("windowWillClose:"))) {
                objc_send_name<void>(win_->delegate, "windowWillClose:", nullptr);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    NSWindowObj* win_;
};

// A QMainWindow is kept as the NSWindow host because the AppKit bridge owns a
// real QMenuBar. It is given the same frameless rounded chrome as a native
// FinnWindow: the system title bar is removed, a FinnOS title/icon/control
// pill is drawn in its place, and the surface is masked to the token radius.
class FinnGuestChrome final : public QWidget {
public:
    explicit FinnGuestChrome(QMainWindow* host, QWidget* parent = nullptr)
        : QWidget(parent), host_(host) {
        setObjectName(QStringLiteral("finn_guest_chrome"));
        setFixedHeight(40);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(14, 0, 8, 0);
        row->setSpacing(8);

        icon_ = new QLabel(this);
        icon_->setObjectName(QStringLiteral("finn_guest_icon"));
        const QIcon app_icon =
            finn::ui::FinnThemeManager::instance().icon(QStringLiteral("darwin"));
        if (!app_icon.isNull()) icon_->setPixmap(app_icon.pixmap(20, 20));
        row->addWidget(icon_);

        title_ = new QLabel(this);
        title_->setObjectName(QStringLiteral("finn_guest_title"));
        title_->setText(host_ ? host_->windowTitle() : QString());
        row->addWidget(title_);

        // The AppKit bridge owns a real QMenuBar. Give the chrome its own bar
        // and hide QMainWindow's, so there is a single top strip as in a
        // FinnWindow. attach_main_menu_to_window() finds this bar by name.
        auto* bar = new QMenuBar(this);
        bar->setObjectName(QStringLiteral("finn_guest_menubar"));
        row->addWidget(bar);
        row->addStretch(1);

        // Use the exact FinnOS window control pill from libfinnui.
        controls_ = new finn::ui::FinnWindowControls(this);
        row->addWidget(controls_);
        connect(controls_, &finn::ui::FinnWindowControls::minimizeRequested,
                host_, &QWidget::showMinimized);
        connect(controls_, &finn::ui::FinnWindowControls::maximizeRequested,
                this, [this] {
                    if (!host_) return;
                    if (host_->isMaximized()) host_->showNormal();
                    else host_->showMaximized();
                });
        connect(controls_, &finn::ui::FinnWindowControls::closeRequested,
                host_, &QWidget::close);
    }

    void set_title(const QString& text) {
        if (title_) title_->setText(text);
    }

protected:
    void mousePressEvent(QMouseEvent*) override {
        if (host_ && host_->window() && host_->window()->windowHandle()) {
            host_->window()->windowHandle()->startSystemMove();
        }
    }

private:
    QMainWindow* host_ = nullptr;
    QLabel* icon_ = nullptr;
    QLabel* title_ = nullptr;
    finn::ui::FinnWindowControls* controls_ = nullptr;
};

// Applies FinnWindow's measured 20px rounded mask to the frameless host.
class RoundedGuestFilter final : public QObject {
public:
    explicit RoundedGuestFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            if (auto* w = qobject_cast<QWidget*>(watched); w && !w->isMaximized()) {
                const QRect r = w->rect();
                constexpr int radius = 20;
                if (!r.isEmpty()) {
                    const int d = 2 * radius;
                    QRegion mask(QRect(r.left() + radius, r.top(), r.width() - d, r.height()));
                    mask |= QRegion(QRect(r.left(), r.top() + radius, r.width(), r.height() - d));
                    mask |= QRegion(QRect(r.left(), r.top(), d, d), QRegion::Ellipse);
                    mask |= QRegion(QRect(r.right() - d + 1, r.top(), d, d), QRegion::Ellipse);
                    mask |= QRegion(QRect(r.left(), r.bottom() - d + 1, d, d), QRegion::Ellipse);
                    mask |= QRegion(QRect(r.right() - d + 1, r.bottom() - d + 1, d, d),
                                    QRegion::Ellipse);
                    w->setMask(mask);
                }
            } else if (auto* w = qobject_cast<QWidget*>(watched); w && w->isMaximized()) {
                w->clearMask();
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

void populate_qt_menu(QMenu* qmenu, NSMenuObj* nsmenu) {
    if (!qmenu || !nsmenu || !nsmenu->items) return;
    qmenu->clear();
    for (ObjcId item_id : *nsmenu->items) {
        auto* item = reinterpret_cast<NSMenuItemObj*>(item_id);
        if (!item) continue;
        if (item->is_separator) {
            qmenu->addSeparator();
            continue;
        }
        if (item->submenu) {
            auto* sub = reinterpret_cast<NSMenuObj*>(item->submenu);
            const std::string sub_title =
                (sub->title && !sub->title->empty())
                    ? *sub->title
                    : (item->title ? *item->title : "");
            QMenu* qsub = qmenu->addMenu(QString::fromStdString(sub_title));
            sub->menu = qsub;
            populate_qt_menu(qsub, sub);
        } else {
            const std::string label = item->title ? *item->title : "";
            QAction* act = qmenu->addAction(QString::fromStdString(label));
            act->setEnabled(item->enabled);
            if (item->key_equiv && !item->key_equiv->empty()) {
                act->setShortcut(QKeySequence(QString::fromStdString("Ctrl+" + *item->key_equiv)));
            }
            item->qaction = act;
            QObject::connect(act, &QAction::triggered, [item_id]() {
                auto* mi = reinterpret_cast<NSMenuItemObj*>(item_id);
                if (!mi || !mi->action) return;
                ObjcId tgt = mi->target;
                if (!tgt && g_shared_app) {
                    auto* app = reinterpret_cast<NSAppObj*>(g_shared_app);
                    if (app->delegate && finn_objc_respondsToSelector(app->delegate, mi->action)) {
                        tgt = app->delegate;
                    } else if (finn_objc_respondsToSelector(g_shared_app, mi->action)) {
                        tgt = g_shared_app;
                    }
                }
                if (tgt) {
                    objc_send<void>(tgt, mi->action, item_id);
                }
            });
        }
    }
}

void attach_main_menu_to_window(QMainWindow* qwin) {
    if (!qwin || !g_shared_app) return;
    auto* app = reinterpret_cast<NSAppObj*>(g_shared_app);
    if (!app->main_menu) return;
    auto* top_menu = reinterpret_cast<NSMenuObj*>(app->main_menu);
    if (!top_menu->items) return;
    QMenuBar* bar = qwin->findChild<QMenuBar*>(QStringLiteral("finn_guest_menubar"));
    if (!bar) bar = qwin->menuBar();
    bar->clear();
    for (ObjcId item_id : *top_menu->items) {
        auto* item = reinterpret_cast<NSMenuItemObj*>(item_id);
        if (!item) continue;
        if (item->submenu) {
            auto* sub = reinterpret_cast<NSMenuObj*>(item->submenu);
            const std::string title =
                (sub->title && !sub->title->empty())
                    ? *sub->title
                    : (item->title ? *item->title : "Menu");
            QMenu* qm = bar->addMenu(QString::fromStdString(title));
            sub->menu = qm;
            populate_qt_menu(qm, sub);
        } else if (item->title && !item->title->empty()) {
            QAction* act = bar->addAction(QString::fromStdString(*item->title));
            act->setEnabled(item->enabled);
            item->qaction = act;
            QObject::connect(act, &QAction::triggered, [item_id]() {
                auto* mi = reinterpret_cast<NSMenuItemObj*>(item_id);
                if (mi && mi->target && mi->action) {
                    objc_send<void>(mi->target, mi->action, item_id);
                }
            });
        }
    }
}
#endif

// --- Destructors ---
void nsview_dtor(ObjcId obj) {
    auto* v = reinterpret_cast<NSViewObj*>(obj);
    if (v->subviews) {
        for (ObjcId sub : *v->subviews) finn_objc_release(sub);
        delete v->subviews;
        v->subviews = nullptr;
    }
    delete v->title;
    v->title = nullptr;
    finn_objc_release(v->document_view);
    v->document_view = nullptr;
    finn_objc_release(v->layer);
    v->layer = nullptr;
}

void nswindow_dtor(ObjcId obj) {
    auto* w = reinterpret_cast<NSWindowObj*>(obj);
    delete w->title;
    w->title = nullptr;
    finn_objc_release(w->content_view);
    w->content_view = nullptr;
}

void nsmenu_dtor(ObjcId obj) {
    auto* m = reinterpret_cast<NSMenuObj*>(obj);
    delete m->title;
    m->title = nullptr;
    if (m->items) {
        for (ObjcId item : *m->items) finn_objc_release(item);
        delete m->items;
        m->items = nullptr;
    }
}

void nsmenuitem_dtor(ObjcId obj) {
    auto* mi = reinterpret_cast<NSMenuItemObj*>(obj);
    delete mi->title;
    delete mi->key_equiv;
    mi->title = nullptr;
    mi->key_equiv = nullptr;
    finn_objc_release(mi->submenu);
    mi->submenu = nullptr;
}

void nsalert_dtor(ObjcId obj) {
    auto* a = reinterpret_cast<NSAlertObj*>(obj);
    delete a->message;
    delete a->info;
    a->message = nullptr;
    a->info = nullptr;
}

void nsfont_dtor(ObjcId obj) {
    auto* f = reinterpret_cast<NSFontObj*>(obj);
    delete f->family;
    f->family = nullptr;
}

void nsimage_dtor(ObjcId obj) {
    auto* img = reinterpret_cast<NSImageObj*>(obj);
    delete img->path;
    img->path = nullptr;
}

void nstimer_dtor(ObjcId obj) {
    auto* t = reinterpret_cast<NSTimerObj*>(obj);
#ifdef FINN_DARWIN_QT6
    if (t->qtimer) {
        t->qtimer->stop();
        delete t->qtimer;
        t->qtimer = nullptr;
    }
#endif
    finn_objc_release(t->user_info);
    t->user_info = nullptr;
}

void nssavepanel_dtor(ObjcId obj) {
    auto* p = reinterpret_cast<NSSavePanelObj*>(obj);
    delete p->title;
    delete p->default_name;
    delete p->directory;
    delete p->selected_path;
    p->title = nullptr;
    p->default_name = nullptr;
    p->directory = nullptr;
    p->selected_path = nullptr;
}

void nspasteboard_dtor(ObjcId obj) {
    auto* pb = reinterpret_cast<NSPasteboardObj*>(obj);
    delete pb->entries;
    pb->entries = nullptr;
}

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
signed char nsapp_setActivationPolicy(ObjcId, ObjcSel, std::intptr_t) { return 1; }
void nsapp_activateIgnoringOtherApps(ObjcId, ObjcSel, signed char) {}
void nsapp_setMainMenu(ObjcId self, ObjcSel, ObjcId menu) {
    auto* app = reinterpret_cast<NSAppObj*>(self);
    if (app->main_menu == menu) return;
    finn_objc_retain(menu);
    finn_objc_release(app->main_menu);
    app->main_menu = menu;
#ifdef FINN_DARWIN_QT6
    if (app->windows) {
        for (ObjcId wobj : *app->windows) {
            auto* w = reinterpret_cast<NSWindowObj*>(wobj);
            if (w && w->window) attach_main_menu_to_window(w->window);
        }
    }
#endif
}
ObjcId nsapp_mainMenu(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSAppObj*>(self)->main_menu;
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
void init_view_defaults(NSViewObj* v, DarwinRect frame) {
    v->frame = frame;
    if (!v->subviews) v->subviews = new std::vector<ObjcId>();
    if (!v->title) v->title = new std::string();
    v->min_val = 0.0;
    v->max_val = 100.0;
    v->cur_val = 0.0;
    v->editable = true;
    v->hidden = false;
    v->enabled = true;
    v->has_vscroller = false;
    v->has_hscroller = false;
    v->indeterminate = false;
    v->animating = false;
    v->wants_layer = false;
}

ObjcId nsview_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    init_view_defaults(v, frame);
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
        if (!child->hidden) child->widget->show();
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

void nsview_setHidden(ObjcId self, ObjcSel, signed char hidden) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->hidden = (hidden != 0);
#ifdef FINN_DARWIN_QT6
    if (v->widget) v->widget->setVisible(!v->hidden);
#endif
}

signed char nsview_isHidden(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->hidden ? 1 : 0;
}

void nsview_setWantsLayer(ObjcId self, ObjcSel, signed char wants) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->wants_layer = (wants != 0);
    if (v->wants_layer && !v->layer) {
        if (ObjcClass* cls = finn_objc_getClass("CALayer")) {
            v->layer = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(cls), "layer");
        }
    }
}

signed char nsview_wantsLayer(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->wants_layer ? 1 : 0;
}

ObjcId nsview_layer(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->layer;
}

void nsview_setLayer(ObjcId self, ObjcSel, ObjcId layer) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    if (v->layer == layer) return;
    finn_objc_retain(layer);
    finn_objc_release(v->layer);
    v->layer = layer;
}

// --- NSControl ---
void nscontrol_setTarget(ObjcId self, ObjcSel, ObjcId target) {
    reinterpret_cast<NSViewObj*>(self)->target = target;
}
ObjcId nscontrol_target(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->target;
}
void nscontrol_setAction(ObjcId self, ObjcSel, ObjcSel action) {
    reinterpret_cast<NSViewObj*>(self)->action = action;
}
ObjcSel nscontrol_action(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->action;
}
void nscontrol_setEnabled(ObjcId self, ObjcSel, signed char enabled) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->enabled = (enabled != 0);
#ifdef FINN_DARWIN_QT6
    if (v->widget) v->widget->setEnabled(v->enabled);
#endif
}
signed char nscontrol_isEnabled(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->enabled ? 1 : 0;
}

// --- NSButton ---
ObjcId nsbutton_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    init_view_defaults(v, frame);
    v->button_state = 0;
    v->button_type = 0;
    v->bezel_style = 1;
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    auto* btn = new QPushButton();
    v->widget = btn;
    QObject::connect(btn, &QPushButton::clicked, [self](bool checked) {
        auto* bv = reinterpret_cast<NSViewObj*>(self);
        if (auto* pb = qobject_cast<QPushButton*>(bv->widget)) {
            if (pb->isCheckable()) bv->button_state = checked ? 1 : 0;
        }
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

void nsbutton_setButtonType(ObjcId self, ObjcSel, std::uintptr_t btype) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->button_type = btype;
#ifdef FINN_DARWIN_QT6
    if (auto* btn = qobject_cast<QPushButton*>(v->widget)) {
        // PushOnPushOff=1, Toggle=2, Switch=3, Radio=4
        const bool checkable = (btype >= 1 && btype <= 4);
        btn->setCheckable(checkable);
        if (checkable) btn->setChecked(v->button_state != 0);
    }
#endif
}

std::uintptr_t nsbutton_buttonType(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->button_type;
}

void nsbutton_setState(ObjcId self, ObjcSel, std::intptr_t st) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->button_state = st;
#ifdef FINN_DARWIN_QT6
    if (auto* btn = qobject_cast<QPushButton*>(v->widget)) {
        if (btn->isCheckable()) btn->setChecked(st != 0);
    }
#endif
}

std::intptr_t nsbutton_state(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->button_state;
}

void nsbutton_setBezelStyle(ObjcId self, ObjcSel, std::uintptr_t style) {
    reinterpret_cast<NSViewObj*>(self)->bezel_style = style;
}

std::uintptr_t nsbutton_bezelStyle(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->bezel_style;
}

void nsbutton_performClick(ObjcId self, ObjcSel, ObjcId) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    if (!v->enabled) return;
    if (v->button_type >= 1 && v->button_type <= 4) {
        v->button_state = (v->button_state == 0) ? 1 : 0;
#ifdef FINN_DARWIN_QT6
        if (auto* btn = qobject_cast<QPushButton*>(v->widget)) {
            btn->setChecked(v->button_state != 0);
        }
#endif
    }
    if (v->target && v->action) {
        objc_send<void>(v->target, v->action, self);
    }
}

// --- NSTextField ---
ObjcId nstextfield_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    init_view_defaults(v, frame);
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    v->widget = new QLineEdit();
#endif
    return self;
}

ObjcId nstextfield_labelWithString(ObjcId, ObjcSel, ObjcId text) {
    ObjcId tf = finn_objc_alloc_object(g_cls_nstextfield, sizeof(NSViewObj));
    auto* v = reinterpret_cast<NSViewObj*>(tf);
    init_view_defaults(v, {{0, 0}, {220, 24}});
    *v->title = nsstring_to_utf8(text);
    v->editable = false;
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

void nstextfield_setEditable(ObjcId self, ObjcSel, signed char editable) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->editable = (editable != 0);
#ifdef FINN_DARWIN_QT6
    if (auto* le = qobject_cast<QLineEdit*>(v->widget)) {
        le->setReadOnly(!v->editable);
    }
#endif
}

signed char nstextfield_isEditable(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->editable ? 1 : 0;
}

// --- NSTextView -> QPlainTextEdit ---
ObjcId nstextview_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    init_view_defaults(v, frame);
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    auto* edit = new QPlainTextEdit();
    edit->resize(std::max(1, static_cast<int>(frame.size.width)),
                 std::max(1, static_cast<int>(frame.size.height)));
    v->widget = edit;
#endif
    return self;
}

void nstextview_setString(ObjcId self, ObjcSel, ObjcId text) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    const std::string s = nsstring_to_utf8(text);
    if (!v->title) v->title = new std::string();
    *v->title = s;
#ifdef FINN_DARWIN_QT6
    if (auto* edit = qobject_cast<QPlainTextEdit*>(v->widget)) {
        edit->setPlainText(QString::fromStdString(s));
    }
#endif
}

ObjcId nstextview_string(ObjcId self, ObjcSel) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
#ifdef FINN_DARWIN_QT6
    if (auto* edit = qobject_cast<QPlainTextEdit*>(v->widget)) {
        return nsstring_from_utf8(edit->toPlainText().toStdString());
    }
#endif
    return nsstring_from_utf8(v->title ? *v->title : "");
}

void nstextview_setEditable(ObjcId self, ObjcSel, signed char editable) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->editable = (editable != 0);
#ifdef FINN_DARWIN_QT6
    if (auto* edit = qobject_cast<QPlainTextEdit*>(v->widget)) {
        edit->setReadOnly(!v->editable);
    }
#endif
}

// --- NSScrollView -> QScrollArea ---
ObjcId nsscrollview_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    init_view_defaults(v, frame);
    v->has_vscroller = true;
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    auto* area = new QScrollArea();
    area->setWidgetResizable(true);
    area->resize(std::max(1, static_cast<int>(frame.size.width)),
                 std::max(1, static_cast<int>(frame.size.height)));
    v->widget = area;
#endif
    return self;
}

void nsscrollview_setDocumentView(ObjcId self, ObjcSel, ObjcId doc) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    if (v->document_view != doc) {
        finn_objc_retain(doc);
        finn_objc_release(v->document_view);
        v->document_view = doc;
    }
#ifdef FINN_DARWIN_QT6
    if (auto* area = qobject_cast<QScrollArea*>(v->widget)) {
        auto* dv = reinterpret_cast<NSViewObj*>(doc);
        if (dv && dv->widget) area->setWidget(dv->widget);
    }
#endif
}

ObjcId nsscrollview_documentView(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->document_view;
}

void nsscrollview_setHasVScroller(ObjcId self, ObjcSel, signed char flag) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->has_vscroller = (flag != 0);
#ifdef FINN_DARWIN_QT6
    if (auto* area = qobject_cast<QScrollArea*>(v->widget)) {
        area->setVerticalScrollBarPolicy(v->has_vscroller ? Qt::ScrollBarAsNeeded
                                                          : Qt::ScrollBarAlwaysOff);
    }
#endif
}

signed char nsscrollview_hasVScroller(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->has_vscroller ? 1 : 0;
}

void nsscrollview_setHasHScroller(ObjcId self, ObjcSel, signed char flag) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->has_hscroller = (flag != 0);
#ifdef FINN_DARWIN_QT6
    if (auto* area = qobject_cast<QScrollArea*>(v->widget)) {
        area->setHorizontalScrollBarPolicy(v->has_hscroller ? Qt::ScrollBarAsNeeded
                                                            : Qt::ScrollBarAlwaysOff);
    }
#endif
}

// --- NSSlider -> QSlider ---
#ifdef FINN_DARWIN_QT6
void sync_qt_slider(NSViewObj* v) {
    if (auto* sl = qobject_cast<QSlider*>(v->widget)) {
        sl->blockSignals(true);
        sl->setRange(0, 1000);
        const double span = v->max_val - v->min_val;
        const double norm = (span > 0.0) ? std::clamp((v->cur_val - v->min_val) / span, 0.0, 1.0) : 0.0;
        sl->setValue(static_cast<int>(std::lround(norm * 1000.0)));
        sl->blockSignals(false);
    }
}
#endif

ObjcId nsslider_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    init_view_defaults(v, frame);
    v->min_val = 0.0;
    v->max_val = 100.0;
    v->cur_val = 0.0;
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    auto* sl = new QSlider(Qt::Horizontal);
    sl->setRange(0, 1000);
    v->widget = sl;
    QObject::connect(sl, &QSlider::valueChanged, [self](int pos) {
        auto* sv = reinterpret_cast<NSViewObj*>(self);
        sv->cur_val = sv->min_val + (sv->max_val - sv->min_val) * (static_cast<double>(pos) / 1000.0);
        if (sv->target && sv->action) {
            objc_send<void>(sv->target, sv->action, self);
        }
    });
#endif
    return self;
}

ObjcId nsslider_factory(ObjcId, ObjcSel, double val, double min_v, double max_v,
                        ObjcId target, ObjcSel action) {
    ObjcId sl = finn_objc_alloc_object(g_cls_nsslider, sizeof(NSViewObj));
    nsslider_initWithFrame(sl, nullptr, {{0, 0}, {160, 24}});
    auto* v = reinterpret_cast<NSViewObj*>(sl);
    v->min_val = min_v;
    v->max_val = max_v;
    v->cur_val = std::clamp(val, min_v, max_v);
    v->target = target;
    v->action = action;
#ifdef FINN_DARWIN_QT6
    sync_qt_slider(v);
#endif
    return sl;
}

void nsslider_setMinValue(ObjcId self, ObjcSel, double val) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->min_val = val;
#ifdef FINN_DARWIN_QT6
    sync_qt_slider(v);
#endif
}
double nsslider_minValue(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->min_val;
}
void nsslider_setMaxValue(ObjcId self, ObjcSel, double val) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->max_val = val;
#ifdef FINN_DARWIN_QT6
    sync_qt_slider(v);
#endif
}
double nsslider_maxValue(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->max_val;
}
void nsslider_setDoubleValue(ObjcId self, ObjcSel, double val) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->cur_val = (v->max_val >= v->min_val) ? std::clamp(val, v->min_val, v->max_val) : val;
#ifdef FINN_DARWIN_QT6
    sync_qt_slider(v);
#endif
}
double nsslider_doubleValue(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->cur_val;
}
void nsslider_setFloatValue(ObjcId self, ObjcSel sel, float val) {
    nsslider_setDoubleValue(self, sel, static_cast<double>(val));
}
float nsslider_floatValue(ObjcId self, ObjcSel) {
    return static_cast<float>(reinterpret_cast<const NSViewObj*>(self)->cur_val);
}
void nsslider_setIntValue(ObjcId self, ObjcSel sel, int val) {
    nsslider_setDoubleValue(self, sel, static_cast<double>(val));
}
int nsslider_intValue(ObjcId self, ObjcSel) {
    return static_cast<int>(std::lround(reinterpret_cast<const NSViewObj*>(self)->cur_val));
}

// --- NSProgressIndicator -> QProgressBar ---
#ifdef FINN_DARWIN_QT6
void sync_qt_progress(NSViewObj* v) {
    if (auto* pb = qobject_cast<QProgressBar*>(v->widget)) {
        if (v->indeterminate && v->animating) {
            pb->setRange(0, 0);
        } else {
            pb->setRange(0, 1000);
            const double span = v->max_val - v->min_val;
            const double norm = (span > 0.0) ? std::clamp((v->cur_val - v->min_val) / span, 0.0, 1.0) : 0.0;
            pb->setValue(static_cast<int>(std::lround(norm * 1000.0)));
        }
    }
}
#endif

ObjcId nsprogress_initWithFrame(ObjcId self, ObjcSel, DarwinRect frame) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    init_view_defaults(v, frame);
    v->min_val = 0.0;
    v->max_val = 100.0;
    v->cur_val = 0.0;
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    auto* pb = new QProgressBar();
    pb->setTextVisible(false);
    pb->setRange(0, 1000);
    v->widget = pb;
#endif
    return self;
}

void nsprogress_setMinValue(ObjcId self, ObjcSel, double val) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->min_val = val;
#ifdef FINN_DARWIN_QT6
    sync_qt_progress(v);
#endif
}
void nsprogress_setMaxValue(ObjcId self, ObjcSel, double val) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->max_val = val;
#ifdef FINN_DARWIN_QT6
    sync_qt_progress(v);
#endif
}
void nsprogress_setDoubleValue(ObjcId self, ObjcSel, double val) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->cur_val = val;
#ifdef FINN_DARWIN_QT6
    sync_qt_progress(v);
#endif
}
void nsprogress_setIndeterminate(ObjcId self, ObjcSel, signed char flag) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->indeterminate = (flag != 0);
#ifdef FINN_DARWIN_QT6
    sync_qt_progress(v);
#endif
}
signed char nsprogress_isIndeterminate(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSViewObj*>(self)->indeterminate ? 1 : 0;
}
void nsprogress_startAnimation(ObjcId self, ObjcSel, ObjcId) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->animating = true;
#ifdef FINN_DARWIN_QT6
    sync_qt_progress(v);
#endif
}
void nsprogress_stopAnimation(ObjcId self, ObjcSel, ObjcId) {
    auto* v = reinterpret_cast<NSViewObj*>(self);
    v->animating = false;
#ifdef FINN_DARWIN_QT6
    sync_qt_progress(v);
#endif
}

// --- NSMenu & NSMenuItem -> QMenuBar / QMenu / QAction ---
ObjcId nsmenuitem_initWithTitle(ObjcId self, ObjcSel, ObjcId title, ObjcSel action, ObjcId key_eq) {
    auto* mi = reinterpret_cast<NSMenuItemObj*>(self);
    if (!mi->title) mi->title = new std::string();
    if (!mi->key_equiv) mi->key_equiv = new std::string();
    *mi->title = nsstring_to_utf8(title);
    *mi->key_equiv = nsstring_to_utf8(key_eq);
    mi->action = action;
    mi->enabled = true;
    mi->is_separator = false;
    return self;
}

ObjcId nsmenuitem_init(ObjcId self, ObjcSel) {
    return nsmenuitem_initWithTitle(self, nullptr, nullptr, nullptr, nullptr);
}

ObjcId nsmenuitem_separatorItem(ObjcId, ObjcSel) {
    ObjcId item = finn_objc_alloc_object(g_cls_nsmenuitem, sizeof(NSMenuItemObj));
    nsmenuitem_init(item, nullptr);
    reinterpret_cast<NSMenuItemObj*>(item)->is_separator = true;
    return item;
}

void nsmenuitem_setTitle(ObjcId self, ObjcSel, ObjcId title) {
    auto* mi = reinterpret_cast<NSMenuItemObj*>(self);
    if (!mi->title) mi->title = new std::string();
    *mi->title = nsstring_to_utf8(title);
#ifdef FINN_DARWIN_QT6
    if (mi->qaction) mi->qaction->setText(QString::fromStdString(*mi->title));
#endif
}
ObjcId nsmenuitem_title(ObjcId self, ObjcSel) {
    auto* mi = reinterpret_cast<const NSMenuItemObj*>(self);
    return nsstring_from_utf8(mi->title ? *mi->title : "");
}
void nsmenuitem_setTarget(ObjcId self, ObjcSel, ObjcId target) {
    reinterpret_cast<NSMenuItemObj*>(self)->target = target;
}
ObjcId nsmenuitem_target(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSMenuItemObj*>(self)->target;
}
void nsmenuitem_setAction(ObjcId self, ObjcSel, ObjcSel action) {
    reinterpret_cast<NSMenuItemObj*>(self)->action = action;
}
ObjcSel nsmenuitem_action(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSMenuItemObj*>(self)->action;
}
void nsmenuitem_setSubmenu(ObjcId self, ObjcSel, ObjcId submenu) {
    auto* mi = reinterpret_cast<NSMenuItemObj*>(self);
    if (mi->submenu == submenu) return;
    finn_objc_retain(submenu);
    finn_objc_release(mi->submenu);
    mi->submenu = submenu;
}
ObjcId nsmenuitem_submenu(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSMenuItemObj*>(self)->submenu;
}
ObjcId nsmenuitem_keyEquivalent(ObjcId self, ObjcSel) {
    auto* mi = reinterpret_cast<const NSMenuItemObj*>(self);
    return nsstring_from_utf8(mi->key_equiv ? *mi->key_equiv : "");
}
void nsmenuitem_setEnabled(ObjcId self, ObjcSel, signed char en) {
    auto* mi = reinterpret_cast<NSMenuItemObj*>(self);
    mi->enabled = (en != 0);
#ifdef FINN_DARWIN_QT6
    if (mi->qaction) mi->qaction->setEnabled(mi->enabled);
#endif
}
signed char nsmenuitem_isEnabled(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSMenuItemObj*>(self)->enabled ? 1 : 0;
}

ObjcId nsmenu_initWithTitle(ObjcId self, ObjcSel, ObjcId title) {
    auto* m = reinterpret_cast<NSMenuObj*>(self);
    if (!m->title) m->title = new std::string();
    *m->title = nsstring_to_utf8(title);
    if (!m->items) m->items = new std::vector<ObjcId>();
    return self;
}
ObjcId nsmenu_init(ObjcId self, ObjcSel) {
    return nsmenu_initWithTitle(self, nullptr, nullptr);
}
void nsmenu_setTitle(ObjcId self, ObjcSel, ObjcId title) {
    auto* m = reinterpret_cast<NSMenuObj*>(self);
    if (!m->title) m->title = new std::string();
    *m->title = nsstring_to_utf8(title);
#ifdef FINN_DARWIN_QT6
    if (m->menu) m->menu->setTitle(QString::fromStdString(*m->title));
#endif
}
ObjcId nsmenu_title(ObjcId self, ObjcSel) {
    auto* m = reinterpret_cast<const NSMenuObj*>(self);
    return nsstring_from_utf8(m->title ? *m->title : "");
}
void nsmenu_addItem(ObjcId self, ObjcSel, ObjcId item) {
    if (!self || !item) return;
    auto* m = reinterpret_cast<NSMenuObj*>(self);
    if (!m->items) m->items = new std::vector<ObjcId>();
    m->items->push_back(finn_objc_retain(item));
#ifdef FINN_DARWIN_QT6
    if (m->menu) populate_qt_menu(m->menu, m);
#endif
}
ObjcId nsmenu_addItemWithTitleActionKey(ObjcId self, ObjcSel, ObjcId title, ObjcSel action,
                                        ObjcId key_eq) {
    ObjcId item = finn_objc_alloc_object(g_cls_nsmenuitem, sizeof(NSMenuItemObj));
    nsmenuitem_initWithTitle(item, nullptr, title, action, key_eq);
    nsmenu_addItem(self, nullptr, item);
    finn_objc_release(item);
    return item;
}
std::intptr_t nsmenu_numberOfItems(ObjcId self, ObjcSel) {
    auto* m = reinterpret_cast<const NSMenuObj*>(self);
    return (m && m->items) ? static_cast<std::intptr_t>(m->items->size()) : 0;
}
ObjcId nsmenu_itemAtIndex(ObjcId self, ObjcSel, std::intptr_t idx) {
    auto* m = reinterpret_cast<const NSMenuObj*>(self);
    if (!m || !m->items || idx < 0 || static_cast<std::size_t>(idx) >= m->items->size()) {
        return nullptr;
    }
    return (*m->items)[static_cast<std::size_t>(idx)];
}
ObjcId nsmenu_itemArray(ObjcId self, ObjcSel) {
    auto* m = reinterpret_cast<const NSMenuObj*>(self);
    ObjcClass* arr_cls = finn_objc_getClass("NSMutableArray");
    ObjcId arr = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(arr_cls), "array");
    if (m && m->items) {
        for (ObjcId item : *m->items) {
            objc_send_name<void>(arr, "addObject:", item);
        }
    }
    return finn_objc_autorelease(arr);
}

// --- NSWindow ---
ObjcId nswindow_init(ObjcId self, ObjcSel, DarwinRect rect, std::uintptr_t,
                     std::uintptr_t, signed char) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    w->frame = rect;
    w->min_size = {0.0, 0.0};
    w->title = new std::string();
    w->visible = false;
    w->close_notified = false;
    ObjcId content = finn_objc_alloc_object(g_cls_nsview, sizeof(NSViewObj));
    nsview_initWithFrame(content, nullptr, {{0, 0}, rect.size});
    w->content_view = content;
    if (g_shared_app) {
        auto* app = reinterpret_cast<NSAppObj*>(g_shared_app);
        if (app->windows) app->windows->push_back(self);
    }
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    w->window = new QMainWindow();
    w->window->setObjectName(QStringLiteral("finn_guest_root"));
    w->window->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    w->window->setStyleSheet(finn::compat::finn_guest_window_qss() + QStringLiteral("\n") +
                             finn::compat::finn_guest_chrome_qss());
    // Reserve the chrome's height so the guest content is not clipped.
    w->window->resize(std::max(120, static_cast<int>(rect.size.width)),
                      std::max(80, static_cast<int>(rect.size.height)) + 40);
    w->window->installEventFilter(new WindowCloseFilter(w, w->window));
    w->window->installEventFilter(new RoundedGuestFilter(w->window));
    auto* cv = reinterpret_cast<NSViewObj*>(content);
    auto* chrome = new FinnGuestChrome(w->window);
    if (QMenuBar* native = w->window->menuBar()) native->hide();
    attach_main_menu_to_window(w->window);
    // A guest binary often never calls setTitle:. Label the chrome with the
    // bundle's own CFBundleName so the window is identifiable either way.
    if (w->window->windowTitle().isEmpty()) {
        const std::string bundle = main_bundle_path();
        if (!bundle.empty()) {
            std::string err;
            const auto info = parse_plist_file(bundle + "/Contents/Info.plist", &err);
            if (info) {
                const std::string name = info->get_string("CFBundleDisplayName",
                                                          info->get_string("CFBundleName"));
                if (!name.empty()) chrome->set_title(QString::fromStdString(name));
            }
        }
    }
    // Chrome above content in one stack, set as the central widget: Qt lays it
    // out reliably, unlike setMenuWidget() with a frameless QMainWindow.
    auto* wrapper = new QWidget(w->window);
    wrapper->setObjectName(QStringLiteral("finn_guest_wrapper"));
    auto* stack = new QVBoxLayout(wrapper);
    stack->setObjectName(QStringLiteral("finn_guest_stack"));
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);
    stack->addWidget(chrome);
    if (cv->widget) stack->addWidget(cv->widget, 1);
    w->window->setCentralWidget(wrapper);
#endif
    return self;
}

void nswindow_setTitle(ObjcId self, ObjcSel, ObjcId title) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    const std::string s = nsstring_to_utf8(title);
    if (!w->title) w->title = new std::string();
    *w->title = s;
#ifdef FINN_DARWIN_QT6
    if (w->window) {
        w->window->setWindowTitle(QString::fromStdString(s));
        if (auto* label =
                w->window->findChild<QLabel*>(QStringLiteral("finn_guest_title"))) {
            label->setText(QString::fromStdString(s));
        }
    }
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
    if (w->content_view != view) {
        finn_objc_retain(view);
        finn_objc_release(w->content_view);
        w->content_view = view;
    }
#ifdef FINN_DARWIN_QT6
    auto* v = reinterpret_cast<NSViewObj*>(view);
    if (w->window && v && v->widget) {
        if (auto* stack = w->window->findChild<QVBoxLayout*>(
                QStringLiteral("finn_guest_stack"))) {
            while (stack->count() > 1) {
                QLayoutItem* old = stack->takeAt(stack->count() - 1);
                if (old->widget()) old->widget()->setParent(nullptr);
                delete old;
            }
            stack->addWidget(v->widget, 1);
        } else {
            w->window->setCentralWidget(v->widget);
        }
    }
#endif
}
void nswindow_setMinSize(ObjcId self, ObjcSel, DarwinSize sz) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    w->min_size = sz;
#ifdef FINN_DARWIN_QT6
    if (w->window) {
        w->window->setMinimumSize(std::max(0, static_cast<int>(std::lround(sz.width))),
                                  std::max(0, static_cast<int>(std::lround(sz.height))));
    }
#endif
}
DarwinSize nswindow_minSize(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSWindowObj*>(self)->min_size;
}
void nswindow_setDelegate(ObjcId self, ObjcSel, ObjcId del) {
    reinterpret_cast<NSWindowObj*>(self)->delegate = del;
}
ObjcId nswindow_delegate(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSWindowObj*>(self)->delegate;
}
void nswindow_makeKeyAndOrderFront(ObjcId self, ObjcSel, ObjcId) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    w->visible = true;
    w->close_notified = false;
#ifdef FINN_DARWIN_QT6
    if (w->window) {
        attach_main_menu_to_window(w->window);
        w->window->show();
        w->window->raise();
        w->window->activateWindow();
    }
#endif
}
void nswindow_orderOut(ObjcId self, ObjcSel, ObjcId) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    w->visible = false;
#ifdef FINN_DARWIN_QT6
    if (w->window) w->window->hide();
#endif
}
signed char nswindow_isVisible(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSWindowObj*>(self)->visible ? 1 : 0;
}
void nswindow_center(ObjcId, ObjcSel) {}
void nswindow_close(ObjcId self, ObjcSel) {
    auto* w = reinterpret_cast<NSWindowObj*>(self);
    w->visible = false;
#ifdef FINN_DARWIN_QT6
    if (w->window) w->window->close();
#endif
    if (!w->close_notified) {
        w->close_notified = true;
        if (w->delegate &&
            finn_objc_respondsToSelector(w->delegate,
                                         finn_sel_registerName("windowWillClose:"))) {
            objc_send_name<void>(w->delegate, "windowWillClose:", nullptr);
        }
    }
}

// --- NSOpenPanel & NSSavePanel -> QFileDialog ---
ObjcId nssavepanel_create(ObjcClass* cls) {
    auto* p = reinterpret_cast<NSSavePanelObj*>(
        finn_objc_alloc_object(cls, sizeof(NSSavePanelObj)));
    p->title = new std::string();
    p->default_name = new std::string();
    p->directory = new std::string();
    p->selected_path = new std::string();
    p->can_choose_files = true;
    p->can_choose_dirs = false;
    p->allows_multiple = false;
    return reinterpret_cast<ObjcId>(p);
}

ObjcId nssavepanel_savePanel(ObjcId, ObjcSel) {
    return nssavepanel_create(g_cls_nssavepanel);
}
ObjcId nsopenpanel_openPanel(ObjcId, ObjcSel) {
    return nssavepanel_create(g_cls_nsopenpanel);
}
void nssavepanel_setTitle(ObjcId self, ObjcSel, ObjcId title) {
    auto* p = reinterpret_cast<NSSavePanelObj*>(self);
    if (!p->title) p->title = new std::string();
    *p->title = nsstring_to_utf8(title);
}
void nssavepanel_setNameField(ObjcId self, ObjcSel, ObjcId name) {
    auto* p = reinterpret_cast<NSSavePanelObj*>(self);
    if (!p->default_name) p->default_name = new std::string();
    *p->default_name = nsstring_to_utf8(name);
}
void nssavepanel_setDirURL(ObjcId self, ObjcSel, ObjcId url) {
    auto* p = reinterpret_cast<NSSavePanelObj*>(self);
    if (!p->directory) p->directory = new std::string();
    ObjcId path_obj = url ? objc_send_name<ObjcId>(url, "path") : nullptr;
    *p->directory = nsstring_to_utf8(path_obj);
}
void nsopenpanel_setCanChooseFiles(ObjcId self, ObjcSel, signed char v) {
    reinterpret_cast<NSSavePanelObj*>(self)->can_choose_files = (v != 0);
}
void nsopenpanel_setCanChooseDirs(ObjcId self, ObjcSel, signed char v) {
    reinterpret_cast<NSSavePanelObj*>(self)->can_choose_dirs = (v != 0);
}
void nsopenpanel_setAllowsMultiple(ObjcId self, ObjcSel, signed char v) {
    reinterpret_cast<NSSavePanelObj*>(self)->allows_multiple = (v != 0);
}
ObjcId nssavepanel_URL(ObjcId self, ObjcSel) {
    auto* p = reinterpret_cast<const NSSavePanelObj*>(self);
    if (!p || !p->selected_path || p->selected_path->empty()) return nullptr;
    return finn_objc_autorelease(nsurl_from_path(*p->selected_path));
}
ObjcId nsopenpanel_URLs(ObjcId self, ObjcSel) {
    ObjcClass* arr_cls = finn_objc_getClass("NSMutableArray");
    ObjcId arr = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(arr_cls), "array");
    if (ObjcId u = nssavepanel_URL(self, nullptr)) {
        objc_send_name<void>(arr, "addObject:", u);
    }
    return finn_objc_autorelease(arr);
}

std::intptr_t nssavepanel_runModal(ObjcId self, ObjcSel) {
    auto* p = reinterpret_cast<NSSavePanelObj*>(self);
    if (!p->selected_path) p->selected_path = new std::string();
    p->selected_path->clear();
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    const QString title = QString::fromStdString(p->title ? *p->title : "Save");
    std::string init_path = p->directory ? *p->directory : "";
    if (p->default_name && !p->default_name->empty()) {
        init_path = (fs::path(init_path) / *p->default_name).string();
    }
    const QString chosen = QFileDialog::getSaveFileName(
        nullptr, title, QString::fromStdString(init_path));
    if (chosen.isEmpty()) return 0;  // NSModalResponseCancel
    *p->selected_path = chosen.toStdString();
    return 1;  // NSModalResponseOK
#else
    std::fprintf(stderr, "finn-darwin: NSSavePanel runModal unavailable without Qt6 backend.\n");
    return 0;
#endif
}

std::intptr_t nsopenpanel_runModal(ObjcId self, ObjcSel) {
    auto* p = reinterpret_cast<NSSavePanelObj*>(self);
    if (!p->selected_path) p->selected_path = new std::string();
    p->selected_path->clear();
#ifdef FINN_DARWIN_QT6
    (void)ensure_qapp();
    const QString title = QString::fromStdString(p->title ? *p->title : "Open");
    const QString dir = QString::fromStdString(p->directory ? *p->directory : "");
    QString chosen;
    if (p->can_choose_dirs && !p->can_choose_files) {
        chosen = QFileDialog::getExistingDirectory(nullptr, title, dir);
    } else {
        chosen = QFileDialog::getOpenFileName(nullptr, title, dir);
    }
    if (chosen.isEmpty()) return 0;
    *p->selected_path = chosen.toStdString();
    return 1;
#else
    std::fprintf(stderr, "finn-darwin: NSOpenPanel runModal unavailable without Qt6 backend.\n");
    return 0;
#endif
}

// --- NSPasteboard -> QClipboard ---
ObjcId nspasteboard_general(ObjcId, ObjcSel) {
    if (!g_general_pasteboard) {
        auto* pb = reinterpret_cast<NSPasteboardObj*>(
            finn_objc_alloc_object(g_cls_nspasteboard, sizeof(NSPasteboardObj)));
        pb->entries = new std::map<std::string, std::string>();
        pb->change_count = 0;
        g_general_pasteboard = reinterpret_cast<ObjcId>(pb);
    }
    return g_general_pasteboard;
}

std::intptr_t nspasteboard_clearContents(ObjcId self, ObjcSel) {
    auto* pb = reinterpret_cast<NSPasteboardObj*>(self);
    if (pb->entries) pb->entries->clear();
    ++pb->change_count;
#ifdef FINN_DARWIN_QT6
    if (QApplication* qa = ensure_qapp()) {
        if (QClipboard* cb = qa->clipboard()) cb->clear();
    }
#endif
    return pb->change_count;
}

signed char nspasteboard_setStringForType(ObjcId self, ObjcSel, ObjcId str, ObjcId type) {
    auto* pb = reinterpret_cast<NSPasteboardObj*>(self);
    if (!pb->entries) pb->entries = new std::map<std::string, std::string>();
    const std::string text = nsstring_to_utf8(str);
    const std::string key = type ? nsstring_to_utf8(type) : "public.utf8-plain-text";
    (*pb->entries)[key] = text;
    ++pb->change_count;
#ifdef FINN_DARWIN_QT6
    if (QApplication* qa = ensure_qapp()) {
        if (QClipboard* cb = qa->clipboard()) cb->setText(QString::fromStdString(text));
    }
#endif
    return 1;
}

ObjcId nspasteboard_stringForType(ObjcId self, ObjcSel, ObjcId type) {
    auto* pb = reinterpret_cast<NSPasteboardObj*>(self);
#ifdef FINN_DARWIN_QT6
    if (QApplication* qa = ensure_qapp()) {
        if (QClipboard* cb = qa->clipboard()) {
            const QString txt = cb->text();
            if (!txt.isEmpty()) return nsstring_from_utf8(txt.toStdString());
        }
    }
#endif
    if (!pb->entries) return nullptr;
    const std::string key = type ? nsstring_to_utf8(type) : "public.utf8-plain-text";
    if (auto it = pb->entries->find(key); it != pb->entries->end()) {
        return nsstring_from_utf8(it->second);
    }
    return nullptr;
}

// --- NSWorkspace -> finn-open ---
ObjcId nsworkspace_shared(ObjcId, ObjcSel) {
    if (!g_shared_workspace) {
        g_shared_workspace = finn_objc_alloc_object(g_cls_nsworkspace);
    }
    return g_shared_workspace;
}

signed char nsworkspace_openURL(ObjcId, ObjcSel, ObjcId url) {
    if (!url) return 0;
    ObjcId str_obj = objc_send_name<ObjcId>(url, "absoluteString");
    const std::string target = nsstring_to_utf8(str_obj);
    if (target.empty()) return 0;
    if (!finn::compat::executable_exists("finn-open")) {
        std::fprintf(stderr, "finn-darwin: NSWorkspace openURL requires finn-open in PATH.\n");
        return 0;
    }
    const pid_t pid = ::fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        char prog[] = "finn-open";
        char* argv[] = {prog, const_cast<char*>(target.c_str()), nullptr};
        ::execvp(prog, argv);
        ::_exit(127);
    }
    return 1;
}

// --- NSImage ---
bool probe_image_size(const std::string& path, DarwinSize* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::uint8_t hdr[24] = {};
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (in.gcount() >= 24 &&
        hdr[0] == 0x89u && hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G' &&
        hdr[12] == 'I' && hdr[13] == 'H' && hdr[14] == 'D' && hdr[15] == 'R') {
        const std::uint32_t w = (static_cast<std::uint32_t>(hdr[16]) << 24) |
                                (static_cast<std::uint32_t>(hdr[17]) << 16) |
                                (static_cast<std::uint32_t>(hdr[18]) << 8) | hdr[19];
        const std::uint32_t h = (static_cast<std::uint32_t>(hdr[20]) << 24) |
                                (static_cast<std::uint32_t>(hdr[21]) << 16) |
                                (static_cast<std::uint32_t>(hdr[22]) << 8) | hdr[23];
        out->width = static_cast<double>(w);
        out->height = static_cast<double>(h);
        return true;
    }
#ifdef FINN_DARWIN_QT6
    QPixmap pm(QString::fromStdString(path));
    if (!pm.isNull()) {
        out->width = static_cast<double>(pm.width());
        out->height = static_cast<double>(pm.height());
        return true;
    }
#endif
    return true;
}

ObjcId nsimage_initWithContentsOfFile(ObjcId self, ObjcSel, ObjcId path_obj) {
    const std::string path = nsstring_to_utf8(path_obj);
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) {
        finn_objc_release(self);
        return nullptr;
    }
    auto* img = reinterpret_cast<NSImageObj*>(self);
    delete img->path;
    img->path = new std::string(path);
    img->size = {0.0, 0.0};
    img->valid = probe_image_size(path, &img->size);
    return self;
}

ObjcId nsimage_initWithSize(ObjcId self, ObjcSel, DarwinSize sz) {
    auto* img = reinterpret_cast<NSImageObj*>(self);
    if (!img->path) img->path = new std::string();
    img->size = sz;
    img->valid = true;
    return self;
}

ObjcId nsimage_imageNamed(ObjcId, ObjcSel, ObjcId name_obj) {
    const std::string name = nsstring_to_utf8(name_obj);
    if (name.empty()) return nullptr;
    ObjcClass* bcls = finn_objc_getClass("NSBundle");
    ObjcId bundle = bcls ? objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(bcls), "mainBundle") : nullptr;
    ObjcId bpath = bundle ? objc_send_name<ObjcId>(bundle, "bundlePath") : nullptr;
    const std::string root = nsstring_to_utf8(bpath);
    std::error_code ec;
    if (!root.empty()) {
        const fs::path res = fs::path(root) / "Contents" / "Resources";
        const std::vector<fs::path> candidates = {
            res / name,
            res / (name + ".png"),
            res / (name + ".icns"),
        };
        for (const auto& c : candidates) {
            if (fs::is_regular_file(c, ec)) {
                ObjcId img = finn_objc_alloc_object(g_cls_nsimage, sizeof(NSImageObj));
                return nsimage_initWithContentsOfFile(img, nullptr, nsstring_from_utf8(c.string()));
            }
        }
    }
    if (fs::is_regular_file(name, ec)) {
        ObjcId img = finn_objc_alloc_object(g_cls_nsimage, sizeof(NSImageObj));
        return nsimage_initWithContentsOfFile(img, nullptr, name_obj);
    }
    return nullptr;
}

DarwinSize nsimage_size(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSImageObj*>(self)->size;
}

void nsimage_setSize(ObjcId self, ObjcSel, DarwinSize sz) {
    reinterpret_cast<NSImageObj*>(self)->size = sz;
}

signed char nsimage_isValid(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSImageObj*>(self)->valid ? 1 : 0;
}

// --- NSTimer -> QTimer ---
ObjcId nstimer_scheduledTimer(ObjcId, ObjcSel, double interval, ObjcId target, ObjcSel sel,
                              ObjcId user_info, signed char repeats) {
    auto* t = reinterpret_cast<NSTimerObj*>(
        finn_objc_alloc_object(g_cls_nstimer, sizeof(NSTimerObj)));
    t->interval = std::max(0.0, interval);
    t->target = target;
    t->selector = sel;
    t->user_info = finn_objc_retain(user_info);
    t->repeats = (repeats != 0);
    t->valid = true;
    ObjcId self = reinterpret_cast<ObjcId>(t);
#ifdef FINN_DARWIN_QT6
    if (ensure_qapp()) {
        auto* qt = new QTimer();
        qt->setSingleShot(!t->repeats);
        const int ms = std::max(1, static_cast<int>(std::lround(t->interval * 1000.0)));
        QObject::connect(qt, &QTimer::timeout, [self]() {
            auto* tm = reinterpret_cast<NSTimerObj*>(self);
            if (!tm->valid) return;
            if (!tm->repeats) tm->valid = false;
            if (tm->target && tm->selector) {
                objc_send<void>(tm->target, tm->selector, self);
            }
        });
        qt->start(ms);
        t->qtimer = qt;
    }
#endif
    return self;
}

void nstimer_invalidate(ObjcId self, ObjcSel) {
    auto* t = reinterpret_cast<NSTimerObj*>(self);
    t->valid = false;
#ifdef FINN_DARWIN_QT6
    if (t->qtimer) {
        t->qtimer->stop();
    }
#endif
}

signed char nstimer_isValid(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSTimerObj*>(self)->valid ? 1 : 0;
}

void nstimer_fire(ObjcId self, ObjcSel) {
    auto* t = reinterpret_cast<NSTimerObj*>(self);
    if (!t->valid) return;
    if (t->target && t->selector) {
        objc_send<void>(t->target, t->selector, self);
    }
    if (!t->repeats) t->valid = false;
}

ObjcId nstimer_userInfo(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSTimerObj*>(self)->user_info;
}

double nstimer_timeInterval(ObjcId self, ObjcSel) {
    return reinterpret_cast<const NSTimerObj*>(self)->interval;
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

    // NSApplication
    g_cls_nsapp = finn_objc_allocateClassPair(nsobj, "NSApplication", sizeof(NSAppObj));
    finn_class_addClassMethod(g_cls_nsapp, finn_sel_registerName("sharedApplication"), to_imp(&nsapp_shared));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("setDelegate:"), to_imp(&nsapp_setDelegate));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("delegate"), to_imp(&nsapp_delegate));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("setActivationPolicy:"), to_imp(&nsapp_setActivationPolicy));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("activateIgnoringOtherApps:"), to_imp(&nsapp_activateIgnoringOtherApps));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("setMainMenu:"), to_imp(&nsapp_setMainMenu));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("mainMenu"), to_imp(&nsapp_mainMenu));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("run"), to_imp(&nsapp_run));
    finn_class_addMethod(g_cls_nsapp, finn_sel_registerName("terminate:"), to_imp(&nsapp_terminate));
    finn_objc_registerClassPair(g_cls_nsapp);

    // NSView
    g_cls_nsview = finn_objc_allocateClassPair(nsobj, "NSView", sizeof(NSViewObj));
    g_cls_nsview->cpp_destructor = &nsview_dtor;
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("initWithFrame:"), to_imp(&nsview_initWithFrame));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("addSubview:"), to_imp(&nsview_addSubview));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("setFrame:"), to_imp(&nsview_setFrame));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("frame"), to_imp(&nsview_frame));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("setHidden:"), to_imp(&nsview_setHidden));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("isHidden"), to_imp(&nsview_isHidden));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("setWantsLayer:"), to_imp(&nsview_setWantsLayer));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("wantsLayer"), to_imp(&nsview_wantsLayer));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("layer"), to_imp(&nsview_layer));
    finn_class_addMethod(g_cls_nsview, finn_sel_registerName("setLayer:"), to_imp(&nsview_setLayer));
    finn_objc_registerClassPair(g_cls_nsview);

    // NSControl
    g_cls_nscontrol = finn_objc_allocateClassPair(g_cls_nsview, "NSControl");
    g_cls_nscontrol->cpp_destructor = &nsview_dtor;
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("setTarget:"), to_imp(&nscontrol_setTarget));
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("target"), to_imp(&nscontrol_target));
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("setAction:"), to_imp(&nscontrol_setAction));
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("action"), to_imp(&nscontrol_action));
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("setEnabled:"), to_imp(&nscontrol_setEnabled));
    finn_class_addMethod(g_cls_nscontrol, finn_sel_registerName("isEnabled"), to_imp(&nscontrol_isEnabled));
    finn_objc_registerClassPair(g_cls_nscontrol);

    // NSButton
    g_cls_nsbutton = finn_objc_allocateClassPair(g_cls_nscontrol, "NSButton");
    g_cls_nsbutton->cpp_destructor = &nsview_dtor;
    finn_class_addClassMethod(g_cls_nsbutton, finn_sel_registerName("buttonWithTitle:target:action:"), to_imp(&nsbutton_buttonWithTitleTargetAction));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("initWithFrame:"), to_imp(&nsbutton_initWithFrame));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("setTitle:"), to_imp(&nsbutton_setTitle));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("title"), to_imp(&nsbutton_title));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("setButtonType:"), to_imp(&nsbutton_setButtonType));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("buttonType"), to_imp(&nsbutton_buttonType));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("setState:"), to_imp(&nsbutton_setState));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("state"), to_imp(&nsbutton_state));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("setBezelStyle:"), to_imp(&nsbutton_setBezelStyle));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("bezelStyle"), to_imp(&nsbutton_bezelStyle));
    finn_class_addMethod(g_cls_nsbutton, finn_sel_registerName("performClick:"), to_imp(&nsbutton_performClick));
    finn_objc_registerClassPair(g_cls_nsbutton);

    // NSTextField
    g_cls_nstextfield = finn_objc_allocateClassPair(g_cls_nscontrol, "NSTextField");
    g_cls_nstextfield->cpp_destructor = &nsview_dtor;
    finn_class_addClassMethod(g_cls_nstextfield, finn_sel_registerName("labelWithString:"), to_imp(&nstextfield_labelWithString));
    finn_class_addClassMethod(g_cls_nstextfield, finn_sel_registerName("textFieldWithString:"), to_imp(&nstextfield_labelWithString));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("initWithFrame:"), to_imp(&nstextfield_initWithFrame));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("setStringValue:"), to_imp(&nstextfield_setStringValue));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("stringValue"), to_imp(&nstextfield_stringValue));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("setEditable:"), to_imp(&nstextfield_setEditable));
    finn_class_addMethod(g_cls_nstextfield, finn_sel_registerName("isEditable"), to_imp(&nstextfield_isEditable));
    finn_objc_registerClassPair(g_cls_nstextfield);

    // NSTextView
    g_cls_nstextview = finn_objc_allocateClassPair(g_cls_nsview, "NSTextView");
    g_cls_nstextview->cpp_destructor = &nsview_dtor;
    finn_class_addMethod(g_cls_nstextview, finn_sel_registerName("initWithFrame:"), to_imp(&nstextview_initWithFrame));
    finn_class_addMethod(g_cls_nstextview, finn_sel_registerName("setString:"), to_imp(&nstextview_setString));
    finn_class_addMethod(g_cls_nstextview, finn_sel_registerName("string"), to_imp(&nstextview_string));
    finn_class_addMethod(g_cls_nstextview, finn_sel_registerName("setEditable:"), to_imp(&nstextview_setEditable));
    finn_class_addMethod(g_cls_nstextview, finn_sel_registerName("isEditable"), to_imp(&nstextfield_isEditable));
    finn_objc_registerClassPair(g_cls_nstextview);

    // NSScrollView
    g_cls_nsscrollview = finn_objc_allocateClassPair(g_cls_nsview, "NSScrollView");
    g_cls_nsscrollview->cpp_destructor = &nsview_dtor;
    finn_class_addMethod(g_cls_nsscrollview, finn_sel_registerName("initWithFrame:"), to_imp(&nsscrollview_initWithFrame));
    finn_class_addMethod(g_cls_nsscrollview, finn_sel_registerName("setDocumentView:"), to_imp(&nsscrollview_setDocumentView));
    finn_class_addMethod(g_cls_nsscrollview, finn_sel_registerName("documentView"), to_imp(&nsscrollview_documentView));
    finn_class_addMethod(g_cls_nsscrollview, finn_sel_registerName("setHasVerticalScroller:"), to_imp(&nsscrollview_setHasVScroller));
    finn_class_addMethod(g_cls_nsscrollview, finn_sel_registerName("hasVerticalScroller"), to_imp(&nsscrollview_hasVScroller));
    finn_class_addMethod(g_cls_nsscrollview, finn_sel_registerName("setHasHorizontalScroller:"), to_imp(&nsscrollview_setHasHScroller));
    finn_objc_registerClassPair(g_cls_nsscrollview);

    // NSSlider
    g_cls_nsslider = finn_objc_allocateClassPair(g_cls_nscontrol, "NSSlider");
    g_cls_nsslider->cpp_destructor = &nsview_dtor;
    finn_class_addClassMethod(g_cls_nsslider, finn_sel_registerName("sliderWithValue:minValue:maxValue:target:action:"), to_imp(&nsslider_factory));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("initWithFrame:"), to_imp(&nsslider_initWithFrame));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("setMinValue:"), to_imp(&nsslider_setMinValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("minValue"), to_imp(&nsslider_minValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("setMaxValue:"), to_imp(&nsslider_setMaxValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("maxValue"), to_imp(&nsslider_maxValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("setDoubleValue:"), to_imp(&nsslider_setDoubleValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("doubleValue"), to_imp(&nsslider_doubleValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("setFloatValue:"), to_imp(&nsslider_setFloatValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("floatValue"), to_imp(&nsslider_floatValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("setIntValue:"), to_imp(&nsslider_setIntValue));
    finn_class_addMethod(g_cls_nsslider, finn_sel_registerName("intValue"), to_imp(&nsslider_intValue));
    finn_objc_registerClassPair(g_cls_nsslider);

    // NSProgressIndicator
    g_cls_nsprogress = finn_objc_allocateClassPair(g_cls_nsview, "NSProgressIndicator");
    g_cls_nsprogress->cpp_destructor = &nsview_dtor;
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("initWithFrame:"), to_imp(&nsprogress_initWithFrame));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("setMinValue:"), to_imp(&nsprogress_setMinValue));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("minValue"), to_imp(&nsslider_minValue));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("setMaxValue:"), to_imp(&nsprogress_setMaxValue));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("maxValue"), to_imp(&nsslider_maxValue));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("setDoubleValue:"), to_imp(&nsprogress_setDoubleValue));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("doubleValue"), to_imp(&nsslider_doubleValue));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("setIndeterminate:"), to_imp(&nsprogress_setIndeterminate));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("isIndeterminate"), to_imp(&nsprogress_isIndeterminate));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("startAnimation:"), to_imp(&nsprogress_startAnimation));
    finn_class_addMethod(g_cls_nsprogress, finn_sel_registerName("stopAnimation:"), to_imp(&nsprogress_stopAnimation));
    finn_objc_registerClassPair(g_cls_nsprogress);

    // NSWindow
    g_cls_nswindow = finn_objc_allocateClassPair(nsobj, "NSWindow", sizeof(NSWindowObj));
    g_cls_nswindow->cpp_destructor = &nswindow_dtor;
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("initWithContentRect:styleMask:backing:defer:"), to_imp(&nswindow_init));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("setTitle:"), to_imp(&nswindow_setTitle));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("title"), to_imp(&nswindow_title));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("contentView"), to_imp(&nswindow_contentView));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("setContentView:"), to_imp(&nswindow_setContentView));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("setMinSize:"), to_imp(&nswindow_setMinSize));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("minSize"), to_imp(&nswindow_minSize));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("setDelegate:"), to_imp(&nswindow_setDelegate));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("delegate"), to_imp(&nswindow_delegate));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("makeKeyAndOrderFront:"), to_imp(&nswindow_makeKeyAndOrderFront));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("orderFront:"), to_imp(&nswindow_makeKeyAndOrderFront));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("orderOut:"), to_imp(&nswindow_orderOut));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("isVisible"), to_imp(&nswindow_isVisible));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("center"), to_imp(&nswindow_center));
    finn_class_addMethod(g_cls_nswindow, finn_sel_registerName("close"), to_imp(&nswindow_close));
    finn_objc_registerClassPair(g_cls_nswindow);

    // NSMenu & NSMenuItem
    g_cls_nsmenu = finn_objc_allocateClassPair(nsobj, "NSMenu", sizeof(NSMenuObj));
    g_cls_nsmenu->cpp_destructor = &nsmenu_dtor;
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("init"), to_imp(&nsmenu_init));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("initWithTitle:"), to_imp(&nsmenu_initWithTitle));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("setTitle:"), to_imp(&nsmenu_setTitle));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("title"), to_imp(&nsmenu_title));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("addItem:"), to_imp(&nsmenu_addItem));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("addItemWithTitle:action:keyEquivalent:"), to_imp(&nsmenu_addItemWithTitleActionKey));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("numberOfItems"), to_imp(&nsmenu_numberOfItems));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("itemAtIndex:"), to_imp(&nsmenu_itemAtIndex));
    finn_class_addMethod(g_cls_nsmenu, finn_sel_registerName("itemArray"), to_imp(&nsmenu_itemArray));
    finn_objc_registerClassPair(g_cls_nsmenu);

    g_cls_nsmenuitem = finn_objc_allocateClassPair(nsobj, "NSMenuItem", sizeof(NSMenuItemObj));
    g_cls_nsmenuitem->cpp_destructor = &nsmenuitem_dtor;
    finn_class_addClassMethod(g_cls_nsmenuitem, finn_sel_registerName("separatorItem"), to_imp(&nsmenuitem_separatorItem));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("init"), to_imp(&nsmenuitem_init));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("initWithTitle:action:keyEquivalent:"), to_imp(&nsmenuitem_initWithTitle));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("setTitle:"), to_imp(&nsmenuitem_setTitle));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("title"), to_imp(&nsmenuitem_title));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("setTarget:"), to_imp(&nsmenuitem_setTarget));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("target"), to_imp(&nsmenuitem_target));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("setAction:"), to_imp(&nsmenuitem_setAction));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("action"), to_imp(&nsmenuitem_action));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("setSubmenu:"), to_imp(&nsmenuitem_setSubmenu));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("submenu"), to_imp(&nsmenuitem_submenu));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("keyEquivalent"), to_imp(&nsmenuitem_keyEquivalent));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("setEnabled:"), to_imp(&nsmenuitem_setEnabled));
    finn_class_addMethod(g_cls_nsmenuitem, finn_sel_registerName("isEnabled"), to_imp(&nsmenuitem_isEnabled));
    finn_objc_registerClassPair(g_cls_nsmenuitem);

    // NSAlert
    g_cls_nsalert = finn_objc_allocateClassPair(nsobj, "NSAlert", sizeof(NSAlertObj));
    g_cls_nsalert->cpp_destructor = &nsalert_dtor;
    finn_class_addMethod(g_cls_nsalert, finn_sel_registerName("setMessageText:"), to_imp(&nsalert_setMessageText));
    finn_class_addMethod(g_cls_nsalert, finn_sel_registerName("setInformativeText:"), to_imp(&nsalert_setInformativeText));
    finn_class_addMethod(g_cls_nsalert, finn_sel_registerName("runModal"), to_imp(&nsalert_runModal));
    finn_objc_registerClassPair(g_cls_nsalert);

    // NSSavePanel & NSOpenPanel
    g_cls_nssavepanel = finn_objc_allocateClassPair(nsobj, "NSSavePanel", sizeof(NSSavePanelObj));
    g_cls_nssavepanel->cpp_destructor = &nssavepanel_dtor;
    finn_class_addClassMethod(g_cls_nssavepanel, finn_sel_registerName("savePanel"), to_imp(&nssavepanel_savePanel));
    finn_class_addMethod(g_cls_nssavepanel, finn_sel_registerName("setTitle:"), to_imp(&nssavepanel_setTitle));
    finn_class_addMethod(g_cls_nssavepanel, finn_sel_registerName("setNameFieldStringValue:"), to_imp(&nssavepanel_setNameField));
    finn_class_addMethod(g_cls_nssavepanel, finn_sel_registerName("setDirectoryURL:"), to_imp(&nssavepanel_setDirURL));
    finn_class_addMethod(g_cls_nssavepanel, finn_sel_registerName("runModal"), to_imp(&nssavepanel_runModal));
    finn_class_addMethod(g_cls_nssavepanel, finn_sel_registerName("URL"), to_imp(&nssavepanel_URL));
    finn_objc_registerClassPair(g_cls_nssavepanel);

    g_cls_nsopenpanel = finn_objc_allocateClassPair(g_cls_nssavepanel, "NSOpenPanel");
    g_cls_nsopenpanel->cpp_destructor = &nssavepanel_dtor;
    finn_class_addClassMethod(g_cls_nsopenpanel, finn_sel_registerName("openPanel"), to_imp(&nsopenpanel_openPanel));
    finn_class_addMethod(g_cls_nsopenpanel, finn_sel_registerName("setCanChooseFiles:"), to_imp(&nsopenpanel_setCanChooseFiles));
    finn_class_addMethod(g_cls_nsopenpanel, finn_sel_registerName("setCanChooseDirectories:"), to_imp(&nsopenpanel_setCanChooseDirs));
    finn_class_addMethod(g_cls_nsopenpanel, finn_sel_registerName("setAllowsMultipleSelection:"), to_imp(&nsopenpanel_setAllowsMultiple));
    finn_class_addMethod(g_cls_nsopenpanel, finn_sel_registerName("runModal"), to_imp(&nsopenpanel_runModal));
    finn_class_addMethod(g_cls_nsopenpanel, finn_sel_registerName("URLs"), to_imp(&nsopenpanel_URLs));
    finn_objc_registerClassPair(g_cls_nsopenpanel);

    // NSPasteboard
    g_cls_nspasteboard = finn_objc_allocateClassPair(nsobj, "NSPasteboard", sizeof(NSPasteboardObj));
    g_cls_nspasteboard->cpp_destructor = &nspasteboard_dtor;
    finn_class_addClassMethod(g_cls_nspasteboard, finn_sel_registerName("generalPasteboard"), to_imp(&nspasteboard_general));
    finn_class_addMethod(g_cls_nspasteboard, finn_sel_registerName("clearContents"), to_imp(&nspasteboard_clearContents));
    finn_class_addMethod(g_cls_nspasteboard, finn_sel_registerName("setString:forType:"), to_imp(&nspasteboard_setStringForType));
    finn_class_addMethod(g_cls_nspasteboard, finn_sel_registerName("stringForType:"), to_imp(&nspasteboard_stringForType));
    finn_objc_registerClassPair(g_cls_nspasteboard);

    // NSWorkspace
    g_cls_nsworkspace = finn_objc_allocateClassPair(nsobj, "NSWorkspace");
    finn_class_addClassMethod(g_cls_nsworkspace, finn_sel_registerName("sharedWorkspace"), to_imp(&nsworkspace_shared));
    finn_class_addMethod(g_cls_nsworkspace, finn_sel_registerName("openURL:"), to_imp(&nsworkspace_openURL));
    finn_objc_registerClassPair(g_cls_nsworkspace);

    // NSColor & NSFont
    g_cls_nscolor = finn_objc_allocateClassPair(nsobj, "NSColor", sizeof(NSColorObj));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("colorWithRed:green:blue:alpha:"), to_imp(&nscolor_rgba));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("whiteColor"), to_imp(&nscolor_white));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("blackColor"), to_imp(&nscolor_black));
    finn_class_addClassMethod(g_cls_nscolor, finn_sel_registerName("clearColor"), to_imp(&nscolor_clear));
    finn_objc_registerClassPair(g_cls_nscolor);

    g_cls_nsfont = finn_objc_allocateClassPair(nsobj, "NSFont", sizeof(NSFontObj));
    g_cls_nsfont->cpp_destructor = &nsfont_dtor;
    finn_class_addClassMethod(g_cls_nsfont, finn_sel_registerName("systemFontOfSize:"), to_imp(&nsfont_system));
    finn_class_addClassMethod(g_cls_nsfont, finn_sel_registerName("boldSystemFontOfSize:"), to_imp(&nsfont_system));
    finn_objc_registerClassPair(g_cls_nsfont);

    // NSImage
    g_cls_nsimage = finn_objc_allocateClassPair(nsobj, "NSImage", sizeof(NSImageObj));
    g_cls_nsimage->cpp_destructor = &nsimage_dtor;
    finn_class_addClassMethod(g_cls_nsimage, finn_sel_registerName("imageNamed:"), to_imp(&nsimage_imageNamed));
    finn_class_addMethod(g_cls_nsimage, finn_sel_registerName("initWithContentsOfFile:"), to_imp(&nsimage_initWithContentsOfFile));
    finn_class_addMethod(g_cls_nsimage, finn_sel_registerName("initWithSize:"), to_imp(&nsimage_initWithSize));
    finn_class_addMethod(g_cls_nsimage, finn_sel_registerName("size"), to_imp(&nsimage_size));
    finn_class_addMethod(g_cls_nsimage, finn_sel_registerName("setSize:"), to_imp(&nsimage_setSize));
    finn_class_addMethod(g_cls_nsimage, finn_sel_registerName("isValid"), to_imp(&nsimage_isValid));
    finn_objc_registerClassPair(g_cls_nsimage);

    // NSTimer
    g_cls_nstimer = finn_objc_allocateClassPair(nsobj, "NSTimer", sizeof(NSTimerObj));
    g_cls_nstimer->cpp_destructor = &nstimer_dtor;
    finn_class_addClassMethod(g_cls_nstimer,
                              finn_sel_registerName("scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:"),
                              to_imp(&nstimer_scheduledTimer));
    finn_class_addMethod(g_cls_nstimer, finn_sel_registerName("invalidate"), to_imp(&nstimer_invalidate));
    finn_class_addMethod(g_cls_nstimer, finn_sel_registerName("isValid"), to_imp(&nstimer_isValid));
    finn_class_addMethod(g_cls_nstimer, finn_sel_registerName("fire"), to_imp(&nstimer_fire));
    finn_class_addMethod(g_cls_nstimer, finn_sel_registerName("userInfo"), to_imp(&nstimer_userInfo));
    finn_class_addMethod(g_cls_nstimer, finn_sel_registerName("timeInterval"), to_imp(&nstimer_timeInterval));
    finn_objc_registerClassPair(g_cls_nstimer);

    g_pasteboard_type_string = nsstring_from_utf8("public.utf8-plain-text");
}

void register_appkit_symbols(std::unordered_map<std::string, void*>* table) {
    (*table)["_NSApplicationMain"] = to_ptr(&shim_NSApplicationMain);
    (*table)["_NSApp"] = &g_shared_app;
    (*table)["_NSPasteboardTypeString"] = &g_pasteboard_type_string;
}

}  // namespace finn::darwin
