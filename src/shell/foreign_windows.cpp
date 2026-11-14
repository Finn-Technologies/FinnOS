// Implements ForeignWindowsManager using wlr-foreign-toplevel-management-v1.

#include "shell/foreign_windows.hpp"

#include <QGuiApplication>
#include <QTimer>
#include <qguiapplication_platform.h>
#include <wayland-client.h>

#include <algorithm>
#include <cstring>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

namespace finn::shell {

struct ForeignWindowsManager::Entry {
    ForeignWindowsManager* owner = nullptr;
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    ForeignWindowInfo pending;
    ForeignWindowInfo committed;
};

namespace {

void on_title(void* data, zwlr_foreign_toplevel_handle_v1*, const char* title) {
    auto* e = static_cast<ForeignWindowsManager::Entry*>(data);
    e->pending.title = title ? QString::fromUtf8(title) : QString();
}

void on_app_id(void* data, zwlr_foreign_toplevel_handle_v1*, const char* app_id) {
    auto* e = static_cast<ForeignWindowsManager::Entry*>(data);
    e->pending.app_id = app_id ? QString::fromUtf8(app_id) : QString();
}

void on_output_enter(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}
void on_output_leave(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}

void on_state(void* data, zwlr_foreign_toplevel_handle_v1*, wl_array* state) {
    auto* e = static_cast<ForeignWindowsManager::Entry*>(data);
    e->pending.maximized = false;
    e->pending.minimized = false;
    e->pending.activated = false;
    e->pending.fullscreen = false;
    if (!state || !state->data) return;
    const auto* vals = static_cast<const std::uint32_t*>(state->data);
    const std::size_t n = state->size / sizeof(std::uint32_t);
    for (std::size_t i = 0; i < n; ++i) {
        switch (vals[i]) {
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
                e->pending.maximized = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
                e->pending.minimized = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
                e->pending.activated = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
                e->pending.fullscreen = true;
                break;
            default:
                break;
        }
    }
}

void on_done(void* data, zwlr_foreign_toplevel_handle_v1*) {
    auto* e = static_cast<ForeignWindowsManager::Entry*>(data);
    e->committed = e->pending;
    e->committed.id = reinterpret_cast<quintptr>(e->handle);
    if (e->owner) e->owner->handleDone();
}

void on_closed(void* data, zwlr_foreign_toplevel_handle_v1*) {
    auto* e = static_cast<ForeignWindowsManager::Entry*>(data);
    if (e->owner) e->owner->handleClosed(e);
}

void on_parent(void*, zwlr_foreign_toplevel_handle_v1*,
               zwlr_foreign_toplevel_handle_v1*) {}

const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {
    on_title,
    on_app_id,
    on_output_enter,
    on_output_leave,
    on_state,
    on_done,
    on_closed,
    on_parent,
};

}  // namespace

ForeignWindowsManager::ForeignWindowsManager(QObject* parent) : QObject(parent) {
    QTimer::singleShot(0, this, &ForeignWindowsManager::bindWayland);
}

ForeignWindowsManager::~ForeignWindowsManager() {
    for (Entry* e : entries_) {
        if (e->handle) zwlr_foreign_toplevel_handle_v1_destroy(e->handle);
        delete e;
    }
    entries_.clear();
    if (manager_) {
        zwlr_foreign_toplevel_manager_v1_stop(manager_);
        zwlr_foreign_toplevel_manager_v1_destroy(manager_);
        manager_ = nullptr;
    }
    if (registry_) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
}

void ForeignWindowsManager::bindWayland() {
    if (!QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                  Qt::CaseInsensitive)) {
        return;
    }
    auto* native =
        QGuiApplication::instance()
            ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()
            : nullptr;
    if (!native) return;
    wl_display* dpy = native->display();
    seat_ = native->seat();
    if (!dpy) return;

    static const wl_registry_listener kRegListener = {
        &ForeignWindowsManager::onGlobal,
        &ForeignWindowsManager::onGlobalRemove,
    };
    registry_ = wl_display_get_registry(dpy);
    if (!registry_) return;
    wl_registry_add_listener(registry_, &kRegListener, this);
    wl_display_roundtrip(dpy);
}

void ForeignWindowsManager::onGlobal(void* data, wl_registry* reg,
                                     std::uint32_t name, const char* interface,
                                     std::uint32_t version) {
    auto* self = static_cast<ForeignWindowsManager*>(data);
    if (std::strcmp(interface,
                    zwlr_foreign_toplevel_manager_v1_interface.name) == 0 &&
        !self->manager_) {
        const std::uint32_t bind_ver = std::min<std::uint32_t>(version, 3);
        self->manager_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
            wl_registry_bind(reg, name,
                             &zwlr_foreign_toplevel_manager_v1_interface,
                             bind_ver));
        static const zwlr_foreign_toplevel_manager_v1_listener kMgrListener = {
            &ForeignWindowsManager::onToplevel,
            &ForeignWindowsManager::onManagerFinished,
        };
        zwlr_foreign_toplevel_manager_v1_add_listener(self->manager_,
                                                      &kMgrListener, self);
    }
}

void ForeignWindowsManager::onGlobalRemove(void*, wl_registry*, std::uint32_t) {}

void ForeignWindowsManager::onToplevel(void* data,
                                       zwlr_foreign_toplevel_manager_v1*,
                                       zwlr_foreign_toplevel_handle_v1* handle) {
    auto* self = static_cast<ForeignWindowsManager*>(data);
    auto* e = new Entry();
    e->owner = self;
    e->handle = handle;
    e->pending.id = reinterpret_cast<quintptr>(handle);
    e->committed.id = e->pending.id;
    self->entries_.push_back(e);
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, e);
}

void ForeignWindowsManager::onManagerFinished(void* data,
                                              zwlr_foreign_toplevel_manager_v1*) {
    auto* self = static_cast<ForeignWindowsManager*>(data);
    self->manager_ = nullptr;
}

void ForeignWindowsManager::handleDone() { rebuildPublicList(); }

void ForeignWindowsManager::handleClosed(Entry* entry) {
    entries_.removeAll(entry);
    if (entry->handle) {
        zwlr_foreign_toplevel_handle_v1_destroy(entry->handle);
    }
    delete entry;
    rebuildPublicList();
}

void ForeignWindowsManager::rebuildPublicList() {
    windows_.clear();
    for (const Entry* e : entries_) {
        windows_.push_back(e->committed);
    }
    emit windowsChanged();
}

QVector<ForeignWindowInfo> ForeignWindowsManager::windowsForApp(
    const QString& app_id) const {
    QVector<ForeignWindowInfo> out;
    const QString target = app_id.toLower();
    for (const auto& w : windows_) {
        const QString wid = w.app_id.toLower();
        if (wid == target || wid == (QStringLiteral("finn-") + target) ||
            wid.endsWith(QLatin1Char('.') + target)) {
            out.push_back(w);
        }
    }
    return out;
}

bool ForeignWindowsManager::hasRunningApp(const QString& app_id) const {
    return !windowsForApp(app_id).isEmpty();
}

void ForeignWindowsManager::activateWindow(quintptr id) {
    if (!seat_) {
        if (auto* native =
                qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
            seat_ = native->seat();
        }
    }
    for (Entry* e : entries_) {
        if (reinterpret_cast<quintptr>(e->handle) == id && seat_) {
            if (e->committed.minimized) {
                zwlr_foreign_toplevel_handle_v1_unset_minimized(e->handle);
            }
            zwlr_foreign_toplevel_handle_v1_activate(e->handle, seat_);
            break;
        }
    }
}

void ForeignWindowsManager::setMinimized(quintptr id, bool minimized) {
    for (Entry* e : entries_) {
        if (reinterpret_cast<quintptr>(e->handle) == id) {
            if (minimized) {
                zwlr_foreign_toplevel_handle_v1_set_minimized(e->handle);
            } else {
                zwlr_foreign_toplevel_handle_v1_unset_minimized(e->handle);
            }
            break;
        }
    }
}

void ForeignWindowsManager::toggleMinimize(quintptr id) {
    for (Entry* e : entries_) {
        if (reinterpret_cast<quintptr>(e->handle) == id) {
            if (e->committed.minimized || !e->committed.activated) {
                activateWindow(id);
            } else {
                zwlr_foreign_toplevel_handle_v1_set_minimized(e->handle);
            }
            break;
        }
    }
}

void ForeignWindowsManager::setMaximized(quintptr id, bool maximized) {
    for (Entry* e : entries_) {
        if (reinterpret_cast<quintptr>(e->handle) == id) {
            if (maximized) {
                zwlr_foreign_toplevel_handle_v1_set_maximized(e->handle);
            } else {
                zwlr_foreign_toplevel_handle_v1_unset_maximized(e->handle);
            }
            break;
        }
    }
}

void ForeignWindowsManager::closeWindow(quintptr id) {
    for (Entry* e : entries_) {
        if (reinterpret_cast<quintptr>(e->handle) == id) {
            zwlr_foreign_toplevel_handle_v1_close(e->handle);
            break;
        }
    }
}

}  // namespace finn::shell
